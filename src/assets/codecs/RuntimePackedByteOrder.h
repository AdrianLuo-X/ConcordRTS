#pragma once
#include <cstddef>
#include <vector>
namespace concord::asset_bytes {
// Caller retains the original validated-range precondition.
inline unsigned int readUInt16LE(const std::vector<unsigned char>& data, std::size_t offset) {
    return static_cast<unsigned int>(data[offset])
        | (static_cast<unsigned int>(data[offset + 1]) << 8);
}
} // namespace concord::asset_bytes
