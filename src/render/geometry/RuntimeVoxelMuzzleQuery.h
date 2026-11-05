#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;
struct RuntimeMapRenderCache;

// Existing voxel muzzle in signed screen pixels. A failed query leaves point
// unchanged. Body center may alias point; the original producer owns its copy.
bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& bodyCenter, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, RuntimeInputPixelPoint& point, bool useBarrelEndpoint = true);
