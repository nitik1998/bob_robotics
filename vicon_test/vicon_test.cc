// C++ includes
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

// BoB robotics includes
#include "../vicon/capture_control.h"
#include "../vicon/udp.h"

using namespace BoBRobotics::Vicon;
using namespace std::literals;

#define toDeg(RAD) static_cast<degree_t>(RAD)

int main()
{
    UDPClient<ObjectData> vicon(51001);
    CaptureControl viconCaptureControl("192.168.1.100", 3003, "c:\\users\\ad374\\Desktop");
    while(vicon.getNumObjects() == 0) {
        std::this_thread::sleep_for(1s);
        std::cout << "Waiting for object" << std::endl;
    }
    
    if(!viconCaptureControl.startRecording("test1")) {
        return EXIT_FAILURE;
    }
    for(int i = 0; i < 10000; i++) {
        auto objectData = vicon.getObjectData(0);
        const auto &position = objectData.getPosition();
        const auto &attitude = objectData.getAttitude();

        std::cout << position[0] << ", " << position[1] << ", " << position[2] << ", "
                  << toDeg(attitude[0]) << ", " << toDeg(attitude[1]) << ", " << toDeg(attitude[2]) << std::endl;
    }
    if(!viconCaptureControl.stopRecording("test1")) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
