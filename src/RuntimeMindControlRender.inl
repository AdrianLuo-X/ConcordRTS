// Mind control has a house-coloured selection arc and an attached status
// animation. Neither is an electric weapon; casting SHPs are transient actors.
POINT runtimeMindControlRingAnchor(
    const WorldObjectInstance& object,
    const RECT& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
) {
    POINT point = screenPoint(runtimeObjectDrawPoint(
        object, runtimeObjectFoundationDimensions(object, context), camera),
        viewport, camera);
    // Infantry SHPs have different canvas sizes and foot offsets. Attach the
    // ring above the rendered body, rather than above its logical cell centre.
    RECT bodyBounds{};
    bool hasBounds = false;
    if (object.kind == WorldObjectKind::Unit && objectUsesVoxelArt(object, context)) {
        if (const auto* voxel = loadRuntimeVoxelFromCache(object, context, cache)) {
            bodyBounds = runtimeShpFrameScreenRect(runtimeVoxelModelDrawCenter(point, camera),
                camera, voxel->image);
            hasBounds = runtimeRectIsValid(bodyBounds);
        }
    } else {
        hasBounds = resolveRuntimeObjectShpSpriteBounds(
            object, point, context, camera, cache, bodyBounds);
    }
    if (hasBounds) {
        point.y = bodyBounds.top - std::max(3, camera.tileHeight / 5);
    } else if (object.kind == WorldObjectKind::Structure) {
        const ArtType* art = findArtTypeForObject(object, context);
        point.y -= std::max(1, art == nullptr ? 1 : art->height)
            * camera.tileHeight / 2;
    } else {
        point.y -= camera.tileHeight * 4 / 5;
    }
    if (object.kind != WorldObjectKind::Structure) {
        const UnitType* unit = findObjectUnitTypeForRender(context, object);
        const int offset = unit == nullptr ? 140 : unit->mindControlRingOffset;
        point.y -= static_cast<LONG>(std::lround(
            (static_cast<double>(offset) - 140.0) * camera.tileHeight / 175.0));
    }
    return point;
}

int runtimeMindControlRingFrame(
    const SpecialMechanismRenderSnapshot& snapshot,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
) {
    const ArtType* art = context.art == nullptr ? nullptr
        : context.art->findArtType(snapshot.resourceId);
    const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(
        snapshot.resourceId, 0, context, cache, runtimeArtShadowEnabled(art, false));
    if (first == nullptr || first->frameCount <= 0) return 0;
    const int frames = runtimeVisibleMainFrameCount(
        first->frameCount, runtimeArtShadowEnabled(art, false));
    const int start = std::clamp(art == nullptr ? 0
        : (art->hasAnimationLoopStart ? art->animationLoopStart
            : (art->hasAnimationStart ? art->animationStart : 0)), 0, frames - 1);
    const int end = std::clamp(art == nullptr ? frames
        : (art->hasAnimationLoopEnd ? art->animationLoopEnd
            : (art->hasAnimationEnd ? start + std::max(1, art->animationEnd) : frames)),
        start + 1, frames);
    const int span = end - start;
    const int step = std::max(0, snapshot.progress) / runtimeAnimationFrameDelayTicks(art, context.fixedTickHz);
    if (art != nullptr && art->pingPong && span > 1) {
        const int phase = step % (2 * span - 2);
        return start + (phase < span ? phase : 2 * span - 2 - phase);
    }
    return start + step % span;
}

void drawRuntimeMindControlArc(
    HDC dc,
    const RECT& viewport,
    const RuntimeMapRenderContext& context,
    const POINT& from,
    const POINT& to,
    COLORREF color
) {
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    const double length = std::hypot(dx, dy);
    const double height = std::clamp(length * 0.21, 12.0, 96.0);
    const int segments = std::clamp(static_cast<int>(length / 8.0), 16, 64);
    std::vector<POINT> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / segments;
        points.push_back({
            static_cast<LONG>(std::lround(from.x + dx * t)),
            static_cast<LONG>(std::lround(from.y + dy * t - 4.0 * height * t * (1.0 - t)))
        });
    }
    if (!hasPixelSurface(context)) {
        if (dc == nullptr) return;
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        if (pen == nullptr) return;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        const HGDIOBJ previous = SelectObject(dc, pen);
        Polyline(dc, points.data(), static_cast<int>(points.size()));
        SelectObject(dc, previous);
        if (saved != 0) RestoreDC(dc, saved);
        DeleteObject(pen);
        return;
    }
    GdiFlush();
    const std::uint32_t pixel = packRuntimeDibColor(color);
    for (int i = 1; i <= segments; ++i) {
        int x = points[i - 1].x, y = points[i - 1].y;
        const int endX = points[i].x, endY = points[i].y;
        const int run = std::abs(endX - x), rise = -std::abs(endY - y);
        const int stepX = x < endX ? 1 : -1, stepY = y < endY ? 1 : -1;
        int error = run + rise;
        for (;;) {
            if (x >= viewport.left && x < viewport.right
                && y >= viewport.top && y < viewport.bottom
                && x >= 0 && x < context.surfaceWidth
                && y >= 0 && y < context.surfaceHeight) {
                context.surfacePixels[y * context.surfaceWidth + x] = pixel;
            }
            if (x == endX && y == endY) break;
            const int doubled = 2 * error;
            if (doubled >= rise) { error += rise; x += stepX; }
            if (doubled <= run) { error += run; y += stepY; }
        }
    }
}

void drawRuntimeMindControlStatus(
    HDC dc,
    const RECT& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RuntimeMapRenderStats& stats,
    const SpecialMechanismRenderSnapshot& snapshot,
    const WorldObjectInstance* source,
    const WorldObjectInstance* target
) {
    if (target == nullptr || !target->alive
        || runtimeSpecialObjectIsHidden(context, *target)
        || !runtimeVisionObjectHasCurrentVisibility(context, *target)) return;
    const POINT to = runtimeMindControlRingAnchor(*target, viewport, context, camera, cache);
    const auto selected = [&](int id) {
        return context.selectedObjectIds != nullptr
            && std::find(context.selectedObjectIds->begin(), context.selectedObjectIds->end(), id)
                != context.selectedObjectIds->end();
    };
    const bool showArc = source != nullptr && source->alive
        && !runtimeSpecialObjectIsHidden(context, *source)
        && runtimeVisionObjectHasCurrentVisibility(context, *source)
        && (snapshot.progress < snapshot.requiredProgress
            || selected(snapshot.objectId) || selected(snapshot.targetObjectId));
    if (showArc) {
        const POINT from = runtimeMindControlRingAnchor(*source, viewport, context, camera, cache);
        drawRuntimeMindControlArc(dc, viewport, context, from, to,
            runtimeOwnerRemapColor(context, source->owner));
    }
    const int frame = runtimeMindControlRingFrame(snapshot, context, cache);
    const ArtType* art = context.art == nullptr ? nullptr
        : context.art->findArtType(snapshot.resourceId);
    if (!snapshot.resourceId.empty() && !equalAsciiNoCase(snapshot.resourceId, "none")) {
        (void)drawRuntimeClassifiedAuxiliaryEffect(
            dc, viewport, to, context, camera, cache, snapshot.resourceId, frame,
            0, 0, runtimeArtShadowEnabled(art, false), RuntimeAuxiliaryEffectKind::Impact,
            true, true);
    }
    recordRuntimeSpecialMechanismRenderIntent(stats, snapshot, frame * 2 + (showArc ? 1 : 0));
}
