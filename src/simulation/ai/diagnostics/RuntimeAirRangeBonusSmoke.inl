bool runRuntimeAirRangeBonusSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& reason){error="Air range: "+reason;return false;};
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fail("original resources unavailable");
    const auto* fv=stock->rules.findUnitType("UnitType","FV");
    if(!fv || fv->airRangeBonus!=4.0 || stock->rules.findWeaponType("HoverMissile")->range!=6.0
        || stock->rules.findWeaponType("HoverMissileE")->range!=6.0)return fail("original FV range definitions");
    std::string root="build/smoke/feature-checks/03-air-range/checks";
    if(const char* path=std::getenv("CONCORD_AIR_RANGE_DIR"))root=path;
    std::filesystem::create_directories(root);
    const auto make=[&](int hz,bool air,int distance,bool elite=false){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=config;s.config.fixedTickHz=hz;s.config.playerSlots.clear();s.config.computerAiEnabled=false;
        s.config.noCooldown=false;s.config.multiplayerActive=false;s.config.shortGame=false;s.config.superweapons=false;
        s.report=stock->report;s.rules=stock->rules;s.art=stock->art;s.theater=stock->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        for(int y=1;y<=40;++y)for(int x=1;x<=40;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);
        }
        addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"FV",{12,12});
        addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,air?"DISK":"HTNK",{12+distance,12});
        s.world.accessObjects()[0].veterancy=elite?2:0;
        s.world.accessObjects()[0].mission="Guard";s.world.accessObjects()[1].mission="Sleep";
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);
        return box;
    };
    const auto step=[](RuntimeWindowState& s){++s.tick;advanceRuntimeSimulation(s);};
    const auto shot=[&](RuntimeWindowState& s,int id){
        return std::find_if(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return p.attackerObjectId==id;});
    };
    const auto fire=[&](RuntimeWindowState& s,int id){
        for(int i=0;i<s.config.fixedTickHz*20 && shot(s,id)==s.projectiles.end();++i)step(s);
        return shot(s,id)!=s.projectiles.end();
    };
    int cases=0;
    for(int hz:{30,60})for(bool elite:{false,true})for(int distance:{6,8,10,11}){
        auto box=make(hz,true,distance,elite);auto& s=*box;
        const int id=s.world.getObjects()[0].instanceId,target=s.world.getObjects()[1].instanceId;
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,id),findObjectById(s,target));
        if(!profile.weapon || runtimeEffectiveWeaponRange(s,*findObjectById(s,id),*findObjectById(s,target),*profile.weapon)!=10.0)
            return fail("air range did not become 10 cells");
        applySelection(s,{id},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        if(!fire(s,id))return fail("manual air attack did not fire: "+s.simulationStatus);
        const auto* source=findObjectById(s,id);
        if(shot(s,id)->weaponId!=(elite?"HoverMissileE":"HoverMissile"))return fail("wrong elite/normal weapon");
        if(distance<=10 && (source->x!=12 || source->y!=12))return fail("inside air bonus range still caused a chase");
        if(runtimeWeaponDistanceToTarget(s,*source,*findObjectById(s,target),*profile.weapon)>10.001)
            return fail("fired beyond air range");
        if(distance==11 && source->x==12 && source->y==12)return fail("11-cell target fired on without approaching");
        if(hz==30 && !elite && distance==8){
            const std::string path=root+"/air-burst.ini";
            if(!saveRuntimeState(s,path,error))return fail("save: "+error);
            auto loaded=make(hz,true,distance);
            if(!loadRuntimeState(*loaded,path,error)
                || loaded->combatDetails.deterministicHash()!=s.combatDetails.deterministicHash())return fail("reload: "+error);
            const auto restored=runtimeWeaponProfileForObject(*loaded,*findObjectById(*loaded,id),findObjectById(*loaded,target));
            if(!restored.weapon || runtimeEffectiveWeaponRange(*loaded,*findObjectById(*loaded,id),*findObjectById(*loaded,target),*restored.weapon)!=10.0)
                return fail("reload lost air range");
        }
        ++cases;
    }
    for(int hz:{30,60})for(int distance:{6,8}){
        auto box=make(hz,false,distance);auto& s=*box;
        const int id=s.world.getObjects()[0].instanceId,target=s.world.getObjects()[1].instanceId;
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,id),findObjectById(s,target));
        if(!profile.weapon || runtimeEffectiveWeaponRange(s,*findObjectById(s,id),*findObjectById(s,target),*profile.weapon)!=6.0)
            return fail("ground target gained air range bonus");
        applySelection(s,{id},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        if(!fire(s,id) || runtimeWeaponDistanceToTarget(s,*findObjectById(s,id),*findObjectById(s,target),*profile.weapon)>6.001)
            return fail("ground firing/chase boundary");
        ++cases;
    }
    for(int hz:{30,60})for(bool air:{false,true}){
        auto box=make(hz,air,8);auto& s=*box;
        const int id=s.world.getObjects()[0].instanceId;s.tick=runtimeRuleFramesToTicks(4,hz);advanceRuntimeGuardBehavior(s);
        if(s.behaviors.contains(id)!=air)return fail("default Guard borrowed another passenger mode's range or missed air target");
        if(air && (!fire(s,id) || findObjectById(s,id)->x!=12 || findObjectById(s,id)->y!=12))return fail("Guard air target caused unnecessary chase");
        ++cases;
    }
    {
        auto box=make(30,true,8);auto& s=*box;
        auto& source=s.world.accessObjects()[0];auto& target=s.world.accessObjects()[1];
        const auto* weapon=s.rules.findWeaponType("HoverMissile");
        for(int height:{1,8}){
            target.airborne=true;target.level=height;
            if(runtimeEffectiveWeaponRange(s,source,target,*weapon)!=10.0)return fail("air altitude changed the authored bonus");
        }
        target.airborne=false;target.level=0;
        if(runtimeEffectiveWeaponRange(s,source,target,*weapon)!=6.0)return fail("landed target retained air bonus");
        ++cases;
    }
    for(const std::string passengerType:{"GGI","E1"}){
        auto box=make(30,true,11);auto& s=*box;
        const int id=s.world.getObjects()[0].instanceId,target=s.world.getObjects()[1].instanceId;
        const auto* passenger=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,passengerType,{12,13});
        if(!passenger)return fail("passenger fixture");const int passengerId=passenger->instanceId;
        applySelection(s,{passengerId},false,false);
        if(!queueRuntimeTransportTarget(s,*findMutableObjectById(s,id)) || !waitForRuntimeTransportSmokeMotion(s,id,passengerId))
            return fail("passenger boarding");
        eraseRuntimeBehavior(s,passengerId);
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,id),findObjectById(s,target));
        if(passengerType=="GGI"){
            if(!profile.weapon || profile.weapon->id!="CRMissileLauncher"
                || runtimeEffectiveWeaponRange(s,*findObjectById(s,id),*findObjectById(s,target),*profile.weapon)!=12.0)
                return fail("GGI mode did not use 8+4 air range");
            applySelection(s,{id},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
            if(!fire(s,id) || findObjectById(s,id)->x!=12 || findObjectById(s,id)->y!=12)
                return fail("GGI mode chased an 11-cell air target");
            eraseRuntimeBehavior(s,id);
        }else if(profile.weapon)return fail("air range bonus enabled a ground-only gunner weapon");
        if(unloadRuntimeTransport(s,*findMutableObjectById(s,id))!=1)return fail("passenger unload");
        const auto empty=runtimeWeaponProfileForObject(s,*findObjectById(s,id),findObjectById(s,target));
        if(!empty.weapon || empty.weapon->id!="HoverMissile"
            || runtimeEffectiveWeaponRange(s,*findObjectById(s,id),*findObjectById(s,target),*empty.weapon)!=10.0)
            return fail("unload did not restore 6+4 air range");
        ++cases;
    }
    std::cout<<"Air range passed: "<<cases<<" cases; FV ground=6 air=10, elite, 30/60 Hz, Guard, chase, landing, GGI=12, E1 AA rejection, unload and full save\n";
    return true;
}
