#pragma once

#include "simulation/movement/RuntimeObjectBehavior.h"

#include <array>

struct WorldObjectInstance;
struct UnitType;
enum class PathMovementMode;

// Synchronous, non-owning queries at the original short-circuit call sites.
// Returned rule data is borrowed only for the current call.
class RuntimeVehicleArrivalQueries {
public:
    virtual ~RuntimeVehicleArrivalQueries() = default;
    virtual const UnitType* unitType(const WorldObjectInstance&) const = 0;
    virtual PathMovementMode movementMode(const WorldObjectInstance&) const = 0;
    virtual bool factoryEgressBypassesArrival(const WorldObjectInstance&, const RuntimeObjectBehavior&) const = 0;
};

double runtimeVehicleStartupDistanceBefore(const RuntimeObjectBehavior& behavior,
    double length);

double runtimeVehicleStartupRenderProgress(const RuntimeObjectBehavior& behavior, double elapsed);

void replaceRuntimeBehaviorPath(
    RuntimeObjectBehavior& behavior,
    std::vector<RuntimeCellPoint> path
);

bool runtimeObjectUsesVehicleArrival(const RuntimeVehicleArrivalQueries& queries,
    const WorldObjectInstance& object);

double prepareRuntimeVehicleArrival(const RuntimeVehicleArrivalQueries& queries,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to);

double runtimeVehicleMoveRenderProgressAtRemaining(const RuntimeObjectBehavior& behavior, int remainingTicks);

double runtimeVehicleMoveRenderProgress(const RuntimeObjectBehavior& behavior);

bool runtimeInfantryOffsetStartsAtSourceSubcell(int subCell, double fromOffsetX, double fromOffsetY);

bool runtimeInfantryMoveUsesCommittedCorner(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move);

std::array<double, 2> runtimeGroundRenderMovePoint(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move, double progress);

void restoreRuntimeVehicleMovePresentation(WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior);

void inheritRuntimeVehicleMoveSegment(WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, const RuntimeObjectBehavior& previous);
