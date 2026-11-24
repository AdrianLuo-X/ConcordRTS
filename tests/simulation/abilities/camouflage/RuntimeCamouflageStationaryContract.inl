#include "simulation/abilities/unit_special/RuntimeCamouflageStationary.h"
#include "simulation/movement/RuntimeDeploymentMissions.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "world/model/GameWorld.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <bit>
#include <climits>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace camouflage_stationary_model_detail {
using concord::simulation::abilities::RuntimeCamouflageStationaryQueries;
namespace current=concord::simulation::abilities;
int checks=0,states=0,behaviorProbes=0,classifications=0,faults=0,positive=0,negative=0,aliases=0;
void require(bool value,const std::string& error){++checks;if(!value)throw std::runtime_error(error);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
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
using runtime_save_text::toLowerAsciiLocal;
bool runtimeMissionIsMcvDeployPending(const WorldObjectInstance& object) {
    const auto mission = toLowerAsciiLocal(object.mission);
    return mission == "mcvdeployturning" || mission == "mcvdeployturningunselected"
        || mission == "mcvdeployready" || mission == "mcvdeployreadyunselected";
}
bool runtimeMissionIsDeploying(const WorldObjectInstance& object) {
    return toLowerAsciiLocal(object.mission) == "deploying" || runtimeMissionIsMcvDeployPending(object);
}
bool runtimeMissionIsUndeploying(const WorldObjectInstance& object) {
    return toLowerAsciiLocal(object.mission) == "undeploying";
}
bool execute(int objectId, const RuntimeCamouflageStationaryQueries& queries) {
        const WorldObjectInstance* object = queries.findObject(objectId);
        if (object == nullptr || !object->alive
            || object->dockedToObjectId >= 0
            || object->hasRenderPosition
            || object->deployed
            || runtimeMissionIsDeploying(*object)
            || runtimeMissionIsUndeploying(*object)) {
            return false;
        }
        const RuntimeObjectBehavior* behavior = queries.findBehavior(objectId);
        if (behavior != nullptr) {
            const auto& action = *behavior;
            // Shooting from rest must retain logical camouflage. The weapon's
            // fake-blink timer reveals its body; pathing/chasing still unmasks it.
            return action.kind == RuntimeBehaviorKind::Attack
                && action.path.empty() && !action.pathRequestPending
                && action.renderMoveTicksRemaining <= 0
                && action.state != RuntimeBehaviorState::Moving
                && action.state != RuntimeBehaviorState::Chasing
                && action.state != RuntimeBehaviorState::Blocked;
        }
        const std::string mission = toLowerAsciiLocal(object->mission);
        return mission == "guard"
            || mission == "idle"
            || mission == "idle1"
            || mission == "idle2"
            || mission == "deployedidle";
    }
}
struct Queries final:RuntimeCamouflageStationaryQueries {
 mutable WorldObjectInstance object;mutable RuntimeObjectBehavior behavior;
 bool objectPresent=true,behaviorPresent=false;int mode=0,faultAt=0;
 mutable int calls=0;mutable std::vector<std::string> trace;
 void touch(const std::string& label) const{trace.push_back(label);++calls;if(calls==faultAt)throw std::runtime_error("injected-lookup-"+std::to_string(calls));}
 const WorldObjectInstance* findObject(int id)const override {touch("object:"+std::to_string(id));if(mode==3)object.deployed=true;return objectPresent?&object:nullptr;}
 const RuntimeObjectBehavior* findBehavior(int id)const override {touch("behavior:"+std::to_string(id));if(mode==2)object.mission="Guard";if(mode==4){behavior.state=RuntimeBehaviorState::Attacking;behavior.path.clear();behavior.pathRequestPending=false;behavior.renderMoveTicksRemaining=0;}return behaviorPresent?&behavior:nullptr;}
};
struct Result{bool value=false,threw=false;std::string error;};
Result execute(bool old,Queries& q){Result r;try{r.value=old?original::execute(42,q):current::runtimeCamouflageIsStationary(42,q);}catch(const std::exception& e){r.threw=true;r.error=e.what();}return r;}
void compare(Queries& a,Queries& b){const auto beforeA=encode(a.object)+encode(a.behavior);const auto x=execute(true,a),y=execute(false,b);
 require(x.value==y.value&&x.threw==y.threw&&x.error==y.error,"original/new return and exact exception");
 require(a.trace==b.trace&&a.calls==b.calls,"complete first-object then conditional-behavior query order");
 require(encode(a.object)==encode(b.object)&&encode(a.behavior)==encode(b.behavior),"complete87World and49Behavior fields exact");
 if(a.mode==0)require(beforeA==encode(a.object)+encode(a.behavior),"read-only policy has no authority writes");
 if(x.threw)++faults;else if(x.value)++positive;else ++negative;++states;
}
Queries fixture(const std::string& mission,int mask,int action,int alias){Queries q;q.object.instanceId=42;q.object.health=100;q.object.alive=(mask&1)==0;q.object.dockedToObjectId=(mask&2)?9:-1;
 q.object.hasRenderPosition=(mask&4)!=0;q.object.deployed=(mask&8)!=0;q.object.mission=mission;q.mode=alias;
 q.behaviorPresent=action!=0;q.behavior.kind=RuntimeBehaviorKind::Attack;q.behavior.state=RuntimeBehaviorState::Idle;
 if(action==2)q.behavior.state=RuntimeBehaviorState::Attacking;
 if(action==3)q.behavior.state=RuntimeBehaviorState::Cooldown;
 if(action==4)q.behavior.path.push_back({3,4});
 if(action==5)q.behavior.pathRequestPending=true;
 if(action==6)q.behavior.state=RuntimeBehaviorState::Moving;
 if(action==7)q.behavior.state=RuntimeBehaviorState::Chasing;
 if(action==8)q.behavior.state=RuntimeBehaviorState::Blocked;
 if(action==9)q.behavior.renderMoveTicksRemaining=1;
 if(action==10)q.behavior.kind=RuntimeBehaviorKind::Move;
 if(action==11)q.behavior.kind=RuntimeBehaviorKind::None;
 if(action==12)q.behavior.renderMoveTicksRemaining=INT_MIN;
 if(action==13){q.behavior.state=static_cast<RuntimeBehaviorState>(17);q.behavior.renderMoveTicksRemaining=-1;}
 return q;
}
bool runContract(){try{verifyBehavior();
 std::vector<std::string> missions={"Guard","guard","GUARD","Idle","idle1","idle2","DeployedIdle","Deploying","deploying","Undeploying","undeploying","McvDeployTurning","McvDeployTurningUnselected","McvDeployReady","McvDeployReadyUnselected","MCVDEPLOYREADY","mcvdeployturningunselected","Attack","Move",""," guard "," deploying ","undeployed","Idle3","Harvest","Stop",std::string("Guard\0suffix",12),"Mirage_伪装"};
 for(const auto& mission:missions){WorldObjectInstance object;object.mission=mission;
  require(original::runtimeMissionIsMcvDeployPending(object)==concord::simulation::movement::runtimeMissionIsMcvDeployPending(mission),"full exact MCV mission predicate");
  require(original::runtimeMissionIsDeploying(object)==concord::simulation::movement::runtimeMissionIsDeploying(mission),"deploying string projection");
  require(original::runtimeMissionIsUndeploying(object)==concord::simulation::movement::runtimeMissionIsUndeploying(mission),"undeploying string projection");++classifications;
 }
 for(const auto& mission:missions)for(int mask=0;mask<16;++mask)for(int action=0;action<14;++action){
  auto a=fixture(mission,mask,action,0),b=fixture(mission,mask,action,0);compare(a,b);const int calls=a.calls;
  for(int fault=1;fault<=calls;++fault){auto x=fixture(mission,mask,action,0),y=fixture(mission,mask,action,0);x.faultAt=y.faultAt=fault;compare(x,y);}
 }
 for(int mode:{2,3,4})for(const auto& mission:missions)for(int action=0;action<14;++action){auto a=fixture(mission,0,action,mode),b=fixture(mission,0,action,mode);compare(a,b);++aliases;}
 {auto a=fixture("Guard",0,0,0),b=a;a.objectPresent=b.objectPresent=false;compare(a,b);require(a.calls==1,"missing object never queries behavior");}
 require(positive>0&&negative>0&&faults>0&&behaviorProbes==49,"all positive/negative/fault and fullBehavior probes mandatory");
 std::cout<<"camouflage-stationary checks="<<checks<<" states="<<states<<" behavior-probes="<<behaviorProbes<<" missions="<<classifications<<" faults="<<faults<<" positive="<<positive<<" negative="<<negative<<" aliases="<<aliases<<" full87World-49Behavior-and-ordered-queries=exact failures=0\n";return true;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}}
}
