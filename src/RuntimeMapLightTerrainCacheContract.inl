bool runRuntimeMapLightTerrainCacheContractCheck(std::string& error) {
    IniFile ini;
    ini.loadFromString("[BuildingTypes]\n0=CACHELIGHT\n[CACHELIGHT]\n"
        "LightIntensity=0.35\nLightVisibility=1200\nLightRedTint=0.7\n"
        "LightGreenTint=0.4\nLightBlueTint=0.2\nPowered=yes\n");
    RulesDatabase rules;
    if (!rules.loadFromIni(ini)) { error = "map light cache fixture rules"; return false; }
    const auto* lightType = rules.findBuildingType("CACHELIGHT");
    if (!lightType || lightType->lightIntensity == 0 || !lightType->powered) {
        error = "map light cache fixture has no powered source"; return false;
    }
    GameWorld world;
    WorldObjectInstance lamp;
    lamp.instanceId = 7; lamp.type = "CACHELIGHT";
    lamp.kind = WorldObjectKind::Structure; lamp.alive = true;
    lamp.health = 100; lamp.poweredOperational = true;
    lamp.x = 1; lamp.y = 1;
    world.accessObjects().push_back(lamp);
    for (int x = 0; x < 2; ++x) {
        TerrainCell cell; cell.x = x; cell.y = 1; cell.cellIndex512 = x;
        world.accessTerrainCells().push_back(cell);
    }
    RuntimeMapRenderCache cache;
    cache.terrainByCellIndex = {&world.getTerrainCells()[0], &world.getTerrainCells()[1]};
    auto& layer = cache.terrainLayer;
    layer.built = true; layer.key = "light-cache-contract";
    layer.width = 8; layer.height = 6;
    layer.pixels.assign(48, 0x123456U);
    for (int index = 0; index < 48; ++index) layer.terrainCellIndices.push_back(index % 3 - 1);
    RECT viewport{2, 1, 10, 7};
    RuntimeMapCamera camera;
    RuntimeLightingTint global{0.6, 0.5, 0.4};
    MapLightingProfile profile{0.5, 0.7, 0.8, 0.9, 0.1, 0.0};
    RuntimeMapRenderContext context;
    context.rules = &rules; context.lightingTint = &global; context.lightingProfile = &profile;

    // Compare a reused cache with a genuinely rebuilt frame after every
    // dependency change. Compare both receiver IDs and every LUT entry, then
    // exercise the same RGB/alpha lookup used by the real compositor.
    const auto check = [&](const char* stage, bool mustInvalidate) {
        RuntimeMapLightFrame frame(viewport, world, context, camera, cache);
        const auto& currentLamp = world.getObjects().front();
        const std::size_t expectedSources = currentLamp.alive && currentLamp.poweredOperational ? 1U : 0U;
        if (frame.sources.size() != expectedSources) {
            error = std::string("map light cache missing source: ") + stage; return false;
        }
        RuntimeMapRenderCache fresh = cache;
        fresh.mapLightTerrain = {};
        RuntimeMapLightFrame rebuilt(viewport, world, context, camera, fresh);
        if ((mustInvalidate && frame.terrainCacheHit)
            || frame.pixels != rebuilt.pixels || frame.lookups != rebuilt.lookups) {
            error = std::string("map light cache mismatch: ") + stage; return false;
        }
        if (!frame.sources.empty()) {
            for (int y = viewport.top; y < viewport.bottom; ++y)
                for (int x = viewport.left; x < viewport.right; ++x) {
                    const std::uint32_t pixel = 0xa5000000U | ((x * 15371U + y * 7919U) & 0xffffffU);
                    if (frame.transform(x, y, pixel) != rebuilt.transform(x, y, pixel)) {
                        error = std::string("map light cache RGB mismatch: ") + stage; return false;
                    }
                }
            const bool cacheEnabled = cache.mapLightTerrain.valid && !frame.referenceMode;
            RuntimeMapLightFrame warm(viewport, world, context, camera, cache);
            if (cacheEnabled && !warm.terrainCacheHit) {
                error = std::string("map light cache did not reuse terrain: ") + stage; return false;
            }
            // A frame's moving-object receiver must not contaminate the saved
            // terrain map or the receiver numbering of the following frame.
            warm.pixels.front() = warm.receiver(50, 50);
            RuntimeMapLightFrame following(viewport, world, context, camera, cache);
            if (following.pixels != rebuilt.pixels || following.lookups != rebuilt.lookups) {
                error = std::string("map light object receiver persisted: ") + stage; return false;
            }
        }
        return true;
    };
    if (!check("initial", true) || !check("unchanged", false)) return false;
    camera.worldOffsetX = 2;
    if (!check("pan", true)) return false;
    viewport.right -= 2;
    if (!check("resize", true)) return false;
    global.red = 0.61;
    if (!check("global tint", true)) return false;
    profile.ambient = 0.8;
    if (!check("profile", true)) return false;
    context.lightingProfile = nullptr;
    if (!check("profile removed", true)) return false;
    world.accessObjects().front().x = 3;
    if (!check("lamp moved", true)) return false;
    world.accessObjects().front().poweredOperational = false;
    if (!check("lamp power off", true)) return false;
    world.accessObjects().front().poweredOperational = true;
    if (!check("lamp power restored", false)) return false;
    layer.originY += 1;
    if (!check("terrain origin", true)) return false;
    std::swap(layer.terrainCellIndices[1], layer.terrainCellIndices[2]);
    ++cache.terrainLayerCacheMisses;
    if (!check("terrain rebuilt", true)) return false;
    layer.built = false;
    if (!check("terrain unavailable", true)) return false;
    world.accessObjects().front().alive = false;
    if (!check("lamp destroyed", true)) return false;
    std::cout << "Map light terrain cache: unchanged, pan, resize, tint, profile, lamp state, terrain and object isolation passed\n";
    return true;
}
