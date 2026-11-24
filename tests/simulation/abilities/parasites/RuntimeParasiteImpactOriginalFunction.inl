bool applyRuntimeParasiteProjectileImpactExclusive(
    FixtureState& state,
    const RuntimeProjectileEffect& projectile) {
    const auto pendingIt =
        state.specialMechanisms.pendingParasiteLaunches().find(
            projectile.actorId
        );
    if (pendingIt
        == state.specialMechanisms.pendingParasiteLaunches().end()) {
        return false;
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(
            state,
            pending.sourceObjectId
        );
    const WorldObjectInstance* target = findObjectById(
        state,
        projectile.targetObjectId
    );
    const WarheadType* warhead =
        findRuntimeWarheadType(state.rules, projectile.warheadId);
    const WeaponType* weapon =
        findRuntimeWeaponType(state.rules, projectile.weaponId);
    const UnitType* targetType = target == nullptr
        ? nullptr
        : findRuntimeUnitType(state.rules, *target);
    const bool targetKindMatches = target != nullptr
        && ((pending.targetKind == ParasiteTargetKind::Infantry
                && target->kind == WorldObjectKind::Infantry)
            || (pending.targetKind == ParasiteTargetKind::Vehicle
                && target->kind == WorldObjectKind::Unit
                && !target->authoredAircraft && !target->airborne));
    const bool causalIdentityMatches = source != nullptr
        && source->health > 0 && !source->alive
        && source->mission == "ParasiteLimbo"
        && source->type == pending.sourceTypeId
        && projectile.attackerObjectId == pending.sourceObjectId
        && projectile.targetObjectId == pending.targetObjectId
        && projectile.owner == pending.owner
        && projectile.weaponId == pending.weaponId
        && projectile.warheadId == pending.warheadId;
    const bool strictWeapon = weapon != nullptr && warhead != nullptr
        && runtimeParasiteWarheadIsExclusive(*weapon, *warhead);
    const bool legalHit = causalIdentityMatches && strictWeapon
        && target != nullptr && target->alive && target->health > 0
        && target->type == pending.targetTypeId && targetKindMatches
        && targetType != nullptr
        && (pending.targetKind == ParasiteTargetKind::Infantry
            || targetType->parasiteable)
        && runtimeProjectileCanHitTarget(state, projectile, *target)
        && runtimeWarheadArmorMultiplier(
            warhead,
            runtimeTargetArmor(state, *target)
        ) > 0.0
        && !runtimeParasiteObjectHasExclusiveAuthority(state, *target, true)
        && (!state.superWeaponsInitialized
            || !state.superWeapons.isObjectProtected(
                target->instanceId,
                state.tick
            ));

    const RuntimeCellPoint impactCell = target != nullptr && target->alive
        ? runtimeCellPointForObject(*target)
        : projectile.toCell;
    const std::optional<RuntimeCellPoint> returnCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {impactCell.x, impactCell.y},
            nullptr,
            legalHit && pending.targetKind == ParasiteTargetKind::Infantry
                ? target->instanceId : -1
        );
    ParasiteImpactRequest request;
    request.projectileId = projectile.actorId;
    request.currentTick = state.tick;
    request.impact = legalHit
        ? ParasiteImpactKind::Hit
        : ParasiteImpactKind::Miss;
    request.targetObjectId = target == nullptr
        ? pending.targetObjectId
        : target->instanceId;
    request.targetTypeId = target == nullptr
        ? pending.targetTypeId
        : target->type;
    request.targetKind = pending.targetKind;
    request.targetPresent = target != nullptr;
    request.targetAlive = target != nullptr && target->alive
        && target->health > 0;
    request.targetParasiteable = targetType != nullptr
        && (target->kind == WorldObjectKind::Infantry
            || targetType->parasiteable);
    request.targetMaximumHealth = targetType == nullptr
        ? std::max(1, pending.targetMaximumHealth)
        : std::max(1, targetType->strength);
    request.impactCell = {impactCell.x, impactCell.y};
    request.returnCellAvailable = returnCell.has_value();
    if (returnCell.has_value()) {
        request.returnCell = {returnCell->x, returnCell->y};
    }

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult resolved =
        specialCandidate.resolveParasiteImpact(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, resolved.actions);
    if (!resolved.accepted
        || !validateRuntimeParasiteActionBatch(state, prepared)) {
        state.simulationStatus = resolved.reason.empty()
            ? "parasite impact action rejected atomically"
            : "parasite impact rejected: " + resolved.reason;
        return true;
    }

    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            prepared,
            runtimeDeathWeaponCauseForProjectile(projectile),
            pending.sourceObjectId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite impact commit rejected unexpectedly";
        return true;
    }
    RuntimeProjectileEffect relationshipImpact = projectile;
    relationshipImpact.impactAnimId.clear();
    relationshipImpact.particleId.clear();
    relationshipImpact.damage = 0;
    relationshipImpact.cellSpread = 0.0;
    relationshipImpact.bright = false;
    relationshipImpact.reportSoundId.clear();
    recordRuntimeProjectileReplayEvent(
        state,
        relationshipImpact,
        "ProjectileImpact"
    );
    ++state.model->impacts;
    (void)drainRuntimeObjectExitQueue(state);
    state.simulationStatus = legalHit
        ? "parasite impact accepted"
        : "parasite impact resolved as miss";
    return true;
}
