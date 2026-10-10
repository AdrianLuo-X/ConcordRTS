static bool runRuntimePaintGeometryReuseContractCheck(std::string& error);
static bool runRuntimePaintGeometryReuseAssetContractCheck(const RuntimeMapRenderContext& context,
    std::vector<WorldObjectInstance> probes, std::string& error);
static bool runRuntimeEmptyBuildingDepthContractCheck(std::string& error);
static bool runRuntimeEmptyBuildingDepthAssetContractCheck(const RuntimeMapRenderContext& context,
    std::string& error);
static bool runRuntimePaintBuildingDepthReuseContractCheck(std::string& error);
static bool runRuntimePaintBuildingDepthReuseAssetContractCheck(const GameWorld& world,
    const RuntimeMapRenderContext& context, std::string& error);

static bool runtimeGeometryMetadataEqualForContract(const RuntimeGeometryTypeMetadata& left,
    const RuntimeGeometryTypeMetadata& right) {
    return left.unit == right.unit && left.art == right.art
        && left.visualTypeId == right.visualTypeId && left.artId == right.artId
        && left.spriteImageId == right.spriteImageId && left.bodyCandidates == right.bodyCandidates
        && left.turretCandidates == right.turretCandidates && left.barrelCandidates == right.barrelCandidates
        && left.footprintWidth == right.footprintWidth && left.footprintHeight == right.footprintHeight
        && left.aircraft == right.aircraft && left.turret == right.turret;
}

static RuntimeGeometryTypeMetadata runtimeGeometryMetadataReference(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context) {
    RuntimeGeometryTypeMetadata result;
    result.unit = findObjectUnitTypeForRender(context, object);
    result.art = findArtTypeForObject(object, context);
    result.visualTypeId = resolveObjectVisualTypeIdForRender(object, context);
    result.artId = resolveObjectArtId(object, context);
    result.spriteImageId = resolveObjectSpriteImageId(object, context);
    result.aircraft = runtimeObjectLooksAircraftForRender(object, context);
    result.turret = runtimeObjectHasTurretForRender(object, context);
    const auto footprint = runtimeObjectFoundationDimensions(object, context);
    result.footprintWidth = footprint.width; result.footprintHeight = footprint.height;
    result.bodyCandidates = resolveObjectVoxelImageIdCandidates(object, context);
    if (object.kind == WorldObjectKind::Unit && result.turret) {
        result.turretCandidates = runtimeUnitTurretImageCandidates(object, context);
        result.barrelCandidates = runtimeUnitBarrelImageCandidates(object, context);
    }
    return result;
}

