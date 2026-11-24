#include "simulation/abilities/parasites/RuntimeParasiteHostDeathPolicy.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
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
namespace parasite_host_death_model_detail {
namespace current=concord::simulation::abilities;
using Kind=SpecialMechanismActionKind;
int checks=0,states=0,worldProbes=0,actionProbes=0,linkProbes=0,requestProbes=0,resultProbes=0,faults=0,mutations=0,nestedRuns=0,capacityChecks=0;
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


SpecialMechanismAction action(Kind kind,int source=11,int target=21){SpecialMechanismAction a;a.kind=kind;a.tick=7;a.projectileId=31;a.objectId=source;a.targetObjectId=target;a.owner="BLUE";a.damage=3;a.warheadId="warhead/full";a.forceDestruction=true;a.paralyzes=2;a.culling=true;a.stunTicks=7;a.cell={4,5};a.reason="original reason";return a;}
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

struct ApplyResult {bool accepted=true;bool changed=false;};
struct Fault {int position;bool after;std::string port;};
struct Outcome {std::string failure;bool accepted=true;std::string state;std::vector<std::string> trace;};
struct Model;
Model base(Kind kind);
Outcome execute(Model& m,bool reference,int depth=0);
struct Model final:current::RuntimeParasiteHostDeathQueries,current::RuntimeParasiteHostDeathCandidate,current::RuntimeParasiteHostDeathRejection {
 std::vector<WorldObjectInstance> objects;
 std::map<int,ParasiteLinkState> candidateLinks;
 std::map<int,ParasitePendingLaunchState> candidatePending;
 std::map<int,ParasiteResolvedLaunchState> candidateResolved;
 std::map<int,std::string> otherAbilityState;
 std::vector<SpecialMechanismAction> actions,deathActions;
 std::vector<ParasiteHostEventRequest> requests;
 ParasiteOperationResult response;
 ApplyResult result;
 int tick=7,applied=100,sourceOverride=-2;
 bool immune=false,eraseLink=true,eraseOther=false,addNext=false,hasHostDeath=false;
 RuntimeCellPoint cell{4,5};std::optional<RuntimeCellPoint> placement=RuntimeCellPoint{7,8};
 std::string status="before-status";
 int calls=0,faultAt=-1,mutateAt=-1,mutationMode=0,nestedAt=-1;
 bool faultAfter=false,lightTrace=false,referenceMode=false;
 std::function<void()> nested;
 std::string nestedDigest;
 std::vector<std::string> trace,ports;
 std::string snapshot() const {
  return encode(objects)+encode(candidateLinks)+encode(candidatePending)+encode(candidateResolved)+encode(otherAbilityState)
   +encode(actions)+encode(deathActions)+encode(requests)+encode(response)+encode(result.accepted)+encode(result.changed)
   +encode(tick)+encode(applied)+encode(sourceOverride)+encode(immune)+encode(eraseLink)+encode(eraseOther)+encode(addNext)
   +encode(hasHostDeath)+encode(cell)+encode(placement.has_value())+(placement?encode(*placement):std::string{})
   +encode(status)+encode(calls)+encode(nestedDigest);
 }
 std::string observation() const {
  if(!lightTrace)return snapshot();
  return encode(candidateLinks.size())+encode(deathActions.size())+encode(requests.size())+encode(tick)
   +encode(hasHostDeath)+encode(result.accepted)+encode(status);
 }
 void mutate() {
  ++mutations;
  if(mutationMode==0){for(auto& o:objects)if(o.instanceId==21)o.health=0;}
  else if(mutationMode==1){tick+=101;}
  else if(mutationMode==2){for(auto& o:objects)if(o.instanceId==21){o.x=77;o.y=88;o.instanceId=22;}}
  else if(mutationMode==3){for(auto& item:candidateLinks)item.second.sourceObjectId=12;}
  else if(mutationMode==4){placement.reset();}
  else if(mutationMode==5){response.accepted=false;response.idempotent=true;}
  else if(mutationMode==6){for(auto& o:objects)o.owner+="/live";response.reason+=std::string(60,'x');}
  else if(mutationMode==7){for(auto& o:objects)if(o.instanceId==11)o.alive=!o.alive;}
  else if(mutationMode==8){applied=-1;cell={-7,123};}
 }
 struct Probe {
  Model& m;int at;std::string port;int exceptions;
  Probe(Model& owner,std::string name,std::string payload):m(owner),at(++m.calls),port(std::move(name)),exceptions(std::uncaught_exceptions()) {
   m.ports.push_back(port);m.trace.push_back("enter|"+encode(at)+port+"|"+payload+"|"+m.observation());
   if(m.faultAt==at&&!m.faultAfter)throw Fault{at,false,port};
   if(m.mutateAt==at)m.mutate();
   if(m.nestedAt==at&&m.nested)m.nested();
  }
  ~Probe() noexcept(false) {
   if(std::uncaught_exceptions()!=exceptions)return;
   m.trace.push_back("leave|"+encode(at)+port+"|"+m.observation());
   if(m.faultAt==at&&m.faultAfter)throw Fault{at,true,port};
  }
 };
 const WorldObjectInstance* find(int id) const {for(const auto& o:objects)if(o.instanceId==id)return &o;return nullptr;}
 const WorldObjectInstance* rawObject(int id){Probe p(*this,"object",encode(id));return find(id);}
 const WorldObjectInstance* rawImmuneSource(int id){Probe p(*this,"immune-source",encode(id));return sourceOverride==-2?find(id):(sourceOverride<0?nullptr:find(sourceOverride));}
 bool rawImmune(const WorldObjectInstance& target,const WorldObjectInstance* source,int damage){Probe p(*this,"immune",encode(target.instanceId)+encode(source?source->instanceId:-1)+encode(damage));return immune;}
 int rawAuthored(const WorldObjectInstance& target,int damage,const std::string& warhead,double distance,double modifier){Probe p(*this,"authored",encode(target.instanceId)+encode(damage)+encode(warhead)+encode(distance)+encode(modifier));return applied;}
 RuntimeCellPoint rawCell(const WorldObjectInstance& object){Probe p(*this,"cell",encode(object.instanceId));return cell;}
 std::optional<RuntimeCellPoint> rawPlacement(const WorldObjectInstance& source,const SpecialCell& requested,const std::unordered_set<int>* reserved,int host){
  Probe p(*this,"placement",encode(source.instanceId)+encode(requested)+encode(reserved==nullptr)+encode(host));require(reserved==nullptr,"preflight must keep original null reserved set");return placement;
 }
 const std::map<int,ParasiteLinkState>& rawLinks(){Probe p(*this,"links","");return candidateLinks;}
 ParasiteOperationResult rawHandle(const ParasiteHostEventRequest& request){
  Probe p(*this,"handle",encode(request));requests.push_back(request);
  int sourceId=-1;const auto it=candidateLinks.find(request.targetObjectId);if(it!=candidateLinks.end())sourceId=it->second.sourceObjectId;
  if(eraseLink)candidateLinks.erase(request.targetObjectId);
  if(eraseOther)candidateLinks.erase(22);
  if(addNext){ParasiteLinkState l;l.targetObjectId=22;l.sourceObjectId=12;candidateLinks[22]=l;}
  if(!candidatePending.empty())candidatePending.begin()->second.damage+=1;
  if(!candidateResolved.empty())candidateResolved.begin()->second.hostTerminated=!candidateResolved.begin()->second.hostTerminated;
  otherAbilityState[77]+="/handled";
  auto value=response;
  for(auto& actionValue:value.actions){actionValue.objectId=sourceId;actionValue.targetObjectId=request.targetObjectId;actionValue.tick=request.currentTick;actionValue.cell=request.ejectCell;}
  return value;
 }
 void rawStatus(const char* text){Probe p(*this,"reject",encode(std::string(text)));status=text;}
 const WorldObjectInstance* objectIncludingInactive(int id)const override{return const_cast<Model*>(this)->rawObject(id);}
 bool damageIsTypeImmune(const WorldObjectInstance& target,int sourceId,int damage)const override{auto& m=*const_cast<Model*>(this);return m.rawImmune(target,m.rawImmuneSource(sourceId),damage);}
 int authoredDamage(const WorldObjectInstance& target,int damage,const std::string& warhead,double distance,double modifier)const override{return const_cast<Model*>(this)->rawAuthored(target,damage,warhead,distance,modifier);}
 RuntimeCellPoint objectCell(const WorldObjectInstance& object)const override{return const_cast<Model*>(this)->rawCell(object);}
 std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance& source,const SpecialCell& requested,const std::unordered_set<int>* reserved,int host)const override{return const_cast<Model*>(this)->rawPlacement(source,requested,reserved,host);}
 const std::map<int,ParasiteLinkState>& links()const override{return const_cast<Model*>(this)->rawLinks();}
 ParasiteOperationResult handle(const ParasiteHostEventRequest& request)override{return rawHandle(request);}
 void rejectAtomically()override{result.accepted=false;rawStatus("parasite-caused host death rejected atomically");}
};
namespace original {
using RuntimeParasiteActionApplyResult=ApplyResult;
using SpecialEventSink=std::function<void(const SpecialMechanismEvent&)>;
struct Status {Model& model;Status& operator=(const char* text){model.rawStatus(text);return *this;}};
struct RuntimeGameSessionState {Model& model;int& tick;Status simulationStatus;};
struct Candidate {
 Model& model;
 const std::map<int,ParasiteLinkState>& parasiteLinks()const{return model.rawLinks();}
 ParasiteOperationResult handleParasiteHostEvent(const ParasiteHostEventRequest& request,const SpecialEventSink& sink){require(!sink,"original empty event sink retained");return model.rawHandle(request);}
};
thread_local Model* active=nullptr;
struct Scope {Model* previous;explicit Scope(Model& value):previous(active){active=&value;}~Scope(){active=previous;}};
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(const RuntimeGameSessionState& state,int id){return state.model.rawObject(id);}
const WorldObjectInstance* runtimeTypeImmuneSource(const RuntimeGameSessionState& state,int id){return state.model.rawImmuneSource(id);}
bool runtimeDamageIsTypeImmune(const RuntimeGameSessionState& state,const WorldObjectInstance& target,const WorldObjectInstance* source,int damage){return state.model.rawImmune(target,source,damage);}
int runtimeAuthoredDamageAgainst(const RuntimeGameSessionState& state,const WorldObjectInstance& target,int damage,const std::string& warhead,double distance,double modifier){return state.model.rawAuthored(target,damage,warhead,distance,modifier);}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& object){require(active!=nullptr,"original scoped stateless cell query");return active->rawCell(object);}
std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(const RuntimeGameSessionState& state,const WorldObjectInstance& source,const SpecialCell& requested,const std::unordered_set<int>* reserved,int host){return state.model.rawPlacement(source,requested,reserved,host);}
RuntimeParasiteActionApplyResult collect(RuntimeGameSessionState& state,const std::vector<SpecialMechanismAction>& prepared,Candidate& parasiteDeathCandidate,std::vector<SpecialMechanismAction>& parasiteDeathActions,bool& hasParasiteCausedHostDeath,RuntimeParasiteActionApplyResult& result) {
    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind != SpecialMechanismActionKind::ParasiteDamage
            && action.kind
                != SpecialMechanismActionKind::ParasiteKillTarget) {
            continue;
        }
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        if (target == nullptr || !target->alive || target->health <= 0) {
            continue;
        }
        if (action.kind == SpecialMechanismActionKind::ParasiteDamage
            && runtimeDamageIsTypeImmune(state, *target,
                runtimeTypeImmuneSource(state, action.objectId), action.damage)) continue;
        bool lethal = action.kind
            == SpecialMechanismActionKind::ParasiteKillTarget;
        if (!lethal) {
            const int appliedDamage = runtimeAuthoredDamageAgainst(
                state,
                *target,
                action.damage,
                action.warheadId,
                0.0,
                1.0
            );
            lethal = appliedDamage >= target->health;
        }
        const auto link = parasiteDeathCandidate.parasiteLinks().find(
            action.targetObjectId
        );
        if (!lethal
            || link == parasiteDeathCandidate.parasiteLinks().end()) {
            continue;
        }
        const WorldObjectInstance* source =
            findRuntimeObjectByIdIncludingInactive(
                state,
                link->second.sourceObjectId
            );
        const RuntimeCellPoint targetCell =
            runtimeCellPointForObject(*target);
        const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
            ? std::nullopt
            : runtimeParasitePlacementCell(
                state,
                *source,
                {targetCell.x, targetCell.y},
                nullptr,
                target->instanceId
            );
        ParasiteHostEventRequest hostDeath;
        hostDeath.currentTick = state.tick;
        hostDeath.targetObjectId = action.targetObjectId;
        hostDeath.event = ParasiteHostEventKind::DestroyedByParasite;
        hostDeath.ejectCellAvailable = ejectCell.has_value();
        if (ejectCell.has_value()) {
            hostDeath.ejectCell = {ejectCell->x, ejectCell->y};
        }
        const ParasiteOperationResult handled =
            parasiteDeathCandidate.handleParasiteHostEvent(
                hostDeath,
                SpecialEventSink{}
            );
        if (!handled.accepted) {
            result.accepted = false;
            state.simulationStatus =
                "parasite-caused host death rejected atomically";
            return result;
        }
        parasiteDeathActions.insert(
            parasiteDeathActions.end(),
            handled.actions.begin(),
            handled.actions.end()
        );
        hasParasiteCausedHostDeath = true;
    }

    return result;
}
}
Model base(Kind kind){
 Model m;WorldObjectInstance source;source.instanceId=11;source.kind=WorldObjectKind::Unit;source.health=40;source.alive=false;source.owner="BLUE";source.type="DRON";source.mission="ParasiteLimbo";
 auto target=source;target.instanceId=21;target.alive=true;target.health=100;target.type="HTNK";target.mission="Guard";target.x=4;target.y=5;
 auto source2=source;source2.instanceId=12;auto target2=target;target2.instanceId=22;target2.kind=WorldObjectKind::Infantry;target2.type="E1";
 m.objects={source,target,source2,target2};ParasiteLinkState l;l.projectileId=31;l.sourceObjectId=11;l.targetObjectId=21;l.owner="BLUE";l.sourceTypeId="DRON";l.targetTypeId="HTNK";l.warheadId="WH";l.damage=5;l.paralyzes=7;l.culling=true;m.candidateLinks[21]=l;
 auto other=l;other.sourceObjectId=12;other.targetObjectId=22;m.candidateLinks[22]=other;
 ParasitePendingLaunchState pending;pending.projectileId=99;pending.sourceObjectId=88;pending.damage=11;m.candidatePending[99]=pending;
 ParasiteResolvedLaunchState resolved;resolved.projectileId=97;resolved.sourceObjectId=86;m.candidateResolved[97]=resolved;m.otherAbilityState={{77,"full-other-ability-sentinel"},{99,std::string(80,'a')}};
 m.actions={action(kind)};m.response.accepted=true;m.response.reason=std::string(80,'r');m.response.actions={action(Kind::ParasiteEject),action(Kind::ParasiteReleaseTarget)};return m;
}
Outcome execute(Model& m,bool reference,int depth){
 m.referenceMode=reference;m.calls=0;m.trace.clear();m.ports.clear();
 m.nested=depth==0?std::function<void()>([&m,reference,depth]{++nestedRuns;Model child=base(Kind::ParasiteKillTarget);child.actions.push_back(action(Kind::ParasiteDamage,12,22));auto result=execute(child,reference,depth+1);m.nestedDigest=result.failure+encode(result.accepted)+result.state+encode(result.trace);m.tick+=3;}):std::function<void()>{};
 const std::vector<SpecialMechanismAction> prepared=m.actions;
 original::Scope scope(m);Outcome outcome;
 try {
  if(reference){original::RuntimeGameSessionState state{m,m.tick,{m}};original::Candidate candidate{m};outcome.accepted=original::collect(state,prepared,candidate,m.deathActions,m.hasHostDeath,m.result).accepted;}
  else {outcome.accepted=current::collectRuntimeParasiteHostDeathActions(prepared,m.tick,m,m,m.deathActions,m.hasHostDeath,m);require(outcome.accepted==m.result.accepted,"rejection result state and boolean agree");}
 }catch(const Fault& f){outcome.failure=encode(f.position)+encode(f.after)+f.port;outcome.accepted=m.result.accepted;}
 m.nested={};outcome.state=m.snapshot();outcome.trace=m.trace;return outcome;
}
Outcome compare(const Model& seed){
 Model a=seed,b=seed;auto old=execute(a,true),now=execute(b,false);
 require(old.failure==now.failure,"exception boundary differs");require(old.accepted==now.accepted,"return/rejection differs");
 require(old.state==now.state,"complete World/candidate/output/tick/status state differs");require(old.trace==now.trace,"full ordered query/state trace differs");++states;return now;
}
void exercise(const Model& seed,bool inject){
 compare(seed);Model observed=seed;const auto out=execute(observed,true);require(out.failure.empty(),"baseline reference unexpectedly failed");
 if(!inject)return;
 for(int at=1;at<=observed.calls;++at)for(bool after:{false,true}){
  Model trial=seed;trial.faultAt=at;trial.faultAfter=after;const auto result=compare(trial);require(!result.failure.empty(),"injected query/effect fault missing");faultPorts.insert(observed.ports[static_cast<std::size_t>(at-1)]);++faults;
 }
}
void variant(Model& m,int v){
 switch(v){
 case 0:break;
 case 1:m.actions[0].targetObjectId=999;break;
 case 2:m.objects[1].alive=false;break;
 case 3:m.objects[1].health=0;break;
 case 4:m.objects[1].health=-7;break;
 case 5:m.immune=true;break;
 case 6:m.applied=99;break;
 case 7:m.applied=100;break;
 case 8:m.applied=101;break;
 case 9:m.applied=-5;break;
 case 10:m.candidateLinks[21].sourceObjectId=999;break;
 case 11:m.candidateLinks.erase(21);break;
 case 12:m.placement.reset();break;
 case 13:m.tick=-9;break;
 case 14:m.actions[0].tick=999;break;
 case 15:m.eraseLink=false;break;
 case 16:m.response.accepted=false;break;
 case 17:m.response.actions.clear();break;
 case 18:m.response.idempotent=true;break;
 case 19:m.deathActions={action(Kind::ParasiteKillParasite,88,99)};m.hasHostDeath=true;break;
 case 20:m.candidateLinks[21].sourceObjectId=21;break;
 case 21:{auto duplicate=m.objects[1];duplicate.alive=false;duplicate.health=1;m.objects.insert(m.objects.begin(),duplicate);break;}
 case 22:{auto duplicate=m.objects[0];duplicate.owner="duplicate-first";duplicate.health=0;m.objects.insert(m.objects.begin(),duplicate);break;}
 case 23:m.candidateLinks[21].sourceObjectId=-1;break;
 case 24:m.actions[0].targetObjectId=-1;break;
 case 25:m.mutateAt=4;m.mutationMode=0;break;
 case 26:m.cell={-7,123};break;
 case 27:m.placement=RuntimeCellPoint{-3,123456};break;
 case 28:m.mutateAt=7;m.mutationMode=1;break;
 case 29:m.actions.push_back(action(Kind::ParasiteDamage,12,22));break;
 case 30:std::swap(m.objects[0],m.objects[3]);break;
 case 31:m.sourceOverride=-1;break;
 case 32:m.sourceOverride=22;break;
 case 33:m.deathActions={action(Kind::ParasiteEject,77,88)};break;
 case 34:m.eraseOther=true;m.actions.push_back(action(Kind::ParasiteKillTarget,12,22));break;
 case 35:m.candidateLinks.erase(22);m.addNext=true;m.actions.push_back(action(Kind::ParasiteKillTarget,12,22));break;
 case 36:m.result.changed=true;break;
 case 37:m.actions[0].warheadId.clear();m.actions[0].owner="owner/full";m.actions[0].reason=std::string(90,'z');m.actions[0].damage=-999;m.actions[0].forceDestruction=false;break;
 case 38:m.response.accepted=false;m.response.idempotent=true;m.response.actions.clear();break;
 case 39:m.actions.push_back(action(Kind::ParasiteDamage,12,21));m.eraseLink=false;break;
 default:break;
 }
}
void verifyExtraValues(){
 ParasiteLinkState link;const auto linkBefore=encode(link);
{auto v=link;mutateEncoderField(v.projectileId);require(encode(v)!=linkBefore,"link projectileId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.targetObjectId);require(encode(v)!=linkBefore,"link targetObjectId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.sourceObjectId);require(encode(v)!=linkBefore,"link sourceObjectId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.owner);require(encode(v)!=linkBefore,"link owner omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.sourceTypeId);require(encode(v)!=linkBefore,"link sourceTypeId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.targetTypeId);require(encode(v)!=linkBefore,"link targetTypeId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.weaponId);require(encode(v)!=linkBefore,"link weaponId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.warheadId);require(encode(v)!=linkBefore,"link warheadId omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.damage);require(encode(v)!=linkBefore,"link damage omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.attachedTick);require(encode(v)!=linkBefore,"link attachedTick omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.nextDamageTick);require(encode(v)!=linkBefore,"link nextDamageTick omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.damageIntervalTicks);require(encode(v)!=linkBefore,"link damageIntervalTicks omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.organic);require(encode(v)!=linkBefore,"link organic omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.sourceElite);require(encode(v)!=linkBefore,"link sourceElite omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.paralyzes);require(encode(v)!=linkBefore,"link paralyzes omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.culling);require(encode(v)!=linkBefore,"link culling omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.cullingThresholdPerMyriad);require(encode(v)!=linkBefore,"link cullingThresholdPerMyriad omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.targetMaximumHealth);require(encode(v)!=linkBefore,"link targetMaximumHealth omitted");++linkProbes;}
{auto v=link;mutateEncoderField(v.sourceExpectedInLimbo);require(encode(v)!=linkBefore,"link sourceExpectedInLimbo omitted");++linkProbes;}
ParasiteHostEventRequest request;const auto requestBefore=encode(request);
{auto v=request;mutateEncoderField(v.currentTick);require(encode(v)!=requestBefore,"request currentTick omitted");++requestProbes;}
{auto v=request;mutateEncoderField(v.targetObjectId);require(encode(v)!=requestBefore,"request targetObjectId omitted");++requestProbes;}
{auto v=request;mutateEncoderField(v.event);require(encode(v)!=requestBefore,"request event omitted");++requestProbes;}
{auto v=request;mutateEncoderField(v.ejectCellAvailable);require(encode(v)!=requestBefore,"request ejectCellAvailable omitted");++requestProbes;}
{auto v=request;mutateEncoderField(v.ejectCell);require(encode(v)!=requestBefore,"request ejectCell omitted");++requestProbes;}
{auto v=request;mutateEncoderField(v.stunTicks);require(encode(v)!=requestBefore,"request stunTicks omitted");++requestProbes;}
ParasiteOperationResult result;const auto resultBefore=encode(result);
{auto v=result;mutateEncoderField(v.accepted);require(encode(v)!=resultBefore,"result accepted omitted");++resultProbes;}
{auto v=result;mutateEncoderField(v.idempotent);require(encode(v)!=resultBefore,"result idempotent omitted");++resultProbes;}
{auto v=result;mutateEncoderField(v.reason);require(encode(v)!=resultBefore,"result reason omitted");++resultProbes;}
{auto v=result;mutateEncoderField(v.actions);require(encode(v)!=resultBefore,"result actions omitted");++resultProbes;}

}
void anchors(){
 {auto m=base(Kind::TimedBombDetonate);const auto r=compare(m);require(r.accepted&&r.failure.empty(),"timedbomb scope");Model observed=m;execute(observed,false);require(observed.calls==0&&!observed.hasHostDeath,"timedbomb skipped before any query");}
 {auto m=base(Kind::ParasiteDamage);m.applied=99;Model n=m;const auto r=execute(n,false);require(r.accepted&&!n.hasHostDeath&&n.ports==std::vector<std::string>{"object","immune-source","immune","authored","links"},"nonlethal still performs original first candidate lookup");compare(m);}
 {auto m=base(Kind::ParasiteKillTarget);m.immune=true;m.candidateLinks[21].sourceObjectId=999;Model n=m;const auto r=execute(n,false);require(r.accepted&&n.requests.size()==1&&!n.requests[0].ejectCellAvailable&&n.requests[0].stunTicks==0,"missing source keeps cell query and default request");require(std::find(n.ports.begin(),n.ports.end(),"cell")!=n.ports.end()&&std::find(n.ports.begin(),n.ports.end(),"placement")==n.ports.end()&&std::find(n.ports.begin(),n.ports.end(),"immune")==n.ports.end(),"kill skips immunity and missing source skips only placement");compare(m);}
 {auto m=base(Kind::ParasiteDamage);m.response.actions.clear();Model n=m;execute(n,false);require(n.hasHostDeath&&n.deathActions.empty(),"accepted empty actions still marks host death");compare(m);}
 {auto m=base(Kind::ParasiteDamage);m.mutateAt=9;m.mutationMode=1;Model n=m;execute(n,false);require(n.requests.size()==1&&n.requests[0].currentTick==108,"request borrows live tick after placement");compare(m);}
 {auto m=base(Kind::ParasiteDamage);m.response.accepted=false;Model n=m;execute(n,false);require(!n.result.accepted&&n.status=="parasite-caused host death rejected atomically"&&!n.hasHostDeath,"original rejection effect");compare(m);}
}
void capacity(){
 Model m=base(Kind::ParasiteKillTarget);m.objects.clear();m.actions.clear();m.candidateLinks.clear();m.lightTrace=true;
 for(int i=0;i<4096;++i){WorldObjectInstance source;source.instanceId=i+1;source.kind=WorldObjectKind::Unit;source.health=1;source.alive=false;source.type="DRON";auto target=source;target.instanceId=i+10001;target.health=100;target.alive=true;target.type="HOST";m.objects.push_back(source);m.objects.push_back(target);ParasiteLinkState link;link.sourceObjectId=source.instanceId;link.targetObjectId=target.instanceId;m.candidateLinks[target.instanceId]=link;m.actions.push_back(action(Kind::ParasiteKillTarget,source.instanceId,target.instanceId));}
 compare(m);Model current=m;execute(current,false);require(current.candidateLinks.empty()&&current.requests.size()==4096&&current.deathActions.size()==8192&&current.hasHostDeath,"full4096 host preflight capacity/order");
 for(int i=0;i<4096;++i){const auto& e=current.deathActions[static_cast<std::size_t>(i)*2];const auto& r=current.deathActions[static_cast<std::size_t>(i)*2+1];require(e.kind==Kind::ParasiteEject&&r.kind==Kind::ParasiteReleaseTarget&&e.objectId==i+1&&e.targetObjectId==i+10001&&r.targetObjectId==i+10001,"complete4096 ordered emitted action pairs");++capacityChecks;}
}
bool runContract(){try{
 checks=states=worldProbes=actionProbes=linkProbes=requestProbes=resultProbes=faults=mutations=nestedRuns=capacityChecks=0;faultPorts.clear();verifyWorld();verifyActions();verifyExtraValues();anchors();
 const std::array<Kind,9> kinds{Kind::TimedBombDetonate,Kind::ParasiteEnterLimbo,Kind::ParasiteAttachHost,Kind::ParasiteReturnFromLimbo,Kind::ParasiteKillTarget,Kind::ParasiteDamage,Kind::ParasiteEject,Kind::ParasiteKillParasite,Kind::ParasiteReleaseTarget};
 for(Kind kind:kinds)for(int v=0;v<40;++v){auto m=base(kind);variant(m,v);exercise(m,true);}
 for(Kind a:kinds)for(Kind b:kinds)for(int mode=0;mode<4;++mode){auto m=base(a);m.actions.push_back(action(b,12,mode==0?21:22));if(mode==2)m.response.accepted=false;if(mode==3){m.eraseLink=false;m.immune=true;}exercise(m,false);}
 for(Kind kind:{Kind::ParasiteDamage,Kind::ParasiteKillTarget}){Model baseline=base(kind);Model observed=baseline;execute(observed,true);for(int at=1;at<=observed.calls;++at)for(int mode=0;mode<9;++mode){auto m=baseline;m.mutateAt=at;m.mutationMode=mode;compare(m);}for(int at=1;at<=observed.calls;++at){auto m=baseline;m.nestedAt=at;compare(m);}}
 capacity();require(worldProbes==87&&actionProbes==17&&linkProbes==19&&requestProbes==6&&resultProbes==4,"all complete encoder probes");require(faultPorts==std::set<std::string>{"object","immune-source","immune","authored","links","cell","placement","handle","reject"},"all original query/candidate/rejection fault boundaries");require(mutations>0&&nestedRuns>0&&capacityChecks==4096,"live mutation/nesting/fullcapacity");
 std::cout<<"parasite-host-death checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" action-probes="<<actionProbes<<" link-probes="<<linkProbes<<" request-probes="<<requestProbes<<" result-probes="<<resultProbes<<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" live-mutations="<<mutations<<" nested="<<nestedRuns<<" capacity-hosts="<<capacityChecks<<" complete-original79line-preflight-state-order-partial-failure-and-live-tick=exact failures=0"<<std::endl;return true;
 }catch(const std::exception& e){std::cerr<<"parasite-host-death failure: "<<e.what()<<std::endl;return false;}}
}
