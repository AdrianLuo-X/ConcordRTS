bool drawRuntimeTemporalBeam(
    HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context,
    const POINT& from, const POINT& to, COLORREF color, double scale
) {
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 1.0) return false;
    scale = std::clamp(scale, 0.25, 4.0);
    if (!hasPixelSurface(context)&&!runtimeGpuWorldTarget(context)) {
        if (dc == nullptr) return false;
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
        for (int layer = 2; layer >= 0; --layer) {
            const COLORREF shade = RGB(GetRValue(color) / (layer + 1),
                GetGValue(color) / (layer + 1), GetBValue(color) / (layer + 1));
            HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>((layer * 2 + 1) * scale)), shade);
            if (pen == nullptr) continue;
            const auto previous = SelectObject(dc, pen);
            MoveToEx(dc, from.x, from.y, nullptr);
            LineTo(dc, to.x, to.y);
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
        if (saved != 0) RestoreDC(dc, saved);
        return true;
    }
    GdiFlush();
    const int margin = static_cast<int>(std::ceil(5.0 * scale));
    const int left = std::max<int>({0, viewport.left, std::min(from.x, to.x) - margin});
    const int right = std::min<int>({context.surfaceWidth, viewport.right, std::max(from.x, to.x) + margin + 1});
    const int top = std::max<int>({0, viewport.top, std::min(from.y, to.y) - margin});
    const int bottom = std::min<int>({context.surfaceHeight, viewport.bottom, std::max(from.y, to.y) + margin + 1});
    const double phase = static_cast<double>(std::max(0, context.animationTick))
        * 15.0 / std::max(1, context.fixedTickHz);
    bool drawn = false;
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const double t = ((x - from.x) * dx + (y - from.y) * dy) / lengthSquared;
        if (t < 0.0 || t > 1.0) continue;
        const double distance = std::hypot(x - from.x - t * dx, y - from.y - t * dy);
        // IsRadBeam is a smooth ray, with a narrow muzzle and a wider target
        // end. Animate the glow along that ray without Tesla bolt geometry.
        const double pulse = 0.88 + 0.12 * std::sin(phase * 0.7 - t * 24.0);
        const double radius = scale * (1.2 + 2.6 * t) * pulse;
        if (distance >= radius) continue;
        const double edge = 1.0 - distance / radius;
        const int alpha = std::clamp(static_cast<int>(255.0 * edge * edge * pulse), 0, 255);
        if (alpha == 0) continue;
        if(runtimeGpuWorldPixel(context,x,y,concord::render::MapDrawKind::ConstantBlend,packRuntimeDibColor(color),alpha)){drawn=true;continue;}
        auto& pixel = context.surfacePixels[y * context.surfaceWidth + x];
        const COLORREF original = RGB((pixel >> 16U) & 255U, (pixel >> 8U) & 255U, pixel & 255U);
        pixel = packRuntimeDibColor(runtimeBlendTint(original, color, alpha));
        drawn = true;
    }
    return drawn;
}

POINT runtimeTemporalBeamMuzzlePoint(
    const SpecialMechanismRenderSnapshot& snapshot,
    const RECT& viewport, const RuntimeMapCamera& camera
) {
    POINT from = screenPoint(projectCellXY(snapshot.sourceX, snapshot.sourceY,
        snapshot.sourceLevel, camera), viewport, camera);
    from.x += runtimeMuzzlePixelX(snapshot.sourceScreenOffsetX, camera, snapshot.sourceNativeFixedX);
    from.y += runtimeMuzzlePixelY(snapshot.sourceScreenOffsetY, camera, snapshot.sourceNativeFixedY);
    return from;
}

void drawRuntimeTemporalStatus(
    HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats,
    const SpecialMechanismRenderSnapshot& snapshot,
    const WorldObjectInstance* source, const WorldObjectInstance* target, const GameWorld* world = nullptr
) {
    if (source && !source->alive && source->health > 0 && source->mission == "InsideTransport" && world) {
        for (const auto& host : world->getObjects()) {
            if (!host.alive || host.health <= 0 || std::find(host.transportPassengerIds.begin(),
                    host.transportPassengerIds.end(), source->instanceId) == host.transportPassengerIds.end()) continue;
            const auto* type = context.rules ? context.rules->findUnitType("UnitType", host.type) : nullptr;
            if (type && type->openTopped) { source = &host; break; }
        }
    }
    if (!snapshot.hasBeamEndpoints || source == nullptr || target == nullptr
        || !source->alive || !target->alive
        || runtimeSpecialObjectIsHidden(context, *source)
        || runtimeSpecialObjectIsHidden(context, *target)
        || !runtimeVisionObjectHasCurrentVisibility(context, *source)
        || !runtimeVisionObjectHasCurrentVisibility(context, *target)) return;
    const POINT from = runtimeTemporalBeamMuzzlePoint(snapshot, viewport, camera);
    const POINT targetOrigin = screenPoint(runtimeObjectDrawPoint(*target,
        runtimeObjectFoundationDimensions(*target, context), camera), viewport, camera);
    const POINT to = runtimeChronoEffectAnchor(*target, targetOrigin, context, camera, cache, false);
    if (drawRuntimeTemporalBeam(dc, viewport, context, from, to,
            runtimeElectricColorFromRules(snapshot.color.empty() ? "128,200,255" : snapshot.color, false),
            static_cast<double>(camera.tileWidth) / 60.0)) {
        recordRuntimeSpecialMechanismRenderIntent(stats, snapshot, context.animationTick);
    }
}
