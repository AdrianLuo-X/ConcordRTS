bool runRuntimeGapPowerSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& text) { error="gap power: "+text; return false; };
    const char* output=std::getenv("CONCORD_GAP_POWER_OUTPUT");
    const std::string root=output?output:"";
    if (!root.empty()) std::filesystem::create_directories(root);
    auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
    s.config=live.config; s.report=live.report; s.rules=live.rules; s.rulesLoaded=true;
    s.art=live.art; s.artLoaded=true; s.theater=live.theater; s.theaterLoaded=live.theaterLoaded;
    s.world=live.world; s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
    s.world.accessTerrainCells().clear(); s.world.accessSmudges().clear();
    s.config.computerAiEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
    s.economy.owner="Americans"; s.economy.credits=100000; s.economy.techLevel=10;
    s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
    initializeRuntimeLocalProductionLanes(s.economy);
    for (int y=40;y<=112;++y) for (int x=40;x<=112;++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
        cell.tileIndex=0; s.world.accessTerrainCells().push_back(cell);
    }
    int nextId=1;
    const auto add=[&](const std::string& type,int x,int y) {
        WorldObjectInstance object; object.instanceId=nextId++; object.kind=WorldObjectKind::Structure;
        object.type=type; object.owner=s.economy.owner; object.health=s.rules.findBuildingType(type)->strength;
        object.alive=true; object.x=x; object.y=y; object.cell=runtimeCellKey(x,y); object.mission="Guard";
        s.world.accessObjects().push_back(object); return object.instanceId;
    };
    const auto* gap=s.rules.findBuildingType("GAGAP");
    if (!gap || gap->power!=-100 || gap->extraPower!=-9000) return fail("stock gap rules differ");
    const int powerId=add("NANRCT",80,80);
    add("GACNST",80,85);
    const RECT track=runtimeSidebarPowerTrackRect(runtimeSidebarRect({0,0,800,600}));
    const int capacity=std::max(1,static_cast<int>(track.bottom-track.top-4)/3);
    const int baseDrain=std::max(0,(capacity-5)*10);
    int remaining=baseDrain, consumer=0;
    for (const auto& [type,cost] : std::array<std::pair<const char*,int>,3>{{{"GACSPH",200},{"GAREFN",50},{"GAPILE",10}}}) {
        while (remaining>=cost) {
            add(type,56+(consumer%5)*5,56+(consumer/5)*5);
            ++consumer; remaining-=cost;
        }
    }
    initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s); snapRuntimeSidebarPowerAnimation(s);
    if (s.economy.powerProduced!=2000 || s.economy.powerUsed!=baseDrain) return fail("baseline grid");
    const auto capture=[&](RuntimeWindowState& state,const std::string& stage,int width,int height) {
        if (root.empty()) return true;
        std::vector<std::uint32_t> pixels;
        if (!drawRuntimeStateOffscreenAtCell(state,{83,82},width,height,error,"",false,true,-1,&pixels,true)) return false;
        RuntimeBackBuffer surface;
        HDC screen=GetDC(nullptr);
        const bool ready=ensureRuntimeBackBuffer(surface,screen,width,height);
        if (screen) ReleaseDC(nullptr,screen);
        if (!ready) return false;
        std::copy(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(surface.pixels));
        SetBkMode(surface.memoryDc,TRANSPARENT);
        drawRuntimeSidebar(surface.memoryDc,{0,0,width,height},state);
        GdiFlush();
        const bool written=writeRuntimeSmokeBmp(root+"/"+stage+"-"+std::to_string(width)+".bmp",
            surface.pixels,width,height,error);
        releaseRuntimeBackBuffer(surface); return written;
    };
    if (!capture(s,"before",800,600)) return fail(error);
    RuntimeBuildCatalogItem item; item.typeId="GAGAP"; item.kind=RuntimeBuildKind::Defense;
    item.cost=gap->cost; item.buildTicks=1;
    if (!stageRuntimeReadyPlacementForProbe(s,item) || !placeRuntimeReadyBuilding(s,{84,80}))
        return fail("actual placement: "+s.economy.status);
    const int gapId=s.world.getObjects().back().instanceId;
    auto* placed=findMutableObjectById(s,gapId);
    if (!placed || placed->type!="GAGAP") return fail("placed gap missing");
    placed->buildAnimTicksRemaining=0;
    snapRuntimeSidebarPowerAnimation(s);
    const auto check=[&](RuntimeWindowState& state,bool powered) {
        const auto* object=findObjectById(state,gapId);
        if (!object || object->poweredOperational!=powered
            || state.economy.powerUsed!=baseDrain+100) return false;
        const auto gaps=RuntimeSpecialBuildingSystem::activeGapGenerators(makeRuntimeSpecialBuildingContext(state));
        const bool active=std::any_of(gaps.begin(),gaps.end(),[&](const auto& g){return g.sourceObjectId==gapId;});
        if (active!=powered) return false;
        for (const POINT size : {POINT{800,600},POINT{1280,720}}) {
            const RECT bar=runtimeSidebarPowerTrackRect(runtimeSidebarRect({0,0,size.x,size.y}));
            const int count=std::max(1,static_cast<int>(bar.bottom-bar.top-4)/3);
            const auto segments=runtimeSidebarPowerSegmentCounts(runtimeSidebarPowerDisplay(state),count);
            if ((segments.yellow+segments.green>0)!=powered
                || segments.used+segments.yellow+segments.green>count) return false;
        }
        return true;
    };
    if (!check(s,true) || !capture(s,"after-gap",800,600) || !capture(s,"after-gap",1280,720))
        return fail("powered gap filled the bar red: "+error);
    findMutableObjectById(s,powerId)->alive=false;
    rebuildRuntimeEconomyPower(s); snapRuntimeSidebarPowerAnimation(s);
    if (!check(s,false) || !capture(s,"low-power",800,600)) return fail("true deficit was hidden");
    auto* plant=findMutableRuntimeObjectByIdIncludingInactive(s,powerId);
    if (!plant) return fail("power plant missing");
    plant->alive=true;
    rebuildRuntimeEconomyPower(s); snapRuntimeSidebarPowerAnimation(s);
    if (!check(s,true) || !capture(s,"recovered",800,600)) return fail("power did not recover");
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(s,saved,error)) return fail(error);
    IniFile ini;
    if (!ini.loadFromString(saved.str())) return fail("save parse");
    auto loaded=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded,s,false);
    if (!materializeRuntimeLoadStateFromIni(*loaded,ini,error)) return fail("reload: "+error);
    snapRuntimeSidebarPowerAnimation(*loaded);
    if (!check(*loaded,true) || !capture(*loaded,"loaded",800,600)) return fail("loaded power bar");
    std::string legacyContracts;
    if (!validateRuntimeSidebarPowerUi(legacyContracts)) return fail(legacyContracts);
    std::cout << "Gap power: produced=2000 used=" << baseDrain << "->" << baseDrain+100
        << " ordinaryDrain=100 extra9000=unused adaptiveBar=ok lowPower/recovery=ok"
        << " savedGrid=ok originalSmallGridAndAnimation=ok\n";
    return true;
}
