//
//  USBDevice.cpp
//  usbmuxd2
//
//  Created by tihmstar on 08.12.20.
//

#include "USBDevice.hpp"
#include <libgeneral/macros.h>
#include "../Manager/USBDeviceManager.hpp"
#include "../Muxer.hpp"
#include "../TCP.hpp"
#include "../Client.hpp"
#include <string.h>

#pragma mark libusb_callback definitions

void tx_callback(struct libusb_transfer *xfer) noexcept;

#pragma mark USBDevice

USBDevice::USBDevice(std::shared_ptr<gref_Muxer> mux, std::shared_ptr<gref_USBDeviceManager> parent, uint16_t pid)
: Device(mux, Device::MUXCONN_USB), _selfref{},
_parent(parent), _pid(pid),
_bus(0), _address(0),_interface(0),_ep_in(0),_ep_out(0),_devdesc{},_wMaxPacketSize(0),_speed(0),_usbdev(NULL), _nextPort(),
_muxdev{}, _state{MUXDEV_INIT}
{
    assure(_muxdev.pktbuf = (unsigned char *)malloc(DEV_MRU));
    _muxdev.pktlen = 0;
}

USBDevice::~USBDevice(){
    debug("deleting device %s",_serial);

    //free resources
    if (_usbdev){
        libusb_release_interface(_usbdev, _interface);
        libusb_close(_usbdev);
        _usbdev = NULL;
    }

    safeFree(_muxdev.pktbuf);
    debug("[deleted] device (%p) %s",this,_serial);
}

void USBDevice::kill() noexcept{
  debug("[Killing] device %s",_serial);
  (*_mux)->delete_device(_selfref.lock());
  
  //cancel all rx transfers
  _rx_xfers.addMember();
  for (auto xfer : _rx_xfers._elems) {
      debug("cancelling _rx_xfers(%p)",xfer);
      libusb_cancel_transfer(xfer);
  }
  _rx_xfers.delMember();

  //cancel all tx transfers
  _tx_xfers.addMember();
  for (auto xfer : _tx_xfers._elems) {
      debug("cancelling _tx_xfers(%p)",xfer);
      libusb_cancel_transfer(xfer);
  }
  _tx_xfers.delMember();

  // wait until all xfers are closed
  while (_rx_xfers._elems.size() > 0){
      _rx_xfers.notifyBlock();
  }
  while (_tx_xfers._elems.size() > 0) {
      _tx_xfers.notifyBlock();
  }

  //cancel all TCP connections
  _conns.addMember();
  while (_conns._elems.size()){
      uint16_t sport = _conns._elems.begin()->first;
      _conns.delMember();
      debug("cancelling conn(%u)",sport);
      closeConnection(sport);
      _conns.addMember();
  }
  _conns.delMember();
}


void USBDevice::mux_init(){
    mux_version_header vh = {};

    vh.major = htonl(2);
    vh.minor = htonl(0);
    vh.padding = 0;

    send_packet(MUX_PROTO_VERSION, &vh, sizeof(vh));
}

void USBDevice::send_packet(enum mux_protocol proto, const void *data, size_t length, tcphdr *header){
    /*
     buf allocated and guaranteed transfered to usb_send without failing in between.
     usb_send will always make sure buf is freed, even in case of failure.
     Don't free buf in this function in any case!
     */
    unsigned char *buf = NULL; //unchecked
    size_t buflen = 0;
    struct mux_header *mhdr = NULL; //unchecked
    int mux_header_size = 0;

    mux_header_size = ((_muxdev.version < 2) ? 8 : sizeof(struct mux_header));

    buflen = mux_header_size + length;
    if (header) {
        buflen += sizeof(tcphdr);
    }

    assure(buflen>length); //sanity check

    retassure(buflen <= USB_MTU, "Tried to send packet larger than USB MTU (hdr %zu data %zu total %zu) to device %s", buflen, length, buflen, _serial);

    buf = (unsigned char *)malloc(buflen); //unchecked
    mhdr = (struct mux_header *)buf;
    mhdr->protocol = htonl(proto);
    mhdr->length = htonl(buflen);

    {
        std::unique_lock<std::mutex> ul(_usbLck);
        if (_muxdev.version >= 2) {
            mhdr->magic = htonl(0xfeedface);
            if (proto == MUX_PROTO_SETUP) {
                _muxdev.tx_seq = 0;
                _muxdev.rx_seq = 0xFFFF;
            }
            mhdr->tx_seq = htons(_muxdev.tx_seq);
            mhdr->rx_seq = htons(_muxdev.rx_seq);
            _muxdev.tx_seq++;
        }
    }

    if (header) {
        memcpy(buf + mux_header_size, header, sizeof(tcphdr));
        memcpy(buf + mux_header_size + sizeof(tcphdr), data, length);
    }else{
        memcpy(buf + mux_header_size, data, length);
    }

    try {
        unsigned char *sendbuf = buf; buf = NULL; //freed by usb_send in any case
        usb_send(sendbuf, buflen);
    } catch (tihmstar::exception &e) {
        debug("failed to send packet to usbdevice(%p) error=%s code=%d",this,e.what(),e.code());
        (*_mux)->delete_device(_selfref.lock());
        throw;
    }
}

