#include "CursorTranslation.hpp"
#include "Logging.hpp"
#include <vector>
#include <algorithm>
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
const int SMOOTHING_WINDOW = 5;
const int DISPLAY_X = 1920;
const int DISPLAY_Y = 1080;
const float GAZE_LR_MIN = 45;  // Right
const float GAZE_LR_MAX = 30;  // Left
const float GAZE_UD_MIN = 20;  // Up
const float GAZE_UD_MAX = 35;  // Down
std::vector<float> recent_gaze_lr, recent_gaze_ud;
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

int mapX(double x) {
            return (int)((x - 1.2) / (0.88 - 1.2) * 1920.0);
        }

// Map y from [1.2, 0.98] to [0, 1080] (inverted input range)
int mapY(double y) {
    return (int)((y - 0.85) / (1.2-0.85) * 1080.0);
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
        double gaze_left_right, gaze_up_down;
        sscanf(received_str.c_str(), "%lf %lf", &gaze_left_right, &gaze_up_down);
            struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        gettimeofday(&ev.time, nullptr);
        
        // Smooth gaze ratios
        recent_gaze_lr.push_back(gaze_left_right);
        recent_gaze_ud.push_back(gaze_up_down);
        if (recent_gaze_lr.size() > SMOOTHING_WINDOW) {
            recent_gaze_lr.erase(recent_gaze_lr.begin());
            recent_gaze_ud.erase(recent_gaze_ud.begin());
        }
        float avg_lr = 0, avg_ud = 0;
        for (size_t i = 0; i < recent_gaze_lr.size(); ++i) {
            avg_lr += recent_gaze_lr[i];
            avg_ud += recent_gaze_ud[i];
        }
        avg_lr /= recent_gaze_lr.size();
        avg_ud /= recent_gaze_ud.size();

        // Map gaze ratios to normalized coordinates [0, 1]
        float norm_x = (avg_lr - GAZE_LR_MIN) / (GAZE_LR_MAX - GAZE_LR_MIN); // 1.2->0 (right), 1.5->1 (left)
        float norm_y = (avg_ud - GAZE_UD_MIN) / (GAZE_UD_MAX - GAZE_UD_MIN); // 0.9->0 (up), 1.2->1 (down)

        // Map normalized coordinates to display coordinates
        int display_x = static_cast<int>(norm_x * DISPLAY_X); // 0 to 1920
        int display_y = static_cast<int>(norm_y * DISPLAY_Y); // 0 to 1080

        // Clamp coordinates to display bounds
        display_x = std::max(0, std::min(display_x, DISPLAY_X));
        display_y = std::max(0, std::min(display_y, DISPLAY_Y));
            

        
        ev.type = EV_ABS;
        ev.code = ABS_X;
        ev.value = display_x;
        write(fd, &ev, sizeof(ev));
        
        ev.code = ABS_Y;
        ev.value = display_y;
        write(fd, &ev, sizeof(ev));
        
        // Synchronize
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        write(fd, &ev, sizeof(ev));
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Parsed: x=" << display_x << ", y=" << display_y << std::endl;
    }
    
    
    


    // Send message via ZeroMQ
    std::string msg_str = message.str();
    zmq::message_t msg(msg_str.size());
    memcpy(msg.data(), msg_str.data(), msg_str.size());
    // Non-blocking with dontwait
    zmq_push_control_socket.send(msg, zmq::send_flags::dontwait);  
}
