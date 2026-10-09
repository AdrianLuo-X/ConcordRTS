namespace {

bool drawRuntimeCamouflageBody(HDC dc, const RECT& viewport, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const SpecialMechanismRenderSnapshot& disguise) {
    const ArtType* treeArt = context.art == nullptr ? nullptr
        : context.art->findArtType(disguise.resourceId);
    const bool mainFrames = runtimeArtShadowEnabled(treeArt, true);
    return drawRuntimeAuxiliaryTmpSprite(dc, viewport, center, context, camera,
            cache, disguise.resourceId, 0)
        || drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context, camera,
            cache, disguise.resourceId, 0, 0, 0, mainFrames, false, false,
            RGB(255,255,255), nullptr, true);
}

WorldObjectInstance runtimeSpyBodyProjection(const WorldObjectInstance& object,
    const SpecialMechanismRenderSnapshot& disguise) {
    WorldObjectInstance projected = object;
    projected.type = disguise.resourceId;
    projected.owner = disguise.owner;
    projected.kind = disguise.phase == "Vehicle"
        ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
    projected.authoredAircraft = false;
    projected.airborne = false;
    projected.submerged = false;
    projected.cloaked = false;
    return projected;
}

bool drawRuntimeSpyDisguiseBody(HDC dc, const RECT& viewport,
    const WorldObjectInstance& projected, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const SpecialMechanismRenderSnapshot& disguise) {
    if (disguise.phase == "Terrain") {
        return drawRuntimeAuxiliaryTmpSprite(dc, viewport, center, context, camera,
                cache, disguise.resourceId, 0)
            || drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context, camera,
                cache, disguise.resourceId, 0);
    }
    return (projected.kind == WorldObjectKind::Unit
            && tryDrawVoxelSprite(dc, viewport, projected, center, context, camera, cache))
        || tryDrawObjectShpSprite(dc, viewport, projected, center, context, camera, cache);
}

bool drawRuntimeMobileParachute(HDC dc, const RECT& viewport,
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (object.kind != WorldObjectKind::Infantry || !object.airborne
        || object.mission != "ParadropDrop" || context.rules == nullptr) return false;
    const auto* rule = context.rules->findSuperWeaponType("ParaDropSpecial");
    if (rule == nullptr) rule = context.rules->findSuperWeaponType("AmericanParaDropSpecial");
    const auto* art = rule && context.art ? context.art->findArtType(rule->parachuteAnim) : nullptr;
    if (art == nullptr) return false;
    const int count = runtimeAuxiliaryShpFrameCount(context, cache, rule->parachuteAnim);
    const double gameFrames = object.movementAnimationDistance * 256.0
        / 2.449489742783178 / std::max(1, rule->parachuteFallRate);
    const int step = static_cast<int>(gameFrames * std::max(1, art->rate) / 256.0);
    const int loopStart = std::clamp(art->animationLoopStart, 0, std::max(0,count-1));
    const int loopEnd = std::clamp(art->animationLoopEnd, loopStart, std::max(loopStart,count-1));
    const int frame = step < loopStart ? step
        : loopStart + (step-loopStart) % std::max(1,loopEnd-loopStart+1);
    int attachY = 0;
    const auto* infantrySprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
    const auto* chuteSprite = loadRuntimeAuxiliaryShpSpriteFromCache(rule->parachuteAnim,
        frame, context, cache, true);
    if (infantrySprite && chuteSprite) {
        const auto infantryRect = runtimeShpFrameScreenRect(center, camera, infantrySprite->image);
        const auto chuteCenter = runtimeAuxiliaryShpAdjustedCenter(center, camera,
            chuteSprite->image, 0, 0, true);
        const auto chuteRect = runtimeShpFrameScreenRect(chuteCenter, camera, chuteSprite->image);
        attachY = infantryRect.top - chuteRect.bottom + 1;
    }
    return count > 0 && drawRuntimeAuxiliaryShpSprite(dc, viewport, center, context,
        camera, cache, rule->parachuteAnim, frame, 0, attachY, true, false,
        true, runtimeOwnerRemapColor(context, object.owner), nullptr, true);
}

bool drawRuntimeMobileGeometryBody(HDC dc, const RECT& viewport,
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (object.kind == WorldObjectKind::Structure) return false;
    if (const auto* disguise = runtimeCamouflageSnapshot(context, object))
        return drawRuntimeCamouflageBody(dc, viewport, center, context, camera, cache, *disguise);
    if (const auto* disguise = runtimeSpyDisguiseSnapshot(context, object.instanceId)) {
        const auto projected = runtimeSpyBodyProjection(object, *disguise);
        if (drawRuntimeSpyDisguiseBody(dc, viewport, projected, center,
                context, camera, cache, *disguise)) return true;
    }
    const bool drawn = object.kind == WorldObjectKind::Unit && objectUsesVoxelArt(object, context)
        ? tryDrawVoxelSprite(dc, viewport, object, center, context, camera, cache)
        : tryDrawObjectShpSprite(dc, viewport, object, center, context, camera, cache);
    drawRuntimeMobileParachute(dc, viewport, object, center, context, camera, cache);
    return drawn;
}

