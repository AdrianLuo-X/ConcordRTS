const std::vector<RuntimePaletteColor>* runtimeBuildingCustomPaletteForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    const auto* art = findArtTypeForObject(object, context);
    if (!art || art->palette.empty()) return nullptr;
    // Original building Palette is a prefix (Palette=lib -> libtem.pal).
    if (std::filesystem::path(art->palette).extension().empty()) {
        const auto candidates = runtimeUnitPaletteCandidates(context);
        const auto& ordinary = candidates.back();
        if (const auto* palette = runtimeNamedPaletteForRender(context, cache, art->palette + ordinary.substr(4)))
            return palette;
    }
    return runtimeNamedPaletteForRender(context, cache, art->palette);
}

bool drawRuntimeBuildingCollapse(
    HDC dc, const RECT& viewport, const POINT& center,
    const WorldObjectInstance& object, const RuntimeBuildingCollapseState& collapse, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeMapRenderStats& stats, bool shadowPass
) {
    const auto* art = context.art ? context.art->findArtType(collapse.animId) : nullptr;
    const bool splitShadow = art && art->shadow;
    const int frame = runtimeBuildingCollapseFrame(collapse, context.animationTick, context.fixedTickHz);
    if (shadowPass) {
        if (!splitShadow) return false;
        const auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(collapse.animId, frame, context, cache, true);
        if (!sprite || !sprite->hasShadowFrame) return false;
        const POINT anchor = runtimeAuxiliaryShpAdjustedCenter(center, camera, sprite->shadowImage, 0, 0, true);
        int pixels = 0;
        if (!drawRuntimeShpShadowFrame(dc, viewport, anchor, context, camera, sprite->shadowImage, &pixels)) return false;
        ++stats.shpShadowMasksResolved;
        if (pixels > 0) { ++stats.shadowsDrawn; ++stats.shpShadowsDrawn; stats.shadowPixelsDarkened += pixels; }
        return true;
    }
    // Both the building and DestroyAnim use the SHP's full canvas at the foundation origin.
    const auto* customPalette = runtimeBuildingCustomPaletteForRender(object, context, cache);
    const bool drawn = customPalette
        ? drawRuntimeAuxiliaryShpSpriteWithPalette(dc, viewport, center, context, camera, cache,
            collapse.animId, frame, *customPalette, 0, 0, splitShadow, false, RGB(255, 255, 255), 255, true)
        : drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context, camera, cache,
            collapse.animId, frame, 0, 0, splitShadow, false, false, RGB(255, 255, 255), nullptr, true);
    if (!drawn) {
        ++stats.buildingCollapseMissing; return false;
    }
    ++stats.buildingCollapseSprites; ++stats.visibleStructureSprites; ++stats.visibleWorldObjects;
    return true;
}
