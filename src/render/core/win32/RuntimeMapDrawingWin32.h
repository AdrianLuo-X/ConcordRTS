#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"

// Legacy device entry points are private to the existing Win32 implementation
// and its independent native fixtures. Their algorithms and ABI stay unchanged.
// Preview/auxiliary/shake only move declarations here as a coupled prerequisite
// for the standard header closure; their W02002/03/04 ports remain required.
RuntimeMapRenderStats drawRuntimeMap(
    HDC dc,
    const RECT& viewport,
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
);

bool drawRuntimeObjectPreviewSprite(
    HDC dc,
    const RECT& viewport,
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RECT* spriteRect = nullptr
);

bool drawRuntimeAuxiliaryShpFrame(
    HDC dc,
    const RECT& viewport,
    const POINT& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    int frameIndex,
    int offsetX = 0,
    int offsetY = 0,
    bool mainFramesOnly = false
);

bool applyRuntimeScreenShake(HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);

// UI-thread DC borrow only. No device/resource ownership, queue, second raster
// algorithm or Presenter. The original map implementation still chooses the
// existing software/GDI/GPU path and whole-frame failure fallback.
class RuntimeMapDrawTargetWin32 final : public RuntimeMapDrawTarget {
public:
    explicit RuntimeMapDrawTargetWin32(HDC dc) noexcept : dc_(dc) {}
    RuntimeMapDrawingCapability capability() const noexcept override {
        return RuntimeMapDrawingCapability::Available;
    }
    RuntimeMapRenderStats drawMap(const RuntimeInputPixelRect& viewport,
        const GameWorld& world, const RuntimeMapRenderContext& context,
        RuntimeMapCamera& camera, RuntimeMapRenderCache& cache) override {
        const RECT native{viewport.left,viewport.top,viewport.right,viewport.bottom};
        return ::drawRuntimeMap(dc_,native,world,context,camera,cache);
    }
private:
    HDC dc_ = nullptr;
};
// Private native assembly for the real window/export consumers. Every call
// crosses the standard port with the same viewport and exact borrowed objects.
inline RuntimeMapRenderStats drawRuntimeMapWin32(HDC dc, const RECT& viewport,
    const GameWorld& world, const RuntimeMapRenderContext& context,
    RuntimeMapCamera& camera, RuntimeMapRenderCache& cache) {
    RuntimeMapDrawTargetWin32 target(dc);
    const RuntimeInputPixelRect pixels{viewport.left,viewport.top,viewport.right,viewport.bottom};
    return ::drawRuntimeMap(target,pixels,world,context,camera,cache);
}
