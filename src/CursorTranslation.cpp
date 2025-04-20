#include "CursorTranslation.hpp"
#include "Logging.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>

// Static counter for unique message IDs
static int message_counter = 0;

void cursorTranslationService() {
    // Get current timestamp for message
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::stringstream message;
    message << "ProducerMsg_" << message_counter++ << "_" << now.tv_sec << "." << std::setw(9) << std::setfill('0') << now.tv_nsec;

    // Send message via ZeroMQ
    std::string msg_str = message.str();
    zmq::message_t msg(msg_str.size());
    memcpy(msg.data(), msg_str.data(), msg_str.size());
    // Non-blocking with dontwait
    zmq_push_control_socket.send(msg, zmq::send_flags::dontwait);  
}