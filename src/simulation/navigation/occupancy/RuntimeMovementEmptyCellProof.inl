#include "simulation/navigation/occupancy/RuntimeGroundPoseOccupancyRuntime.inl"
#include "simulation/navigation/occupancy/RuntimeGroundPoseSpatial.inl"

// Include after the occupancy builder and before runtimeTickOccupancyLookup.
bool runtimeFrameScalingReferenceMovement() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

void captureRuntimeMovementEmptyCellProof(RuntimeWindowState& state) {
    auto& proof = state.movementEmptyCellProof;
    const auto& objects = state.world.getObjects();
    const auto& terrainObjects = state.world.getTerrainObjects();
    const auto& terrainCells = state.world.getTerrainCells();
    proof.subcellCompatible = true;
    proof.valid = state.tickCrushEmptyCellProofValid
        && state.magBeams.bindings().empty();
    proof.objectData = objects.data();
    proof.terrainObjectData = terrainObjects.data();
    proof.terrainCellData = terrainCells.data();
    proof.mapFactCellData = state.mapFacts.cells.data();
    proof.objectCount = objects.size();
    proof.terrainObjectCount = terrainObjects.size();
    proof.terrainCellCount = terrainCells.size();
    proof.mapFactCellCount = state.mapFacts.cells.size();
    proof.unindexedFoundations.clear();
    proof.subcellAirObjectIndices.clear();
    proof.additionalGroundPoseObjectIndices.clear();
    proof.additionalGroundPoseIndexReady=false;
    proof.allGroundBodyIndicesByCell.clear();
    proof.allGroundBodyIndexReady=true;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (!runtimeAppendAllGroundBodySpatialObject(state,index)) proof.allGroundBodyIndexReady=false;
        if (!object.alive) continue;
        if (runtimeGroundPoseIsSpatialCandidate(state,object))
            proof.additionalGroundPoseObjectIndices.push_back(index);
        // Navigation can omit airborne/docked Air objects. Their current
        // ground pose is checked again at query time, including same-tick landings.
        if (object.kind != WorldObjectKind::Structure
            && runtimeObjectMovementMode(state, object) == PathMovementMode::Air)
            proof.subcellAirObjectIndices.push_back(index);
        // Navigation omits id -1. Conservatively exclude all synthetic ids.
        if (object.instanceId < 0) proof.valid = false;
        if (object.kind != WorldObjectKind::Structure) continue;
        const auto footprint = runtimeStructureFootprintForObject(state, object);
        const std::int64_t width = std::max(1, footprint.width);
        const std::int64_t height = std::max(1, footprint.height);
        // The scalar hard-blocker predicate sees the whole foundation even
        // where vehicle navigation omits a drive row or an invisible building.
        if (runtimeInvisibleMapBuilding(&state.rules, object)
            || runtimeStructureBlockedDriveRowCount(state, object) < width) {
            proof.unindexedFoundations.push_back({object.x, object.y,
                static_cast<std::int64_t>(object.x) + width - 1,
                static_cast<std::int64_t>(object.y) + height - 1});
        }
    }
    if(!buildRuntimeGroundPoseSpatialIndex(state)) proof.valid=false;
    for (const auto& terrain : terrainObjects) {
        if (!runtimeTerrainObjectBlocksGround(terrain)) continue;
        const std::int64_t width = std::max(1, terrain.footprintWidth);
        const std::int64_t height = std::max(1, terrain.footprintHeight);
        if (width > 4096 || height > 4096
            || static_cast<std::int64_t>(terrain.x) + width - 1
                > std::numeric_limits<int>::max()
            || static_cast<std::int64_t>(terrain.y) + height - 1
                > std::numeric_limits<int>::max()) proof.valid = false;
    }
}

bool runtimeMovementTickProvesEmptyCell(
    const RuntimeWindowState& state, const RuntimeCellPoint& cell
) {
    const auto& proof = state.movementEmptyCellProof;
    const auto& objects = state.world.getObjects();
    const auto& terrainObjects = state.world.getTerrainObjects();
    const auto& terrainCells = state.world.getTerrainCells();
    if (!proof.valid || state.tickOccupancyCacheTick != state.tick
        || !state.magBeams.bindings().empty()
        || proof.objectData != objects.data() || proof.objectCount != objects.size()
        || proof.terrainObjectData != terrainObjects.data()
        || proof.terrainObjectCount != terrainObjects.size()
        || proof.terrainCellData != terrainCells.data()
        || proof.terrainCellCount != terrainCells.size()
        || proof.mapFactCellData != state.mapFacts.cells.data()
        || proof.mapFactCellCount != state.mapFacts.cells.size()
        // Keep unusual and legacy coordinate encodings on the scalar path.
        || cell.x < 0 || cell.y < 0 || cell.x >= 512 || cell.y >= 512) return false;
    const int key = runtimeCellKey(cell.x, cell.y);
    if (state.tickOccupiedByCell.contains(key)
        || state.tickGroundOccupantCountByCell.contains(key)) return false;
    if(!proof.additionalGroundPoseIndexReady
        || proof.additionalGroundPoseIndicesByCell.contains(key)) return false;
    for (const auto& bounds : proof.unindexedFoundations) {
        if (cell.x >= bounds[0] && cell.y >= bounds[1]
            && cell.x <= bounds[2] && cell.y <= bounds[3]) return false;
    }
    return true;
}
