#pragma once

#include "RulesDatabase.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>

inline constexpr double runtimeMissileHalfPi = 1.5707963267948966;
inline constexpr double runtimeMissileLevelsPerCell = 2.449489742783178;
inline constexpr int runtimeMissileExhaustFlameFrames = 3;

inline int runtimeMissileExhaustFrameDelay(int rate, int fixedTickHz) {
    const int delay30 = rate > 0 ? std::clamp((900 + rate / 2) / rate, 1, 36) : 6;
    return std::max(1, static_cast<int>(std::lround(delay30 * fixedTickHz / 30.0)));
}

inline int runtimeMissileExhaustEmissionDelay(int rate, int fixedTickHz) {
    // Smoke emission is independent of the original animation playback rate.
    // The nozzle flame remains attached while each detached puff dissipates.
    return runtimeMissileExhaustFrameDelay(rate, fixedTickHz) * 3;
}

inline const ProjectileType* runtimeNuclearMissileProjectile(const RulesDatabase& rules,
    const std::string& resourceId) {
    if (const auto* type = rules.findProjectileType(resourceId)) return type;
    const auto* weapon = rules.findWeaponType(resourceId);
    // Stock SpecialWeapons retains the obsolete NukeUp/NukeDown names;
    // the actual YR weapons point to GiantNukeUp/GiantNukeDown and their SHPs.
    if (!weapon && resourceId == "NukeUp") weapon = rules.findWeaponType("NukeCarrier");
    if (!weapon && resourceId == "NukeDown") weapon = rules.findWeaponType("NukePayload");
    return weapon ? rules.findProjectileType(weapon->projectile) : nullptr;
}

inline std::string runtimeMissileExhaustImage(const ProjectileType* type, std::string image) {
    std::transform(image.begin(), image.end(), image.begin(), [](unsigned char c) { return std::toupper(c); });
    if (image == "NKMSLUP" || image == "NKMSLDN") return "NUKEPUFF";
    if (image == "V3ROCKET" || image == "DMISL" || image == "BSUBMISL") return "V3TRAIL";
    if (type == nullptr || type->inviso || type->imageIsNone || type->dropping
        || type->id == "NormalBomb" || type->id == "DepthCharge") return {};
    // These are the stock missile images. ROT alone also describes jumping
    // dogs, terror drones and torpedoes, which must not gain rocket exhaust.
    if (image == "DRAGON" || image == "MEDUSA" || image == "DREDMISS"
        || image == "MISLCHEM" || image == "MISLMLTI") return "SMOKEY2";
    return {};
}

inline std::array<double, 3> runtimeMissileExhaustOrigin(double x, double y, double z,
    double heading, double pitch, double bodyLengthLeptons) {
    const double halfBody = std::max(0.0, bodyLengthLeptons) / 512.0;
    return {x - std::cos(heading) * std::cos(pitch) * halfBody,
        y - std::sin(heading) * std::cos(pitch) * halfBody,
        z - std::sin(pitch) * halfBody * runtimeMissileLevelsPerCell};
}
