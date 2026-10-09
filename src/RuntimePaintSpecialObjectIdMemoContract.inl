// Renderer-only contracts. The pointer oracle is an independent original
// first-match loop, and the pixel oracle calls the complete production renderer.
static bool runRuntimePaintSpecialObjectIdIdentityContract(std::string& error) {
    const auto fail = [&](const char* text) { error = std::string("paint special object ID: ") + text; return false; };
    struct Mask {
        RuntimeGeometryQueryFrame* previous = RuntimeGeometryQueryFrame::active;
        Mask() { RuntimeGeometryQueryFrame::active = nullptr; }
        ~Mask() { RuntimeGeometryQueryFrame::active = previous; }
    } mask;
    RuntimePaintGeometryReferenceScope geometryReference(false);
    RuntimePaintSpecialObjectIdReferenceScope optimized(false);
    GameWorld world;
    const auto make = [](int id, bool alive) {
        WorldObjectInstance object; object.instanceId = id; object.type = "MTNK";
        object.kind = WorldObjectKind::Unit; object.alive = alive;
        object.health = alive ? 100 : 0; object.owner = "Observer"; object.mission = "Guard";
        return object;
    };
    // Negative/duplicate IDs deliberately test the old helper's general return
    // semantics; they are not claimed to be accepted by real save/load codecs.
    world.accessObjects() = {make(0, true), make(-7, false), make(40, false), make(5, true)};
    for (int i = 0; i != 10000; ++i) world.accessObjects().push_back(make(100 + i, false));
    world.accessObjects().push_back(make(40, true));
    world.accessObjects().push_back(make(20042, true));
    const auto original = [](const GameWorld& source, int id) -> const WorldObjectInstance* {
        for (const auto& object : source.getObjects()) if (object.instanceId == id) return &object;
        return nullptr;
    };
    std::uint64_t checks = 0;
    const auto check = [&](const GameWorld& source, int id) {
        ++checks; return findRuntimeSpecialRenderObject(source, id) == original(source, id);
    };
    RuntimeMapRenderContext context; context.stableRenderConfiguration = true;
    context.animationTick = 42; context.visionObserverOwner = "Observer";
    RuntimeMapCamera camera; camera.initialized = true;
    RuntimeMapRenderCache cache;
    const RECT viewport{0, 0, 800, 600};
    {
        RuntimePaintSpecialObjectIdObservation observation;
        RuntimePaintSpecialObjectIdObservationScope observe(observation);
        if (!check(world, 20042) || !check(world, -1) || observation.scalarQueries != 2
            || observation.hits || observation.scopeFallbacks != 2) return fail("no-scope original fallback");
    }
    {
        RuntimePaintSpecialObjectIdObservation observation;
        RuntimePaintSpecialObjectIdObservationScope observe(observation);
        RuntimeGeometryQueryFrame outer(world, viewport, context, camera, cache, false);
        for (int repeat = 0; repeat != 3; ++repeat)
            for (int id : {0, -7, 40, 5, -1, 999999, 20042}) if (!check(world, id)) return fail("original pointer identity");
        if (observation.queries != 21 || observation.scalarQueries != 7 || observation.hits != 14
            || observation.negativeQueries != 6 || observation.negativeHits != 4 || observation.missingHits != 4
            || outer.specialObjectIds.size() != 7
            || findRuntimeSpecialRenderObject(world, 40) != &world.getObjects()[2])
            return fail("dead/negative/null/first-ID positive witnesses");
        const auto beforeHits = observation.hits;
        {
            RuntimeGeometryQueryFrame nested(world, viewport, context, camera, cache, false);
            if (nested.memoOwner != &outer || !check(world, 20042) || !check(world, 42))
                return fail("compatible nested exact owner");
        }
        if (!check(world, 42) || observation.hits != beforeHits + 2 || outer.specialObjectIds.size() != 8)
            return fail("nested publication did not remain with outer owner");
        const auto beforeScalar = observation.scalarQueries;
        {
            auto foreignContext = context; foreignContext.visionObserverOwner = "Foreign";
            RuntimeGeometryQueryFrame incompatible(world, viewport, foreignContext, camera, cache, false);
            if (incompatible.memoOwner != &incompatible || !check(world, 20042)
                || incompatible.specialObjectIds.size()) return fail("incompatible nested fallback");
        }
        GameWorld foreign; foreign.accessObjects() = {make(20042, false)};
        if (!check(foreign, 20042)) return fail("foreign world without a nested frame");
        {
            RuntimeGeometryQueryFrame nestedForeign(foreign, viewport, context, camera, cache, false);
            if (!check(world, 20042) || !check(foreign, 20042) || nestedForeign.specialObjectIds.size())
                return fail("foreign nested old pointer identity");
        }
        if (observation.scalarQueries != beforeScalar + 4 || !check(world, 20042))
            return fail("outer owner restored after foreign queries");
        const auto referenceScalar = observation.scalarQueries;
        const auto referenceHits = observation.hits;
        {
            RuntimePaintSpecialObjectIdReferenceScope reference(true);
            RuntimeGeometryQueryFrame nestedReference(world, viewport, context, camera, cache, false);
            if (!check(world, 20042) || observation.scalarQueries != referenceScalar + 1
                || observation.hits != referenceHits) return fail("nested reference bypasses shared owner");
        }
        if (!check(world, 20042) || observation.hits != referenceHits + 1)
            return fail("nested reference restores optimized outer owner");
    }
    {
        RuntimePaintSpecialObjectIdObservation observation;
        RuntimePaintSpecialObjectIdObservationScope observe(observation);
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        for (std::size_t i = 0; i != RuntimePaintSpecialObjectIdMemo::capacity; ++i)
            if (!check(world, 200000 + static_cast<int>(i))) return fail("capacity fill missing identity");
        if (!check(world, -9999) || !check(world, -9999) || !check(world, 200000)
            || batch.specialObjectIds.size() != RuntimePaintSpecialObjectIdMemo::capacity
            || observation.capacityFallbacks != 2 || observation.scalarQueries != 66 || observation.hits != 1)
            return fail("bounded full memo preserves old fallback and existing hits");
    }
    {
        RuntimePaintSpecialObjectIdReferenceScope reference(true);
        RuntimePaintSpecialObjectIdObservation observation;
        RuntimePaintSpecialObjectIdObservationScope observe(observation);
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        if (!check(world, 20042) || !check(world, 20042) || observation.hits
            || observation.scalarQueries != 2 || batch.specialObjectIds.size()) return fail("independent original reference");
    }
    {
        RuntimePaintGeometryReferenceScope masterReference(true);
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        if (!check(world, -7) || batch.specialObjectIds.size()) return fail("geometry reference original fallback");
    }
    // An in-place mutation is permitted only after explicit invalidation, or
    // outside the paint scope. It is never inferred from pointer/count/tick.
    {
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        if (!check(world, 5)) return fail("pre-invalidate pointer");
        batch.specialObjectIds.invalidate();
        world.accessObjects()[3].instanceId = 6;
        if (!check(world, 5) || !check(world, 6) || batch.specialObjectIds.size())
            return fail("explicit in-place invalidation");
    }
    world.accessObjects().back().instanceId = 20043;
    {
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        if (!check(world, 20043) || batch.specialObjectIds.size() != 1)
            return fail("pre-replacement pointer");
        // Equal size is not a lifetime proof: a distinct live buffer must revoke
        // the memo before it could return a pointer into the retired allocation.
        auto replacement = world.getObjects();
        const auto* oldData = world.getObjects().data();
        if (replacement.data() == oldData || replacement.size() != world.getObjects().size())
            return fail("same-count replacement witness");
        replacement.back().instanceId = 20044;
        world.accessObjects().swap(replacement);
        if (!check(world, 20043) || !check(world, 20044) || batch.specialObjectIds.size())
            return fail("data-only structural change defensive fallback");
    }
    {
        RuntimeGeometryQueryFrame batch(world, viewport, context, camera, cache, false);
        if (!check(world, 20042) || !check(world, 20044) || batch.specialObjectIds.size() != 2)
            return fail("new paint does not reuse previous IDs or missing state");
        // Defensive count changes revoke the lease permanently even if the
        // vector happens not to relocate. No pointer in a revoked memo is read.
        world.accessObjects().push_back(make(24680, true));
        if (!check(world, 20044) || !check(world, 24680) || batch.specialObjectIds.size())
            return fail("structural change defensive fallback");
    }
    std::cout << "Paint special object ID identity: independent first-match pointers checks=" << checks
        << " history=10000 capacity=64 dead/negative/missing/duplicate/nested/foreign/invalidate exact passed=yes\n";
    return true;
}

