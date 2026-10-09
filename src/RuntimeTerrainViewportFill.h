#pragma once
#include "RuntimeRendererEnvironment.h"
#include <cstdint>
#include <limits>

inline thread_local int runtimeTerrainViewportFillReferenceOverride = -1;
class RuntimeTerrainViewportFillReferenceScope {
public:
    explicit RuntimeTerrainViewportFillReferenceScope(bool reference) noexcept
        : previous_(runtimeTerrainViewportFillReferenceOverride) {
        runtimeTerrainViewportFillReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeTerrainViewportFillReferenceScope() noexcept {
        runtimeTerrainViewportFillReferenceOverride = previous_;
    }
    RuntimeTerrainViewportFillReferenceScope(const RuntimeTerrainViewportFillReferenceScope&) = delete;
    RuntimeTerrainViewportFillReferenceScope& operator=(const RuntimeTerrainViewportFillReferenceScope&) = delete;
private:
    int previous_;
};
inline bool runtimeTerrainViewportFillReferenceForRender() {
    if (runtimeTerrainViewportFillReferenceOverride >= 0)
        return runtimeTerrainViewportFillReferenceOverride != 0;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_TERRAIN_VIEWPORT_FILL_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
inline void runtimeCountTerrainViewportFill(std::uint64_t& counter) noexcept {
    if (counter != (std::numeric_limits<std::uint64_t>::max)()) ++counter;
}

// Contract-only per-draw witness; null on ordinary rendering. No pixel timers.
struct RuntimeTerrainViewportFillObservation {
    std::uint64_t fills = 0, omitted = 0, flushCalls = 0;
};
inline thread_local RuntimeTerrainViewportFillObservation* runtimeTerrainViewportFillObservation = nullptr;
class RuntimeTerrainViewportFillObservationScope {
public:
    explicit RuntimeTerrainViewportFillObservationScope(RuntimeTerrainViewportFillObservation& value) noexcept
        : previous_(runtimeTerrainViewportFillObservation) { runtimeTerrainViewportFillObservation = &value; }
    ~RuntimeTerrainViewportFillObservationScope() noexcept { runtimeTerrainViewportFillObservation = previous_; }
    RuntimeTerrainViewportFillObservationScope(const RuntimeTerrainViewportFillObservationScope&) = delete;
    RuntimeTerrainViewportFillObservationScope& operator=(const RuntimeTerrainViewportFillObservationScope&) = delete;
private:
    RuntimeTerrainViewportFillObservation* previous_;
};

// The caller immediately invokes the existing drawTerrainLayerToViewport. This
// proves its full-viewport memcpy branch, not the fallible/partial GDI branch.
// No DC, context, camera or layer mutation may occur between proof and copy.
inline bool runtimeTryOmitTerrainViewportFill(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    const RuntimeTerrainLayerCache& layer) noexcept {
    if (!context.surfaceTopDownOwned || !dc || !context.surfacePixels
        || context.surfaceWidth <= 0 || context.surfaceHeight <= 0
        || viewport.left < 0 || viewport.top < 0
        || viewport.right <= viewport.left || viewport.bottom <= viewport.top
        || viewport.right > context.surfaceWidth || viewport.bottom > context.surfaceHeight
        || !layer.built || layer.width <= 0 || layer.height <= 0
        || static_cast<std::uint64_t>(layer.width) * static_cast<std::uint64_t>(layer.height) > layer.pixels.size()
        || static_cast<std::uint64_t>(context.surfaceWidth) * 4U > (std::numeric_limits<LONG>::max)()
        || static_cast<std::uint64_t>(context.surfaceWidth) * static_cast<std::uint64_t>(context.surfaceHeight)
            > (std::numeric_limits<std::size_t>::max)() / sizeof(std::uint32_t)) return false;
    const std::int64_t startX = static_cast<std::int64_t>(viewport.left) + layer.originX;
    const std::int64_t startY = static_cast<std::int64_t>(viewport.top) + layer.originY;
    if (startX < (std::numeric_limits<int>::min)() || startX > (std::numeric_limits<int>::max)()
        || startY < (std::numeric_limits<int>::min)() || startY > (std::numeric_limits<int>::max)()) return false;
    const std::int64_t screenX = startX - camera.worldOffsetX;
    const std::int64_t screenY = startY - camera.worldOffsetY;
    if (screenX < (std::numeric_limits<int>::min)() || screenY < (std::numeric_limits<int>::min)()
        || screenX + layer.width > (std::numeric_limits<int>::max)()
        || screenY + layer.height > (std::numeric_limits<int>::max)()
        || screenX > viewport.left || screenY > viewport.top
        || screenX + layer.width < viewport.right || screenY + layer.height < viewport.bottom) return false;
    if (GetMapMode(dc) != MM_TEXT || GetGraphicsMode(dc) != GM_COMPATIBLE
        || GetLayout(dc) != 0 || SetICMMode(dc, ICM_QUERY) != ICM_OFF) return false;
    POINT window{}, origin{}; RECT clip{};
    if (!GetWindowOrgEx(dc, &window) || !GetViewportOrgEx(dc, &origin)
        || window.x != 0 || window.y != 0 || origin.x != 0 || origin.y != 0
        || GetClipBox(dc, &clip) != SIMPLEREGION
        || clip.left != 0 || clip.top != 0
        || clip.right != context.surfaceWidth || clip.bottom != context.surfaceHeight) return false;
    const HGDIOBJ bitmap = GetCurrentObject(dc, OBJ_BITMAP);
    DIBSECTION dib{};
    if (!bitmap || GetObjectW(bitmap, sizeof(dib), &dib) != sizeof(dib)
        || dib.dsBm.bmBits != context.surfacePixels || dib.dsBm.bmWidth != context.surfaceWidth
        || dib.dsBm.bmHeight != context.surfaceHeight || dib.dsBm.bmWidthBytes != context.surfaceWidth * 4
        || dib.dsBm.bmPlanes != 1 || dib.dsBm.bmBitsPixel != 32
        || dib.dsBmih.biSize != sizeof(BITMAPINFOHEADER) || dib.dsBmih.biWidth != context.surfaceWidth
        || (dib.dsBmih.biHeight != context.surfaceHeight && dib.dsBmih.biHeight != -context.surfaceHeight)
        || dib.dsBmih.biPlanes != 1 || dib.dsBmih.biBitCount != 32 || dib.dsBmih.biCompression != BI_RGB
        || dib.dsBmih.biClrUsed != 0 || dib.dshSection != nullptr || dib.dsOffset != 0) return false;
    // Creation direction comes ONLY from the caller's negative-height producer
    // token; GetObject's normalized sign cannot establish it. Synchronize all
    // previous GDI before the mandatory CPU row copies, including queued fills.
    if (runtimeTerrainViewportFillObservation) ++runtimeTerrainViewportFillObservation->flushCalls;
    return GdiFlush() != FALSE;
}
