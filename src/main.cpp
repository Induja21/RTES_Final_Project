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

std::atomic<bool> _runningstate{true};

void signalHandler(int signum)
{
    std::puts("\nReceived Ctrl+C, stopping services ");
    flushCsvFile();
    _runningstate.store(false, std::memory_order_relaxed);
  
}


int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    

    Sequencer sequencer{};
  
    sequencer.addService(messageQueueToCsvService, 1, 99, 300);
    // Add the producer service (runs every 250ms)
    sequencer.addService(cursorTranslationService, 1, 98, 250);
    sequencer.startServices();
    while (_runningstate.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    sequencer.stopServices();
    return 0;
}