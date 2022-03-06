// jkcoxson

#define WIFIDevice_hpp

#include <libimobiledevice/heartbeat.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

#include <libgeneral/Manager.hpp>
#include <string>

#include "Device.hpp"

class ManualDevice : public Device, tihmstar::Manager {
    std::weak_ptr<ManualDevice> _selfref;
    std::string _ipaddr;
    std::string _serviceName;
    heartbeat_client_t _hbclient;
    plist_t _hbrsp;
    idevice_t _idev;

    virtual void loopEvent() override;
    virtual void beforeLoop() override;

   public:
    ManualDevice(std::string uuid, std::string ipaddr, std::string serviceName, std::shared_ptr<gref_Muxer> mux);
    ManualDevice(const ManualDevice &) = delete;  // delete copy constructor
    ManualDevice(ManualDevice &&o) = delete;      // move constructor
    virtual ~ManualDevice() override;

    virtual void kill() noexcept override;
    void startLoop();
    virtual void start_connect(uint16_t dport, std::shared_ptr<Client> cli) override;

    friend class Muxer;
    friend class ManualDeviceManager;
};