#include "simulation/abilities/parasites/RuntimeParasiteStunState.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <sstream>
#include <streambuf>
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
using runtime_authority_hash::runtimeStableHashMix;
using runtime_authority_hash::runtimeStableHashMixString;
namespace parasite_stun_model_detail {
std::string where;
int checks=0,worldProbes=0,states=0,faults=0,liveTicks=0,queryCalls=0,capacity=0,actionProbes=0,codecCases=0,codecFaults=0,streamFaults=0;
void require(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(where+why);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (std::is_same_v<T,ParasiteLaunchRequest>) {return encode(v.projectileId)+encode(v.currentTick)+encode(v.sourceObjectId)+encode(v.targetObjectId)+encode(v.sourceOwner)+encode(v.sourceTypeId)+encode(v.targetTypeId)+encode(v.weaponId)+encode(v.warheadId)+encode(v.targetKind)+encode(v.targetCell)+encode(v.damage)+encode(v.damageIntervalTicks)+encode(v.paralyzes)+encode(v.targetMaximumHealth)+encode(v.targetParasiteable)+encode(v.sourceOrganic)+encode(v.sourceElite)+encode(v.culling)+encode(v.normalCullingThresholdPermyriad)+encode(v.eliteCullingThresholdPermyriad);}
    else if constexpr (std::is_same_v<T,ParasiteImpactRequest>) {return encode(v.projectileId)+encode(v.currentTick)+encode(v.impact)+encode(v.targetObjectId)+encode(v.targetTypeId)+encode(v.targetKind)+encode(v.targetPresent)+encode(v.targetAlive)+encode(v.targetParasiteable)+encode(v.targetMaximumHealth)+encode(v.impactCell)+encode(v.returnCellAvailable)+encode(v.returnCell);}
    else if constexpr (std::is_same_v<T,ParasiteHostEventRequest>) {return encode(v.currentTick)+encode(v.targetObjectId)+encode(v.event)+encode(v.ejectCellAvailable)+encode(v.ejectCell)+encode(v.stunTicks);}
    else if constexpr (std::is_same_v<T,ParasitePendingLaunchState>) {return encode(v.projectileId)+encode(v.sourceObjectId)+encode(v.targetObjectId)+encode(v.owner)+encode(v.sourceTypeId)+encode(v.targetTypeId)+encode(v.weaponId)+encode(v.warheadId)+encode(v.targetKind)+encode(v.targetCell)+encode(v.launchedTick)+encode(v.damage)+encode(v.damageIntervalTicks)+encode(v.paralyzes)+encode(v.targetMaximumHealth)+encode(v.cullingThresholdPerMyriad)+encode(v.targetParasiteable)+encode(v.organic)+encode(v.sourceElite)+encode(v.culling);}
    else if constexpr (std::is_same_v<T,ParasiteResolvedLaunchState>) {return encode(v.projectileId)+encode(v.sourceObjectId)+encode(v.targetObjectId)+encode(v.resolvedTick)+encode(v.hostTerminated);}
    else if constexpr (std::is_same_v<T,ParasiteLinkState>) {return encode(v.projectileId)+encode(v.targetObjectId)+encode(v.sourceObjectId)+encode(v.owner)+encode(v.sourceTypeId)+encode(v.targetTypeId)+encode(v.weaponId)+encode(v.warheadId)+encode(v.damage)+encode(v.attachedTick)+encode(v.nextDamageTick)+encode(v.damageIntervalTicks)+encode(v.organic)+encode(v.sourceElite)+encode(v.paralyzes)+encode(v.culling)+encode(v.cullingThresholdPerMyriad)+encode(v.targetMaximumHealth)+encode(v.sourceExpectedInLimbo);}
    else if constexpr (std::is_same_v<T,ParasiteOperationResult>) {return encode(v.accepted)+encode(v.idempotent)+encode(v.reason)+encode(v.actions);}
    else if constexpr (std::is_same_v<T,SpecialMechanismAction>) {return encode(v.kind)+encode(v.tick)+encode(v.projectileId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.owner)+encode(v.damage)+encode(v.warheadId)+encode(v.forceDestruction)+encode(v.paralyzes)+encode(v.culling)+encode(v.suppressDeathWeapon)+encode(v.stunTicks)+encode(v.cell)+encode(v.reason)+encode(v.replacesTerminatingHost)+encode(v.sourceWasAttachedToHost);}
    else if constexpr (std::is_same_v<T,SpecialMechanismEvent>) {return encode(v.kind)+encode(v.tick)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.value)+encode(v.cell)+encode(v.reason);}
    else if constexpr (requires {v.first;v.second;}) {return encode(v.first)+encode(v.second);}
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
    else if constexpr(requires {typename T::mapped_type;}) {if(v.empty())v.emplace(typename T::key_type{},typename T::mapped_type{});else v.clear();}
    else if constexpr(requires {v.empty();v.clear();v.emplace_back();}) {if(v.empty())v.emplace_back();else v.clear();}
    else if constexpr(requires {v.x;v.y;}) v.x=v.x==0?1:0;
    else static_assert(sizeof(T)==0,"unprobed canonical WorldObject field");
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



struct Original {
using Container=std::unordered_map<int,int>;Container values;
bool isActive(int id,const int& tick)const{auto p=values.find(id);return p!=values.end() && tick<p->second;}
void apply(const WorldObjectInstance* source,const int& tick,const SpecialMechanismAction& action){
            if (action.stunTicks > 0) {
                values[source->instanceId] =
                    std::max(
                        tick,
                        action.tick
                    ) + action.stunTicks;
            } else {
                values.erase(
                    source->instanceId
                );
            }
}
void advance(const int& tick,const RuntimeParasiteStunObjectQueries& queries,bool& changed){
    for (auto stun = values.begin();
         stun != values.end();) {
        if (tick < stun->second) {
            ++stun;
            continue;
        }
        WorldObjectInstance* object = queries.mutableObject(stun->first);
        if (object != nullptr && object->alive
            && object->mission == "ParasiteStunned") {
            object->mission = "Guard";
        }
        stun = values.erase(stun);
        changed = true;
    }

}

void writeSaveSection(std::ostream& output,const RuntimeParasiteStunReadQueries& queries,const int& tick)const{
        std::vector<std::pair<int, int>> parasiteStuns;
        parasiteStuns.reserve(
            values.size()
        );
        for (const auto& [objectId, untilTick] :
                values) {
            const WorldObjectInstance* object = queries.object(objectId);
            if (object != nullptr && object->alive
                && object->health > 0 && untilTick > tick) {
                parasiteStuns.emplace_back(objectId, untilTick);
            }
        }
        std::sort(parasiteStuns.begin(), parasiteStuns.end());
        output << "\n[ParasiteStuns]\n";
        output << "Schema=1\n";
        output << "Count=" << parasiteStuns.size() << "\n";
        for (std::size_t index = 0; index < parasiteStuns.size(); ++index) {
            output << index << "=" << parasiteStuns[index].first << "|"
                   << parasiteStuns[index].second << "\n";
        }
}
void mixPersistentHash(std::uint64_t& hash,const RuntimeParasiteStunReadQueries& queries,const int& tick)const{
    std::vector<std::pair<int, int>> parasiteStuns;
    parasiteStuns.reserve(
        values.size()
    );
    for (const auto& [objectId, untilTick] :
            values) {
        const WorldObjectInstance* object = queries.object(objectId);
        if (object != nullptr && object->alive && object->health > 0
            && object->mission == "ParasiteStunned"
            && untilTick > tick) {
            parasiteStuns.emplace_back(objectId, untilTick);
        }
    }
    std::sort(parasiteStuns.begin(), parasiteStuns.end());
    runtimeStableHashMixString(hash, "ParasiteStuns/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(parasiteStuns.size())
    );
    for (const auto& [objectId, untilTick] : parasiteStuns) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, untilTick);
    }
}
void mixAuthorityHash(std::uint64_t& hash)const{
        std::vector<std::pair<int, int>> sorted(
            values.begin(),
            values.end()
        );
        std::sort(sorted.begin(), sorted.end());
        runtimeStableHashMix(hash, static_cast<long long>(sorted.size()));
        for (const auto& [key, value] : sorted) {
            runtimeStableHashMix(hash, key);
            runtimeStableHashMix(hash, value);
        }
}
};

