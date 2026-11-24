#pragma once
#include "world/values/RuntimeCellPoint.h"
#include <optional>
struct WorldObjectInstance;
struct RuntimeObjectBehavior;
struct UnitType;
struct ChronoMoveState;
namespace concord::simulation::abilities {
class RuntimeChronoTransportQueries {
public:
    virtual ~RuntimeChronoTransportQueries() = default;
    virtual const UnitType* unitType(const WorldObjectInstance& object) const = 0;
    virtual std::optional<int> pendingHostId(int objectId) const = 0;
    virtual const WorldObjectInstance* findIncludingInactive(int objectId) const = 0;
    virtual const RuntimeObjectBehavior* behavior(int objectId) const = 0;
    virtual bool canEnter(const WorldObjectInstance& passenger, const WorldObjectInstance& host) const = 0;
    virtual const ChronoMoveState* moveState(int objectId) const = 0;
    virtual bool inTransit(int objectId) const = 0;
};
bool runtimePersonalChronoTransportPassenger(const WorldObjectInstance& passenger, const RuntimeChronoTransportQueries& queries);
std::optional<bool> runtimeChronoTransportDestinationAdmission(const WorldObjectInstance& passenger, const RuntimeCellPoint& destination, const RuntimeChronoTransportQueries& queries);
bool runtimeChronoTransportArrivalReady(const WorldObjectInstance& passenger, const WorldObjectInstance& host, const int& currentTick, const RuntimeChronoTransportQueries& queries);
}
