bool runRuntimeSubmarineRevealSmoke(RuntimeWindowState& live,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="submarine reveal: "+scene+": "+why;return false;};
    std::optional<TerrainCell> water,land;
    for(const auto& cell:live.world.getTerrainCells()){
        if(runtimeCachedTerrainInfoForCell(live,cell).water){if(!water)water=cell;}
        else if(!land)land=cell;
        if(water&&land)break;
    }
    if(!water||!land)return fail("original water and land tiles");
    const char* output=std::getenv("CONCORD_SUBMARINE_REVEAL_OUTPUT");const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    int cases=0,saves=0,captures=0;
    for(int hz:{15,30,60})for(const char* type:{"SUB","BSUB"})for(const char* action:{"fire","damage","sensor","missile"}){
        const std::string mode=action;if(mode=="missile"&&std::string(type)!="BSUB")continue;
        scene=std::string(type)+" "+mode+" hz="+std::to_string(hz);
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=live.config;s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;s.config.playerSlots.clear();
        s.config.multiplayerActive=false;s.report=live.report;s.art=live.art;s.theater=live.theater;
        IniFile rules;if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules))return fail("native rules");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner=std::string(type)=="SUB"?"Russians":"YuriCountry";s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=44;y<=90;++y)for(int x=44;x<=90;++x){
            auto cell=mode=="missile"&&x>=78?*land:*water;cell.x=x;cell.y=y;cell.level=0;cell.cellIndex512=y*512+x;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance sub;sub.instanceId=1;sub.type=type;sub.kind=WorldObjectKind::Unit;sub.owner=s.economy.owner;
        sub.alive=true;sub.health=runtimeMaxHealthForObject(s,sub);sub.currentAmmo=-1;sub.mission="Guard";
        sub.x=sub.y=64;sub.cell=runtimeCellKey(64,64);sub.submerged=sub.cloaked=true;
        sub.facing=runtimeFacingFromDelta(1,0);sub.turretFacing=sub.facing;
        auto other=sub;other.instanceId=2;other.owner="Americans";other.type=mode=="sensor"?"DEST":mode=="damage"?"SUB":"DRED";
        other.cloaked=other.submerged=false;other.x=mode=="sensor"?72:70;other.cell=runtimeCellKey(other.x,other.y);
        other.health=100000;other.mission="Sleep";other.weaponCooldownTicksRemaining=100000;
        if(mode=="missile"){other.type="CAEAST01";other.kind=WorldObjectKind::Structure;other.x=80;other.cell=runtimeCellKey(80,64);}
        s.world.accessObjects()={sub,other};initializeRuntimeNavigation(s);s.structureFootprints=buildRuntimeStructureFootprints(s);
        // Navigation initializes newly spawned cloak timers. Begin this scene
        // after the submarine has already completed that initial transition.
        auto* concealedSub=findMutableObjectById(s,1);concealedSub->cloaked=concealedSub->submerged=true;
        concealedSub->cloakCooldownTicks=concealedSub->submergeCooldownTicks=0;
        RuntimeSnapshotActiveStateScope active(s);ensureRuntimeSpecialMechanismsInitialized(s);
        const auto hidden=[&](const RuntimeWindowState& state){const auto* object=findObjectById(state,1);
            return object&&object->cloaked&&object->submerged&&!runtimeObjectVisibleToOwner(state,"Americans",*object);};
        const auto visible=[&](const RuntimeWindowState& state){const auto* object=findObjectById(state,1);
            return object&&!object->cloaked&&!object->submerged&&runtimeObjectVisibleToOwner(state,"Americans",*object);};
        const auto step=[](RuntimeWindowState& state){++state.tick;advanceRuntimeCombatBursts(state);advanceRuntimeEffects(state);};
        if(!hidden(s))return fail("initial enemy stealth");
        if(mode=="fire"||mode=="missile"){
            s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*findObjectById(s,2));
            for(int tick=0;tick<hz*3&&!visible(s);++tick){
                ++s.tick;auto behavior=s.behaviors.find(1);if(behavior==s.behaviors.end())return fail("attack order");
                advanceRuntimeAttackBehavior(s,*findMutableObjectById(s,1),behavior->second);
                advanceRuntimeCombatBursts(s);advanceRuntimeEffects(s);
            }
            if(!visible(s)||findObjectById(s,1)->weaponCooldownTicksRemaining<=0)return fail("real discharge did not surface: "+s.simulationStatus);
            for(int tick=0;tick<hz;++tick){step(s);if(!visible(s))return fail("stationary combat lost visibility");}
        }else if(mode=="damage"){
            const auto* weapon=s.rules.findWeaponType("SubTorpedo");RuntimeWeaponProfile profile;
            if(!weapon)return fail("native torpedo");finalizeRuntimeWeaponProfile(profile,s,RuntimeWeaponSlot::Primary,*weapon,"revealDamage");
            spawnRuntimeProjectile(s,*findObjectById(s,2),*findObjectById(s,1),*weapon,profile,weapon->damage);
            const auto projectile=s.projectiles.back();s.projectiles.clear();
            if(!applyRuntimeProjectileImpact(s,projectile)||!visible(s)||findObjectById(s,1)->health>=sub.health)return fail("actual hit did not reveal");
            auto* attacker=findMutableObjectById(s,2);attacker->x=85;attacker->cell=runtimeCellKey(85,64);clearRuntimeRenderPosition(*attacker);
        }else{
            ++s.tick;advanceRuntimeCloakState(s);if(!visible(s))return fail("native destroyer inclusive sensor range");
            for(int tick=0;tick<hz*3;++tick){step(s);if(!visible(s))return fail("sensor presence allowed re-submerge");}
        }
        if(hz==30&&!root.empty()){
            const auto owner=s.economy.owner;s.economy.owner="Americans";
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{65,64},800,600,error,root+"/"+type+"-"+mode+".bmp",false,false);
            s.economy.owner=owner;if(!stats||!stats->rendered)return fail("native visible frame: "+error);++captures;
        }
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("full visible reload: "+error);
        if(!visible(*restored))return fail("reload lost visible state");++saves;
        for(auto* state:{&s,restored.get()}){
            RuntimeSnapshotActiveStateScope restoreScope(*state);
            if(mode=="fire"||mode=="missile"){state->selectedObjectIds={1};issueRuntimeStopCommand(*state);}
            if(mode=="sensor"){auto* detector=findMutableObjectById(*state,2);detector->x=73;detector->cell=runtimeCellKey(73,64);clearRuntimeRenderPosition(*detector);}
        }
        bool concealed=false;
        for(int tick=0;tick<hz*7;++tick){
            step(s);{RuntimeSnapshotActiveStateScope restoreScope(*restored);step(*restored);}
            const auto* first=findObjectById(s,1);const auto* second=findObjectById(*restored,1);
            if(!first||!second||first->cloaked!=second->cloaked||first->submerged!=second->submerged
                ||first->cloakCooldownTicks!=second->cloakCooldownTicks||first->submergeCooldownTicks!=second->submergeCooldownTicks)return fail("reload reveal clock diverged");
            if(hidden(s)){concealed=true;break;}
        }
        if(!concealed)return fail("did not return to concealed submarine after activity ended");
        if(mode=="sensor"){
            auto* detector=findMutableObjectById(s,2);detector->x=65;detector->cell=runtimeCellKey(65,64);detector->owner=s.economy.owner;
            ++s.tick;advanceRuntimeCloakState(s);if(!hidden(s))return fail("allied sensors revealed own submarine");
            detector->owner="Americans";detector->type="DRED";
            ++s.tick;advanceRuntimeCloakState(s);if(!hidden(s))return fail("non-sensor ship invented detection");
        }
        ++cases;
    }
    std::cout<<"Submarine reveal passed: "<<cases<<" native SUB/BSUB firing/hit/sensor scenes, "<<saves
        <<" full reloads, "<<captures<<" enemy-view frames, inclusive range, allies/non-sensors, return to concealment\n";
    return true;
}
