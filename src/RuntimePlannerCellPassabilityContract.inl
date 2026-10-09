// Include after RuntimeMassMovementBenchmark.inl and all simulation/command helpers.
// Actual scalar body, production BFS and command driver are compared; no mirrored
// predicate or reconstructed component algorithm substitutes for either branch.
namespace RuntimePlannerPassabilityContractDetail {
struct Observation {
    int moverId = -1;
    RuntimePlannerPassabilityKey key;
    bool softPresent = false, value = false;
    std::vector<int> soft;
    bool operator==(const Observation&) const = default;
};
struct Observations {
    std::vector<Observation> values;
    std::size_t hits = 0;
    static void append(void* raw, const WorldObjectInstance& mover,
        const RuntimePlannerPassabilityKey& key, const std::unordered_set<int>* soft,
        bool value, bool hit) {
        auto& output = *static_cast<Observations*>(raw);
        Observation item{mover.instanceId, key, soft != nullptr, value, {}};
        if (soft) { item.soft.assign(soft->begin(), soft->end()); std::sort(item.soft.begin(), item.soft.end()); }
        output.values.push_back(std::move(item)); output.hits += hit ? 1 : 0;
    }
};
struct Authority {
    std::uint64_t world = 0, subcells = 0, gates = 0, repair = 0, mag = 0;
    std::string serialized;
    bool operator==(const Authority&) const = default;
};
bool capture(const RuntimeWindowState& state, Authority& out, std::string& error) {
    out.world = state.world.deterministicSimulationStateHash();
    out.subcells = state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects());
    out.gates = state.gates.stateHash(); out.repair = state.repairDepots.stateHash(); out.mag = state.magBeams.stateHash();
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(state, saved, error)) return false;
    out.serialized = saved.str();
    return !out.serialized.empty();
}
WorldObjectInstance object(int id, WorldObjectKind kind, const char* type, int x, int y) {
    WorldObjectInstance out;
    out.instanceId = id; out.kind = kind; out.type = type; out.owner = "Player";
    out.alive = true; out.health = 100; out.mission = "Guard";
    out.x = x; out.y = y; out.cell = runtimeCellKey(x, y); out.subCell = 2;
    return out;
}
const char* rulesText =
    "[VehicleTypes]\n0=CAR\n1=BOAT\n2=HOVER\n3=AIR\n4=MCV\n"
    "[InfantryTypes]\n0=MAN\n[BuildingTypes]\n0=SOLID\n1=LANE\n2=GATE\n3=REPAIR\n4=WALL\n5=YARD\n"
    "[CAR]\nStrength=100\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nCrusher=yes\nRepairable=yes\n"
    "[BOAT]\nStrength=100\nSpeed=6\nSpeedType=Float\nMovementZone=Water\nNaval=yes\n"
    "[HOVER]\nStrength=100\nSpeed=6\nSpeedType=Hover\nMovementZone=Amphibious\n"
    "[AIR]\nStrength=100\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
    "[MCV]\nStrength=100\nSpeed=6\nSpeedType=Track\nMovementZone=Normal\nDeploysInto=YARD\n"
    "[MAN]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Infantry\nCrushable=yes\n"
    "[SOLID]\nStrength=500\n[LANE]\nStrength=500\nUnitRepair=yes\nNumberImpassableRows=1\n"
    "[GATE]\nStrength=500\nGate=yes\n[REPAIR]\nStrength=500\nUnitRepair=yes\nNumberOfDocks=1\n"
    "[WALL]\nStrength=100\nWall=yes\nCrushable=yes\n[YARD]\nStrength=500\n";
