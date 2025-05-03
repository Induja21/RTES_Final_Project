#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <atomic>
#include <string>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <iostream>
#include <unistd.h>
#include <linux/gpio.h>
#include <chrono>
#include <thread>

#include <fstream>
#include "Sequencer.hpp"
#include "Logging.hpp"
#include "CursorTranslation.hpp"
#include "ImageCapture.hpp"
#include "Compression.hpp"
#include "MessageQueue.hpp"
#include "ImageProcessing.hpp"

static constexpr uint8_t CURSOR_TRANSLATION_PRIORITY= 99;
static constexpr uint8_t IMAGE_CAPTURE_PRIORITY= 98;
static constexpr uint8_t FACE_EYE_DETECTION_PRIORITY= 97;
static constexpr uint8_t IMAGE_COMPRESSION_PRIORITY= 99;
static constexpr uint8_t LOGGING_PRIORITY= 98;

static constexpr uint8_t CURSOR_TRANSLATION_DEADLINE= 50;
static constexpr uint8_t IMAGE_CAPTURE_DEADLINE= 60;
static constexpr uint8_t FACE_EYE_DETECTION_DEADLINE= 100;
static constexpr uint8_t IMAGE_COMPRESSION_DEADLINE= 70;
static constexpr uint8_t LOGGING_DEADLINE= 250;
std::atomic<bool> _runningstate{true};

void signalHandler(int signum)
{
    // Only set the flag to ensure async-signal-safety
    _runningstate.store(false, std::memory_order_relaxed);
}

int main(int argc, char* argv[])
{
    // Install signal handler for SIGINT
    if (std::signal(SIGINT, signalHandler) == SIG_ERR) {
        std::cerr << "Error: Failed to install SIGINT handler\n";
        return 1;
    }
    
    if (argc < 2) {
        std::cerr << "Detection Type: " << argv[0] << " <method_number>\n"
                  << "Where <method_number> corresponds to the detection type:\n"
                  << "  1: Face Detection\n"
                  << "  2: Eye Detection\n";
        return 1;
    }

    int detection_type = std::stoi(argv[1]);
    if (detection_type > 2) {
        std::cerr << "Invalid input. Please enter 1 (Face Detection), 2 (Eye Detection)\n";
        return 1;
    }
    


    // Declare sequencer outside try block to ensure scope in catch
    Sequencer sequencer;

    // Initialize resources
    try {
        cursorInit(detection_type);
		initImageProcessingService(detection_type);
        imageCaptureInit();
        initialize_zmq();
        initCompressionService();
        initLoggingService();

        // Add services
        sequencer.addService("cursorTranslationService", cursorTranslationService, 0, CURSOR_TRANSLATION_PRIORITY, CURSOR_TRANSLATION_DEADLINE);
        sequencer.addService("imageCaptureService", imageCaptureService, 0, IMAGE_CAPTURE_PRIORITY, IMAGE_CAPTURE_DEADLINE);
        sequencer.addService("DetectionService", DetectionService, 0, FACE_EYE_DETECTION_PRIORITY, FACE_EYE_DETECTION_DEADLINE);
        sequencer.addService("imageCompressionService", imageCompressionService, 1, IMAGE_COMPRESSION_PRIORITY, IMAGE_COMPRESSION_DEADLINE);
        sequencer.addService("messageQueueToCsvService", messageQueueToCsvService, 1, LOGGING_PRIORITY, LOGGING_DEADLINE);

        // Start services
        sequencer.startServices();

        // Main loop: Wait until SIGINT or error
        while (_runningstate.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Shutdown: Stop services and clean up
        std::puts("Stopping services...");
        sequencer.stopServices(); // Stop services in main thread

        // Clean up resources
        std::puts("Cleaning up resources...");
        flushCsvFile();
        cursorDeinit();
        cleanup_zmq();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        _runningstate.store(false, std::memory_order_relaxed);
        sequencer.stopServices(); // Now in scope
        flushCsvFile();
        cursorDeinit();
        cleanup_zmq();
        return 1;
    }

    std::puts("Shutdown complete.");
    return 0;
}
