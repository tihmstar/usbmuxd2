//
//  TCP.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "TCP.hpp"
#include <log.h>
#include <libgeneral/macros.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <Devices/USBDevice.hpp>
#include <Client.hpp>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <system_error>


#define MIN(a,b) (((a)<(b)) ? (a) : (b))

TCP::TCP(uint16_t sPort, uint16_t dPort, USBDevice *dev, Client *cli)
    : _stx{0,0,0,0,0,131072}, _connState(CONN_CONNECTING), _cli(cli), _device(dev), _payloadBuf(NULL), _killInProcess(false), _didConnect(false), _refCnt(1),
        _lockStx{}, _sPort(sPort), _dPort(dPort), _pfds(NULL)
{
    debug("[TCP] (%d) creating connection for sport=%u",cli->_fd,_sPort);
    assure(_payloadBuf = (char*)malloc(TCP::bufsize));

    _stx.seqAcked = _stx.seq = (uint32_t)random();
    assure(_pfds = (struct pollfd*) malloc(sizeof(struct pollfd)));
    _pfds[0].fd = cli->_fd;
    _pfds[0].events = POLLIN;
}

TCP::~TCP() {
#ifdef DEBUG
    if (!_killInProcess) {
        error("THIS DESTRUCTOR IS NOT MEANT TO BE CALLED OTHER THAN THROUGH teardown()!!");
        assert(0);
    }
#endif

    debug("[TCP] destroying connection for sport=%u",_sPort);
    if (_didConnect && _pfds[0].fd>0) { // only kill socket if we connected successfully, otherwise the socket still belongs to client
        int delfd = _pfds[0].fd; _pfds[0].fd = -1;
        close(delfd);
    }


    try {
        send_tcp(TH_RST);
    } catch (...) {
        //
    }

#warning UAF: device might be dead by now
#warning TODO: add reference counting to Device
    _device->close_connection(_sPort);
    _connState = CONN_DYING;
    _lockCanSend.notifyAll();
    stopLoop();

    while (!_didConnect || _refCnt) { //connect is always called right away, let's wait until we did
        sched_yield();
    }

    safeFree(_payloadBuf);
    safeFree(_pfds);
}

void TCP::loopEvent(){
    int err = 0;
    ssize_t cnt = 0;

    uint32_t lseqAck = ((uint64_t)_stx.seqAcked + TCP::bufsize)%TCP::bufsize;
    uint32_t lseq = ((uint64_t)_stx.seq + TCP::bufsize)%TCP::bufsize;
    char *bufstart = _payloadBuf+lseq;

    size_t maxRCV = (lseq >= lseqAck) ? (TCP::bufsize - lseq) : lseqAck-lseq;

    assure((err = poll(_pfds,1,-1)) != -1);
    if ((_pfds[0].revents & (~POLLIN)) != 0){
      kill();
      reterror("[TCP] (fd=%d) unexpected poll revent=%d",_pfds[0].fd,_pfds[0].revents);
    }else if (!(_pfds[0].revents & POLLIN)){
      warning("[TCP] poll returned, but no data to read");
      return;
    }else if ((cnt = recv(_pfds[0].fd, bufstart, maxRCV, MSG_DONTWAIT))<=0){
        kill();
        reterror("[TCP] recv failed on client %d with error=%d (%s)",_pfds[0].fd,errno,strerror(errno));
    }

    debug("got packet of size %zd",cnt);

    while (cnt>0) {
        ssize_t doSend = MIN(cnt,TCP::TCP_MTU);
        if (ssize_t optSend = MIN(doSend, _stx.inWin - (_stx.seq - _stx.seqAcked))){ //packet size optimization
            doSend = optSend; //don't "optimize" if value is zero (which means we have to wait for ACK anyways)
        }
    lessPayload:
        try {
            send_data(bufstart,doSend);
        } catch (int leftdata) {
            doSend = MIN(leftdata,doSend);
            debug("less payload=%u",leftdata);
            goto lessPayload;
        }
        bufstart += doSend;
        cnt -= doSend;
    }
}

void TCP::send_tcp(std::uint8_t flags) {
    tcphdr tcp_header{};
    _lockStx.lock();
    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seq);
    tcp_header.th_ack = htonl(_stx.ack);

    tcp_header.th_flags = flags;
    tcp_header.th_off = sizeof(tcp_header) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    debug("[TCP OUT] tcp header packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u [%u] len=%u",
          _sPort, _dPort, _stx.seq, _stx.ack, flags, _stx.win, _stx.win >> 8, 0);
    // Update TCP states
    _stx.acked = _stx.ack;
    _lockStx.unlock();

    _device->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}

