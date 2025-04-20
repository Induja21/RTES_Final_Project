#include "ImageCapture.hpp"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "base64.h"

void imageCaptureService() {
    static cv::VideoCapture cap(0, cv::CAP_V4L2); 
    static bool cap_initialized = false;

    if (!cap_initialized) {
        if (!cap.isOpened()) {
            std::fputs("Failed to open camera\n", stderr);
            return;
        }
        cap_initialized = true;
    }

    // Non-blocking frame grab
    if (!cap.grab()) {
        // No frame available yet, return without blocking
        //https://docs.opencv.org/4.9.0/d8/dfe/classcv_1_1VideoCapture.html#a9d2ca36789e7fcfe7a7be3b328038585
        return;
    }

    // Frame is available, retrieve it
    cv::Mat frame;
    if (!cap.retrieve(frame)) {
        std::fputs("Failed to retrieve frame\n", stderr);
        return;
    }

    if (frame.empty()) {
        std::fputs("Failed to capture frame\n", stderr);
        return;
    }

    // Encode the frame as JPEG
    std::vector<uchar> buffer;
    if (!cv::imencode(".jpg", frame, buffer)) {
        std::fputs("Failed to encode frame\n", stderr);
        return;
    }

    // Base64 encode the buffer
    std::string encoded = base64_encode(buffer);
    // std::fprintf(stderr, "Sending encoded data size: %zu bytes\n", encoded.size());

    // Send the encoded data via ZMQ (non-blocking with dontwait)
    zmq::message_t msg(encoded.size());
    memcpy(msg.data(), encoded.data(), encoded.size());
    if (!zmq_push_socket.send(msg, zmq::send_flags::dontwait)) {
        std::fputs("Failed to send message via ZMQ\n", stderr);
        return;
    }
}