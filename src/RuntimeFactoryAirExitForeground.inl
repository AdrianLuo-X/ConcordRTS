// Retail factories split the opened building into UnderRoofDoorAnim (rear /
// interior) and RoofDeployingAnim (front wall / moving roof). The emerging
// aircraft belongs between those layers, not above a rectangular roof cut.
bool drawRuntimeFactoryAirExitForeground(HDC dc, const RECT& viewport,
    const WorldObjectInstance& host, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (!host.alive || !host.factoryExitAirborne
        || host.factoryExitAnimationStartTick < 0
        || context.animationTick >= host.factoryExitAnimationEndTick) return false;
    const auto* art = findArtTypeForObject(host, context);
    if (!art) return false;
    RuntimeBuildingDepthObjectScope buildingDepthObject(host, center, context);
    for (const auto& layer : art->auxiliaryAnimations) {
        if (toLowerAscii(layer.key) != "roofdeployinganim") continue;
        const std::string& image = runtimeObjectIsDamaged(host, context) && !layer.damagedImage.empty()
            ? layer.damagedImage : layer.image;
        const auto* animation = context.art ? context.art->findArtType(image) : nullptr;
        const bool shadow = runtimeArtShadowEnabled(animation, false);
        const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(image, 0, context, cache, shadow);
        if (!first) return false;
        const int frame = runtimeFactoryExitFrame(animation,
            shadow ? first->frameCount / 2 : first->frameCount,
            host.factoryExitAnimationStartTick, host.factoryExitAnimationEndTick,
            context.animationTick, context.fixedTickHz);
        RuntimeBuildingDepthPartScope depthLayer(animation && animation->flat
            ? RuntimeBuildingDepthLayer::Flat : RuntimeBuildingDepthLayer::Animation,
            layer.zAdjust, animation ? animation->yDrawOffset : 0,
            runtimeBuildingAnimationSlotYSort(layer.offsetX, layer.offsetY, layer.ySort), true);
        return drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context, camera, cache,
            image, frame, layer.offsetX, layer.offsetY + (animation ? animation->yDrawOffset : 0), false, false,
            runtimeStructureLayerShouldOwnerRemap(host, context, image),
            runtimeOwnerRemapColor(context, host.owner), nullptr, true);
    }
    return false;
}

using RuntimeFactoryAirExitOcclusionPixels = std::vector<std::pair<std::size_t, std::uint32_t>>;

RuntimeFactoryAirExitOcclusionPixels captureRuntimeFactoryAirExitForegroundPixels(
    const RECT& clip, const WorldObjectInstance& host, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    RuntimeFactoryAirExitOcclusionPixels pixels;
    if (!hasPixelSurface(context)) return pixels;
    std::vector<RuntimeSpriteHitSpan> spans;
    RuntimeSpriteGeometryCapture capture;
    capture.hitSpans = &spans;
    {
        RuntimeSpriteGeometryScope scope(&capture);
        (void)drawRuntimeFactoryAirExitForeground(nullptr, clip, host, center, context, camera, cache);
    }
    // Preserve the already composited wall, including its damage fires and
    // lighting, instead of repainting over those details with a bare SHP.
    GdiFlush();
    for (const auto& span : spans) {
        if (span.y < std::max<LONG>(0, clip.top)
            || span.y >= std::min<LONG>(context.surfaceHeight, clip.bottom)) continue;
        for (LONG x = std::max<LONG>(0, std::max(span.left, clip.left));
                x < std::min<LONG>(context.surfaceWidth, std::min(span.right, clip.right)); ++x) {
            const auto index = static_cast<std::size_t>(span.y) * context.surfaceWidth + x;
            pixels.emplace_back(index, context.surfacePixels[index]);
        }
    }
    return pixels;
}
