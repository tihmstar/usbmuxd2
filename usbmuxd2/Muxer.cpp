//
//  Muxer.cpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#include "Muxer.hpp"
#include "Devices/USBDevice.hpp"
#include "Manager/USBDeviceManager.hpp"
#include "Manager/ClientManager.hpp"
#include "Client.hpp"
#include "sysconf/preflight.hpp"

#include <libgeneral/macros.h>

#ifdef HAVE_LIBIMOBILEDEVICE
#   include "Devices/WIFIDevice.hpp"
#endif //HAVE_LIBIMOBILEDEVICE

#ifdef HAVE_WIFI_AVAHI
#   include "Manager/WIFIDeviceManager-avahi.hpp"
#elif HAVE_WIFI_MDNS
#   include "Manager/WIFIDeviceManager-mDNS.hpp"
#endif //HAVE_AVAHI

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>

#define MAXID (INT_MAX/2)
#define INVALID_ID (MAXID + 1)

Muxer::Muxer(bool doPreflight, bool allowHeartlessWifi)
: _climgr(nullptr), _usbdevmgr(nullptr), _wifidevmgr(nullptr)
, _doPreflight(doPreflight), _allowHeartlessWifi(allowHeartlessWifi)
, _newid(1)
{
    info("Starting Muxer: preflight=%s allowHeartlessWifi=%s", doPreflight ? "YES" : "NO"
                                                             , allowHeartlessWifi ? "YES" : "NO");
}

Muxer::~Muxer(){
    safeDelete(_climgr);
    safeDelete(_usbdevmgr);
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    safeDelete(_wifidevmgr);
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
}

#pragma mark Managers
void Muxer::spawnClientManager(){
    assure(!_climgr);
    _climgr = new ClientManager(this);
    _climgr->startLoop();
}
void Muxer::spawnUSBDeviceManager(){
    assure(!_usbdevmgr);
    _usbdevmgr = new USBDeviceManager(this);
    _usbdevmgr->startLoop();
}

void Muxer::spawnWIFIDeviceManager(){
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    assure(!_wifidevmgr);
    _wifidevmgr = new WIFIDeviceManager(this);
    _wifidevmgr->startLoop();
#else
    reterror("Compiled without wifi support");
#endif
}

bool Muxer::hasDeviceManager() noexcept{
    return !!_usbdevmgr || !!_wifidevmgr;
}

#pragma mark Clients
void Muxer::add_client(std::shared_ptr<Client> cli){
    debug("add_client %d",cli->_fd);
    _clientsGuard.lockMember();
    _clients.insert(cli);
    try{
        cli->startLoop();
    }catch(tihmstar::exception &e){
        _clientsGuard.unlockMember();
        delete_client(cli);
        throw;
    }
    _clientsGuard.unlockMember();
}

void Muxer::delete_client(int cli_fd) noexcept{
    debug("delete_client fd %d",cli_fd);
    _clientsGuard.lockMember();
    for (auto c : _clients) {
        if (c->_fd == cli_fd) {
            _clients.erase(c);
            _clientsGuard.unlockMember();
            c->kill();
            return;
        }
    }
    _clientsGuard.unlockMember();
}

void Muxer::delete_client(std::shared_ptr<Client> cli) noexcept{
    debug("delete_client %d",cli->_fd);
    _clientsGuard.lockMember();
    if (_clients.erase(cli)) {
        _clientsGuard.unlockMember();
        cli->kill();
    }else{
        _clientsGuard.unlockMember();
    }
}

#pragma mark Devices
void Muxer::add_device(std::shared_ptr<Device> dev, bool notify) noexcept {
    debug("add_device %s",dev->_serial);

    //get id of already connected device but with the other connection type
    //discard the id-based connection type information
    dev->_id = id_for_device(dev->_serial, dev->_conntype == Device::MUXCONN_USB ? Device::MUXCONN_WIFI : Device::MUXCONN_USB) & ~1;

    if (!dev->_id){
        //there can be no device with ID 1 or 0
        //thus if id is 0 then this is the device's first connection
        //assign it a fresh ID
        guardRead(_devicesGuard);
    retryID:
        for (auto odev : _devices) {
            if ((odev->_id >> 1) == _newid) {
                _newid++;
                if (_newid>MAXID) {
                    _newid = 1;
                    goto retryID;
                }
            }
        }
        dev->_id = (_newid << 1);
    }

    //fixup connection information in ID
    dev->_id |= (dev->_conntype == Device::MUXCONN_WIFI);

    debug("Muxer: adding device %s assigning id %d",dev->_serial,dev->_id);

    {
        guardWrite(_devicesGuard);
        _devices.insert(dev);
    }

#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    if (dev->_conntype == Device::MUXCONN_WIFI){
        std::shared_ptr<WIFIDevice> wifidev = std::static_pointer_cast<WIFIDevice>(dev);
        try{
            wifidev->startLoop();
        }catch (tihmstar::exception &e){
            error("Failed to start WIFIDevice %s with error=%d (%s)",wifidev->_serial,e.code(),e.what());
            if (!_allowHeartlessWifi){
                delete_device(wifidev);
            }
            return;
        }
    }
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    
#ifdef HAVE_LIBIMOBILEDEVICE
    if (dev->_conntype == Device::MUXCONN_USB && _doPreflight){
        try {
            preflight_device(dev->_serial,dev->_id);
        } catch (tihmstar::exception &e) {
            warning("Failed to preflight device '%s' with err:\n%s",dev->_serial,e.dumpStr().c_str());
        }
    }
