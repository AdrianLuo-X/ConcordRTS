void drawRuntimeBehindMarkers(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats,
    const RuntimeBehindMarkerFrameState& frame) {
    if (!context.seeHiddenObjects || context.effectOnlyProbe) return;
    static const bool trace = std::getenv("CONCORD_BEHIND_MARKER_TRACE") != nullptr;
    if (trace) for (const auto& unit : frame.candidates)
        std::cout << "Behind silhouette: object=" << unit.objectId << " samples=" << unit.sampleCount
            << " coveredMask=" << unit.covered << '\n';
    if (std::none_of(frame.candidates.begin(), frame.candidates.end(),
            runtimeBehindMarkerCandidateCovered)) return;
    std::string imageId = context.rules ? context.rules->getAudioVisualValue("Behind") : "BEHIND";
    if (imageId.empty() || equalAsciiNoCase(imageId, "none") || equalAsciiNoCase(imageId, "(none)")) return;
    auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(imageId, 0, context, cache);
    const auto* palette = runtimeAnimPaletteForRender(context, cache);
    if (!first || !palette) { ++stats.behindMarkerMissing; return; }
    const auto* art = context.art ? context.art->findArtType(imageId) : nullptr;
    const int animationFrame = selectRuntimeAuxiliaryAnimationFrame(context, art, first->frameCount, false);
    for (const auto& unit : frame.candidates) {
        if (!runtimeBehindMarkerCandidateCovered(unit)) continue;
        if (drawRuntimeAuxiliaryShpSpriteWithPalette(dc, viewport, unit.center,
                context, camera, cache, imageId, animationFrame, *palette,
                0, 0, false, false, RGB(255, 255, 255), 255, true)) {
            ++stats.behindMarkerSprites;
            stats.behindMarkerFrame = animationFrame;
        } else ++stats.behindMarkerMissing;
    }
}
