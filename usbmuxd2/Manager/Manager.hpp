//
//  Manager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 04.07.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef Manager_hpp
#define Manager_hpp

#include <future>

/*
 Abstract class
 */

enum loop_state{
    LOOP_UNINITIALISED = 0,
    LOOP_CONSTRUCTING,
    LOOP_RUNNING,
    LOOP_STOPPING,
    LOOP_STOPPED
};

class Manager{
    std::thread *_loopThread;
    std::mutex _sleepy;
protected:
    std::atomic<loop_state> _loopState;
    
    virtual void loopEvent();
    
public:
    Manager(const Manager&) = delete; //delete copy constructor
    Manager(Manager &&o) = delete; //move constructor
    
    Manager(); //default constructor
    virtual ~Manager();
    
    void startLoop();
    void stopLoop() noexcept;

    virtual void beforeLoop(); //execute before Loop started
    virtual void afterLoop() noexcept; //execute after Loop stopped (e.g. because it died)
    virtual void stopAction() noexcept; //execute when stopping Loop (before waiting for the thread to finish)
};

#endif /* Manager_hpp */
