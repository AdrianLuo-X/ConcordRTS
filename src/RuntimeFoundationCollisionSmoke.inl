// Independent swept-point oracle: the actual rendered infantry center may
// lie between cells after Stop, even though its authority has committed ahead.
bool runtimeFoundationTraceEntersRectangle(
    double fromX, double fromY, double toX, double toY,
    double left, double top, double right, double bottom
) {
    constexpr double epsilon = 1.0e-9;
    left += epsilon; top += epsilon; right -= epsilon; bottom -= epsilon;
    double first = 0.0, last = 1.0;
    const auto clip = [&](double from, double to, double low, double high) {
        const double delta = to - from;
        if (std::abs(delta) <= epsilon) return from >= low && from <= high;
        double enter = (low - from) / delta, leave = (high - from) / delta;
        if (enter > leave) std::swap(enter, leave);
        first = std::max(first, enter); last = std::min(last, leave);
        return first <= last;
    };
    return clip(fromX, toX, left, right) && clip(fromY, toY, top, bottom);
}

bool runRuntimeStoppedInfantryFoundationCornerSmoke(std::string& error) {
    std::cout << std::unitbuf;
    int cases = 0, sweptSegments = 0, saves = 0, pairedTicks = 0, redirects = 0;
    struct Actor { const char* type; int subCell; };
    const std::array<Actor, 5> actors{{{"CORNERFOOT",2}, {"CORNERFOOT",4},
        {"CORNERFOOT",3}, {"CORNERWHEEL",-1}, {"CORNERFAST",4}}};
    for (int hz : {15, 30, 60}) for (const auto& spec : actors) {
        const bool infantry = spec.subCell >= 0;
        const bool northBend=infantry && spec.subCell==3;
        const RuntimeCellPoint bendTarget=northBend?RuntimeCellPoint{9,8}:RuntimeCellPoint{10,9};
        const std::string label = std::to_string(hz) + "Hz " + spec.type
            + " subcell=" + std::to_string(spec.subCell);
        const auto fail = [&](const std::string& message) {
            error = "stopped foundation corner " + label + ": " + message; return false;
        };
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        IniFile rules, art;
        if (!rules.loadFromString(
                "[VehicleTypes]\n0=CORNERWHEEL\n[InfantryTypes]\n0=CORNERFOOT\n1=CORNERFAST\n"
                "[BuildingTypes]\n0=CORNERHOUSE\n"
                "[CORNERFOOT]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
                "[CORNERFAST]\nStrength=100\nSpeed=10\nSpeedType=Foot\nMovementZone=Normal\n"
                "[CORNERWHEEL]\nStrength=200\nSpeed=8\nROT=10\nSpeedType=Wheel\nMovementZone=Normal\n"
                "[CORNERHOUSE]\nStrength=1000\nImage=CORNERHOUSEART\n")
            || !art.loadFromString("[CORNERHOUSEART]\nFoundation=4x3\n")
            || !state.rules.loadFromIni(rules) || !state.art.loadFromIni(art)) return fail("fixture rules/art");
        state.rulesLoaded = state.artLoaded = true;
        state.config.fixedTickHz = hz; state.config.computerAiEnabled = false;
        state.config.defaultMapPath = "foundation-corner-regression.map";
        state.config.crates = state.config.superweapons = false;
        state.economy.owner = state.config.playerCountry = "Player";
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance building;
        building.instanceId = 1; building.kind = WorldObjectKind::Structure;
        building.type = "CORNERHOUSE"; building.owner = "Player";
        building.x = building.y = 10; building.cell = runtimeCellKey(10, 10);
        building.alive = true; building.health = 1000; building.mission = "Guard";
        WorldObjectInstance mover;
        mover.instanceId = 2; mover.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        mover.type = spec.type; mover.owner = "Player"; mover.alive = true; mover.health = 100;
        mover.mission = "Guard"; mover.x = 8; mover.y = 10; mover.cell = runtimeCellKey(8, 10);
        mover.subCell = spec.subCell; mover.facing = runtimeFacingFromDelta(1, -1);
        state.world.accessObjects() = {building, mover};
        // Real residents occupy the earlier legal slots at both cells. This
        // exercises the actual reservation order, rather than rewriting it.
        int residentId = 3;
        if (infantry) for (const RuntimeCellPoint cell : {RuntimeCellPoint{9,9}, bendTarget})
            for (int slot : {2, 4}) {
                if (slot == spec.subCell || (spec.subCell == 2)) break;
                auto resident = mover; resident.instanceId = residentId++; resident.type = "CORNERFOOT";
                resident.x = cell.x; resident.y = cell.y; resident.cell = runtimeCellKey(cell.x, cell.y);
                resident.subCell = slot; state.world.accessObjects().push_back(resident);
            }
        // The synthetic flat map has no bridge override; its display houses
        // are explicit, so navigation need not probe its save identity as a file.
        state.destroyableBridges = state.rules.getDestroyableBridges();
        state.bridgeSpecialFlagsLoaded = true;
        state.ownerRemapColors["player"] = RGB(0,255,0);
        state.ownerRemapColors["neutral"] = RGB(128,128,128);
        state.neutralRemapOwners.insert("neutral");
        initializeRuntimeNavigation(state);
        for (auto& [key, info] : state.terrainInfoByCell) { (void)key; info = RuntimeTerrainInfo{}; }
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.selectedObjectIds = {2};
        const auto center = [](const WorldObjectInstance& object) {
            const auto offset = worldObjectSubCellOffset(object);
            return std::array<double, 2>{runtimeObjectRenderX(object) + offset.x,
                runtimeObjectRenderY(object) + offset.y};
        };
        std::map<int,std::array<double,2>> residentBodies;
        for(const auto& object:state.world.getObjects()) if(object.instanceId>=3)
            residentBodies.emplace(object.instanceId,center(object));
        const auto centersMatch = [](const std::array<double,2>& a, const std::array<double,2>& b) {
            return std::abs(a[0]-b[0]) < 1.0e-6 && std::abs(a[1]-b[1]) < 1.0e-6;
        };
        const auto trace = [&](const RuntimeWindowState& scene, const std::array<double,2>& before,
                const std::array<double,2>& after, const char* stage) {
            const auto* current = findObjectById(scene, 2);
            for(const auto& [id,original]:residentBodies) {
                const auto* resident=findObjectById(scene,id);
                if(!resident || !resident->alive || resident->renderMoveActive || !centersMatch(center(*resident),original)) {
                    std::ostringstream detail;detail<<std::setprecision(17)<<stage<<" changed a real subcell resident id="<<id
                        <<" tick="<<scene.tick<<" expectedBody="<<original[0]<<','<<original[1];
                    if(resident) {
                        const auto actual=center(*resident);
                        detail<<" actualBody="<<actual[0]<<','<<actual[1]<<" logical="<<resident->x<<','<<resident->y
                            <<" cell="<<resident->cell<<" slot="<<resident->subCell<<" alive="<<resident->alive
                            <<" health="<<resident->health<<" pose="<<resident->hasRenderPosition<<'/'<<resident->renderMoveActive
                            <<" base="<<runtimeObjectRenderX(*resident)<<','<<runtimeObjectRenderY(*resident)
                            <<" mission="<<resident->mission;
                        const auto motion=scene.behaviors.find(id);
                        if(motion==scene.behaviors.end()) detail<<" behavior=none";
                        else {const auto& move=motion->second;
                            detail<<" behavior="<<static_cast<int>(move.kind)<<'/'<<static_cast<int>(move.state)
                                <<" auto="<<move.autoAcquired<<" goal="<<move.moveTarget.x<<','<<move.moveTarget.y
                                <<" from="<<move.renderFrom.x<<','<<move.renderFrom.y<<" to="<<move.renderTo.x<<','<<move.renderTo.y
                                <<" cooldown="<<move.moveCooldownTicks<<" remaining="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal
                                <<" path="<<move.path.size()<<" pending="<<move.pathRequestPending;
                            for(const auto cell:move.path) detail<<' '<<cell.x<<','<<cell.y;
                        }
                    } else detail<<" resident=missing";
                    detail<<" moverBody="<<after[0]<<','<<after[1]<<" moverBefore="<<before[0]<<','<<before[1]
                        <<" moverLogical="<<current->x<<','<<current->y<<" moverSlot="<<current->subCell
                        <<" moverPose="<<current->hasRenderPosition<<'/'<<current->renderMoveActive;
                    const auto context=runtimeGroundPoseContext(scene);
                    if(const auto held=runtimeAdditionalGroundPoseOccupancy(*current,&context))
                        detail<<" moverAdditional="<<held->x<<','<<held->y<<'/'<<held->subCell;
                    const auto motion=scene.behaviors.find(2);
                    if(motion!=scene.behaviors.end()) {const auto& move=motion->second;
                        detail<<" moverSegment="<<move.renderFrom.x<<','<<move.renderFrom.y
                            <<" offset="<<move.renderFromOffsetX<<','<<move.renderFromOffsetY
                            <<" -> "<<move.renderTo.x<<','<<move.renderTo.y
                            <<" remaining="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal;
                    }
                    for(const auto& event:scene.recentReplayEvents) if(event.objectId==id && event.tick>=scene.tick-2)
                        detail<<" event="<<event.tick<<'/'<<event.kind<<'/'<<event.reason;
                    return fail(detail.str());
                }
                const double dx=after[0]-before[0],dy=after[1]-before[1];
                const double length=dx*dx+dy*dy;
                const double part=length>1e-18?std::clamp(((original[0]-before[0])*dx+(original[1]-before[1])*dy)/length,0.0,1.0):0.0;
                const double rx=before[0]+dx*part-original[0],ry=before[1]+dy*part-original[1];
                if(rx*rx+ry*ry+1e-9<0.25) {
                    std::ostringstream detail;detail<<std::setprecision(17)<<stage<<" tick="<<scene.tick
                        <<" swept body overlapped real resident="<<id<<" distance="<<std::hypot(rx,ry)
                        <<" body="<<before[0]<<','<<before[1]<<" -> "<<after[0]<<','<<after[1]
                        <<" resident="<<original[0]<<','<<original[1];
                    return fail(detail.str());
                }
            }
            // This is an ordinary move. Authorized docking, garrison entry,
            // factory egress and open gates have their separate service tests.
            if (!runtimeFoundationTraceEntersRectangle(before[0], before[1], after[0], after[1],
                    9.5, 9.5, 13.5, 12.5)) return true;
            std::ostringstream detail;
            detail << std::setprecision(17) << stage << " tick=" << scene.tick
                << " actual body segment=" << before[0] << ',' << before[1]
                << " -> " << after[0] << ',' << after[1]
                << " authority=" << current->x << ',' << current->y << " subcell=" << current->subCell;
            return fail(detail.str());
        };
        auto previous = center(*findObjectById(state, 2));
        issueRuntimeMoveCommand(state, {9, 9});
        if (state.lastCommand.acceptedCount != 1) return fail("first actual Move rejected");
        bool committed = false;
        for (int tick = 0; tick < hz*4; ++tick) {
            ++state.tick; advanceRuntimeSimulation(state);
            const auto* current = findObjectById(state, 2); const auto actual = center(*current);
            if (!trace(state, previous, actual, "first Move")) return false;
            previous = actual;
            if (current->x == 9 && current->y == 9 && current->renderMoveActive) { committed = true; break; }
        }
        const auto* current = findObjectById(state, 2);
        if (!committed || (infantry && current->subCell != spec.subCell))
            return fail("real diagonal/reserved subcell segment did not start");
        issueRuntimeStopCommand(state);
        current = findObjectById(state, 2);
        if (state.lastCommand.acceptedCount != 1 || state.behaviors.contains(2)
            || !worldObjectHasStoppedGroundPose(*current)) return fail("Stop did not preserve the unfinished pose");
        if (!centersMatch(center(*current), previous)) return fail("Stop changed the visible body");
        if(northBend) {
            // Independent geometric proof: the old eastern slot-3 leg ran
            // through source slot 4. This north bend keeps half-cell clearance
            // from all real source/destination residents on both saved legs.
            const auto slot=worldInfantrySubCellOffset(spec.subCell);
            const std::array<double,2> pivot{9.0+slot.x,9.0+slot.y};
            const std::array<double,2> end{bendTarget.x+slot.x,bendTarget.y+slot.y};
            const auto squaredDistance=[](const std::array<double,2>& point,
                    const std::array<double,2>& from,const std::array<double,2>& to) {
                const double dx=to[0]-from[0],dy=to[1]-from[1],length=dx*dx+dy*dy;
                const double part=length>1e-18?std::clamp(((point[0]-from[0])*dx+(point[1]-from[1])*dy)/length,0.0,1.0):0.0;
                const double px=from[0]+dx*part-point[0],py=from[1]+dy*part-point[1];
                return px*px+py*py;
            };
            for(const auto& [id,body]:residentBodies) {
                if(squaredDistance(body,previous,pivot)+1e-9<0.25 || squaredDistance(body,pivot,end)+1e-9<0.25)
                    return fail("north slot-3 bend mathematical clearance proof failed resident="+std::to_string(id));
            }
        }
        RuntimeCellPoint finalGoal=northBend?RuntimeCellPoint{9,3}:RuntimeCellPoint{16,9};
        issueRuntimeMoveCommand(state, finalGoal);
        if (state.lastCommand.acceptedCount != 1) return fail("second actual Move rejected");
        current = findObjectById(state, 2);
        auto actual = center(*current);
        if (!centersMatch(actual, previous) || !trace(state, previous, actual, "Move command"))
            return fail("new Move changed the visible body before stepping");
        previous = actual;
        bool complete = false, checkpointed = false, redirected = false;
        int movingSegments = 0, beforeRedirectPairs = 0, afterRedirectPairs = 0;
        std::unique_ptr<RuntimeWindowState> restored;
        std::array<double,2> restoredPrevious{};
        RuntimeObjectBehavior checkpointSegment;
        const auto sameSegment = [&](const RuntimeObjectBehavior& segment) {
            return segment.renderFrom == checkpointSegment.renderFrom && segment.renderTo == checkpointSegment.renderTo
                && segment.renderFromOffsetX == checkpointSegment.renderFromOffsetX
                && segment.renderFromOffsetY == checkpointSegment.renderFromOffsetY
                && segment.renderToOffsetX == checkpointSegment.renderToOffsetX
                && segment.renderToOffsetY == checkpointSegment.renderToOffsetY;
        };
        for (int tick = 0; tick < hz*30; ++tick) {
            ++state.tick; advanceRuntimeSimulation(state);
            current = findObjectById(state, 2); actual = center(*current);
            if (!trace(state, previous, actual, "Stop -> Move")) return false;
            if (!centersMatch(actual, previous)) { ++movingSegments; ++sweptSegments; }
            previous = actual;
            if (restored) {
                { RuntimeSnapshotActiveStateScope restoredScope(*restored);
                    ++restored->tick; advanceRuntimeSimulation(*restored); }
                const auto* clone = findObjectById(*restored, 2);
                const auto cloneActual = center(*clone);
                if (!trace(*restored, restoredPrevious, cloneActual, "restored active bend")) return false;
                restoredPrevious = cloneActual;
                const auto it = restored->behaviors.find(2);
                if (clone->renderMoveActive && it != restored->behaviors.end() && sameSegment(it->second)) {
                    if (!centersMatch(actual, cloneActual)) return fail(redirected
                        ? "new Move replaced the inherited active bend with a different path"
                        : "loaded active bend diverged on continuation");
                    ++pairedTicks;
                    if (redirected) ++afterRedirectPairs; else ++beforeRedirectPairs;
                } else {
                    if (!redirected || afterRedirectPairs < 2) return fail("active bend ended before paired inheritance checks");
                    restored.reset();
                }
            }
            const auto behavior = state.behaviors.find(2);
            if (infantry && !checkpointed && movingSegments >= 2 && current->renderMoveActive
                && behavior != state.behaviors.end() && behavior->second.renderFrom == RuntimeCellPoint{9,9}
                && behavior->second.renderTo == bendTarget
                && std::abs(behavior->second.renderFromOffsetX) > 0.1
                && std::abs(behavior->second.renderFromOffsetY) > 0.1) {
                if (current->subCell != spec.subCell) return fail("second segment changed the required test subcell");
                checkpointSegment = behavior->second;
                std::ostringstream saved; IniFile ini; restored = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*restored, state, false);
                if (!writeRuntimeStateToStream(state, saved, error) || !ini.loadFromString(saved.str())
                    || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("active bend save/load: " + error);
                const auto* clone = findObjectById(*restored,2);
                if (!clone || clone->subCell != spec.subCell || !centersMatch(actual, center(*clone)))
                    return fail("loaded active bend changed its subcell or visible position");
                restoredPrevious = center(*clone); checkpointed = true; ++saves;
            }
            if (restored && !redirected && beforeRedirectPairs >= 3) {
                const auto before = center(*findObjectById(state,2));
                finalGoal = {16,8}; state.selectedObjectIds = {2}; issueRuntimeMoveCommand(state, finalGoal);
                const auto after = center(*findObjectById(state,2));
                if (state.lastCommand.acceptedCount != 1 || !centersMatch(before,after))
                    return fail("redirecting Move changed the current visible position");
                if (!trace(state, before, after, "active new Move command")) return false;
                previous = after; redirected = true; ++redirects;
            }
            if (!state.behaviors.contains(2)) { complete = true; break; }
        }
        current = findObjectById(state,2);
        if (!complete || runtimeCellPointForObject(*current) != finalGoal || current->renderMoveActive
            || std::abs(runtimeObjectRenderX(*current)-finalGoal.x)>1e-9
            || std::abs(runtimeObjectRenderY(*current)-finalGoal.y)>1e-9) {
            std::ostringstream detail;detail<<std::setprecision(17)<<"new Move never reached its requested destination"
                <<" tick="<<state.tick<<" goal="<<finalGoal.x<<','<<finalGoal.y
                <<" logical="<<current->x<<','<<current->y<<" body="<<center(*current)[0]<<','<<center(*current)[1]
                <<" slot="<<current->subCell<<" pose="<<current->hasRenderPosition<<'/'<<current->renderMoveActive
                <<" complete="<<complete<<" checkpointed="<<checkpointed<<" redirected="<<redirected
                <<" paired="<<beforeRedirectPairs<<'/'<<afterRedirectPairs<<" status="<<state.simulationStatus;
            const auto found=state.behaviors.find(2);
            if(found!=state.behaviors.end()) {
                const auto& move=found->second;
                detail<<" state="<<static_cast<int>(move.state)<<" blocked="<<move.blockedTicks
                    <<" cooldown="<<move.moveCooldownTicks<<" remaining="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal
                    <<" from="<<move.renderFrom.x<<','<<move.renderFrom.y
                    <<" offset="<<move.renderFromOffsetX<<','<<move.renderFromOffsetY
                    <<" to="<<move.renderTo.x<<','<<move.renderTo.y<<" path="<<move.path.size()<<" pending="<<move.pathRequestPending;
                if(!move.path.empty()) {
                    const auto next=move.path.front();std::vector<int> blockers;
                    const int slot=runtimeInfantryBodyPreviewDestinationSubcell(state,*current,next);
                    const bool blocked=runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(state,*current,
                        runtimeCellPointForObject(*current),next,slot,&blockers);
                    detail<<" next="<<next.x<<','<<next.y<<" nextSlot="<<slot<<" nextBodyBlocked="<<blocked<<" blockers=";
                    for(int id:blockers) detail<<id<<',';
                }
                const auto search=findRuntimeCloserSharedMoveSlot(state,*current,move);
                detail<<" bodyRoute="<<search.applicable<<'/'<<search.inwardRoute<<'/'<<search.exhausted
                    <<" bodyPath="<<search.path.size();
                for(const auto cell:search.path) detail<<' '<<cell.x<<','<<cell.y;
            }
            return fail(detail.str());
        }
        if (infantry && (!checkpointed || !redirected || beforeRedirectPairs < 3 || afterRedirectPairs < 2))
            return fail("save/load and active Move inheritance did not exercise the bent segment");
        if (findObjectById(state,1)->health != 1000) return fail("ordinary movement damaged the building");
        std::cout << "Stopped foundation corner " << label << ": " << movingSegments
            << " swept body segments, " << beforeRedirectPairs+afterRedirectPairs << " paired active ticks passed\n";
        ++cases;
    }
    std::cout << "Stopped foundation corners: " << cases << " real Move/Stop/Move cases, "
        << sweptSegments << " swept body segments, " << saves << " active-bend save/loads, "
        << pairedTicks << " paired continuation ticks and " << redirects << " inherited new Moves passed\n";
    error.clear(); return true;
}

