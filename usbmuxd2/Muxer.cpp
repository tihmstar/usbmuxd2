//
//  Muxer.cpp
//  usbmuxd2
//
//  Created by tihmstar on 07.12.20.
//

#include "Muxer.hpp"
#include "Manager/USBDeviceManager.hpp"
#include "Manager/ClientManager.hpp"
#include <libgeneral/macros.h>
#include "Client.hpp"
#include "sysconf/preflight.hpp"
#include "Devices/USBDevice.hpp"
#include "Devices/WIFIDevice.hpp"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#ifdef HAVE_WIFI_SUPPORT
#   ifdef HAVE_WIFI_AVAHI
#       include "Manager/WIFIDeviceManager-avahi.hpp"
#   elif HAVE_WIFI_MDNS
#       include "Manager/WIFIDeviceManager-mDNS.hpp"
#   endif //HAVE_AVAHI
#endif


#define MAXID (INT_MAX/2)

#pragma mark gref_Muxer

gref_Muxer::gref_Muxer(Muxer *mgr)
: _mgr(mgr)
{
    //
}

gref_Muxer::~gref_Muxer(){
    _mgr->_finalUnrefEvent.notifyAll();
}

Muxer *gref_Muxer::operator->(){
    return _mgr;
}

#pragma mark Muxer

#ifdef DEBUG
long Muxer::_get_selfref_usecount(){
  return __debug_ref.use_count();
}
#endif

Muxer::Muxer(bool doPreflight)
: _ref{std::make_shared<gref_Muxer>(this)},
    _doPreflight(doPreflight), _climgr(nullptr), _usbdevmgr(nullptr), _wifidevmgr(nullptr), _newid(1)
{
#ifdef DEBUG
  __debug_ref = _ref;
#endif
}

Muxer::~Muxer(){
    debug("[Muxer] destroing muxer");
    _ref = nullptr;

    if (_usbdevmgr) {
        _usbdevmgr->kill();
    }

    if (_wifidevmgr) {
        _wifidevmgr->kill();
    }

    //delete all devices
    _devices.addMember();
    while (_devices._elems.size()) {
      auto dev = _devices._elems.front();
      _devices.delMember();
      delete_device(dev);
      _devices.addMember();
    }
    _devices.delMember();

    //delete all clients
    _clients.addMember();
    while (_clients._elems.size()) {
      auto cli = _clients._elems.front();
      _clients.delMember();
      delete_client(cli);
      _clients.addMember();
    }
    _clients.delMember();


    safeDelete(_climgr);
    safeDelete(_usbdevmgr);
    safeDelete(_wifidevmgr);
    _finalUnrefEvent.wait(); //wait until no more references to this object exist
}

#pragma mark Managers

void Muxer::spawnClientManager(){
    _climgr = new ClientManager(_ref);
    _climgr->startLoop();
}

void Muxer::spawnUSBDeviceManager(){
    assure(!_usbdevmgr);
    _usbdevmgr = new USBDeviceManager(_ref);
    _usbdevmgr->startLoop();
}

void Muxer::spawnWIFIDeviceManager(){
#ifndef HAVE_WIFI_SUPPORT
    reterror("compiled without wifi support");
#else
    assure(!_wifidevmgr);
    _wifidevmgr = new WIFIDeviceManager(_ref);
    _wifidevmgr->startLoop();
#endif
}

bool Muxer::hasDeviceManager() noexcept{
    return _wifidevmgr != NULL || _usbdevmgr != NULL;
}

#pragma mark Clients

void Muxer::add_client(std::shared_ptr<Client> cli){
    debug("add_client %d",cli->_fd);
    _clients.lockMember();
    _clients._elems.push_back(cli);
    try{
        cli->startLoop();
    }catch(tihmstar::exception &e){
        _clients.unlockMember();
        delete_client(cli->_fd);
        throw;
    }
    _clients.unlockMember();
}

void Muxer::delete_client(int cli_fd) noexcept{
    debug("delete_client %d",cli_fd);
    std::shared_ptr<Client> hold = nullptr;
    _clients.addMember();
    for (auto c : _clients._elems) {
        if (c->_fd == cli_fd) {
            hold = c;//hold shared ptr until after we unlocked _clients
            _clients.delMember();
            _clients.lockMember();
            const auto target = std::remove(_clients._elems.begin(), _clients._elems.end(), c);
            _clients._elems.erase(target, _clients._elems.end());
            _clients.unlockMember();
            return; //return here, iteration no longer valid!
        }
    }
    _clients.delMember();
    if (hold){
      hold->kill();
    }
}

void Muxer::delete_client(std::shared_ptr<Client> cli) noexcept{
    debug("delete_client %d",cli->_fd);
    _clients.lockMember();
    const auto target = std::remove(_clients._elems.begin(), _clients._elems.end(), cli);
    if (target != _clients._elems.end()) {
      _clients._elems.erase(target, _clients._elems.end());
      _clients.unlockMember();
      cli->kill();
    }else{
      _clients.unlockMember();
    }
}

