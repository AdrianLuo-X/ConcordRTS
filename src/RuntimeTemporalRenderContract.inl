bool runRuntimeTemporalRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) { error = "temporal rendering: " + message; return false; };
    if (context.rules == nullptr || context.art == nullptr) return fail("resources unavailable");
    constexpr int width = 480, height = 224;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * height * 3, background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    probe.fixedTickHz = 30;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -width / 2;
    camera.worldOffsetY = -100;
    camera.initialized = true;
    const RECT viewport{0, 0, width, height};
    GameWorld world;
    WorldObjectInstance source;
    source.instanceId = 1;
    source.kind = WorldObjectKind::Infantry;
    source.type = "CLEG";
    source.owner = "Americans";
    source.health = 125;
    source.x = 0;
    source.y = 4;
    source.facing = 64;
    source.mission = "Fire";
    auto target = source;
    target.instanceId = 2;
    target.kind = WorldObjectKind::Unit;
    target.type = "MTNK";
    target.owner = "Russians";
    target.x = 4;
    target.y = 0;
    target.facing = 192;
    target.mission = "Temporal";
    world.accessObjects() = {source, target};
    SpecialMechanismRenderSnapshot ray;
    ray.kind = "temporalBeam";
    ray.objectId = 1;
    ray.targetObjectId = 2;
    ray.sourceX = 125.0 / std::sqrt(2.0) / 256.0;
    ray.sourceY = 4.0 - 75.0 / std::sqrt(2.0) / 256.0;
    ray.sourceLevel = 135.0 / 256.0;
    ray.targetX = 4;
    ray.targetY = 0;
    ray.color = context.rules->getAudioVisualValue("ChronoBeamColor");
    ray.hasBeamEndpoints = true;
    RECT firingBody{};
    if (!resolveRuntimeObjectShpSpriteBounds(source, {0, 0}, probe, camera, *cache, firingBody))
        return fail("infantry firing body unavailable");
    ray.sourceScreenOffsetY = (firingBody.top + firingBody.bottom) / 2;
    std::vector<SpecialMechanismRenderSnapshot> snapshots{ray};
    probe.specialMechanismSnapshots = &snapshots;
    const auto draw = [&]() {
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
        return stats.specialMechanismRenderActors;
    };
    probe.animationTick = 8;
    if (draw() != 1) return fail("active beam was not drawn to the pixel surface");
    RECT sourceBounds{};
    const POINT sourceOrigin = screenPoint(runtimeObjectDrawPoint(source, {}, camera), viewport, camera);
    const POINT muzzle = runtimeTemporalBeamMuzzlePoint(ray, viewport, camera);
    if (!resolveRuntimeObjectShpSpriteBounds(source, sourceOrigin, probe, camera, *cache, sourceBounds)
        || muzzle.y < sourceBounds.top || muzzle.y >= sourceBounds.bottom)
        return fail("beam muzzle missed the rendered infantry body: y=" + std::to_string(muzzle.y)
            + " body=" + std::to_string(sourceBounds.top) + ".." + std::to_string(sourceBounds.bottom));
    const auto initial = pixels;
    const auto colored = std::count_if(pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
        return pixel != background && (pixel & 255U) > ((pixel >> 16U) & 255U);
    });
    if (colored < 100) return fail("stock blue beam was invisible");
    if (draw() != 1 || pixels != initial) return fail("same tick repaint changed the beam");
    probe.fixedTickHz = 60;
    probe.animationTick = 16;
    if (draw() != 1 || pixels != initial) return fail("simulation frequency changed beam timing");
    probe.fixedTickHz = 30;
    probe.animationTick = 20;
    if (draw() != 1 || pixels == initial) return fail("beam glow did not animate");
    snapshots.front().color = "255,32,24";
    if (draw() != 1 || std::none_of(pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
            return pixel != background && ((pixel >> 16U) & 255U) > (pixel & 255U);
        })) return fail("authored beam color override was ignored");
    snapshots.front() = ray;
    world.accessObjects().front().alive = false;
    if (draw() != 0 || std::any_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel != background; }))
        return fail("dead attacker left a beam");
    world.accessObjects().front().alive = true;
    SpecialMechanismRenderSnapshot hidden;
    hidden.kind = "chrono";
    hidden.objectId = 2;
    hidden.phase = "InTransit";
    snapshots.push_back(hidden);
    if (draw() != 0) return fail("hidden target leaked a beam");
    snapshots = {ray};
    std::fill(pixels.begin(), pixels.end(), background);
    const RECT clipped{150, 100, 300, 180};
    if (!drawRuntimeTemporalBeam(nullptr, clipped, probe, {40, 140}, {430, 140}, RGB(128, 200, 255), 1.0))
        return fail("clipped beam disappeared");
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if ((x < clipped.left || x >= clipped.right || y < clipped.top || y >= clipped.bottom)
            && pixels[y * width + x] != background) return fail("beam wrote outside the viewport");
    }
    const auto drawBodies = [&]() {
        for (const auto& object : world.getObjects()) {
            if (!object.alive) continue;
            const POINT point = screenPoint(runtimeObjectDrawPoint(object,
                runtimeObjectFoundationDimensions(object, probe), camera), viewport, camera);
            const bool drawn = objectUsesVoxelArt(object, probe)
                ? tryDrawVoxelSprite(nullptr, viewport, object, point, probe, camera, *cache)
                : tryDrawObjectShpSprite(nullptr, viewport, object, point, probe, camera, *cache);
            if (!drawn) return false;
        }
        return true;
    };
    for (int panel = 0; panel < 2; ++panel) {
        probe.animationTick = panel == 0 ? 8 : 20;
        std::fill(pixels.begin(), pixels.end(), background);
        if (!drawBodies()) return fail("stock preview unit could not be drawn");
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
        std::copy(pixels.begin(), pixels.end(), sheet.begin() + panel * width * height);
    }
    const auto& warpAway = context.rules->getSpecialMechanismRules().warpAwayAnim;
    const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(warpAway, 0, probe, *cache, false);
    if (first == nullptr || first->frameCount < 2) return fail("original WarpAway frames unavailable");
    const int frameCount = first->frameCount;
    snapshots.clear();
    world.accessObjects()[1].alive = false;
    RuntimeImpactRenderActor erase;
    erase.lifecycleState = "TemporalErase";
    erase.renderIntent = "temporalErase";
    erase.animId = warpAway;
    erase.targetObjectId = 2;
    erase.x = 4;
    erase.y = 0;
    erase.centerOnAnchor = true;
    int drawnFrames = 0;
    for (int frame = 0; frame < frameCount; ++frame) {
        erase.frameIndex = frame;
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, erase, stats);
        drawnFrames += stats.impactActorDrawn;
        if (frame == frameCount / 3) {
            if (!drawBodies()) return fail("completion preview unit unavailable");
            std::copy(pixels.begin(), pixels.end(), sheet.begin() + 2 * width * height);
        }
    }
    if (drawnFrames == 0) return fail("WarpAway did not render after its victim was removed");
    if (const char* path = std::getenv("CONCORD_TEMPORAL_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height * 3, sheet)) return fail("preview write failed");
    }
    std::cout << "Temporal rendering: blueBeam=ok pulse=ok colorOverride=ok fixedTickHz=ok "
              << "hiddenDead=ok clip=ok WarpAwayFrames=" << frameCount << '\n';
    return true;
}
