#pragma once
#include <string>
#include <vector>
// Preserves the existing map packed-section whitespace and padding grammar.
namespace concord::assets {
bool decodeBase64(
    const std::string& encoded,
    std::vector<unsigned char>& decoded,
    std::string& errorMessage
);
}
