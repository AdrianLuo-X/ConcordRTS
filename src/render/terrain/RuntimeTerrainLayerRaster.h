#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct RuntimeTmpSubTileImage;

// The caller owns the color, depth and ownership buffers.
struct RuntimeTerrainLayerPlanes {
    RuntimeInputPixelSize size;
    std::span<std::uint32_t> pixels;
    std::span<std::uint16_t> terrainDepthWords;
    std::span<std::int32_t> terrainCellIndices;
    std::span<std::int32_t> surfaceCellIndices;
};

inline void runtimeWriteTerrainLayerPixel(
    RuntimeTerrainLayerPlanes layer,
    int x,
    int y,
    std::uint32_t color,
    int terrainCellIndex = -1,
    int surfaceCellIndex = -1
) {
    if (x < 0 || y < 0 || x >= layer.size.width || y >= layer.size.height) {
        return;
    }

    const std::size_t offset =
        (static_cast<std::size_t>(y)
            * static_cast<std::size_t>(layer.size.width))
        + static_cast<std::size_t>(x);
    layer.pixels[offset] = color;
    // A painter without authored TMP Z must not leave the hidden previous
    // tile eligible to reject a later pixel. Native put restores accepted Z.
    if (layer.terrainDepthWords.size() == layer.pixels.size()) {
        layer.terrainDepthWords[offset] = 0xffffU;
    }
    if (terrainCellIndex >= 0
        && layer.terrainCellIndices.size() == layer.pixels.size()) {
        layer.terrainCellIndices[offset] = terrainCellIndex;
    }
    const int surfaceOwner = surfaceCellIndex >= 0
        ? surfaceCellIndex : terrainCellIndex;
    if (surfaceOwner >= 0
        && layer.surfaceCellIndices.size() == layer.pixels.size()) {
        layer.surfaceCellIndices[offset] = surfaceOwner;
    }
}

bool runtimeRasterTerrainTileOpaque(
    RuntimeTerrainLayerPlanes layer, int targetX, int targetY,
    const RuntimeTmpSubTileImage& image, const std::array<std::uint32_t, 256>& packedPalette,
    int terrainCellIndex, int surfaceCellIndex);

bool runtimeRasterTerrainTileNativeDepth(
    RuntimeTerrainLayerPlanes layer, std::span<std::uint16_t> zBuffer,
    RuntimeInputPixelPoint center, RuntimeInputPixelPoint cell, RuntimeInputPixelSize tile,
    const RuntimeTmpSubTileImage& image, const std::array<std::uint32_t, 256>& packedPalette,
    int terrainCellIndex);

void runtimeRasterTerrainTileColorSurface(
    const RuntimeTmpSubTileImage& image, const std::array<std::uint32_t, 256>& packedPalette,
    RuntimeInputPixelRect sourceClip, RuntimeInputPixelPoint target,
    std::uint32_t* surfacePixels, RuntimeInputPixelSize surface) noexcept;
