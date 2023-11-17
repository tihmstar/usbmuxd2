//
//  WIFIDeviceManager-mDNS.hpp
//  usbmuxd2
//
//  Created by tihmstar on 26.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDeviceManager_mDNS_hpp
#define WIFIDeviceManager_mDNS_hpp

#include "../Muxer.hpp"
#include "DeviceManager.hpp"
#include "../Devices/WIFIDevice.hpp"

#include <libgeneral/DeliveryEvent.hpp>

#include <map>

#include <poll.h>

extern "C"{
    typedef uint32_t DNSServiceFlags;
    typedef uint32_t DNSServiceProtocol;
    typedef int32_t DNSServiceErrorType;
    typedef struct _DNSServiceRef_t *DNSServiceRef;
    
    typedef void (*DNSServiceBrowseReply)(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceErrorType errorCode,
        const char  *serviceName,
        const char  *regtype,
        const char  *replyDomain,
        void        *context
    );

    typedef void (*DNSServiceResolveReply)(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceErrorType errorCode,
        const char  *fullname,
        const char  *hosttarget,
        uint16_t port, /* In network byte order */
        uint16_t txtLen,
        const unsigned char *txtRecord,
        void *context
    );
    typedef void (*DNSServiceGetAddrInfoReply)(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceErrorType errorCode,
        const char *hostname,
        const struct sockaddr *address,
        uint32_t ttl,
        void *context
    );
    
    DNSServiceErrorType DNSServiceBrowse(
        DNSServiceRef          *sdRef,
        DNSServiceFlags        flags,
        uint32_t               interfaceIndex,
        const char             *regtype,
        const char             *domain,    /* may be NULL */
        DNSServiceBrowseReply  callBack,
        void                   *context    /* may be NULL */
    );
    DNSServiceErrorType DNSServiceResolve(
     DNSServiceRef                       *sdRef,
     DNSServiceFlags flags,
     uint32_t interfaceIndex,
     const char                          *name,
     const char                          *regtype,
     const char                          *domain,
     DNSServiceResolveReply callBack,
     void                                *context  /* may be NULL */
    );
    int DNSServiceRefSockFD(DNSServiceRef sdRef);
    DNSServiceErrorType DNSServiceProcessResult(DNSServiceRef sdRef);
    void DNSServiceRefDeallocate(DNSServiceRef sdRef);

    DNSServiceErrorType DNSServiceGetAddrInfo(
        DNSServiceRef *sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceProtocol protocol,
        const char *hostname,
        DNSServiceGetAddrInfoReply callBack,
        void *context
    );
};

class WIFIDeviceManager : public DeviceManager{
private:
    std::set<WIFIDevice *> _children;  //raw ptr to shared objec
    std::mutex _childrenLck;
    tihmstar::Event _childrenEvent;
    std::thread _devReaperThread;
    tihmstar::DeliveryEvent<std::shared_ptr<WIFIDevice>> _reapDevices;
    
    DNSServiceRef _client;
    DNSServiceRef _clientPairing;
    int _dns_sd_fd;
    int _dns_sd_pairing_fd;
    int _wakePipe[2];
    std::vector<DNSServiceRef> _resolveClients;
    std::vector<DNSServiceRef> _removeClients;
    std::vector<struct pollfd> _pfds;
    std::map<DNSServiceRef, DNSServiceRef> _linkedClients;
    std::map<DNSServiceRef, std::vector<std::string>> _clientAddrs;

    virtual bool loopEvent() override;
    virtual void stopAction() noexcept override;

    void reaper_runloop();
public:
    WIFIDeviceManager(Muxer *mux);
    virtual ~WIFIDeviceManager() override;
        
    void device_add(std::shared_ptr<WIFIDevice> dev, bool notify = true);
    
    friend WIFIDevice;
    friend void browse_reply(DNSServiceRef sdref, const DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType errorCode, const char *replyName, const char *replyType, const char *replyDomain, void *context) noexcept;
    friend void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char*fullname, const char*hosttarget, uint16_t port, uint16_t txtLen, const unsigned char*txtRecord, void*context) noexcept;
    friend void getaddr_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *hostname, const struct sockaddr *address, uint32_t ttl, void *context) noexcept;
};



#endif /* WIFIDeviceManager_mDNS_hpp */
