#pragma once
#include "render/core/RuntimeMapRenderer.h"

struct OriginalNativeMobileProbe {
    RECT spriteBounds{};
    RECT renderedBounds{};
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
    POINT imageOrigin{};
};
