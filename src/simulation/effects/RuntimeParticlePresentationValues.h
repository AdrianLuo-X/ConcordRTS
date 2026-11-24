#pragma once
#include <cstddef>
#include <vector>
inline constexpr std::size_t runtimeParticleMaximumClearanceRoofs = 64U;
struct RuntimeParticleClearanceRoof {
    double cellSum = 0.0;
    double level = 0.0;
    int authoredHeight = 0;
    int nativeSpriteHeight = 0;
    bool rubble = false;
    bool operator==(const RuntimeParticleClearanceRoof&) const = default;
};

struct RuntimeParticleAirbornePresentation {
    bool enabled = false;
    double sourceCellSum = 0.0;
    double sourceLevel = 0.0;
    int factoryLiftCancellation = -1;
    // Airport descent fades the complete roof clearance at the captured
    // flight height. Kept with the frozen geometry for every render zoom.
    double clearanceFraction = 1.0;
    // A managed Kirov freezes its already-smoothed display lift at emission.
    // This optional local presentation value is outside gameplay authority.
    double resolvedClearanceLevel = -1.0;
    std::vector<RuntimeParticleClearanceRoof> roofs;
    bool operator==(const RuntimeParticleAirbornePresentation&) const = default;
};
