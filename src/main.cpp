#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <atomic>
#include <string>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <iostream>
#include <unistd.h>
#include <linux/gpio.h>
#include <linux/uinput.h>
#include <X11/Xlib.h>
#include <fstream>
#include "Sequencer.hpp"
#include "Logging.hpp"
#include "CursorTranslation.hpp"
#include "ImageCapture.hpp"
#include "Compression.hpp"
#include "MessageQueue.hpp"
#include "ImageProcessing.hpp"

int fd = 0;
std::atomic<bool> _runningstate{true};

void signalHandler(int signum)
{
    std::puts("\nReceived Ctrl+C, stopping services ");
    _runningstate.store(false, std::memory_order_relaxed);
    flushCsvFile();

    //cleanup_zmq();
  
}


int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to open /dev/uinput\n";
        return 1;
    }

    // Enable mouse button and relative movement events
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);

    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER); // Important for X to recognize

    // Setup the device
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Test Mouse");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    write(fd, &uidev, sizeof(uidev));

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        std::cerr << "UI_DEV_CREATE failed\n";
        return 1;
    }

    std::cout << "Virtual mouse created. Waiting...\n";

    sleep(2); // Allow system to recognize device

    // Simulate mouse movement
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, nullptr);
    ev.type = EV_REL;
    ev.code = REL_X;
    ev.value = 50;
    write(fd, &ev, sizeof(ev));

    ev.code = REL_Y;
    ev.value = 50;
    write(fd, &ev, sizeof(ev));

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(fd, &ev, sizeof(ev));

    std::cout << "Moved mouse!\n";

    // Keep the device alive for a while so you can inspect with evtest/xinput
    sleep(5);

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);

    initialize_zmq();
    Sequencer sequencer{};
  

    // Add the producer service (runs every 250ms)
    sequencer.addService(cursorTranslationService, 1, 99, 50);
    sequencer.addService(imageCaptureService, 1, 98, 100);   
    sequencer.addService(eyeDetectionService, 2, 97, 100); 
    sequencer.addService(imageCompressionService, 2, 96, 100);  
    sequencer.addService(messageQueueToCsvService, 2, 95, 250);

    sequencer.startServices();
    while (_runningstate.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sequencer.stopServices();
    return 0;
}
