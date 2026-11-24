#include "render/terrain/RuntimeTerrainLayerRaster.h"

#include "assets/codecs/RuntimeTmpTypes.h"
#include "render/terrain/RuntimeTerrainNativeDepth.h"

#include <algorithm>
#include <vector>

namespace {

void writeNativeDepthPixel(
    RuntimeTerrainLayerPlanes layer,
    std::span<std::uint16_t> zBuffer,
    int x,
    int y,
    std::uint16_t zValue,
    std::uint32_t color,
    int terrainCellIndex
) {
    if (x < 0 || y < 0 || x >= layer.size.width || y >= layer.size.height) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y) * layer.size.width + x;
    if (index >= zBuffer.size() || zValue > zBuffer[index]) {
        return;
    }
    // TMP accepts equal Z (unlike the strict object SHP leaves). Color and
    // both ownership maps must describe the same accepted source pixel.
    runtimeWriteTerrainLayerPixel(layer, x, y, color, terrainCellIndex, terrainCellIndex);
    zBuffer[index] = zValue;
}
std::uint64_t runtimePackedPaletteHash(
    const std::array<std::uint32_t, 256>& packedPalette
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint32_t color : packedPalette) {
        hash ^= static_cast<std::uint64_t>(color);
        hash *= 1099511628211ULL;
    }
    return hash;
}

const std::vector<std::uint32_t>& runtimePackedTmpPixels(
    const RuntimeTmpSubTileImage& image,
    const std::array<std::uint32_t, 256>& packedPalette
) {
    const std::size_t pixelCount =
        static_cast<std::size_t>(std::max(0, image.width))
        * static_cast<std::size_t>(std::max(0, image.height));
    const std::uint64_t paletteHash = runtimePackedPaletteHash(packedPalette);
    if (image.packedPixelPaletteHash == paletteHash
        && image.packedPixels.size() == pixelCount) {
        return image.packedPixels;
    }

    image.packedPixelPaletteHash = paletteHash;
    image.packedPixels.assign(pixelCount, 0);
    const std::size_t count =
        std::min(pixelCount, image.pixels.size());
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char paletteIndex = image.pixels[index];
        image.packedPixels[index] =
            paletteIndex == 0 ? 0 : packedPalette[paletteIndex];
    }
    return image.packedPixels;
}

// Only standard native TMPs with complete base/extra Z data enter this path.
// Bridge adapters and custom color-only images retain their existing painter.
bool runtimeTmpImageHasNativeDepth(const RuntimeTmpSubTileImage& image) {
    constexpr std::size_t nativeDiamondPixels = 900;
    if (image.baseWidth != 60 || image.baseHeight != 30
        || image.basePixels.size() < nativeDiamondPixels
        || !image.hasBaseZData || image.baseZData.size() < nativeDiamondPixels) {
        return false;
    }
    if (!image.hasExtra) return true;
    const auto count = static_cast<std::size_t>(std::max(0, image.extraWidth))
        * static_cast<std::size_t>(std::max(0, image.extraHeight));
    return count > 0 && image.hasExtraZData
        && image.extraPixels.size() >= count && image.extraZData.size() >= count;
}

} // namespace

bool runtimeRasterTerrainTileOpaque(
    RuntimeTerrainLayerPlanes layer,
    int targetX,
    int targetY,
    const RuntimeTmpSubTileImage& image,
    const std::array<std::uint32_t, 256>& packedPalette,
    int terrainCellIndex,
    int surfaceCellIndex
) {
    if (image.width <= 0
        || image.height <= 0
        || image.pixels.size()
            < static_cast<std::size_t>(image.width)
                * static_cast<std::size_t>(image.height)
    ) {
        return false;
    }

    const int startY = std::max(0, -targetY);
    const int endY = std::min(image.height, layer.size.height - targetY);
    const int startX = std::max(0, -targetX);
    const int endX = std::min(image.width, layer.size.width - targetX);
    const std::vector<std::uint32_t>& packedPixels =
        runtimePackedTmpPixels(image, packedPalette);

    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            const std::uint32_t color =
                packedPixels[static_cast<std::size_t>(y * image.width + x)];
            // Transparency is palette index zero. An opaque black entry may
            // also pack to RGB zero and must still cover the terrain below.
            if (image.pixels[static_cast<std::size_t>(y * image.width + x)] == 0) {
                continue;
            }

            runtimeWriteTerrainLayerPixel(
                layer,
                targetX + x,
                targetY + y,
                color,
                terrainCellIndex,
                surfaceCellIndex
            );
        }
    }

    return true;
}

