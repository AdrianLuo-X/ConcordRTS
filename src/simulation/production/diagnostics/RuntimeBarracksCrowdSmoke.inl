bool runRuntimeBarracksCrowdSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="Barracks crowd: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_BARRACKS_CROWD_OUTPUT");const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("native resources");
    assets->theaterLoaded = true;
    const auto metadata = runtimeTerrainMetadataContext(*assets);
    std::unordered_map<int, int> flatGroundLevels;
    for (const auto& cell : assets->world.getTerrainCells()) {
        const auto surface = runtimeNavigationTerrainInfoForCell(*assets, cell, metadata);
        if (surface.passable && surface.kind == RuntimeTerrainKind::Land)
            flatGroundLevels.emplace(runtimeCellKey(cell.x, cell.y), runtimeEffectiveTerrainLevel(cell));
    }
    const auto qualifies = [&](const RuntimeCellPoint& center) {
        const auto base = flatGroundLevels.find(runtimeCellKey(center.x, center.y));
        if (base == flatGroundLevels.end()) return false;
        const auto onFlatGround = [&](int x, int y) {
            const auto found = flatGroundLevels.find(runtimeCellKey(x, y));
            return found != flatGroundLevels.end() && found->second == base->second;
        };
        for (int dy = -7; dy <= 7; ++dy)
            for (int dx = -7; dx <= 7; ++dx)
                if (!onFlatGround(center.x + dx, center.y + dy)) return false;
        for (int dy = -9; dy <= -7; ++dy)
            for (int dx = -9; dx <= -7; ++dx)
                if (!onFlatGround(center.x + dx, center.y + dy)) return false;
        return true;
    };
    RuntimeCellPoint factoryCell{112, 215};
    if (!qualifies(factoryCell)) {
        const auto& mapCells = assets->world.getTerrainCells();
        const auto found = std::find_if(mapCells.begin(), mapCells.end(),
            [&](const auto& cell) { return qualifies({cell.x, cell.y}); });
        if (found == mapCells.end()) return fail("native map lacks a flat barracks and power-plant test area");
        factoryCell = {found->x, found->y};
    }
    const RuntimeCellPoint powerCell{factoryCell.x - 9, factoryCell.y - 9};
    const RuntimeCellPoint cameraCell{factoryCell.x + 2, factoryCell.y + 4};
    std::cout << "Barracks crowd stock terrain: factory=" << factoryCell.x << ',' << factoryCell.y
              << " power=" << powerCell.x << ',' << powerCell.y << '\n';
    int cases=0,totalProduced=0,farthest=0,saves=0,paired=0;
    for(const auto& types:std::array<std::array<const char*,4>,3>{{
        {{"GAPILE","E1","Americans","GAPOWR"}},{{"NAHAND","E2","Russians","NAPOWR"}},{{"YABRCK","INIT","YuriCountry","YAPOWR"}}
    }})for(int hz:{30,15,60}) {
        scene=std::string(types[0])+"-"+std::to_string(hz);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=hz;s.config.playerCountry=types[2];s.config.playerSlots.clear();
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=s.config.multiplayerActive=false;
        s.config.startingUnits=0;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        auto& cells=s.world.accessTerrainCells();
        cells.erase(std::remove_if(cells.begin(), cells.end(), [&](const auto& cell) {
            return cell.x < factoryCell.x - 20 || cell.x > factoryCell.x + 28
                || cell.y < factoryCell.y - 17 || cell.y > factoryCell.y + 29;
        }), cells.end());
        s.report=assets->report;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner=types[2];s.economy.credits=1000000;s.economy.skirmishStartApplied=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        auto* factory = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, types[0], factoryCell);
        if (!factory) return fail("native barracks");
        const int factoryId = factory->instanceId;
        factory->buildAnimTicksTotal = factory->buildAnimTicksRemaining = 0;
        auto* power = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, types[3], powerCell);
        if (!power) return fail("power plant");
        power->buildAnimTicksTotal = power->buildAnimTicksRemaining = 0;
        s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);refreshRuntimeObjectOccupancyAfterLocalChange(s);
        RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Infantry;item.typeId=types[1];item.requiredFactory="InfantryType";
        const auto* native=findRuntimeUnitTypeById(s.rules,item.typeId);if(!native)return fail("native infantry");
        item.cost=native->cost;item.buildTicks=runtimeBaseBuildTimeTicks(s,item);s.economy.catalog={item};
        const auto spawn=findRuntimeProductionSpawnForOwner(s,item,s.economy.owner,false);
        if(!spawn) {
            const auto* barracks = findObjectById(s, factoryId);
            if (!barracks) return fail("native exit: barracks missing before production");
            std::ostringstream detail;
            detail << "native exit: factory=" << barracks->type << " at=" << barracks->x << ',' << barracks->y
                   << " powered=" << barracks->poweredOperational
                   << " buildupTicks=" << barracks->buildAnimTicksRemaining << '/' << barracks->buildAnimTicksTotal
                   << " terrainCells=" << cells.size();
            const auto footprint = runtimeStructureFootprintCellsForObject(s, *barracks);
            for (const auto& cell : footprint)
                detail << " foundation=" << cell.x << ',' << cell.y << ':'
                       << (s.terrainByCell.contains(runtimeCellKey(cell.x,cell.y)) ? "terrain" : "missing");
            return fail(detail.str());
        }
        const auto door=spawn->cell;
        const auto capture=[&](const char* suffix){return root.empty()||hz!=30||drawRuntimeStateOffscreenAtCell(s,cameraCell,960,720,error,root+"/"+scene+"-"+suffix+".bmp",false).has_value();};
        int queued=0,lastCompletion=0,lastCount=0,maxDistance=0;bool savedCrowd=false;
        constexpr int targetCount=96;
        for(int n=0;n<hz*400 && s.economy.completedInfantry<targetCount;++n) {
            while(queued<targetCount&&queued-s.economy.completedInfantry<5) {
                if(!queueRuntimeBuildOrder(s,item))return fail("real queue: "+s.economy.status);++queued;
            }
            auto& lane=s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Infantry)];
            if(lane.active&&!completeRuntimeActiveProductionForProbe(s,item))return fail("completed order payment");
            std::map<int,RuntimeCellPoint> before;
            for(const auto& o:s.world.getObjects())if(o.kind==WorldObjectKind::Infantry)before.emplace(o.instanceId,runtimeCellPointForObject(o));
            ++s.tick;advanceRuntimeSimulation(s,false);
            std::map<int,int> counts;
            for(const auto& o:s.world.getObjects())if(o.kind==WorldObjectKind::Infantry&&o.alive) {
                if(++counts[runtimeCellKey(o.x,o.y)]>3)return fail("more than three infantry in one cell");
                const auto old=before.find(o.instanceId);
                if(old!=before.end()&&(std::abs(old->second.x-o.x)>1||std::abs(old->second.y-o.y)>1))return fail("clearance teleported infantry");
                maxDistance=std::max(maxDistance,std::max(std::abs(o.x-door.x),std::abs(o.y-door.y)));
            }
            if(lastCount!=s.economy.completedInfantry) {
                lastCount=s.economy.completedInfantry;lastCompletion=s.tick;
                if(lastCount%24==0)std::cout<<scene<<" produced="<<lastCount<<" tick="<<s.tick<<" radius="<<maxDistance<<'\n';
            }
            if(hz==30 && lastCount>=48 && !savedCrowd) {
                std::array<std::unique_ptr<RuntimeWindowState>,2> restored;
                for(int exact=0;exact<2;++exact) {
                    std::ostringstream saved;IniFile ini;
                    if(!writeRuntimeStateToStream(s,saved,error)
                        ||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))
                        ||!ini.loadFromString(saved.str()))return fail("crowd save: "+error);
                    restored[exact]=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored[exact],s,false);
                    if(!materializeRuntimeLoadStateFromIni(*restored[exact],ini,error,exact!=0)
                        ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*restored[exact],ini,error)))return fail("crowd reload: "+error);
                    ++saves;
                }
                const auto advance=[&](RuntimeWindowState& state){
                    RuntimeSnapshotActiveStateScope scope(state);
                    const auto& pending=state.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Infantry)];
                    if(pending.active&&!completeRuntimeActiveProductionForProbe(state,item))return false;
                    ++state.tick;advanceRuntimeSimulation(state,false);return true;
                };
                for(int tick=0;tick<120;++tick) {
                    if(!advance(s))return fail("live continuation payment");
                    for(auto& clone:restored) {
                        if(!advance(*clone))return fail("restored continuation payment");++paired;
                        if(s.economy.credits!=clone->economy.credits||s.economy.completedInfantry!=clone->economy.completedInfantry
                            ||runtimeProductionLanesStateHash(s.economy.localProductionLanes)!=runtimeProductionLanesStateHash(clone->economy.localProductionLanes))return fail("restored queue/payment diverged");
                        for(const auto& o:s.world.getObjects())if(o.kind==WorldObjectKind::Infantry) {
                            const auto* other=findObjectById(*clone,o.instanceId);
                            if(!other||o.x!=other->x||o.y!=other->y||o.subCell!=other->subCell||o.renderMoveActive!=other->renderMoveActive)
                                return fail("restored infantry motion diverged");
                        }
                    }
                }
                savedCrowd=true;lastCount=s.economy.completedInfantry;lastCompletion=s.tick;
            }
            if (s.tick - lastCompletion > hz * 15) {
                capture("stalled");
                std::ostringstream detail;
                detail << "production stalled at " << lastCount << " status=" << s.economy.status
                       << " door=" << door.x << ',' << door.y << " tick=" << s.tick;
                for (const auto& actor : s.world.getObjects()) {
                    if (!actor.alive || actor.kind != WorldObjectKind::Infantry
                        || std::abs(actor.x - door.x) > 4 || std::abs(actor.y - door.y) > 4) continue;
                    const auto offset = worldObjectSubCellOffset(actor);
                    detail << " [id=" << actor.instanceId << " at=" << actor.x << ',' << actor.y
                           << " body=" << runtimeObjectRenderX(actor) + offset.x << ','
                           << runtimeObjectRenderY(actor) + offset.y << " slot=" << actor.subCell;
                    const auto order = s.behaviors.find(actor.instanceId);
                    if (order != s.behaviors.end()) {
                        const auto& move = order->second;
                        detail << " goal=" << move.moveTarget.x << ',' << move.moveTarget.y
                               << " state=" << static_cast<int>(move.state)
                               << " path=" << move.path.size() << " cooldown=" << move.moveCooldownTicks;
                    }
                    detail << ']';
                }
                return fail(detail.str());
            }
        }
        if(s.economy.completedInfantry!=targetCount||s.economy.credits!=1000000-targetCount*item.cost||maxDistance<5)
            return fail("bulk queue count/payment/distance");
        if(!capture("finished"))return fail("native crowd rendering: "+error);
        totalProduced+=s.economy.completedInfantry;farthest=std::max(farthest,maxDistance);++cases;
    }
    std::cout<<"Barracks crowd: cases="<<cases<<" produced="<<totalProduced<<" maxRadius="<<farthest<<" fullSaves="<<saves<<" pairedTicks="<<paired<<" realQueues=yes paidOnce=yes noTeleport=yes threePerCell=yes PASS\n";
    return true;
}
