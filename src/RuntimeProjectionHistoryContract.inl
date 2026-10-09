// Runs only on the interaction smoke's private loaded fixture. It checks the
// actual projection/cache and actual map+overlay pixels, not a duplicate hash.
static bool runRuntimeProjectionHistoryContract(RuntimeWindowState& probe,
    const RECT& clientRect, std::string& error) {
    RuntimeHistoryQueryReferenceScope optimized(false);
    RuntimeEmptyBuildingDepthReferenceScope emptyDepth(false);
    const auto fail = [&](const std::string& message) {
        error = "projection history contract: " + message; return false;
    };
    struct Restore {
        RuntimeWindowState& state;
        std::vector<WorldObjectInstance> objects = state.world.getObjects();
        std::vector<int> selected = state.selectedObjectIds;
        RuntimeMapCamera camera = state.camera;
        bool mouseValid = state.mouseClientPointValid;
        ~Restore() {
            state.world.accessObjects() = std::move(objects);
            state.selectedObjectIds = std::move(selected);
            state.camera = camera; state.mouseClientPointValid = mouseValid;
            state.projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(state);
        }
    } restore{probe};
    if (restore.objects.size() != 2 || !restore.objects[0].alive || !restore.objects[1].alive)
        return fail("two live fixture bodies required");
    const RECT viewport = runtimeMapViewport(clientRect);
    auto dead = restore.objects[0];
    dead.instanceId += 100; dead.alive = false; dead.health = 0; dead.mission = "Destroyed";
    dead.x += 2; dead.cell = runtimeCellKey(dead.x, dead.y);
    auto otherDead = dead; ++otherDead.instanceId;
    otherDead.kind = WorldObjectKind::Infantry; otherDead.type = "E1";
    probe.world.accessObjects().push_back(dead);
    probe.world.accessObjects().push_back(otherDead);
    const auto fixture = probe.world.getObjects();
    probe.selectedObjectIds = {fixture[0].instanceId}; probe.mouseClientPointValid = false;
    probe.projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(probe);

    const auto signature = [&](bool reference) {
        RuntimeHistoryQueryReferenceScope path(reference);
        return runtimeProjectedObjectsSignature(probe);
    };
    const auto sameSpans = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i)
            if (left[i].y != right[i].y || left[i].left != right[i].left || left[i].right != right[i].right) return false;
        return true;
    };
    const auto sameProjection = [&](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            const auto& a = left[i]; const auto& b = right[i];
            const auto point = [](POINT x, POINT y) { return x.x == y.x && x.y == y.y; };
            if (a.object != b.object || a.observerType != b.observerType || a.observerOwner != b.observerOwner
                || a.observerKind != b.observerKind || a.spyDisguiseProjected != b.spyDisguiseProjected
                || !point(a.center, b.center) || !point(a.renderCenter, b.renderCenter)
                || !runtimeRectEquals(a.bounds, b.bounds) || !runtimeRectEquals(a.spriteBounds, b.spriteBounds)
                || a.hasMobileSpriteBounds != b.hasMobileSpriteBounds
                || !runtimeRectEquals(a.selectionBounds, b.selectionBounds)
                || !runtimeRectEquals(a.selectionBaseBounds, b.selectionBaseBounds)
                || a.drawLevel != b.drawLevel || a.projectedX != b.projectedX || a.projectedY != b.projectedY
                || a.usesFootprintHit != b.usesFootprintHit
                || a.hasStructureSpriteGeometry != b.hasStructureSpriteGeometry
                || a.airborneForeground != b.airborneForeground || a.hasBodyHitClip != b.hasBodyHitClip
                || !runtimeRectEquals(a.bodyHitClip, b.bodyHitClip)
                || a.factoryForegroundHostId != b.factoryForegroundHostId
                || !runtimeRectEquals(a.footprintBounds, b.footprintBounds)
                || !sameSpans(a.structureSpriteHitSpans, b.structureSpriteHitSpans)
                || !sameSpans(a.structureForegroundHitSpans, b.structureForegroundHitSpans)
                || a.hasBuildingDepthVisibility != b.hasBuildingDepthVisibility
                || !sameSpans(a.visibleBuildingHitSpans, b.visibleBuildingHitSpans)
                || a.hasHostedSortOverride != b.hasHostedSortOverride || a.hostedSortCellSum != b.hostedSortCellSum
                || a.footprintHalfWidth != b.footprintHalfWidth || a.footprintHalfHeight != b.footprintHalfHeight
                || a.footprintHitCenters.size() != b.footprintHitCenters.size()) return false;
            for (std::size_t p = 0; p < a.footprintHitCenters.size(); ++p)
                if (!point(a.footprintHitCenters[p], b.footprintHitCenters[p])) return false;
        }
        return true;
    };
    // WM_PAINT prepares these immutable authorities before calculating the
    // capture/projection signature. A fresh standalone fixture must do the
    // same: the first refresh initializes gate/special state included in the
    // signature, independently of any object's pose.
    refreshRuntimeGateRenderSnapshots(probe);
    refreshRuntimeSpecialMechanismRenderSnapshots(probe);
    const auto baseSignature = signature(false);
    const std::vector<RuntimeProjectedObject> baseline = runtimeProjectedObjectsForViewport(probe, viewport);
    if (baseline.size() != 2) return fail("dead fixture appeared in projection");
    if (signature(false) != baseSignature || probe.projectedObjectsCacheSignature != baseSignature)
        return fail("prepared baseline changed signature during initial projection");
    const auto* cachedData = probe.projectedObjectsCache.data();
    auto& changedDead = probe.world.accessObjects()[2];
    changedDead.facing = 3; changedDead.turretFacing = 99; changedDead.owner = "Russians";
    changedDead.type = "MISSING_DEAD_BODY"; changedDead.mission = "Attack";
    changedDead.health = 123; changedDead.deployed = true; changedDead.airborne = true;
    changedDead.cloaked = changedDead.submerged = changedDead.prone = true;
    changedDead.factoryExitHostId = fixture[1].instanceId;
    changedDead.factoryExitAirborne = true; changedDead.tankBunkerHostId = fixture[1].instanceId;
    changedDead.movementAnimationDistance = 25.5;
    changedDead.bodyFacingClock = {256, 1536, 256, 1, probe.tick, 1};
    changedDead.turretFacingClock = {512, 2048, 256, 2, probe.tick, 2};
    changedDead.hasRenderPosition = changedDead.renderMoveActive = true;
    changedDead.renderX = changedDead.x + 0.25; changedDead.renderY = changedDead.y - 0.5;
    changedDead.renderLevel = changedDead.level + 4.0;
    const auto mutatedDead = changedDead;
    const auto legacyChanged = signature(true);
    if (signature(false) != baseSignature) return fail("unused dead pose changed prepared signature");
    if (!sameProjection(baseline, runtimeProjectedObjectsForViewport(probe, viewport)))
        return fail("unused dead pose changed cached projection geometry/order");
    if (probe.projectedObjectsCache.data() != cachedData)
        return fail("unused dead pose rebuilt prepared projection cache");
    {
        RuntimeHistoryQueryReferenceScope reference(true);
        const auto direct = runtimeProjectedObjectsForViewport(probe, viewport);
        if (!sameProjection(baseline, direct)) return fail("original projection differs for dead pose");
    }
    probe.world.accessObjects()[2] = fixture[2];
    if (signature(true) == legacyChanged) return fail("shared reference did not retain original dead pose hash");
    for (int variation = 0; variation != 4; ++variation) {
        if (variation == 0) probe.world.accessObjects()[2].alive = true;
        if (variation == 1) ++probe.world.accessObjects()[2].instanceId;
        if (variation == 2) probe.world.accessObjects()[2].kind = WorldObjectKind::Structure;
        if (variation == 3) std::swap(probe.world.accessObjects()[2], probe.world.accessObjects()[3]);
        if (signature(false) == baseSignature) return fail("dead liveness/identity/order did not invalidate signature");
        probe.world.accessObjects()[2] = fixture[2]; probe.world.accessObjects()[3] = fixture[3];
    }
    {
        auto& revived = probe.world.accessObjects()[2];
        revived.alive = true; revived.health = fixture[0].health; revived.mission = "Guard";
        const std::vector<RuntimeProjectedObject> actual = runtimeProjectedObjectsForViewport(probe, viewport);
        RuntimeHistoryQueryReferenceScope reference(true);
        const auto direct = runtimeProjectedObjectsForViewport(probe, viewport);
        if (actual.size() != 3 || !sameProjection(actual, direct))
            return fail("revived body did not invalidate and retain original projection");
        probe.world.accessObjects()[2] = fixture[2];
    }

    // Every field used by the original live-object hash remains present. Both
    // facing clocks and both conditional render-pose paths are active here.
    auto live = fixture[0]; live.hasRenderPosition = live.renderMoveActive = true;
    live.renderX = live.x; live.renderY = live.y; live.renderLevel = live.level;
    live.bodyFacingClock = {256, 1536, 256, 1, probe.tick, 1};
    live.turretFacingClock = {512, 2048, 256, 2, probe.tick, 2};
    probe.world.accessObjects()[0] = live;
    const auto fastLive = signature(false), originalLive = signature(true);
    const auto checkLive = [&](const char* name, const auto& mutate) {
        probe.world.accessObjects()[0] = live; mutate(probe.world.accessObjects()[0]);
        if (signature(false) == fastLive || signature(true) == originalLive) {
            error = std::string("projection history contract: live field omitted: ") + name; return false;
        }
        return true;
    };
