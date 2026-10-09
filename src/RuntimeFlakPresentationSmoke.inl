bool runRuntimeFlakPresentationSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="flak presentation: "+why; return false;};
    const char* output=std::getenv("CONCORD_FLAK_RENDER_DIR");
    const std::string root=output?output:"";
    if (!root.empty()) std::filesystem::create_directories(root);
    const char* debug=std::getenv("CONCORD_DEBUG_TURRET_FACING");
    const std::string previousDebug=debug?debug:"";
    struct RestoreDebug {std::string value; ~RestoreDebug(){_putenv_s("CONCORD_DEBUG_TURRET_FACING",value.c_str());}} restore{previousDebug};
    // Readback must observe the GDI lines, not a pending command batch.
    struct RestoreGdiBatch {DWORD limit; ~RestoreGdiBatch(){GdiSetBatchLimit(limit);}} batch{GdiSetBatchLimit(1)};
    auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
    seedRuntimeLoadScratch(s,live,false);
    s.world=live.world; s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
    s.world.accessTerrainCells().clear(); s.world.accessObjects().clear();
    s.economy.owner="Russians"; s.runtimeSuppressPlayerAudioFeedback=true;
    s.runtimeTargetLinesEnabled=false;
    for(int y=48;y<=80;++y) for(int x=48;x<=80;++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance cannon;
    cannon.instanceId=1; cannon.type="NAFLAK"; cannon.kind=WorldObjectKind::Structure;
    cannon.owner="Russians"; cannon.alive=true; cannon.health=s.rules.findBuildingType("NAFLAK")->strength;
    cannon.x=cannon.y=64; cannon.cell=runtimeCellKey(64,64); cannon.mission="Guard";
    auto target=cannon; target.instanceId=2; target.type="DISK"; target.kind=WorldObjectKind::Unit;
    target.owner="Americans"; target.x=66; target.y=62; target.cell=runtimeCellKey(66,62);
    target.airborne=true; target.flightLevel=target.level=6; target.health=1000;
    s.world.accessObjects()={cannon,target}; initializeRuntimeNavigation(s);
    const auto* flak=s.rules.findProjectileType("FlakProj");
    if(!flak || !flak->inviso || !flak->imageIsNone) return fail("stock invisible FlakProj missing");
    int totalChanged=0;
    for(const bool selected : {true,false}) for(int facing=0;facing<256;facing+=32) {
        auto* source=findMutableObjectById(s,1); source->facing=source->turretFacing=facing;
        s.selectedObjectIds=selected?std::vector<int>{1}:std::vector<int>{};
        s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear();
        if(!selected) {RuntimeObjectBehavior attack; attack.kind=RuntimeBehaviorKind::Attack;
            attack.targetObjectId=2; s.behaviors[1]=attack;}
        const auto profile=runtimeWeaponProfileForObject(s,*source,findObjectById(s,2));
        if(!profile.weapon || profile.weapon->projectile!="FlakProj") return fail("native anti-air weapon missing");
        spawnRuntimeProjectile(s,*source,*findObjectById(s,2),*profile.weapon,profile,1.0);
        std::vector<std::uint32_t> normal, diagnostic;
        const std::string name=(selected?"selected-":"attacking-")+std::to_string(facing);
        _putenv_s("CONCORD_DEBUG_TURRET_FACING","");
        const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,64},800,600,error,
            root.empty()?"":root+"/"+name+".bmp",false,true,-1,&normal);
        _putenv_s("CONCORD_DEBUG_TURRET_FACING","1");
        if(!stats || !drawRuntimeStateOffscreenAtCell(s,{64,64},800,600,error,
                root.empty() || (facing!=0 && facing!=32)?"":root+"/"+name+"-old-line.bmp",false,true,-1,&diagnostic)) return fail(error);
        int changed=0;
        for(std::size_t i=0;i<normal.size();++i) if(normal[i]!=diagnostic[i]) {
            if((diagnostic[i]&0x00ffffffU)!=0x00ffd65cU) return fail("unexpected pixels beyond the yellow debug line");
            ++changed;
        }
        totalChanged+=changed;
        if(stats->projectileActorTravelDrawn!=0 || stats->projectileActorTravelMissing!=0
            || stats->weaponAnimDrawn<=0 || stats->weaponAnimMissing!=0) return fail("invisible projectile / muzzle / line comparison: "
                +name+" changed="+std::to_string(changed)+" travel="+std::to_string(stats->projectileActorTravelDrawn)
                +" travelMissing="+std::to_string(stats->projectileActorTravelMissing)
                +" muzzle="+std::to_string(stats->weaponAnimDrawn)+" muzzleMissing="+std::to_string(stats->weaponAnimMissing));
        std::cout<<"Flak native render: "<<name<<", removed-yellow-pixels="<<changed<<", invisible shot and GUNFIRE intact PASS\n";
    }
    if(totalChanged<20) return fail("legacy yellow line not reproduced in native readback");
    return true;
}
