//
//  USBDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef USBDeviceManager_hpp
#define USBDeviceManager_hpp

#include "DeviceManager.hpp"
#include <libgeneral/Event.hpp>
#include <libgeneral/DeliveryEvent.hpp>
#include <libusb.h>
#include <set>
#include <memory>
#include <mutex>

#define INTERFACE_CLASS 255
#define INTERFACE_SUBCLASS 254
#define INTERFACE_PROTOCOL 2

// max transmission packet size
// libusb fragments these too, but doesn't send ZLPs so we're safe
// but we need to send a ZLP ourselves at the end (see usb-linux.c)
// we're using 3 * 16384 to optimize for the fragmentation
// this results in three URBs per full transfer, 32 USB packets each
// if there are ZLP issues this should make them show up easily too
#define USB_MTU (3 * 16384)
#define USB_MRU USB_MTU

#define USB_PACKET_SIZE 512

#define VID_APPLE 0x5ac
#define PID_RANGE_LOW 0x1290
#define PID_RANGE_MAX 0x12af

#define NUM_RX_LOOPS 3

class USBDevice_receiver;
class USBDevice;
class USBDeviceManager : public DeviceManager{
    libusb_context *_ctx;
    libusb_hotplug_callback_handle _usb_hotplug_cb_handle;
    
    std::set<uint16_t> _constructing;
    tihmstar::GuardAccess _constructingGuard;
    std::set<USBDevice *> _children;  //raw ptr to shared objec
    std::mutex _childrenLck;
    tihmstar::Event _childrenEvent;
    std::thread _devReaperThread;
    tihmstar::DeliveryEvent<std::shared_ptr<USBDevice>> _reapDevices;
        
private:
#pragma mark inheritance override
    virtual bool loopEvent() override;
    virtual void stopAction() noexcept override;
    
#pragma mark private members
    void add_constructing(uint8_t bus, uint8_t addr);
    void del_constructing(uint8_t bus, uint8_t addr);
    bool is_constructing(uint8_t bus, uint8_t addr);

    void device_add(libusb_device *dev);

    void reaper_runloop();
    
public:
    USBDeviceManager(Muxer *parent);
    virtual ~USBDeviceManager() override;
    
#pragma mark friends
    friend USBDevice_receiver;
    friend USBDevice;
    friend int usb_hotplug_cb(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) noexcept;
    friend void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept;
    friend void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept;
};

#endif /* USBDeviceManager_hpp */
