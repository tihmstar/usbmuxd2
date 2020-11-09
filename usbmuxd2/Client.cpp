//
//  Client.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "Client.hpp"
#include <log.h>
#include <libgeneral/macros.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sysconf/sysconf.hpp>
#include <system_error>


Client::Client(Muxer *mux, int fd, uint64_t number)
    :  _muxer(mux), _recvbuffer(NULL), _info{}, _killInProcess(false), _wlock{}, _recvbufferSize(0), _number(number), _fd(fd),
        _proto_version(0), _isListening(false)
{
    debug("[allocing] client (%p) %d",this,_fd);
    const int bufsize = Client::bufsize;
    constexpr int yes = 1;


    assure(!pthread_mutex_init(&_wlock, 0));
    
    _recvbuffer = (char*)malloc(bufsize);
    hdr = (usbmuxd_header*)_recvbuffer;
    
    if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) == -1) {
        warning("Could not set send buffer for client socket");
    }
    
    if (setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int)) == -1) {
        warning("Could not set receive buffer for client socket");
    }
    
    setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&yes, sizeof(int));
#ifdef __APPLE__
    setsockopt(_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&yes, sizeof(int));
#endif
}

Client::~Client(){
#ifdef DEBUG
    if (!_killInProcess) {
        error("THIS DESTRUCTOR IS NOT MEANT TO BE CALLED OTHER THAN THROUGH kill()!!");
    }
#endif
    
    _muxer->delete_client(this); //triggers kill, but that's fine
    safeFree(_info.bundleID);
    safeFree(_info.clientVersionString);
    safeFree(_info.progName);
    
    if (_fd>0) {
        int cfd = _fd; _fd = -1;
        close(cfd);
    }
    stopLoop();

    safeFree(_recvbuffer);
    debug("[deleted] client (%p) %d",this,_fd);
}

void Client::loopEvent(){
    _recvbufferSize = 0;
    try {
        recv_data();
    } catch (tihmstar::exception &e) {
        error("failed to recv_data on client %d with error=%s code=%d",_fd,e.what(),e.code());
        this->kill(); //safe to call multiple times
        throw; //immediately terminate this thread
    }
}

void Client::update_client_info(const plist_t dict){
    plist_t node = NULL;
    if ((node = plist_dict_get_item(dict, "ClientVersionString")) && (plist_get_node_type(node) == PLIST_STRING)) {
        plist_get_string_val(node, &_info.clientVersionString);
    }
    
    if ((node = plist_dict_get_item(dict, "BundleID")) && (plist_get_node_type(node) == PLIST_STRING)) {
        plist_get_string_val(node, &_info.bundleID);
    }
    
    if ((node = plist_dict_get_item(dict, "ProgName")) && (plist_get_node_type(node) == PLIST_STRING)) {
        plist_get_string_val(node, &_info.progName);
    }
    
    if ((node = plist_dict_get_item(dict, "kLibUSBMuxVersion")) && (plist_get_node_type(node) == PLIST_UINT)) {
        plist_get_uint_val(node, &_info.kLibUSBMuxVersion);
    }
}

void Client::readData(){
    ssize_t got = 0;
    got = recv(_fd, _recvbuffer+_recvbufferSize, Client::bufsize-_recvbufferSize, 0);
    if (got == 0) {
        reterror("client %d disconnected!",_fd);
    }
    assure(got > 0);
    _recvbufferSize+=got;
}

void Client::recv_data(){    
    readData();
    
    if (_recvbufferSize < sizeof(usbmuxd_header)) {
        readData();
        retassure(_recvbufferSize >= sizeof(struct usbmuxd_header), "message is too short for header");
    }
    
    while(_recvbufferSize < hdr->length) {
        readData();
        retassure(_recvbufferSize<Client::bufsize, "no more space to read");
    }
    
    processData(hdr);
}

