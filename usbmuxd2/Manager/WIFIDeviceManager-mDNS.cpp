//
//  WIFIDeviceManager-mDNS.cpp
//  usbmuxd2
//
//  Created by tihmstar on 26.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//


#include <libgeneral/macros.h>

#ifdef HAVE_WIFI_MDNS
#include "WIFIDeviceManager-mDNS.hpp"
#include "../Devices/WIFIDevice.hpp"
#include "../sysconf/sysconf.hpp"
#include "../Devices/WIFIDevice.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>

#pragma mark definitions

#define kDNSServiceInterfaceIndexAny 0
#define kDNSServiceInterfaceIndexLocalOnly ((uint32_t)-1)
#define kDNSServiceInterfaceIndexUnicast   ((uint32_t)-2)
#define kDNSServiceInterfaceIndexP2P       ((uint32_t)-3)
#define kDNSServiceInterfaceIndexBLE       ((uint32_t)-4)


#define kDNSServiceFlagsMoreComing 0x1
#define kDNSServiceFlagsAdd     0x2
#define LONG_TIME 100000000

#define kDNSServiceProtocol_IPv4 0x01
#define kDNSServiceProtocol_IPv6 0x02


#pragma mark callbacks
void getaddr_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *hostname, const struct sockaddr *address, uint32_t ttl, void *context) noexcept{
    int err = 0;
    WIFIDeviceManager *devmgr = (WIFIDeviceManager *)context;
    
    std::vector<std::string> &addrs = devmgr->_clientAddrs[sdRef];

    std::string ipaddr;
    ipaddr.resize(INET6_ADDRSTRLEN+1);
    if (address->sa_family == AF_INET6) {
        ipaddr = inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)address)->sin6_addr), ipaddr.data(), (socklen_t)ipaddr.size());
    }else{
        ipaddr = inet_ntop(AF_INET, &(((struct sockaddr_in *)address)->sin_addr), ipaddr.data(), (socklen_t)ipaddr.size());
    }
    addrs.push_back(ipaddr);


    if (!(flags & kDNSServiceFlagsMoreComing)) {
        bool notifyadd = true;
        std::string serviceName = addrs.front();
        addrs.erase(addrs.begin());
        
        std::string macAddr{serviceName.substr(0,serviceName.find("@"))};
        std::string uuid;
        if (strstr(serviceName.c_str(), "_remotepairing-manual-pairing._tcp")) {
            uuid = "WIFIPAIR-"+serviceName.substr(0,serviceName.find("."));
            macAddr = {};
            if (devmgr->_mux->have_wifi_device_with_ip(addrs)) goto error;
            notifyadd = false;
        }else{
            try{
                uuid = sysconf_udid_for_macaddr(macAddr);
            }catch (tihmstar::exception &e){
                creterror("failed to find uuid for mac=%s with error=%d (%s)",macAddr.c_str(),e.code(),e.what());
            }
            if (devmgr->_mux->have_wifi_device_with_mac(macAddr)) goto error;
            devmgr->_mux->delete_wifi_pairing_device_with_ip(addrs);
            notifyadd = true;
        }
    
        {
            std::shared_ptr<WIFIDevice> dev = nullptr;
            try{
                dev = std::make_shared<WIFIDevice>(devmgr->_mux, devmgr, uuid, addrs, serviceName, interfaceIndex);
                devmgr->device_add(dev, notifyadd); dev = NULL;
            } catch (tihmstar::exception &e){
                creterror("failed to construct device with error=%d (%s)",e.code(),e.what());
            }
        }
    }
    
error:
    if (!(flags & kDNSServiceFlagsMoreComing)) {
        devmgr->_clientAddrs.erase(sdRef);
        DNSServiceRef sdResolv = devmgr->_linkedClients[sdRef];
        devmgr->_linkedClients.erase(sdRef);
        devmgr->_removeClients.push_back(sdRef); //idk why, but order is important!
        devmgr->_removeClients.push_back(sdResolv);
    }
    if (err) {
        error("getaddr_reply failed with error=%d",err);
    }
}

void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullname, const char *hosttarget, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context) noexcept{
    int err = 0;
    WIFIDeviceManager *devmgr = (WIFIDeviceManager *)context;
    DNSServiceErrorType res = 0;
    DNSServiceRef resolvClient = NULL;
    int resolvfd = -1;

    cassure(!(res = DNSServiceGetAddrInfo(&resolvClient, 0, kDNSServiceInterfaceIndexAny, kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, hosttarget, getaddr_reply, context)));
    
    cassure((resolvfd = DNSServiceRefSockFD(resolvClient))>0);
    devmgr->_pfds.push_back({
        .fd = resolvfd,
        .events = POLLIN
    });

    devmgr->_clientAddrs[resolvClient] = {fullname};
    
    devmgr->_resolveClients.push_back(resolvClient);
    devmgr->_linkedClients[resolvClient] = sdRef;
    
error:
    if (err) {
        error("resolve_reply failed with error=%d",err);
    }
}

