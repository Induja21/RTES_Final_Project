#include "CursorTranslation.hpp"
#include "Logging.hpp"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <linux/uinput.h>
#include <X11/Xlib.h>
#include "MessageQueue.hpp"
// Static counter for unique message IDs
static int message_counter = 0;
static int fd = 0;
#define DISPLAY_X 1920
#define DISPLAY_Y 1080

uint8_t cursorInit()
{
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to open /dev/uinput\n";
        return 1;
    }
    
    // Enable absolute events for X and Y
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    
    // Also allow clicks (optional)
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    
    // Set up device
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Absolute Mouse");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;
    
    // Set ABS_X and ABS_Y ranges (based on screen size — adjust if needed)
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = DISPLAY_X;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = DISPLAY_Y;
    
    write(fd, &uidev, sizeof(uidev));
    
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        std::cerr << "UI_DEV_CREATE failed\n";
        return 1;
    }
    
    sleep(2); // Let device initialize
}

void cursorDeinit()
{
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

void cursorTranslationService() {
    // Get current timestamp for message
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::stringstream message;
    message << "ProducerMsg_" << message_counter++ << "_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec;

    zmq::message_t eyeball_msg;
    if (zmq_pull_eyeball_socket.recv(eyeball_msg, zmq::recv_flags::dontwait)) {
        std::string received_str(static_cast<char*>(eyeball_msg.data()), eyeball_msg.size());
        std::cout << "Received eyeball data: " << received_str << std::endl;

        // Optionally parse it
        int x, y, r;
        sscanf(received_str.c_str(), "Eyeball:%d,%d,%d", &x, &y, &r);
        std::cout << "Parsed: x=" << x << ", y=" << y << ", r=" << r << std::endl;
    }
    
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, nullptr);
    
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = DISPLAY_X - 20;
    write(fd, &ev, sizeof(ev));
    
    ev.code = ABS_Y;
    ev.value = DISPLAY_Y - 20;
    write(fd, &ev, sizeof(ev));
    
    // Synchronize
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(fd, &ev, sizeof(ev));

    // Send message via ZeroMQ
    std::string msg_str = message.str();
    zmq::message_t msg(msg_str.size());
    memcpy(msg.data(), msg_str.data(), msg_str.size());
    // Non-blocking with dontwait
    zmq_push_control_socket.send(msg, zmq::send_flags::dontwait);  
}
