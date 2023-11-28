
//
//  WIFIDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 27.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDeviceManager_avahi_hpp
#define WIFIDeviceManager_avahi_hpp

#include "../Muxer.hpp"
#include "DeviceManager.hpp"
#include "../Devices/WIFIDevice.hpp"

#include <libgeneral/DeliveryEvent.hpp>

#include <avahi-common/simple-watch.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

class WIFIDeviceManager : public DeviceManager{
private:
    std::set<WIFIDevice *> _children;  //raw ptr to shared objec
    std::mutex _childrenLck;
    tihmstar::Event _childrenEvent;
    std::thread _devReaperThread;
    tihmstar::DeliveryEvent<std::shared_ptr<WIFIDevice>> _reapDevices;

    AvahiSimplePoll *_simple_poll;
    AvahiClient *_avahi_client;
    AvahiServiceBrowser *_avahi_sb;
    AvahiServiceBrowser *_avahi_sb2;

    virtual bool loopEvent() override;
    virtual void stopAction() noexcept override;

    void reaper_runloop();
public:
    WIFIDeviceManager(Muxer *mux);
    virtual ~WIFIDeviceManager() override;

    void device_add(std::shared_ptr<WIFIDevice> dev, bool notify = true);

    friend WIFIDevice;
    friend void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata) noexcept;
    friend void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event,
        const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void* userdata) noexcept;
    friend void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
        AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name,
        const AvahiAddress *address, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata) noexcept;
};


#endif /* WIFIDeviceManager_avahi_hpp */
