const WorldObjectInstance* runtimeFlakImpactBodyTarget(
    const RuntimeWindowState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeProjectileTargetPosition& hit
) {
    const auto* type = findRuntimeProjectileType(state.rules, projectile.projectileId);
    if (projectile.targetObjectId >= 0 || !type || !type->flakScatter
        || !projectile.aa || projectile.ag || projectile.damage <= 0
        || projectile.cellSpread <= 0.0 || projectile.particleDamagePulse) return nullptr;
    const auto* warhead = findRuntimeWarheadType(state.rules, projectile.warheadId);
    if (warhead && (warhead->ivanBomb || warhead->bombDisarm || warhead->psychedelic
        || warhead->makesDisguise || warhead->mindControl || warhead->electricAssault
        || warhead->temporal)) return nullptr;
    // Keep scattered, non-tracking physical endpoints. Only the saved visual
    // offset moves to a body in the existing damage footprint. A miss must
    // remain a miss, including after the original shooter disappears.
    WorldObjectInstance source;
    source.instanceId = projectile.attackerObjectId;
    source.owner = projectile.owner;
    const auto* attacker = findObjectById(state, projectile.attackerObjectId);
    if (attacker && attacker->alive) source.owner = attacker->owner;
    const auto* weapon = findRuntimeWeaponType(state.rules, projectile.weaponId);
    const bool lockedDamage = runtimeProjectileHasLockedDeathWeaponDamage(projectile);
    const int authoredDamage = weapon && !lockedDamage
        ? runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon) : projectile.damage;
    const WorldObjectInstance* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (const auto& candidate : state.world.getObjects()) {
        const double distance = runtimeCellDistanceToObjectFootprint(state, hit.cell, candidate);
        if (!candidate.alive || candidate.health <= 0 || !candidate.airborne
            || candidate.kind == WorldObjectKind::Structure || candidate.tankBunkerHostId >= 0
            || candidate.instanceId == projectile.attackerObjectId
            || distance > projectile.cellSpread
            || !runtimeProjectileCanHitTarget(state, projectile, candidate)
            || !runtimeWarheadCanAffectTarget(state, projectile, source, candidate)
            || runtimeObjectIgnoresDamage(state, candidate)
            || (warhead && warhead->tiberium && runtimeObjectIsTiberiumProof(state, candidate))
            || (state.specialMechanismsInitialized && state.specialMechanisms.isTemporalFrozen(candidate.instanceId))
            || (state.superWeaponsInitialized && state.superWeapons.isObjectProtected(candidate.instanceId, state.tick))
            || runtimeDamageIsTypeImmune(state, candidate,
                runtimeTypeImmuneSource(state, projectile.attackerObjectId), authoredDamage)) continue;
        const int damage = !weapon ? projectile.damage : runtimeAuthoredDamageAgainst(state,
            candidate, projectile.spawnedMissileDamage ? projectile.spawnedMissileBaseDamage : authoredDamage,
            lockedDamage ? projectile.warheadId : weapon->warhead, distance,
            lockedDamage ? 1.0 : projectile.veteranDamageMultiplier);
        if (damage <= 0) continue;
        const double dx = runtimeObjectRenderX(candidate) - hit.x;
        const double dy = runtimeObjectRenderY(candidate) - hit.y;
        const double bodyDistance = dx * dx + dy * dy;
        if (!std::isfinite(bodyDistance)) continue;
        if (bodyDistance < nearestDistance || (bodyDistance == nearestDistance
            && (!nearest || candidate.instanceId < nearest->instanceId))) {
            nearest = &candidate;
            nearestDistance = bodyDistance;
        }
    }
    return nearest;
}
