#include "Logging.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

// Definition of the shared message queue and mutex
std::queue<std::string> message_queue;
std::mutex queue_mutex;

// Mutex for CSV file access
static std::mutex csv_mutex;

// Static variables for CSV file and filename
static std::ofstream csv_file;
static std::string csv_filename;
static bool initialized = false;

void messageQueueToCsvService() {
    // Initialize CSV file with timestamp in filename if not already done
    if (!initialized) {
        // Get current timestamp for filename
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        std::time_t sec = now.tv_sec;
        std::stringstream filename;
        filename << "data_" << std::put_time(std::localtime(&sec), "%Y-%m-%dT%H-%M-%S") << ".csv";
        csv_filename = filename.str(); // Store filename

        // Open CSV file in append mode
        {
            std::lock_guard<std::mutex> lock(csv_mutex);
            csv_file.open(csv_filename, std::ios::app);
            if (!csv_file.is_open()) {
                // Construct the error message
                std::string error_message = "Failed to open CSV file: " + csv_filename;
                std::puts(error_message.c_str());
                return;
            }
            // Write header
            csv_file << "timestamp,data\n";
        }
        initialized = true;
    }

    // Check queue for data
    std::string data;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!message_queue.empty()) {
            data = message_queue.front();
            message_queue.pop();
        }
    }

    // Append to CSV if data was found
    if (!data.empty() && csv_file.is_open()) {
        std::lock_guard<std::mutex> lock(csv_mutex);
        csv_file << data << "\n";
        // No flush() here to reduce I/O overhead
    }
}


void flushCsvFile() {
    std::lock_guard<std::mutex> lock(csv_mutex);
    if (csv_file.is_open()) {
        csv_file.flush();
    }
}