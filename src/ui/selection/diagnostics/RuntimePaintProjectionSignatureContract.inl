// Included by the existing frame-overlay smoke after its projection-history
// contract. All probes are private; no map/world authority clock is fabricated.
#include "ui/selection/diagnostics/RuntimePaintProjectionSignatureContractOracle.h"

static bool runRuntimePaintProjectionSignatureContract(RuntimeWindowState& source,
    const RECT& clientRect, std::string& error) {
    const auto fail = [&](const std::string& text) {
        error = "paint projection signature: " + text; return false;
    };
    struct Mask {
        RuntimePaintProjectionSignatureScope* previous = RuntimePaintProjectionSignatureScope::active;
        Mask() { RuntimePaintProjectionSignatureScope::active = nullptr; }
        ~Mask() { RuntimePaintProjectionSignatureScope::active = previous; }
    } mask;
    RuntimeHistoryQueryReferenceScope history(false);
    RuntimePaintProjectionSignatureReferenceScope optimized(false);
    auto memory = std::make_unique<RuntimeWindowState>();
    auto& state = *memory;
    seedRuntimeLoadScratch(state, source, false);
    state.economy = source.economy;
    state.tick = source.tick;
    state.visionEnabled = false;
    if (source.world.getObjects().size() != 2) return fail("two actual stock fixture bodies required");
    state.world.accessObjects() = source.world.getObjects();
    refreshRuntimeGateRenderSnapshots(state);
    refreshRuntimeSpecialMechanismRenderSnapshots(state);
    auto live = state.world.getObjects()[0];
    live.hasRenderPosition = live.renderMoveActive = true;
    live.renderX = live.x; live.renderY = live.y; live.renderLevel = live.level;
    live.bodyFacingClock = {256, 1536, 256, 1, state.tick, 1};
    live.turretFacingClock = {512, 2048, 256, 2, state.tick, 2};
    state.world.accessObjects()[0] = live;
    std::uint64_t boundaryChecks = 0, fieldChecks = 0;
    const auto original = [&](const RuntimeWindowState& s) { return runtimeProjectedObjectsSignatureUnmemoized(s); };
    const auto pair = [&]() {
        const auto expected = original(state);
        return runtimeProjectedObjectsSignatureForPaintCapture(state) == expected
            && runtimeProjectedObjectsSignature(state) == expected;
    };
    {
        RuntimeHistoryQueryReferenceScope originalHistory(true);
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (!pair() || observation.hits != 1 || observation.scalarQueries != 1)
            return fail("complete original-history algorithm retains exact scoped reuse");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        if (!pair() || observation.hits || observation.scalarQueries != 2) return fail("no lease scalar path");
        ++boundaryChecks;
    }
    for (bool disabled : {false, true}) {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, !disabled);
        if (!pair() || runtimeProjectedObjectsSignature(state) != original(state)
            || observation.hits != (disabled ? 0 : 1)
            || observation.scalarQueries != (disabled ? 3 : 2)) return fail("positive single-consumption/default-false lease");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureReferenceScope reference(true);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (!pair() || observation.hits || observation.published || observation.scalarQueries != 2)
            return fail("independent original reference");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        // Ordinary projection calls cannot prime a result, even inside a lease.
        if (runtimeProjectedObjectsSignature(state) != original(state)
            || runtimeProjectedObjectsSignature(state) != original(state)
            || observation.hits || observation.published || observation.scalarQueries != 2)
            return fail("capture-short-circuit does not introduce generic UI memo");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        RuntimePaintProjectionSignatureReferenceScope changedAfterEntry(true);
        if (!pair() || observation.hits != 1 || observation.scalarQueries != 1)
            return fail("reference route is latched only at lease entry");
        ++boundaryChecks;
    }
    {
        auto foreignMemory = std::make_unique<RuntimeWindowState>();
        auto& foreign = *foreignMemory;
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (runtimeProjectedObjectsSignatureForPaintCapture(state) != original(state)
            || runtimeProjectedObjectsSignature(foreign) != original(foreign)
            || runtimeProjectedObjectsSignatureForPaintCapture(foreign) != original(foreign)
            || runtimeProjectedObjectsSignature(state) != original(state)
            || observation.hits != 1 || observation.published != 1 || observation.scalarQueries != 3)
            return fail("foreign state cannot consume or overwrite original owner result");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (runtimeProjectedObjectsSignatureForPaintCapture(state) != original(state)) return fail("nested prime");
        {
            RuntimePaintProjectionSignatureScope nested(state, true);
            if (!pair() || observation.hits) return fail("nested lease original fallback");
        }
        if (RuntimePaintProjectionSignatureScope::active != &lease
            || runtimeProjectedObjectsSignature(state) != original(state)
            || observation.hits || observation.scalarQueries != 4 || observation.invalidations != 1)
            return fail("nested return cannot revive an unproved outer lease");
        ++boundaryChecks;
    }
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (runtimeProjectedObjectsSignatureForPaintCapture(state) != original(state)) return fail("history prime");
        RuntimeHistoryQueryReferenceScope changedHistory(true);
        if (runtimeProjectedObjectsSignature(state) != original(state) || observation.hits)
            return fail("history-reference algorithm change falls back");
        ++boundaryChecks;
    }
    const auto checkField = [&](const char* name, const auto& mutate) {
        state.world.accessObjects()[0] = live;
        const auto before = original(state);
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (runtimeProjectedObjectsSignatureForPaintCapture(state) != before) return false;
        // The lease promises immutable sources. Explicitly invalidate BEFORE
        // every same-address write, never infer a mutable epoch from size/tick.
        lease.invalidate();
        mutate(state.world.accessObjects()[0]);
        const auto expected = original(state);
        if (expected == before || runtimeProjectedObjectsSignature(state) != expected || observation.hits) {
            error = std::string("paint projection signature: changed field ") + name; return false;
        }
        ++fieldChecks; return true;
    };
