


//
//  WIFIDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 27.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDeviceManager_hpp
#define WIFIDeviceManager_hpp

#include <Muxer.hpp>
#include <Manager/DeviceManager/DeviceManager.hpp>
#include <avahi-common/simple-watch.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

class WIFIDeviceManager : public DeviceManager{
	AvahiSimplePoll *_simple_poll;
	AvahiClient *_avahi_client;
	AvahiServiceBrowser *_avahi_sb;

    virtual void loopEvent() override;
public:
	WIFIDeviceManager(Muxer *mux);
    virtual ~WIFIDeviceManager() override;    

    friend void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata) noexcept;
    friend void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event,
        const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void* userdata) noexcept;
    friend void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
        AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name,
        const AvahiAddress *address, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata) noexcept;
};


#endif /* WIFIDeviceManager_hpp */