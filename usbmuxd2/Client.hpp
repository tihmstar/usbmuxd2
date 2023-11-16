//
//  Client.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef Client_hpp
#define Client_hpp

#include "usbmuxd2-proto.h"
#include "Manager/ClientManager.hpp"
#include <libgeneral/Manager.hpp>
#include <libgeneral/Event.hpp>
#include <plist/plist.h>
#include <memory>

class Muxer;
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
    Muxer *_mux; //not owned
    ClientManager *_parent; //not owned
    int _fd;
    uint64_t _number;

    char *_recvbuffer;
    size_t _recvBytesCnt;
    uint32_t _proto_version;
    bool _isListening;
    uint32_t _connectTag;
    cinfo _info;
    std::mutex _wlock;

#pragma mark inheritance function
    virtual void stopAction() noexcept override;
    virtual void afterLoop() noexcept override;
    virtual bool loopEvent() override;


#pragma mark private member function
    void update_client_info(const plist_t dict);

    void readData();
    void recv_data();

    void processData(const usbmuxd_header *hdr);

    void writeData(struct usbmuxd_header *hdr, void *buf, size_t buflen);
    void send_pkt(uint32_t tag, usbmuxd_msgtype msg, void *payload, int payload_length);
    void send_plist_pkt(uint32_t tag, plist_t plist);
    void send_result(uint32_t tag, uint32_t result);

public:
    Client(Muxer *mux, ClientManager *parent, int fd, uint64_t number);
    ~Client();

#pragma mark public member function
    void kill() noexcept;
    void deconstruct() noexcept;

    const cinfo &getClientInfo(){return _info;};

#pragma mark friends
    friend class ClientManager;
    friend class Muxer;
    friend class TCP;
};

#endif /* Client_hpp */
