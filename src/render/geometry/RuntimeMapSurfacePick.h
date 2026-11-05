#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>

class GameWorld;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Logical map-cell coordinates returned by the existing terrain surface query.
// These are not screen pixels or the host navigation value with invalid (-1,-1).
struct RuntimeMapSurfaceCell {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// The viewport is half-open. A failed query leaves cell unchanged.
bool pickRuntimeMapSurfaceCell(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    const RuntimeInputPixelRect& viewport,
    RuntimeInputPixelPoint screenPoint,
    RuntimeMapRenderCache& cache,
    RuntimeMapSurfaceCell& cell
);
