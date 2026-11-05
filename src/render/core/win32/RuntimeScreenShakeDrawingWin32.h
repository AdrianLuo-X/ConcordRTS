#pragma once
// Private host assembly only; keep the original Native algorithm/owner.
#include "render/core/win32/RuntimeMapDrawingWin32.h"
static_assert(sizeof(LONG) == sizeof(std::int32_t));
class RuntimeScreenShakeDrawTargetWin32 final : public RuntimeScreenShakeDrawTarget {
public:
    explicit RuntimeScreenShakeDrawTargetWin32(HDC dc) noexcept : dc_(dc) {}
    RuntimeMapDrawingCapability capability() const noexcept override {
        return RuntimeMapDrawingCapability::Available;
    }
    bool apply(const RuntimeInputPixelRect& viewport,
        const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) override {
        const RECT native{viewport.left, viewport.top, viewport.right, viewport.bottom};
        return ::applyRuntimeScreenShake(dc_, native, context, cache);
    }
private:
    HDC dc_ = nullptr; // Same current UI paint/capture DC borrow; never acquired/released.
};
inline bool applyRuntimeScreenShakeWin32(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) {
    RuntimeScreenShakeDrawTargetWin32 target(dc);
    const RuntimeInputPixelRect pixels{viewport.left, viewport.top, viewport.right, viewport.bottom};
    return ::applyRuntimeScreenShake(target, pixels, context, cache);
}