bool runRuntimeFoundationCollisionSmoke(std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "foundation collision: " + message; return false;
    };
    int checks = 0;
    for (const bool infantry : {false, true}) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        IniFile rules, art;
        if (!rules.loadFromString(
            "[VehicleTypes]\n0=CAR\n[InfantryTypes]\n0=MAN\n[BuildingTypes]\n0=HOUSE\n"
            "[CAR]\nStrength=200\nSpeed=8\nROT=10\nSpeedType=Wheel\nMovementZone=Normal\n"
            "[MAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
            "[HOUSE]\nStrength=1000\nImage=HOUSEART\n")
            || !art.loadFromString("[HOUSEART]\nFoundation=4x3\n")
            || !state.rules.loadFromIni(rules) || !state.art.loadFromIni(art))
            return fail("fixture rules/art");
        state.rulesLoaded = state.artLoaded = true;
        state.config.fixedTickHz = 30; state.config.computerAiEnabled = false;
        state.config.crates = state.config.superweapons = false;
        state.economy.owner = state.config.playerCountry = "Player";
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance mover;
        mover.instanceId = 2; mover.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        mover.type = infantry ? "MAN" : "CAR"; mover.owner = "Player";
        mover.x = 12; mover.y = 9; mover.cell = runtimeCellKey(mover.x, mover.y);
        mover.alive = true; mover.health = 100; mover.subCell = infantry ? 2 : -1;
        mover.mission = "Guard";
        state.world.accessObjects().push_back(mover);
        WorldObjectInstance building;
        building.instanceId = 1; building.kind = WorldObjectKind::Structure;
        building.type = "HOUSE"; building.owner = "Player";
        building.x = building.y = 10; building.cell = runtimeCellKey(10, 10);
        building.alive = true; building.health = 1000; building.mission = "Guard";
        state.world.accessObjects().push_back(building);
        initializeRuntimeNavigation(state);
        for (auto& [key, info] : state.terrainInfoByCell) info = RuntimeTerrainInfo{};
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        const auto* actor = findObjectById(state, 2);
        const std::unordered_map<int, int> missingOccupancy;
        // A cached or group-filtered cell map cannot override a live foundation.
        for (int y = 10; y < 13; ++y) for (int x = 10; x < 14; ++x) {
            if (runtimeCellIsPassable(state, state.terrainByCell, missingOccupancy, {x, y}, *actor))
                return fail(std::string(infantry ? "infantry" : "vehicle")
                    + " accepts an occupied foundation with an incomplete cell map at "
                    + std::to_string(x) + "," + std::to_string(y));
            ++checks;
        }
        // A soft moving representative inside a host must not hide that host.
        WorldObjectInstance worker = mover;
        worker.instanceId = 3; worker.kind = WorldObjectKind::Infantry; worker.type = "MAN";
        worker.x = 12; worker.y = 10; worker.cell = runtimeCellKey(12, 10); worker.subCell = 2;
        state.world.accessObjects().insert(state.world.accessObjects().begin(), worker);
        state.behaviors[3].kind = RuntimeBehaviorKind::Move;
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        actor = findObjectById(state, 2);
        const auto occupancy = buildRuntimeOccupancyLookup(state);
        const std::unordered_set<int> soft{3};
        if (runtimeCellIsPassable(state, state.terrainByCell, occupancy, {12, 10}, *actor, -1, &soft))
            return fail("a soft occupant hides the building");
        ++checks;
        if (runtimeCellTransitionIsPassable(state, state.terrainByCell, missingOccupancy,
                {9, 10}, {10, 9}, *actor))
            return fail("a diagonal cuts the foundation corner");
        ++checks;
        // Group filters only remove mobile traffic, never a solid building.
        const std::unordered_set<int> ignored{1, 3};
        const auto path = findRuntimePath(state, *actor, {12, 9}, {12, 16}, -1, &ignored);
        if (path.empty() || path.back() != RuntimeCellPoint{12, 16}) return fail("detour is missing");
        for (const auto& cell : path) {
            if (cell.x >= 10 && cell.x < 14 && cell.y >= 10 && cell.y < 13)
                return fail("a filtered path crosses the foundation");
        }
        ++checks;
        findMutableObjectById(state, 3)->alive = false;
        state.behaviors.erase(3);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        // Reproduce the actual commit gate with a route/occupancy view from
        // before a building appeared. Local avoidance must remain outside too.
        RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move;
        move.state = RuntimeBehaviorState::Moving; move.moveTarget = {12, 16};
        move.path.push_back({12, 10});
        state.behaviors[2] = move;
        (void)runtimeTickOccupancyLookup(state);
        for (int y = 10; y < 13; ++y) for (int x = 10; x < 14; ++x) {
            state.tickOccupiedByCell.erase(runtimeCellKey(x, y));
            state.tickGroundOccupantCountByCell.erase(runtimeCellKey(x, y));
        }
        if (infantry) (void)reserveRuntimeInfantryMoveDestination(state, *actor, {12, 10});
        (void)advanceRuntimeMoveBehavior(state, *findMutableObjectById(state, 2), state.behaviors.at(2));
        actor = findObjectById(state, 2);
        if (actor->x >= 10 && actor->x < 14 && actor->y >= 10 && actor->y < 13)
            return fail("actual movement entered the building through stale occupancy");
        ++checks;
        // Destroying a building must release its former cells immediately.
        findMutableObjectById(state, 1)->alive = false;
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        if (!runtimeCellIsPassable(state, state.terrainByCell, missingOccupancy, {12, 11}, *actor))
            return fail("destroyed foundation still blocks movement");
        ++checks;
        (void)runtimeTickOccupancyLookup(state);
        (void)buildRuntimePathTerrainSnapshot(state, *actor);
        if (!addRuntimeObjectInstanceForOwner(state, "Player", RuntimeBuildKind::Building,
                "HOUSE", {15, 15})) return fail("building creation");
        actor = findObjectById(state, 2);
        if (state.tickOccupancyCacheTick == state.tick || state.pathTerrainCache.dynamicValid)
            return fail("new building retained an earlier movement cache");
        const auto updated = buildRuntimePathTerrainSnapshot(state, *actor);
        for (const auto& cell : updated) {
            if (cell.cell.x >= 15 && cell.cell.x < 19 && cell.cell.y >= 15 && cell.cell.y < 18
                && cell.passable) return fail("new building is absent from the same-tick path map");
        }
        ++checks;
    }
    // Water and shoreline are genuinely traversable in these fixtures: first
    // prove the exact edge with the building inactive, then add the live
    // foundation without changing object-vector identity or size. Incomplete
    // traffic maps must not turn either naval or amphibious movement into a
    // shortcut through the structure or its two diagonal side constraints.
    struct AquaticFoundationCase { const char* type; bool naval; bool shore; };
    const std::array<AquaticFoundationCase, 3> aquaticCases{{
        {"AMPH", false, false}, {"AMPH", false, true}, {"SHIP", true, false}
    }};
    struct AquaticBlockedEdge { RuntimeCellPoint from, to; };
    const std::array<AquaticBlockedEdge, 3> blockedEdges{{
        {{12, 9}, {12, 10}}, // Destination lies inside the foundation.
        {{9, 10}, {10, 9}}, // Horizontal side lies inside; destination is clear.
        {{10, 9}, {9, 10}}  // Vertical side lies inside; destination is clear.
    }};
    for (const auto& sample : aquaticCases) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        const std::string label = std::string(sample.type) + (sample.shore ? " shore" : " water");
        IniFile rules, art;
        if (!rules.loadFromString(
            "[VehicleTypes]\n0=AMPH\n1=SHIP\n[BuildingTypes]\n0=PIER\n"
            "[AMPH]\nStrength=200\nSpeed=8\nROT=10\nSpeedType=Amphibious\nMovementZone=Amphibious\n"
            "[SHIP]\nStrength=200\nSpeed=8\nROT=10\nNaval=yes\nWaterBound=yes\nSpeedType=Float\nMovementZone=Water\n"
            "[PIER]\nStrength=1000\nImage=PIERART\nWaterBound=yes\n"
            "[Water]\nAmphibious=100%\nFloat=100%\n[Beach]\nAmphibious=100%\n")
            || !art.loadFromString("[PIERART]\nFoundation=4x3\n")
            || !state.rules.loadFromIni(rules) || !state.art.loadFromIni(art))
            return fail(label + " fixture rules/art");
        state.rulesLoaded = state.artLoaded = true;
        state.config.fixedTickHz = 30; state.config.computerAiEnabled = false;
        state.config.crates = state.config.superweapons = false;
        state.economy.owner = state.config.playerCountry = "Player";
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance mover;
        mover.instanceId = 2; mover.kind = WorldObjectKind::Unit;
        mover.type = sample.type; mover.owner = "Player";
        mover.x = 12; mover.y = 9; mover.cell = runtimeCellKey(12, 9);
        mover.alive = true; mover.health = 200; mover.subCell = -1; mover.mission = "Guard";
        state.world.accessObjects().push_back(mover);
        WorldObjectInstance building;
        building.instanceId = 1; building.kind = WorldObjectKind::Structure;
        building.type = "PIER"; building.owner = "Player";
        building.x = building.y = 10; building.cell = runtimeCellKey(10, 10);
        building.alive = false; building.health = 1000; building.mission = "Guard";
        state.world.accessObjects().push_back(building);
        initializeRuntimeNavigation(state);
        for (auto& [key, info] : state.terrainInfoByCell) {
            (void)key; info = RuntimeTerrainInfo{};
            info.kind = sample.shore ? RuntimeTerrainKind::Shore : RuntimeTerrainKind::Water;
            info.water = !sample.shore; info.shore = sample.shore;
            info.authoredLandType = info.movementLandType = sample.shore
                ? RuntimeTerrainLandType::Beach : RuntimeTerrainLandType::Water;
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        const auto* actor = findObjectById(state, 2);
        const auto expectedMode = sample.naval ? PathMovementMode::Naval : PathMovementMode::Amphibious;
        if (runtimeObjectMovementMode(state, *actor) != expectedMode)
            return fail(label + " movement classification");
        const std::unordered_map<int, int> missingOccupancy;
        for (const auto& edge : blockedEdges) {
            if (!runtimeCellTransitionIsPassable(state, state.terrainByCell,
                    missingOccupancy, edge.from, edge.to, *actor))
                return fail(label + " unobstructed water/shore edge is not traversable");
            ++checks;
        }
        auto* pier = findMutableRuntimeObjectByIdIncludingInactive(state, 1);
        if (pier == nullptr) return fail(label + " inactive foundation fixture missing");
        activateRuntimeWorldObject(state, *pier);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        for (const auto& edge : blockedEdges) {
            if (runtimeCellTransitionIsPassable(state, state.terrainByCell,
                    missingOccupancy, edge.from, edge.to, *actor))
                return fail(label + " ignores the live foundation at a destination or diagonal side");
            ++checks;
        }
        for (const auto& edge : blockedEdges) {
            auto* moving = findMutableObjectById(state, 2);
            setRuntimeObjectCell(state, *moving, edge.from);
            moving->mission = "Guard";
            moving->facing = runtimeFacingFromDelta(edge.to.x - edge.from.x, edge.to.y - edge.from.y);
            RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Moving; move.moveTarget = {12, 16};
            move.path.push_back(edge.to);
            state.behaviors[2] = std::move(move);
            refreshRuntimeObjectOccupancyAfterLocalChange(state);
            (void)runtimeTickOccupancyLookup(state);
            for (int y = 10; y < 13; ++y) for (int x = 10; x < 14; ++x) {
                state.tickOccupiedByCell.erase(runtimeCellKey(x, y));
                state.tickGroundOccupantCountByCell.erase(runtimeCellKey(x, y));
            }
            (void)advanceRuntimeMoveBehavior(state, *moving, state.behaviors.at(2));
            actor = findObjectById(state, 2);
            if (runtimeCellPointForObject(*actor) == edge.to
                || (actor->x >= 10 && actor->x < 14 && actor->y >= 10 && actor->y < 13))
                return fail(label + " actual movement committed a forbidden destination/diagonal");
            if (findObjectById(state, 1)->health != 1000)
                return fail(label + " ordinary movement damaged the foundation");
            ++checks;
        }
    }
    std::cout << "Foundation collision: " << checks
        << " incomplete/soft/filtered occupancy, land/water/shore corners, actual commits and destruction checks passed\n";
    return true;
}

