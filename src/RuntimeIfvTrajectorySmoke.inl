bool runRuntimeIfvTrajectorySmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail=[&](const std::string& why){error="IFV trajectory "+scene+": "+why;return false;};
    IniFile native; RulesDatabase rules;
    if(!native.loadFromFile(live.report.rulesPath)||!rules.loadFromIni(native))return fail("native rules");
    const char* output=std::getenv("CONCORD_IFV_TRAJECTORY_OUTPUT");
    const std::string root=output?output:"build/smoke/feature-set-14/36-ifv-trajectory/checks";
    std::filesystem::create_directories(root);
    std::ofstream records(root+"/trajectories.csv");
    records<<"scene,tick,actor,x,y,z,pitch,remaining\n";
    int cases=0,rounds=0,saves=0,frames=0,rendered=0;
    const auto make=[&](int hz) {
        auto result=std::make_unique<RuntimeWindowState>();auto& s=*result;
        s.config=live.config;s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.computerAiEnabled=s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.rules=rules;s.art=live.art;s.theater=live.theater;s.report=live.report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        return result;
    };
    for(int hz:{15,30,60})for(int rank:{0,2})for(bool passenger:{false,true})for(int scenario=0;scenario<6;++scenario){
        scene=std::to_string(hz)+"-"+std::to_string(rank)+"-"+std::to_string(passenger)+"-"+std::to_string(scenario);
        auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        const int ground=scenario==3?3:0;
        for(int y=45;y<=82;++y)for(int x=45;x<=82;++x){
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.level=ground;c.tileIndex=c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        WorldObjectInstance source;source.instanceId=1;source.kind=WorldObjectKind::Unit;source.type="FV";
        source.owner="Americans";source.alive=true;source.health=200;source.currentAmmo=-1;source.veterancy=rank;
        source.x=source.y=64;source.level=ground;source.cell=runtimeCellKey(64,64);source.mission="Guard";
        source.facing=source.turretFacing=runtimeFacingFromDelta(1,-1);
        auto target=source;target.instanceId=2;target.type=scenario==4?"JUMPJET":"HTNK";
        target.kind=scenario==4?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        target.owner="Russians";target.health=10000;target.veterancy=0;target.mission="Sleep";
        target.x=scenario==1?65:68;target.y=scenario==1?63:60;target.cell=runtimeCellKey(target.x,target.y);
        if(scenario==4){target.airborne=true;target.flightLevel=target.level=ground+10;}
        s.world.accessObjects()={source,target};initializeRuntimeNavigation(s);
        if(passenger){
            auto* cargo=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"GGI",{63,64});
            if(!cargo)return fail("GGI fixture");const int id=cargo->instanceId;s.selectedObjectIds={id};
            if(!queueRuntimeTransportTarget(s,*findMutableObjectById(s,1))||!waitForRuntimeTransportSmokeMotion(s,1,id))
                return fail("actual GGI boarding");
            eraseRuntimeBehavior(s,id);
        }
        auto* shooter=findMutableObjectById(s,1);const auto* enemy=findObjectById(s,2);
        const auto profile=runtimeWeaponProfileForObject(s,*shooter,enemy);
        if(!profile.weapon||!profile.projectile||profile.projectile->id!="AAHeatSeeker2"
            ||(passenger&&profile.weapon->id!="CRMissileLauncher"))return fail("native weapon selection");
        const int expected=std::max(1,profile.weapon->burst);
        if(beginRuntimeCombatBurst(s,*shooter,*enemy,*profile.weapon,profile,1.0)!=1||s.projectiles.empty())
            return fail("real volley creation");
        const int first=s.projectiles.front().actorId;const double startZ=s.projectiles.front().sourceZ;
        double peak=startZ;bool rise=false,dive=false,removed=false;std::set<int> shots;
        std::unique_ptr<RuntimeWindowState> restored;
        std::vector<std::vector<RuntimeProjectileRenderActor>> phases;
        std::vector<RuntimeProjectileRenderActor> actors;
        RuntimeMapRenderContext context;context.rules=&s.rules;context.art=&s.art;
        context.assetRootPath=runtimeAssetRootPath(s.config);context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.fixedTickHz=hz;
        for(int tick=0;tick<hz*12;++tick){
            if(scenario==2&&tick==hz/2){auto* moving=findMutableObjectById(s,2);++moving->x;++moving->y;moving->cell=runtimeCellKey(moving->x,moving->y);}
            ++s.tick;advanceRuntimeCombatBursts(s);advanceRuntimeEffects(s);
            if(restored){
                RuntimeSnapshotActiveStateScope restoredScope(*restored);++restored->tick;
                advanceRuntimeCombatBursts(*restored);advanceRuntimeEffects(*restored);
                if(restored->projectiles.size()!=s.projectiles.size()||findObjectById(*restored,2)->health!=findObjectById(s,2)->health)
                    return fail("restored flight/damage diverged");
                for(std::size_t n=0;n<s.projectiles.size();++n)
                    if(serializeRuntimeProjectileEffect(s.projectiles[n])!=serializeRuntimeProjectileEffect(restored->projectiles[n]))
                        return fail("restored position/direction diverged");
            }
            actors.clear();
            for(const auto& shot:s.projectiles){
                shots.insert(shot.actorId);++frames;
                records<<scene<<','<<s.tick<<','<<shot.actorId<<','<<shot.currentX<<','<<shot.currentY<<','<<shot.currentZ
                    <<','<<shot.homingPitch<<','<<shot.remainingTicks<<'\n';
                if(!shot.homingInitialized||!std::isfinite(shot.currentZ))return fail("guidance absent");
                if(shot.actorId==first){peak=std::max(peak,shot.currentZ);rise=rise||shot.homingPitch>0.2;dive=dive||shot.homingPitch<-.2;}
                auto actor=makeRuntimeProjectileRenderActor(shot);
                if(!actor.hasFlightDirection||std::abs(actor.flightDirectionZ-std::sin(shot.homingPitch)*runtimeMissileLevelsPerCell)>1e-12)
                    return fail("sprite direction lost flight pitch");
                actors.push_back(actor);
            }
            if(hz==30&&rank==0&&!passenger&&scenario==0&&!actors.empty()) phases.push_back(actors);
            if(scenario==0&&rank==0&&!restored&&tick==hz/3){
                std::ostringstream text;IniFile ini;restored=make(hz);seedRuntimeLoadScratch(*restored,s,false);
                if(!writeRuntimeStateToStream(s,text,error)||!ini.loadFromString(text.str())
                    ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("full in-flight reload: "+error);
                if(!saveRuntimeState(s,root+"/flight-"+scene+".ini",error))return fail("save artifact: "+error);
                ++saves;
                auto fields=splitRuntimeSaveFields(serializeRuntimeProjectileEffect(s.projectiles.front()));
                std::string legacy;
                for(std::size_t i=0;i<runtimeProjectileSaveFieldCountV4;++i){if(i)legacy+='|';legacy+=fields[i];}
                RuntimeProjectileEffect old;
                if(!parseRuntimeProjectileEffect(legacy,old,runtimeProjectileSaveSchemaV4)||old.homingInitialized)
                    return fail("previous projectile schema rejected");
            }
            if(scenario==5&&!removed&&s.combatDetails.bursts.empty()){
                auto* gone=findMutableObjectById(s,1);gone->health=0;gone->alive=false;gone->mission="Destroyed";removed=true;
            }
            if(s.projectiles.empty()&&s.combatDetails.bursts.empty())break;
        }
        const auto* hit=findRuntimeObjectByIdIncludingInactive(s,2);
        if(!s.projectiles.empty()||static_cast<int>(shots.size())!=expected||!hit||hit->health>=target.health)
            return fail("volley failed to finish/hit: shots="+std::to_string(shots.size())+" expected="+std::to_string(expected));
        if(scenario!=1&&scenario!=4&&(!rise||!dive||peak<startZ+.5))return fail("missing sloping climb then dive");
        if(scenario==1&&peak>startZ+.4)return fail("close shot acquired distant cruise climb");
        if(scenario==4&&peak>target.level+.5)return fail("air target acquired ground cruise overshoot");
        if(!phases.empty()){
            if(!runRuntimeIfvTrajectoryRenderCheck(context,phases,root+"/ifv-trajectory.bmp",error))return fail(error);
            rendered+=static_cast<int>(phases.size());
        }
        rounds+=expected;++cases;
        std::cout<<"IFV trajectory "<<scene<<" rounds="<<expected<<" peak="<<peak-startZ<<" PASS\n";
    }
    const auto* torpedo=rules.findProjectileType("Torpedo");
    if(!torpedo||!torpedo->level)return fail("native Level projectile parsing");
    std::cout<<"IFV trajectory passed: "<<cases<<" actual volleys, "<<rounds<<" rounds, "<<frames
        <<" flight frames, "<<saves<<" full reloads, "<<rendered<<" native DRAGON/trail draws; 15/30/60 Hz\n";
    return true;
}
