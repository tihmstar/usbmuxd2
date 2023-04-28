//
//  sysconf.cpp
//  usbmuxd2
//
//  Created by tihmstar on 18.12.20.
//

#include "sysconf.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <libgeneral/macros.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <map>
#include <dirent.h>
#include <string.h>
#include <mutex>

#define CONFIG_DIR  "lockdown"
#define CONFIG_FILE "SystemConfiguration"

#define CONFIG_SYSTEM_BUID_KEY "SystemBUID"
#define CONFIG_HOST_ID_KEY "HostID"

#ifdef __APPLE__
#   define BASE_CONFIG_DIR "/var/db"
#else
#   define BASE_CONFIG_DIR "/var/lib"
#endif

static std::map<std::string,std::string> gKnownMacAddrs;
static std::mutex gKnownMacAddrsLck;

constexpr const char *sysconf_get_config_dir(){
    return BASE_CONFIG_DIR "/" CONFIG_DIR;
}

static plist_t readPlist(const char *filePath){
    int fd = -1;
    char *fbuf = NULL;
    cleanup([&]{
        safeFree(fbuf);
        safeClose(fd);
    });
    struct stat finfo = {};
    
    retassure((fd = open(filePath, O_RDONLY))>0, "Failed to read plist at path '%s'",filePath);
    assure(!fstat(fd, &finfo));
    
    assure(fbuf = (char*)malloc(finfo.st_size));
    
    assure(read(fd, fbuf, finfo.st_size) == finfo.st_size);
    
    {
        plist_t pl = NULL;
        plist_from_memory(fbuf, (uint32_t)finfo.st_size, &pl, NULL);
        retassure(pl, "failed to parse plist at path '%s'",filePath);        
        return pl;
    }
}

static void mkdir_with_parents(const char *dir, int mode){
    char *parent = NULL;
    cleanup([&]{
        safeFree(parent);
    });
    char* parentdir = NULL; //not allocated

#ifdef DEBUG
    assure(dir);
#endif
    
    if (mkdir(dir, mode) == 0 || errno == EEXIST) {
        return;
    }
    
    assure(parent = strdup(dir));
    assure(parentdir = dirname(parent));
    mkdir_with_parents(parentdir, mode);
#warning TODO is this even correct??
}

