#include "MessageQueue.hpp"

zmq::context_t zmq_context(1);

// Sockets for image data
zmq::socket_t zmq_push_socket(zmq_context, ZMQ_PUSH);
zmq::socket_t zmq_pull_socket(zmq_context, ZMQ_PULL);

// Sockets for control messages
zmq::socket_t zmq_push_control_socket(zmq_context, ZMQ_PUSH);
zmq::socket_t zmq_pull_control_socket(zmq_context, ZMQ_PULL);

void initialize_zmq() {
    // Image data sockets
    zmq_pull_socket.bind("inproc://image_data");
    zmq_push_socket.connect("inproc://image_data");

    // Control message sockets
    zmq_pull_control_socket.bind("inproc://control_data");
    zmq_push_control_socket.connect("inproc://control_data");

    // Optional: Set socket options (e.g., high water mark to limit queue size)
    zmq_push_socket.set(zmq::sockopt::sndhwm, 10);  // Limit queue to 10 messages
    zmq_pull_socket.set(zmq::sockopt::rcvhwm, 10);
    zmq_push_control_socket.set(zmq::sockopt::sndhwm, 10);
    zmq_pull_control_socket.set(zmq::sockopt::rcvhwm, 10);
}

void cleanup_zmq() {
    // Close sockets
    zmq_push_socket.close();
    zmq_pull_socket.close();
    zmq_push_control_socket.close();
    zmq_pull_control_socket.close();

    // Terminate context
    zmq_context.close();
}