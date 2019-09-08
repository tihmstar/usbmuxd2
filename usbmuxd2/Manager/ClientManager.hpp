//
//  ClientManager.hpp
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef ClientManager_hpp
#define ClientManager_hpp

#include <stdint.h>
#include <Muxer.hpp>
#include <Manager/Manager.hpp>

class ClientManager : public Manager{
    Muxer *_mux; //unmanaged
    uint64_t _clientNumber;
    int _listenfd;
    
    virtual void loopEvent() override;
    virtual void stopAction() noexcept override; 

    
    int accept_client();
    void handle_client(int client_fd);
    
public:
    ClientManager(Muxer *mux);
    
    virtual ~ClientManager() override;
};

#endif /* ClientManager_hpp */
