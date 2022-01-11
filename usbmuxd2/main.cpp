//
//  main.cpp
//  usbmuxd2
//
//  Created by tihmstar on 07.12.20.
//

#include <stdio.h>
#include "Muxer.hpp"
#include <mutex>
#include <unistd.h>

int main(int argc, const char * argv[]) {
    printf("start\n");
    std::mutex m;

    Muxer mux;

    mux.spawnClientManager();
    mux.spawnUSBDeviceManager();
    
    m.lock();
    m.lock();
    
    printf("done\n");
    return 0;
}
