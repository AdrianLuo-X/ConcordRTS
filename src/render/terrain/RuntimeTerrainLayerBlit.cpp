#include "render/terrain/RuntimeTerrainLayerBlit.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

std::optional<RuntimeTerrainLayerBlit> runtimeTerrainLayerClip(
    RuntimeInputPixelRect viewport, RuntimeInputPixelPoint layerOrigin,
    RuntimeInputPixelPoint worldOffset, RuntimeInputPixelSize imageSize) noexcept {
    const RuntimeInputPixelPoint imageOrigin{
        viewport.left + layerOrigin.x - worldOffset.x,
        viewport.top + layerOrigin.y - worldOffset.y
    };
    const RuntimeInputPixelRect destination{
        std::max(viewport.left, imageOrigin.x),
        std::max(viewport.top, imageOrigin.y),
        std::min(viewport.right, imageOrigin.x + imageSize.width),
        std::min(viewport.bottom, imageOrigin.y + imageSize.height)
    };
    if (destination.right <= destination.left || destination.bottom <= destination.top) {
        return std::nullopt;
    }
    return RuntimeTerrainLayerBlit{
        destination, imageOrigin,
        {destination.left - imageOrigin.x, destination.top - imageOrigin.y}
    };
}

void runtimeCopyTerrainLayerColorRows(
    const std::uint32_t* pixels, int sourceStride, RuntimeInputPixelPoint sourceOrigin,
    std::uint32_t* destination, int destinationStride, RuntimeInputPixelSize copySize) noexcept {
    const std::size_t rowBytes = static_cast<std::size_t>(copySize.width) * sizeof(std::uint32_t);
    for (int row = 0; row < copySize.height; ++row) {
        const auto* sourceRow = pixels
            + static_cast<std::size_t>(sourceOrigin.y + row) * static_cast<std::size_t>(sourceStride)
            + static_cast<std::size_t>(sourceOrigin.x);
        auto* destinationRow = destination
            + static_cast<std::size_t>(row) * static_cast<std::size_t>(destinationStride);
        std::memcpy(destinationRow, sourceRow, rowBytes);
    }
}
