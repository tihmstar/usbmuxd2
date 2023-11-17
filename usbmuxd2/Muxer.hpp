//
//  Muxer.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef Muxer_hpp
#define Muxer_hpp

#include "Devices/Device.hpp"

#include <libgeneral/macros.h>
#include <libgeneral/GuardAccess.hpp>
#include <plist/plist.h>

#include <set>

class ClientManager;
class USBDeviceManager;
class WIFIDeviceManager;

class Muxer {
    ClientManager *_climgr;
    USBDeviceManager *_usbdevmgr;
    WIFIDeviceManager *_wifidevmgr;

    bool _doPreflight;
    bool _allowHeartlessWifi;
    int _newid;
    std::set<std::shared_ptr<Device>> _devices;
    tihmstar::GuardAccess _devicesGuard;
    std::set<std::shared_ptr<Client>> _clients;
    tihmstar::GuardAccess _clientsGuard;
public:
    Muxer(bool doPreflight = true, bool allowHeartlessWifi = false);
    ~Muxer();

#pragma mark Managers
    void spawnClientManager();
    void spawnUSBDeviceManager();
    void spawnWIFIDeviceManager();
    bool hasDeviceManager() noexcept;

#pragma mark Clients
    void add_client(std::shared_ptr<Client> cli);
    void delete_client(int cli_fd) noexcept;
    void delete_client(std::shared_ptr<Client> cli) noexcept;

#pragma mark Devices
    void add_device(std::shared_ptr<Device> dev, bool notify = true) noexcept;
    void delete_device(std::shared_ptr<Device> dev) noexcept;
    void delete_device(uint8_t bus, uint8_t address) noexcept;
    void delete_wifi_pairing_device_with_ip(std::vector<std::string> ipaddrs) noexcept;
    bool have_usb_device(uint8_t bus, uint8_t address) noexcept;
    bool have_wifi_device_with_mac(std::string macaddr) noexcept;
    bool have_wifi_device_with_ip(std::vector<std::string> ipaddrs) noexcept;
    int id_for_device(const char *uuid, Device::mux_conn_type type) noexcept;
    size_t devices_cnt() noexcept;

#pragma mark Connection
    void start_connect(int device_id, uint16_t dport, std::shared_ptr<Client> cli);
    void send_deviceList(std::shared_ptr<Client> cli, uint32_t tag);
    void send_listenerList(std::shared_ptr<Client> cli, uint32_t tag);

#pragma mark Notification
    void notify_device_add(std::shared_ptr<Device> dev) noexcept;
    void notify_device_remove(int deviceID) noexcept;
    void notify_device_paired(int deviceID) noexcept;
    void notify_alldevices(std::shared_ptr<Client> cli) noexcept;

#pragma mark Static
    static plist_t getDevicePlist(std::shared_ptr<Device> dev) noexcept;
    static plist_t getClientPlist(std::shared_ptr<Client> cli) noexcept;
};

#endif /* Muxer_hpp */
