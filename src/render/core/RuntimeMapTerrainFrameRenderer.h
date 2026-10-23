#pragma once

#include "render/core/RuntimeMapTerrainFrame.h"

struct RuntimeMapRenderContext;

// Legacy Renderer assembly only. The named const frame, including its owned
// tint, must outlive the returned context and every map/interaction draw.
// Keep this adapter separate from the platform-free preparation contract.
RuntimeMapRenderContext runtimeMapRenderContextForTerrainFrame(
    const RuntimeMapTerrainFrame& frame);
RuntimeMapRenderContext runtimeMapRenderContextForTerrainFrame(
    RuntimeMapTerrainFrame&&) = delete;
RuntimeMapRenderContext runtimeMapRenderContextForTerrainFrame(
    const RuntimeMapTerrainFrame&&) = delete;
