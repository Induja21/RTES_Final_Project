#include <zmq.hpp>

zmq::context_t zmq_context(1);

// Sockets for image data
zmq::socket_t zmq_pub_socket(zmq_context, ZMQ_PUB); // Publisher socket for imageCaptureService
zmq::socket_t zmq_sub_socket_face(zmq_context, ZMQ_SUB); // Subscriber socket for faceCenterDetectionService
zmq::socket_t zmq_sub_socket_compress(zmq_context, ZMQ_SUB); // Subscriber socket for imageCompressionService

// Sockets for control messages
zmq::socket_t zmq_push_control_socket(zmq_context, ZMQ_PUSH); // Push socket for control messages
zmq::socket_t zmq_pull_control_socket(zmq_context, ZMQ_PULL); // Pull socket for control messages

// Sockets for face center data
zmq::socket_t zmq_push_face_socket(zmq_context, ZMQ_PUSH); // Push socket for faceCenterDetectionService to send face center
zmq::socket_t zmq_pull_face_socket(zmq_context, ZMQ_PULL); // Pull socket for cursorTranslationService to receive face center

void initialize_zmq() {
    // Bind the publisher socket for image data (used by imageCaptureService to send frames)
    zmq_pub_socket.bind("tcp://*:5555");

    // Connect the subscriber sockets for image data and subscribe to all messages
    zmq_sub_socket_face.connect("tcp://localhost:5555");
    zmq_sub_socket_face.set(zmq::sockopt::subscribe, "");

    zmq_sub_socket_compress.connect("tcp://localhost:5555");
    zmq_sub_socket_compress.set(zmq::sockopt::subscribe, "");

    // Bind the push socket for face center data (used by faceCenterDetectionService)
    zmq_push_face_socket.bind("tcp://*:5556");

    // Connect the pull socket for face center data (used by cursorTranslationService)
    zmq_pull_face_socket.connect("tcp://localhost:5556");
    zmq_pull_face_socket.set(zmq::sockopt::rcvhwm, 10);

    // Control message sockets (inproc for same-process communication)
    zmq_pull_control_socket.bind("inproc://control_data");
    zmq_push_control_socket.connect("inproc://control_data");

    // Set high water marks to limit queue size
    zmq_push_control_socket.set(zmq::sockopt::sndhwm, 10);
    zmq_pull_control_socket.set(zmq::sockopt::rcvhwm, 10);
    zmq_push_face_socket.set(zmq::sockopt::sndhwm, 10);
}

void cleanup_zmq() {
    // Close all sockets
    zmq_pub_socket.close();
    zmq_sub_socket_face.close();
    zmq_sub_socket_compress.close();
    zmq_push_control_socket.close();
    zmq_pull_control_socket.close();
    zmq_push_face_socket.close();
    zmq_pull_face_socket.close();

    // Terminate context
    zmq_context.close();
}