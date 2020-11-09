//
//  WIFIDevice.hpp
//  usbmuxd2
//
//  Created by tihmstar on 21.06.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDevice_hpp
#define WIFIDevice_hpp

#include <Device.hpp>
#include <string>
#include <Manager/Manager.hpp>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

class WIFIDevice : public Device, Manager {
    std::string _ipaddr;
    std::string _serviceName;
    heartbeat_client_t _hbclient;
	plist_t _hbrsp;
	idevice_t _idev;

    virtual ~WIFIDevice() override;
    virtual void loopEvent() override;
    virtual void beforeLoop() override;
    virtual void afterLoop() noexcept override;

public:
    WIFIDevice(std::string uuid, std::string ipaddr, std::string serviceName, Muxer *mux);
    WIFIDevice(const WIFIDevice &) =delete; //delete copy constructor
    WIFIDevice(WIFIDevice &&o) = delete; //move constructor

    void startLoop();
    virtual void start_connect(uint16_t dport, Client *cli) override;

    friend class Muxer;
    friend class WIFIDeviceManager;
};

#endif /* WIFIDevice_hpp */