/*
 always frees buf
 */
void USBDevice::usb_send(void *buf, size_t length){
    struct libusb_transfer *xfer = NULL;
    std::shared_ptr<USBDevice> *txcbargref = nullptr;
    cleanup([&]{
        safeDelete(txcbargref);
        safeFree(buf);
        if (xfer) {
            _tx_xfers.lockMember();
            _tx_xfers._elems.erase(xfer);
            _tx_xfers.unlockMember();
            safeFree(xfer->buffer);
            {
                std::shared_ptr<USBDevice> *userdata = (std::shared_ptr<USBDevice> *)xfer->user_data; xfer->user_data = NULL;
                safeDelete(userdata);
            }
            libusb_free_transfer(xfer);
        }
    });
    int ret = 0;


    assure(length<=INT_MAX); //sanity check
    assure(xfer = libusb_alloc_transfer(0));
    xfer->user_data = NULL;

    txcbargref = new std::shared_ptr<USBDevice>(_selfref.lock());
    libusb_fill_bulk_transfer(xfer, _usbdev, _ep_out, (unsigned char *)buf, (int)length, tx_callback, txcbargref, 0);
    buf = NULL;
    txcbargref = nullptr;

    _tx_xfers.lockMember();
    _tx_xfers._elems.insert(xfer);
    _tx_xfers.unlockMember();
    retassure((ret = libusb_submit_transfer(xfer)) >=0, "Failed to submit TX transfer %p len %zu to device %d-%d: %d", buf, length, _bus, _address, ret);
    xfer = NULL;
    if (length % _wMaxPacketSize == 0) {
        debug("Send ZLP");
        // Send Zero Length Packet
        assure(buf = malloc(1));
        assure(xfer = libusb_alloc_transfer(0));
        xfer->user_data = NULL;

        txcbargref = new std::shared_ptr<USBDevice>(_selfref.lock());
        libusb_fill_bulk_transfer(xfer, _usbdev, _ep_out, (unsigned char *)buf, 0, tx_callback, txcbargref, 0);
        buf = NULL;
        txcbargref = nullptr;

        _tx_xfers.lockMember();
        _tx_xfers._elems.insert(xfer);
        _tx_xfers.unlockMember();
        retassure((ret = libusb_submit_transfer(xfer)) >=0, "Failed to submit TX ZLP transfer to device %d-%d: %d", _bus, _address, ret);
        xfer = NULL;
    }
}


void USBDevice::start_connect(uint16_t dport, std::shared_ptr<Client> cli){
    std::shared_ptr<TCP> conn;
    assure(_conns._elems.size() < 0xfff0); //we can't handle more connections than we have ports!

    _conns.lockMember();
    while (_conns._elems.find(_nextPort) != _conns._elems.end() || _nextPort == 0){
        _nextPort++;
    }
    try {
        conn = std::make_shared<TCP>(_nextPort,dport,_selfref.lock(),cli);
    } catch (...) {
        _conns.unlockMember();
        throw;
    }
    _conns._elems[_nextPort] = conn;
    _conns.unlockMember();

    try {
        conn->connect();
    } catch (tihmstar::exception &e) {
        error("failed to connect client dport=%d error=%s code=%d",dport,e.what(),e.code());
        throw;
    }
}

