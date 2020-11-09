//
//  WIFIDevice.cpp
//  usbmuxd2
//
//  Created by tihmstar on 21.06.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include <Devices/WIFIDevice.hpp>
#include <SockConn.hpp>
#include <assert.h>
#include <log.h>
#include <libgeneral/macros.h>
#include <SockConn.hpp>
#include <string.h>
#include <Muxer.hpp>
#include <plist/plist.h>
#include <sysconf/sysconf.hpp>

WIFIDevice::WIFIDevice(std::string uuid, std::string ipaddr, std::string serviceName, Muxer *mux) 
: Device(mux,Device::MUXCONN_WIFI), _ipaddr(ipaddr), _serviceName(serviceName), _hbclient(NULL), _hbrsp(NULL),
	_idev(NULL)
{
	strncpy(_serial, uuid.c_str(), sizeof(_serial));
}

WIFIDevice::~WIFIDevice() {
    stopLoop();
    _muxer->delete_device(this);
    safeFreeCustom(_hbclient, heartbeat_client_free);
    safeFreeCustom(_hbrsp, plist_free);
    safeFreeCustom(_idev, idevice_free);
}

void WIFIDevice::loopEvent(){
    plist_t hbeat = NULL;
    cleanup([&]{
        safeFreeCustom(hbeat, plist_free);
    });
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;

	retassure((hret = heartbeat_receive_with_timeout(_hbclient,&hbeat,15000)) == HEARTBEAT_E_SUCCESS, "[WIFIDevice] failed to recv heartbeat with error=%d",hret);
    retassure((hret = heartbeat_send(_hbclient,_hbrsp)) == HEARTBEAT_E_SUCCESS,"[WIFIDevice] failed to send heartbeat");
}

void WIFIDevice::beforeLoop(){
    retassure(_hbclient, "Not starting loop, because we don't have a _hbclient");
}

void WIFIDevice::afterLoop() noexcept{
	kill();
}

void WIFIDevice::startLoop(){
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;
    _loopState = LOOP_STOPPED;
    
    assure(_hbrsp = plist_new_dict());
    plist_dict_set_item(_hbrsp, "Command", plist_new_string("Polo"));
    
    assure(!idevice_new_with_options(&_idev,_serial, IDEVICE_LOOKUP_NETWORK));

    retassure((hret = heartbeat_client_start_service(_idev, &_hbclient, "usbmuxd2")) == HEARTBEAT_E_SUCCESS,"[WIFIDevice] Failed to start heartbeat service with error=%d",hret);

	_loopState = LOOP_UNINITIALISED;
    Manager::startLoop();
}


void WIFIDevice::start_connect(uint16_t dport, Client *cli){
	SockConn *conn = nullptr;
	cleanup([&]{
		if (conn){
			conn->kill();
		}
	});

	conn = new SockConn(_ipaddr,dport,cli);
	conn->connect();
	conn = nullptr; //let SockConn float and manage itself
}
