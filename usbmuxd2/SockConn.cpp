//
//  SockConn.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//


#include "SockConn.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <libgeneral/macros.h>
#include "Client.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <system_error>


SockConn::SockConn(std::string ipaddr, uint16_t dPort, std::shared_ptr<Client> cli) 
: _ipaddr(ipaddr), _cli(cli), _dPort(dPort), _killInProcess(false), _didConnect(false), _cfd(-1), _dfd(-1), _pfds(NULL)
{

}

SockConn::~SockConn(){
	debug("Destroying SockConn (%p) dPort=%u",this,_dPort);
	auto fdlist = {_cfd,_dfd};
	_cfd = -1; 
	_dfd = -1;

	for (int fd : fdlist){
		if (fd>0) {
			debug("~SockConn(%p) closing(%d)",this,fd);
        	close(fd);
  		}
	}
	safeFree(_pfds);
}

void SockConn::connect(){
	cleanup([&]{
        _didConnect = true; // make sure destructor knows this function was called and returned
    });
	int err = 0;
	struct sockaddr_in devaddr = {};

	retassure((_dfd = socket(AF_INET, SOCK_STREAM, 0))>0, "failed to create socket");

	devaddr.sin_family = AF_INET;
    if ((devaddr.sin_addr.s_addr = inet_addr(_ipaddr.c_str())) == (in_addr_t)-1){
        struct hostent *he = NULL;
        struct in_addr **addr_list = NULL;
        assure(he = gethostbyname(_ipaddr.c_str()));
        
        addr_list = (struct in_addr **)he->h_addr_list;
        for (int i=0; addr_list[i] != NULL; i++) {
            _ipaddr = inet_ntoa(*addr_list[i]);
            if ((devaddr.sin_addr.s_addr = inet_addr(_ipaddr.c_str())) != (in_addr_t)-1) {
                break;
            }
        }
        if (devaddr.sin_addr.s_addr == (in_addr_t)-1)
            reterror("failed to resolve to ip address");
    }
	devaddr.sin_port = htons(_dPort);

	retassure(!(err = ::connect(_dfd, (sockaddr*)&devaddr, sizeof(devaddr))), "failed to connect to device on port=%d with err=%d errno=%d(%s)",_dPort,err,errno,strerror(errno));
	
    _cli->send_result(_cli->_connectTag, RESULT_OK);
    _cfd = _cli->_fd;

    _cli->_fd = -1; //discard file descriptor, because the device (TCP) owns it now
    _cli = nullptr; // we don't need to keep a pointer in here anymore!
    debug("SockConn connected _cfd=%d _dfd=%d",_cfd,_dfd);

    _pfds = (struct pollfd*) malloc(sizeof(struct pollfd)*2);
    _pfds[0].fd = _cfd;
	_pfds[0].events = POLLIN;
    _pfds[1].fd = _dfd;
	_pfds[1].events = POLLIN;

    startLoop();
}

void SockConn::kill() noexcept{
    //sets _killInProcess to true and executes if statement if it was false before
    if (!_killInProcess.exchange(true)) {
        
        std::thread delthread([this](){
#ifdef DEBUG
            debug("killing SockConn (%p) C=%d D=%d",this,_cfd,_dfd);
#else
            info("killing SockConn C=%d D=%d",_cfd,_dfd);
#endif
            delete this;
        });
        delthread.detach();   
    }
}

void SockConn::loopEvent(){
	int err = 0;
	ssize_t cnt = 0;
	char buf[0x4000];
	assure((err = poll(_pfds,2,-1)) != -1);

	for (int i = 0; i < 2; ++i){
		retassure((_pfds[i].revents & (~POLLIN)) == 0, "bad poll revents=0x%02x for fd=%d",_pfds[i].revents,i);
		if (_pfds[i].revents & POLLIN){
			retassure((cnt = read(_pfds[i].fd, buf, sizeof(buf)))>0, "read failed on fd=%d with cnt=%lld err=%s",_pfds[i],cnt,strerror(errno));
			retassure((cnt = write(_pfds[(i+1)&1].fd, buf, cnt))>0, "send failed on fd=%d with cnt=%lld err=%s",_pfds[i],cnt,strerror(errno));
		}
	}
}

void SockConn::afterLoop() noexcept{
	kill();
}
