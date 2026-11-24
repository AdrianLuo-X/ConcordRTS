bool runtimeParasiteObjectIsContained(
    const FixtureState& state,
    const WorldObjectInstance& object) {
    if (object.garrisonHostObjectId >= 0
        || !object.garrisonOccupantIds.empty()
        || !object.transportPassengerIds.empty()) {
        return true;
    }
    for (const WorldObjectInstance& candidate : state.world.getObjects()) {
        if (std::find(
                candidate.transportPassengerIds.begin(),
                candidate.transportPassengerIds.end(),
                object.instanceId
            ) != candidate.transportPassengerIds.end()) {
            return true;
        }
    }
    return false;
}

bool runtimeParasiteObjectHasExclusiveAuthority(
    const FixtureState& state,
    const WorldObjectInstance& object,
    bool asHost) {
    if (state.parasiteStunUntilTickByObject.isActive(object.instanceId, state.tick)
        || runtimeParasiteObjectIsContained(state, object)
        || object.dockedToObjectId >= 0 || object.dockIndex >= 0
        || state.pendingEngineerCaptures.find(object.instanceId)
            != state.pendingEngineerCaptures.end()
        || state.pendingOccupyTargets.find(object.instanceId)
            != state.pendingOccupyTargets.end()
        || state.pendingTransportTargets.find(object.instanceId)
            != state.pendingTransportTargets.end()
        || state.pendingTimedBombIntents.find(object.instanceId)
            != state.pendingTimedBombIntents.end()) {
        return true;
    }
    if (state.repairDepotsInitialized) {
        for (const RuntimeRepairDepotRelation& relation :
                state.repairDepots.relations()) {
            if (relation.unitId == object.instanceId
                || relation.hostId == object.instanceId) {
                return true;
            }
        }
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            state.factoryDeliveries.leases()) {
        if (lease.producedObjectId == object.instanceId
            || lease.hostId == object.instanceId) {
            return true;
        }
    }
    if (state.specialMechanismsInitialized
        && (state.specialMechanisms.isCarrierChild(object.instanceId)
            // The host stays in the world, so its aircraft can remain attached.
            // A parasite source would enter limbo and cannot own such children.
            || (!asHost && state.specialMechanisms.carrierParents().find(
                object.instanceId
            ) != state.specialMechanisms.carrierParents().end()))) {
        return true;
    }
    return false;
}

bool runtimeParasiteWarheadIsExclusive(
    const WeaponType& weapon,
    const WarheadType& warhead) {
    return weapon.limboLaunch && warhead.parasite
        && std::max(1, weapon.burst) == 1
        && !weapon.spawner
        && !weapon.sonic
        && !weapon.radBeam
        && !weapon.electricBolt
        && !weapon.magBeam
        && !warhead.psychedelic
        && !warhead.psychicDamage
        && !warhead.ivanBomb
        && !warhead.bombDisarm
        && !warhead.airstrike
        && !warhead.isLocomotor
        && !warhead.mindControl
        && !warhead.temporal
        && !warhead.radiation
        && !warhead.electricAssault
        && trimAsciiLocal(warhead.particle).empty()
        && trimAsciiLocal(warhead.particleSystem).empty();
}
