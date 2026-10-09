// Each completed barracks order creates an obligation. A busy vat delays
// delivery without discarding that order or charging for another unit.
bool runtimeCloneDeliveryOwnsDoorway(
    const RuntimeWindowState& state, int objectId
) {
    const auto* clone = findObjectById(state, objectId);
    if (!clone || !clone->alive || clone->health <= 0
        || clone->kind != WorldObjectKind::Infantry) return false;
    const auto owner = runtimeRepairDepotCanonicalOwnerId(state, clone->owner);
    for (const auto& host : state.world.getObjects()) {
        if (host.specialCloneDeliveryObjectId != objectId
            || !host.alive || host.health <= 0 || host.rubble
            || host.kind != WorldObjectKind::Structure) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, host.type);
        if (building && building->cloning
            && runtimeRepairDepotCanonicalOwnerId(state, host.owner) == owner)
            return true;
    }
    return false;
}

bool runtimePendingCloneDeliveryEligible(
    const RuntimeWindowState& state, const RuntimeSpecialBuildingClone& request
) {
    const auto* host = findObjectById(state, request.sourceObjectId);
    const auto* building = host ? findRuntimeBuildingTypeById(state.rules, host->type) : nullptr;
    const auto context = makeRuntimeSpecialBuildingContext(state);
    return host && building && building->cloning
        && host->kind == WorldObjectKind::Structure && host->alive
        && host->health > 0 && !host->rubble
        && state.rules.findUnitType("InfantryType", request.infantryTypeId)
        && !trimAsciiLocal(request.owner).empty()
        && (!context.ownerIsNeutral || !context.ownerIsNeutral(request.owner))
        && runtimeRepairDepotCanonicalOwnerId(state, host->owner)
            == runtimeRepairDepotCanonicalOwnerId(state, request.owner);
}

std::uint64_t runtimeClonePendingDeliveriesHash(const RuntimeWindowState& state) {
    std::uint64_t hash = 1469598103934665603ULL;
    runtimeStableHashMixString(hash, "ClonePendingDeliveries/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.pendingCloneDeliveries.size()));
    for (const auto& request : state.pendingCloneDeliveries) {
        runtimeStableHashMix(hash, request.sourceObjectId);
        runtimeStableHashMixString(hash, runtimeRepairDepotCanonicalOwnerId(state, request.owner));
        runtimeStableHashMixString(hash, toUpperAsciiLocal(trimAsciiLocal(request.infantryTypeId)));
    }
    return hash;
}

std::vector<int> advanceRuntimePendingCloneDeliveries(
    RuntimeWindowState& state, bool leasesAlreadyReconciled = false
) {
    if (state.pendingCloneDeliveries.empty()) return {};
    if (!leasesAlreadyReconciled) (void)reconcileRuntimeCloneDeliveryLeases(state);
    auto requests = std::move(state.pendingCloneDeliveries);
    state.pendingCloneDeliveries.clear();
    std::vector<int> cloneIds;
    std::set<int> blockedHosts;
    for (auto& request : requests) {
        // Sale, destruction, or a change of owner cancels that vat's debt.
        if (!runtimePendingCloneDeliveryEligible(state, request)) continue;
        const auto* host = findObjectById(state, request.sourceObjectId);
        if (blockedHosts.contains(request.sourceObjectId)
            || host->specialCloneDeliveryObjectId >= 0
            || host->factoryExitAnimationEndTick > state.tick) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        const auto spawn = runtimeSpecialBuildingInfantryExitCell(
            state, *host, request.owner, request.infantryTypeId);
        if (!spawn) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        auto* clone = addRuntimeObjectInstanceForOwner(state, request.owner,
            RuntimeBuildKind::Infantry, request.infantryTypeId, *spawn,
            RuntimeProductionLane::Infantry);
        if (!clone) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        applyRuntimeProducedObjectVeterancy(state, *clone);
        recordRuntimeMatchBuild(state, clone->owner);
        const int cloneId = clone->instanceId;
        cloneIds.push_back(cloneId);
        auto* liveClone = findMutableObjectById(state, cloneId);
        auto* liveHost = findMutableObjectById(state, request.sourceObjectId);
        if (liveClone && liveHost) {
            liveHost->specialCloneDeliveryObjectId = cloneId;
            beginRuntimeFactoryExitAnimation(state, *liveHost, *liveClone);
            if (liveHost->factoryExitAnimationEndTick <= state.tick) {
                liveHost->factoryExitAnimationStartTick = state.tick;
                liveHost->factoryExitAnimationEndTick = state.tick
                    + std::max(1, state.config.fixedTickHz * 2);
                liveHost->factoryExitAirborne = false;
            }
            queueRuntimeProductionExitMove(state, *liveClone, *liveHost);
            const auto* building = findRuntimeBuildingTypeById(state.rules, liveHost->type);
            if (building && !trimAsciiLocal(building->createUnitSound).empty())
                emitRuntimeSound(state, "sound:" + trimAsciiLocal(building->createUnitSound));
        }
        blockedHosts.insert(request.sourceObjectId);
    }
    return cloneIds;
}

std::vector<int> cloneRuntimeProducedInfantry(
    RuntimeWindowState& state, const std::string& owner,
    const std::string& infantryTypeId, int producingFactoryObjectId = -1
) {
    const auto requests = RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
        makeRuntimeSpecialBuildingContext(state), owner, infantryTypeId,
        producingFactoryObjectId, state.tick);
    state.pendingCloneDeliveries.insert(state.pendingCloneDeliveries.end(),
        requests.begin(), requests.end());
    return advanceRuntimePendingCloneDeliveries(state);
}
