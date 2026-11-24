#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace concord::render {

inline std::uint32_t* mapSoftwarePixelAt(
    std::uint32_t* pixels, int width, int height, int x, int y
) {
    if (pixels == nullptr || width <= 0 || height <= 0
        || x < 0 || y < 0 || x >= width || y >= height) {
        return nullptr;
    }
    return pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
        + static_cast<std::size_t>(x);
}

inline std::uint32_t shadeMapRgb(std::uint32_t color, int delta) {
    const auto channel = [&](int shift) {
        const int value = static_cast<int>((color >> shift) & 255U);
        return static_cast<std::uint32_t>(std::clamp(value + delta, 0, 255));
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

// Existing CPU RGB blend shared with the map GPU's software oracle.
// It transforms RGB and clears the unused DIB high byte, including alpha 0.
inline std::uint32_t blendMapVisionRgb(std::uint32_t pixel,std::uint32_t color,int alpha) {
    const unsigned int opacity=static_cast<unsigned int>(alpha);
    const unsigned int keep=255U-opacity;
    const auto channel=[&](int shift) {
        return (((pixel>>shift)&255U)*keep+((color>>shift)&255U)*opacity+127U)/255U;
    };
    return (channel(16)<<16)|(channel(8)<<8)|channel(0);
}
} // namespace concord::render