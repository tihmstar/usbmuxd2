//
//  USBDevice.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef USBDevice_hpp
#define USBDevice_hpp

#include "Device.hpp"
#include "USBDevice_receiver.hpp"
#include <libusb.h>
#include <libgeneral/Manager.hpp>
#include <libgeneral/GuardAccess.hpp>
#include <libgeneral/DeliveryEvent.hpp>
#include <set>
#include <map>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define DEV_MRU 65535

class TCP;
class USBDeviceManager;
class USBDevice_receiver;
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
    struct mux_header_v1{
        uint32_t protocol;
        uint32_t length;
    };
    struct mux_header_v2{
        uint32_t protocol;
        uint32_t length;
        
        uint32_t magic;
        uint16_t tx_seq;
        uint16_t rx_seq;
    };
    union mux_header{
        struct{
            uint32_t protocol;
            uint32_t length;
        };
        mux_header_v1 v1;
        mux_header_v2 v2;
    };

    struct mux_device{
        int version;
        uint8_t *pktbuf;
        uint32_t pktlen;
        uint16_t tx_seq;
        uint16_t rx_seq;
    };
    enum mux_protocol {
        MUX_PROTO_VERSION = 0,
        MUX_PROTO_CONTROL = 1,
        MUX_PROTO_SETUP = 2,
        MUX_PROTO_TCP = IPPROTO_TCP,
    };
private:
    std::weak_ptr<USBDevice> _selfref;
    USBDeviceManager *_parent; //not owned
    uint16_t _pid;
    uint8_t _bus, _address;
    uint8_t _interface, _ep_in, _ep_out;

    struct libusb_device_descriptor _devdesc;
    int _wMaxPacketSize;
    uint64_t _speed;
    
    libusb_device_handle *_usbdev;
    uint16_t _nextPort;
    
    mux_dev_state _state;
    mux_device _muxdev;
    std::mutex _usbLck;
    tihmstar::Event _data_in_event;

    std::set<USBDevice_receiver*> _receivers;
    
    std::set<struct libusb_transfer *> _rx_xfers;
    tihmstar::GuardAccess _rx_xfers_Guard;
    std::set<struct libusb_transfer *> _tx_xfers;
    tihmstar::GuardAccess _tx_xfers_Guard;
    std::map<uint16_t,std::shared_ptr<TCP>> _conns;
    tihmstar::GuardAccess _conns_Guard;
    tihmstar::Event _conns_close_event;

    tihmstar::DeliveryEvent<struct libusb_transfer *> _arrived_xfer;

    std::thread _conReaperThread;
    tihmstar::DeliveryEvent<uint16_t> _reapConnections;

private:
    bool isDeviceReadyForDestruction();
    void addReceiver();
    void reaper_runloop();

public:
    USBDevice(Muxer *mux, USBDeviceManager *parent, uint16_t pid);
    virtual ~USBDevice() override;

#pragma mark inheritence provider
    virtual void kill() noexcept override;
    void deconstruct() noexcept;
    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) override;
    void closeConnection(uint16_t sport);

#pragma mark members
    uint32_t usb_location();
    uint64_t getSpeed();
    uint16_t getPid();
    
    void mux_init();
    void send_packet(enum mux_protocol proto, const void *data, size_t length, tcphdr *header = NULL);
    void usb_send(void *buf, size_t length);
    
    void device_data_input(unsigned char *buffer, uint32_t length);
    void device_version_input(struct mux_version_header *vh);
    void device_control_input(unsigned char *payload, uint32_t payload_length);
    
    
#pragma mark friends
    friend USBDevice_receiver;
    friend USBDeviceManager;
    friend void usb_get_langid_callback(struct libusb_transfer *transfer) noexcept;
    friend void usb_get_serial_callback(struct libusb_transfer *transfer) noexcept;
    friend void usb_start_rx_loop(std::shared_ptr<USBDevice> dev);
    friend void rx_callback(struct libusb_transfer *xfer) noexcept;
    friend void tx_callback(struct libusb_transfer *xfer) noexcept;
};

#endif /* USBDevice_hpp */
