bool runRuntimeSpecialDamageSmokeSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const char* output=std::getenv("CONCORD_SPECIAL_DAMAGE_SMOKE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19-complete/02-damage-smoke/special-checks";
    std::filesystem::create_directories(root);
    auto live=std::make_unique<RuntimeWindowState>();live->config=config;
    live->config.playerSlots.clear();live->config.gameModeRulesOverride.clear();
    live->report=buildRuntimeStartupReport(live->config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success){error="Special smoke: stock startup failed";return false;}
    std::ofstream rows(root/"special-sources.csv");
    rows<<"case,hz,tick,phase,type,x,y,z,offset_y,roofs,cancellation,attachment_x,attachment_y,fixed_x,fixed_y,result\n";
    int failures=0,captures=0,saves=0,frozen=0,cameraChecks=0,renderChecks=0;
    std::string first;
    const auto fail=[&](const std::string& why){++failures;if(first.empty())first=why;
        std::cout<<"Special smoke FAIL "<<why<<'\n';};
    const auto make=[&](int hz){auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=live->config;s.config.fixedTickHz=hz;s.config.computerAiEnabled=s.config.multiplayerActive=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=s.config.noCooldown=false;
        s.report=live->report;s.rules=live->rules;s.art=live->art;s.theater=live->theater;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.world=live->world;
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessTerrainCells().clear();s.world.accessSmudges().clear();
        for(int y=1;y<=44;++y)for(int x=1;x<=48;++x){TerrainCell cell;
            cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);}
        s.economy.owner="Americans";s.economy.credits=100000;s.economy.skirmishStartApplied=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.economy.localProductionLanes.nextSerial=2;
        s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(authority));return storage;};
    const auto building=[&](RuntimeWindowState& s,const std::string& type,int x,int y,int id){
        WorldObjectInstance value;value.instanceId=id;value.kind=WorldObjectKind::Structure;value.type=type;
        value.owner="Americans";value.x=x;value.y=y;value.cell=runtimeCellKey(x,y);value.health=1000;
        value.alive=value.poweredOperational=true;value.mission="Guard";s.world.accessObjects().push_back(value);};
    const auto initialize=[&](RuntimeWindowState& s){s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        for(auto& [key,cell]:s.terrainInfoByCell){(void)key;cell.movementLandType=cell.authoredLandType=RuntimeTerrainLandType::Clear;
            cell.kind=RuntimeTerrainKind::Land;cell.water=cell.shore=cell.bridge=false;cell.passable=true;cell.cost=10;}
        s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false;
        s.structureFootprints=buildRuntimeStructureFootprints(s);};
    const auto damage=[&](RuntimeWindowState& s,int id){auto* object=findMutableObjectById(s,id);
        const auto* type=object?findRuntimeUnitType(s.rules,*object):nullptr;
        if(!type)return false;object->health=std::max(1,type->strength/3);return true;};
    const auto fresh=[&](const RuntimeWindowState& s,int id){std::uint64_t newest=0;
        for(const auto& puff:s.particles.particles())if(puff.sourceObjectId==id&&puff.sourceTag=="vehicle-damage-smoke"&&puff.ageTicks==1)
            newest=std::max(newest,puff.id);return newest;};
    std::map<std::pair<std::string,std::uint64_t>,RuntimeParticleInstance> born;
    const auto retained=[&](const RuntimeWindowState& s,const std::string& key){
        for(const auto& puff:s.particles.particles())if(puff.sourceTag=="vehicle-damage-smoke"){
            const auto identity=std::make_pair(key,puff.id);const auto old=born.find(identity);
            if(old==born.end()){born.emplace(identity,puff);continue;}
            if(old->second.screenOffsetY!=puff.screenOffsetY||old->second.presentation!=puff.presentation
                ||old->second.spriteAttachment!=puff.spriteAttachment)fail(key+" old puff changed birth geometry");
            else ++frozen;
        }};
    const auto capture=[&](RuntimeWindowState& s,int id,const std::string& key,const std::string& phase,
        const GameWorld* emissionWorld=nullptr){
        const GameWorld& sourceWorld=emissionWorld?*emissionWorld:s.world;
        const auto sourceIt=std::find_if(sourceWorld.getObjects().begin(),sourceWorld.getObjects().end(),
            [&](const auto& candidate){return candidate.instanceId==id;});
        const auto* object=sourceIt==sourceWorld.getObjects().end()?nullptr:&*sourceIt;
        const auto newborn=fresh(s,id);
        const RuntimeParticleSystemInstance* emitter=nullptr;
        for(const auto& source:s.particles.systems())if(source.sourceObjectId==id&&source.sourceTag=="vehicle-damage-smoke"&&source.emitting){
            if(emitter)fail(key+" duplicate producer");emitter=&source;}
        if(!object||!emitter||!newborn){fail(key+" "+phase+" no real newborn/producer");return;}
        const auto before=failures;const auto* unit=findRuntimeUnitType(s.rules,*object);
        const auto offset=unit->damageSmokeOffset;
        double x=runtimeObjectRenderX(*object),y=runtimeObjectRenderY(*object),z=runtimeObjectRenderLevel(*object);
        RuntimeParticleSpriteAttachment attachment;
        if(unit->missileSpawn){
            const auto found=s.specialMechanisms.spawnedMissiles().find(id);
            if(found==s.specialMechanisms.spawnedMissiles().end()){fail(key+" missing authoritative missile");return;}
            const auto& missile=found->second;x=missile.x;y=missile.y;z=missile.z;
            const double distance=std::sqrt(std::pow(x-missile.originX,2)+std::pow(y-missile.originY,2)+std::pow(z-missile.originZ,2)/6.0);
            const double weight=missile.released?std::max(0.0,1.0-distance/2.0):1.0;
            attachment.screenOffsetX=static_cast<int>(std::lround(missile.originScreenOffsetX*weight));
            attachment.screenOffsetY=static_cast<int>(std::lround(missile.originScreenOffsetY*weight));
            attachment.nativeFixedX=static_cast<int>(std::lround(missile.originNativeFixedX*weight));
            attachment.nativeFixedY=static_cast<int>(std::lround(missile.originNativeFixedY*weight));
            attachment.voxelAnchor=true;attachment.airborne=missile.released;
            if(emitter->screenOffsetY!=0||emitter->presentation.enabled||emitter->spriteAttachment!=attachment)
                fail(key+" missile smoke did not preserve independent FLH fade / voxel anchor");
        }
        if(std::abs(emitter->origin.x-(x*256+offset[0]))>1e-8
            ||std::abs(emitter->origin.y-(y*256+offset[1]))>1e-8
            ||std::abs(emitter->origin.z-(z*256+offset[2]))>1e-8)fail(key+" "+phase+" source world coordinates");
        auto snapshots=s.particles.renderSnapshots(s.rules,&s.art);
        std::set<std::uint64_t> own;
        for(const auto& puff:s.particles.particles())if(puff.sourceObjectId==id&&puff.sourceTag=="vehicle-damage-smoke")own.insert(puff.id);
        snapshots.erase(std::remove_if(snapshots.begin(),snapshots.end(),[&](const auto& puff){return !own.contains(puff.instanceId);}),snapshots.end());
        const auto newbornIt=std::find_if(snapshots.begin(),snapshots.end(),[&](const auto& puff){return puff.instanceId==newborn;});
        if(newbornIt==snapshots.end()){fail(key+" newborn render snapshot absent");return;}
        std::iter_swap(snapshots.begin(),newbornIt);
        if(snapshots.front().presentation!=emitter->presentation||snapshots.front().spriteAttachment!=emitter->spriteAttachment)
            fail(key+" newborn lost emission geometry");
        const auto origin=emitter->origin;
        rows<<key<<','<<s.config.fixedTickHz<<','<<s.tick<<','<<phase<<','<<object->type<<','<<std::setprecision(14)
            <<origin.x<<','<<origin.y<<','<<origin.z<<','<<emitter->screenOffsetY<<','<<emitter->presentation.roofs.size()
            <<','<<emitter->presentation.factoryLiftCancellation<<','<<emitter->spriteAttachment.screenOffsetX
            <<','<<emitter->spriteAttachment.screenOffsetY<<','<<emitter->spriteAttachment.nativeFixedX
            <<','<<emitter->spriteAttachment.nativeFixedY<<','<<(before==failures?"PASS":"FAIL")<<'\n';
        ++captures;
        std::ostringstream serialized;s.particles.save(serialized);IniFile ini;RuntimeParticleSystemState restored(s.config.fixedTickHz);std::string detail;
        if(!ini.loadFromString(serialized.str())||!restored.load(ini,s.rules,&detail)||restored.stateHash()!=s.particles.stateHash())
            fail(key+" presentation save/load "+detail);else ++saves;
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        // Establish an idempotent producer sync before varying only the camera.
        // This also isolates camera dependence from normal post-movement sync.
        syncRuntimeVehicleDamageSmoke(s);
        const auto particleHash=s.particles.stateHash();const auto authorityHash=runtimeStandaloneSimulationStateHash(s,detail);
        if(!detail.empty())fail(key+" invalid authority fixture: "+detail);
        const auto camera=s.camera;s.camera.initialized=true;s.camera.tileWidth=72;s.camera.tileHeight=36;
        s.camera.worldOffsetX+=137;s.camera.worldOffsetY-=59;syncRuntimeVehicleDamageSmoke(s);
        const auto changedHash=runtimeStandaloneSimulationStateHash(s,detail);
        if(!detail.empty()||s.particles.stateHash()!=particleHash||changedHash!=authorityHash)fail(key+" camera changed authority or birth geometry: "+detail);
        s.camera=camera;++cameraChecks;
        if(s.config.fixedTickHz==30){RuntimeMapRenderContext context;
            context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;
            context.assetRootPath=runtimeAssetRootPath(s.config);context.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
            context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
            context.palettePath=runtimePalettePath(s.config,s.report.map);context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
            context.animationTick=s.tick;context.fixedTickHz=s.config.fixedTickHz;
            context.specialMechanismSnapshots=&s.specialMechanismRenderSnapshots;
            if(!runRuntimeSpecialDamageSmokeRenderContractCheck(sourceWorld,context,id,origin,snapshots,key+"-"+phase,detail))fail(detail);
            ++renderChecks;
            if(runtimeStandaloneSimulationStateHash(s,detail)!=authorityHash||!detail.empty()||s.particles.stateHash()!=particleHash)
                fail(key+" rendering changed authority: "+detail);
        }
        retained(s,key);
    };
    // This query-only equivalence fixture supplements, rather than replaces,
    // the real production/movement and independent pixel oracles below.
    {
        auto box=make(30);auto& s=*box;RuntimeMapRenderContext context;
        context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        auto linearCache=std::make_unique<RuntimeMapRenderCache>();
        auto batchCache=std::make_unique<RuntimeMapRenderCache>();
        std::vector<WorldObjectInstance> queries;
        const auto query=[&](double x,double y,int factory=-1){WorldObjectInstance air;
            air.instanceId=1000+static_cast<int>(queries.size());air.kind=WorldObjectKind::Unit;air.type="SHAD";
            air.alive=air.airborne=air.hasRenderPosition=air.renderMoveActive=true;air.health=50;air.owner="Americans";
            air.x=static_cast<int>(std::lround(x));air.y=static_cast<int>(std::lround(y));air.cell=runtimeCellKey(air.x,air.y);
            air.renderX=x;air.renderY=y;air.renderLevel=.5;air.level=air.flightLevel=1;air.factoryExitHostId=factory;
            queries.push_back(air);};
        for(int i=0;i<40;++i){const int x=5+(i%8)*5,y=5+(i/8)*6;
            building(s,"GATECH",x,y,i+1);query(x+1,y+.5);}
        // Original zero-Height art exercises the real SHP metadata cache.
        building(s,"GASAND",45,40,80);query(45,40);
        const std::size_t boundaryStart=queries.size();
        for(double x:{4.5-.0000005,4.5-.000002,7.5+.0000005,7.5+.000002})query(x,5.0);
        for(const auto& source:queries)s.world.accessObjects().push_back(source);
        std::ofstream batchRows(root/"capture-batch.csv");
        batchRows<<"phase,world_objects,structures,queries,candidates,failures\n";
        const auto compare=[&](const std::string& phase,bool sparse){
            const int before=failures;
            RuntimeAircraftAirbornePresentationBatch batch(s.world,context,*batchCache);
            for(std::size_t i=0;i<queries.size();++i){RuntimeParticleAirbornePresentation expected,actual;std::string a,b;
                const bool linear=captureRuntimeAircraftAirbornePresentation(s.world,queries[i],context,*linearCache,expected,&a);
                const bool indexed=batch.capture(queries[i],actual,&b);
                if(linear!=indexed||a!=b||expected!=actual)fail("batch "+phase+" source "+std::to_string(i)+" changed capture/order/limit");
                if(sparse&&i>=boundaryStart){const bool inside=((i-boundaryStart)%2)==0;
                    if(!indexed||actual.roofs.size()!=(inside?1U:0U))fail("batch half-cell epsilon "+std::to_string(i));}
                if(sparse&&i==40&&(actual.roofs.size()!=1||actual.roofs.front().nativeSpriteHeight<=0))
                    fail("batch original zero-Height SHP was not decoded");
            }
            const auto stats=batch.statistics();
            if(stats.worldObjectsScanned!=s.world.getObjects().size()||stats.queries!=queries.size()
                ||(sparse&&stats.candidatesVisited>=stats.queries*stats.structuresIndexed/2))
                fail("batch "+phase+" did not share one sparse structure scan");
            batchRows<<phase<<','<<stats.worldObjectsScanned<<','<<stats.structuresIndexed<<','<<stats.queries
                <<','<<stats.candidatesVisited<<','<<failures-before<<'\n';
            std::cout<<"Special smoke batch "<<phase<<" world="<<stats.worldObjectsScanned
                <<" structures="<<stats.structuresIndexed<<" sources="<<stats.queries<<" candidates="<<stats.candidatesVisited<<'\n';
        };
        compare("cold",true);
        const auto bytes=batchCache->objectShpBytesByImage.size(),frames=batchCache->objectShpSpritesByType.size();
        compare("warm",true);
        if(batchCache->objectShpBytesByImage.size()!=bytes||batchCache->objectShpSpritesByType.size()!=frames)
            fail("batch warm capture grew original sprite caches");
        // The actual smoke synchronizer must use the same batch result for
        // many concurrent sources, including ones outside all foundations.
        ensureRuntimeParticlesInitialized(s);syncRuntimeVehicleDamageSmoke(s);
        for(const auto& source:queries){RuntimeParticleAirbornePresentation expected;std::string detail;
            captureRuntimeAircraftAirbornePresentation(s.world,source,context,*linearCache,expected,&detail);
            const auto producer=std::find_if(s.particles.systems().begin(),s.particles.systems().end(),
                [&](const auto& system){return system.sourceObjectId==source.instanceId&&system.emitting;});
            if(producer==s.particles.systems().end()||producer->presentation!=expected)fail("batch multi-source sync differed from linear capture");}
        s.world.accessObjects().clear();queries.clear();
        for(int i=0;i<64;++i)building(s,"GATECH",18,20,i+1);
        query(19,20.5);compare("64-roofs",false);
        {RuntimeParticleAirbornePresentation p;std::string detail;
            if(!captureRuntimeAircraftAirbornePresentation(s.world,queries.front(),context,*linearCache,p,&detail)||p.roofs.size()!=64)
                fail("batch 64-roof boundary not exercised");}
        building(s,"GATECH",18,20,65);compare("65-roofs-rejected",false);
        {RuntimeParticleAirbornePresentation p;std::string detail;
            RuntimeAircraftAirbornePresentationBatch batch(s.world,context,*batchCache);
            if(batch.capture(queries.front(),p,&detail)||detail.empty()||p.enabled||!p.roofs.empty())
                fail("batch overflow was truncated or accepted");}
        building(s,"GAWEAP",18,20,90);queries.front().factoryExitHostId=90;
        // Preserve input order: a producing host encountered before the limit
        // cancels/clears prior roofs, whereas a later host cannot undo rejection.
        std::rotate(s.world.accessObjects().begin()+1,s.world.accessObjects().end()-1,s.world.accessObjects().end());
        compare("factory-before-limit",false);
        {RuntimeParticleAirbornePresentation p;std::string detail;
            RuntimeAircraftAirbornePresentationBatch batch(s.world,context,*batchCache);
            if(!batch.capture(queries.front(),p,&detail)||p.factoryLiftCancellation!=25||!p.roofs.empty())
                fail("batch producing host lost priority over collected roofs");}
        std::rotate(s.world.accessObjects().begin()+1,s.world.accessObjects().begin()+2,s.world.accessObjects().end());
        compare("factory-after-limit",false);
        // A new batch observes changed geometry immediately; there is no
        // persistent index whose invalidation could depend on camera/paint.
        s.world.accessObjects().erase(s.world.accessObjects().begin(),s.world.accessObjects().end()-1);
        s.world.accessObjects().front().x=40;queries.front().factoryExitHostId=-1;
        compare("next-update-relocated",false);
        {RuntimeParticleAirbornePresentation p;std::string detail;
            RuntimeAircraftAirbornePresentationBatch batch(s.world,context,*batchCache);
            if(!batch.capture(queries.front(),p,&detail)||!p.roofs.empty())fail("batch retained a previous update's roof");}
    }
    for(int hz:{15,30,60}){
        for(const std::string type:{"DISK","SHAD"}){
            auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
            const std::string key=type+"-building-"+std::to_string(hz);
            building(s,"GATECH",18,20,1);
            auto* source=addRuntimeObjectInstance(s,runtimeBuildKindForUnitTypeId(s.rules,type),type,{12,20});
            if(!source){fail(key+" create");continue;}const int id=source->instanceId;damage(s,id);initialize(s);
            applySelection(s,{id},false,false);issueRuntimeMoveCommand(s,{28,20});
            if(s.lastCommand.acceptedCount!=1){fail(key+" actual Move rejected");continue;}
            std::set<std::string> stages;GameWorld emissionWorld=s.world;
            for(int n=0;n<hz*80&&stages.size()<3;++n){++s.tick;
                // Production updates Jumpjet before particles, then behaviors
                // afterward. Save its actual emission-time pose, not the later
                // end-of-tick body. The lastTick guard prevents double physics.
                advanceRuntimeJumpjetFlights(s);emissionWorld.accessObjects()=s.world.getObjects();
                advanceRuntimeSimulation(s);retained(s,key);
                const auto emission=std::find_if(emissionWorld.getObjects().begin(),emissionWorld.getObjects().end(),
                    [&](const auto& candidate){return candidate.instanceId==id;});
                if(emission==emissionWorld.getObjects().end()||!emission->alive){fail(key+" source lost");break;}
                if(!emission->airborne||!fresh(s,id))continue;
                const double px=runtimeObjectRenderX(*emission),py=runtimeObjectRenderY(*emission);
                const std::string phase=px<17.5?"outside":px<=20.5&&py>=19.5&&py<=21.5?"over":px>20.5?"departed":"";
                if(phase.empty()||stages.contains(phase)||(phase=="departed"&&!stages.contains("over")))continue;
                if(phase=="over"){
                    bool roof=false;for(const auto& puff:s.particles.particles())if(puff.id==fresh(s,id))roof=!puff.presentation.roofs.empty();
                    if(!roof)fail(key+" overflight never captured stock tall roof");}
                capture(s,id,key,phase,&emissionWorld);stages.insert(phase);
            }
            if(stages.size()!=3)fail(key+" missing real outside/over/departed progression");
            // Remove the real source and roof only after observing movement.
            // Existing puffs remain value objects and must retain birth geometry.
            s.world.accessObjects().clear();++s.tick;advanceRuntimeParticles(s);retained(s,key);
        }
        for(const auto& pair:std::array<std::pair<const char*,const char*>,2>{{{"NAWEAP","ZEP"},{"GAWEAP","SHAD"}}}){
            auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
            const std::string key=std::string(pair.second)+"-factory-"+std::to_string(hz);
            building(s,pair.first,18,20,1);initialize(s);
            RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.typeId=pair.second;item.requiredFactory="UnitType";
            if(!completeRuntimeProductionOrder(s,item,false,RuntimeProductionLane::Vehicles,1)) {fail(key+" real production rejected");continue;}
            const int id=s.world.getObjects().back().instanceId;damage(s,id);std::set<std::string> stages;GameWorld emissionWorld=s.world;
            for(int n=0;n<hz*120&&stages.size()<3;++n){++s.tick;
                advanceRuntimeJumpjetFlights(s);emissionWorld.accessObjects()=s.world.getObjects();
                advanceRuntimeSimulation(s);retained(s,key);
                const auto emission=std::find_if(emissionWorld.getObjects().begin(),emissionWorld.getObjects().end(),
                    [&](const auto& candidate){return candidate.instanceId==id;});
                const auto* source=emission==emissionWorld.getObjects().end()?nullptr:&*emission;
                if(!source){fail(key+" produced source lost");break;}
                if(!fresh(s,id))continue;
                const auto* unit=findRuntimeUnitType(s.rules,*source);
                const double fraction=runtimeObjectRenderLevel(*source)*256.0/unit->jumpJetHeight;
                const std::string phase=source->factoryExitHostId<0?"departed":fraction>0&&fraction<.45?"early":fraction>=.45&&fraction<.95?"middle":"";
                if(phase.empty()||stages.contains(phase))continue;
                capture(s,id,key,phase,&emissionWorld);stages.insert(phase);
            }
            if(stages.size()!=3)fail(key+" missing early/middle/departed actual factory climb");
            if(auto* source=findMutableObjectById(s,id))source->alive=false;
            ++s.tick;advanceRuntimeParticles(s);retained(s,key);
        }
        for(const std::string launcher:{"DRED","BSUB","V3"}){
            auto box=make(hz);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
            const std::string key=launcher+"-missile-"+std::to_string(hz);
            auto* parent=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,launcher,{8,20});
            if(!parent){fail(key+" launcher create");continue;}const int parentId=parent->instanceId;
            parent->facing=parent->turretFacing=runtimeFacingFromDelta(1,0);parent->mission="Attack";
            building(s,"CAEAST01",30,20,100);findMutableObjectById(s,100)->owner="Russians";findMutableObjectById(s,100)->health=3000;
            initialize(s);RuntimeObjectBehavior attack;attack.kind=RuntimeBehaviorKind::Attack;
            attack.state=RuntimeBehaviorState::Queued;attack.targetObjectId=100;s.behaviors[parentId]=attack;
            advanceRuntimeSpecialMechanisms(s);
            // The real parent manager has created docked children. A damaged
            // but unlaunched child must not enter the dedicated launch source.
            const auto carrier=s.specialMechanisms.carrierParents().find(parentId);
            if(carrier==s.specialMechanisms.carrierParents().end()||carrier->second.childBySlot.empty()){
                fail(key+" no real docked child");continue;}
            const int dockedId=carrier->second.childBySlot.begin()->second;
            if(!damage(s,dockedId)){fail(key+" docked child absent");continue;}
            ++s.tick;advanceRuntimeParticles(s);
            if(runtimeVehicleDamageSmokeType(s,*findObjectById(s,dockedId))||fresh(s,dockedId))
                fail(key+" unlaunched docked child emitted damage smoke");
            const auto* weapon=s.rules.findWeaponType(launcher=="DRED"?"DredLauncher":launcher=="BSUB"?"CruiseLauncher":"V3Launcher");
            if(!weapon||!launchRuntimeSpawnedMissileVolley(s,*findObjectById(s,parentId),*findObjectById(s,100),*weapon,1.0)
                ||s.specialMechanisms.spawnedMissiles().empty()){fail(key+" real volley rejected");continue;}
            const int id=s.specialMechanisms.spawnedMissiles().begin()->first;
            if(!damage(s,id)){fail(key+" generated child absent");continue;}
            const int requiredStages=launcher=="BSUB"?2:3;
            std::set<std::string> stages;bool prepared=false,nonzeroFlh=false;
            for(int n=0;n<hz*90&&static_cast<int>(stages.size())<requiredStages;++n){++s.tick;advanceRuntimeEffects(s);
                advanceRuntimeParticles(s);retained(s,key);
                const auto missile=s.specialMechanisms.spawnedMissiles().find(id);
                if(missile==s.specialMechanisms.spawnedMissiles().end())break;
                const auto& m=missile->second;
                nonzeroFlh=nonzeroFlh||m.originScreenOffsetX||m.originScreenOffsetY||m.originNativeFixedX||m.originNativeFixedY;
                if(!m.released){prepared=true;
                    if(launcher=="BSUB"){
                        if(runtimeVehicleDamageSmokeType(s,*findObjectById(s,id))||fresh(s,id))
                            fail(key+" hidden submarine preparation emitted smoke");
                    }else if(fresh(s,id)&&!stages.contains("preparing")){
                        capture(s,id,key,"preparing");stages.insert("preparing");}
                    continue;}
                if(!fresh(s,id))continue;
                const double travelled=std::sqrt(std::pow(m.x-m.originX,2)+std::pow(m.y-m.originY,2)+std::pow(m.z-m.originZ,2)/6.0);
                const std::string phase=travelled<2.0?"launch-attachment":"free-flight";
                if(stages.contains(phase))continue;capture(s,id,key,phase);stages.insert(phase);
            }
            if(!prepared||!nonzeroFlh||static_cast<int>(stages.size())!=requiredStages)
                fail(key+" missing preparation/nonzero FLH/attached and free flight smoke");
            if(auto* source=findMutableObjectById(s,id))source->alive=false;
            ++s.tick;advanceRuntimeParticles(s);retained(s,key);
        }
    }
    std::cout<<"Special damage smoke: captures="<<captures<<" saves="<<saves<<" frozenPuffChecks="<<frozen
        <<" cameraChecks="<<cameraChecks<<" renderCases="<<renderChecks<<" failures="<<failures<<'\n';
    if(failures){error="Special damage smoke: "+std::to_string(failures)+" failures; "+first;return false;}
    error.clear();return captures>0&&saves==captures&&renderChecks>0;
}
