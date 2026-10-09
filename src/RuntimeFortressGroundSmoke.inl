bool runRuntimeFortressGroundSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout<<std::unitbuf;
    const char* output=std::getenv("CONCORD_FORTRESS_GROUND_OUTPUT");
    const std::string root=output?output:"build/fortress-ground";
    std::filesystem::create_directories(root);
    std::ofstream records(root+"/shots.csv");
    records<<"scene,weapon,rounds,damage,frames\n";
    std::string scene;
    const auto fail=[&](const std::string& why){error="Fortress ground "+scene+": "+why;return false;};
    IniFile original,overlay;RulesDatabase rules;
    if(!original.loadFromFile(live.report.rulesPath)||!overlay.loadFromString("[E1]\nStrength=100000\n"))
        return fail("durable victim fixture");
    original.appendFrom(overlay);if(!rules.loadFromIni(original))return fail("rules fixture");
    // The combat fixture owns a dry flat patch; the original map and live world stay intact.
    auto tileCache=std::make_unique<RuntimeMapRenderCache>();
    const auto metadata=runtimeTerrainMetadataContext(live);
    std::optional<TerrainCell> flat;
    for(const auto& set:live.theater.getTileSets())for(int tile=set.startTileIndex;
        tile<set.startTileIndex+set.tilesInSet&&!flat;++tile)for(int sub=0;sub<16&&!flat;++sub) {
        TerrainCell c;c.tileIndex=tile;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,c);
        if(info.passable&&!info.water&&!info.bridge&&!info.cliff&&!info.shore&&!info.ramp
            &&resolveRuntimeTerrainSubTileRampType(c,metadata,*tileCache)==0
            &&resolveRuntimeTerrainSubTileLandType(c,metadata,*tileCache)==RuntimeTerrainLandType::Clear)flat=c;
    }
    if(!flat)return fail("original authored flat Clear TMP required");
    const auto inCombatPatch=[](int x,int y){return x>=40&&x<=55&&y>=40&&y<=49;};
    std::set<int> patchCells;for(const auto& c:live.world.getTerrainCells())
        if(inCombatPatch(c.x,c.y))patchCells.insert(runtimeCellKey(c.x,c.y));
    if(patchCells.size()!=160)return fail("complete original terrain rectangle required for combat fixture");
    std::cout<<"Fortress fixture original flat TMP tile="<<flat->tileIndex<<" sub="<<flat->subTileIndex
        <<" patch=40,40..55,49 cells="<<patchCells.size()<<'\n';
    struct Fixture {std::unique_ptr<RuntimeWindowState> box;int host=-1,passenger=-1;};
    const auto make=[&](int hz,const std::string& type,int rank) {
        Fixture f;f.box=std::make_unique<RuntimeWindowState>();auto& s=*f.box;
        seedRuntimeLoadScratch(s,live,false);s.rules=rules;s.rulesLoaded=true;
        s.config.fixedTickHz=hz;s.config.playerSlots.clear();s.config.noCooldown=false;
        s.config.playerCountry="Americans";s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
        s.visionEnabled=s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;s.economy.owner="Americans";
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(s.economy);
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(auto& c:s.world.accessTerrainCells())if(inCombatPatch(c.x,c.y)) {
            const int x=c.x,y=c.y;c=*flat;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.level=0;
        }
        // Copied original water facts must not override this scratch-only terrain.
        s.mapFacts={};s.mapFacts.theater=live.mapFacts.theater;s.mapFacts.lighting=live.mapFacts.lighting;
        s.mapFactsLoaded=false;s.terrainByCell.clear();s.terrainInfoByCell.clear();
        f.host=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"BFRT",{44,44})->instanceId;
        f.passenger=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,{44,45})->instanceId;
        findMutableObjectById(s,f.passenger)->veterancy=rank;
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);
        return f;
    };
    const auto board=[&](Fixture& f,int id) {
        auto& s=*f.box;applySelection(s,{id},false,false);
        if(!queueRuntimeTransportTarget(s,*findMutableObjectById(s,f.host))||!waitForRuntimeTransportSmokeMotion(s,f.host,id))return false;
        eraseRuntimeBehavior(s,id);return true;
    };
    const auto order=[&](Fixture& f,RuntimeCellPoint cell) {
        auto& s=*f.box;applySelection(s,{f.host},false,false);
        return dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,cell)
            && s.behaviors.contains(f.host)&&s.behaviors.at(f.host).attackGround;
    };
    const auto step=[](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s);++s.tick;
        advanceRuntimeCombatBursts(s);advanceRuntimeEffects(s);
    };
    struct Case {const char* type;const char* weapon;const char* elite;int normalDamage;int eliteDamage;};
    // Original values against armor=none, including transport 1.2 and elite
    // firepower 1.1. Flak is scattered and checked at its actual hit cells.
    const Case types[]={{"E1","Para","ParaE",30,33},{"E2","M1Carbine","M1CarbineE",18,26},
        {"GGI","MissileLauncher","MissileLauncherE",10,13},{"INIT","PsychicJab","PsychicJabE",30,40},
        {"SHK","ElectricBolt","ElectricBoltE",60,66},{"DESO","RadBeamWeapon","RadBeamWeaponE",150,264},
        {"FLAKT","FlakGuyGun","FlakGuyGunE",36,40},{"VIRUS","Virusgun","VirusgunE",150,165},
        {"TANY","DoublePistols","DoublePistolsE",150,165},{"BORIS","AKM","AKME",156,238},
        {"SNIPE","AWP","AWPE",300,330}};
    int cases=0,rounds=0,saves=0,paired=0,captures=0,boundaries=0;
    for(int hz:{15,30,60})for(int rank:{0,2})for(const auto& item:types) {
        scene=std::to_string(hz)+"-"+item.type+"-"+std::to_string(rank);
        auto f=make(hz,item.type,rank);auto& s=*f.box;RuntimeSnapshotActiveStateScope active(s);
        if(!board(f,f.passenger)||!order(f,{48,44}))return fail("actual boarding/cell order");
        ++s.tick;(void)advanceRuntimeCombatIndependentFire(s);
        if(s.projectiles.size()!=1)return fail("empty cell did not fire exactly one passenger round");
        const auto first=s.projectiles.front();
        const std::string weaponId=rank?item.elite:item.weapon;
        if(first.attackerObjectId!=f.passenger||first.weaponId!=weaponId||first.targetObjectId>=0
            ||!first.affectsAllies)return fail("passenger identity/weapon/forced target");
        const auto* weapon=s.rules.findWeaponType(weaponId);
        if(!weapon||first.electricBolt!=weapon->electricBolt)
            return fail("authored attack effect lost");
        if((std::string(item.type)=="INIT"||std::string(item.type)=="VIRUS")
            && std::none_of(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& fx){
                return fx.animId=="GUNFIRE"&&fx.attackerObjectId==f.passenger;}))return fail("transport muzzle absent");
        if(hz==30&&rank==0) {
            const auto drawn=drawRuntimeStateOffscreenAtCell(s,{46,44},1024,640,error,root+"/"+scene+".bmp",false);
            if(!drawn||!drawn->rendered)return fail("native effect draw: "+error);
            if(std::string(item.type)=="DESO"&&drawn->projectileActorTravelDrawn<=0)
                return fail("authored green ray was invisible");
            ++captures;
        }
        // A friendly victim enters the resolved impact cell after the shot.
        // Scatter is already resolved at spawn; other weapons retain the aimed cell.
        const RuntimeCellPoint victimCell=std::string(item.type)=="FLAKT"?first.toCell:RuntimeCellPoint{48,44};
        const int victim=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"E1",victimCell)->instanceId;
        findMutableObjectById(s,victim)->mission="Sleep";
        std::unique_ptr<RuntimeWindowState> restored;
        if(std::string(item.type)=="GGI"||std::string(item.type)=="FLAKT") {
            std::ostringstream saved;IniFile ini;restored=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*restored,s,false);
            if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("flight/burst save: "+error);
            ++saves;
        }
        std::set<int> primaryActors{first.actorId};int frames=0;
        const auto remember=[&](const auto& list){for(const auto& shot:list)
            if(shot.attackerObjectId==f.passenger&&shot.parentActorId<0)primaryActors.insert(shot.actorId);};
        for(;frames<hz*8&&(!s.projectiles.empty()||!s.combatDetails.bursts.empty());++frames) {
            step(s);
            remember(s.projectiles);remember(s.recentExpiredProjectiles);
            if(restored) {
                step(*restored);++paired;
                const auto* a=findObjectById(s,victim);const auto* b=findObjectById(*restored,victim);
                if(!a||!b||a->health!=b->health||s.projectiles.size()!=restored->projectiles.size()
                    ||s.combatDetails.deterministicHash()!=restored->combatDetails.deterministicHash())
                    return fail("restored damage/burst changed");
                for(std::size_t i=0;i<s.projectiles.size();++i)
                    if(serializeRuntimeProjectileEffect(s.projectiles[i])!=serializeRuntimeProjectileEffect(restored->projectiles[i]))
                        return fail("restored projectile changed");
            }
        }
        if(!s.projectiles.empty()||!s.combatDetails.bursts.empty())return fail("projectile did not finish");
        const auto* hit=findObjectById(s,victim);if(!hit)return fail("durable victim disappeared");
        const int actual=100000-hit->health;
        const int count=static_cast<int>(primaryActors.size());
        if(count!=std::max(1,weapon->burst))return fail("authored burst count");
        if(std::string(item.type)!="FLAKT"&&actual!=(rank?item.eliteDamage:item.normalDamage))
            return fail("authored damage expected="+std::to_string(rank?item.eliteDamage:item.normalDamage)+" actual="+std::to_string(actual));
        if(std::string(item.type)=="FLAKT"&&actual<=0)return fail("scattered flak failed to damage aimed cell");
        records<<scene<<','<<weaponId<<','<<count<<','<<actual<<','<<frames<<'\n';
        std::cout<<"Fortress "<<scene<<" weapon="<<weaponId<<" rounds="<<count<<" damage="<<actual<<" PASS\n";
        ++cases;rounds+=count;
    }
    for(int hz:{15,30,60}) {
        // The cell order wins over a nearby enemy and respects independent
        // passenger range, cooldown and FireInTransport restrictions.
        auto f=make(hz,"SHK",0);auto& s=*f.box;RuntimeSnapshotActiveStateScope active(s);
        scene=std::to_string(hz)+"-boundaries";
        if(!board(f,f.passenger))return fail("boundary boarding");
        const int brute=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"BRUTE",{44,45})->instanceId;
        if(!board(f,brute)||!order(f,{48,44}))return fail("mixed boarding/order");
        const int enemy=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Infantry,"E1",{45,44})->instanceId;
        findMutableObjectById(s,enemy)->mission="Sleep";
        ++s.tick;advanceRuntimeCombatIndependentFire(s);
        if(s.projectiles.size()!=1||s.projectiles.front().attackerObjectId!=f.passenger
            ||s.projectiles.front().targetObjectId>=0||s.projectiles.front().toCell!=RuntimeCellPoint{48,44})
            return fail("forced cell displaced by nearby enemy or forbidden passenger fired");
        const auto before=s.nextProjectileActorId;advanceRuntimeCombatIndependentFire(s);
        if(s.nextProjectileActorId!=before)return fail("cooldown bypass");
        findMutableRuntimeObjectByIdIncludingInactive(s,f.passenger)->weaponCooldownTicksRemaining=0;
        if(!order(f,{51,44}))return fail("distant ground order");
        advanceRuntimeCombatIndependentFire(s);
        if(s.nextProjectileActorId!=before)return fail("passenger exceeded range or auto-retargeted");
        // Retarget back in range, then a real stop command prevents new fire.
        if(!order(f,{48,44}))return fail("return cell order");
        advanceRuntimeCombatIndependentFire(s);
        if(s.nextProjectileActorId!=before+1)return fail("retarget did not resume passenger fire");
        s.world.accessObjects().erase(std::remove_if(s.world.accessObjects().begin(),s.world.accessObjects().end(),
            [&](const auto& o){return o.instanceId==enemy;}),s.world.accessObjects().end());
        issueRuntimeStopCommand(s);
        findMutableRuntimeObjectByIdIncludingInactive(s,f.passenger)->weaponCooldownTicksRemaining=0;
        const auto stopped=s.nextProjectileActorId;advanceRuntimeCombatIndependentFire(s);
        if(s.nextProjectileActorId!=stopped)return fail("stopped cell command kept firing");
        boundaries+=5;
    }
    int mixedSnapshots=0;
    for(int hz:{15,30,60}) {
        scene=std::to_string(hz)+"-mixed-simulation";
        auto f=make(hz,"SHK",0);auto& s=*f.box;RuntimeSnapshotActiveStateScope active(s);
        if(!board(f,f.passenger))return fail("mixed first passenger");
        std::set<int> expected{f.passenger},seen;
        for(const char* type:{"GGI","INIT"}) {
            const int id=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,{44,45})->instanceId;
            if(!board(f,id))return fail("mixed additional passenger");expected.insert(id);
        }
        if(!order(f,{48,44}))return fail("mixed player order");
        const auto collect=[&](const auto& list){for(const auto& shot:list)if(expected.contains(shot.attackerObjectId)) {
            if(shot.targetObjectId>=0||shot.toCell!=RuntimeCellPoint{48,44}||!shot.affectsAllies)return false;
            seen.insert(shot.attackerObjectId);
        }return true;};
        for(int n=0;n<hz*2&&seen!=expected;++n) {
            ++s.tick;advanceRuntimeSimulation(s,false);
            if(!collect(s.projectiles)||!collect(s.recentExpiredProjectiles))return fail("mixed target changed");
        }
        if(seen!=expected)return fail("full simulation omitted a passenger");
        std::ostringstream snapshot;IniFile ini;auto after=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*after,s,false);
        if(!writeRuntimeStateToStream(s,snapshot,error)||!writeRuntimeMultiplayerSnapshotExtension(s,snapshot,error)
            ||!ini.loadFromString(snapshot.str())||!materializeRuntimeLoadStateFromIni(*after,ini,error)
            ||!materializeRuntimeMultiplayerSnapshotExtension(*after,ini,error))return fail("mixed snapshot: "+error);
        if(after->projectiles.size()!=s.projectiles.size()||after->combatDetails.deterministicHash()!=s.combatDetails.deterministicHash())
            return fail("mixed snapshot lost firing state");
        applySelection(*after,{f.host},false,false);issueRuntimeStopCommand(*after);
        const int actorId=after->nextProjectileActorId;
        for(int n=0;n<hz;++n){++after->tick;advanceRuntimeSimulation(*after,false);}
        if(after->nextProjectileActorId!=actorId)return fail("restored stop left passenger fire running");
        ++mixedSnapshots;
    }
    RuntimeMapRenderContext render;render.rules=&live.rules;render.art=&live.art;render.theater=&live.theater;
    render.assetRootPath=runtimeAssetRootPath(live.config);
    render.structureRootPath=runtimeStructureRootPath(live.config,live.report.map);
    render.structurePalettePath=runtimeStructurePalettePath(live.config,live.report.map);
    render.palettePath=runtimePalettePath(live.config,live.report.map);
    render.tileRootPath=runtimeTileRootPath(live.config,live.report.map);
    if(!runRuntimeTemporalRenderContractCheck(render,error))return fail("temporal ray regression: "+error);
    std::cout<<"Fortress ground passed: cases="<<cases<<" rounds="<<rounds<<" saves="<<saves
        <<" continuedTicks="<<paired<<" nativeCaptures="<<captures<<" boundaries="<<boundaries
        <<" mixedSnapshots="<<mixedSnapshots<<" temporalRender=PASS\n";
    return true;
}
