// Retail factories split the opened building into UnderRoofDoorAnim (rear /
// interior) and RoofDeployingAnim (front wall / moving roof). The emerging
// aircraft belongs between those layers, not above a rectangular roof cut.
bool drawRuntimeFactoryAirExitForeground(HDC dc, const RECT& viewport,
    const WorldObjectInstance& host, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (!concord::render::runtimeFactoryAirExitForegroundActive(host.alive, host.factoryExitAirborne,
        host.factoryExitAnimationStartTick, host.factoryExitAnimationEndTick, context.animationTick)) return false;
    const auto* art = findArtTypeForObject(host, context);
    if (!art) return false;
    RuntimeBuildingDepthObjectScope buildingDepthObject(host, center, context);
    const auto* roof = concord::render::selectRuntimeFactoryAirExitForegroundLayer(
        art->auxiliaryAnimations, [](const std::string& key) { return toLowerAscii(key); });
    if (!roof) return false;
    const auto& layer = *roof;
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

using concord::render::RuntimeFactoryAirExitOcclusionPixels;

RuntimeFactoryAirExitOcclusionPixels captureRuntimeFactoryAirExitForegroundPixels(
    const RECT& clip, const WorldObjectInstance& host, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (!hasPixelSurface(context)) return {};
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
    return concord::render::collectRuntimeFactoryAirExitForegroundPixels(
        runtimeRenderPlacementPixelRect(clip), context.surfaceWidth, context.surfaceHeight,
        std::span<const std::uint32_t>{context.surfacePixels,
            static_cast<std::size_t>(context.surfaceWidth) * context.surfaceHeight},
        spans);
}
