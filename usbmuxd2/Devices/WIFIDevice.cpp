//
//  WIFIDevice.cpp
//  usbmuxd2
//
//  Created by tihmstar on 21.06.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "WIFIDevice.hpp"
//#include "SockConn.hpp"
#include "../Muxer.hpp"
#include "../sysconf/sysconf.hpp"

#include <libgeneral/macros.h>
#include <plist/plist.h>

#include <assert.h>
#include <string.h>

WIFIDevice::WIFIDevice(Muxer *mux, std::string uuid, std::vector<std::string> ipaddr, std::string serviceName)
: Device(mux,Device::MUXCONN_WIFI), _ipaddr(ipaddr), _serviceName(serviceName), _hbclient(NULL), _hbrsp(NULL),
    _idev(NULL)
{
    strncpy(_serial, uuid.c_str(), sizeof(_serial));
}

WIFIDevice::~WIFIDevice() {
#ifdef HAVE_LIBIMOBILEDEVICE
    safeFreeCustom(_hbclient, heartbeat_client_free);
    safeFreeCustom(_idev, idevice_free);
#endif //HAVE_LIBIMOBILEDEVICE
    safeFreeCustom(_hbrsp, plist_free);
}

bool WIFIDevice::loopEvent(){
#ifndef HAVE_LIBIMOBILEDEVICE
    reterror("Compiled without libimobiledevice");
#else
    plist_t hbeat = NULL;
    cleanup([&]{
        safeFreeCustom(hbeat, plist_free);
    });
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;

    retassure((hret = heartbeat_receive_with_timeout(_hbclient,&hbeat,15000)) == HEARTBEAT_E_SUCCESS, "[WIFIDevice] failed to recv heartbeat with error=%d",hret);
    retassure((hret = heartbeat_send(_hbclient,_hbrsp)) == HEARTBEAT_E_SUCCESS,"[WIFIDevice] failed to send heartbeat");
    return true;
#endif //HAVE_LIBIMOBILEDEVICE
}

void WIFIDevice::beforeLoop(){
    retassure(_hbclient, "Not starting loop, because we don't have a _hbclient");
}

void WIFIDevice::kill() noexcept{
    stopLoop();
    _mux->delete_device(_selfref.lock());
}

void WIFIDevice::startLoop(){
#ifndef HAVE_LIBIMOBILEDEVICE
    reterror("Compiled without libimobiledevice");
#else
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;
    _loopState = tihmstar::LOOP_STOPPED;
    
    assure(_hbrsp = plist_new_dict());
    plist_dict_set_item(_hbrsp, "Command", plist_new_string("Polo"));
    
    assure(!idevice_new_with_options(&_idev,_serial, IDEVICE_LOOKUP_NETWORK));

    retassure((hret = heartbeat_client_start_service(_idev, &_hbclient, "usbmuxd2")) == HEARTBEAT_E_SUCCESS,"[WIFIDevice] Failed to start heartbeat service with error=%d",hret);

    _loopState = tihmstar::LOOP_UNINITIALISED;
    Manager::startLoop();
#endif //HAVE_LIBIMOBILEDEVICE
}


void WIFIDevice::start_connect(uint16_t dport, std::shared_ptr<Client> cli){
    reterror("TODO");
//    SockConn *conn = nullptr;
//    cleanup([&]{
//        if (conn){
//            conn->kill();
//        }
//    });
//
//    conn = new SockConn(_ipaddr,dport,cli);
//    conn->connect();
//    conn = nullptr; //let SockConn float and manage itself
}
