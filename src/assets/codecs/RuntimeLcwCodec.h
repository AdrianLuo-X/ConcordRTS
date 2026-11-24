#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace concord::assets {
bool decodeLcwBlock(
    const std::vector<unsigned char>& data,
    std::size_t compressedOffset,
    std::size_t compressedBytes,
    std::size_t expectedBytes,
    std::size_t& decodedBytes,
    std::vector<unsigned char>* decodedOutput,
    std::string& errorMessage
);
}
