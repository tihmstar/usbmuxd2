//
//  Muxer.hpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef Muxer_hpp
#define Muxer_hpp

#include <stdint.h>
#include <vector>
#include <lck_container.h>
#include <plist/plist.h>
#include <set>
#include <Device.hpp>
#include <Event.hpp>

class Client;
class ClientManager;
class USBDeviceManager;
class WIFIDeviceManager;

class Muxer {
    ClientManager *_climgr;
    USBDeviceManager* _usbdevmgr;
    WIFIDeviceManager* _wifidevmgr;
    lck_contrainer<std::vector<Device *>> _devices;
    lck_contrainer<std::vector<Client *>> _clients;
    int _newid;
    bool _isDying;
    std::atomic<int> _refcnt;
    Event _refevent;

    Device *get_device_by_id(int id);
    
public:
    bool _doPreflight;

    Muxer(const Muxer&) = delete; //delete copy constructor
    Muxer(Muxer &&o) = delete; //move constructor
    
    Muxer();
    ~Muxer();
    
    //---- Managers ----
    void spawnClientManager();
    void spawnUSBDeviceManager();
    void spawnWIFIDeviceManager();
    bool hasDeviceManager() noexcept;

    //---- Clients ----
    void add_client(Client *client);
    void delete_client(Client *client);

    //---- Devices ----
    void add_device(Device *dev) noexcept;
    void delete_device(Device *dev) noexcept;
    void delete_device(int id) noexcept;
    void delete_device_async(uint8_t bus, uint8_t address) noexcept;
    bool have_usb_device(uint8_t bus, uint8_t address) noexcept;
    bool have_wifi_device(std::string macaddr) noexcept;
    int id_for_device(const char *uuid, Device::mux_conn_type type) noexcept;
    size_t devices_cnt() noexcept;

    //---- Connection ----
    void start_connect(int device_id, uint16_t dport, Client *cli);
    void send_deviceList(Client *client, uint32_t tag);
    void send_listenerList(Client *client, uint32_t tag);
    
    //---- Notification ----
    void notify_device_add(Device *dev) noexcept;
    void notify_device_remove(int deviceID) noexcept;
    void notify_device_paired(int deviceID) noexcept;
    void notify_alldevices(Client *cli) noexcept;
    
    //---- Static ----
    static plist_t getDevicePlist(Device *dev) noexcept;
    static plist_t getClientPlist(Client *client) noexcept;
};


#endif /* Muxer_hpp */