bool make(RuntimeWindowState& state, const RuntimeConfig& config, int history, int phase,
    std::string& error) {
    const auto setupFail = [&](const std::string& reason) {
        error = "planner passability memo: synthetic setup history=" + std::to_string(history)
            + " phase=" + std::to_string(phase) + ": " + reason;
        return false;
    };
    state.config = config; state.config.fixedTickHz = 30;
    state.config.computerAiEnabled = state.config.multiplayerActive = false;
    state.config.crates = state.config.superweapons = state.config.shortGame = false;
    if (state.config.defaultMapPath.empty()) state.config.defaultMapPath = "PlannerMemoContract.map";
    IniFile rules;
    if (!rules.loadFromString(rulesText)) return setupFail("INI parse");
    if (!state.rules.loadFromIni(rules)) return setupFail("rules load");
    const auto* carRules = state.rules.findUnitType("Vehicle", "CAR");
    const auto* gateRules = state.rules.findBuildingType("GATE");
    const auto* repairRules = state.rules.findBuildingType("REPAIR");
    if (!carRules || carRules->strength != 100 || !carRules->repairable
        || !gateRules || !gateRules->gate || !repairRules || !repairRules->unitRepair
        || repairRules->numberOfDocks != 1)
        return setupFail("CAR strength/repairable, GATE gate, or REPAIR UnitRepair/NumberOfDocks rules readback");
    state.rulesLoaded = true; state.tick = 10;
    state.economy.owner = "Player"; state.economy.skirmishStartApplied = true;
    state.triggers.bootstrapComplete = true; state.visionEnabled = false;
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
        state.world.accessTerrainCells().push_back(cell);
    }
    // The real lookup keeps its original duplicate-coordinate insertion rule.
    TerrainCell duplicate = state.world.getTerrainCells()[12];
    duplicate.level = 3; state.world.accessTerrainCells().push_back(duplicate);
    auto& objects = state.world.accessObjects();
    objects.push_back(object(1, WorldObjectKind::Unit, "CAR", 1, 1));
    objects.push_back(object(2, WorldObjectKind::Infantry, "MAN", 2, 1));
    objects.push_back(object(3, WorldObjectKind::Unit, "BOAT", 10, 1));
    objects.push_back(object(4, WorldObjectKind::Unit, "HOVER", 1, 2));
    objects.push_back(object(5, WorldObjectKind::Unit, "AIR", 1, 3));
    objects.back().airborne = true;
    objects.push_back(object(6, WorldObjectKind::Unit, "MISSING", 1, 4));
    objects.push_back(object(7, WorldObjectKind::Unit, "MCV", 2, 3));
    objects.back().mission = "McvDeployTurning";
    state.behaviors[7].moveTarget = {3, 3};
    objects.push_back(object(30, WorldObjectKind::Structure, "SOLID", 6, 6));
    objects.push_back(object(31, WorldObjectKind::Structure, "LANE", 1, 8));
    objects.push_back(object(32, WorldObjectKind::Structure, "GATE", 8, 4));
    objects.push_back(object(33, WorldObjectKind::Structure, "REPAIR", 5, 8));
    objects.push_back(object(34, WorldObjectKind::Structure, "WALL", 8, 7));
    objects.back().owner = "Enemy";
    objects.push_back(object(35, WorldObjectKind::Infantry, "MAN", 8, 8));
    objects.back().owner = "Enemy";
    state.behaviors[35].moveTarget={8,8}; // Actual soft-occupancy branch requires behavior presence.
    objects.push_back(object(36, WorldObjectKind::Unit, "CAR", 7, 8));
    objects.back().health = 0; // alive zero-health retains the scalar eligibility
    objects.push_back(object(37, WorldObjectKind::Unit, "CAR", 7, 9));
    objects.back().alive = false; objects.back().health = 0; objects.back().transportPassengerIds = {38};
    objects.push_back(object(38, WorldObjectKind::Infantry, "MAN", 4, 4));
    objects.push_back(object(40, WorldObjectKind::Infantry, "MAN", 4, 6));
    objects.push_back(object(40, WorldObjectKind::Unit, "CAR", 4, 7)); // same ID, distinct residents
    objects.push_back(object(-3, WorldObjectKind::Infantry, "MAN", 9, 9));
    objects.push_back(object(41, WorldObjectKind::Unit, "CAR", 2, 8));
    // reserveDock validates allowFullHealth=false; this actual service client
    // must need repair, rather than receiving the UnitAlreadyFull veto.
    objects.back().health = 50;
    objects.push_back(object(42, WorldObjectKind::Unit, "AIR", 3, 2)); objects.back().airborne = true;
    objects.push_back(object(43, WorldObjectKind::Infantry, "MAN", 11, 10));
    state.behaviors[42].moveCooldownTicks = 2; state.behaviors[42].moveSegmentFrom = {4, 2};
    for (int n = 0; n < history; ++n) {
        auto dead = object(10000 + n, WorldObjectKind::Unit, "CAR", 3, 3);
        dead.alive = false; dead.health = 0; objects.push_back(std::move(dead));
    }
    if (phase == 1) {
        auto dead = object(40, WorldObjectKind::Unit, "CAR", 4, 5);
        dead.alive = false; dead.health = 0; objects.insert(objects.begin(), dead);
        std::reverse(objects.begin(), objects.end());
    }
    state.structureFootprints["SOLID"] = {2,2,true}; state.structureFootprints["LANE"] = {3,2,true};
    state.structureFootprints["GATE"] = {1,1,true}; state.structureFootprints["REPAIR"] = {2,2,true};
    state.structureFootprints["WALL"] = {1,1,true}; state.structureFootprints["YARD"] = {2,2,true};
    WorldTerrainObject terrain;
    terrain.x = 3; terrain.y = 6; terrain.cell = runtimeCellKey(3,6);
    terrain.alive = true; terrain.health = terrain.strength = 100;
    terrain.occupationBits = 1; terrain.occupiedSubCells = {2}; terrain.footprintWidth = 2;
    state.world.accessTerrainObjects().push_back(terrain);
    initializeRuntimeNavigation(state);
    for (auto& [key, info] : state.terrainInfoByCell) {
        const auto* cell=state.terrainByCell.at(key);
        if(!cell) return setupFail("null terrain lookup for key="+std::to_string(key));
        const int x = cell->x, y = cell->y;
        info = RuntimeTerrainInfo{}; info.passable = true; info.cost = 10;
        info.authoredLandType = info.movementLandType = x == 10 ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
        info.water = x == 10; info.shore = x == 9;
        info.bridge = x == 10 && y == 5; info.ramp = x == 8 && y == 5;
        if (x == 8 && y == 6) { info.cliff = true; info.passable = false; }
    }
    if(!state.terrainInfoByCell.at(runtimeCellKey(10,5)).bridge
        || !state.terrainInfoByCell.at(runtimeCellKey(10,4)).water
        || !state.terrainInfoByCell.at(runtimeCellKey(9,4)).shore
        || !state.terrainInfoByCell.at(runtimeCellKey(8,5)).ramp
        || !state.terrainInfoByCell.at(runtimeCellKey(8,6)).cliff)
        return setupFail("bridge/water/shore/ramp/cliff terrain readback");
    ensureRuntimeGatesInitialized(state);
    if (!state.gatesInitialized || state.gates.findGate(32) == nullptr)
        return setupFail("ensureRuntimeGatesInitialized did not create live GATE id=32");
    const std::vector<RuntimeSubcellMoveRequest> subcellRequests{
        {2, {0,0,runtimeCellKey(0,0),false}}};
    const auto reserved = state.subcells.reserveBatch(state.world.getObjects(),
        state.world.getTerrainObjects(),subcellRequests);
    state.subcellsInitialized = true;
    if (reserved.size()!=1) return setupFail("subcell reserveBatch result count="+std::to_string(reserved.size()));
    if (!reserved.front().reserved())
        return setupFail("subcell reserveBatch mover=2 status="+std::to_string(static_cast<int>(reserved.front().status)));
    RuntimeRepairDepotContext repair;
    repair.rules = &state.rules; repair.objects = &state.world.getObjects();
    repair.areAllied = [](const std::string& a,const std::string& b){return a==b;};
    repair.resolveHostDomain = [](int){return std::optional<RuntimeRepairDepotDomain>{RuntimeRepairDepotDomain::Ground};};
    repair.resolveUnitDomain = repair.resolveHostDomain;
    repair.resolveDockCell = [](int,int,int){return std::optional<RuntimeRepairDepotCell>{{5,7,false}};};
    repair.canPathTo = [](int,const RuntimeRepairDepotCell&){return true;};
    const auto* repairUnit = findObjectById(state,41);
    if (!repairUnit || !repairUnit->alive || repairUnit->health != 50
        || repairUnit->health >= carRules->strength)
        return setupFail("CAR41 must be alive and actually damaged before reserveDock");
    const auto repairReserved = state.repairDepots.reserveDock(repair,33,41,state.tick);
    if (!repairReserved.accepted || repairReserved.veto != RuntimeRepairDepotVeto::None)
        return setupFail("repair reserveDock host=33 unit=41 veto="
            +std::to_string(static_cast<int>(repairReserved.veto))+" reason="+repairReserved.reason);
    state.repairDepotsInitialized = true; // The full save must include this real authority.
    if (state.repairDepots.relations().size()!=1)
        return setupFail("repair reserveDock relation count="+std::to_string(state.repairDepots.relations().size()));
    const auto& relation = state.repairDepots.relations().front();
    if (relation.hostId!=33 || relation.unitId!=41 || relation.dockIndex!=0
        || relation.phase!=RuntimeRepairDepotRelationPhase::Reserved
        || relation.dockCell!=RuntimeRepairDepotCell{5,7,false})
        return setupFail("repair reserveDock did not establish the exact host=33/unit=41/dock=0/cell=(5,7) reservation");
    if (!runtimeCellReservedByOtherRepairUnit(state,{5,7},1))
        return setupFail("actual repair reservation not visible to runtimeCellReservedByOtherRepairUnit");
    if (state.gates.findGate(32) == nullptr) return setupFail("live GATE32 lost after reservation setup");
    return true;
}
} // namespace RuntimePlannerPassabilityContractDetail

