//
//  USBDeviceManager.cpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#include "USBDeviceManager.hpp"
#include "../Devices/USBDevice.hpp"

#include <unistd.h>
#include <string.h>

#pragma mark libusb_callback definitions
int usb_hotplug_cb(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) noexcept;
void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept;
void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept;
void rx_callback(struct libusb_transfer *xfer) noexcept;
void usb_start_rx_loop(std::shared_ptr<USBDevice> dev);

#pragma mark libusb_callback implementations

int usb_hotplug_cb(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) noexcept{
    int err = 0;
    USBDeviceManager *devmgr = (USBDeviceManager*)user_data;
    
    switch (event) {
        case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
            try {
                debug("Adding device");
                devmgr->device_add(device);
            } catch (tihmstar::exception &e) {
                uint8_t bus = libusb_get_bus_number(device);
                uint8_t address = libusb_get_device_address(device);
                creterror("failed to add device on bus=0x%02x address=0x%02x error=%s code=%d",bus,address,e.what(),e.code());
            }
            break;
        case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
        {
            uint8_t bus = libusb_get_bus_number(device);
            uint8_t address = libusb_get_device_address(device);
            devmgr->_mux->delete_device(bus, address);
        }
            break;
        default:
            error("Unhandled event %d", event);
            break;
    }
    
error:
    return 0;
}

void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept{
    std::shared_ptr<USBDevice> usbdev = *(std::shared_ptr<USBDevice> *)transfer->user_data;
    
    try {
        uint16_t langid = 0;
        const unsigned char *data = NULL;
        int ret = 0;

        retassure(usbdev != nullptr, "USBDevice was dead when usb_get_langid_callback fired");

        transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
        retassure(transfer->status == LIBUSB_TRANSFER_COMPLETED, "Failed to request lang ID for device %d-%d (%i)", usbdev->_bus, usbdev->_address, transfer->status);

        data = libusb_control_transfer_get_data(transfer); //error-free function
        langid = (uint16_t)(data[2] | (data[3] << 8));
        info("Got lang ID %u for device %d-%d", langid, usbdev->_bus, usbdev->_address);

        /* re-use the same transfer */
        libusb_fill_control_setup(transfer->buffer, LIBUSB_ENDPOINT_IN, LIBUSB_REQUEST_GET_DESCRIPTOR,
                                  (uint16_t)((LIBUSB_DT_STRING << 8) | usbdev->_devdesc.iSerialNumber),
                                  langid, 1024 + LIBUSB_CONTROL_SETUP_SIZE);

        libusb_fill_control_transfer(transfer, usbdev->_usbdev, transfer->buffer, usb_get_serial_callback, transfer->user_data, 1000);

        retassure((ret = libusb_submit_transfer(transfer)) >= 0, "Could not request transfer for device %d-%d (%d)", usbdev->_bus, usbdev->_address, ret);
    } catch (tihmstar::exception &e) {
        error("[usb_get_langid_callback] Failed with error=%d (%s)",e.code(),e.what());
        e.dump();

        usbdev->_parent->del_constructing(usbdev->_bus, usbdev->_address);
        //at this point we are the only owner of usbdev (the floating shared_ptr to it)
        //if anything goes wrong, this is the last chance to delete the object without leaking it
        {
            std::shared_ptr<USBDevice> *floating_usbdev = (std::shared_ptr<USBDevice> *)transfer->user_data;transfer->user_data = NULL;
            delete floating_usbdev; floating_usbdev = NULL;
        }

        safeFreeCustom(transfer, libusb_free_transfer);
    }
}

