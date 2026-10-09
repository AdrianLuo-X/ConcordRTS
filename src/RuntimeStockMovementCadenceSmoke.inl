bool runRuntimeStockMovementCadenceSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Stock movement cadence: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original rules unavailable");
    const bool diagnostic = std::getenv("CONCORD_STOCK_MOVEMENT_DIAGNOSTIC") != nullptr;
    const char* output = std::getenv("CONCORD_STOCK_MOVEMENT_CADENCE_OUTPUT");
    const std::filesystem::path root = output && *output ? output
        : "build/smoke/feature-set-19/04-stock-cadence/movement";
    std::filesystem::create_directories(root);
    std::ofstream csv(root / "stock-cruise.csv");
    csv << "type,hz,speed,jumpjet,leptonsPerRuleFrame,cells,ticks,retailTicks,excessTicks,seconds,allowedSegmentLossTicks\n";
    // Ordinary Speed goldens are independently established by the retail
    // TechnoType::Read_INI probe. JumpjetSpeed is already leptons/frame and
    // must not pass through the percentage conversion a second time.
    struct Golden { const char* type; int speed; int leptons; bool jumpjet; bool fixedWing; };
    constexpr Golden goldens[] = {
        {"MTNK",7,17,false,false}, {"HTNK",6,15,false,false}, {"FV",10,25,false,false},
        {"CMIN",4,10,false,false}, {"HARV",4,10,false,false}, {"E1",4,10,false,false},
        {"JUMPJET",9,30,true,false}, {"ZEP",5,5,true,false},
        {"SCHP",12,30,true,false}, {"DISK",15,16,true,false},
        {"ORCA",14,35,false,true}, {"BEAG",14,35,false,true}
    };
    int cases = 0, violations = 0;
    std::map<std::pair<std::string,int>,double> seconds;
    for (const auto& golden : goldens) for (const int hz : {15,30,60}) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.config.gameModeRulesOverride.clear();
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater; s.report = assets->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        initializeRuntimeLocalProductionLanes(s.economy);
        RuntimeHumanHouseAuthority authority; authority.owner = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y=1; y<=24; ++y) for (int x=1; x<=56; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* mover = addRuntimeObjectInstance(s, runtimeBuildKindForUnitTypeId(s.rules,golden.type),
            golden.type, {4,12});
        if (!mover) return fail(std::string(golden.type)+" creation");
        const int id = mover->instanceId;
        const auto* unit = findRuntimeUnitType(s.rules,*mover);
        if (!unit || unit->speed != golden.speed
            || runtimeUnitUsesJumpjetMovement(*unit) != golden.jumpjet
            || (golden.jumpjet && unit->jumpjetSpeed != golden.leptons))
            return fail(std::string(golden.type)+" authored speed fixture changed");
        mover->facing = runtimeFacingFromDelta(1,0); mover->harvestAutoSearchSuspended = true;
        // Start airborne types at their cruise height to isolate horizontal
        // cadence from factory exit, takeoff, turning and final braking.
        if (golden.jumpjet || golden.fixedWing) {
            mover->airborne = true; mover->hasRenderPosition = true;
            mover->renderX = mover->x; mover->renderY = mover->y;
            mover->renderLevel = golden.jumpjet ? unit->jumpJetHeight/256.0 : 6.0;
            mover->level = mover->flightLevel = static_cast<int>(std::ceil(mover->renderLevel));
            mover->onBridge = mover->renderOnBridge = false;
        }
        initializeRuntimeNavigation(s);
        for (auto& [key,info] : s.terrainInfoByCell) {
            (void)key; info.movementLandType=info.authoredLandType=RuntimeTerrainLandType::Clear;
            info.water=info.shore=info.bridge=false; info.passable=true; info.cost=10;
        }
        RuntimeSnapshotActiveStateScope activeState(s);
        applySelection(s,{id},false,false); issueRuntimeMoveCommand(s,{48,12});
        if (s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(id))
            return fail(std::string(golden.type)+" player move rejected");
        // Keep the measured route cardinal; command acceptance and actual
        // simulation movement remain the same paths used by normal play.
        std::vector<RuntimeCellPoint> path;
        for (int x=5; x<=48; ++x) path.push_back({x,12});
        replaceRuntimeBehaviorPath(s.behaviors.at(id),path);
        s.behaviors.at(id).pathRequestPending=false;
        int first=-1,last=-1;
        for (int n=0; n<hz*200 && last<0; ++n) {
            ++s.tick; advanceRuntimeSimulation(s); mover=findMutableObjectById(s,id);
            if (!mover || !mover->alive) return fail(std::string(golden.type)+" mover disappeared");
            if (mover->x>=12 && first<0) first=s.tick;
            if (mover->x>=36 && last<0) last=s.tick;
        }
        if (first<0 || last<0) return fail(std::string(golden.type)+" did not complete cruise window");
        constexpr double cells=24.0;
        const double expected=cells*256.0*hz/(15.0*golden.leptons);
        const int elapsed=last-first;
        // All types retain fractional movement across legal cell boundaries.
        // Only the two sampled endpoint ticks may differ from continuous time.
        const double allowedLoss=0.0;
        const bool valid=elapsed>=expected-1.1 && elapsed<=expected+allowedLoss+1.1;
        ++cases; violations+=valid?0:1;
        seconds[{golden.type,hz}]=double(elapsed)/hz;
        csv<<golden.type<<','<<hz<<','<<golden.speed<<','<<golden.jumpjet<<','<<golden.leptons
            <<",24,"<<elapsed<<','<<std::setprecision(12)<<expected<<','<<elapsed-expected
            <<','<<double(elapsed)/hz<<','<<allowedLoss<<'\n';
        std::cout<<"Stock cruise: "<<golden.type<<" hz="<<hz<<" ticks="<<elapsed
            <<" retail="<<expected<<" excess="<<elapsed-expected<<" allowedSegmentLoss="<<allowedLoss
            <<" valid="<<valid<<'\n';
        if (!valid && !diagnostic) return fail(std::string(golden.type)+" hz="+std::to_string(hz)
            +" actual="+std::to_string(elapsed)+" retail="+std::to_string(expected));
    }
    for (int hz : {15,30,60}) {
        const bool relative=seconds.at({"JUMPJET",hz})<seconds.at({"MTNK",hz})
            && seconds.at({"FV",hz})<seconds.at({"MTNK",hz})
            && seconds.at({"MTNK",hz})<seconds.at({"HTNK",hz})
            && seconds.at({"HTNK",hz})<seconds.at({"HARV",hz})
            && seconds.at({"HARV",hz})<seconds.at({"ZEP",hz})
            && seconds.at({"ORCA",hz})<seconds.at({"JUMPJET",hz})
            && std::abs(seconds.at({"ORCA",hz})-seconds.at({"BEAG",hz}))<=1.0/hz;
        if (!relative) { ++violations; if (!diagnostic) return fail("cross-type retail speed order"); }
    }
    std::cout<<"Stock movement cadence: "<<(diagnostic?"diagnostic":"passed")<<" cases="<<cases
        <<" violations="<<violations<<" distance=24 middle cells; no per-cell loss allowance\n";
    return diagnostic || violations==0;
}
