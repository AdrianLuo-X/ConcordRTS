#include "render/terrain/RuntimeTerrainVisualSelection.h"

#include "assets/codecs/RuntimeTmpTypes.h"

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace concord::render {

static bool runtimeTerrainVisualMaskMatches(
    const std::vector<unsigned char>& first,
    const std::vector<unsigned char>& second
) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        if ((first[index] == 0) != (second[index] == 0)) return false;
    }
    return true;
}

bool runtimeTerrainVisualSingleCell(const RuntimeTmpTileFile& file) {
    return file.loaded && file.gridWidth == 1 && file.gridHeight == 1
        && file.images.size() == 1 && file.images.front().present
        && file.images.front().hasTerrainType;
}

bool runtimeTerrainVisualVariantCompatible(
    const RuntimeTmpTileFile& canonical, const RuntimeTmpTileFile& variant
) {
    if (!runtimeTerrainVisualSingleCell(canonical)
        || !runtimeTerrainVisualSingleCell(variant)
        || canonical.blockWidth != variant.blockWidth
        || canonical.blockHeight != variant.blockHeight) return false;
    const auto& original = canonical.images.front();
    const auto& alternate = variant.images.front();
    if (original.terrainType != alternate.terrainType
        || original.rampType != alternate.rampType
        || original.offsetX != alternate.offsetX
        || original.offsetY != alternate.offsetY
        || original.width != alternate.width || original.height != alternate.height
        || original.globalWidth != alternate.globalWidth
        || original.globalHeight != alternate.globalHeight
        || original.baseOffsetX != alternate.baseOffsetX
        || original.baseOffsetY != alternate.baseOffsetY
        || original.baseWidth != alternate.baseWidth
        || original.baseHeight != alternate.baseHeight
        || original.hasBaseZData != alternate.hasBaseZData
        || original.hasExtra != alternate.hasExtra
        || !runtimeTerrainVisualMaskMatches(original.pixels, alternate.pixels)
        || !runtimeTerrainVisualMaskMatches(original.basePixels, alternate.basePixels)) return false;
    if (original.hasBaseZData && original.baseZData != alternate.baseZData) return false;
    if (original.hasExtra
        && (original.extraOffsetX != alternate.extraOffsetX
            || original.extraOffsetY != alternate.extraOffsetY
            || original.extraWidth != alternate.extraWidth
            || original.extraHeight != alternate.extraHeight
            || original.hasExtraZData != alternate.hasExtraZData
            || !runtimeTerrainVisualMaskMatches(original.extraPixels, alternate.extraPixels)
            || (original.hasExtraZData && original.extraZData != alternate.extraZData))) return false;
    // Color pixels and radar RGB may vary. All radar queries retain canonical TMPs.
    return true;
}

std::uint64_t runtimeTerrainVisualFamilyHash(
    int x, int y, int tileIndex, std::string_view normalizedTheaterExtension
) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&](unsigned char byte) {
        hash ^= byte; hash *= 1099511628211ULL;
    };
    for (std::uint32_t value : {static_cast<std::uint32_t>(x),
            static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(tileIndex)}) {
        for (int shift = 0; shift != 32; shift += 8) append(static_cast<unsigned char>(value >> shift));
    }
    for (unsigned char byte : normalizedTheaterExtension) append(byte);
    // Mix every coordinate bit into low output bits before modulo selection.
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    return hash ^ (hash >> 31);
}

} // namespace concord::render
