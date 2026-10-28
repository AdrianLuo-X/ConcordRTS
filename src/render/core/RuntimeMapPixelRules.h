#pragma once
#include <cstdint>
namespace concord::render {
// Existing CPU shroud formula, shared as the map GPU's software oracle.
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