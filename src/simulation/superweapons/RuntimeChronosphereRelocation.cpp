#include "simulation/superweapons/RuntimeChronosphereRelocation.h"

#include "world/model/GameWorld.h"

namespace {
RuntimeChronospherePose poseOf(const WorldObjectInstance& object) {
    return {object.x, object.y, object.cell, object.level, object.subCell,
        object.onBridge, object.airborne, object.hasRenderPosition,
        object.renderMoveActive, object.renderOnBridge,
        object.renderX, object.renderY, object.renderLevel};
}
}

RuntimeChronosphereRelocationResult commitRuntimeChronosphereRelocation(
    WorldObjectInstance* object,
    const RuntimeChronosphereRelocationRequest& request,
    RuntimeChronosphereRelocationPort& port) {
    RuntimeChronosphereRelocationResult result;
    result.request = request;
    if (!object) return result;
    result.oldPose = poseOf(*object);
    object->x = request.targetX;
    object->y = request.targetY;
    object->cell = request.targetCell;
    if (port.hasTerrainLevel())
        object->level = port.terrainLevel(request.targetX, request.targetY);
    if (request.arrival == RuntimeChronosphereArrival::FatalWater)
        object->onBridge = object->renderOnBridge = false;
    object->hasRenderPosition = false;
    object->renderMoveActive = false;
    if (request.arrival == RuntimeChronosphereArrival::Formation)
        object->mission = object->mission.empty() ? "Guard" : object->mission;
    result.newPose = poseOf(*object);
    result.healthAtNotification = object->health;
    result.applied = true;
    port.onRelocated(result);
    return result;
}
