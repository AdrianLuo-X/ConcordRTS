#include "simulation/movement/ground/RuntimeCommittedGroundMotion.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/navigation/pathfinding/PathGoalTypes.h"
#include "simulation/movement/ground/RuntimeVehicleArrivalMotion.h"
#include "simulation/movement/ground/RuntimeVehicleStartupMotion.h"
#include "world/model/GameWorldSubCellOffset.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.kind;v.moveTarget; }) {return encode(v.kind)+encode(v.state)+encode(v.moveTarget)+encode(v.targetObjectId)+encode(v.autoAcquired)+encode(v.forceFire)+encode(v.attackGround)+encode(v.attackBridge)+encode(v.attackBridgeCell)+encode(v.path)+encode(v.pathRequestPending)+encode(v.pendingPathTowardRange)+encode(v.pendingPathStart)+encode(v.pendingPathGoal)+encode(v.pendingHasSharedGoal)+encode(v.pendingSharedGoal)+encode(v.pendingAllowedOccupiedObjectId)+encode(v.pendingTargetObjectId)+encode(v.pendingRangeCells)+encode(v.pendingAllowNearbyTarget)+encode(v.pendingIgnoredObjectIds)+encode(v.repackMoveGroup)+encode(v.moveCooldownTicks)+encode(v.moveSegmentFrom)+encode(v.moveIntervalRemainderQ16)+encode(v.moveStartupDistanceQ16)+encode(v.renderStartupAccelerationDistance)+encode(v.factoryExitClearanceHostId)+encode(v.transportTransferHostId)+encode(v.transportTransferKind)+encode(v.weaponCooldownTicks)+encode(v.blockedTicks)+encode(v.pathReplans)+encode(v.nextRepathTick)+encode(v.returnToDeployed)+encode(v.approachSlotIndex)+encode(v.renderFrom)+encode(v.renderTo)+encode(v.renderFromOffsetX)+encode(v.renderFromOffsetY)+encode(v.renderToOffsetX)+encode(v.renderToOffsetY)+encode(v.renderFromLevel)+encode(v.renderToLevel)+encode(v.renderFromOnBridge)+encode(v.renderToOnBridge)+encode(v.renderMoveTicksTotal)+encode(v.renderMoveTicksRemaining)+encode(v.renderArrivalDistanceAfter);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.instanceId)+encode(v.kind)+encode(v.mapId)+encode(v.owner)+encode(v.type)+encode(v.health)+encode(v.x)+encode(v.y)+encode(v.cell)+encode(v.level)+encode(v.facing)+encode(v.turretFacing)+encode(v.bodyFacingClock)+encode(v.turretFacingClock)+encode(v.subCell)+encode(v.mission)+encode(v.tag)+encode(v.veterancy)+encode(v.veterancyExperience)+encode(v.crateArmorMultiplier)+encode(v.crateFirepowerMultiplier)+encode(v.crateSpeedMultiplier)+encode(v.crateCloakable)+encode(v.harvestAutoSearchSuspended)+encode(v.hasBeenCaptured)+encode(v.onBridge)+encode(v.authoredAircraft)+encode(v.alive)+encode(v.rubble)+encode(v.submerged)+encode(v.cloaked)+encode(v.cloakCooldownTicks)+encode(v.submergeCooldownTicks)+encode(v.deployed)+encode(v.deployStateTicksRemaining)+encode(v.prone)+encode(v.proneTicksRemaining)+encode(v.weaponCooldownTicksRemaining)+encode(v.spriteFiringStartTick)+encode(v.radiationEruptionCooldownTicksRemaining)+encode(v.hasRenderPosition)+encode(v.renderX)+encode(v.renderY)+encode(v.renderMoveActive)+encode(v.renderLevel)+encode(v.renderOnBridge)+encode(v.movementAnimationDistance)+encode(v.buildAnimTicksTotal)+encode(v.buildAnimTicksRemaining)+encode(v.structureRecoilStartTick)+encode(v.chargeTurretStartTick)+encode(v.workAnimationActive)+encode(v.poweredOperational)+encode(v.powerAnimationPausedAtTick)+encode(v.powerAnimationPausedTicks)+encode(v.factoryExitAnimationStartTick)+encode(v.factoryExitAnimationEndTick)+encode(v.factoryExitAirborne)+encode(v.factoryExitHostId)+encode(v.specialCashNextTick)+encode(v.specialCloneDeliveryObjectId)+encode(v.transportPassengerIds)+encode(v.bioReactorAnimationStartTick)+encode(v.bioReactorAnimationPoweredStartTick)+encode(v.activeGunnerMode)+encode(v.activeGunnerWeaponIndex)+encode(v.activeGunnerTurretIndex)+encode(v.tankBunkerHostId)+encode(v.tankBunkerOccupantId)+encode(v.tankBunkerAnimationStartTick)+encode(v.tankBunkerLowering)+encode(v.dockedToObjectId)+encode(v.dockIndex)+encode(v.airborne)+encode(v.flightLevel)+encode(v.aircraftDockFlightProgress)+encode(v.aircraftBuildingClearanceLevel)+encode(v.wallConnectionMask)+encode(v.garrisonHostObjectId)+encode(v.garrisonOccupantIds)+encode(v.garrisonFireCursor)+encode(v.currentAmmo)+encode(v.pendingAircraftAuxSounds)+encode(v.turretSoundActivityTick)+encode(v.activeTurretSoundEvent)+encode(v.pendingTurretSounds)+encode(v.modExtensions);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {return encode(v.initial)+encode(v.target)+encode(v.rate)+encode(v.elapsedNumerator)+encode(v.lastTick)+encode(v.lastFacing);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {return encode(v.initialized)+encode(v.technoType)+encode(v.shieldType)+encode(v.shieldCapacity)+encode(v.shieldHealth)+encode(v.shieldTimer)+encode(v.shieldOnline)+encode(v.innateDelayTicks)+encode(v.effects);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {return encode(v.sourceType)+encode(v.fromWarhead)+encode(v.remainingTicks)+encode(v.elapsedTicks);}
    else if constexpr (requires { v.event;v.takingOff; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.takingOff);}
    else if constexpr (requires { v.event;v.rotating; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.rotating);}
    else if constexpr (requires { v.center;v.members; }) {return encode(v.center)+encode(v.members);}
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}
template<class T> void fill(T& v,unsigned seed) {
    if constexpr(std::is_same_v<T,bool>)v=(seed&1u)!=0;
    else if constexpr(std::is_enum_v<T>)v=static_cast<T>(seed%3u);
    else if constexpr(std::is_integral_v<T>)v=static_cast<T>(static_cast<int>(seed%101u)-50);
    else if constexpr(std::is_floating_point_v<T>)v=static_cast<T>(static_cast<int>(seed%97u)-48)/8.0;
    else if constexpr(std::is_same_v<T,std::string>)v="sentinel-"+std::to_string(seed)+"; // |";
    else if constexpr(requires {v.emplace();v.has_value();}) {if(seed%3u){v.emplace();fill(*v,seed+1u);}else v.reset();}
    else if constexpr (requires { v.kind;v.moveTarget; }) {fill(v.kind,seed+1u); fill(v.state,seed+2u); fill(v.moveTarget,seed+3u); fill(v.targetObjectId,seed+4u); fill(v.autoAcquired,seed+5u); fill(v.forceFire,seed+6u); fill(v.attackGround,seed+7u); fill(v.attackBridge,seed+8u); fill(v.attackBridgeCell,seed+9u); fill(v.path,seed+10u); fill(v.pathRequestPending,seed+11u); fill(v.pendingPathTowardRange,seed+12u); fill(v.pendingPathStart,seed+13u); fill(v.pendingPathGoal,seed+14u); fill(v.pendingHasSharedGoal,seed+15u); fill(v.pendingSharedGoal,seed+16u); fill(v.pendingAllowedOccupiedObjectId,seed+17u); fill(v.pendingTargetObjectId,seed+18u); fill(v.pendingRangeCells,seed+19u); fill(v.pendingAllowNearbyTarget,seed+20u); fill(v.pendingIgnoredObjectIds,seed+21u); fill(v.repackMoveGroup,seed+22u); fill(v.moveCooldownTicks,seed+23u); fill(v.moveSegmentFrom,seed+24u); fill(v.moveIntervalRemainderQ16,seed+25u); fill(v.moveStartupDistanceQ16,seed+26u); fill(v.renderStartupAccelerationDistance,seed+27u); fill(v.factoryExitClearanceHostId,seed+28u); fill(v.transportTransferHostId,seed+29u); fill(v.transportTransferKind,seed+30u); fill(v.weaponCooldownTicks,seed+31u); fill(v.blockedTicks,seed+32u); fill(v.pathReplans,seed+33u); fill(v.nextRepathTick,seed+34u); fill(v.returnToDeployed,seed+35u); fill(v.approachSlotIndex,seed+36u); fill(v.renderFrom,seed+37u); fill(v.renderTo,seed+38u); fill(v.renderFromOffsetX,seed+39u); fill(v.renderFromOffsetY,seed+40u); fill(v.renderToOffsetX,seed+41u); fill(v.renderToOffsetY,seed+42u); fill(v.renderFromLevel,seed+43u); fill(v.renderToLevel,seed+44u); fill(v.renderFromOnBridge,seed+45u); fill(v.renderToOnBridge,seed+46u); fill(v.renderMoveTicksTotal,seed+47u); fill(v.renderMoveTicksRemaining,seed+48u); fill(v.renderArrivalDistanceAfter,seed+49u);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {fill(v.instanceId,seed+1u); fill(v.kind,seed+2u); fill(v.mapId,seed+3u); fill(v.owner,seed+4u); fill(v.type,seed+5u); fill(v.health,seed+6u); fill(v.x,seed+7u); fill(v.y,seed+8u); fill(v.cell,seed+9u); fill(v.level,seed+10u); fill(v.facing,seed+11u); fill(v.turretFacing,seed+12u); fill(v.bodyFacingClock,seed+13u); fill(v.turretFacingClock,seed+14u); fill(v.subCell,seed+15u); fill(v.mission,seed+16u); fill(v.tag,seed+17u); fill(v.veterancy,seed+18u); fill(v.veterancyExperience,seed+19u); fill(v.crateArmorMultiplier,seed+20u); fill(v.crateFirepowerMultiplier,seed+21u); fill(v.crateSpeedMultiplier,seed+22u); fill(v.crateCloakable,seed+23u); fill(v.harvestAutoSearchSuspended,seed+24u); fill(v.hasBeenCaptured,seed+25u); fill(v.onBridge,seed+26u); fill(v.authoredAircraft,seed+27u); fill(v.alive,seed+28u); fill(v.rubble,seed+29u); fill(v.submerged,seed+30u); fill(v.cloaked,seed+31u); fill(v.cloakCooldownTicks,seed+32u); fill(v.submergeCooldownTicks,seed+33u); fill(v.deployed,seed+34u); fill(v.deployStateTicksRemaining,seed+35u); fill(v.prone,seed+36u); fill(v.proneTicksRemaining,seed+37u); fill(v.weaponCooldownTicksRemaining,seed+38u); fill(v.spriteFiringStartTick,seed+39u); fill(v.radiationEruptionCooldownTicksRemaining,seed+40u); fill(v.hasRenderPosition,seed+41u); fill(v.renderX,seed+42u); fill(v.renderY,seed+43u); fill(v.renderMoveActive,seed+44u); fill(v.renderLevel,seed+45u); fill(v.renderOnBridge,seed+46u); fill(v.movementAnimationDistance,seed+47u); fill(v.buildAnimTicksTotal,seed+48u); fill(v.buildAnimTicksRemaining,seed+49u); fill(v.structureRecoilStartTick,seed+50u); fill(v.chargeTurretStartTick,seed+51u); fill(v.workAnimationActive,seed+52u); fill(v.poweredOperational,seed+53u); fill(v.powerAnimationPausedAtTick,seed+54u); fill(v.powerAnimationPausedTicks,seed+55u); fill(v.factoryExitAnimationStartTick,seed+56u); fill(v.factoryExitAnimationEndTick,seed+57u); fill(v.factoryExitAirborne,seed+58u); fill(v.factoryExitHostId,seed+59u); fill(v.specialCashNextTick,seed+60u); fill(v.specialCloneDeliveryObjectId,seed+61u); fill(v.transportPassengerIds,seed+62u); fill(v.bioReactorAnimationStartTick,seed+63u); fill(v.bioReactorAnimationPoweredStartTick,seed+64u); fill(v.activeGunnerMode,seed+65u); fill(v.activeGunnerWeaponIndex,seed+66u); fill(v.activeGunnerTurretIndex,seed+67u); fill(v.tankBunkerHostId,seed+68u); fill(v.tankBunkerOccupantId,seed+69u); fill(v.tankBunkerAnimationStartTick,seed+70u); fill(v.tankBunkerLowering,seed+71u); fill(v.dockedToObjectId,seed+72u); fill(v.dockIndex,seed+73u); fill(v.airborne,seed+74u); fill(v.flightLevel,seed+75u); fill(v.aircraftDockFlightProgress,seed+76u); fill(v.aircraftBuildingClearanceLevel,seed+77u); fill(v.wallConnectionMask,seed+78u); fill(v.garrisonHostObjectId,seed+79u); fill(v.garrisonOccupantIds,seed+80u); fill(v.garrisonFireCursor,seed+81u); fill(v.currentAmmo,seed+82u); fill(v.pendingAircraftAuxSounds,seed+83u); fill(v.turretSoundActivityTick,seed+84u); fill(v.activeTurretSoundEvent,seed+85u); fill(v.pendingTurretSounds,seed+86u); fill(v.modExtensions,seed+87u);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {fill(v.initial,seed+1u); fill(v.target,seed+2u); fill(v.rate,seed+3u); fill(v.elapsedNumerator,seed+4u); fill(v.lastTick,seed+5u); fill(v.lastFacing,seed+6u);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {fill(v.initialized,seed+1u); fill(v.technoType,seed+2u); fill(v.shieldType,seed+3u); fill(v.shieldCapacity,seed+4u); fill(v.shieldHealth,seed+5u); fill(v.shieldTimer,seed+6u); fill(v.shieldOnline,seed+7u); fill(v.innateDelayTicks,seed+8u); fill(v.effects,seed+9u);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {fill(v.sourceType,seed+1u); fill(v.fromWarhead,seed+2u); fill(v.remainingTicks,seed+3u); fill(v.elapsedTicks,seed+4u);}
    else if constexpr (requires { v.event;v.takingOff; }) {fill(v.event,seed+1u); fill(v.tick,seed+2u); fill(v.x,seed+3u); fill(v.y,seed+4u); fill(v.level,seed+5u); fill(v.takingOff,seed+6u);}
    else if constexpr (requires { v.event;v.rotating; }) {fill(v.event,seed+1u); fill(v.tick,seed+2u); fill(v.x,seed+3u); fill(v.y,seed+4u); fill(v.level,seed+5u); fill(v.rotating,seed+6u);}
    else if constexpr (requires { v.center;v.members; }) {fill(v.center,seed+1u); fill(v.members,seed+2u);}
    else if constexpr (requires { v.x;v.y; }) {fill(v.x,seed+1u); fill(v.y,seed+2u);}
    else if constexpr(requires {v.begin();v.end();typename T::value_type;}) {
        v.clear();for(unsigned n=0;n<seed%4u;++n){typename T::value_type item{};fill(item,seed+5u*n);
            if constexpr(requires {v.push_back(item);})v.push_back(item);else v.insert(item);}
    }else static_assert(sizeof(T)==0,"uninitialized complete field");
}
struct Probe final:RuntimeVehicleArrivalQueries {
    UnitType type;PathMovementMode mode=PathMovementMode::Ground;bool nullType=false,bypass=false;int throwAt=0;
    mutable int calls=0;mutable std::vector<std::string> trace;
    void record(std::string value) const {trace.push_back(std::move(value));if(++calls==throwAt)throw std::runtime_error("injected-query-"+std::to_string(calls));}
    const UnitType* unitType(const WorldObjectInstance& object) const override {record("unit:"+encode(object));return nullType?nullptr:&type;}
    PathMovementMode movementMode(const WorldObjectInstance& object) const override {record("mode:"+encode(object));return mode;}
    template<class B>bool factory(const WorldObjectInstance& object,const B& behavior)const {record("factory:"+encode(object)+encode(behavior));return bypass;}
    bool factoryEgressBypassesArrival(const WorldObjectInstance& object,const RuntimeObjectBehavior& behavior)const override{return factory(object,behavior);}
};
namespace original {
enum class RuntimeBehaviorKind {
    None,
    Move,
    Attack
};

enum class RuntimeBehaviorState {
    Idle,
    Queued,
    Moving,
    Blocked,
    Chasing,
    Attacking,
    Cooldown,
    Complete
};

struct RuntimeRepackMoveGroupIntent {
    RuntimeCellPoint center;
    // Full identities survive a peer's folding phase; only mobile members
    // are projected into the path request's soft occupancy exclusions.
    std::vector<int> members;
};

enum class RuntimeTransportTransferKind { None, Boarding, Egress };

struct RuntimeObjectBehavior {
    RuntimeBehaviorKind kind = RuntimeBehaviorKind::None;
    RuntimeBehaviorState state = RuntimeBehaviorState::Idle;
    RuntimeCellPoint moveTarget;
    int targetObjectId = -1;
    bool autoAcquired = false;
    bool forceFire = false;
    bool attackGround = false;
    bool attackBridge = false;
    // Fixed terrain target shared by bridge attacks and ground force-fire.
    RuntimeCellPoint attackBridgeCell;
    std::deque<RuntimeCellPoint> path;
    bool pathRequestPending = false;
    bool pendingPathTowardRange = false;
    RuntimeCellPoint pendingPathStart;
    RuntimeCellPoint pendingPathGoal;
    bool pendingHasSharedGoal = false;
    RuntimeCellPoint pendingSharedGoal;
    int pendingAllowedOccupiedObjectId = -1;
    int pendingTargetObjectId = -1;
    double pendingRangeCells = 0.0;
    bool pendingAllowNearbyTarget = false;
    std::unordered_set<int> pendingIgnoredObjectIds;
    std::optional<RuntimeRepackMoveGroupIntent> repackMoveGroup;
    int moveCooldownTicks = 0;
    // Authoritative departure cell, held until a movement segment finishes.
    // Rendering anchors may be reset independently or omitted on load.
    RuntimeCellPoint moveSegmentFrom{-1, -1};
    std::int32_t moveIntervalRemainderQ16 = 0;
    std::int64_t moveStartupDistanceQ16 = 0;
    double renderStartupAccelerationDistance = 0.0;
    // Explicit provenance of a fresh automatic factory yield, never a player order.
    int factoryExitClearanceHostId = -1;
    // Immutable presentation provenance for one already accepted adjacent cargo transfer.
    int transportTransferHostId = -1;
    RuntimeTransportTransferKind transportTransferKind = RuntimeTransportTransferKind::None;
    int weaponCooldownTicks = 0;
    int blockedTicks = 0;
    int pathReplans = 0;
    int nextRepathTick = 0;
    bool returnToDeployed = false;
    int approachSlotIndex = 0;
    RuntimeCellPoint renderFrom;
    RuntimeCellPoint renderTo;
    double renderFromOffsetX = 0.0, renderFromOffsetY = 0.0;
    double renderToOffsetX = 0.0, renderToOffsetY = 0.0;
    double renderFromLevel = 0.0;
    double renderToLevel = 0.0;
    bool renderFromOnBridge = false;
    bool renderToOnBridge = false;
    int renderMoveTicksTotal = 0;
    int renderMoveTicksRemaining = 0;
    // Derived segment presentation; the adjusted cooldown is authoritative.
    double renderArrivalDistanceAfter = -1.0;
};
struct OriginalArrivalQueryBinding {
    const Probe& queries;
    const OriginalArrivalQueryBinding& rules;
    explicit OriginalArrivalQueryBinding(const Probe& input):queries(input),rules(*this) {}
};
const UnitType* findRuntimeUnitType(const OriginalArrivalQueryBinding& state,const WorldObjectInstance& object) {return state.queries.unitType(object);}
PathMovementMode runtimeObjectMovementMode(const OriginalArrivalQueryBinding& state,const WorldObjectInstance& object) {return state.queries.movementMode(object);}
bool runtimeFactoryEgressBypassesArrival(const OriginalArrivalQueryBinding& state,const WorldObjectInstance& object,const RuntimeObjectBehavior& behavior) {return state.queries.factory(object,behavior);}
double runtimeVehicleStartupDistanceBefore(const RuntimeObjectBehavior& behavior,
    double length) {
    const auto segmentDistance = RuntimeVehicleStartupMotion::distanceQ16(length);
    return double(std::max<std::int64_t>(0, behavior.moveStartupDistanceQ16-segmentDistance))
        / RuntimeVehicleStartupMotion::distanceOneQ16;
}
double runtimeVehicleStartupRenderProgress(const RuntimeObjectBehavior& behavior, double elapsed) {
    const double length = std::hypot(double(behavior.renderTo.x-behavior.renderFrom.x),
        double(behavior.renderTo.y-behavior.renderFrom.y));
    return RuntimeVehicleStartupMotion::progress(elapsed, length,
        runtimeVehicleStartupDistanceBefore(behavior, length),
        behavior.renderStartupAccelerationDistance, behavior.renderArrivalDistanceAfter);
}
bool runtimeObjectUsesVehicleArrival(const OriginalArrivalQueryBinding& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (type == nullptr || type->organic) return false;
    const auto mode = runtimeObjectMovementMode(state, object);
    return mode == PathMovementMode::Ground || mode == PathMovementMode::Amphibious;
}

double prepareRuntimeVehicleArrival(const OriginalArrivalQueryBinding& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    behavior.renderArrivalDistanceAfter = -1.0;
    if (runtimeFactoryEgressBypassesArrival(state, object, behavior)) return 1.0;
    if (behavior.kind != RuntimeBehaviorKind::Move
        || !runtimeObjectUsesVehicleArrival(state, object)
        || from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1)
        return 1.0;
    // The caller has consumed the current step. Only a complete remaining route
    // identifies an arrival: an empty local-avoidance/repath segment does not.
    if (behavior.path.empty() ? to != behavior.moveTarget
        : behavior.path.back() != behavior.moveTarget) return 1.0;
    if (behavior.path.size() >= 2) return 1.0;
    double distanceAfter = 0.0;
    auto previous = to;
    for (const auto& next : behavior.path) {
        if (next == previous || std::abs(next.x - previous.x) > 1
            || std::abs(next.y - previous.y) > 1) return 1.0;
        distanceAfter += std::hypot(double(next.x - previous.x), double(next.y - previous.y));
        previous = next;
    }
    behavior.renderArrivalDistanceAfter = distanceAfter;
    return RuntimeVehicleArrivalMotion::durationScale(
        std::hypot(double(to.x - from.x), double(to.y - from.y)), distanceAfter);
}

double runtimeVehicleMoveRenderProgressAtRemaining(const RuntimeObjectBehavior& behavior, int remainingTicks) {
    const double elapsed = 1.0 - double(remainingTicks)
        / std::max(1, behavior.renderMoveTicksTotal);
    if (behavior.renderStartupAccelerationDistance > 0.0)
        return runtimeVehicleStartupRenderProgress(behavior, elapsed);
    if (behavior.renderArrivalDistanceAfter < 0.0) return std::clamp(elapsed, 0.0, 1.0);
    return RuntimeVehicleArrivalMotion::progress(elapsed,
        std::hypot(double(behavior.renderTo.x - behavior.renderFrom.x),
            double(behavior.renderTo.y - behavior.renderFrom.y)),
        behavior.renderArrivalDistanceAfter);
}

double runtimeVehicleMoveRenderProgress(const RuntimeObjectBehavior& behavior) {
    return runtimeVehicleMoveRenderProgressAtRemaining(behavior, behavior.renderMoveTicksRemaining);
}

// A stopped infantry body may still occupy the preceding part of its
// committed step. Complete that step before turning; a direct chord can cut
// the neighbouring building corner once the infantry subcell is added. The
// route is derived from the existing saved endpoints, so restore and order
// inheritance use exactly the same curve.
bool runtimeInfantryOffsetStartsAtSourceSubcell(int subCell, double fromOffsetX, double fromOffsetY) {
    const auto destinationSlot = worldInfantrySubCellOffset(subCell);
    const double startX = destinationSlot.x + fromOffsetX;
    const double startY = destinationSlot.y + fromOffsetY;
    for (int sourceSubcell : {2, 4, 3}) {
        const auto sourceSlot = worldInfantrySubCellOffset(sourceSubcell);
        if (std::abs(startX - sourceSlot.x) <= 1.0e-9
            && std::abs(startY - sourceSlot.y) <= 1.0e-9) return true;
    }
    return false;
}

bool runtimeInfantryMoveUsesCommittedCorner(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move) {
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || move.renderFrom == move.renderTo
        || move.renderToOffsetX != 0.0 || move.renderToOffsetY != 0.0) return false;
    // Only an explicit accepted transport transfer bypasses the stopped-body pivot.
    // Its provenance is independent of the original host's current state.
    if (move.transportTransferHostId >= 0 && move.transportTransferKind != RuntimeTransportTransferKind::None) return false;
    // A slot change can shift the base while the body remains at a legal
    // source-cell slot. Start directly there instead of visiting a new slot
    // in the same cell, which could be occupied by another resident.
    if (runtimeInfantryOffsetStartsAtSourceSubcell(object.subCell,
        move.renderFromOffsetX, move.renderFromOffsetY)) return false;
    const double dx = move.renderTo.x - move.renderFrom.x;
    const double dy = move.renderTo.y - move.renderFrom.y;
    return std::abs(move.renderFromOffsetX * dy - move.renderFromOffsetY * dx) > 1.0e-9;
}

std::array<double, 2> runtimeGroundRenderMovePoint(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move, double progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    const double fromX = move.renderFrom.x + move.renderFromOffsetX;
    const double fromY = move.renderFrom.y + move.renderFromOffsetY;
    const double toX = move.renderTo.x + move.renderToOffsetX;
    const double toY = move.renderTo.y + move.renderToOffsetY;
    if (!runtimeInfantryMoveUsesCommittedCorner(object, move))
        return {fromX + (toX - fromX) * progress, fromY + (toY - fromY) * progress};
    const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
    const double second = std::hypot(toX - move.renderFrom.x, toY - move.renderFrom.y);
    const int ticks = std::max(2, move.renderMoveTicksTotal);
    const int firstTicks = std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
        1, ticks - 1);
    const double elapsed = ticks * progress;
    if (elapsed < firstTicks) {
        const double part = elapsed / firstTicks;
        return {fromX + (move.renderFrom.x - fromX) * part,
            fromY + (move.renderFrom.y - fromY) * part};
    }
    const double part = (elapsed - firstTicks) / (ticks - firstTicks);
    return {move.renderFrom.x + (toX - move.renderFrom.x) * part,
        move.renderFrom.y + (toY - move.renderFrom.y) * part};
}

void restoreRuntimeVehicleMovePresentation(WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.renderMoveTicksTotal <= 0 || behavior.renderMoveTicksRemaining <= 0) return;
    const double progress = runtimeVehicleMoveRenderProgress(behavior);
    object.hasRenderPosition = object.renderMoveActive = true;
    const auto point = runtimeGroundRenderMovePoint(object, behavior, progress);
    object.renderX = point[0]; object.renderY = point[1];
    object.renderLevel = behavior.renderFromLevel
        + (behavior.renderToLevel - behavior.renderFromLevel) * progress;
    object.renderOnBridge = progress < 0.5 ? behavior.renderFromOnBridge : behavior.renderToOnBridge;
}

// A new order takes effect after the already committed cell segment. Keeping
// its authoritative cooldown avoids advancing a ground unit or ship on a
// click, including vehicles in a braking pose. The replacement owns its goal.
void inheritRuntimeVehicleMoveSegment(WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, const RuntimeObjectBehavior& previous) {
    behavior.moveCooldownTicks = previous.moveCooldownTicks;
    behavior.moveSegmentFrom = previous.moveSegmentFrom;
    behavior.moveIntervalRemainderQ16 = previous.moveIntervalRemainderQ16;
    behavior.moveStartupDistanceQ16 = previous.moveStartupDistanceQ16;
    behavior.renderStartupAccelerationDistance = previous.renderStartupAccelerationDistance;
    behavior.renderFrom = previous.renderFrom;
    behavior.renderTo = previous.renderTo;
    behavior.renderFromOffsetX = previous.renderFromOffsetX;
    behavior.renderFromOffsetY = previous.renderFromOffsetY;
    behavior.renderToOffsetX = previous.renderToOffsetX;
    behavior.renderToOffsetY = previous.renderToOffsetY;
    behavior.renderFromLevel = previous.renderFromLevel;
    behavior.renderToLevel = previous.renderToLevel;
    behavior.renderFromOnBridge = previous.renderFromOnBridge;
    behavior.renderToOnBridge = previous.renderToOnBridge;
    behavior.renderMoveTicksTotal = previous.renderMoveTicksTotal;
    behavior.renderMoveTicksRemaining = previous.renderMoveTicksRemaining;
    behavior.renderArrivalDistanceAfter = previous.renderArrivalDistanceAfter;
    behavior.transportTransferHostId = previous.transportTransferHostId;
    behavior.transportTransferKind = previous.transportTransferKind;
    restoreRuntimeVehicleMovePresentation(object, behavior);
    if (previous.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
        // A replacement order cancels entry permission while keeping the exact shown body.
        // The original adjacent source still owns its navigation cell and infantry slot.
        object.renderMoveActive = false;
        behavior.moveCooldownTicks = behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
        behavior.moveSegmentFrom = {-1, -1};
        behavior.transportTransferHostId = -1;
        behavior.transportTransferKind = RuntimeTransportTransferKind::None;
    }
}

void replaceRuntimeBehaviorPath(
    RuntimeObjectBehavior& behavior,
    std::vector<RuntimeCellPoint> path
) {
    behavior.path.clear();
    behavior.path.insert(
        behavior.path.end(),
        std::make_move_iterator(path.begin()),
        std::make_move_iterator(path.end())
    );
}
}

static_assert(sizeof(RuntimeObjectBehavior)==sizeof(original::RuntimeObjectBehavior));
static_assert(alignof(RuntimeObjectBehavior)==alignof(original::RuntimeObjectBehavior));
static_assert(sizeof(RuntimeRepackMoveGroupIntent)==sizeof(original::RuntimeRepackMoveGroupIntent));
static_assert(alignof(RuntimeRepackMoveGroupIntent)==alignof(original::RuntimeRepackMoveGroupIntent));
static_assert(sizeof(RuntimeBehaviorKind)==sizeof(original::RuntimeBehaviorKind));
static_assert(sizeof(RuntimeBehaviorState)==sizeof(original::RuntimeBehaviorState));
static_assert(sizeof(RuntimeTransportTransferKind)==sizeof(original::RuntimeTransportTransferKind));
int checks=0,observations=0,scenarios=0;std::ostringstream retained;
void need(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
std::uint64_t fingerprint(const std::string& value){std::uint64_t h=1469598103934665603ULL;for(unsigned char c:value){h^=c;h*=1099511628211ULL;}return h;}
template<class A,class B>void pair(const std::string& label,A&& actual,B&& reference) {
    const auto a=actual(),b=reference();need(a==b,label);++observations;
    retained<<label<<'|'<<a.size()<<'|'<<fingerprint(a)<<"\n";
    if(observations<5)retained<<"complete="<<a<<"\n";
}
template<class B>B behavior(unsigned seed) {
    B b;fill(b,seed);b.renderMoveTicksTotal=static_cast<int>(seed%19u)-2;b.renderMoveTicksRemaining=static_cast<int>(seed%23u)-3;
    b.moveStartupDistanceQ16=static_cast<std::int64_t>(seed%17u)*16384;
    b.renderStartupAccelerationDistance=seed%3u?double(seed%8u)/4.0:0.0;
    b.renderArrivalDistanceAfter=double(static_cast<int>(seed%7u)-2)/2.0;
    return b;
}
template<class B>void prepareFixture(B& b,unsigned seed,const RuntimeCellPoint& from,const RuntimeCellPoint& to) {
    b.path.clear();b.kind=static_cast<decltype(b.kind)>(seed%3u);b.moveTarget=to;
    if(seed%4u==0)b.path.push_back(to);
    if(seed%4u==1){b.path.push_back({to.x+1,to.y});b.moveTarget={to.x+1,to.y};}
    if(seed%4u==2){b.path.push_back(to);b.path.push_back({to.x+2,to.y});b.moveTarget={to.x+2,to.y};}
    if(seed%4u==3){b.path.push_back(from);b.moveTarget={to.x+3,to.y+2};}
}
template<class B,class F>std::string queryObservation(Probe& q,WorldObjectInstance& object,B& b,F&& call) {
    std::string result;try{result="returned:"+encode(call());}catch(const std::exception& e){result="exception:"+std::string(e.what());}
    return result+encode(object)+encode(b)+encode(q.trace);
}
int main(int argc,char** argv)try {
    need(encode(RuntimeObjectBehavior{})==encode(original::RuntimeObjectBehavior{}),"all original defaults");
    for(unsigned seed=0;seed<512u;++seed){
        auto b=behavior<RuntimeObjectBehavior>(seed);auto old=behavior<original::RuntimeObjectBehavior>(seed);
        WorldObjectInstance object;fill(object,seed);auto other=object;
        need(encode(b)==encode(old),"complete seeded behavior");
        const auto id=std::to_string(seed);
        pair("pose-"+id,[&]{return encode(runtimeVehicleStartupDistanceBefore(b,1.25))+encode(runtimeVehicleStartupRenderProgress(b,0.375))+encode(runtimeVehicleMoveRenderProgress(b))+encode(runtimeVehicleMoveRenderProgressAtRemaining(b,-3))+encode(runtimeInfantryOffsetStartsAtSourceSubcell(object.subCell,b.renderFromOffsetX,b.renderFromOffsetY))+encode(runtimeInfantryMoveUsesCommittedCorner(object,b))+encode(runtimeGroundRenderMovePoint(object,b,0.375));},
          [&]{return encode(original::runtimeVehicleStartupDistanceBefore(old,1.25))+encode(original::runtimeVehicleStartupRenderProgress(old,0.375))+encode(original::runtimeVehicleMoveRenderProgress(old))+encode(original::runtimeVehicleMoveRenderProgressAtRemaining(old,-3))+encode(original::runtimeInfantryOffsetStartsAtSourceSubcell(other.subCell,old.renderFromOffsetX,old.renderFromOffsetY))+encode(original::runtimeInfantryMoveUsesCommittedCorner(other,old))+encode(original::runtimeGroundRenderMovePoint(other,old,0.375));});
        pair("restore-"+id,[&]{restoreRuntimeVehicleMovePresentation(object,b);return encode(object)+encode(b);},[&]{original::restoreRuntimeVehicleMovePresentation(other,old);return encode(other)+encode(old);});
        for(int alias=0;alias<2;++alias){auto next=behavior<RuntimeObjectBehavior>(seed+11u);auto previous=behavior<RuntimeObjectBehavior>(seed);auto oldNext=behavior<original::RuntimeObjectBehavior>(seed+11u);auto oldPrevious=behavior<original::RuntimeObjectBehavior>(seed);
          pair("inherit-"+id+"-"+std::to_string(alias),[&]{inheritRuntimeVehicleMoveSegment(object,next,alias?next:previous);return encode(object)+encode(next)+encode(previous);},[&]{original::inheritRuntimeVehicleMoveSegment(other,oldNext,alias?oldNext:oldPrevious);return encode(other)+encode(oldNext)+encode(oldPrevious);});}
        std::vector<RuntimeCellPoint> path;fill(path,seed+3u);
        pair("replace-"+id,[&]{replaceRuntimeBehaviorPath(b,path);return encode(b)+encode(path);},[&]{original::replaceRuntimeBehaviorPath(old,path);return encode(old)+encode(path);});
        for(int failure=0;failure<5;++failure){
          Probe q,r;q.type.organic=r.type.organic=(seed%5u==0);q.nullType=r.nullType=(seed%7u==0);q.bypass=r.bypass=(seed%11u==0);q.mode=r.mode=static_cast<PathMovementMode>(seed%5u);q.throwAt=r.throwAt=failure;
          original::OriginalArrivalQueryBinding native(r);auto a=behavior<RuntimeObjectBehavior>(seed);auto z=behavior<original::RuntimeObjectBehavior>(seed);
          pair("eligibility-"+id+"-"+std::to_string(failure),[&]{return queryObservation(q,object,a,[&]{return runtimeObjectUsesVehicleArrival(q,object);});},[&]{return queryObservation(r,other,z,[&]{return original::runtimeObjectUsesVehicleArrival(native,other);});});
          q.trace.clear();r.trace.clear();q.calls=r.calls=0;
          RuntimeCellPoint from{4,7},to{4+static_cast<int>(seed%3u)-1,7+static_cast<int>((seed/3u)%3u)-1};prepareFixture(a,seed,from,to);prepareFixture(z,seed,from,to);
          pair("prepare-"+id+"-"+std::to_string(failure),[&]{return queryObservation(q,object,a,[&]{return prepareRuntimeVehicleArrival(q,object,a,from,to);});},[&]{return queryObservation(r,other,z,[&]{return original::prepareRuntimeVehicleArrival(native,other,z,from,to);});});++scenarios;
        }
    }
    for(double length:{0.0,-1.0,1.25,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        const auto b=behavior<RuntimeObjectBehavior>(71u);const auto old=behavior<original::RuntimeObjectBehavior>(71u);
        pair("startup-distance-nonfinite-"+std::to_string(observations),[&]{return encode(runtimeVehicleStartupDistanceBefore(b,length));},[&]{return encode(original::runtimeVehicleStartupDistanceBefore(old,length));});
    }
    for(int turns=0;turns<4;++turns) {
        Probe q,r;WorldObjectInstance object;object.kind=WorldObjectKind::Unit;
        auto b=behavior<RuntimeObjectBehavior>(44u);auto old=behavior<original::RuntimeObjectBehavior>(44u);
        b.kind=RuntimeBehaviorKind::Move;old.kind=original::RuntimeBehaviorKind::Move;
        b.renderFrom=old.renderFrom={4,7};b.renderTo=old.renderTo={5,7};b.moveTarget=old.moveTarget={5,7};b.path.clear();old.path.clear();
        if(turns==1){b.path.push_back({6,7});old.path.push_back({6,7});b.moveTarget=old.moveTarget={6,7};}
        if(turns==2){b.path.push_back({6,8});old.path.push_back({6,8});b.moveTarget=old.moveTarget={6,8};}
        if(turns==3){b.path.push_back({6,7});b.path.push_back({8,7});old.path.push_back({6,7});old.path.push_back({8,7});b.moveTarget=old.moveTarget={8,7};}
        original::OriginalArrivalQueryBinding native(r);
        pair("actual-brake-alias-"+std::to_string(turns),[&]{return queryObservation(q,object,b,[&]{return prepareRuntimeVehicleArrival(q,object,b,b.renderFrom,b.renderTo);});},[&]{return queryObservation(r,object,old,[&]{return original::prepareRuntimeVehicleArrival(native,object,old,old.renderFrom,old.renderTo);});});
    }
    Probe q;WorldObjectInstance object;RuntimeObjectBehavior b;
    object.kind=WorldObjectKind::Infantry;need(!runtimeObjectUsesVehicleArrival(q,object)&&q.calls==0,"non-vehicle does not query");
    object.kind=WorldObjectKind::Unit;object.airborne=true;need(!runtimeObjectUsesVehicleArrival(q,object)&&q.calls==0,"airborne does not query");
    object.airborne=false;q.nullType=true;need(!runtimeObjectUsesVehicleArrival(q,object)&&q.trace.size()==1,"null rule short circuit");
    q.trace.clear();q.calls=0;q.nullType=false;q.type.organic=true;need(!runtimeObjectUsesVehicleArrival(q,object)&&q.trace.size()==1,"organic short circuit");
    q.trace.clear();q.calls=0;q.bypass=true;b.renderArrivalDistanceAfter=91.0;
    need(prepareRuntimeVehicleArrival(q,object,b,{1,1},{2,1})==1.0&&b.renderArrivalDistanceAfter==-1.0&&q.trace.size()==1,"factory reset precedes bypass/eligibility");
    b={};b.kind=RuntimeBehaviorKind::Move;b.renderFrom={1,2};b.renderTo={3,4};object.kind=WorldObjectKind::Unit;
    need(runtimeGroundRenderMovePoint(object,b,0.0)==std::array<double,2>{1,2},"straight start");need(runtimeGroundRenderMovePoint(object,b,1.0)==std::array<double,2>{3,4},"straight end");
    auto previous=b;previous.transportTransferKind=RuntimeTransportTransferKind::Boarding;previous.transportTransferHostId=8;previous.renderMoveTicksRemaining=4;previous.renderMoveTicksTotal=8;
    inheritRuntimeVehicleMoveSegment(object,b,previous);need(!object.renderMoveActive&&b.moveCooldownTicks==0&&b.renderMoveTicksTotal==0&&b.renderMoveTicksRemaining==0&&b.moveSegmentFrom==RuntimeCellPoint{-1,-1}&&b.transportTransferHostId==-1&&b.transportTransferKind==RuntimeTransportTransferKind::None,"boarding replacement cancels permission and preserves shown pose");
    if(argc>1){std::filesystem::create_directories(argv[1]);std::ofstream file(std::filesystem::path(argv[1])/"complete-observations.txt",std::ios::binary);file<<retained.str();need(static_cast<bool>(file),"retained observations");}
    std::cout<<"Committed ground: "<<checks<<" checks, "<<observations<<" complete observations, "<<scenarios<<" query/exception scenarios, 49 behavior fields/87 object fields, skipped=0\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"Committed ground failed: "<<e.what()<<'\n';return 1;}
