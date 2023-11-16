//
//  USBDevice_receiver.cpp
//  usbmuxd2
//
//  Created by tihmstar on 09.09.23.
//

#include "USBDevice_receiver.hpp"
#include "USBDevice.hpp"
#include "../Manager/USBDeviceManager.hpp"

USBDevice_receiver::USBDevice_receiver(USBDevice *parent)
: _parent(parent)
{
    startLoop();
}

USBDevice_receiver::~USBDevice_receiver(){
    stopLoop();
}

bool USBDevice_receiver::loopEvent(){
    struct libusb_transfer *xfer = _parent->_arrived_xfer.wait();
    cleanup([&]{
        /*
            Always re-submit transfer and let USBDeviceManager properly delete it in case something went wrong
         */
        libusb_submit_transfer(xfer);
    });
    try {
        _parent->device_data_input(xfer->buffer, xfer->actual_length);
        return true;
    } catch (tihmstar::exception &e) {
        error("failed to device_data_input usbdev=%s error=%s code=%d",_parent->_serial,e.what(),e.code());
        _parent->kill();
        return false;
    }
}
