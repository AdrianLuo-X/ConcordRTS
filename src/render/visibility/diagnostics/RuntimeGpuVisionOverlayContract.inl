// Derived independent GPU consumer fixture. Original movement oracle is unchanged.
static std::string runtimeGpuVisionAuthority(const GameWorld& world,const RuntimeVisionSystem& vision) {
    std::string snapshot=std::to_string(world.deterministicSimulationStateHash());
    const auto append=[&](const auto& value){snapshot.append(reinterpret_cast<const char*>(&value),sizeof(value));};
    for(const auto& owner:vision.observerOwners()){
        snapshot+=owner;RuntimeVisionGridView view;if(!vision.gridViewFor(owner,view))throw std::runtime_error("vision authority view");
        append(view.cellCount);append(view.revision);append(view.layoutEpoch);
        for(const auto* data:{view.validCells,view.visibleCells,view.exploredCells,view.gapShroudedCells}){append(data!=nullptr);if(data)snapshot.append(reinterpret_cast<const char*>(data),view.cellCount);}
        if(const auto* stats=vision.statsFor(owner)){append(stats->validCells);append(stats->visibleCells);append(stats->exploredCells);append(stats->visionSources);append(stats->spySatSources);append(stats->gapGeneratorSources);append(stats->gapShroudedCells);append(stats->lastUpdateTick);append(stats->revision);}
    }return snapshot;
}
bool runRuntimeGpuVisionOverlayContractCheck(const std::string& assetRoot,bool warp,std::string& error) try {
    int cases=0;MapVisionGpuFixture fixture(warp);
    const auto runMode=[&](bool nativeAtlas,bool globalReference){
    RuntimeFrameScalingReferenceScope scaling(globalReference);

    GameWorld world;
    for (int y = 0; y <= 20; ++y) for (int x = 0; x <= 20; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        world.accessTerrainCells().push_back(cell);
    }
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString("[InfantryTypes]\n0=__FOG_SCOUT__\n"
            "[BuildingTypes]\n0=__FOG_GAP__\n"
            "[__FOG_SCOUT__]\nSight=2\nStrength=100\n"
            "[__FOG_GAP__]\nSight=1\nStrength=500\nGapGenerator=yes\n"
            "GapRadiusInCells=3\nPowered=yes\n") || !rules.loadFromIni(ini)) {
        error = "moving shroud fixture rules did not load"; return false;
    }
    WorldObjectInstance scout;
    scout.instanceId = 1; scout.kind = WorldObjectKind::Infantry;
    scout.type = "__FOG_SCOUT__"; scout.owner = "Observer";
    scout.x = scout.y = 1; scout.health = 100; scout.alive = true;
    world.accessObjects().push_back(scout);
    RuntimeVisionSystem vision;
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
    vision.update(world, &rules, observers, 1);
    vision.exploreArea("Observer", 10, 10, 13);
    RuntimeMapRenderContext context;
    context.vision = &vision; context.visionObserverOwner = "Observer";
    context.rules = &rules; context.surfaceWidth = 320; context.surfaceHeight = 240;
    context.assetRootPath=nativeAtlas?assetRoot:std::string{};
    RuntimeMapCamera camera;
    camera.initialized = true; camera.tileWidth = 12; camera.tileHeight = 6;
    camera.worldOffsetX = -160; camera.worldOffsetY = -40;
    RECT viewport{0, 0, 320, 240};
    RuntimeMapRenderCache cache;
    auto& terrain = cache.terrainLayer;
    terrain.built = true; terrain.key = "moving-shroud-TMP-ownership";
    terrain.width = 320; terrain.height = 240;
    terrain.originX = camera.worldOffsetX; terrain.originY = camera.worldOffsetY;
    terrain.pixels.assign(320 * 240, 0);
    terrain.terrainCellIndices.assign(320 * 240, -1);
    terrain.terrainCellIndices[210 * 320 + 2] = 20 * 512 + 20;
    terrain.pixels[210 * 320 + 2] = 0x00ffffffU;
    int paint = 0;
    const auto check = [&](const char* label, bool expectedHit) {
        const std::size_t count = static_cast<std::size_t>(context.surfaceWidth)
            * static_cast<std::size_t>(context.surfaceHeight);
        std::vector<std::uint32_t> actual(count), expected(count);
        ++paint;
        for (std::size_t index = 0; index < count; ++index)
            actual[index] = static_cast<std::uint32_t>((index * 2654435761ULL
                + paint * 131ULL) & 0xffffffffULL);
        expected = actual;
        RuntimeMapRenderCache reference;
        reference.terrainLayer = cache.terrainLayer;
        RuntimeMapRenderStats actualStats, expectedStats;
        const auto authority=runtimeGpuVisionAuthority(world,vision);
        context.surfacePixels = expected.data();
        applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, reference, expectedStats);
        fixture.begin(context.surfaceWidth,context.surfaceHeight,actual);context.surfacePixels=fixture.pixels();
        applyRuntimeVisionOverlay(fixture.drawDc(),viewport,world,context,camera,cache,actualStats);
        fixture.finish(actual);
        const std::string name=std::to_string(cases)+(nativeAtlas?"-stock-":"-geometric-")+(globalReference?"global-":"incremental-")+label;
        const bool same=fixture.equals(expected,name);fixture.save(name,actual,expected,same);
        if(!same||runtimeGpuVisionAuthority(world,vision)!=authority
            || actualStats.visionVisibleCells != expectedStats.visionVisibleCells
            || actualStats.visionFoggedCells != expectedStats.visionFoggedCells
            || actualStats.visionShroudedCells != expectedStats.visionShroudedCells
            || actualStats.visionOverlayPixels != expectedStats.visionOverlayPixels
            || actualStats.visionOverlayCacheHits != static_cast<int>(expectedHit)
            || actualStats.visionOverlayCacheMisses != static_cast<int>(!expectedHit)
            || (nativeAtlas&&(!cache.nativeShroudAtlas.loaded||!reference.nativeShroudAtlas.loaded))) {
            error="GPU vision complete pixels/counts/cache/authority/native-asset differ: "+name;return false;
        }
        ++cases;
        return true;
    };
    if (!check("cold", false) || !check("same revision", true)) return false;
    const auto initialRevision = vision.revisionFor("Observer");
    world.accessObjects()[0].x = world.accessObjects()[0].y = 5;
    vision.update(world, &rules, observers, 2);
    if (vision.revisionFor("Observer") == initialRevision) {
        error = "moving shroud fixture did not change current sight"; return false;
    }
    if (!check("Visible to Fogged", !runtimeMovementReferenceForRender())) return false;
    world.accessObjects()[0].x = world.accessObjects()[0].y = 1;
    vision.update(world, &rules, observers, 3);
    if (!check("Fogged to Visible", !runtimeMovementReferenceForRender())) return false;
    vision.exploreArea("Observer", 20, 20, 1);
    if (!check("new exploration and TMP overhang", false)) return false;

    WorldObjectInstance gap;
    gap.instanceId = 2; gap.kind = WorldObjectKind::Structure;
    gap.type = "__FOG_GAP__"; gap.owner = "Enemy"; gap.x = gap.y = 15;
    gap.alive = true; gap.health = 500; gap.poweredOperational = true;
    world.accessObjects().push_back(gap);
    vision.update(world, &rules, observers, 4);
    if (vision.visibilityFor("Observer", 15, 15) != RuntimeCellVisibility::Shrouded) {
        error = "moving shroud fixture did not activate Gap coverage"; return false;
    }
    if (!check("Gap coverage", false)) return false;
    world.accessObjects().back().y = 11;
    vision.update(world, &rules, observers, 5);
    if (!check("equal-size Gap move", false)) return false;
    world.accessObjects().back().poweredOperational = false;
    vision.update(world, &rules, observers, 6);
    if (!check("Gap removed", false)) return false;
    context.visionObserverOwner = "Enemy";
    if (!check("owner changed", false)) return false;
    context.visionObserverOwner = "Observer";
    if (!check("owner restored", false)) return false;
    camera.worldOffsetX += 13; camera.worldOffsetY += 7;
    if (!check("camera moved", false) || !check("warm moved camera", true)) return false;
    context.surfaceWidth = 352; context.surfaceHeight = 256;
    viewport.right = 352; viewport.bottom = 256;
    if (!check("surface resized", false)) return false;
    viewport.left = 7; viewport.top = 9;
    if (!check("viewport clipped", false)) return false;
    camera.tileWidth = 18; camera.tileHeight = 9;
    if (!check("tile zoom", false)) return false;
    vision.resetShroudArea("Observer", 10, 10, 2);
    if (!check("explicit reshroud", false)) return false;
    vision.reset();
    vision.update(world, &rules, observers, 7);
    return check("vision layout replaced", false);
    };
    for(const bool nativeAtlas:{false,true})for(const bool globalReference:{false,true})if(!runMode(nativeAtlas,globalReference))return false;
    fixture.close();if(cases!=68){error="GPU vision required cases missing";return false;}
    std::cout<<"S41 GPU vision overlay contract passed driver="<<(warp?"warp":"hardware")<<" cases="<<cases<<" originalSoftwareFullBgra=exact cpuRgbWrites=0 authority=unchanged normalReadback=not-used-diagnostic-only\n";return true;
}catch(const std::exception& failure){error=failure.what();return false;}
