#include "Compression.hpp"
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <vector>
#include <fstream>
#include "base64.h"

void imageCompressionService() {
    static bool folder_initialized = false;
    if (!folder_initialized) {
        std::filesystem::create_directory("images");
        folder_initialized = true;
    }

    bool received_message = false;
    while (true) {
        std::string encoded_data;
        zmq::message_t message;
        if (!zmq_pull_socket.recv(message, zmq::recv_flags::dontwait)) {
            break;
        }

        received_message = true;
        encoded_data = std::string(static_cast<char*>(message.data()), message.size());

      //  std::fprintf(stderr, "Received encoded data size: %zu bytes\n", encoded_data.size());

        std::vector<unsigned char> image_data = base64_decode(encoded_data);
        if (image_data.empty()) {
            std::fputs("Base64 decoding failed\n", stderr);
            continue;
        }

       // std::fprintf(stderr, "Decoded data size: %zu bytes\n", image_data.size());

        if (image_data.size() < 1000) {
            //std::fputs("Decoded data too small to be a valid image\n", stderr);
            continue;
        }

        cv::Mat image = cv::imdecode(image_data, cv::IMREAD_COLOR);
        if (image.empty()) {
           // std::fputs("Failed to decode image data with cv::imdecode\n", stderr);
            continue;
        }

        std::vector<unsigned char> compressed_data;
        std::vector<int> compression_params = {cv::IMWRITE_JPEG_QUALITY, 80};
        if (!cv::imencode(".jpg", image, compressed_data, compression_params)) {
            std::fputs("Failed to compress image\n", stderr);
            continue;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        std::stringstream filename;
        filename << "images/image_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec << ".jpg";

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