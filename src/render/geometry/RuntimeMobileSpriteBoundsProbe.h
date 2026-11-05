#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <string>
#include <vector>

// Original diagnostic result data. Rasterization and geometry verification remain
// at their existing owners; partial results and owned pixels keep their lifetime.
struct RuntimeMobileSpriteBoundsProbe {
    RuntimeInputPixelRect spriteBounds{};
    RuntimeInputPixelRect renderedBounds{};
    int renderedPixels = 0;
    int outsidePixels = 0;
    int layerCount = 0;
    bool resourceResolved = false;
    bool exactMatch = false;
    bool clipped = false;
    std::string diagnostic;
    // The isolated body render, for contact sheets and visual review.
    std::vector<std::uint32_t> pixels;
    int imageWidth = 0;
    int imageHeight = 0;
    RuntimeInputPixelPoint imageOrigin{};
};
