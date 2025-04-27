#include "CursorTranslation.hpp"
#include "Logging.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <linux/uinput.h>
#include <X11/Xlib.h>
#include <vector>
#include <opencv2/core.hpp>
#include "MessageQueue.hpp"

static int message_counter = 0;
static int fd = 0;
#define DISPLAY_X 1920
#define DISPLAY_Y 1080
#define CAMERA_X 640 // Camera width
#define CAMERA_Y 480 // Camera height
#define FACE_X_MIN 120 // Min x-coordinate for 60 deg left (after inversion)
#define FACE_X_MAX 520 // Max x-coordinate for 60 deg right (after inversion)
#define FACE_Y_MIN 80  // Min y-coordinate for 60 deg up
#define FACE_Y_MAX 400 // Max y-coordinate for 60 deg down
#define SMOOTHING_WINDOW 5 // Number of frames for moving average

uint8_t cursorInit() {
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
    
    // Set ABS_X and ABS_Y ranges
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = DISPLAY_X;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = DISPLAY_Y;
    
    write(fd, &uidev, sizeof(uidev));
    
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        std::cerr << "UI_DEV_CREATE failed\n";
        return 1;
    }
    
    return 0;
}

void cursorDeinit() {
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

void cursorTranslationService() {
    // Get current timestamp for message
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::stringstream message;
    message << "ProducerMsg_" << message_counter++ << "_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec;

    // Smoothing buffer
    static std::vector<cv::Point> recent_centers;

    // Receive face center coordinates
    zmq::message_t face_msg;
    if (zmq_pull_face_socket.recv(face_msg, zmq::recv_flags::dontwait)) {
        std::string received_str(static_cast<char*>(face_msg.data()), face_msg.size());
        std::cout << "Received face center data: " << received_str << std::endl;

        // Parse the face center coordinates
        int x, y;
        if (sscanf(received_str.c_str(), "FaceCenter:%d,%d", &x, &y) == 2) {
            std::cout << "Parsed: x=" << x << ", y=" << y << std::endl;

            // Invert x-coordinate to correct for mirrored camera image
            x = CAMERA_X - x;

            // Smooth coordinates using moving average
            recent_centers.push_back(cv::Point(x, y));
            if (recent_centers.size() > SMOOTHING_WINDOW) {
                recent_centers.erase(recent_centers.begin());
            }
            float avg_x = 0, avg_y = 0;
            for (const auto& p : recent_centers) {
                avg_x += p.x;
                avg_y += p.y;
            }
            avg_x /= recent_centers.size();
            avg_y /= recent_centers.size();
            x = static_cast<int>(avg_x);
            y = static_cast<int>(avg_y);

            // Log smoothed coordinates
            std::cout << "Smoothed: x=" << x << ", y=" << y << std::endl;

            // Normalize coordinates to [0, 1] based on face movement range
            float norm_x = static_cast<float>(x - FACE_X_MIN) / (FACE_X_MAX - FACE_X_MIN);
            float norm_y = static_cast<float>(y - FACE_Y_MIN) / (FACE_Y_MAX - FACE_Y_MIN);

            // Log normalized coordinates
            std::cout << "Normalized: norm_x=" << norm_x << ", norm_y=" << norm_y << std::endl;

            // Map normalized coordinates to display coordinates
            int display_x = static_cast<int>(norm_x * DISPLAY_X);
            int display_y = static_cast<int>(norm_y * DISPLAY_Y);

            // Ensure coordinates are within display bounds
            display_x = std::max(0, std::min(display_x, DISPLAY_X));
            display_y = std::max(0, std::min(display_y, DISPLAY_Y));

            // Log final display coordinates
            std::cout << "Display: x=" << display_x << ", y=" << display_y << std::endl;

            // Move cursor using uinput
            struct input_event ev;
            memset(&ev, 0, sizeof(ev));
            gettimeofday(&ev.time, nullptr);

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
        } else {
            std::cerr << "Failed to parse face center data: " << received_str << std::endl;
        }
    }

    // Send message via ZeroMQ (unchanged)
    std::string msg_str = message.str();
    zmq::message_t msg(msg_str.size());
    memcpy(msg.data(), msg_str.data(), msg_str.size());
    zmq_push_control_socket.send(msg, zmq::send_flags::dontwait);  
}