#define CONCORD_PAINT_SIGNATURE_INT(field) if (!checkField(#field, [](auto& o) { ++o.field; })) return false
    CONCORD_PAINT_SIGNATURE_INT(instanceId); CONCORD_PAINT_SIGNATURE_INT(facing); CONCORD_PAINT_SIGNATURE_INT(turretFacing);
    CONCORD_PAINT_SIGNATURE_INT(health); CONCORD_PAINT_SIGNATURE_INT(activeGunnerTurretIndex); CONCORD_PAINT_SIGNATURE_INT(dockedToObjectId);
    CONCORD_PAINT_SIGNATURE_INT(buildAnimTicksRemaining); CONCORD_PAINT_SIGNATURE_INT(buildAnimTicksTotal);
    CONCORD_PAINT_SIGNATURE_INT(powerAnimationPausedAtTick); CONCORD_PAINT_SIGNATURE_INT(powerAnimationPausedTicks);
    CONCORD_PAINT_SIGNATURE_INT(structureRecoilStartTick); CONCORD_PAINT_SIGNATURE_INT(chargeTurretStartTick);
    CONCORD_PAINT_SIGNATURE_INT(factoryExitHostId); CONCORD_PAINT_SIGNATURE_INT(factoryExitAnimationStartTick);
    CONCORD_PAINT_SIGNATURE_INT(factoryExitAnimationEndTick); CONCORD_PAINT_SIGNATURE_INT(tankBunkerHostId);
    CONCORD_PAINT_SIGNATURE_INT(tankBunkerOccupantId); CONCORD_PAINT_SIGNATURE_INT(tankBunkerAnimationStartTick);
    CONCORD_PAINT_SIGNATURE_INT(cloakCooldownTicks); CONCORD_PAINT_SIGNATURE_INT(submergeCooldownTicks);
    CONCORD_PAINT_SIGNATURE_INT(currentAmmo); CONCORD_PAINT_SIGNATURE_INT(deployStateTicksRemaining);
    CONCORD_PAINT_SIGNATURE_INT(specialCashNextTick); CONCORD_PAINT_SIGNATURE_INT(specialCloneDeliveryObjectId);
    CONCORD_PAINT_SIGNATURE_INT(proneTicksRemaining); CONCORD_PAINT_SIGNATURE_INT(weaponCooldownTicksRemaining);
    CONCORD_PAINT_SIGNATURE_INT(x); CONCORD_PAINT_SIGNATURE_INT(y); CONCORD_PAINT_SIGNATURE_INT(level); CONCORD_PAINT_SIGNATURE_INT(subCell);
    CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.initial); CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.target);
    CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.rate); CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.elapsedNumerator);
    CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.lastTick); CONCORD_PAINT_SIGNATURE_INT(bodyFacingClock.lastFacing);
    CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.initial); CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.target);
    CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.rate); CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.elapsedNumerator);
    CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.lastTick); CONCORD_PAINT_SIGNATURE_INT(turretFacingClock.lastFacing);
