//
//  WIFIDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 15.01.22.
//

#ifndef WIFIDeviceManager_hpp
#define WIFIDeviceManager_hpp

class WIFIDeviceManager;

class gref_WIFIDeviceManager{
    WIFIDeviceManager *_mgr;
public:
    gref_WIFIDeviceManager(WIFIDeviceManager *mgr);
    ~gref_WIFIDeviceManager();

    WIFIDeviceManager *operator->();
};

#endif /* WIFIDeviceManager_hpp */
