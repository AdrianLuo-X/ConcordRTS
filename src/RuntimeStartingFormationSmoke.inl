bool runRuntimeStartingFormationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Starting formation: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original resources");
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    const int clearSet = assets->theater.getGeneralTileSetIndex("ClearTile"); int clearTile = -1;
    const auto terrainContext = runtimeTerrainMetadataContext(*assets);
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        if (assets->theater.resolveTileIndex(tile).tileSetIndex == clearSet
            && resolveRuntimeTerrainSubTileLandType(cell, terrainContext, assets->renderCache) == RuntimeTerrainLandType::Clear
            && resolveRuntimeTerrainSubTileRampType(cell, terrainContext, assets->renderCache) == 0) { clearTile = tile; break; }
    }
    if (clearTile < 0) return fail("flat native Clear TMP");
    const char* output = std::getenv("CONCORD_STARTING_FORMATION_DIR");
    const std::filesystem::path root = output && *output ? output : "build/smoke/feature-set-20/03-starting-formation";
    std::filesystem::create_directories(root); std::ofstream csv(root / "positions.csv");
    csv << "country,count,obstacle,unholy,type,x,y,veterancy\n";
    struct Scene { int count; bool obstacle, unholy; };
    const Scene scenes[] = {{0,false,false},{6,false,false},{10,false,false},{15,false,false},
        {50,false,false},{15,true,false},{15,false,true}};
    int cases = 0, deployments = 0;
    for (const char* country : {"Americans", "Russians", "YuriCountry"}) for (const auto& scene : scenes) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.playerSlots.clear(); s.config.playerCountry = country;
        s.config.gameMode = scene.unholy ? "UnholyAlliance" : "Battle"; s.config.gameModeRulesOverride.clear();
        s.config.fixedTickHz = 30; s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater; s.report = assets->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true;
        s.world = assets->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear(); s.mapFacts = {};
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = country; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        RuntimeHumanHouseAuthority authority; authority.owner = country;
        authority.canonicalOwner = toLowerAsciiLocal(country); authority.localFacade = true;
        s.humanHouseAuthorities = {authority}; initializeRuntimeLocalProductionLanes(s.economy);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = clearTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact; fact.x = x; fact.y = y; fact.cellIndex = cell.cellIndex512; fact.hasTerrain = true;
            fact.terrain.x = x; fact.terrain.y = y; fact.terrain.tileIndex = clearTile; fact.terrain.subTileIndex = 0;
            fact.terrain.level = fact.heightLevel = 0; fact.terrainClass = "clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex, s.mapFacts.cells.size()); s.mapFacts.cells.push_back(fact);
        }
        s.mapFacts.terrainCellCount = s.mapFacts.lookupIndexSize = s.mapFacts.cells.size(); s.mapFacts.contentHash = 0x20030001;
        initializeRuntimeNavigation(s); RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint origin{30,30}; const std::string yard = runtimeConstructionYardForCountry(country);
        // The ordinary start finder supplies a corner; keep this fixture
        // centered on its original reference ring at origin (stock 4x4 ART).
        auto* mcv = spawnRuntimeSkirmishBase(s, country, yard, {origin.x-1,origin.y-1});
        if (!mcv) return fail("original MCV");
        if (scene.obstacle && !addRuntimeObjectInstanceForOwner(s, "Obstacle", RuntimeBuildKind::Building,
                "GAPOWR", {25,25})) return fail("formation obstacle");
        const int spawned = spawnRuntimeSkirmishStartingUnits(s, country, country, origin, scene.count);
        const int expectedExtra = scene.unholy ? 2 : 0;
        if (spawned != scene.count + expectedExtra) return fail(std::string(country) + " changed starting count");
        std::vector<int> mcvs; std::map<int,int> infantry, vehicles;
        std::set<std::pair<int,int>> vehiclePositions; std::map<std::pair<int,int>,int> infantryPositions;
        for (const auto& object : s.world.getObjects()) if (object.alive && object.owner == country) {
            csv << country << ',' << scene.count << ',' << scene.obstacle << ',' << scene.unholy << ','
                << object.type << ',' << object.x << ',' << object.y << ',' << object.veterancy << '\n';
            if (object.veterancy != 0) return fail("reference layout introduced a unit promotion");
            const auto* type = findRuntimeUnitType(s.rules, object);
            if (type && runtimeUnitTypeIsMcv(s, *type)) { mcvs.push_back(object.instanceId); continue; }
            if (object.kind == WorldObjectKind::Infantry) {
                ++infantry[object.cell]; ++infantryPositions[{object.x,object.y}];
            } else { ++vehicles[object.cell]; vehiclePositions.emplace(object.x,object.y); }
            if (!scene.obstacle && !scene.unholy && scene.count <= 15
                && std::max(std::abs(object.x-origin.x),std::abs(object.y-origin.y)) < 3)
                return fail("starting troop placed beside/inside the MCV center");
        }
        for (const auto& [cell,count] : infantry) if (count > 3 || vehicles.contains(cell)) return fail("mixed ground occupancy");
        for (const auto& [cell,count] : vehicles) if (count > 1) return fail("multiple vehicles in one cell");
        if (scene.count == 15 && !scene.obstacle && !scene.unholy) {
            const std::set<std::pair<int,int>> expectedVehicles{{25,25},{30,25},{35,25},{35,30},{35,35}};
            const std::map<std::pair<int,int>,int> expectedInfantry{{{25,30},3},{{25,35},3},{{30,35},3},{{34,30},1}};
            if (vehiclePositions != expectedVehicles || infantryPositions != expectedInfantry)
                return fail("open-ground reference ring/groups differ");
        }
        if (scene.count == 15 && !scene.obstacle && !scene.unholy
            && !drawRuntimeStateOffscreenAtCell(s, origin, 960,640,error,
                (root / (std::string(country)+"-opening.bmp")).string(), false)) return fail("opening render: "+error);
        // Original ART specifies 4 x 4 for all three construction yards. Check actors
        // independently, then execute the real selected-MCV deployment command.
        for (int mcvId : mcvs) {
            const auto* base = findObjectById(s,mcvId); if (!base) return fail("missing MCV");
            const auto* type = findRuntimeUnitType(s.rules,*base);
            const auto item = type ? runtimeBuildItemForBuilding(s,type->deploysInto) : std::optional<RuntimeBuildCatalogItem>{};
            const auto* originalArt = type ? s.art.findArtType(type->deploysInto) : nullptr;
            if (!originalArt || originalArt->foundation != "4x4") return fail("original 4x4 foundation fixture changed");
            for (const auto& troop : s.world.getObjects()) if (troop.alive && troop.instanceId != mcvId
                && troop.x >= base->x-1 && troop.x < base->x+3 && troop.y >= base->y-1 && troop.y < base->y+3)
                return fail("starting troop blocks 4x4 deployment foundation");
            if (!item || !runtimeCellCanPlaceBuilding(s,*item,
                {base->x-1,base->y-1},mcvId,nullptr,country))
                return fail("MCV footprint is not immediately deployable");
        }
        // The turn now advances the real simulation. Keep an opposing house
        // alive so this single-player fixture does not settle before the turn.
        const char* opponent=std::string(country)=="Russians"?"Americans":"Russians";
        if(!addRuntimeObjectInstanceForOwner(s,opponent,RuntimeBuildKind::Building,"NAPOWR",{1,1}))
            return fail("surviving opposing-house fixture");
        applySelection(s,mcvs,false,false);
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(s)) return fail("player deployment rejected");
        for (int tick=0;tick<s.config.fixedTickHz*6;++tick) {
            bool mobile=false; for(int id:mcvs) mobile=mobile||findObjectById(s,id)!=nullptr;
            if(!mobile) break;
            RuntimeSnapshotActiveStateScope scope(s); ++s.tick; advanceRuntimeSimulation(s,false);
            for(int id:mcvs)if(const auto* stopped=findObjectById(s,id);stopped&&!runtimeMissionIsMcvDeployPending(*stopped)) {
                const auto occupancy=buildRuntimeOccupancyLookup(s,id);
                const auto committed=runtimeGroundMoveSegmentPlacementCells(s,id);
                std::cout<<"Deployment rejection tick="<<s.tick<<" status="<<s.economy.status<<"\n";
                for(const auto& object:s.world.getObjects())std::cout<<"Deployment actor "<<object.type<<"/"<<object.instanceId
                    <<" alive="<<object.alive<<" owner="<<object.owner<<" cell="<<object.x<<","<<object.y
                    <<" mission="<<object.mission<<"\n";
                for(const auto& p:runtimeStructureFootprintCells(
                    {stopped->x-1,stopped->y-1},
                    runtimeStructureFootprintForBuildItem(s,*runtimeBuildItemForBuilding(s,yard)))) {
                    const int key=runtimeCellKey(p.x,p.y);
                    if(occupancy.contains(key)||committed.contains(key))std::cout<<"Deployment blocked cell="<<p.x<<","<<p.y
                        <<" occupant="<<(occupancy.contains(key)?occupancy.at(key):-1)<<" committed="<<committed.contains(key)<<"\n";
                }
                return fail(std::string(country)+"/"+std::to_string(scene.count)+" turn ended without deployment");
            }
        }
        for (int mcvId : mcvs) if (const auto* remaining=findObjectById(s,mcvId)) {
            for(const auto& object:s.world.getObjects())if(object.alive&&object.x>=origin.x-1&&object.x<=origin.x+4
                &&object.y>=origin.y-1&&object.y<=origin.y+4)std::cout<<"Deployment neighbor "<<object.type<<"/"<<object.instanceId
                    <<" cell="<<object.x<<","<<object.y<<" mission="<<object.mission<<"\n";
            return fail(std::string(country)+"/"+std::to_string(scene.count)+" MCV remained mobile after deploy; outcome="
                +s.skirmishResult+" mission="+remaining->mission+" facing="+std::to_string(remaining->facing)+" status="+s.economy.status);
        }
        deployments += static_cast<int>(mcvs.size()); ++cases;
    }
    std::cout << "Starting formation: passed cases=" << cases << " unobstructedDeployments=" << deployments
        << " factions=3 counts=0/6/10/15/50 obstacleFallback=yes unholy=yes referenceLayout=yes ranks=rookie\n";
    return true;
}