bool captureRuntimeMobileSpriteGeometry(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeSpriteGeometryCapture& geometry) {
    RuntimeSpriteGeometryScope scope(&geometry);
    // Capture is before clipping in each low-level draw; no surface is touched.
    const LONG low = std::numeric_limits<LONG>::lowest() / 2;
    const LONG high = std::numeric_limits<LONG>::max() / 2;
    const RECT viewport{low, low, high, high};
    return drawRuntimeMobileGeometryBody(nullptr, viewport, object, center, context, camera, cache);
}

} // namespace

bool resolveRuntimeMobileObjectSpriteBounds(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RECT& spriteRect) {
    bool memoResolved = false;
    if (findRuntimeGeometryMobileBounds(object, center, context, camera, cache, spriteRect, memoResolved))
        return memoResolved;
    RuntimeSpriteGeometryCapture geometry;
    const bool resolved = captureRuntimeMobileSpriteGeometry(object, center, context, camera, cache, geometry);
    spriteRect = geometry.bounds;
    rememberRuntimeGeometryMobileBounds(object, center, context, camera, cache,
        spriteRect, resolved && geometry.valid);
    return resolved && geometry.valid;
}

bool verifyRuntimeMobileObjectSpriteBounds(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeMobileSpriteBoundsProbe& probe) {
    probe = {};
    RuntimeSpriteGeometryCapture geometry;
    probe.resourceResolved = captureRuntimeMobileSpriteGeometry(object, center, context, camera, cache, geometry);
    probe.spriteBounds = geometry.bounds;
    probe.layerCount = geometry.layerCount;
    if (!probe.resourceResolved || !geometry.valid) {
        probe.diagnostic = object.type + ": body resource missing or no visible pixels";
        return false;
    }
    constexpr LONG padding = 32;
    probe.imageOrigin = {geometry.canvasBounds.left - padding, geometry.canvasBounds.top - padding};
    probe.imageWidth = static_cast<int>(geometry.canvasBounds.right - probe.imageOrigin.x + padding);
    probe.imageHeight = static_cast<int>(geometry.canvasBounds.bottom - probe.imageOrigin.y + padding);
    if (probe.imageWidth <= 0 || probe.imageHeight <= 0
        || probe.imageWidth > 4096 || probe.imageHeight > 4096) {
        probe.clipped = true;
        probe.diagnostic = object.type + ": invalid or excessive body canvas";
        return false;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = probe.imageWidth;
    info.bmiHeader.biHeight = -probe.imageHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        probe.diagnostic = object.type + ": failed to allocate pixel probe";
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    const std::size_t count = static_cast<std::size_t>(probe.imageWidth) * probe.imageHeight;
    std::vector<unsigned char> touched(count, 0);
    RuntimeMapRenderContext drawContext = context;
    drawContext.surfacePixels = pixels;
    drawContext.surfaceWidth = probe.imageWidth;
    drawContext.surfaceHeight = probe.imageHeight;
    const RECT viewport{0, 0, probe.imageWidth, probe.imageHeight};
    const POINT drawCenter{center.x - probe.imageOrigin.x, center.y - probe.imageOrigin.y};
    RuntimeSpriteGeometryScope realDrawScope(nullptr);
    bool allDrawn = true;
    for (std::uint32_t background : {0x00ffffffU, 0x00000000U}) {
        std::fill(pixels, pixels + count, background);
        allDrawn = drawRuntimeMobileGeometryBody(dc, viewport, object, drawCenter,
            drawContext, camera, cache) && allDrawn;
        GdiFlush();
        for (std::size_t index = 0; index < count; ++index)
            if ((pixels[index] & 0x00ffffffU) != background) touched[index] = 1;
    }
    probe.pixels.assign(pixels, pixels + count);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    bool rendered = false;
    for (int y = 0; y < probe.imageHeight; ++y) for (int x = 0; x < probe.imageWidth; ++x) {
        if (!touched[static_cast<std::size_t>(y) * probe.imageWidth + x]) continue;
        const POINT pixel{probe.imageOrigin.x + x, probe.imageOrigin.y + y};
        RuntimeSpriteGeometryCapture::unite(probe.renderedBounds, rendered,
            RECT{pixel.x, pixel.y, pixel.x + 1, pixel.y + 1});
        ++probe.renderedPixels;
        if (!PtInRect(&probe.spriteBounds, pixel)) ++probe.outsidePixels;
        if (x == 0 || y == 0 || x + 1 == probe.imageWidth || y + 1 == probe.imageHeight)
            probe.clipped = true;
    }
    probe.exactMatch = rendered && EqualRect(&probe.spriteBounds, &probe.renderedBounds);
    const bool passed = allDrawn && rendered && probe.exactMatch
        && probe.outsidePixels == 0 && !probe.clipped;
    probe.diagnostic = object.type + ": layers=" + std::to_string(probe.layerCount)
        + " pixels=" + std::to_string(probe.renderedPixels)
        + " outside=" + std::to_string(probe.outsidePixels)
        + " exact=" + std::to_string(probe.exactMatch)
        + " clipped=" + std::to_string(probe.clipped);
    return passed;
}
