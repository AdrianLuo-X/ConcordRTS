#pragma once
#include <cstdint>

// UI RGB data, not a native color/brush/handle. The original menu bitmap path
// reads 0xAABBGGRR with Get*Value: upper flags are ignored, alpha is supplied
// by the pixel writer. Other legacy color/remap consumers keep their owners.
class RuntimeRgbColorValue {
public:
    static constexpr RuntimeRgbColorValue fromPackedRgb(std::uint32_t packed) noexcept {
        RuntimeRgbColorValue result;
        result.packed_ = packed & 0x00ffffffU;
        return result;
    }
    static constexpr RuntimeRgbColorValue fromChannels(
        std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept {
        return fromPackedRgb(std::uint32_t(red) | (std::uint32_t(green) << 8)
            | (std::uint32_t(blue) << 16));
    }
    constexpr std::uint8_t red() const noexcept { return static_cast<std::uint8_t>(packed_); }
    constexpr std::uint8_t green() const noexcept { return static_cast<std::uint8_t>(packed_ >> 8); }
    constexpr std::uint8_t blue() const noexcept { return static_cast<std::uint8_t>(packed_ >> 16); }
    constexpr std::uint32_t packedRgb() const noexcept { return packed_; }
private:
    std::uint32_t packed_ = 0;
};
