//
//  WIFIDevice.hpp
//  usbmuxd2
//
//  Created by tihmstar on 30.05.21.
//

#ifndef WIFIDevice_hpp
#define WIFIDevice_hpp

#include "Device.hpp"
#include <string>
#include <libgeneral/Manager.hpp>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

class WIFIDevice : public Device, tihmstar::Manager {
    std::weak_ptr<WIFIDevice> _selfref;
    std::vector<std::string> _ipaddr;
    std::string _serviceName;
    heartbeat_client_t _hbclient;
    plist_t _hbrsp;
    idevice_t _idev;

    virtual bool loopEvent() override;
    virtual void beforeLoop() override;

public:
    WIFIDevice(Muxer *mux, std::string uuid, std::vector<std::string> ipaddr, std::string serviceName);
    WIFIDevice(const WIFIDevice &) =delete; //delete copy constructor
    WIFIDevice(WIFIDevice &&o) = delete; //move constructor
    virtual ~WIFIDevice() override;

    virtual void kill() noexcept override;
    void startLoop();
    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) override;

    friend class Muxer;
    friend class WIFIDeviceManager;
};

#endif /* WIFIDevice_hpp */
