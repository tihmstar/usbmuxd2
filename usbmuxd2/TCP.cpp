//
//  TCP.cpp
//  usbmuxd2
//
//  Created by tihmstar on 30.05.21.
//

#include "TCP.hpp"
#include <libgeneral/macros.h>
#include "Client.hpp"
#include "Devices/USBDevice.hpp"
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define unacked ((uint64_t)((_stx.seq >= _stx.seqAcked) ? (_stx.seq - _stx.seqAcked) : ((uint32_t)(0x100000000ULL + _stx.seq - _stx.seqAcked))))

TCP::TCP(uint16_t sPort, uint16_t dPort, std::shared_ptr<USBDevice> dev, std::shared_ptr<Client> cli)
: _connState(CONN_CONNECTING), _stx{0,0,0,0,0,0x80000},
 _sPort(sPort), _dPort(dPort), _dev(dev), _cli(cli), _payloadBuf(NULL), _pfd{.fd = -1, .events=POLLIN}
{
    debug("[TCP] (%d) creating connection for sport=%u",cli->_fd,_sPort);
    assure(_payloadBuf = (char*)malloc(TCP::bufsize));
    
    _stx.seqAcked = _stx.seq = (uint32_t)random();
}

TCP::~TCP(){
    debug("destroying TCP %p",this);
    stopLoop();
    safeFree(_payloadBuf);
    safeClose(_pfd.fd);
}

bool TCP::loopEvent(){
    int err = 0;
    bool remoteDidClose = false;
    ssize_t cnt = 0;

    uint32_t lseqAck = 0;
    uint32_t lseq = 0;
    char *bufstart = NULL;
    size_t maxRCV = 0;
    
    {
        std::unique_lock<std::mutex> ul(_lockStx);
        lseqAck = ((uint64_t)_stx.seqAcked + TCP::bufsize)%TCP::bufsize;
        lseq = ((uint64_t)_stx.seq + TCP::bufsize)%TCP::bufsize;
    }
    bufstart = _payloadBuf+lseq;
    maxRCV = (lseq >= lseqAck) ? (TCP::bufsize - lseq) : lseqAck-lseq;
    
    retassure(_pfd.fd != -1, "[TCP CLIENT] bad pollfd");
    retassure((err = poll(&_pfd,1,-1)) != -1, "[TCP CLIENT] poll failed");
    if (_pfd.revents & POLLHUP){
        remoteDidClose = true;
        debug("[TCP CLIENT] Remote connection closed");
    }
    
    if ((_pfd.revents & (~(POLLIN | POLLHUP))) != 0){
      kill(__LINE__);
      reterror("[TCP CLIENT] (fd=%d) unexpected poll revent=0x%x",_pfd.fd,_pfd.revents);
    }else if (!(_pfd.revents & POLLIN)){
      warning("[TCP CLIENT] poll returned, but no data to read");
      return true;
    }
    
    do{
        if ((cnt = recv(_pfd.fd, bufstart, maxRCV, MSG_DONTWAIT))<0){
            kill(__LINE__);
            reterror("[TCP CLIENT] recv failed on client %d with error=%d (%s)",_pfd.fd,errno,strerror(errno));
        }
        
        if (cnt == 0) break;
        
        debug("[TCP CLIENT] got packet of size %zd",cnt);

        while (cnt>0) {
            ssize_t doSend = MIN(cnt,TCP::TCP_MTU);
            size_t didSend = send_data(bufstart,doSend);
            bufstart += didSend;
            cnt -= didSend;
        }
    }while (remoteDidClose);
    
    if (remoteDidClose) {
        send_fin();
        return false;
    }
    
    return true;
}

void TCP::stopAction() noexcept{
    if (_pfd.fd != -1) shutdown(_pfd.fd, SHUT_RDWR);
}

void TCP::send_tcp(std::uint8_t flags) {
    tcphdr tcp_header{};
    std::unique_lock<std::mutex> ul(_lockStx);

    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seq);
    tcp_header.th_ack = htonl(_stx.ack);

    tcp_header.th_flags = flags;
    tcp_header.th_off = sizeof(tcp_header) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    debug("[TCP OUT] tcp header packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x len=%u",
          _sPort, _dPort, _stx.seq, _stx.ack, flags, 0);
    // Update TCP states
    _stx.acked = _stx.ack;
    _dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}

void TCP::send_ack_nolock(){
    bool doSend = false;
    tcphdr tcp_header{};
    if ((doSend = (_stx.acked != _stx.ack))) {
        debug("Sending tcp ack packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x",
              _sPort, _dPort, _stx.seq, _stx.ack, TH_ACK);

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
}

void TCP::send_rst_nolock(){
    tcphdr tcp_header{};
    debug("Sending tcp rst packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x",
          _sPort, _dPort, _stx.seq, _stx.ack, TH_ACK);

    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seq);
    tcp_header.th_ack = htonl(_stx.ack);
    tcp_header.th_flags = TH_RST;
    tcp_header.th_off = sizeof(tcphdr) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    _dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}

