bool runRuntimeFinalDefenseSmoke(const RuntimeConfig& config,std::string& error){
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Final defense presentation: "+why; return false;};
    auto live=std::make_unique<RuntimeWindowState>(); live->config=config;
    live->report=buildRuntimeStartupReport(config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success) return fail("original resources unavailable");
    const auto make=[&](const std::string& type,int hz,int rank){
        auto p=std::make_unique<RuntimeWindowState>(); auto& s=*p;
        s.config=config; s.config.fixedTickHz=hz; s.config.computerAiEnabled=false;
        s.report=live->report; s.rules=live->rules; s.art=live->art; s.theater=live->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
        // Preserve a loaded world summary for the full native map renderer.
        s.world=live->world; s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner="Russians"; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        for(int y=1;y<=28;++y) for(int x=1;x<=28;++x){
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
        }
        WorldObjectInstance source; source.instanceId=1; source.type=type; source.alive=true;
        source.kind=type=="SHK" ? WorldObjectKind::Infantry : type=="NAFLAK" ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        source.owner="Russians"; source.health=1000; source.mission="Guard"; source.poweredOperational=true;
        source.x=source.y=10; source.cell=runtimeCellKey(10,10); source.veterancy=rank; source.currentAmmo=-1;
        auto target=source; target.instanceId=2; target.type="MTNK"; target.kind=WorldObjectKind::Unit;
        target.owner="Americans"; target.x=14; target.y=10; target.cell=runtimeCellKey(14,10);
        target.veterancy=0; target.health=400; target.mission="Sleep"; target.weaponCooldownTicksRemaining=100000;
        if(type=="NAFLAK"){target.type="DISK"; target.airborne=true; target.flightLevel=target.level=4;}
        s.world.accessObjects()={source,target}; initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        return p;
    };
    int arcs=0;
    for(int hz:{30,60}) for(int rank:{0,2}) for(const std::string type:{"SHK","TTNK"}) for(int facing=0;facing<256;facing+=32){
        auto p=make(type,hz,rank); auto& s=*p;
        auto* source=findMutableObjectById(s,1); source->facing=source->turretFacing=facing;
        source->mission="Fire";
        source->subCell=type=="SHK" ? (facing/32)%5 : -1;
        const auto profile=runtimeWeaponProfileForObject(s,*source,findObjectById(s,2));
        if(!profile.weapon || !profile.weapon->electricBolt) return fail(type+" native electric weapon missing");
        spawnRuntimeProjectile(s,*source,*findObjectById(s,2),*profile.weapon,profile,1);
        if(s.projectiles.size()!=1) return fail(type+" first bolt missing");
        const auto shot=s.projectiles.front();
        if(!applyRuntimeProjectileImpact(s,shot)) return fail(type+" impact rejected");
        const auto check=[&](){
            const auto snapshots=s.specialMechanisms.renderSnapshots(s.tick,hz);
            const auto arc=std::find_if(snapshots.begin(),snapshots.end(),[](const auto& v){return v.kind=="electricArc";});
            if(arc==snapshots.end()) return false;
            const auto first=runtimeProjectCellXY(shot.sourceX,shot.sourceY,shot.sourceZ,s.camera);
            const auto second=runtimeProjectCellXY(arc->sourceX,arc->sourceY,arc->sourceLevel,s.camera);
            return std::abs(second.x-first.x-shot.sourceScreenOffsetX)<=1.0
                && std::abs(second.y-first.y-shot.sourceScreenOffsetY)<=1.0;
        };
        if(!check()) return fail(type+" second bolt lost the sprite muzzle alignment, facing="+std::to_string(facing));
        std::ostringstream saved; s.specialMechanisms.save(saved); IniFile ini; ini.loadFromString(saved.str());
        RuntimeSpecialMechanismState restored; restored.reset(s.rules.getSpecialMechanismRules());
        if(!restored.load(ini,error) || restored.stateHash()!=s.specialMechanisms.stateHash()) return fail("electric arc save failed: "+error);
        s.specialMechanisms=std::move(restored);
        if(!check()) return fail("electric origin moved after reload");
        if(hz==30 && rank==0 && facing==32){
            if(const char* folder=std::getenv("CONCORD_FINAL_DEFENSE_DIR")) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{12,10},640,400,error,
                    std::string(folder)+"/tesla-"+type+".bmp",false);
                if(!stats || !stats->rendered || stats->visibleWorldObjects<2
                    || stats->specialMechanismRenderActors<1)
                    return fail("native Tesla scene was not rendered: "+error);
            }
        }
        ++arcs;
    }
    auto flak=make("NAFLAK",30,0);
    for(int facing=0;facing<256;facing+=32){
        auto* source=findMutableObjectById(*flak,1); source->facing=source->turretFacing=facing;
        const auto profile=runtimeWeaponProfileForObject(*flak,*source,findObjectById(*flak,2));
        if(!profile.weapon) return fail("native flak weapon missing");
        const auto muzzle=runtimeProjectileSourcePosition(*flak,*source,profile,facing);
        const auto* rules=flak->rules.findBuildingType("NAFLAK");
        if(muzzle.screenOffsetY!=rules->turretAnimY) return fail("flak muzzle lost its foundation compensation");
    }
    RuntimeMapRenderContext context;
    context.rules=&live->rules; context.art=&live->art; context.theater=&live->theater;
    context.assetRootPath=runtimeAssetRootPath(config);
    context.structureRootPath=runtimeStructureRootPath(config,live->report.map);
    context.palettePath=runtimePalettePath(config,live->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,live->report.map);
    if(!runRuntimeFlakHeightRenderContractCheck(context,*findObjectById(*flak,1),error)) return false;
    std::cout<<"Final defense presentation passed: "<<arcs<<" stock Tesla origin/save cases, 8 flak muzzles and native 8-facing height comparison\n";
    return true;
}