#define CONCORD_PROJECTION_INT(field) if (!checkLive(#field, [](auto& o) { ++o.field; })) return false
    CONCORD_PROJECTION_INT(instanceId); CONCORD_PROJECTION_INT(facing); CONCORD_PROJECTION_INT(turretFacing);
    CONCORD_PROJECTION_INT(health); CONCORD_PROJECTION_INT(activeGunnerTurretIndex); CONCORD_PROJECTION_INT(dockedToObjectId);
    CONCORD_PROJECTION_INT(buildAnimTicksRemaining); CONCORD_PROJECTION_INT(buildAnimTicksTotal);
    CONCORD_PROJECTION_INT(powerAnimationPausedAtTick); CONCORD_PROJECTION_INT(powerAnimationPausedTicks);
    CONCORD_PROJECTION_INT(structureRecoilStartTick); CONCORD_PROJECTION_INT(chargeTurretStartTick);
    CONCORD_PROJECTION_INT(factoryExitHostId); CONCORD_PROJECTION_INT(factoryExitAnimationStartTick);
    CONCORD_PROJECTION_INT(factoryExitAnimationEndTick); CONCORD_PROJECTION_INT(tankBunkerHostId);
    CONCORD_PROJECTION_INT(tankBunkerOccupantId); CONCORD_PROJECTION_INT(tankBunkerAnimationStartTick);
    CONCORD_PROJECTION_INT(cloakCooldownTicks); CONCORD_PROJECTION_INT(submergeCooldownTicks);
    CONCORD_PROJECTION_INT(currentAmmo); CONCORD_PROJECTION_INT(deployStateTicksRemaining);
    CONCORD_PROJECTION_INT(specialCashNextTick); CONCORD_PROJECTION_INT(specialCloneDeliveryObjectId);
    CONCORD_PROJECTION_INT(proneTicksRemaining); CONCORD_PROJECTION_INT(weaponCooldownTicksRemaining);
    CONCORD_PROJECTION_INT(x); CONCORD_PROJECTION_INT(y); CONCORD_PROJECTION_INT(level); CONCORD_PROJECTION_INT(subCell);
    CONCORD_PROJECTION_INT(bodyFacingClock.initial); CONCORD_PROJECTION_INT(bodyFacingClock.target);
    CONCORD_PROJECTION_INT(bodyFacingClock.rate); CONCORD_PROJECTION_INT(bodyFacingClock.elapsedNumerator);
    CONCORD_PROJECTION_INT(bodyFacingClock.lastTick); CONCORD_PROJECTION_INT(bodyFacingClock.lastFacing);
    CONCORD_PROJECTION_INT(turretFacingClock.initial); CONCORD_PROJECTION_INT(turretFacingClock.target);
    CONCORD_PROJECTION_INT(turretFacingClock.rate); CONCORD_PROJECTION_INT(turretFacingClock.elapsedNumerator);
    CONCORD_PROJECTION_INT(turretFacingClock.lastTick); CONCORD_PROJECTION_INT(turretFacingClock.lastFacing);
