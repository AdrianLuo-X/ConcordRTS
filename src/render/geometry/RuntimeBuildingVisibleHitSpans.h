#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include "render/geometry/RuntimeSpriteHitSpan.h"
#include <unordered_map>
#include <vector>

class GameWorld;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Original synchronous visible-span query. Output clear, fallback, ownership
// and the original frame/cache lifetime and replay algorithms are unchanged.
bool resolveRuntimeBuildingVisibleHitSpans(const GameWorld& world, const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>>& visibleSpans);
