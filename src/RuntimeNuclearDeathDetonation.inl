// A reactor's DeathWeapon is an explosion at the dead building, not a
// strategic missile. Resolve it while the ObjectExit queue is being drained;
// secondary deaths remain queued, preserving the existing cascade guard.
bool detonateRuntimeNuclearDeathWeapon(
    RuntimeWindowState& state,
    RuntimeProjectileEffect& projectile,
    const WorldObjectInstance& source
) {
    if (!runtimeProjectileIsNuclearDeathDetonation(projectile)
        || projectile.parentActorId >= 0 || projectile.generation != 0) {
        return false;
    }
    const auto center = runtimeProjectileTargetPosition(state, source);
    projectile.targetObjectId = -1;
    projectile.sourceAnchorObjectId = -1;
    projectile.fromCell = projectile.toCell = projectile.currentCell = center.cell;
    projectile.fromLevel = projectile.toLevel = projectile.currentLevel = center.level;
    projectile.hasSourcePosition = projectile.hasTargetPosition = true;
    projectile.sourceX = projectile.targetX = projectile.currentX = center.x;
    projectile.sourceY = projectile.targetY = projectile.currentY = center.y;
    projectile.sourceZ = projectile.targetZ = projectile.currentZ = center.level;
    projectile.sourceScreenOffsetX = projectile.sourceScreenOffsetY = 0;
    projectile.sourceNativeFixedX = projectile.sourceNativeFixedY = 0;
    projectile.affectsAllies = projectile.affectsEnemies = true;
    projectile.spawnTick = projectile.impactTick = state.tick;
    projectile.speedCellsPerTick = 0.0;
    projectile.totalTicks = projectile.remainingTicks = 1;
    projectile.elapsedTicks = 0;
    recordRuntimeProjectileReplayEvent(state, projectile, "ProjectileSpawned");

    // This value exists only for the synchronous impact. No traveling or
    // expired missile is retained in saves/snapshots or projected on screen.
    projectile.remainingTicks = 0;
    projectile.elapsedTicks = 1;
    projectile.progress = 1.0;
    projectile.lifecycle = RuntimeProjectileLifecycleState::Impacting;
    projectile.lifecycleState = runtimeProjectileLifecycleName(projectile.lifecycle);
    (void)applyRuntimeProjectileImpact(state, projectile);
    projectile.lifecycle = RuntimeProjectileLifecycleState::Expired;
    projectile.lifecycleState = runtimeProjectileLifecycleName(projectile.lifecycle);
    recordRuntimeProjectileReplayEvent(state, projectile, "ProjectileExpired");
    state.actionSummary.projectilesInFlight = static_cast<int>(state.projectiles.size());
    return true;
}
