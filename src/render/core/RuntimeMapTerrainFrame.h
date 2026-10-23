#pragma once

#include "render/lighting/RuntimeLightingSystem.h"

#include <string>

class ArtDatabase;
class RulesDatabase;
class TheaterDatabase;
class RuntimeVisionSystem;
struct MapFacts;

// Synchronous owner inputs. Readiness retains the existing nullable policy;
// every non-null pointer belongs to the caller. No world or render cache enters
// this boundary, and no resource, geometry or visibility-grid query is issued.
struct RuntimeMapTerrainFrameSources {
    const TheaterDatabase* theater = nullptr;
    bool theaterLoaded = false;
    const ArtDatabase* art = nullptr;
    bool artLoaded = false;
    const RulesDatabase* rules = nullptr;
    bool rulesLoaded = false;
    const MapFacts* mapFacts = nullptr;
    bool mapFactsLoaded = false;
    const RuntimeVisionSystem* vision = nullptr;
    bool visionRestricted = false;
    std::string visionObserverOwner;
    bool campaignMode = false;
    int campaignIonStormUntilTick = 0;
    int tick = 0;
};

// A frame owns its derived tint and observer text, and borrows only immutable
// databases, lighting profile and vision. Keep this value alive and unchanged
// until both map and interaction rendering and the geometry lease have ended.
// Do not persist it across resource reload, lighting/vision mutation or ticks.
// The existing renderer reads grid revision/layoutEpoch at its original query
// point; this preparation neither captures a stale revision nor invalidates a
// cache. Resource paths and all surface/presenter state remain at their owners.
struct RuntimeMapTerrainFrame {
    const TheaterDatabase* theater = nullptr;
    const ArtDatabase* art = nullptr;
    const RulesDatabase* rules = nullptr;
    const MapFacts* mapFacts = nullptr;
    const RuntimeVisionSystem* vision = nullptr;
    std::string visionObserverOwner;
    RuntimeLightingTint lightingTint;
    const MapLightingProfile* lightingProfile = nullptr;
};

RuntimeMapTerrainFrame prepareRuntimeMapTerrainFrame(
    const RuntimeMapTerrainFrameSources& sources,
    const RuntimeLightingSystemState& lighting);
