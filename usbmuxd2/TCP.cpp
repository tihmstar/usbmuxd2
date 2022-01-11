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

TCP::TCP(uint16_t sPort, uint16_t dPort, std::weak_ptr<USBDevice> dev, std::shared_ptr<Client> cli)
: _sPort(sPort), _dPort(dPort), _dev(dev), _cli(cli), _payloadBuf(NULL), _cfd(-1),
_stx{0,0,0,0,0,131072}
{
    debug("[TCP] (%d) creating connection for sport=%u",cli->_fd,_sPort);
    assure(_payloadBuf = (char*)malloc(TCP::bufsize));

    _stx.seqAcked = _stx.seq = (uint32_t)random();
}

TCP::~TCP(){
    safeFree(_payloadBuf);
    safeClose(_cfd);
}

void TCP::connect(){
    bool connectionWasSuccessfull = false;
    cleanup([&]{
        if (connectionWasSuccessfull){
            _cli->_fd = -1; //disown client, we take care of this fd now
            _cli->stopLoop(); //shutdown client
            _cli = nullptr; //free client
        }else{
            _cfd = -1;
        }
    });
    _cfd = _cli->_fd;
#warning TODO: make sure cli Client actually gets freed!
    
    
    reterror("todo");
    connectionWasSuccessfull = true;
}
