// Included after projectile positioning helpers and before spawnRuntimeProjectile.
long long runtimeCombatProjectileAuthoredDamage(const RuntimeWindowState& state,
    const RuntimeProjectileEffect& projectile, const WeaponType& weapon) {
    int rounds = 1;
    // Cluster fragments, shrapnel and spawned-missile terminal weapons have
    // their own actor counts. Their virtual weapon burst remains aggregate.
    if (projectile.parentActorId >= 0 && projectile.generation > 0)
        rounds = std::max(1, weapon.burst);
    else if (const auto found = state.combatDetails.legacyProjectileRounds.find(projectile.actorId);
        found != state.combatDetails.legacyProjectileRounds.end()) rounds = found->second;
    return static_cast<long long>(weapon.damage) * rounds;
}

void prepareRuntimeCombatProjectile(
    RuntimeWindowState& state, RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& attacker, const WorldObjectInstance& target,
    const WeaponType& weapon, const ProjectileType* projectileType
) {
    if (projectile.spawnedMissileDamage) return;
    const auto& detailRules = state.rules.getCombatDetailRules();
    if (projectileType != nullptr && (projectileType->inaccurate || projectileType->flakScatter)
        && !projectileType->vertical && !projectileType->dropping
        && !weapon.areaFire && !weapon.laser && !weapon.electricBolt) {
        const auto seed = runtimeCombatRandomMix(
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(projectile.actorId))
            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.tick)) << 32U)
            ^ static_cast<std::uint32_t>(attacker.instanceId)
            ^ runtimeCombatRandomMix(static_cast<std::uint64_t>(state.config.simulationRandomSeed)));
        const auto offset = runtimeCombatScatterOffset(seed, detailRules.ballisticScatter);
        projectile.targetX += static_cast<double>(offset.xLeptons) / 256.0;
        projectile.targetY += static_cast<double>(offset.yLeptons) / 256.0;
        projectile.toCell = {static_cast<int>(std::lround(projectile.targetX)),
            static_cast<int>(std::lround(projectile.targetY))};
        // An inaccurate round aims at this immutable world point. Retaining
        // targetObjectId would make syncRuntimeProjectileTargetEndpoint undo
        // the scatter and guarantee a direct hit even outside CellSpread.
        projectile.targetObjectId = -1;
    }
    const double speedMultiplier = runtimeCombatProjectileSpeedMultiplier(
        weapon.turboBoost, runtimeObjectIsAirTarget(state, target), detailRules.turboBoost);
    if (speedMultiplier != 1.0 && !weapon.laser && !weapon.electricBolt && !weapon.diskLaser) {
        projectile.totalTicks = std::clamp(static_cast<int>(std::ceil(
            static_cast<double>(projectile.totalTicks) / speedMultiplier)), 1, 9600);
        projectile.remainingTicks = projectile.totalTicks;
        projectile.impactTick = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.tick) + projectile.totalTicks));
        projectile.speedCellsPerTick *= speedMultiplier;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, attacker);
    if (unit != nullptr && unit->radialFireSegments > 0 && projectile.parentActorId < 0
        && projectileType != nullptr && projectileType->rot > 0
        && !weapon.laser && !weapon.electricBolt) {
        auto& cursor = state.combatDetails.fireCursors[attacker.instanceId];
        projectile.attackerFacing = runtimeCombatRadialFacing(
            attacker.facing, unit->radialFireSegments, cursor.radialSegment);
        cursor.radialSegment = (cursor.radialSegment + 1) % std::clamp(unit->radialFireSegments, 1, 128);
        // renderIntent and attackerFacing are already part of save, snapshot
        // and authoritative projectile hashes. The heading survives reload.
        projectile.renderIntent = "radialProjectile";
    }
}

// Defined later with the common missile steering code.
bool advanceRuntimeHomingMissilePosition(const RuntimeWindowState& state,
    RuntimeProjectileEffect& shot, const ProjectileType& type);

bool advanceRuntimeRadialProjectilePosition(const RuntimeWindowState& state, RuntimeProjectileEffect& projectile) {
    const auto* type = findRuntimeProjectileType(state.rules, projectile.projectileId);
    if (!type) return false;
    const bool reached = advanceRuntimeHomingMissilePosition(state, projectile, *type);
    const int facing = runtimeFacingFromDelta(
        static_cast<int>(std::lround(std::cos(projectile.homingYaw) * 32767.0)),
        static_cast<int>(std::lround(std::sin(projectile.homingYaw) * 32767.0)));
    if (facing >= 0) projectile.attackerFacing = facing;
    projectile.progress = runtimeProjectileProgress(projectile);
    projectile.currentLevel = static_cast<int>(std::lround(projectile.currentZ));
    projectile.currentCell = {static_cast<int>(std::lround(projectile.currentX)),
        static_cast<int>(std::lround(projectile.currentY))};
    return reached;
}
