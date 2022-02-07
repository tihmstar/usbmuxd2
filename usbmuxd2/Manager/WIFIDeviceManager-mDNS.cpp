//
//  WIFIDeviceManager-mDNS.cpp
//  usbmuxd2
//
//  Created by tihmstar on 26.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//


#include <libgeneral/macros.h>

#include "WIFIDeviceManager-mDNS.hpp"
#include "../Devices/WIFIDevice.hpp"
#include "../sysconf/sysconf.hpp"
#include "../Devices/WIFIDevice.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>


#ifdef HAVE_WIFI_MDNS

#pragma mark definitions

#define kDNSServiceInterfaceIndexAny 0
#define kDNSServiceInterfaceIndexLocalOnly ((uint32_t)-1)
#define kDNSServiceInterfaceIndexUnicast   ((uint32_t)-2)
#define kDNSServiceInterfaceIndexP2P       ((uint32_t)-3)
#define kDNSServiceInterfaceIndexBLE       ((uint32_t)-4)


#define kDNSServiceFlagsAdd     0x2
#define LONG_TIME 100000000


#pragma mark callbacks
void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullname, const char *hosttarget, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context)noexcept{

    int err = 0;
    std::shared_ptr<gref_WIFIDeviceManager> devmgr = *(std::shared_ptr<gref_WIFIDeviceManager> *)context;
    std::shared_ptr<WIFIDevice> dev = nullptr;
    struct hostent *he = NULL;
    std::string ipaddr = hosttarget;


    debug("Service '%s' at '%s':\n", fullname, hosttarget);
    std::string serviceName{fullname};
    std::string macAddr{serviceName.substr(0,serviceName.find("@"))};
    std::string uuid;

    try{
        uuid = sysconf_udid_for_macaddr(macAddr);
    }catch (tihmstar::exception &e){
        creterror("failed to find uuid for mac=%s with error=%d (%s)",macAddr.c_str(),e.code(),e.what());
    }

    if (!(*(*devmgr)->_mux)->have_wifi_device(macAddr)) {
        // found new device

        if (inet_addr(ipaddr.c_str()) == 0xffffffff) {
            cretassure(he = gethostbyname(ipaddr.c_str()), "failed to get hostbyname");
            struct in_addr **addr_list = (struct in_addr **) he->h_addr_list;

            for(int i = 0; addr_list[i] != NULL; i++){
                if(const char *ipv4addr_str=inet_ntoa(*addr_list[i])){
                    ipaddr = ipv4addr_str;
                    break;
                }
            }
        }

        try{
            dev = std::make_shared<WIFIDevice>(uuid, ipaddr.c_str(), serviceName, (*devmgr)->_mux);
            (*devmgr)->device_add(dev); dev = NULL;
        } catch (tihmstar::exception &e){
            creterror("failed to construct device with error=%d (%s)",e.code(),e.what());
        }
    }

error:
    if (err) {
        error("resolve_reply failed with error=%d",err);
    }
}


void browse_reply(DNSServiceRef sdref, const DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType errorCode, const char *replyName, const char *replyType, const char *replyDomain, void *context) noexcept{
    int err = 0;
    DNSServiceErrorType res = 0;
    std::shared_ptr<gref_WIFIDeviceManager> devmgr = *(std::shared_ptr<gref_WIFIDeviceManager> *)context;
    DNSServiceRef resolvClient = NULL;
    int resolvfd = -1;

    if (!(flags & kDNSServiceFlagsAdd)) {
        debug("ignoring event=%d. We only care about Add events at the moment",flags);
        return;
    }

    const char *op = (flags & kDNSServiceFlagsAdd) ? "Add" : "Rmv";
        printf("%s %8X %3d %-20s %-20s %s\n",
               op, flags, ifIndex, replyDomain, replyType, replyName);

    cassure(!(res = DNSServiceResolve(&resolvClient, 0, kDNSServiceInterfaceIndexAny, replyName, replyType, replyDomain, resolve_reply, context)));

    cassure((resolvfd = DNSServiceRefSockFD(resolvClient))>0);
    FD_SET(resolvfd, &(*devmgr)->_readfds);

    if (resolvfd>(*devmgr)->_nfds) (*devmgr)->_nfds = resolvfd;

    (*devmgr)->_resolveClients.push_back(resolvClient);

error:
    if (err) {
        error("browse_reply failed with error=%d",err);
    }
}


#pragma mark WIFIDevice

WIFIDeviceManager::WIFIDeviceManager(std::shared_ptr<gref_Muxer> mux)
: DeviceManager(mux), _ref{std::make_shared<gref_WIFIDeviceManager>(this)},_wifi_cb_refarg(nullptr), _client(NULL), _dns_sd_fd(-1), _readfds{}, _nfds(0), _tv{}
{
    int err = 0;
    debug("WIFIDeviceManager mDNS-client");
    assure(_wifi_cb_refarg = new std::shared_ptr<gref_WIFIDeviceManager>(_ref));
    assure(!(err = DNSServiceBrowse(&_client, 0, kDNSServiceInterfaceIndexAny, "_apple-mobdev2._tcp", "", browse_reply, _wifi_cb_refarg)));

    assure((_dns_sd_fd = DNSServiceRefSockFD(_client))>0);

    FD_ZERO(&_readfds);
    FD_SET(_dns_sd_fd, &_readfds);
    _nfds = _dns_sd_fd;

    _tv.tv_sec  = LONG_TIME;
    _tv.tv_usec = 0;
}

WIFIDeviceManager::~WIFIDeviceManager(){
    safeFreeCustom(_client, DNSServiceRefDeallocate);
    safeDelete(_wifi_cb_refarg);
}

void WIFIDeviceManager::device_add(std::shared_ptr<WIFIDevice> dev){
    dev->_selfref = dev;
    (*_mux)->add_device(dev);
}

void WIFIDeviceManager::kill() noexcept{
    debug("[WIFIDeviceManager] killing WIFIDeviceManager");
    stopLoop();
}

void WIFIDeviceManager::loopEvent(){
    int res = 0;
    res = select(_nfds+1, &_readfds, (fd_set*)NULL, (fd_set*)NULL, &_tv);
    if (res > 0){
        std::vector<DNSServiceRef> removeClients;
        cleanup([&]{
            for (auto &rc : removeClients) {
                const auto target = std::remove(_resolveClients.begin(), _resolveClients.end(), rc);
                if (target != _resolveClients.end()){
                    _resolveClients.erase(target, _resolveClients.end());
                    DNSServiceRefDeallocate(*target);
                }
            }
        });
        DNSServiceErrorType err = 0;
        if (FD_ISSET(_dns_sd_fd, &_readfds))
            assure(!(err |= DNSServiceProcessResult(_client)));

        for (auto &rc : _resolveClients) {
            int rcfd = DNSServiceRefSockFD(rc);
            if (rcfd != -1 && FD_ISSET(rcfd, &_readfds)){
                assure(!(err |= DNSServiceProcessResult(rc)));
                FD_CLR(rcfd, &_readfds);
                removeClients.push_back(rc);
            }
        }
    }else if (res != 0){
        reterror("select() returned %d errno %d %s\n", res, errno, strerror(errno));
    }
}

#endif //HAVE_WIFI_MDNS
