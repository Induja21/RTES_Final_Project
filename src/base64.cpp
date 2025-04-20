//Reference : https://github.com/ReneNyffenegger/cpp-base64/blob/master/base64.cpp


#include "base64.h"

std::string base64_encode(const unsigned char* data, size_t length) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string ret;
    ret.reserve((length + 2) / 3 * 4);

    for (size_t i = 0; i < length; i += 3) {
        unsigned char b1 = data[i];
        unsigned char b2 = i + 1 < length ? data[i + 1] : 0;
        unsigned char b3 = i + 2 < length ? data[i + 2] : 0;

        ret += base64_chars[(b1 >> 2) & 0x3F];
        ret += base64_chars[((b1 & 0x03) << 4) | ((b2 >> 4) & 0x0F)];
        ret += (i + 1 < length) ? base64_chars[((b2 & 0x0F) << 2) | ((b3 >> 6) & 0x03)] : '=';
        ret += (i + 2 < length) ? base64_chars[b3 & 0x3F] : '=';
    }

    return ret;
}

std::string base64_encode(const std::vector<unsigned char>& data) {
    return base64_encode(data.data(), data.size());
}

std::vector<unsigned char> base64_decode(const std::string& encoded) {
    static const unsigned char base64_decode_table[] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62,  255, 255, 255, 63,
        52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  255, 255, 255, 255, 255, 255,
        255, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,
        15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  255, 255, 255, 255, 255,
        255, 26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
        41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
    };

    std::vector<unsigned char> decoded;
    decoded.reserve(encoded.size() * 3 / 4);

    for (size_t i = 0; i < encoded.size(); i += 4) {
        if (encoded[i] == '=') break;

        unsigned char b1 = base64_decode_table[static_cast<unsigned char>(encoded[i])];
        if (b1 == 255) return decoded;  // Invalid character

        unsigned char b2 = (i + 1 < encoded.size() && encoded[i + 1] != '=') ?
                          base64_decode_table[static_cast<unsigned char>(encoded[i + 1])] : 0;
        if (b2 == 255) return decoded;

        unsigned char b3 = (i + 2 < encoded.size() && encoded[i + 2] != '=') ?
                          base64_decode_table[static_cast<unsigned char>(encoded[i + 2])] : 0;
        if (b3 == 255) return decoded;

        unsigned char b4 = (i + 3 < encoded.size() && encoded[i + 3] != '=') ?
                          base64_decode_table[static_cast<unsigned char>(encoded[i + 3])] : 0;
        if (b4 == 255) return decoded;

        decoded.push_back((b1 << 2) | (b2 >> 4));
        if (i + 2 < encoded.size() && encoded[i + 2] != '=') {
            decoded.push_back(((b2 & 0x0F) << 4) | (b3 >> 2));
        }
        if (i + 3 < encoded.size() && encoded[i + 3] != '=') {
            decoded.push_back(((b3 & 0x03) << 6) | b4);
        }
    }

    return decoded;
}