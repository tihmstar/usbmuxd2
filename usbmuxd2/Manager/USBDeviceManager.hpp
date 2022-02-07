//
//  USBDeviceManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 07.12.20.
//

#ifndef USBDeviceManager_hpp
#define USBDeviceManager_hpp

#include <libgeneral/Event.hpp>
#include <libusb-1.0/libusb.h>
#include "DeviceManager.hpp"
#include <libgeneral/lck_container.hpp>
#include <set>
#include <vector>

#pragma mark USBDefines

#define INTERFACE_CLASS 255
#define INTERFACE_SUBCLASS 254
#define INTERFACE_PROTOCOL 2

// libusb fragments packets larger than this (usbfs limitation)
// on input, this creates race conditions and other issues
#define USB_MRU 16384

// max transmission packet size
// libusb fragments these too, but doesn't send ZLPs so we're safe
// but we need to send a ZLP ourselves at the end (see usb-linux.c)
// we're using 3 * 16384 to optimize for the fragmentation
// this results in three URBs per full transfer, 32 USB packets each
// if there are ZLP issues this should make them show up easily too
#define USB_MTU (3 * 16384)

#define USB_PACKET_SIZE 512

#define VID_APPLE 0x5ac
#define PID_RANGE_LOW 0x1290
#define PID_RANGE_MAX 0x12af

#define NUM_RX_LOOPS 3

class USBDeviceManager;

class gref_USBDeviceManager{
    USBDeviceManager *_mgr;
public:
    gref_USBDeviceManager(USBDeviceManager *mgr);
    ~gref_USBDeviceManager();

    USBDeviceManager *operator->();
};

class USBDevice;
class USBDeviceManager : public DeviceManager {
private: //for lifecycle management only
    tihmstar::Event _finalUnrefEvent;
    std::shared_ptr<gref_USBDeviceManager> _ref;
#ifdef DEBUG
    std::weak_ptr<gref_USBDeviceManager> __debug_ref;
    std::vector<std::weak_ptr<USBDevice>> __debug_devices;
#endif

private: //instance variables
    std::atomic<bool> _isDying;
    libusb_hotplug_callback_handle _usb_hotplug_cb_handle;
    std::shared_ptr<gref_USBDeviceManager> *_usb_hotplug_cb_refarg;
    tihmstar::lck_contrainer<std::set<uint16_t>> _constructing;

private: //class inheritance function overrides
    virtual void loopEvent() override;
    virtual void stopAction() noexcept override;


private: //private member functions
    void add_constructing(uint8_t bus, uint8_t addr);
    void del_constructing(uint8_t bus, uint8_t addr);
    bool is_constructing(uint8_t bus, uint8_t addr);

    void device_add(libusb_device *dev);


public:
    USBDeviceManager(std::shared_ptr<gref_Muxer> mux);
    virtual ~USBDeviceManager() override;

    void kill() noexcept;

    friend gref_USBDeviceManager;
    friend int usb_hotplug_cb(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) noexcept;
    friend void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept;
    friend void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept;
};


#endif /* USBDeviceManager_hpp */