#endif //HAVE_LIBIMOBILEDEVICE
    
    if (notify) notify_device_add(dev);
}

void Muxer::delete_device(std::shared_ptr<Device> dev) noexcept {
    {
        guardWrite(_devicesGuard);
        _devices.erase(dev);
    }
    notify_device_remove(dev->_id);
}

void Muxer::delete_device(uint8_t bus, uint8_t address) noexcept {
    int devid = INVALID_ID;
    {
        guardWrite(_devicesGuard);
        for (auto dev : _devices){
            if (dev->_conntype == Device::MUXCONN_USB) {
                USBDevice *usbdev = (USBDevice*)dev.get();
                if (usbdev->usb_location() == (((uint16_t)bus << 16) | address)){
                    devid = dev->_id;
                    _devices.erase(dev);
                    break;
                }
            }
        }
    }
    if (devid != INVALID_ID) notify_device_remove(devid);
}

void Muxer::delete_wifi_pairing_device_with_ip(std::vector<std::string> ipaddrs) noexcept{
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    guardWrite(_devicesGuard);
    for (auto dev : _devices){
        if (dev->_conntype == Device::MUXCONN_WIFI) {
            WIFIDevice *wifidev = (WIFIDevice*)dev.get();
            for (auto nip : ipaddrs) {
                if (strncmp(wifidev->_serial, "WIFIPAIR", sizeof("WIFIPAIR")-1) == 0 &&
                    std::find(wifidev->_ipaddr.begin(), wifidev->_ipaddr.end(), nip) != wifidev->_ipaddr.end()) {
                    _devices.erase(dev);
                    return;
                }
            }
        }
    }
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
}

bool Muxer::have_usb_device(uint8_t bus, uint8_t address) noexcept {
    guardRead(_devicesGuard);
    for (auto dev : _devices){
        if (dev->_conntype == Device::MUXCONN_USB) {
            USBDevice *usbdev = (USBDevice*)dev.get();
            if (usbdev->usb_location() == (((uint16_t)bus << 16) | address)){
                return true;
            }
        }
    }
    return false;
}

bool Muxer::have_wifi_device_with_mac(std::string macaddr) noexcept{
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    guardRead(_devicesGuard);
    for (auto dev : _devices){
        if (dev->_conntype == Device::MUXCONN_WIFI) {
            WIFIDevice *wifidev = (WIFIDevice*)dev.get();
            if (wifidev->_serviceName.substr(0,wifidev->_serviceName.find("@")) == macaddr) {
                return true;
            }
        }
    }
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    return false;
}

bool Muxer::have_wifi_device_with_ip(std::vector<std::string> ipaddrs) noexcept{
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    guardRead(_devicesGuard);
    for (auto dev : _devices){
        if (dev->_conntype == Device::MUXCONN_WIFI) {
            WIFIDevice *wifidev = (WIFIDevice*)dev.get();
            for (auto nip : ipaddrs) {
                if (std::find(wifidev->_ipaddr.begin(), wifidev->_ipaddr.end(), nip) != wifidev->_ipaddr.end()) {
                    return true;
                }
            }
        }
    }
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    return false;
}


int Muxer::id_for_device(const char *uuid, Device::mux_conn_type type) noexcept {
    int ret = 0;
    guardRead(_devicesGuard);
    for (auto dev : _devices){
        if (dev->_conntype == type && strcmp(uuid,dev->_serial) == 0) {
            ret = dev->_id;
            break;
        }
    }
    return ret;
}

size_t Muxer::devices_cnt() noexcept {
    guardRead(_devicesGuard);
    return _devices.size();
}

#pragma mark Connection
void Muxer::start_connect(int device_id, uint16_t dport, std::shared_ptr<Client> cli){
    std::shared_ptr<Device> dev;
    {
        guardRead(_devicesGuard);
        for (auto d : _devices) {
            if (d->_id == device_id) {
                dev = d;
                goto found_device;
            }
        }
        reterror("start_connect(%d,%d,%d) failed",device_id,dport,cli->_fd);
    found_device:;
    }
    try {
        dev->start_connect(dport, cli);
    } catch (...) {
        throw;
    }
    return;
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
    {
        guardRead(_devicesGuard);
        for (auto &dev : _devices) {
            plist_array_append_item(p_devarr, getDevicePlist(dev));
        }
    }
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


    {
        guardRead(_clientsGuard);
        for (auto &c : _clients) {
            plist_array_append_item(p_cliarr, getClientPlist(c));
        }
    }

    plist_dict_set_item(p_rsp, "ListenerList", p_cliarr); p_cliarr = NULL; //transfer ownership
    cli->send_plist_pkt(tag, p_rsp);
}

