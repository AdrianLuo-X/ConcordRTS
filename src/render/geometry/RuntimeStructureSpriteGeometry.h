#pragma once

#include "render/geometry/RuntimeSpriteHitSpan.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <vector>

// Existing owned structure sprite result. Pixel bounds retain signed32 values;
// resolve, paint, normalization and hit semantics remain with their original owners.
struct RuntimeStructureSpriteGeometry {
    RuntimeInputPixelRect bounds{};
    std::vector<RuntimeSpriteHitSpan> hitSpans;
    std::vector<RuntimeSpriteHitSpan> foregroundHitSpans;
    int layerCount = 0;
};
