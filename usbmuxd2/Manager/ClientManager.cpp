//
//  ClientManager.cpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#include "ClientManager.hpp"
#include <libgeneral/macros.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include "Client.hpp"
#include <memory>
#include <poll.h>

#ifdef SOCKET_PATH
static const char *socket_path = SOCKET_PATH;
#else
static const char *socket_path = "/var/run/usbmuxd";
#endif

#pragma mark ClientManager
ClientManager::ClientManager(Muxer *mux)
: _mux(mux)
, _clientNumber(0), _listenfd(-1)
,_wakePipe{}
{
    struct sockaddr_un bind_addr = {};
    
    retassure(unlink(socket_path) != 1 || errno == ENOENT, "unlink(%s) failed: %s", socket_path, strerror(errno));
    
    retassure((_listenfd = socket(AF_UNIX, SOCK_STREAM, 0))>=0, "socket() failed: %s", strerror(errno));
    
    bind_addr.sun_family = AF_UNIX;
    strcpy(bind_addr.sun_path, socket_path);
    retassure(!bind(_listenfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)), "bind() failed: %s", strerror(errno));
    
    retassure(!listen(_listenfd, 5), "listen() failed: %s", strerror(errno));
    
    assure(!chmod(socket_path, 0666));
    
    assure(!pipe(_wakePipe));
    
    _cliReaperThread = std::thread([this]{
        reaper_runloop();
    });
}

ClientManager::~ClientManager(){
    info("[destroying] ClientManager");
    stopLoop();
    
    safeClose(_wakePipe[0]);
    safeClose(_wakePipe[1]);

    if (_children.size()) {
        debug("waiting for client children to die...");
        std::unique_lock<std::mutex> ul(_childrenLck);
        while (size_t s = _children.size()) {
            for (auto c : _children) c->kill();
            uint64_t wevent = _childrenEvent.getNextEvent();
            ul.unlock();
            debug("Need to kill %zu more client children",s);
            _childrenEvent.waitForEvent(wevent);
            ul.lock();
        }
    }
    _reapClients.kill();
    _cliReaperThread.join();

    if (_listenfd > 0) {
        int cfd = _listenfd; _listenfd = -1;
        close(cfd);
    }
}

void ClientManager::stopAction() noexcept{
    if (_listenfd) shutdown(_listenfd, SHUT_RDWR);
    safeClose(_wakePipe[1]);
}

bool ClientManager::loopEvent(){
    int cfd = 0;
    
    cfd = accept_client();
    if (cfd == -1) return true;
    try {
        handle_client(cfd); //always consumes cfd
    } catch (tihmstar::exception &e) {
        error("failed to handle client %d with error=%d",cfd,e.code());
    }
    return true;
}

void ClientManager::afterLoop() noexcept{
    if (_listenfd > 0) {
        int cfd = _listenfd; _listenfd = -1;
        close(cfd);
    }
}

void ClientManager::reaper_runloop(){
    while (true) {
        std::shared_ptr<Client> cli;
        try {
            cli = _reapClients.wait();
        } catch (...) {
            break;
        }
        //make device go out of scope so it can die in piece
        cli->deconstruct();
    }
}

int ClientManager::accept_client(){
    struct sockaddr_un addr = {};
    int err = 0;
    int cfd = 0;
    socklen_t len = 0;
    len = sizeof(struct sockaddr_un);
    struct pollfd pfd[2] = {
        {
            .fd = _listenfd,
            .events = POLLIN
        },
        {
            .fd = _wakePipe[0],
            .events = POLLIN
        }
    };
    if ((err = poll(pfd,2,-1)) == -1){
        retassure(errno == EINTR, "[CLIENTMANAGER] poll failed errno=%d (%s)",errno,strerror(errno));
        return -1;
    }
    retassure(!(pfd[1].revents & POLLHUP), "graceful kill requested");
    retassure(pfd[0].revents & POLLIN, "poll returned, but there is no POLLIN event on client");
    retassure((cfd = accept(_listenfd, (struct sockaddr *)&addr, &len))>=0, "accept() failed (%s)", strerror(errno));
    
    return cfd;
}

void ClientManager::handle_client(int client_fd){
    std::shared_ptr<Client> client = nullptr;
    cleanup([&]{
        if (client_fd > 0) {
            close(client_fd); client_fd = -1;
        }
    });
    
    try {
        client = std::make_shared<Client>(_mux,this, client_fd,_clientNumber++); client_fd = 0;
        client->_selfref = client;
    } catch (tihmstar::exception &e) {
        reterror("failed to handle client with error=%d",e.code());
    }

    {
        std::unique_lock<std::mutex> ul(_childrenLck);
        _children.insert(client.get());
    }
    
    //transfer ownership to muxer
    _mux->add_client(client); client = NULL;
}
