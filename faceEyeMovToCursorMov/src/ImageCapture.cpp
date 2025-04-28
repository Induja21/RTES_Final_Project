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

// Static buffer array to preserve original configurations
static struct v4l2_buffer buffers[NUM_OF_MEM_BUFFERS];
// Track buffer state (true = dequeued, false = queued)
static bool buffer_dequeued[NUM_OF_MEM_BUFFERS] = {false};

// Callback to free the buffer after ZMQ is done sending
void free_buffer(void* data, void* hint) {
    struct BufferContext {
        int fd;
        unsigned int index;
    };
    BufferContext* context = static_cast<BufferContext*>(hint);
    
    // Mark buffer as dequeued
    buffer_dequeued[context->index] = true;

    // Re-queue the buffer using the original configuration
    struct v4l2_buffer requeue_buf = buffers[context->index];
    requeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requeue_buf.memory = V4L2_MEMORY_MMAP;
    requeue_buf.index = context->index;

    if (ioctl(context->fd, VIDIOC_QBUF, &requeue_buf) != -1) {
        buffer_dequeued[context->index] = false;
    }
    
    delete context; // Clean up the context
}

void imageCaptureService() {
    static int fd = -1;
    static bool cap_initialized = false;
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
            return;
        }

        // Query the camera's capabilities
        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
            close(fd);
            return;
        }
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
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
            close(fd);
            return;
        }

        // Verify the format and dimensions
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
            close(fd);
            return;
        }
        width = fmt.fmt.pix.width;
        height = fmt.fmt.pix.height;

        // Request buffers
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = NUM_OF_MEM_BUFFERS;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1 || req.count < NUM_OF_MEM_BUFFERS) {
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
                for (unsigned int j = 0; j < i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }

            buffer_lengths[i] = buffers[i].length;
            buffer_starts[i] = mmap(NULL, buffers[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffers[i].m.offset);
            if (buffer_starts[i] == MAP_FAILED) {
                for (unsigned int j = 0; j < i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }

            // Queue the buffer
            if (ioctl(fd, VIDIOC_QBUF, &buffers[i]) == -1) {
                for (unsigned int j = 0; j <= i; ++j) {
                    munmap(buffer_starts[j], buffer_lengths[j]);
                }
                close(fd);
                return;
            }
            buffer_dequeued[i] = false; // Initially queued
        }

        // Start streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
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
            return;
        }
        return;
    }

    unsigned int buf_index = buf.index;
    if (buf_index >= NUM_OF_MEM_BUFFERS) {
        // Re-queue to avoid hanging
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = buf_index;
        ioctl(fd, VIDIOC_QBUF, &buf);
        return;
    }

    // Update the static buffer array and mark as dequeued
    buffers[buf_index] = buf;
    buffer_dequeued[buf_index] = true;

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
        // Re-queue the buffer
        struct v4l2_buffer requeue_buf = buffers[buf_index];
        requeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requeue_buf.memory = V4L2_MEMORY_MMAP;
        requeue_buf.index = buf_index;
        if (ioctl(fd, VIDIOC_QBUF, &requeue_buf) != -1) {
            buffer_dequeued[buf_index] = false;
        }
        return;
    }

    // Create a context to pass to the free callback
    struct BufferContext {
        int fd;
        unsigned int index;
    };
    BufferContext* context = new BufferContext{fd, buf_index};

    // Send the raw frame data as a pointer using zmq::message_t constructor
    zmq::message_t frame_msg(buffer_starts[buf_index], buf.bytesused, free_buffer, context);
    if (!zmq_pub_socket.send(frame_msg, zmq::send_flags::dontwait)) {
        // Clean up context since free_buffer won't be called
        delete context;
        // Re-queue the buffer
        struct v4l2_buffer requeue_buf = buffers[buf_index];
        requeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requeue_buf.memory = V4L2_MEMORY_MMAP;
        requeue_buf.index = buf_index;
        if (ioctl(fd, VIDIOC_QBUF, &requeue_buf) != -1) {
            buffer_dequeued[buf_index] = false;
        }
        return;
    }

    // Try to queue a new buffer
    bool queued = false;
    for (unsigned int i = 0; i < NUM_OF_MEM_BUFFERS; ++i) {
        unsigned int try_index = (next_overwrite_index + i) % NUM_OF_MEM_BUFFERS;
        if (!buffer_dequeued[try_index]) {
            continue;
        }

        struct v4l2_buffer try_buf = buffers[try_index];
        try_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try_buf.memory = V4L2_MEMORY_MMAP;
        try_buf.index = try_index;

        if (ioctl(fd, VIDIOC_QBUF, &try_buf) != -1) {
            buffer_dequeued[try_index] = false;
            next_overwrite_index = (try_index + 1) % NUM_OF_MEM_BUFFERS;
            queued = true;
            break;
        }
    }

    if (!queued) {
        // All buffers are in use; overwrite the oldest dequeued buffer
        unsigned int overwrite_index = next_overwrite_index;
        if (buffer_dequeued[overwrite_index]) {
            struct v4l2_buffer overwrite_buf = buffers[overwrite_index];
            overwrite_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            overwrite_buf.memory = V4L2_MEMORY_MMAP;
            overwrite_buf.index = overwrite_index;

            if (ioctl(fd, VIDIOC_QBUF, &overwrite_buf) != -1) {
                buffer_dequeued[overwrite_index] = false;
                next_overwrite_index = (overwrite_index + 1) % NUM_OF_MEM_BUFFERS;
            }
        }
    }
}