void TCP::send_ack(){
    bool doSend = false;
    tcphdr tcp_header{};
    _lockStx.lock();
    if ((doSend = (_stx.acked != _stx.ack))) {
        debug("Sending tcp ack packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u[%u]",
              _sPort, _dPort, _stx.seq, _stx.ack, TH_ACK, _stx.win, _stx.win >> 8);

        tcp_header.th_sport = htons(_sPort);
        tcp_header.th_dport = htons(_dPort);
        tcp_header.th_seq = htonl(_stx.seq);
        tcp_header.th_ack = htonl(_stx.ack);
        tcp_header.th_flags = TH_ACK;
        tcp_header.th_off = sizeof(tcphdr) / 4;
        tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

        // Update TCP states
        _stx.acked = _stx.ack;
    }
    _lockStx.unlock();
    if (doSend) {
        _device->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
    }
}

void TCP::send_data(void* buf, size_t len) {
    tcphdr tcp_header{};
    int rembytes = 0;
retry:
    for (int i=0; i<10; i++) {
        _lockStx.lock();
        if (_stx.seq + len - _stx.seqAcked > _stx.inWin) {
            _lockStx.unlock();
            sched_yield();
        }else{
            goto cnt_label; //fast path
        }
    }
    _lockStx.lock();
    //slow path
    rembytes = (int)(_stx.inWin - (_stx.seq - _stx.seqAcked));

    //account for seq overflows
    if (_stx.seq < _stx.seqAcked) {
        rembytes = (int)(_stx.inWin - (uint32_t)(((uint64_t)(_stx.seq+0x100000000) - _stx.seqAcked)));
        if (len <= rembytes) {
            goto cnt_label;
        }
    }

    if (rembytes<=0) {
        //at this point we *have to* wait for an ACK
        //no smaller payload is possible
        debug("we have to wait for ACK before sending more data!");

        // **** Put this thread to sleep until we can send more data **** //


        _lockStx.unlock(); //unlocking _lockStx inbetween _lockCanSend locks, makes sure we never lock if we could send data!

        assure(_connState == CONN_CONNECTED);
        _lockCanSend.wait();//this lock will always be "blocking", unless we can send more data

        goto retry;
    }
    _lockStx.unlock();

    throw int(rembytes);
cnt_label:

    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seq);
    tcp_header.th_ack = htonl(_stx.ack);
    tcp_header.th_flags = TH_ACK;
    tcp_header.th_off = sizeof(tcphdr) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    // Update TCP states
    _stx.acked = _stx.ack;
    _stx.seq += len;
    _lockStx.unlock();
    debug("Sending tcp payload packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u[%u] len=%zu rwindow=%u[%u]",
          htons(tcp_header.th_sport), htons(tcp_header.th_dport), htonl(tcp_header.th_seq), htonl(tcp_header.th_ack),
          TH_ACK, htons(tcp_header.th_win)<<8, htons(tcp_header.th_win), len, _stx.inWin, _stx.inWin >> 8);

    _device->send_packet(USBDevice::MUX_PROTO_TCP, buf, len, &tcp_header);
}


void TCP::connect() {
    cleanup([&]{
        _didConnect = true; // make sure destructor knows this function was called and returned
    });
    info("Starting TCP connection");

    send_tcp(TH_SYN);

    while (_connState == CONN_CONNECTING)
        sched_yield();

    assure(_connState == CONN_CONNECTED);
    info("TCP Connected to device");
    try {
        _cli->send_result(_cli->hdr->tag, RESULT_OK);
    } catch (...) {
        _pfds[0].fd = -1; //discard file descriptor, because (Client) still owns it
        throw;
    }
    _cli->_fd = -1; //discard file descriptor, because the device (TCP) owns it now
    _cli = nullptr; // we don't need to keep a pointer in here anymore!
    startLoop();
}

