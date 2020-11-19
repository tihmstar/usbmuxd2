//
//  Device.cpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "Device.hpp"
#include <libgeneral/macros.h>
#include <algorithm>
#include <thread>
#include <unistd.h>
#include <system_error>

Device::Device(Muxer *mux, mux_conn_type conntype)
    : _muxer(mux), _conntype(conntype), _killInProcess(false), _id(0), _serial{}
{
    //
}

Device::~Device(){
#ifdef DEBUG
    if (!_killInProcess) {
        error("THIS DESTRUCTOR IS NOT MEANT TO BE CALLED OTHER THAN THROUGH kill()!!");
    }
#endif
    assert(_killInProcess);
}

void Device::kill() noexcept{
    //sets _killInProcess to true and executes if statement if it was false before
    if (!_killInProcess.exchange(true)){
    
        std::thread delthread([this](){
#ifdef DEBUG
            debug("killing device (%p) %s",this,_serial);
#else
            info("killing device %s",_serial);
#endif
            delete this;
        });
        delthread.detach();
        
    }
}
