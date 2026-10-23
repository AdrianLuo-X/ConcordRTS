#include "render/core/RuntimeMapTerrainFrame.h"

RuntimeMapTerrainFrame prepareRuntimeMapTerrainFrame(
    const RuntimeMapTerrainFrameSources& sources,
    const RuntimeLightingSystemState& lighting) {
    RuntimeMapTerrainFrame frame;
    frame.theater = sources.theaterLoaded ? sources.theater : nullptr;
    frame.art = sources.artLoaded ? sources.art : nullptr;
    frame.rules = sources.rulesLoaded ? sources.rules : nullptr;
    frame.mapFacts = sources.mapFactsLoaded ? sources.mapFacts : nullptr;
    if (sources.visionRestricted) {
        frame.vision = sources.vision;
        frame.visionObserverOwner = sources.visionObserverOwner;
    }
    frame.lightingProfile = &lighting.currentProfile();
    frame.lightingTint = lighting.renderTint();
    if (sources.campaignMode
        && sources.campaignIonStormUntilTick > sources.tick
        && frame.mapFacts != nullptr) {
        frame.lightingProfile = &frame.mapFacts->lighting.ion;
        frame.lightingTint = runtimeLightingTintForProfile(*frame.lightingProfile);
    }
    return frame;
}