bool runRuntimeGeometryQueryContractCheck(std::string& error) {
    const auto fail = [&](const char* message) { error = std::string("geometry batch: ") + message; return false; };
    IniFile rulesIni, artIni;
    RulesDatabase rules;
    ArtDatabase art;
    if (!rulesIni.loadFromString(
            "[VehicleTypes]\n0=__BATCH_BASE__\n1=__BATCH_ALT__\n"
            "[BuildingTypes]\n0=__BATCH_BUILD__\n[InfantryTypes]\n0=__BATCH_INF__\n"
            "[AircraftTypes]\n0=__BATCH_AIR__\n"
            "[__BATCH_BASE__]\nImage=__BATCH_BASE_ART__\nTurret=yes\n"
            "UnloadingClass=__BATCH_ALT__\nIsSimpleDeployer=yes\nDeployToLand=yes\nChargeTurret=yes\nTurretCount=4\n"
            "[__BATCH_ALT__]\nImage=__BATCH_ALT_ART__\nTurret=no\n"
            "[__BATCH_BUILD__]\nImage=__BATCH_BUILD_ART__\n")
        || !rules.loadFromIni(rulesIni)
        || !artIni.loadFromString(
            "[__BATCH_BASE_ART__]\nImage=__BATCH_BASE_IMAGE__\nVoxel=yes\n"
            "[__BATCH_ALT_ART__]\nImage=__BATCH_ALT_IMAGE__\nVoxel=yes\n"
            "[__BATCH_BUILD_ART__]\nFoundation=4x3\nHeight=10\n")
        || !art.loadFromIni(artIni)) return fail("fixture load failed");
    GameWorld world;
    RuntimeMapCamera camera;
    RuntimeMapRenderCache cache, otherCache;
    RuntimeMapRenderContext context;
    context.rules = &rules; context.art = &art; context.animationTick = 30;
    context.stableRenderConfiguration = true;
    std::vector<SpecialMechanismRenderSnapshot> snapshots(2);
    snapshots[0].objectId = 4; snapshots[0].kind = "missileDeck";
    snapshots[1].objectId = 17; snapshots[1].kind = "spyDisguise";
    snapshots[1].resourceId = "__BATCH_ALT__"; snapshots[1].phase = "Vehicle";
    context.specialMechanismSnapshots = &snapshots;
    const RECT viewport{0, 0, 1280, 720};
    std::vector<WorldObjectInstance> objects(96);
    std::vector<RuntimeGeometryTypeMetadata> expected;
    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        auto& object = objects[index];
        object.instanceId = index; object.kind = WorldObjectKind::Unit;
        object.type = index % 13 == 0 ? "__BATCH_AIR__" : "__BATCH_BASE__";
        object.mission = index % 5 == 0 ? "Dock" : index % 3 == 0 ? "Deploying" : "Guard";
        object.deployed = index % 7 == 0;
        object.chargeTurretStartTick = index % 4 == 0 ? 20 : index % 4 == 1 ? 29 : -1;
        object.activeGunnerTurretIndex = index % 4 - 1;
        if (index % 19 == 0) { object.kind = WorldObjectKind::Structure; object.type = "__BATCH_BUILD__"; }
        if (index % 23 == 0) { object.kind = WorldObjectKind::Infantry; object.type = "__BATCH_INF__"; }
        expected.push_back(runtimeGeometryMetadataReference(object, context));
    }
    const auto* previous = RuntimeGeometryQueryFrame::active;
    {
        RuntimeGeometryQueryScope batch(world, viewport, context, camera, cache);
        auto copied = context; copied.chaosBodyTint = true; copied.airstrikeBodyColorAdd = 17;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const auto* result = runtimeGeometryMetadataForObject(objects[index], copied);
            if (!result || !runtimeGeometryMetadataEqualForContract(expected[index], *result))
                return fail("type/mission/deploy/turret/deck metadata differs");
        }
        auto mismatched = context; ++mismatched.animationTick;
        if (runtimeGeometryMetadataForObject(objects[0], mismatched)) return fail("changed tick matched");
        mismatched = context; mismatched.rules = nullptr;
        if (runtimeGeometryMetadataForObject(objects[0], mismatched)) return fail("changed rules matched");
        const auto* outer = RuntimeGeometryQueryFrame::active;
        try {
            RuntimeGeometryQueryScope nested(world, viewport, mismatched, camera, otherCache);
            if (RuntimeGeometryQueryFrame::active == outer) return fail("nested scope did not activate");
            throw 1;
        } catch (int) {}
        if (RuntimeGeometryQueryFrame::active != outer) return fail("nested exception did not restore scope");

        // Ordinary frames remain valid while independently cached tilted poses
        // exceed their LRU capacity. The frame memo never borrows tilted poses.
        auto& ordinary = cache.voxelObjectsByType["__batch_unrotated__"];
        ordinary.loaded = ordinary.hasSprite = true;
        rememberRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.5, 0, 1, 0, &ordinary);
        for (int index = 0; index < 800; ++index) {
            const auto key = "__batch_tilt__" + std::to_string(index);
            touchRuntimeVehicleBodyVoxelCache(cache, cache.voxelObjectsByType[key], key, {});
        }
        trimRuntimeVehicleBodyVoxelCache(cache);
        RuntimeVoxelCacheEntry* result = nullptr;
        if (!findRuntimeGeometryVoxelFrame("image", "owner", 2, copied, cache, 0.5, 0, 1, 0, result)
            || result != &ordinary || cache.vehicleBodyPoseUses.size() != 768) return fail("LRU frame borrowing failed");
        if (findRuntimeGeometryVoxelFrame("image", "owner", 2, context, otherCache, 0.5, 0, 1, 0, result))
            return fail("different cache matched");
        mismatched = context; mismatched.assetRootPath = "another-root";
        if (findRuntimeGeometryVoxelFrame("image", "owner", 2, mismatched, cache, 0.5, 0, 1, 0, result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 3, context, cache, 0.5, 0, 1, 0, result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.6, 0, 1, 0, result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.5, 1, 1, 0, result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.5, 0, 2, 0, result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.5, 0, 1, 1, result))
            return fail("typed frame dependency mismatch");
    }
    if (RuntimeGeometryQueryFrame::active != previous) return fail("scope exit did not restore prior batch");
    // Reuse the same rules/art object addresses after a reload, then change a
    // snapshot and the resource root. No metadata or borrowed frame crosses batches.
    if (!artIni.loadFromString("[__BATCH_BASE_ART__]\nImage=__BATCH_CHANGED__\nVoxel=yes\n")
        || !art.loadFromIni(artIni)
        || !rulesIni.loadFromString("[VehicleTypes]\n0=__BATCH_BASE__\n"
            "[__BATCH_BASE__]\nImage=__BATCH_BASE_ART__\nTurret=no\n")
        || !rules.loadFromIni(rulesIni)) return fail("reload fixture failed");
    snapshots[0].objectId = 5; context.assetRootPath = "changed-root";
    const auto changed = runtimeGeometryMetadataReference(objects[5], context);
    {
        RuntimeGeometryQueryScope batch(world, viewport, context, camera, cache);
        const auto* result = runtimeGeometryMetadataForObject(objects[5], context);
        RuntimeVoxelCacheEntry* stale = nullptr;
        if (!result || !runtimeGeometryMetadataEqualForContract(changed, *result)
            || findRuntimeGeometryVoxelFrame("image", "owner", 2, context, cache, 0.5, 0, 1, 0, stale))
            return fail("metadata or frames leaked across batches");
    }
    return runRuntimePaintGeometryReuseContractCheck(error)
        && runRuntimeEmptyBuildingDepthContractCheck(error)
        && runRuntimePaintBuildingDepthReuseContractCheck(error);
}

