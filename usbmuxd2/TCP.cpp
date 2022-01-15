//
//  TCP.cpp
//  usbmuxd2
//
//  Created by tihmstar on 30.05.21.
//

#include "TCP.hpp"
#include <libgeneral/macros.h>
#include "Client.hpp"
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define unacked ((uint64_t)((_stx.seq >= _stx.seqAcked) ? (_stx.seq - _stx.seqAcked) : (0x100000000 - _stx.seqAcked + _stx.seq)))


TCP::TCP(uint16_t sPort, uint16_t dPort, std::shared_ptr<USBDevice> dev, std::shared_ptr<Client> cli)
: _connState(CONN_CONNECTING), _stx{0,0,0,0,0,131072},
    _sPort(sPort), _dPort(dPort), _dev(dev), _cli(cli), _payloadBuf(NULL), _pfd{.fd = -1, .events=POLLIN}
{
    debug("[TCP] (%d) creating connection for sport=%u",cli->_fd,_sPort);
    assure(_payloadBuf = (char*)malloc(TCP::bufsize));
    _stx.win = TCP::bufsize >> 8;

    #warning i think there is a bug somewhere related to seq numbers, where the device would send a RST package....
    _stx.seqAcked = _stx.seq = 0;//(uint32_t)random();
}

TCP::~TCP(){
    safeFree(_payloadBuf);
    safeClose(_pfd.fd);
}

void TCP::loopEvent(){
    int err = 0;
    ssize_t cnt = 0;

    uint32_t lseqAck = 0;
    uint32_t lseq = 0;
    char *bufstart = _payloadBuf+lseq;
    size_t maxRCV = (lseq >= lseqAck) ? (TCP::bufsize - lseq) : lseqAck-lseq;
    
    {
        std::unique_lock<std::mutex> ul(_lockStx);
        lseqAck = ((uint64_t)_stx.seqAcked + TCP::bufsize)%TCP::bufsize;
        lseq = ((uint64_t)_stx.seq + TCP::bufsize)%TCP::bufsize;
    }

    assure((err = poll(&_pfd,1,-1)) != -1);
    if ((_pfd.revents & (~POLLIN)) != 0){
      kill();
      reterror("[TCP] (fd=%d) unexpected poll revent=%d",_pfd.fd,_pfd.revents);
    }else if (!(_pfd.revents & POLLIN)){
      warning("[TCP] poll returned, but no data to read");
      return;
    }else if ((cnt = recv(_pfd.fd, bufstart, maxRCV, MSG_DONTWAIT))<=0){
        kill();
        reterror("[TCP] recv failed on client %d with error=%d (%s)",_pfd.fd,errno,strerror(errno));
    }

    debug("got packet of size %zd",cnt);

    while (cnt>0) {
        ssize_t doSend = MIN(cnt,TCP::TCP_MTU);
        size_t didSend = send_data(bufstart,doSend);
        bufstart += didSend;
        cnt -= didSend;
    }
}

void TCP::stopAction() noexcept{
    if (_pfd.fd != -1) shutdown(_pfd.fd, SHUT_RDWR);
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
    _dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
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
    if (doSend) {
        _dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
    }
    _lockStx.unlock();
}

size_t TCP::send_data(void *buf, size_t len){
    if (!len) return 0;
    tcphdr tcp_header{};
    int64_t rembytes = 0;
    int sendfails = 0;
retry:
    for (int i=0; i<10; i++) {
        _lockStx.lock();
        if (unacked + len > _stx.inWin) {
            _lockStx.unlock();
            sched_yield();
        }else{
            goto cnt_label; //fast path
        }
    }
    _lockStx.lock();
    //slow path
    rembytes = _stx.inWin - unacked;
 
    if (rembytes<=0) {
        //at this point we *have to* wait for an ACK
        //no smaller payload is possible
        ++sendfails;
        debug("[%d] we have to wait for ACK before sending more data!",sendfails);
        
        // **** Put this thread to sleep until we can send more data **** //

        _lockStx.unlock(); //unlocking _lockStx inbetween _lockCanSend locks, makes sure we never lock if we could send data!

        _lockCanSend.wait();//this lock will always be "blocking", unless we can send more data
        assure(_connState == CONN_CONNECTED);

        goto retry;
    }
    len = rembytes;
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
    debug("Sending tcp payload packet: sport=%u dport=%u seq=%u seqAcked=%u ack=%u flags=0x%x window=%u[%u] len=%zu rwindow=%u[%u]",
          htons(tcp_header.th_sport), htons(tcp_header.th_dport), htonl(tcp_header.th_seq), _stx.seqAcked, htonl(tcp_header.th_ack),
          TH_ACK, _stx.win, _stx.win >> 8, len, _stx.inWin, _stx.inWin >> 8);

    _dev->send_packet(USBDevice::MUX_PROTO_TCP, buf, len, &tcp_header);
    _lockStx.unlock();
    return len;
}

#pragma mark public

void TCP::kill() noexcept{
    std::thread delthread([this](std::shared_ptr<USBDevice> ldev,uint16_t sport){
#ifdef DEBUG
        debug("killing TCP (%p) %d",this,_pfd.fd);
#else
        info("killing TCP %d",_pfd.fd);
#endif
        ldev->closeConnection(sport);
    },_dev,_sPort);
    delthread.detach();
}

void TCP::handle_input(tcphdr* tcp_header, uint8_t* payload, uint32_t payload_len){
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
            retassure(tcp_header->th_flags & TH_RST, "Received unexpected data while connecting");
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
                debug("[TCP IN] sport=%u dport=%u _stx.ack=%u len=%u",_sPort, _dPort, _stx.ack, payload_len);
                _lockStx.unlock();
                if (payload_len) { // don't bounce doubleACKs
                    send_ack();

                    //forward without buffering
                    if(send(_pfd.fd, payload, payload_len, 0) != payload_len){
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

#warning This was never tested. But also i don't think this can ever happen
                char *bufstart = _payloadBuf+((uint64_t)rAck + TCP::bufsize)%TCP::bufsize;
                _dev->send_packet(USBDevice::MUX_PROTO_TCP, bufstart, _stx.seqAcked-rAck, &tcp_header);
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

void TCP::connect(){
    bool connectionWasSuccessfull = false;
    cleanup([&]{
        if (connectionWasSuccessfull){
            _cli->_fd = -1; //disown client, we take care of this fd now
            _cli = nullptr; //free client
        }else{
            _pfd.fd = -1;
        }
    });
    _pfd.fd = _cli->_fd;

    info("Starting TCP connection");

    send_tcp(TH_SYN);

    while (_connState == CONN_CONNECTING)
        sched_yield();

    assure(_connState == CONN_CONNECTED);
    info("TCP Connected to device");
    try {
        _cli->send_result(_cli->_connectTag, RESULT_OK);
    } catch (...) {
        _pfd.fd = -1; //discard file descriptor, because (Client) still owns it
        throw;
    }
    startLoop();
    connectionWasSuccessfull = true;
}

#pragma mark static
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

