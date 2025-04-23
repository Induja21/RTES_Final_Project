#include "MessageQueue.hpp"

zmq::context_t zmq_context(1);

// Sockets for image data
zmq::socket_t zmq_pub_socket(zmq_context, ZMQ_PUB); // Publisher socket for imageCaptureService
zmq::socket_t zmq_sub_socket_eye(zmq_context, ZMQ_SUB); // Subscriber socket for eyeDetectionService
zmq::socket_t zmq_sub_socket_compress(zmq_context, ZMQ_SUB); // Subscriber socket for imageCompressionService

// Sockets for control messages
zmq::socket_t zmq_push_control_socket(zmq_context, ZMQ_PUSH);
zmq::socket_t zmq_pull_control_socket(zmq_context, ZMQ_PULL);

// Socket for eyeball data
zmq::socket_t zmq_push_eyeball_socket(zmq_context, ZMQ_PUSH);
zmq::socket_t zmq_pull_eyeball_socket(zmq_context, ZMQ_PULL);
void initialize_zmq() {
    // Bind the publisher socket
    zmq_pub_socket.bind("tcp://*:5555");

    // Connect the subscriber sockets and subscribe to all messages
    zmq_sub_socket_eye.connect("tcp://localhost:5555");
    // Subscribe to all messages
    zmq_sub_socket_eye.set(zmq::sockopt::subscribe, ""); 

    zmq_sub_socket_compress.connect("tcp://localhost:5555");
    zmq_sub_socket_compress.set(zmq::sockopt::subscribe, "");

    // Control message sockets
    zmq_pull_control_socket.bind("inproc://control_data");
    zmq_push_control_socket.connect("inproc://control_data");

    zmq_push_control_socket.set(zmq::sockopt::sndhwm, 10);
    zmq_pull_control_socket.set(zmq::sockopt::rcvhwm, 10);

    // Eyeball data PUSH socket
    zmq_push_eyeball_socket.bind("tcp://*:5556");
    zmq_push_eyeball_socket.set(zmq::sockopt::sndhwm, 10);

    zmq_pull_eyeball_socket.bind("inproc://eyeball_data");
    zmq_push_eyeball_socket.connect("inproc://eyeball_data");

}

void cleanup_zmq() {
    // Close sockets
    zmq_pub_socket.close();
    zmq_sub_socket_eye.close();
    zmq_sub_socket_compress.close();
    zmq_push_control_socket.close();
    zmq_pull_control_socket.close();
    zmq_push_eyeball_socket.close();

    // Terminate context
    zmq_context.close();
}