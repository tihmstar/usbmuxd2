//
//  DeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 07.12.20.
//

#ifndef DeviceManager_hpp
#define DeviceManager_hpp

#include <libgeneral/Manager.hpp>

class gref_Muxer;

class DeviceManager : public tihmstar::Manager {
protected:
    
public:
    std::shared_ptr<gref_Muxer> _mux;
    DeviceManager(std::shared_ptr<gref_Muxer> mux) : _mux(mux) {}
    virtual ~DeviceManager() {}    
};

#endif /* DeviceManager_hpp */
