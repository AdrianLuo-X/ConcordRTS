std::string runtimeTurretSoundForObject(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    if (object.kind == WorldObjectKind::Structure) {
        const auto* type = findRuntimeBuildingType(state.rules, object);
        return type && type->turret ? trimAsciiLocal(type->turretRotateSound) : std::string();
    }
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type && type->turret ? trimAsciiLocal(type->turretRotateSound) : std::string();
}

bool runtimeTurretSoundObjectPresent(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    return object.alive && object.health > 0 && !object.rubble && !runtimeGateObjectIsLimbo(object)
        && object.garrisonHostObjectId < 0
        && !(state.specialMechanismsInitialized && (state.specialMechanisms.isChronoInTransit(object.instanceId)
            || state.specialMechanisms.isCarrierChildDocked(object.instanceId)));
}

void queueRuntimeTurretSoundActivity(const RuntimeWindowState& state, WorldObjectInstance& object,
    const std::string& event, bool rotating) {
    if (event.empty()) return;
    if (object.pendingTurretSounds.size() >= 32) object.pendingTurretSounds.erase(object.pendingTurretSounds.begin());
    object.pendingTurretSounds.push_back({event, state.tick, object.x, object.y, object.level, rotating});
}

void markRuntimeTurretSoundRotation(const RuntimeWindowState& state, WorldObjectInstance& object) {
    if (!runtimeTurretSoundObjectPresent(state, object)) return;
    const auto event = runtimeTurretSoundForObject(state, object);
    if (event.empty()) return;
    if (object.activeTurretSoundEvent != event) {
        queueRuntimeTurretSoundActivity(state, object, object.activeTurretSoundEvent, false);
        object.activeTurretSoundEvent = event;
        queueRuntimeTurretSoundActivity(state, object, event, true);
    }
    object.turretSoundActivityTick = state.tick;
}

void finishRuntimeTurretSoundActivities(const RuntimeWindowState& state, GameWorld& world) {
    for (auto& object : world.accessObjects()) {
        if (object.activeTurretSoundEvent.empty()) continue;
        if (object.turretSoundActivityTick != state.tick || !runtimeTurretSoundObjectPresent(state, object)
            || runtimeTurretSoundForObject(state, object) != object.activeTurretSoundEvent) {
            queueRuntimeTurretSoundActivity(state, object, object.activeTurretSoundEvent, false);
            object.activeTurretSoundEvent.clear(); object.turretSoundActivityTick = -1;
        }
    }
}
