//
//  preflight.cpp
//  usbmuxd2
//
//  Created by tihmstar on 11.12.20.
//

#include "preflight.hpp"
#include <libgeneral/macros.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/notification_proxy.h>


void preflight_device(const char *serial, int id){
    idevice_t dev = NULL;
    lockdownd_client_t lockdown = NULL;
    cleanup([&]{
        safeFreeCustom(lockdown, lockdownd_client_free);
        safeFreeCustom(dev, idevice_free);
    });
    idevice_error_t iret = IDEVICE_E_SUCCESS;
    lockdownd_error_t lret = LOCKDOWN_E_SUCCESS;

    info("preflighting device %s",serial);

    retassure(!(iret = idevice_new_with_options(&dev,serial,IDEVICE_LOOKUP_USBMUX)), "failed to create device with iret=%d",iret);
    
    retassure(!(lret = lockdownd_client_new(dev, &lockdown, "usbmuxd2")),"%s: ERROR: Could not connect to lockdownd on device %s, lockdown error %d", __func__, serial, lret);
    
    /* Check if device is in normal mode */
    {
        char *str_lockdowntype = NULL;
        cleanup([&]{
            safeFree(str_lockdowntype);
        });
        retassure(!(lret = lockdownd_query_type(lockdown, &str_lockdowntype)),"%s: ERROR: Could not get lockdownd type from device %s, lockdown error %d", __func__, serial, lret);

        if (strcmp(str_lockdowntype, "com.apple.mobile.lockdown") != 0){
            //this is a restore mode device
            info("%s: Finished preflight on device int restore mode %s", __func__, serial);
            return;
        }
    }

    
    
    reterror("todo implement");
}
