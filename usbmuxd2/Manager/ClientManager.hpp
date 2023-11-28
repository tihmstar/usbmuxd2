//
//  ClientManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 20.07.23.
//

#ifndef ClientManager_hpp
#define ClientManager_hpp

#include "Muxer.hpp"
#include <libgeneral/Manager.hpp>
#include <libgeneral/DeliveryEvent.hpp>

class ClientManager : public tihmstar::Manager{
    Muxer *_mux; //not owned
    uint64_t _clientNumber;
    int _listenfd;
    int _wakePipe[2];
    std::set<Client *> _children; //raw ptr to shared objec
    std::mutex _childrenLck;
    tihmstar::Event _childrenEvent;
    std::thread _cliReaperThread;
    tihmstar::DeliveryEvent<std::shared_ptr<Client>> _reapClients;
    
    virtual void stopAction() noexcept override;
    virtual bool loopEvent() override;
    virtual void afterLoop() noexcept override;

    void reaper_runloop();

    int accept_client();
    void handle_client(int client_fd);    
public:
    ClientManager(Muxer *mux);
    virtual ~ClientManager() override;

    friend Client;
};

#endif /* ClientManager_hpp */
