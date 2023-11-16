//
//  Device.cpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#include "Device.hpp"

#pragma mark Device
Device::Device(Muxer *mux, mux_conn_type conntype)
: _mux(mux)
, _conntype(conntype), _id(0), _serial{}
{
    
}

Device::~Device(){
    //
}

#pragma mark provider
void Device::kill() noexcept{
  //
}

const char *Device::getSerial() noexcept{
    return _serial;
}
