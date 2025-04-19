#pragma once

#include <queue>
#include <mutex>
#include <string>

// Declaration of the message queue service function
void messageQueueToCsvService();

void flushCsvFile();

// Shared message queue and mutex
extern std::queue<std::string> message_queue;
extern std::mutex queue_mutex;