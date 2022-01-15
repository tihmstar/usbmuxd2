//
//  ClientManager.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.12.20.
//

#include "ClientManager.hpp"
#include <libgeneral/macros.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include "Client.hpp"
#include <memory>

#ifdef SOCKET_PATH
static const char *socket_path = SOCKET_PATH;
#else
static const char *socket_path = "/var/run/usbmuxd";
#endif

ClientManager::ClientManager(std::shared_ptr<gref_Muxer> mux)
: _mux(mux), _clientNumber(0), _listenfd(-1)
{
    struct sockaddr_un bind_addr = {};
    
    retassure(unlink(socket_path) != 1 || errno == ENOENT, "unlink(%s) failed: %s", socket_path, strerror(errno));
    
    retassure((_listenfd = socket(AF_UNIX, SOCK_STREAM, 0))>=0, "socket() failed: %s", strerror(errno));
    
    bind_addr.sun_family = AF_UNIX;
    strcpy(bind_addr.sun_path, socket_path);
    retassure(!bind(_listenfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)), "bind() failed: %s", strerror(errno));
    
    retassure(!listen(_listenfd, 5), "listen() failed: %s", strerror(errno));
    
    assure(!chmod(socket_path, 0666));
}

ClientManager::~ClientManager(){
    info("[destroying] ClientManager");
    stopLoop();
    
    if (_listenfd > 0) {
        int cfd = _listenfd; _listenfd = -1;
        close(cfd);
    }
}

void ClientManager::stopAction() noexcept{
    if (_listenfd) shutdown(_listenfd, SHUT_RDWR);
}

void ClientManager::loopEvent(){
    int cfd = 0;
    
    cfd = accept_client();

    try {
        handle_client(cfd); //always consumes cfd
    } catch (tihmstar::exception &e) {
        error("failed to handle client %d with error=%d",cfd,e.code());
    }
}

int ClientManager::accept_client(){
    struct sockaddr_un addr = {};
    int cfd = 0;
    socklen_t len = 0;
    
    len = sizeof(struct sockaddr_un);
    
    retassure((cfd = accept(_listenfd, (struct sockaddr *)&addr, &len))>=0, "accept() failed (%s)", strerror(errno));
    
    return cfd;
}

void ClientManager::handle_client(int client_fd){
    std::shared_ptr<Client> client = nullptr;
    cleanup([&]{
        if (client_fd > 0) {
            close(client_fd);
        }
    });
    
    try {
        client = std::make_shared<Client>(_mux,client_fd,_clientNumber++); client_fd = 0;
        client->_selfref = client;
    } catch (tihmstar::exception &e) {
        reterror("failed to handle client with error=%d",e.code());
    }

    //transfer ownership to muxer
    (*_mux)->add_client(client); client = NULL;
}
