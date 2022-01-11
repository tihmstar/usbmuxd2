//
//  TCP.hpp
//  usbmuxd2
//
//  Created by tihmstar on 30.05.21.
//

#ifndef TCP_hpp
#define TCP_hpp

#include <stdint.h>
#include <memory>
#include "Devices/USBDevice.hpp"
#include "USBDeviceManager.hpp"
#include <libgeneral/Manager.hpp>

class Client;
class TCP : public tihmstar::Manager {
    enum mux_conn_state {
        CONN_CONNECTING,    // SYN
        CONN_CONNECTED,        // SYN/SYNACK/ACK -> active
        CONN_REFUSED,        // RST received during SYN
        CONN_DYING            // RST received
    } _connState;
    struct TCPSenderState {
        uint32_t seq, seqAcked, ack, acked, inWin, win;//131072
    } _stx;
    
    uint16_t _sPort;
    uint16_t _dPort;
    std::weak_ptr<USBDevice> _dev;
    std::shared_ptr<Client> _cli;
    
    int _cfd;
    char *_payloadBuf;

public:
    static constexpr int bufsize = 0x20000;
    static constexpr int TCP_MTU = (USB_MTU-sizeof(tcphdr)-sizeof(USBDevice::mux_header))&0xff00;

    TCP(uint16_t sPort, uint16_t dPort, std::weak_ptr<USBDevice> dev, std::shared_ptr<Client> cli);
    ~TCP();
    void connect();
};

#endif /* TCP_hpp */
