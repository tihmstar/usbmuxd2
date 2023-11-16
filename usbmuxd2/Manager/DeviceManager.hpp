//
//  DeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef DeviceManager_hpp
#define DeviceManager_hpp

#include <libgeneral/Manager.hpp>
#include "Muxer.hpp"

class DeviceManager : public tihmstar::Manager {
protected:
    Muxer *_mux; //not owned
public:
    DeviceManager(Muxer *mux);
    virtual ~DeviceManager();
};
#endif /* DeviceManager_hpp */
