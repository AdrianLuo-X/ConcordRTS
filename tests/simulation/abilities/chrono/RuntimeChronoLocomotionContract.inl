#include "simulation/abilities/chrono/RuntimeChronoLocomotionPolicy.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace chrono_locomotion_model_detail {
namespace current=concord::simulation::abilities;
int checks=0,states=0,worldProbes=0,behaviorProbes=0,faults=0,aliases=0,moveTrue=0,moveFalse=0,moveNone=0,held=0,notHeld=0,pathStorageChecks=0,scopeFinishes=0;
std::set<std::string> faultPorts;
int liveMutations=0;std::set<int> mutatedAliasModes;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(why);}
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
void verifyWorld(){WorldObjectInstance zero;const auto before=encode(zero);
{auto v=zero;mutateEncoderField(v.instanceId);require(encode(v)!=before,"world instanceId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.kind);require(encode(v)!=before,"world kind omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.mapId);require(encode(v)!=before,"world mapId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.owner);require(encode(v)!=before,"world owner omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.type);require(encode(v)!=before,"world type omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.health);require(encode(v)!=before,"world health omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.x);require(encode(v)!=before,"world x omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.y);require(encode(v)!=before,"world y omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cell);require(encode(v)!=before,"world cell omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.level);require(encode(v)!=before,"world level omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.facing);require(encode(v)!=before,"world facing omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretFacing);require(encode(v)!=before,"world turretFacing omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bodyFacingClock);require(encode(v)!=before,"world bodyFacingClock omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretFacingClock);require(encode(v)!=before,"world turretFacingClock omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.subCell);require(encode(v)!=before,"world subCell omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.mission);require(encode(v)!=before,"world mission omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tag);require(encode(v)!=before,"world tag omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.veterancy);require(encode(v)!=before,"world veterancy omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.veterancyExperience);require(encode(v)!=before,"world veterancyExperience omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateArmorMultiplier);require(encode(v)!=before,"world crateArmorMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateFirepowerMultiplier);require(encode(v)!=before,"world crateFirepowerMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateSpeedMultiplier);require(encode(v)!=before,"world crateSpeedMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateCloakable);require(encode(v)!=before,"world crateCloakable omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.harvestAutoSearchSuspended);require(encode(v)!=before,"world harvestAutoSearchSuspended omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.hasBeenCaptured);require(encode(v)!=before,"world hasBeenCaptured omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.onBridge);require(encode(v)!=before,"world onBridge omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.authoredAircraft);require(encode(v)!=before,"world authoredAircraft omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.alive);require(encode(v)!=before,"world alive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.rubble);require(encode(v)!=before,"world rubble omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.submerged);require(encode(v)!=before,"world submerged omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cloaked);require(encode(v)!=before,"world cloaked omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cloakCooldownTicks);require(encode(v)!=before,"world cloakCooldownTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.submergeCooldownTicks);require(encode(v)!=before,"world submergeCooldownTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.deployed);require(encode(v)!=before,"world deployed omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.deployStateTicksRemaining);require(encode(v)!=before,"world deployStateTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.prone);require(encode(v)!=before,"world prone omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.proneTicksRemaining);require(encode(v)!=before,"world proneTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.weaponCooldownTicksRemaining);require(encode(v)!=before,"world weaponCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.spriteFiringStartTick);require(encode(v)!=before,"world spriteFiringStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.radiationEruptionCooldownTicksRemaining);require(encode(v)!=before,"world radiationEruptionCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.hasRenderPosition);require(encode(v)!=before,"world hasRenderPosition omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderX);require(encode(v)!=before,"world renderX omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderY);require(encode(v)!=before,"world renderY omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderMoveActive);require(encode(v)!=before,"world renderMoveActive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderLevel);require(encode(v)!=before,"world renderLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderOnBridge);require(encode(v)!=before,"world renderOnBridge omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.movementAnimationDistance);require(encode(v)!=before,"world movementAnimationDistance omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.buildAnimTicksTotal);require(encode(v)!=before,"world buildAnimTicksTotal omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.buildAnimTicksRemaining);require(encode(v)!=before,"world buildAnimTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.structureRecoilStartTick);require(encode(v)!=before,"world structureRecoilStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.chargeTurretStartTick);require(encode(v)!=before,"world chargeTurretStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.workAnimationActive);require(encode(v)!=before,"world workAnimationActive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.poweredOperational);require(encode(v)!=before,"world poweredOperational omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.powerAnimationPausedAtTick);require(encode(v)!=before,"world powerAnimationPausedAtTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.powerAnimationPausedTicks);require(encode(v)!=before,"world powerAnimationPausedTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAnimationStartTick);require(encode(v)!=before,"world factoryExitAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAnimationEndTick);require(encode(v)!=before,"world factoryExitAnimationEndTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAirborne);require(encode(v)!=before,"world factoryExitAirborne omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitHostId);require(encode(v)!=before,"world factoryExitHostId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.specialCashNextTick);require(encode(v)!=before,"world specialCashNextTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.specialCloneDeliveryObjectId);require(encode(v)!=before,"world specialCloneDeliveryObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.transportPassengerIds);require(encode(v)!=before,"world transportPassengerIds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bioReactorAnimationStartTick);require(encode(v)!=before,"world bioReactorAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bioReactorAnimationPoweredStartTick);require(encode(v)!=before,"world bioReactorAnimationPoweredStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerMode);require(encode(v)!=before,"world activeGunnerMode omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerWeaponIndex);require(encode(v)!=before,"world activeGunnerWeaponIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerTurretIndex);require(encode(v)!=before,"world activeGunnerTurretIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerHostId);require(encode(v)!=before,"world tankBunkerHostId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerOccupantId);require(encode(v)!=before,"world tankBunkerOccupantId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerAnimationStartTick);require(encode(v)!=before,"world tankBunkerAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerLowering);require(encode(v)!=before,"world tankBunkerLowering omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.dockedToObjectId);require(encode(v)!=before,"world dockedToObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.dockIndex);require(encode(v)!=before,"world dockIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.airborne);require(encode(v)!=before,"world airborne omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.flightLevel);require(encode(v)!=before,"world flightLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.aircraftDockFlightProgress);require(encode(v)!=before,"world aircraftDockFlightProgress omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.aircraftBuildingClearanceLevel);require(encode(v)!=before,"world aircraftBuildingClearanceLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.wallConnectionMask);require(encode(v)!=before,"world wallConnectionMask omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonHostObjectId);require(encode(v)!=before,"world garrisonHostObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonOccupantIds);require(encode(v)!=before,"world garrisonOccupantIds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonFireCursor);require(encode(v)!=before,"world garrisonFireCursor omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.currentAmmo);require(encode(v)!=before,"world currentAmmo omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.pendingAircraftAuxSounds);require(encode(v)!=before,"world pendingAircraftAuxSounds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretSoundActivityTick);require(encode(v)!=before,"world turretSoundActivityTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeTurretSoundEvent);require(encode(v)!=before,"world activeTurretSoundEvent omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.pendingTurretSounds);require(encode(v)!=before,"world pendingTurretSounds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.modExtensions);require(encode(v)!=before,"world modExtensions omitted");++worldProbes;}
}
struct Fixture final:current::RuntimeChronoRecoveryQueries,current::RuntimeChronoMoveQueries,current::RuntimeChronoLocomotionEffects {
 mutable WorldObjectInstance object;
 mutable RuntimeObjectBehavior behavior;
 mutable UnitType unit;
 mutable bool haveType=true,initialized=true,locked=true,transit=false,requestAccepted=true;
 mutable std::optional<bool> admission;
 mutable int tick=100,calls=0,faultAt=0,mode=0;
 bool afterFault=false,inputFromBehavior=false;
 mutable std::vector<std::string> trace;
 RuntimeCellPoint destination{5,6};
 mutable RuntimeCellPoint activeDestination{5,6};
 mutable const std::deque<RuntimeCellPoint>* requestPathObject=nullptr;
 mutable std::size_t requestPathSize=0;
 mutable bool requestReturned=false,storageChecked=false,samePathContainer=false,requestPathCaptured=false;
 mutable bool requestAttempted=false,scopeAlive=false;mutable int finishCalls=0;int activeEntry=0;
 std::string image()const{return encode(object)+encode(behavior)+encode(unit.teleporter)+encode(unit.harvester)+encode(haveType)+encode(initialized)+encode(locked)+encode(transit)+encode(requestAccepted)+encode(admission)+encode(tick)+encode(destination)+encode(activeDestination)+encode(requestReturned)+encode(requestPathSize)+encode(storageChecked)+encode(samePathContainer)+encode(requestPathCaptured)+encode(requestAttempted)+encode(scopeAlive)+encode(finishCalls);}
 void before(const std::string& name,const std::string& args)const{
  ++calls;trace.push_back("before:"+name+args+image());
  if(faultAt==calls&&!afterFault){faultPorts.insert(name);throw std::runtime_error("fault-before:"+name+":"+std::to_string(calls));}
 }
 void mutate(const std::string& name)const{
  if(mode==1&&name=="unit"){++liveMutations;mutatedAliasModes.insert(mode);object.x=activeDestination.x;object.y=activeDestination.y;behavior.moveTarget={19,23};unit.harvester=!unit.harvester;}
  if(mode==2&&name=="admission"){++liveMutations;mutatedAliasModes.insert(mode);object.x=activeDestination.x;object.y=activeDestination.y;behavior.state=static_cast<RuntimeBehaviorState>(19);admission=false;}
  if(mode==3&&name=="locked"){++liveMutations;mutatedAliasModes.insert(mode);initialized=!initialized;tick=INT_MAX;object.instanceId=53;transit=!transit;}
  if(mode==4&&name=="request"){++liveMutations;mutatedAliasModes.insert(mode);object.instanceId=73;behavior.path.push_back({7,8});behavior.moveCooldownTicks=55;behavior.moveTarget={44,45};object.hasRenderPosition=true;}
  if(mode==5&&name=="release"){++liveMutations;mutatedAliasModes.insert(mode);object.x=-11;object.y=8;object.onBridge=!object.onBridge;behavior.state=static_cast<RuntimeBehaviorState>(19);}
  if(mode==6&&name=="pose"){++liveMutations;mutatedAliasModes.insert(mode);object.instanceId=89;tick=-1;behavior.pathRequestPending=true;object.mission="via-pose";}
  if(mode==7&&name=="transit"){++liveMutations;mutatedAliasModes.insert(mode);behavior.state=static_cast<RuntimeBehaviorState>(19);object.mission="via-transit";transit=!transit;}
 }
 void after(const std::string& name)const{
  mutate(name);trace.push_back("after:"+name+image());
  if(faultAt==calls&&afterFault){faultPorts.insert(name);throw std::runtime_error("fault-after:"+name+":"+std::to_string(calls));}
 }
 const UnitType* unitType(const WorldObjectInstance& x)const override{
  require(&x==&object,"unit query must borrow original World");before("unit",encode(x.instanceId));const auto* out=haveType?&unit:nullptr;after("unit");return out;
 }
 std::optional<bool> transportAdmission(const WorldObjectInstance& x,const RuntimeCellPoint& d)const override{
  require(&x==&object,"admission must borrow original World");before("admission",encode(x.instanceId)+encode(d));const auto out=admission;after("admission");return out;
 }
 bool movementLocked(int id,int currentTick)const override{before("locked",encode(id)+encode(currentTick));const bool out=locked;after("locked");return out;}
 bool inTransit(int id)const override{before("transit",encode(id));const bool out=transit;after("transit");return out;}
 bool requestMove(WorldObjectInstance& x,const RuntimeCellPoint& d,const std::optional<bool>& a)override{
  require(&x==&object,"request must borrow original World");requestAttempted=true;before("request",encode(x.instanceId)+encode(d)+encode(a));const bool out=requestAccepted;scopeAlive=true;after("request");
  requestReturned=true;requestPathObject=&behavior.path;requestPathSize=behavior.path.size();return out;
 }
 void releaseInfantrySubcellReservation(int id)override{
  require(scopeAlive,"request temporary maps/callbacks alive during release");
  storageChecked=true;samePathContainer=&behavior.path==requestPathObject;requestPathCaptured=requestPathObject!=nullptr;
  require(requestReturned&&behavior.path.empty()&&requestPathCaptured&&samePathContainer,"original deque is borrowed andcleared,not a copied path");++pathStorageChecks;
  before("release",encode(id));after("release");
 }
 void clearRenderPosition(WorldObjectInstance& x)override{
  require(&x==&object,"pose effect must borrow original World");if(activeEntry==1)require(scopeAlive,"request temporary maps/callbacks alive during final pose");before("pose",encode(x.instanceId));
  x.hasRenderPosition=false;x.renderX=static_cast<double>(x.x);x.renderY=static_cast<double>(x.y);x.renderMoveActive=false;x.renderLevel=static_cast<double>(x.level);x.renderOnBridge=x.onBridge;
  after("pose");
 }
 void finishRequestScope() noexcept override{++finishCalls;++scopeFinishes;trace.push_back("scope-finish-before:"+image());scopeAlive=false;trace.push_back("scope-finish-after:"+image());}
};
Fixture fixture(unsigned mask){Fixture f;f.object.instanceId=41;f.object.kind=std::array{WorldObjectKind::Infantry,WorldObjectKind::Unit,WorldObjectKind::Unit,WorldObjectKind::Structure}[(mask>>16)&3u];f.object.authoredAircraft=((mask>>16)&3u)==2u;f.object.mapId="chrono-policy";f.object.type="CLEG";f.object.owner="Americans";f.object.alive=(mask&(1u<<10))==0;f.object.health=(mask&(1u<<11))?0:100;f.object.x=1;f.object.y=2;f.object.level=3;f.object.onBridge=(mask&(1u<<12))!=0;f.object.hasRenderPosition=(mask&(1u<<15))==0;f.object.renderX=13.25;f.object.renderY=-9.75;f.object.renderLevel=7.125;f.object.renderMoveActive=true;f.object.renderOnBridge=!f.object.onBridge;f.object.mission="model-mission";
 f.unit.teleporter=(mask&2u)==0;f.unit.harvester=(mask&4u)!=0;f.haveType=(mask&1u)==0;f.initialized=(mask&128u)==0;f.locked=(mask&256u)==0;f.transit=(mask&512u)!=0;f.requestAccepted=(mask&64u)==0;
 if(mask&8u)f.admission=false;else if(mask&16u)f.admission=true;if(mask&32u){f.object.x=f.destination.x;f.object.y=f.destination.y;}
 f.behavior.kind=RuntimeBehaviorKind::Move;f.behavior.state=static_cast<RuntimeBehaviorState>(19);f.behavior.moveTarget=f.destination;if(!(mask&(1u<<13)))f.behavior.path={{2,3},{4,5},{6,7}};f.behavior.pathRequestPending=true;f.behavior.moveCooldownTicks=17;f.behavior.renderMoveTicksRemaining=19;f.behavior.blockedTicks=23;f.inputFromBehavior=(mask&(1u<<14))!=0;return f;
}
namespace original {
bool hold(const bool& initialized,WorldObjectInstance& object,RuntimeObjectBehavior& behavior,const int& tick,const current::RuntimeChronoRecoveryQueries& queries,current::RuntimeChronoPoseEffects& effects){
 if(!initialized||!queries.movementLocked(object.instanceId,tick))return false;
 effects.clearRenderPosition(object);behavior.state=RuntimeBehaviorState::Cooldown;
 if(!queries.inTransit(object.instanceId))object.mission="ChronoFrozen";
 return true;
}
std::optional<bool> advance(WorldObjectInstance& object,RuntimeObjectBehavior& behavior,RuntimeCellPoint destination,const current::RuntimeChronoMoveQueries& queries,current::RuntimeChronoLocomotionEffects& effects){
 const UnitType* unit=queries.unitType(object);
 if(unit==nullptr||!unit->teleporter||unit->harvester)return std::nullopt;
 const auto admission=queries.transportAdmission(object,destination);
 if(admission&&!*admission){behavior.state=RuntimeBehaviorState::Blocked;return false;}
 if(RuntimeCellPoint{object.x,object.y}==destination&&!admission)return std::nullopt;
 try {
 if(!effects.requestMove(object,destination,admission)){behavior.state=RuntimeBehaviorState::Blocked;effects.finishRequestScope();return false;}
 behavior.path.clear();behavior.pathRequestPending=false;behavior.moveCooldownTicks=0;behavior.renderMoveTicksRemaining=0;behavior.blockedTicks=0;behavior.state=RuntimeBehaviorState::Cooldown;
 effects.releaseInfantrySubcellReservation(object.instanceId);effects.clearRenderPosition(object);effects.finishRequestScope();return true;
 }catch(...){effects.finishRequestScope();throw;}
}
}
struct Result{std::optional<bool> value;bool threw=false;std::string error;};
Result execute(bool old,int entry,Fixture& f){Result out;f.activeEntry=entry;f.activeDestination=f.inputFromBehavior?f.behavior.moveTarget:f.destination;try{
 if(entry==0)out.value=old?original::hold(f.initialized,f.object,f.behavior,f.tick,f,f):current::holdRuntimeChronoRecovery(f.initialized,f.object,f.behavior,f.tick,f,f);
 else if(f.inputFromBehavior)out.value=old?original::advance(f.object,f.behavior,f.behavior.moveTarget,f,f):current::advanceRuntimeTeleporterMove(f.object,f.behavior,f.behavior.moveTarget,f,f);
 else out.value=old?original::advance(f.object,f.behavior,f.destination,f,f):current::advanceRuntimeTeleporterMove(f.object,f.behavior,f.destination,f,f);
 }catch(const std::exception& e){out.threw=true;out.error=e.what();}return out;
}
int compare(Fixture& a,Fixture& b,int entry){const auto x=execute(true,entry,a),y=execute(false,entry,b);
 require(x.value==y.value&&x.threw==y.threw&&x.error==y.error,"exact original result and exception");require(a.calls==b.calls&&a.trace==b.trace,"original ordered read/effect trace including live aliases");require(a.image()==b.image(),"all World87/Behavior49 andoriginaldeque/request scope exact");
 require(!a.scopeAlive&&!b.scopeAlive&&a.finishCalls==(a.requestAttempted?1:0)&&b.finishCalls==(b.requestAttempted?1:0),"request owner cleanup once after all normal/exceptional outcomes");
 if(a.faultAt==0)require(!x.threw,"unexpected baseline exception");else require(x.threw,"every selected before/after fault must actually run");
 if(x.threw)++faults;else if(entry==0){require(x.value.has_value(),"recovery bool result");if(*x.value)++held;else ++notHeld;}else if(!x.value)++moveNone;else if(*x.value)++moveTrue;else ++moveFalse;++states;return a.calls;
}
void exercise(unsigned mask,int entry,int mode=0){auto a=fixture(mask),b=fixture(mask);a.mode=b.mode=mode;const int n=compare(a,b,entry);
 for(int fault=1;fault<=n;++fault)for(bool after:{false,true}){auto x=fixture(mask),y=fixture(mask);x.mode=y.mode=mode;x.faultAt=y.faultAt=fault;x.afterFault=y.afterFault=after;compare(x,y,entry);}
}
void verifyFullStateCases(){
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.instanceId);mutateEncoderField(b.object.instanceId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.kind);mutateEncoderField(b.object.kind);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.mapId);mutateEncoderField(b.object.mapId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.owner);mutateEncoderField(b.object.owner);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.type);mutateEncoderField(b.object.type);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.health);mutateEncoderField(b.object.health);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.x);mutateEncoderField(b.object.x);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.y);mutateEncoderField(b.object.y);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.cell);mutateEncoderField(b.object.cell);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.level);mutateEncoderField(b.object.level);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.facing);mutateEncoderField(b.object.facing);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.turretFacing);mutateEncoderField(b.object.turretFacing);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.bodyFacingClock);mutateEncoderField(b.object.bodyFacingClock);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.turretFacingClock);mutateEncoderField(b.object.turretFacingClock);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.subCell);mutateEncoderField(b.object.subCell);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.mission);mutateEncoderField(b.object.mission);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.tag);mutateEncoderField(b.object.tag);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.veterancy);mutateEncoderField(b.object.veterancy);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.veterancyExperience);mutateEncoderField(b.object.veterancyExperience);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.crateArmorMultiplier);mutateEncoderField(b.object.crateArmorMultiplier);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.crateFirepowerMultiplier);mutateEncoderField(b.object.crateFirepowerMultiplier);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.crateSpeedMultiplier);mutateEncoderField(b.object.crateSpeedMultiplier);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.crateCloakable);mutateEncoderField(b.object.crateCloakable);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.harvestAutoSearchSuspended);mutateEncoderField(b.object.harvestAutoSearchSuspended);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.hasBeenCaptured);mutateEncoderField(b.object.hasBeenCaptured);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.onBridge);mutateEncoderField(b.object.onBridge);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.authoredAircraft);mutateEncoderField(b.object.authoredAircraft);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.alive);mutateEncoderField(b.object.alive);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.rubble);mutateEncoderField(b.object.rubble);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.submerged);mutateEncoderField(b.object.submerged);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.cloaked);mutateEncoderField(b.object.cloaked);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.cloakCooldownTicks);mutateEncoderField(b.object.cloakCooldownTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.submergeCooldownTicks);mutateEncoderField(b.object.submergeCooldownTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.deployed);mutateEncoderField(b.object.deployed);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.deployStateTicksRemaining);mutateEncoderField(b.object.deployStateTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.prone);mutateEncoderField(b.object.prone);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.proneTicksRemaining);mutateEncoderField(b.object.proneTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.weaponCooldownTicksRemaining);mutateEncoderField(b.object.weaponCooldownTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.spriteFiringStartTick);mutateEncoderField(b.object.spriteFiringStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.radiationEruptionCooldownTicksRemaining);mutateEncoderField(b.object.radiationEruptionCooldownTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.hasRenderPosition);mutateEncoderField(b.object.hasRenderPosition);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.renderX);mutateEncoderField(b.object.renderX);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.renderY);mutateEncoderField(b.object.renderY);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.renderMoveActive);mutateEncoderField(b.object.renderMoveActive);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.renderLevel);mutateEncoderField(b.object.renderLevel);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.renderOnBridge);mutateEncoderField(b.object.renderOnBridge);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.movementAnimationDistance);mutateEncoderField(b.object.movementAnimationDistance);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.buildAnimTicksTotal);mutateEncoderField(b.object.buildAnimTicksTotal);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.buildAnimTicksRemaining);mutateEncoderField(b.object.buildAnimTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.structureRecoilStartTick);mutateEncoderField(b.object.structureRecoilStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.chargeTurretStartTick);mutateEncoderField(b.object.chargeTurretStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.workAnimationActive);mutateEncoderField(b.object.workAnimationActive);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.poweredOperational);mutateEncoderField(b.object.poweredOperational);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.powerAnimationPausedAtTick);mutateEncoderField(b.object.powerAnimationPausedAtTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.powerAnimationPausedTicks);mutateEncoderField(b.object.powerAnimationPausedTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.factoryExitAnimationStartTick);mutateEncoderField(b.object.factoryExitAnimationStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.factoryExitAnimationEndTick);mutateEncoderField(b.object.factoryExitAnimationEndTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.factoryExitAirborne);mutateEncoderField(b.object.factoryExitAirborne);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.factoryExitHostId);mutateEncoderField(b.object.factoryExitHostId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.specialCashNextTick);mutateEncoderField(b.object.specialCashNextTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.specialCloneDeliveryObjectId);mutateEncoderField(b.object.specialCloneDeliveryObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.transportPassengerIds);mutateEncoderField(b.object.transportPassengerIds);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.bioReactorAnimationStartTick);mutateEncoderField(b.object.bioReactorAnimationStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.bioReactorAnimationPoweredStartTick);mutateEncoderField(b.object.bioReactorAnimationPoweredStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.activeGunnerMode);mutateEncoderField(b.object.activeGunnerMode);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.activeGunnerWeaponIndex);mutateEncoderField(b.object.activeGunnerWeaponIndex);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.activeGunnerTurretIndex);mutateEncoderField(b.object.activeGunnerTurretIndex);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.tankBunkerHostId);mutateEncoderField(b.object.tankBunkerHostId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.tankBunkerOccupantId);mutateEncoderField(b.object.tankBunkerOccupantId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.tankBunkerAnimationStartTick);mutateEncoderField(b.object.tankBunkerAnimationStartTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.tankBunkerLowering);mutateEncoderField(b.object.tankBunkerLowering);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.dockedToObjectId);mutateEncoderField(b.object.dockedToObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.dockIndex);mutateEncoderField(b.object.dockIndex);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.airborne);mutateEncoderField(b.object.airborne);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.flightLevel);mutateEncoderField(b.object.flightLevel);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.aircraftDockFlightProgress);mutateEncoderField(b.object.aircraftDockFlightProgress);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.aircraftBuildingClearanceLevel);mutateEncoderField(b.object.aircraftBuildingClearanceLevel);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.wallConnectionMask);mutateEncoderField(b.object.wallConnectionMask);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.garrisonHostObjectId);mutateEncoderField(b.object.garrisonHostObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.garrisonOccupantIds);mutateEncoderField(b.object.garrisonOccupantIds);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.garrisonFireCursor);mutateEncoderField(b.object.garrisonFireCursor);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.currentAmmo);mutateEncoderField(b.object.currentAmmo);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.pendingAircraftAuxSounds);mutateEncoderField(b.object.pendingAircraftAuxSounds);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.turretSoundActivityTick);mutateEncoderField(b.object.turretSoundActivityTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.activeTurretSoundEvent);mutateEncoderField(b.object.activeTurretSoundEvent);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.pendingTurretSounds);mutateEncoderField(b.object.pendingTurretSounds);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);mutateEncoderField(a.object.modExtensions);mutateEncoderField(b.object.modExtensions);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.kind);perturb(b.behavior.kind);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.state);perturb(b.behavior.state);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.moveTarget);perturb(b.behavior.moveTarget);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.targetObjectId);perturb(b.behavior.targetObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.autoAcquired);perturb(b.behavior.autoAcquired);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.forceFire);perturb(b.behavior.forceFire);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.attackGround);perturb(b.behavior.attackGround);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.attackBridge);perturb(b.behavior.attackBridge);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.attackBridgeCell);perturb(b.behavior.attackBridgeCell);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.path);perturb(b.behavior.path);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pathRequestPending);perturb(b.behavior.pathRequestPending);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingPathTowardRange);perturb(b.behavior.pendingPathTowardRange);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingPathStart);perturb(b.behavior.pendingPathStart);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingPathGoal);perturb(b.behavior.pendingPathGoal);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingHasSharedGoal);perturb(b.behavior.pendingHasSharedGoal);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingSharedGoal);perturb(b.behavior.pendingSharedGoal);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingAllowedOccupiedObjectId);perturb(b.behavior.pendingAllowedOccupiedObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingTargetObjectId);perturb(b.behavior.pendingTargetObjectId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingRangeCells);perturb(b.behavior.pendingRangeCells);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingAllowNearbyTarget);perturb(b.behavior.pendingAllowNearbyTarget);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pendingIgnoredObjectIds);perturb(b.behavior.pendingIgnoredObjectIds);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.repackMoveGroup);perturb(b.behavior.repackMoveGroup);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.moveCooldownTicks);perturb(b.behavior.moveCooldownTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.moveSegmentFrom);perturb(b.behavior.moveSegmentFrom);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.moveIntervalRemainderQ16);perturb(b.behavior.moveIntervalRemainderQ16);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.moveStartupDistanceQ16);perturb(b.behavior.moveStartupDistanceQ16);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderStartupAccelerationDistance);perturb(b.behavior.renderStartupAccelerationDistance);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.factoryExitClearanceHostId);perturb(b.behavior.factoryExitClearanceHostId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.transportTransferHostId);perturb(b.behavior.transportTransferHostId);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.transportTransferKind);perturb(b.behavior.transportTransferKind);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.weaponCooldownTicks);perturb(b.behavior.weaponCooldownTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.blockedTicks);perturb(b.behavior.blockedTicks);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.pathReplans);perturb(b.behavior.pathReplans);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.nextRepathTick);perturb(b.behavior.nextRepathTick);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.returnToDeployed);perturb(b.behavior.returnToDeployed);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.approachSlotIndex);perturb(b.behavior.approachSlotIndex);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderFrom);perturb(b.behavior.renderFrom);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderTo);perturb(b.behavior.renderTo);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderFromOffsetX);perturb(b.behavior.renderFromOffsetX);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderFromOffsetY);perturb(b.behavior.renderFromOffsetY);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderToOffsetX);perturb(b.behavior.renderToOffsetX);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderToOffsetY);perturb(b.behavior.renderToOffsetY);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderFromLevel);perturb(b.behavior.renderFromLevel);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderToLevel);perturb(b.behavior.renderToLevel);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderFromOnBridge);perturb(b.behavior.renderFromOnBridge);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderToOnBridge);perturb(b.behavior.renderToOnBridge);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderMoveTicksTotal);perturb(b.behavior.renderMoveTicksTotal);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderMoveTicksRemaining);perturb(b.behavior.renderMoveTicksRemaining);compare(a,b,e);}}();
[](){for(int e:{0,1}){auto a=fixture(0),b=fixture(0);perturb(a.behavior.renderArrivalDistanceAfter);perturb(b.behavior.renderArrivalDistanceAfter);compare(a,b,e);}}();
}
bool runContract(){try{
 verifyWorld();verifyBehavior();verifyFullStateCases();
 for(unsigned kind=0;kind<4;++kind)for(unsigned mask=0;mask<1024;++mask)exercise(mask|(kind<<16),1);
 for(unsigned kind=0;kind<4;++kind)for(unsigned flags=0;flags<8;++flags)exercise((kind<<16)|((flags&1u)?128u:0u)|((flags&2u)?256u:0u)|((flags&4u)?512u:0u),0);
 unsigned seed=0x21379bdu;for(int i=0;i<512;++i){seed=seed*1664525u+1013904223u;for(int entry:{0,1})exercise(seed&((1u<<18)-1),entry);}
 for(int mode=1;mode<=7;++mode)for(unsigned mask:{0u,16u,32u,48u,64u,128u,256u,512u,1u<<14,48u|(1u<<14)})for(int entry:{0,1}){exercise(mask,entry,mode);++aliases;}
 for(int tick:{INT_MIN,-1,0,99,100,101,INT_MAX})for(int id:{INT_MIN,-1,0,41,53,100,INT_MAX})for(unsigned flags=0;flags<8;++flags){auto a=fixture((flags&1u)?128u:0u),b=fixture((flags&1u)?128u:0u);a.locked=b.locked=(flags&2u)!=0;a.transit=b.transit=(flags&4u)!=0;a.tick=b.tick=tick;a.object.instanceId=b.object.instanceId=id;compare(a,b,0);}
 require(worldProbes==87&&behaviorProbes==49&&faultPorts.size()==7&&mutatedAliasModes.size()==7&&liveMutations>0&&moveTrue>0&&moveFalse>0&&moveNone>0&&held>0&&notHeld>0&&faults>0&&aliases>0&&pathStorageChecks>0&&scopeFinishes>0,"all branches,complete fields,7faultports,aliases andoriginaldeque/object identity checks required");
 std::cout<<"chrono-locomotion checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" behavior-probes="<<behaviorProbes<<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" alias-configs="<<aliases<<" live-mutations="<<liveMutations<<" alias-modes="<<mutatedAliasModes.size()<<" accepted="<<moveTrue<<" rejected="<<moveFalse<<" absent="<<moveNone<<" held="<<held<<" not-held="<<notHeld<<" path-container-checks="<<pathStorageChecks<<" request-scope-finishes="<<scopeFinishes<<" full87World-49Behavior-ordered-queries-effects-and-live-aliases=exact failures=0\n";return true;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}}
}
