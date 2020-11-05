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

void Client::update_client_info(plist_t dict){
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
    
        {
        thread_retry:
            try {
                std::thread delthread([this](){
        #ifdef DEBUG
                    debug("killing client (%p) %d",this,_fd);
        #else
                    info("killing client %d",_fd);
        #endif
                    delete this;
                });
                delthread.detach();
            } catch (std::system_error &e) {
                if (e.code() == std::errc::resource_unavailable_try_again) {
                    error("[THREAD] creating thread threw EAGAIN! retrying in 5 seconds...");
                    sleep(5);
                    goto thread_retry;
                }
                error("[THREAD] got unhandled std::system_error %d (%s)",e.code().value(),e.exception::what());
                throw;
            }
        }
        
    }
}

void Client::processData(usbmuxd_header *hdr){
    plist_t pTmp = NULL;
    PList::Dictionary *dict = nullptr;
    cleanup([&]{
        if (pTmp) {
            plist_free(pTmp);
        }
        if (dict) {
            delete dict;
        }
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
            
            plist_from_xml(payload, payload_size, &pTmp);
            {
                auto somenode = PList::Node::FromPlist(pTmp);
                dict = dynamic_cast<PList::Dictionary*>(somenode);
            }
            pTmp = NULL; //ownership held by dict now
            retassure(dict, "Could not parse plist from payload!");
            
            {
                PList::String *pMessage = nullptr;
                assert(pMessage = dynamic_cast<PList::String*>((*dict)["MessageType"]));
                message = pMessage->GetValue();
            }
            update_client_info(dict->GetPlist());
            
            if (message == "Listen") {
                goto PLIST_CLIENT_LISTEN_LOC;
            } else if (message == "Connect") {
                PList::Integer *pInt = nullptr; //not allocated
                
                // get device id
                try {
                    assure(pInt = dynamic_cast<PList::Integer*>((*dict)["DeviceID"]));
                    device_id = (uint16_t)pInt->GetValue();
                } catch (tihmstar::exception &e) {
                    error("Received connect request without device_id!");
                    send_result(hdr->tag, RESULT_BADDEV);
                    return;
                }
                
                // get port number
                try {
                    assure(pInt = dynamic_cast<PList::Integer*>((*dict)["PortNumber"]));
                    portnum = ntohs((uint16_t)pInt->GetValue());
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
                std::string buid = sysconf_get_system_buid();
                PList::Dictionary rspDict;
                rspDict.Set("BUID", PList::String(buid));
                send_plist_pkt(hdr->tag, rspDict.GetPlist());
                return;
            } else if (message == "ReadPairRecord") {
                PList::String *pRecord_id = nullptr;
                std::string record_id;
                PList::Dictionary *devrecord = nullptr;
                cleanup([&]{
                    if (devrecord) {
                        delete devrecord;
                    }
                });
                
                // get pair record id
                try {
                    assure(pRecord_id = dynamic_cast<PList::String*>((*dict)["PairRecordID"]));
                    record_id = pRecord_id->GetValue();
                } catch (tihmstar::exception &e) {
                    error("Reading record id failed!");
                    send_result(hdr->tag, EINVAL);
                    return;
                }
                
                try {
                    devrecord = sysconf_get_device_record(record_id.c_str());
                } catch (tihmstar::exception &e) {
                    info("no record data found for device %s",record_id.c_str());
                    send_result(hdr->tag, ENOENT);
                    return;
                }
                
                PList::Dictionary rspDict;
                rspDict.Set("PairRecordData", PList::Data(devrecord->ToBin()));
                send_plist_pkt(hdr->tag, rspDict.GetPlist());
                return;
            } else if (message == "SavePairRecord") {
                PList::Integer *pDevID = nullptr;
                PList::String *pRecord_id = nullptr;
                PList::Data *pPairRecord = nullptr;
                std::string record_id;
                std::vector<char> pairRecord;
                constexpr const char bplist[] = "bplist00";
                bool isBinaryPlist = true;
                PList::Dictionary *pRecordData = nullptr;
                cleanup([&]{
                    if (pRecordData) {
                        delete pRecordData;
                    }
                });
                // get pair record id
                try {
                    assure(pRecord_id = dynamic_cast<PList::String*>((*dict)["PairRecordID"]));
                    record_id = pRecord_id->GetValue();
                    assure(pPairRecord = dynamic_cast<PList::Data*>((*dict)["PairRecordData"]));
                    pairRecord = pPairRecord->GetValue();
                } catch (tihmstar::exception &e) {
                    error("Reading record id or record data failed!");
                    send_result(hdr->tag, EINVAL);
                    return;
                }
                
                try {
                    for (int i=0; i<sizeof(bplist)-1; i++) {
                        //if this throws
                        if (pairRecord.at(i) != bplist[i]) {
                            isBinaryPlist = false;
                            break;
                        }
                    }
                } catch (std::exception &e) {
                    reterror("pair record too short to be a plist");
                }
                if (isBinaryPlist) {
                    auto somenode = PList::Structure::FromBin(pairRecord);
                    pRecordData = dynamic_cast<PList::Dictionary*>(somenode);
                }else{
                    //yea it's converting to plist and back to string in the next function call,
                    //but this way we at least check if plist is valid
                    std::string xmlrecord{pairRecord.begin(),pairRecord.end()};
                    auto somenode = PList::Structure::FromXml(xmlrecord);
                    pRecordData = dynamic_cast<PList::Dictionary*>(somenode);
                }
                
                sysconf_set_device_record(record_id.c_str(), pRecordData);
                
                assure(pDevID = dynamic_cast<PList::Integer*>((*dict)["DeviceID"]));
                _muxer->notify_device_paired((int)pDevID->GetValue());
                
                send_result(hdr->tag, RESULT_OK);
                return;
            } else if (message == "DeletePairRecord") {
                PList::String *pRecord_id = nullptr;
                std::string record_id;
                
                // get pair record id
                try {
                    assure(pRecord_id = dynamic_cast<PList::String*>((*dict)["PairRecordID"]));
                    record_id = pRecord_id->GetValue();
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
