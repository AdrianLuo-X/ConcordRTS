#include "simulation/abilities/parasites/RuntimeParasiteActionPolicy.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
namespace parasite_action_model_detail {
namespace current=concord::simulation::abilities;
using Kind=SpecialMechanismActionKind;
using Queries=current::RuntimeParasiteActionQueries;
int checks=0,states=0,worldProbes=0,actionProbes=0,faults=0,mutations=0,nestedRuns=0,capacityChecks=0;
std::set<std::string> faultPorts;
void require(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
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

namespace original {
struct State { const Queries& queries;const std::unordered_set<int>& pendingObjectExitIds; };
using RuntimeGameSessionState=State;
thread_local const Queries* activeQueries=nullptr;
struct Scope { const Queries* before;explicit Scope(const Queries& q):before(activeQueries){activeQueries=&q;}~Scope(){activeQueries=before;} };
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(const State& state,int id){return state.queries.objectIncludingInactive(id);}
std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(const State& state,const WorldObjectInstance& source,const SpecialCell& requested,const std::unordered_set<int>* reserved,int terminatingHostId){return state.queries.placementCell(source,requested,reserved,terminatingHostId);}
int runtimeCellKey(int x,int y){require(activeQueries!=nullptr,"original cell key scope");return activeQueries->cellKey(x,y);}
std::string runtimeRepairDepotCanonicalOwnerId(const State& state,const std::string& owner){return state.queries.canonicalOwner(owner);}
bool runtimeParasiteObjectHasExclusiveAuthority(const State& state,const WorldObjectInstance& object,bool asHost=false){return state.queries.exclusiveAuthority(object,asHost);}
std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions) {
    std::vector<SpecialMechanismAction> prepared = actions;
    std::unordered_set<int> reservedPlacementCells;
    for (SpecialMechanismAction& action : prepared) {
        if (action.kind
                != SpecialMechanismActionKind::ParasiteReturnFromLimbo
            && action.kind != SpecialMechanismActionKind::ParasiteEject) {
            continue;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(state, action.objectId);
        const std::optional<RuntimeCellPoint> placement =
            source == nullptr
                ? std::nullopt
                : runtimeParasitePlacementCell(
                    state,
                    *source,
                    action.cell,
                    &reservedPlacementCells,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                );
        if (!placement.has_value()) {
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.suppressDeathWeapon = true;
            action.replacesTerminatingHost = false;
            action.stunTicks = 0;
            action.reason += "; no legal host placement";
            continue;
        }
        action.cell = {placement->x, placement->y};
        reservedPlacementCells.insert(
            runtimeCellKey(placement->x, placement->y)
        );
    }
    return prepared;
}

bool validateRuntimeParasiteActionBatch(
    const RuntimeGameSessionState& state,
    const std::vector<SpecialMechanismAction>& actions) {
    std::unordered_set<int> returningSources;
    std::unordered_set<int> terminalObjects;
    for (const SpecialMechanismAction& action : actions) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        if (action.tick < 0 || action.projectileId == 0
            || action.objectId < 0 || action.targetObjectId < 0) {
            return false;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(state, action.objectId);
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        if (action.replacesTerminatingHost
            && (source == nullptr || target == nullptr
                || source->instanceId == target->instanceId
                || action.stunTicks != 0
                || !((action.kind
                        == SpecialMechanismActionKind::ParasiteReturnFromLimbo
                        && target->kind == WorldObjectKind::Infantry)
                    || (action.kind == SpecialMechanismActionKind::ParasiteEject
                        && target->kind == WorldObjectKind::Unit)))) {
            return false;
        }
        if (action.sourceWasAttachedToHost
            && action.kind != SpecialMechanismActionKind::ParasiteEject
            && action.kind != SpecialMechanismActionKind::ParasiteKillParasite) {
            return false;
        }
        if (source != nullptr && !action.owner.empty()
            && runtimeRepairDepotCanonicalOwnerId(state, source->owner)
                != runtimeRepairDepotCanonicalOwnerId(
                    state,
                    action.owner
                )) {
            return false;
        }
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo:
            if (source == nullptr || !source->alive || source->health <= 0
                || state.pendingObjectExitIds.find(source->instanceId)
                    != state.pendingObjectExitIds.end()
                || runtimeParasiteObjectHasExclusiveAuthority(
                    state,
                    *source
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteAttachHost:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || target == nullptr || !target->alive
                || target->health <= 0
                || runtimeParasiteObjectHasExclusiveAuthority(
                    state,
                    *target,
                    true
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || !returningSources.insert(source->instanceId).second
                || !runtimeParasitePlacementCell(
                    state,
                    *source,
                    action.cell,
                    nullptr,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                ).has_value()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target == nullptr || !target->alive || target->health <= 0
                || action.damage < 0) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
            if (target == nullptr || !target->alive || target->health <= 0
                || !terminalObjects.insert(target->instanceId).second
                || state.pendingObjectExitIds.find(target->instanceId)
                    != state.pendingObjectExitIds.end()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillParasite:
            if (source != nullptr && source->health > 0
                && (!terminalObjects.insert(source->instanceId).second
                    || state.pendingObjectExitIds.find(source->instanceId)
                        != state.pendingObjectExitIds.end())) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    return true;
}
}
struct QueryFault { int at;std::string port; };
struct Outcome { bool completed=false,accepted=false;std::vector<SpecialMechanismAction> actions;int failedAt=0;std::string failedPort; };
struct Model;
Outcome execute(Model& model,bool reference,bool prepare);
struct Model final : Queries {
 mutable std::vector<WorldObjectInstance> objects;
 mutable std::vector<SpecialMechanismAction> actions;
 mutable std::unordered_set<int> pending;
 mutable std::map<std::string,std::string> aliases{{"BLUE","blue"},{"blue","blue"},{"local","blue"},{"RED","red"},{"red","red"}};
 mutable std::set<std::pair<int,bool>> exclusive;
 mutable std::vector<RuntimeCellPoint> choices{{8,9},{9,9},{10,9}};
 mutable std::vector<std::string> trace;
 mutable int calls=0;int faultAt=0,mutationAt=0,mutationKind=0,nestAt=0;
 mutable std::function<void()> nested;
 mutable std::string nestedDigest;
 bool honorReservations=true,largePlacement=false;
 void touch(const std::string& port,const std::string& payload) const {
  ++calls;trace.push_back(port+":"+payload);
  if(calls==mutationAt){
   if(mutationKind==1&&!objects.empty()){objects[0].health=0;objects[0].owner="RED";}
   if(mutationKind==2&&objects.size()>1){objects[1].alive=false;objects[1].mission="Guard";}
   if(mutationKind==3){pending.insert(11);pending.insert(21);}
   if(mutationKind==4&&!actions.empty()){actions[0].owner="RED";actions[0].damage=-1;actions[0].sourceWasAttachedToHost=true;}
   if(mutationKind==5){aliases["BLUE"]="red";aliases["local"]="red";}
   if(mutationKind==6&&!objects.empty()){objects[0].renderX=123.25;objects[0].submerged=!objects[0].submerged;}
   if(mutationKind==7){choices.clear();}
  }
  if(calls==nestAt&&nested)nested();
  if(calls==faultAt)throw QueryFault{calls,port};
 }
 const WorldObjectInstance* objectIncludingInactive(int id) const override {
  touch("object",encode(id));
  for(std::size_t i=0;i<objects.size();++i)if(objects[i].instanceId==id){trace.push_back("found:"+encode(i)+encode(objects[i]));return &objects[i];}
  trace.push_back("absent");return nullptr;
 }
 std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance& source,const SpecialCell& requested,const std::unordered_set<int>* reserved,int terminatingHostId) const override {
  const auto sorted=reserved?std::set<int>(reserved->begin(),reserved->end()):std::set<int>{};
  touch("placement",encode(source)+encode(requested)+(reserved?"reserved":"null")+encode(sorted)+encode(terminatingHostId));
  if(requested.x<0||requested.y<0){trace.push_back("placement:none");return std::nullopt;}
  if(largePlacement){RuntimeCellPoint p{source.instanceId%512,source.instanceId/512};trace.push_back("placement:"+encode(p));return p;}
  for(const auto& p:choices){const int key=p.x<0||p.y<0?-1:p.y*1000+p.x;if(!honorReservations||reserved==nullptr||!reserved->contains(key)){trace.push_back("placement:"+encode(p));return p;}}
  trace.push_back("placement:none");return std::nullopt;
 }
 int cellKey(int x,int y) const override {touch("cell-key",encode(x)+encode(y));const int key=x<0||y<0?-1:y*1000+x;trace.push_back("key:"+encode(key));return key;}
 std::string canonicalOwner(const std::string& owner) const override {touch("owner",encode(owner));const auto i=aliases.find(owner);const std::string value=i==aliases.end()?owner:i->second;trace.push_back("owner:"+encode(value));return value;}
 bool exclusiveAuthority(const WorldObjectInstance& object,bool asHost) const override {touch("exclusive",encode(object)+encode(asHost));const bool value=exclusive.contains({object.instanceId,asHost});trace.push_back("exclusive:"+encode(value));return value;}
 std::string snapshot() const {return encode(objects)+encode(actions)+encode(std::set<int>(pending.begin(),pending.end()))+encode(aliases)+encode(exclusive)+encode(choices)+encode(nestedDigest);}
};
SpecialMechanismAction action(Kind kind,int source=11,int target=21){SpecialMechanismAction a;a.kind=kind;a.tick=7;a.projectileId=31;a.objectId=source;a.targetObjectId=target;a.owner="BLUE";a.damage=3;a.warheadId="warhead/full";a.forceDestruction=true;a.paralyzes=2;a.culling=true;a.stunTicks=7;a.cell={4,5};a.reason="original reason";return a;}
Model base(Kind kind){
 Model m;WorldObjectInstance source;source.instanceId=11;source.kind=WorldObjectKind::Unit;source.alive=false;source.health=100;source.owner="BLUE";source.type="DRON";source.mission="ParasiteLimbo";source.x=4;source.y=5;source.cell=5004;
 auto target=source;target.instanceId=21;target.alive=true;target.health=200;target.type="HTNK";target.mission="Guard";
 auto other=source;other.instanceId=12;auto infantry=target;infantry.instanceId=22;infantry.kind=WorldObjectKind::Infantry;infantry.type="E1";
 auto duplicate=source;duplicate.owner="RED";duplicate.health=0;duplicate.mission="bad duplicate";
 m.objects={source,target,other,infantry,duplicate};m.actions={action(kind)};return m;
}
Outcome execute(Model& m,bool reference,bool prepare){
 Outcome out;
 m.nested=[&m,reference,prepare]{
  ++nestedRuns;Model child=base(Kind::ParasiteReturnFromLimbo);child.actions.push_back(action(Kind::ParasiteEject,12));
  Outcome value=execute(child,reference,prepare);require(value.completed,"nested completion");
  m.nestedDigest=encode(value.accepted)+encode(value.actions)+encode(child.trace)+child.snapshot();
 };
 original::Scope active(m);
 try {
  if(prepare){out.actions=reference?original::prepareRuntimeParasiteActions({m,m.pending},m.actions):current::prepareRuntimeParasiteActions(m.actions,m);out.accepted=true;}
  else out.accepted=reference?original::validateRuntimeParasiteActionBatch({m,m.pending},m.actions):current::validateRuntimeParasiteActionBatch(m.actions,m.pending,m);
  out.completed=true;
 }catch(const QueryFault& fault){out.failedAt=fault.at;out.failedPort=fault.port;}
 m.nested={};return out;
}
struct Compared { int calls=0;Outcome result;std::vector<std::string> trace; };
Compared compare(const Model& seed,bool prepare){
 Model originalModel=seed,currentModel=seed;
 Outcome a=execute(originalModel,true,prepare),b=execute(currentModel,false,prepare);
 ++states;require(a.completed==b.completed&&a.accepted==b.accepted&&a.failedAt==b.failedAt&&a.failedPort==b.failedPort,"result/exception differs");
 require(encode(a.actions)==encode(b.actions),"complete action value differs");require(originalModel.trace==currentModel.trace,"ordered full query trace differs");require(originalModel.snapshot()==currentModel.snapshot(),"complete World/input/pending/alias/nested state differs");require(originalModel.calls==currentModel.calls,"query count differs");
 if(!a.completed){++faults;faultPorts.insert(a.failedPort);}if(seed.mutationAt>0)++mutations;
 return {originalModel.calls,b,currentModel.trace};
}
void exercise(const Model& seed,bool deep){
 for(bool prepare:{false,true}){
  const auto observed=compare(seed,prepare);
  if(deep)for(int at=1;at<=observed.calls;++at){Model failed=seed;failed.faultAt=at;compare(failed,prepare);}
 }
}
void variant(Model& m,int which){
 switch(which){
 case 1:m.objects[0].alive=true;m.objects[0].mission="Guard";break;
 case 2:std::erase_if(m.objects,[](const auto& o){return o.instanceId==11;});break;
 case 3:std::erase_if(m.objects,[](const auto& o){return o.instanceId==21;});break;
 case 4:m.objects[0].health=0;break;
 case 5:m.objects[1].health=0;break;
 case 6:m.objects[1].alive=false;break;
 case 7:m.objects[0].mission="Guard";break;
 case 8:m.actions[0].objectId=-1;break;
 case 9:m.actions[0].targetObjectId=-1;break;
 case 10:m.actions[0].projectileId=0;break;
 case 11:m.actions[0].tick=-1;break;
 case 12:m.actions[0].owner.clear();break;
 case 13:m.actions[0].owner="RED";break;
 case 14:m.actions[0].owner="local";break;
 case 15:m.actions[0].replacesTerminatingHost=true;break;
 case 16:m.actions[0].sourceWasAttachedToHost=true;break;
 case 17:m.actions[0].damage=-1;break;
 case 18:m.actions[0].stunTicks=0;break;
 case 19:m.objects[1].kind=WorldObjectKind::Infantry;break;
 case 20:m.actions[0].targetObjectId=11;break;
 case 21:m.pending.insert(11);break;
 case 22:m.pending.insert(21);break;
 case 23:m.exclusive.insert({11,false});break;
 case 24:m.exclusive.insert({21,true});break;
 case 25:m.objects[0].owner="local";break;
 case 26:m.choices.clear();break;
 case 27:m.choices.resize(1);break;
 case 28:m.actions[0].cell={-1,-1};break;
 case 29:m.objects[1].kind=WorldObjectKind::Structure;m.actions[0].replacesTerminatingHost=true;break;
 case 30:m.actions[0].warheadId="all value fields";m.actions[0].reason=std::string(80,'r');m.actions[0].paralyzes=99;m.actions[0].culling=false;m.actions[0].forceDestruction=false;break;
 case 31:std::swap(m.objects[0],m.objects[4]);break;
 default:break;
 }
}
void verifyActions(){
 const auto originalValue=action(Kind::ParasiteEject);const auto before=encode(originalValue);

 {auto value=originalValue;mutateEncoderField(value.kind);require(encode(value)!=before,"action kind omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.tick);require(encode(value)!=before,"action tick omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.projectileId);require(encode(value)!=before,"action projectileId omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.objectId);require(encode(value)!=before,"action objectId omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.targetObjectId);require(encode(value)!=before,"action targetObjectId omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.owner);require(encode(value)!=before,"action owner omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.damage);require(encode(value)!=before,"action damage omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.warheadId);require(encode(value)!=before,"action warheadId omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.forceDestruction);require(encode(value)!=before,"action forceDestruction omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.paralyzes);require(encode(value)!=before,"action paralyzes omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.culling);require(encode(value)!=before,"action culling omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.suppressDeathWeapon);require(encode(value)!=before,"action suppressDeathWeapon omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.stunTicks);require(encode(value)!=before,"action stunTicks omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.cell);require(encode(value)!=before,"action cell omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.reason);require(encode(value)!=before,"action reason omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.replacesTerminatingHost);require(encode(value)!=before,"action replacesTerminatingHost omitted");++actionProbes;}
 {auto value=originalValue;mutateEncoderField(value.sourceWasAttachedToHost);require(encode(value)!=before,"action sourceWasAttachedToHost omitted");++actionProbes;}

 require(actionProbes==17,"all17 action fields probed");
}
void anchors(){
 Model timer=base(Kind::TimedBombDetonate);timer.actions[0].tick=-1;timer.actions[0].projectileId=0;timer.actions[0].objectId=-1;timer.actions[0].targetObjectId=-1;timer.actions[0].replacesTerminatingHost=true;timer.actions[0].sourceWasAttachedToHost=true;
 auto t=compare(timer,false);require(t.result.accepted&&t.calls==0,"TimedBomb skip before checks");auto p=compare(timer,true);require(encode(p.result.actions)==encode(timer.actions)&&p.calls==0,"non-placement action copy exact");
 Model fail=base(Kind::ParasiteEject);fail.choices.clear();fail.actions[0].replacesTerminatingHost=true;auto f=compare(fail,true);const auto& dead=f.result.actions.at(0);
 require(dead.kind==Kind::ParasiteKillParasite&&dead.suppressDeathWeapon&&!dead.replacesTerminatingHost&&dead.stunTicks==0&&dead.reason=="original reason; no legal host placement"&&dead.cell.x==4&&dead.cell.y==5,"failed placement original rewrite");
 Model pair=base(Kind::ParasiteReturnFromLimbo);pair.actions.push_back(action(Kind::ParasiteEject,12));auto q=compare(pair,true);require(q.result.actions[0].cell.x==8&&q.result.actions[1].cell.x==9&&q.result.actions[0].cell.y==9&&q.result.actions[1].cell.y==9,"reserved placement uses original1000 keys");
 Model duplicate=base(Kind::ParasiteReturnFromLimbo);duplicate.actions.push_back(action(Kind::ParasiteEject));require(!compare(duplicate,false).result.accepted,"returning source duplicate rejects");
 Model kill=base(Kind::ParasiteKillTarget);kill.actions.push_back(action(Kind::ParasiteKillTarget));require(!compare(kill,false).result.accepted,"terminal duplicate rejects");
 Model alive=base(Kind::ParasiteEnterLimbo);alive.objects[0].alive=true;require(compare(alive,false).result.accepted,"positive enter");
 Model attach=base(Kind::ParasiteAttachHost);require(compare(attach,false).result.accepted,"positive attach");
 Model host=base(Kind::ParasiteReturnFromLimbo);host.actions[0].replacesTerminatingHost=true;host.actions[0].stunTicks=0;host.objects[1].kind=WorldObjectKind::Infantry;require(compare(host,false).result.accepted,"positive terminating infantry replacement");
 Model alias=base(Kind::ParasiteDamage);alias.actions[0].owner="local";require(compare(alias,false).result.accepted,"canonical owner alias retained");
}
void capacity(){
 Model m;m.objects.clear();m.actions.clear();m.largePlacement=true;
 for(int i=1;i<=4096;++i){WorldObjectInstance source;source.instanceId=i;source.kind=WorldObjectKind::Unit;source.health=100;source.alive=false;source.owner="BLUE";source.mission="ParasiteLimbo";m.objects.push_back(source);m.actions.push_back(action(Kind::ParasiteReturnFromLimbo,i,9000));}
 WorldObjectInstance target;target.instanceId=9000;target.kind=WorldObjectKind::Unit;target.health=100;target.alive=true;target.owner="BLUE";m.objects.push_back(target);
 const auto out=compare(m,true);require(out.result.actions.size()==4096,"all4096 returned actions");std::set<int> cells;
 for(const auto& a:out.result.actions){require(a.kind==Kind::ParasiteReturnFromLimbo,"4096 placement unchanged kind");cells.insert(a.cell.y*1000+a.cell.x);++capacityChecks;}
 require(cells.size()==4096&&compare(m,false).result.accepted,"4096 unique reservations and returning sources");
}
bool runContract(){
 try{
 checks=states=worldProbes=actionProbes=faults=mutations=nestedRuns=capacityChecks=0;faultPorts.clear();verifyWorld();verifyActions();anchors();
 const std::array<Kind,9> kinds{Kind::TimedBombDetonate,Kind::ParasiteEnterLimbo,Kind::ParasiteAttachHost,Kind::ParasiteReturnFromLimbo,Kind::ParasiteKillTarget,Kind::ParasiteDamage,Kind::ParasiteEject,Kind::ParasiteKillParasite,Kind::ParasiteReleaseTarget};
 for(Kind kind:kinds)for(int v=0;v<32;++v){Model m=base(kind);variant(m,v);exercise(m,true);}
 for(Kind a:kinds)for(Kind b:kinds)for(int alive=0;alive<2;++alive)for(int positions=0;positions<3;++positions){Model m=base(a);m.objects[0].alive=alive!=0;m.actions.push_back(action(b,positions==0?11:12));if(positions==1)m.choices.resize(1);if(positions==2)m.honorReservations=false;exercise(m,false);}
 for(Kind kind:kinds){Model seed=base(kind);if(kind==Kind::ParasiteEnterLimbo)seed.objects[0].alive=true;
  for(bool prepare:{false,true}){auto baseline=compare(seed,prepare);
   for(int at=1;at<=baseline.calls;++at){for(int mode=1;mode<=7;++mode){Model m=seed;m.mutationAt=at;m.mutationKind=mode;compare(m,prepare);}Model nested=seed;nested.nestAt=at;compare(nested,prepare);}
  }
 }
 capacity();require(worldProbes==87&&actionProbes==17&&faultPorts==std::set<std::string>{"cell-key","exclusive","object","owner","placement"}&&mutations>0&&nestedRuns>0&&capacityChecks==4096,"all required query/field/capacity coverage");
 std::cout<<"parasite-action checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" action-probes="<<actionProbes<<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" live-mutations="<<mutations<<" nested="<<nestedRuns<<" capacity-checks="<<capacityChecks<<" complete-original-two-functions-actions-order-query-alias-and-failure=exact failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"parasite-action model failure: "<<e.what()<<"\n";return false;}
}
}
