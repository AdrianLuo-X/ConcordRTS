#include "assets/codecs/PalDecoder.h"

#include <cstddef>

namespace concord::assets {
namespace {
std::uint8_t expandChannel(std::uint8_t value, PalColorPolicy policy) {
    switch (policy) {
    case PalColorPolicy::RoundedSixBit:
        return static_cast<std::uint8_t>((static_cast<unsigned int>(value) * 255U + 31U) / 63U);
    case PalColorPolicy::DacShiftTwo:
        return static_cast<std::uint8_t>(static_cast<unsigned int>(value) << 2U);
    case PalColorPolicy::ReplicatedSixBit:
        return static_cast<std::uint8_t>((value << 2U) | (value >> 4U));
    }
    return value;
}
} // namespace

bool decodePalBytes(std::span<const std::uint8_t> bytes,
    PalRawPalette& output, PalLengthPolicy lengthPolicy) {
    switch (lengthPolicy) {
    case PalLengthPolicy::Exact768:
        if (bytes.size() != 768U) return false;
        break;
    case PalLengthPolicy::Prefix768:
        if (bytes.size() < 768U) return false;
        break;
    default:
        return false;
    }
    PalRawPalette decoded;
    for (std::size_t index = 0; index < decoded.colors.size(); ++index) {
        const std::size_t offset = index * 3U;
        const auto r = bytes[offset];
        const auto g = bytes[offset + 1U];
        const auto b = bytes[offset + 2U];
        decoded.colors[index] = {r, g, b};
        if (r > 63U || g > 63U || b > 63U) decoded.sixBit = false;
    }
    output = decoded;
    return true;
}

std::array<PalRgbChannels, 256> mapPalColors(
    const PalRawPalette& raw, PalColorPolicy colorPolicy) {
    auto colors = raw.colors;
    if (!raw.sixBit) return colors;
    for (auto& color : colors) {
        color = {expandChannel(color.r, colorPolicy),
            expandChannel(color.g, colorPolicy), expandChannel(color.b, colorPolicy)};
    }
    return colors;
}
} // namespace concord::assets