bool runRuntimeNativeFoundationRoutes(const RuntimeConfig& config, std::string& error) {
    if (std::getenv("CONCORD_BUILDING_CORNER_ONLY")) return true;
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) { error = "native foundation resources failed to load"; return false; }
    struct Building { const char* type; int width, height; };
    const std::array<Building, 6> buildings{{{"GAPOWR",2,2}, {"GAPILE",3,2},
        {"GAREFN",4,3}, {"GAWEAP",5,3}, {"GACNST",4,4}, {"YABRCK",2,3}}};
    const bool isolated=std::getenv("CONCORD_NATIVE_FOUNDATION_ONLY")!=nullptr;
    const bool isolatedInfantry=std::getenv("CONCORD_NATIVE_FOUNDATION_INFANTRY_ONLY")!=nullptr;
    int routes = 0, poseChecks = 0, crowdedUnits = 0;
    for (const auto& spec : buildings) for (const bool infantry : {false, true})
        for (int direction = 0; direction < 8; ++direction) {
        if(isolated && (std::string(spec.type)!="GAWEAP" || infantry!=isolatedInfantry || direction!=0)) continue;
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        const std::string label = std::string(spec.type) + (infantry ? " E1 " : " MTNK ")
            + std::to_string(direction);
        const auto fail = [&](const std::string& why) { error = "native foundation " + label + ": " + why; return false; };
        state.rules = stock->rules; state.art = stock->art;
        state.rulesLoaded = state.artLoaded = true;
        state.config.fixedTickHz = 30; state.config.computerAiEnabled = false;
        state.config.crates = state.config.superweapons = false;
        state.economy.owner = state.config.playerCountry = "Americans";
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance building;
        building.instanceId = 1; building.kind = WorldObjectKind::Structure;
        building.type = spec.type; building.owner = "Americans";
        building.x = building.y = 16; building.cell = runtimeCellKey(16,16);
        building.alive = true; building.health = 1000; building.mission = "Guard";
        state.world.accessObjects().push_back(building);
        const int left = 12, top = 12, right = 20 + spec.width, bottom = 20 + spec.height;
        const int centerX = 16 + spec.width / 2, centerY = 16 + spec.height / 2;
        const std::array<RuntimeCellPoint,8> ring{{{centerX,top},{right,top},{right,centerY},
            {right,bottom},{centerX,bottom},{left,bottom},{left,centerY},{left,top}}};
        const auto start = ring[direction], goal = ring[(direction + 4) % 8];
        const bool crowded = std::string(spec.type) == "GAWEAP" && direction == 0;
        const int count = crowded ? 18 : 1;
        for (int i = 0; i < count; ++i) {
            WorldObjectInstance mover;
            mover.instanceId = 2 + i; mover.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            mover.type = infantry ? "E1" : "MTNK"; mover.owner = "Americans";
            mover.x = start.x + (crowded ? i % 6 - 3 : 0); mover.y = start.y - i / 6;
            mover.cell = runtimeCellKey(mover.x,mover.y); mover.alive = true;
            mover.health = 100; mover.subCell = infantry ? 2 : -1; mover.mission = "Guard";
            mover.facing = runtimeFacingFromDelta(goal.x - mover.x, goal.y - mover.y);
            state.world.accessObjects().push_back(mover);
            state.selectedObjectIds.push_back(mover.instanceId);
        }
        initializeRuntimeNavigation(state);
        for (auto& [key, info] : state.terrainInfoByCell) info = RuntimeTerrainInfo{};
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        const auto footprint = runtimeStructureFootprintForObject(state, *findObjectById(state,1));
        if (footprint.width != spec.width || footprint.height != spec.height) return fail("stock ART foundation mismatch");
        const auto inside = [&](double x, double y) {
            return x > 15.5 && x < 15.5 + spec.width && y > 15.5 && y < 15.5 + spec.height;
        };
        std::vector<std::array<double,2>> previousCenters;
        for (int i = 0; i < count; ++i) {
            const auto* mover = findObjectById(state,2+i);
            const auto offset = worldObjectSubCellOffset(*mover);
            previousCenters.push_back({runtimeObjectRenderX(*mover)+offset.x, runtimeObjectRenderY(*mover)+offset.y});
        }
        issueRuntimeMoveCommand(state, goal);
        if (state.lastCommand.acceptedCount != count) return fail("move command rejected");
        bool complete = false;
        const int tickBudget=crowded?3600:1800;
        int completionTicks=0,maxContinuousBodyWait=0,maxWaitingId=-1,maxWaitStart=-1,maxWaitEnd=-1;
        int acceptedCrowdRetargets=0;
        struct WaitSample {
            bool active=false;int remaining=0;
            RuntimeCellPoint from,to,logical;double bodyX=0.0,bodyY=0.0;
            int subCell=-1;bool hadRender=false,wasMoving=false;
            bool hadMove=false,crowdArrivalAllowed=false,hasSharedGoal=false;
            RuntimeCellPoint sharedGoal;int sharedArrivalRadius=0;
        };
        std::array<WaitSample,18> waitBefore{};
        std::array<int,18> continuousWait{};
        std::array<RuntimeCellPoint,18> completionGoals{};
        if(crowded) for(int i=0;i<count;++i) {
            const auto found=state.behaviors.find(2+i);
            if(found==state.behaviors.end()) return fail("accepted crowd actor has no movement goal");
            completionGoals[i]=found->second.moveTarget;
        }
        for (int tick = 0; tick < tickBudget; ++tick) {
            if(crowded) for(int i=0;i<count;++i) {
                auto& sample=waitBefore[i];sample={};
                const auto* actor=findObjectById(state,2+i);if(!actor) continue;
                const auto offset=worldObjectSubCellOffset(*actor);
                sample.bodyX=runtimeObjectRenderX(*actor)+offset.x;
                sample.bodyY=runtimeObjectRenderY(*actor)+offset.y;
                sample.logical=runtimeCellPointForObject(*actor);sample.subCell=actor->subCell;
                sample.hadRender=actor->hasRenderPosition;sample.wasMoving=actor->renderMoveActive;
                const auto found=state.behaviors.find(actor->instanceId);
                if(found==state.behaviors.end()) continue;
                completionGoals[i]=found->second.moveTarget;
                sample.hadMove=found->second.kind==RuntimeBehaviorKind::Move;
                sample.crowdArrivalAllowed=runtimeMoveAllowsNearbyCrowdArrival(state,*actor,found->second);
                sample.hasSharedGoal=found->second.pendingHasSharedGoal;
                sample.sharedGoal=found->second.pendingSharedGoal;
                sample.sharedArrivalRadius=runtimeSharedMoveArrivalRadius(found->second);
                if(!actor->renderMoveActive) continue;
                const auto& move=found->second;
                sample.active=move.moveCooldownTicks>0 && move.renderMoveTicksRemaining>0;
                sample.remaining=move.renderMoveTicksRemaining;sample.from=move.renderFrom;sample.to=move.renderTo;
            }
            const auto firstNewReplaySequence=state.nextReplayEventSequence;
            ++state.tick; advanceRuntimeSimulation(state);
            complete = true;
            for (int i = 0; i < count; ++i) {
                const auto* mover = findObjectById(state,2+i);
                if (!mover || !mover->alive || inside(mover->x,mover->y)) return fail("unit entered the occupied cells");
                const auto offset = worldObjectSubCellOffset(*mover);
                const std::array<double,2> actual{runtimeObjectRenderX(*mover)+offset.x, runtimeObjectRenderY(*mover)+offset.y};
                const auto& before = previousCenters[i];
                if (runtimeFoundationTraceEntersRectangle(before[0],before[1],actual[0],actual[1],
                        15.5,15.5,15.5+spec.width,15.5+spec.height))
                    return fail("swept visible body crossed the foundation");
                previousCenters[i] = actual;
                ++poseChecks;
                const auto moving=state.behaviors.find(mover->instanceId);
                if(crowded) {
                    if(moving!=state.behaviors.end()) completionGoals[i]=moving->second.moveTarget;
                    else {
                        const auto current=runtimeCellPointForObject(*mover);
                        if(current!=completionGoals[i]) {
                            // Crowded arrival can accept and erase a newly assigned
                            // goal in this same tick. Require its actual arrival
                            // reason, never infer that acceptance from a still body.
                            const auto& sample=waitBefore[i];
                            const bool accepted=std::any_of(state.recentReplayEvents.begin(),state.recentReplayEvents.end(),
                                [&](const auto& event) {return event.sequence>=firstNewReplaySequence && event.tick==state.tick
                                    && event.kind=="MissionTransition" && event.objectId==mover->instanceId
                                    && event.reason=="moveCrowdedGoalReached"
                                    && event.missionTo==runtimeMissionName(RuntimeMissionIntent::Idle);});
                            const bool inRange=sample.hasSharedGoal
                                ? runtimeCellDistance(current,sample.sharedGoal)<=sample.sharedArrivalRadius+0.001
                                : runtimeCellDistance(current,completionGoals[i])<=2.25;
                            if(!sample.hadMove || !sample.crowdArrivalAllowed || !accepted || !inRange
                                || mover->mission!=runtimeMissionName(RuntimeMissionIntent::Idle)) {
                                std::ostringstream details;details<<"crowd actor ended without an accepted in-range goal retarget id="
                                    <<mover->instanceId<<" tick="<<state.tick<<" oldGoal="<<completionGoals[i].x<<','<<completionGoals[i].y
                                    <<" current="<<current.x<<','<<current.y<<" acceptedReason="<<accepted<<" inRange="<<inRange
                                    <<" mission="<<mover->mission;
                                return fail(details.str());
                            }
                            completionGoals[i]=current;++acceptedCrowdRetargets;
                        }
                        if(current!=completionGoals[i] || mover->renderMoveActive
                            || std::abs(runtimeObjectRenderX(*mover)-completionGoals[i].x)>1e-9
                            || std::abs(runtimeObjectRenderY(*mover)-completionGoals[i].y)>1e-9)
                            return fail("crowd actor ended before reaching/settling at its assigned move goal id="
                                +std::to_string(mover->instanceId));
                    }
                }
                complete = complete && moving==state.behaviors.end();
            }
            if(crowded) {
                // Include every body, even a completed stationary actor whose
                // render pose has cleared. A soft route filter is no waiver.
                for(int a=0;a<count;++a) for(int b=a+1;b<count;++b) {
                    const auto* first=findObjectById(state,2+a);const auto* second=findObjectById(state,2+b);
                    const bool firstLayer=worldObjectHasGroundRenderPose(*first)?first->renderOnBridge:first->onBridge;
                    const bool secondLayer=worldObjectHasGroundRenderPose(*second)?second->renderOnBridge:second->onBridge;
                    if(firstLayer!=secondLayer) continue;
                    const double dx=previousCenters[a][0]-previousCenters[b][0],dy=previousCenters[a][1]-previousCenters[b][1];
                    const double minimum=infantry?0.5:1.0;
                    if(dx*dx+dy*dy+1e-9<minimum*minimum) {
                        std::ostringstream details;details<<std::setprecision(17)
                            <<"crowded bodies overlap tick="<<state.tick<<" actors="<<first->instanceId<<','<<second->instanceId
                            <<" distance="<<std::hypot(dx,dy)<<" minimum="<<minimum;
                        for(const auto* actor:{first,second}) {
                            const auto offset=worldObjectSubCellOffset(*actor);
                            const auto& before=waitBefore[actor->instanceId-2];
                            details<<" [id="<<actor->instanceId<<" body="<<runtimeObjectRenderX(*actor)+offset.x
                                <<','<<runtimeObjectRenderY(*actor)+offset.y<<" slot="<<actor->subCell
                                <<" pose="<<actor->hasRenderPosition<<'/'<<actor->renderMoveActive
                                <<" beforeBody="<<before.bodyX<<','<<before.bodyY<<" beforeSlot="<<before.subCell
                                <<" beforeLogical="<<before.logical.x<<','<<before.logical.y
                                <<" beforePose="<<before.hadRender<<'/'<<before.wasMoving
                                <<" beforeRemaining="<<before.remaining;
                            const auto found=state.behaviors.find(actor->instanceId);
                            if(found!=state.behaviors.end()) {const auto& move=found->second;
                                details<<" from="<<move.renderFrom.x<<','<<move.renderFrom.y<<" to="<<move.renderTo.x<<','<<move.renderTo.y
                                    <<" remaining="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal;}
                            details<<']';
                        }
                        return fail(details.str());
                    }
                }
                for(int i=0;i<count;++i) {
                    const auto* actor=findObjectById(state,2+i);const auto found=state.behaviors.find(2+i);
                    bool waited=false;const auto& before=waitBefore[i];
                    if(before.active && actor->renderMoveActive && found!=state.behaviors.end()) {
                        const auto& move=found->second;
                        waited=move.state==RuntimeBehaviorState::Blocked && move.renderFrom==before.from && move.renderTo==before.to
                            && move.renderMoveTicksRemaining==before.remaining
                            && previousCenters[i][0]==before.bodyX && previousCenters[i][1]==before.bodyY;
                    }
                    continuousWait[i]=waited?continuousWait[i]+1:0;
                    if(continuousWait[i]>maxContinuousBodyWait) {
                        maxContinuousBodyWait=continuousWait[i];maxWaitingId=2+i;
                        maxWaitStart=state.tick-maxContinuousBodyWait+1;maxWaitEnd=state.tick;
                    }
                }
            }
            if (complete) {completionTicks=tick+1;break;}
        }
        if (!complete) {
            std::cerr<<std::setprecision(17)<<"Native foundation timeout "<<label<<" tick="<<state.tick
                <<" actors="<<count<<" budget="<<tickBudget<<" maxContinuousBodyWait="<<maxContinuousBodyWait
                <<" waitingActor="<<maxWaitingId<<" waitTicks="<<maxWaitStart<<'-'<<maxWaitEnd
                <<" spatial="<<runtimeGroundPoseSpatialIndexIsCurrent(state)<<'\n';
            const auto& occupancy=runtimeTickOccupancyLookup(state);
            for(int i=0;i<count;++i) {
                const auto* object=findObjectById(state,2+i);if(!object) continue;
                const auto slot=worldObjectSubCellOffset(*object);
                const double bodyX=runtimeObjectRenderX(*object)+slot.x,bodyY=runtimeObjectRenderY(*object)+slot.y;
                std::cerr<<" actor="<<object->instanceId<<" logical="<<object->x<<','<<object->y
                    <<" body="<<bodyX<<','<<bodyY<<" bridge="<<object->onBridge<<'/'<<object->renderOnBridge
                    <<" pose="<<object->hasRenderPosition<<'/'<<object->renderMoveActive;
                const auto found=state.behaviors.find(object->instanceId);
                if(found==state.behaviors.end()) {std::cerr<<" complete=1\n";continue;}
                const auto& move=found->second;
                std::cerr<<" state="<<static_cast<int>(move.state)<<" kind="<<static_cast<int>(move.kind)
                    <<" from="<<move.moveSegmentFrom.x<<','<<move.moveSegmentFrom.y
                    <<" renderFrom="<<move.renderFrom.x+move.renderFromOffsetX<<','<<move.renderFrom.y+move.renderFromOffsetY
                    <<" renderTo="<<move.renderTo.x+move.renderToOffsetX<<','<<move.renderTo.y+move.renderToOffsetY
                    <<" layers="<<move.renderFromOnBridge<<'/'<<move.renderToOnBridge
                    <<" cooldown="<<move.moveCooldownTicks<<" renderTicks="<<move.renderMoveTicksRemaining<<'/'<<move.renderMoveTicksTotal
                    <<" blocked="<<move.blockedTicks<<" path="<<move.path.size()
                    <<" goal="<<move.moveTarget.x<<','<<move.moveTarget.y<<" queued="<<move.pathRequestPending;
                if(!move.path.empty()) {
                    const auto nextCell=move.path.front();const auto occupied=occupancy.find(runtimeCellKey(nextCell.x,nextCell.y));
                    std::cerr<<" pathFront="<<nextCell.x<<','<<nextCell.y
                        <<" occupiedBy="<<(occupied==occupancy.end()?-1:occupied->second)
                        <<" departureHold="<<runtimeGroundDepartureHoldsTransition(state,*object,runtimeCellPointForObject(*object),nextCell);
                }
                if(!object->renderMoveActive || move.renderMoveTicksTotal<=0 || move.renderMoveTicksRemaining<=0) {
                    std::cerr<<" bodyWait=0\n";continue;
                }
                const double progress=runtimeVehicleMoveRenderProgressAtRemaining(move,std::max(0,move.renderMoveTicksRemaining-1));
                const auto point=runtimeGroundRenderMovePoint(*object,move,progress);
                const double nextX=point[0]+slot.x,nextY=point[1]+slot.y;
                const bool currentLayer=object->renderOnBridge;
                const bool nextLayer=progress<0.5?move.renderFromOnBridge:move.renderToOnBridge;
                std::cerr<<" preview="<<nextX<<','<<nextY<<" previewBridge="<<nextLayer
                    <<" bodyWait="<<runtimeStoppedGroundPoseBlocksRenderMove(state,*object,point[0],point[1])<<'\n';
                for(const auto& other:state.world.getObjects()) {
                    if(other.instanceId==object->instanceId || !runtimeGroundPoseIsSpatialCandidate(state,other)
                        || nextLayer!=other.renderOnBridge) continue;
                    if(runtimeCrusherUsesCrushingMovement(state,*object)
                        && runtimeCrusherCanCrushObject(state,*object,other,runtimeCellPointForObject(other),true)) continue;
                    const auto otherSlot=worldObjectSubCellOffset(other);
                    const double otherX=other.renderX+otherSlot.x,otherY=other.renderY+otherSlot.y;
                    const bool foot=object->kind==WorldObjectKind::Infantry,otherFoot=other.kind==WorldObjectKind::Infantry;
                    const double minimum=foot&&otherFoot?0.5:(!foot&&!otherFoot?1.0:0.75);
                    const double currentDistance=(bodyX-otherX)*(bodyX-otherX)+(bodyY-otherY)*(bodyY-otherY);
                    const double nextDistance=(nextX-otherX)*(nextX-otherX)+(nextY-otherY)*(nextY-otherY);
                    if(nextDistance+1e-9>=minimum*minimum || (currentLayer==other.renderOnBridge
                        && currentDistance+1e-9<minimum*minimum && nextDistance>currentDistance+1e-9)) continue;
                    std::cerr<<"  spacingPartner="<<other.instanceId<<" body="<<otherX<<','<<otherY
                        <<" currentDistance="<<std::sqrt(currentDistance)<<" nextDistance="<<std::sqrt(nextDistance)
                        <<" minimum="<<minimum<<" active="<<other.renderMoveActive
                        <<" stopped="<<worldObjectHasStoppedGroundPose(other);
                    const auto partner=state.behaviors.find(other.instanceId);
                    if(partner!=state.behaviors.end()) std::cerr<<" from="<<partner->second.renderFrom.x<<','<<partner->second.renderFrom.y
                        <<" to="<<partner->second.renderTo.x<<','<<partner->second.renderTo.y
                        <<" cooldown="<<partner->second.moveCooldownTicks<<" renderTicks="<<partner->second.renderMoveTicksRemaining
                        <<'/'<<partner->second.renderMoveTicksTotal;
                    std::cerr<<'\n';
                }
            }
            return fail("detour did not finish");
        }
        if (!crowded && runtimeCellPointForObject(*findObjectById(state,2)) != goal)
            return fail("unit did not reach the opposite side");
        if (crowded) {
            crowdedUnits += count;
            std::cout<<"Native crowded spacing "<<label<<": actors="<<count<<" completionTicks="<<completionTicks
                <<" budget="<<tickBudget<<" sameLayerMinimum="<<(infantry?0.5:1.0)
                <<" maxContinuousBodyWait="<<maxContinuousBodyWait<<" waitingActor="<<maxWaitingId
                <<" waitTicks="<<maxWaitStart<<'-'<<maxWaitEnd<<" acceptedCrowdRetargets="<<acceptedCrowdRetargets
                <<" everyFramePairs=passed\n";
        }
        ++routes;
    }
    std::cout << "Native foundation routes: " << routes << " stock-building routes, "
        << crowdedUnits << " crowded infantry/vehicles, " << poseChecks
        << " continuous pose checks passed\n";
    return true;
}
