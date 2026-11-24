#pragma once
#include "world/values/RuntimeCellPoint.h"
#include <optional>
struct WorldObjectInstance;
struct RuntimeObjectBehavior;
struct UnitType;
namespace concord::simulation::abilities {
class RuntimeChronoRecoveryQueries {
public:
    virtual ~RuntimeChronoRecoveryQueries() = default;
    virtual bool movementLocked(int objectId, int currentTick) const = 0;
    virtual bool inTransit(int objectId) const = 0;
};
class RuntimeChronoMoveQueries {
public:
    virtual ~RuntimeChronoMoveQueries() = default;
    virtual const UnitType* unitType(const WorldObjectInstance& object) const = 0;
    virtual std::optional<bool> transportAdmission(const WorldObjectInstance& object, const RuntimeCellPoint& destination) const = 0;
};
class RuntimeChronoPoseEffects {
public:
    virtual ~RuntimeChronoPoseEffects() = default;
    virtual void clearRenderPosition(WorldObjectInstance& object) = 0;
};
class RuntimeChronoLocomotionEffects : public RuntimeChronoPoseEffects {
public:
    virtual bool requestMove(WorldObjectInstance& object, const RuntimeCellPoint& destination, const std::optional<bool>& transportAdmission) = 0;
    virtual void releaseInfantrySubcellReservation(int objectId) = 0;
    virtual void finishRequestScope() noexcept = 0;
};
bool holdRuntimeChronoRecovery(const bool& initialized, WorldObjectInstance& object, RuntimeObjectBehavior& behavior, const int& currentTick, const RuntimeChronoRecoveryQueries& queries, RuntimeChronoPoseEffects& effects);
std::optional<bool> advanceRuntimeTeleporterMove(WorldObjectInstance& object, RuntimeObjectBehavior& behavior, RuntimeCellPoint destination, const RuntimeChronoMoveQueries& queries, RuntimeChronoLocomotionEffects& effects);
}
