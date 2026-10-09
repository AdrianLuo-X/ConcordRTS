bool runRuntimeChronoEffectsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Chrono effects: " + why; return false; };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("startup");
    live->rulesLoaded = live->artLoaded = live->theaterLoaded = true;
    live->runtimeSoundsEnabled = false; live->runtimeSuppressPlayerAudioFeedback = true;
    if (!runRuntimeChronoTransportIssue25SmokeProbe(*live, error)) return false;
    auto s = std::make_unique<RuntimeWindowState>();
    s->config = config; s->config.fixedTickHz = 30; s->report = live->report;
    s->rules = live->rules; s->art = live->art; s->theater = live->theater;
    s->rulesLoaded = s->artLoaded = s->theaterLoaded = true;
    s->runtimeSoundsEnabled = false; s->runtimeSuppressPlayerAudioFeedback = true;
    s->runtimePreferencePersistenceEnabled = false; s->economy.owner = "Americans";
    s->world = live->world; s->world.accessObjects().clear();
    s->world.accessTerrainCells().clear(); s->world.accessTerrainObjects().clear();
    s->world.accessSmudges().clear();
    for (int y = 1; y < 80; ++y) for (int x = 1; x < 80; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.tileIndex = 0; cell.cellIndex512 = y*512+x;
        s->world.accessTerrainCells().push_back(cell);
        s->terrainInfoByCell.emplace(runtimeCellKey(x,y), RuntimeTerrainInfo{});
    }
    WorldObjectInstance source; source.instanceId = 1; source.kind = WorldObjectKind::Infantry;
    source.type = "CLEG"; source.owner = "Americans"; source.x = source.y = 40;
    source.cell = runtimeCellKey(40,40); source.health = 125; source.facing = 64; source.mission = "Guard";
    auto target = source; target.instanceId = 2; target.kind = WorldObjectKind::Unit;
    target.type = "MTNK"; target.owner = "Russians"; target.x = 42; target.health = 400;
    target.cell = runtimeCellKey(42,40);
    s->world.accessObjects() = {source,target};
    s->terrainByCell = buildRuntimeTerrainLookup(s->world); initializeRuntimeNavigation(*s);
    s->visionEnabled = true; updateRuntimeVisionState(*s);
    s->selectedObjectIds = {1}; issueRuntimeAttackCommand(*s, *findObjectById(*s,2));
    if (s->lastCommand.acceptedCount != 1) return fail("actual attack command rejected");
    for (int tick = 0; tick < 180 && !s->specialMechanisms.isTemporalFrozen(2); ++tick) {
        ++s->tick; advanceRuntimeSimulation(*s);
    }
    if (!s->specialMechanisms.isTemporalFrozen(2)) return fail("full simulation never establishes attack");
    for (int tick = 0; tick < 20; ++tick) { ++s->tick; advanceRuntimeSimulation(*s); }
    refreshRuntimeSpecialMechanismRenderSnapshots(*s);
    const auto beamCount = std::count_if(s->specialMechanismRenderSnapshots.begin(),
        s->specialMechanismRenderSnapshots.end(), [](const auto& ray) {
            return ray.kind == "temporalBeam" && ray.hasBeamEndpoints;
        });
    if (beamCount != 1) return fail("full simulation lost cooldown beam");
    RuntimeMapRenderContext context; context.rules=&s->rules; context.art=&s->art; context.theater=&s->theater;
    context.assetRootPath=runtimeAssetRootPath(config); context.tileRootPath=runtimeTileRootPath(config,s->report.map);
    context.palettePath=runtimePalettePath(config,s->report.map);
    context.structureRootPath=runtimeStructureRootPath(config,s->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,s->report.map);
    context.fixedTickHz=30; context.animationTick=s->tick;
    context.vision=&s->vision; context.visionObserverOwner=s->economy.owner;
    context.specialMechanismSnapshots=&s->specialMechanismRenderSnapshots;
    if (!runRuntimeChronoPaletteContractCheck(context,error)
        || !runRuntimeTemporalRenderContractCheck(context,error)
        || !runRuntimeChronoRenderContractCheck(context,error)
        || !runRuntimeTemporalAttackSmokeProbe(*live,error)
        || !runRuntimeTemporalTimingSmokeProbe(*live,error)
        || !runRuntimeChronoMovementSmokeProbe(*live,error)) return false;
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(*s,saved,error)) return fail("save: "+error);
    IniFile savedIni; savedIni.loadFromString(saved.str());
    auto restored=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,*s,false);
    if (!materializeRuntimeLoadStateFromIni(*restored,savedIni,error)
        || restored->specialMechanisms.stateHash()!=s->specialMechanisms.stateHash())
        return fail("normal save lost active temporal state: "+error);
    refreshRuntimeSpecialMechanismRenderSnapshots(*restored);
    if (std::count_if(restored->specialMechanismRenderSnapshots.begin(),restored->specialMechanismRenderSnapshots.end(),
        [](const auto& ray){return ray.kind=="temporalBeam" && ray.hasBeamEndpoints;})!=1)
        return fail("normal save lost resolved beam");
    for (int tick=0;tick<20;++tick) { ++restored->tick; advanceRuntimeSimulation(*restored); }
    if (!restored->specialMechanisms.isTemporalFrozen(2)) return fail("resumed simulation lost beam target");
    const auto progress = [&]() {
        for (const auto& status : restored->specialMechanisms.renderSnapshots(restored->tick,30))
            if (status.kind=="temporal" && status.targetObjectId==2) return status.progress;
        return 0;
    };
    const int initialProgress=progress();
    int continuedTicks=0;
    while (progress()<=initialProgress && continuedTicks<600) {
        ++restored->tick; ++continuedTicks; advanceRuntimeSimulation(*restored);
        refreshRuntimeSpecialMechanismRenderSnapshots(*restored);
        if (std::count_if(restored->specialMechanismRenderSnapshots.begin(),restored->specialMechanismRenderSnapshots.end(),
                [](const auto& ray){return ray.kind=="temporalBeam" && ray.hasBeamEndpoints;})!=1)
            return fail("resumed real weapon cooldown lost its persistent beam");
    }
    if (progress()<=initialProgress || continuedTicks > 2) return fail("continuous erasure did not advance on next rule frame");
    std::cout << "Chrono resumed attack: continuous progress after " << continuedTicks << " ticks, progress "
        << initialProgress << " -> " << progress() << '\n';
    constexpr int width=640,height=360;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr); void* raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if (!dc || !bitmap || !raw) { if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("surface"); }
    const auto old=SelectObject(dc,bitmap); auto* pixels=static_cast<std::uint32_t*>(raw);
    context.surfacePixels=pixels; context.surfaceWidth=width; context.surfaceHeight=height;
    const RECT viewport{0,0,width,height}; RuntimeMapCamera camera; camera.initialized=true;
    // Default projection is 60x30, north-west world origin at the viewport origin.
    camera.worldOffsetX=30-width/2; camera.worldOffsetY=41*30-height/2;
    const auto before=runtimeStandaloneSimulationStateHash(*s,error);
    bool ok=error.empty();
    for (int detail=0;detail<=2;++detail) {
        context.visualDetail=detail;
        const auto actual=drawRuntimeMap(dc,viewport,s->world,context,camera,s->renderCache); GdiFlush();
        const std::vector<std::uint32_t> full(pixels,pixels+width*height);
        auto blank=context; blank.specialMechanismSnapshots=nullptr;
        drawRuntimeMap(dc,viewport,s->world,blank,camera,s->renderCache); GdiFlush();
        int changed=0; for (int i=0;i<width*height;++i) changed+=full[i]!=pixels[i];
        std::cout << "Chrono full map detail=" << detail << " actors=" << actual.specialMechanismRenderActors
            << " changedPixels=" << changed << '\n';
        ok=ok && actual.rendered && actual.specialMechanismRenderActors==2 && changed>=40;
        drawRuntimeMap(dc,viewport,s->world,context,camera,s->renderCache); GdiFlush();
        ok=ok && std::equal(full.begin(),full.end(),pixels);
        if (detail==2) if (const char* dir=std::getenv("CONCORD_CHRONO_EFFECTS_DIR"); dir && *dir)
            ok=writeRuntimeSmokeBmp(std::string(dir)+"/chrono-full-map.bmp",pixels,width,height,error) && ok;
    }
    ok=ok && before==runtimeStandaloneSimulationStateHash(*s,error) && error.empty();
    if (ok) {
        issueRuntimeStopCommand(*s);
        issueRuntimeMoveCommand(*s,{45,40});
        ok=s->lastCommand.acceptedCount==1;
        for (int tick=0;tick<10;++tick) { ++s->tick; advanceRuntimeSimulation(*s); }
        refreshRuntimeSpecialMechanismRenderSnapshots(*s);
        context.animationTick=s->tick;
        context.impactActors=runtimeImpactRenderActorsForRender(*s);
        int warpCount=0;
        for (const auto& actor : *context.impactActors) {
            if (actor.lifecycleState!="ChronoWarpIn" && actor.lifecycleState!="ChronoWarpOut") continue;
            ++warpCount;
            ok=ok && actor.animId=="WARPOUT";
        }
        const auto arrivalBefore=runtimeStandaloneSimulationStateHash(*s,error);
        const auto actual=drawRuntimeMap(dc,viewport,s->world,context,camera,s->renderCache); GdiFlush();
        ok=ok && warpCount==2 && actual.impactActorDrawn>=2 && actual.specialMechanismRenderActors>=1
            && arrivalBefore==runtimeStandaloneSimulationStateHash(*s,error) && error.empty();
        if (const char* dir=std::getenv("CONCORD_CHRONO_EFFECTS_DIR"); dir && *dir)
            ok=writeRuntimeSmokeBmp(std::string(dir)+"/chrono-arrival-full-map.bmp",pixels,width,height,error) && ok;
        std::cout << "Chrono actual arrival: effects=" << warpCount << " drawn=" << actual.impactActorDrawn
            << " recovery=" << actual.specialMechanismRenderActors << '\n';
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    if (!ok) return fail("actual map beam/arrival, paused pixels or render authority");
    std::cout << "Chrono effects: passed; full simulation, visibility, three detail levels, paused pixels and unchanged authority\n";
    return true;
}
