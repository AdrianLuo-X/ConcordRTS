bool runRuntimeChaosSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Chaos: " + why; return false; };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("original resources unavailable");
    const auto make = [&](const std::string& type, int hz) {
        auto p = std::make_unique<RuntimeWindowState>(); auto& s = *p;
        s.config = config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.rules = live->rules; s.art = live->art; s.theater = live->theater; s.report = live->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true;
        s.economy.owner = "YuriCountry"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.world=live->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y=1; y<=26; ++y) for (int x=1; x<=26; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
            MapCellFacts facts; facts.x=x; facts.y=y; facts.cellIndex=y*512+x;
            facts.hasTerrain=true; facts.terrainClass="land"; facts.passabilityHint="passable";
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex,s.mapFacts.cells.size());
            s.mapFacts.cells.push_back(facts);
        }
        WorldObjectInstance source;
        source.instanceId=1; source.kind=type=="DESO" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        source.type=type; source.owner=s.economy.owner; source.alive=true;
        source.x=source.y=10; source.cell=runtimeCellKey(10,10); source.mission="Guard";
        source.currentAmmo=-1; source.poweredOperational=true; source.facing=64;
        source.health=runtimeMaxHealthForObject(s,source);
        s.world.accessObjects().push_back(source);
        s.terrainByCell=buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        ensureRuntimeSpecialMechanismsInitialized(s); s.selectedObjectIds={1};
        return p;
    };
    int cases=0;
    const auto moveCursor = [](RuntimeWindowState& s) {
        const RECT client{0,0,1280,720}; const RECT viewport=runtimeMapViewport(client);
        // Projection clamps a loaded map's camera. Choose empty ground after
        // that clamp, so the test does not accidentally point at the drone.
        ensureRuntimeCameraForViewport(s,viewport); s.mouseClientPointValid=true;
        for (LONG y=viewport.top+60;y<viewport.bottom-60;y+=80)
            for (LONG x=viewport.left+60;x<viewport.right-60;x+=80) {
                const POINT point{x,y};
                if (runtimeCursorObjectAtPoint(s,viewport,point)) continue;
                s.mouseClientPoint=point;
                const auto cursor=runtimeMouseCursorFrameForPoint(s,client,point);
                if (!cursor || cursor->frameIndex<31 || cursor->frameIndex>40) {
                    std::cout<<"Chaos cursor frame="<<(cursor?cursor->frameIndex:-1)
                        <<" movable="<<selectedMovableObjectCount(s)<<" selected="<<s.selectedObjectIds.size()<<'\n';
                    return false;
                }
                return true;
            }
        return false;
    };
    std::vector<RuntimeImpactRenderActor> gasFrames;
    GameWorld gasWorld;
    for (int hz : {30,60}) {
        auto p=make("CAOS",hz); auto& s=*p;
        for (const std::string type : {"E1","HTNK"}) {
            auto target=s.world.getObjects().front(); target.instanceId=static_cast<int>(s.world.getObjects().size())+1;
            target.owner="Americans"; target.type=type;
            target.kind=type=="E1" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            target.x+=target.instanceId-1; target.cell=runtimeCellKey(target.x,target.y);
            target.health=runtimeMaxHealthForObject(s,target); target.mission="Sleep";
            target.weaponCooldownTicksRemaining=100000; s.world.accessObjects().push_back(target);
        }
        invalidateRuntimeTickOccupancyCache(s);
        auto* source=findMutableObjectById(s,1); source->deployed=true; source->mission="Deployed";
        const auto profile=runtimeWeaponProfileForObject(s,*source,source);
        if (!profile.weapon || !profile.warhead || !profile.weapon->areaFire || !profile.warhead->psychedelic)
            return fail("stock deployed weapon missing");
        spawnRuntimeProjectile(s,*source,*source,*profile.weapon,profile,profile.weapon->damage);
        if (s.projectiles.size()!=1) return fail("area shot missing");
        const auto shot=s.projectiles.front(); s.projectiles.clear();
        if (std::abs(shot.sourceX-10.0)>0.001 || std::abs(shot.sourceY-10.0)>0.001
            || shot.sourceZ<=0 || shot.sourceZ>=1.0) return fail("spray is not centered inside the hull");
        const int infantryHealth=findObjectById(s,2)->health, tankHealth=findObjectById(s,3)->health;
        if (!applyRuntimeProjectileImpact(s,shot) || s.psychicStatuses.statuses().size()!=2)
            return fail("original area attack failed to affect infantry and vehicle");
        const auto gas=[&]() { return std::find_if(s.impactEffects.begin(),s.impactEffects.end(),
            [](const auto& impact){return impact.animId=="CDGAS";}); };
        if (gas()==s.impactEffects.end()) return fail("native gas missing");
        const auto first=*gas();
        if (first.animationSequenceSteps<10 || first.totalTicks<20*hz/30
            || first.worldX!=shot.sourceX || first.worldY!=shot.sourceY || first.worldLevel!=shot.sourceZ)
            return fail("gas sequence is clipped or detached from the middle of the drone");
        RuntimeImpactEffect restored;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(first),restored)
            || restored.animationSequenceSteps!=first.animationSequenceSteps || restored.worldLevel!=first.worldLevel)
            return fail("gas snapshot lost its full sequence or anchor");
        int previous=-1;
        for (int age=0; age<first.totalTicks; ++age) {
            const auto current=gas();
            if (current==s.impactEffects.end()) return fail("gas disappeared before its native last frame");
            if (current->frameIndex!=previous && hz==30) gasFrames.push_back(makeRuntimeImpactRenderActor(*current));
            previous=current->frameIndex;
            ++s.tick; advanceRuntimeEffects(s);
        }
        if (gas()!=s.impactEffects.end() || findObjectById(s,2)->health!=infantryHealth
            || findObjectById(s,3)->health!=tankHealth) return fail("visual repeated damage or did not expire");
        if (hz==30) gasWorld=s.world;
        std::cout<<"Chaos gas hz="<<hz<<" frames="<<first.animationSequenceSteps
            <<" duration="<<first.totalTicks<<" center/status/no-extra-damage/save=ok\n";
        ++cases;
    }
    for (int hz : {30,60}) for (const std::string type : {"CAOS","DESO"}) for (bool during : {false,true}) {
        auto p=make(type,hz); auto& s=*p;
        const auto step=[&](){++s.tick; advanceRuntimeSimulation(s);};
        if (!executeRuntimeSecondaryAction(s,1)) return fail(type+" deploy rejected");
        if (type=="CAOS" && !moveCursor(s)) return fail(type+" deploying move cursor missing");
        if (type=="DESO" && selectedMovableObjectCount(s)!=0)
            return fail(type+" deploying infantry still presents as movable");
        if (!during) {
            for (int i=0; i<hz*4 && runtimeMissionIsDeploying(*findObjectById(s,1)); ++i) step();
            if (!findObjectById(s,1)->deployed) return fail(type+" deploy did not complete");
            if (type=="CAOS" && !moveCursor(s)) return fail(type+" deployed move cursor missing");
        }
        if (type=="DESO") {
            issueRuntimeMoveCommand(s,{15,10});
            if (s.lastCommand.acceptedCount!=0 || runtimeMissionIsUndeploying(*findObjectById(s,1))
                || !findObjectById(s,1)->deployed)
                return fail(type+" move automatically unfolded deployed infantry");
            for (int i=0; i<hz*4 && runtimeMissionIsDeploying(*findObjectById(s,1)); ++i) step();
            if (!executeRuntimeSecondaryAction(s,1)) return fail(type+" manual undeploy rejected");
        }
        issueRuntimeMoveCommand(s,{15,10});
        const auto* folded=findObjectById(s,1);
        if (s.lastCommand.acceptedCount!=1 || !runtimeMissionIsUndeploying(*folded))
            return fail(type+" move lost pending undeploy: "+s.simulationStatus);
        const int remaining=folded->buildAnimTicksRemaining;
        issueRuntimeMoveCommand(s,{16,10});
        if (findObjectById(s,1)->buildAnimTicksRemaining!=remaining || s.behaviors.at(1).moveTarget.x!=16)
            return fail(type+" repeated move restarted folding or lost destination");
        for (int i=0; i<remaining; ++i) {
            if (findObjectById(s,1)->x!=10 || findObjectById(s,1)->y!=10)
                return fail(type+" moved before its folding animation completed");
            step();
        }
        for (int i=0; i<hz*8 && findObjectById(s,1)->x==10; ++i) step();
        if (findObjectById(s,1)->deployed || findObjectById(s,1)->x==10)
            return fail(type+" queued move did not resume after folding: "+s.simulationStatus);
        std::cout<<type<<" hz="<<hz<<" duringDeploy="<<during<<" click/fold/wait/move/repeated-order=ok\n";
        ++cases;
    }
    int grinderCases=0, grinderSaves=0;
    for (int hz : {15,30,60}) for (const std::string phase : {"mobile","deploying","deployed","active-gas"}) {
        auto p=make("CAOS",hz); auto& s=*p; RuntimeSnapshotActiveStateScope active(s);
        s.economy.credits=1000;
        auto host=s.world.getObjects().front(); host.instanceId=10; host.kind=WorldObjectKind::Structure;
        host.type="YAGRND"; host.x=15; host.y=10; host.cell=runtimeCellKey(15,10);
        host.health=runtimeMaxHealthForObject(s,host); s.world.accessObjects().push_back(host);
        auto power=host; power.instanceId=11; power.type="YAPOWR"; power.x=power.y=22;
        power.cell=runtimeCellKey(22,22); power.health=runtimeMaxHealthForObject(s,power);
        s.world.accessObjects().push_back(power);
        if (phase=="active-gas") {
            auto victim=s.world.getObjects().front(); victim.instanceId=2; victim.kind=WorldObjectKind::Infantry;
            victim.type="E1"; victim.owner="Americans"; victim.x=11; victim.cell=runtimeCellKey(11,10);
            victim.health=runtimeMaxHealthForObject(s,victim); victim.mission="Sleep";
            victim.weaponCooldownTicksRemaining=100000; s.world.accessObjects().push_back(victim);
        }
        initializeRuntimeNavigation(s);
        const auto step=[&](){++s.tick; advanceRuntimeSimulation(s);};
        if (phase!="mobile") {
            if (!executeRuntimeSecondaryAction(s,1)) return fail("grinder deploy setup");
            if (phase!="deploying") {
                for (int i=0;i<hz*6;++i) {
                    step();
                    if (findObjectById(s,1)->deployed && (phase!="active-gas" || s.psychicStatuses.findByTarget(2))) break;
                }
                if (!findObjectById(s,1)->deployed || (phase=="active-gas" && !s.psychicStatuses.findByTarget(2)))
                    return fail("grinder gas/deploy did not complete");
            }
        }
        if (!moveCursor(s) || runtimeTargetCursorIntent(s,*findObjectById(s,10))!=RuntimeTargetCursorIntent::Enter)
            return fail("grinder cursor phase="+phase+" hz="+std::to_string(hz));
        const int refund=runtimeGrindingExpectedRefund(s,*findObjectById(s,1),runtimeVeterancyObjectCost(s,*findObjectById(s,1)));
        if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {15,10},findObjectById(s,10)) || s.lastCommand.acceptedCount!=1)
            return fail("grinder order phase="+phase+": "+s.simulationStatus);
        if (phase!="mobile" && !runtimeMissionIsUndeploying(*findObjectById(s,1)))
            return fail("grinder did not fold deployed source");
        std::ostringstream saved; IniFile ini; auto restored=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("grinder queued save phase="+phase+": "+error);
        if (!runtimeGrindingRelationForSource(restored->grinding,1)) return fail("grinder reload lost entry");
        ++grinderSaves;
        for (int i=0;i<hz*15 && findObjectById(s,1);++i) step();
        if (findObjectById(s,1) || s.economy.credits!=1000+refund || !s.grinding.relations().empty())
            return fail("grinder arrival/refund phase="+phase+" hz="+std::to_string(hz)+": "+s.simulationStatus);
        if (phase=="active-gas" && !s.psychicStatuses.findByTarget(2))
            return fail("recycling caster prematurely removed its existing gas status");
        // A restored folding/queued entry must also reach the grinder.
        {
            auto& copy=*restored; RuntimeSnapshotActiveStateScope restoredActive(copy);
            for (int i=0;i<hz*15 && findObjectById(copy,1);++i) {++copy.tick; advanceRuntimeSimulation(copy);}
            if (findObjectById(copy,1) || copy.economy.credits!=s.economy.credits)
                return fail("restored grinder did not finish phase="+phase);
        }
        // The surviving temporary effect remains valid when its source is gone.
        if (phase=="active-gas") {
            std::ostringstream consumed; IniFile consumedIni; auto loaded=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*loaded,s,false);
            if (!writeRuntimeStateToStream(s,consumed,error) || !consumedIni.loadFromString(consumed.str())
                || !materializeRuntimeLoadStateFromIni(*loaded,consumedIni,error)
                || !loaded->psychicStatuses.findByTarget(2)) return fail("consumed caster status reload: "+error);
            ++grinderSaves;
        }
        ++grinderCases;
    }
    if (!std::getenv("CONCORD_COMMAND_SMOKE_CHAOS_DEPLOY_ONLY") && !runRuntimePsychicStatusHostSmokeProbe(error)) return false;
    if (!runRuntimeFullDeploySemanticsProbe(*live,error)) return false;
    RuntimeMapRenderContext context;
    context.rules=&live->rules; context.art=&live->art; context.theater=&live->theater;
    context.assetRootPath=runtimeAssetRootPath(config);
    context.structureRootPath=runtimeStructureRootPath(config,live->report.map);
    context.palettePath=runtimePalettePath(config,live->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,live->report.map);
    if (!runRuntimeChaosRenderContractCheck(context,gasWorld,gasFrames,error)) return false;
    std::cout<<"Chaos passed: "<<cases<<" stock cases; gas, status, auto undeploy, native red bodies, expiry and deployment regression; moveCursor=ok grinderCases="
        <<grinderCases<<" grinderReloads="<<grinderSaves<<" gasRocking=none\n";
    return true;
}
