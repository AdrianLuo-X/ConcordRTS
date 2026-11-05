#pragma once

#include "render/geometry/RuntimeSpriteHitSpan.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <vector>

// Existing query: spans retain their original sorted order and borrowed lifetime.
// Screen pixels are signed32; each span contains [left,right) on its exact row.
// The original geometry owner retains the complete containment algorithm.
bool runtimeSpriteHitSpansContain(const std::vector<RuntimeSpriteHitSpan>& spans,
    RuntimeInputPixelPoint point);
