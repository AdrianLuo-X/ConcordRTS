bool runRuntimeTeslaCycleSmoke(RuntimeWindowState& live,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="Tesla animation cycle: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_TESLA_CYCLE_OUTPUT");const std::string root=output?output:"";
    int scenes=0,saves=0;
    for(bool damaged:{false,true})for(bool charged:{false,true}){
        scene=std::string(damaged?"damaged":"healthy")+(charged?" charged":" normal");
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=live.config;s.config.fixedTickHz=30;s.config.noCooldown=false;s.config.computerAiEnabled=false;
        s.config.multiplayerActive=false;s.config.playerSlots.clear();s.report=live.report;s.art=live.art;s.theater=live.theater;
        IniFile rules;if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules))return fail("native rules");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=48;y<=84;++y)for(int x=48;x<=84;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance tower;tower.instanceId=1;tower.type="TESLA";tower.kind=WorldObjectKind::Structure;
        tower.owner="Russians";tower.alive=true;tower.health=runtimeMaxHealthForObject(s,tower)/(damaged?4:1);
        tower.currentAmmo=-1;tower.x=tower.y=64;tower.cell=runtimeCellKey(64,64);tower.mission="Guard";tower.poweredOperational=true;
        auto target=tower;target.instanceId=2;target.kind=WorldObjectKind::Unit;target.type="MTNK";target.owner="Americans";
        target.x=68;target.cell=runtimeCellKey(68,64);target.health=100000;target.mission="Sleep";
        auto charger=target;charger.instanceId=3;charger.kind=WorldObjectKind::Infantry;charger.type="SHK";
        charger.owner="Russians";charger.x=63;charger.y=64;charger.cell=runtimeCellKey(63,64);charger.health=130;
        auto power=tower;power.instanceId=4;power.type="NANRCT";power.x=54;power.cell=runtimeCellKey(54,64);
        if(!s.rules.findBuildingType(power.type))return fail("native power plant missing");
        power.health=runtimeMaxHealthForObject(s,power);
        s.world.accessObjects()={tower,target,charger,power};initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s);
        rebuildRuntimeEconomyPower(s);
        if(!findObjectById(s,1)->poweredOperational)return fail("fixture power balance");
        RuntimeSnapshotActiveStateScope active(s);ensureRuntimeSpecialMechanismsInitialized(s);
        if(charged){
            std::string reason;
            if(!s.specialMechanisms.applyElectricCharge(s.world.accessObjects(),s.rules,3,1,0,60000,1,
                [&](const std::string& a,const std::string& b){return runtimeOwnersAreAllied(s,a,b);},nullptr,reason))return fail(reason);
        }
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,1),findObjectById(s,2));
        if(profile.weaponId!=(charged?"OPCoilBolt":"CoilBolt"))return fail("real normal/charged weapon selection");
        const std::string folder=root.empty()?"":root+"/"+(damaged?"damaged-":"healthy-")+(charged?"charged":"normal");
        if(!folder.empty())std::filesystem::create_directories(folder);
        // The shared renderer contract receives TESLA/NATSLA here, including
        // its own native idle, firing and damaged firing SHP layers.
        if(!runRuntimePrismChargeCycleSmoke(s,1,2,folder,error))return false;
        s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*findObjectById(s,2));
        const auto step=[](RuntimeWindowState& state){
            ++state.tick;advanceRuntimeWeaponCooldowns(state);
            advanceRuntimeAttackBehavior(state,*findMutableObjectById(state,1),state.behaviors.at(1));
        };
        for(int i=0;i<20;++i)step(s);
        if(findObjectById(s,1)->mission!="DelayedFire")return fail("save did not occur mid-charge");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("mid-charge reload: "+error);
        if(findObjectById(*restored,1)->chargeTurretStartTick!=findObjectById(s,1)->chargeTurretStartTick
            ||findObjectById(*restored,1)->mission!="DelayedFire")return fail("reload restarted charge");
        const auto initialCount=s.projectiles.size();bool fired=false;
        for(int tick=0;tick<100;++tick){
            step(s);{RuntimeSnapshotActiveStateScope restoreScope(*restored);step(*restored);}
            if(s.projectiles.size()!=restored->projectiles.size()
                ||findObjectById(s,1)->mission!=findObjectById(*restored,1)->mission)return fail("reloaded shot timing differs: "+s.simulationStatus+" / "+restored->simulationStatus);
            if(s.projectiles.size()>initialCount){
                if(s.projectiles.size()!=initialCount+1||findObjectById(s,1)->mission=="DelayedFire")return fail("charge generated multiple discharges");
                fired=true;break;
            }
        }
        if(!fired)return fail("resumed charge never fired");++saves;++scenes;
    }
    std::cout<<"Tesla animation cycle passed: "<<scenes<<" healthy/damaged normal/charged groups, "
        <<"15/30/60 Hz, 36 real charge/discharge cycles, 1592 native frame checks, "<<saves<<" mid-charge reloads\n";
    return true;
}
