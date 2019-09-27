//
//  WIFIDeviceManager-mDNS.hpp
//  usbmuxd2
//
//  Created by tihmstar on 26.09.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef WIFIDeviceManager_mDNS_hpp
#define WIFIDeviceManager_mDNS_hpp

#include <Muxer.hpp>
#include <Manager/DeviceManager/DeviceManager.hpp>

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
    const char                          *fullname,
    const char                          *hosttarget,
    uint16_t port,                                   /* In network byte order */
    uint16_t txtLen,
    const unsigned char                 *txtRecord,
    void                                *context
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
};

class WIFIDeviceManager : public DeviceManager{
    DNSServiceRef _client;
    int _dns_sd_fd;
    fd_set _readfds;
    int _nfds;
    struct timeval _tv;
    std::vector<DNSServiceRef> _resolveClients;
    
    virtual void loopEvent() override;
public:
    WIFIDeviceManager(Muxer *mux);
    virtual ~WIFIDeviceManager() override;
    
    
    friend void browse_reply(DNSServiceRef sdref, const DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType errorCode, const char *replyName, const char *replyType, const char *replyDomain, void *context) noexcept;
    friend void resolve_reply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char*fullname, const char*hosttarget, uint16_t port, uint16_t txtLen, const unsigned char*txtRecord, void*context) noexcept;
};



#endif /* WIFIDeviceManager_mDNS_hpp */