#undef CONCORD_PAINT_SIGNATURE_INT
#define CONCORD_PAINT_SIGNATURE_BOOL(field) if (!checkField(#field, [](auto& o) { o.field = !o.field; })) return false
    CONCORD_PAINT_SIGNATURE_BOOL(deployed); CONCORD_PAINT_SIGNATURE_BOOL(workAnimationActive); CONCORD_PAINT_SIGNATURE_BOOL(poweredOperational);
    CONCORD_PAINT_SIGNATURE_BOOL(factoryExitAirborne); CONCORD_PAINT_SIGNATURE_BOOL(tankBunkerLowering);
    CONCORD_PAINT_SIGNATURE_BOOL(submerged); CONCORD_PAINT_SIGNATURE_BOOL(cloaked); CONCORD_PAINT_SIGNATURE_BOOL(airborne);
    CONCORD_PAINT_SIGNATURE_BOOL(prone); CONCORD_PAINT_SIGNATURE_BOOL(alive); CONCORD_PAINT_SIGNATURE_BOOL(onBridge);
    CONCORD_PAINT_SIGNATURE_BOOL(hasRenderPosition); CONCORD_PAINT_SIGNATURE_BOOL(renderMoveActive);
    CONCORD_PAINT_SIGNATURE_BOOL(renderOnBridge);
