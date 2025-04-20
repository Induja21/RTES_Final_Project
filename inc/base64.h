#pragma once

#include <string>
#include <vector>

// Function declarations
std::string base64_encode(const unsigned char* data, size_t length);
std::string base64_encode(const std::vector<unsigned char>& data);
std::vector<unsigned char> base64_decode(const std::string& encoded);