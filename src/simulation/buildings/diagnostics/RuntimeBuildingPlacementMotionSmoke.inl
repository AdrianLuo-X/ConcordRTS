#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBuildingPlacementMotionSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Moving foundation placement: "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success) return fail("native resources");
    const RuntimeCellPoint directions[]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
    int cases=0,corners=0;
    for(const char* type:{"MTNK","E1"}) for(int hz:{15,30,60}) for(const auto direction:directions) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
        seedRuntimeLoadScratch(s,*assets,false); s.config=config; s.config.fixedTickHz=hz;
        s.config.playerSlots.clear(); s.config.playerCountry="Americans";
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true; s.economy.owner="Americans";
        s.economy.skirmishStartApplied=true; s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts={}; s.mapFactsLoaded=s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width=-1;
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact; fact.x=x; fact.y=y; fact.cellIndex=cell.cellIndex512; fact.hasTerrain=true;
            fact.terrain.x=x; fact.terrain.y=y;
            fact.terrain.tileIndex=fact.terrain.subTileIndex=0;
            fact.terrainClass="land"; fact.passabilityHint="passable";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size()); s.mapFacts.cells.push_back(fact);
        }
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();
        if(!addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,"GACNST",{7,7})) return fail("base");
        auto* actor=addRuntimeObjectInstanceForOwner(s,"Americans",std::string(type)=="E1"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,type,{12,12});
        if(!actor) return fail("native mover"); const int id=actor->instanceId;
        actor->facing=runtimeFacingFromDelta(direction.x,direction.y); actor->weaponCooldownTicksRemaining=100000;
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        RuntimeSnapshotActiveStateScope active(s);
        RuntimeBuildCatalogItem item; item.typeId="GAPOWR"; item.kind=RuntimeBuildKind::Building; item.cost=800; item.buildTicks=1;
        const RuntimeCellPoint origin{12,12},goal{12+direction.x,12+direction.y};
        const RuntimeCellPoint topLeft{12+(direction.x<0?0:-1),12+(direction.y<0?0:-1)};
        if(!runtimeBuildingPlacementInRange(s,item,topLeft)||runtimeCellCanPlaceBuilding(s,item,topLeft)) return fail("initial occupied foundation fixture");
        s.selectedObjectIds={id}; issueRuntimeMoveCommand(s,goal);
        for(int tick=0;tick<hz*3;++tick) {
            ++s.tick; advanceRuntimeSimulation(s); actor=findMutableObjectById(s,id);
            if(actor&&actor->renderMoveActive) break;
        }
        if(!actor||runtimeCellPointForObject(*actor)!=goal||!actor->renderMoveActive||!s.behaviors.contains(id)
            ||s.behaviors.at(id).moveCooldownTicks<=0||s.behaviors.at(id).moveSegmentFrom!=origin) return fail("actual committed step");
        if(!stageRuntimeReadyPlacementForProbe(s,item)) return fail("ready placement");
        s.economy.placementCell=topLeft;
        const auto checks=runtimePlacementCellChecks(s,item,topLeft);
        const auto departure=std::find_if(checks.begin(),checks.end(),[&](const auto& c){return c.cell==origin;});
        if(departure==checks.end()||departure->valid||runtimeCellCanPlaceBuilding(s,item,topLeft)
            ||runtimePlacementPreviewValid(s)) return fail(std::string(type)+" "+std::to_string(hz)+"Hz building may appear under committed departure");
        const auto count=s.world.getObjects().size(); const auto lanes=runtimeProductionLanesStateHash(s.economy.localProductionLanes);
        if(placeRuntimeReadyBuilding(s,topLeft)||s.world.getObjects().size()!=count
            ||runtimeProductionLanesStateHash(s.economy.localProductionLanes)!=lanes) return fail("blocked click consumed building");
        if(!runtimeCellCanPlaceBuilding(s,item,topLeft,id)) return fail("ignored deployer was blocked by its own segment");
        const auto savedActor=*actor;
        actor->type="ORCA"; actor->kind=WorldObjectKind::Unit; actor->airborne=true;
        if(!runtimeCellCanPlaceBuilding(s,item,topLeft)) return fail("air segment blocks ground construction");
        *actor=savedActor; actor->alive=false; actor->health=0;
        if(!runtimeCellCanPlaceBuilding(s,item,topLeft)) return fail("dead mover retains ground construction clearance");
        *actor=savedActor;
        if(direction.x!=0&&direction.y!=0) {
            auto cornerItem=item; cornerItem.typeId="GAPILL"; cornerItem.kind=RuntimeBuildKind::Defense;
            for(const auto side:{RuntimeCellPoint{origin.x,goal.y},RuntimeCellPoint{goal.x,origin.y}}) {
                if(runtimeCellCanPlaceBuilding(s,cornerItem,side)) return fail("diagonal side is buildable during committed crossing");
                ++corners;
            }
        }
        clearRuntimeRenderPosition(*actor);
        if(runtimeCellCanPlaceBuilding(s,item,topLeft)) return fail("presentation reset released departure");
        while(s.behaviors.contains(id)&&s.behaviors.at(id).moveCooldownTicks>0) {++s.tick;advanceRuntimeSimulation(s);}
        ++s.tick;
        if(!runtimeCellCanPlaceSidebarBuilding(s,item,topLeft)||!runtimePlacementPreviewValid(s)
            ||!placeRuntimeReadyBuilding(s,topLeft)) return fail("arrival did not release placement");
        const auto* placed=findObjectById(s,s.world.getObjects().back().instanceId);
        if(!placed||placed->type!="GAPOWR"||runtimeStructureFootprintForObject(s,*placed).width!=2
            ||runtimeStructureFootprintForObject(s,*placed).height!=2) return fail("placed foundation differs from preview");
        ++cases;
    }
    std::cout<<"Moving foundation placement: "<<cases<<" native tank/infantry cases, "<<corners
        <<" diagonal side checks; preview/click/ready-order/arrival/render-reset/air/dead/ignored-deployer passed\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
