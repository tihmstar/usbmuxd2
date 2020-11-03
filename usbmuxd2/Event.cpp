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
: _members(0), _curEvent(0), _isDying(false)
{
    
}


Event::~Event(){
    std::unique_lock<std::mutex> dm(_m);
    _isDying = true;
    dm.unlock();
    while (_members) {
        std::unique_lock<std::mutex> lk(_m);
        
        _cm.wait(lk, [&]{return !_members;});
        
        lk.unlock();
    }
}

void Event::wait(){
    std::unique_lock<std::mutex> lk(_m);
    cleanup([&]{
        lk.unlock();
    });
    assure(!_isDying);
    
    ++_members;
    
    uint64_t waitingForEvent = _curEvent+1;
    if (waitingForEvent == 0) waitingForEvent++;
    _cm.notify_all();
    _cv.wait(lk, [&]{return _curEvent>=waitingForEvent;});
    
    --_members;
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
    assure(!_isDying);
    
    if (_members == 0) {
//        //if nobody is listening atm, delay the notification to avoid race locks
//        std::thread bg([this](std::unique_lock<std::mutex> *clk){
//            ++_members;
//            _cm.wait(*clk, [this]{return _members > 1;});
//            --_members;
//            
//            if(++_curEvent == 0) ++_curEvent;
//            _cv.notify_all();
//
//            clk->unlock();            
//            delete clk;
//        },lk);
//        doUnlockHere = false;
//        bg.detach();
    }else{
        if(++_curEvent == 0) ++_curEvent;
        _cv.notify_all();
    }
}

uint64_t Event::members() const{
    return _members;
}
