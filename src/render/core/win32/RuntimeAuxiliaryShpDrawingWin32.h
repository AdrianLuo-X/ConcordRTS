#pragma once
// Private implementation assembly only. Reuse the original Native declaration;
// the one SHP loading/classification/raster algorithm stays in the renderer.
#include "render/core/win32/RuntimeMapDrawingWin32.h"
static_assert(sizeof(int) == sizeof(std::int32_t));
class RuntimeAuxiliaryShpDrawTargetWin32 final : public RuntimeAuxiliaryShpDrawTarget {
public:
    explicit RuntimeAuxiliaryShpDrawTargetWin32(HDC dc) noexcept : dc_(dc) {}
    RuntimeMapDrawingCapability capability() const noexcept override {
        return RuntimeMapDrawingCapability::Available;
    }
    bool drawFrame(const RuntimeInputPixelRect& viewport,
        const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
        const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
        const std::string& imageId, int frameIndex, std::int32_t offsetX,
        std::int32_t offsetY, bool mainFramesOnly) override {
        const RECT nativeViewport{viewport.left, viewport.top, viewport.right, viewport.bottom};
        const POINT nativeCenter{center.x, center.y};
        return ::drawRuntimeAuxiliaryShpFrame(dc_, nativeViewport, nativeCenter, context,
            camera, cache, imageId, frameIndex, static_cast<int>(offsetX),
            static_cast<int>(offsetY), mainFramesOnly);
    }
private:
    HDC dc_ = nullptr; // Borrow only; never selects/releases a device object.
};
// Original native host assembly. The original exact viewport/point/defaults
// cross the standard port; no DC or cache lifetime is extended by this scope.
inline bool drawRuntimeAuxiliaryShpFrameWin32(HDC dc, const RECT& viewport,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    const std::string& imageId, int frameIndex, int offsetX = 0, int offsetY = 0,
    bool mainFramesOnly = false) {
    RuntimeAuxiliaryShpDrawTargetWin32 target(dc);
    const RuntimeInputPixelRect pixels{viewport.left, viewport.top, viewport.right, viewport.bottom};
    const RuntimeInputPixelPoint anchor{center.x, center.y};
    return ::drawRuntimeAuxiliaryShpFrame(target, pixels, anchor, context, camera,
        cache, imageId, frameIndex, offsetX, offsetY, mainFramesOnly);
}
