//
//  USBDevice_receiver.hpp
//  usbmuxd2
//
//  Created by tihmstar on 09.09.23.
//

#ifndef USBDevice_receiver_hpp
#define USBDevice_receiver_hpp

#include <libgeneral/Manager.hpp>

class USBDevice;
class USBDevice_receiver : public tihmstar::Manager{
    USBDevice *_parent;

private:
#pragma mark inheritance override
    virtual bool loopEvent() override;

public:
    USBDevice_receiver(USBDevice *parent);
    ~USBDevice_receiver();
};


#endif /* USBDevice_receiver_hpp */