bool runRuntimeGeometryAssetContractCheck(const GameWorld& world,
    const RuntimeMapRenderContext& sourceContext, std::string& error) {
    const auto sameSpans = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
            if (left[index].y != right[index].y || left[index].left != right[index].left
                || left[index].right != right[index].right) return false;
        return true;
    };
    struct Result {
        bool resolved = false;
        RECT bounds{}, canvas{};
        int layers = 0;
        std::vector<RuntimeSpriteHitSpan> spans, foreground;
        RuntimeObjectWorldRenderPlacement placement;
    };
    RuntimeMapRenderContext context = sourceContext;
    context.stableRenderConfiguration = true;
    RuntimeMapCamera camera;
    RuntimeMapRenderCache cache;
    const RECT viewport{0, 0, 1280, 720};
    std::vector<WorldObjectInstance> probes;
    for (const auto& type : {"MTNK", "CMIN", "SREF", "SHAD", "E1", "GAPOWR"}) {
        for (int variant = 0; variant < 4; ++variant) {
            WorldObjectInstance object;
            object.instanceId = 920000 + static_cast<int>(probes.size());
            object.type = type; object.owner = "Americans"; object.health = 1000;
            object.kind = object.type == "GAPOWR" ? WorldObjectKind::Structure
                : object.type == "E1" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            object.mission = variant == 1 ? "Dock" : variant == 2 ? "Deploying" : "Guard";
            object.facing = variant * 64; object.turretFacing = 255 - variant * 64;
            object.deployed = variant == 2;
            object.chargeTurretStartTick = variant == 3 ? context.animationTick : -1;
            object.activeGunnerTurretIndex = variant == 3 ? 1 : -1;
            object.x = variant * 3; object.y = variant * 2;
            if (object.type == "SHAD" && variant == 3) {
                object.airborne = true; object.level = object.flightLevel = 4;
                for (const auto& host : world.getObjects()) {
                    if (host.kind != WorldObjectKind::Structure) continue;
                    object.x = host.x; object.y = host.y; object.factoryExitHostId = host.instanceId;
                    break;
                }
            }
            probes.push_back(std::move(object));
        }
    }
    const auto capture = [&](const WorldObjectInstance& object) {
        Result result;
        const POINT center{200 + object.x * 2, 240 + object.y * 3};
        result.placement = runtimeObjectWorldRenderPlacement(object, world, viewport, context, camera, cache);
        if (object.kind == WorldObjectKind::Structure) {
            RuntimeStructureSpriteGeometry geometry;
            result.resolved = resolveRuntimeStructureSpriteGeometry(object, center, context, camera, cache, geometry);
            result.bounds = geometry.bounds; result.layers = geometry.layerCount;
            result.spans = std::move(geometry.hitSpans); result.foreground = std::move(geometry.foregroundHitSpans);
        } else {
            RuntimeSpriteGeometryCapture geometry;
            geometry.hitSpans = &result.spans;
            result.resolved = captureRuntimeMobileSpriteGeometry(object, center, context, camera, cache, geometry);
            result.bounds = geometry.bounds; result.canvas = geometry.canvasBounds; result.layers = geometry.layerCount;
        }
        return result;
    };
    std::vector<Result> direct;
    for (const auto& object : probes) direct.push_back(capture(object));
    {
        RuntimeGeometryQueryScope batch(world, viewport, context, camera, cache);
        for (std::size_t index = 0; index < probes.size(); ++index) {
            const auto actual = capture(probes[index]);
            const auto& expected = direct[index];
            if (actual.resolved != expected.resolved || actual.layers != expected.layers
                || !EqualRect(&actual.bounds, &expected.bounds) || !EqualRect(&actual.canvas, &expected.canvas)
                || !sameSpans(actual.spans, expected.spans) || !sameSpans(actual.foreground, expected.foreground)
                || actual.placement.center.x != expected.placement.center.x
                || actual.placement.center.y != expected.placement.center.y
                || actual.placement.sortPoint.x != expected.placement.sortPoint.x
                || actual.placement.sortPoint.y != expected.placement.sortPoint.y
                || actual.placement.airborneForeground != expected.placement.airborneForeground
                || !EqualRect(&actual.placement.bodyViewport, &expected.placement.bodyViewport)) {
                error = "geometry batch asset bounds/spans/placement differ: " + probes[index].type
                    + " variant=" + std::to_string(index % 4);
                return false;
            }
        }
    }
    return runRuntimePaintGeometryReuseAssetContractCheck(context, std::move(probes), error)
        && runRuntimeEmptyBuildingDepthAssetContractCheck(context, error)
        && runRuntimePaintBuildingDepthReuseAssetContractCheck(world, context, error);
}
