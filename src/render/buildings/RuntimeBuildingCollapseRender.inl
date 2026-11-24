const std::vector<RuntimePaletteColor>* runtimeBuildingCustomPaletteForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    const auto* art = findArtTypeForObject(object, context);
    if (!art) return nullptr;
    return concord::render::resolveRuntimeBuildingCustomPalette(art->palette,
        [&] { return runtimeUnitPaletteCandidates(context); },
        [&](const std::string& name) { return runtimeNamedPaletteForRender(context, cache, name); });
}

bool drawRuntimeBuildingCollapse(
    HDC dc, const RECT& viewport, const POINT& center,
    const WorldObjectInstance& object, const RuntimeBuildingCollapseState& collapse, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeMapRenderStats& stats, bool shadowPass
) {
    const auto* art = context.art ? context.art->findArtType(collapse.animId) : nullptr;
    const auto appearance = concord::render::runtimeBuildingCollapseAppearance(
        collapse, art && art->shadow, context.animationTick, context.fixedTickHz, shadowPass);
    using concord::render::RuntimeBuildingCollapseLayer;
    if (appearance.layer == RuntimeBuildingCollapseLayer::Hidden) return false;
    if (appearance.layer == RuntimeBuildingCollapseLayer::Shadow) {
        const auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(collapse.animId, appearance.frame, context, cache, true);
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
            collapse.animId, appearance.frame, *customPalette, 0, 0, appearance.splitShadow, false, RGB(255, 255, 255), 255, true)
        : drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context, camera, cache,
            collapse.animId, appearance.frame, 0, 0, appearance.splitShadow, false, false, RGB(255, 255, 255), nullptr, true);
    if (!drawn) {
        ++stats.buildingCollapseMissing; return false;
    }
    ++stats.buildingCollapseSprites; ++stats.visibleStructureSprites; ++stats.visibleWorldObjects;
    return true;
}