static void sysconf_create_config_dir(void){
    struct stat st{};
    constexpr const char *config_path = sysconf_get_config_dir();
    
    if (stat(config_path, &st) != 0) {
        mkdir_with_parents(config_path, 0755);
    }
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

std::string get_device_record_path(const char *udid){
    constexpr const char *config_path = sysconf_get_config_dir();
    sysconf_create_config_dir();
    
    std::string ret = config_path;
    ret += '/';
    ret += udid;
    ret += ".plist";

    return ret;
}

static void sysconf_load_known_macaddrs(){
    std::unique_lock<std::mutex> ul(gKnownMacAddrsLck);
    constexpr const char *config_path = sysconf_get_config_dir();

    sysconf_create_config_dir();

    gKnownMacAddrs.clear();

    std::string sysconfigpath = get_device_record_path(CONFIG_FILE);

    {
        DIR *dir = NULL;
        cleanup([&]{
            safeFreeCustom(dir, closedir);
        });
        struct dirent *ent = NULL;
        
        assure(dir = opendir(config_path));
        
        while ((ent = readdir (dir)) != NULL) {
            if (ent->d_type != DT_REG)
                continue;
            std::string path = config_path;
            path+= "/";
            path+= ent->d_name;
            
            if (path == sysconfigpath)
                continue; //ignore sysconfig file
            
            debug("reading file=%s\n",path.c_str());
            try{ //we ignore any error happening in here
                plist_t p_devrecord = NULL;
                cleanup([&]{
                    safeFreeCustom(p_devrecord, plist_free);
                });
                plist_t p_macaddr = NULL;
                std::string macaddr;

                p_devrecord = readPlist(path.c_str());
                
                retassure(p_macaddr = plist_dict_get_item(p_devrecord, "WiFiMACAddress"), "Failed to read macaddr from pairing record");
                
                {
                    const char *str = NULL;
                    uint64_t str_len = 0;
                    retassure(str = plist_get_string_ptr(p_macaddr, &str_len), "Faile to get str ptr from MacAddress");
                    macaddr = std::string(str,str_len);
                }
                
                size_t lastSlashPos = path.find_last_of("/")+1;
                size_t dotPos = path.find(".");

                std::string uuid = path.substr(lastSlashPos,dotPos-lastSlashPos);
                debug("adding macaddr=%s for uuid=%s",macaddr.c_str(),uuid.c_str());
                
                gKnownMacAddrs[macaddr] = uuid;
                
            } catch (tihmstar::exception &e){
                debug("failed to read record with error=%d (%s)",e.code(),e.what());
            }
            
        }
    }
    
}


void writePlistToFile(plist_t plist, const char *dst){
    char *buf = NULL;
    FILE * saveFile = NULL;
    cleanup([&]{
        safeFree(buf);
        safeFreeCustom(saveFile, fclose);
    });
    uint32_t bufLen = 0;
    plist_to_xml(plist, &buf, &bufLen);
    
    retassure(saveFile = fopen(dst, "w"), "Failed to write plist file to=%s",dst);
    assure(fwrite(buf, 1, bufLen, saveFile) == bufLen);
}

plist_t sysconf_get_value(const std::string &key){
    plist_t p_devrecord = NULL;
    cleanup([&]{
        safeFreeCustom(p_devrecord, plist_free);
    });
    plist_t p_val = NULL;
    std::string filepath = get_device_record_path(CONFIG_FILE);
    
    p_devrecord = readPlist(filepath.c_str());
    
    retassure(p_val = plist_dict_get_item(p_devrecord, key.c_str()), "Failed to get value for key '%s'",key.c_str());

    return plist_copy(p_val);
}

void sysconf_set_value(const std::string &key, plist_t val){
    plist_t p_sysconf = NULL;
    cleanup([&]{
        safeFreeCustom(p_sysconf, plist_free);
    });
    std::string filepath = get_device_record_path(CONFIG_FILE);
    
    try {
        p_sysconf = readPlist(filepath.c_str());
    } catch (tihmstar::exception &e) {
        warning("%s: Reading %s failed! Regenerating!",__func__,CONFIG_FILE);
        p_sysconf = plist_new_dict();
    }
    
    plist_dict_set_item(p_sysconf, key.c_str(), plist_copy(val));
    writePlistToFile(p_sysconf, filepath.c_str());
}


plist_t sysconf_get_device_record(const char *udid){
    std::string filepath = get_device_record_path(udid);
    return readPlist(filepath.c_str());
}

void sysconf_set_device_record(const char *udid, const plist_t record){
    assure(udid);
    assure(record);
    std::string filepath = get_device_record_path(udid);
    
    writePlistToFile(record, filepath.c_str());
    sysconf_load_known_macaddrs();
}

void sysconf_remove_device_record(const char *udid){
    std::string filepath = get_device_record_path(udid);
    
    retassure(!remove(filepath.c_str()), "could not remove %s: %s", filepath.c_str(), strerror(errno));
    sysconf_load_known_macaddrs();
}


std::string sysconf_get_system_buid(){
    plist_t p_buid = NULL;
    cleanup([&]{
        safeFreeCustom(p_buid, plist_free);
    });
    const char *buid_str = NULL;
    uint64_t buid_str_len = 0;
    
    try {
        p_buid = sysconf_get_value(CONFIG_SYSTEM_BUID_KEY);
    } catch (tihmstar::exception &e) {
        warning("Failed to get SystemBuid! regenerating %s",CONFIG_FILE);
        char *buid_str = NULL;
        cleanup([&]{
            safeFree(buid_str);
        })
        assure(buid_str = sysconf_generate_system_buid());
        p_buid = plist_new_string(buid_str);
        sysconf_set_value(CONFIG_SYSTEM_BUID_KEY, p_buid);
    }
    
    retassure(buid_str = plist_get_string_ptr(p_buid, &buid_str_len), "Failed to get str ptr from build");
    
    return std::string(buid_str,buid_str_len);
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
    {
        DIR *dir = NULL;
        cleanup([&]{
            safeFreeCustom(dir, closedir);
        });
        struct dirent *ent = NULL;
        
        assure(dir = opendir(config_path));
        
        while ((ent = readdir (dir)) != NULL) {
            if (ent->d_type != DT_REG)
                continue;
            std::string path = config_path;
            path+= "/";
            path+= ent->d_name;    
            assure(!chown(path.c_str(), uid, gid));     
        }
    }
}

#pragma mark config
bool sysconf_try_getconfig_bool(std::string key, bool defaultValue){
    plist_t p_boolVal = NULL;
    cleanup([&]{
        safeFreeCustom(p_boolVal, plist_free);
    });
    try {
        p_boolVal = sysconf_get_value(key);
        assure(plist_get_node_type(p_boolVal) == PLIST_BOOLEAN);
        return plist_bool_val_is_true(p_boolVal);
    } catch (tihmstar::exception &e) {
        warning("Failed to get %s! setting it to default val",key.c_str());
        p_boolVal = plist_new_bool(defaultValue);
        sysconf_set_value(key, p_boolVal);
        return defaultValue;
    }
}

Config::Config() : 
//config
doPreflight(false),
enableWifiDeviceManager(false),
enableUSBDeviceManager(false),
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
    doPreflight = sysconf_try_getconfig_bool("doPreflight",true);
    enableWifiDeviceManager = sysconf_try_getconfig_bool("enableWifiDeviceManager",true);
    enableUSBDeviceManager = sysconf_try_getconfig_bool("enableUSBDeviceManager",true);
    info("Loaded config");    
}
