#pragma once
#include <cstdint>
#include <fcntl.h>
uint8_t cursorInit(uint8_t detectiontype);
void cursorDeinit();
// Declaration of the producer service function
void cursorTranslationService();
