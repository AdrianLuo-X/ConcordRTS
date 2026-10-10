#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace westwood {

bool decryptMixIndexBlocks(
    const std::vector<std::uint8_t>& keyBlock,
    const std::vector<std::uint8_t>& encryptedBlocks,
    std::vector<std::uint8_t>& decryptedBlocks,
    std::string& errorMessage
);

} // namespace westwood
