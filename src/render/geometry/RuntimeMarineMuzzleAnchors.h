#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <string>

namespace concord::render::geometry {
// Original native dolphin SHP frame canvas lookup. False leaves output unchanged.
bool findMarineMuzzleAnchor(const std::string& imageId, int frameIndex,
    int width, int height, RuntimeInputPixelPoint& result);

struct MarineMuzzleFrame {
    const std::string& imageId;
    std::uint64_t sourceFingerprint;
    int frameIndex;
    int width;
    int height;
};

// Project the stock SHP anchor before the renderer's common underwater offset.
bool resolveMarineMuzzleAttachment(const MarineMuzzleFrame& frame,
    const RuntimeInputPixelPoint& centerPixels,
    int tileHeight, RuntimeInputPixelPoint& result);
}
