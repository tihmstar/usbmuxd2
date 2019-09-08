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
#include <libgeneral/macros.h>
#include <SockConn.hpp>
#include <string.h>
#include <Muxer.hpp>
#include <plist/plist.h>

WIFIDevice::WIFIDevice(std::string uuid, std::string ipaddr, std::string serviceName, Muxer *mux) 
: Device(mux,Device::MUXCONN_WIFI), _ipaddr(ipaddr), _serviceName(serviceName), _hbclient(NULL), _hbeat(NULL), _hbrsp(NULL),
	_idev(NULL)
{
	strncpy(_serial, uuid.c_str(), sizeof(_serial));
}

WIFIDevice::~WIFIDevice() {
    _muxer->delete_device(this);
	stopLoop();
	if (_hbclient){
		heartbeat_client_free(_hbclient);
    }
    if (_hbeat){
		plist_free(_hbeat);
    }
    if (_hbrsp){
		plist_free(_hbrsp);
    }
    if (_idev){
    	idevice_free(_idev);
    }
}

void WIFIDevice::loopEvent(){
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;
	retassure((hret = heartbeat_receive_with_timeout(_hbclient,&_hbeat,15000)) == HEARTBEAT_E_SUCCESS, "failed to recv heartbeat");
    if (_hbeat){
       plist_free(_hbeat);_hbeat=NULL;
    }
    retassure((hret = heartbeat_send(_hbclient,_hbrsp)) == HEARTBEAT_E_SUCCESS,"failed to send heartbeat");
}

void WIFIDevice::afterLoop() noexcept{
	kill();
}

void WIFIDevice::startLoop(){
    plist_t polo = NULL;
    cleanup([&]{
	    if (polo){
			plist_free(polo);
    	}

    });
    heartbeat_error_t hret = HEARTBEAT_E_SUCCESS;
    _loopState = LOOP_STOPPED;
    
    assure(polo = plist_new_string("Polo"));
    assure(_hbrsp = plist_new_dict());
    plist_dict_set_item(_hbrsp, "Command", polo);polo = NULL;

    assure(!idevice_new(&_idev,_serial));

    retassure((hret = heartbeat_client_start_service(_idev, &_hbclient, "usbmuxd2")) == HEARTBEAT_E_SUCCESS,"Failed to start heartbeat service with error=%d",hret);

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
