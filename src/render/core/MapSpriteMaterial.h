#pragma once
#include <cstdint>
namespace concord::render {
// Owned presentation resource offsets only. Source coverage and geometry remain CPU values.
struct MapSpriteMaterial {
    std::uint32_t width=0,height=0,indices=0,remapMask=0,lightLevels=0;
    std::uint32_t baseColors=0,remapColors=0,litColors=0,litRemapColors=0;
    std::uint32_t flags=0,airstrike=0,reserved=0;
    static constexpr std::uint32_t Owner=1,Mask=2,Light=4,Soften=8,Chaos=16;
};
static_assert(sizeof(MapSpriteMaterial)==48);
} // namespace concord::render
