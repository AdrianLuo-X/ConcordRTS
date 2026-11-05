#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// Existing raw packed RGB values and table ownership. Preserve all32 bits,
// including upper flags; native colour arithmetic and pixel alpha stay unchanged.
using RuntimeMapColorValue = std::uint32_t;
using RuntimeOwnerRemapColorTable = std::unordered_map<std::string, RuntimeMapColorValue>;
using RuntimeTerrainColorTable = std::unordered_map<int, RuntimeMapColorValue>;
