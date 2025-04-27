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
#include <fstream>
#include <string>
#include <fcntl.h>

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

// Structure to hold calibration data
struct CalibrationData {
    int left_x;   // Max left turn (replaces FACE_X_MIN)
    int right_x;  // Max right turn (replaces FACE_X_MAX)
    int top_y;    // Max up (replaces FACE_Y_MIN)
    int bottom_y; // Max down (replaces FACE_Y_MAX)
};

// Structure to hold face and cursor coordinates for message queue
struct FaceAndCursorCoords {
    int face_x;
    int face_y;
    int cursor_x;
    int cursor_y;
};

// Global calibration data (defaults match original constants)
static CalibrationData calib_data = {120, 520, 80, 400};

// Callback to free the FaceAndCursorCoords memory
void free_coords(void* data, void* hint) {
    delete static_cast<FaceAndCursorCoords*>(hint);
}

// Load calibration data from file
void loadCalibrationData(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open calibration file: " << filename << ". Using default values.\n";
        return;
    }

    std::string line;
    // Read header
    std::getline(file, line);
    // Read data
    if (std::getline(file, line)) {
        int straight_x, straight_y;
        if (sscanf(line.c_str(), "%d,%d,%d,%d,%d,%d",
                   &straight_x, &straight_y,
                   &calib_data.left_x, &calib_data.right_x,
                   &calib_data.top_y, &calib_data.bottom_y) == 6) {
        } else {
            std::cerr << "Invalid calibration data format in " << filename << ". Using default values.\n";
        }
    } else {
        std::cerr << "Empty calibration file: " << filename << ". Using default values.\n";
    }
}

uint8_t cursorInit() {
    // Load calibration data
    loadCalibrationData("calibration.csv");

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
    // Smoothing buffer
    static std::vector<cv::Point> recent_centers;

    // Receive face center coordinates
    zmq::message_t face_msg;
    if (zmq_pull_face_socket.recv(face_msg, zmq::recv_flags::dontwait)) {
        std::string received_str(static_cast<char*>(face_msg.data()), face_msg.size());

        // Parse the face center coordinates
        int x, y;
        if (sscanf(received_str.c_str(), "FaceCenter:%d,%d", &x, &y) == 2) {
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

            // Normalize coordinates to [0, 1] based on calibration data
            float norm_x = static_cast<float>(x - calib_data.right_x) / (calib_data.left_x - calib_data.right_x);
            float norm_y = static_cast<float>(y - calib_data.top_y) / (calib_data.bottom_y - calib_data.top_y);

            // Map normalized coordinates to display coordinates
            int display_x = static_cast<int>(norm_x * DISPLAY_X);
            int display_y = static_cast<int>(norm_y * DISPLAY_Y);

            // Ensure coordinates are within display bounds
            display_x = std::max(0, std::min(display_x, DISPLAY_X));
            display_y = std::max(0, std::min(display_y, DISPLAY_Y));

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

            // Send face and cursor coordinates to message queue
            FaceAndCursorCoords* coords = new FaceAndCursorCoords{x, y, display_x, display_y};
            zmq::message_t msg(coords, sizeof(FaceAndCursorCoords), free_coords, coords);
            zmq_push_control_socket.send(msg, zmq::send_flags::dontwait);
        } else {
            std::cerr << "Failed to parse face center data: " << received_str << std::endl;
        }
    }
}