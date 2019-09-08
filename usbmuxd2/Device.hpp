//
//  Device.hpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef Device_hpp
#define Device_hpp

#include <atomic>

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
    Muxer *_muxer; //unmanaged
    
    mux_conn_type _conntype;
    std::atomic_bool _killInProcess;
    int _id; //even ID is USB, odd ID is WiFi
    char _serial[256];

    virtual ~Device();
public:
    Device(const Device &) =delete; //delete copy constructor
    Device(Device &&o) = delete; //move constructor
    
    Device(Muxer *mux, mux_conn_type conntype);
    
    virtual void start_connect(uint16_t dport, Client *cli) = 0;

    const char *getSerial() noexcept {return _serial;}
    void kill() noexcept;
    
    friend Muxer;
};

#endif /* Device_hpp */
