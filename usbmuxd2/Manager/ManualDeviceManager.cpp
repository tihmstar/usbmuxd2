// jkcoxson

#include "ManualDeviceManager.hpp"
#include <libgeneral/macros.h>
#include <map>


#define PORT 32498

std::vector<std::string> split(std::string str, std::string delimiter) {
    std::vector<std::string> internal;
    size_t pos = 0;
    std::string token;
    while ((pos = str.find(delimiter)) != std::string::npos) {
        token = str.substr(0, pos);
        internal.push_back(token);
        str.erase(0, pos + delimiter.length());
    }
    internal.push_back(str);
    return internal;
}

void socketThread(void *userdata, std::shared_ptr<gref_Muxer> mux) noexcept {
    std::shared_ptr<gref_ManualDeviceManager> devmgr = *(std::shared_ptr<gref_ManualDeviceManager> *)userdata;

    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        debug("Socket failed");
        return;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        debug("setsockopt failed");
        return;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        debug("Bind failed");
        return;
    }
    if (listen(server_fd, 3) < 0) {
        debug("Listen failed");
        return;
    }

    std::map<std::string, std::shared_ptr<ManualDevice>> devices;

    while (0 == 0) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            debug("Injection socket accept failed");
            continue;
        }
        debug("New connection on the injection socket");
        valread = read(new_socket, buffer, 1024);

        // Get array of strings by splitting by '\n'
        std::vector<std::string> lines = split(buffer, "\n");
        std::string toggle = lines[0];
        std::string uuid = lines[1];
        std::string serviceName = lines[2];
        std::string addr = lines[3];
        if (toggle == "1") {
            std::shared_ptr<ManualDevice> dev = nullptr;
            dev = std::make_shared<ManualDevice>(uuid, addr, serviceName, (*devmgr)->_mux);
            devices[uuid] = dev;
            (*devmgr)->device_add(dev);
            dev = NULL;
            write(new_socket, "OK", 3);
        } else {
            if (devices.find(uuid) != devices.end()) {
                devices[uuid]->kill();
                devices.erase(uuid);
                write(new_socket, "OK", 3);
            } else {
                debug("Tried to remove a device that doesn't exist");
                write(new_socket, "ERR", 4);
            }      
        }
        
    }
}

ManualDeviceManager::ManualDeviceManager(std::shared_ptr<gref_Muxer> mux)
    : DeviceManager(mux), _ref{std::make_shared<gref_ManualDeviceManager>(this)}, _manual_cb_refarg(nullptr) {
#ifdef DEBUG
    __debug_ref = _ref;  // only for debugging!
#endif
    debug("ManualDeviceManager created");

    assure(_manual_cb_refarg = new std::shared_ptr<gref_ManualDeviceManager>(_ref));

    std::thread device_injector(socketThread, _manual_cb_refarg, mux);
    device_injector.detach();
}

gref_ManualDeviceManager::gref_ManualDeviceManager(ManualDeviceManager *mgr)
    : _mgr(mgr) {
    //
}

gref_ManualDeviceManager::~gref_ManualDeviceManager() {
    _mgr->_finalUnrefEvent.notifyAll();
}

ManualDeviceManager *gref_ManualDeviceManager::operator->() {
    return _mgr;
}

ManualDeviceManager::~ManualDeviceManager() {
    _ref = nullptr;
    kill();
    // make sure _simple_poll is valid, while the event loop tries to use it
    _finalUnrefEvent.wait();  // wait until no more references to this object exist
}

void ManualDeviceManager::device_add(std::shared_ptr<ManualDevice> dev) {
    dev->_selfref = dev;
    (*_mux)->add_device(dev);
}

void ManualDeviceManager::kill() noexcept {
    debug("[ManualDeviceManager] killing ManualDeviceManager");
    stopLoop();
}

void ManualDeviceManager::loopEvent() {

}

