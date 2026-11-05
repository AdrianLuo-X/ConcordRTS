#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing nonzero-pixel missile envelope in signed screen pixels.
// Right and bottom are exclusive; failed queries leave bounds unchanged.
bool resolveRuntimeSpawnedMissileSpriteBounds(
    const WorldObjectInstance& object, const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeInputPixelRect& bounds
);
