//
//  USBDevice.cpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#include "USBDevice.hpp"
#include "../Manager/USBDeviceManager.hpp"
#include "TCP.hpp"

#include <libgeneral/macros.h>

#include <mutex>

#include <string.h>

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
        dev->kill();
    }

    //remove transfer
    {
        guardWrite(dev->_tx_xfers_Guard);
        dev->_tx_xfers.erase(xfer);
    }

    safeFree(xfer->buffer);
    {
        std::shared_ptr<USBDevice> *userarg = (std::shared_ptr<USBDevice> *)xfer->user_data;xfer->user_data = NULL;
        safeDelete(userarg);
    }
    libusb_free_transfer(xfer);
}

#pragma mark USBDevice
USBDevice::USBDevice(Muxer *mux, USBDeviceManager *parent, uint16_t pid)
: Device(mux, MUXCONN_USB), _selfref{}, _parent(parent)
, _pid(pid)
, _bus(0), _address(0)
, _interface(0), _ep_in(0), _ep_out(0)
, _devdesc{}
, _wMaxPacketSize(0), _speed(0)
, _state{}, _usbdev(NULL), _nextPort(0)
, _muxdev{}, _usbLck{}
, _rx_xfers{}, _tx_xfers{}
{
    retassure(_muxdev.pktbuf = (uint8_t*)malloc(DEV_MRU), "Failed to alloc pktbuf");
    _conReaperThread = std::thread([this]{
        reaper_runloop();
    });

}

USBDevice::~USBDevice(){
    _arrived_xfer.kill();
    while (_receivers.size()) {
        auto r = *_receivers.begin();
        _receivers.erase(r);
        delete r;
    }
    debug("deleting device %s",_serial);
    {
        std::unique_lock<std::mutex> ul(_parent->_childrenLck);
        _parent->_children.erase(this);
        _parent->_childrenEvent.notifyAll();
        _parent = NULL;
    }
    
    assert(_receivers.size() == 0);
    
    _reapConnections.kill();
    _conReaperThread.join();
    
    safeFree(_muxdev.pktbuf);
    //free resources
    if (_usbdev){
        libusb_release_interface(_usbdev, _interface);
        safeFreeCustom(_usbdev, libusb_close);
    }
    assert(isDeviceReadyForDestruction());
}

#pragma mark private
bool USBDevice::isDeviceReadyForDestruction(){
    return _rx_xfers.size() == 0 && _tx_xfers.size() == 0;
}

void USBDevice::addReceiver(){
    _receivers.insert(new USBDevice_receiver(this));
}

void USBDevice::reaper_runloop(){
    while (true) {
        uint16_t conport = 0;
        try {
            conport = _reapConnections.wait();
        } catch (...) {
            break;
        }
        {
            guardWrite(_conns_Guard);
            auto cp = _conns.find(conport);
            if (cp != _conns.end()){
                cp->second->deconstruct();
            }
            _conns.erase(conport);
            _conns_close_event.notifyAll();
        }
    }
}

#pragma mark inheritence provider
void USBDevice::kill() noexcept{
    debug("[Killing] USBDevice %s",_serial);
    std::shared_ptr<USBDevice> selfref = _selfref.lock();
    _parent->_reapDevices.post(selfref);
}

void USBDevice::deconstruct() noexcept{
    debug("[Deconstructing] USBDevice %s",_serial);
    std::shared_ptr<USBDevice> selfref = _selfref.lock();
    _mux->delete_device(selfref);
    //cancel all rx transfers
    {
        guardRead(_rx_xfers_Guard);
        for (auto xfer : _rx_xfers) {
            debug("cancelling _rx_xfers(%p)",xfer);
            libusb_cancel_transfer(xfer);
        }
    }

    //cancel all tx transfers
    {
        guardRead(_tx_xfers_Guard);
        for (auto xfer : _tx_xfers) {
            debug("cancelling _tx_xfers(%p)",xfer);
            libusb_cancel_transfer(xfer);
        }
    }
    
    //cancel all TCP connections
    {
        {
            guardRead(_conns_Guard);
            for (auto c : _conns) {
                _reapConnections.post(c.first);
            }
        }
        while (true) {
            uint64_t wevent = 0;
            {
                guardRead(_conns_Guard);
                if (_conns.size() == 0) break;
                wevent = _conns_close_event.getNextEvent();
            }
            _conns_close_event.waitForEvent(wevent);
        }
    }
}