void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept{
    std::shared_ptr<USBDevice> usbdev = *(std::shared_ptr<USBDevice> *)transfer->user_data;
    try {
        const unsigned char *data = NULL;

        retassure(usbdev != nullptr, "USBDevice was dead when usb_get_serial_callback fired");

        retassure(transfer->status == LIBUSB_TRANSFER_COMPLETED, "Failed to request serial for device %d-%d (%i)", usbdev->_bus, usbdev->_address, transfer->status);

        /* De-unicode, taken from libusb */
        assure(data = libusb_control_transfer_get_data(transfer));

        {
            unsigned int di = 0, si = 2;
            for (; si < data[0] && di < sizeof(usbdev->_serial)-1; si += 2) {
                if ((data[si] & 0x80) || (data[si + 1])) /* non-ASCII */
                    usbdev->_serial[di++] = '?';
                else if (data[si] == '\0')
                    break;
                else
                    usbdev->_serial[di++] = data[si];
            }
            //should already be zero terminated at the correct offset (hopefully)
            //just doing sanity zero termination
            usbdev->_serial[sizeof(usbdev->_serial)-1] = 0;

            /* new style UDID: add hyphen between first 8 and following 16 digits */
            if (di == 24) {
                memmove(&usbdev->_serial[9], &usbdev->_serial[8], 16);
                usbdev->_serial[8] = '-';
                usbdev->_serial[di+1] = '\0';
            }
        }

        info("Got serial '%s' for device %d-%d", usbdev->_serial, usbdev->_bus, usbdev->_address);

        // Spin up NUM_RX_LOOPS parallel usb data retrieval loops
        // Old usbmuxds used only 1 rx loop, but that leaves the
        // USB port sleeping most of the time
        {
            int rx_loops = 0;
            for (; rx_loops < NUM_RX_LOOPS; rx_loops++) {
                try {
                    usb_start_rx_loop(usbdev);
                } catch (tihmstar::exception &e) {
                    warning("Failed to start RX loop number %d", NUM_RX_LOOPS - rx_loops);
                }
            }
            // Ensure we have at least 1 RX loop going
            retassure(rx_loops, "Failed to start any RX loop for device %d-%d", usbdev->_bus, usbdev->_address);
            if (rx_loops != NUM_RX_LOOPS) {
                warning("Failed to start all %d RX loops. Going on with %d loops. This may have negative impact on device read speed.", NUM_RX_LOOPS, rx_loops);
            } else {
                debug("All %d RX loops started successfully", NUM_RX_LOOPS);
            }
        }

        usbdev->mux_init();

    } catch (tihmstar::exception &e) {
        error("[usb_get_serial_callback] Failed with error=%d (%s)",e.code(),e.what());
        e.dump();
    }
    usbdev->_parent->del_constructing(usbdev->_bus, usbdev->_address); //device is either done or failed at this point
    
    //at this point we are the only owner of usbdev (the floating shared_ptr to it)
    //if anything goes wrong, this is the last chance to delete the object without leaking it
    {
        std::shared_ptr<USBDevice> *floating_usbdev = (std::shared_ptr<USBDevice> *)transfer->user_data;transfer->user_data = NULL;
        delete floating_usbdev; floating_usbdev = NULL;
    }
    safeFreeCustom(transfer, libusb_free_transfer);
}

// Start a read-callback loop for this device
void usb_start_rx_loop(std::shared_ptr<USBDevice> dev){
    void *buf = NULL;
    struct libusb_transfer *xfer = NULL;
    std::shared_ptr<USBDevice> *devrefarg = nullptr;
    cleanup([&](){ //cleanup only code
        safeDelete(devrefarg);
        safeFree(buf);
        if (xfer) {
            {
                guardWrite(dev->_rx_xfers_Guard);
                dev->_rx_xfers.erase(xfer);
            }
            safeFree(xfer->buffer);
            {
                std::shared_ptr<USBDevice>* ud = (std::shared_ptr<USBDevice>*)xfer->user_data;xfer->user_data = NULL;
                safeDelete(ud);
            }
            libusb_free_transfer(xfer);
        }
    });
    int ret = 0;

    assure(buf = malloc(USB_MRU));
    assure(xfer = libusb_alloc_transfer(0));
    xfer->user_data = NULL;

    devrefarg = new std::shared_ptr<USBDevice>{dev};
    libusb_fill_bulk_transfer(xfer, dev->_usbdev, dev->_ep_in, (unsigned char *)buf, USB_MRU, rx_callback, devrefarg, 0);
    buf = NULL; //owned by xfer now
    devrefarg = nullptr; //owned by xfer now

    {
        guardWrite(dev->_rx_xfers_Guard);
        dev->_rx_xfers.insert(xfer); //transfer ownsership of transfer to device
    }
    retassure(!((ret = libusb_submit_transfer(xfer)),ret),"Failed to submit RX transfer to device %d-%d: %d", dev->_bus, dev->_address, ret);
    xfer = NULL;
    dev->addReceiver();
}

