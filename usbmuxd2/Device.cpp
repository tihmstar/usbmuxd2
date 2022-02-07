//
//  Device.cpp
//  usbmuxd2
//
//  Created by tihmstar on 08.12.20.
//

#include "Device.hpp"

Device::Device(std::shared_ptr<gref_Muxer> mux, mux_conn_type conntype)
: _mux{mux}, _conntype{conntype}, _id(0), _serial{}
{
    //
}

Device::~Device(){
    //
}

void Device::kill() noexcept{
  //
}
