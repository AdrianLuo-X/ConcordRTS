bool runRuntimeOreDestructionSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="Ore destruction: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_ORE_DESTRUCTION_OUTPUT");const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("native resources");
    const RuntimeCellPoint aimed{114,215},neighbor{115,215};
    int cases=0,saves=0,restoredTicks=0,captures=0;
    for(const char* type:{"MTNK","HTNK"})for(const char* family:{"TIB","GEM"})
    for(int rank:{0,2})for(int hz:{15,30,60}) {
        scene=std::string(type)+"-"+family+"-r"+std::to_string(rank)+"-"+std::to_string(hz);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=hz;s.config.playerCountry="Americans";
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=false;
        s.config.multiplayerActive=false;s.config.startingUnits=0;s.config.playerSlots.clear();
        s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.report=assets->report;s.mapFacts=assets->mapFacts;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        auto* actor=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type,{110,215});
        if(!actor)return fail("attacker creation");const int id=actor->instanceId;actor->veterancy=rank;actor->mission="Sleep";
        for(const auto cell:{aimed,neighbor}) {
            auto* terrain=findMutableRuntimeTerrainCellAt(s,cell);if(!terrain)return fail("native target terrain");
            if(runtimeCachedTerrainInfoForCell(s,*terrain).water)return fail("fixture in water");
            setRuntimeResourceTerrainBails(*terrain,family,runtimeOreMaxBails);
        }
        invalidateRuntimeResourceCaches(s);s.stableRuntimeResourceTracking=true;
        const auto bails=[&](const RuntimeWindowState& state,RuntimeCellPoint cell){
            const auto* t=findRuntimeTerrainCellAt(state,cell);return t?runtimeResourceBailCountForTerrain(*t):-1;};
        const auto step=[](RuntimeWindowState& state){RuntimeSnapshotActiveStateScope scope(state);++state.tick;advanceRuntimeSimulation(state,false);};
        const auto capture=[&](const char* phase){
            if(root.empty()||hz!=30||rank!=0)return true;
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{113,215},800,600,error,root+"/"+scene+"-"+phase+".bmp",false);
            ++captures;return stats&&stats->rendered&&stats->missingVoxelProxies==0;};
        const auto radarBefore=runtimeRadarResourceStateHash(s);const auto revisionBefore=s.resourceStateRevision;
        const auto worldBefore=s.world.deterministicSimulationStateHash();
        if(!capture("before"))return fail("before rendering: "+error);
        s.selectedObjectIds={id};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,aimed)
            ||s.lastCommand.acceptedCount!=1)return fail("real force-fire rejected");
        bool fired=false,partial=false,cleared=false,savedFlight=false;std::set<int> projectileIds;
        std::map<int,int> restoredCurve;
        const auto reloadFlight=[&](bool exact){
            std::ostringstream saved;IniFile ini;
            if(!writeRuntimeStateToStream(s,saved,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))
                ||!ini.loadFromString(saved.str()))return false;
            auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!materializeRuntimeLoadStateFromIni(*loaded,ini,error,exact)
                ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error)))return false;
            ++saves;loaded->stableRuntimeResourceTracking=true;const auto before=runtimeRadarResourceStateHash(*loaded);
            for(int n=0;n<hz*20;++n) {
                step(*loaded);++restoredTicks;const int amount=bails(*loaded,aimed);
                if(!exact)restoredCurve[loaded->tick]=amount;
                else if(!restoredCurve.contains(loaded->tick)||restoredCurve.at(loaded->tick)!=amount){error="ordinary/snapshot ore timing differs";return false;}
                if(amount==0) {
                    if(runtimeRadarResourceStateHash(*loaded)==before){error="reload cleared ore retained radar signature";return false;}
                    return bails(*loaded,neighbor)==runtimeOreMaxBails;
                }
            }
            error="restored tank never cleared ore";return false;
        };
        for(int n=0;n<hz*20;++n) {
            step(s);
            for(const auto& p:s.projectiles)if(p.attackerObjectId==id){
                const auto* weapon=s.rules.findWeaponType(p.weaponId);
                if(!weapon||weapon->projectile!="Cannon")return fail("expected native tank shell");
                projectileIds.insert(p.actorId);fired=true;
            }
            if(fired&&!savedFlight&&hz==30) {
                if(!reloadFlight(false)||!reloadFlight(true))return fail("flight save: "+error);savedFlight=true;
            }
            const int amount=bails(s,aimed);
            if(restoredCurve.contains(s.tick)&&restoredCurve.at(s.tick)!=amount)return fail("live/reload mineral timing differs");
            if(amount<runtimeOreMaxBails&&!fired)return fail("mineral changed before firing");
            if(amount>0&&amount<runtimeOreMaxBails&&!partial){partial=true;if(!capture("partial"))return fail(error);}
            if(amount==0){cleared=true;break;}
        }
        if(!fired||!cleared||!partial||bails(s,neighbor)!=runtimeOreMaxBails)return fail("real shells did not partially reduce then clear only the aimed cell");
        if(runtimeRadarResourceStateHash(s)==radarBefore||s.resourceStateRevision<=revisionBefore) {
            if(!root.empty())capture("before-fix-cleared");
            return fail("cleared ore retained radar signature");
        }
        if(s.world.deterministicSimulationStateHash()==worldBefore)return fail("ore removal missing from world hash");
        s.selectedObjectIds={id};issueRuntimeStopCommand(s);
        for(int n=0;n<hz*3&&!s.impactEffects.empty();++n)step(s);
        if(bails(s,aimed)!=0||!capture("cleared"))return fail("cleared rendering: "+error);
        if(hz==30) {
            std::ostringstream saved;IniFile ini;auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*loaded,ini,error))return fail("cleared save: "+error);
            if(bails(*loaded,aimed)!=0||bails(*loaded,neighbor)!=runtimeOreMaxBails)return fail("cleared ore returned after reload");++saves;
        }
        ++cases;std::cout<<"Ore destruction: "<<scene<<" shells="<<projectileIds.size()<<" partial/empty/neighbor/radar/save=ok\n";
    }
    std::cout<<"Ore destruction: cases="<<cases<<" fullSaves="<<saves<<" restoredTicks="<<restoredTicks<<" nativeCaptures="<<captures<<" PASS\n";
    return true;
}
