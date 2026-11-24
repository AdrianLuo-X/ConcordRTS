#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <cstdint>
#include <optional>

struct RuntimeTerrainLayerBlit {
    RuntimeInputPixelRect destination;
    RuntimeInputPixelPoint imageOrigin;
    RuntimeInputPixelPoint sourceOrigin;
};

// Screen coordinates and differences stay in the original signed pixel domain.
// The caller supplies the dimensions of a built, nonempty terrain color layer.
std::optional<RuntimeTerrainLayerBlit> runtimeTerrainLayerClip(
    RuntimeInputPixelRect viewport, RuntimeInputPixelPoint layerOrigin,
    RuntimeInputPixelPoint worldOffset, RuntimeInputPixelSize imageSize) noexcept;

// The caller has clipped both buffers. The destination points at the first
// output pixel and both strides are expressed in pixels.
void runtimeCopyTerrainLayerColorRows(
    const std::uint32_t* pixels, int sourceStride, RuntimeInputPixelPoint sourceOrigin,
    std::uint32_t* destination, int destinationStride, RuntimeInputPixelSize copySize) noexcept;
