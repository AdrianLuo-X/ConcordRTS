// These qualifications are constant only inside one path snapshot builder.
// Nothing is retained across ticks, movers, or world/behavior mutations.
class RuntimePathTerrainQueryScope {
public:
    RuntimePathTerrainQueryScope(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, int allowedObjectId,
        const std::unordered_set<int>& planningGateIds, bool usesCrushingMovement,
        bool includeRepairReservations)
        : state_(state), mover_(mover), allowedObjectId_(allowedObjectId),
          planningGateIds_(planningGateIds), usesCrushingMovement_(usesCrushingMovement),
          reference_(runtimeSimulationQueryReuseReference()) {
        if (reference_) return;
        const auto* allowed = allowedObjectId >= 0 ? findObjectById(state, allowedObjectId) : nullptr;
        if (allowed && allowed->kind == WorldObjectKind::Structure) {
            allowedObject_ = allowed;
            allowedCells_ = runtimeStructureFootprintCellsForObject(state, *allowed);
            const auto* building = findRuntimeBuildingTypeById(state.rules, allowed->type);
            allowedUsesDriveRows_ = building && building->numberImpassableRows > 0
                && runtimeObjectMayUseStructureDriveRows(state, mover);
            if (!allowedUsesDriveRows_) {
                allowedCellKeys_.reserve(allowedCells_.size());
                for (const auto& cell : allowedCells_) allowedCellKeys_.insert(exactCellKey(cell));
            }
        }
        if (includeRepairReservations) {
            repairCellLayers_.reserve(state.repairDepots.relations().size());
            for (const auto& relation : state.repairDepots.relations()) {
                if (relation.unitId == mover.instanceId) continue;
                const auto& reserved = relation.phase == RuntimeRepairDepotRelationPhase::Queued
                    ? relation.queueCell : relation.dockCell;
                if (reserved.x < 0 || reserved.y < 0) continue;
                repairCellLayers_[exactCellKey({reserved.x, reserved.y})]
                    |= static_cast<unsigned char>(reserved.onBridge ? 2 : 1);
            }
        }
    }

    bool repairReserved(const RuntimeCellPoint& cell) const {
        if (reference_) return runtimeCellReservedByOtherRepairUnit(state_, cell, mover_.instanceId);
        const auto reserved = repairCellLayers_.find(exactCellKey(cell));
        if (reserved == repairCellLayers_.end()) return false;
        const unsigned char layer = runtimeCellIsBridge(state_, cell.x, cell.y) ? 2 : 1;
        return (reserved->second & layer) != 0;
    }

    bool allowedFootprint(const RuntimeCellPoint& cell) const {
        if (reference_) return runtimeCellIsAllowedOccupiedStructureFootprint(
            state_, cell, allowedObjectId_, &mover_);
        if (!allowedObject_) return false;
        // Drive rows use the original geometric predicate, including its
        // special meaning for an invisible building with authored drive rows.
        return allowedUsesDriveRows_
            ? runtimeStructureCellIsVehicleDriveRow(state_, *allowedObject_, cell, mover_)
            : allowedCellKeys_.contains(exactCellKey(cell));
    }

    const std::vector<RuntimeCellPoint>& allowedCells() const { return allowedCells_; }

    bool foundationBlocks(const RuntimeCellPoint& cell,
        const std::vector<std::size_t>& foundationIndices, bool gateCell) const {
        for (const auto index : foundationIndices) {
            const auto& building = state_.world.getObjects()[index];
            if (!building.alive || building.instanceId == mover_.instanceId
                || (gateCell && planningGateIds_.contains(building.instanceId))) continue;
            if (runtimeMissionIsMcvDeployPending(building)) return true;
            if (building.instanceId == allowedObjectId_ && allowedFootprint(cell)) continue;
            if (usesCrushingMovement_
                && runtimeCrusherCanCrushObject(state_, mover_, building, cell)) continue;
            return true;
        }
        return false;
    }

    static std::uint64_t exactCellKey(const RuntimeCellPoint& cell) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 32U)
            | static_cast<std::uint32_t>(cell.y);
    }

