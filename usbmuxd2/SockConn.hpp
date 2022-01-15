//
//  SockConn.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef SockConn_hpp
#define SockConn_hpp

#include <libgeneral/Manager.hpp>
#include <atomic>

class Client;

class SockConn : tihmstar::Manager{
	std::string _ipaddr;
    std::shared_ptr<Client> _cli;
    uint16_t _dPort;
	std::atomic_bool _killInProcess;
    std::atomic_bool _didConnect;
    int _cfd; //client socket lifetime managed by this class
    int _dfd; //device socket also managed
    struct pollfd *_pfds;

	virtual void loopEvent() override;
    virtual void afterLoop() noexcept override;
	~SockConn();
public:
	SockConn(std::string ipaddr, uint16_t dPort, std::shared_ptr<Client> cli);

	void connect();

	void kill() noexcept;
};


#endif /* SockConn_hpp */