#undef CONCORD_PROJECTION_INT
#define CONCORD_PROJECTION_BOOL(field) if (!checkLive(#field, [](auto& o) { o.field = !o.field; })) return false
    CONCORD_PROJECTION_BOOL(alive); CONCORD_PROJECTION_BOOL(deployed); CONCORD_PROJECTION_BOOL(workAnimationActive);
    CONCORD_PROJECTION_BOOL(poweredOperational); CONCORD_PROJECTION_BOOL(factoryExitAirborne);
    CONCORD_PROJECTION_BOOL(tankBunkerLowering); CONCORD_PROJECTION_BOOL(submerged); CONCORD_PROJECTION_BOOL(cloaked);
    CONCORD_PROJECTION_BOOL(airborne); CONCORD_PROJECTION_BOOL(prone); CONCORD_PROJECTION_BOOL(onBridge);
    CONCORD_PROJECTION_BOOL(hasRenderPosition); CONCORD_PROJECTION_BOOL(renderMoveActive); CONCORD_PROJECTION_BOOL(renderOnBridge);
#undef CONCORD_PROJECTION_BOOL
#define CONCORD_PROJECTION_COORD(field) if (!checkLive(#field, [](auto& o) { o.field += 0.5; })) return false
    CONCORD_PROJECTION_COORD(renderX); CONCORD_PROJECTION_COORD(renderY); CONCORD_PROJECTION_COORD(renderLevel);
    CONCORD_PROJECTION_COORD(movementAnimationDistance);
