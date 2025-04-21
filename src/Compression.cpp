#include "Compression.hpp"
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <vector>
#include <fstream>
#include <linux/videodev2.h>
#include <zmq.hpp>

extern zmq::socket_t zmq_sub_socket_compress; // Changed to sub socket

void imageCompressionService() {
    static bool folder_initialized = false;
    if (!folder_initialized) {
        std::filesystem::create_directory("images");
        folder_initialized = true;
    }

    while (true) {
        // Receive the metadata (first part of the multi-part message)
        zmq::message_t metadata_msg;
        if (!zmq_sub_socket_compress.recv(metadata_msg, zmq::recv_flags::dontwait)) {
            break; // No message available, exit loop
        }

        // Ensure the second part (frame data) is available
        if (!metadata_msg.more()) {
            std::fputs("Missing frame data part in ZMQ message\n", stderr);
            continue;
        }

        // Extract metadata
        struct FrameMetadata {
            int width;
            int height;
            uint32_t format;
            size_t data_size;
        };
        if (metadata_msg.size() != sizeof(FrameMetadata)) {
            std::fputs("Invalid metadata size received\n", stderr);
            continue;
        }
        FrameMetadata metadata;
        memcpy(&metadata, metadata_msg.data(), sizeof(FrameMetadata));

        // Verify format
        if (metadata.format != V4L2_PIX_FMT_YUYV) {
            std::fprintf(stderr, "Unsupported frame format: %u\n", metadata.format);
            continue;
        }

        // Receive the raw frame data (second part)
        zmq::message_t frame_msg;
        if (!zmq_sub_socket_compress.recv(frame_msg, zmq::recv_flags::dontwait)) {
            std::fputs("Failed to receive frame data\n", stderr);
            continue;
        }

        if (frame_msg.size() != metadata.data_size) {
            std::fprintf(stderr, "Frame data size mismatch: expected %zu, received %zu\n",
                         metadata.data_size, frame_msg.size());
            continue;
        }

        // Create a cv::Mat from the raw YUYV data
        cv::Mat yuyv(metadata.height, metadata.width, CV_8UC2, frame_msg.data());
        cv::Mat image;
        try {
            cv::cvtColor(yuyv, image, cv::COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& e) {
            std::fprintf(stderr, "Color conversion failed: %s\n", e.what());
            continue;
        }
        if (image.empty()) {
            std::fputs("Failed to convert frame to BGR\n", stderr);
            continue;
        }

        // Compress the image to JPEG
        std::vector<unsigned char> compressed_data;
        std::vector<int> compression_params = {cv::IMWRITE_JPEG_QUALITY, 80};
        if (!cv::imencode(".jpg", image, compressed_data, compression_params)) {
            std::fputs("Failed to compress image\n", stderr);
            continue;
        }

        // Generate a unique filename based on timestamp
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        std::stringstream filename;
        filename << "images/image_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec << ".jpg";

        // Save the compressed image to file
        std::ofstream outfile(filename.str(), std::ios::binary);
        if (!outfile.is_open()) {
            std::string error_message = "Failed to open image file: " + filename.str() + "\n";
            std::fputs(error_message.c_str(), stderr);
            continue;
        }
        outfile.write(reinterpret_cast<const char*>(compressed_data.data()), compressed_data.size());
        outfile.close();
    }
}