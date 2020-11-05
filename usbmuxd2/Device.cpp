//
//  Device.cpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "Device.hpp"
#include "log.h"
#include <libgeneral/macros.h>
#include <algorithm>
#include <thread>
#include <unistd.h>

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
    
        {
        thread_retry:
            try {
                std::thread delthread([this](){
        #ifdef DEBUG
                    debug("killing device (%p) %s",this,_serial);
        #else
                    info("killing device %s",_serial);
        #endif
                    delete this;
                });
                delthread.detach();
            } catch (std::system_error &e) {
                if (e.code() == std::errc::resource_unavailable_try_again) {
                    error("[THREAD] creating thread threw EAGAIN! retrying in 5 seconds...");
                    sleep(5);
                    goto thread_retry;
                }
                error("[THREAD] got unhandled std::system_error %d (%s)",e.code().value(),e.exception::what());
                throw;
            }
        }
        
    }
}
