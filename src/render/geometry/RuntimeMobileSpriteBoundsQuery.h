#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing memo/capture mobile envelope, in signed screen pixels.
// Right/bottom are exclusive. Every original output is retained even on false.
bool resolveRuntimeMobileObjectSpriteBounds(
    const WorldObjectInstance& object, const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeInputPixelRect& spriteRect
);