void USBDevice::closeConnection(uint16_t sport){
    _conns.lockMember();
    _conns._elems.erase(sport);
    _conns.unlockMember();
}

void USBDevice::device_data_input(unsigned char *buffer, uint32_t length){
    struct mux_header *mhdr = NULL;
    unsigned char *payload = NULL;
    uint32_t payload_length = 0;
    int mux_header_size = 0;

    if(!length)
        return;

    // sanity check (should never happen with current USB implementation)
    retassure((length <= USB_MRU) && (length <= DEV_MRU),"Too much data received from USB (%u), file a bug", length);

    debug("Mux data input for device %s: len %u", _serial, length);
    mhdr = (struct mux_header *)_muxdev.pktbuf;

    std::unique_lock<std::mutex> ul(_usbLck);
    // handle broken up transfers
    if(_muxdev.pktlen) {
        if((length + _muxdev.pktlen) > DEV_MRU) {
            error("Incoming split packet is too large (%u so far), dropping!", length + _muxdev.pktlen);
            _muxdev.pktlen = 0;
            return;
        }

        memcpy(_muxdev.pktbuf + _muxdev.pktlen, buffer, length);

        if((length < USB_MRU) || (ntohl(mhdr->length) == (length + _muxdev.pktlen))) {
            buffer = _muxdev.pktbuf;
            length += _muxdev.pktlen;
            _muxdev.pktlen = 0;
            debug("Gathered mux data from buffer (total size: %u)", length);
        } else {
            _muxdev.pktlen += (uint32_t)length;
            debug("Appended mux data to buffer (total size: %u)", _muxdev.pktlen);
            return;
        }
    } else {
        if((length == USB_MRU) && (length < ntohl(mhdr->length))) {
            memcpy(_muxdev.pktbuf, buffer, length);
            _muxdev.pktlen = (uint32_t)length;
            debug("Copied mux data to buffer (size: %u)", _muxdev.pktlen);
            return;
        }
    }

    mhdr = (struct mux_header *)buffer;
    mux_header_size = ((_muxdev.version < 2) ? 8 : sizeof(struct mux_header));
    retassure(ntohl(mhdr->length) == length, "Incoming packet size mismatch (dev %s, expected %d, got %u)", _serial, ntohl(mhdr->length), length);

    if (_muxdev.version >= 2) {
        _muxdev.rx_seq = ntohs(mhdr->rx_seq);
    }

    switch(ntohl(mhdr->protocol)) {
        case MUX_PROTO_VERSION:
            retassure(length >= (mux_header_size + sizeof(struct mux_version_header)), "Incoming version packet is too small (%u)", length);
        {
            mux_version_header vh = *(struct mux_version_header *)((char*)mhdr+mux_header_size);
            vh.major = ntohl(vh.major);
            vh.minor = ntohl(vh.minor);
            ul.unlock();
            device_version_input(&vh);
        }
            break;
        case MUX_PROTO_CONTROL:
            payload = (unsigned char *)(mhdr+1);
            payload_length = length - mux_header_size;
            device_control_input(payload, payload_length);
            break;
        case MUX_PROTO_TCP:
            retassure(length >= (mux_header_size + sizeof(struct tcphdr)), "Incoming TCP packet is too small (%u)", length);
        {
            size_t tcpdataSize = 0;
            tcphdr *tcp_header = reinterpret_cast<tcphdr*>(mhdr+1);
            payload = reinterpret_cast<std::uint8_t*>(tcp_header+1);
            payload_length = length - sizeof(tcphdr) - mux_header_size;
            uint16_t dport = htons(tcp_header->th_dport);
            std::shared_ptr<TCP> connect = nullptr;
            {
                _conns.addMember();
                auto conn = _conns._elems.find(dport);
                if (conn != _conns._elems.end()){
                    connect = conn->second;
                }
                _conns.delMember();
            }
            ul.unlock();
            if (!connect){
                try {
                   TCP::send_RST(this, tcp_header);
               } catch (...) {
                   //
               }
               error("no connection found with snum=%d",dport);
            }else{
               try {
                   connect->handle_input(tcp_header, payload, payload_length);
               } catch (tihmstar::exception &e) {
                   error("failed to handle input on snum=%d device(%d)=%s with error=%d (%s)",dport,_id,_serial,e.code(),e.what());
                   throw;
               }
            }
            return;
        }
            break;
        default:
            error("Incoming packet for device %s has unknown protocol 0x%x)", _serial, ntohl(mhdr->protocol));
            break;
    }
}


