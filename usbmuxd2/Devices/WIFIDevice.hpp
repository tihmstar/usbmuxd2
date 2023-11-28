//
//  WIFIDevice.hpp
//  usbmuxd2
//
//  Created by tihmstar on 30.05.21.
//

#ifndef WIFIDevice_hpp
#define WIFIDevice_hpp

#include "Device.hpp"
#include <libgeneral/Manager.hpp>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

#include <iostream>
#include <vector>

class WIFIDeviceManager;
class WIFIDevice : public Device, tihmstar::Manager {
    WIFIDeviceManager *_parent;
    std::weak_ptr<WIFIDevice> _selfref;
    std::vector<std::string> _ipaddr;
    std::string _serviceName;
    uint32_t _interfaceIndex;
    heartbeat_client_t _hbclient;
    plist_t _hbrsp;
    idevice_t _idev;

    virtual bool loopEvent() override;
    virtual void beforeLoop() override;
    virtual void afterLoop() noexcept override;

public:
    WIFIDevice(Muxer *mux, WIFIDeviceManager *parent, std::string uuid, std::vector<std::string> ipaddr, std::string serviceName, uint32_t interfaceIndex = 0);
    WIFIDevice(const WIFIDevice &) =delete; //delete copy constructor
    WIFIDevice(WIFIDevice &&o) = delete; //move constructor
    virtual ~WIFIDevice() override;

    virtual void kill() noexcept override;
    void deconstruct() noexcept;
    void startLoop();
    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) override;

    friend class Muxer;
    friend class WIFIDeviceManager;
};

#endif /* WIFIDevice_hpp */
