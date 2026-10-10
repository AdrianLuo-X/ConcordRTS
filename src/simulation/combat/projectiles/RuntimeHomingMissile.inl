#include "simulation/combat/projectiles/RuntimeMissileAcceleration.h"

// Retail gamemd.exe 0x5B20F0: ROT missiles keep a direction vector, climb
// toward a terrain-relative cruise height, then steer toward the target.
// World Z is in terrain levels; XY and the normalized direction use cells.
// Fractional rule frames adapt the native 15 Hz steering to the fixed clock.
bool runtimeUsesHomingMissileGuidance(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    return type.rot > 0 && type.ranged && !type.arcing && !type.vertical
        && !type.dropping && !type.inviso && !shot.spawnedMissileDamage
        && !shot.laser && !shot.electricBolt && !shot.voxelDebris.enabled;
}

// Startup acceleration uses the same eligibility for ordinary ranged and
// radial launches. Special projectile paths keep their existing cruising step.
bool runtimeUsesMissileStartupAcceleration(const RuntimeWindowState& state,
    const RuntimeProjectileEffect& shot, const ProjectileType& type) {
    const auto* weapon = findRuntimeWeaponType(state.rules, shot.weaponId);
    if (weapon && weapon->diskLaser) return false;
    return type.rot > 0 && (type.ranged || shot.renderIntent == "radialProjectile")
        && !type.arcing && !type.vertical && !type.dropping && !type.inviso
        && !shot.spawnedMissileDamage && !shot.laser && !shot.electricBolt
        && !shot.voxelDebris.enabled && shot.renderIntent != "diskLaserCharge"
        && !runtimeProjectileIsParasiteLeap(state, shot);
}

// Keep the stock Aegis check-mark launch close to the ship. The immutable
// launch identity survives source destruction and ordinary/canonical restores.
bool runtimeUsesCompactAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    if (shot.renderIntent != "radialProjectile" || !shot.launchSourceValidated
        || toUpperAsciiLocal(trimAsciiLocal(shot.launchSourceType)) != "AEGIS"
        || toUpperAsciiLocal(trimAsciiLocal(shot.projectileId)) != "MEDUSAPROJECTILE"
        || toUpperAsciiLocal(trimAsciiLocal(type.id)) != "MEDUSAPROJECTILE") return false;
    const auto weapon = toUpperAsciiLocal(trimAsciiLocal(shot.weaponId));
    return weapon == "MEDUSA" || weapon == "MEDUSAE";
}

bool runtimeUsesCloseAegisRadialLaunch(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    return runtimeUsesCompactAegisRadialLaunch(shot, type)
        && std::hypot(shot.targetX-shot.sourceX, shot.targetY-shot.sourceY) <= 5.0;
}

double runtimeHomingMissileCourseLockRuleFrames(const RuntimeProjectileEffect& shot,
    const ProjectileType& type) {
    const double authored = std::max(0, type.courseLockDuration);
    // A nearby target can lie inside the original turn circle. Start steering
    // after one rule frame, from the direct initial bearing below, instead of
    // letting a high-speed missile circle that target indefinitely.
    return runtimeUsesCompactAegisRadialLaunch(shot, type)
        ? std::min(authored, runtimeUsesCloseAegisRadialLaunch(shot, type) ? 1.0 : 3.0)
        : authored;
}

