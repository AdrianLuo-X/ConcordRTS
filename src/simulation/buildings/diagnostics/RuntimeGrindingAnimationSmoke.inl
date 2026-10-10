bool runRuntimeGrindingAnimationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Grinder animation: "+why; return false;};
    auto live=std::make_unique<RuntimeWindowState>(); live->config=config;
    live->report=buildRuntimeStartupReport(config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success) return fail("original resources unavailable");
    const auto make=[&](const std::string& type,int hz,bool damaged){
        auto p=std::make_unique<RuntimeWindowState>(); auto& s=*p;
        s.config=config; s.config.fixedTickHz=hz; s.config.computerAiEnabled=false;
        s.rules=live->rules; s.art=live->art; s.theater=live->theater; s.report=live->report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.economy.owner="YuriCountry"; s.economy.skirmishStartApplied=true; s.economy.credits=1000;
        s.triggers.bootstrapComplete=true; s.visionEnabled=false;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;
        for(int y=1;y<=26;++y) for(int x=1;x<=26;++x){
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512,RuntimeTerrainInfo{});
        }
        WorldObjectInstance host;
        host.instanceId=1; host.kind=WorldObjectKind::Structure; host.type="YAGRND";
        host.owner=s.economy.owner; host.alive=true; host.poweredOperational=true; host.mission="Guard";
        host.x=host.y=10; host.cell=runtimeCellKey(10,10); host.health=damaged ? 300 : 900;
        auto unit=host; unit.instanceId=2; unit.type=type;
        unit.kind=(type=="E1" || type=="INIT") ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        unit.authoredAircraft=type=="ORCA"; unit.airborne=type=="ORCA" || type=="DISK";
        unit.health=runtimeMaxHealthForObject(s,unit); unit.currentAmmo=-1;
        unit.x=7; unit.y=13; unit.cell=runtimeCellKey(7,13);
        s.world.accessObjects()={host,unit};
        s.terrainByCell=buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        ensureRuntimeSpecialMechanismsInitialized(s); return p;
    };
    std::vector<WorldObjectInstance> hosts;
    std::vector<std::vector<RuntimeImpactRenderActor>> poses;
    int cases=0;
    for(int hz:{30,60}) for(bool damaged:{false,true}) for(const std::string type:{"E1","HTNK","INIT"}){
        auto p=make(type,hz,damaged); auto& s=*p;
        if(!queueRuntimeGrindingEntry(s,1,2,false) || !s.impactEffects.empty())
            return fail(type+" entry failed or animated before consumption");
        for(int i=0;i<hz*20 && findObjectById(s,2);++i){++s.tick; advanceRuntimeSimulation(s);}
        const auto* consumed=findRuntimeObjectByIdIncludingInactive(s,2);
        const auto cycle=[&](){return std::find_if(s.impactEffects.begin(),s.impactEffects.end(),
            [](const auto& effect){return effect.renderIntent=="grinderCycle";});};
        if(!consumed || consumed->alive || consumed->mission!="InsideGrinder" || cycle()==s.impactEffects.end()) {
            const auto* art=runtimeArtTypeForObject(s,*findObjectById(s,1));
            RuntimeImpactEffect diagnostic; diagnostic.animId="YAGRND_B";
            initializeRuntimeImpactAnimationTimeline(s,diagnostic,true);
            std::string details=" alive="+std::to_string(consumed && consumed->alive)
                +" mission="+(consumed ? consumed->mission : "missing")
                +" tick="+std::to_string(s.tick)+" effects="+std::to_string(s.impactEffects.size())
                +" frames="+std::to_string(diagnostic.animationFrameCount);
            if(art) for(const auto& layer:art->specialAnimations) details+=" layer="+layer.key+":"+layer.image;
            return fail(type+" consumption did not start the special building animation: "+s.simulationStatus+details);
        }
        const auto first=*cycle();
        if(first.animationStartFrame!=(damaged ? 49:0) || first.animationSequenceSteps!=(damaged ? 47:48)
            // Both stock grinding halves have Rate=400: 900/400
            // truncates to two original frames per sprite frame.
            || first.targetObjectId!=1 || first.damage!=0 || first.totalTicks!=first.animationSequenceSteps*2*hz/15)
            return fail("wrong normal/damaged native cycle range: start="+std::to_string(first.animationStartFrame)
                +" steps="+std::to_string(first.animationSequenceSteps)+" ticks="+std::to_string(first.totalTicks));
        const int credits=s.economy.credits;
        std::vector<RuntimeImpactRenderActor> frames; int previous=-1;
        for(int age=0;age<first.totalTicks;++age){
            auto current=cycle();
            if(current==s.impactEffects.end() || current->frameIndex<first.animationStartFrame
                || current->frameIndex>=first.animationEndFrame) return fail("native cycle vanished or crossed into the other damage half");
            if(current->frameIndex!=previous) frames.push_back(makeRuntimeImpactRenderActor(*current));
            previous=current->frameIndex;
            if(age==first.totalTicks/2){
                RuntimeImpactEffect restored;
                if(!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(*current),restored)
                    || restored.remainingTicks!=current->remainingTicks || restored.animationEndFrame!=current->animationEndFrame)
                    return fail("cycle save lost its timing");
            }
            ++s.tick; advanceRuntimeEffects(s);
        }
        if(cycle()!=s.impactEffects.end() || s.economy.credits!=credits || !s.projectiles.empty())
            return fail("animation repeated a refund, damage or did not end");
        if(hz==30 && type=="E1"){hosts.push_back(*findObjectById(s,1)); poses.push_back(frames);}
        std::cout<<"Grinder "<<type<<" hz="<<hz<<" damaged="<<damaged
            <<" frames="<<frames.size()<<" entry/consume/one-refund/expiry/save=ok\n";
        ++cases;
    }
    for(const std::string type:{"DEST","SUB","CARRIER","ORCA","DISK"}){
        auto p=make(type,30,false);
        if(queueRuntimeGrindingEntry(*p,1,2,false) || !p->grinding.relations().empty()
            || !p->impactEffects.empty() || p->economy.credits!=1000)
            return fail(type+" should be excluded without starting a cycle");
        ++cases;
    }
    if(!runRuntimeGrindingHostSmokeProbe(error)) return false;
    RuntimeMapRenderContext context;
    context.rules=&live->rules; context.art=&live->art; context.theater=&live->theater;
    context.assetRootPath=runtimeAssetRootPath(config);
    context.structureRootPath=runtimeStructureRootPath(config,live->report.map);
    context.palettePath=runtimePalettePath(config,live->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,live->report.map);
    if(!runRuntimeGrindingAnimationRenderContractCheck(context,hosts,poses,error)) return false;
    std::cout<<"Grinder animation passed: "<<cases<<" stock cases; normal/damaged native cycles, ground entry, navy/air exclusion, refund and transaction regression\n";
    return true;
}
