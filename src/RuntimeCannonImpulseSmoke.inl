bool runRuntimeCannonImpulseSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="cannon impulse "+scene+": "+why;return false;};
    IniFile native;RulesDatabase rules;
    if(!native.loadFromFile(live.report.rulesPath)||!rules.loadFromIni(native))return fail("native rules");
    int scenes=0,flightFrames=0,restoredFrames=0;
    for(const char* sourceType:{"MTNK","HTNK","LTNK","APOC","TNKD","GTGCAN"})
        for(int rank:{0,2})for(int hz:{15,30,60})for(const char* targetType:{"MTNK","HTNK","APOC","FV"}) {
        const bool grand=std::string(sourceType)=="GTGCAN";if(grand&&rank!=0)continue;
        scene=std::string(sourceType)+"-"+std::to_string(rank)+"-"+std::to_string(hz)+"-"+targetType;
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=live.config;s.report=live.report;s.world=live.world;s.art=live.art;s.theater=live.theater;
        s.rules=rules;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.config.fixedTickHz=hz;
        s.config.noCooldown=false;s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.config.playerSlots.clear();
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.terrainInfoByCell.clear();
        for(int y=1;y<=20;++y)for(int x=1;x<=20;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);
            c.tileIndex=c.subTileIndex=0;s.world.accessTerrainCells().push_back(c);}
        WorldObjectInstance source;source.instanceId=1;source.kind=grand?WorldObjectKind::Structure:WorldObjectKind::Unit;
        source.type=sourceType;source.owner="Russians";source.health=1000;source.alive=true;source.poweredOperational=true;
        source.x=6;source.y=10;source.cell=runtimeCellKey(6,10);source.mission="Guard";source.facing=source.turretFacing=64;source.veterancy=rank;
        auto target=source;target.instanceId=2;target.kind=WorldObjectKind::Unit;target.type=targetType;target.owner="Americans";
        target.health=10000;target.x=10;target.cell=runtimeCellKey(10,10);target.facing=96;target.turretFacing=160;target.veterancy=0;
        s.world.accessObjects()={source,target};initializeRuntimeNavigation(s);RuntimeSnapshotActiveStateScope active(s);
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,1),findObjectById(s,2));
        if(!profile.weapon||!profile.projectile||!profile.warhead) return fail("weapon profile");
        if(grand?(!profile.warhead->rocker||profile.warhead->cellSpread!=2.0)
            :(profile.warhead->rocker||profile.warhead->cellSpread>0.5))return fail("native cannon distinction");
        spawnRuntimeProjectile(s,*findObjectById(s,1),*findObjectById(s,2),*profile.weapon,profile,profile.weapon->damage);
        if(s.projectiles.size()!=1)return fail("real shell spawn");const int id=s.projectiles.front().actorId;
        std::vector<std::vector<RuntimeImpactRenderActor>> timeline(1),restored(1);bool hit=false;
        for(int t=0;t<hz*120;++t){
            ++s.tick;advanceRuntimeEffects(s);flightFrames+=!s.projectiles.empty();
            std::vector<RuntimeImpactRenderActor> frame,copy;
            for(const auto& effect:s.impactEffects)if(effect.projectileActorId==id&&effect.renderIntent=="warheadImpact"){
                hit=true;frame.push_back(makeRuntimeImpactRenderActor(effect));RuntimeImpactEffect decoded;
                if(!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect),decoded))return fail("impact record");
                copy.push_back(makeRuntimeImpactRenderActor(decoded));++restoredFrames;
            }
            if(hit){timeline.push_back(std::move(frame));restored.push_back(std::move(copy));if(timeline.back().empty()&&s.projectiles.empty())break;}
        }
        const auto* damaged=findRuntimeObjectByIdIncludingInactive(s,2);
        if(!hit||!s.projectiles.empty()||!damaged||damaged->health>=10000)return fail("actual hit/damage");
        RuntimeMapRenderContext context;context.rules=&s.rules;context.art=&s.art;context.fixedTickHz=hz;
        context.assetRootPath=runtimeAssetRootPath(s.config);context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        const auto hash=s.world.deterministicSimulationStateHash();const int healthBefore=damaged->health;
        if(!runRuntimeCannonImpulseRenderCheck(context,target,timeline,restored,scene,error))return false;
        if(s.world.deterministicSimulationStateHash()!=hash||healthBefore!=findRuntimeObjectByIdIncludingInactive(s,2)->health)return fail("render mutated battle");
        ++scenes;
    }
    std::cout<<"Cannon impulse passed: "<<scenes<<" actual shell impacts, "<<flightFrames<<" flying frames, "<<restoredFrames
        <<" snapshot impact frames, five normal/elite tanks plus Grand Cannon, four target types, 15/30/60 Hz\n";
    return true;
}
