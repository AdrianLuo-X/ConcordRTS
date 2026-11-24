#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <string>

namespace concord::render::geometry {
// Original stock SHP canvas lookup. A failed query leaves the point unchanged.
bool findInfantryMuzzleAnchor(const std::string& imageId,
    const std::string& sequenceName, int frameIndex, int globalWidth, int globalHeight,
    RuntimeInputPixelPoint& canvasPoint);

struct InfantryMuzzleFrame {
    const std::string& imageId;
    const std::string& sequenceName;
    std::uint64_t sourceFingerprint;
    int frameIndex;
    int width;
    int height;
};

bool resolveInfantryMuzzleAttachment(const InfantryMuzzleFrame& frame,
    const RuntimeInputPixelPoint& centerPixels,
    int tileHeight, RuntimeInputPixelPoint& result);
}
