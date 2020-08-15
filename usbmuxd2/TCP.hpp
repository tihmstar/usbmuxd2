//
//  TCP.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef TCP_hpp
#define TCP_hpp

#include <Manager/Manager.hpp>
#include <stdint.h>
#include <Manager/DeviceManager/USBDeviceManager.hpp>
#include <netinet/tcp.h>
#include <Devices/USBDevice.hpp>
#include <Event.hpp>

class Client;
class TCP : Manager {
    enum mux_conn_state {
        CONN_CONNECTING,    // SYN
        CONN_CONNECTED,        // SYN/SYNACK/ACK -> active
        CONN_REFUSED,        // RST received during SYN
        CONN_DYING            // RST received
    } _connState;
    struct TCPSenderState {
        uint32_t seq, seqAcked, ack, acked, inWin, win;//131072
    } _stx;

    Client *_cli; //unmanaged
    USBDevice* _device; // lifetime of this object is NOT managed by this class!
    char *_payloadBuf;
    std::atomic_bool _killInProcess;
    std::atomic_bool _didConnect;
    std::atomic_uint32_t _refCnt;
    std::mutex _lockStx;
    Event _lockCanSend; //puts threads to sleep if we can't send data
    uint16_t _sPort; //unmanaged
    uint16_t _dPort;
    struct pollfd *_pfds;  //socket lifetime IS managed by this class

    virtual void loopEvent() override;
    void send_tcp(std::uint8_t flags);
    void send_ack();

    void send_data(void *buf, size_t len);

    ~TCP();
public:
    static constexpr int bufsize = 0x20000;
    static constexpr int TCP_MTU = (USB_MTU-sizeof(tcphdr)-sizeof(USBDevice::mux_header))&0xff00;

    TCP(uint16_t sPort, uint16_t dPort, USBDevice *dev, Client *cli);
    TCP(const TCP &) = delete;
    TCP(TCP &&o) = delete;

    void connect();
    void handle_input(tcphdr* tcp_header, uint8_t* payload, uint32_t payload_len);

    void retain() noexcept {++_refCnt;};
    void release() noexcept {--_refCnt;};


    void kill() noexcept;

    static void send_RST(USBDevice *dev, tcphdr *hdr);
};

#endif /* TCP_hpp */