#undef CONCORD_PAINT_SIGNATURE_BOOL
    if (!checkField("owner", [](auto& o) { o.owner += "_changed"; })
        || !checkField("kind", [](auto& o) { o.kind = WorldObjectKind::Infantry; })
        || !checkField("type", [](auto& o) { o.type += "_changed"; })
        || !checkField("mission", [](auto& o) { o.mission += "_changed"; })
        || !checkField("movementAnimationDistance", [](auto& o) { o.movementAnimationDistance += 0.5; })
        || !checkField("renderX", [](auto& o) { o.renderX += 0.5; })
        || !checkField("renderY", [](auto& o) { o.renderY += 0.5; })
        || !checkField("renderLevel", [](auto& o) { o.renderLevel += 0.5; })) return false;
    state.world.accessObjects()[0] = live;
    auto dead = live; dead.instanceId += 100; dead.alive = false; dead.health = 0;
    state.world.accessObjects().push_back(dead);
    auto secondDead = dead; ++secondDead.instanceId; state.world.accessObjects().push_back(secondDead);
    const auto fixture = state.world.getObjects();
    const auto stateChange = [&](const char* name, const auto& change, bool mustChange) {
        const auto before = original(state);
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        if (runtimeProjectedObjectsSignatureForPaintCapture(state) != before) return false;
        lease.invalidate(); change();
        const auto expected = original(state);
        if ((expected != before) != mustChange || runtimeProjectedObjectsSignature(state) != expected || observation.hits) {
            error = std::string("paint projection signature: state boundary ") + name; return false;
        }
        ++boundaryChecks; return true;
    };
    if (!stateChange("dead ignored pose", [&] { state.world.accessObjects()[2].facing += 8; }, false)) return false;
    state.world.accessObjects() = fixture;
    if (!stateChange("dead revival", [&] { state.world.accessObjects()[2].alive = true; }, true)) return false;
    state.world.accessObjects() = fixture;
    if (!stateChange("dead identity", [&] { ++state.world.accessObjects()[2].instanceId; }, true)) return false;
    state.world.accessObjects() = fixture;
    if (!stateChange("dead structure kind", [&] { state.world.accessObjects()[2].kind = WorldObjectKind::Structure; }, true)) return false;
    state.world.accessObjects() = fixture;
    if (!stateChange("ordered dead markers", [&] { std::swap(state.world.accessObjects()[2], state.world.accessObjects()[3]); }, true)) return false;
    state.world.accessObjects() = fixture;
    if (!stateChange("same-count live content", [&] { ++state.world.accessObjects()[0].health; }, true)) return false;
    state.world.accessObjects() = fixture;
    {
        RuntimePaintProjectionSignatureObservation observation;
        RuntimePaintProjectionSignatureObservationScope observe(observation);
        RuntimePaintProjectionSignatureScope lease(state, true);
        (void)runtimeProjectedObjectsSignatureForPaintCapture(state);
        auto replacement = state.world.getObjects(); // allocation precedes swap
        replacement[0].health += 2;
        const auto* prior = state.world.getObjects().data();
        state.world.accessObjects().swap(replacement);
        if (state.world.getObjects().data() == prior
            || runtimeProjectedObjectsSignature(state) != original(state) || observation.hits || observation.invalidations != 1)
            return fail("same-count different-buffer structural fallback");
        ++boundaryChecks;
    }
    state.world.accessObjects() = fixture;
    {
        RuntimePaintProjectionSignatureScope lease(state, true);
        (void)runtimeProjectedObjectsSignatureForPaintCapture(state);
        state.world.accessObjects().push_back(dead);
        if (runtimeProjectedObjectsSignature(state) != original(state)) return fail("count-change structural fallback");
        ++boundaryChecks;
    }
    state.world.accessObjects() = fixture;
    if (!stateChange("tick", [&] { ++state.tick; }, true)) return false;
    if (!stateChange("owner same-storage", [&] { state.economy.owner[0] = state.economy.owner[0] == 'A' ? 'B' : 'A'; }, true)) return false;
    if (!stateChange("cheer", [&] { state.playerCommunication.cheerUntilTick[fixture[0].instanceId] = state.tick + 20; }, true)) return false;
    if (!stateChange("impact", [&] { RuntimeImpactEffect impact; impact.cell = {live.x, live.y}; impact.damage = 12;
            impact.remainingTicks = impact.totalTicks = 6; impact.warheadId = "HE"; state.impactEffects.push_back(impact); }, true)) return false;
    if (!stateChange("impact same-storage", [&] { ++state.impactEffects.back().damage; }, true)) return false;
    WorldTerrainObject terrainObject; terrainObject.mapId = "PaintSignature"; terrainObject.type = "TREE01";
    terrainObject.x = live.x; terrainObject.y = live.y;
    if (!stateChange("terrain object", [&] { state.world.accessTerrainObjects().push_back(terrainObject); }, true)) return false;
    if (!stateChange("terrain same-storage", [&] { ++state.world.accessTerrainObjects().back().health; }, true)) return false;
    if (!stateChange("special initialized", [&] { state.specialMechanismsInitialized = !state.specialMechanismsInitialized; }, true)) return false;
    if (!stateChange("gates initialized", [&] { state.gatesInitialized = !state.gatesInitialized; }, true)) return false;
    const auto fresh = original(state);
    { RuntimePaintProjectionSignatureScope nextPaint(state, true); if (!pair()) return fail("new paint complete current signature"); }
    if (runtimeProjectedObjectsSignature(state) != fresh) return fail("scope destruction leaves no reused result");
    ++boundaryChecks;

    // Actual complete map + interaction draw. Each route has independent owned
    // UI/render caches, exactly the production snapshot preparation, and the
    // original capture predicate. Warm paint primes from its real prior cache.
    const RECT viewport = runtimeMapViewport(clientRect);
    const int width = clientRect.right - clientRect.left, height = clientRect.bottom - clientRect.top;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    struct Screen { HDC dc = GetDC(nullptr); ~Screen() { if (dc) ReleaseDC(nullptr, dc); } } screen;
    if (!screen.dc) return fail("actual DIB screen DC");
    std::uint64_t pairs = 0, actualHits = 0, actualScalarSavings = 0;
    for (int scene = 0; scene != 4; ++scene) {
        std::array<std::unique_ptr<RuntimeWindowState>, 2> probes;
        for (auto& target : probes) {
            target = std::make_unique<RuntimeWindowState>(); auto& s = *target;
            seedRuntimeLoadScratch(s, source, false); s.economy = source.economy; s.tick = source.tick;
            s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
            s.runtimeSoundsEnabled = false; s.world.accessObjects() = source.world.getObjects();
            s.selectedObjectIds = {s.world.getObjects()[0].instanceId}; s.mouseClientPointValid = false;
            s.visionEnabled = false; s.camera = source.camera;
            if (scene == 1) for (int n = 0; n != 10000; ++n) {
                auto object = s.world.getObjects()[0]; object.instanceId = 900000 + n;
                object.alive = false; object.health = 0; object.mission = "Destroyed";
                s.world.accessObjects().push_back(std::move(object));
            }
            if (scene == 2) {
                auto selling = s.world.getObjects()[1]; selling.instanceId += 20000;
                selling.alive = false; selling.health = 0; selling.mission = "Sold";
                selling.buildAnimTicksTotal = 24; selling.buildAnimTicksRemaining = 12;
                s.world.accessObjects().push_back(std::move(selling));
            }
            if (scene == 3) {
                s.visionEnabled = true; s.world.accessObjects()[1].owner = "Enemy";
                const std::vector<RuntimeVisionObserverDefinition> observers{
                    {s.economy.owner, {s.economy.owner}}, {"Enemy", {"Enemy"}}};
                (void)s.vision.update(s.world, &s.rules, observers, s.tick);
                const auto& cells = s.world.getTerrainCells();
                if (cells.empty()) return fail("real visibility terrain missing");
                const auto originalCell = RuntimeCellPoint{s.world.getObjects()[0].x, s.world.getObjects()[0].y};
                const auto buildingCell = RuntimeCellPoint{s.world.getObjects()[1].x, s.world.getObjects()[1].y};
                const auto distance = [](int x, int y, const RuntimeCellPoint& point) {
                    return std::abs(static_cast<long long>(x) - point.x) + std::abs(static_cast<long long>(y) - point.y);
                };
                const auto farthestTerrainCell = std::max_element(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
                    return distance(a.x, a.y, buildingCell) < distance(b.x, b.y, buildingCell);
                });
                auto& scout = s.world.accessObjects()[0]; scout.x = farthestTerrainCell->x; scout.y = farthestTerrainCell->y;
                scout.level = farthestTerrainCell->level; scout.cell = runtimeCellKey(scout.x, scout.y);
                scout.hasRenderPosition = scout.renderMoveActive = false;
                const auto scoutCell = RuntimeCellPoint{scout.x, scout.y};
                const auto hiddenCell = std::max_element(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
                    return std::min(distance(a.x, a.y, originalCell), distance(a.x, a.y, scoutCell))
                        < std::min(distance(b.x, b.y, originalCell), distance(b.x, b.y, scoutCell));
                });
                auto enemy = s.world.getObjects()[0]; enemy.instanceId += 30000; enemy.owner = "Enemy";
                enemy.x = hiddenCell->x; enemy.y = hiddenCell->y; enemy.level = hiddenCell->level;
                enemy.cell = runtimeCellKey(enemy.x, enemy.y); s.world.accessObjects().push_back(std::move(enemy));
                (void)s.vision.update(s.world, &s.rules, observers, s.tick + 1);
                const auto& building = s.world.getObjects()[1]; const auto& hidden = s.world.getObjects()[2];
                if (s.vision.visibilityFor(s.economy.owner, building.x, building.y) != RuntimeCellVisibility::Fogged
                    || s.vision.visibilityFor(s.economy.owner, hidden.x, hidden.y) != RuntimeCellVisibility::Shrouded)
                    return fail("real fog/shroud fixture witness");
            }
            refreshRuntimeGateRenderSnapshots(s); refreshRuntimeSpecialMechanismRenderSnapshots(s);
            s.renderCache = {}; s.projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(s);
            if (!ensureRuntimeBackBuffer(s.backBuffer, screen.dc, width, height)) return fail("actual owned top-down DIB");
        }
        for (int pass = 0; pass != 3; ++pass) {
            std::array<RuntimeMapRenderStats, 2> stats;
            std::array<RuntimePaintProjectionSignatureObservation, 2> observations;
            std::array<std::vector<std::uint32_t>, 2> pixels;
            std::array<std::vector<std::size_t>, 2> objectIndices;
            std::array<unsigned long long, 2> signatures{};
            for (std::size_t route = 0; route != 2; ++route) {
                auto& s = *probes[route]; ++s.tick;
                RuntimeSnapshotActiveStateScope snapshotScope(s);
                ensureRuntimeCameraForViewport(s, viewport);
                RuntimeMapRenderContext context;
                context.theater = s.theaterLoaded ? &s.theater : nullptr;
                context.art = s.artLoaded ? &s.art : nullptr; context.rules = &s.rules;
                context.mapFacts = s.mapFactsLoaded ? &s.mapFacts : nullptr;
                context.assetRootPath = runtimeAssetRootPath(s.config);
                context.tileRootPath = runtimeTileRootPath(s.config, s.report.map);
                context.palettePath = runtimePalettePath(s.config, s.report.map);
                context.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
                context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
                context.ownerRemapColors = &s.ownerRemapColors; context.neutralRemapOwners = &s.neutralRemapOwners;
                if (runtimeLocalVisionRestricted(s)) { context.vision = &s.vision; context.visionObserverOwner = s.economy.owner; }
                if (scene == 3 && !context.vision) return fail("actual owner vision was not enabled for shrouded draw");
                const auto* lighting = &s.lighting.currentProfile(); auto tint = s.lighting.renderTint();
                context.lightingProfile = lighting; context.lightingTint = &tint;
                refreshRuntimeParticleRenderSnapshots(s, context);
                context.projectileActors = runtimeProjectileRenderActorsForRender(s);
                context.impactActors = runtimeImpactRenderActorsForRender(s);
                context.wrecks = &s.wrecks; context.buildingCollapses = &s.buildingCollapses;
                refreshRuntimeSpecialMechanismRenderSnapshots(s); context.specialMechanismSnapshots = &s.specialMechanismRenderSnapshots;
                refreshRuntimeGateRenderSnapshots(s); context.gateSnapshots = &s.gateRenderSnapshots;
                context.selectedObjectIds = &s.selectedObjectIds; context.playerCheerUntilTick = &s.playerCommunication.cheerUntilTick;
                context.stableRenderConfiguration = true; context.animationTick = s.tick; context.fixedTickHz = s.config.fixedTickHz;
                context.visualDetail = s.runtimeVisualDetail; context.seeHiddenObjects = s.config.seeHiddenObjects;
                context.shadowsEnabled = s.runtimeVisualDetail >= 1;
                if (!s.uiResourceRenderSummaryValid) { s.uiResourceRenderSummary = buildRuntimeUiResourceRenderSummary(s); s.uiResourceRenderSummaryValid = true; }
                context.uiResources = &s.uiResourceRenderSummary;
                context.surfacePixels = static_cast<std::uint32_t*>(s.backBuffer.pixels);
                context.surfaceWidth = width; context.surfaceHeight = height; context.surfaceTopDownOwned = s.backBuffer.topDownDib;
                RuntimePaintProjectionSignatureReferenceScope reference(route == 0);
                RuntimePaintProjectionSignatureObservationScope observe(observations[route]);
                const auto before = original(s); const auto worldBefore = s.world.deterministicSimulationStateHash();
                const auto revision = s.vision.revisionFor(s.economy.owner);
                GdiFlush(); std::fill_n(context.surfacePixels, pixelCount, 0xA5283071U);
                SetBkMode(s.backBuffer.memoryDc, TRANSPARENT);
                std::optional<RuntimePaintProjectionSignatureScope> signatureScope;
                signatureScope.emplace(s, true);
                context.captureBuildingDepthHitSpans = runtimePaintBuildingDepthCaptureAllowed(s.world)
                    && runtimeInteractionNeedsProjectedObjects(s, viewport)
                    && (!s.projectedObjectsCacheValid || !runtimeRectEquals(s.projectedObjectsCacheViewport, viewport)
                        || s.projectedObjectsCacheCameraX != s.camera.worldOffsetX || s.projectedObjectsCacheCameraY != s.camera.worldOffsetY
                        || s.projectedObjectsCacheTileWidth != s.camera.tileWidth || s.projectedObjectsCacheTileHeight != s.camera.tileHeight
                        || s.projectedObjectsCacheSignature != runtimeProjectedObjectsSignatureForPaintCapture(s));
                RuntimeGeometryQueryScope geometry(s.world, viewport, context, s.camera, s.renderCache, false);
                stats[route] = drawRuntimeMap(s.backBuffer.memoryDc, viewport, s.world, context, s.camera, s.renderCache);
                drawRuntimeInteractionOverlay(s.backBuffer.memoryDc, clientRect, s, context);
                signatureScope.reset();
                GdiFlush(); pixels[route].assign(context.surfacePixels, context.surfacePixels + pixelCount);
                signatures[route] = s.projectedObjectsCacheSignature;
                for (const auto& object : s.projectedObjectsCache) {
                    const auto found = std::find_if(s.world.getObjects().begin(), s.world.getObjects().end(),
                        [&](const auto& candidate) { return &candidate == object.object; });
                    if (found == s.world.getObjects().end()) return fail("projection contains a foreign/stale object pointer");
                    objectIndices[route].push_back(static_cast<std::size_t>(found - s.world.getObjects().begin()));
                }
                if (!stats[route].rendered || before != original(s) || signatures[route] != before
                    || s.world.deterministicSimulationStateHash() != worldBefore || s.vision.revisionFor(s.economy.owner) != revision)
                    return fail("actual map/initial projection changed a signature input or authority");
            }
            std::string field;
            if (pixels[0] != pixels[1] || objectIndices[0] != objectIndices[1] || signatures[0] != signatures[1]
                || !runtimePaintProjectionSignatureContractRenderEqual(stats[0], stats[1], probes[0]->renderCache, probes[1]->renderCache, field))
                return fail("scene=" + std::to_string(scene) + " pass=" + std::to_string(pass) + " raw32/order/full353/deep resources " + field);
            if (observations[0].hits || observations[0].published
                || observations[0].captureQueries != observations[1].captureQueries
                || observations[0].projectionQueries != observations[1].projectionQueries
                || observations[0].scalarQueries != observations[1].scalarQueries + observations[1].hits
                || (pass != 0 && observations[1].hits != 1)) return fail("actual capture->projection positive single-reuse witness");
            if (scene == 0 && (!stats[0].visibleVoxelBodies || !probes[0]->lastUnitSelectionFrames))
                return fail("actual stock VXL body and selection were not drawn");
            actualHits += observations[1].hits;
            actualScalarSavings += observations[0].scalarQueries - observations[1].scalarQueries;
            ++pairs;
        }
    }
    if (boundaryChecks != 28 || fieldChecks != 64 || pairs != 12 || actualHits != 8 || actualScalarSavings != 8)
        return fail("actual 12-pair/eight warm capture reuse totals");
    std::cout << "Paint projection signature: boundaries=" << boundaryChecks << " liveFields=" << fieldChecks
        << " pairs=12 actualHits=8 originalComputationsOmitted=8 fullSignature/raw32/353stats/deepResources/pointerOrder/world/vision=exact passed=yes\n";
    return true;
}