#pragma mark Notification
void Muxer::notify_device_add(std::shared_ptr<Device> dev) noexcept{
    debug("notify_device_add(%d)",dev->_id);
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });

    p_rsp = getDevicePlist(dev);

    {
        guardRead(_clientsGuard);
        for (auto &c : _clients){
            if (c->_isListening) {
                try {
                    c->send_plist_pkt(0, p_rsp);
                } catch (...) {
                    //we don't care if this fails
                }
            }
        }
    }
}

void Muxer::notify_device_remove(int deviceID) noexcept{
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });
    
    p_rsp = plist_new_dict();
    plist_dict_set_item(p_rsp, "MessageType", plist_new_string("Detached"));
    plist_dict_set_item(p_rsp, "DeviceID", plist_new_uint(deviceID));
    
    {
        guardRead(_clientsGuard);
        for (auto c : _clients){
            if (c->_isListening) {
                try {
                    c->send_plist_pkt(0, p_rsp);
                } catch (...) {
                    //we don't care if this fails
                }
            }
        }
    }
}

void Muxer::notify_device_paired(int deviceID) noexcept{
    plist_t p_rsp = NULL;
    cleanup([&]{
        safeFreeCustom(p_rsp, plist_free);
    });

    p_rsp = plist_new_dict();
    plist_dict_set_item(p_rsp, "MessageType", plist_new_string("Paired"));
    plist_dict_set_item(p_rsp, "DeviceID", plist_new_uint(deviceID));

    {
        guardRead(_clientsGuard);
        for (auto c : _clients){
            if (c->_isListening) {
                try {
                    c->send_plist_pkt(0, p_rsp);
                } catch (...) {
                    //we don't care if this fails
                }
            }
        }
    }
}

void Muxer::notify_alldevices(std::shared_ptr<Client> cli) noexcept {
    debug("notify_alldevices(%d)",cli->_fd);
    if (!cli->_isListening) {
        error("notify_alldevices called on a client which is not listening");
        return;
    }
    
    {
        guardRead(_devicesGuard);
        for (auto &d : _devices){
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
    }
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
        plist_dict_set_item(p_props, "ConnectionSpeed", plist_new_uint(usbdev->getSpeed()));
        plist_dict_set_item(p_props, "ConnectionType", plist_new_string("USB"));
        plist_dict_set_item(p_props, "LocationID", plist_new_uint(usbdev->usb_location()));
        plist_dict_set_item(p_props, "ProductID", plist_new_uint(usbdev->getPid()));
    }else if (dev->_conntype == Device::MUXCONN_WIFI){
#if defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
        std::shared_ptr<WIFIDevice> wifidev = std::static_pointer_cast<WIFIDevice>(dev);
        plist_dict_set_item(p_props, "ConnectionType", plist_new_string("Network"));
        plist_dict_set_item(p_props, "EscapedFullServiceName", plist_new_string(wifidev->_serviceName.c_str()));

        std::string ipaddr = wifidev->_ipaddr.front();

        for (auto ipaddr : wifidev->_ipaddr) {
            char buf[0x80] = {};
            if (ipaddr.find(":") == std::string::npos){
                //this is an IPv4 addr
                struct sockaddr_in *ip = (struct sockaddr_in *)buf;
#ifdef HAVE_STRUCT_SOCKADDR_SIN__LEN
                ip->sin_len = sizeof(struct sockaddr_in);
#endif
                ip->sin_family = AF_INET;
                ip->sin_addr.s_addr = inet_addr(ipaddr.c_str());
                plist_dict_set_item(p_props, "NetworkAddress", plist_new_data(buf, sizeof(buf)));
            }else{
                struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)buf;
#ifdef HAVE_STRUCT_SOCKADDR_SIN__LEN
                ip6->sin6_len = sizeof(sockaddr_in6);
#endif
                ip6->sin6_family = AF_INET6;
                ip6->sin6_scope_id = wifidev->_interfaceIndex;
                if (!inet_pton(AF_INET6, ipaddr.c_str(), &ip6->sin6_addr)) continue;
                plist_dict_set_item(p_props, "NetworkAddress", plist_new_data(buf, sizeof(buf)));
            }
            break;
        }
        if (wifidev->_interfaceIndex) {
            plist_dict_set_item(p_props, "InterfaceIndex", plist_new_int(wifidev->_interfaceIndex));
        }
#endif //defined(HAVE_WIFI_AVAHI) || defined(HAVE_WIFI_MDNS)
    }else{
        assert(0); //THIS SHOULD NOT HAPPEN!!!
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
