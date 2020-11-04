//
//  sysconf.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#include "sysconf.hpp"
#include "log.h"
#include <libgeneral/macros.h>
#include <libgeneral/exception.hpp>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <map>

#ifdef HAVE_FILESYSTEM
#include <filesystem>
#else
#include <dirent.h>
#endif //HAVE_FILESYSTEM

#define CONFIG_DIR  "lockdown"
#define CONFIG_FILE "SystemConfiguration"

#define CONFIG_SYSTEM_BUID_KEY "SystemBUID"
#define CONFIG_HOST_ID_KEY "HostID"


#ifdef __APPLE__
#   define BASE_CONFIG_DIR "/var/db"
#else
#   define BASE_CONFIG_DIR "/var/lib"
#endif


#ifndef HAVE_FILESYSTEM
//really crappy implementation in case <filesystem> isn't available :o
class myfile{
    std::string _path;
public:
    myfile(std::string p): _path(p){}
    std::string path(){return _path;}
};

class diriter{
public:
    std::vector<myfile> _file;
    auto begin(){return _file.begin();}
    auto end(){return _file.end();}
};

namespace std {
    namespace filesystem{
        diriter directory_iterator(std::string);
    }
}

diriter std::filesystem::directory_iterator(std::string dirpath){
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    diriter ret;
    
    assure(dir = opendir(dirpath.c_str()));
    
    while ((ent = readdir (dir)) != NULL) {
        if (ent->d_type != DT_REG)
            continue;
        ret._file.push_back({dirpath + "/" + ent->d_name});
    }
    
    if (dir) closedir(dir);
    return ret;
}
#endif



static std::map<std::string,std::string> gKnownMacAddrs;

static void sysconf_load_known_macaddrs();


constexpr const char *sysconf_get_config_dir(){
    return BASE_CONFIG_DIR "/" CONFIG_DIR;
}

static char *sysconf_generate_system_buid(){
    char *uuid = (char *) malloc(sizeof(char) * 37);
    const char *chars = "ABCDEF0123456789";
    srand((unsigned)time(NULL));
    int i = 0;
    
    for (i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            uuid[i] = '-';
            continue;
        } else {
            uuid[i] = chars[random() % 16];
        }
    }
    
    uuid[36] = '\0';
    return uuid;
}

static void mkdir_with_parents(const char *dir, int mode){
    char *parent = NULL;
    char* parentdir = NULL; //not allocated
    cleanup([&]{
        safeFree(parent);
    });
    assure(dir);
    if (mkdir(dir, mode) == 0 || errno == EEXIST) {
        return;
    }
    
    parent = strdup(dir);
    assure(parentdir = dirname(parent));
    mkdir_with_parents(parentdir, mode);
}

PList::Node *readPlist(const char *filePath){
    int fd = 0;
    struct stat finfo{};
    char *fbuf = NULL;
    plist_t pl = NULL;
    PList::Node *ret = nullptr; //we return this, so don't free it
    cleanup([&]{
        if (fd) {
            close(fd);
        }
        safeFree(fbuf);
    });
    
    assure((fd = open(filePath, O_RDONLY))>0);
    assure(!fstat(fd, &finfo));
    
    assure(fbuf = (char*)malloc(finfo.st_size));
    
    assure(read(fd, fbuf, finfo.st_size) == finfo.st_size);
    
    if (memcmp(fbuf, "bplist00", 8) == 0) {
        plist_from_bin(fbuf, (uint32_t)finfo.st_size, &pl);
    } else {
        plist_from_xml(fbuf, (uint32_t)finfo.st_size, &pl);
    }
    
    assure(ret = PList::Node::FromPlist(pl));
    //ret object constructed successfully, don't free pl
    pl = NULL;
    return ret;
}

void writePlist(const char *filePath, const PList::Structure *plist){
    int fd = 0;
    std::string xml;
    cleanup([&]{
        if (fd) {
            close(fd);
        }
    });
    
    assure((fd = open(filePath, O_WRONLY | O_CREAT, 0644))>0);
    
    xml = plist->ToXml();
    assure(write(fd, xml.c_str(), xml.size()) == xml.size());
}

