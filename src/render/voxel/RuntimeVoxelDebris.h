#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

// Retail BounceClass uses single-precision leptons, one update per game frame.
// Host rendering uses cells in XY and isometric height levels in Z.
inline constexpr double runtimeDebrisHeightPerLepton = 2.449489742783178 / 256.0;
inline constexpr double runtimeDebrisGravity = 1.399999976158142;
inline constexpr double runtimeDebrisRadiansPerDegree = 0.01745277777777778;

struct RuntimeVoxelDebrisState {
    bool enabled = false;
    int framesLeft = 0;
    int frameRemainder = 0;
    int frameCount = 0;
    int bounces = 0;
    double elasticity = 0.8;
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 4> orientation{0, 0, 0, 1};
    std::array<float, 4> spin{0, 0, 0, 1};
};

inline std::uint64_t runtimeDebrisRandom(std::uint64_t& seed) {
    // Retain Concord's deterministic stream; this is not Westwood's global RNG.
    auto value = (seed += 0x9e3779b97f4a7c15ull);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

inline double runtimeDebrisModulo(std::uint64_t& seed, double width) {
    // The retail division truncates the interval before taking the remainder.
    // Invalid mod data must not divide by zero or overflow a signed integer.
    const auto modulus = static_cast<std::uint64_t>(std::clamp(std::trunc(width), 1.0, 2147483647.0));
    return static_cast<double>(runtimeDebrisRandom(seed) % modulus);
}

inline RuntimeVoxelDebrisState runtimeMakeVoxelDebris(
    double x, double y, double z, int duration, double elasticity,
    double minZ, double maxZ, double maxXY, double minAngularRadians,
    double maxAngularRadians, std::uint64_t& seed
) {
    RuntimeVoxelDebrisState motion;
    motion.enabled = true;
    motion.framesLeft = std::clamp(duration, 0, 1000000);
    motion.elasticity = std::clamp(elasticity, 0.0, 1.0);
    // VoxelAnimClass::ctor (0x7494EE) adds ten leptons to object coordinates.
    motion.position = {static_cast<float>(std::trunc(x * 256.0)),
        static_cast<float>(std::trunc(y * 256.0)),
        static_cast<float>(std::trunc(z / runtimeDebrisHeightPerLepton) + 10.0)};
    const double angular = minAngularRadians
        + runtimeDebrisModulo(seed, maxAngularRadians - minAngularRadians + 1.0);
    motion.velocity[2] = static_cast<float>(minZ + runtimeDebrisModulo(seed, maxZ - minZ + 1.0));
    motion.velocity[0] = static_cast<float>(maxXY > 0 ? runtimeDebrisModulo(seed, 2 * maxXY) - maxXY : 0.0);
    motion.velocity[1] = static_cast<float>(maxXY > 0 ? runtimeDebrisModulo(seed, 2 * maxXY) - maxXY : 0.0);
    std::array<double, 3> axis{};
    for (auto& component : axis) component = (static_cast<int>(runtimeDebrisRandom(seed) % 131071) - 65535) / 65535.0;
    double length = std::sqrt(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
    if (length == 0.0) { axis = {1, 0, 0}; length = 1.0; }
    for (int i = 0; i < 3; ++i) motion.spin[i] = static_cast<float>(axis[i] / length * std::sin(angular * 0.5));
    motion.spin[3] = static_cast<float>(std::cos(angular * 0.5));
    return motion;
}

struct RuntimeDebrisSurface {
    bool exists = true;
    double height = 0.0; // leptons
    std::array<double, 3> normal{0, 0, 1};
    bool water = false;
    bool ceiling = false;
};

enum class RuntimeDebrisContact { None, Bounce, Stopped, Water, Outside };

inline void runtimeRotateVoxelDebris(RuntimeVoxelDebrisState& motion) {
    const auto a = motion.orientation, b = motion.spin;
    motion.orientation = {
        a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
        a[3]*b[1] + a[1]*b[3] + a[2]*b[0] - a[0]*b[2],
        a[3]*b[2] + a[2]*b[3] + a[0]*b[1] - a[1]*b[0],
        a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2]};
}

template<class SurfaceAt>
inline RuntimeDebrisContact runtimeStepVoxelDebris(RuntimeVoxelDebrisState& motion, SurfaceAt&& surfaceAt) {
    const auto previous = motion.position;
    motion.velocity[2] = static_cast<float>(motion.velocity[2] - runtimeDebrisGravity);
    for (int i = 0; i < 3; ++i) motion.position[i] += motion.velocity[i];
    const auto surface = surfaceAt(previous, motion.position);
    if (!surface.exists) return RuntimeDebrisContact::Outside;
    const bool contact = surface.ceiling ? motion.position[2] >= surface.height : motion.position[2] < surface.height;
    if (contact) {
        motion.position[2] = static_cast<float>(surface.height);
        double dot = 0;
        for (int i = 0; i < 3; ++i) dot += motion.velocity[i] * surface.normal[i];
        for (int i = 0; i < 3; ++i) {
            motion.velocity[i] = static_cast<float>((motion.velocity[i] - 2 * dot * surface.normal[i]) * motion.elasticity);
            // Retail reverses the incremental quaternion's vector part on bounce.
            motion.spin[i] = -motion.spin[i];
        }
        ++motion.bounces;
    }
    runtimeRotateVoxelDebris(motion);
    const double height = std::trunc(motion.position[2] - surface.height);
    const double vx = std::trunc(motion.velocity[0]), vy = std::trunc(motion.velocity[1]);
    const double vz = std::trunc(motion.velocity[2] + height * runtimeDebrisGravity);
    // 0x439A10 + 0x43A087: an integer magnitude, compared against 2.5.
    if (std::trunc(std::sqrt(vx*vx + vy*vy + vz*vz)) < 2.5) return RuntimeDebrisContact::Stopped;
    if (contact && surface.water) return RuntimeDebrisContact::Water;
    return contact ? RuntimeDebrisContact::Bounce : RuntimeDebrisContact::None;
}

inline std::string serializeRuntimeVoxelDebris(const RuntimeVoxelDebrisState& motion) {
    if (!motion.enabled) return "-";
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(17) << 1 << ' ' << motion.framesLeft << ' ' << motion.frameRemainder
        << ' ' << motion.frameCount << ' ' << motion.bounces << ' ' << motion.elasticity;
    for (float value : motion.position) out << ' ' << value;
    for (float value : motion.velocity) out << ' ' << value;
    for (float value : motion.orientation) out << ' ' << value;
    for (float value : motion.spin) out << ' ' << value;
    return out.str();
}

inline bool parseRuntimeVoxelDebris(const std::string& text, RuntimeVoxelDebrisState& motion) {
    if (text == "-") { motion = {}; return true; }
    std::istringstream in(text); in.imbue(std::locale::classic());
    RuntimeVoxelDebrisState candidate; int version = 0;
    if (!(in >> version >> candidate.framesLeft >> candidate.frameRemainder >> candidate.frameCount
        >> candidate.bounces >> candidate.elasticity) || version != 1) return false;
    for (auto* values : {&candidate.position, &candidate.velocity}) for (float& value : *values)
        if (!(in >> value) || !std::isfinite(value) || std::abs(value) > 10000000.0f) return false;
    for (auto* values : {&candidate.orientation, &candidate.spin}) {
        double norm = 0;
        for (float& value : *values) {
            if (!(in >> value) || !std::isfinite(value)) return false;
            norm += value * value;
        }
        if (std::abs(norm - 1.0) > 0.02) return false;
    }
    in >> std::ws;
    if (!in.eof() || candidate.framesLeft < 0 || candidate.framesLeft > 1000000
        || candidate.frameRemainder < 0 || candidate.frameRemainder > 1000
        || candidate.frameCount < 0 || candidate.frameCount > 1000000
        || candidate.bounces < 0 || candidate.bounces > candidate.frameCount
        || !std::isfinite(candidate.elasticity) || candidate.elasticity < 0 || candidate.elasticity > 1) return false;
    candidate.enabled = true; motion = candidate; return true;
}
