//
//  Muxer.cpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "Muxer.hpp"
#include <log.h>
#include <libgeneral/macros.h>
#include <Device.hpp>
#include <Devices/USBDevice.hpp>
#include <Devices/WIFIDevice.hpp>
#include <algorithm>
#include <Manager/DeviceManager/USBDeviceManager.hpp>
#include <Manager/ClientManager.hpp>
#include <Client.hpp>
#include <limits.h>
#include <sysconf/preflight.hpp>
#include <sysconf/sysconf.hpp>
#include <string.h>
#include <arpa/inet.h>

#ifdef HAVE_WIFI_SUPPORT
#include <Manager/DeviceManager/WIFIDeviceManager.hpp>
#endif

#define MAXID (INT_MAX/2)

Muxer::Muxer()
    : _climgr(NULL), _usbdevmgr(NULL),_wifidevmgr(NULL), _newid(1), _isDying(false)
{
    //
}

Muxer::~Muxer(){
    debug("[Muxer] destroing muxer");
    _isDying = true;
    
    debug("[Muxer] deleting clients");
    while (_clients._elems.size()) {
        Client *cli = nullptr;
        _clients.addMember();
        if (_clients._elems.size()) {
            cli = *_clients._elems.begin();
        }
        _clients.delMember();
        if (cli) {
            delete_client(cli);
        }
    }
    
    debug("[Muxer] deleting devices");
    while (_devices._elems.size()) {
        Device *dev = nullptr;
        _devices.addMember();
        if (_devices._elems.size()) {
            dev = *_devices._elems.begin();
        }
        _devices.delMember();
        if (dev) {
            delete_device(dev);
        }
    }
    
    if (_climgr) {
        delete _climgr;
    }
    if (_usbdevmgr) {
        delete _usbdevmgr;
    }
#ifdef HAVE_WIFI_SUPPORT
    if (_wifidevmgr) {
        delete _wifidevmgr;
    }
#endif
}

void Muxer::spawnClientManager(){
    assure(!_isDying);
    assert(!_climgr);
    _climgr = new ClientManager(this);
    _climgr->startLoop();
}

void Muxer::spawnUSBDeviceManager(){
    assure(!_isDying);
    assert(!_usbdevmgr);
    _usbdevmgr = new USBDeviceManager(this);
    _usbdevmgr->startLoop();
}

void Muxer::spawnWIFIDeviceManager(){
#ifndef HAVE_WIFI_SUPPORT
    reterror("compiled without wifi support");
#else
    assure(!_isDying);
    assert(!_wifidevmgr);
    _wifidevmgr = new WIFIDeviceManager(this);
    _wifidevmgr->startLoop();
#endif
}

bool Muxer::hasDeviceManager() noexcept{
    return _wifidevmgr != NULL || _usbdevmgr != NULL;
}


void Muxer::add_client(Client *cli){
    assure(!_isDying);
    debug("add_client %p",cli);
    _clients.lockMember();
    _clients._elems.push_back(cli);
    try{
        cli->startLoop();
    }catch(tihmstar::exception &e){
        _clients.unlockMember();
        throw;
    }
    _clients.unlockMember();
}

void Muxer::delete_client(Client *cli){
    debug("delete_client %p",cli);
    _clients.lockMember();
    const auto target = std::remove(_clients._elems.begin(), _clients._elems.end(), cli);
    if (target != _clients._elems.end()){
        _clients._elems.erase(target, _clients._elems.end());
        cli->kill();
    }
    _clients.unlockMember();
}

void Muxer::add_device(Device *dev) noexcept{
    if(_isDying){
        dev->kill();
        return;
    }
    debug("add_device %p",dev);

    //get id of already connected device but with the other connection type
    //discard the id-based connection type information
    dev->_id = id_for_device(dev->_serial, dev->_conntype == Device::MUXCONN_USB ? Device::MUXCONN_WIFI : Device::MUXCONN_USB) & ~1;
    
    if (!dev->_id){
        //there can be no device with ID 1 or 0
        //thus if id is 0 then this is the device's first connection
        //assign it a fresh ID

        _devices.addMember();
    retryID:
        for (auto odev : _devices._elems) {
            if ((odev->_id >> 1) == _newid) {
                _newid++;
                if (_newid>MAXID) {
                    _newid = 1;
                    goto retryID;
                }
            }
        }
        dev->_id = (_newid << 1);
        _devices.delMember();
    }
    
    //fixup connection information in ID
    dev->_id |= (dev->_conntype == Device::MUXCONN_WIFI);

    debug("Muxer: adding device (%p) %s assigning id %d",dev,dev->_serial,dev->_id);
    
    _devices.lockMember();
    _devices._elems.push_back(dev);
    _devices.unlockMember();

    _devices.addMember();
    if (std::find(_devices._elems.begin(), _devices._elems.end(), dev) == _devices._elems.end()) {
        error("Device disappeared before it could be used!");
        _devices.delMember();
        return;
    }

    notify_device_add(dev);
    
#warning TODO make preflighting a configurable option!
#ifdef HAVE_LIBIMOBILEDEVICE
    if (dev->_conntype == Device::MUXCONN_USB){
        char *serial = strdup(dev->_serial);
        std::thread b([](char *serial, int devID){
            try {
                preflight_device(serial,devID);
            } catch (tihmstar::exception &e) {
                error("failed to preflight device %s with error=%s code=%d",serial,e.what(),e.code());
            }
            free(serial);
        },serial,dev->_id);
        b.detach();
    }
#ifdef HAVE_WIFI_SUPPORT
    else if (dev->_conntype == Device::MUXCONN_WIFI){
        WIFIDevice *wifidev = (WIFIDevice*)dev;
        try{
            wifidev->startLoop();
        }catch (tihmstar::exception &e){
            error("Failed to start WIFIDevice %s with error=%d (%s)",wifidev->_serial,e.code(),e.what());
            _devices.delMember();
            delete_device(wifidev);
            return;
        }
    }
#endif //HAVE_WIFI_SUPPORT
    
#endif //HAVE_LIBIMOBILEDEVICE
    _devices.delMember();
}

