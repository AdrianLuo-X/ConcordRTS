#include "RuntimeNativeCliffShroudContract.inl"
#include "RuntimeNativeFlatShroudContract.inl"
#include "RuntimeOverlayArchiveContract.inl"
#include "RuntimeLowBridgeFrameContract.inl"
#include "RuntimeBridgeConnectivityContract.inl"

bool runRuntimeNativeShroudMovementContractCheck(const RuntimeMapRenderContext& assets, std::string& error) {
    if (std::getenv("CONCORD_BRIDGE_CONNECTIVITY_ONLY"))
        return runRuntimeBridgeConnectivityContract(assets, error);
    if (!runRuntimeNativeFlatShroudContract(assets,error)) return false;
    if (std::getenv("CONCORD_NATIVE_SHROUD_FLAT_ONLY")) return true;
    if (!runRuntimeLowBridgeFrameContract(assets,error)) return false;
    if (!runRuntimeOverlayArchiveContract(assets,error)) return false;
    if (!runRuntimeNativeCliffShroudContract(assets,error)) return false;
    std::uint32_t randomState = 0x710ce3a5U;
    const auto random = [&]() {
        randomState ^= randomState << 13; randomState ^= randomState >> 17;
        randomState ^= randomState << 5; return randomState;
    };
    GameWorld world;
    for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.level = (x * 13 + y * 7) % 5;
        world.accessTerrainCells().push_back(cell);
    }
    IniFile ini; RulesDatabase rules;
    if (!ini.loadFromString("[InfantryTypes]\n0=__TILE_SCOUT__\n[BuildingTypes]\n0=__TILE_GAP__\n"
            "[__TILE_SCOUT__]\nSight=2\nStrength=100\n"
            "[__TILE_GAP__]\nSight=1\nStrength=500\nGapGenerator=yes\nGapRadiusInCells=3\nPowered=yes\n")
        || !rules.loadFromIni(ini)) {
        error = "incremental shroud fixture rules did not load"; return false;
    }
    WorldObjectInstance scout; scout.instanceId = 1; scout.kind = WorldObjectKind::Infantry;
    scout.type = "__TILE_SCOUT__"; scout.owner = "Observer"; scout.x = scout.y = 4;
    scout.health = 100; scout.alive = true;
    world.accessObjects().push_back(scout);
    WorldObjectInstance gap; gap.instanceId = 2; gap.kind = WorldObjectKind::Structure;
    gap.type = "__TILE_GAP__"; gap.owner = "Enemy"; gap.x = gap.y = 20;
    gap.alive = true; gap.health = 500; gap.poweredOperational = false;
    world.accessObjects().push_back(gap);
    RuntimeVisionSystem vision;
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
    vision.update(world, &rules, observers, 1);
    RuntimeMapRenderContext context = assets;
    context.vision = &vision; context.visionObserverOwner = "Observer"; context.rules = &rules;
    context.surfaceWidth = 512; context.surfaceHeight = 384;
    RuntimeMapCamera camera; camera.initialized = true;
    camera.tileWidth = 12; camera.tileHeight = 6;
    camera.worldOffsetX = -256; camera.worldOffsetY = -20;
    RECT viewport{3, 5, 509, 381};
    RuntimeMapRenderCache cache;
    auto& terrain = cache.terrainLayer;
    terrain.built = true; terrain.key = "incremental-shroud-owned-TMP";
    terrain.width = 512; terrain.height = 384;
    terrain.originX = camera.worldOffsetX; terrain.originY = camera.worldOffsetY;
    terrain.pixels.assign(512 * 384, 0x00ffffffU);
    terrain.terrainCellIndices.assign(512 * 384, -1);
    for (int y = 0; y < 384; ++y) for (int x = 0; x < 512; ++x)
        if ((x / 23 + y / 19) % 5 == 0)
            terrain.terrainCellIndices[y * 512 + x] = ((y / 12) % 41) * 512 + (x / 16) % 41;
    // This owner is far from its geometric diamond and crosses tile edges.
    for (int x = 58; x < 134; ++x) terrain.terrainCellIndices[311 * 512 + x] = 40 * 512 + 40;
    if (!runtimeLoadNativeShroud(context, cache)) { error="native movement atlas missing"; return false; }
    int paint = 0;
    int partialUpdates = 0;
    std::uint64_t hiddenTerrainPixels = 0;
    const auto check = [&](const char* label, RuntimeMapRenderCache& actualCache) {
        const std::size_t count = static_cast<std::size_t>(context.surfaceWidth) * context.surfaceHeight;
        std::vector<std::uint32_t> actual(count), expected(count);
        ++paint;
        for (std::size_t i = 0; i < count; ++i) actual[i] = expected[i] = random();
        RuntimeMapRenderCache reference; reference.terrainLayer = actualCache.terrainLayer;
        RuntimeMapRenderStats a, b;
        {
            RuntimeFrameScalingReferenceScope optimized(false);
            context.surfacePixels = actual.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, actualCache, a);
        }
        {
            RuntimeFrameScalingReferenceScope original(true);
            context.surfacePixels = expected.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, reference, b);
        }
        if (!actualCache.visionOverlay.nativeShroudFingerprint) { error="native path not selected"; return false; }
        RuntimeVisionGridView view;
        if (!vision.gridViewFor(context.visionObserverOwner, view)) return false;
        if (runtimeVisionTileHasOwnership(actualCache.terrainLayer)) {
            for (int y=std::max<LONG>(0,viewport.top);y<std::min<LONG>(context.surfaceHeight,viewport.bottom);++y)
                for (int x=std::max<LONG>(0,viewport.left);x<std::min<LONG>(context.surfaceWidth,viewport.right);++x) {
                    const int owner=runtimeVisionTileOwnerAt(actualCache.terrainLayer,viewport,camera,x,y);
                    if (owner<0 || static_cast<std::size_t>(owner)>=view.cellCount
                        || view.visibilityAtIndex(owner)!=RuntimeCellVisibility::Shrouded) continue;
                    if (actual[static_cast<std::size_t>(y)*context.surfaceWidth+x]!=0) {
                        error="native shroud leaked an unexplored TMP overhang";return false;
                    }
                    ++hiddenTerrainPixels;
                }
        }
        if (actual != expected || a.visionVisibleCells != b.visionVisibleCells
            || a.visionFoggedCells != b.visionFoggedCells || a.visionShroudedCells != b.visionShroudedCells
            || a.visionOverlayPixels != b.visionOverlayPixels) {
            const auto different = std::mismatch(actual.begin(), actual.end(), expected.begin());
            error = std::string("incremental shroud pixels/counts differ: ") + label
                + " pixel=" + std::to_string(different.first - actual.begin());
            return false;
        }
        const auto& incremental = actualCache.visionOverlay.incremental;
        const std::uint64_t area = static_cast<std::uint64_t>(std::max(0, incremental.right - incremental.left))
            * std::max(0, incremental.bottom - incremental.top);
        if (a.visionOverlayCacheMisses && incremental.rebuiltMaskPixels > 0
            && incremental.rebuiltEffectPixels < area) ++partialUpdates;
        return true;
    };
    if (!check("cold", cache) || !check("warm", cache)) return false;
    for (int tick = 2; tick <= 25; ++tick) {
        auto& moving = world.accessObjects()[0];
        moving.x = 4 + tick % 11; moving.y = 4 + tick % 7;
        auto& movingGap = world.accessObjects()[1];
        movingGap.poweredOperational = tick % 4 != 0;
        movingGap.x = 17 + tick % 5; movingGap.y = 17 + tick % 3;
        vision.update(world, &rules, observers, tick);
        vision.exploreArea("Observer", 18 + tick % 13, 17 + tick % 11, 1);
        if (tick % 3 == 0) vision.resetShroudArea("Observer", 29, 27, 2);
        if (!check("moving sight, exploration and reversible Gap", cache)) return false;
    }
    vision.exploreArea("Observer", 40, 40, 1);
    if (!check("distant TMP owner and tile-crossing overhang", cache)) return false;
    RuntimeMapRenderCache copied;
    copied.terrainLayer = cache.terrainLayer; copied.visionOverlay = cache.visionOverlay;
    const auto keptMask = cache.visionOverlay.incremental.mask;
    vision.resetShroudArea("Observer", 40, 40, 1);
    if (!check("copied cache", copied) || cache.visionOverlay.incremental.mask != keptMask
        || !check("original after copied cache", cache)) {
        if (error.empty()) error = "incremental shroud cache copies share mutable state";
        return false;
    }
    context.visionObserverOwner = "Enemy";
    if (!check("owner switched", cache)) return false;
    context.visionObserverOwner = "Observer";
    if (!check("owner restored", cache)) return false;
    camera.worldOffsetX -= 7; camera.worldOffsetY += 9;
    if (!check("cold camera", cache) || !check("warm camera", cache)) return false;
    context.surfaceWidth = 533; context.surfaceHeight = 401;
    viewport = RECT{-7, -3, 520, 395};
    if (!check("resize and negative clipped viewport", cache)) return false;
    cache.terrainLayer.key += "-replaced";
    cache.terrainLayer.terrainCellIndices[311 * 512 + 63] = 4 * 512 + 4;
    if (!check("TMP ownership replacement", cache)) return false;
    cache.terrainLayer.built = false;
    if (!check("terrain layer removed", cache)) return false;
    camera.tileWidth = 18; camera.tileHeight = 9;
    if (!check("tile scale changed", cache)) return false;
    viewport = RECT{80, 20, 81, 25};
    if (!check("one-pixel viewport", cache)) return false;
    viewport = RECT{0, 0, 533, 401};
    vision.reset(); vision.update(world, &rules, observers, 30);
    if (!check("vision storage replaced", cache)) return false;
    if (!runtimeMovementReferenceForRender() && partialUpdates == 0) {
        error = "incremental shroud fixture did not prove partial pixel work"; return false;
    }
    std::cout << "Native incremental shroud: paints=" << paint
        << " partialUpdates=" << partialUpdates << " hiddenTerrainPixels=" << hiddenTerrainPixels
        << " pixelsAndCounts=exact\n";
    return true;
}
