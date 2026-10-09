int runtimeComputerTacticLane(const RuntimeBuildCatalogItem& item) {
    return item.kind == RuntimeBuildKind::Infantry ? 0
        : toUpperAsciiLocal(item.requiredFactory) == "AIRCRAFTTYPE" ? 2 : 1;
}

std::optional<RuntimeProductionSpawn> runtimeComputerTacticAircraftSpawn(
    const RuntimeWindowState& state, const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    RuntimeAircraftProductionOrder order;
    order.serial = 1; order.typeId = item.typeId;
    order.phase = RuntimeAircraftProductionOrderPhase::Prospective;
    const auto build = buildRuntimeAircraftProductionMatchInputForOwner(
        state, owner, order, nullptr, false);
    if (build.error) return std::nullopt;
    const auto match = runtimeMatchAircraftProductionDocks(build.input);
    if (!match.feasible) return std::nullopt;
    for (const auto& assignment : match.assignments) {
        if (assignment.orderSerial != order.serial) continue;
        const auto* host = findObjectById(state, assignment.hostId);
        const auto* rule = host ? findRuntimeBuildingTypeById(state.rules, host->type) : nullptr;
        if (host && rule && (!rule->powered || host->poweredOperational))
            return runtimeAircraftDockSpawn(state, *host, assignment.dockIndex);
    }
    return std::nullopt;
}

// Completion uses the computer's real factory and pad reservations. The
// caller owns payment/progress; this function never grants credits or units.
bool completeRuntimeComputerTacticUnit(RuntimeWindowState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item, int& producedId) {
    producedId = -1;
    if (runtimeComputerTacticLane(item) != 2)
        return runtimeComputerAiBuildUnit(state, ai, item, &producedId);
    refreshRuntimeAircraftDockAssignments(state);
    const auto spawn = runtimeComputerTacticAircraftSpawn(state, ai.owner, item);
    if (!spawn || !spawn->anchor) return false;
    const int hostId = spawn->anchor->instanceId;
    const int dockIndex = spawn->dockIndex;
    auto* aircraft = addRuntimeObjectInstanceForOwner(
        state, ai.owner, item.kind, item.typeId, spawn->cell);
    if (!aircraft) return false;
    producedId = aircraft->instanceId;
    applyRuntimeProducedObjectVeterancy(state, *aircraft);
    scaleRuntimeComputerAiObjectHealth(state, ai, *aircraft);
    if (const auto* host = findObjectById(state, hostId))
        applyRuntimeAircraftDockPose(state, *aircraft, *host, dockIndex);
    recordRuntimeCompletedBuild(state, ai.owner, item);
    ++ai.unitsBuilt;
    ai.lastBuildType = item.typeId;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

bool queueRuntimeComputerTacticProduction(RuntimeWindowState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item,
    int teamInstanceId = -1) {
    auto& tactics = state.triggers.computerTacticsByOwner[
        toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    auto& order = tactics.production[runtimeComputerTacticLane(item)];
    if (!order.typeId.empty() || ai.credits < item.cost
        || !runtimeComputerAiItemAllowed(state, ai, item)) return false;
    if (runtimeComputerTacticLane(item) == 2
        && !runtimeComputerTacticAircraftSpawn(state, ai.owner, item)) return false;
    order.teamInstanceId = teamInstanceId;
    order.typeId = item.typeId;
    order.remainingTicks = order.totalTicks = std::max(1, item.buildTicks);
    order.paidCost = item.cost;
    spendRuntimeComputerStoredOre(state, ai.owner, item.cost);
    ai.credits -= item.cost;
    ai.creditsSpent += item.cost;
    ++ai.productionSerial;
    ai.status = "building " + item.typeId;
    return true;
}

bool advanceRuntimeComputerTacticProduction(RuntimeWindowState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics) {
    bool changed = false;
    for (auto& order : tactics.production) {
        if (order.typeId.empty()) continue;
        const auto item = runtimeComputerAiBuildItem(state, order.typeId, ai.owner);
        if (!item) continue;
        // A destroyed/captured factory or lost prerequisite pauses a paid
        // order. Completion waits for a real exit instead of losing its unit.
        if (!runtimeComputerAiItemAllowed(state, ai, *item)) continue;
        const auto* anchor = runtimeComputerAiFactoryAnchor(state, ai, *item);
        const auto* building = anchor
            ? findRuntimeBuildingTypeById(state.rules, anchor->type) : nullptr;
        if (!anchor || !building || (building->powered && !anchor->poweredOperational)) continue;
        if (runtimeComputerTacticLane(*item) == 2
            && !runtimeComputerTacticAircraftSpawn(state, ai.owner, *item)) continue;
        if (order.remainingTicks > 0) { --order.remainingTicks; changed = true; }
        if (order.remainingTicks > 0) continue;
        int objectId = -1;
        if (!completeRuntimeComputerTacticUnit(state, ai, *item, objectId)) continue;
        const auto team = tactics.teams.find(order.teamInstanceId);
        if (team != tactics.teams.end()) {
            team->second.objectIds.push_back(objectId);
            if (team->second.launched) team->second.joiningObjectIds.push_back(objectId);
            const auto missing = std::find_if(team->second.missingMembers.begin(),
                team->second.missingMembers.end(), [&](const auto& type) {
                    return toUpperAsciiLocal(type) == toUpperAsciiLocal(order.typeId);
                });
            if (missing != team->second.missingMembers.end()) team->second.missingMembers.erase(missing);
            team->second.lastProgressTick = state.tick;
        }
        order = {};
        changed = true;
    }
    return changed;
}
