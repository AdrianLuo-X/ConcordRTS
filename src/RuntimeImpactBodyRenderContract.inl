bool runRuntimeImpactBodyRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& text) {
        error = "body impact render: " + text;
        return false;
    };
    constexpr int width = 160, height = 180, columns = 4;
    constexpr std::uint32_t background = 0x004c5850U;
    const char* types[] = {"MTNK", "HTNK", "E1", "JUMPJET"};
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * columns * height * 2, background);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapRenderCache cache;
    const RECT viewport{0, 0, width, height};
    for (int column = 0; column < columns; ++column) {
        GameWorld world;
        WorldObjectInstance unit;
        unit.instanceId = 1;
        unit.type = types[column];
        unit.kind = column < 2 ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
        unit.owner = "Americans";
        unit.health = 100;
        unit.x = unit.y = 0;
        unit.subCell = column == 2 ? 2 : 0;
        unit.facing = 64;
        unit.mission = "Guard";
        unit.airborne = column == 3;
        unit.level = unit.flightLevel = unit.airborne ? 4 : 0;
        world.accessObjects().push_back(unit);
        RuntimeMapCamera camera;
        camera.worldOffsetX = -width / 2;
        camera.worldOffsetY = -140 - unit.level * camera.tileHeight / 2;
        camera.initialized = true;
        const POINT bodyOrigin = runtimeObjectBodyAnchorForRender(unit, viewport, probe, camera);
        std::fill(pixels.begin(), pixels.end(), background);
        const bool bodyDrawn = column < 2
            ? tryDrawVoxelSprite(nullptr, viewport, unit, bodyOrigin, probe, camera, cache)
            : tryDrawObjectShpSprite(nullptr, viewport, unit, bodyOrigin, probe, camera, cache);
        if (!bodyDrawn) return fail(std::string("original body missing: ") + unit.type);
        const auto body = pixels;
        RECT bodyBounds{width, height, 0, 0};
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            if (body[y * width + x] == background) continue;
            bodyBounds.left = std::min<LONG>(bodyBounds.left, x);
            bodyBounds.right = std::max<LONG>(bodyBounds.right, x + 1);
            bodyBounds.top = std::min<LONG>(bodyBounds.top, y);
            bodyBounds.bottom = std::max<LONG>(bodyBounds.bottom, y + 1);
        }
        RuntimeProjectileRenderActor beam;
        beam.actorId = 3;
        beam.targetObjectId = unit.instanceId;
        beam.owner = unit.owner;
        beam.weaponId = "Comet";
        beam.laser = true;
        beam.fromX = -1; beam.fromY = 1;
        beam.fromLevel = unit.level + 1;
        beam.toLevel = unit.level;
        beam.lifecycleState = "InFlight";
        beam.totalTicks = beam.remainingTicks = 15;
        std::vector<RuntimeProjectileRenderActor> beams{beam};
        probe.projectileActors = &beams;
        RuntimeMapRenderStats beamStats;
        drawRuntimeProjectileActors(nullptr, viewport, probe, camera, cache, beamStats, &world);
        probe.projectileActors = nullptr;
        int beamBodyOverlap = 0;
        for (std::size_t i = 0; i < body.size(); ++i)
            if (body[i] != background && pixels[i] != body[i]) ++beamBodyOverlap;
        if (beamStats.projectileActorTravelDrawn != 1 || beamBodyOverlap < 3)
            return fail(std::string("prism beam missed original body: ") + unit.type);

        RuntimeImpactRenderActor actor;
        actor.actorId = 2;
        actor.targetObjectId = unit.instanceId;
        actor.owner = unit.owner;
        actor.x = actor.y = 0;
        actor.level = unit.level;
        actor.animId = "S_CLSN22";
        actor.warheadId = "AP";
        actor.lifecycleState = "Impact";
        actor.renderIntent = "warheadImpact";
        actor.totalTicks = 30;
        actor.remainingTicks = 26;
        actor.frameIndex = 4;
        const POINT originalOrigin = runtimeImpactActorScreenPoint(actor, viewport, camera);
        const auto drawLegacy = [&] {
            return drawRuntimeClassifiedAuxiliaryEffect(nullptr, viewport, originalOrigin,
                probe, camera, cache, actor.animId, actor.frameIndex, 0, 0, false,
                RuntimeAuxiliaryEffectKind::Impact, false, false);
        };
        std::vector<std::uint32_t> legacy;
        for (int row = 0; row < 2; ++row) {
            std::copy(body.begin(), body.end(), pixels.begin());
            if (row == 0) {
                if (!drawLegacy()) return fail("original impact resource missing");
                legacy = pixels;
            } else {
                RuntimeMapRenderStats stats;
                drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, cache, actor, stats);
                if (stats.impactActorDrawn != 1) return fail("ordinary impact was not drawn");
                int changed = 0, overlap = 0, sumX = 0, sumY = 0;
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    if (pixels[y * width + x] == body[y * width + x]) continue;
                    ++changed;
                    sumX += x;
                    sumY += y;
                    if (body[y * width + x] != background) ++overlap;
                }
                if (changed < 8 || overlap < 6
                    || sumX / changed < bodyBounds.left - 3
                    || sumX / changed > bodyBounds.right + 3
                    || sumY / changed < bodyBounds.top - 3
                    || sumY / changed > bodyBounds.bottom + 3)
                    return fail(std::string(types[column]) + " burst is outside the body"
                        + " pixels=" + std::to_string(changed) + " overlap=" + std::to_string(overlap)
                        + " centroid=" + std::to_string(changed ? sumY / changed : -1)
                        + " body=" + runtimeRectToString(bodyBounds));
            }
            for (int y = 0; y < height; ++y)
                std::copy_n(pixels.begin() + y * width, width,
                    sheet.begin() + (row * height + y) * width * columns + column * width);
        }
        const auto fixed = pixels;
        auto& moved = world.accessObjects().front();
        moved.x = moved.y = 10;
        moved.health = 0;
        moved.alive = false;
        moved.mission = "Die";
        std::copy(body.begin(), body.end(), pixels.begin());
        RuntimeMapRenderStats movedStats;
        drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, cache, actor, movedStats);
        if (pixels != fixed) return fail("hit burst moved after its victim moved or died");
        actor.targetObjectId = -1;
        actor.centerOnAnchor = true;
        std::copy(body.begin(), body.end(), pixels.begin());
        if (!drawRuntimeClassifiedAuxiliaryEffect(nullptr, viewport, originalOrigin,
                probe, camera, cache, actor.animId, actor.frameIndex, 0, 0, false,
                RuntimeAuxiliaryEffectKind::Impact, false, true)) return fail("centered ground burst missing");
        const auto groundReference = pixels;
        std::copy(body.begin(), body.end(), pixels.begin());
        RuntimeMapRenderStats groundStats;
        drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, cache, actor, groundStats);
        if (pixels != groundReference) return fail("ground explosion moved off its projectile anchor");
    }
    if (const char* path = std::getenv("CONCORD_IMPACT_BODY_BMP"))
        if (*path && !writeRuntimeBgraBmp(path, width * columns, height * 2, sheet))
            return fail("preview write failed");
    std::cout << "Impact body placement: two tank bodies, infantry/subcell, airborne infantry, "
                 "fixed hit position and ground explosions passed\n";
    error.clear();
    return true;
}
