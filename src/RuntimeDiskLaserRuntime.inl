bool syncRuntimeDiskLaserCharge(RuntimeWindowState& state, RuntimeProjectileEffect& shot) {
    const auto* owner = findObjectById(state, shot.attackerObjectId);
    const auto* target = findObjectById(state, shot.targetObjectId);
    // Cell force-fire has a saved endpoint, but no live world object. Keep
    // charging against that cell; a lost positive-id unit must still cancel.
    std::optional<WorldObjectInstance> groundTarget;
    if (shot.targetObjectId < 0 && shot.hasTargetPosition) {
        groundTarget = runtimeGroundAttackTargetObject(state, shot.toCell);
        if (groundTarget) target = &*groundTarget;
    }
    const auto* weapon = state.rules.findWeaponType(shot.weaponId);
    if (owner == nullptr || target == nullptr || !owner->alive || !target->alive
        || owner->owner != shot.owner || weapon == nullptr
        || runtimeWeaponDistanceToTarget(state, *owner, *target, *weapon) > weapon->range)
        return false;
    RuntimeWeaponProfile profile;
    profile.weapon = weapon;
    profile.slot = shot.weaponSlot == "elitePrimary"
        ? RuntimeWeaponSlot::ElitePrimary : RuntimeWeaponSlot::Primary;
    auto source = runtimeProjectileSourcePosition(state, *owner, profile,
        runtimeWeaponFiringFacing(state, *owner, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *owner, profile, runtimeWeaponFiringFacing(state, *owner, *target), source);
    shot.sourceX = source.x;
    shot.sourceY = source.y;
    shot.sourceZ = source.z;
    shot.sourceScreenOffsetX = source.screenOffsetX;
    shot.sourceNativeFixedX = source.nativeFixedX;
    shot.sourceScreenOffsetY = source.screenOffsetY;
    shot.sourceNativeFixedY = source.nativeFixedY;
    shot.fromCell = runtimeCellPointForObject(*owner);
    shot.fromLevel = static_cast<int>(std::lround(source.z));
    return true;
}

void spawnRuntimeDiskLaserBeam(RuntimeWindowState& state, const RuntimeProjectileEffect& charge) {
    RuntimeProjectileEffect beam = charge;
    const auto& point = runtimeDiskLaserPoints[(charge.attackerFacing / 16) & 15];
    beam.sourceX += point[0] / 256.0;
    beam.sourceY += point[1] / 256.0;
    beam.fromCell = {static_cast<int>(std::lround(beam.sourceX)),
        static_cast<int>(std::lround(beam.sourceY))};
    beam.actorId = nextRuntimeProjectileActorId(state);
    beam.parentActorId = charge.actorId;
    beam.generation = std::min(4, charge.generation + 1);
    beam.renderIntent = "diskLaserBeam";
    beam.targetObjectId = beam.sourceAnchorObjectId = -1;
    beam.projectileId.clear();
    beam.projectileImageId.clear();
    beam.impactAnimId.clear();
    beam.damage = 0;
    beam.laser = true;
    beam.totalTicks = runtimeAuthoredFramesToFixedTicks(state, std::max(1, charge.laserDuration));
    beam.remainingTicks = beam.totalTicks;
    beam.elapsedTicks = 0;
    beam.spawnTick = state.tick;
    beam.impactTick = state.tick + beam.totalTicks;
    beam.lifecycle = RuntimeProjectileLifecycleState::Spawned;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    updateRuntimeProjectileTravelPosition(beam);
    recordRuntimeProjectileReplayEvent(state, beam, "ProjectileSpawned");
    state.projectiles.push_back(std::move(beam));
    if (!charge.reportSoundId.empty()) emitRuntimeSound(state, "weapon:" + charge.reportSoundId, RuntimeSpatialAudioPosition{charge.sourceX, charge.sourceY, charge.sourceZ});
}
