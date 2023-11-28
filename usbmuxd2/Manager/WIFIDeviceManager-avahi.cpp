
//
//  WIFIDeviceManager.cpp
//  usbmuxd2
//
//  Created by tihmstar on 27.05.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include <libgeneral/macros.h>

#ifdef HAVE_WIFI_AVAHI
#include "WIFIDeviceManager-avahi.hpp"

#include <sysconf/sysconf.hpp>

#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/address.h>

#include <string.h>

#pragma mark avahi_callback definitions
void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata) noexcept;
void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event,
       const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void* userdata) noexcept;
void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
       AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name,
       const AvahiAddress *address, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata) noexcept;

#pragma mark WIFIDeviceManager

WIFIDeviceManager::WIFIDeviceManager(Muxer *mux)
: DeviceManager(mux)
{
   int err = 0;
   debug("WIFIDeviceManager avahi-client");

   assure(_simple_poll = avahi_simple_poll_new());

   retassure(_avahi_client = avahi_client_new(avahi_simple_poll_get(_simple_poll), (AvahiClientFlags)0, avahi_client_callback, this, &err),
       "Failed to start avahi_client with error=%d. Is the daemon running?",err);
   assure(!err);

   assure(_avahi_sb = avahi_service_browser_new(_avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_apple-mobdev2._tcp", NULL, (AvahiLookupFlags)0, avahi_browse_callback, this));
   assure(_avahi_sb2 = avahi_service_browser_new(_avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_remotepairing-manual-pairing._tcp", NULL, (AvahiLookupFlags)0, avahi_browse_callback, this));
   debug("WIFIDeviceManager created avahi service_browser");

    _devReaperThread = std::thread([this]{
        reaper_runloop();
    });
}

WIFIDeviceManager::~WIFIDeviceManager(){
    stopLoop();
    if (_children.size()) {
        debug("waiting for wifi children to die...");
        std::unique_lock<std::mutex> ul(_childrenLck);
        while (size_t s = _children.size()) {
            for (auto c : _children) c->kill();
            uint64_t wevent = _childrenEvent.getNextEvent();
            ul.unlock();
            debug("Need to kill %zu more wifi children",s);
            _childrenEvent.waitForEvent(wevent);
            ul.lock();
        }
    }
    _reapDevices.kill();
    _devReaperThread.join();

    safeFreeCustom(_avahi_sb,avahi_service_browser_free);
    safeFreeCustom(_avahi_sb2,avahi_service_browser_free);
    safeFreeCustom(_avahi_client,avahi_client_free);
    safeFreeCustom(_simple_poll,avahi_simple_poll_free);
}

void WIFIDeviceManager::device_add(std::shared_ptr<WIFIDevice> dev, bool notify){
    dev->_selfref = dev;
    _children.insert(dev.get());
    _mux->add_device(dev, notify);
}


bool WIFIDeviceManager::loopEvent(){
    int err = avahi_simple_poll_loop(_simple_poll); //it's fine if this is blocking
    debug("WIFIDeviceManager avahi main loop finished");
    return err == 0;
}

void WIFIDeviceManager::stopAction() noexcept{
    avahi_simple_poll_quit(_simple_poll);
}

void WIFIDeviceManager::reaper_runloop(){
    while (true) {
        std::shared_ptr<WIFIDevice>dev;
        try {
            dev = _reapDevices.wait();
        } catch (...) {
            break;
        }
        //make device go out of scope so it can die in piece
        dev->deconstruct();
    }
}

#pragma mark avahi_callback implementations

void avahi_client_callback(AvahiClient *c, AvahiClientState state, void *userdata) noexcept{
   WIFIDeviceManager *devmgr = (WIFIDeviceManager*)userdata;
   /* Called whenever the client or server state changes */
   if (state == AVAHI_CLIENT_FAILURE) {
       debug("Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
       avahi_simple_poll_quit(devmgr->_simple_poll);
   }
}

void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event,
       const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void* userdata) noexcept{
   WIFIDeviceManager *devmgr = (WIFIDeviceManager*)userdata;

   switch (event) {
   case AVAHI_BROWSER_FAILURE:
       debug("(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
       avahi_simple_poll_quit(devmgr->_simple_poll);
       return;
   case AVAHI_BROWSER_NEW:
       debug("(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
       /* We ignore the returned resolver object. In the callback
          function we free it. If the server is terminated before
          the callback function is called the server will free
          the resolver for us. */
       if (!(avahi_service_resolver_new(devmgr->_avahi_client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0, avahi_resolve_callback, userdata)))
           debug("Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(devmgr->_avahi_client)));
       break;
   case AVAHI_BROWSER_REMOVE:
       debug("(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
       break;
   case AVAHI_BROWSER_ALL_FOR_NOW:
   case AVAHI_BROWSER_CACHE_EXHAUSTED:
       debug("(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
       break;
   }
}

void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
        AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name,
        const AvahiAddress *address, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata) noexcept{
    char addr[AVAHI_ADDRESS_STR_MAX] = {};
    int err = 0;
    WIFIDeviceManager *devmgr = (WIFIDeviceManager*)userdata;
    char *t = NULL;
    std::shared_ptr<WIFIDevice> dev = nullptr;

    /* Called whenever a service has been resolved successfully or timed out */
    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            debug("(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
            break;
        case AVAHI_RESOLVER_FOUND: {
            // TODO: inform muxer about devices leaving
            debug("Service '%s' of type '%s' in domain '%s':\n", name, type, domain);
            avahi_address_snprint(addr, sizeof(addr), address);
            t = avahi_string_list_to_string(txt);
            std::string serviceName{name};
            std::string macAddr{serviceName.substr(0,serviceName.find("@"))};
            std::string uuid;
            serviceName += ".";
            serviceName += type;


            std::vector<std::string> addrs{addr};
            bool notifyadd = true;
            if (strstr(serviceName.c_str(), "_remotepairing-manual-pairing._tcp")) {
                uuid = "WIFIPAIR-"+serviceName.substr(0,serviceName.find("."));
                macAddr = {};
                if (devmgr->_mux->have_wifi_device_with_ip(addrs)) goto error;
                notifyadd = false;
            }else{
                try{
                    uuid = sysconf_udid_for_macaddr(macAddr);
                }catch (tihmstar::exception &e){
                    debug("failed to find uuid for mac=%s with error=%d (%s)",macAddr.c_str(),e.code(),e.what());
                    break;
                }

                if (devmgr->_mux->have_wifi_device_with_mac(macAddr)) goto error;
                devmgr->_mux->delete_wifi_pairing_device_with_ip(addrs);
                notifyadd = true;
            }

            {
                std::shared_ptr<WIFIDevice> dev = nullptr;
                try{
                    dev = std::make_shared<WIFIDevice>(devmgr->_mux, devmgr, uuid, addrs, serviceName, interface);
                    devmgr->device_add(dev, notifyadd); dev = NULL;
                } catch (tihmstar::exception &e){
                    creterror("failed to construct device with error=%d (%s)",e.code(),e.what());
                }
            }

            break;
        }
        default:
            error("unknown event=%d",event);
            break;
    }

error:
    avahi_service_resolver_free(r);
    if (t){
        avahi_free(t);
    }
    if (dev){
        dev->kill();
    }
}

#endif //HAVE_WIFI_SUPPORT
