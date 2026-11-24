#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace concord::assets {
// Exact original LZO1x block contract. The caller supplies an already validated
// compressed range, as the Map chunk-table owner does. Partial/error/output
// and allocation behavior retain the original implementation.
bool decodeLzo1xBlock(
    const std::vector<unsigned char>& data,
    std::size_t compressedOffset,
    std::size_t compressedBytes,
    std::size_t expectedBytes,
    std::size_t& decodedBytes,
    std::vector<unsigned char>* decodedOutput,
    std::string& errorMessage);
} // namespace concord::assets