void TCP::handle_input(tcphdr* tcp_header, uint8_t* payload, uint32_t payload_len) {
    debug("[TCP IN] sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u[%u] len=%u",
          _sPort, _dPort, ntohl(tcp_header->th_seq), ntohl(tcp_header->th_ack), tcp_header->th_flags, ntohs(tcp_header->th_win) << 8, ntohs(tcp_header->th_win), payload_len);

    // Update TCP receiver state
    uint32_t rSeq = ntohl(tcp_header->th_seq);
    uint32_t rAck = ntohl(tcp_header->th_ack);

    if(_connState == CONN_CONNECTING) {
        if(tcp_header->th_flags == (TH_SYN | TH_ACK)) {
            debug("Received SYN/ACK during device handshake");
            _stx.seq++;
            _stx.ack = rSeq+1; //just copy this on first packet without parsing
            _stx.inWin = ntohs(tcp_header->th_win) << 8;

            send_ack();
            _connState = CONN_CONNECTED;
        } else {
            retassure(tcp_header->th_flags & TH_RST,"Received unexpected data while connecting");
            _connState = CONN_REFUSED;
            info("Connection refused by device");
            kill();
        }
    } else if (_connState == CONN_CONNECTED) {
        debug("Receive data!");

        if (tcp_header->th_flags == TH_ACK) {

            //either forward packet, or discard it
            _lockStx.lock();
            if (_stx.ack == rSeq) {
                _stx.ack += payload_len;
                _lockStx.unlock();
                if (payload_len) { // don't bounce doubleACKs
                    debug("[TCP IN ACKING] sport=%u dport=%u _stx.ack=%u len=%u",_sPort, _dPort, _stx.ack, payload_len);
                    send_ack();

                    //forward without buffering
                    if(send(_pfds[0].fd, payload, payload_len, 0) != payload_len){
                        //client died, but don't throw, since it wasn't the devices fault!
                        //terminate TCP instead
                        kill();
                        return;
                    }
                }
            }else{
                debug("discarding packet");
                _lockStx.unlock();
                send_ack();
            }

            _lockStx.lock();

            if (rAck >= _stx.seqAcked || _stx.seq < _stx.seqAcked){//check for seq overflow

                _stx.seqAcked = rAck; //update ACK on sent packets
                _stx.inWin = ntohs(tcp_header->th_win) << 8;

                _lockCanSend.notifyAll();

                _lockStx.unlock();
            }else {

                //this will never ever happen (i think)
                debug("Re-sending tcp payload packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u[%u] len=%u",
                      _sPort, _dPort, rAck, _stx.ack, TH_ACK, _stx.win, _stx.win >> 8, _stx.seqAcked-rAck);

                tcphdr tcp_header{};

                tcp_header.th_sport = htons(_sPort);
                tcp_header.th_dport = htons(_dPort);
                tcp_header.th_seq = htonl(rAck);
                tcp_header.th_ack = htonl(_stx.ack);
                tcp_header.th_flags = TH_ACK;
                tcp_header.th_off = sizeof(tcphdr) / 4;
                tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

                // Update TCP states
                _stx.acked = _stx.ack;
                _lockStx.unlock();

#warning This was never tested lol
                char *bufstart = _payloadBuf+((uint64_t)rAck + TCP::bufsize)%TCP::bufsize;
                _device->send_packet(USBDevice::MUX_PROTO_TCP, bufstart, _stx.seqAcked-rAck, &tcp_header);
            }
        } else if (tcp_header->th_flags == TH_RST){
            info("Connection reset by device, flags: %u sport=%u dport=%u", tcp_header->th_flags,_sPort,_dPort);
            kill();
        }else{
            warning("unexpected flags=0x%02x",tcp_header->th_flags);
        }
    } else {
        warning("Data for unexpected connection state:");
    }
}

void TCP::kill() noexcept{
    //sets _killInProcess to true and executes if statement if it was false before
    if (!_killInProcess.exchange(true)) {
        
        std::thread delthread([this](){
#ifdef DEBUG
            debug("killing TCP (%p) %d",this,_pfds[0].fd);
#else
            info("killing TCP %d",_pfds[0].fd);
#endif
            delete this;
        });
        delthread.detach();
        

    }
}


void TCP::send_RST(USBDevice *dev, tcphdr *hdr){
    tcphdr tcp_header{};
    tcp_header.th_sport = hdr->th_dport;
    tcp_header.th_dport = hdr->th_sport;
    tcp_header.th_seq = hdr->th_ack;
    tcp_header.th_ack = hdr->th_seq;

    tcp_header.th_flags = TH_RST;
    tcp_header.th_off = sizeof(tcp_header) / 4;
    tcp_header.th_win = hdr->th_win;

    debug("[OOL TCP OUT RST] tcp header packet: sport=%u dport=%u", htons(tcp_header.th_sport), htons(tcp_header.th_sport));
    dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}