static bool runRuntimePaintSpecialObjectIdMemoContractCheck(
    const RuntimeMapRenderContext& source, std::string& error) {
    if (!runRuntimePaintSpecialObjectIdIdentityContract(error)) return false;
    const auto fail = [&](const std::string& text) { error = "paint special object ID actual DIB: " + text; return false; };
    if (!source.rules || !source.art || !source.theater) return fail("stock context missing");
    struct Mask {
        RuntimeGeometryQueryFrame* previous = RuntimeGeometryQueryFrame::active;
        Mask() { RuntimeGeometryQueryFrame::active = nullptr; }
        ~Mask() { RuntimeGeometryQueryFrame::active = previous; }
    } mask;
    RuntimePaintGeometryReferenceScope geometryReference(false);
    constexpr int width = 800, height = 600;
    constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    const RECT viewport{0, 0, width, height};
    HDC dc = CreateCompatibleDC(nullptr); BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0) : nullptr;
    if (!dc || !bitmap || !raw) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); return fail("DIB allocation"); }
    HGDIOBJ old = SelectObject(dc, bitmap);
    struct Cleanup { HDC dc; HBITMAP bitmap; HGDIOBJ old;
        ~Cleanup() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, old};
    auto* pixels = static_cast<std::uint32_t*>(raw);
    std::uint64_t pairs = 0, hits = 0, missingHits = 0, negativeHits = 0, avoidedScans = 0;
    for (int scene = 0; scene != 4; ++scene) {
        IniFile ini; MapDatabase map; GameWorld world;
        if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
            || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("original map loader fixture");
        if (world.getTerrainCells().empty()) for (int y = 0; y != 32; ++y) for (int x = 0; x != 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            world.accessTerrainCells().push_back(cell);
        }
        const auto make = [](int id, const char* type, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object; object.instanceId = id; object.type = type; object.kind = kind;
            object.owner = "Observer"; object.alive = true; object.health = 1000; object.mission = "Guard";
            object.x = x; object.y = y; object.facing = 64; object.turretFacing = 128; return object;
        };
        world.accessObjects() = {make(1, "MTNK", WorldObjectKind::Unit, 8, 8),
            make(2, "E1", WorldObjectKind::Infantry, 10, 8),
            make(3, "GAPOWR", WorldObjectKind::Structure, 11, 10),
            make(4, "MTNK", WorldObjectKind::Unit, 8, 10)};
        world.accessObjects()[3].alive = false; world.accessObjects()[3].health = 0;
        if (scene == 1) for (int n = 0; n != 10000; ++n) {
            auto dead = make(100 + n, "MTNK", WorldObjectKind::Unit, n % 30, n / 30 % 30);
            dead.alive = false; dead.health = 0; world.accessObjects().push_back(std::move(dead));
        }
        if (scene == 2) {
            auto& selling = world.accessObjects()[2]; selling.alive = false; selling.health = 0;
            selling.mission = "Sold"; selling.buildAnimTicksTotal = 12; selling.buildAnimTicksRemaining = 8;
        }
        std::vector<SpecialMechanismRenderSnapshot> snapshots;
        const std::array<std::pair<int, int>, 4> requests{{{1, 2}, {4, 2}, {1, 999999}, {-1, 2}}};
        for (int repeat = 0; repeat != 4; ++repeat) {
            for (const auto& ids : requests) {
                SpecialMechanismRenderSnapshot snapshot; snapshot.kind = "mindControl";
                snapshot.instanceId = static_cast<int>(snapshots.size()) + 1;
                snapshot.objectId = ids.first; snapshot.targetObjectId = ids.second;
                snapshot.resourceId = "PSYCON"; snapshot.owner = "Observer";
                snapshot.requiredProgress = 100; snapshot.progress = repeat;
                snapshots.push_back(std::move(snapshot));
            }
        }
        RuntimeVisionSystem vision;
        const std::vector<RuntimeVisionObserverDefinition> observers{{"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
        if (scene == 3) {
            world.accessObjects()[0].owner = "Enemy"; world.accessObjects()[2].owner = "Enemy";
            (void)vision.update(world, source.rules, observers, 1);
            world.accessObjects()[1].x = 1; world.accessObjects()[1].y = 1;
            world.accessObjects()[0].x = 25; world.accessObjects()[0].y = 25;
            (void)vision.update(world, source.rules, observers, 2);
            if (vision.visibilityFor("Observer", 25, 25) != RuntimeCellVisibility::Shrouded
                || vision.visibilityFor("Observer", 11, 10) != RuntimeCellVisibility::Fogged)
                return fail("real shroud/fog witness missing");
        } else (void)vision.update(world, source.rules, observers, 42);
        RuntimeMapRenderContext context = source;
        context.stableRenderConfiguration = true; context.animationTick = 42; context.fixedTickHz = 30;
        context.vision = &vision; context.visionObserverOwner = "Observer"; context.mapFacts = nullptr;
        context.projectileActors = nullptr; context.impactActors = nullptr; context.wrecks = nullptr;
        context.buildingCollapses = nullptr; context.specialMechanismSnapshots = &snapshots;
        context.gateSnapshots = nullptr; context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr;
        context.localLightSnapshots = nullptr; context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr;
        context.uiResources = nullptr; context.lightingTint = nullptr; context.lightingProfile = nullptr;
        context.detailStatsEnabled = false; context.coarseStageTimingEnabled = false;
        context.effectOnlyProbe = false; context.shadowsEnabled = true; context.captureBuildingDepthHitSpans = false;
        context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
        RuntimeMapCamera camera; camera.initialized = true; camera.worldOffsetX = -width / 2; camera.worldOffsetY = -80;
        RuntimeMapRenderCache reference, optimized;
        for (int pass = 0; pass != 3; ++pass) {
            const auto revision = vision.revisionFor("Observer");
            RuntimeMapRenderStats expectedStats; RuntimePaintSpecialObjectIdObservation expectedObservation;
            std::vector<std::uint32_t> expectedPixels;
            for (bool fast : {false, true}) {
                RuntimePaintSpecialObjectIdReferenceScope mode(!fast);
                RuntimePaintSpecialObjectIdObservation observation;
                RuntimePaintSpecialObjectIdObservationScope observe(observation);
                auto& cache = fast ? optimized : reference; auto drawCamera = camera;
                // Cold stand-alone map, then two warm maps nested in the same
                // public paint lease used by real WM_PAINT. Neither scope may
                // leak cached object pointers into the following paint.
                std::optional<RuntimeGeometryQueryScope> outer;
                if (pass != 0) outer.emplace(world, viewport, context, drawCamera, cache, false);
                GdiFlush(); std::fill_n(pixels, pixelCount, 0xA53B7091U);
                const auto stats = drawRuntimeMap(dc, viewport, world, context, drawCamera, cache);
                GdiFlush();
                if (!stats.rendered) return fail("production map not rendered");
                if (!fast) {
                    expectedStats = stats; expectedObservation = observation;
                    expectedPixels.assign(pixels, pixels + pixelCount);
                    if (scene == 0 && (!stats.visibleVoxelBodies || !stats.specialMechanismRenderActors))
                        return fail("actual VXL/special-status producer did not run");
                    continue;
                }
                std::string field;
                if (!std::equal(expectedPixels.begin(), expectedPixels.end(), pixels)
                    || !runtimeGroundShadowContractStatsEqual(expectedStats, stats, field)
                    || !runtimeGroundShadowContractCacheEqual(reference, optimized)
                    || vision.revisionFor("Observer") != revision)
                    return fail("scene=" + std::to_string(scene) + " pass=" + std::to_string(pass) + " raw32/stats/cache/vision " + field);
                if (expectedObservation.hits || expectedObservation.scalarQueries != expectedObservation.queries
                    || observation.queries != expectedObservation.queries
                    || observation.negativeQueries != expectedObservation.negativeQueries
                    || !observation.hits || !observation.missingHits || !observation.negativeHits
                    || observation.scannedObjects >= expectedObservation.scannedObjects)
                    return fail("actual original scan/missing/negative/hit witnesses absent");
                ++pairs; hits += observation.hits; missingHits += observation.missingHits;
                negativeHits += observation.negativeHits;
                avoidedScans += expectedObservation.scannedObjects - observation.scannedObjects;
            }
        }
    }
    if (pairs != 12) return fail("complete cold/warm stock pair count");
    std::cout << "Paint special object ID memo: stockRaw32Pairs=" << pairs
        << " hits=" << hits << " missingHits=" << missingHits << " negativeHits=" << negativeHits
        << " avoidedScannedObjects=" << avoidedScans
        << " original353Stats/resources/vision=exact passed=yes\n";
    return true;
}
