#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct RuntimeProjectileRenderActor;
struct RuntimeMapCamera;

// Original synchronous projectile screen projection. Existing position, source
// offsets and arc formulas retain their owners and arithmetic domain.
RuntimeInputPixelPoint runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& actor,
    const RuntimeInputPixelRect& viewport, const RuntimeMapCamera& camera);
