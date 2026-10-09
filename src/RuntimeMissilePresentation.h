#pragma once

#include "RuntimeParticleSystem.h"
#include "RuntimeSpecialMechanisms.h"

#include <algorithm>
#include <array>
#include <cmath>

// The launched body's saved muzzle attachment fades over two cells of actual
// travel. Smoke freezes this value when emitted, instead of following the body.
inline RuntimeParticleSpriteAttachment runtimeSpawnedMissileSpriteAttachment(
    const SpawnedMissileState& missile
) {
    const double dx = missile.x - missile.originX;
    const double dy = missile.y - missile.originY;
    const double dz = missile.z - missile.originZ;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz / 6.0);
    const double weight = missile.released
        ? std::clamp(1.0 - distance / 2.0, 0.0, 1.0) : 1.0;
    RuntimeParticleSpriteAttachment result;
    result.screenOffsetX = static_cast<int>(std::lround(missile.originScreenOffsetX * weight));
    result.screenOffsetY = static_cast<int>(std::lround(missile.originScreenOffsetY * weight));
    result.nativeFixedX = static_cast<int>(std::lround(missile.originNativeFixedX * weight));
    result.nativeFixedY = static_cast<int>(std::lround(missile.originNativeFixedY * weight));
    result.voxelAnchor = true;
    result.airborne = missile.released;
    return result;
}

struct RuntimeSpawnedMissileSmokePose {
    RuntimeParticlePoint origin;
    RuntimeParticleSpriteAttachment spriteAttachment;
};

inline RuntimeSpawnedMissileSmokePose runtimeSpawnedMissileDamageSmokePose(
    const SpawnedMissileState& missile,
    const std::array<int, 3>& damageSmokeOffset
) {
    // Retail GetParticleSysOffset uses world axes. Preserve authored offsets;
    // generated missiles have their own voxel anchor and no aircraft body lift.
    return {{missile.x * 256.0 + damageSmokeOffset[0],
        missile.y * 256.0 + damageSmokeOffset[1],
        missile.z * 256.0 + damageSmokeOffset[2]},
        runtimeSpawnedMissileSpriteAttachment(missile)};
}
