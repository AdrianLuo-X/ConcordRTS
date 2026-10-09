#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeMagnetronLiftPresentationSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Magnetron lift presentation: "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success) return fail("native resources");
    RuntimeMapRenderContext render;
    render.rules=&assets->rules; render.art=&assets->art; render.theater=&assets->theater;
    render.assetRootPath=runtimeAssetRootPath(config);
    render.structureRootPath=runtimeStructureRootPath(config,assets->report.map);
    render.palettePath=runtimePalettePath(config,assets->report.map);
    render.structurePalettePath=runtimeStructurePalettePath(config,assets->report.map);
    if(!runRuntimeMagnetronLiftShadowContractCheck(render,assets->world,error)) return false;
    const char* output=std::getenv("CONCORD_MAGNETRON_LIFT_DIR");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-20/05-magnetron-lift";
    std::filesystem::create_directories(root); std::ofstream csv(root/"trajectory.csv");
    csv<<"type,hz,direction,tick,x,y,z,phase\n";
    const RuntimeCellPoint directions[]={{8,0},{0,8},{-8,0},{0,-8}};
    int cases=0;
    for(const char* type:{"MTNK","HTNK","FV","DEST"}) for(int hz:{15,30,60}) for(int direction=0;direction<4;++direction) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
        seedRuntimeLoadScratch(s,*assets,false); s.config=config; s.config.fixedTickHz=hz;
        s.config.playerSlots.clear(); s.config.playerCountry="YuriCountry";
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true; s.economy.owner="YuriCountry";
        s.economy.skirmishStartApplied=true; s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts={}; s.mapFactsLoaded=s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width=-1;
        const bool naval=std::string(type)=="DEST";
        for(int y=8;y<56;++y) for(int x=8;x<56;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            MapCellFacts facts; facts.x=x; facts.y=y; facts.cellIndex=cell.cellIndex512; facts.hasTerrain=true;
            facts.terrain.x=x; facts.terrain.y=y; facts.terrain.tileIndex=facts.terrain.subTileIndex=0;
            facts.isWater=naval&&std::max(std::abs(x-32),std::abs(y-32))>2;
            facts.terrainClass=facts.isWater?"water":"land"; facts.passabilityHint="passable";
            RuntimeTerrainInfo terrain; terrain.water=facts.isWater;
            s.terrainInfoByCell.emplace(runtimeCellKey(x,y),terrain);
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex,s.mapFacts.cells.size()); s.mapFacts.cells.push_back(facts);
        }
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();
        const RuntimeCellPoint start{32+directions[direction].x,32+directions[direction].y};
        auto* source=addRuntimeObjectInstanceForOwner(s,"YuriCountry",RuntimeBuildKind::Unit,"TELE",{32,32});
        if(!source) return fail("native magnetron"); const int sourceId=source->instanceId;
        auto* target=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,type,start);
        if(!target) return fail("native target"); const int targetId=target->instanceId,health=target->health;
        target->mission="Sleep"; target->weaponCooldownTicksRemaining=100000;
        source=findMutableObjectById(s,sourceId); source->facing=source->turretFacing=runtimeFacingFromDelta(directions[direction].x,directions[direction].y);
        initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds={sourceId};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,start,findObjectById(s,targetId))
            ||s.lastCommand.acceptedCount!=1) return fail("UI attack");
        const bool preview=std::string(type)=="MTNK"&&hz==30&&direction==0;
        const auto capture=[&](const char* label){return !preview||drawRuntimeStateOffscreenAtCell(s,{36,32},960,640,error,
            pathString(root/(std::string(label)+".bmp")),false);};
        if(!capture("0-ground")) return false;
        bool rising=false,held=false,pulled=false,earlyTranslation=false;
        for(int tick=0;tick<hz*6&&!pulled;++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
            const auto* binding=runtimeMagBeamBindingForTarget(s,targetId); if(!binding) continue;
            const auto& k=binding->kinematics;
            csv<<type<<','<<hz<<','<<direction<<','<<s.tick<<','<<k.worldX<<','<<k.worldY<<','<<k.worldZ<<','<<runtimeMagBeamPhaseName(binding->phase)<<'\n';
            const double translation=std::hypot(k.worldX-start.x,k.worldY-start.y);
            if(k.worldZ<2.9&&translation>1e-5) earlyTranslation=true;
            if(!rising&&k.worldZ>=1.0) { rising=true; if(!capture("1-rising")) return false; }
            if(!held&&k.worldZ>=2.9) { held=true; if(!capture("2-suspended")) return false; }
            pulled=held&&translation>=1.0;
        }
        if(!capture("3-pulled")) return false;
        target=findMutableObjectById(s,targetId);
        if(earlyTranslation||!rising||!held||!pulled||!target||target->health!=health||!target->airborne)
            return fail(std::string(type)+" hz="+std::to_string(hz)+" direction="+std::to_string(direction)
                +" vertical-before-pull="+std::to_string(!earlyTranslation)
                +" rising="+std::to_string(rising)+" suspended="+std::to_string(held)
                +" pulled="+std::to_string(pulled));
        if(preview) {
            const auto save=root/"midair.ini";
            if(!saveRuntimeState(s,pathString(save),error)||!loadRuntimeState(s,pathString(save),error)) return false;
            target=findMutableObjectById(s,targetId);
            if(!target||!target->airborne||target->renderLevel<2.9) return fail("midair save/load");
        }
        s.selectedObjectIds={sourceId}; issueRuntimeStopCommand(s);
        source=findMutableObjectById(s,sourceId); source->weaponCooldownTicksRemaining=hz*60;
        for(int tick=0;tick<hz*8&&runtimeMagBeamBindingForTarget(s,targetId);++tick) {++s.tick;advanceRuntimeSimulation(s);}
        target=findMutableObjectById(s,targetId);
        if(runtimeMagBeamBindingForTarget(s,targetId)||!target||!target->alive||target->airborne||target->health!=health)
            return fail("stop/drop lost intact target");
        ++cases;
    }
    std::cout<<"Magnetron lift presentation: passed cases="<<cases<<" vertical-before-pull=yes stop/drop=yes midair-save=yes\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
