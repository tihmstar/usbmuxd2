//
//  Device.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef Device_hpp
#define Device_hpp

#include <stdint.h>
#include <memory>

class Muxer;
class Client;

class Device{
public:
    enum mux_conn_type{
        MUXCONN_UNAVAILABLE = 0,
        MUXCONN_USB  = 1 << 0,
        MUXCONN_WIFI = 1 << 1
    };
protected:
    Muxer *_mux; //not owned
    mux_conn_type _conntype;
    int _id; //even ID is USB, odd ID is WiFi
    char _serial[256];

public:
    Device(Muxer *mux, mux_conn_type conntype);
    virtual ~Device();

#pragma mark no-provider
    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) = 0;

#pragma mark provider
    virtual void kill() noexcept;
    const char *getSerial() noexcept;
    
    friend Muxer;
};
#endif /* Device_hpp */
