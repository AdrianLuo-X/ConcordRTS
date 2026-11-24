#include "simulation/abilities/chrono/RuntimeChronoTransportAdmission.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <bit>
#include <climits>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace chrono_transport_model_detail {
using Q=concord::simulation::abilities::RuntimeChronoTransportQueries;
namespace current=concord::simulation::abilities;
int checks=0,states=0,behaviorProbes=0,chronoProbes=0,faults=0,positive=0,negative=0,absent=0,aliases=0;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (std::is_same_v<T,ChronoMoveState>) { return encode(v.objectId)+encode(v.phase)+encode(v.source)+encode(v.destination)+encode(v.sourceLevel)+encode(v.destinationLevel)+encode(v.startTick)+encode(v.completeTick)+encode(v.cooldownUntilTick); }
    else if constexpr (std::is_same_v<T,SuperWeaponObjectLifecycleEvent>) {return encode(v.kind)+encode(v.removalDisposition)+encode(v.tick)+encode(v.objectId)+encode(v.healthBefore)+encode(v.healthAfter)+encode(v.superWeaponInstanceId)+encode(v.sourceOwner)+encode(v.superWeaponId)+encode(v.warheadId)+encode(v.reason)+encode(v.oldCell)+encode(v.newCell)+encode(v.oldOwner)+encode(v.newOwner);}
    else if constexpr (requires { v.voxelDebris;v.parentActorId;v.deathWeaponRequestId; }) { return encode(v.voxelDebris)+encode(v.homingInitialized)+encode(v.homingYaw)+encode(v.homingPitch)+encode(v.actorId)+encode(v.parentActorId)+encode(v.generation)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.sourceAnchorObjectId)+encode(v.owner)+encode(v.fromCell)+encode(v.toCell)+encode(v.currentCell)+encode(v.hasSourcePosition)+encode(v.sourceX)+encode(v.sourceY)+encode(v.sourceZ)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasTargetPosition)+encode(v.targetX)+encode(v.targetY)+encode(v.targetZ)+encode(v.fromLevel)+encode(v.toLevel)+encode(v.currentLevel)+encode(v.attackerFacing)+encode(v.targetFacing)+encode(v.spawnTick)+encode(v.impactTick)+encode(v.elapsedTicks)+encode(v.damage)+encode(v.veteranDamageMultiplier)+encode(v.spawnedMissileDamage)+encode(v.spawnedMissileBaseDamage)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.lifecycle)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.warheadId)+encode(v.weaponAnimId)+encode(v.impactAnimId)+encode(v.trailerAnimId)+encode(v.trailerIntentId)+encode(v.warheadImpactIntentId)+encode(v.reportSoundId)+encode(v.arcing)+encode(v.vertical)+encode(v.acceleration)+encode(v.high)+encode(v.veryHigh)+encode(v.shadow)+encode(v.animates)+encode(v.aa)+encode(v.ag)+encode(v.cluster)+encode(v.bright)+encode(v.laser)+encode(v.houseColorLaser)+encode(v.electricBolt)+encode(v.alternateElectricColor)+encode(v.electricColor)+encode(v.shrapnel)+encode(v.shrapnelReleased)+encode(v.laserDuration)+encode(v.laserThickness)+encode(v.cellSpread)+encode(v.percentAtMax)+encode(v.affectsAllies)+encode(v.affectsEnemies)+encode(v.particleId)+encode(v.progress)+encode(v.currentX)+encode(v.currentY)+encode(v.currentZ)+encode(v.speedCellsPerTick)+encode(v.rot)+encode(v.deathWeaponCascadeId)+encode(v.deathWeaponRequestId)+encode(v.deathWeaponCascadeDepth)+encode(v.launchSourceKind)+encode(v.launchSourceType)+encode(v.launchSourceAuthoredAircraft)+encode(v.launchSourceValidated)+encode(v.airstrikeStrikeId)+encode(v.airstrikePlaneToken)+encode(v.particleDamagePulse)+encode(v.suppressWarheadParticleSpawn);}
    else if constexpr (requires { v.framesLeft;v.orientation;v.spin; }) {return encode(v.enabled)+encode(v.framesLeft)+encode(v.frameRemainder)+encode(v.frameCount)+encode(v.bounces)+encode(v.elasticity)+encode(v.position)+encode(v.velocity)+encode(v.orientation)+encode(v.spin);}
    else if constexpr (requires { v.cell;v.x;v.y;v.level; } && !requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.cell)+encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.kind;v.moveTarget; }) {return encode(v.kind)+encode(v.state)+encode(v.moveTarget)+encode(v.targetObjectId)+encode(v.autoAcquired)+encode(v.forceFire)+encode(v.attackGround)+encode(v.attackBridge)+encode(v.attackBridgeCell)+encode(v.path)+encode(v.pathRequestPending)+encode(v.pendingPathTowardRange)+encode(v.pendingPathStart)+encode(v.pendingPathGoal)+encode(v.pendingHasSharedGoal)+encode(v.pendingSharedGoal)+encode(v.pendingAllowedOccupiedObjectId)+encode(v.pendingTargetObjectId)+encode(v.pendingRangeCells)+encode(v.pendingAllowNearbyTarget)+encode(v.pendingIgnoredObjectIds)+encode(v.repackMoveGroup)+encode(v.moveCooldownTicks)+encode(v.moveSegmentFrom)+encode(v.moveIntervalRemainderQ16)+encode(v.moveStartupDistanceQ16)+encode(v.renderStartupAccelerationDistance)+encode(v.factoryExitClearanceHostId)+encode(v.transportTransferHostId)+encode(v.transportTransferKind)+encode(v.weaponCooldownTicks)+encode(v.blockedTicks)+encode(v.pathReplans)+encode(v.nextRepathTick)+encode(v.returnToDeployed)+encode(v.approachSlotIndex)+encode(v.renderFrom)+encode(v.renderTo)+encode(v.renderFromOffsetX)+encode(v.renderFromOffsetY)+encode(v.renderToOffsetX)+encode(v.renderToOffsetY)+encode(v.renderFromLevel)+encode(v.renderToLevel)+encode(v.renderFromOnBridge)+encode(v.renderToOnBridge)+encode(v.renderMoveTicksTotal)+encode(v.renderMoveTicksRemaining)+encode(v.renderArrivalDistanceAfter);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.instanceId)+encode(v.kind)+encode(v.mapId)+encode(v.owner)+encode(v.type)+encode(v.health)+encode(v.x)+encode(v.y)+encode(v.cell)+encode(v.level)+encode(v.facing)+encode(v.turretFacing)+encode(v.bodyFacingClock)+encode(v.turretFacingClock)+encode(v.subCell)+encode(v.mission)+encode(v.tag)+encode(v.veterancy)+encode(v.veterancyExperience)+encode(v.crateArmorMultiplier)+encode(v.crateFirepowerMultiplier)+encode(v.crateSpeedMultiplier)+encode(v.crateCloakable)+encode(v.harvestAutoSearchSuspended)+encode(v.hasBeenCaptured)+encode(v.onBridge)+encode(v.authoredAircraft)+encode(v.alive)+encode(v.rubble)+encode(v.submerged)+encode(v.cloaked)+encode(v.cloakCooldownTicks)+encode(v.submergeCooldownTicks)+encode(v.deployed)+encode(v.deployStateTicksRemaining)+encode(v.prone)+encode(v.proneTicksRemaining)+encode(v.weaponCooldownTicksRemaining)+encode(v.spriteFiringStartTick)+encode(v.radiationEruptionCooldownTicksRemaining)+encode(v.hasRenderPosition)+encode(v.renderX)+encode(v.renderY)+encode(v.renderMoveActive)+encode(v.renderLevel)+encode(v.renderOnBridge)+encode(v.movementAnimationDistance)+encode(v.buildAnimTicksTotal)+encode(v.buildAnimTicksRemaining)+encode(v.structureRecoilStartTick)+encode(v.chargeTurretStartTick)+encode(v.workAnimationActive)+encode(v.poweredOperational)+encode(v.powerAnimationPausedAtTick)+encode(v.powerAnimationPausedTicks)+encode(v.factoryExitAnimationStartTick)+encode(v.factoryExitAnimationEndTick)+encode(v.factoryExitAirborne)+encode(v.factoryExitHostId)+encode(v.specialCashNextTick)+encode(v.specialCloneDeliveryObjectId)+encode(v.transportPassengerIds)+encode(v.bioReactorAnimationStartTick)+encode(v.bioReactorAnimationPoweredStartTick)+encode(v.activeGunnerMode)+encode(v.activeGunnerWeaponIndex)+encode(v.activeGunnerTurretIndex)+encode(v.tankBunkerHostId)+encode(v.tankBunkerOccupantId)+encode(v.tankBunkerAnimationStartTick)+encode(v.tankBunkerLowering)+encode(v.dockedToObjectId)+encode(v.dockIndex)+encode(v.airborne)+encode(v.flightLevel)+encode(v.aircraftDockFlightProgress)+encode(v.aircraftBuildingClearanceLevel)+encode(v.wallConnectionMask)+encode(v.garrisonHostObjectId)+encode(v.garrisonOccupantIds)+encode(v.garrisonFireCursor)+encode(v.currentAmmo)+encode(v.pendingAircraftAuxSounds)+encode(v.turretSoundActivityTick)+encode(v.activeTurretSoundEvent)+encode(v.pendingTurretSounds)+encode(v.modExtensions);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {return encode(v.initial)+encode(v.target)+encode(v.rate)+encode(v.elapsedNumerator)+encode(v.lastTick)+encode(v.lastFacing);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {return encode(v.initialized)+encode(v.technoType)+encode(v.shieldType)+encode(v.shieldCapacity)+encode(v.shieldHealth)+encode(v.shieldTimer)+encode(v.shieldOnline)+encode(v.innateDelayTicks)+encode(v.effects);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {return encode(v.sourceType)+encode(v.fromWarhead)+encode(v.remainingTicks)+encode(v.elapsedTicks);}
    else if constexpr (requires { v.event;v.takingOff; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.takingOff);}
    else if constexpr (requires { v.event;v.rotating; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.rotating);}
    else if constexpr (requires { v.center;v.members; }) {return encode(v.center)+encode(v.members);}
    else if constexpr (requires { v.x;v.y;v.level; }) {return encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}
template<class T> void mutateEncoderField(T& v) {
    if constexpr(std::is_same_v<T,bool>) v=!v;
    else if constexpr(std::is_enum_v<T>) v=static_cast<T>(static_cast<int>(v)==0?1:0);
    else if constexpr(std::is_integral_v<T>) v=v==0?1:0;
    else if constexpr(std::is_floating_point_v<T>) {
        using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
        v=std::bit_cast<T>(std::bit_cast<U>(v)^U{1});
    } else if constexpr(std::is_same_v<T,std::string>) v += "encoder-probe";
    else if constexpr(requires {v.elapsedNumerator;v.lastFacing;}) v.lastFacing=v.lastFacing==0?1:0;
    else if constexpr(requires {v.initialized;v.technoType;v.shieldType;}) v.initialized=!v.initialized;
    else if constexpr(requires {v.empty();v.clear();v.emplace_back();}) {if(v.empty())v.emplace_back();else v.clear();}
    else if constexpr(requires {v.x;v.y;}) v.x=v.x==0?1:0;
    else static_assert(sizeof(T)==0,"unprobed canonical WorldObject field");
}

template<class T>void perturb(T& v){
 if constexpr(std::is_same_v<T,bool>)v=!v;
 else if constexpr(std::is_enum_v<T>)v=static_cast<T>(19);
 else if constexpr(std::is_integral_v<T>)v=127;
 else if constexpr(std::is_floating_point_v<T>)v=-1.25;
 else if constexpr(requires{v.emplace();})v.emplace();
 else if constexpr(requires{v.push_back(typename T::value_type{});})v.push_back(typename T::value_type{});
 else if constexpr(requires{v.insert(31);})v.insert(31);
 else if constexpr(requires{v.x;v.y;}){v.x=17;v.y=-23;}
 else static_assert(sizeof(T)==0,"unhandled behavior field");
}
void verifyBehavior(){RuntimeObjectBehavior zero;const auto before=encode(zero);
{auto v=zero;perturb(v.kind);require(encode(v)!=before,"behavior kind field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.state);require(encode(v)!=before,"behavior state field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.moveTarget);require(encode(v)!=before,"behavior moveTarget field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.targetObjectId);require(encode(v)!=before,"behavior targetObjectId field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.autoAcquired);require(encode(v)!=before,"behavior autoAcquired field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.forceFire);require(encode(v)!=before,"behavior forceFire field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.attackGround);require(encode(v)!=before,"behavior attackGround field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.attackBridge);require(encode(v)!=before,"behavior attackBridge field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.attackBridgeCell);require(encode(v)!=before,"behavior attackBridgeCell field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.path);require(encode(v)!=before,"behavior path field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pathRequestPending);require(encode(v)!=before,"behavior pathRequestPending field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingPathTowardRange);require(encode(v)!=before,"behavior pendingPathTowardRange field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingPathStart);require(encode(v)!=before,"behavior pendingPathStart field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingPathGoal);require(encode(v)!=before,"behavior pendingPathGoal field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingHasSharedGoal);require(encode(v)!=before,"behavior pendingHasSharedGoal field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingSharedGoal);require(encode(v)!=before,"behavior pendingSharedGoal field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingAllowedOccupiedObjectId);require(encode(v)!=before,"behavior pendingAllowedOccupiedObjectId field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingTargetObjectId);require(encode(v)!=before,"behavior pendingTargetObjectId field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingRangeCells);require(encode(v)!=before,"behavior pendingRangeCells field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingAllowNearbyTarget);require(encode(v)!=before,"behavior pendingAllowNearbyTarget field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pendingIgnoredObjectIds);require(encode(v)!=before,"behavior pendingIgnoredObjectIds field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.repackMoveGroup);require(encode(v)!=before,"behavior repackMoveGroup field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.moveCooldownTicks);require(encode(v)!=before,"behavior moveCooldownTicks field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.moveSegmentFrom);require(encode(v)!=before,"behavior moveSegmentFrom field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.moveIntervalRemainderQ16);require(encode(v)!=before,"behavior moveIntervalRemainderQ16 field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.moveStartupDistanceQ16);require(encode(v)!=before,"behavior moveStartupDistanceQ16 field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderStartupAccelerationDistance);require(encode(v)!=before,"behavior renderStartupAccelerationDistance field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.factoryExitClearanceHostId);require(encode(v)!=before,"behavior factoryExitClearanceHostId field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.transportTransferHostId);require(encode(v)!=before,"behavior transportTransferHostId field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.transportTransferKind);require(encode(v)!=before,"behavior transportTransferKind field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.weaponCooldownTicks);require(encode(v)!=before,"behavior weaponCooldownTicks field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.blockedTicks);require(encode(v)!=before,"behavior blockedTicks field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.pathReplans);require(encode(v)!=before,"behavior pathReplans field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.nextRepathTick);require(encode(v)!=before,"behavior nextRepathTick field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.returnToDeployed);require(encode(v)!=before,"behavior returnToDeployed field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.approachSlotIndex);require(encode(v)!=before,"behavior approachSlotIndex field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderFrom);require(encode(v)!=before,"behavior renderFrom field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderTo);require(encode(v)!=before,"behavior renderTo field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderFromOffsetX);require(encode(v)!=before,"behavior renderFromOffsetX field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderFromOffsetY);require(encode(v)!=before,"behavior renderFromOffsetY field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderToOffsetX);require(encode(v)!=before,"behavior renderToOffsetX field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderToOffsetY);require(encode(v)!=before,"behavior renderToOffsetY field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderFromLevel);require(encode(v)!=before,"behavior renderFromLevel field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderToLevel);require(encode(v)!=before,"behavior renderToLevel field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderFromOnBridge);require(encode(v)!=before,"behavior renderFromOnBridge field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderToOnBridge);require(encode(v)!=before,"behavior renderToOnBridge field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderMoveTicksTotal);require(encode(v)!=before,"behavior renderMoveTicksTotal field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderMoveTicksRemaining);require(encode(v)!=before,"behavior renderMoveTicksRemaining field missing from full encoder");++behaviorProbes;}
{auto v=zero;perturb(v.renderArrivalDistanceAfter);require(encode(v)!=before,"behavior renderArrivalDistanceAfter field missing from full encoder");++behaviorProbes;}
}
namespace original {
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& object) {
    return {object.x, object.y};
}
bool personal(const WorldObjectInstance& passenger,const Q& queries) {
    const auto* type=queries.unitType(passenger);
    return passenger.kind==WorldObjectKind::Infantry&&type&&type->teleporter&&!type->harvester;
}
std::optional<bool> admission(const WorldObjectInstance& passenger,const RuntimeCellPoint& destination,const Q& queries) {
    if(!personal(passenger,queries))return std::nullopt;
    const auto pending=queries.pendingHostId(passenger.instanceId);
    if(!pending.has_value())return std::nullopt;
    const auto* host=queries.findIncludingInactive(pending.value());
    if(!host||host->kind!=WorldObjectKind::Unit)return std::nullopt;
    const auto* move=queries.behavior(passenger.instanceId);
    if(move==nullptr||move->kind!=RuntimeBehaviorKind::Move
       ||move->pendingAllowedOccupiedObjectId!=host->instanceId
       ||move->moveTarget!=destination||runtimeCellPointForObject(*host)!=destination
       ||host->airborne||host->renderMoveActive||!queries.canEnter(passenger,*host))return false;
    const auto* warp=queries.moveState(passenger.instanceId);
    if(warp&&queries.inTransit(passenger.instanceId)
       &&(warp->destination.x!=destination.x||warp->destination.y!=destination.y||warp->destinationLevel!=host->level))return false;
    return true;
}
bool ready(const WorldObjectInstance& passenger,const WorldObjectInstance& host,const int& tick,const Q& queries) {
    const auto* warp=queries.moveState(passenger.instanceId);
    const auto admitted=admission(passenger,runtimeCellPointForObject(host),queries);
    return admitted&&*admitted&&warp
        &&(warp->phase==ChronoPhase::WarpIn||warp->phase==ChronoPhase::Cooldown)
        &&warp->completeTick<=tick
        &&warp->destination.x==host.x&&warp->destination.y==host.y
        &&warp->destinationLevel==host.level
        &&runtimeCellPointForObject(passenger)==runtimeCellPointForObject(host)
        &&passenger.level==host.level&&!passenger.renderMoveActive;
}
}

struct Queries final:Q {
 mutable WorldObjectInstance passenger,host;
 mutable RuntimeObjectBehavior action;
 mutable UnitType type;
 mutable ChronoMoveState warp;
 mutable RuntimeCellPoint destination{5,6};mutable int tick=100;
 bool typePresent=true,pendingPresent=true,hostPresent=true,behaviorPresent=true,warpPresent=true,enter=true,transit=false;
 int pending=52,mode=0,faultAt=0;bool afterFault=false;
 mutable int calls=0;mutable std::vector<std::string> trace;
 void op(const std::string& name)const{
  ++calls;trace.push_back(name);
  if(!afterFault&&calls==faultAt)throw std::runtime_error("fault:"+name);
  if(mode==1&&name.starts_with("type:")){type.harvester=!type.harvester;passenger.kind=WorldObjectKind::Infantry;}
  if(mode==2&&name.starts_with("host:"))action.kind=RuntimeBehaviorKind::Attack;
  if(mode==3&&name.starts_with("behavior:")){host.x=destination.x+1;passenger.renderMoveActive=true;}
  if(mode==4&&name.starts_with("enter:")){host.level+=1;warp.destinationLevel=host.level;}
  if(mode==5&&name.starts_with("transit:"))warp.destination.x+=1;
  if(mode==6&&name.starts_with("enter:")){tick=warp.completeTick-1;passenger.level=host.level;}
  if(mode==7&&name.starts_with("type:"))warp.phase=ChronoPhase::Cooldown;
  if(afterFault&&calls==faultAt)throw std::runtime_error("fault:"+name);
 }
 const UnitType* unitType(const WorldObjectInstance& p)const override{require(&p==&passenger,"type borrows actual passenger");op("type:"+std::to_string(p.instanceId));return typePresent?&type:nullptr;}
 std::optional<int> pendingHostId(int id)const override{op("pending:"+std::to_string(id));return pendingPresent?std::optional<int>{pending}:std::nullopt;}
 const WorldObjectInstance* findIncludingInactive(int id)const override{op("host:"+std::to_string(id));return hostPresent?&host:nullptr;}
 const RuntimeObjectBehavior* behavior(int id)const override{op("behavior:"+std::to_string(id));return behaviorPresent?&action:nullptr;}
 bool canEnter(const WorldObjectInstance& p,const WorldObjectInstance& h)const override{require(&p==&passenger&&&h==&host,"canEnter keeps both original borrows");op("enter:"+std::to_string(p.instanceId)+":"+std::to_string(h.instanceId));return enter;}
 const ChronoMoveState* moveState(int id)const override{op("warp:"+std::to_string(id));return warpPresent?&warp:nullptr;}
 bool inTransit(int id)const override{op("transit:"+std::to_string(id));return transit;}
 std::string image()const{return encode(passenger)+encode(host)+encode(action)+encode(warp)+encode(destination)+encode(tick)+encode(type.teleporter)+encode(type.harvester)+encode(typePresent)+encode(pendingPresent)+encode(hostPresent)+encode(behaviorPresent)+encode(warpPresent)+encode(enter)+encode(transit)+encode(pending);}
};
Queries base(){Queries q;q.passenger.instanceId=41;q.passenger.kind=WorldObjectKind::Infantry;q.passenger.x=q.host.x=5;q.passenger.y=q.host.y=6;q.passenger.level=q.host.level=2;q.passenger.health=q.host.health=100;q.passenger.alive=q.host.alive=true;
 q.host.instanceId=52;q.host.kind=WorldObjectKind::Unit;q.type.teleporter=true;q.type.harvester=false;
 q.action.kind=RuntimeBehaviorKind::Move;q.action.pendingAllowedOccupiedObjectId=52;q.action.moveTarget={5,6};q.warp.objectId=41;q.warp.phase=ChronoPhase::WarpIn;q.warp.destination={5,6};q.warp.destinationLevel=2;q.warp.completeTick=100;return q;
}
Queries fixture(unsigned mask,int phase){auto q=base();
 if(mask&(1u<<0))q.typePresent=false;
 if(mask&(1u<<1))q.passenger.kind=WorldObjectKind::Unit;
 if(mask&(1u<<2))q.type.teleporter=false;
 if(mask&(1u<<3))q.type.harvester=true;
 if(mask&(1u<<4))q.pendingPresent=false;
 if(mask&(1u<<5))q.hostPresent=false;
 if(mask&(1u<<6))q.host.kind=WorldObjectKind::Structure;
 if(mask&(1u<<7))q.behaviorPresent=false;
 if(mask&(1u<<8))q.action.kind=RuntimeBehaviorKind::Attack;
 if(mask&(1u<<9))q.action.pendingAllowedOccupiedObjectId=53;
 if(mask&(1u<<10))q.action.moveTarget.x=7;
 if(mask&(1u<<11))q.host.x=7;
 if(mask&(1u<<12))q.host.airborne=true;
 if(mask&(1u<<13))q.host.renderMoveActive=true;
 if(mask&(1u<<14))q.enter=false;
 if(mask&(1u<<15))q.warpPresent=false;
 if(mask&(1u<<16))q.transit=true;
 if(mask&(1u<<17))q.warp.destination.y=9;
 if(mask&(1u<<18))q.warp.destinationLevel=3;
 if(mask&(1u<<19))q.warp.completeTick=101;
 if(mask&(1u<<20))q.passenger.x=4;
 if(mask&(1u<<21))q.passenger.level=1;
 if(mask&(1u<<22))q.passenger.renderMoveActive=true;
 q.warp.phase=static_cast<ChronoPhase>(phase);return q;
}
struct Result{std::optional<bool> value;bool threw=false;std::string error;};
Result execute(bool old,int entry,Queries& q){Result r;try{
 if(entry==0)r.value=old?original::personal(q.passenger,q):current::runtimePersonalChronoTransportPassenger(q.passenger,q);
 else if(entry==1)r.value=old?original::admission(q.passenger,q.destination,q):current::runtimeChronoTransportDestinationAdmission(q.passenger,q.destination,q);
 else r.value=old?original::ready(q.passenger,q.host,q.tick,q):current::runtimeChronoTransportArrivalReady(q.passenger,q.host,q.tick,q);
}catch(const std::exception& e){r.threw=true;r.error=e.what();}return r;}
void compare(Queries& a,Queries& b,int entry){const auto before=a.image();const auto x=execute(true,entry,a),y=execute(false,entry,b);
 require(x.value==y.value&&x.threw==y.threw&&x.error==y.error,"old/current tri-state and exact exception");require(a.calls==b.calls&&a.trace==b.trace,"complete originalorderedqueries");require(a.image()==b.image(),"full passenger87/host87/behavior49/warp9 andlive values exact");
 if(a.mode==0)require(a.image()==before,"read-only domain policy changedauthority");
 if(x.threw)++faults;else if(!x.value)++absent;else if(*x.value)++positive;else ++negative;++states;
}
bool runContract(){try{
 verifyBehavior();{ChronoMoveState zero;const auto before=encode(zero);
{auto v=zero;perturb(v.objectId);require(encode(v)!=before,"chrono objectId omitted");++chronoProbes;}
{auto v=zero;perturb(v.phase);require(encode(v)!=before,"chrono phase omitted");++chronoProbes;}
{auto v=zero;perturb(v.source.x);require(encode(v)!=before,"chrono source.x omitted");++chronoProbes;}
{auto v=zero;perturb(v.source.y);require(encode(v)!=before,"chrono source.y omitted");++chronoProbes;}
{auto v=zero;perturb(v.destination.x);require(encode(v)!=before,"chrono destination.x omitted");++chronoProbes;}
{auto v=zero;perturb(v.destination.y);require(encode(v)!=before,"chrono destination.y omitted");++chronoProbes;}
{auto v=zero;perturb(v.sourceLevel);require(encode(v)!=before,"chrono sourceLevel omitted");++chronoProbes;}
{auto v=zero;perturb(v.destinationLevel);require(encode(v)!=before,"chrono destinationLevel omitted");++chronoProbes;}
{auto v=zero;perturb(v.startTick);require(encode(v)!=before,"chrono startTick omitted");++chronoProbes;}
{auto v=zero;perturb(v.completeTick);require(encode(v)!=before,"chrono completeTick omitted");++chronoProbes;}
{auto v=zero;perturb(v.cooldownUntilTick);require(encode(v)!=before,"chrono cooldownUntilTick omitted");++chronoProbes;}
 }
 std::vector<unsigned> masks{0};for(unsigned bit=0;bit<23;++bit)masks.push_back(1u<<bit);
 for(unsigned a=0;a<23;++a)for(unsigned b=a+1;b<23;++b)masks.push_back((1u<<a)|(1u<<b));
 unsigned sequence=0x13579bdu;for(int i=0;i<1536;++i){sequence=sequence*1664525u+1013904223u;masks.push_back(sequence&((1u<<23)-1));}
 for(unsigned mask:masks)for(int phase:{-1,0,1,2,3,4,5,17})for(int entry=0;entry<3;++entry){auto a=fixture(mask,phase),b=a;compare(a,b,entry);const int count=a.calls;
  for(int fault=1;fault<=count;++fault){for(bool after:{false,true}){auto x=fixture(mask,phase),y=x;x.faultAt=y.faultAt=fault;x.afterFault=y.afterFault=after;compare(x,y,entry);}}
 }
 for(int mode=1;mode<=7;++mode)for(unsigned mask:{0u,1u<<15,1u<<16,(1u<<16)|(1u<<17)})for(int phase:{0,1,2,3,4,17})for(int entry=0;entry<3;++entry){auto a=fixture(mask,phase),b=a;a.mode=b.mode=mode;compare(a,b,entry);++aliases;}
 for(int tick:{INT_MIN,-1,0,99,100,101,INT_MAX})for(int complete:{INT_MIN,-1,0,100,101,INT_MAX}){auto a=base(),b=a;a.tick=b.tick=tick;a.warp.completeTick=b.warp.completeTick=complete;compare(a,b,2);}
 require(behaviorProbes==49&&chronoProbes==11&&positive>0&&negative>0&&absent>0&&faults>0&&aliases>0,"allbranches and fullstate probes required");
 std::cout<<"chrono-transport-admission checks="<<checks<<" states="<<states<<" behavior-probes="<<behaviorProbes<<" chrono-leaf-probes="<<chronoProbes<<" faults="<<faults<<" positive="<<positive<<" negative="<<negative<<" absent="<<absent<<" aliases="<<aliases<<" full87+87World-49Behavior-9Chrono-and-ordered-queries=exact failures=0\n";return true;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}}
}
