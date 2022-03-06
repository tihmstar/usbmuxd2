// jkcoxson

#include <assert.h>
#include <libgeneral/macros.h>
#include <plist/plist.h>
#include <string.h>

#include "../Muxer.hpp"
#include "../sysconf/sysconf.hpp"
#include "SockConn.hpp"
#include "ManualDevice.hpp"

ManualDevice::ManualDevice(std::string uuid, std::string ipaddr, std::string serviceName, std::shared_ptr<gref_Muxer> mux)
    : Device(mux, Device::MUXCONN_WIFI), _ipaddr(ipaddr), _serviceName(serviceName), _hbclient(NULL), _hbrsp(NULL), _idev(NULL) {
    strncpy(_serial, uuid.c_str(), sizeof(_serial));
}

ManualDevice::~ManualDevice() {
    safeFreeCustom(_hbclient, heartbeat_client_free);
    safeFreeCustom(_hbrsp, plist_free);
    safeFreeCustom(_idev, idevice_free);
}

void ManualDevice::loopEvent() {
    plist_t hbeat = NULL;
    cleanup([&] {
        safeFreeCustom(hbeat, plist_free);
    });
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;

    retassure((hret = heartbeat_receive_with_timeout(_hbclient, &hbeat, 15000)) == HEARTBEAT_E_SUCCESS, "[ManualDevice] failed to recv heartbeat with error=%d", hret);
    retassure((hret = heartbeat_send(_hbclient, _hbrsp)) == HEARTBEAT_E_SUCCESS, "[ManualDevice] failed to send heartbeat");
}

void ManualDevice::beforeLoop() {
    retassure(_hbclient, "Not starting loop, because we don't have a _hbclient");
}

void ManualDevice::kill() noexcept {
    stopLoop();
    (*_mux)->delete_device(_selfref.lock());
}

void ManualDevice::startLoop() {
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;
    _loopState = tihmstar::LOOP_STOPPED;

    assure(_hbrsp = plist_new_dict());
    plist_dict_set_item(_hbrsp, "Command", plist_new_string("Polo"));

    assure(!idevice_new_with_options(&_idev, _serial, IDEVICE_LOOKUP_NETWORK));

    retassure((hret = heartbeat_client_start_service(_idev, &_hbclient, "usbmuxd2")) == HEARTBEAT_E_SUCCESS, "[ManualDevice] Failed to start heartbeat service with error=%d", hret);

    _loopState = tihmstar::LOOP_UNINITIALISED;
    Manager::startLoop();
}

void ManualDevice::start_connect(uint16_t dport, std::shared_ptr<Client> cli) {
    SockConn *conn = nullptr;
    cleanup([&] {
        if (conn) {
            conn->kill();
        }
    });

    conn = new SockConn(_ipaddr, dport, cli);
    conn->connect();
    conn = nullptr;  // let SockConn float and manage itself
}