void USBDevice::device_version_input(struct mux_version_header *vh){
    retassure(_state == MUXDEV_INIT, "Version packet from already initialized device %s", _serial);

    if(vh->major != 2 && vh->major != 1) {
        error("Device %s has unknown version %d.%d", _serial, vh->major, vh->minor);
        (*_mux)->delete_device(_selfref.lock());
        return;
    }
    _muxdev.version = vh->major;

    if (_muxdev.version >= 2) {
        send_packet(MUX_PROTO_SETUP, "\x07", 1);
    }

    info("Connected to v%d.%d device %s on location 0x%x", _muxdev.version, vh->minor, _serial, usb_location());
    _state = MUXDEV_ACTIVE;

    //this device is now set up and ready to be used by muxer
    (*_mux)->add_device(_selfref.lock());
}

void USBDevice::device_control_input(unsigned char *payload, uint32_t payload_length){
    char* buf = NULL;
    cleanup([&]{
        safeFree(buf);
    });

    if (payload_length > 0) {
        switch (payload[0]) {
            case 3:
                if (payload_length > 1) {
                    buf = (char*)malloc(payload_length);
                    strncpy(buf, (char*)payload+1, payload_length-1);
                    buf[payload_length-1] = '\0';
#ifdef DEBUG
                    debug("%s: (%p) ERROR: %s", __func__, this, buf);
#else
                    error("%s: ERROR: %s", __func__, buf);
#endif
                } else {
                    error("%s: Error occurred, but empty error message", __func__);
                }
                break;
            case 7:
                if (payload_length > 1) {
                    buf = (char*)malloc(payload_length);
                    strncpy(buf, (char*)payload+1, payload_length-1);
                    buf[payload_length-1] = '\0';
                    info("%s: %s", __func__, buf);
                }
                break;
            default:
                break;
        }
    } else {
        warning("%s: got a type 1 packet without payload", __func__);
    }
}


#pragma mark libusb_callback implementations

void tx_callback(struct libusb_transfer *xfer) noexcept{
    std::shared_ptr<USBDevice> dev = *(std::shared_ptr<USBDevice> *)xfer->user_data;

    if(xfer->status != LIBUSB_TRANSFER_COMPLETED) {
        switch(xfer->status) {
            case LIBUSB_TRANSFER_COMPLETED: //shut up compiler
            case LIBUSB_TRANSFER_ERROR:
                // funny, this happens when we disconnect the device while waiting for a transfer, sometimes
                info("Device %d-%d TX aborted due to error or disconnect", dev->_bus, dev->_address);
                break;
            case LIBUSB_TRANSFER_TIMED_OUT:
                error("TX transfer timed out for device %d-%d", dev->_bus, dev->_address);
                break;
            case LIBUSB_TRANSFER_CANCELLED:
                debug("Device %d-%d TX transfer cancelled", dev->_bus, dev->_address);
                break;
            case LIBUSB_TRANSFER_STALL:
                error("TX transfer stalled for device %d-%d", dev->_bus, dev->_address);
                break;
            case LIBUSB_TRANSFER_NO_DEVICE:
                // other times, this happens, and also even when we abort the transfer after device removal
                info("Device %d-%d TX aborted due to disconnect", dev->_bus, dev->_address);
                break;
            case LIBUSB_TRANSFER_OVERFLOW:
                error("TX transfer overflow for device %d-%d", dev->_bus, dev->_address);
                break;
                // and nothing happens (this never gets called) if the device is freed after a disconnect! (bad)
            default:
                // this should never be reached.
                break;
        }
        (*dev->_mux)->delete_device(dev);
    }

    //remove transfer
    dev->_tx_xfers.lockMember();
    dev->_tx_xfers._elems.erase(xfer);
    dev->_tx_xfers.unlockMember();

    safeFree(xfer->buffer);
    {
        std::shared_ptr<USBDevice> *userarg = (std::shared_ptr<USBDevice> *)xfer->user_data;xfer->user_data = NULL;
        safeDelete(userarg);
    }
    libusb_free_transfer(xfer);
}
