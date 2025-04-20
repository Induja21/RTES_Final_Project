#pragma once

#include <zmq.hpp>

extern zmq::context_t zmq_context;

// Sockets for image data
extern zmq::socket_t zmq_push_socket;  // Used by imageCaptureService to send images
extern zmq::socket_t zmq_pull_socket;  // Used by imageCompressionService to receive images

// Sockets for control messages
extern zmq::socket_t zmq_push_control_socket;  // Used for sending timestamp to LoggingService
extern zmq::socket_t zmq_pull_control_socket;  // Used by Logging to receive the messages sent to this service

void initialize_zmq();
void cleanup_zmq();