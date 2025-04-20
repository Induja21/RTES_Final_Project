#include "ImageCapture.hpp"
#include <linux/videodev2.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <vector>
#include <string>
#include <cstring>
#include <opencv2/opencv.hpp>
#include "base64.h"

void imageCaptureService() {
    static int fd = -1;
    static bool cap_initialized = false;
    static struct v4l2_buffer buf;
    static void* buffer_start = nullptr;
    static unsigned int buffer_length = 0;
    static int width = 0;
    static int height = 0;

    // Initialize the camera device
    if (!cap_initialized) {
        // Open the video device
        fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
        if (fd == -1) {
            std::fputs("Failed to open video device\n", stderr);
            return;
        }

        // Query the camera's capabilities
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
            std::fputs("Failed to query capabilities\n", stderr);
            close(fd);
            return;
        }
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            std::fputs("Device does not support video capture\n", stderr);
            close(fd);
            return;
        }

        // Set the format
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;  // Default, will be adjusted
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; // YUYV format
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            std::fputs("Failed to set format\n", stderr);
            close(fd);
            return;
        }

        // Verify the format and dimensions
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
            std::fputs("Camera does not support YUYV format\n", stderr);
            close(fd);
            return;
        }
        width = fmt.fmt.pix.width;
        height = fmt.fmt.pix.height;
        fprintf(stderr, "Camera resolution: %dx%d\n", width, height);

        // Request buffers
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 1; // Single buffer for simplicity
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
            std::fputs("Failed to request buffers\n", stderr);
            close(fd);
            return;
        }

        // Map the buffer
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = 0;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            std::fputs("Failed to query buffer\n", stderr);
            close(fd);
            return;
        }

        buffer_length = buf.length;
        buffer_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffer_start == MAP_FAILED) {
            std::fputs("Failed to map buffer\n", stderr);
            close(fd);
            return;
        }

        // Queue the buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            std::fputs("Failed to queue buffer\n", stderr);
            munmap(buffer_start, buffer_length);
            close(fd);
            return;
        }

        // Start streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
            std::fputs("Failed to start streaming\n", stderr);
            munmap(buffer_start, buffer_length);
            close(fd);
            return;
        }

        cap_initialized = true;
    }

    // Non-blocking frame capture
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    // Check if a frame is ready (non-blocking)
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            // No frame available yet, return without blocking
            return;
        }
        std::fputs("Failed to dequeue buffer\n", stderr);
        return;
    }

    // Convert YUYV to BGR
    cv::Mat yuyv(height, width, CV_8UC2, buffer_start);
    cv::Mat frame;
    try {
        cv::cvtColor(yuyv, frame, cv::COLOR_YUV2BGR_YUYV); // Use the correct conversion code
    } catch (const cv::Exception& e) {
        std::fprintf(stderr, "Color conversion failed: %s\n", e.what());
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }
    if (frame.empty()) {
        std::fputs("Failed to convert frame\n", stderr);
        // Re-queue the buffer
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Encode the frame as JPEG
    std::vector<uchar> buffer;
    if (!cv::imencode(".jpg", frame, buffer)) {
        std::fputs("Failed to encode frame\n", stderr);
        // Re-queue the buffer
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Base64 encode the buffer
    std::string encoded = base64_encode(buffer);

    // Send the encoded data via ZMQ (non-blocking with dontwait)
    zmq::message_t msg(encoded.size());
    memcpy(msg.data(), encoded.data(), encoded.size());
    if (!zmq_push_socket.send(msg, zmq::send_flags::dontwait)) {
        std::fputs("Failed to send message via ZMQ\n", stderr);
        // Re-queue the buffer
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Re-queue the buffer for the next capture
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        std::fputs("Failed to re-queue buffer\n", stderr);
        return;
    }
}