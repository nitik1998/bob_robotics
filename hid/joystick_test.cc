#include "joystick.h"
#include <string>

using namespace GeNNRobotics;

HID::Joystick joystick;

void
callback(HID::Event *js, void *)
{
    if (!js) {
        std::cerr << "Error reading from joystick" << std::endl;
        exit(1);
    }

    if (js->isInitial) {
        std::cout << "[initial] ";
    }

    if (js->isAxis) {
        std::string name = js->axisName();
        std::cout << "Axis " << name << " (" << js->number << "): "
                  << js->value << std::endl;
    } else {
        std::string name = js->buttonName();
        std::cout << "Button " << name << " (" << js->number << (js->value ? ") pushed" : ") released")
                  << std::endl;
    }
}

int
main()
{
    std::cout << "Joystick test program" << std::endl;
    std::cout << "Press return to quit" << std::endl << std::endl;

    std::cout << "Opened joystick" << std::endl;
    joystick.startThread(callback, nullptr);

    std::cin.ignore();

    joystick.close();
    std::cout << "Controller closed" << std::endl;

    return 0;
}
