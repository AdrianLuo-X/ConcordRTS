// Benchmark-only binding AFTER normal authored-map navigation/economy/start/
// trigger/vision initialization. No GameWorld, RulesDatabase or map rewrite.
#pragma once

bool prepareRuntimeKeepAuthoredMovementBenchmark(RuntimeWindowState& state,
    std::vector<int>& armyIds, std::array<RuntimeCellPoint, 2>& goals,
    RuntimeCellPoint& camera, std::string& error) {
    const auto fail = [&](const char* reason) {
        error = std::string("keep authored movement: ") + reason; return false;
    };
    if (state.tick != 0 || !state.behaviors.empty() || !state.report.success
        || !state.report.map.loaded || !state.rulesLoaded || !state.theaterLoaded
        || !state.visionEnabled || state.economy.owner != "Americans"
        || state.config.multiplayerActive || state.campaignMode
        || !state.config.skirmishLoadSavePath.empty()) return fail("normal fresh authored startup required");
    if (state.report.map.size != "0,0,240,240"
        || state.report.map.localSize != "2,4,236,232"
        || state.report.map.width != 480 || state.report.map.height != 480
        || state.world.getObjects().size() != 65)
        return fail("flat lake authored geometry/population differs");
    if (std::getenv("CONCORD_LIVE_BENCHMARK_CELL"))
        return fail("full-map diagnostic camera override is incompatible");
    constexpr std::array<const char*,12> roster{
        "MTNK","SREF","TTNK","FV","SCHP","HTNK",
        "E1","CLEG","JUMPJET","SNIPE","GHOST","E2"};
    constexpr std::array<const char*,9> bases{
        "GACNST","GAPOWR","GAPOWR","GAREFN","GAPILE",
        "GAWEAP","AMRADR","GATECH","GADEPT"};
    std::map<std::string,int> census;
    std::set<std::string> mapIds;
    std::set<std::pair<int,int>> occupiedFootprints;
    std::size_t neutral = 0, baseCount = 0;
    const WorldObjectInstance* ground = nullptr;
    armyIds.clear(); armyIds.reserve(48);
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health != runtimeMaxHealthForObject(state,object))
            return fail("authored object is not alive at normal full health");
        if (!mapIds.insert(object.mapId).second) return fail("duplicate authored map identity");
        const auto point = runtimeCellPointForObject(object);
        if (point != RuntimeCellPoint{object.x,object.y}
            || !findRuntimeTerrainCellAt(state,point)) return fail("authored actor cell absent");
        if (object.kind == WorldObjectKind::Structure) {
            const auto footprint = runtimeStructureFootprintForObject(state,object);
            for (int y=0;y<footprint.height;++y) for (int x=0;x<footprint.width;++x) {
                const RuntimeCellPoint cell{object.x+x,object.y+y};
                if (!findRuntimeTerrainCellAt(state,cell)
                    || !occupiedFootprints.emplace(cell.x,cell.y).second)
                    return fail("structure footprint outside terrain or overlapping");
            }
            if (object.owner == "Neutral") { ++neutral; continue; }
            if (object.owner != "Americans" || !object.mapId.starts_with("KEEP_BASE_"))
                return fail("unexpected player structure");
            ++baseCount;
            ++census[std::string("base:")+object.type];
            continue;
        }
        if (object.owner != "Americans"
            || !(object.mapId.starts_with("KEEP_UNIT_") || object.mapId.starts_with("KEEP_INF_")))
            return fail("non-roster actor entered selected army");
        const auto* type = findRuntimeUnitType(state.rules,object);
        if (!type || object.harvestAutoSearchSuspended)
            return fail("stock unit rules absent or harvesting artificially paused");
        const auto legal = findRuntimePassableCellNear(state,object,point,0);
        if (!legal || *legal != point) return fail("authored unit native initial cell not passable");
        if (!ground && object.kind == WorldObjectKind::Unit
            && runtimeObjectMovementMode(state,object) == PathMovementMode::Ground) ground=&object;
        ++census[object.type]; armyIds.push_back(object.instanceId);
    }
    for (int id : armyIds) {
        const auto* actor=findObjectById(state,id);
        if (!actor || occupiedFootprints.contains({actor->x,actor->y}))
            return fail("army overlaps original or appended structure footprint");
    }
    if (neutral != 8 || baseCount != 9 || armyIds.size() != 48 || !ground)
        return fail("8 original neutral + 9 base + 48 army census differs");
    for (const char* type : roster) if (census[type] != 4)
        return fail("stock 12-type roster must contain exactly four each");
    std::map<std::string,int> baseCensus;
    for (const char* type : bases) ++baseCensus[type];
    for (const auto& [type,count] : baseCensus) if (census["base:"+type] != count)
        return fail("stock nine-building roster differs");
    if (!runtimeRadarAvailable(state) || state.economy.powerProduced < state.economy.powerUsed)
        return fail("normal radar or normal power not available");
    goals = {RuntimeCellPoint{132,148},RuntimeCellPoint{132,136}};
    for (const auto& cell : goals) {
        const auto legal = findRuntimePassableCellNear(state,*ground,cell,0);
        if (!state.vision.isCellExploredBy(state.economy.owner,cell.x,cell.y)
            || !legal || *legal != cell) return fail("normal explored/passable cyclic goal absent");
    }
    camera={132,143};
    std::sort(armyIds.begin(),armyIds.end());
    std::size_t ore=0,gems=0;
    for (const auto& cell : state.world.getTerrainCells()) {
        ore+=cell.overlayIsOre; gems+=cell.overlayIsGem;
    }
    const auto* vision=state.vision.statsFor(state.economy.owner);
    if (!vision || vision->exploredCells <= 0 || vision->exploredCells >= vision->validCells)
        return fail("normal partial exploration not witnessed");
    std::cout << "Live authored world fixture: keepAuthoredWorld=yes hydration=normal"
        << " worldObjects=65 originalNeutral=8 baseObjects=9 armyUnits=48 distinctTypes=12"
        << " terrainCells=" << state.world.getTerrainCells().size()
        << " headerWidth=" << state.report.map.width << " headerHeight=" << state.report.map.height
        << " mapSize=" << state.report.map.size << " localSize=" << state.report.map.localSize
        << " overlays=" << state.world.getOverlayCells().size() << " oreCells=" << ore << " gemCells=" << gems
        << " vision=yes radar=yes powerProduced=" << state.economy.powerProduced
        << " powerUsed=" << state.economy.powerUsed << " exploredCells=" << vision->exploredCells
        << " validCells=" << vision->validCells << " fullReveal=no automaticHarvestPaused=no"
        << " activeSidebarTab=" << static_cast<int>(state.activeBuildTab)
        << " catalogEntries=" << state.economy.catalog.size()
        << " goals=132,148;132,136 camera=132,143 normalStartupCompletedTick=0 passed=yes\n";
    error.clear(); return true;
}
