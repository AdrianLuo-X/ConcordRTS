// Issue 16: genuine starting troops, original ore and immutable mine obstacles.
// The four-mine cage is a deterministic navigation counterexample; it is not
// asserted to be the layout in the user's small screenshot.
namespace RuntimeStartingOreEscapeOracle {
inline std::array<double, 3> body(const WorldObjectInstance& object) {
    const bool render = worldObjectHasGroundRenderPose(object);
    return {render ? object.renderX : double(object.x), render ? object.renderY : double(object.y),
        render ? object.renderLevel : double(object.level)};
}
inline std::string describe(const RuntimeWindowState& state, int id) {
    const auto* object = findObjectById(state, id);
    if (!object) return "missing#" + std::to_string(id);
    const auto point = body(*object);
    std::ostringstream out;
    out << std::setprecision(17) << object->type << '#' << id << " HP=" << object->health
        << " cell=" << object->x << ',' << object->y << " body=" << point[0] << ',' << point[1]
        << " mission=" << object->mission;
    const auto it = state.behaviors.find(id);
    if (it != state.behaviors.end()) {
        const auto& move = it->second;
        out << " goal=" << move.moveTarget.x << ',' << move.moveTarget.y
            << " pending=" << move.pathRequestPending << " blocked=" << move.blockedTicks
            << " cooldown=" << move.moveCooldownTicks << " path=" << move.path.size()
            << " replans=" << move.pathReplans;
    }
    return out.str();
}
inline void step(RuntimeWindowState& state) {
    RuntimeSnapshotActiveStateScope active(state);
    ++state.tick;
    advanceRuntimeSimulation(state, false);
    completeRuntimeWorldRefresh(state);
}
inline bool settled(const RuntimeWindowState& state, int id) {
    const auto* object = findObjectById(state, id);
    return object && object->alive && !object->renderMoveActive && !state.behaviors.contains(id);
}
inline bool insideMine(double x, double y, const WorldTerrainObject& mine) {
    return x > mine.x - .5 + 1e-8 && x < mine.x + mine.footprintWidth - .5 - 1e-8
        && y > mine.y - .5 + 1e-8 && y < mine.y + mine.footprintHeight - .5 - 1e-8;
}
} // namespace RuntimeStartingOreEscapeOracle

