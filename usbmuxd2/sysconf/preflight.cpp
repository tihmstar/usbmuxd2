//
//  preflight.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "preflight.hpp"

#include <libgeneral/macros.h>
#include "sysconf.hpp"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <future>
#include <plist/plist.h>
#include <system_error>


#ifdef HAVE_LIBIMOBILEDEVICE
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/notification_proxy.h>

struct idevice_private {
    char *udid;
    uint32_t mux_id;
    idevice_connection_type conn_type;
    void *conn_data;
    int version;
};

struct np_cb_data{
    idevice_t dev;
    np_client_t np;
};

static void lockdownd_set_untrusted_host_buid(lockdownd_client_t lockdown){
    std::string system_buid = sysconf_get_system_buid();
    debug("%s: Setting UntrustedHostBUID to %s", __func__, system_buid.c_str());
    assure(!lockdownd_set_value(lockdown, NULL, "UntrustedHostBUID", plist_new_string(system_buid.c_str())));
}

static void pairing_callback(const char* notification, void* userdata) noexcept{
    int err = 0;
    np_cb_data *cb_data = (np_cb_data*)userdata;
    lockdownd_error_t lret = LOCKDOWN_E_SUCCESS;
    lockdownd_client_t lockdown = NULL;
    idevice_t dev = cb_data->dev; //just a copy for easier access

    cretassure(strlen(notification), "Failed to receive pairing_callback");

    cretassure(!(lret = lockdownd_client_new(dev, &lockdown, "usbmuxd")),"%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, dev->udid, lret);
    if (strcmp(notification, "com.apple.mobile.lockdown.request_pair") == 0) {
        debug("%s: user trusted this computer on device %s, pairing now", __func__, dev->udid);
        cretassure(!(lret = lockdownd_pair(lockdown, NULL)), "%s: ERROR: Pair failed for device %s, lockdown error %d", __func__, dev->udid, lret);
        info("Device %s is now paired", dev->udid);
    } else if (strcmp(notification, "com.apple.mobile.lockdown.request_host_buid") == 0) {
        lockdownd_set_untrusted_host_buid(lockdown);
    }
error:
    if (lockdown)
        lockdownd_client_free(lockdown);
    if (cb_data) {
        std::thread delthread([](np_cb_data *cb_data){
            debug("deleing pairing_callback cb_data(%p)",cb_data);
            if (cb_data->np){ //this needs to be set!
                np_set_notify_callback(cb_data->np, NULL, NULL); //join thread and make sure no more callbacks!
                np_client_free(cb_data->np);
            }
            if (cb_data->dev) {
                idevice_free(cb_data->dev);
            }
            safeFree(cb_data);
        },cb_data);
        delthread.detach();
    }
}

void preflight_device(const char *serial, int id){
    int version_major = 0;
    lockdownd_error_t lret = LOCKDOWN_E_SUCCESS;
    idevice_error_t iret = IDEVICE_E_SUCCESS;
    idevice_t dev = nullptr;
    lockdownd_client_t lockdown = NULL;
    char *lockdowntype = NULL;
    plist_t p_pairingRecord = NULL;
    plist_t pProdVers = NULL;
    char *version_str = NULL;
    lockdownd_service_descriptor_t service = NULL;
    np_client_t np = NULL;
    np_cb_data *cb_data = NULL;
    np_error_t npret = NP_E_SUCCESS;
    cleanup([&]{
        if (pProdVers) {
            plist_free(pProdVers);
        }
        if (dev)
            idevice_free(dev);
        if (lockdown)
            lockdownd_client_free(lockdown);
        safeFree(lockdowntype);
        safeFree(version_str);
        safeFreeCustom(p_pairingRecord, plist_free);
        if (service) {
            lockdownd_service_descriptor_free(service);
        }
        if (np) {
            np_client_free(np);
        }
        if (cb_data) {
            if (cb_data->dev) {
                idevice_free(cb_data->dev);
            }
            if (cb_data->np) {
                np_client_free(cb_data->np);
            }
            safeFree(cb_data);
        }
    });

    info("preflighting device %s",serial);

    retassure(!(iret = idevice_new_with_options(&dev,serial,IDEVICE_LOOKUP_USBMUX)), "failed to create device with iret=%d",iret);

    retassure(!(lret = lockdownd_client_new(dev, &lockdown, "usbmuxd2")),"%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, serial, lret);

    retassure(!(lret = lockdownd_query_type(lockdown, &lockdowntype)),"%s: ERROR: Could not get lockdownd type from device %s, lockdown error %d", __func__, serial, lret);

    if (strcmp(lockdowntype, "com.apple.mobile.lockdown") != 0){
        //this is a restore mode device
        info("%s: Finished preflight on device int restore mode %s", __func__, serial);
        return;
    }

    try {
        p_pairingRecord = sysconf_get_device_record(serial);
    } catch (tihmstar::exception &e) {
        info("No pairing record loaded for device %s",serial);
        goto pairing_required;
    }

    {
        char *hostid_str = NULL;
        cleanup([&]{
            safeFree(hostid_str);
        });
        plist_t p_hostid = NULL;

        retassure(p_hostid = plist_dict_get_item(p_pairingRecord, "HostID"), "Failed to get HostID from pairing record");
        retassure((plist_get_string_val(p_hostid, &hostid_str),hostid_str), "Failed to get str ptr from HostID");

        if (!(lret = lockdownd_start_session(lockdown, hostid_str, NULL, NULL))){
            info("%s: Finished preflight on device %s", __func__, serial);
            return;
        }
    }

    error("%s: StartSession failed on device %s, lockdown error %d", __func__, serial, lret);

    if (lret == LOCKDOWN_E_SSL_ERROR) {
        error("%s: The stored pair record for device %s is invalid. Removing.", __func__, serial);
        sysconf_remove_device_record(serial);
    }

pairing_required:

    assure(!(lret = lockdownd_get_value(lockdown, NULL, "ProductVersion", &pProdVers)));
    assure(pProdVers && plist_get_node_type(pProdVers) == PLIST_STRING);

    plist_get_string_val(pProdVers, &version_str);
    retassure(version_str, "%s: Could not get ProductVersion string from device %s id %d", __func__, serial, id);

    version_major = (int)strtol(version_str, NULL, 10);

    info("%s: Found ProductVersion %s device %s", __func__, version_str, serial);

    lockdownd_set_untrusted_host_buid(lockdown);
    if ((lret = lockdownd_pair(lockdown, NULL)) == LOCKDOWN_E_SUCCESS) {
        info("%s: Pair success for device %s", __func__, serial);
        info("%s: Finished preflight on device %s", __func__, serial);
        return;
    } else if (lret == LOCKDOWN_E_PASSWORD_PROTECTED) {
        //iOS 6 mode
        info("%s: Device %s is locked with a passcode. Cannot pair.", __func__, serial);
        return;
    }


    //if we didn't instantly pair, then we assume the pairing dialog is pending!
    //otherwise something unexpected happened

    //this works starting with iOS 7, otherwise we're done pairing anyways
    switch (lret) {
        case LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING:
            break;
        case LOCKDOWN_E_RECEIVE_TIMEOUT:
            reterror("%s: Device %s in unexpected pair state LOCKDOWN_E_RECEIVE_TIMEOUT'", __func__, serial,lret);
        case LOCKDOWN_E_USER_DENIED_PAIRING:
            reterror("%s: Device %s in unexpected pair state LOCKDOWN_E_USER_DENIED_PAIRING'", __func__, serial,lret);
            
        default:
            reterror("%s: Device %s in unexpected pair state %d", __func__, serial,lret);
    }

    retassure((lret = lockdownd_start_service(lockdown, "com.apple.mobile.insecure_notification_proxy", &service)) == LOCKDOWN_E_SUCCESS, "%s: ERROR: Could not start insecure_notification_proxy on %s, lockdown error %d", __func__, serial, lret);

    assure(!(npret = np_client_new(dev, service, &np)));

    assure(cb_data = (np_cb_data*)malloc(sizeof(np_cb_data)));
    cb_data->dev = dev;dev = NULL; //transfer ownership to cb_data
    cb_data->np = np;np = NULL; //transfer ownership to cb_data

    static const char* spec[] = {
        "com.apple.mobile.lockdown.request_pair",
        "com.apple.mobile.lockdown.request_host_buid",
        NULL
    };

    assure(!(npret = np_observe_notifications(cb_data->np, (const char **)spec)));

    assure(!(npret = np_set_notify_callback(cb_data->np, pairing_callback, cb_data)));

    info("%s: Waiting for user to trust this computer on device %s", __func__, serial);
    cb_data = NULL; //cb_data ownership transfered to pairing_callback
    return;
}
#endif
