
//
//  WIFIDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 27.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDeviceManager_avahi_hpp
#define WIFIDeviceManager_avahi_hpp

#include <avahi-common/simple-watch.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include "../Muxer.hpp"
#include "DeviceManager.hpp"
#include "WIFIDeviceManager.hpp"
#include "../Devices/WIFIDevice.hpp"

class WIFIDeviceManager : public DeviceManager{
private: //for lifecycle management only
    tihmstar::Event _finalUnrefEvent;
    std::shared_ptr<gref_WIFIDeviceManager> _ref;
#ifdef DEBUG
    std::weak_ptr<gref_WIFIDeviceManager> __debug_ref;
#endif
private:
    std::shared_ptr<gref_WIFIDeviceManager> *_wifi_cb_refarg;

	AvahiSimplePoll *_simple_poll;
	AvahiClient *_avahi_client;
	AvahiServiceBrowser *_avahi_sb;

    virtual void loopEvent() override;
public:
    WIFIDeviceManager(std::shared_ptr<gref_Muxer> mux);
    virtual ~WIFIDeviceManager() override;

    void device_add(std::shared_ptr<WIFIDevice> dev);
    void kill() noexcept;

    friend gref_WIFIDeviceManager;
    friend void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata) noexcept;
    friend void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event,
        const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void* userdata) noexcept;
    friend void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
        AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name,
        const AvahiAddress *address, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata) noexcept;
};


#endif /* WIFIDeviceManager_avahi_hpp */