void USBDevice::start_connect(uint16_t dport, std::shared_ptr<Client> cli){
    std::shared_ptr<TCP> conn;
    assure(_conns.size() < 0xfff0); //we can't handle more connections than we have ports!

    {
        guardWrite(_conns_Guard);
        bool didIterAllPorts = false;
        while (_conns.find(_nextPort) != _conns.end() || _nextPort == 0){
            if (_nextPort == 0) {
                retassure(!didIterAllPorts, "Failed to find available port!");
                didIterAllPorts = true;
            }
            _nextPort++;
        }
        try {
            conn = std::make_shared<TCP>(_nextPort,dport,_selfref.lock(),cli);
        } catch (...) {
            throw;
        }
        _conns[_nextPort] = conn;
    }

    try {
        conn->connect();
    } catch (tihmstar::exception &e) {
        error("failed to connect client dport=%d error=%s code=%d",dport,e.what(),e.code());
        throw;
    }
}

void USBDevice::closeConnection(uint16_t sport){
    _reapConnections.post(sport);
}


#pragma mark members
uint32_t USBDevice::usb_location(){
    return (_bus << 16) | _address;
}

uint64_t USBDevice::getSpeed(){
    return _speed;
}

uint16_t USBDevice::getPid(){
    return _pid;
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
    mux_header *mhdr = NULL; //unchecked
    int mux_header_size = 0;

    mux_header_size = ((_muxdev.version < 2) ? sizeof(struct mux_header_v1) : sizeof(struct mux_header_v2));

    buflen = mux_header_size + length;
    if (header) {
        buflen += sizeof(tcphdr);
    }

    assure(buflen>length); //sanity check

    retassure(buflen <= USB_MTU, "Tried to send packet larger than USB MTU (hdr %zu data %zu total %zu) to device %s", buflen, length, buflen, _serial);

    buf = (unsigned char *)malloc(buflen); //unchecked
    mhdr = (mux_header *)buf;
    mhdr->protocol = htonl(proto);
    mhdr->length = htonl(buflen);

    {
        std::unique_lock<std::mutex> ul(_usbLck);
        if (_muxdev.version >= 2) {
            mhdr->v2.magic = htonl(0xfeedface);
            if (proto == MUX_PROTO_SETUP) {
                _muxdev.tx_seq = 0;
                _muxdev.rx_seq = 0xffff;
            }
            mhdr->v2.tx_seq = htons(_muxdev.tx_seq);
            mhdr->v2.rx_seq = htons(_muxdev.rx_seq);
//            debug("----- MUX UPDATE SEND _muxdev.tx_seq=%d _muxdev.rx_seq=%d",_muxdev.tx_seq,_muxdev.rx_seq);
            _muxdev.tx_seq++;
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
            kill();
            throw;
        }
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
            {
                guardWrite(_tx_xfers_Guard);
                _tx_xfers.erase(xfer);
            }
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

    {
        guardWrite(_tx_xfers_Guard);
        _tx_xfers.insert(xfer);
    }
    retassure((ret = libusb_submit_transfer(xfer)) >=0, "Failed to submit TX transfer %p len %zu to device %d-%d: %d", buf, length, _bus, _address, ret);
    xfer = NULL;
    if (length % _wMaxPacketSize == 0 && length >= _wMaxPacketSize) {
        debug("Send ZLP");
        // Send Zero Length Packet
        assure(buf = malloc(1));
        assure(xfer = libusb_alloc_transfer(0));
        xfer->user_data = NULL;

        txcbargref = new std::shared_ptr<USBDevice>(_selfref.lock());
        libusb_fill_bulk_transfer(xfer, _usbdev, _ep_out, (unsigned char *)buf, 0, tx_callback, txcbargref, 0);
        buf = NULL;
        txcbargref = nullptr;

        {
            guardWrite(_tx_xfers_Guard);
            _tx_xfers.insert(xfer);
        }
        retassure((ret = libusb_submit_transfer(xfer)) >=0, "Failed to submit TX ZLP transfer to device %d-%d: %d", _bus, _address, ret);
        xfer = NULL;
    }
}

void USBDevice::device_data_input(unsigned char *buffer, uint32_t length){
    mux_header *mhdr = NULL;
    unsigned char *payload = NULL;
    uint32_t payload_length = 0;
    int mux_header_size = 0;

    if(!length)
        return;

    // sanity check (should never happen with current USB implementation)
    retassure((length <= USB_MRU) && (length <= DEV_MRU),"Too much data received from USB (%u), file a bug", length);

//    debug("Mux data input for device %s: len %u", _serial, length);
    mhdr = (mux_header *)buffer;

    {
        std::unique_lock<std::mutex> ul(_usbLck);
        mux_header_size = ((_muxdev.version < 2) ? sizeof(struct mux_header_v1) : sizeof(struct mux_header_v2));
#ifdef XCODE
        assert(ntohl(mhdr->length) <= USB_MRU);
#endif
        retassure(ntohl(mhdr->length) == length, "Incoming packet size mismatch (dev %s, expected %d, got %u)", _serial, ntohl(mhdr->length), length);
        if (_muxdev.version >= 2) {
            cleanup([&]{
                _data_in_event.notifyAll();
            });
            uint16_t txseq = ntohs(mhdr->v2.tx_seq);
//            debug("----- MUX txseq=%d -- _muxdev.tx_seq=%d _muxdev.rx_seq=%d",txseq,_muxdev.tx_seq,_muxdev.rx_seq);
            if ((uint16_t)(_muxdev.rx_seq+1) != txseq) {
                while ((uint16_t)(_muxdev.rx_seq+1) < txseq || (uint16_t)(_muxdev.rx_seq+1+_rx_xfers.size()) < txseq + _rx_xfers.size()) {
                    uint64_t wevent = _data_in_event.getNextEvent();
                    ul.unlock();
                    _data_in_event.waitForEvent(wevent);
                    ul.lock();
                }
            }
            if ((uint16_t)(_muxdev.rx_seq+1) != txseq){
                debug("Discarding duplicated MUX packet txseq=%d -- _muxdev.tx_seq=%d _muxdev.rx_seq=%d",txseq,_muxdev.tx_seq,_muxdev.rx_seq);
                return;
            }
            _muxdev.rx_seq = txseq;
        }
        
        // handle broken up transfers
        if(_muxdev.pktlen) {
            if (_muxdev.version < 2){
                error("Mux v1 doesn't support broken up transfers!");
                reterror("Mux v1 doesn't support broken up transfers!");
            }

            //check rx/tx
            retassure((length + _muxdev.pktlen) <= DEV_MRU, "Incoming split packet is too large (%u so far), dropping!", length + _muxdev.pktlen);

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
        }else{
            if((length == USB_MRU) && (length < ntohl(mhdr->length))) {
                memcpy(_muxdev.pktbuf, buffer, length);
                _muxdev.pktlen = (uint32_t)length;
                debug("Copied mux data to buffer (size: %u)", _muxdev.pktlen);
                return;
            }
        }
    }

    switch(ntohl(mhdr->protocol)) {
        case MUX_PROTO_VERSION:
            retassure(length >= (mux_header_size + sizeof(struct mux_version_header)), "Incoming version packet is too small (%u)", length);
        {
            mux_version_header vh = *(struct mux_version_header *)((char*)mhdr+mux_header_size);
            vh.major = ntohl(vh.major);
            vh.minor = ntohl(vh.minor);
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
            tcphdr *tcp_header = reinterpret_cast<tcphdr*>((uint8_t*)mhdr+mux_header_size);
            payload = reinterpret_cast<std::uint8_t*>(tcp_header+1);
            payload_length = length - sizeof(tcphdr) - mux_header_size;
            uint16_t dport = htons(tcp_header->th_dport);
            std::shared_ptr<TCP> connect = nullptr;
            {
                guardRead(_conns_Guard);
                auto conn = _conns.find(dport);
                if (conn != _conns.end()){
                    connect = conn->second;
                }
            }
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
        kill();
        return;
    }
    _muxdev.version = vh->major;

    if (_muxdev.version >= 2) {
        send_packet(MUX_PROTO_SETUP, "\x07", 1);
    }

    info("Connected to v%d.%d device %s on location 0x%x", _muxdev.version, vh->minor, _serial, usb_location());
    _state = MUXDEV_ACTIVE;

    //this device is now set up and ready to be used by muxer
    _mux->add_device(_selfref.lock());
}

void USBDevice::device_control_input(unsigned char *payload, uint32_t payload_length){
    char* buf = NULL;
    cleanup([&]{
        safeFree(buf);
    });
    
    if (payload_length > 0) {
        const char *type = "UNK";
        switch (payload[0]) {
            case 3:
                type = "ERROR";
                break;
            case 4:
                type = "2WARNING2";
                break;
            case 5:
                type = "WARNING";
                break;
            case 7:
                type = "INFO";
                break;
            default:
                break;
        }
        if (payload_length > 1) {
            asprintf(&buf, "%s: %.*s",type,payload_length-1,payload+1);
            printf("%s: %s\n", __func__, buf);
        } else {
            warning("%s: got a type %d packet without payload", __func__, (int)payload[0]);
        }
    }
}
