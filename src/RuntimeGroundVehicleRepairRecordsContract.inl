namespace RuntimeRepairRecordsContractDetail {
static bool recordsEqual(const std::vector<RuntimeWorldObjectDrawRecord>& a,
    const std::vector<RuntimeWorldObjectDrawRecord>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i != a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.object != y.object || x.drawLevel != y.drawLevel
            || x.ySortOverrideLeptons != y.ySortOverrideLeptons
            || x.sortPoint.x != y.sortPoint.x || x.sortPoint.y != y.sortPoint.y
            || x.center.x != y.center.x || x.center.y != y.center.y
            || x.shadowCenter.x != y.shadowCenter.x || x.shadowCenter.y != y.shadowCenter.y
            || x.cellSum != y.cellSum || x.kindBias != y.kindBias || x.instanceId != y.instanceId
            || x.cullMargin != y.cullMargin || x.repairDepotHostId != y.repairDepotHostId
            || x.footprintWidth != y.footprintWidth || x.footprintHeight != y.footprintHeight
            || x.usesVoxelArt != y.usesVoxelArt || x.aircraft != y.aircraft
            || x.airborneForeground != y.airborneForeground || x.shadowOnBridge != y.shadowOnBridge
            || x.bodyVisible != y.bodyVisible || x.hasYSortOverride != y.hasYSortOverride) return false;
    }
    return true;
}
static bool query(const WorldObjectInstance& object, const GameWorld& world,
    const RuntimeMapRenderContext& context, RuntimeGroundVehicleRepairRecordsMemo* memo,
    std::vector<RuntimeWorldObjectDrawRecord>& records) {
    records.assign(1, RuntimeWorldObjectDrawRecord{}); records.front().object = &object;
    if (!memo || !memo->append(object, world, context, records)) {
        runtimeAppendOriginalGroundVehicleRepairRecords(world, context, records);
        if (memo) memo->remember(object, world, context, records);
    }
    return records.size() > 1 && applyRuntimeRepairDepotForegroundOrder(records, context) > 0;
}
static bool primitive(GameWorld& world, const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail = [&](const char* why) { error = std::string("repair records primitive: ") + why; return false; };
    const auto& object = world.getObjects().front();
    std::vector<RuntimeWorldObjectDrawRecord> expected, actual;
    const bool oldResult = query(object, world, context, nullptr, expected);
    RuntimeRepairRecordsObservation observation;
    RuntimeRepairRecordsObservationScope observe(observation);
    RuntimeGroundVehicleRepairRecordsMemo memo(world, context, true);
    if (query(object, world, context, &memo, actual) != oldResult || !recordsEqual(expected, actual)
        || query(object, world, context, &memo, actual) != oldResult || !recordsEqual(expected, actual)
        || observation.scalarScans != 1 || observation.publications != 1 || observation.hits != 1)
        return fail("cold/hit original records/order/apply mismatch");
    const auto hits = observation.hits;
    auto projected = object; auto foreignContext = context; GameWorld foreignWorld = world;
    if (query(projected, world, context, &memo, actual) != query(projected, world, context, nullptr, expected)
        || !recordsEqual(expected, actual) || observation.hits != hits) return fail("temporary observer borrowed");
    if (query(object, world, foreignContext, &memo, actual) != query(object, world, foreignContext, nullptr, expected)
        || !recordsEqual(expected, actual) || observation.hits != hits) return fail("foreign context borrowed");
    if (query(foreignWorld.getObjects().front(), foreignWorld, context, &memo, actual)
        != query(foreignWorld.getObjects().front(), foreignWorld, context, nullptr, expected)
        || !recordsEqual(expected, actual) || observation.hits != hits) return fail("foreign world borrowed");
    {
        RuntimeGroundVehicleRepairRecordsMemo nested(world, context, true);
        const auto before = observation.scalarScans;
        const auto nestedResult = query(object, world, context, &nested, actual);
        const auto scalarResult = query(object, world, context, nullptr, expected);
        if (nestedResult != scalarResult || !recordsEqual(expected, actual)
            || observation.scalarScans != before + 2) return fail("nested scope reused outer state");
    }
    // An explicitly invalidated in-place change must be seen by the original builder.
    const auto oldType = world.getObjects().back().type;
    memo.invalidate(); world.accessObjects().back().type = "GAPOWR";
    if (query(world.getObjects().front(), world, context, &memo, actual)
        != query(world.getObjects().front(), world, context, nullptr, expected)
        || !recordsEqual(expected, actual) || observation.hits != hits) return fail("in-place invalidate stale");
    world.accessObjects().back().type = oldType;
    const auto footprint = runtimeObjectFoundationDimensions(world.getObjects().back(), context);
    const auto* building = context.rules->findBuildingType(oldType);
    if (!building || expected.size() < 1 || footprint.width <= 0) return fail("stock depot foundation absent");
    const int blocked = std::clamp(building->numberImpassableRows, 0, footprint.width);
    if (blocked >= footprint.width) return fail("stock depot has no positive platform witness");
    const int originalX = world.getObjects().front().x, originalY = world.getObjects().front().y;
    world.accessObjects().front().x = world.getObjects().back().x + blocked;
    world.accessObjects().front().y = world.getObjects().back().y;
    {
        RuntimeGroundVehicleRepairRecordsMemo platform(world, context, true);
        if (!query(world.getObjects().front(), world, context, nullptr, expected)
            || !query(world.getObjects().front(), world, context, &platform, actual)
            || !recordsEqual(expected, actual)
            || !query(world.getObjects().front(), world, context, &platform, actual)
            || !recordsEqual(expected, actual)) return fail("real depot original positive apply/order changed");
    }
    world.accessObjects().front().x = originalX; world.accessObjects().front().y = originalY;
    const auto postPlatformHits = observation.hits;
    RuntimeGroundVehicleRepairRecordsMemo disabled(world, context, false);
    const auto publications = observation.publications;
    if (query(world.getObjects().front(), world, context, &disabled, actual)
        != query(world.getObjects().front(), world, context, nullptr, expected)
        || !recordsEqual(expected, actual) || observation.publications != publications || observation.hits != postPlatformHits)
        return fail("disabled memo published/borrowed");
    auto mutableContext = context;
    RuntimeGroundVehicleRepairRecordsMemo configuration(world, mutableContext, true);
    query(world.getObjects().front(), world, mutableContext, &configuration, actual);
    const auto configHits = observation.hits;
    mutableContext.art = nullptr;
    const auto configResult = query(world.getObjects().front(), world, mutableContext, &configuration, actual);
    const auto configScalar = query(world.getObjects().front(), world, mutableContext, nullptr, expected);
    if (configResult != configScalar || !recordsEqual(expected, actual) || observation.hits != configHits)
        return fail("same context source field mutation borrowed");
    {
        RuntimeRepairRecordsReferenceScope outer(true);
        if (!runtimeRepairRecordsMemoReference()) return fail("outer reference missing");
        { RuntimeRepairRecordsReferenceScope inner(false); if (runtimeRepairRecordsMemoReference()) return fail("inner override missing"); }
        if (!runtimeRepairRecordsMemoReference()) return fail("nested reference not restored");
    }
    GameWorld oversized = world;
    for (int n = 0; n != 1025; ++n) {
        auto depot = world.getObjects().back(); depot.instanceId = 100000 + n;
        oversized.accessObjects().push_back(std::move(depot));
    }
    RuntimeGroundVehicleRepairRecordsMemo capped(oversized, context, true);
    const auto cappedHits = observation.hits, cappedPublications = observation.publications;
    for (int repeat = 0; repeat != 2; ++repeat) {
        const auto newResult = query(oversized.getObjects().front(), oversized, context, &capped, actual);
        const auto originalResult = query(oversized.getObjects().front(), oversized, context, nullptr, expected);
        if (newResult != originalResult || !recordsEqual(expected, actual)
            || observation.hits != cappedHits || observation.publications != cappedPublications)
            return fail("oversized list not scalar fallback");
    }
    // No implicit pointer/count epoch: structural replacement is permanently refused.
    GameWorld changed = world;
    RuntimeGroundVehicleRepairRecordsMemo changing(changed, context, true);
    query(changed.getObjects().front(), changed, context, &changing, actual);
    auto appended = changed.getObjects().front(); appended.instanceId = 200000;
    changed.accessObjects().push_back(appended);
    const auto structuralHits = observation.hits;
    const auto changedResult = query(changed.getObjects().front(), changed, context, &changing, actual);
    const auto changedScalar = query(changed.getObjects().front(), changed, context, nullptr, expected);
    if (changedResult != changedScalar || !recordsEqual(expected, actual) || observation.hits != structuralHits)
        return fail("structural mutation reused old list");
    return true;
}
static bool drawWorld(const GameWorld& world, const RuntimeMapRenderContext& source,
    const RuntimeMapCamera& camera, bool requireHits, std::string& error,
    std::uint64_t& pairs, std::uint64_t& savedScans) {
    using RuntimeTerrainViewportFillContractDetail::DIB;
    DIB old(800, 600), actual(800, 600);
    if (!old.valid() || !actual.valid()) { error = "repair records actual DIB allocation"; return false; }
    RuntimeMapRenderContext context = source;
    context.detailStatsEnabled = context.coarseStageTimingEnabled = false;
    context.surfaceWidth = 800; context.surfaceHeight = 600; context.surfaceTopDownOwned = true;
    RuntimeMapRenderCache reference, optimized;
    const auto authority = world.deterministicSimulationStateHash();
    for (int pass = 0; pass != 2; ++pass) {
        RuntimeMapRenderStats expectedStats; std::vector<std::uint32_t> expectedPixels;
        RuntimeRepairRecordsObservation expectedObservation;
        for (bool fast : {false, true}) {
            RuntimeRepairRecordsReferenceScope mode(!fast);
            RuntimeRepairRecordsObservation observation; RuntimeRepairRecordsObservationScope observe(observation);
            auto& dib = fast ? actual : old; dib.initialize(); context.surfacePixels = dib.pixels;
            auto drawCamera = camera;
            const auto stats = drawRuntimeMap(dib.dc, RECT{0, 0, 800, 600}, world, context,
                drawCamera, fast ? optimized : reference);
            const auto pixels = dib.copy();
            if (!stats.rendered || world.deterministicSimulationStateHash() != authority) {
                error = "repair records actual producer/authority"; return false;
            }
            if (!fast) { expectedPixels = pixels; expectedStats = stats; expectedObservation = observation; continue; }
            std::string field;
            if (pixels != expectedPixels || !runtimeGroundShadowContractStatsEqual(expectedStats, stats, field)
                || !runtimeGroundShadowContractCacheEqual(reference, optimized)
                || GetDCBrushColor(actual.dc) != GetDCBrushColor(old.dc)) {
                error = "repair records actual raw32/stats353/cache/error/DC mismatch: " + field; return false;
            }
            if (requireHits && (!observation.hits || observation.publications != 1 || observation.scalarScans != 1
                || expectedObservation.scalarScans != observation.scalarScans + observation.hits
                || expectedObservation.scannedObjects != observation.scannedObjects
                    + observation.hits * world.getObjects().size())) {
                error = "repair records actual positive one-build/saved history scan witness missing"; return false;
            }
            if (requireHits && (!expectedStats.visibleVoxelBodies || !expectedStats.visibleStructureSprites)) {
                error = "repair records actual stock body/building path absent"; return false;
            }
            savedScans += observation.hits * world.getObjects().size(); ++pairs;
        }
    }
    return true;
}
} // namespace RuntimeRepairRecordsContractDetail

