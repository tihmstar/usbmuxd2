//
//  USBDevice.hpp
//  usbmuxd2
//
//  Created by tihmstar on 08.12.20.
//

#ifndef USBDevice_hpp
#define USBDevice_hpp

#include <stdint.h>
#include "Device.hpp"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <libusb-1.0/libusb.h>
#include <libgeneral/lck_container.hpp>
#include <set>
#include <map>

#define DEV_MRU 65535

class gref_USBDeviceManager;
class USBDeviceManager;
class TCP;

class USBDevice : public Device{
public:
    enum mux_dev_state {
        MUXDEV_INIT,      // sent version packet
        MUXDEV_ACTIVE,    // received version packet, active
        MUXDEV_DEAD       // dead
    };
    struct mux_version_header{
        uint32_t major;
        uint32_t minor;
        uint32_t padding;
    };
    struct mux_header{
        uint32_t protocol;
        uint32_t length;
        uint32_t magic;
        uint16_t tx_seq;
        uint16_t rx_seq;
    };
    struct mux_device{
        int version;
        unsigned char *pktbuf;
        uint32_t pktlen;
        uint16_t rx_seq;
        uint16_t tx_seq;
    };
    enum mux_protocol {
        MUX_PROTO_VERSION = 0,
        MUX_PROTO_CONTROL = 1,
        MUX_PROTO_SETUP = 2,
        MUX_PROTO_TCP = IPPROTO_TCP,
    };
private:
    std::weak_ptr<USBDevice> _selfref;
    std::shared_ptr<gref_USBDeviceManager> _parent;
    uint16_t _pid;
    uint8_t _bus, _address;
    uint8_t _interface, _ep_in, _ep_out;

    struct libusb_device_descriptor _devdesc;
    int _wMaxPacketSize;
    uint64_t _speed;
    libusb_device_handle *_usbdev;
    uint16_t _nextPort;

    mux_device _muxdev;
    std::mutex _usbLck;
    mux_dev_state _state;


    tihmstar::lck_contrainer<std::set<struct libusb_transfer *>> _rx_xfers;
    tihmstar::lck_contrainer<std::set<struct libusb_transfer *>> _tx_xfers;
    tihmstar::lck_contrainer<std::map<uint16_t,std::shared_ptr<TCP>>> _conns;


public:
    USBDevice(const USBDevice &) =delete; //delete copy constructor
    USBDevice(USBDevice &&o) = delete; //move constructor

    USBDevice(std::shared_ptr<gref_Muxer> mux, std::shared_ptr<gref_USBDeviceManager> parent, uint16_t pid);
    virtual ~USBDevice() override;

    virtual void kill() noexcept override;

    uint32_t usb_location(){return (_bus << 16) | _address;}

    void mux_init();
    void send_packet(enum mux_protocol proto, const void *data, size_t length, tcphdr *header = NULL);
    void usb_send(void *buf, size_t length);

    void device_data_input(unsigned char *buffer, uint32_t length);
    void device_version_input(struct mux_version_header *vh);
    void device_control_input(unsigned char *payload, uint32_t payload_length);


    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) override;
    void closeConnection(uint16_t sport);

    friend Muxer;
    friend USBDeviceManager;
    friend void usb_start_rx_loop(std::shared_ptr<USBDevice> dev);
    friend void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept;
    friend void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept;
    friend void rx_callback(struct libusb_transfer *xfer) noexcept;
    friend void tx_callback(struct libusb_transfer *xfer) noexcept;
};

#endif /* USBDevice_hpp */
