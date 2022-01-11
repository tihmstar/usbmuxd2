//
//  sysconf.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.12.20.
//

#ifndef sysconf_hpp
#define sysconf_hpp

#include <plist/plist.h>
#include <iostream>

plist_t sysconf_get_device_record(const char *udid);
void sysconf_set_device_record(const char *udid, const plist_t record);
void sysconf_remove_device_record(const char *udid);


std::string sysconf_get_system_buid();

#endif /* sysconf_hpp */
