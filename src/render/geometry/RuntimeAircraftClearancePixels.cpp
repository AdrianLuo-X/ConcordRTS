#include "RuntimeAircraftClearancePixels.h"

#include <algorithm>
#include <cmath>

namespace concord::render::geometry {
int aircraftClearanceRoofHeightPixels(
    const RuntimeParticleClearanceRoof& roof, int tileHeight
) {
    if (roof.rubble) return 0;
    const int heightPixelScale = std::max(4, tileHeight / 3);
    const int fallbackHeightPixels = std::max(18, tileHeight);
    return std::clamp(
        std::max({
            fallbackHeightPixels,
            roof.authoredHeight * heightPixelScale,
            roof.nativeSpriteHeight
        }),
        fallbackHeightPixels,
        512
    );
}

int aircraftAirborneClearanceLiftPixels(
    const RuntimeParticleAirbornePresentation& presentation,
    int tileHeight, int screenOffsetY
) {
    if (!presentation.enabled) return 0;
    if (presentation.factoryLiftCancellation >= 0)
        return -presentation.factoryLiftCancellation;
    if (presentation.resolvedClearanceLevel >= 0.0) {
        return static_cast<int>(std::lround(presentation.resolvedClearanceLevel
            * tileHeight / 2.0));
    }
    const auto projectedY = [tileHeight](double cellSum, double level) {
        return static_cast<int>(std::llround(cellSum * tileHeight / 2.0
            - level * tileHeight / 2.0));
    };
    const int aircraftY = projectedY(presentation.sourceCellSum, presentation.sourceLevel)
        + screenOffsetY;
    const int margin = std::max(4, tileHeight / 6);
    int lift = 0;
    for (const auto& roof : presentation.roofs) {
        const int top = projectedY(roof.cellSum, roof.level) + tileHeight / 2
            - aircraftClearanceRoofHeightPixels(roof, tileHeight);
        lift = std::max(lift, aircraftY - (top - margin));
    }
    // Fade the resolved clearance, including its camera-dependent fallback
    // height and margin. Scaling authored roof dimensions loses that shape.
    return static_cast<int>(std::lround(std::clamp(lift, 0, 512)
        * std::clamp(presentation.clearanceFraction, 0.0, 1.0)));
}
}
