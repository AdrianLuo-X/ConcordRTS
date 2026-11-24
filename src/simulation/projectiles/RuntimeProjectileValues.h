#pragma once
#include "world/values/RuntimeCellPoint.h"
#include "simulation/projectiles/RuntimeVoxelDebrisState.h"
#include <cstdint>
#include <string>

// Existing qualified value identities and defaults, with no host dependencies.
namespace concord::runtime::session_detail {
enum class RuntimeProjectileLifecycleState {
    Spawned,
    Traveling,
    Impacting,
    Expired
};

struct RuntimeProjectileEffect {
    RuntimeVoxelDebrisState voxelDebris;
    bool homingInitialized = false;
    double homingYaw = 0.0;
    double homingPitch = 0.0;
    int actorId = -1;
    int parentActorId = -1;
    int generation = 0;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    int sourceAnchorObjectId = -1;
    std::string owner;
    RuntimeCellPoint fromCell;
    RuntimeCellPoint toCell;
    RuntimeCellPoint currentCell;
    bool hasSourcePosition = false;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double sourceZ = 0.0;
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
    bool hasTargetPosition = false;
    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;
    int fromLevel = 0;
    int toLevel = 0;
    int currentLevel = 0;
    int attackerFacing = -1;
    int targetFacing = -1;
    int spawnTick = 0;
    int impactTick = 0;
    int elapsedTicks = 0;
    int damage = 0;
    // Snapshot at fire time. The shooter may promote or die while a
    // projectile is in flight; neither may retroactively change its damage.
    double veteranDamageMultiplier = 1.0;
    bool spawnedMissileDamage = false;
    int spawnedMissileBaseDamage = 0;
    int totalTicks = 1;
    int remainingTicks = 1;
    RuntimeProjectileLifecycleState lifecycle =
        RuntimeProjectileLifecycleState::Spawned;
    std::string lifecycleState = "Spawned";
    std::string renderIntent = "projectile";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string weaponAnimId;
    std::string impactAnimId;
    std::string trailerAnimId;
    std::string trailerIntentId;
    std::string warheadImpactIntentId;
    std::string reportSoundId;
    bool arcing = false;
    bool vertical = false;
    int acceleration = 0;
    bool high = false;
    bool veryHigh = false;
    bool shadow = false;
    bool animates = false;
    bool aa = false;
    bool ag = true;
    int cluster = 0;
    bool bright = false;
    bool laser = false;
    bool houseColorLaser = false;
    bool electricBolt = false;
    bool alternateElectricColor = false;
    std::string electricColor;
    bool shrapnel = false;
    bool shrapnelReleased = false;
    int laserDuration = 0;
    int laserThickness = 1;
    double cellSpread = 0.0;
    double percentAtMax = 0.0;
    bool affectsAllies = false;
    bool affectsEnemies = true;
    std::string particleId;
    double progress = 0.0;
    double currentX = 0.0;
    double currentY = 0.0;
    double currentZ = 0.0;
    double speedCellsPerTick = 0.0;
    int rot = 0;
    // Value-only provenance for projectiles emitted by DeathWeapon. Ordinary
    // projectiles keep all three fields at zero. Derived shrapnel/debris keeps
    // the originating request so a later death continues the same cascade.
    std::uint64_t deathWeaponCascadeId = 0;
    std::uint64_t deathWeaponRequestId = 0;
    std::uint32_t deathWeaponCascadeDepth = 0;
    // Schema 3 appends immutable launch attribution and reserved causal
    // markers. Keep this tail value-only: impact consumers must never infer
    // a vanished source from a current world pointer.
    int launchSourceKind = -1;
    std::string launchSourceType;
    bool launchSourceAuthoredAircraft = false;
    bool launchSourceValidated = false;
    int airstrikeStrikeId = 0;
    int airstrikePlaneToken = 0;
    bool particleDamagePulse = false;
    bool suppressWarheadParticleSpawn = false;
};

struct RuntimeProjectileTargetPosition {
    RuntimeCellPoint cell;
    double x = 0.0;
    double y = 0.0;
    int level = 0;
};
}
