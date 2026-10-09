#include "RuntimeSonicWaveGeometry.inl"

// A sonic shot has a conventional end hit plus an independently advancing wave.
// Reuse the saved projectile timeline and geometric fields: progress is the
// front, currentXYZ is the trailing point, speed is the front's advance/cycle.
void spawnRuntimeSonicWave(RuntimeWindowState& state,
    const RuntimeProjectileEffect& shot, const WeaponType& weapon) {
    if (!weapon.sonic || shot.deathWeaponCascadeId != 0 || shot.airstrikeStrikeId != 0) return;
    RuntimeProjectileEffect wave = shot;
    wave.actorId = nextRuntimeProjectileActorId(state);
    wave.parentActorId = -1;
    wave.renderIntent = "sonicWave";
    wave.projectileImageId.clear(); wave.impactAnimId.clear(); wave.trailerAnimId.clear();
    wave.particleId.clear(); wave.laser = false; wave.electricBolt = false;
    wave.shrapnel = wave.shrapnelReleased = false;
    wave.totalTicks = std::max(1, (101 * std::max(1, state.config.fixedTickHz) + 14) / 15);
    wave.remainingTicks = wave.totalTicks; wave.elapsedTicks = 0;
    wave.impactTick = wave.spawnTick + wave.totalTicks;
    wave.progress = static_cast<double>(0.05f); // retail ctor's first Update_Wave
    const double distance = std::hypot(wave.targetX - wave.sourceX, wave.targetY - wave.sourceY);
    // Retail ctor rejects rays shorter than 240 leptons (0x75EA83).
    if (distance < 240.0/256.0) return;
    wave.targetZ += runtimeWeaponHeightInLevels(50);
    wave.rot = runtimeSonicWaveInitialDirection(wave);
    wave.speedCellsPerTick = distance * static_cast<double>(0.05f);
    wave.currentX = wave.sourceX; wave.currentY = wave.sourceY; wave.currentZ = wave.sourceZ;
    wave.currentCell = wave.fromCell; wave.currentLevel = wave.fromLevel;
    state.projectiles.push_back(std::move(wave));
    recordRuntimeProjectileReplayEvent(state, state.projectiles.back(), "ProjectileSpawned");
}