void Muxer::add_device(std::shared_ptr<Device> dev) noexcept{
    debug("add_device %s",dev->_serial);

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

    debug("Muxer: adding device %s assigning id %d",dev->_serial,dev->_id);

    _devices.lockMember();
    _devices._elems.push_back(dev);
    _devices.unlockMember();

    _devices.addMember();
    if (std::find(_devices._elems.begin(), _devices._elems.end(), dev) == _devices._elems.end()) {
        error("Device disappeared before it could be used!");
        _devices.delMember();
        return;
    }

#ifdef HAVE_WIFI_SUPPORT
    if (dev->_conntype == Device::MUXCONN_WIFI){
        std::shared_ptr<WIFIDevice> wifidev = std::static_pointer_cast<WIFIDevice>(dev);
        try{
            wifidev->startLoop();
        }catch (tihmstar::exception &e){
            error("Failed to start WIFIDevice %s with error=%d (%s)",wifidev->_serial,e.code(),e.what());
            _devices.delMember();
            delete_device(dev);
            return;
        }
    }
#endif //HAVE_WIFI_SUPPORT


#ifdef HAVE_LIBIMOBILEDEVICE
    if (dev->_conntype == Device::MUXCONN_USB && _doPreflight){
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
#endif //HAVE_LIBIMOBILEDEVICE
    _devices.delMember();
    notify_device_add(dev);
}

void Muxer::delete_device(std::shared_ptr<Device> dev) noexcept{
    debug("delete_device %s",dev->_serial);
    _devices.lockMember();
    const auto target = std::remove(_devices._elems.begin(), _devices._elems.end(), dev);
    if (target != _devices._elems.end()){
        _devices._elems.erase(target, _devices._elems.end());
        notify_device_remove(dev->_id);
        _devices.unlockMember();
        dev->kill();
    }else{
      _devices.unlockMember();
    }
}

void Muxer::delete_device_async(uint8_t bus, uint8_t address) noexcept{
    std::thread async([this,bus,address](std::shared_ptr<gref_Muxer> ref){
        _devices.addMember();
        for (auto dev : _devices._elems){
            if (dev->_conntype == Device::MUXCONN_USB) {
                std::shared_ptr<USBDevice> usbdev = std::static_pointer_cast<USBDevice>(dev);
                if (usbdev->_address == address && usbdev->_bus == bus) {
                    _devices.delMember();
                    delete_device(dev);
                    return;
                }
            }
        }
        _devices.delMember();
        error("We are not managing a device on bus 0x%02x, address 0x%02x",bus,address);
    },_ref);
    async.detach();
}


bool Muxer::have_usb_device(uint8_t bus, uint8_t address) noexcept{
    _devices.addMember();
    for (auto dev : _devices._elems){
        if (dev->_conntype == Device::MUXCONN_USB) {
            std::shared_ptr<USBDevice> usbdev = std::static_pointer_cast<USBDevice>(dev);
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
            std::shared_ptr<WIFIDevice> wifidev = std::static_pointer_cast<WIFIDevice>(dev);
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

#pragma mark connection

void Muxer::start_connect(int device_id, uint16_t dport, std::shared_ptr<Client> cli){
    _devices.addMember();
    for (auto dev : _devices._elems) {
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

void Muxer::send_deviceList(std::shared_ptr<Client> cli, uint32_t tag){
    plist_t p_rsp = NULL;
    plist_t p_devarr = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
        safeFreeCustom(p_devarr, plist_free);
    });
    assure(p_rsp = plist_new_dict());
    assure(p_devarr = plist_new_array());
     _devices.addMember();
     for (auto &dev : _devices._elems) {
         plist_array_append_item(p_devarr, getDevicePlist(dev));
     }
     _devices.delMember();

     plist_dict_set_item(p_rsp, "DeviceList", p_devarr); p_devarr = NULL; //transfer ownership

     cli->send_plist_pkt(tag, p_rsp);
}

void Muxer::send_listenerList(std::shared_ptr<Client> cli, uint32_t tag){
    plist_t p_rsp = NULL;
    plist_t p_cliarr = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
        safeFreeCustom(p_cliarr, plist_free);
    });
    assure(p_rsp = plist_new_dict());
    assure(p_cliarr = plist_new_array());


    _clients.addMember();
    for (auto &c : _clients._elems) {
        plist_array_append_item(p_cliarr, getClientPlist(c));
    }
    _clients.delMember();

    plist_dict_set_item(p_rsp, "ListenerList", p_cliarr); p_cliarr = NULL; //transfer ownership

    cli->send_plist_pkt(tag, p_rsp);
}


#pragma mark notification
void Muxer::notify_device_add(std::shared_ptr<Device> dev) noexcept{
    debug("notify_device_add(%d)",dev->_id);
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });

    p_rsp = getDevicePlist(dev);

    _clients.addMember();
    for (auto &c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, p_rsp);
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}