#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeStartingOreEscapeSmoke(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimeStartingOreEscapeOracle;
    std::cout << std::unitbuf;
    const char* destination = std::getenv("CONCORD_STARTING_ORE_ESCAPE_OUTPUT");
    const std::filesystem::path root = destination && *destination
        ? destination : "build/issue22_16_starting_ore_escape";
    const char* chosen = std::getenv("CONCORD_STARTING_ORE_ESCAPE_CASE");
    const std::string filter = chosen ? chosen : "";
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Starting ore escape " + label + " [" + stage + "]: " + why;
        return false;
    };
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original resources");
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    IniFile raw;
    if (!raw.loadFromFile(assets->report.rulesPath)) return fail("original rules file");
    const auto originalHash = assets->rules.getDetailRulesHash();
    int clear = -1;
    const int clearSet = assets->theater.getGeneralTileSetIndex("ClearTile");
    const auto metadata = runtimeTerrainMetadataContext(*assets);
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        if (assets->theater.resolveTileIndex(tile).tileSetIndex == clearSet
            && resolveRuntimeTerrainSubTileLandType(cell, metadata, assets->renderCache) == RuntimeTerrainLandType::Clear
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, assets->renderCache) == 0) {
            clear = tile; break;
        }
    }
    if (clear < 0) return fail("original flat Clear TMP");
    std::filesystem::create_directories(root);
    for (const char* name : {"cases.csv", "births.csv", "orders.csv", "movement.csv", "terrain.csv"})
        if (std::filesystem::exists(root / name)) return fail(std::string("evidence already exists: ") + name);
    std::ofstream cases(root / "cases.csv"), births(root / "births.csv"), orders(root / "orders.csv"),
        movement(root / "movement.csv"), terrain(root / "terrain.csv");
    if (!cases || !births || !orders || !movement || !terrain) return fail("evidence files unavailable");
    movement << std::setprecision(17);
    cases << "case,country,hz,requested,spawned,mines,ore_cells,short_orders,far_orders,ticks,result\n";
    births << "case,id,type,kind,owner,hp,x,y,level,subcell,veterancy,mcv\n";
    orders << "case,stage,tick,id,requested_x,requested_y,accepted,assigned_x,assigned_y,manhattan\n";
    movement << "case,stage,tick,hz,id,type,hp,x,y,level,subcell,body_x,body_y,body_z,moving,"
        "behavior,goal_x,goal_y,pending,blocked,cooldown,path_count,path_first_x,path_first_y,"
        "path_last_x,path_last_y,replans,ore_bails,local_avoidance_moves,sim_repaths\n";
    terrain << "case,kind,type,x,y,radius,width,height,strength,immune,occupation,bails,overlay\n";
    struct Scene { const char* country; int count; int hz; bool cage; };
    std::vector<Scene> scenes;
    for (const char* country : {"Americans", "Russians", "YuriCountry"})
        for (int count : {15, 50}) for (int hz : {30, 60}) scenes.push_back({country, count, hz, false});
    for (const char* country : {"Americans", "Russians", "YuriCountry"}) scenes.push_back({country, 15, 30, true});
    int passed = 0, totalShort = 0, totalFar = 0;
    try {
        for (const auto& scene : scenes) {
            label = std::string(scene.cage ? "cage-" : "ore-") + scene.country
                + (scene.cage ? "" : "-" + std::to_string(scene.count)) + '-' + std::to_string(scene.hz);
            if (!filter.empty() && label != filter && label.find(filter) == std::string::npos) continue;
            stage = "original fixture";
            auto box = std::make_unique<RuntimeWindowState>(); auto& state = *box;
            state.config = config;
            state.config.playerCountry = scene.country;
            state.config.gameMode = "Battle";
            state.config.gameModeRulesOverride.clear(); state.config.playerSlots.clear();
            state.config.fixedTickHz = scene.hz; state.config.startingUnits = scene.count;
            state.config.computerAiEnabled = state.config.multiplayerActive = state.config.noCooldown = false;
            state.config.shortGame = state.config.crates = state.config.superweapons = false;
            state.rules = assets->rules; state.art = assets->art; state.theater = assets->theater;
            state.report = assets->report;
            state.report.map.size.clear(); state.report.map.localSize.clear();
            state.report.map.width = state.report.map.height = -1;
            state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
            state.mapFacts = {}; state.mapFactsLoaded = false;
            state.world = assets->world;
            state.world.accessObjects().clear(); state.world.accessTerrainCells().clear();
            state.world.accessTerrainObjects().clear(); state.world.accessSmudges().clear();
            state.visionEnabled = state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
            state.runtimeSuppressPlayerAudioFeedback = true;
            state.economy.owner = scene.country;
            state.economy.skirmishStartApplied = true;
            state.triggers.loaded = state.triggers.bootstrapComplete = true;
            RuntimeHumanHouseAuthority human;
            human.owner = human.country = scene.country;
            human.canonicalOwner = toLowerAsciiLocal(scene.country); human.localFacade = true;
            state.humanHouseAuthorities = {human};
            initializeRuntimeLocalProductionLanes(state.economy);
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
                TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
                cell.tileIndex = clear; cell.subTileIndex = 0;
                state.world.accessTerrainCells().push_back(cell);
            }
            const auto addMine = [&](const char* type, RuntimeCellPoint cell, int radius) {
                const auto* definition = state.rules.findTerrainType(type);
                const auto* art = state.art.findArtType(type);
                if (!definition || !definition->spawnsTiberium || !definition->immune || !art || art->foundation != "1x1")
                    return false;
                WorldTerrainObject mine;
                mine.type = type; mine.mapId = "issue16-" + std::to_string(state.world.getTerrainObjects().size());
                mine.x = cell.x; mine.y = cell.y; mine.cell = runtimeCellKey(cell.x, cell.y);
                mine.strength = std::stoi(raw.getValue(type, "Strength", raw.getValue("General", "TreeStrength", "1")));
                if (mine.strength == 0) mine.strength = std::stoi(raw.getValue("General", "TreeStrength", "1"));
                mine.strength = std::max(1, mine.strength); mine.health = mine.strength;
                mine.immune = definition->immune;
                const bool snow = toLowerAsciiLocal(state.theater.getTileExtension()).find("sno") != std::string::npos;
                mine.occupationBits = std::stoi(raw.getValue(type, snow ? "SnowOccupationBits" : "TemperateOccupationBits", "7"));
                // All three original mines default to full-cell occupation 7.
                if (mine.occupationBits != 7) return false;
                mine.occupiedSubCells = {0, 1, 2, 3, 4};
                state.world.accessTerrainObjects().push_back(mine);
                terrain << label << ",mine," << type << ',' << cell.x << ',' << cell.y << ',' << radius
                    << ",1,1," << mine.strength << ",1,7,0,none\n";
                return true;
            };
            if (scene.cage) {
                for (const auto& pair : std::array<std::pair<const char*, RuntimeCellPoint>, 4>{{
                        {"TIBTRE01", {24,25}}, {"TIBTRE02", {26,25}},
                        {"TIBTRE03", {25,24}}, {"TIBTRE01", {25,26}}}})
                    if (!addMine(pair.first, pair.second, 0)) return fail("original cage mine definition");
            } else if (!addMine("TIBTRE01", {24,25}, 3) || !addMine("TIBTRE01", {24,35}, 4))
                return fail("original mine definition");
            const auto placeOre = [&](RuntimeCellPoint center, int radius) {
                for (auto& cell : state.world.accessTerrainCells()) {
                    const int dx = cell.x - center.x, dy = cell.y - center.y;
                    if (dx*dx + dy*dy > radius*radius) continue;
                    bool mineCell = false;
                    for (const auto& mine : state.world.getTerrainObjects())
                        mineCell = mineCell || (cell.x == mine.x && cell.y == mine.y);
                    if (!mineCell) setRuntimeResourceTerrainBails(cell, "TIB", runtimeOreMaxBails);
                }
            };
            if (scene.cage) placeOre({25,25}, 3);
            else { placeOre({24,25}, 3); placeOre({24,35}, 4); }
            initializeRuntimeNavigation(state);
            RuntimeSnapshotActiveStateScope active(state);
            const RuntimeCellPoint origin{30,30};
            const std::string yard = runtimeConstructionYardForCountry(scene.country);
            auto* base = spawnRuntimeSkirmishBase(state, scene.country, yard, origin);
            if (!base) return fail("original mobile MCV");
            const int mcv = base->instanceId;
            if (spawnRuntimeSkirmishStartingUnits(state, scene.country, scene.country, origin, scene.count) != scene.count)
                return fail("starting troop count changed");
            std::vector<int> troops, infantry, vehicles;
            std::map<int, int> originalHealth;
            std::map<int, RuntimeCellPoint> originalCells;
            std::map<int, int> infantryCells, vehicleCells;
            int oreCells = 0;
            for (const auto& cell : state.world.getTerrainCells()) if (cell.overlayIsOre) {
                ++oreCells;
                terrain << label << ",ore," << cell.overlayType << ',' << cell.x << ',' << cell.y
                    << ",0,1,1,0,0,0," << runtimeResourceBailCountForTerrain(cell) << ',' << cell.overlayIndex << '\n';
            }
            for (const auto& object : state.world.getObjects()) if (object.alive && object.owner == scene.country) {
                const auto* type = findRuntimeUnitType(state.rules, object);
                if (!type || object.health != type->strength || object.veterancy != 0) return fail("original full HP / rookie identity");
                const bool isMcv = object.instanceId == mcv;
                births << label << ',' << object.instanceId << ',' << object.type << ',' << int(object.kind) << ',' << object.owner
                    << ',' << object.health << ',' << object.x << ',' << object.y << ',' << object.level << ',' << object.subCell
                    << ',' << object.veterancy << ',' << isMcv << '\n';
                if (isMcv) continue;
                if (object.x >= 30 && object.x < 34 && object.y >= 30 && object.y < 34) return fail("troop blocked the original MCV 4x4 foundation");
                troops.push_back(object.instanceId); originalHealth[object.instanceId] = object.health;
                originalCells[object.instanceId] = runtimeCellPointForObject(object);
                if (object.kind == WorldObjectKind::Infantry) { infantry.push_back(object.instanceId); ++infantryCells[object.cell]; }
                else { vehicles.push_back(object.instanceId); ++vehicleCells[object.cell]; }
            }
            if (troops.size() != static_cast<std::size_t>(scene.count) || infantry.size() < 3 || vehicles.empty() || !oreCells)
                return fail("original troop/ore inventory");
            for (const auto& [cell, count] : infantryCells)
                if (count > 3 || vehicleCells.contains(cell)) return fail("invalid initial infantry capacity");
            for (const auto& [cell, count] : vehicleCells)
                if (count > 1) return fail("invalid initial vehicle capacity");
            const auto item = runtimeBuildItemForBuilding(state, yard);
            const auto* yardArt = state.art.findArtType(yard);
            if (!item || !yardArt || yardArt->foundation != "4x4"
                || !runtimeCellCanPlaceBuilding(state, *item, origin, mcv, nullptr, scene.country))
                return fail("MCV original 4x4 deployment blocked");
            const char* opponent = std::string(scene.country) == "Russians" ? "Americans" : "Russians";
            if (!addRuntimeObjectInstanceForOwner(state, opponent, RuntimeBuildKind::Building, "NAPOWR", {1,1}))
                return fail("live opposing-house control");
            initializeRuntimeOfflineGameplayRouting(state);
            if (!drawRuntimeStateOffscreenAtCell(state, {27,30}, 960,640, error,
                    pathString(root / (label + "-opening.bmp")), false)) return fail("native opening capture: " + error);
            int shortOrders = 0, farOrders = 0;
            const auto record = [&]() {
                for (int id : troops) {
                    const auto* object = findObjectById(state, id);
                    if (!object || !object->alive || object->health != originalHealth.at(id)
                        || state.rules.getDetailRulesHash() != originalHash || state.config.noCooldown) return fail("troop HP/identity or stock rules changed");
                    const auto point = body(*object);
                    for (const auto& mine : state.world.getTerrainObjects()) {
                        if (!mine.alive || !mine.immune || mine.health != mine.strength) return fail("immune original mine changed");
                        if (insideMine(object->x, object->y, mine) || insideMine(point[0], point[1], mine))
                            return fail("native troop entered a mine footprint: " + describe(state,id));
                    }
                    const auto it = state.behaviors.find(id);
                    const RuntimeObjectBehavior* move = it == state.behaviors.end() ? nullptr : &it->second;
                    const RuntimeCellPoint first = move && !move->path.empty() ? move->path.front() : RuntimeCellPoint{-1,-1};
                    const RuntimeCellPoint last = move && !move->path.empty() ? move->path.back() : RuntimeCellPoint{-1,-1};
                    const auto* cell = findRuntimeTerrainCellAt(state, {object->x,object->y});
                    movement << label << ',' << stage << ',' << state.tick << ',' << scene.hz << ',' << id << ',' << object->type
                        << ',' << object->health << ',' << object->x << ',' << object->y << ',' << object->level << ',' << object->subCell
                        << ',' << point[0] << ',' << point[1] << ',' << point[2] << ',' << object->renderMoveActive
                        << ',' << (move ? int(move->kind) : -1) << ',' << (move ? move->moveTarget.x : -1)
                        << ',' << (move ? move->moveTarget.y : -1) << ',' << (move && move->pathRequestPending)
                        << ',' << (move ? move->blockedTicks : 0) << ',' << (move ? move->moveCooldownTicks : 0)
                        << ',' << (move ? move->path.size() : 0) << ',' << first.x << ',' << first.y << ',' << last.x << ',' << last.y
                        << ',' << (move ? move->pathReplans : 0) << ',' << (cell ? runtimeResourceBailCountForTerrain(*cell) : 0)
                        << ',' << state.actionSummary.localAvoidanceMovesThisTick << ',' << state.actionSummary.repathsThisTick << '\n';
                }
                return true;
            };
            const auto runOrder = [&](const std::vector<int>& selected, RuntimeCellPoint requested, bool shortRoute) {
                state.selectedObjectIds = selected;
                const bool dispatched = dispatchRuntimeTargetOrderFromUi(state, RuntimeMultiplayerGameplayTargetOrderKind::Primary, requested, nullptr);
                std::map<int, RuntimeCellPoint> assigned;
                std::map<int, int> arrivalRadii;
                for (int id : selected) {
                    const auto* object = findObjectById(state, id);
                    if (!object) return fail("selected starting troop disappeared");
                    const auto it = state.behaviors.find(id);
                    const RuntimeCellPoint goal = it == state.behaviors.end() ? runtimeCellPointForObject(*object) : it->second.moveTarget;
                    const int distance = std::abs(requested.x-object->x) + std::abs(requested.y-object->y);
                    orders << label << ',' << stage << ',' << state.tick << ',' << id << ',' << requested.x << ',' << requested.y
                        << ',' << dispatched << ',' << goal.x << ',' << goal.y << ',' << distance << '\n';
                    assigned[id] = goal;
                    // Retain the actual native group-arrival policy at dispatch.
                    // Fifty units legitimately use a larger shared formation
                    // radius than a single infantry group; it is not always 3.
                    arrivalRadii[id] = it == state.behaviors.end() ? 0 : runtimeSharedMoveArrivalRadius(it->second);
                    if ((shortRoute && distance > 10) || (!shortRoute && distance < 12)) return fail("route distance class is not exercised");
                }
                if (!record()) return false;
                if (!dispatched || state.lastCommand.acceptedCount != static_cast<int>(selected.size()))
                    return fail("real UI Move rejected: " + state.interactionStatus + " " + describe(state,selected.front()));
                shortRoute ? ++shortOrders : ++farOrders;
                for (int tick = 0; tick < scene.hz * 120; ++tick) {
                    step(state);
                    if (!record()) return false;
                    bool complete = true;
                    for (int id : selected) {
                        const auto* object = findObjectById(state,id);
                        if (!object) return fail("moving starting troop disappeared");
                        const bool atAssigned = runtimeCellPointForObject(*object) == assigned.at(id);
                        const bool nativeSharedArrival = selected.size() > 1 && arrivalRadii.at(id) > 0
                            && std::max(std::abs(object->x-requested.x), std::abs(object->y-requested.y)) <= arrivalRadii.at(id);
                        complete = complete && settled(state,id) && (atAssigned || nativeSharedArrival);
                        if (selected.size() == 1 && settled(state,id) && runtimeCellPointForObject(*object) != assigned.at(id))
                            return fail("single troop accepted another cell as arrival: " + describe(state,id));
                    }
                    if (complete) return true;
                }
                return fail("natural movement did not escape / settle: " + describe(state,selected.front()));
            };
            stage = "initial native body";
            if (!record()) return false;
            if (scene.cage) {
                const auto* first = findObjectById(state,vehicles.front());
                const auto occupancy = buildRuntimeOccupancyLookup(state,first->instanceId);
                const auto* centerOre = findRuntimeTerrainCellAt(state,{25,25});
                if (!centerOre || !centerOre->overlayIsOre) return fail("cage center lost its original ore");
                for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                    if (dx==0 && dy==0) continue;
                    if (runtimeCellTransitionIsPassable(state,state.terrainByCell,occupancy,
                            {25,25},{25+dx,25+dy},*first,-1,nullptr,false))
                        return fail("cage must reject cardinal and corner-cutting transitions");
                }
                // The first original vehicle, not a newly fabricated tank.
                stage = "cage real UI escape";
                if (!runOrder({vehicles.front()}, {36,28}, false)) return false;
            } else {
                const auto nearer = [&](int a, int b, RuntimeCellPoint target) {
                    const auto ca = originalCells.at(a), cb = originalCells.at(b);
                    return std::make_tuple(std::abs(ca.x-target.x)+std::abs(ca.y-target.y),a)
                        < std::make_tuple(std::abs(cb.x-target.x)+std::abs(cb.y-target.y),b);
                };
                std::sort(vehicles.begin(),vehicles.end(),[&](int a,int b){return nearer(a,b,{24,25});});
                std::sort(infantry.begin(),infantry.end(),[&](int a,int b){return nearer(a,b,{24,35});});
                const int tank = vehicles.front(), foot = infantry.front();
                stage = "single vehicle short mine detour";
                if (!runOrder({tank}, {19,25}, true)) return false;
                stage = "single infantry short mine detour";
                if (!runOrder({foot}, {18,35}, true)) return false;
                stage = "infantry real UI return to original slot";
                if (!runOrder({foot}, originalCells.at(foot), true)) return false;
                stage = "small infantry group short mine detour";
                if (!runOrder({infantry[0],infantry[1],infantry[2]}, {18,35}, true)) return false;
                for (int id : {infantry[0],infantry[1],infantry[2]}) {
                    const auto* object = findObjectById(state,id);
                    if (!object || object->x > 22 || body(*object)[0] > 22.5)
                        return fail("short infantry group did not pass the mine to the requested side");
                }
                stage = "all starting troops far ore escape";
                if (!runOrder(troops, {48,45}, false)) return false;
                for (int id : troops) {
                    const auto* object = findObjectById(state,id);
                    const auto* cell = findRuntimeTerrainCellAt(state,{object->x,object->y});
                    if (!cell || cell->overlayIsOre || cell->overlayIsGem || !settled(state,id))
                        return fail("far formation still occupies ore: " + describe(state,id));
                }
            }
            if (!findObjectById(state,mcv) || runtimeCellPointForObject(*findObjectById(state,mcv)) != origin)
                return fail("manual troop escape moved the MCV");
            if (!drawRuntimeStateOffscreenAtCell(state, scene.cage ? RuntimeCellPoint{30,27} : RuntimeCellPoint{44,42},
                    960,640,error,pathString(root / (label + "-escaped.bmp")),false)) return fail("native escaped capture: " + error);
            cases << label << ',' << scene.country << ',' << scene.hz << ',' << scene.count << ',' << troops.size()
                << ',' << state.world.getTerrainObjects().size() << ',' << oreCells << ',' << shortOrders << ',' << farOrders
                << ',' << state.tick << ",PASS\n";
            ++passed; totalShort += shortOrders; totalFar += farOrders;
            std::cout << "Starting ore escape " << label << " PASS troops=" << troops.size()
                << " shortOrders=" << shortOrders << " farOrders=" << farOrders << " ticks=" << state.tick << '\n';
        }
    } catch (const std::exception& exception) { return fail(exception.what()); }
    if (!passed || (filter.empty() && passed != 15)) return fail("complete 12 ore scenes + 3 deterministic cage scenes");
    std::cout << "Starting ore escape summary cases=" << passed << " shortOrders=" << totalShort
        << " farOrders=" << totalFar << " originalRules=yes trueUI=yes PASS\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
