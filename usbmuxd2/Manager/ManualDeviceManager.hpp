// jkcoxson

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../Devices/ManualDevice.hpp"
#include "../Muxer.hpp"
#include "DeviceManager.hpp"

class gref_ManualDeviceManager {
    ManualDeviceManager *_mgr;

   public:
    gref_ManualDeviceManager(ManualDeviceManager *mgr);
    ~gref_ManualDeviceManager();

    ManualDeviceManager *operator->();
};

class ManualDeviceManager : public DeviceManager {
   private:  // for lifecycle management only
    tihmstar::Event _finalUnrefEvent;
    std::shared_ptr<gref_ManualDeviceManager> _ref;

    // Variables for socket
    sockaddr_in _addr;
#ifdef DEBUG
    std::weak_ptr<gref_ManualDeviceManager> __debug_ref;
#endif
   private:
    std::shared_ptr<gref_ManualDeviceManager> *_manual_cb_refarg;

    virtual void loopEvent() override;

   public:
    ManualDeviceManager(std::shared_ptr<gref_Muxer> mux);
    virtual ~ManualDeviceManager() override;

    void device_add(std::shared_ptr<ManualDevice> dev);
    void kill() noexcept;

    friend gref_ManualDeviceManager;
};