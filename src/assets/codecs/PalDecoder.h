#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace concord::assets {

struct PalRgbChannels {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    bool operator==(const PalRgbChannels&) const = default;
};

// Original stored bytes, before any display-purpose expansion.
struct PalRawPalette {
    std::array<PalRgbChannels, 256> colors{};
    bool sixBit = true;
    bool operator==(const PalRawPalette&) const = default;
};

enum class PalLengthPolicy { Exact768, Prefix768 };
enum class PalColorPolicy { RoundedSixBit, DacShiftTwo, ReplicatedSixBit };

// Failure leaves output unchanged. Prefix768 ignores bytes after the first 768.
// If any stored channel exceeds 63, the whole palette is treated as eight-bit.
bool decodePalBytes(std::span<const std::uint8_t> bytes,
    PalRawPalette& output, PalLengthPolicy lengthPolicy);

// Purpose-specific mappings are explicit; no color, transparency, remap, alpha,
// pixel-format, resource selection or file-system policy is inferred here.
std::array<PalRgbChannels, 256> mapPalColors(
    const PalRawPalette& raw, PalColorPolicy colorPolicy);

} // namespace concord::assets