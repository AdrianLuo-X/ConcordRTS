#include "assets/codecs/RuntimeMapBase64Codec.h"
#include <cctype>
#include <cstddef>
namespace concord::assets {
namespace {
int base64Value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }

    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }

    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }

    if (value == '+') {
        return 62;
    }

    if (value == '/') {
        return 63;
    }

    return -1;
}
}
bool decodeBase64(
    const std::string& encoded,
    std::vector<unsigned char>& decoded,
    std::string& errorMessage
) {
    decoded.clear();
    errorMessage.clear();

    unsigned int value = 0;
    int bitCount = -8;
    bool sawPadding = false;

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(encoded[i]);

        if (std::isspace(ch) != 0) {
            continue;
        }

        if (encoded[i] == '=') {
            sawPadding = true;
            continue;
        }

        if (sawPadding) {
            errorMessage = "found data after base64 padding at offset "
                + std::to_string(i);
            return false;
        }

        const int digit = base64Value(encoded[i]);
        if (digit < 0) {
            errorMessage = "invalid base64 character at offset "
                + std::to_string(i);
            return false;
        }

        value = ((value << 6) | static_cast<unsigned int>(digit)) & 0x00FFFFFFu;
        bitCount += 6;

        if (bitCount >= 0) {
            decoded.push_back(static_cast<unsigned char>((value >> bitCount) & 0xFFu));
            bitCount -= 8;
        }
    }

    return true;
}
}
