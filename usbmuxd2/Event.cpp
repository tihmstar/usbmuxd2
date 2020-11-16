//
//  Event.cpp
//  usbmuxd2
//
//  Created by tihmstar on 15.08.20.
//  Copyright © 2020 tihmstar. All rights reserved.
//

#include <libgeneral/macros.h>
#include "Event.hpp"

Event::Event()
: _members(0), _curSendEvent(0), _curWaitEvent(0), _isDying(false)
{
    
}


Event::~Event(){
    _m.lock();
    _isDying = true;
    _m.unlock();
    
    while (_members) {
        std::unique_lock<std::mutex> lk(_m);
        _cm.wait(lk, [&]{return !_members;});
    }
}

void Event::wait(){
    std::unique_lock<std::mutex> lk(_m);
    ++_members;
    assert(!_isDying);

    uint64_t waitingForEvent = _curWaitEvent+1;
    if (waitingForEvent == 0) waitingForEvent++;

    _cv.wait(lk, [&]{return _curSendEvent>=waitingForEvent;});
    
    _curWaitEvent = _curSendEvent;
    
    --_members;
    _cm.notify_all();
}

void Event::notifyAll(){
    std::unique_lock<std::mutex> *lk = new std::unique_lock<std::mutex>(_m);
    bool doUnlockHere = true;
    cleanup([&]{
        if (doUnlockHere) {
            lk->unlock();
            delete lk;
        }
    });
    assert(!_isDying);
    
    if(++_curSendEvent == 0) ++_curSendEvent;
    _cv.notify_all();
}

uint64_t Event::members() const{
    return _members;
}
