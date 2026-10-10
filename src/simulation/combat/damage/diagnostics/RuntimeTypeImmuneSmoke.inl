bool runRuntimeTypeImmuneSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){ error="TypeImmune: "+why; return false; };
    auto stock=std::make_unique<RuntimeWindowState>(); stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,
        &stock->art,&stock->rules,&stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    if (!stock->rules.findUnitType("UnitType","DLPH")->typeImmune
        || !stock->rules.findUnitType("InfantryType","YURI")->typeImmune
        || stock->rules.findUnitType("UnitType","MTNK")->typeImmune)
        return fail("retail unit flags");
    const std::string root="build/smoke/feature-checks/07-type-immune/checks";
    std::filesystem::create_directories(root);
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;
    rebuildRuntimeTerrainInfoCache(*stock);
    const TerrainCell* waterTile=nullptr;
    for(const auto& cell:stock->world.getTerrainCells()) {
        const auto& info=stock->terrainInfoByCell.at(runtimeCellKey(cell.x,cell.y));
        if(info.movementLandType==RuntimeTerrainLandType::Water && info.water
            && !info.bridge && !info.shore && !info.ramp) { waterTile=&cell;break; }
    }
    if(!waterTile) return fail("original map has no water TMP for a persistent fixture");
    const auto make=[&](int hz,bool waterSurface=true) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
        s.config=config; s.config.fixedTickHz=hz; s.config.playerSlots.clear();
        s.config.gameMode="BATTLE"; s.config.noCooldown=s.config.computerAiEnabled=false;
        s.config.multiplayerActive=s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.rules=stock->rules; s.art=stock->art; s.theater=stock->theater; s.report=stock->report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
        s.economy.owner="Americans"; s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true; s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        s.skirmishTeamByOwner["americans"]="team1"; s.skirmishTeamByOwner["french"]="team1";
        for (int y=1;y<=24;++y) for(int x=1;x<=32;++x) {
            TerrainCell c; c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);
            c.tileIndex=waterSurface?waterTile->tileIndex:0;
            c.subTileIndex=waterSurface?waterTile->subTileIndex:0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        // Use authored TMP metadata, so loading the fixture reconstructs the same water.
        return box;
    };
    const auto add=[](RuntimeWindowState& s, RuntimeBuildKind kind, const char* type,
        const char* owner, RuntimeCellPoint p) {
        auto* o=addRuntimeObjectInstanceForOwner(s,owner,kind,type,p);
        if (!o) return -1;
        o->mission="Sleep"; o->facing=runtimeFacingFromDelta(1,0);
        return o->instanceId;
    };
    const auto pulse=[](RuntimeWindowState& s,int source,int target,const char* weaponId,bool force=true) {
        const auto* victim=findObjectById(s,target); const auto* w=findRuntimeWeaponType(s.rules,weaponId);
        RuntimeProjectileEffect p; p.actorId=nextRuntimeProjectileActorId(s);
        p.attackerObjectId=source;p.targetObjectId=target;p.owner="Americans";
        p.weaponId=weaponId;p.warheadId=w->warhead;p.damage=w->damage;
        p.toCell=runtimeCellPointForObject(*victim);p.targetX=victim->x;p.targetY=victim->y;
        p.hasTargetPosition=true;p.ag=true;p.aa=true;p.affectsAllies=force;p.affectsEnemies=true;
        p.cellSpread=findRuntimeWarheadType(s.rules,w->warhead)->cellSpread;
        return p;
    };
    int cases=0;
    for (int hz:{30,60}) for (bool elite:{false,true}) for (int relation=0;relation<4;++relation) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const char* owner=relation==1?"French":relation==2?"Russians":"Americans";
        const int target=add(s,RuntimeBuildKind::Unit,relation==3?"DEST":"DLPH",owner,{10,12});
        findMutableObjectById(s,source)->veterancy=elite?2:0;
        const int before=findObjectById(s,target)->health;
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target),true);
        if (s.lastCommand.acceptedCount!=1) return fail("actual forced command rejected");
        bool launched=false,ambient=false;
        for(int i=0;i<hz*4;++i) {
            ++s.tick;advanceRuntimeSimulation(s);
            for(const auto& p:s.projectiles) if(p.attackerObjectId==source) {
                launched=true;ambient=ambient||p.renderIntent=="sonicWave";
            }
        }
        const auto* targetAfter=findRuntimeObjectByIdIncludingInactive(s,target);
        const bool immune=relation==0;
        if (!launched || !ambient || !targetAfter || ((targetAfter->health==before)!=immune))
            return fail("actual sonic direct/house/type matrix hz="+std::to_string(hz)
                +" relation="+std::to_string(relation)+" health="+(targetAfter?std::to_string(targetAfter->health):"missing")
                +" launched="+std::to_string(launched)+" wave="+std::to_string(ambient));
        ++cases;
    }
    for (int hz:{30,60}) {
        // A real wave crosses friendly and enemy cells before reaching its aim.
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int friendId=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{6,12});
        const int ally=add(s,RuntimeBuildKind::Unit,"DLPH","French",{7,12});
        const int enemy=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{8,12});
        const int outside=add(s,RuntimeBuildKind::Unit,"DLPH","French",{7,14});
        const int target=add(s,RuntimeBuildKind::Unit,"CARRIER","Russians",{11,12});
        const int friendHP=findObjectById(s,friendId)->health, allyHP=findObjectById(s,ally)->health;
        const int enemyHP=findObjectById(s,enemy)->health,outHP=findObjectById(s,outside)->health;
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        for(int i=0;i<hz*2;++i){++s.tick;advanceRuntimeSimulation(s);}
        if (findRuntimeObjectByIdIncludingInactive(s,friendId)->health!=friendHP
            || findRuntimeObjectByIdIncludingInactive(s,ally)->health>=allyHP
            || findRuntimeObjectByIdIncludingInactive(s,enemy)->health>=enemyHP
            || findRuntimeObjectByIdIncludingInactive(s,outside)->health!=outHP)
            return fail("real along-path sonic immunity/ally/enemy/off-path");
        ++cases;
    }
    for (int hz:{30,60}) for(int mode=0;mode<6;++mode) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{10,12});
        auto p=pulse(s,source,target,"SonicZap");
        if(mode==0) const_cast<UnitType*>(s.rules.findUnitType("UnitType","DLPH"))->typeImmune=false;
        if(mode==1) findMutableObjectById(s,source)->owner="Russians";
        if(mode==2) findMutableObjectById(s,target)->owner="Russians";
        if(mode==3) findMutableObjectById(s,source)->type="DEST";
        if(mode==4) {auto* o=findMutableObjectById(s,source);o->alive=false;o->health=0;o->mission="Destroyed";}
        if(mode==5) p.attackerObjectId=99999;
        const int before=findObjectById(s,target)->health;
        (void)applyRuntimeProjectileImpact(s,p);
        if(findObjectById(s,target)->health>=before) return fail("disabled/changed/orphan source mode="+std::to_string(mode));
        ++cases;
    }
    for (int hz:{30,60}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Infantry,"YURI","Americans",{5,12});
        const int same=add(s,RuntimeBuildKind::Infantry,"YURI","Americans",{10,12});
        const int other=add(s,RuntimeBuildKind::Infantry,"E1","Americans",{11,12});
        const int ally=add(s,RuntimeBuildKind::Infantry,"YURI","French",{10,13});
        const int enemy=add(s,RuntimeBuildKind::Infantry,"YURI","Russians",{11,13});
        const int before=findObjectById(s,same)->health;
        auto p=pulse(s,source,same,"PsiWave");(void)applyRuntimeProjectileImpact(s,p);
        if (!findObjectById(s,same) || findObjectById(s,same)->health!=before
            || findRuntimeObjectByIdIncludingInactive(s,other)->health>0
            || findRuntimeObjectByIdIncludingInactive(s,ally)->health>0
            || findRuntimeObjectByIdIncludingInactive(s,enemy)->health>0)
            return fail("stock Yuri pulse owner/type area matrix");
        ++cases;
    }
    for (int hz:{30,60}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{10,12});
        findMutableObjectById(s,target)->health=100;
        auto* w=const_cast<WeaponType*>(s.rules.findWeaponType("SonicZap"));w->damage=-20;
        auto p=pulse(s,source,target,"SonicZap");(void)applyRuntimeProjectileImpact(s,p);
        if(findObjectById(s,target)->health<=100) return fail("negative damage healing was blocked");
        if(runtimeDamageIsTypeImmune(s,*findObjectById(s,target),findObjectById(s,source),20,true))
            return fail("explicit ignore-defenses bypass");
        ++cases;
    }
    for (const auto& [kind,type]:std::vector<std::pair<RuntimeBuildKind,const char*>>{
            {RuntimeBuildKind::Infantry,"E1"},{RuntimeBuildKind::Unit,"MTNK"},
            {RuntimeBuildKind::Unit,"ORCA"},{RuntimeBuildKind::Building,"GAPOWR"}}) {
        auto box=make(30);auto& s=*box;
        const int source=add(s,kind,type,"Americans",{5,12});
        const int target=add(s,kind,type,"Americans",{10,12});
        if(source<0||target<0) return fail("custom category creation");
        if(kind==RuntimeBuildKind::Building)
            const_cast<BuildingType*>(findRuntimeBuildingTypeById(s.rules,type))->typeImmune=true;
        else const_cast<UnitType*>(findRuntimeUnitType(s.rules,*findObjectById(s,target)))->typeImmune=true;
        auto p=pulse(s,source,target,"SonicZap");const int before=findObjectById(s,target)->health;
        (void)applyRuntimeProjectileImpact(s,p);
        if(findObjectById(s,target)->health!=before) return fail("custom category immunity");
        ++cases;
    }
    for(int hz:{30,60}) for(bool stopped:{false,true}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int friendly=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{7,12});
        const int target=add(s,RuntimeBuildKind::Unit,"CARRIER","Russians",{11,12});
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        for(int i=0;i<hz/3;++i){++s.tick;advanceRuntimeSimulation(s);}
        bool wave=false;for(const auto& p:s.projectiles) wave=wave||p.renderIntent=="sonicWave";
        if(!wave) return fail("no active wave to save");
        if(stopped) {
            applySelection(s,{source},false,false);issueRuntimeStopCommand(s);
            for(int i=0;i<hz/15;++i){++s.tick;advanceRuntimeSimulation(s);}
        }
        const std::string path=root+"/wave-"+std::to_string(hz)+(stopped?"-stopped":"")+".ini";
        if(!saveRuntimeState(s,path,error)) return fail("wave save: "+error);
        auto restored=make(hz);if(!loadRuntimeState(*restored,path,error)) return fail("wave load: "+error);
        if(!restored->terrainInfoByCell.at(runtimeCellKey(11,12)).water)
            return fail("saved water TMP did not restore water");
        if(s.projectiles.size()!=restored->projectiles.size()) return fail("wave initial restore count");
        for(std::size_t n=0;n<s.projectiles.size();++n)
            if(serializeRuntimeProjectileEffect(s.projectiles[n])!=serializeRuntimeProjectileEffect(restored->projectiles[n]))
                return fail("wave initial restore actor differs");
        for(int i=0;i<hz*2;++i) {
            ++s.tick;advanceRuntimeSimulation(s);++restored->tick;advanceRuntimeSimulation(*restored);
            for(int id:{source,friendly,target}) {
                const auto* a=findRuntimeObjectByIdIncludingInactive(s,id);
                const auto* b=findRuntimeObjectByIdIncludingInactive(*restored,id);
                if(!a||!b||a->health!=b->health||a->alive!=b->alive) {
                    std::string ignored;
                    (void)saveRuntimeState(s,root+"/live-after.ini",ignored);
                    (void)saveRuntimeState(*restored,root+"/restored-after.ini",ignored);
                    const auto describe=[&](const RuntimeWindowState& state) {
                        std::ostringstream out;
                        for(const auto& p:state.projectiles) if(p.renderIntent=="sonicWave")
                            out<<" actor="<<p.actorId<<" elapsed="<<p.elapsedTicks<<" front="<<p.progress
                               <<" tail="<<runtimeSonicWaveTail(p)<<" speed="<<p.speedCellsPerTick;
                        const auto b=state.behaviors.find(source);
                        if(b!=state.behaviors.end()) out<<" behavior="<<static_cast<int>(b->second.kind)
                            <<" target="<<b->second.targetObjectId<<" cd="<<b->second.weaponCooldownTicks;
                        return out.str();
                    };
                    return fail("wave save diverged tick="+std::to_string(s.tick)+" object="+std::to_string(id)
                        +" hp="+(a?std::to_string(a->health):"missing")+"/"+(b?std::to_string(b->health):"missing")
                        +" live:"+describe(s)+" restored:"+describe(*restored));
                }
            }
        }
        ++cases;
    }
    {
        auto box=make(30);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{10,12});
        s.campaignMode=true;s.triggers.campaignCanonicalHouseByAlias["authoredplayer"]="Americans";
        findMutableObjectById(s,source)->owner="AuthoredPlayer";
        const int before=findObjectById(s,target)->health;
        (void)applyRuntimeProjectileImpact(s,pulse(s,source,target,"SonicZap"));
        if(findObjectById(s,target)->health!=before) return fail("same-house campaign alias");
        ++cases;
    }
    for(int hz:{30,60}) {
        auto box=make(hz,false);auto& s=*box;
        const int host=add(s,RuntimeBuildKind::Unit,"BFRT","Americans",{5,12});
        const int passenger=add(s,RuntimeBuildKind::Infantry,"YURI","Americans",{5,13});
        const int target=add(s,RuntimeBuildKind::Infantry,"YURI","Americans",{10,12});
        const int ally=add(s,RuntimeBuildKind::Infantry,"YURI","French",{10,13});
        applySelection(s,{passenger},false,false);
        if(!queueRuntimeTransportTarget(s,*findMutableObjectById(s,host))
            || !waitForRuntimeTransportSmokeMotion(s,host,passenger)) return fail("real passenger boarding");
        eraseRuntimeBehavior(s,passenger);
        if(!runtimeTypeImmuneSource(s,passenger)
            || findRuntimeObjectByIdIncludingInactive(s,passenger)->alive)
            return fail("live passenger source identity");
        const int before=findObjectById(s,target)->health;
        auto p=pulse(s,passenger,target,"PsiWave");p.sourceAnchorObjectId=host;
        (void)applyRuntimeProjectileImpact(s,p);
        if(findRuntimeObjectByIdIncludingInactive(s,target)->health!=before
            || findRuntimeObjectByIdIncludingInactive(s,ally)->health>0)
            return fail("passenger own-house immunity and allied-house damage");
        ++cases;
    }
    {
        auto box=make(30);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DEST","Russians",{8,12});
        // An already-created wave must use the owner's CURRENT primary weapon.
        auto shot=pulse(s,source,target,"SonicZap");shot.sourceX=5;shot.sourceY=12;
        shot.targetX=8;shot.targetY=12;shot.fromCell={5,12};shot.spawnTick=s.tick;
        spawnRuntimeSonicWave(s,shot,*s.rules.findWeaponType("SonicZap"));
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        RuntimeProjectileEffect wave=s.projectiles.back();s.projectiles.clear();
        wave.progress=0.9;wave.currentX=5;wave.currentY=12;
        findMutableObjectById(s,source)->veterancy=2;
        const int before=findObjectById(s,target)->health;
        if(!advanceRuntimeSonicWave(s,wave)||!advanceRuntimeSonicWave(s,wave)
            || before-findObjectById(s,target)->health!=12)
            return fail("live elite primary AmbientDamage=15, heavy armor=80 percent");
        ++cases;
    }
    for(int hz:{30,60}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int reached=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{7,12});
        const int beyond=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{8,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{10,12});
        auto shot=pulse(s,source,target,"SonicZap");shot.sourceX=5.5;shot.sourceY=12;
        spawnRuntimeSonicWave(s,shot,*s.rules.findWeaponType("SonicZap"));
        RuntimeProjectileEffect wave=s.projectiles.back();s.projectiles.clear();
        wave.progress=0.35;
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        const int reachedHP=findObjectById(s,reached)->health,beyondHP=findObjectById(s,beyond)->health;
        for(int i=0;i<hz/15;++i) (void)advanceRuntimeSonicWave(s,wave);
        if(findObjectById(s,reached)->health>=reachedHP || findObjectById(s,beyond)->health!=beyondHP)
            return fail("sonic front half-step boundary");
        ++cases;
    }
    for(int hz:{30,60}) for(int distance:{8,9}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{5+distance,12});
        auto shot=pulse(s,source,target,"SonicZap");shot.sourceX=5.5;shot.sourceY=12;
        spawnRuntimeSonicWave(s,shot,*s.rules.findWeaponType("SonicZap"));
        RuntimeProjectileEffect wave=s.projectiles.back();s.projectiles.clear();
        applySelection(s,{source},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
        bool active=true;for(int i=0;i<hz/15 && active;++i) active=advanceRuntimeSonicWave(s,wave);
        if(active!=(distance==8)) return fail("sonic six-diagonal-cell tracking distance");
        ++cases;
    }
    {
        auto box=make(30);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int target=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{6,12});
        auto shot=pulse(s,source,target,"SonicZap");shot.sourceX=5.5;shot.sourceY=12;
        spawnRuntimeSonicWave(s,shot,*s.rules.findWeaponType("SonicZap"));
        if(!s.projectiles.empty()) return fail("sub-240-lepton sonic wave should not spawn");
        ++cases;
    }
    for(int hz:{30,60}) for(bool reverse:{false,true}) {
        auto box=make(hz);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",reverse?RuntimeCellPoint{9,9}:RuntimeCellPoint{5,5});
        const int target=add(s,RuntimeBuildKind::Unit,"CARRIER","Russians",reverse?RuntimeCellPoint{5,5}:RuntimeCellPoint{9,9});
        const int edge=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{7,8});
        const int friendEdge=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{7,8});
        const int center=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",reverse?RuntimeCellPoint{7,7}:RuntimeCellPoint{8,8});
        const int outside=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{7,9});
        auto shot=pulse(s,source,target,"SonicZap");
        shot.sourceX=shot.sourceY=reverse?9.5:5.5;
        shot.targetX=shot.targetY=reverse?5.5:9.5;
        spawnRuntimeSonicWave(s,shot,*s.rules.findWeaponType("SonicZap"));
        RuntimeProjectileEffect wave=s.projectiles.back();s.projectiles.clear();
        wave.speedCellsPerTick=0.0;wave.progress=0.7;wave.currentX=wave.currentY=7.5;
        const int edgeHP=findObjectById(s,edge)->health,friendHP=findObjectById(s,friendEdge)->health;
        const int centerHP=findObjectById(s,center)->health,outHP=findObjectById(s,outside)->health;
        for(int i=0;i<hz/15;++i) (void)advanceRuntimeSonicWave(s,wave);
        const auto order=runtimeSonicWaveDamageCells(s,wave,runtimeSonicWaveTail(wave));
        if(order.size()<2 || order[0]!=runtimeCellKey(7,8)
            || order[1]!=(reverse?runtimeCellKey(7,7):runtimeCellKey(8,8)))
            return fail("released wave cells lost their causal sampling order");
        if(findObjectById(s,edge)->health>=edgeHP || findObjectById(s,center)->health>=centerHP
            || findObjectById(s,friendEdge)->health!=friendHP || findObjectById(s,outside)->health!=outHP)
            return fail("released diagonal side/centre/immune/off-path reverse="+std::to_string(reverse));
        ++cases;
    }
    for(bool upper:{false,true}) {
        auto box=make(30);auto& s=*box;
        const int source=add(s,RuntimeBuildKind::Unit,"DLPH","Americans",{5,12});
        const int below=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{10,12});
        const int above=add(s,RuntimeBuildKind::Unit,"DLPH","Russians",{10,12});
        auto* tile=const_cast<TerrainCell*>(s.terrainByCell.at(runtimeCellKey(10,12)));
        tile->hasOverlay=true;tile->overlayIndex=24;tile->overlayType="BRIDGE";tile->overlayFrame=0;
        findMutableObjectById(s,above)->onBridge=true;findMutableObjectById(s,above)->level=4;
        findMutableObjectById(s,below)->onBridge=false;findMutableObjectById(s,below)->level=0;
        s.bridgeHealthByCell[runtimeCellKey(10,12)]=100;
        const int belowHP=findObjectById(s,below)->health,aboveHP=findObjectById(s,above)->health;
        auto p=pulse(s,source,below,"SonicZap");p.renderIntent="sonicAmbientPulse";
        p.targetObjectId=-1;p.cellSpread=0;p.damage=10;p.particleDamagePulse=true;
        p.suppressWarheadParticleSpawn=true;p.sourceZ=upper?4.0:0.0;
        (void)applyRuntimeProjectileImpact(s,p);
        if((findObjectById(s,below)->health<belowHP)==upper
            || (findObjectById(s,above)->health<aboveHP)!=upper
            || s.bridgeHealthByCell.at(runtimeCellKey(10,12))!=100)
            return fail("sonic bridge contents must use one height layer and not blast the deck");
        ++cases;
    }
    for(const char* category:{"InfantryTypes","VehicleTypes","AircraftTypes","BuildingTypes"}) {
        std::uint64_t baseline=0;
        for(int setting=0;setting<3;++setting) {
            IniFile ini;RulesDatabase rules;
            const std::string text="["+std::string(category)+"]\n0=TEST\n[TEST]\nStrength=100\n"
                +(setting==0?"":setting==1?"TypeImmune=no\n":"TypeImmune=yes\n");
            if(!ini.loadFromString(text)||!rules.loadFromIni(ini)) return fail("rule parsing fixture");
            const auto hash=rules.getDetailRulesHash();
            if(setting==0) baseline=hash;
            else if((hash==baseline)!=(setting==1)) return fail("rule default/compatibility category "+std::string(category));
        }
        ++cases;
    }
    std::cout<<"TypeImmune passed: "<<cases<<" cases; stock dolphins/Yuri, direct and actual sonic path, "
        "same house/allies/enemies/types, 30/60 Hz, elite, source changes/removal, healing, "
        "custom categories, wave save/load and defaults/hash\n";
    return true;
}
