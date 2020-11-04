//
//  sysconf.hpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef sysconf_hpp
#define sysconf_hpp


#include <plist/plist++.h>
#include <string>

constexpr const char *sysconf_get_config_dir();

PList::Dictionary *sysconf_get_device_record(const char *udid);

void sysconf_set_device_record(const char *udid, const PList::Dictionary *record);
void sysconf_remove_device_record(const char *udid);

std::string sysconf_get_system_buid();

std::string sysconf_udid_for_macaddr(std::string macaddr);

void sysconf_fix_permissions(int uid, int gid);

class Config{
public:
	//config
	bool doPreflight;
	bool enableWifiDeviceManager;
	bool enableUSBDeviceManager;

	//commandline
	bool enableExit;
	bool daemonize;
	bool useLogfile;
    int debugLevel;
	std::string dropUser;
	
	Config();
	void load();
};

#endif /* sysconf_hpp */
