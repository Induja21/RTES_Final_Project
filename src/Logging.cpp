#include "Logging.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

// Mutex for CSV file access
static std::mutex csv_mutex;

// Static variables for CSV file and filename
static std::ofstream csv_file;
static std::string csv_filename;
static bool initialized = false;

void messageQueueToCsvService() {
    // Initialize CSV file with timestamp in filename if not already done
    if (!initialized) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);  // Use CLOCK_REALTIME for correct date/time
        std::time_t sec = now.tv_sec;
        std::stringstream filename;
        filename << "data_" << std::put_time(std::localtime(&sec), "%Y-%m-%dT%H-%M-%S") << ".csv";
        csv_filename = filename.str();

        {
            std::lock_guard<std::mutex> lock(csv_mutex);
            csv_file.open(csv_filename, std::ios::app);
            if (!csv_file.is_open()) {
                std::string error_message = "Failed to open CSV file: " + csv_filename;
                std::puts(error_message.c_str());
                return;
            }
            csv_file << "timestamp,data\n";
        }
        initialized = true;
    }

    // Get current timestamp for record
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);  // Use CLOCK_MONOTONIC for high-resolution timestamp
    std::stringstream timestamp;
    timestamp << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec;

    // Check for data from ZeroMQ control socket
    std::string data;
    zmq::message_t message;
    if (zmq_pull_control_socket.recv(message, zmq::recv_flags::dontwait)) {  // Non-blocking
        data = std::string(static_cast<char*>(message.data()), message.size());
    }

    // Append to CSV if data was found
    if (!data.empty() && csv_file.is_open()) {
        std::lock_guard<std::mutex> lock(csv_mutex);
        csv_file << timestamp.str() << "," << data << "\n";
    }
}

void flushCsvFile() {
    std::lock_guard<std::mutex> lock(csv_mutex);
    if (csv_file.is_open()) {
        csv_file.flush();
        csv_file.close();
    }
}