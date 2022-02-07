//
//  Muxer.hpp
//  usbmuxd2
//
//  Created by tihmstar on 07.12.20.
//

#ifndef Muxer_hpp
#define Muxer_hpp

#include <stdint.h>
#include <vector>
#include <memory>
#include <libgeneral/lck_container.hpp>
#include "Device.hpp"
#include <plist/plist.h>

class ClientManager;
class USBDeviceManager;
class WIFIDeviceManager;

class gref_Muxer{
    Muxer *_mgr;
public:
    gref_Muxer(Muxer *mgr);
    ~gref_Muxer();

    Muxer *operator->();
};

class Muxer {
private: //for lifecycle management only
    tihmstar::Event _finalUnrefEvent;
    std::shared_ptr<gref_Muxer> _ref;
#ifdef DEBUG
    std::weak_ptr<gref_Muxer> __debug_ref;
    long _get_selfref_usecount();
#endif
private:
    bool _doPreflight;
    ClientManager *_climgr;
    USBDeviceManager *_usbdevmgr;
    WIFIDeviceManager *_wifidevmgr;
    int _newid;
    tihmstar::lck_contrainer<std::vector<std::shared_ptr<Device>>> _devices;
    tihmstar::lck_contrainer<std::vector<std::shared_ptr<Client>>> _clients;


public:
    Muxer(const Muxer&) = delete; //delete copy constructor
    Muxer(Muxer &&o) = delete; //move constructor

    Muxer(bool doPreflight = true);
    ~Muxer();

    //---- Managers ----
    void spawnClientManager();
    void spawnUSBDeviceManager();
    void spawnWIFIDeviceManager();
    bool hasDeviceManager() noexcept;

    //---- Clients ----
    void add_client(std::shared_ptr<Client> cli);
    void delete_client(int cli_fd) noexcept;
    void delete_client(std::shared_ptr<Client> cli) noexcept;

   //---- Devices ----
    void add_device(std::shared_ptr<Device> dev) noexcept;
    void delete_device(std::shared_ptr<Device> dev) noexcept;
    void delete_device_async(uint8_t bus, uint8_t address) noexcept;
    bool have_usb_device(uint8_t bus, uint8_t address) noexcept;
    bool have_wifi_device(std::string macaddr) noexcept;
    int id_for_device(const char *uuid, Device::mux_conn_type type) noexcept;
    size_t devices_cnt() noexcept;

    //---- Connection ----
    void start_connect(int device_id, uint16_t dport, std::shared_ptr<Client> cli);
    void send_deviceList(std::shared_ptr<Client> cli, uint32_t tag);
    void send_listenerList(std::shared_ptr<Client> cli, uint32_t tag);

   //---- Notification ----
    void notify_device_add(std::shared_ptr<Device> dev) noexcept;
    void notify_device_remove(int deviceID) noexcept;
    void notify_device_paired(int deviceID) noexcept;
    void notify_alldevices(std::shared_ptr<Client> cli) noexcept;

    //---- Static ----
    static plist_t getDevicePlist(std::shared_ptr<Device> dev) noexcept;
    static plist_t getClientPlist(std::shared_ptr<Client> cli) noexcept;

    friend gref_Muxer;
};

#endif /* Muxer_hpp */
