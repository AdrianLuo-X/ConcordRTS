// The snapshot builder asks these two boolean predicates repeatedly while the
// world is constant. Their filters intentionally differ from navigation's
// representative occupant map (transport, bridge layer and hidden foundations).
// Keep their exact scalar meanings and lifetime local to one snapshot build.
class RuntimePathSnapshotOccupancy {
public:
    RuntimePathSnapshotOccupancy(
        const RuntimeWindowState& state,
        const std::vector<PathTerrainCell>& snapshot,
        int moverObjectId,
        int allowedOccupiedObjectId,
        const std::unordered_set<int>& planningGateIds,
        bool includeHardBlockers,
        bool includeUnexpectedOccupants,
        const RuntimePathDynamicPreparationBounds* provenBounds = nullptr
    ) {
        if (snapshot.empty()
            || (!includeHardBlockers && !includeUnexpectedOccupants)) return;
        int minX = snapshot.front().cell.x, maxX = minX;
        int minY = snapshot.front().cell.y, maxY = minY;
        if (provenBounds != nullptr) {
            minX = provenBounds->minimumX; maxX = provenBounds->maximumX;
            minY = provenBounds->minimumY; maxY = provenBounds->maximumY;
        } else {
            for (const PathTerrainCell& cell : snapshot) {
                minX = std::min(minX, cell.cell.x);
                maxX = std::max(maxX, cell.cell.x);
                minY = std::min(minY, cell.cell.y);
                maxY = std::max(maxY, cell.cell.y);
            }
        }
        flagsByCell_.reserve(std::min(snapshot.size(),
            state.world.getObjects().size() + state.world.getTerrainObjects().size()));
        const auto addRectangle = [&](int x, int y, int width, int height,
                                      unsigned char flags) {
            const std::int64_t firstX = std::max<std::int64_t>(minX, x);
            const std::int64_t firstY = std::max<std::int64_t>(minY, y);
            const std::int64_t lastX = std::min<std::int64_t>(maxX,
                static_cast<std::int64_t>(x) + std::max(1, width) - 1);
            const std::int64_t lastY = std::min<std::int64_t>(maxY,
                static_cast<std::int64_t>(y) + std::max(1, height) - 1);
            if (firstX > lastX || firstY > lastY) return;
            const std::uint64_t columns = lastX - firstX + 1;
            const std::uint64_t rows = lastY - firstY + 1;
            const auto add = [&](int cellX, int cellY) {
                flagsByCell_[runtimeCrushCandidateCellKey(cellX, cellY)] |= flags;
            };
            // Bound unusual foundations and sparse maps by actual query count.
            if (columns > static_cast<std::uint64_t>(snapshot.size()) / rows) {
                for (const PathTerrainCell& cell : snapshot) {
                    if (cell.cell.x >= firstX && cell.cell.x <= lastX
                        && cell.cell.y >= firstY && cell.cell.y <= lastY) {
                        add(cell.cell.x, cell.cell.y);
                    }
                }
            } else {
                for (std::int64_t cellY = firstY; cellY <= lastY; ++cellY) {
                    for (std::int64_t cellX = firstX; cellX <= lastX; ++cellX) {
                        add(static_cast<int>(cellX), static_cast<int>(cellY));
                    }
                }
            }
        };
        const unsigned char terrainFlags =
            (includeHardBlockers ? HardBlocker : 0)
            | (includeUnexpectedOccupants ? UnexpectedOccupant : 0);
        for (const WorldTerrainObject& object : state.world.getTerrainObjects()) {
            if (!runtimeTerrainObjectBlocksGround(object)) continue;
            // The scalar footprint generator uses int coordinates. Retain its
            // result for authored rectangles whose endpoint would overflow int.
            const std::int64_t endX = static_cast<std::int64_t>(object.x)
                + std::max(1, object.footprintWidth) - 1;
            const std::int64_t endY = static_cast<std::int64_t>(object.y)
                + std::max(1, object.footprintHeight) - 1;
            if (endX > std::numeric_limits<int>::max()
                || endY > std::numeric_limits<int>::max()) {
                scalarTerrainFallback_ = true;
                continue;
            }
            addRectangle(object.x, object.y, object.footprintWidth,
                object.footprintHeight, terrainFlags);
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive || object.instanceId == moverObjectId
                || object.instanceId == allowedOccupiedObjectId) continue;
            const bool hard = includeHardBlockers
                && object.kind != WorldObjectKind::Infantry
                && !planningGateIds.contains(object.instanceId);
            if (!hard && !includeUnexpectedOccupants) continue;
            if (runtimeObjectOffGroundByMagBeam(state, object.instanceId)
                || (object.kind != WorldObjectKind::Structure
                    && runtimeObjectMovementMode(state, object)
                        == PathMovementMode::Air)) continue;
            const unsigned char flags = (hard ? HardBlocker : 0)
                | (includeUnexpectedOccupants ? UnexpectedOccupant : 0);
            if (object.kind == WorldObjectKind::Structure) {
                const auto footprint = runtimeStructureFootprintForObject(state, object);
                const auto origin = runtimeCellPointForObject(object);
                addRectangle(origin.x, origin.y, footprint.width, footprint.height, flags);
            } else {
                addRectangle(object.x, object.y, 1, 1, flags);
            }
        }
    }

    bool needsScalarTerrainFallback() const { return scalarTerrainFallback_; }
    bool hardBlocker(const RuntimeCellPoint& cell) const {
        return hasFlag(cell, HardBlocker);
    }
    bool unexpectedOccupant(const RuntimeCellPoint& cell) const {
        return hasFlag(cell, UnexpectedOccupant);
    }

private:
    static constexpr unsigned char HardBlocker = 1;
    static constexpr unsigned char UnexpectedOccupant = 2;
    bool hasFlag(const RuntimeCellPoint& cell, unsigned char flag) const {
        const auto it = flagsByCell_.find(runtimeCrushCandidateCellKey(cell.x, cell.y));
        return it != flagsByCell_.end() && (it->second & flag) != 0;
    }
    std::unordered_map<std::uint64_t, unsigned char> flagsByCell_;
    bool scalarTerrainFallback_ = false;
};

bool runtimePathSnapshotReferenceOccupancy() {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return useReference;
}