static void sysconf_create_config_dir(void){
    struct stat st{};
    constexpr const char *config_path = sysconf_get_config_dir();
    
    if (stat(config_path, &st) != 0) {
        mkdir_with_parents(config_path, 0755);
    }
}

char *get_device_record_path(const char *udid){
    constexpr const char *config_path = sysconf_get_config_dir();
    size_t filepathSize = 0;
    char *filepath = NULL;
    
    assure(udid);
    
    sysconf_create_config_dir();
    
    filepathSize = strlen(config_path) + strlen(udid) + sizeof("/.plist");
    assure(filepath = (char*)malloc(filepathSize));
    
    snprintf(filepath, filepathSize, "%s/%s.plist", config_path,udid);
    return filepath;
}

PList::Dictionary *sysconf_get_device_record(const char *udid){
    char *filepath = NULL;
    PList::Node *somenode = nullptr;
    PList::Dictionary *ret = nullptr;
    cleanup([&]{
        safeFree(filepath);
        if (somenode) {
            delete somenode;
        }
    });
    filepath = get_device_record_path(udid);
    
    somenode = readPlist(filepath);
    ret = dynamic_cast<PList::Dictionary*>(somenode);somenode = nullptr;
    return ret;
}


void sysconf_set_device_record(const char *udid, const PList::Dictionary *record){
    char *filepath = NULL;
    std::string xmlRecord;
    cleanup([&]{
        safeFree(filepath);
    });
    assure(udid);
    assure(record);
    filepath = get_device_record_path(udid);
    
    writePlist(filepath, record);
    sysconf_load_known_macaddrs();
}

void sysconf_remove_device_record(const char *udid){
    char *filepath = NULL;
    cleanup([&]{
        safeFree(filepath);
    });
    filepath = get_device_record_path(udid);
    
    retassure(!remove(filepath), "could not remove %s: %s", filepath, strerror(errno));
    sysconf_load_known_macaddrs();
}


//allocated a Node
PList::Node *sysconf_get_value(const std::string &key){
    char *filepath = NULL;
    PList::Dictionary *conf = nullptr;
    PList::Node *somenode = nullptr;
    PList::Node *ret = nullptr;
    cleanup([&]{
        safeFree(filepath);
        if (conf) {
            delete conf;
        }
        if (somenode) {
            delete somenode;
        }
    });
    filepath = get_device_record_path(CONFIG_FILE);
    
    somenode = readPlist(filepath);
    conf = dynamic_cast<PList::Dictionary*>(somenode);somenode = nullptr;
    assure(ret = (*conf)[key]);
    
    return ret->Clone();
}

void sysconf_set_value(const std::string &key, PList::Node *val){
    char *filepath = NULL;
    PList::Dictionary *conf = nullptr;
    PList::Node *somenode = nullptr;
    cleanup([&]{
        safeFree(filepath);
        if (somenode) {
            delete somenode;
        }
    });
    filepath = get_device_record_path(CONFIG_FILE);
    
    try {
        somenode = readPlist(filepath);
        conf = dynamic_cast<PList::Dictionary*>(somenode);somenode=nullptr;
    } catch (tihmstar::exception &e) {
        warning("%s: Reading %s failed! Regenerating!",__func__,CONFIG_FILE);
        conf = new PList::Dictionary();
    }
    conf->Set(key, val);
    writePlist(filepath, conf);
}

std::string sysconf_get_system_buid(){
    PList::String *pBuid = nullptr;
    PList::Node *somenode = nullptr;
    cleanup([&]{
        if (pBuid) {
            delete pBuid;
        }
        if (somenode) {
            delete somenode;
        }
    })
    try {
        somenode = sysconf_get_value(CONFIG_SYSTEM_BUID_KEY);
        pBuid = dynamic_cast<PList::String*>(somenode);somenode = nullptr;
    } catch (tihmstar::exception &e) {
        warning("Failed to get SystemBuid! regenerating %s",CONFIG_FILE);
        std::string buid = sysconf_generate_system_buid();
        pBuid = new PList::String(buid);
        sysconf_set_value(CONFIG_SYSTEM_BUID_KEY, pBuid);
    }
    
    return pBuid->GetValue();
}

