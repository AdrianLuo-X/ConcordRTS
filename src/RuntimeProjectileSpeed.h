#pragma once

#include "RulesDatabase.h"
#include "RuntimeRuleTime.h"
#include <bit>

// Retail 0x48AB90 multiplies Range (integer leptons), Gravity and 1.2 with
// the x87 toward-zero control word. 0x4CAC40 then uses the top 13 mantissa
// bits for its square-root table. Reconstruct just the selected entry rather
// than distributing the original table or changing global rounding modes.
inline int runtimeBallisticSpeedLeptons(int rangeLeptons, double gravity) {
    const double base = static_cast<double>(rangeLeptons) * gravity;
    double product = base * 1.2;
    if (product <= 0.0) return 1;
    if (std::fma(base, 1.2, -product) < 0.0) product = std::nextafter(product, 0.0);
    float input = static_cast<float>(product);
    if (static_cast<double>(input) > product) input = std::nextafter(input, 0.0f);
    const auto bits = std::bit_cast<std::uint32_t>(input);
    const int exponent = static_cast<int>((bits >> 23U) & 255U) - 127;
    const auto index = ((bits & 0x7fffffU) >> 10U) + ((exponent & 1) ? 8192U : 0U);
    const double normalized = (1.0 + static_cast<double>(index % 8192U) / 8192.0)
        * (index >= 8192U ? 2.0 : 1.0);
    const auto rootMantissa = static_cast<std::uint32_t>((std::sqrt(normalized) - 1.0) * 8388608.0);
    const auto rootExponent = static_cast<std::uint32_t>(static_cast<int>(std::floor(exponent / 2.0)) + 127);
    const float root = std::bit_cast<float>((rootExponent << 23U) + rootMantissa);
    return std::max(1, static_cast<int>(root));
}

// WeaponType::CalculateSpeed (0x7729F0) replaces ReadSpeed for ROT=0.
// Its result is already leptons per rule frame, not a Speed percentage.
inline int runtimeProjectileSpeedLeptons(const WeaponType& weapon,
    const ProjectileType* projectile, int gravity) {
    // Invisible relationship weapons retain their separate effect cadence.
    if (projectile != nullptr && !projectile->inviso && projectile->rot == 0) {
        const double range = std::isfinite(weapon.range) ? std::clamp(weapon.range, 0.0, 32767.0) : 0.0;
        const int rangeLeptons = static_cast<int>(range * 256.0);
        const double effectiveGravity = std::clamp(gravity, 0, 1000)
            * (projectile->floater ? 0.5 : 1.0);
        return runtimeBallisticSpeedLeptons(rangeLeptons, effectiveGravity);
    }
    return std::max(1, runtimeRuleSpeedLeptons(weapon.speed > 0 ? weapon.speed : 40));
}
