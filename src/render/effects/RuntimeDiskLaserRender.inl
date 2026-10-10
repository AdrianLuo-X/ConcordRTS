COLORREF runtimeDiskLaserColor(const std::string& text) {
    std::istringstream input(text);
    int r = 0, g = 0, b = 0;
    char comma = 0;
    if (!(input >> r >> comma) || comma != ',' || !(input >> g >> comma)
        || comma != ',' || !(input >> b)) return RGB(0, 0, 0);
    return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

void drawRuntimeDiskLaserLine(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, POINT from, POINT to,
    COLORREF inner, double intensity) {
    // Retail creates both charging arcs and the final beam with LaserDrawClass.
    // Keep their translucent pale-purple light consistent at the join.
    drawRuntimePrismBeam(dc, viewport, context, from, to, inner, 1, false, intensity);
}

bool drawRuntimeDiskLaser(HDC dc, const RECT& viewport,
    const RuntimeProjectileRenderActor& actor, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera) {
    const bool charge = actor.renderIntent == "diskLaserCharge";
    if (!charge && actor.renderIntent != "diskLaserBeam") return false;
    if (runtimeActorLifecycleIsExpired(actor.lifecycleState)) return true;
    const auto* weapon = context.rules == nullptr ? nullptr : context.rules->findWeaponType(actor.weaponId);
    if (weapon == nullptr) return true;
    const COLORREF inner = weapon->houseColorLaser ? runtimeOwnerRemapColor(context, actor.owner)
        : runtimeDiskLaserColor(weapon->laserInnerColor);

    const auto point = [&](double x, double y) {
        POINT result = screenPoint(projectCellXY(x, y, actor.fromLevel, camera), viewport, camera);
        result.x += runtimeMuzzlePixelX(actor.sourceScreenOffsetX, camera, actor.sourceNativeFixedX);
        result.y += runtimeMuzzlePixelY(actor.sourceScreenOffsetY, camera, actor.sourceNativeFixedY);
        return result;
    };
    if (!charge) {
        drawRuntimePrismBeam(dc, viewport, context, point(actor.fromX, actor.fromY),
            runtimeProjectileActorTargetPoint(actor, viewport, camera), inner, 1, false,
            static_cast<double>(actor.remainingTicks) / std::max(1, actor.totalTicks));
        return true;
    }
    const int age = runtimeDiskLaserAuthoredFrame(
        runtimeActorFrameIndex(actor.frameIndex, actor.totalTicks, actor.remainingTicks), context.fixedTickHz);
    const int rear = ((actor.facing / 16) + 8) & 15;
    const auto ringPoint = [&](int index) {
        const auto& offset = runtimeDiskLaserPoints[(index + 32) & 15];
        return point(actor.fromX + offset[0] / 256.0, actor.fromY + offset[1] / 256.0);
    };
    for (int stage = 0; stage < 8; ++stage) {
        const int elapsed = age - 2 * stage;
        const int duration = 8 - stage;
        if (elapsed < 0 || elapsed >= duration) continue;
        const double intensity = 1.0 - 0.5 * static_cast<double>(elapsed) / duration;
        drawRuntimeDiskLaserLine(dc, viewport, context, ringPoint(rear + stage),
            ringPoint(rear + stage + 1), inner, intensity);
        drawRuntimeDiskLaserLine(dc, viewport, context, ringPoint(rear - stage),
            ringPoint(rear - stage - 1), inner, intensity);
    }
    return true;
}
