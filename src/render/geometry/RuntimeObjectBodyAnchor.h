#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

struct WorldObjectInstance;
struct RuntimeMapRenderContext;
struct RuntimeMapCamera;

// Original synchronous body projection. Existing snapshot/lift/formula owners
// and their signed arithmetic domain remain unchanged. Return pixels are owned.
RuntimeInputPixelPoint runtimeObjectBodyAnchorForRender(
    const WorldObjectInstance& object,
    const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera
);
