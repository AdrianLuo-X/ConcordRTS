void resolveRuntimeMagBeamRenderEndpoints(RuntimeWindowState& state,
    SpecialMechanismRenderSnapshot& snapshot, const RuntimeMagBeamBindingState& binding) {
    const auto* source = findObjectById(state, binding.sourceObjectId);
    const auto* target = findObjectById(state, binding.targetObjectId);
    if (!source || !target) return;
    const auto sourceOrder = state.behaviors.find(binding.sourceObjectId);
    const bool profileForceFire = sourceOrder != state.behaviors.end()
        && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
        && sourceOrder->second.targetObjectId == binding.targetObjectId
        && sourceOrder->second.forceFire;
    const auto profile = runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
    if (!profile.weapon) return;
    auto muzzle = runtimeProjectileSourcePosition(state, *source, profile,
        runtimeWeaponFiringFacing(state, *source, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *source, profile, runtimeWeaponFiringFacing(state, *source, *target), muzzle);
    snapshot.sourceX = muzzle.x; snapshot.sourceY = muzzle.y; snapshot.sourceLevel = muzzle.z;
    snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
    snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
    snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
    snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
    snapshot.targetX = binding.kinematics.worldX;
    snapshot.targetY = binding.kinematics.worldY;
    snapshot.targetLevel = binding.kinematics.worldZ;
    snapshot.color = state.rules.getAudioVisualValue("MagnaBeamColor");
    snapshot.hasBeamEndpoints = true;
}

bool advanceRuntimeMagBeamVisual(RuntimeWindowState& state, RuntimeProjectileEffect& beam) {
    const auto* source = findObjectById(state, beam.attackerObjectId);
    const auto* target = findObjectById(state, beam.targetObjectId);
    const auto order = state.behaviors.find(beam.attackerObjectId);
    if (!source || !target || source->health <= 0 || target->health <= 0
        || source->owner != beam.owner || source->renderMoveActive
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.targetObjectId != target->instanceId
        || (order->second.state != RuntimeBehaviorState::Attacking
            && order->second.state != RuntimeBehaviorState::Cooldown)) return false;
    const auto profile = runtimeWeaponProfileForObject(state, *source, target, order->second.forceFire);
    if (!profile.weapon || !profile.weapon->magBeam || !profile.warhead || profile.warhead->isLocomotor
        || toLowerAsciiLocal(profile.weapon->id) != toLowerAsciiLocal(beam.weaponId)
        || runtimeWeaponDistanceToTarget(state, *source, *target, *profile.weapon)
            > runtimeEffectiveWeaponRange(state, *source, *target, *profile.weapon)
        || !runtimeWeaponHasLineOfSight(state, *source, *target, *profile.weapon)) return false;
    // A single presentation actor follows this firing order. Refreshing its
    // deadline never applies damage or restarts the weapon's reload clock.
    syncRuntimeProjectileTargetEndpoint(state, beam);
    auto muzzle = runtimeProjectileSourcePosition(state, *source, profile,
        runtimeWeaponFiringFacing(state, *source, *target));
    alignRuntimeWeaponMuzzleToSprite(state, *source, profile, runtimeWeaponFiringFacing(state, *source, *target), muzzle);
    beam.hasSourcePosition = true;
    beam.sourceX = muzzle.x; beam.sourceY = muzzle.y; beam.sourceZ = muzzle.z;
    beam.sourceScreenOffsetX = muzzle.screenOffsetX;
    beam.sourceNativeFixedX = muzzle.nativeFixedX;
    beam.sourceScreenOffsetY = muzzle.screenOffsetY;
    beam.sourceNativeFixedY = muzzle.nativeFixedY;
    beam.fromCell = runtimeCellPointForObject(*source); beam.fromLevel = source->level;
    beam.elapsedTicks = std::max(0, state.tick - beam.spawnTick);
    beam.totalTicks = beam.elapsedTicks + 2; beam.remainingTicks = 2;
    beam.impactTick = state.tick + 2;
    beam.lifecycle = RuntimeProjectileLifecycleState::Traveling;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    updateRuntimeProjectileTravelPosition(beam);
    return true;
}

void spawnRuntimeMagBeamVisual(RuntimeWindowState& state, const RuntimeProjectileEffect& shot) {
    if (shot.renderIntent == "magBeamVisual") return;
    const auto* weapon = state.rules.findWeaponType(shot.weaponId);
    const auto* warhead = state.rules.findWarheadType(shot.warheadId);
    // The locomotor owns vehicle links. A building firing order owns one
    // persistent presentation actor, independent of its periodic damage.
    if (!weapon || !weapon->magBeam || !warhead || warhead->isLocomotor) return;
    const auto sameLink = [&](const RuntimeProjectileEffect& actor) {
        return actor.renderIntent == "magBeamVisual" && actor.remainingTicks > 0
            && actor.attackerObjectId == shot.attackerObjectId
            && actor.targetObjectId == shot.targetObjectId && actor.weaponId == shot.weaponId;
    };
    if (std::any_of(state.projectiles.begin(), state.projectiles.end(), sameLink)
        || std::any_of(state.projectileTickScratch.begin(), state.projectileTickScratch.end(), sameLink)) return;
    RuntimeProjectileEffect beam = shot;
    beam.actorId = nextRuntimeProjectileActorId(state);
    beam.parentActorId = shot.actorId;
    beam.generation = std::min(4, shot.generation + 1);
    beam.renderIntent = "magBeamVisual";
    beam.sourceAnchorObjectId = -1; // FLH is refreshed explicitly, not the body center.
    beam.projectileId.clear(); beam.projectileImageId.clear(); beam.impactAnimId.clear();
    beam.weaponAnimId.clear(); beam.reportSoundId.clear();
    beam.damage = 0; beam.laser = true; beam.electricBolt = false;
    beam.totalTicks = 2;
    beam.remainingTicks = beam.totalTicks; beam.elapsedTicks = 0;
    beam.spawnTick = state.tick; beam.impactTick = state.tick + beam.totalTicks;
    beam.lifecycle = RuntimeProjectileLifecycleState::Spawned;
    beam.lifecycleState = runtimeProjectileLifecycleName(beam.lifecycle);
    if (!advanceRuntimeMagBeamVisual(state, beam)) return;
    recordRuntimeProjectileReplayEvent(state, beam, "ProjectileSpawned");
    state.projectiles.push_back(std::move(beam));
}
