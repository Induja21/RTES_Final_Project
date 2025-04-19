#include "CursorTranslation.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include "Logging.hpp"

// Static counter for unique message IDs
static int message_counter = 0;

void cursorTranslationService() {
    // Get current timestamp for message
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::stringstream message;
    message << "ProducerMsg_" << message_counter++ << "_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec;

    // Push message to queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        message_queue.push(message.str());
    }
}