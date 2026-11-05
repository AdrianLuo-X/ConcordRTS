#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing SHP envelope in signed screen pixels, right/bottom exclusive.
// The producer resets the output and preserves each computed rectangle even
// when it returns false. stableGlobalCanvas retains the original default.
bool resolveRuntimeObjectShpSpriteBounds(
    const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RuntimeInputPixelRect& spriteRect,
    bool stableGlobalCanvas = false
);
