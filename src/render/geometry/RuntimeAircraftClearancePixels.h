#pragma once

#include "simulation/effects/RuntimeParticlePresentationValues.h"

namespace concord::render::geometry {
int aircraftClearanceRoofHeightPixels(const RuntimeParticleClearanceRoof& roof, int tileHeight);

int aircraftAirborneClearanceLiftPixels(const RuntimeParticleAirbornePresentation& presentation,
    int tileHeight, int screenOffsetY);
}
