#pragma once
#include "world/model/GameWorld.h"
#include "render/units/RuntimeVehicleBodyDynamics.h"
#include <algorithm>
#include <cmath>

// Detached death presentation. The live object has already left combat,
// selection, transport and occupancy; this value never enters those systems.
struct RuntimeWreckState {
    WorldObjectInstance object;
    int startTick = 0;
    bool sinking = false;
    bool jumpjet = false;
    bool tilt = false;
    double x = 0.0;
    double y = 0.0;
    double startLevel = 0.0;
    double groundLevel = 0.0;
    double fallSpeed = 0.0;
    double gravity = 6.0;
    double driftX = 0.0;
    double driftY = 0.0;
    std::string impactAnim;
    std::string warhead;
};

inline double runtimeWreckAgeFrames(const RuntimeWreckState& wreck, int tick, int hz) {
    return std::max(0, tick - wreck.startTick) * 15.0 / std::max(1, hz);
}

inline double runtimeWreckFallLeptons(const RuntimeWreckState& wreck, double frames) {
    if (wreck.sinking) return frames * 5.0;
    return wreck.jumpjet ? frames * wreck.fallSpeed
        : wreck.gravity * frames * (frames + 1.0) * 0.5;
}

inline WorldObjectInstance runtimeWreckObjectPose(const RuntimeWreckState& wreck, int tick, int hz) {
    WorldObjectInstance object = wreck.object;
    const double frames = runtimeWreckAgeFrames(wreck, tick, hz);
    object.alive = false; object.health = 0;
    object.mission = wreck.sinking ? "SinkingWreck" : "CrashingWreck";
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = wreck.x + wreck.driftX * frames;
    object.renderY = wreck.y + wreck.driftY * frames;
    const double fallHeight = std::max(1.0, (wreck.startLevel - wreck.groundLevel - 34.0 / 15.0) * 256.0);
    object.renderLevel = wreck.sinking ? wreck.groundLevel
        : wreck.startLevel - runtimeWreckFallLeptons(wreck, frames) / fallHeight * (wreck.startLevel - wreck.groundLevel);
    object.x = static_cast<int>(std::lround(object.renderX));
    object.y = static_cast<int>(std::lround(object.renderY));
    object.level = static_cast<int>(std::lround(object.renderLevel));
    object.airborne = !wreck.sinking;
    object.dockedToObjectId = -1; object.dockIndex = -1;
    object.garrisonHostObjectId = -1; object.tankBunkerHostId = -1;
    object.submerged = false; object.cloaked = false;
    return object;
}

inline RuntimeVehicleBodyPose runtimeWreckBodyPose(const RuntimeWreckState& wreck, int tick, int hz) {
    if (wreck.sinking || !wreck.tilt) return {};
    const double frames = runtimeWreckAgeFrames(wreck, tick, hz);
    const double sign = (wreck.object.instanceId & 1) ? 1.0 : -1.0;
    return {std::min(0.8, frames * 0.025), sign * std::min(1.1, frames * 0.035), 0.0};
}

inline const RuntimeWreckState* runtimeWreckForObject(
    const std::vector<RuntimeWreckState>* wrecks, const WorldObjectInstance& object) {
    if (!wrecks || object.alive || (object.mission != "CrashingWreck" && object.mission != "SinkingWreck"))
        return nullptr;
    for (const auto& wreck : *wrecks) if (wreck.object.instanceId == object.instanceId) return &wreck;
    return nullptr;
}