static void sysconf_load_known_macaddrs(){
    constexpr const char *config_path = sysconf_get_config_dir();
    char *sysconfigpath = NULL;
    PList::Node *somenode = nullptr;
    PList::Dictionary *pairingRecord = nullptr;
    cleanup([&]{
        safeFree(sysconfigpath);
        if (somenode) {
            delete somenode;
        }
    });

    sysconf_create_config_dir();

    gKnownMacAddrs.clear();

    sysconfigpath = get_device_record_path(CONFIG_FILE);

    for(auto& p: std::filesystem::directory_iterator(config_path)){
        if (p.path() == sysconfigpath)
            continue; //ignore sysconfig file
        if (somenode) {
            delete somenode;somenode = nullptr;
        }
        debug("reading file=%s\n",p.path().c_str());
        try{ //we ignore any error happening in here
            PList::String *macaddr = nullptr;
            somenode = readPlist(p.path().c_str());
            pairingRecord = dynamic_cast<PList::Dictionary*>(somenode);
            assure(macaddr = dynamic_cast<PList::String*>((*pairingRecord)["WiFiMACAddress"]));
            std::string path = p.path();

            size_t lastSlashPos = path.find_last_of("/")+1;
            size_t dotPos = path.find(".");

            std::string uuid = path.substr(lastSlashPos,dotPos-lastSlashPos);
            debug("adding macaddr=%s for uuid=%s",macaddr->GetValue().c_str(),uuid.c_str());
            
            gKnownMacAddrs[macaddr->GetValue()] = uuid;
            
        } catch (tihmstar::exception &e){
            debug("failed to read record with error=%d (%s)",e.code(),e.what());
        }
    }
}

std::string sysconf_udid_for_macaddr(std::string macaddr){
    if (!gKnownMacAddrs.size()){
        sysconf_load_known_macaddrs();
    }
    try{
        return gKnownMacAddrs.at(macaddr);        
    }catch (...){
        reterror("macaddr=%s is not paired",macaddr.c_str());
    }
}

void sysconf_fix_permissions(int uid, int gid){
    constexpr const char *config_path = sysconf_get_config_dir();

    sysconf_create_config_dir();

    assure(!chown(config_path, uid, gid));

    for(auto& p: std::filesystem::directory_iterator(config_path)){
        assure(!chown(p.path().c_str(), uid, gid));
   }
}


#pragma mark config

template <typename PType, typename RType>
RType sysconf_try_getconfig(std::string key, RType defaultValue){
    PType *pVal = nullptr;
    PList::Node *somenode = nullptr;
    cleanup([&]{
        if (pVal) {
            delete pVal;
        }
        if (somenode) {
            delete somenode;
        }
    })
    try {
        somenode = sysconf_get_value(key);
        pVal = dynamic_cast<PType*>(somenode);somenode = nullptr;
    } catch (tihmstar::exception &e) {
        warning("Failed to get %s! setting it to default val",key.c_str());
        pVal = new PType(defaultValue);
        sysconf_set_value(key, pVal);
    }
    
    return pVal->GetValue();
}

Config::Config() : 
//commandline
enableExit(false),
daemonize(false),
useLogfile(false),
debugLevel(0)
{
    //empty
}

void Config::load(){
    //config
    doPreflight = sysconf_try_getconfig<PList::Boolean, bool>("doPreflight",true);
    enableWifiDeviceManager = sysconf_try_getconfig<PList::Boolean, bool>("enableWifiDeviceManager",true);
    enableUSBDeviceManager = sysconf_try_getconfig<PList::Boolean, bool>("enableUSBDeviceManager",true);
    info("Loaded config");    
}