private:
    const RuntimeWindowState& state_;
    const WorldObjectInstance& mover_;
    int allowedObjectId_;
    const std::unordered_set<int>& planningGateIds_;
    bool usesCrushingMovement_;
    bool reference_;
    const WorldObjectInstance* allowedObject_ = nullptr;
    bool allowedUsesDriveRows_ = false;
    std::vector<RuntimeCellPoint> allowedCells_;
    std::unordered_set<std::uint64_t> allowedCellKeys_;
    std::unordered_map<std::uint64_t, unsigned char> repairCellLayers_;
};

bool runtimePathSnapshotHasCellIndex(const RuntimePathTerrainSnapshotCache& cache,
    const std::vector<PathTerrainCell>& snapshot) {
    // Legacy fixtures may author staticCells directly. Fall back to the old
    // scan if the static builder has not generated its coordinate index.
    return cache.staticNextIndexByCell.size() == snapshot.size()
        && !cache.staticFirstIndexByCell.empty();
}

void applyRuntimePathFoundationConstraints(const RuntimeWindowState& state,
    const WorldObjectInstance& mover, int allowedObjectId, bool usesCrushingMovement,
    const RuntimePathTerrainQueryScope& query, std::vector<PathTerrainCell>& snapshot) {
    const auto& cache = state.pathTerrainCache;
    if (runtimeSimulationQueryReuseReference() || !runtimePathSnapshotHasCellIndex(cache, snapshot)) {
        for (auto& cell : snapshot) {
            if (cell.passable && runtimeFoundationBlocksMovement(state, mover,
                {cell.cell.x, cell.cell.y}, allowedObjectId, usesCrushingMovement,
                RuntimeGatePassageMode::Planning)) cell.passable = false;
        }
        return;
    }
    // The scalar path never initializes the foundation cache when all cells
    // are blocked. Preserve that cache lifetime as well as the boolean result.
    if (std::none_of(snapshot.begin(), snapshot.end(), [](const PathTerrainCell& cell) {
            return cell.passable;
        })) return;
    // Use exactly the original cache and its object offsets; do not rescan or
    // apply different alive/health/transport eligibility to its contents.
    const auto& foundations = runtimeSolidFoundationsByCell(state);
    struct Candidate {
        std::size_t snapshotIndex;
        const std::vector<std::size_t>* foundationIndices;
        bool gateCell;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(std::min(snapshot.size(), foundations.size()));
    for (const auto& [key, indices] : foundations) {
        const auto first = cache.staticFirstIndexByCell.find(key);
        if (first == cache.staticFirstIndexByCell.end()) continue;
        const bool gateCell = state.gateIdsByCell.contains(key);
        for (auto index = first->second; index != std::numeric_limits<std::size_t>::max();
                index = cache.staticNextIndexByCell[index])
            candidates.push_back({index, &indices, gateCell});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.snapshotIndex < b.snapshotIndex;
    });
    for (const auto& candidate : candidates) {
        auto& cell = snapshot[candidate.snapshotIndex];
        if (cell.passable && query.foundationBlocks({cell.cell.x, cell.cell.y},
            *candidate.foundationIndices, candidate.gateCell)) cell.passable = false;
    }
}

template<class UnexpectedOccupant>
bool restoreRuntimeAllowedPathFoundation(const RuntimeWindowState& state,
    const RuntimePathTerrainQueryScope& query, PathMovementMode movementMode,
    UnexpectedOccupant&& unexpectedOccupant, std::vector<PathTerrainCell>& snapshot) {
    const auto& cache = state.pathTerrainCache;
    if (runtimeSimulationQueryReuseReference() || !runtimePathSnapshotHasCellIndex(cache, snapshot))
        return false;
    std::vector<std::size_t> candidates;
    for (const auto& allowedCell : query.allowedCells()) {
        const auto first = cache.staticFirstIndexByCell.find(runtimeCellKey(allowedCell.x, allowedCell.y));
        if (first == cache.staticFirstIndexByCell.end()) continue;
        for (auto index = first->second; index != std::numeric_limits<std::size_t>::max();
                index = cache.staticNextIndexByCell[index]) {
            if (snapshot[index].cell.x == allowedCell.x && snapshot[index].cell.y == allowedCell.y)
                candidates.push_back(index);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const auto index : candidates) {
        auto& cell = snapshot[index];
        const RuntimeCellPoint point{cell.cell.x, cell.cell.y};
        if (query.allowedFootprint(point)
            && (movementMode == PathMovementMode::Air || !query.repairReserved(point)))
            cell.passable = movementMode == PathMovementMode::Air || !unexpectedOccupant(point);
    }
    return true;
}