void browse_reply(DNSServiceRef sdref, const DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType errorCode, const char *replyName, const char *replyType, const char *replyDomain, void *context) noexcept{
    int err = 0;
    DNSServiceErrorType res = 0;
    WIFIDeviceManager *devmgr = (WIFIDeviceManager *)context;
    DNSServiceRef resolvClient = NULL;
    int resolvfd = -1;

    if (!(flags & kDNSServiceFlagsAdd)) {
//        debug("ignoring event=%d. We only care about Add events at the moment",flags);
        return;
    }
    
    const char *op = (flags & kDNSServiceFlagsAdd) ? "Add" : "Rmv";
    debug("%s %8X %3d %-20s %-20s %s",
           op, flags, ifIndex, replyDomain, replyType, replyName);
    
    cassure(!(res = DNSServiceResolve(&resolvClient, 0, kDNSServiceInterfaceIndexAny, replyName, replyType, replyDomain, resolve_reply, context)));

    cassure((resolvfd = DNSServiceRefSockFD(resolvClient))>0);
    devmgr->_pfds.push_back({
        .fd = resolvfd,
        .events = POLLIN
    });

error:
    if (resolvClient){
        devmgr->_resolveClients.push_back(resolvClient);
    }
    if (err) {
        error("browse_reply failed with error=%d",err);
    }
}


#pragma mark WIFIDevice

WIFIDeviceManager::WIFIDeviceManager(Muxer *mux)
: DeviceManager(mux), _client(NULL), _clientPairing(NULL), _dns_sd_fd(-1), _dns_sd_pairing_fd(-1), _wakePipe{}
{
    int err = 0;
    debug("WIFIDeviceManager mDNS-client");
    assure(!(err = DNSServiceBrowse(&_client, 0, kDNSServiceInterfaceIndexAny, "_apple-mobdev2._tcp", "", browse_reply, this)));
    assure(!(err = DNSServiceBrowse(&_clientPairing, 0, kDNSServiceInterfaceIndexAny, "_remotepairing-manual-pairing._tcp", "", browse_reply, this)));

    assure((_dns_sd_fd = DNSServiceRefSockFD(_client))>0);
    _pfds.push_back({.fd = _dns_sd_fd, .events = POLLIN});

    assure((_dns_sd_pairing_fd = DNSServiceRefSockFD(_clientPairing))>0);
    _pfds.push_back({.fd = _dns_sd_pairing_fd, .events = POLLIN});

    assure(!pipe(_wakePipe));
    _pfds.push_back({.fd = _wakePipe[0], .events = POLLIN});
    
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
    {
        for (auto rc : _resolveClients) 
            safeFreeCustom(rc, DNSServiceRefDeallocate);
        _resolveClients.clear();
    }
    safeFreeCustom(_client, DNSServiceRefDeallocate);
    safeFreeCustom(_clientPairing, DNSServiceRefDeallocate);
    safeClose(_wakePipe[0]);
    safeClose(_wakePipe[1]);
}

void WIFIDeviceManager::device_add(std::shared_ptr<WIFIDevice> dev, bool notify){
    dev->_selfref = dev;
    _children.insert(dev.get());
    _mux->add_device(dev, notify);
}

bool WIFIDeviceManager::loopEvent(){
    int res = 0;
    res = poll(_pfds.data(), (int)_pfds.size(), -1);
    if (res > 0){
        cleanup([&]{
            for (auto &rc : _removeClients) {
                const auto target = std::remove(_resolveClients.begin(), _resolveClients.end(), rc);
                if (target != _resolveClients.end()){
                    DNSServiceRef tgt = *target;
                    _resolveClients.erase(target, _resolveClients.end());
                    DNSServiceRefDeallocate(tgt);
                }
            }
            _removeClients.clear();
            _pfds.clear();
            {
                _pfds.push_back({.fd = _dns_sd_fd, .events = POLLIN});
                _pfds.push_back({.fd = _dns_sd_pairing_fd, .events = POLLIN});
                _pfds.push_back({.fd = _wakePipe[0], .events = POLLIN});
            }
            for (auto c : _resolveClients) {
                int cfd = DNSServiceRefSockFD(c);
                if (cfd != -1){
                    _pfds.push_back({.fd = cfd, .events = POLLIN});
                }else{
                    _removeClients.push_back(c);
                }
            }
        });
        DNSServiceErrorType err = 0;
        auto cpy_pfds = _pfds;
        for (auto pfd : cpy_pfds) {
            if (pfd.revents & POLLIN) {
                pfd.revents &= ~POLLIN;
                if (pfd.fd == DNSServiceRefSockFD(_client)) {
                    assure(!(err |= DNSServiceProcessResult(_client)));
                }else if (pfd.fd == DNSServiceRefSockFD(_clientPairing)) {
                    assure(!(err |= DNSServiceProcessResult(_clientPairing)));
                }else{
                    for (auto rc : _resolveClients) {
                        int rcfd = DNSServiceRefSockFD(rc);
                        if (rcfd == pfd.fd) {
                            assure(!(err |= DNSServiceProcessResult(rc)));
                            break;
                        }
                    }
                }
            }
        }
    }else if (res != 0){
        reterror("poll() returned %d errno %d %s\n", res, errno, strerror(errno));
    }
    return true;
}

void WIFIDeviceManager::stopAction() noexcept{
    safeClose(_wakePipe[1]);
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

#endif //HAVE_WIFI_MDNS
