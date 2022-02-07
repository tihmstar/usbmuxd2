//
//  Client.hpp
//  usbmuxd2
//
//  Created by tihmstar on 11.12.20.
//

#ifndef Client_hpp
#define Client_hpp


#include <libgeneral/Manager.hpp>
#include <memory>
#include "usbmuxd2-proto.h"
#include <plist/plist.h>
#include <libgeneral/Event.hpp>

class Muxer;
class gref_Muxer;

/*
 Well a Client is also a Manager, because it manages the connection to the other end of the socket
 */
class Client : public tihmstar::Manager{
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
private: //for lifecycle management only
    std::weak_ptr<Client> _selfref;
private:
    std::shared_ptr<gref_Muxer> _mux;
    int _fd;
    uint64_t _number;

    char *_recvbuffer;
    size_t _recvBytesCnt;
    uint32_t _proto_version;
    bool _isListening;
    uint32_t _connectTag;
    cinfo _info;
    std::mutex _wlock;

    virtual void stopAction() noexcept override;
    virtual void loopEvent() override;
    void update_client_info(const plist_t dict);

    void readData();
    void recv_data();

    void processData(const usbmuxd_header *hdr);

    void writeData(struct usbmuxd_header *hdr, void *buf, size_t buflen);
    void send_pkt(uint32_t tag, usbmuxd_msgtype msg, void *payload, int payload_length);
    void send_plist_pkt(uint32_t tag, plist_t plist);
    void send_result(uint32_t tag, uint32_t result);

public:
    Client(const Client &) =delete; //delete copy constructor
    Client(Client &&o) = delete; //move constructor

    Client(std::shared_ptr<gref_Muxer> mux, int fd, uint64_t number);
    ~Client();

    void kill() noexcept;


    const cinfo &getClientInfo(){return _info;};

    friend class Muxer;
    friend class ClientManager;
    friend class TCP;
    friend class SockConn;
};

#endif /* Client_hpp */
