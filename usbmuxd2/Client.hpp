//
//  Client.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef Client_hpp
#define Client_hpp

#include <Manager/Manager.hpp>
#include <Muxer.hpp>
#include <usbmuxd2-proto.h>
#include <plist/plist.h>
#include <functional>

/*
 Well a Client is also a Manager, because it manages the connection to the other end of the socket
 */
class Client : public Manager{
public:
    static constexpr int bufsize = 0x20000;
    struct cinfo{
        char *bundleID;
        char *clientVersionString;
        char *progName;
        uint64_t kLibUSBMuxVersion;
    };
    enum state {
        CLIENT_COMMAND,        // waiting for command
        CLIENT_LISTEN,         // listening for devices
        CLIENT_CONNECTED      // connected
    };
private:
    Muxer *_muxer; // unmanaged
    usbmuxd_header *hdr; //unmanaged
    char *_recvbuffer;
    cinfo _info;
    std::atomic_bool _killInProcess;
    pthread_mutex_t _wlock;
    size_t _recvbufferSize;
    uint64_t _number;
    int _fd;
    uint32_t _proto_version;
    bool _isListening;
    
    virtual void loopEvent() override;
    void update_client_info(const plist_t dict);
    
    void readData();
    void recv_data();
    
    void processData(usbmuxd_header *hdr);
    
    void writeData(struct usbmuxd_header *hdr, void *buf, size_t buflen);
    void send_pkt(uint32_t tag, usbmuxd_msgtype msg, void *payload, int payload_length);
    void send_plist_pkt(uint32_t tag, plist_t plist);
    void send_result(uint32_t tag, uint32_t result);
    
    ~Client();
public:
    Client(const Client &) =delete; //delete copy constructor
    Client(Client &&o) = delete; //move constructor
    
    Client(Muxer *mux, int fd, uint64_t number);
    void kill() noexcept;
    
    const cinfo &getClientInfo(){return _info;};
    friend class Muxer;
    friend class TCP;
    friend class SockConn;
};

#endif /* Client_hpp */
