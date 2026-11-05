#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include "render/geometry/RuntimeStructureSpriteGeometry.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Original synchronous query. The existing owned result, cache, raster/replay
// and geometry scope lifetime remain with their original implementation.
bool resolveRuntimeStructureSpriteGeometry(
    const WorldObjectInstance& object, const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeStructureSpriteGeometry& geometry
);
