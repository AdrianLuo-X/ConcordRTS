bool launchRuntimeParasiteProjectile(
    FixtureState& state,
    const WorldObjectInstance& source,
    const WorldObjectInstance& target,
    const WeaponType& weapon,
    const FixtureWeaponProfile& weaponProfile) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const WarheadType* warhead = weaponProfile.warhead;
    const UnitType* sourceType = findRuntimeUnitType(state.rules, source);
    const UnitType* targetType = findRuntimeUnitType(state.rules, target);
    const ProjectileType* projectileType =
        findRuntimeProjectileType(state.rules, weapon.projectile);
    const bool validTargetKind = target.kind == WorldObjectKind::Infantry
        || (target.kind == WorldObjectKind::Unit
            && !target.authoredAircraft && !target.airborne);
    if (warhead == nullptr || sourceType == nullptr || targetType == nullptr
        || !runtimeParasiteWarheadIsExclusive(weapon, *warhead)
        || projectileType == nullptr
        || runtimeSpawnedMissileProfileForWeapon(
            state,
            source,
            weapon
        ) != nullptr
        || projectileType->airburst || projectileType->shrapnelCount > 0
        || !trimAsciiLocal(projectileType->airburstWeapon).empty()
        || !trimAsciiLocal(projectileType->shrapnelWeapon).empty()
        || !source.alive || source.health <= 0
        || !target.alive || target.health <= 0
        || source.instanceId == target.instanceId
        || !validTargetKind
        || (target.kind == WorldObjectKind::Unit
            && !targetType->parasiteable)
        || runtimeParasiteObjectHasExclusiveAuthority(state, source)
        || runtimeParasiteObjectHasExclusiveAuthority(state, target, true)
        || !runtimeWeaponCanTargetObject(state, weapon, target)
        || (state.superWeaponsInitialized
            && state.superWeapons.isObjectProtected(
                target.instanceId,
                state.tick
            ))) {
        state.simulationStatus = "parasite launch rejected by host";
        return false;
    }

    const int previousNextActorId = state.nextProjectileActorId;
    const int projectileActorId = nextRuntimeProjectileActorId(state);
    ParasiteLaunchRequest request;
    request.projectileId = projectileActorId;
    request.currentTick = state.tick;
    request.sourceObjectId = source.instanceId;
    request.targetObjectId = target.instanceId;
    request.sourceOwner = source.owner;
    request.sourceTypeId = source.type;
    request.targetTypeId = target.type;
    request.weaponId = weapon.id;
    request.warheadId = warhead->id;
    request.targetKind = target.kind == WorldObjectKind::Infantry
        ? ParasiteTargetKind::Infantry
        : ParasiteTargetKind::Vehicle;
    const RuntimeCellPoint targetCell = runtimeCellPointForObject(target);
    request.targetCell = {targetCell.x, targetCell.y};
    request.damage = std::max(0, weapon.damage);
    request.sourceOrganic = sourceType->organic;
    request.sourceElite = source.veterancy >= 2;
    request.damageIntervalTicks = runtimeParasiteRuleFramesToTicks(
        request.sourceOrganic ? 36 : std::max(1, weapon.rof),
        state.config.fixedTickHz
    );
    request.paralyzes = std::max(0, warhead->paralyzes);
    request.targetMaximumHealth = std::max(1, targetType->strength);
    request.targetParasiteable = target.kind == WorldObjectKind::Infantry
        || targetType->parasiteable;
    request.culling = warhead->culling;
    request.normalCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                state.specialMechanisms.rules().conditionRed,
                0.0,
                1.0
            ) * 10000.0
        ));
    request.eliteCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                state.specialMechanisms.rules().conditionYellow,
                0.0,
                1.0
            ) * 10000.0
        ));

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult began = specialCandidate.beginParasiteLaunch(
        request,
        SpecialEventSink{}
    );
    if (!began.accepted || began.idempotent || began.actions.size() != 1U
        || !validateRuntimeParasiteActionBatch(
            state,
            prepareRuntimeParasiteActions(state, began.actions)
        )) {
        state.nextProjectileActorId = previousNextActorId;
        state.simulationStatus = began.reason.empty()
            ? "parasite launch action rejected atomically"
            : "parasite launch rejected: " + began.reason;
        return false;
    }

    spawnRuntimeProjectile(
        state,
        source,
        target,
        weapon,
        weaponProfile,
        request.damage,
        -1,
        0,
        false,
        -1,
        -1.0,
        0,
        0,
        0,
        true,
        nullptr,
        projectileActorId
    );
    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            began.actions,
            {},
            source.instanceId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite launch commit rejected unexpectedly";
        return false;
    }
    state.simulationStatus = "parasite launched #"
        + std::to_string(source.instanceId)
        + " -> #" + std::to_string(target.instanceId);
    return true;
}
