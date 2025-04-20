#pragma once

#include <zmq.hpp>
#include <string>
#include "MessageQueue.hpp"

// Declaration of the message queue service function
void messageQueueToCsvService();

// Function to get the CSV filename
std::string getCsvFileName();

// Function to flush the CSV file
void flushCsvFile();