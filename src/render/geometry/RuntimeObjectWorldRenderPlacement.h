#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

// Existing placement result only. The original projection, lift, factory clipping
// and sorting algorithms retain their owners and signed32 arithmetic domain.
struct RuntimeObjectWorldRenderPlacement {
    RuntimeInputPixelPoint center{};
    RuntimeInputPixelPoint sortPoint{};
    RuntimeInputPixelRect bodyViewport{};
    bool airborneForeground = false;
    int factoryForegroundHostId = -1;
};

struct WorldObjectInstance;
class GameWorld;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(
    const WorldObjectInstance& object, const GameWorld& world, const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
);
