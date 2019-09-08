//
//  DeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef DeviceManager_hpp
#define DeviceManager_hpp

#include <Manager/Manager.hpp>

class Muxer;

class DeviceManager : public Manager{
protected:
    Muxer *_mux;
public:
    DeviceManager(Muxer *mux) : _mux(mux) {}
    
    virtual ~DeviceManager() {}
};


#endif /* DeviceManager_hpp */