bool runtimeRasterTerrainTileNativeDepth(
    RuntimeTerrainLayerPlanes layer,
    std::span<std::uint16_t> zBuffer,
    RuntimeInputPixelPoint center,
    RuntimeInputPixelPoint cell,
    RuntimeInputPixelSize tile,
    const RuntimeTmpSubTileImage& image,
    const std::array<std::uint32_t, 256>& packedPalette,
    int terrainCellIndex
) {
    if (tile.width != 60 || tile.height != 30
        || !runtimeTmpImageHasNativeDepth(image)) {
        return runtimeRasterTerrainTileOpaque(
            layer, center.x - tile.width / 2 + image.offsetX,
            center.y - tile.height / 2 + image.offsetY, image, packedPalette, terrainCellIndex, -1);
    }
    const int targetX = center.x - tile.width / 2;
    const int targetY = center.y - tile.height / 2;
    int rowWidth = 0;
    std::size_t sourceIndex = 0;
    for (int y = 0; y < image.baseHeight; ++y) {
        rowWidth += y < image.baseHeight / 2 ? 4 : -4;
        const int xStart = (image.baseWidth - rowWidth) / 2;
        for (int x = 0; x < rowWidth; ++x, ++sourceIndex) {
            const auto color = image.basePixels[sourceIndex];
            if (color == 0) continue;
            writeNativeDepthPixel(layer, zBuffer,
                targetX + image.baseOffsetX + xStart + x,
                targetY + image.baseOffsetY + y,
                runtimeTmpNativeDepthWord(cell.x, cell.y, image.baseZData[sourceIndex]),
                packedPalette[color], terrainCellIndex);
        }
    }
    if (!image.hasExtra) return true;
    const int extraTargetX = targetX + image.extraOffsetX - image.baseOffsetX;
    const int extraTargetY = targetY + image.extraOffsetY - image.baseOffsetY;
    for (int y = 0; y < image.extraHeight; ++y) {
        for (int x = 0; x < image.extraWidth; ++x) {
            const auto source = static_cast<std::size_t>(y) * image.extraWidth + x;
            const auto color = image.extraPixels[source];
            if (color == 0) continue;
            writeNativeDepthPixel(layer, zBuffer, extraTargetX + x, extraTargetY + y,
                runtimeTmpNativeDepthWord(cell.x, cell.y, image.extraZData[source]),
                packedPalette[color], terrainCellIndex);
        }
    }
    return true;
}

void runtimeRasterTerrainTileColorSurface(
    const RuntimeTmpSubTileImage& image, const std::array<std::uint32_t, 256>& packedPalette,
    RuntimeInputPixelRect sourceClip, RuntimeInputPixelPoint target,
    std::uint32_t* surfacePixels, RuntimeInputPixelSize surface) noexcept {
    const int clippedStartX = std::max(sourceClip.left, -target.x);
    const int clippedEndX = std::min(sourceClip.right, surface.width - target.x);
    if (clippedStartX >= clippedEndX) {
        return;
    }

    for (int y = sourceClip.top; y < sourceClip.bottom; ++y) {
        const int screenY = target.y + y;
        if (screenY < 0 || screenY >= surface.height) {
            continue;
        }

        const unsigned char* source =
            image.pixels.data()
            + (static_cast<std::size_t>(y)
                * static_cast<std::size_t>(image.width))
            + static_cast<std::size_t>(clippedStartX);
        std::uint32_t* colorRow =
            surfacePixels
            + (static_cast<std::size_t>(screenY)
                * static_cast<std::size_t>(surface.width))
            + static_cast<std::size_t>(target.x + clippedStartX);

        for (int x = clippedStartX; x < clippedEndX; ++x) {
            const unsigned char paletteIndex =
                source[static_cast<std::size_t>(x - clippedStartX)];
            if (paletteIndex == 0) {
                continue;
            }

            colorRow[static_cast<std::size_t>(x - clippedStartX)] =
                packedPalette[paletteIndex];
        }
    }
}
