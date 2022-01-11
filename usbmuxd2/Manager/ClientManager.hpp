//
//  ClientManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.12.20.
//

#ifndef ClientManager_hpp
#define ClientManager_hpp

#include <libgeneral/Manager.hpp>
#include "Muxer.hpp"

class ClientManager : public tihmstar::Manager{
    std::shared_ptr<gref_Muxer> _mux;
    uint64_t _clientNumber;
    int _listenfd;
    
    virtual void stopAction() noexcept override;
    virtual void loopEvent() override;

    
    int accept_client();
    void handle_client(int client_fd);
    
public:
    ClientManager(std::shared_ptr<gref_Muxer> mux);
    virtual ~ClientManager() override;

};

#endif /* ClientManager_hpp */