#undef CONCORD_PROJECTION_COORD
    if (!checkLive("kind", [](auto& o) { o.kind = WorldObjectKind::Infantry; })
        || !checkLive("owner", [](auto& o) { o.owner += "_changed"; })
        || !checkLive("type", [](auto& o) { o.type += "_changed"; })
        || !checkLive("mission", [](auto& o) { o.mission += "_changed"; })) return false;
    probe.world.accessObjects()[0] = fixture[0];

    GameWorld mobileOnly;
    mobileOnly.accessObjects() = {fixture[0], fixture[2], fixture[3]};
    if (runtimePaintBuildingDepthCaptureAllowed(mobileOnly)) return fail("mobile-only paint retained unused capture");
    {
        RuntimeHistoryQueryReferenceScope reference(true);
        if (!runtimePaintBuildingDepthCaptureAllowed(mobileOnly)) return fail("shared reference omitted original capture");
    }
    {
        RuntimeEmptyBuildingDepthReferenceScope reference(true);
        if (!runtimePaintBuildingDepthCaptureAllowed(mobileOnly)) return fail("empty-depth reference omitted capture");
    }
    mobileOnly.accessObjects().back().kind = WorldObjectKind::Structure;
    if (!runtimePaintBuildingDepthCaptureAllowed(mobileOnly)) return fail("dead structure bypassed conservative capture");

    struct Surface {
        RuntimeBackBuffer buffer;
        HDC screen = GetDC(nullptr);
        ~Surface() { releaseRuntimeBackBuffer(buffer); if (screen) ReleaseDC(nullptr, screen); }
    } surface;
    const int width = clientRect.right - clientRect.left, height = clientRect.bottom - clientRect.top;
    if (!surface.screen || !ensureRuntimeBackBuffer(surface.buffer, surface.screen, width, height))
        return fail("pixel comparison surface allocation");
    RuntimeMapRenderContext context;
    context.theater = probe.theaterLoaded ? &probe.theater : nullptr;
    context.art = probe.artLoaded ? &probe.art : nullptr; context.rules = &probe.rules;
    context.mapFacts = probe.mapFactsLoaded ? &probe.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(probe.config);
    context.tileRootPath = runtimeTileRootPath(probe.config, probe.report.map);
    context.palettePath = runtimePalettePath(probe.config, probe.report.map);
    context.structureRootPath = runtimeStructureRootPath(probe.config, probe.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(probe.config, probe.report.map);
    context.animationTick = probe.tick; context.fixedTickHz = probe.config.fixedTickHz;
    context.stableRenderConfiguration = true; context.selectedObjectIds = &probe.selectedObjectIds;
    context.ownerRemapColors = &probe.ownerRemapColors; context.neutralRemapOwners = &probe.neutralRemapOwners;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    const auto render = [&](bool reference) {
        RuntimeHistoryQueryReferenceScope path(reference);
        probe.projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(probe);
        GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(surface.buffer.pixels), count, 0x00202d38U);
        context.surfacePixels = static_cast<std::uint32_t*>(surface.buffer.pixels);
        context.surfaceWidth = width; context.surfaceHeight = height;
        drawRuntimeMap(surface.buffer.memoryDc, viewport, probe.world, context, probe.camera, probe.renderCache);
        std::swap(probe.backBuffer, surface.buffer);
        drawRuntimeInteractionOverlay(probe.backBuffer.memoryDc, clientRect, probe, context, true);
        GdiFlush(); std::swap(probe.backBuffer, surface.buffer);
        const auto* pixels = static_cast<const std::uint32_t*>(surface.buffer.pixels);
        return std::vector<std::uint32_t>(pixels, pixels + count);
    };
    const auto expectedPixels = render(false);
    probe.world.accessObjects()[2] = mutatedDead;
    if (render(false) != expectedPixels || render(true) != expectedPixels)
        return fail("unused dead pose changed actual map/overlay pixels");

    // Unlike dead mobile history, a dead structure can still draw rubble or
    // the reverse buildup. These layers also contribute to the live building's
    // scene-Z hit spans. Keep precisely the fields in the original dead hash;
    // its existing early return still excludes the dead object's render pose.
    auto deadStructure = fixture[1];
    deadStructure.instanceId += 500;
    deadStructure.alive = false; deadStructure.health = 0;
    deadStructure.mission = "Destroyed";
    deadStructure.bodyFacingClock = {256, 1536, 256, 1, probe.tick, 1};
    deadStructure.turretFacingClock = {512, 2048, 256, 2, probe.tick, 2};
    probe.world.accessObjects() = {fixture[0], fixture[1], deadStructure};
    for (int state = 0; state != 3; ++state) {
        auto base = deadStructure;
        base.rubble = state == 1;
        if (state == 2) {
            base.mission = "Sold";
            base.buildAnimTicksTotal = base.buildAnimTicksRemaining = 24;
        }
        probe.world.accessObjects()[2] = base;
        const auto fast = signature(false), original = signature(true);
        if (fast != original) return fail("dead structure did not retain the complete original hash");
        const auto checkStructure = [&](const char* name, const auto& mutate) {
            probe.world.accessObjects()[2] = base;
            mutate(probe.world.accessObjects()[2]);
            const auto changedFast = signature(false), changedOriginal = signature(true);
            if (changedFast == fast || changedOriginal == original || changedFast != changedOriginal) {
                error = std::string("projection history contract: dead structure field omitted: ") + name;
                return false;
            }
            return true;
        };
#define CONCORD_PROJECTION_DEAD_INT(field) if (!checkStructure(#field, [](auto& o) { ++o.field; })) return false
        CONCORD_PROJECTION_DEAD_INT(instanceId); CONCORD_PROJECTION_DEAD_INT(facing);
        CONCORD_PROJECTION_DEAD_INT(turretFacing); CONCORD_PROJECTION_DEAD_INT(health);
        CONCORD_PROJECTION_DEAD_INT(activeGunnerTurretIndex); CONCORD_PROJECTION_DEAD_INT(dockedToObjectId);
        CONCORD_PROJECTION_DEAD_INT(buildAnimTicksRemaining); CONCORD_PROJECTION_DEAD_INT(buildAnimTicksTotal);
        CONCORD_PROJECTION_DEAD_INT(powerAnimationPausedAtTick); CONCORD_PROJECTION_DEAD_INT(powerAnimationPausedTicks);
        CONCORD_PROJECTION_DEAD_INT(structureRecoilStartTick); CONCORD_PROJECTION_DEAD_INT(chargeTurretStartTick);
        CONCORD_PROJECTION_DEAD_INT(factoryExitHostId); CONCORD_PROJECTION_DEAD_INT(factoryExitAnimationStartTick);
        CONCORD_PROJECTION_DEAD_INT(factoryExitAnimationEndTick); CONCORD_PROJECTION_DEAD_INT(tankBunkerHostId);
        CONCORD_PROJECTION_DEAD_INT(tankBunkerOccupantId); CONCORD_PROJECTION_DEAD_INT(tankBunkerAnimationStartTick);
        CONCORD_PROJECTION_DEAD_INT(cloakCooldownTicks); CONCORD_PROJECTION_DEAD_INT(submergeCooldownTicks);
        CONCORD_PROJECTION_DEAD_INT(currentAmmo); CONCORD_PROJECTION_DEAD_INT(deployStateTicksRemaining);
        CONCORD_PROJECTION_DEAD_INT(specialCashNextTick); CONCORD_PROJECTION_DEAD_INT(specialCloneDeliveryObjectId);
        CONCORD_PROJECTION_DEAD_INT(proneTicksRemaining); CONCORD_PROJECTION_DEAD_INT(weaponCooldownTicksRemaining);
        CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.initial); CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.target);
        CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.rate); CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.elapsedNumerator);
        CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.lastTick); CONCORD_PROJECTION_DEAD_INT(bodyFacingClock.lastFacing);
        CONCORD_PROJECTION_DEAD_INT(turretFacingClock.initial); CONCORD_PROJECTION_DEAD_INT(turretFacingClock.target);
        CONCORD_PROJECTION_DEAD_INT(turretFacingClock.rate); CONCORD_PROJECTION_DEAD_INT(turretFacingClock.elapsedNumerator);
        CONCORD_PROJECTION_DEAD_INT(turretFacingClock.lastTick); CONCORD_PROJECTION_DEAD_INT(turretFacingClock.lastFacing);
