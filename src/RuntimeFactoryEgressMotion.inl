bool runtimeFactoryLandVehicleEligible(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type && !type->naval && !type->waterBound
        && runtimeObjectUsesVehicleArrival(state, object);
}

bool runtimeFactoryLandDeliveryOwnsEgress(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    if (!runtimeFactoryLandVehicleEligible(state, object)) return false;
    return std::any_of(state.factoryDeliveries.leases().begin(),
        state.factoryDeliveries.leases().end(), [&](const auto& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Land
                && lease.lane == RuntimeProductionLane::Vehicles;
        });
}

bool runtimeFactoryClearanceBehaviorIsValid(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    if (behavior.factoryExitClearanceHostId == -1) return true;
    return behavior.factoryExitClearanceHostId >= 0
        && runtimeFactoryLandVehicleEligible(state, object)
        && behavior.kind == RuntimeBehaviorKind::Move
        && behavior.targetObjectId == -1 && !behavior.forceFire
        && !behavior.attackGround && !behavior.attackBridge;
}

bool runtimeFactoryFastClearanceSource(const RuntimeWindowState& state,
    const WorldObjectInstance& object, int hostId) {
    if (hostId < 0 || !runtimeFactoryLandVehicleEligible(state, object)
        || !runtimeNoCooldownForOwner(state, object.owner)) return false;
    const auto* host = findObjectById(state, hostId);
    if (!host || !host->alive || host->health <= 0
        || host->kind != WorldObjectKind::Structure
        || runtimeCanonicalHumanHouseOwner(host->owner)
            != runtimeCanonicalHumanHouseOwner(object.owner)) return false;
    const auto* type = findRuntimeBuildingTypeById(state.rules, host->type);
    return type && type->weaponsFactory && !type->naval && !type->waterBound
        && toUpperAsciiLocal(type->factory) == "UNITTYPE";
}

bool runtimeFactoryFastClearanceMove(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    return behavior.factoryExitClearanceHostId >= 0
        && runtimeFactoryClearanceBehaviorIsValid(state, object, behavior)
        && runtimeFactoryFastClearanceSource(state, object,
            behavior.factoryExitClearanceHostId);
}

bool runtimeFactoryEgressBypassesArrival(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    // Retail ForceTrack 66 bypasses the ordinary destination slowdown. A
    // released car's fresh yield remains an ordinary Move unless fast build
    // explicitly grants it the short factory clearance clock below.
    return runtimeFactoryLandDeliveryOwnsEgress(state, object)
        || runtimeFactoryFastClearanceMove(state, object, behavior);
}

bool runtimeFactoryEgressUsesFastClock(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    if (runtimeFactoryFastClearanceMove(state, object, behavior)) return true;
    if (!runtimeFactoryLandDeliveryOwnsEgress(state, object)) return false;
    return std::any_of(state.factoryDeliveries.leases().begin(),
        state.factoryDeliveries.leases().end(), [&](const auto& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Land
                && lease.lane == RuntimeProductionLane::Vehicles
                && runtimeFactoryFastClearanceSource(state, object, lease.hostId);
        });
}