bool runRuntimeGroundVehicleRepairRecordsWorldContractCheck(const GameWorld& world,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera, std::string& error) {
    std::uint64_t pairs = 0, scans = 0;
    if (!RuntimeRepairRecordsContractDetail::drawWorld(world, context, camera, false, error, pairs, scans)) return false;
    std::cout << "Repair records world: actual complete draw raw32/stats353/cache pairs=" << pairs
        << " savedObjectVisits=" << scans << " authorityHash=yes pass\n";
    return true; // The Shell caller additionally checks its complete authoritative writer bytes.
}

static bool runRuntimeGroundVehicleRepairRecordsAssetContractCheck(const RuntimeMapRenderContext& source,
    std::string& error) {
    using namespace RuntimeRepairRecordsContractDetail;
    const auto fail = [&](const char* why) { error = std::string("repair records stock: ") + why; return false; };
    if (!source.rules || !source.art || !source.theater) return fail("stock context absent");
    const auto* depot = source.rules->findBuildingType("GADEPT");
    if (!depot || !depot->unitRepair || depot->naval || depot->waterBound) return fail("real GADEPT repair rules absent");
    std::uint64_t pairs = 0, savedScans = 0;
    for (int scene = 0; scene != 6; ++scene) {
        IniFile ini; MapDatabase map; GameWorld world;
        if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
            || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("map load");
        if (world.getTerrainCells().empty()) for (int y = 0; y != 32; ++y) for (int x = 0; x != 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            world.accessTerrainCells().push_back(cell);
        }
        auto make = [](int id, const char* type, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object; object.instanceId = id; object.type = type; object.kind = kind;
            object.owner = "Observer"; object.alive = true; object.health = 1000; object.mission = "Guard";
            object.x = x; object.y = y; object.facing = 64; object.turretFacing = 128; return object;
        };
        for (int n = 0; n != 4; ++n) world.accessObjects().push_back(make(n + 1, "MTNK", WorldObjectKind::Unit, 8 + n, 8));
        world.accessObjects().push_back(make(10, "GAPOWR", WorldObjectKind::Structure, 11, 10));
        world.accessObjects().push_back(make(11, "GADEPT", WorldObjectKind::Structure, 14, 12));
        if (scene == 0 && !primitive(world, source, error)) return false;
        if (scene == 1) for (int n = 0; n != 10000; ++n) {
            auto object = make(100 + n, "MTNK", WorldObjectKind::Unit, n % 30, n / 30 % 30);
            object.alive = false; object.health = 0; world.accessObjects().push_back(std::move(object));
        }
        if (scene == 2) { auto& object = world.accessObjects()[5]; object.alive = false; object.mission = "Sold";
            object.buildAnimTicksTotal = 12; object.buildAnimTicksRemaining = 8; }
        if (scene == 3) world.accessObjects()[5].type = "GAPOWR"; // Valid empty repair list is memoized too.
        if (scene == 4) { world.accessObjects()[2].onBridge = true; world.accessObjects()[3].deployed = true; }
        MapFacts facts;
        for (const auto& cell : world.getTerrainCells()) {
            MapCellFacts fact; fact.x = cell.x; fact.y = cell.y; fact.cellIndex = cell.cellIndex512;
            fact.hasTerrain = true; fact.terrain.tileIndex = cell.tileIndex;
            fact.terrain.subTileIndex = cell.subTileIndex; fact.heightLevel = cell.level;
            if (scene == 5) fact.isRamp = true;
            facts.cellIndexLookup.emplace(fact.cellIndex, facts.cells.size()); facts.cells.push_back(std::move(fact));
        }
        RuntimeVisionSystem vision; (void)vision.update(world, source.rules, {{"Observer", {"Observer"}}}, 42);
        RuntimeMapRenderContext context = source; context.animationTick = 42; context.fixedTickHz = 30;
        context.stableRenderConfiguration = true; context.mapFacts = &facts;
        context.vision = &vision; context.visionObserverOwner = "Observer";
        context.projectileActors = nullptr; context.impactActors = nullptr; context.wrecks = nullptr;
        context.buildingCollapses = nullptr; context.specialMechanismSnapshots = nullptr; context.gateSnapshots = nullptr;
        context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
        context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr; context.uiResources = nullptr;
        context.lightingTint = nullptr; context.lightingProfile = nullptr;
        context.effectOnlyProbe = false; context.shadowsEnabled = true; context.captureBuildingDepthHitSpans = false;
        RuntimeMapCamera camera; camera.initialized = true; camera.worldOffsetX = -400; camera.worldOffsetY = -80;
        if (!drawWorld(world, context, camera, true, error, pairs, savedScans)) return false;
    }
    std::cout << "Repair records memo: actual stock raw32/stats353/cache pairs=" << pairs
        << " savedObjectVisits=" << savedScans << " positiveOriginalAndObserverFallback=yes pass\n";
    return true;
}
