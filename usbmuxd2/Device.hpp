//
//  Device.hpp
//  usbmuxd2
//
//  Created by tihmstar on 08.12.20.
//

#ifndef Device_hpp
#define Device_hpp

#include <atomic>
#include <memory>

class Muxer;
class gref_Muxer;
class Client;

class Device{
public:
    enum mux_conn_type{
        MUXCONN_UNAVAILABLE = 0,
        MUXCONN_USB  = 1 << 0,
        MUXCONN_WIFI = 1 << 1
    };
protected:
    std::shared_ptr<gref_Muxer> _mux;
    mux_conn_type _conntype;
    int _id; //even ID is USB, odd ID is WiFi
    char _serial[256];

public:
    Device(const Device &) =delete; //delete copy constructor
    Device(Device &&o) = delete; //move constructor
    
    Device(std::shared_ptr<gref_Muxer> mux, mux_conn_type conntype);
    virtual ~Device();

    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) = 0;
    virtual void kill() noexcept;

    const char *getSerial() noexcept {return _serial;}
    
    
    friend Muxer;
};

#endif /* Device_hpp */
