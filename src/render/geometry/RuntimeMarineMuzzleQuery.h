#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing marine muzzle in signed screen pixels. A failed query leaves
// result unchanged; center and result may refer to the same point.
bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result);