void Client::kill() noexcept{
    //sets _killInProcess to true and executes if statement if it was false before
    if (!_killInProcess.exchange(true)) {
    
        std::thread delthread([this](){
#ifdef DEBUG
            debug("killing client (%p) %d",this,_fd);
#else
            info("killing client %d",_fd);
#endif
            delete this;
        });
        delthread.detach();
        
    }
}

void Client::processData(usbmuxd_header *hdr){
    plist_t p_recieved = NULL;
    cleanup([&]{
        safeFreeCustom(p_recieved, plist_free);
    });
    const char *payload = NULL; //not alloced
    const struct usbmuxd_connect_request *conn_req = NULL; //not allocated
    
    uint32_t payload_size = 0;
    uint16_t portnum = 0;
    uint32_t device_id = 0;
    
    std::string message;
    
    debug("Client command in fd %d len %d ver %d msg %d tag %d", _fd, hdr->length, hdr->version, hdr->message, hdr->tag);
    
    if((hdr->version != 0) && (hdr->version != 1)) {
        info("Client %d version mismatch: expected 0 or 1, got %d", _fd, hdr->version);
        send_result(hdr->tag, RESULT_BADVERSION);
        return;
    }
    
    switch(hdr->message) {
        case MESSAGE_PLIST:
        {
            _proto_version = 1;
            payload = (char*)(hdr) + sizeof(struct usbmuxd_header);
            payload_size = hdr->length - sizeof(struct usbmuxd_header);
            
            plist_from_xml(payload, payload_size, &p_recieved);
            
            {
                plist_t p_messageType = NULL;
                const char *str = NULL;
                uint64_t str_len = 0;
                
                retassure(p_messageType = plist_dict_get_item(p_recieved, "MessageType"), "Failed to get MessageType from recieved plist");
                
                retassure(str = plist_get_string_ptr(p_messageType, &str_len), "Failed to get str ptr from MessageType");
                
                message = std::string(str,str_len);
            }
            
            update_client_info(p_recieved);
            
            if (message == "Listen") {
                goto PLIST_CLIENT_LISTEN_LOC;
            } else if (message == "Connect") {
                plist_t p_intval = NULL;
                
                // get device id
                try {
                    uint64_t tmpDeviceID = 0;
                    assure(p_intval = plist_dict_get_item(p_recieved, "DeviceID"));
                    assure(plist_get_node_type(p_intval) == PLIST_UINT);
                    
                    plist_get_uint_val(p_intval, &tmpDeviceID);
                    device_id = (uint32_t)tmpDeviceID;
                } catch (tihmstar::exception &e) {
                    error("Received connect request without device_id!");
                    send_result(hdr->tag, RESULT_BADDEV);
                    return;
                }
                
                // get port number
                try {
                    uint64_t tmpPortNumber = 0;
                    assure(p_intval = plist_dict_get_item(p_recieved, "PortNumber"));
                    assure(plist_get_node_type(p_intval) == PLIST_UINT);
                    
                    plist_get_uint_val(p_intval, &tmpPortNumber);
                    portnum = ntohs((uint16_t)tmpPortNumber);
                } catch (tihmstar::exception &e) {
                    error("Received connect request without port number!");
                    send_result(hdr->tag, RESULT_BADDEV);
                    return;
                }
                
                goto PLIST_CLIENT_CONNECTION_LOC;
            } else if (message == "ListDevices") {
                _muxer->send_deviceList(this, hdr->tag);
                return;
            } else if (message == "ReadBUID") {
                plist_t p_rsp = NULL;
                cleanup([&]{
                    safeFreeCustom(p_rsp, plist_free);
                });
                std::string buid = sysconf_get_system_buid();
                p_rsp = plist_new_dict();
                plist_dict_set_item(p_rsp, "BUID", plist_new_string(buid.c_str()));
                send_plist_pkt(hdr->tag, p_rsp);
                return;
            } else if (message == "ReadPairRecord") {
                plist_t p_devrecord = NULL;
                plist_t p_rsp = NULL;
                cleanup([&]{
                    safeFreeCustom(p_devrecord, plist_free);
                    safeFreeCustom(p_rsp, plist_free);
                });
                std::string record_id;
                plist_t p_recordid = NULL;
                
                // get pair record id
                try {
                    const char *str = NULL;
                    uint64_t str_len = 0;

                    assure(p_recordid = plist_dict_get_item(p_recieved, "PairRecordID"));
                    retassure(str = plist_get_string_ptr(p_recordid, &str_len), "Failed to get str ptr from PairRecordID");
                    
                    record_id = std::string(str,str_len);
                } catch (tihmstar::exception &e) {
                    error("Reading record id failed!");
                    send_result(hdr->tag, EINVAL);
                    return;
                }
                
                try {
                    p_devrecord = sysconf_get_device_record(record_id.c_str());
                } catch (tihmstar::exception &e) {
                    info("no record data found for device %s",record_id.c_str());
                    send_result(hdr->tag, ENOENT);
                    return;
                }
                p_rsp = plist_new_dict();
                {
                    char *plistbin = NULL;
                    uint32_t plistbin_len = 0;
                    cleanup([&]{
                        safeFreeCustom(plistbin, plist_to_bin_free);
                    });
                    plist_to_bin(p_devrecord, &plistbin, &plistbin_len);
                    plist_dict_set_item(p_rsp, "PairRecordData", plist_new_data(plistbin, plistbin_len));
                }
                send_plist_pkt(hdr->tag, p_rsp);
                return;
            } else if (message == "SavePairRecord") {
                plist_t p_parsedPairRecord = NULL;
                cleanup([&]{
                    safeFreeCustom(p_parsedPairRecord, plist_free);
                });
                const char *pairRecord = NULL;
                uint64_t pairRecord_len = 0;
                plist_t p_pairRecord = NULL;
                std::string record_id;
                
                // get pair record id
                try {
                    const char *str = NULL;
                    uint64_t str_len = 0;
                    plist_t p_recordid = NULL;
                    assure(p_recordid = plist_dict_get_item(p_recieved, "PairRecordID"));
                    
                    retassure(str = plist_get_string_ptr(p_recordid, &str_len), "Failed to get str ptr for PairRecordID");
                    record_id = std::string(str,str_len);
                    
                    assure(p_pairRecord = plist_dict_get_item(p_recieved, "PairRecordData"));
                } catch (tihmstar::exception &e) {
                    error("Reading record id or record data failed!");
                    send_result(hdr->tag, EINVAL);
                    return;
                }
                
                retassure(pairRecord = plist_get_data_ptr(p_pairRecord, &pairRecord_len), "Failed to get data ptr for PairRecordData");

                plist_from_memory(pairRecord, (uint32_t)pairRecord_len, &p_parsedPairRecord);
                retassure(p_parsedPairRecord, "Failed to plist-parse received PairRecordData");
                
                
                sysconf_set_device_record(record_id.c_str(), p_parsedPairRecord);
                
                {
                    plist_t p_intval = NULL;
                    uint64_t intval = 0;
                    
                    assure(p_intval = plist_dict_get_item(p_recieved, "DeviceID"));
                    assure(plist_get_node_type(p_intval) == PLIST_UINT);
                    
                    plist_get_uint_val(p_intval, &intval);
                    _muxer->notify_device_paired((int)intval);
                }
                
                send_result(hdr->tag, RESULT_OK);
                return;
            } else if (message == "DeletePairRecord") {
                std::string record_id;
                
                // get pair record id
                try {
                    const char *str = NULL;
                    uint64_t str_len = 0;
                    plist_t p_recordid = NULL;
                    
                    assure(p_recordid = plist_dict_get_item(p_recieved, "PairRecordID"));
                    
                    retassure(str = plist_get_string_ptr(p_recordid, &str_len), "Failed to get str ptr for PairRecordID");
                    record_id = std::string(str,str_len);
                } catch (tihmstar::exception &e) {
                    error("Reading record id failed!");
                    send_result(hdr->tag, EINVAL);
                    return;
                }
                sysconf_remove_device_record(record_id.c_str());
                send_result(hdr->tag, RESULT_OK);
                return;
            }else if (message == "ListListeners") {
#warning UNTESTED
                _muxer->send_listenerList(this, hdr->tag);
                return;
            }else{
                error("Unexpected command '%s' received!", message.c_str());
                send_result(hdr->tag, RESULT_BADCOMMAND);
                return;
            }
            assert(0); //should not be reached?!
        }
        case MESSAGE_LISTEN:
            goto PLIST_CLIENT_LISTEN_LOC;
        case MESSAGE_CONNECT:
            conn_req = (usbmuxd_connect_request*)hdr;
            portnum = conn_req->port;
            device_id = conn_req->device_id;
            goto PLIST_CLIENT_CONNECTION_LOC;
        default:
            error("Client %d invalid command %d", _fd, hdr->message);
            send_result(hdr->tag, RESULT_BADCOMMAND);
            return;
    }
    reterror("we should not get here :o");
    
PLIST_CLIENT_CONNECTION_LOC:
    debug("Client %d connection request to device %d port %d", _fd, device_id, portnum);
    try {
        //transfer socket ownership to device!
        _muxer->start_connect(device_id, portnum, this);
    } catch (tihmstar::exception &e) {
        send_result(hdr->tag, RESULT_CONNREFUSED);
        return;
    }
    reterror("graceful kill");
    
PLIST_CLIENT_LISTEN_LOC:
    send_result(hdr->tag, RESULT_OK);
    debug("Client %d now LISTENING", _fd);
    _isListening = true;
    _muxer->notify_alldevices(this); //inform client about all connected devices
    return;
}

