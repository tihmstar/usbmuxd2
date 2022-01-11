//
//  usbmuxd2-proto.h
//  usbmuxd2
//
//  Created by tihmstar on 18.12.20.
//

#ifndef usbmuxd2_proto_h
#define usbmuxd2_proto_h

#ifdef __cplusplus
extern "C"{
#endif
    
#include <stdint.h>
    
    enum usbmuxd_result {
        RESULT_OK = 0,
        RESULT_BADCOMMAND = 1,
        RESULT_BADDEV = 2,
        RESULT_CONNREFUSED = 3,
        // ???
        // ???
        RESULT_BADVERSION = 6,
    };
    
    struct usbmuxd_header {
        uint32_t length;    // length of message, including header
        uint32_t version;   // protocol version
        uint32_t message;   // message type
        uint32_t tag;       // responses to this query will echo back this tag
    } __attribute__((__packed__));
    
    enum usbmuxd_msgtype {
        MESSAGE_RESULT  = 1,
        MESSAGE_CONNECT = 2,
        MESSAGE_LISTEN = 3,
        MESSAGE_DEVICE_ADD = 4,
        MESSAGE_DEVICE_REMOVE = 5,
        MESSAGE_DEVICE_PAIRED = 6,
        //???
        MESSAGE_PLIST = 8,
    };
    
    struct usbmuxd_connect_request {
        struct usbmuxd_header header;
        uint32_t device_id;
        uint16_t port;   // TCP port number
        uint16_t reserved;   // set to zero
    } __attribute__((__packed__));
    
#ifdef __cplusplus
};
#endif

#endif /* usbmuxd2_proto_h */
