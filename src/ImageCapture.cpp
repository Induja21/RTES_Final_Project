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
#include <zmq.hpp>

static constexpr uint8_t NUM_OF_MEM_BUFFERS = 4;

extern zmq::socket_t zmq_pub_socket; // PUB socket for ZeroMQ

// Callback to free the buffer after ZMQ is done sending
void free_buffer(void* data, void* hint) {
    struct BufferContext {
        int fd;
        struct v4l2_buffer buf;
    };
    BufferContext* context = static_cast<BufferContext*>(hint);
    
    // Re-queue the buffer
    if (ioctl(context->fd, VIDIOC_QBUF, &context->buf) == -1) {
        std::fprintf(stderr, "Failed to re-queue buffer: %s\n", strerror(errno));
    }
    
    delete context; // Clean up the context
}

void imageCaptureService() {
    static int fd = -1;
    static bool cap_initialized = false;
    static struct v4l2_buffer buffers[NUM_OF_MEM_BUFFERS];
    static void* buffer_starts[NUM_OF_MEM_BUFFERS] = {nullptr};
    static unsigned int buffer_lengths[NUM_OF_MEM_BUFFERS] = {0};
    static int width = 0;
    static int height = 0;
    static unsigned int next_overwrite_index = 0; // Tracks oldest buffer to overwrite

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
        if (req.count < NUM_OF_MEM_BUFFERS) {
            std::fprintf(stderr, "Insufficient buffers allocated: %d\n", req.count);
            close(fd);
            return;
        }

        // Map and queue all buffers
        for (unsigned int i = 0; i < NUM_OF_MEM_BUFFERS; ++i) {
            memset(&buffers[i], 0, sizeof(buffers[i]));
            buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffers[i].memory = V4L2_MEMORY_MMAP;
            buffers[i].index = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &buffers[i]) == -1) {
                std::fprintf(stderr, "Failed to query buffer %u: %s\n", i, strerror(errno));
                for (unsigned int j = 0; j < i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }

            buffer_lengths[i] = buffers[i].length;
            buffer_starts[i] = mmap(NULL, buffers[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffers[i].m.offset);
            if (buffer_starts[i] == MAP_FAILED) {
                std::fprintf(stderr, "Failed to map buffer %u: %s\n", i, strerror(errno));
                for (unsigned int j = 0; j < i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }

            // Queue the buffer
            if (ioctl(fd, VIDIOC_QBUF, &buffers[i]) == -1) {
                std::fprintf(stderr, "Failed to queue buffer %u: %s\n", i, strerror(errno));
                for (unsigned int j = 0; j <= i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }
        }

        // Start streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
            std::fprintf(stderr, "Failed to start streaming: %s\n", strerror(errno));
            for (unsigned int i = 0; i < NUM_OF_MEM_BUFFERS; ++i) {
                munmap(buffer_starts[i], buffer_lengths[i]);
            }
            close(fd);
            return;
        }

        cap_initialized = true;
    }

    // Non-blocking frame capture
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Try to dequeue a buffer
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            // No frame available yet, return without blocking
            return;
        }
        std::fprintf(stderr, "Failed to dequeue buffer: %s\n", strerror(errno));
        return;
    }

    unsigned int buf_index = buf.index;
    if (buf_index >= NUM_OF_MEM_BUFFERS) {
        std::fprintf(stderr, "Invalid buffer index: %u\n", buf_index);
        ioctl(fd, VIDIOC_QBUF, &buf); // Re-queue to avoid hanging
        return;
    }

    // Send the raw frame data via ZMQ (non-blocking)
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
        // Re-queue the buffer
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Create a context to pass to the free callback
    struct BufferContext {
        int fd;
        struct v4l2_buffer buf;
    };
    BufferContext* context = new BufferContext{fd, buf};

    // Send the raw frame data as a pointer using zmq::message_t constructor
    zmq::message_t frame_msg(buffer_starts[buf_index], buf.bytesused, free_buffer, context);
    if (!zmq_pub_socket.send(frame_msg, zmq::send_flags::dontwait)) {
        std::fputs("Failed to send raw frame data via ZMQ\n", stderr);
        // Clean up context since free_buffer won't be called
        delete context;
        // Re-queue the buffer
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Try to queue a new buffer
    bool queued = false;
    for (unsigned int i = 0; i < NUM_OF_MEM_BUFFERS; ++i) {
        unsigned int try_index = (next_overwrite_index + i) % NUM_OF_MEM_BUFFERS;
        struct v4l2_buffer& try_buf = buffers[try_index];
        
        // Check if the buffer is available (not dequeued or in use)
        try_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try_buf.memory = V4L2_MEMORY_MMAP;
        try_buf.index = try_index;

        if (ioctl(fd, VIDIOC_QUERYBUF, &try_buf) == -1) {
            continue; // Buffer might be in use, try next
        }

        if (ioctl(fd, VIDIOC_QBUF, &try_buf) == -1) {
            continue; // Failed to queue, try next
        }

        next_overwrite_index = (try_index + 1) % NUM_OF_MEM_BUFFERS;
        queued = true;
        break;
    }

    if (!queued) {
        // All buffers are in use; overwrite the oldest dequeued buffer
        unsigned int overwrite_index = next_overwrite_index;
        struct v4l2_buffer& overwrite_buf = buffers[overwrite_index];

        overwrite_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        overwrite_buf.memory = V4L2_MEMORY_MMAP;
        overwrite_buf.index = overwrite_index;

        if (ioctl(fd, VIDIOC_QBUF, &overwrite_buf) == -1) {
            std::fprintf(stderr, "Failed to overwrite buffer %u: %s\n", overwrite_index, strerror(errno));
        } else {
            next_overwrite_index = (overwrite_index + 1) % NUM_OF_MEM_BUFFERS;
        }
    }
}