// Ordinary saves retain the ready route of an in-flight Kirov. Its physical
// terminal-segment braking reads this deque before navigation runs next tick.
// Older saves keep their existing route reconstruction; no flight is advanced.
namespace {
constexpr std::size_t runtimeJumpjetSavedMoveMaximumNodes = 100000U;

bool runtimeJumpjetSavedMoveObjectIsEligible(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight
) {
    return state.rulesLoaded && object.alive && object.health > 0 && object.airborne
        && object.factoryExitHostId < 0
        && toUpperAsciiLocal(trimAsciiLocal(object.type)) == "ZEP"
        && runtimeJumpjetProfile(state, object) && (flight.active || flight.stopped);
}

bool runtimeJumpjetSavedMoveCellIsValid(
    const RuntimeWindowState& state, const RuntimeCellPoint& cell
) {
    return cell.x >= 0 && cell.x < 512 && cell.y >= 0 && cell.y < 512
        && findRuntimeTerrainCellAt(state, cell) != nullptr;
}

bool runtimeJumpjetSavedPathKindIsEligible(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior
) {
    if (behavior.kind == RuntimeBehaviorKind::Move) return true;
    if (behavior.kind != RuntimeBehaviorKind::Attack || behavior.attackGround
        || behavior.attackBridge || behavior.targetObjectId < 0) return false;
    const auto* target = findObjectById(state, behavior.targetObjectId);
    return target && runtimeKirovBuildingBombProfile(object, *target,
            runtimeWeaponProfileForObject(state, object, target, behavior.forceFire))
        && behavior.moveTarget == runtimeTargetCellForProjectile(state, *target);
}
bool runtimeJumpjetSavedMoveRouteIsValid(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeJumpjetFlight& flight, const RuntimeObjectBehavior& behavior
) {
    if (!runtimeJumpjetSavedPathKindIsEligible(state, object, behavior)
        || behavior.pathRequestPending
        || behavior.state == RuntimeBehaviorState::Idle
        || behavior.state == RuntimeBehaviorState::Complete
        || behavior.path.size() > runtimeJumpjetSavedMoveMaximumNodes
        || behavior.moveCooldownTicks <= 0 || behavior.weaponCooldownTicks < 0
        || behavior.moveIntervalRemainderQ16 < -0x8000
        || behavior.moveIntervalRemainderQ16 > 0x7fff
        || behavior.blockedTicks < 0 || behavior.pathReplans < 0
        || behavior.nextRepathTick < 0 || behavior.approachSlotIndex < 0
        || !std::isfinite(behavior.pendingRangeCells) || behavior.pendingRangeCells < 0.0
        || behavior.factoryExitClearanceHostId >= 0
        || behavior.attackBridge || behavior.attackGround
        || flight.lastTick > state.tick || flight.verticalTick > state.tick
        || flight.clearanceTick > state.tick
        || flight.length <= 0 || flight.progress >= flight.length
        || object.x != flight.toX || object.y != flight.toY
        || behavior.moveSegmentFrom != RuntimeCellPoint{flight.fromX, flight.fromY}
        || !runtimeJumpjetSavedMoveCellIsValid(state, behavior.moveTarget)) return false;
    RuntimeCellPoint previous = runtimeCellPointForObject(object);
    if (!runtimeJumpjetSavedMoveCellIsValid(state, previous)
        || !runtimeJumpjetSavedMoveCellIsValid(state, behavior.moveSegmentFrom)) return false;
    for (const auto& cell : behavior.path) {
        if (!runtimeJumpjetSavedMoveCellIsValid(state, cell) || cell == previous
            || std::abs(cell.x - previous.x) > 1
            || std::abs(cell.y - previous.y) > 1) return false;
        previous = cell;
    }
    // Empty is meaningful: the saved segment really may be terminal or waiting
    // for a new route. Future occupancy is checked by normal air navigation.
    return behavior.path.empty() || behavior.path.back() == behavior.moveTarget;
}

bool runtimeJumpjetSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary
) {
    return restored.kind == ordinary.kind && restored.moveTarget == ordinary.moveTarget
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

void writeRuntimeJumpjetMovePaths(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<int> ids;
    std::size_t nodes = 0;
    for (const auto& [id, behavior] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        const auto flight = state.jumpjetFlights.find(id);
        if (!object || flight == state.jumpjetFlights.end()
            || !runtimeJumpjetSavedMoveObjectIsEligible(state, *object, flight->second)
            || !runtimeJumpjetSavedPathKindIsEligible(state, *object, behavior)
            || behavior.pathRequestPending
            || behavior.state == RuntimeBehaviorState::Idle
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        if (!runtimeJumpjetSavedMoveRouteIsValid(state, *object, flight->second, behavior)
            || behavior.path.size() > runtimeJumpjetSavedMoveMaximumNodes - nodes
            || ids.size() >= 100000U) {
            output.setstate(std::ios::failbit);
            return;
        }
        nodes += behavior.path.size();
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[JumpjetMovePaths]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t index = 0; index < ids.size(); ++index)
        output << index << '=' << runtimeMultiplayerSnapshotBehaviorRecord(
            ids[index], state.behaviors.at(ids[index])) << '\n';
}

bool restoreRuntimeJumpjetMovePaths(
    RuntimeWindowState& state, const IniFile& ini, std::string& error
) {
    const auto* section = ini.getSection("JumpjetMovePaths");
    if (!section) return true;
    const auto fail = [&](int id, const char* reason) {
        error = "invalid Jumpjet move path"
            + (id > 0 ? " #" + std::to_string(id) : std::string{}) + ": " + reason;
        return false;
    };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue("JumpjetMovePaths", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("JumpjetMovePaths", "Count", ""), count)
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
    RuntimeJumpjetFlights flights;
    if (count > 0) {
        std::string payload;
        const auto flightSchema = ini.getValue("JumpjetFlights", "Schema", "");
        if ((flightSchema != "1" && flightSchema != "2" && flightSchema != "3"
                && flightSchema != "4" && flightSchema != "5")
            || !iniHexDecode(ini.getValue("JumpjetFlights", "DataHex", ""), payload,
                16U * 1024U * 1024U)
            || !payload.starts_with("JumpjetFlights " + flightSchema + " ")
            || !deserializeRuntimeJumpjetFlights(payload, flights)) return fail(-1, "flight authority");
    }
    std::unordered_set<int> seen;
    std::vector<std::pair<int, RuntimeObjectBehavior>> staged;
    staged.reserve(static_cast<std::size_t>(count));
    std::size_t nodes = 0;
    for (int index = 0; index < count; ++index) {
        const auto record = ini.getValue("JumpjetMovePaths", std::to_string(index), "");
        const auto fields = splitRuntimeSaveFields(record);
        int expectedId = -1, objectId = -1;
        if (record.size() > 16U * 1024U * 1024U || fields.empty()
            || !parseRuntimeInt(fields[0], expectedId) || expectedId <= 0
            || !seen.insert(expectedId).second) return fail(expectedId, "object ID/record size");
        const auto ordinary = state.behaviors.find(expectedId);
        const auto* object = findObjectById(state, expectedId);
        const auto flight = flights.find(expectedId);
        if (!object || ordinary == state.behaviors.end() || flight == flights.end()
            || !runtimeJumpjetSavedPathKindIsEligible(state, *object, ordinary->second)
            || !runtimeJumpjetSavedMoveObjectIsEligible(state, *object, flight->second))
            return fail(expectedId, "Kirov/flight/ordinary move authority");
        RuntimeObjectBehavior restored = ordinary->second;
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(record, objectId, restored)
            || objectId != expectedId) return fail(expectedId, "native behavior record");
        if (!runtimeJumpjetSavedMoveMatchesOrdinary(restored, ordinary->second))
            return fail(expectedId, "ordinary authority differs");
        if (!runtimeJumpjetSavedMoveRouteIsValid(state, *object, flight->second, restored))
            return fail(expectedId, "route/segment/goal/clock geometry");
        if (restored.path.size() > runtimeJumpjetSavedMoveMaximumNodes - nodes)
            return fail(expectedId, "total node budget");
        nodes += restored.path.size();
        staged.emplace_back(expectedId, std::move(restored));
    }
    for (auto& [id, behavior] : staged) state.behaviors.at(id) = std::move(behavior);
    return true;
}
