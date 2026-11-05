#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing structure muzzle in signed screen pixels. A failed query leaves
// result unchanged; center and result may refer to the same point.
bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, RuntimeInputPixelPoint& result);
