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
#include <errno.h>

static constexpr uint8_t NUM_OF_MEM_BUFFERS = 4;

extern zmq::socket_t zmq_pub_socket; // Changed to pub socket

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
            std::fprintf(stderr, "Failed to open video device: %s\n", strerror(errno));
            return;
        }

        // Query the camera's capabilities
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
            std::fprintf(stderr, "Failed to query capabilities: %s\n", strerror(errno));
            close(fd);
            return;
        }
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            std::fputs("Device does not support video capture\n", stderr);
            close(fd);
            return;
        }
        if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
            std::fputs("Device does not support streaming i/o\n", stderr);
            close(fd);
            return;
        }

        // Set the format
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            std::fprintf(stderr, "Failed to set format: %s\n", strerror(errno));
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
        req.count = NUM_OF_MEM_BUFFERS;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
            std::fprintf(stderr, "Failed to request buffers: %s\n", strerror(errno));
            close(fd);
            return;
        }

        // Map the buffer
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = 0;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            std::fprintf(stderr, "Failed to query buffer: %s\n", strerror(errno));
            close(fd);
            return;
        }

        buffer_length = buf.length;
        buffer_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffer_start == MAP_FAILED) {
            std::fprintf(stderr, "Failed to map buffer: %s\n", strerror(errno));
            close(fd);
            return;
        }

        // Queue the buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            std::fprintf(stderr, "Failed to queue buffer: %s\n", strerror(errno));
            munmap(buffer_start, buffer_length);
            close(fd);
            return;
        }

        // Start streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
            std::fprintf(stderr, "Failed to start streaming: %s\n", strerror(errno));
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
        std::fprintf(stderr, "Failed to dequeue buffer: %s\n", strerror(errno));
        return;
    }

    // Send the raw frame data via ZMQ (non-blocking)
    // First, send metadata (width, height, format) as a header
    struct FrameMetadata {
        int width;
        int height;
        uint32_t format;
        size_t data_size;
    };
    FrameMetadata metadata = {width, height, V4L2_PIX_FMT_YUYV, buf.bytesused};

    // Send metadata as the first part of a multi-part message
    zmq::message_t metadata_msg(sizeof(FrameMetadata));
    memcpy(metadata_msg.data(), &metadata, sizeof(FrameMetadata));
    if (!zmq_pub_socket.send(metadata_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait)) {
        std::fputs("Failed to send metadata via ZMQ\n", stderr);
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Send the raw frame data as the second part
    zmq::message_t frame_msg(buf.bytesused);
    memcpy(frame_msg.data(), buffer_start, buf.bytesused);
    if (!zmq_pub_socket.send(frame_msg, zmq::send_flags::dontwait)) {
        std::fputs("Failed to send raw frame data via ZMQ\n", stderr);
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Re-queue the buffer for the next capture
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        std::fprintf(stderr, "Failed to re-queue buffer: %s\n", strerror(errno));
        return;
    }
}