void Muxer::delete_device(Device *dev) noexcept{
    debug("delete_device %p",dev);
    _devices.lockMember();
    const auto target = std::remove(_devices._elems.begin(), _devices._elems.end(), dev);
    if (target != _devices._elems.end()){
        _devices._elems.erase(target, _devices._elems.end());
        notify_device_remove(dev->_id);
        dev->kill();
    }
    _devices.unlockMember();
}

void Muxer::delete_device(int id) noexcept{
    Device *dev = nullptr;
    debug("delete_device id=%d",id);
    try{
        dev = get_device_by_id(id);
    }catch (...){
        //ignore failure
        return;
    }
    delete_device(dev);
}

Device *Muxer::get_device_by_id(int id){
    _devices.addMember();
    for (Device *dev : _devices._elems) {
        if (dev->_id == id) {
            _devices.delMember();
            return dev;
        }
    }
    _devices.delMember();
    reterror("get_device_by_id failed");
}

void Muxer::delete_device_async(uint8_t bus, uint8_t address) noexcept{
    std::mutex waiter;
    waiter.lock();
    std::thread async([this,bus,address,&waiter]{
        _devices.addMember();
        waiter.unlock();
        for (auto dev : _devices._elems){
            if (dev->_conntype == Device::MUXCONN_USB) {
                USBDevice *usbdev = (USBDevice*)dev;
                if (usbdev->_address == address && usbdev->_bus == bus) {
                    _devices.delMember();
                    delete_device(dev);
                    return;
                }
            }
        }
        _devices.delMember();
        error("We are not managing a device on bus 0x%02x, address 0x%02x",bus,address);
    });
    async.detach();
    waiter.lock(); //wait until other thread unlocked once
}

bool Muxer::have_usb_device(uint8_t bus, uint8_t address) noexcept{
    _devices.addMember();
    for (auto dev : _devices._elems){
        if (dev->_conntype == Device::MUXCONN_USB) {
            USBDevice *usbdev = (USBDevice*)dev;
            if (usbdev->_address == address && usbdev->_bus == bus) {
                _devices.delMember();
                return true;
            }
        }
    }
    _devices.delMember();
    return false;
}

bool Muxer::have_wifi_device(std::string macaddr) noexcept{
    _devices.addMember();
    for (auto dev : _devices._elems){
        if (dev->_conntype == Device::MUXCONN_WIFI) {
            WIFIDevice *wifidev = (WIFIDevice*)dev;

            if (wifidev->_serviceName.substr(0,wifidev->_serviceName.find("@")) == macaddr) {
                _devices.delMember();
                return true;
            }
        }
    }
    _devices.delMember();
    return false;
}

int Muxer::id_for_device(const char *uuid, Device::mux_conn_type type) noexcept{
    int ret = 0;
    _devices.addMember();
    for (auto dev : _devices._elems){
        if (dev->_conntype == type && strcmp(uuid,dev->_serial) == 0) {
            ret = dev->_id;
            break;
        }
    }
    _devices.delMember();
    return ret;
}

size_t Muxer::devices_cnt() noexcept{
    return _devices._elems.size();
}



#pragma mark Connection
void Muxer::start_connect(int device_id, uint16_t dport, Client *cli){
    assure(!_isDying);
    _devices.addMember();
    for (Device *dev : _devices._elems) {
        if (dev->_id == device_id) {
            try {
                dev->start_connect(dport, cli);
            } catch (...) {
                _devices.delMember();
                throw;
            }
            _devices.delMember();
            return;
        }
    }
    _devices.delMember();
    reterror("start_connect(%d,%d,%d) failed",device_id,dport,cli->_fd);
}


