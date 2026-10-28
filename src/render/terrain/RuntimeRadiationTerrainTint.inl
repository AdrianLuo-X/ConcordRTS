// Radiation changes the illumination of the visible terrain pixels. Keeping
// each pixel's original intensity preserves snow, rock and cliff detail.
COLORREF runtimeRadiationTerrainLight(COLORREF original, COLORREF tint, int alpha,
    double tintFactor = 1.0, double lightFactor = 0.1) {
    tintFactor = std::clamp(tintFactor, 0.0, 4.0);
    lightFactor = std::clamp(lightFactor, 0.0, 4.0);
    const double strength = std::clamp(alpha / 128.0, 0.0, 2.0)
        / std::max(0.001, std::max(tintFactor, lightFactor));
    const auto channel = [strength, tintFactor, lightFactor](int value, int light) {
        return std::clamp(static_cast<int>(std::lround(value
            * (1.0 + strength * (tintFactor * (0.95 * light / 255.0 - 0.35)
                + lightFactor * light / 255.0)))), 0, 255);
    };
    return RGB(channel(GetRValue(original), GetRValue(tint)),
        channel(GetGValue(original), GetGValue(tint)),
        channel(GetBValue(original), GetBValue(tint)));
}

#include "render/core/win32/RuntimeMapGpuRadiationWin32.inl"

bool tintRuntimeRadiationTerrainPixel(HDC dc, const RuntimeMapRenderContext& context,
    int x, int y, COLORREF tint, int alpha) {
    if(runtimeGpuWorldTarget(context))return runtimeGpuCollectRadiationPixel(context,x,y,tint,alpha);
    const auto* rules = context.rules == nullptr ? nullptr : &context.rules->getSpecialMechanismRules();
    const auto lightPixel = [&](COLORREF original) {
        return runtimeRadiationTerrainLight(original, tint, alpha,
            rules == nullptr ? 1.0 : rules->radiationTintFactor,
            rules == nullptr ? 0.1 : rules->radiationLightFactor);
    };
    if (hasPixelSurface(context)) {
        if (x < 0 || y < 0 || x >= context.surfaceWidth || y >= context.surfaceHeight) return false;
        auto& pixel = context.surfacePixels[static_cast<std::size_t>(y) * context.surfaceWidth + x];
        const COLORREF original = RGB((pixel >> 16U) & 255U, (pixel >> 8U) & 255U, pixel & 255U);
        pixel = (pixel & 0xff000000U) | packDibColor(lightPixel(original));
        return true;
    }
    if (dc == nullptr) return false;
    const COLORREF original = concord::ui::win32::MapNativeOverlayWin32::readPixel(dc, x, y);
    return original != CLR_INVALID
        && SetPixelV(dc, x, y, lightPixel(original)) != FALSE;
}

bool drawRuntimeRadiationOnVisibleTerrain(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const SpecialMechanismRenderSnapshot& snapshot,
    COLORREF color, double strength, int radius, std::uint64_t& tintedPixels,
    int& tintedCells) {
    const auto& layer = cache.terrainLayer;
    if (!layer.built || layer.width <= 0 || layer.height <= 0
        || layer.terrainCellIndices.size() != layer.pixels.size()) {
        if(runtimeGpuWorldTarget(context)){runtimeGpuWorldOwner->reject("world-radiation-owner-cache");return true;}
        return false;
    }
    RuntimeGpuRadiationScope gpuRadiation(viewport,context,color);
    // This ownership buffer comes from the same TMP draw that produced the
    // terrain: ramps, extra cliff pixels and overlapping high ground all keep
    // their real silhouettes. A flat diamond cannot describe those surfaces.
    const int originX = viewport.left + layer.originX - camera.worldOffsetX;
    const int originY = viewport.top + layer.originY - camera.worldOffsetY;
    const int left = std::max(static_cast<int>(viewport.left), originX);
    const int top = std::max(static_cast<int>(viewport.top), originY);
    const int right = std::min(static_cast<int>(viewport.right), originX + layer.width);
    const int bottom = std::min(static_cast<int>(viewport.bottom), originY + layer.height);
    struct Receiver { bool active = false; POINT center{}; double dx = 0, dy = 0; bool tinted = false; };
    std::unordered_map<int, Receiver> receivers;
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const int index = layer.terrainCellIndices[static_cast<std::size_t>(y - originY)
            * layer.width + (x - originX)];
        if (index < 0) continue;
        auto [entry, inserted] = receivers.try_emplace(index);
        auto& receiver = entry->second;
        if (inserted && static_cast<std::size_t>(index) < cache.terrainByCellIndex.size()) {
            const auto* cell = cache.terrainByCellIndex[index];
            if (cell != nullptr) {
                receiver.dx = cell->x - snapshot.cell.x;
                receiver.dy = cell->y - snapshot.cell.y;
                receiver.active = std::hypot(receiver.dx, receiver.dy) <= radius + 1.0
                    && runtimeVisionAt(context, cell->x, cell->y) != RuntimeCellVisibility::Shrouded;
                receiver.center = screenPoint(projectCell(*cell, camera), viewport, camera);
            }
        }
        if (!receiver.active) continue;
        const double localX = static_cast<double>(x - receiver.center.x) / camera.tileWidth;
        const double localY = static_cast<double>(y - receiver.center.y) / camera.tileHeight;
        // The vertical cliff face belongs to its own ground cell, not to the
        // distant flat cell that a screen-space inverse projection would hit.
        const double dx = receiver.dx + std::clamp(localY + localX, -0.5, 0.5);
        const double dy = receiver.dy + std::clamp(localY - localX, -0.5, 0.5);
        const double falloff = radius == 0
            ? (receiver.dx == 0 && receiver.dy == 0 ? 1.0 : 0.0)
            : runtimeRadiationFalloff(dx, dy, radius);
        const int alpha = std::clamp(static_cast<int>(std::lround(128.0 * strength * falloff)), 0, 255);
        if (alpha > 0 && tintRuntimeRadiationTerrainPixel(dc, context, x, y, color, alpha)) {
            ++tintedPixels;
            if (!receiver.tinted) { receiver.tinted = true; ++tintedCells; }
        }
    }
    return true;
}
