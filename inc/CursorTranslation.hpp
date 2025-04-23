#pragma once
#include <cstdint>
#include <fcntl.h>
uint8_t cursorInit();
void cursorDeinit();
// Declaration of the producer service function
void cursorTranslationService();
