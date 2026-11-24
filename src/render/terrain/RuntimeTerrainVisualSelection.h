#pragma once

#include <cstdint>
#include <string_view>

struct RuntimeTmpTileFile;

namespace concord::render {

bool runtimeTerrainVisualSingleCell(const RuntimeTmpTileFile& file);
bool runtimeTerrainVisualVariantCompatible(
    const RuntimeTmpTileFile& canonical, const RuntimeTmpTileFile& variant);

// Resource lookup supplies the same normalized extension used by the renderer.
std::uint64_t runtimeTerrainVisualFamilyHash(
    int x, int y, int tileIndex, std::string_view normalizedTheaterExtension) noexcept;

} // namespace concord::render