void Muxer::send_deviceList(Client *client, uint32_t tag){
    PList::Dictionary rsp;
    
    PList::Array devs;
    
    _devices.addMember();
    for (Device *dev : _devices._elems) {
        devs.Append(getDevicePlist(dev).Clone());
    }
    _devices.delMember();

    rsp.Set("DeviceList", devs);
    
    client->send_plist_pkt(tag, rsp.GetPlist());
}

void Muxer::send_listenerList(Client *client, uint32_t tag){
    PList::Dictionary rsp;
    
    PList::Array clis;
    
    _clients.addMember();
    for (Client *c : _clients._elems) {
        clis.Append(getClientPlist(c).Clone());
    }
    _clients.delMember();

    rsp.Set("ListenerList", clis);
    
    client->send_plist_pkt(tag, rsp.GetPlist());
}

#pragma mark notification
void Muxer::notify_device_add(Device *dev) noexcept{
    debug("notify_device_add(%p)",dev);
    
    PList::Dictionary rsp = getDevicePlist(dev);
    
    _clients.addMember();
    for (Client *c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, rsp.GetPlist());
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}

void Muxer::notify_device_remove(int deviceID) noexcept{
    PList::Dictionary rsp;
    
    rsp.Set("MessageType", PList::String("Detached"));
    rsp.Set("DeviceID", PList::Integer(deviceID));
    
    _clients.addMember();
    for (Client *c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, rsp.GetPlist());
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}


void Muxer::notify_device_paired(int deviceID) noexcept{
    PList::Dictionary rsp;
    
    rsp.Set("MessageType", PList::String("Paired"));
    rsp.Set("DeviceID", PList::Integer(deviceID));
    
    _clients.addMember();
    for (Client *c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, rsp.GetPlist());
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}

void Muxer::notify_alldevices(Client *cli) noexcept{
    debug("notify_alldevices(%p)",cli);
    if (!cli->_isListening) {
        error("notify_alldevices called on a client which is not listening");
        return;
    }
    
    _devices.addMember();
    for (Device *d : _devices._elems){
        PList::Dictionary rsp = getDevicePlist(d);
        try {
            cli->send_plist_pkt(0, rsp.GetPlist());
        } catch (...) {
            //we don't care if this fails
        }
    }
    _devices.delMember();
}


#pragma mark Static
PList::Dictionary Muxer::getDevicePlist(Device *dev) noexcept{
    PList::Dictionary devP;
    PList::Dictionary props;
    
    devP.Set("DeviceID", PList::Integer(dev->_id));
    devP.Set("MessageType", PList::String("Attached"));
    
    props.Set("DeviceID", PList::Integer(dev->_id));

    if (dev->_conntype == Device::MUXCONN_USB) {
        USBDevice *usbdev = (USBDevice*)dev;
        props.Set("ConnectionSpeed", PList::Integer(usbdev->_speed));
        props.Set("ConnectionType", PList::String("USB"));
        props.Set("LocationID", PList::Integer(usbdev->usb_location()));
        props.Set("ProductID", PList::Integer(usbdev->_pid));
    }else{
        WIFIDevice *wifidev = (WIFIDevice*)dev;
        props.Set("ConnectionType", PList::String("Network"));
        props.Set("EscapedFullServiceName", PList::String(wifidev->_serviceName));

        if (wifidev->_ipaddr.find(":") == std::string::npos){
            //this is an IPv4 addr
            #warning TODO this is really ugly! :(
            std::vector<char> vbuf;
            char buf[0x80] = {};
            ((uint32_t*)buf)[0] = 0x0210;
            ((uint32_t*)buf)[1] = inet_addr(wifidev->_ipaddr.c_str());
            for (int i=0; i<sizeof(buf);i++){
                vbuf.push_back(buf[i]);
            }
            props.Set("NetworkAddress", PList::Data(vbuf));

        }else{
#warning TODO add support for ipv6 NetworkAddress (data)
        }
#warning TODO missing fields: InterfaceIndex (integer)            

    }
    props.Set("SerialNumber", PList::String(dev->getSerial()));
    devP.Set("Properties", props);
    return devP;
}

PList::Dictionary Muxer::getClientPlist(Client *client) noexcept{
    PList::Dictionary cliP;
    std::string idstring;
    
    const Client::cinfo info = client->getClientInfo();
    
    cliP.Set("Blacklisted", PList::Boolean(false));
    cliP.Set("BundleID", PList::String(info.bundleID));
    cliP.Set("ConnType", PList::Integer((uint64_t)0));
    
    idstring = std::to_string(client->_number) +"-";
    idstring += info.progName;
    
    cliP.Set("ID String", PList::String(idstring));
    cliP.Set("ProgName", PList::String(info.progName));
    
    cliP.Set("kLibUSBMuxVersion", PList::Integer(info.kLibUSBMuxVersion));
    
    return cliP;
}