void Client::writeData(struct usbmuxd_header *hdr, void *buf, size_t buflen){
    ssize_t sent = 0;
    bool didGetLock = false;
    
    cleanup([&]{ //cleanup only code
        if (didGetLock)//unlock mutex if we locked it!
            pthread_mutex_unlock(&_wlock);
    });
    
    assure(!pthread_mutex_lock(&_wlock)); didGetLock = true;
    
    assure((sent = send(_fd, hdr, sizeof(usbmuxd_header), 0)) == sizeof(usbmuxd_header));
    assure((sent = send(_fd, buf, buflen, 0)) == buflen);
}

void Client::send_pkt(uint32_t tag, enum usbmuxd_msgtype msg, void *payload, int payload_length){
    struct usbmuxd_header hdr;
    hdr.version = _proto_version;
    hdr.length = sizeof(hdr) + payload_length;
    hdr.message = msg;
    hdr.tag = tag;
    debug("send_pkt fd %d tag %d msg %d payload_length %d", _fd, tag, msg, payload_length);
    writeData(&hdr, payload, payload_length);
}

void Client::send_plist_pkt(uint32_t tag, plist_t plist){
    char *xml = NULL;
    uint32_t xmlsize = 0;
    cleanup([&](){ //cleanup only code
        safeFree(xml);
    });
    
    plist_to_xml(plist, &xml, &xmlsize);
    send_pkt(tag, MESSAGE_PLIST, xml, xmlsize);
}

void Client::send_result(uint32_t tag, uint32_t result){
    plist_t dict = NULL;
    cleanup([&]{
        if (dict) {
            plist_free(dict);
        }
    });
    if (_proto_version == 1) {
        /* XML plist packet */
        dict = plist_new_dict();
        plist_dict_set_item(dict, "MessageType", plist_new_string("Result"));
        plist_dict_set_item(dict, "Number", plist_new_uint(result));
        send_plist_pkt(tag, dict);
    } else {
        /* binary packet */
        send_pkt(tag, MESSAGE_RESULT, &result, sizeof(uint32_t));
    }
}
