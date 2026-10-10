// Starting troops must share a legal route with their original mobile base.
// Keep this query local to one owner's formation; no saved or shared state.
class RuntimeSkirmishStartingConnectivity {
    const RuntimeWindowState& state_;
    std::unordered_map<int, const TerrainCell*> terrainByCell_;
    std::unordered_map<int, int> occupiedAtStart_;
    std::unordered_map<std::string, std::unordered_set<int>> reachableByType_;
    std::vector<RuntimeCellPoint> foundationSeeds_;
    RuntimeCellPoint mainCell_;
    int mainMcvId_ = -1;

    static std::string keyFor(const WorldObjectInstance& object) {
        return std::to_string(static_cast<int>(object.kind)) + ":"
            + toLowerAsciiLocal(trimAsciiLocal(object.type));
    }
    void prepare(RuntimeBuildKind kind, const UnitType& type,
        const std::string& owner) {
        WorldObjectInstance probe;
        probe.instanceId = -1;
        probe.kind = runtimeKindForBuildItem(kind);
        probe.owner = owner;
        probe.type = type.id;
        probe.alive = true;
        probe.health = std::max(1, type.strength);
        const std::string key = keyFor(probe);
        if (reachableByType_.contains(key)) return;
        // All inputs stay read-only until this type's traversal ends. These
        // existing query scopes avoid rescanning every world footprint per
        // map cell, and expire before the first actual formation spawn.
        RuntimeBuildingDepartureReadOnlyQueryScope departureQueries(state_, probe);
        RuntimeSubcellReadOnlyQueryScope subcellQueries(state_.world.getObjects(),
            probe.kind == WorldObjectKind::Infantry);
        RuntimePlannerCellPassabilityScope passabilityQueries(
            state_, probe, terrainByCell_, occupiedAtStart_);
        RuntimePlannerLandPermissionScope landPermissions(state_, probe);
        RuntimePlannerEmptyCellCensusScope emptyCells(state_);
        std::vector<RuntimeCellPoint> seeds;
        if (mainMcvId_ >= 0) for (const auto& cell : foundationSeeds_) {
            // Infantry cannot occupy the MCV's own whole ground cell. Other
            // original future-foundation cells are checked for real free slots.
            if (probe.kind == WorldObjectKind::Infantry && cell == mainCell_)
                continue;
            if (runtimeCellIsPassable(state_, terrainByCell_, occupiedAtStart_,
                    cell, probe, mainMcvId_)) seeds.push_back(cell);
        }
        // The reachable helper accepts seeds unconditionally; only the proven
        // legal seeds above may enter it. Its native traversal keeps diagonal
        // side blockers, terrain height changes, bridges and movement modes.
        reachableByType_.emplace(key, buildRuntimeReachableMoveCells(state_,
            probe, seeds, terrainByCell_, occupiedAtStart_, mainMcvId_));
    }
public:
    RuntimeSkirmishStartingConnectivity(const RuntimeWindowState& state,
        const std::string& owner, RuntimeCellPoint origin,
        const RuntimeSkirmishStartingUnitPools& pools,
        const std::vector<const UnitType*>& extraMcvs)
        : state_(state), mainCell_(origin) {
        terrainByCell_ = state.terrainByCell.empty()
            ? buildRuntimeTerrainLookup(state.world) : state.terrainByCell;
        // Capture before any formation spawn. New mobile actors may block a
        // birth cell, but must not redefine the pre-spawn terrain connectivity.
        occupiedAtStart_ = buildRuntimeOccupancyLookup(state);
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0
                || object.kind != WorldObjectKind::Unit
                || runtimeCellPointForObject(object) != origin
                || toLowerAsciiLocal(trimAsciiLocal(object.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(owner))) continue;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            if (!unit || !runtimeUnitTypeIsMcv(state, *unit)) continue;
            const auto yard = runtimeBuildItemForBuilding(state, unit->deploysInto);
            if (!yard) continue;
            mainMcvId_ = object.instanceId;
            const auto footprint = runtimeStructureFootprintForBuildItem(state, *yard);
            foundationSeeds_ = runtimeStructureFootprintCells(
                runtimePlacementTopLeftForAnchor(origin, footprint), footprint);
            break;
        }
        for (const auto* type : pools.infantry)
            if (type) prepare(RuntimeBuildKind::Infantry, *type, owner);
        for (const auto* type : pools.vehicles)
            if (type) prepare(RuntimeBuildKind::Unit, *type, owner);
        for (const auto* type : extraMcvs)
            if (type) prepare(RuntimeBuildKind::Unit, *type, owner);
    }
    std::optional<RuntimeCellPoint> findCellNear(const WorldObjectInstance& mover,
        RuntimeCellPoint desired, int maxRadiusCells,
        const std::unordered_set<int>* reservedCells = nullptr,
        int allowedOccupiedObjectId = -1) const {
        const auto connected = reachableByType_.find(keyFor(mover));
        if (connected == reachableByType_.end()) return std::nullopt;
        // Rebuild live occupancy for every birth, just as the original finder
        // does. Cached sets only veto disconnected candidate cells.
        const auto occupied = buildRuntimeOccupancyLookup(state_, mover.instanceId);
        const auto canUse = [&](RuntimeCellPoint cell) {
            const int key = runtimeCellKey(cell.x, cell.y);
            return key >= 0
                && (!reservedCells || !reservedCells->contains(key))
                && runtimeCellIsPassable(state_, terrainByCell_, occupied, cell,
                    mover, allowedOccupiedObjectId)
                && connected->second.contains(key);
        };
        if (canUse(desired)) return desired;
        for (int radius = 1; radius <= std::max(0, maxRadiusCells); ++radius)
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                    const RuntimeCellPoint candidate{desired.x + dx, desired.y + dy};
                    if (canUse(candidate)) return candidate;
                }
        return std::nullopt;
    }
};
