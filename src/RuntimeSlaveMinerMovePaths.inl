// Optional ordinary-save authority for completed slave-manager move routes.
// Older saves, and requests that are still pending, keep legacy reconstruction.
// Include after the native behavior record declarations and movement helpers'
// forward declarations, before the ordinary writer/materializer.
namespace {
constexpr std::size_t runtimeSlaveMinerSavedMoveMaximumNodes = 100000U;

bool runtimeSlaveMinerSavedMoveObjectIsEligible(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    if (!state.rulesLoaded || !object.alive || object.health <= 0
        || object.airborne || runtimeGateObjectIsLimbo(object)) return false;
    if (runtimeObjectIsMobileSlaveMiner(state, object)) return true;
    if (object.kind != WorldObjectKind::Infantry) return false;
    const auto* worker = findRuntimeUnitType(state.rules, object);
    const auto task = state.slaveMinerTasks.find(object.instanceId);
    if (!worker || !worker->slaved || task == state.slaveMinerTasks.end()
        || task->second.phase == RuntimeSlaveMinerPhase::Stored) return false;
    const auto* parent = findObjectById(state, task->second.refineryId);
    if (!parent || parent == &object || !parent->alive || parent->health <= 0
        || runtimeGateObjectIsLimbo(*parent)
        || runtimeCanonicalHumanHouseOwner(parent->owner)
            != runtimeCanonicalHumanHouseOwner(object.owner)) return false;
    const auto* building = runtimeSlaveMinerParentBuilding(state, *parent);
    if (!building || building->slavesNumber <= 0
        || toUpperAsciiLocal(trimAsciiLocal(building->enslaves))
            != toUpperAsciiLocal(trimAsciiLocal(object.type))) return false;
    const auto* mobile = parent->kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, *parent) : nullptr;
    return !mobile || mobile->enslaves.empty()
        || toUpperAsciiLocal(trimAsciiLocal(mobile->enslaves))
            == toUpperAsciiLocal(trimAsciiLocal(object.type));
}

bool runtimeSlaveMinerSavedMoveCellIsValid(
    const RuntimeWindowState& state, const RuntimeCellPoint& cell
) {
    // runtimeCellKey is a different indexing contract; bound before lookup.
    return cell.x >= 0 && cell.x < 512 && cell.y >= 0 && cell.y < 512
        && findRuntimeTerrainCellAt(state, cell) != nullptr;
}

bool runtimeSlaveMinerSavedMoveRouteIsValid(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    if (behavior.kind != RuntimeBehaviorKind::Move
        || behavior.pathRequestPending || behavior.path.empty()
        || behavior.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes
        || behavior.moveCooldownTicks < 0 || behavior.weaponCooldownTicks < 0
        || behavior.moveIntervalRemainderQ16 < -0x8000
        || behavior.moveIntervalRemainderQ16 > 0x7fff
        || behavior.blockedTicks < 0 || behavior.pathReplans < 0
        || behavior.nextRepathTick < 0
        || !runtimeSlaveMinerSavedMoveCellIsValid(state, behavior.moveTarget)
        || behavior.path.back() != behavior.moveTarget) return false;
    RuntimeCellPoint previous = runtimeCellPointForObject(object);
    if (!runtimeSlaveMinerSavedMoveCellIsValid(state, previous)) return false;
    if (behavior.moveSegmentFrom.x >= 0 || behavior.moveSegmentFrom.y >= 0) {
        const auto& from = behavior.moveSegmentFrom;
        if (behavior.moveCooldownTicks <= 0
            || !runtimeSlaveMinerSavedMoveCellIsValid(state, from)
            || from == previous || std::abs(from.x - previous.x) > 1
            || std::abs(from.y - previous.y) > 1) return false;
    } else if (behavior.moveSegmentFrom.x != -1 || behavior.moveSegmentFrom.y != -1) {
        return false;
    }
    if (behavior.renderMoveTicksRemaining > 0
        && (behavior.renderMoveTicksRemaining != behavior.moveCooldownTicks
            || behavior.renderMoveTicksTotal < behavior.renderMoveTicksRemaining
            || behavior.renderTo != previous
            || (behavior.moveSegmentFrom.x >= 0
                && behavior.renderFrom != behavior.moveSegmentFrom))) return false;
    for (const auto& cell : behavior.path) {
        if (!runtimeSlaveMinerSavedMoveCellIsValid(state, cell) || cell == previous
            || std::abs(cell.x - previous.x) > 1
            || std::abs(cell.y - previous.y) > 1) return false;
        previous = cell;
    }
    // Occupants can move after this route was planned. Normal movement retains
    // responsibility for its future occupancy, blocking and replanning.
    return true;
}

