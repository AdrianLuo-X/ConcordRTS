bool runRuntimeChronoRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "chrono render: " + message; return false; };
    if (context.rules == nullptr || context.art == nullptr) return fail("resources missing");
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int panelWidth = 180, panelHeight = 160, width = panelWidth * 4, height = panelHeight * 3;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(width * height, background);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -panelWidth / 2;
    camera.worldOffsetY = -125;
    camera.initialized = true;
    RECT viewport{0, 0, panelWidth, panelHeight};
    GameWorld world;
    WorldObjectInstance unit;
    unit.instanceId = 1;
    unit.kind = WorldObjectKind::Infantry;
    unit.type = "CLEG";
    unit.owner = "Americans";
    unit.health = 125;
    unit.x = 0;
    unit.y = 0;
    unit.facing = 64;
    unit.mission = "Guard";
    world.accessObjects() = {unit};
    SpecialMechanismRenderSnapshot snapshot;
    snapshot.kind = "chrono";
    snapshot.objectId = 1;
    snapshot.phase = "WarpIn";
    snapshot.resourceId = context.rules->getSpecialMechanismRules().chronoSparkleAnim;
    snapshot.requiredProgress = 84;
    snapshot.expireTick = 85;
    std::vector<SpecialMechanismRenderSnapshot> snapshots{snapshot};
    probe.specialMechanismSnapshots = &snapshots;
    for (const std::string phase : {"WarpOut", "InTransit", "WarpIn", "Cooldown"}) {
        snapshots.front().phase = phase;
        const bool hidden = phase == "WarpOut" || phase == "InTransit";
        const auto lookup = buildRuntimeSpecialObjectRenderLookup(probe);
        if (runtimeSpecialObjectIsHidden(probe, unit) != hidden
            || runtimeSpecialObjectIsHidden(probe, unit, &lookup) != hidden)
            return fail("incorrect unit visibility during " + phase);
    }
    snapshots.front() = snapshot;
    const auto drawSparkle = [&]() {
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
        return stats.specialMechanismRenderActors;
    };
    if (drawSparkle() != 1) return fail("arrival sparkle missing");
    const auto sparkleFirst = pixels;
    probe.animationTick = 413;
    drawSparkle();
    if (pixels != sparkleFirst) return fail("sparkle jumped with global animation time");
    snapshots.front().progress = runtimeAnimationFrameDelayTicks(context.art->findArtType(snapshot.resourceId), probe.fixedTickHz);
    drawSparkle();
    if (pixels == sparkleFirst) return fail("recovery sparkle was a static image");
    snapshots.front().progress = snapshot.requiredProgress;
    if (drawSparkle() != 0) return fail("sparkle survived recovery");
    snapshots.front() = snapshot;
    world.accessObjects().front().alive = false;
    if (drawSparkle() != 0) return fail("dead unit retained recovery sparkle");
    world.accessObjects().front() = unit;
    int checkedFrames = 0, blankFrames = 0;
    for (const std::string phase : {"ChronoWarpOut", "ChronoWarpIn", "ChronoReinforcementIn"}) {
        RuntimeImpactRenderActor actor;
        actor.lifecycleState = phase;
        actor.renderIntent = "chronoWarp";
        actor.animId = phase == "ChronoReinforcementIn" ? context.rules->getSpecialMechanismRules().warpInAnim
            : context.rules->getSpecialMechanismRules().warpOutAnim;
        actor.owner = unit.owner;
        actor.targetObjectId = 1;
        actor.x = 0;
        actor.y = 0;
        actor.facing = unit.facing;
        actor.centerOnAnchor = true;
        const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId, 0, probe, *cache, false);
        if (first == nullptr || first->frameCount <= 1) return fail(actor.animId + " animation missing");
        const int count = first->frameCount;
        for (int frame = 0; frame < count; ++frame) {
            const auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId, frame, probe, *cache, false);
            if (sprite == nullptr
                || (sprite->image.width == 0 && sprite->image.height == 0)) {
                // Original WARPIN ends with an explicitly empty SHP frame.
                // The decoder now preserves that valid frame as a loaded
                // zero-sized sprite. It advances time without drawing pixels.
                bool authoredBlank = false;
                for (const auto& [key, bytes] : cache->auxiliaryShpBytesByImage) {
                    if (!equalAsciiNoCase(bytes.resolvedImageId, actor.animId)) continue;
                    const std::size_t offset = 8 + static_cast<std::size_t>(frame) * 24;
                    authoredBlank = bytes.bytes.size() >= offset + 24
                        && readUInt16(bytes.bytes, offset + 4) == 0
                        && readUInt16(bytes.bytes, offset + 6) == 0
                        && (bytes.bytes[offset + 8] == 3 || bytes.bytes[offset + 8] == 4)
                        && readUInt32(bytes.bytes, offset + 20) == 0;
                    if (authoredBlank) break;
                }
                if (!authoredBlank) return fail(actor.animId + " frame unavailable: " + std::to_string(frame));
                if (sprite == nullptr || sprite->frameIndex != frame
                    || !sprite->image.pixels.empty())
                    return fail(actor.animId + " authored blank frame did not decode: " + std::to_string(frame));
                actor.frameIndex = frame;
                std::fill(pixels.begin(), pixels.end(), background);
                RuntimeMapRenderStats blankStats;
                drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, blankStats);
                if (blankStats.impactActorDrawn != 0
                    || std::any_of(pixels.begin(), pixels.end(), [](std::uint32_t pixel) {
                        return pixel != background;
                    }))
                    return fail(actor.animId + " authored blank frame drew pixels: " + std::to_string(frame));
                ++blankFrames;
                ++checkedFrames;
                continue;
            }
            if (sprite->frameIndex != frame) return fail(actor.animId + " frame index mismatch");
            actor.frameIndex = frame;
            std::fill(pixels.begin(), pixels.end(), background);
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            if (stats.impactActorDrawn != 1) return fail(actor.animId + " frame not rendered: " + std::to_string(frame));
            const auto beforeMove = pixels;
            world.accessObjects().front().x = 8;
            std::fill(pixels.begin(), pixels.end(), background);
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            if (pixels != beforeMove) return fail("warp effect followed its teleported owner");
            world.accessObjects().front() = unit;
            ++checkedFrames;
        }
        std::cout << "Chrono animation: " << actor.animId << " frames=" << count << '\n';
    }
    std::fill(pixels.begin(), pixels.end(), background);
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 4; ++column) {
        auto& object = world.accessObjects().front();
        object = unit;
        object.type = row == 0 ? "CLEG" : row == 1 ? "CCOMAND" : "CMIN";
        object.kind = row == 2 ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
        object.mission = column >= 2 ? "ChronoFrozen" : "Guard";
        object.level = row == 1 ? 1 : 0;
        viewport = {column * panelWidth, row * panelHeight, (column + 1) * panelWidth, (row + 1) * panelHeight};
        snapshots.clear();
        if (column != 0) {
            snapshots = {snapshot};
            snapshots.front().phase = column == 1 ? "WarpOut" : column == 2 ? "WarpIn" : "Cooldown";
            snapshots.front().progress = column == 3 ? 18 : 0;
        }
        const POINT center = screenPoint(runtimeObjectDrawPoint(object, {}, camera), viewport, camera);
        if (column != 1) {
            const bool drawn = objectUsesVoxelArt(object, probe)
                ? tryDrawVoxelSprite(nullptr, viewport, object, center, probe, camera, *cache)
                : tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache);
            if (!drawn) return fail("preview unit missing: " + object.type);
        }
        if (column == 1 || column == 2) {
            RuntimeImpactRenderActor actor;
            actor.lifecycleState = column == 1 ? "ChronoWarpOut" : "ChronoWarpIn";
            actor.animId = context.rules->getSpecialMechanismRules().warpOutAnim;
            actor.renderIntent = "chronoWarp";
            actor.owner = object.owner;
            actor.x = object.x;
            actor.y = object.y;
            actor.level = object.level;
            actor.targetObjectId = 1;
            actor.facing = object.facing;
            actor.frameIndex = 4;
            actor.centerOnAnchor = true;
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
        }
        RuntimeMapRenderStats stats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, world, probe, camera, *cache, stats);
    }
    if (const char* path = std::getenv("CONCORD_CHRONO_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height, pixels)) return fail("preview write failed");
    }
    std::cout << "Chrono rendering: frames=" << checkedFrames << " authoredBlank=" << blankFrames
        << " phaseVisibility=ok fixedWarpPosition=ok animatedRecovery=ok recoveryEnd=ok\n";
    return true;
}
