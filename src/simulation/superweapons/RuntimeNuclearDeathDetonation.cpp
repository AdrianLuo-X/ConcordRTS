#include "simulation/superweapons/RuntimeNuclearDeathDetonation.h"
namespace concord::simulation::superweapons {
bool detonateRuntimeNuclearDeathWeapon(const int& tick,int& projectilesInFlight,
    const RuntimeNuclearDeathQueries& queries,RuntimeNuclearDeathEffects& effects,
    RuntimeProjectileEffect& projectile,const WorldObjectInstance& source) {
    if (!queries.isNuclearDeath(projectile)
        || projectile.parentActorId >= 0 || projectile.generation != 0) {
        return false;
    }
    const auto center = queries.targetPosition(source);
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
    projectile.spawnTick = projectile.impactTick = tick;
    projectile.speedCellsPerTick = 0.0;
    projectile.totalTicks = projectile.remainingTicks = 1;
    projectile.elapsedTicks = 0;
    effects.recordReplay(projectile, "ProjectileSpawned");

    // This value exists only for the synchronous impact. No traveling or
    // expired missile is retained in saves/snapshots or projected on screen.
    projectile.remainingTicks = 0;
    projectile.elapsedTicks = 1;
    projectile.progress = 1.0;
    projectile.lifecycle = RuntimeProjectileLifecycleState::Impacting;
    projectile.lifecycleState = queries.lifecycleName(projectile.lifecycle);
    (void)effects.applyImpact(projectile);
    projectile.lifecycle = RuntimeProjectileLifecycleState::Expired;
    projectile.lifecycleState = queries.lifecycleName(projectile.lifecycle);
    effects.recordReplay(projectile, "ProjectileExpired");
    projectilesInFlight = static_cast<int>(queries.projectileCount());
    return true;
}
}