bool runtimeSlaveMinerSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary
) {
    return restored.kind == ordinary.kind
        && restored.moveTarget == ordinary.moveTarget
        && restored.targetObjectId == ordinary.targetObjectId
        && restored.moveCooldownTicks == ordinary.moveCooldownTicks
        && restored.weaponCooldownTicks == ordinary.weaponCooldownTicks
        && restored.moveIntervalRemainderQ16 == ordinary.moveIntervalRemainderQ16
        && restored.pendingAllowedOccupiedObjectId == ordinary.pendingAllowedOccupiedObjectId
        && restored.attackBridge == ordinary.attackBridge
        && restored.attackBridgeCell == ordinary.attackBridgeCell
        && restored.forceFire == ordinary.forceFire
        && restored.attackGround == ordinary.attackGround
        && restored.moveSegmentFrom == ordinary.moveSegmentFrom
        && restored.factoryExitClearanceHostId == ordinary.factoryExitClearanceHostId;
}
} // namespace

void writeRuntimeSlaveMinerMovePaths(
    const RuntimeWindowState& state, std::ostream& output
) {
    std::vector<int> ids;
    std::size_t nodes = 0;
    for (const auto& [id, behavior] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (!object || !runtimeSlaveMinerSavedMoveObjectIsEligible(state, *object)
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.pathRequestPending || behavior.path.empty()
            || behavior.path.back() != behavior.moveTarget) continue;
        // An OmniCrusher commits its logical destination after the approach.
        // Its existing VehicleMovePresentation section owns that saved segment.
        if (runtimeOmniCrushPendingSegment(state, *object, behavior)) continue;
        if (!runtimeSlaveMinerSavedMoveRouteIsValid(state, *object, behavior)
            || behavior.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes - nodes
            || ids.size() >= 100000U) {
            output.setstate(std::ios::failbit);
            return;
        }
        nodes += behavior.path.size();
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[SlaveMinerMovePaths]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t index = 0; index < ids.size(); ++index)
        output << index << '=' << runtimeMultiplayerSnapshotBehaviorRecord(
            ids[index], state.behaviors.at(ids[index])) << '\n';
}

bool restoreRuntimeSlaveMinerMovePaths(
    RuntimeWindowState& state, const IniFile& ini, std::string& error
) {
    const auto* section = ini.getSection("SlaveMinerMovePaths");
    if (!section) return true;
    const auto fail = [&](int id, const char* reason) {
        error = "invalid slave miner move path"
            + (id > 0 ? " #" + std::to_string(id) : std::string{}) + ": " + reason;
        return false;
    };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue("SlaveMinerMovePaths", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("SlaveMinerMovePaths", "Count", ""), count)
        || count < 0 || count > 100000
        || static_cast<std::size_t>(count) > state.behaviors.size()) return fail(-1, "header");
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *section) {
        (void)value;
        const auto normalized = toLowerAsciiLocal(trimAsciiLocal(key));
        if (!keys.insert(normalized).second) return fail(-1, "duplicate key");
        if (normalized == "schema" || normalized == "count") continue;
        int index = -1;
        if (!parseRuntimeInt(normalized, index) || index < 0 || index >= count
            || normalized != std::to_string(index)) return fail(-1, "record index");
    }
    if (keys.size() != static_cast<std::size_t>(count) + 2U) return fail(-1, "record count");
    std::unordered_set<int> seen;
    std::vector<std::pair<int, RuntimeObjectBehavior>> staged;
    staged.reserve(static_cast<std::size_t>(count));
    std::size_t nodes = 0;
    for (int index = 0; index < count; ++index) {
        const auto record = ini.getValue("SlaveMinerMovePaths", std::to_string(index), "");
        const auto fields = splitRuntimeSaveFields(record);
        int expectedId = -1, objectId = -1;
        if (fields.empty() || !parseRuntimeInt(fields[0], expectedId)
            || expectedId <= 0 || !seen.insert(expectedId).second) return fail(expectedId, "object ID");
        const auto ordinary = state.behaviors.find(expectedId);
        const auto* object = findObjectById(state, expectedId);
        if (!object || ordinary == state.behaviors.end()
            || ordinary->second.kind != RuntimeBehaviorKind::Move
            || !runtimeSlaveMinerSavedMoveObjectIsEligible(state, *object))
            return fail(expectedId, "worker/home/ordinary move authority");
        // The parser changes native authority only. Seed it with the already
        // validated walk/arrival presentation rather than clearing that state.
        RuntimeObjectBehavior restored = ordinary->second;
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(record, objectId, restored)
            || objectId != expectedId) return fail(expectedId, "native behavior record");
        if (!runtimeSlaveMinerSavedMoveMatchesOrdinary(restored, ordinary->second))
            return fail(expectedId, "ordinary authority differs");
        if (!runtimeSlaveMinerSavedMoveRouteIsValid(state, *object, restored))
            return fail(expectedId, "route/segment/goal geometry");
        if (restored.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes - nodes)
            return fail(expectedId, "total node budget");
        nodes += restored.path.size();
        staged.emplace_back(expectedId, std::move(restored));
    }
    // Validate the complete optional section before altering any staged state.
    for (auto& [id, behavior] : staged) state.behaviors.at(id) = std::move(behavior);
    return true;
}