bool advanceRuntimeHomingMissilePosition(const RuntimeWindowState& state,
    RuntimeProjectileEffect& shot, const ProjectileType& type) {
    constexpr double pi = 3.14159265358979323846;
    constexpr double heightPerLepton = runtimeMissileLevelsPerCell / 256.0;
    constexpr double nativeCellHeight = 104.0 * heightPerLepton;
    const double dt = 15.0 / std::max(1, state.config.fixedTickHz);
    const double step = runtimeUsesMissileStartupAcceleration(state, shot, type)
        ? runtimeMissileAccelerationStepCells(shot.speedCellsPerTick, shot.acceleration,
            shot.elapsedTicks, state.config.fixedTickHz)
        : std::max(0.00001, shot.speedCellsPerTick);
    double dx = shot.targetX - shot.currentX;
    double dy = shot.targetY - shot.currentY;
    double dz = (shot.targetZ - shot.currentZ) / runtimeMissileLevelsPerCell;
    double horizontal = std::hypot(dx, dy);
    const double distance = std::hypot(horizontal, dz);
    if (distance <= step + 0.0001) {
        shot.currentX = shot.targetX; shot.currentY = shot.targetY; shot.currentZ = shot.targetZ;
        return true;
    }
    if (!shot.homingInitialized) {
        shot.homingInitialized = true;
        if (shot.renderIntent == "radialProjectile") {
            const double angle = shot.attackerFacing * (2.0 * pi / 256.0);
            const double sx = std::sin(angle), sy = -std::cos(angle);
            shot.homingYaw = std::atan2(sy-sx, sx+sy);
            if (runtimeUsesCloseAegisRadialLaunch(shot, type)) {
                // A close target can be inside even a scaled radial turn
                // circle at a finer clock. Start on its bearing; all later
                // movement and steering still use the authored speed/ROT.
                shot.homingYaw = std::atan2(dy, dx);
            }
        } else shot.homingYaw = std::atan2(dy, dx);
        // Native launch is horizontal unless the height difference exceeds
        // 200 leptons. Legacy saves begin from their existing flight point.
        shot.homingPitch = !type.level && (std::abs(dz) * 256.0 > 200.0 || shot.elapsedTicks > 1)
            ? std::atan2(dz, horizontal) : 0.0;
    }
    const double vx = std::cos(shot.homingYaw) * std::cos(shot.homingPitch);
    const double vy = std::sin(shot.homingYaw) * std::cos(shot.homingPitch);
    shot.currentX += vx * step;
    shot.currentY += vy * step;
    shot.currentZ += std::sin(shot.homingPitch) * step * runtimeMissileLevelsPerCell;
    dx = shot.targetX - shot.currentX; dy = shot.targetY - shot.currentY;
    dz = (shot.targetZ - shot.currentZ) / runtimeMissileLevelsPerCell;
    horizontal = std::hypot(dx, dy);
    const auto turn = [](double current, double desired, double rate) {
        const double delta = std::remainder(desired - current, 2.0 * pi);
        return std::remainder(current + std::clamp(delta, -rate, rate), 2.0 * pi);
    };
    const double rate = std::clamp(type.rot, 0, 127) * (2.0 * pi / 256.0) * dt;
    if (shot.elapsedTicks * dt < runtimeHomingMissileCourseLockRuleFrames(shot, type)) return false;
    shot.homingYaw = turn(shot.homingYaw, std::atan2(dy, dx), rate);
    if (type.level) { shot.homingPitch = 0.0; return false; }
    const auto* target = findObjectById(state, shot.targetObjectId);
    const bool airTarget = target && runtimeObjectIsAirTarget(state, *target);
    if (!airTarget && type.rot > 1 && (type.airburst || horizontal > (type.veryHigh ? 6.0 : 3.0))) {
        // 0x5B2640 samples the ground six native velocity steps ahead.
        const double ahead = step / dt * 6.0;
        const int ground = terrainLevelAtRuntimeCell(state,
            static_cast<int>(std::lround(shot.currentX + vx * ahead)),
            static_cast<int>(std::lround(shot.currentY + vy * ahead)));
        const double cruise = ground + nativeCellHeight * std::min(
            std::floor(std::hypot(horizontal, dz)), type.veryHigh || type.airburst ? 10.0 : 5.0);
        const double heightError = shot.currentZ - cruise;
        // Native 0x5B2744 has both an 18-lepton height correction and pitch
        // guidance. This produces the sloping launch rather than a sine arc.
        if (heightError < -20.0 * heightPerLepton) shot.currentZ += 18.0 * heightPerLepton * dt;
        else if (heightError > 20.0 * heightPerLepton) shot.currentZ -= 18.0 * heightPerLepton * dt;
        const double desiredPitch = heightError < -nativeCellHeight / 2.0 ? pi / 4.0
            : heightError > nativeCellHeight / 2.0 ? -pi / 16.0 : 0.0;
        shot.homingPitch = turn(shot.homingPitch, desiredPitch, rate / 2.0);
    } else {
        shot.homingPitch = turn(shot.homingPitch, std::atan2(dz, horizontal), rate);
    }
    return false;
}
