bool runRuntimeSuperWeaponAnimationSmoke(const RuntimeConfig& config,std::string& error){
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Superweapon animation: "+why; return false;};
    auto live=std::make_unique<RuntimeWindowState>(); live->config=config;
    live->report=buildRuntimeStartupReport(config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success) return fail("original resources unavailable");
    std::vector<WorldObjectInstance> hosts;
    std::vector<std::vector<SpecialMechanismRenderSnapshot>> phases;
    int cases=0;
    for(int hz:{30,60}) for(const std::string type:{"GACSPH","GAWEAT","NAIRON","NAMISL","YAGNTC","YAPPET"}){
        auto p=std::make_unique<RuntimeWindowState>(); auto& s=*p;
        s.config=config; s.config.fixedTickHz=hz; s.config.computerAiEnabled=false; s.config.superweapons=true;
        // This stock charge-window test must not inherit the user's optional
        // instant-recharge setting, which correctly starts in ready art.
        s.config.noCooldown=false;
        s.report=live->report; s.rules=live->rules; s.art=live->art; s.theater=live->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
        s.economy.owner=type[0]=='G' ? "Americans" : type[0]=='N' ? "Russians" : "YuriCountry";
        s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        for(int y=1;y<=32;++y) for(int x=1;x<=32;++x){
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        const auto* building=s.rules.findBuildingType(type);
        if(!building || building->chargedAnimTime!=1.0) return fail(type+" original charged animation window missing");
        WorldObjectInstance host; host.instanceId=1; host.kind=WorldObjectKind::Structure; host.type=type;
        host.owner=s.economy.owner; host.alive=true; host.health=building->strength;
        host.mission="Guard"; host.poweredOperational=true; host.x=host.y=18; host.cell=runtimeCellKey(18,18);
        auto reactor=host; reactor.instanceId=2; reactor.type="NANRCT"; reactor.health=1000;
        reactor.x=reactor.y=3; reactor.cell=runtimeCellKey(3,3);
        s.world.accessObjects()={host,reactor}; initializeRuntimeNavigation(s); ensureRuntimeSuperWeaponsInitialized(s);
        const auto step=[&](bool powered=true){++s.tick; auto c=runtimeSuperWeaponContext(s);
            if(!powered)c.ownerPowered=[](const auto&){return false;}; s.superWeapons.tick(c);};
        const auto snapshot=[&](){s.specialMechanismRenderSnapshots.clear(); appendRuntimeSuperWeaponBuildingSnapshots(s);
            for(const auto& pose:s.specialMechanismRenderSnapshots) if(pose.objectId==1) return pose;
            return SpecialMechanismRenderSnapshot{};};
        step();
        std::vector<SpecialMechanismRenderSnapshot> pictures{snapshot()};
        if(pictures.back().resourceId!="SuperAnim") {
            const auto* initial=s.superWeapons.findSidebarSnapshot(s.economy.owner,building->superWeapon);
            return fail(type+" initial idle stage mismatch: actual="+pictures.back().resourceId
                +" expected=SuperAnim noCooldown="+std::to_string(s.config.noCooldown)
                +" weapon="+building->superWeapon+" state="+std::to_string(initial?static_cast<int>(initial->state):-1)
                +" remaining="+std::to_string(initial?initial->remainingTicks:-1));
        }
        const int window=hz*60;
        for(int i=0;i<hz*1200;++i){
            const auto* weapon=s.superWeapons.findSidebarSnapshot(s.economy.owner,building->superWeapon);
            if(!weapon) return fail(type+" owned support power missing");
            if(weapon->remainingTicks<=window) break;
            step();
        }
        auto opening=snapshot();
        if(opening.resourceId!="SuperAnimTwo" || opening.progress!=0) return fail(type+" did not start opening at one minute remaining");
        pictures.push_back(opening);
        for(int i=0;i<hz;++i) step();
        opening=snapshot(); pictures.push_back(opening);
        for(int i=0;i<hz;++i) step(false);
        if(snapshot().progress!=opening.progress) return fail(type+" opening did not freeze when charging lost power");
        std::ostringstream saved; s.superWeapons.save(saved); IniFile ini; ini.loadFromString(saved.str());
        auto restored=s.superWeapons;
        if(!restored.load(ini,&error) || restored.stateHash()!=s.superWeapons.stateHash()) return fail(type+" saved charge clock changed: "+error);
        s.superWeapons=std::move(restored);
        if(snapshot().progress!=opening.progress) return fail(type+" animation jumped on reload");
        // Complete the real charging path. forceReady changes authoritative
        // state, while sidebar/render snapshots refresh on the next tick.
        for(int i=0;i<=window+hz && s.superWeapons.stateOf(s.economy.owner,building->superWeapon)!=SuperWeaponRuntimeState::Ready;++i) step();
        if(s.superWeapons.stateOf(s.economy.owner,building->superWeapon)!=SuperWeaponRuntimeState::Ready) return fail(type+" ready transition failed");
        const auto ready=snapshot(); pictures.push_back(ready);
        if(ready.resourceId!="SuperAnimThree") return fail(type+" ready art missing");
        for(int i=0;i<hz;++i) step();
        const auto readyLater=snapshot(); pictures.push_back(readyLater);
        if(readyLater.progress-ready.progress!=hz) return fail(type+" ready animation is stuck on its first frame");
        if(hz==30){hosts.push_back(host); phases.push_back(pictures);}
        std::cout<<"Superweapon "<<type<<" hz="<<hz<<" idle/open/ready-loop/power-pause/save=ok\n";
        ++cases;
    }
    RuntimeMapRenderContext context;
    context.rules=&live->rules; context.art=&live->art; context.theater=&live->theater;
    context.assetRootPath=runtimeAssetRootPath(config);
    context.structureRootPath=runtimeStructureRootPath(config,live->report.map);
    context.palettePath=runtimePalettePath(config,live->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,live->report.map);
    if(!runRuntimeSuperWeaponAnimationRenderContractCheck(context,hosts,phases,error)) return false;
    std::cout<<"Superweapon animations passed: "<<cases<<" stock building/tick-rate cases, original normal/damaged layers, live ready loops and save clocks\n";
    return true;
}
