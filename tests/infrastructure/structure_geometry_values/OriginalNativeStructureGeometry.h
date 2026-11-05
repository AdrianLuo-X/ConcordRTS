#pragma once
#include "render/core/RuntimeMapRenderer.h"

struct OriginalNativeStructureGeometry {
    RECT bounds{};
    std::vector<RuntimeSpriteHitSpan> hitSpans;
    std::vector<RuntimeSpriteHitSpan> foregroundHitSpans;
    int layerCount = 0;
};
