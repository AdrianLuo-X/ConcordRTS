#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <string>

namespace concord::render::geometry {
struct StructureMuzzleFlh {
    int forwardLeptons;
    int lateralLeptons;
    int heightLeptons;
};

struct StructureMuzzleFrame {
    const std::string& imageId;
    std::uint64_t sourceFingerprint;
    int frameIndex;
    int width;
    int height;
};

bool findStructureMuzzleAnchor(const std::string& imageId, std::uint64_t fingerprint,
    int frame, int width, int height, RuntimeInputPixelPoint& point);

bool resolveAuthoredStructureMuzzle(const std::string& text, bool dualOffset,
    const StructureMuzzleFlh& flh, int firingFacing,
    const RuntimeInputPixelPoint& centerPixels, RuntimeInputPixelPoint& result);

bool resolveStructureFrameMuzzle(const StructureMuzzleFrame& frame,
    const RuntimeInputPixelPoint& centerPixels, const RuntimeInputPixelPoint& frameOffset,
    RuntimeInputPixelPoint& result);
}