void Muxer::notify_device_remove(int deviceID) noexcept{
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });

    p_rsp = plist_new_dict();
    plist_dict_set_item(p_rsp, "MessageType", plist_new_string("Detached"));
    plist_dict_set_item(p_rsp, "DeviceID", plist_new_uint(deviceID));

    _clients.addMember();
    for (auto c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, p_rsp);
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}


void Muxer::notify_device_paired(int deviceID) noexcept{
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });

    p_rsp = plist_new_dict();
    plist_dict_set_item(p_rsp, "MessageType", plist_new_string("Paired"));
    plist_dict_set_item(p_rsp, "DeviceID", plist_new_uint(deviceID));

    _clients.addMember();
    for (auto c : _clients._elems){
        if (c->_isListening) {
            try {
                c->send_plist_pkt(0, p_rsp);
            } catch (...) {
                //we don't care if this fails
            }
        }
    }
    _clients.delMember();
}


void Muxer::notify_alldevices(std::shared_ptr<Client> cli) noexcept{
    debug("notify_alldevices(%d)",cli->_fd);
    if (!cli->_isListening) {
        error("notify_alldevices called on a client which is not listening");
        return;
    }

    _devices.addMember();
    for (auto &d : _devices._elems){
        plist_t p_rsp = NULL;
        cleanup([&]{
            safeFreeCustom(p_rsp, plist_free);
        });
        p_rsp = getDevicePlist(d);
        try {
            cli->send_plist_pkt(0, p_rsp);
        } catch (...) {
            //we don't care if this fails
        }
    }
    _devices.delMember();
}

#pragma mark Static
plist_t Muxer::getDevicePlist(std::shared_ptr<Device> dev) noexcept{
    plist_t p_devp = NULL;
    plist_t p_props = NULL;
    cleanup([&]{
        safeFreeCustom(p_devp, plist_free);
        safeFreeCustom(p_props, plist_free);
    });

    p_devp = plist_new_dict();
    p_props = plist_new_dict();

    plist_dict_set_item(p_devp, "MessageType", plist_new_string("Attached"));
    plist_dict_set_item(p_devp, "DeviceID", plist_new_uint(dev->_id));


    plist_dict_set_item(p_props, "DeviceID", plist_new_uint(dev->_id));


    if (dev->_conntype == Device::MUXCONN_USB) {
        std::shared_ptr<USBDevice> usbdev = std::static_pointer_cast<USBDevice>(dev);
        plist_dict_set_item(p_props, "ConnectionSpeed", plist_new_uint(usbdev->_speed));
        plist_dict_set_item(p_props, "ConnectionType", plist_new_string("USB"));
        plist_dict_set_item(p_props, "LocationID", plist_new_uint(usbdev->usb_location()));
        plist_dict_set_item(p_props, "ProductID", plist_new_uint(usbdev->_pid));
    }else{
        std::shared_ptr<WIFIDevice> wifidev = std::static_pointer_cast<WIFIDevice>(dev);
        plist_dict_set_item(p_props, "ConnectionType", plist_new_string("Network"));
        plist_dict_set_item(p_props, "EscapedFullServiceName", plist_new_string(wifidev->_serviceName.c_str()));

        if (wifidev->_ipaddr.find(":") == std::string::npos){
            //this is an IPv4 addr
            #warning TODO this is ugly! :(
            char buf[0x80] = {};
            ((uint32_t*)buf)[0] = 0x0210;
            ((uint32_t*)buf)[1] = inet_addr(wifidev->_ipaddr.c_str());
            plist_dict_set_item(p_props, "NetworkAddress", plist_new_data(buf, sizeof(buf)));
        }else{
#warning TODO add support for ipv6 NetworkAddress (data)
        }
#warning TODO missing fields: InterfaceIndex (integer)

    }
    plist_dict_set_item(p_props, "SerialNumber", plist_new_string(dev->getSerial()));
    plist_dict_set_item(p_devp, "Properties", p_props);p_props = NULL; // transfer ownership

    {
        plist_t ret = p_devp; p_devp = NULL;
        return ret;
    }
}

plist_t Muxer::getClientPlist(std::shared_ptr<Client> cli) noexcept{
    plist_t p_ret = NULL;
    cleanup([&]{
        safeFreeCustom(p_ret, plist_free);
    });

    const Client::cinfo info = cli->getClientInfo();

    p_ret = plist_new_dict();

    plist_dict_set_item(p_ret,"Blacklisted", plist_new_bool(0));
    plist_dict_set_item(p_ret,"BundleID", plist_new_string(info.bundleID));
    plist_dict_set_item(p_ret,"ConnType", plist_new_uint(0));

    {
        std::string idstring;
        idstring = std::to_string(cli->_number) +"-";
        idstring += info.progName;

        plist_dict_set_item(p_ret,"ID String", plist_new_string(idstring.c_str()));
    }
    plist_dict_set_item(p_ret,"ProgName", plist_new_string(info.progName));

    plist_dict_set_item(p_ret,"kLibUSBMuxVersion", plist_new_uint(info.kLibUSBMuxVersion));
    {
        plist_t ret = p_ret; p_ret = NULL;
        return ret;
    }
}