#include "RuntimePlannerKernelContract.inl"

bool runRuntimePlannerCellPassabilityContract(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    RuntimeComponentPassabilityReferenceScope componentMemoEnabled(false);
    RuntimeComponentDenseReferenceScope denseEnabled(false);
    RuntimeComponentKernelReferenceScope kernelEnabled(false);
    const auto fail = [&](const std::string& why) {
        error = "planner passability memo: " + why; return false;
    };
    if (!runRuntimePlannerKernelPrimitiveContract(config,error)) return false;
    struct CollidingHash {
        std::size_t operator()(const RuntimePlannerPassabilityKey&) const noexcept { return 0; }
    };
    std::unordered_map<RuntimePlannerPassabilityKey,int,CollidingHash> collisionKeys;
    const std::array<RuntimePlannerPassabilityKey,7> distinct{{
        {1000,0,-1,false,false,PathMovementMode::Ground},
        {0,1,-1,false,false,PathMovementMode::Ground},
        {1000,0,0,false,false,PathMovementMode::Ground},
        {1000,0,-1,true,false,PathMovementMode::Ground},
        {1000,0,-1,false,true,PathMovementMode::Ground},
        {1000,0,-1,false,true,PathMovementMode::Air},
        {1000,0,std::numeric_limits<int>::min(),false,false,PathMovementMode::Ground}}};
    for(std::size_t n=0;n<distinct.size();++n) collisionKeys.emplace(distinct[n],static_cast<int>(n));
    if(collisionKeys.size()!=distinct.size() || runtimeCellKey(1000,0)!=runtimeCellKey(0,1))
        return fail("forced collision/exact raw key alias fixture");
    for(std::size_t n=0;n<distinct.size();++n)
        if(collisionKeys.at(distinct[n])!=static_cast<int>(n)) return fail("full raw key equality lost to hash collision");
    std::size_t visitedChecks = 0;
    const std::vector<std::vector<int>> visitedDomains{
        {}, {-1,std::numeric_limits<int>::min()}, {0},
        {0,63,64,127,1000,1001}, {0,900000}, {0,1048575},
        {0,1048576,std::numeric_limits<int>::max()}};
    const std::vector<int> visitedQueries{
        -1,std::numeric_limits<int>::min(),0,1,63,64,65,127,128,
        runtimeCellKey(1000,0),runtimeCellKey(0,1),1001,900000,
        1048574,1048575,1048576,std::numeric_limits<int>::max(),0,64,-1};
    for(std::size_t domain=0;domain<visitedDomains.size();++domain) for(bool reference : {true,false}) {
        std::unordered_map<int,const TerrainCell*> terrain;
        for(int key : visitedDomains[domain]) terrain.emplace(key,nullptr);
        RuntimeComponentDenseReferenceScope branch(reference);
        RuntimePlannerVisitedCellKeys visited(terrain);
        std::unordered_set<int> original; original.reserve(terrain.size());
        if(visited.size()!=0 || visited.bitmapBytes()>131072
            || (reference && visited.bitmapBytes()!=0)) return fail("visited bitmap bound/empty/reference setup");
        for(int key : visitedQueries) {
            for(int probe : visitedQueries) {
                ++visitedChecks;
                if(visited.contains(probe)!=(original.find(probe)!=original.end()))
                    return fail("visited original integer-key membership before insert");
            }
            if(visited.insert(key)!=original.insert(key).second || visited.size()!=original.size())
                return fail("visited original integer-key first insert/unique count");
            if(visited.insert(key)!=original.insert(key).second || visited.size()!=original.size())
                return fail("visited original integer-key duplicate insert/alias count");
        }
        for(int probe : visitedQueries) {
            ++visitedChecks;
            if(visited.contains(probe)!=(original.find(probe)!=original.end()))
                return fail("visited original integer-key final membership");
        }
        if(reference && visited.unorderedEntries()!=original.size())
            return fail("visited dense reference did not restore the complete C12 unordered set");
        if(!reference && domain>=2 && visited.bitmapBytes()==0)
            return fail("visited canonical key fixture failed to use actual bitmap membership");
        if(!reference && domain>=5 && visited.bitmapBytes()!=131072)
            return fail("visited high/outside key did not keep the exact 128 KiB bitmap limit");
        RuntimePlannerVisitedCellKeys separate(terrain);
        if(separate.size()!=0 || separate.contains(0) || separate.contains(-1)
            || !separate.insert(-12345) || visited.contains(-12345))
            return fail("visited independent planner storage borrowed previous membership");
    }
    std::size_t predicateChecks = 0, plannerChecks = 0, observedQueries = 0, observedHits = 0;
    const auto comparePlanners = [&](RuntimeWindowState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupied, const std::string& label) {
        RuntimeSnapshotActiveStateScope active(state);
        Authority before, after;
        if (!capture(state,before,error)) return false;
        Observations scalar, memo, unorderedMemo, denseOriginalKernel;
        std::unordered_map<int,int> oldComponent, newComponent, unorderedComponent, denseOriginalComponent;
        std::optional<RuntimeCellPoint> oldNearest, newNearest, oldBest, newBest,
            unorderedNearest, unorderedBest, denseOriginalNearest, denseOriginalBest;
        {
            RuntimeComponentPassabilityReferenceScope reference(true);
            RuntimeComponentKernelReferenceScope kernelReference(true);
            RuntimePlannerPassabilityObserverScope record(&Observations::append,&scalar);
            oldComponent = buildRuntimeMoveComponentByCell(state,mover,terrain,occupied);
            oldNearest = findNearestRuntimePassableCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
            oldBest = findBestRuntimeFormationTargetCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
        }
        if (!capture(state,after,error) || !(before==after)) return fail(label+" reference planner mutated authority/serialized state");
        {
            RuntimeComponentPassabilityReferenceScope reference(false);
            RuntimeComponentDenseReferenceScope denseReference(true);
            RuntimeComponentKernelReferenceScope kernelReference(true);
            RuntimePlannerPassabilityObserverScope record(&Observations::append,&unorderedMemo);
            unorderedComponent = buildRuntimeMoveComponentByCell(state,mover,terrain,occupied);
            unorderedNearest = findNearestRuntimePassableCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
            unorderedBest = findBestRuntimeFormationTargetCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
        }
        if (!capture(state,after,error) || !(before==after)) return fail(label+" C12 unordered planner mutated authority/serialized state");
        {
            RuntimeComponentPassabilityReferenceScope reference(false);
            RuntimeComponentDenseReferenceScope denseReference(false);
            RuntimeComponentKernelReferenceScope kernelReference(true);
            RuntimePlannerPassabilityObserverScope record(&Observations::append,&denseOriginalKernel);
            denseOriginalComponent = buildRuntimeMoveComponentByCell(state,mover,terrain,occupied);
            denseOriginalNearest = findNearestRuntimePassableCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
            denseOriginalBest = findBestRuntimeFormationTargetCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
        }
        if (!capture(state,after,error) || !(before==after)) return fail(label+" C14 dense/original-cost kernel mutated authority/serialized state");
        {
            RuntimeComponentPassabilityReferenceScope reference(false);
            RuntimeComponentKernelReferenceScope kernelReference(false);
            RuntimePlannerPassabilityObserverScope record(&Observations::append,&memo);
            newComponent = buildRuntimeMoveComponentByCell(state,mover,terrain,occupied);
            newNearest = findNearestRuntimePassableCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
            newBest = findBestRuntimeFormationTargetCellWithOccupancy(state,mover,{8,8},-1,nullptr,
                terrain,occupied,nullptr,nullptr,-1);
        }
        ++plannerChecks; observedQueries += scalar.values.size(); observedHits += memo.hits;
        if (!capture(state,after,error) || !(before==after)) return fail(label+" memo planner mutated authority/serialized state");
        if (oldComponent != newComponent || oldNearest != newNearest || oldBest != newBest)
            return fail(label+" complete component numbers/nearest/best differ");
        if (oldComponent != unorderedComponent || oldNearest != unorderedNearest || oldBest != unorderedBest)
            return fail(label+" C12 unordered complete component numbers/nearest/best differ");
        if (oldComponent != denseOriginalComponent || oldNearest != denseOriginalNearest || oldBest != denseOriginalBest)
            return fail(label+" C14 dense/original-cost complete component numbers/nearest/best differ");
        if (scalar.values != memo.values || scalar.values.empty() || scalar.hits != 0)
            return fail(label+" actual candidate query order/raw arguments/results differ");
        if (scalar.values != unorderedMemo.values)
            return fail(label+" C12 unordered actual candidate query order/raw arguments/results differ");
        if (scalar.values != denseOriginalKernel.values)
            return fail(label+" C14 dense/original-cost actual candidate query order/raw arguments/results differ");
        return true;
    };

    std::size_t denseChecks = 0;
    {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        if(!make(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const auto& mover=*findObjectById(state,1);
        const auto actualMode=runtimeObjectMovementMode(state,mover);
        if(actualMode!=PathMovementMode::Ground) return fail("dense fixture must exercise explicit Ground and absent mode independently");
        Authority before,after;
        if(!capture(state,before,error)) return false;
        RuntimeComponentPassabilityReferenceScope optimized(false);
        const auto verify=[&](const auto& terrain,const RuntimeCellPoint& point,
            int allowed=-1,bool crush=false,std::optional<PathMovementMode> mode=std::nullopt) {
            const auto expected=runtimeCellIsPassableUncached(state,terrain,occupied,point,mover,
                allowed,nullptr,crush,mode);
            const auto first=runtimeCellIsPassable(state,terrain,occupied,point,mover,allowed,nullptr,crush,mode);
            const auto second=runtimeCellIsPassable(state,terrain,occupied,point,mover,allowed,nullptr,crush,mode);
            ++denseChecks;
            return expected==first && expected==second;
        };
        for(bool reference : {true,false}) {
            RuntimeComponentDenseReferenceScope branch(reference);
            RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
            if(scope.denseAttempted() || scope.denseBytes()!=0 || scope.entries()!=0)
                return fail("dense domain eagerly scanned/allocated before any common query");
            for(int y=0;y<12;++y) for(int x=0;x<12;++x)
                for(bool hasMode : {false,true}) for(bool crush : {false,true})
                    if(!verify(state.terrainByCell,{x,y},-1,crush,
                        hasMode ? std::optional<PathMovementMode>{actualMode} : std::nullopt))
                        return fail("dense common raw-coordinate/mode-presence/crush scalar comparison");
            if(scope.entries()!=576 || scope.hits()!=576 || scope.misses()!=576)
                return fail("dense/C12 exact four-slot cold and repeated query counts");
            if(reference ? (scope.denseAttempted() || scope.denseBytes()!=0 || scope.denseEntries()!=0
                    || scope.unorderedEntries()!=576 || scope.denseHits()!=0)
                : (!scope.denseAttempted() || scope.denseBytes()!=144 || scope.denseEntries()!=576
                    || scope.unorderedEntries()!=0 || scope.denseHits()!=576))
                return fail("dense-only reference did not restore C12 map or actual dense hits");
            for(const auto mode : {PathMovementMode::Naval,PathMovementMode::Amphibious,PathMovementMode::Air})
                if(!verify(state.terrainByCell,{10,4},-1,false,mode))
                    return fail("dense nonactual movement mode scalar comparison");
            for(int allowed : {0,31,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
                if(!verify(state.terrainByCell,{8,8},allowed,true,actualMode))
                    return fail("dense allowed ID must retain full unordered key");
            for(const auto point : std::array<RuntimeCellPoint,4>{{{-1,0},{1000,0},{0,1},{1000,1}}})
                if(!verify(state.terrainByCell,point)) return fail("dense negative/noncanonical/legacy-alias scalar comparison");
            if(!reference && (scope.denseEntries()!=576 || scope.unorderedEntries()!=10))
                return fail("dense common slots borrowed a nonactual mode/allowed/noncanonical key");
            // Foreign maps/states/movers remain scalar before either memo store.
            auto copy=state.terrainByCell; auto moverCopy=mover;
            const auto hits=scope.hits(), misses=scope.misses();
            if(!verify(copy,{0,0})
                || runtimeCellIsPassable(state,state.terrainByCell,occupied,{0,0},moverCopy)
                    !=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},moverCopy)
                || scope.hits()!=hits || scope.misses()!=misses)
                return fail("dense foreign identity borrowed memo");
        }
        // Bounds are raw coordinates, not legacy integer aliases. Sparse or
        // oversized domains use the C12 map without allocating the dense table.
        const auto verifyFallbackDomain=[&](const auto& terrain,const std::vector<RuntimeCellPoint>& points,
            const char* label) {
            RuntimePlannerCellPassabilityScope scope(state,mover,terrain,occupied);
            for(const auto& point : points) if(!verify(terrain,point)) return fail(label);
            if(!scope.denseAttempted() || scope.denseBytes()!=0 || scope.denseEntries()!=0
                || scope.unorderedEntries()!=points.size() || scope.hits()!=points.size())
                return fail(std::string(label)+" did not exercise old unordered fallback");
            return true;
        };
        std::unordered_map<int,const TerrainCell*> sparse{
            {runtimeCellKey(0,0),state.terrainByCell.at(runtimeCellKey(0,0))},
            {runtimeCellKey(11,11),state.terrainByCell.at(runtimeCellKey(11,11))}};
        if(!verifyFallbackDomain(sparse,{{0,0},{11,11}},"sparse dense-domain scalar comparison")) return false;
        TerrainCell distantTerrain; distantTerrain.x=999; distantTerrain.y=263;
        distantTerrain.cellIndex512=runtimeCellKey(distantTerrain.x,distantTerrain.y);
        auto oversized=sparse; oversized.emplace(distantTerrain.cellIndex512,&distantTerrain);
        if(!verifyFallbackDomain(oversized,{{0,0},{999,263}},"oversized dense-domain scalar comparison")) return false;
        {
            // Exactly the maximum byte domain. The dense memo must retain
            // more than 65536 common results; the independent reference must
            // still stop the C12 unordered store at its original 65536 limit.
            std::vector<TerrainCell> cells; cells.reserve(65537);
            for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
                TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
                cells.push_back(cell);
            }
            TerrainCell corner; corner.x=corner.y=511;
            corner.cellIndex512=runtimeCellKey(corner.x,corner.y); cells.push_back(corner);
            std::unordered_map<int,const TerrainCell*> terrain; terrain.reserve(cells.size());
            for(const auto& cell : cells) terrain.emplace(runtimeCellKey(cell.x,cell.y),&cell);
            for(bool reference : {true,false}) {
                RuntimeComponentDenseReferenceScope branch(reference);
                RuntimePlannerCellPassabilityScope scope(state,mover,terrain,occupied);
                for(int y=0;y<=256;++y) for(int x=0;x<=256;++x)
                    if(!verify(terrain,{x,y},-1,false,actualMode))
                        return fail("maximum dense domain actual scalar values differ");
                if(reference ? (scope.denseBytes()!=0 || scope.denseAttempted()
                        || scope.unorderedEntries()!=65536 || scope.entries()!=65536
                        || scope.fallbacks()!=1026 || scope.hits()!=65536)
                    : (scope.denseBytes()!=262144 || scope.denseEntries()!=66049
                        || scope.unorderedEntries()!=0 || scope.entries()!=66049
                        || scope.fallbacks()!=0 || scope.denseHits()!=66049))
                    return fail("maximum dense byte bound/C12 entry limit/full-domain common retention");
            }
        }
        auto malformed=state.terrainByCell;
        malformed[runtimeCellKey(1000,0)]=nullptr;
        malformed[-1]=state.terrainByCell.at(runtimeCellKey(0,0));
        malformed[900000]=state.terrainByCell.at(runtimeCellKey(0,0));
        if(!comparePlanners(state,mover,malformed,occupied,"dense raw-alias/duplicate/null/key-coordinate mismatch")) return false;
        {
            RuntimePlannerCellPassabilityScope zero(state,mover,state.terrainByCell,occupied,true,0);
            if(!verify(state.terrainByCell,{0,0}) || zero.entries()!=0 || zero.denseAttempted()
                || zero.fallbacks()!=2) return fail("zero capacity must remain allocation-free scalar fallback");
        }
        {
            RuntimePlannerCellPassabilityScope tiny(state,mover,state.terrainByCell,occupied,true,1);
            if(!verify(state.terrainByCell,{0,0}) || !verify(state.terrainByCell,{0,0},31)
                || !verify(state.terrainByCell,{0,0},-1,true,actualMode)
                || tiny.entries()!=1 || tiny.denseEntries()!=1 || tiny.unorderedEntries()!=0
                || tiny.fallbacks()!=4 || tiny.hits()!=1)
                return fail("dense and unordered stores exceeded explicit combined tiny capacity");
        }
        if(!capture(state,after,error) || !(before==after)) return fail("dense domain/key/reference fixtures mutated full authoritative state");
    }

    for (int history : {0,1000,10000}) for (int phase : {0,1}) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope active(state);
        if (!make(state,config,history,phase,error)) return false;
        const auto occupied = buildRuntimeOccupancyLookup(state);
        const std::string label = "history="+std::to_string(history)+" phase="+std::to_string(phase);
        for (int id : {1,2,3,4,5,6,41})
            if (!comparePlanners(state,*findObjectById(state,id),state.terrainByCell,occupied,label+"/"+std::to_string(id))) return false;

        const auto& mover = *findObjectById(state,1);
        if(runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{8,8},mover,-1,nullptr,false,PathMovementMode::Ground)
            || !runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{8,8},mover,-1,nullptr,true,PathMovementMode::Ground))
            return fail(label+" real crush false/true fixture did not distinguish enterability");
        if(!runtimeMissionIsMcvDeployPending(*findObjectById(state,7))
            || state.subcells.availableSubCells(state.world.getObjects(),state.world.getTerrainObjects(),
                {4,4,runtimeCellKey(4,4),false})!=std::vector<int>{2,4,3})
            return fail(label+" pending MCV/dead-host cargo fixture not active");
        const std::array<std::optional<PathMovementMode>,5> modes{std::nullopt,
            PathMovementMode::Ground,PathMovementMode::Naval,PathMovementMode::Amphibious,PathMovementMode::Air};
        const std::array<RuntimeCellPoint,11> points{{{3,3},{4,4},{7,7},{8,7},{8,8},{8,4},{5,7},{10,5},{10,4},{-1,0},{1000,0}}};
        Authority before,after;
        if (!capture(state,before,error)) return false;
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
            std::unordered_set<int> soft{35,36}; // Non-null exact scalar bypass, even changed in place.
            for (const auto& point : points) for (int allowed : {-1,31,32,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
            for (bool crush : {false,true}) for (const auto mode : modes) for (int softCase=0;softCase<3;++softCase) {
                if (softCase==1) soft.clear(); else if (softCase==2) soft={35,40};
                const auto* softPtr = softCase ? &soft : nullptr;
                const auto hits = scope.hits();
                const bool oldValue = runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,
                    point,mover,allowed,softPtr,crush,mode);
                const bool first = runtimeCellIsPassable(state,state.terrainByCell,occupied,
                    point,mover,allowed,softPtr,crush,mode);
                const bool second = runtimeCellIsPassable(state,state.terrainByCell,occupied,
                    point,mover,allowed,softPtr,crush,mode);
                ++predicateChecks;
                if (oldValue != first || first != second || (softPtr && hits != scope.hits()))
                    return fail(label+" raw full-argument/soft-set equivalence");
            }
            soft={35,36};
            const auto softHits=scope.hits();
            if(!runtimeCellIsPassable(state,state.terrainByCell,occupied,{8,8},mover,-1,&soft,false)
                || !runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{8,8},mover,-1,&soft,false))
                return fail(label+" real soft-occupancy enterable fixture not active");
            soft.erase(35); soft.insert(40); // Same set address and size, changed membership.
            if(soft.size()!=2
                || runtimeCellIsPassable(state,state.terrainByCell,occupied,{8,8},mover,-1,&soft,false)
                || runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{8,8},mover,-1,&soft,false)
                || scope.hits()!=softHits) return fail(label+" same-size non-null soft-set edit borrowed stale memo");
            // Transition remains original: repair, diagonal side cells and height each run.
            for (RuntimeCellPoint from : {RuntimeCellPoint{4,6},RuntimeCellPoint{9,4},RuntimeCellPoint{7,5}}) {
                for (const auto& next : runtimeNeighborsOf(from)) {
                    const auto oldValue = [&]{RuntimeComponentPassabilityReferenceScope reference(true);
                        return runtimeCellTraversalCost(state,state.terrainByCell,occupied,from,next,mover);}();
                    const auto newValue = runtimeCellTraversalCost(state,state.terrainByCell,occupied,from,next,mover);
                    ++predicateChecks; if (oldValue != newValue) return fail(label+" actual diagonal/repair/height traversal cost");
                }
            }
        }
        if (!capture(state,after,error) || !(before==after)) return fail(label+" predicate authority/serialize changed");
        // Gate phases are altered strictly outside planner scopes, then reread.
        auto gateContext = runtimeGateContext(state);
        std::vector<RuntimeGatePassageRequest> requests{{32,1}}; gateContext.passageRequests=&requests;
        (void)state.gates.tick(gateContext,state.tick);
        if (state.gates.phaseOf(32) != RuntimeGatePhase::Opening) return fail("real gate fixture not Opening");
        if (!comparePlanners(state,mover,state.terrainByCell,occupied,label+"/openingGate")) return false;
        state.tick+=30; (void)state.gates.tick(gateContext,state.tick);
        if (state.gates.phaseOf(32) != RuntimeGatePhase::Open) return fail("real opening gate fixture not Open");
        if (!comparePlanners(state,mover,state.terrainByCell,occupied,label+"/openGate")) return false;
        gateContext.passageRequests=nullptr;
        gateContext.doorwayOccupied=[](int){return false;};
        state.tick+=state.gates.closeDelayTicks(gateContext,32)+1;
        (void)state.gates.tick(gateContext,state.tick);
        if (state.gates.phaseOf(32) != RuntimeGatePhase::Closing) return fail("real gate fixture not Closing");
        if (!comparePlanners(state,mover,state.terrainByCell,occupied,label+"/closingGate")) return false;

        // Identity, nested masking, raw key aliases, invalidation and capacity.
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope outer(state,mover,state.terrainByCell,occupied,true,2);
            const RuntimeCellPoint point{0,0};
            const auto query = [&]{return runtimeCellIsPassable(state,state.terrainByCell,occupied,point,mover);};
            const bool expected = runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,point,mover);
            if (query()!=expected || query()!=expected || outer.hits()!=1) return fail("outer cache actual warm/hit");
            const auto entries = outer.entries(), hits = outer.hits();
            {
                RuntimePlannerCellPassabilityScope disabled(state,mover,state.terrainByCell,occupied,false);
                if (query()!=expected || disabled.entries()!=0 || outer.hits()!=hits) return fail("disabled nested borrowed outer");
            }
            {
                RuntimePlannerCellPassabilityScope inner(state,mover,state.terrainByCell,occupied);
                if (query()!=expected || query()!=expected || inner.hits()!=1 || outer.hits()!=hits) return fail("nested owns cache");
            }
            if (query()!=expected || outer.hits()!=hits+1) return fail("outer not restored");
            auto terrainCopy=state.terrainByCell; auto occupiedCopy=occupied; auto copiedMover=mover;
            auto foreign=std::make_unique<RuntimeWindowState>();
            const auto misses=outer.misses(), guardHits=outer.hits();
            const auto foreignQuery = [&](const RuntimeWindowState& value,
                const auto& terrain,const auto& occupancy,const WorldObjectInstance& object) {
                return runtimeCellIsPassable(value,terrain,occupancy,point,object)
                    ==runtimeCellIsPassableUncached(value,terrain,occupancy,point,object);
            };
            if(!foreignQuery(state,terrainCopy,occupied,mover)
                || !foreignQuery(state,state.terrainByCell,occupiedCopy,mover)
                || !foreignQuery(state,state.terrainByCell,occupied,copiedMover)
                || !foreignQuery(*foreign,state.terrainByCell,occupied,mover))
                return fail("foreign identity scalar result differs");
            if (outer.misses()!=misses || outer.hits()!=guardHits) return fail("foreign identity reused cache");
            ++state.tick; (void)query(); --state.tick;
            if (outer.misses()!=misses || outer.hits()!=guardHits) return fail("tick identity reused cache");
            outer.invalidate(); // Before append, the original world-member address can expire.
            state.world.accessObjects().push_back(object(900001,WorldObjectKind::Unit,"CAR",11,11));
            (void)runtimeCellIsPassable(state,state.terrainByCell,occupied,point,copiedMover);
            if (outer.misses()!=misses || outer.hits()!=guardHits) return fail("tick/vector storage identity reused cache");
            // Mover reference may have been invalidated by append; use the safe copy for remainder.
            outer.invalidate();
            if (outer.matches(state,copiedMover,state.terrainByCell,occupied)) return fail("invalidation failed");
            if (entries != 1) return fail("unexpected raw entry count");
        }
        // Fresh world-member after possible reallocation. Same-address rules assignment
        // is outside the previous scope, and must start with an empty new scope.
        state.capabilityCache.profilesByType.clear();
        IniFile replacement;
        std::string changedRules=rulesText;
        const auto crusherAt=changedRules.find("Crusher=yes"), movementAt=changedRules.find("SpeedType=Track");
        if(crusherAt==std::string::npos || movementAt==std::string::npos) return fail("rules replacement markers");
        changedRules.replace(crusherAt,std::string("Crusher=yes").size(),"Crusher=no");
        const auto freshMovementAt=changedRules.find("SpeedType=Track");
        changedRules.replace(freshMovementAt,std::string("SpeedType=Track").size(),"SpeedType=Hover");
        if (!replacement.loadFromString(changedRules) || !state.rules.loadFromIni(replacement)) return fail("rules reload");
        auto borrowed=*findObjectById(state,1);
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope scope(state,borrowed,state.terrainByCell,occupied);
            const auto query=[&]{return runtimeCellIsPassable(state,state.terrainByCell,occupied,{0,0},borrowed);};
            if(!query() || !query() || scope.hits()!=1) return fail("borrowed-address fixture not warm");
            scope.invalidate(); borrowed.type="BOAT"; // Same address, different actual locomotor.
            if(query() || runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},borrowed)
                || scope.hits()!=1) return fail("invalidated borrowed address retained previous movement mode");
        }
        borrowed=*findObjectById(state,1);
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope scope(state,borrowed,state.terrainByCell,occupied);
            if(scope.entries()!=0) return fail("same borrowed address leaked previous scope entries");
            (void)runtimeCellIsPassable(state,state.terrainByCell,occupied,{0,0},borrowed);
            const auto hits=scope.hits(), misses=scope.misses();
            // Deliberately present incompatible storage to prove the defensive
            // identity fallback. The borrowed mover is stable across realloc.
            auto dead=object(900002,WorldObjectKind::Unit,"CAR",11,11);
            dead.alive=false; dead.health=0; state.world.accessObjects().push_back(dead);
            if(scope.matches(state,borrowed,state.terrainByCell,occupied)
                || runtimeCellIsPassable(state,state.terrainByCell,occupied,{0,0},borrowed)
                    !=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},borrowed)
                || scope.hits()!=hits || scope.misses()!=misses) return fail("world data/count identity failed to reject changed storage");
            scope.invalidate(); state.world.accessObjects().pop_back();
        }
        auto& newMover = *findMutableObjectById(state,1);
        const auto* reloaded= findRuntimeUnitType(state.rules,newMover);
        if(!reloaded || reloaded->crusher || runtimeObjectMovementMode(state,newMover)!=PathMovementMode::Amphibious)
            return fail("rules reload did not actually change crusher/movement mode");
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope tiny(state,newMover,state.terrainByCell,occupied,true,1);
            for (const auto& point : points) {
                const bool scalar=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,point,newMover);
                if (runtimeCellIsPassable(state,state.terrainByCell,occupied,point,newMover)!=scalar)
                    return fail("bounded-capacity scalar fallback");
            }
            if (tiny.entries()!=1 || tiny.fallbacks()==0) return fail("capacity fixture did not reach reference fallback");
        }
        // Same-address/same-size occupancy and terrain changes are not identity
        // changes. They require explicit invalidation BEFORE either mutation.
        auto mutableOccupied=occupied;
        mutableOccupied[runtimeCellKey(0,0)]=newMover.instanceId;
        {
            RuntimeComponentPassabilityReferenceScope optimized(false);
            RuntimePlannerCellPassabilityScope changing(state,newMover,state.terrainByCell,mutableOccupied);
            const RuntimeCellPoint point{0,0};
            const bool beforeValue=runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover);
            (void)runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover);
            if(!beforeValue || changing.hits()!=1) return fail("in-place invalidate fixture not warm/clear");
            changing.invalidate();
            const auto unchangedOccupancySize=mutableOccupied.size();
            mutableOccupied[runtimeCellKey(0,0)]=runtimeTerrainOccupantId(0);
            const bool scalar=runtimeCellIsPassableUncached(state,state.terrainByCell,mutableOccupied,point,newMover);
            if(mutableOccupied.size()!=unchangedOccupancySize || scalar
                || runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover)!=scalar
                || changing.hits()!=1) return fail("in-place invalidate used stale clear-cell result");
            state.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=false;
            state.subcells.clearReservations();
            newMover.owner="Enemy"; newMover.deployed=true;
            if(runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover)
                !=runtimeCellIsPassableUncached(state,state.terrainByCell,mutableOccupied,point,newMover)
                || changing.hits()!=1) return fail("invalidated terrain/reservation/mover state used stale memo");
            mutableOccupied=occupied;
            if(runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover))
                return fail("independent changed terrain passability was not read");
            state.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=true;
            if(!runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,newMover)
                || changing.hits()!=1) return fail("restored terrain passability used stale memo");
            const auto& infantry=*findObjectById(state,38);
            RuntimePlannerCellPassabilityScope infantryScope(state,infantry,state.terrainByCell,mutableOccupied);
            const auto infantryQuery=[&]{return runtimeCellIsPassable(state,state.terrainByCell,mutableOccupied,point,infantry);};
            if(!infantryQuery() || !infantryQuery() || infantryScope.hits()!=1)
                return fail("reservation invalidate fixture not warm/free");
            infantryScope.invalidate();
            const std::vector<RuntimeSubcellMoveRequest> requests{
                {2,{0,0,runtimeCellKey(0,0),false}},
                {35,{0,0,runtimeCellKey(0,0),false}},
                {43,{0,0,runtimeCellKey(0,0),false}}};
            const auto results=state.subcells.reserveBatch(state.world.getObjects(),state.world.getTerrainObjects(),requests);
            if(results.size()!=3 || !std::all_of(results.begin(),results.end(),[](const auto& result){return result.reserved();}))
                return fail("reservation invalidate fixture did not fill three actual slots");
            if(infantryQuery() || runtimeCellIsPassableUncached(state,state.terrainByCell,mutableOccupied,point,infantry)
                || infantryScope.hits()!=1) return fail("new full reservation set used stale free-cell memo");
            state.subcells.clearReservations();
            if(!infantryQuery() || !runtimeCellIsPassableUncached(state,state.terrainByCell,mutableOccupied,point,infantry)
                || infantryScope.hits()!=1) return fail("cleared reservation set used stale blocked memo");
        }
        // A foreign key may alias the old cellKey but exact x/y must remain different.
        auto malformed=state.terrainByCell;
        const int alias=runtimeCellKey(1000,0);
        malformed[alias] = nullptr;
        malformed[-1] = state.terrainByCell.at(runtimeCellKey(0,0));
        malformed[900000] = state.terrainByCell.at(runtimeCellKey(0,0)); // key disagrees with pointed cell
        if (!comparePlanners(state,newMover,malformed,occupied,label+"/nullLegacyAlias")) return false;
    }
    if (observedHits==0 || observedQueries==0 || plannerChecks==0) return fail("actual planners never exercised memo hits");

    // Real assets and production fixture; separately owned authorities use identical
    // stock rules/art/theater. All three actual commands and 660 real simulation ticks
    // are compared, with standalone full authority hash each tick.
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("original assets unavailable for real command equivalence");
    std::size_t trajectoryChecks=0, commandChecks=0;
    for (int history : {0,1000,10000}) {
        const auto realState = [&]{
            auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
            state.config=config; state.report=assets->report;
            state.rules=assets->rules; state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            return storage;
        };
        auto scalar=realState(), memo=realState();
        std::vector<int> oldIds,newIds;
        for (int side=0;side<2;++side) {
            auto& state=side ? *memo : *scalar;
            RuntimeSnapshotActiveStateScope active(state);
            if (!prepareRuntimeMassMovementWorld(state,256,side ? newIds : oldIds,error,false,true)) return false;
            // MassMovement intentionally clears source-map identity. Restore a
            // contract-only virtual identity to exercise the entire save codec;
            // both authorities use the same generated world and no source scripts.
            state.config.defaultMapPath="PlannerMemoMassMovementContract.map";
            if (history) {
                const int first=nextRuntimeObjectInstanceId(state);
                auto& objects=state.world.accessObjects();
                for(int n=0;n<history;++n) {
                    auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                    objects.push_back(std::move(dead));
                }
                auto terrainInfo=std::move(state.terrainInfoByCell);
                state.objectIndex=RuntimeObjectIndex{}; initializeRuntimeNavigation(state);
                state.terrainInfoByCell=std::move(terrainInfo);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
                invalidateRuntimeTickOccupancyCache(state); (void)completeRuntimeWorldRefresh(state);
            }
        }
        if(oldIds!=newIds) return fail("real fixture ID order differs");
        std::map<int,RuntimeCellPoint> starts;
        for(int id:oldIds) starts.emplace(id,runtimeCellPointForObject(*findObjectById(*scalar,id)));
        std::unordered_set<int> moved;
        bool sawActiveMovement=false;
        for (int n=1;n<=660;++n) {
            scalar->tick=memo->tick=n;
            if(n==61 || n==301 || n==541) {
                const RuntimeCellPoint goal=n==301 ? RuntimeCellPoint{22,48} : RuntimeCellPoint{76,32};
                if(n==61) {
                    RuntimeSnapshotActiveStateScope active(*scalar);
                    const std::unordered_set<int> movingIds(oldIds.begin(),oldIds.end());
                    const auto groupOccupied=buildRuntimeOccupancyLookup(*scalar,-1,nullptr,&movingIds);
                    const auto& realMover=*findObjectById(*scalar,oldIds.front());
                    if(!comparePlanners(*scalar,realMover,scalar->terrainByCell,groupOccupied,
                        "real-assets H="+std::to_string(history))) return false;
                }
                std::map<int,RuntimeCellPoint> oldDest,newDest;
                Observations oldCalls,newCalls;
                for(int side=0;side<2;++side) {
                    auto& state=side ? *memo : *scalar;
                    RuntimeSnapshotActiveStateScope active(state);
                    RuntimeComponentPassabilityReferenceScope reference(side==0);
                    RuntimeComponentKernelReferenceScope kernelReference(side==0);
                    RuntimePlannerPassabilityObserverScope observer(&Observations::append,side ? &newCalls : &oldCalls);
                    if(!issueRuntimeMassMovementOrder(state,side ? newIds : oldIds,goal,side ? newDest : oldDest,error)) return false;
                }
                ++commandChecks;
                const auto command=[](const RuntimeWindowState& state){
                    const auto& c=state.lastCommand;
                    return std::make_tuple(c.valid,c.verb,c.tick,c.selectedCount,c.acceptedCount,c.rejectedCount,
                        c.targetX,c.targetY,c.targetObjectId,c.targetObjectType,c.inputAckImmediate,c.feedbackLatencyTicks,
                        state.selectedObjectIds,state.interactionStatus,state.simulationStatus);
                };
                if(oldDest!=newDest || command(*scalar)!=command(*memo) || oldCalls.values!=newCalls.values)
                    return fail("real accepted IDs/destinations/full command/candidate order at tick="+std::to_string(n));
                Authority oldState,newState;
                {RuntimeSnapshotActiveStateScope active(*scalar); if(!capture(*scalar,oldState,error)) return false;}
                {RuntimeSnapshotActiveStateScope active(*memo); if(!capture(*memo,newState,error)) return false;}
                if(!(oldState==newState)) return fail("real command full serialized authorities at tick="+std::to_string(n));
            }
            std::uint64_t hashes[2]{};
            for(int side=0;side<2;++side) {
                auto& state=side ? *memo : *scalar;
                RuntimeSnapshotActiveStateScope active(state);
                RuntimeComponentPassabilityReferenceScope reference(side==0);
                RuntimeComponentKernelReferenceScope kernelReference(side==0);
                (void)advanceRuntimeSimulation(state);
                hashes[side]=runtimeStandaloneSimulationStateHash(state,error);
                if(hashes[side]==0) return false;
            }
            ++trajectoryChecks;
            if(hashes[0]!=hashes[1]) return fail("real per-tick trajectory/full authority hash at history="
                +std::to_string(history)+" tick="+std::to_string(n));
            for(int id:oldIds) {
                const auto* object=findObjectById(*scalar,id);
                if(!object || !object->alive) return fail("real movement fixture lost selected actor");
                if(runtimeCellPointForObject(*object)!=starts.at(id)) moved.insert(id);
                sawActiveMovement |= object->renderMoveActive;
            }
        }
        if(!sawActiveMovement || moved.size()<oldIds.size()*3/4)
            return fail("real command trajectory fixture did not actually move its formation");
        Authority oldState,newState;
        {RuntimeSnapshotActiveStateScope active(*scalar); if(!capture(*scalar,oldState,error)) return false;}
        {RuntimeSnapshotActiveStateScope active(*memo); if(!capture(*memo,newState,error)) return false;}
        if(!(oldState==newState)) return fail("real final full serialized authorities");
    }
    std::cout << "Planner passability memo: predicates=" << predicateChecks
        << " denseScalar/C12Checks=" << denseChecks
        << " visitedOriginalSetChecks=" << visitedChecks
        << " scalar/C12/C14dense/newKernelComponents=yes"
        << " completeComponent/nearest/best=" << plannerChecks << " actualOrderedQueries=" << observedQueries
        << " actualMemoHits=" << observedHits << " realCommands=" << commandChecks
        << " realPerTickAuthorityHashes=" << trajectoryChecks
        << " fullSerializedAuthorities=yes passed=yes\n";
    error.clear(); return true;
}