#undef CONCORD_PROJECTION_DEAD_INT
#define CONCORD_PROJECTION_DEAD_BOOL(field) if (!checkStructure(#field, [](auto& o) { o.field = !o.field; })) return false
        CONCORD_PROJECTION_DEAD_BOOL(deployed); CONCORD_PROJECTION_DEAD_BOOL(workAnimationActive);
        CONCORD_PROJECTION_DEAD_BOOL(poweredOperational); CONCORD_PROJECTION_DEAD_BOOL(factoryExitAirborne);
        CONCORD_PROJECTION_DEAD_BOOL(tankBunkerLowering); CONCORD_PROJECTION_DEAD_BOOL(submerged);
        CONCORD_PROJECTION_DEAD_BOOL(cloaked); CONCORD_PROJECTION_DEAD_BOOL(airborne); CONCORD_PROJECTION_DEAD_BOOL(prone);
#undef CONCORD_PROJECTION_DEAD_BOOL
        if (!checkStructure("owner", [](auto& o) { o.owner += "_changed"; })
            || !checkStructure("type", [](auto& o) { o.type += "_changed"; })
            || !checkStructure("mission", [](auto& o) { o.mission += "_changed"; })
            || !checkStructure("movementAnimationDistance", [](auto& o) { o.movementAnimationDistance += 0.5; }))
            return false;
    }

    const auto freshProjection = [&](bool reference) {
        RuntimeHistoryQueryReferenceScope path(reference);
        probe.projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(probe);
        return std::vector<RuntimeProjectedObject>(runtimeProjectedObjectsForViewport(probe, viewport));
    };
    const auto liveBuilding = [&](const auto& objects) -> const RuntimeProjectedObject* {
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& object) {
            return object.object && object.object->instanceId == fixture[1].instanceId;
        });
        return found == objects.end() ? nullptr : &*found;
    };
    probe.world.accessObjects()[2] = deadStructure;
    refreshRuntimeGateRenderSnapshots(probe);
    refreshRuntimeSpecialMechanismRenderSnapshots(probe);
    const auto uncovered = freshProjection(true);
    const auto* uncoveredBuilding = liveBuilding(uncovered);
    if (!uncoveredBuilding || !uncoveredBuilding->hasBuildingDepthVisibility
        || uncoveredBuilding->visibleBuildingHitSpans.empty())
        return fail("actual live structure scene-Z fixture is unavailable");

    // Select a real stock selling layer that changes the visible live hit spans.
    // This is fixture setup, not a permissive assertion: absence of an actual
    // occluder fails rather than accepting two fallback/empty geometry results.
    bool foundOccluder = false;
    for (const char* type : {"GAWEAP", "GAPOWR"}) {
        for (const RuntimeCellPoint offset : std::array<RuntimeCellPoint, 9>{{
                {0, 0}, {-1, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}}) {
            auto selling = deadStructure;
            selling.type = type; selling.mission = "Sold";
            selling.buildAnimTicksTotal = selling.buildAnimTicksRemaining = 24;
            selling.x += offset.x; selling.y += offset.y;
            selling.cell = runtimeCellKey(selling.x, selling.y);
            probe.world.accessObjects()[2] = selling;
            const auto covered = freshProjection(false);
            const auto* coveredBuilding = liveBuilding(covered);
            if (coveredBuilding && coveredBuilding->hasBuildingDepthVisibility
                && !sameSpans(coveredBuilding->visibleBuildingHitSpans, uncoveredBuilding->visibleBuildingHitSpans)) {
                deadStructure = selling; foundOccluder = true; break;
            }
        }
        if (foundOccluder) break;
    }
    if (!foundOccluder) return fail("actual selling layer did not occlude live structure hit spans");

    const auto structuralPixels = [&](bool reference) {
        context.captureBuildingDepthHitSpans = true;
        // The public paint scope exercises map-to-overlay reuse. The original
        // History path renders without that outer scope, so the map's internal
        // geometry pool expires before the overlay's fresh visibility replay.
        if (reference) return render(true);
        RuntimeGeometryQueryScope paint(probe.world, viewport, context, probe.camera, probe.renderCache);
        return render(false);
    };
    std::size_t structuralCases = 0;
    for (bool rubble : {false, true}) {
        auto base = deadStructure;
        base.rubble = rubble;
        if (rubble) {
            base.type = "GAPOWR"; base.mission = "Destroyed";
            base.buildAnimTicksRemaining = base.buildAnimTicksTotal = 0;
        }
        probe.world.accessObjects()[2] = base;
        const auto beforeDraw = probe.world.deterministicSimulationStateHash();
        const auto fastPixels = structuralPixels(false);
        const std::vector<RuntimeProjectedObject> fastPaintProjection = probe.projectedObjectsCache;
        const auto originalPixels = structuralPixels(true);
        if (fastPixels != originalPixels || !sameProjection(fastPaintProjection, probe.projectedObjectsCache)
            || probe.world.deterministicSimulationStateHash() != beforeDraw)
            return fail("actual rubble/selling map and paint-to-projection replay differ");
        std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> actualSpans;
        if (!resolveRuntimeBuildingVisibleHitSpans(probe.world, viewport, context, probe.camera,
                probe.renderCache, actualSpans) || !actualSpans.contains(base.instanceId))
            return fail("actual rubble/selling structure did not participate in scene-Z replay");
        const auto placement = runtimeObjectWorldRenderPlacement(base, probe.world, viewport,
            context, probe.camera, probe.renderCache);
        RuntimeStructureSpriteGeometry body;
        if (!resolveRuntimeStructureSpriteGeometry(base, placement.center, context, probe.camera,
                probe.renderCache, body) || body.hitSpans.empty())
            return fail("actual rubble/selling body did not resolve opaque stock asset geometry");

        for (int change = 0; change != 7; ++change) {
            probe.world.accessObjects()[2] = base;
            const auto baselineProjection = freshProjection(false);
            const auto beforeSignature = signature(false);
            auto& changed = probe.world.accessObjects()[2];
            switch (change) {
                case 0: changed.type = base.type == "GAPOWR" ? "GAWEAP" : "GAPOWR"; break;
                case 1: changed.mission = rubble ? "Guard" : "Destroyed"; break;
                case 2: ++changed.health; break;
                case 3: ++changed.facing; break;
                case 4: ++changed.turretFacing; break;
                case 5: ++changed.buildAnimTicksRemaining; break;
                case 6: ++changed.buildAnimTicksTotal; break;
            }
            const auto changedSignature = signature(false);
            if (changedSignature == beforeSignature || changedSignature != signature(true))
                return fail("same-tick renderable dead structure omitted an original hash field");
            const auto beforeQuery = probe.world.deterministicSimulationStateHash();
            const std::vector<RuntimeProjectedObject> actual = runtimeProjectedObjectsForViewport(probe, viewport);
            if (probe.projectedObjectsCacheSignature != changedSignature)
                return fail("same-tick dead structure reused its previous projection signature");
            const auto original = freshProjection(true);
            if (!sameProjection(actual, original)
                || probe.world.deterministicSimulationStateHash() != beforeQuery)
                return fail("same-tick dead structure changed projection, visible spans or world authority");
            if (!rubble && change == 1) {
                const auto* oldBuilding = liveBuilding(baselineProjection);
                const auto* newBuilding = liveBuilding(actual);
                if (!oldBuilding || !newBuilding
                    || sameSpans(oldBuilding->visibleBuildingHitSpans, newBuilding->visibleBuildingHitSpans))
                    return fail("same-tick selling removal did not exercise changed live hit spans");
            }
            const auto changedFastPixels = structuralPixels(false);
            const std::vector<RuntimeProjectedObject> changedFastPaintProjection = probe.projectedObjectsCache;
            const auto changedOriginalPixels = structuralPixels(true);
            if (changedFastPixels != changedOriginalPixels
                || !sameProjection(changedFastPaintProjection, probe.projectedObjectsCache)
                || probe.world.deterministicSimulationStateHash() != beforeQuery)
                return fail("same-tick dead structure differs in actual map/overlay or paint hit geometry");
            ++structuralCases;
        }
    }
    std::cout << "Projection history contract: complete geometry/order and map+overlay pixels match; "
        "dead pose cache hit, identity/revival/order and all live signature fields verified; "
        "all original dead-structure fields and " << structuralCases
        << " same-tick rubble/selling paint and visible-hit cases verified.\n";
    return true;
}