void rx_callback(struct libusb_transfer *xfer) noexcept{
    std::shared_ptr<USBDevice> dev = *(std::shared_ptr<USBDevice> *)xfer->user_data;
//    debug("RX callback dev %d-%d len %d status %d", dev->_bus, dev->_address, xfer->actual_length, xfer->status);
    if(xfer->status == LIBUSB_TRANSFER_COMPLETED) {
        dev->_arrived_xfer.post(xfer);
        return;
    }
    switch(xfer->status) {
        case LIBUSB_TRANSFER_ERROR:
            // funny, this happens when we disconnect the device while waiting for a transfer, sometimes
            info("Device %d-%d RX aborted due to error or disconnect", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_TIMED_OUT:
            error("RX transfer timed out for device %d-%d", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_CANCELLED:
            debug("Device %d-%d RX transfer cancelled", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_STALL:
            error("RX transfer stalled for device %d-%d", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_NO_DEVICE:
            // other times, this happens, and also even when we abort the transfer after device removal
            info("Device %d-%d RX aborted due to disconnect", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_OVERFLOW:
            error("RX transfer overflow for device %d-%d", dev->_bus, dev->_address);
            break;
        case LIBUSB_TRANSFER_COMPLETED: //shut up compiler
        default:
            // this should never be reached.
            break;
    }
error:
    //remove transfer
    {
        guardWrite(dev->_rx_xfers_Guard);
        dev->_rx_xfers.erase(xfer);
    }

    debug("freing rx xfer for USBDevice(%s)",dev->_serial);
    safeFree(xfer->buffer);
    {
        std::shared_ptr<USBDevice> *cbargref = (std::shared_ptr<USBDevice> *)xfer->user_data; xfer->user_data = NULL;
        safeDelete(cbargref);
    }
    libusb_free_transfer(xfer);

    dev->kill();
}

#pragma mark USBDeviceManager
USBDeviceManager::USBDeviceManager(Muxer *parent)
: DeviceManager(parent)
, _ctx(NULL), _usb_hotplug_cb_handle(0)
{
    bool didInit = false;
    cleanup([&]{
        if (!didInit) this->~USBDeviceManager();
    });
    int err = 0;
    info("USBDeviceManager libusb 1.0");
    retassure(libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG), "libusb does not support hotplug events");

    assure(!libusb_init(&_ctx));
    info("Registering for libusb hotplug events");

    retassure(!(err = libusb_hotplug_register_callback(NULL, static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT), LIBUSB_HOTPLUG_ENUMERATE, VID_APPLE, LIBUSB_HOTPLUG_MATCH_ANY, 0, usb_hotplug_cb, this, &_usb_hotplug_cb_handle)),"ERROR: Could not register for libusb hotplug events (%d)", err);
    didInit = true;
    _devReaperThread = std::thread([this]{
        reaper_runloop();
    });
}

USBDeviceManager::~USBDeviceManager(){
    if (_usb_hotplug_cb_handle) {
        libusb_hotplug_deregister_callback(_ctx, _usb_hotplug_cb_handle); _usb_hotplug_cb_handle = 0;
    }
    if (_children.size()) {
        debug("waiting for usb children to die...");
        std::unique_lock<std::mutex> ul(_childrenLck);
        while (size_t s = _children.size()) {
            for (auto c : _children) c->kill();
            uint64_t wevent = _childrenEvent.getNextEvent();
            ul.unlock();
            debug("Need to kill %zu more usb children",s);
            _childrenEvent.waitForEvent(wevent);
            ul.lock();
        }
    }
    _reapDevices.kill();
    _devReaperThread.join();

    stopLoop();
    safeFreeCustom(_ctx, libusb_exit);
}

#pragma mark inheritance override
bool USBDeviceManager::loopEvent(){
    int err = 0;
    retassure(!(err = libusb_handle_events(_ctx)), "libusb_handle_events_completed failed: %d", err);
    return true;
}

void USBDeviceManager::stopAction() noexcept{
    int err = 0;
    /*
        re-registering the event handler, triggers an event
     */
    if ((err = libusb_hotplug_register_callback(NULL, static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT), LIBUSB_HOTPLUG_ENUMERATE, VID_APPLE, LIBUSB_HOTPLUG_MATCH_ANY, 0, usb_hotplug_cb, this, &_usb_hotplug_cb_handle))){
        error("Could not re-register for libusb hotplug events (%d)", err);
    }
    libusb_hotplug_deregister_callback(_ctx, _usb_hotplug_cb_handle);
    _usb_hotplug_cb_handle = 0;
}

#pragma mark private members
void USBDeviceManager::add_constructing(uint8_t bus, uint8_t addr){
    uint16_t dev = (uint16_t)(bus<<16) | addr;
    {
        guardWrite(_constructingGuard);
        _constructing.insert(dev);
    }
}

void USBDeviceManager::del_constructing(uint8_t bus, uint8_t addr){
    uint16_t dev = (uint16_t)(bus<<16) | addr;
    {
        guardWrite(_constructingGuard);
        _constructing.erase(dev);
    }
}

bool USBDeviceManager::is_constructing(uint8_t bus, uint8_t addr){
    bool ret = false;
    uint16_t dev = (uint16_t)(bus<<16) | addr;
    {
        guardRead(_constructingGuard);
        ret = _constructing.find(dev) != _constructing.end();
    }
    return ret;
}

void USBDeviceManager::device_add(libusb_device *dev){
    libusb_device_handle *handle = NULL;
    struct libusb_config_descriptor *config = NULL;
    std::shared_ptr<USBDevice> *transferdevref = nullptr;
    struct libusb_transfer *transfer = NULL;
    unsigned char *transfer_buffer = NULL;
    cleanup([&]{
        safeFree(transfer_buffer);
        safeFreeCustom(transfer, libusb_free_transfer);
        safeDelete(transferdevref);
        safeFreeCustom(config, libusb_free_config_descriptor);
        safeFreeCustom(handle, libusb_close);
    });
    int err = 0;
    uint8_t bus = 0;
    uint8_t address = 0;
    struct libusb_device_descriptor devdesc = {};
    int current_config = 0;
    std::shared_ptr<USBDevice> newDevice;
    
    bus = libusb_get_bus_number(dev);
    assure((address = libusb_get_device_address(dev))>0);

    if (_mux->have_usb_device(bus, address) || is_constructing(bus, address)) {
        //device already found
        return;
    }
    
    retassure(!(err = libusb_get_device_descriptor(dev, &devdesc)), "Could not get device descriptor for device %d-%d: %d", bus, address, err);

    retassure(devdesc.idVendor == VID_APPLE, "USBDevice is not an Apple device");
    retassure(devdesc.idProduct >= PID_RANGE_LOW && devdesc.idProduct <= PID_RANGE_MAX, "USBDevice is Apple, but PID is not in observe range");

    info("Found new device with v/p %04x:%04x at %d-%d", devdesc.idVendor, devdesc.idProduct, bus, address);

    // No blocking operation can follow: it may be run in the libusb hotplug callback and libusb will refuse any
    // blocking call
    retassure(!(err = libusb_open(dev, &handle)),"Could not open device %d-%d: %d", bus, address, err);

    retassure(!(err = libusb_get_configuration(handle, &current_config)), "Could not get configuration for device %d-%d: %d", bus, address, err);

    if (current_config != devdesc.bNumConfigurations) {
        if((err = libusb_get_active_config_descriptor(dev, &config)) != 0) {
            debug("Could not get old configuration descriptor for device %d-%d: %d", bus, address, err);
        } else {
            for(int j=0; j<config->bNumInterfaces; j++) {
                const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
                if((err = libusb_kernel_driver_active(handle, intf->bInterfaceNumber)) < 0) {
                    debug("Could not check kernel ownership of interface %d for device %d-%d: %d", intf->bInterfaceNumber, bus, address, err);
                    continue;
                }
                if(err == 1) {
                    debug("Detaching kernel driver for device %d-%d, interface %d", bus, address, intf->bInterfaceNumber);
                    if((err = libusb_detach_kernel_driver(handle, intf->bInterfaceNumber)) < 0) {
                        warning("Could not detach kernel driver (%d), configuration change will probably fail!", err);
                        continue;
                    }
                }
            }
        }
        debug("Setting configuration for device %d-%d, from %d to %d", bus, address, current_config, devdesc.bNumConfigurations);
        retassure(!(err = libusb_set_configuration(handle, devdesc.bNumConfigurations)), "Could not set configuration %d for device %d-%d: 0x%08x", devdesc.bNumConfigurations, bus, address, err);
    }
    
    retassure(!(err = libusb_get_active_config_descriptor(dev, &config)), "Could not get configuration descriptor for device %d-%d: %d", bus, address, err);
    
    newDevice = std::make_shared<USBDevice>(_mux,this,devdesc.idProduct);
    newDevice->_selfref = newDevice;
    {
        std::unique_lock<std::mutex> ul(_childrenLck);
        _children.insert(newDevice.get());
    }
    /*
        transfer gets own reference, which it needs to free once done
     */
    transferdevref = new std::shared_ptr<USBDevice>(newDevice);
    
    for(int j=0; j<config->bNumInterfaces; j++) {
        const struct libusb_interface_descriptor *intf = &config->interface[j].altsetting[0];
        if(intf->bInterfaceClass != INTERFACE_CLASS ||
           intf->bInterfaceSubClass != INTERFACE_SUBCLASS ||
           intf->bInterfaceProtocol != INTERFACE_PROTOCOL)
            continue;
        if(intf->bNumEndpoints != 2) {
            warning("Endpoint count mismatch for interface %d of device %d-%d", intf->bInterfaceNumber, bus, address);
            continue;
        }
        if((intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
           (intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
            newDevice->_interface = intf->bInterfaceNumber;
            newDevice->_ep_out = intf->endpoint[0].bEndpointAddress;
            newDevice->_ep_in = intf->endpoint[1].bEndpointAddress;
            debug("Found interface %d with endpoints %02x/%02x for device %d-%d", newDevice->_interface, newDevice->_ep_out, newDevice->_ep_in, bus, address);
            goto found_device;
        } else if((intf->endpoint[1].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_OUT &&
                  (intf->endpoint[0].bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
            newDevice->_interface = intf->bInterfaceNumber;
            newDevice->_ep_out = intf->endpoint[1].bEndpointAddress;
            newDevice->_ep_in = intf->endpoint[0].bEndpointAddress;
            warning("Found interface %d with swapped endpoints %02x/%02x for device %d-%d", newDevice->_interface, newDevice->_ep_out, newDevice->_ep_in, bus, address);
            goto found_device;
        } else {
            warning("Endpoint type mismatch for interface %d of device %d-%d", intf->bInterfaceNumber, bus, address);
        }
    }
    reterror("Could not find a suitable USB interface for device %d-%d", bus, address);
found_device:;
    
    retassure(!(err = libusb_claim_interface(handle, newDevice->_interface)), "Could not claim interface %d for device %d-%d: %d", newDevice->_interface, bus, address, err);
    retassure(transfer = libusb_alloc_transfer(0), "Failed to allocate transfer for device %d-%d", bus, address);

    newDevice->_serial[0] = 0;
    newDevice->_bus = bus;
    newDevice->_address = address;
    newDevice->_devdesc = devdesc;
    newDevice->_speed = 480000000;
    newDevice->_usbdev = handle; handle = NULL; //transfering ownership here!
    newDevice->_wMaxPacketSize = libusb_get_max_packet_size(dev, newDevice->_ep_out);

    if (newDevice->_wMaxPacketSize <= 0) {
        error("Could not determine wMaxPacketSize for device %d-%d, setting to 64", newDevice->_bus, newDevice->_address);
        newDevice->_wMaxPacketSize = 64;
    } else {
        debug("Using wMaxPacketSize=%d for device %d-%d", newDevice->_wMaxPacketSize, newDevice->_bus, newDevice->_address);
    }
    
    switch (libusb_get_device_speed(dev)) {
        case LIBUSB_SPEED_LOW:
            newDevice->_speed = 1500000;
            break;
        case LIBUSB_SPEED_FULL:
            newDevice->_speed = 12000000;
            break;
        case LIBUSB_SPEED_SUPER:
            newDevice->_speed = 5000000000;
            break;
        case LIBUSB_SPEED_HIGH:
        case LIBUSB_SPEED_UNKNOWN:
        default:
            newDevice->_speed = 480000000;
            break;
    }
    
    info("USB Speed is %g MBit/s for device %d-%d", (double)(newDevice->_speed / 1000000.0), newDevice->_bus, newDevice->_address);


    /**
     * From libusb:
     *     Asking for the zero'th index is special - it returns a string
     *     descriptor that contains all the language IDs supported by the
     *     device.
     **/
    retassure(transfer_buffer = (unsigned char *)malloc(1024 + LIBUSB_CONTROL_SETUP_SIZE + 8), "Failed to allocate transfer buffer for device %d-%d: %d", bus, address, err);
    memset(transfer_buffer, '\0', 1024 + LIBUSB_CONTROL_SETUP_SIZE + 8);

    libusb_fill_control_setup(transfer_buffer, LIBUSB_ENDPOINT_IN, LIBUSB_REQUEST_GET_DESCRIPTOR, LIBUSB_DT_STRING << 8, 0, 1024 + LIBUSB_CONTROL_SETUP_SIZE);

    libusb_fill_control_transfer(transfer, newDevice->_usbdev, transfer_buffer, usb_get_langid_callback, transferdevref, 1000);

    retassure(!(err = libusb_submit_transfer(transfer)), "Could not request transfer for device %d-%d (%d)", newDevice->_bus, newDevice->_address, err);

    transferdevref = nullptr; //don't cleanup
    transfer = NULL; //transfer in process, needs to be freed by callback
    transfer_buffer = NULL; //transfer in process, needs to be freed by callback

    add_constructing(bus, address);
}

void USBDeviceManager::reaper_runloop(){
    while (true) {
        std::shared_ptr<USBDevice>dev;
        try {
            dev = _reapDevices.wait();
        } catch (...) {
            break;
        }
        //make device go out of scope so it can die in piece
        dev->deconstruct();
    }
}


#pragma mark public