void TCP::send_rst(){
    std::unique_lock<std::mutex> ul(_lockStx);
    return send_rst_nolock();
}

void TCP::send_fin(){
    tcphdr tcp_header{};
    std::unique_lock<std::mutex> ul(_lockStx);

    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seq);
    tcp_header.th_ack = htonl(_stx.ack);
    tcp_header.th_flags = TH_RST;
    tcp_header.th_off = sizeof(tcphdr) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    debug("Sending tcp fin packet: sport=%u dport=%u seq=%u ack=%u flags=0x%x",
          _sPort, _dPort, _stx.seq, _stx.ack, tcp_header.th_flags);

    _dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}

size_t TCP::send_data(void *buf, size_t buflen){
    size_t len = buflen;
    if (!len) return 0;
    tcphdr tcp_header{};
    int64_t rembytes = 0;
    int sendfails = 0;

    constexpr const int Fastpath_Retry_CNT = 1;
    
retry:
    for (int i=0; i<Fastpath_Retry_CNT; i++) {
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
    rembytes = (int64_t)_stx.inWin - unacked;
    if (rembytes<=0) {
        //at this point we *have to* wait for an ACK
        //no smaller payload is possible
        ++sendfails;
        debug("[%d] we have to wait for ACK before sending more data!",sendfails);
        
        // **** Put this thread to sleep until we can send more data **** //
        uint64_t wevent = _canSendEvent.getNextEvent();
        _lockStx.unlock(); //unlocking _lockStx inbetween _canSendEvent locks, makes sure we never lock if we could send data!

        _canSendEvent.waitForEvent(wevent);//this lock will always be "blocking", unless we can send more data
        assure(_connState == CONN_CONNECTED);

        goto retry;
    }
    len = rembytes > buflen ? buflen : rembytes;
cnt_label:
    if (len > TCP_MTU) len = TCP_MTU;
    
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
    debug("Sending tcp payload packet: sport=%u dport=%u seq=%u seqAcked=%u ack=%u flags=0x%x len=%zu rwindow=%u[%u] unacked=%llu",
          htons(tcp_header.th_sport), htons(tcp_header.th_dport), htonl(tcp_header.th_seq), _stx.seqAcked, htonl(tcp_header.th_ack),
          TH_ACK, len, _stx.inWin, _stx.inWin >> 8, unacked);

    _dev->send_packet(USBDevice::MUX_PROTO_TCP, buf, len, &tcp_header);
    _lockStx.unlock();
    return len;
}

void TCP::flush_data(){
    uint32_t lseqAck = 0;
    uint32_t lseq = 0;
    char *bufstart = NULL;
    size_t buflen = 0;
    tcphdr tcp_header{};

    std::unique_lock<std::mutex> ul(_lockStx);

    lseqAck = ((uint64_t)_stx.seqAcked + TCP::bufsize)%TCP::bufsize;
    lseq = ((uint64_t)_stx.seq + TCP::bufsize)%TCP::bufsize;
    bufstart = _payloadBuf+lseqAck;
    buflen = (lseq >= lseqAck) ? lseq-lseqAck : (TCP::bufsize - lseqAck);
    
    if (buflen > TCP_MTU) buflen = TCP_MTU;
    if (!buflen) return;

    tcp_header.th_sport = htons(_sPort);
    tcp_header.th_dport = htons(_dPort);
    tcp_header.th_seq = htonl(_stx.seqAcked);
    tcp_header.th_ack = htonl(_stx.ack);
    tcp_header.th_flags = TH_ACK | TH_FIN;
    tcp_header.th_off = sizeof(tcphdr) / 4;
    tcp_header.th_win = htons(static_cast<std::uint16_t>(_stx.win >> 8));

    debug("Flushing tcp packet: sport=%u dport=%u seq=%u seqAcked=%u ack=%u flags=0x%x len=%zu rwindow=%u[%u]",
          htons(tcp_header.th_sport), htons(tcp_header.th_dport), htonl(tcp_header.th_seq), _stx.seqAcked, htonl(tcp_header.th_ack),
          tcp_header.th_flags, buflen, _stx.inWin, _stx.inWin >> 8);

    _dev->send_packet(USBDevice::MUX_PROTO_TCP, bufstart, buflen, &tcp_header);
}

#pragma mark public

void TCP::kill(int reason) noexcept{
    if (reason) {
        debug("killing TCP connection sport=%d (reason=%d)",_sPort, reason);
    }else{
        debug("killing TCP connection sport=%d (no reason)",_sPort);
    }
    _dev->closeConnection(_sPort);
}

void TCP::deconstruct() noexcept{
    {
        std::unique_lock<std::mutex> ul(_lockStx);
        _connState = CONN_DYING;
        _connStateDidChange.notifyAll();
        _canSendEvent.notifyAll();
    }
    {
        std::unique_lock<std::mutex> ul(_lockClientSend);
        _canClientSendEvent.notifyAll();
    }
}

void TCP::handle_input(tcphdr* tcp_header, uint8_t* payload, uint32_t payload_len){
    uint32_t rSeq = 0;
    uint32_t rAck = 0;
    {
        std::unique_lock<std::mutex> ul(_lockStx);
        debug("[TCP IN] sport=%u dport=%u seq=%u ack=%u flags=0x%x window=%u[%u] len=%u",
            _sPort, _dPort, ntohl(tcp_header->th_seq), ntohl(tcp_header->th_ack), tcp_header->th_flags, ntohs(tcp_header->th_win) << 8, ntohs(tcp_header->th_win), payload_len);
            
        // Update TCP receiver state
        rSeq = ntohl(tcp_header->th_seq);
        rAck = ntohl(tcp_header->th_ack);

        if(_connState == CONN_CONNECTING) {
            if(tcp_header->th_flags == (TH_SYN | TH_ACK)) {
                debug("Received SYN/ACK during device handshake");
                _stx.seq++;
                _stx.ack = rSeq+1; //just copy this on first packet without parsing
                _stx.inWin = ntohs(tcp_header->th_win) << 8;
                _stx.pktForwarded = _stx.ack;
                
                send_ack_nolock();
                _connState = CONN_CONNECTED;
                _connStateDidChange.notifyAll();
            } else {
                retassure(tcp_header->th_flags & TH_RST, "Received unexpected data while connecting");
                _connState = CONN_REFUSED;
                _connStateDidChange.notifyAll();
                info("Connection refused by device");
                kill(__LINE__);
            }
        } else if (_connState == CONN_CONNECTED) {
            if (tcp_header->th_flags == TH_ACK) {
                while (_stx.ack != rSeq) {
                    uint64_t wevent = _canSendEvent.getNextEvent();
                    ul.unlock();
                    _canSendEvent.waitForEvent(wevent);
                    if (_connState != CONN_CONNECTED) return;
                    ul.lock();
                }
                
                _stx.inWin = ntohs(tcp_header->th_win) << 8;
                _stx.seqAcked = rAck; //update ACK on sent packets
                _stx.ack += payload_len;
                if (payload_len && !_canSendEvent.members()){
                    /*
                        We can avoid sending ACK here if we're gonna send data in next packet anyways
                     */
                    send_ack_nolock();
                }
                
                _canSendEvent.notifyAll();
            } else if (tcp_header->th_flags == TH_RST){
                info("Connection reset by device, flags: %u sport=%u dport=%u", tcp_header->th_flags,_sPort,_dPort);
                kill(__LINE__);
                return;
            }else{
                warning("unexpected flags=0x%02x",tcp_header->th_flags);
#ifdef XCODE
            assert(0); //debug this in XCODE
#endif
            }
        } else if (_connState == CONN_REFUSED) {
            return;
        } else if (_connState == CONN_DYING) {
            return;
        } else {
            warning("Data for unexpected connection state: %d",_connState);
    #ifdef XCODE
            assert(0); //debug this in XCODE
    #endif
        }
    }
    
    if (payload_len) {
        std::unique_lock<std::mutex> ul(_lockClientSend);
        while (rSeq != _stx.pktForwarded) {
            uint64_t wevent = _canClientSendEvent.getNextEvent();
            ul.unlock();
            _canClientSendEvent.waitForEvent(wevent);
            if (_connState != CONN_CONNECTED) return;
            ul.lock();
        }
        if (_connState != CONN_CONNECTED) return;
        //forward to client without buffering
        ssize_t didSend = send(_pfd.fd, payload, payload_len, 0);
        if(didSend != payload_len){
            //client died, but don't throw, since it wasn't the devices fault!
            //terminate TCP instead
            error("Failed to send payload to client with didSend=%zd payload_len=%u errno=%d (%s)",didSend,payload_len,errno,strerror(errno));
            kill(__LINE__);
        }
        _stx.pktForwarded += payload_len;
        _canClientSendEvent.notifyAll();
    }
}

void TCP::connect(){
    cleanup([&]{
        _cli = nullptr; //free client
    });

    info("Starting TCP connection clifd=%d",_pfd.fd);

    {
        uint64_t wevent = _connStateDidChange.getNextEvent();
        send_tcp(TH_SYN);
        _connStateDidChange.waitForEvent(wevent);
        retassure(_connState == CONN_CONNECTED, "Failed to establish TCP connection clifd=%d _connState=%d",_pfd.fd,_connState);
    }
    info("TCP Connected to device");
    _cli->send_result(_cli->_connectTag, RESULT_OK);

    _pfd.fd = _cli->_fd; _cli->_fd = -1; //disown client, we take care of this fd now
    startLoop();
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

    debug("[OOL TCP OUT RST] tcp header packet: sport=%u dport=%u", htons(tcp_header.th_sport), htons(tcp_header.th_dport));
    dev->send_packet(USBDevice::MUX_PROTO_TCP, NULL, 0, &tcp_header);
}