using Container=RuntimeParasiteStunState::Container;
struct Fault{};
struct Queries final:RuntimeParasiteStunObjectQueries {
 std::function<WorldObjectInstance*(int)> callback;
 WorldObjectInstance* mutableObject(int id)const override{return callback(id);}
};
struct Fixture {
 int tick=100,calls=0,throwAt=-1,mutateAt=-1,mode=0;
 bool changed=false;
 std::map<int,WorldObjectInstance> world;
 const Container* observed=nullptr;
 std::function<bool(int,const int&)> active;
 std::vector<std::string> trace;
 Queries queries;
 Fixture(int m,int fault,bool initial):throwAt(fault),mode(m),changed(initial){
  tick=100+(m%7);mutateAt=(m%3==0?1:-1);
  for(int i=0;i<11;++i){WorldObjectInstance object;object.instanceId=i-3;object.type="owned-stun-type-"+std::to_string(i);object.owner=std::string(32+i,'o');object.mission=(i%3?"ParasiteStunned":"Guard");object.alive=((i+m)%5!=0);object.health=(i%4==0?0:73);object.x=i*3;object.y=-i;world.emplace(object.instanceId,std::move(object));}
  queries.callback=[this](int id)->WorldObjectInstance*{
   const int call=calls++;++queryCalls;require(observed!=nullptr&&observed->contains(id),"query observes original pending entry before erase");
   trace.push_back(encode(id)+encode(tick)+encode(*observed)+encode(world));
   if(call==mutateAt){tick+=(mode%2?7:-3);++liveTicks;auto it=world.find(id);if(it!=world.end()){it->second.alive=mode%2==0;it->second.mission="ParasiteStunned";it->second.health=0;}}
   trace.push_back(encode(active(id,tick))+encode(tick));
   if(call==throwAt)throw Fault{};
   auto it=world.find(id);if((mode%7==0&&call%2==0)||it==world.end())return nullptr;
   return &it->second;
  };
 }
};
Container seed(int mode){Container values;values.max_load_factor(mode%2?0.75f:1.0f);values.reserve(1+(mode%31));for(int i=0;i<13;++i){const int k=((i*7+mode)%13)-4;values.emplace(k,96+((i+mode)%13));}return values;}
void restorePair(Original& old,RuntimeParasiteStunState& current,const Container& input){Container a=input,b=input;old.values=std::move(a);current.restoreValidated(std::move(b));}
bool sameContainer(const Container& a,const Container& b){return encode(a)==encode(b)&&a.bucket_count()==b.bucket_count()&&a.max_load_factor()==b.max_load_factor();}
void compare(const Original& old,const RuntimeParasiteStunState& now,const Fixture& a,const Fixture& b){
 require(encode(old.values)==encode(now.entries()),"original unordered traversal and every entry");
 require(old.values.bucket_count()==now.entries().bucket_count()&&old.values.max_load_factor()==now.entries().max_load_factor(),"bucket and load-factor lifecycle old="+std::to_string(old.values.bucket_count())+" new="+std::to_string(now.entries().bucket_count())+" oldLoad="+std::to_string(old.values.max_load_factor())+" newLoad="+std::to_string(now.entries().max_load_factor()));
 require(encode(a.world)==encode(b.world),"all87 World fields plus all nested owned values");
 require(a.tick==b.tick&&a.changed==b.changed&&a.calls==b.calls&&a.trace==b.trace,"live-tick,changed-reference,queries and full observation order");
}
int expiryCase(int mode,int fault,bool initial){
 where="expiry mode="+std::to_string(mode)+" fault="+std::to_string(fault)+" initial="+std::to_string(initial)+" ";
 auto input=seed(mode);Original old;RuntimeParasiteStunState current;restorePair(old,current,input);
 where+="setupOldBuckets="+std::to_string(old.values.bucket_count())+" setupNewBuckets="+std::to_string(current.entries().bucket_count())+" ";
 Fixture a(mode,fault,initial),b(mode,fault,initial);a.observed=&old.values;b.observed=&current.entries();a.active=[&](int id,const int& tick){return old.isActive(id,tick);};b.active=[&](int id,const int& tick){return current.isActive(id,tick);};
 for(int id=-7;id<16;++id)for(int tick:std::array<int,8>{INT_MIN,-1,0,99,100,105,200,INT_MAX})require(old.isActive(id,tick)==current.isActive(id,tick),"original active lookup boundaries and missing IDs");
 bool failedA=false,failedB=false;try{old.advance(a.tick,a.queries,a.changed);}catch(const Fault&){failedA=true;}try{current.advanceExpired(b.tick,b.queries,b.changed);}catch(const Fault&){failedB=true;}
 require(failedA==failedB,"same exception observation");compare(old,current,a,b);if(failedA)++faults;++states;
 return a.calls;
}
void transferAndCopy(int mode){
 where="transfer mode="+std::to_string(mode)+" ";Container values=seed(mode),oldMove=values,newMove=values;Original old;old.values=std::move(oldMove);RuntimeParasiteStunState current;current.restoreValidated(std::move(newMove));
 require(sameContainer(oldMove,newMove),"validated restore moved-from input lifecycle");require(sameContainer(old.values,current.entries()),"paired initial validated move layout");
 auto copied=current;RuntimeParasiteStunState assigned;assigned=current;auto originalCopy=old.values;Container originalAssigned;originalAssigned=old.values;
 require(sameContainer(copied.entries(),originalCopy)&&sameContainer(assigned.entries(),originalAssigned),"owned copy and copyassignment preserve their own original map operations");
 WorldObjectInstance object;object.instanceId=500+mode;SpecialMechanismAction action;action.tick=100;action.stunTicks=3;int tick=102;old.apply(&object,tick,action);current.applyEjectionStun(object,tick,action);
 require(sameContainer(old.values,current.entries())&&sameContainer(copied.entries(),originalCopy),"copy independent from later mutation");
 auto moved=std::move(copied);auto oldMoved=std::move(originalCopy);require(sameContainer(moved.entries(),oldMoved)&&sameContainer(copied.entries(),originalCopy),"moveconstructor and moved-from full container");
 RuntimeParasiteStunState moveAssigned;moveAssigned=std::move(assigned);Container oldMoveAssigned;oldMoveAssigned=std::move(originalAssigned);require(sameContainer(moveAssigned.entries(),oldMoveAssigned)&&sameContainer(assigned.entries(),originalAssigned),"moveassignment and moved-from full container");
 using std::swap;swap(current,moved);swap(old.values,oldMoved);require(sameContainer(current.entries(),old.values)&&sameContainer(moved.entries(),oldMoved),"ADLswap matches original map swap");++states;
}
void ejectionCases(){
 for(int id:std::array<int,6>{INT_MIN,-1,0,1,72,INT_MAX})for(int tick:std::array<int,7>{INT_MIN,-77,-1,0,99,104,INT_MAX-7})for(int actionTick:std::array<int,4>{INT_MIN,-1,99,INT_MAX-7})for(int stun:std::array<int,6>{INT_MIN,-1,0,1,3,7}){
  Original old;RuntimeParasiteStunState current;auto input=seed(5);restorePair(old,current,input);WorldObjectInstance object;object.instanceId=id;object.owner=std::string(64,'u');object.mission="source-owned";SpecialMechanismAction action;action.tick=actionTick;action.stunTicks=stun;
  auto beforeObject=encode(object),beforeAction=encode(action);old.apply(&object,tick,action);current.applyEjectionStun(object,tick,action);require(encode(old.values)==encode(current.entries()),"original max(liveTick,actionTick)+stun or erase");require(encode(object)==beforeObject&&encode(action)==beforeAction,"complete borrowed source and action unchanged");++states;
 }
}
void actionFieldProbes(){SpecialMechanismAction zero;const auto before=encode(zero);
{auto value=zero;mutateEncoderField(value.kind);require(encode(value)!=before,"action kind omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.tick);require(encode(value)!=before,"action tick omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=before,"action projectileId omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.objectId);require(encode(value)!=before,"action objectId omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=before,"action targetObjectId omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.owner);require(encode(value)!=before,"action owner omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.damage);require(encode(value)!=before,"action damage omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.warheadId);require(encode(value)!=before,"action warheadId omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.forceDestruction);require(encode(value)!=before,"action forceDestruction omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.paralyzes);require(encode(value)!=before,"action paralyzes omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.culling);require(encode(value)!=before,"action culling omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.suppressDeathWeapon);require(encode(value)!=before,"action suppressDeathWeapon omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.stunTicks);require(encode(value)!=before,"action stunTicks omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.cell);require(encode(value)!=before,"action cell omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.reason);require(encode(value)!=before,"action reason omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.replacesTerminatingHost);require(encode(value)!=before,"action replacesTerminatingHost omitted");++actionProbes;}
{auto value=zero;mutateEncoderField(value.sourceWasAttachedToHost);require(encode(value)!=before,"action sourceWasAttachedToHost omitted");++actionProbes;}
}
void capacityCase(){Container input;input.reserve(8192);for(int i=0;i<4096;++i)input.emplace(i,i%2?100:101);Original old;RuntimeParasiteStunState current;restorePair(old,current,input);int aTick=100,bTick=100;bool aChanged=false,bChanged=false;int aCalls=0,bCalls=0;Queries a,b;a.callback=[&](int){++aCalls;return nullptr;};b.callback=[&](int){++bCalls;return nullptr;};old.advance(aTick,a,aChanged);current.advanceExpired(bTick,b,bChanged);require(encode(old.values)==encode(current.entries())&&aCalls==bCalls&&aChanged==bChanged,"4096 hosts partial expiry original unordered lifecycle");aTick=bTick=101;old.advance(aTick,a,aChanged);current.advanceExpired(bTick,b,bChanged);require(old.values.empty()&&current.entries().empty()&&aCalls==4096&&bCalls==4096,"all4096 expiry queries and missing-object erase");capacity=4096;}

class ReadQueries final:public RuntimeParasiteStunReadQueries {
 const RuntimeParasiteStunObjectQueries& source_;
public:
 explicit ReadQueries(const RuntimeParasiteStunObjectQueries& source):source_(source){}
 const WorldObjectInstance* object(int id)const override{return source_.mutableObject(id);}
};
int codecCase(int mode,int fault,int purpose){
 where="codec mode="+std::to_string(mode)+" fault="+std::to_string(fault)+" purpose="+std::to_string(purpose)+" ";
 auto input=seed(mode);Original old;RuntimeParasiteStunState current;restorePair(old,current,input);
 Fixture a(mode,fault,false),b(mode,fault,false);a.observed=&old.values;b.observed=&current.entries();a.active=[&](int id,const int& tick){return old.isActive(id,tick);};b.active=[&](int id,const int& tick){return current.isActive(id,tick);};ReadQueries qa(a.queries),qb(b.queries);
 std::ostringstream sa,sb;std::uint64_t ha=std::numeric_limits<std::uint64_t>::max()-static_cast<unsigned>(mode),hb=ha;bool fa=false,fb=false;
 try{if(purpose==0)old.writeSaveSection(sa,qa,a.tick);else if(purpose==1)old.mixPersistentHash(ha,qa,a.tick);else old.mixAuthorityHash(ha);}catch(const Fault&){fa=true;}
 try{if(purpose==0){auto snapshot=current.saveSnapshot(qb,b.tick);RuntimeParasiteStunState::writeSaveSection(sb,snapshot);}else if(purpose==1){auto snapshot=current.persistentSnapshot(qb,b.tick);RuntimeParasiteStunState::mixPersistentHash(hb,snapshot);}else current.mixAuthorityHash(hb);}catch(const Fault&){fb=true;}
 require(fa==fb&&sa.str()==sb.str()&&ha==hb,"original saveSchema1/hash purposes/query exception and partial output");compare(old,current,a,b);if(fa)++codecFaults;++codecCases;return a.calls;
}
void codecPurposeWitness(){
 Original old;RuntimeParasiteStunState current;Container input={{1,101},{2,99},{3,102},{4,200},{5,103}};restorePair(old,current,input);std::map<int,WorldObjectInstance> objects;
 for(int i=1;i<5;++i){WorldObjectInstance v;v.instanceId=i;v.alive=i!=4;v.health=i==3?0:90;v.mission=i==1?"Guard":"ParasiteStunned";objects.emplace(i,std::move(v));}
 Queries mutableQueries;mutableQueries.callback=[&](int id)->WorldObjectInstance*{auto p=objects.find(id);return p==objects.end()?nullptr:&p->second;};ReadQueries q(mutableQueries);int tick=100;std::ostringstream a,b;old.writeSaveSection(a,q,tick);auto callerSaveSnapshot=current.saveSnapshot(q,tick);RuntimeParasiteStunState::writeSaveSection(b,callerSaveSnapshot);
 require(a.str()==b.str()&&a.str()=="\n[ParasiteStuns]\nSchema=1\nCount=1\n0=1|101\n","fullSave keeps healthy live unexpired Guard despite other hash mission guard");
 std::uint64_t ha=17,hb=17,expected=17;old.mixPersistentHash(ha,q,tick);auto callerPersistentSnapshot=current.persistentSnapshot(q,tick);RuntimeParasiteStunState::mixPersistentHash(hb,callerPersistentSnapshot);runtimeStableHashMixString(expected,"ParasiteStuns/1");runtimeStableHashMix(expected,0);require(ha==hb&&ha==expected,"persistent hash exact extra mission gate and empty label/count");
 objects.at(1).mission="ParasiteStunned";std::uint64_t changed=17;auto changedSnapshot=current.persistentSnapshot(q,tick);RuntimeParasiteStunState::mixPersistentHash(changed,changedSnapshot);require(changed!=hb,"persistent mission witness positive");
 auto rawA=std::uint64_t{19},rawB=rawA;old.mixAuthorityHash(rawA);current.mixAuthorityHash(rawB);require(rawA==rawB,"authority mixes all original entries including expired/missing/dead/zerohealth");old.values.at(5)=-999;auto different=std::uint64_t{19};old.mixAuthorityHash(different);require(different!=rawB,"authority unfiltered negative deadline witness");
 current.restoreValidated(Container{});objects.clear();std::ostringstream retained;RuntimeParasiteStunState::writeSaveSection(retained,callerSaveSnapshot);auto retainedHash=std::uint64_t{17};RuntimeParasiteStunState::mixPersistentHash(retainedHash,changedSnapshot);require(retained.str()==b.str()&&retainedHash==changed,"caller-owned complete snapshots survive owner and World replacement until original caller scope ends");
}
struct LimitedBuffer final:std::streambuf {
 std::string bytes;std::size_t limit;
 explicit LimitedBuffer(std::size_t maximum):limit(maximum){}
 int_type overflow(int_type c)override{if(traits_type::eq_int_type(c,traits_type::eof()))return traits_type::not_eof(c);if(bytes.size()>=limit)throw std::runtime_error("stun-output-fault");bytes.push_back(traits_type::to_char_type(c));return c;}
 std::streamsize xsputn(const char* data,std::streamsize n)override{for(std::streamsize i=0;i<n;++i)overflow(traits_type::to_int_type(data[i]));return n;}
};
void streamFailureCases(){
 for(std::size_t limit=0;limit<=128;++limit){where="stream limit="+std::to_string(limit)+" ";Original old;RuntimeParasiteStunState current;auto input=seed(1);restorePair(old,current,input);Fixture a(1,-1,false),b(1,-1,false);a.mutateAt=b.mutateAt=-1;a.observed=&old.values;b.observed=&current.entries();a.active=[&](int id,const int& tick){return old.isActive(id,tick);};b.active=[&](int id,const int& tick){return current.isActive(id,tick);};ReadQueries qa(a.queries),qb(b.queries);LimitedBuffer ba(limit),bb(limit);std::ostream oa(&ba),ob(&bb);oa.exceptions(std::ios::badbit|std::ios::failbit);ob.exceptions(std::ios::badbit|std::ios::failbit);bool fa=false,fb=false;
 try{old.writeSaveSection(oa,qa,a.tick);}catch(const std::exception&){fa=true;}try{auto snapshot=current.saveSnapshot(qb,b.tick);RuntimeParasiteStunState::writeSaveSection(ob,snapshot);}catch(const std::exception&){fb=true;}require(fa==fb&&ba.bytes==bb.bytes&&oa.rdstate()==ob.rdstate(),"all stream prefix/error/exception observations exact");compare(old,current,a,b);if(fa)++streamFaults;
 }
 require(streamFaults>0,"positive partial stream failure witness");
}

bool runContract(){try{
 checks=worldProbes=states=faults=liveTicks=queryCalls=capacity=actionProbes=0;
 static_assert(sizeof(RuntimeParasiteStunState)==sizeof(Container));static_assert(alignof(RuntimeParasiteStunState)==alignof(Container));static_assert(std::is_nothrow_swappable_v<RuntimeParasiteStunState>);static_assert(std::is_nothrow_move_constructible_v<RuntimeParasiteStunState> == std::is_nothrow_move_constructible_v<Container>);
 codecCases=codecFaults=streamFaults=0;
 verifyWorld();actionFieldProbes();require(worldProbes==87&&actionProbes==17,"completefieldprobes");
 ejectionCases();for(int mode=0;mode<128;++mode){for(bool initial:{false,true}){int calls=expiryCase(mode,-1,initial);for(int fault=0;fault<calls;++fault)expiryCase(mode,fault,initial);}transferAndCopy(mode);}capacityCase();
 codecPurposeWitness();for(int mode=0;mode<96;++mode){for(int purpose=0;purpose<2;++purpose){int calls=codecCase(mode,-1,purpose);for(int fault=0;fault<calls;++fault)codecCase(mode,fault,purpose);}codecCase(mode,-1,2);}streamFailureCases();
 require(faults>100&&liveTicks>0&&capacity==4096,"positive faults/live-clock/capacity witnesses");
 std::cout<<"parasite-stun checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" action-probes="<<actionProbes<<" query-faults="<<faults<<" live-tick-mutations="<<liveTicks<<" query-observations="<<queryCalls<<" capacity-hosts="<<capacity<<" codec-cases="<<codecCases<<" codec-query-faults="<<codecFaults<<" stream-faults="<<streamFaults<<" codec-purposes=3 original-expiry-ejection-query-order-partial-failure-owned-copy-move-swap-saveSchema1-and-bothhashes=exact failures=0\n";return true;
 }catch(const std::exception& error){std::cerr<<"parasite-stun failure: "<<error.what()<<'\n';return false;}}
}

