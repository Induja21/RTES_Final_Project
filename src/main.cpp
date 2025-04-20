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
#include <fstream>
#include "Sequencer.hpp"
#include "Logging.hpp"
#include "CursorTranslation.hpp"
#include "ImageCapture.hpp"
#include "Compression.hpp"
#include "MessageQueue.hpp"

std::atomic<bool> _runningstate{true};

void signalHandler(int signum)
{
    std::puts("\nReceived Ctrl+C, stopping services ");
    _runningstate.store(false, std::memory_order_relaxed);
    flushCsvFile();
    //cleanup_zmq();
  
}


int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    
    initialize_zmq();
    Sequencer sequencer{};
  
    sequencer.addService(messageQueueToCsvService, 1, 99, 300);
    // Add the producer service (runs every 250ms)
    sequencer.addService(cursorTranslationService, 1, 98, 250);
    sequencer.addService(imageCaptureService, 3, 97, 300);      // Captures images, 300ms
    sequencer.addService(imageCompressionService, 2, 96, 500);  // Compresses images, 500ms
    sequencer.startServices();
    while (_runningstate.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sequencer.stopServices();
    return 0;
}