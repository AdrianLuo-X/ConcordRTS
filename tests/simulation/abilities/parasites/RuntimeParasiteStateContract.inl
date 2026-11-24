#include "simulation/abilities/parasites/RuntimeParasiteState.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace parasite_state_model_detail {
int checks=0,states=0,worldProbes=0,valueProbes=0,faults=0,mutations=0,nestedRuns=0,capacityChecks=0;
std::set<std::string> faultPorts;
void require(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
std::function<void(const std::string&)> activeCheckpoint;
void checkpoint(const std::string& s){if(activeCheckpoint)activeCheckpoint(s);}
using namespace runtime_parasite_values_detail;
SpecialCell objectCell(const WorldObjectInstance* object){return object==nullptr?SpecialCell{}:SpecialCell{object->x,object->y};}
std::string parasiteHostEventName(ParasiteHostEventKind event) {
    switch (event) {
    case ParasiteHostEventKind::Healed:
        return "healed";
    case ParasiteHostEventKind::SonicHit:
        return "sonic hit";
    case ParasiteHostEventKind::Teleporting:
        return "teleporting";
    case ParasiteHostEventKind::DestroyedExternally:
        return "destroyed externally";
    case ParasiteHostEventKind::DestroyedByParasite:
        return "destroyed by parasite";
    }
    return "unknown host event";
}

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


void verifyValueEncoders(){
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.projectileId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.currentTick);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.currentTick omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.sourceObjectId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.sourceObjectId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetObjectId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.sourceOwner);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.sourceOwner omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.sourceTypeId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.sourceTypeId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetTypeId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetTypeId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.weaponId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.weaponId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.warheadId);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.warheadId omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetKind);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetKind omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetCell);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetCell omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.damage);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.damage omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.damageIntervalTicks);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.damageIntervalTicks omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.paralyzes);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.paralyzes omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetMaximumHealth);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetMaximumHealth omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.targetParasiteable);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.targetParasiteable omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.sourceOrganic);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.sourceOrganic omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.sourceElite);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.sourceElite omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.culling);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.culling omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.normalCullingThresholdPermyriad);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.normalCullingThresholdPermyriad omitted");++valueProbes;}
{ParasiteLaunchRequest zero;auto value=zero;mutateEncoderField(value.eliteCullingThresholdPermyriad);require(encode(value)!=encode(zero),"ParasiteLaunchRequest.eliteCullingThresholdPermyriad omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"ParasiteImpactRequest.projectileId omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.currentTick);require(encode(value)!=encode(zero),"ParasiteImpactRequest.currentTick omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.impact);require(encode(value)!=encode(zero),"ParasiteImpactRequest.impact omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetObjectId omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetTypeId);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetTypeId omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetKind);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetKind omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetPresent);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetPresent omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetAlive);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetAlive omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetParasiteable);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetParasiteable omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.targetMaximumHealth);require(encode(value)!=encode(zero),"ParasiteImpactRequest.targetMaximumHealth omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.impactCell);require(encode(value)!=encode(zero),"ParasiteImpactRequest.impactCell omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.returnCellAvailable);require(encode(value)!=encode(zero),"ParasiteImpactRequest.returnCellAvailable omitted");++valueProbes;}
{ParasiteImpactRequest zero;auto value=zero;mutateEncoderField(value.returnCell);require(encode(value)!=encode(zero),"ParasiteImpactRequest.returnCell omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.currentTick);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.currentTick omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.targetObjectId omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.event);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.event omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.ejectCellAvailable);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.ejectCellAvailable omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.ejectCell);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.ejectCell omitted");++valueProbes;}
{ParasiteHostEventRequest zero;auto value=zero;mutateEncoderField(value.stunTicks);require(encode(value)!=encode(zero),"ParasiteHostEventRequest.stunTicks omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.projectileId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.sourceObjectId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.sourceObjectId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetObjectId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.owner);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.owner omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.sourceTypeId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.sourceTypeId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetTypeId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetTypeId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.weaponId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.weaponId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.warheadId);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.warheadId omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetKind);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetKind omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetCell);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetCell omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.launchedTick);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.launchedTick omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.damage);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.damage omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.damageIntervalTicks);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.damageIntervalTicks omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.paralyzes);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.paralyzes omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetMaximumHealth);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetMaximumHealth omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.cullingThresholdPerMyriad);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.cullingThresholdPerMyriad omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.targetParasiteable);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.targetParasiteable omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.organic);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.organic omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.sourceElite);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.sourceElite omitted");++valueProbes;}
{ParasitePendingLaunchState zero;auto value=zero;mutateEncoderField(value.culling);require(encode(value)!=encode(zero),"ParasitePendingLaunchState.culling omitted");++valueProbes;}
{ParasiteResolvedLaunchState zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"ParasiteResolvedLaunchState.projectileId omitted");++valueProbes;}
{ParasiteResolvedLaunchState zero;auto value=zero;mutateEncoderField(value.sourceObjectId);require(encode(value)!=encode(zero),"ParasiteResolvedLaunchState.sourceObjectId omitted");++valueProbes;}
{ParasiteResolvedLaunchState zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasiteResolvedLaunchState.targetObjectId omitted");++valueProbes;}
{ParasiteResolvedLaunchState zero;auto value=zero;mutateEncoderField(value.resolvedTick);require(encode(value)!=encode(zero),"ParasiteResolvedLaunchState.resolvedTick omitted");++valueProbes;}
{ParasiteResolvedLaunchState zero;auto value=zero;mutateEncoderField(value.hostTerminated);require(encode(value)!=encode(zero),"ParasiteResolvedLaunchState.hostTerminated omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"ParasiteLinkState.projectileId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"ParasiteLinkState.targetObjectId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.sourceObjectId);require(encode(value)!=encode(zero),"ParasiteLinkState.sourceObjectId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.owner);require(encode(value)!=encode(zero),"ParasiteLinkState.owner omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.sourceTypeId);require(encode(value)!=encode(zero),"ParasiteLinkState.sourceTypeId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.targetTypeId);require(encode(value)!=encode(zero),"ParasiteLinkState.targetTypeId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.weaponId);require(encode(value)!=encode(zero),"ParasiteLinkState.weaponId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.warheadId);require(encode(value)!=encode(zero),"ParasiteLinkState.warheadId omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.damage);require(encode(value)!=encode(zero),"ParasiteLinkState.damage omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.attachedTick);require(encode(value)!=encode(zero),"ParasiteLinkState.attachedTick omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.nextDamageTick);require(encode(value)!=encode(zero),"ParasiteLinkState.nextDamageTick omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.damageIntervalTicks);require(encode(value)!=encode(zero),"ParasiteLinkState.damageIntervalTicks omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.organic);require(encode(value)!=encode(zero),"ParasiteLinkState.organic omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.sourceElite);require(encode(value)!=encode(zero),"ParasiteLinkState.sourceElite omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.paralyzes);require(encode(value)!=encode(zero),"ParasiteLinkState.paralyzes omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.culling);require(encode(value)!=encode(zero),"ParasiteLinkState.culling omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.cullingThresholdPerMyriad);require(encode(value)!=encode(zero),"ParasiteLinkState.cullingThresholdPerMyriad omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.targetMaximumHealth);require(encode(value)!=encode(zero),"ParasiteLinkState.targetMaximumHealth omitted");++valueProbes;}
{ParasiteLinkState zero;auto value=zero;mutateEncoderField(value.sourceExpectedInLimbo);require(encode(value)!=encode(zero),"ParasiteLinkState.sourceExpectedInLimbo omitted");++valueProbes;}
{ParasiteOperationResult zero;auto value=zero;mutateEncoderField(value.accepted);require(encode(value)!=encode(zero),"ParasiteOperationResult.accepted omitted");++valueProbes;}
{ParasiteOperationResult zero;auto value=zero;mutateEncoderField(value.idempotent);require(encode(value)!=encode(zero),"ParasiteOperationResult.idempotent omitted");++valueProbes;}
{ParasiteOperationResult zero;auto value=zero;mutateEncoderField(value.reason);require(encode(value)!=encode(zero),"ParasiteOperationResult.reason omitted");++valueProbes;}
{ParasiteOperationResult zero;auto value=zero;mutateEncoderField(value.actions);require(encode(value)!=encode(zero),"ParasiteOperationResult.actions omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.kind);require(encode(value)!=encode(zero),"SpecialMechanismAction.kind omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.tick);require(encode(value)!=encode(zero),"SpecialMechanismAction.tick omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.projectileId);require(encode(value)!=encode(zero),"SpecialMechanismAction.projectileId omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.objectId);require(encode(value)!=encode(zero),"SpecialMechanismAction.objectId omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"SpecialMechanismAction.targetObjectId omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.owner);require(encode(value)!=encode(zero),"SpecialMechanismAction.owner omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.damage);require(encode(value)!=encode(zero),"SpecialMechanismAction.damage omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.warheadId);require(encode(value)!=encode(zero),"SpecialMechanismAction.warheadId omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.forceDestruction);require(encode(value)!=encode(zero),"SpecialMechanismAction.forceDestruction omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.paralyzes);require(encode(value)!=encode(zero),"SpecialMechanismAction.paralyzes omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.culling);require(encode(value)!=encode(zero),"SpecialMechanismAction.culling omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.suppressDeathWeapon);require(encode(value)!=encode(zero),"SpecialMechanismAction.suppressDeathWeapon omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.stunTicks);require(encode(value)!=encode(zero),"SpecialMechanismAction.stunTicks omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.cell);require(encode(value)!=encode(zero),"SpecialMechanismAction.cell omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.reason);require(encode(value)!=encode(zero),"SpecialMechanismAction.reason omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.replacesTerminatingHost);require(encode(value)!=encode(zero),"SpecialMechanismAction.replacesTerminatingHost omitted");++valueProbes;}
{SpecialMechanismAction zero;auto value=zero;mutateEncoderField(value.sourceWasAttachedToHost);require(encode(value)!=encode(zero),"SpecialMechanismAction.sourceWasAttachedToHost omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.kind);require(encode(value)!=encode(zero),"SpecialMechanismEvent.kind omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.tick);require(encode(value)!=encode(zero),"SpecialMechanismEvent.tick omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.objectId);require(encode(value)!=encode(zero),"SpecialMechanismEvent.objectId omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.targetObjectId);require(encode(value)!=encode(zero),"SpecialMechanismEvent.targetObjectId omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.value);require(encode(value)!=encode(zero),"SpecialMechanismEvent.value omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.cell);require(encode(value)!=encode(zero),"SpecialMechanismEvent.cell omitted");++valueProbes;}
{SpecialMechanismEvent zero;auto value=zero;mutateEncoderField(value.reason);require(encode(value)!=encode(zero),"SpecialMechanismEvent.reason omitted");++valueProbes;}
}
class Original {
public:
ParasiteOperationResult beginParasiteLaunch(
    const ParasiteLaunchRequest& request,
    const SpecialEventSink& emit
);
ParasiteOperationResult handleParasiteHostEvent(
    const ParasiteHostEventRequest& request,
    const SpecialEventSink& emit
);
bool isParasiteHostParalyzed(
    int objectId
) const;
bool isParasiteSourceLimboed(
    int objectId
) const;
int parasiteTargetForSource(
    int objectId
) const;
std::vector<SpecialMechanismAction>
reconcileParasites(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<int>& activeProjectileIds,
    int tick,
    const SpecialEventSink& emit
);
ParasiteOperationResult resolveParasiteImpact(
    const ParasiteImpactRequest& request,
    const SpecialEventSink& emit
);
std::vector<SpecialMechanismAction>
tickParasites(
    const std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialEventSink& emit
);
    std::map<int,ParasitePendingLaunchState> parasitePendingByProjectile_;
    std::map<int,ParasiteLinkState> parasiteByTarget_;
    std::map<int,ParasiteResolvedLaunchState> parasiteResolvedByProjectile_;
    void reset(){parasitePendingByProjectile_.clear();parasiteByTarget_.clear();parasiteResolvedByProjectile_.clear();}
    void swap(Original& other) noexcept {parasitePendingByProjectile_.swap(other.parasitePendingByProjectile_);parasiteByTarget_.swap(other.parasiteByTarget_);parasiteResolvedByProjectile_.swap(other.parasiteResolvedByProjectile_);}
    const auto& pendingParasiteLaunches() const{return parasitePendingByProjectile_;}
    const auto& parasiteLinks() const{return parasiteByTarget_;}
    const auto& resolvedParasiteLaunches() const{return parasiteResolvedByProjectile_;}
    static const WorldObjectInstance* findObject(const std::vector<WorldObjectInstance>& objects,int id){checkpoint("query:"+std::to_string(id));for(const auto& object:objects)if(object.instanceId==id)return &object;return nullptr;}
    void onObjectRemoved(const std::vector<WorldObjectInstance>& objects,int objectId,int tick,
        const SpecialEventSink& emit,const std::string& reason,std::vector<SpecialMechanismAction>* actions);
};
ParasiteOperationResult Original::beginParasiteLaunch(
    const ParasiteLaunchRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        if (emit != nullptr) {
            emit({
                "ParasiteLaunchRejected",
                request.currentTick,
                request.sourceObjectId,
                request.targetObjectId,
                request.projectileId,
                request.targetCell,
                reason
            });
        }
        return result;
    };
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0
        || request.sourceObjectId < 0 || request.targetObjectId < 0
        || request.sourceObjectId == request.targetObjectId) {
        return reject("invalid parasite launch identity");
    }
    if (!validParasiteText(request.sourceOwner)
        || !validParasiteText(request.sourceTypeId)
        || !validParasiteText(request.targetTypeId)
        || !validParasiteText(request.weaponId)
        || !validParasiteText(request.warheadId)) {
        return reject("invalid parasite launch text snapshot");
    }
    if (request.damage < 0 || request.damageIntervalTicks <= 0
        || request.paralyzes < 0 || request.targetMaximumHealth <= 0
        || !validParasiteRatio(request.normalCullingThresholdPermyriad)
        || !validParasiteRatio(request.eliteCullingThresholdPermyriad)) {
        return reject("invalid parasite launch rules snapshot");
    }
    if (request.targetKind == ParasiteTargetKind::Vehicle
        && !request.targetParasiteable) {
        return reject("vehicle target is not parasiteable");
    }
    const auto existingProjectile = parasitePendingByProjectile_.find(
        request.projectileId
    );
    if (existingProjectile != parasitePendingByProjectile_.end()) {
        const ParasitePendingLaunchState& pending = existingProjectile->second;
        if (pending.sourceObjectId == request.sourceObjectId
            && pending.targetObjectId == request.targetObjectId
            && pending.sourceTypeId == request.sourceTypeId
            && pending.targetTypeId == request.targetTypeId
            && pending.weaponId == request.weaponId
            && pending.warheadId == request.warheadId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("projectile identity already belongs to another launch");
    }
    const auto resolved = parasiteResolvedByProjectile_.find(
        request.projectileId
    );
    if (resolved != parasiteResolvedByProjectile_.end()
        && resolved->second.resolvedTick == request.currentTick) {
        if (resolved->second.sourceObjectId == request.sourceObjectId
            && resolved->second.targetObjectId == request.targetObjectId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("resolved projectile identity conflicts with launch");
    }
    if (parasitePendingByProjectile_.size()
            + parasiteResolvedByProjectile_.size()
        >= kMaximumParasiteRecords) {
        return reject("parasite launch/resolution capacity reached");
    }
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source already owns an in-flight launch");
        }
        if (pending.targetObjectId == request.targetObjectId) {
            return reject("parasite target is already being boarded");
        }
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        if (link.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source is already attached");
        }
        if (targetId == request.targetObjectId) {
            return reject("parasite target already has a parasite");
        }
    }

    ParasitePendingLaunchState pending;
    pending.projectileId = request.projectileId;
    pending.sourceObjectId = request.sourceObjectId;
    pending.targetObjectId = request.targetObjectId;
    pending.owner = request.sourceOwner;
    pending.sourceTypeId = request.sourceTypeId;
    pending.targetTypeId = request.targetTypeId;
    pending.weaponId = request.weaponId;
    pending.warheadId = request.warheadId;
    pending.targetKind = request.targetKind;
    pending.targetCell = request.targetCell;
    pending.launchedTick = request.currentTick;
    pending.damage = request.damage;
    pending.damageIntervalTicks = request.damageIntervalTicks;
    pending.paralyzes = request.paralyzes;
    pending.targetMaximumHealth = request.targetMaximumHealth;
    pending.cullingThresholdPerMyriad = request.sourceElite
        ? request.eliteCullingThresholdPermyriad
        : request.normalCullingThresholdPermyriad;
    pending.targetParasiteable = request.targetParasiteable;
    pending.organic = request.sourceOrganic;
    pending.sourceElite = request.sourceElite;
    pending.culling = request.culling;
    parasitePendingByProjectile_[pending.projectileId] = pending;

    SpecialMechanismAction enterLimbo;
    enterLimbo.kind = SpecialMechanismActionKind::ParasiteEnterLimbo;
    enterLimbo.tick = request.currentTick;
    enterLimbo.projectileId = request.projectileId;
    enterLimbo.objectId = request.sourceObjectId;
    enterLimbo.targetObjectId = request.targetObjectId;
    enterLimbo.owner = request.sourceOwner;
    enterLimbo.damage = request.damage;
    enterLimbo.warheadId = request.warheadId;
    enterLimbo.paralyzes = request.paralyzes;
    enterLimbo.culling = request.culling;
    enterLimbo.cell = request.targetCell;
    enterLimbo.reason = "LimboLaunch projectile began";
    result.actions.push_back(std::move(enterLimbo));
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            "ParasiteLaunchBegan",
            request.currentTick,
            request.sourceObjectId,
            request.targetObjectId,
            request.projectileId,
            request.targetCell,
            request.weaponId
        });
    }
    return result;
}

ParasiteOperationResult Original::handleParasiteHostEvent(
    const ParasiteHostEventRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    if (request.currentTick < 0 || request.targetObjectId < 0
        || request.stunTicks < 0) {
        result.reason = "invalid parasite host event";
        return result;
    }
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    const auto linkIt = parasiteByTarget_.find(request.targetObjectId);
    if (linkIt == parasiteByTarget_.end()) {
        const auto repeated = std::find_if(
            parasiteResolvedByProjectile_.begin(),
            parasiteResolvedByProjectile_.end(),
            [&request](const auto& entry) {
                return entry.second.targetObjectId == request.targetObjectId
                    && entry.second.resolvedTick == request.currentTick
                    && entry.second.hostTerminated;
            }
        );
        if (repeated != parasiteResolvedByProjectile_.end()) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        result.reason = "target has no attached parasite";
        return result;
    }
    const ParasiteLinkState link = linkIt->second;
    const bool killDirectly =
        request.event == ParasiteHostEventKind::DestroyedExternally
        || (request.event == ParasiteHostEventKind::Healed && !link.organic);
    const bool canEject = !killDirectly && request.ejectCellAvailable;

    SpecialMechanismAction action;
    action.kind = canEject
        ? SpecialMechanismActionKind::ParasiteEject
        : SpecialMechanismActionKind::ParasiteKillParasite;
    action.tick = request.currentTick;
    action.projectileId = link.projectileId;
    action.objectId = link.sourceObjectId;
    action.targetObjectId = link.targetObjectId;
    action.owner = link.owner;
    action.paralyzes = link.paralyzes;
    action.cell = canEject ? request.ejectCell : SpecialCell{};
    action.suppressDeathWeapon = !canEject;
    action.replacesTerminatingHost = canEject
        && request.event == ParasiteHostEventKind::DestroyedByParasite;
    action.sourceWasAttachedToHost = true;
    const bool stunned = canEject
        && (request.event == ParasiteHostEventKind::Healed
            || request.event == ParasiteHostEventKind::SonicHit
            || request.event == ParasiteHostEventKind::Teleporting);
    action.stunTicks = stunned ? request.stunTicks : 0;
    action.reason = canEject
        ? "parasite ejected because host was "
            + parasiteHostEventName(request.event)
        : "parasite destroyed without death weapon because host was "
            + parasiteHostEventName(request.event)
            + (request.ejectCellAvailable || killDirectly
                ? std::string()
                : "; no legal eject cell");
    result.actions.push_back(std::move(action));
    parasiteByTarget_.erase(linkIt);
    parasiteResolvedByProjectile_[link.projectileId] = {
        link.projectileId,
        link.sourceObjectId,
        link.targetObjectId,
        request.currentTick,
        true
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            canEject ? "ParasiteEjected" : "ParasiteKilled",
            request.currentTick,
            link.sourceObjectId,
            link.targetObjectId,
            0,
            canEject ? request.ejectCell : SpecialCell{},
            parasiteHostEventName(request.event)
        });
    }
    return result;
}

bool Original::isParasiteHostParalyzed(
    int objectId
) const {
    const auto found = parasiteByTarget_.find(objectId);
    return found != parasiteByTarget_.end() && found->second.paralyzes != 0;
}

bool Original::isParasiteSourceLimboed(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return true;
        }
    }
    for (const auto& [unusedTargetId, link] : parasiteByTarget_) {
        (void)unusedTargetId;
        if (link.sourceObjectId == objectId) {
            return true;
        }
    }
    return false;
}

int Original::parasiteTargetForSource(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return pending.targetObjectId;
        }
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        if (link.sourceObjectId == objectId) {
            return targetId;
        }
    }
    return -1;
}

std::vector<SpecialMechanismAction>
Original::reconcileParasites(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<int>& activeProjectileIds,
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    const std::set<int> activeProjectiles(
        activeProjectileIds.begin(),
        activeProjectileIds.end()
    );
    for (auto pending = parasitePendingByProjectile_.begin();
         pending != parasitePendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        const WorldObjectInstance* source = findObject(
            objects,
            state.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && source->type == state.sourceTypeId
            && ((!source->alive && source->mission == "ParasiteLimbo")
                || (source->alive && tick == state.launchedTick));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0 && target->type == state.targetTypeId;
        const bool projectileValid = activeProjectiles.find(state.projectileId)
            != activeProjectiles.end();
        if (sourceValid && targetValid && projectileValid) {
            ++pending;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = target == nullptr ? state.targetCell : objectCell(target);
        action.suppressDeathWeapon = !sourceValid;
        action.reason = !sourceValid
            ? "pending parasite source invalid during reconciliation"
            : (!targetValid
                ? "pending parasite target invalid during reconciliation"
                : "pending parasite projectile missing during reconciliation");
        actions.push_back(std::move(action));
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceValid ? "ParasiteReturned" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                target == nullptr ? state.targetCell : objectCell(target),
                "pending launch reconciled"
            });
        }
        pending = parasitePendingByProjectile_.erase(pending);
    }
    for (auto link = parasiteByTarget_.begin();
         link != parasiteByTarget_.end();) {
        const ParasiteLinkState state = link->second;
        const WorldObjectInstance* source = findObject(
            objects,
            state.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && (state.sourceTypeId.empty()
                || source->type == state.sourceTypeId)
            && (!state.sourceExpectedInLimbo
                || (!source->alive && source->mission == "ParasiteLimbo"));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0
            && (state.targetTypeId.empty()
                || target->type == state.targetTypeId);
        if (sourceValid && targetValid) {
            ++link;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteKillParasite
            : SpecialMechanismActionKind::ParasiteReleaseTarget;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = objectCell(target);
        action.suppressDeathWeapon = sourceValid;
        action.sourceWasAttachedToHost = sourceValid;
        action.reason = sourceValid
            ? "parasite host invalid during reconciliation"
            : "parasite source invalid during reconciliation";
        actions.push_back(std::move(action));
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            sourceValid
        };
        link = parasiteByTarget_.erase(link);
    }
    return actions;
}

ParasiteOperationResult Original::resolveParasiteImpact(
    const ParasiteImpactRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        return result;
    };
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0) {
        return reject("invalid parasite impact identity");
    }
    const auto pendingIt = parasitePendingByProjectile_.find(
        request.projectileId
    );
    if (pendingIt == parasitePendingByProjectile_.end()) {
        const auto resolved = parasiteResolvedByProjectile_.find(
            request.projectileId
        );
        if (resolved != parasiteResolvedByProjectile_.end()
            && resolved->second.resolvedTick == request.currentTick) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("parasite projectile has no pending launch");
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    if (request.currentTick < pending.launchedTick) {
        return reject("parasite impact precedes launch");
    }
    if (request.targetPresent
        && (!validParasiteText(request.targetTypeId)
            || request.targetMaximumHealth <= 0)) {
        return reject("invalid parasite impact target snapshot");
    }
    if (pending.damageIntervalTicks > 0
        && request.currentTick
            > std::numeric_limits<int>::max()
                - pending.damageIntervalTicks) {
        return reject("parasite impact tick would overflow bite interval");
    }

    const bool identityMatches = request.targetPresent
        && request.targetObjectId == pending.targetObjectId
        && request.targetTypeId == pending.targetTypeId
        && request.targetKind == pending.targetKind;
    const bool liveHit = request.impact == ParasiteImpactKind::Hit
        && identityMatches && request.targetAlive;
    const bool vehicleHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Vehicle
        && pending.targetParasiteable && request.targetParasiteable;
    const bool infantryHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Infantry;

    const auto appendReturnOrKill = [&](const std::string& reason,
                                      bool replacesTerminatingHost = false) {
        SpecialMechanismAction action;
        action.kind = request.returnCellAvailable
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = request.currentTick;
        action.projectileId = pending.projectileId;
        action.objectId = pending.sourceObjectId;
        action.targetObjectId = pending.targetObjectId;
        action.owner = pending.owner;
        action.paralyzes = pending.paralyzes;
        action.cell = request.returnCellAvailable
            ? request.returnCell
            : request.impactCell;
        action.suppressDeathWeapon = !request.returnCellAvailable;
        action.replacesTerminatingHost = request.returnCellAvailable
            && replacesTerminatingHost;
        action.reason = request.returnCellAvailable
            ? reason
            : reason + "; no legal return cell";
        result.actions.push_back(std::move(action));
    };

    if (vehicleHit) {
        if (parasiteByTarget_.size() >= kMaximumParasiteRecords
            || parasiteByTarget_.find(pending.targetObjectId)
                != parasiteByTarget_.end()) {
            return reject("parasite link capacity or target conflict");
        }
        ParasiteLinkState link;
        link.projectileId = pending.projectileId;
        link.targetObjectId = pending.targetObjectId;
        link.sourceObjectId = pending.sourceObjectId;
        link.owner = pending.owner;
        link.sourceTypeId = pending.sourceTypeId;
        link.targetTypeId = pending.targetTypeId;
        link.weaponId = pending.weaponId;
        link.warheadId = pending.warheadId;
        link.damage = pending.damage;
        link.attachedTick = request.currentTick;
        link.damageIntervalTicks = pending.damageIntervalTicks;
        link.nextDamageTick = pending.organic
            ? request.currentTick + pending.damageIntervalTicks
            : request.currentTick;
        link.organic = pending.organic;
        link.sourceElite = pending.sourceElite;
        link.paralyzes = pending.paralyzes;
        link.culling = pending.culling;
        link.cullingThresholdPerMyriad = pending.cullingThresholdPerMyriad;
        link.targetMaximumHealth = std::max(
            1,
            request.targetMaximumHealth
        );
        link.sourceExpectedInLimbo = true;
        parasiteByTarget_[link.targetObjectId] = link;

        SpecialMechanismAction attached;
        attached.kind = SpecialMechanismActionKind::ParasiteAttachHost;
        attached.tick = request.currentTick;
        attached.projectileId = pending.projectileId;
        attached.objectId = pending.sourceObjectId;
        attached.targetObjectId = pending.targetObjectId;
        attached.owner = pending.owner;
        attached.damage = pending.damage;
        attached.warheadId = pending.warheadId;
        attached.paralyzes = pending.paralyzes;
        attached.culling = pending.culling;
        attached.cell = request.impactCell;
        attached.reason = "LimboLaunch parasite attached to vehicle";
        result.actions.push_back(std::move(attached));
    } else if (infantryHit) {
        SpecialMechanismAction kill;
        kill.kind = SpecialMechanismActionKind::ParasiteKillTarget;
        kill.tick = request.currentTick;
        kill.projectileId = pending.projectileId;
        kill.objectId = pending.sourceObjectId;
        kill.targetObjectId = pending.targetObjectId;
        kill.owner = pending.owner;
        kill.damage = std::numeric_limits<int>::max();
        kill.warheadId = pending.warheadId;
        kill.forceDestruction = true;
        kill.cell = request.impactCell;
        kill.reason = "parasite warhead instantly kills infantry";
        result.actions.push_back(std::move(kill));
        appendReturnOrKill("parasite returned after infantry impact", true);
    } else {
        appendReturnOrKill(
            identityMatches
                ? "parasite returned after invalid or dead target"
                : "parasite returned after miss or identity mismatch"
        );
    }

    parasitePendingByProjectile_.erase(pendingIt);
    parasiteResolvedByProjectile_[request.projectileId] = {
        request.projectileId,
        pending.sourceObjectId,
        pending.targetObjectId,
        request.currentTick
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            vehicleHit ? "ParasiteAttached"
                : (infantryHit ? "ParasiteInfantryKilled"
                               : "ParasiteReturned"),
            request.currentTick,
            pending.sourceObjectId,
            pending.targetObjectId,
            request.projectileId,
            request.impactCell,
            pending.warheadId
        });
    }
    return result;
}

std::vector<SpecialMechanismAction>
Original::tickParasites(
    const std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < tick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    for (auto entry = parasiteByTarget_.begin();
         entry != parasiteByTarget_.end();) {
        ParasiteLinkState& link = entry->second;
        const WorldObjectInstance* source = findObject(
            objects,
            link.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            link.targetObjectId
        );
        const bool sourceDestroyed = source == nullptr || source->health <= 0
            || (!link.sourceTypeId.empty()
                && source->type != link.sourceTypeId)
            || (!source->alive
                && (!link.sourceExpectedInLimbo
                    || source->mission != "ParasiteLimbo"));
        if (sourceDestroyed) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteReleaseTarget;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.reason = "parasite source died or left the world";
            actions.push_back(std::move(action));
            parasiteResolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                false
            };
            entry = parasiteByTarget_.erase(entry);
            continue;
        }
        if (target == nullptr || !target->alive || target->health <= 0
            || target->kind != WorldObjectKind::Unit
            || (!link.targetTypeId.empty()
                && target->type != link.targetTypeId)) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.suppressDeathWeapon = true;
            action.sourceWasAttachedToHost = true;
            action.reason = "parasite host died externally or became invalid";
            actions.push_back(std::move(action));
            parasiteResolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                true
            };
            entry = parasiteByTarget_.erase(entry);
            continue;
        }
        if (tick >= link.nextDamageTick) {
            const bool culled = link.culling
                && link.cullingThresholdPerMyriad > 0
                && static_cast<long long>(target->health)
                    * kParasiteRatioPerMyriad
                    <= static_cast<long long>(link.targetMaximumHealth)
                        * link.cullingThresholdPerMyriad;
            SpecialMechanismAction action;
            action.kind = culled
                ? SpecialMechanismActionKind::ParasiteKillTarget
                : SpecialMechanismActionKind::ParasiteDamage;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.damage = culled
                ? std::numeric_limits<int>::max()
                : link.damage;
            action.warheadId = link.warheadId;
            action.forceDestruction = culled;
            action.paralyzes = link.paralyzes;
            action.culling = link.culling;
            action.cell = objectCell(target);
            action.reason = culled
                ? (link.sourceElite ? "elite parasite culling threshold"
                                    : "parasite culling threshold")
                : "parasite damage interval";
            actions.push_back(std::move(action));
            link.nextDamageTick = tick
                    > std::numeric_limits<int>::max()
                        - std::max(1, link.damageIntervalTicks)
                ? std::numeric_limits<int>::max()
                : tick + std::max(1, link.damageIntervalTicks);
            if (emit != nullptr) {
                emit({
                    culled ? "ParasiteCulling" : "ParasiteDamage",
                    tick,
                    link.sourceObjectId,
                    link.targetObjectId,
                    culled ? target->health : link.damage,
                    objectCell(target),
                    link.warheadId
                });
            }
        }
        ++entry;
    }
    return actions;
}

void Original::onObjectRemoved(const std::vector<WorldObjectInstance>& objects,int objectId,int tick,
const SpecialEventSink& emit,const std::string& reason,std::vector<SpecialMechanismAction>* actions){
    for (auto pending = parasitePendingByProjectile_.begin();
         pending != parasitePendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++pending;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteKillParasite
                : SpecialMechanismActionKind::ParasiteReturnFromLimbo;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = state.targetCell;
            action.suppressDeathWeapon = sourceRemoved;
            action.reason = sourceRemoved
                ? "pending parasite source removed"
                : "pending parasite target removed";
            actions->push_back(std::move(action));
        }
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteKilled" : "ParasiteReturned",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                state.targetCell,
                reason
            });
        }
        pending = parasitePendingByProjectile_.erase(pending);
    }

    for (auto parasite = parasiteByTarget_.begin();
         parasite != parasiteByTarget_.end();) {
        const ParasiteLinkState state = parasite->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++parasite;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteReleaseTarget
                : SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = objectCell(findObject(objects, state.targetObjectId));
            action.suppressDeathWeapon = !sourceRemoved;
            action.sourceWasAttachedToHost = !sourceRemoved;
            action.reason = sourceRemoved
                ? "parasite source removed"
                : "parasite host removed externally";
            actions->push_back(std::move(action));
        }
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteLinkCleared" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                0,
                objectCell(findObject(objects, state.targetObjectId)),
                reason
            });
        }
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            !sourceRemoved
        };
        parasite = parasiteByTarget_.erase(parasite);
    }

}

struct Fault {};
template<class State> struct Side {
    State state;
    std::vector<WorldObjectInstance> objects;
    std::vector<std::string> ports;
    std::string trace;
    int failAt=-1, mutateAt=-1, calls=0;
    bool hitFault=false, mutated=false, nested=false, didNested=false;
    std::string canonical() const {return encode(state.pendingParasiteLaunches())+encode(state.parasiteLinks())+encode(state.resolvedParasiteLaunches());}
    void mark(const std::string& port) {
        ++calls;ports.push_back(port);trace+=encode(port)+canonical();
        if(calls==failAt){hitFault=true;throw Fault{};}
        if(calls==mutateAt&&!objects.empty()){
            objects.back().health=objects.back().health==0?73:0;
            objects.front().mission="ProbeWorldMutation";mutated=true;
        }
        if(nested&&!didNested&&port.rfind("event:",0)==0){
            didNested=true;ParasiteLaunchRequest q=validLaunch(901,902,903,20);
            const auto result=state.beginParasiteLaunch(q,{});trace+="nested:"+encode(result)+canonical();
        }
    }
    static ParasiteLaunchRequest validLaunch(int projectile=7,int source=11,int target=21,int tick=10) {
        ParasiteLaunchRequest q;q.projectileId=projectile;q.currentTick=tick;q.sourceObjectId=source;q.targetObjectId=target;
        q.sourceOwner="Americans";q.sourceTypeId="DRON";q.targetTypeId="HTNK";q.weaponId="DroneJump";q.warheadId="Parasite";
        q.targetKind=ParasiteTargetKind::Vehicle;q.targetCell={7,8};q.damage=50;q.damageIntervalTicks=3;q.paralyzes=2;
        q.targetMaximumHealth=400;q.targetParasiteable=true;q.normalCullingThresholdPermyriad=2500;q.eliteCullingThresholdPermyriad=5000;return q;
    }
    static ParasiteImpactRequest validImpact(int projectile=7,int target=21,int tick=10) {
        ParasiteImpactRequest q;q.projectileId=projectile;q.currentTick=tick;q.impact=ParasiteImpactKind::Hit;q.targetObjectId=target;
        q.targetTypeId="HTNK";q.targetKind=ParasiteTargetKind::Vehicle;q.targetPresent=q.targetAlive=q.targetParasiteable=true;
        q.targetMaximumHealth=400;q.impactCell={3,4};q.returnCellAvailable=true;q.returnCell={5,6};return q;
    }
    struct Queries final:RuntimeParasiteObjectQueries {
        Side& side;explicit Queries(Side& x):side(x){}
        const WorldObjectInstance* findObject(int id) const override {
            checkpoint("query:"+std::to_string(id));
            for(const auto& object:side.objects)if(object.instanceId==id)return &object;
            return nullptr;
        }
    };
    void seed(int phase,int variation) {
        objects.clear();
        for(int id:{11,12,21,22}){WorldObjectInstance object;object.instanceId=id;object.type=id<20?"DRON":"HTNK";
            object.kind=WorldObjectKind::Unit;object.health=id<20?100:400;object.alive=true;object.owner=id<20?"Americans":"Russians";
            object.x=id;object.y=3;object.cell=id+3000;object.mission=id<20?"ParasiteLimbo":"Guard";if(id<20)object.alive=false;objects.push_back(object);}
        if(phase){auto q=validLaunch();q.sourceOrganic=(variation&1)!=0;q.sourceElite=(variation&2)!=0;q.culling=(variation&4)!=0;
            if(phase==3){q.targetKind=ParasiteTargetKind::Infantry;q.targetTypeId="E1";}
            require(state.beginParasiteLaunch(q,{}).accepted,"seed first launch");
            if(phase!=1&&phase!=3){require(state.resolveParasiteImpact(validImpact(),{}).accepted,"seed first attach");}
            if(phase==4||phase==6){auto b=validLaunch(8,12,22);b.sourceOrganic=(variation&1)==0;b.paralyzes=variation%3;
                require(state.beginParasiteLaunch(b,{}).accepted,"seed second launch");if(phase==4)require(state.resolveParasiteImpact(validImpact(8,22),{}).accepted,"seed second attach");}
            if(phase==5){ParasiteHostEventRequest h;h.currentTick=10;h.targetObjectId=21;h.event=ParasiteHostEventKind::DestroyedExternally;
                require(state.handleParasiteHostEvent(h,{}).accepted,"seed terminated host");}
        }
        switch(variation%12){
        case 1:objects[0].health=0;break;
        case 2:objects.erase(objects.begin());break;
        case 3:objects[0].mission="WrongLimbo";break;
        case 4:objects[2].alive=false;break;
        case 5:objects[2].health=0;break;
        case 6:objects[2].type="WrongType";break;
        case 7:objects[2].kind=WorldObjectKind::Infantry;break;
        case 8:objects[2].health=100;break;
        case 9:objects[0].alive=true;break;
        case 10:objects[0].type="WrongSource";break;
        case 11:{auto duplicate=objects[2];duplicate.health=0;objects.insert(objects.begin(),duplicate);break;}
        default:break;
        }
    }
    std::string run(int operation,int variant) {
        std::string outcome;SpecialEventSink sink=[this](const auto& event){mark("event:"+event.kind);trace+=encode(event);};
        Queries queries(*this);activeCheckpoint=[this](const auto& port){mark(port);};
        struct Restore {~Restore(){activeCheckpoint={};}} restore;
        try {
            if(operation==0){auto q=validLaunch();
                switch(variant%28){
                case 1:q.projectileId=0;break;case 2:q.currentTick=-1;break;case 3:q.sourceObjectId=-1;break;case 4:q.targetObjectId=q.sourceObjectId;break;
                case 5:q.sourceOwner.clear();break;case 6:q.sourceTypeId=std::string(257,'x');break;case 7:q.targetTypeId.clear();break;case 8:q.weaponId.clear();break;case 9:q.warheadId.clear();break;
                case 10:q.damage=-1;break;case 11:q.damageIntervalTicks=0;break;case 12:q.paralyzes=-1;break;case 13:q.targetMaximumHealth=0;break;
                case 14:q.normalCullingThresholdPermyriad=-1;break;case 15:q.eliteCullingThresholdPermyriad=10001;break;case 16:q.targetParasiteable=false;break;
                case 17:q.projectileId=8;q.sourceObjectId=12;q.targetObjectId=22;break;case 18:q.targetObjectId=22;break;case 19:q.sourceObjectId=12;break;
                case 20:q.currentTick=11;break;case 21:q.currentTick=9;break;case 22:q.projectileId=-7;break;
                case 23:q.targetKind=ParasiteTargetKind::Infantry;q.targetParasiteable=false;break;
                case 24:q.sourceElite=q.sourceOrganic=q.culling=true;break;case 25:q.currentTick=INT_MAX;q.damageIntervalTicks=INT_MAX;break;
                case 26:q.targetKind=static_cast<ParasiteTargetKind>(7);break;case 27:q.sourceOwner=std::string(256,'a');q.damageIntervalTicks=INT_MAX;break;default:break;}
                const auto input=encode(q);outcome=encode(state.beginParasiteLaunch(q,sink));require(encode(q)==input,"launch request modified");
            } else if(operation==1){auto q=validImpact();
                switch(variant%22){case 1:q.projectileId=0;break;case 2:q.currentTick=-1;break;case 3:q.projectileId=999;break;case 4:q.currentTick=9;break;
                case 5:q.targetTypeId.clear();break;case 6:q.targetMaximumHealth=0;break;case 7:q.currentTick=INT_MAX;break;case 8:q.targetPresent=false;break;
                case 9:q.impact=ParasiteImpactKind::Miss;break;case 10:q.targetAlive=false;break;case 11:q.targetObjectId=22;break;case 12:q.targetTypeId="OTHER";break;
                case 13:q.targetKind=ParasiteTargetKind::Infantry;q.targetTypeId="E1";break;case 14:q.targetParasiteable=false;break;case 15:q.returnCellAvailable=false;break;
                case 16:q.targetPresent=false;q.returnCellAvailable=false;break;case 17:q.currentTick=11;break;case 18:q.projectileId=8;q.targetObjectId=22;break;
                case 19:q.targetKind=static_cast<ParasiteTargetKind>(8);break;case 20:q.targetTypeId=std::string(256,'x');break;case 21:q.currentTick=INT_MAX-3;break;default:break;}
                const auto input=encode(q);outcome=encode(state.resolveParasiteImpact(q,sink));require(encode(q)==input,"impact request modified");
            } else if(operation==2){ParasiteHostEventRequest q;q.currentTick=10;q.targetObjectId=21;q.event=static_cast<ParasiteHostEventKind>(variant%7);
                q.ejectCellAvailable=(variant&1)!=0;q.ejectCell={8,9};q.stunTicks=3;
                if(variant==7)q.currentTick=-1;if(variant==8)q.targetObjectId=-1;if(variant==9)q.stunTicks=-1;
                if(variant==10)q.currentTick=11;if(variant==11)q.targetObjectId=22;
                const auto input=encode(q);outcome=encode(state.handleParasiteHostEvent(q,sink));require(encode(q)==input,"host request modified");
            } else if(operation==3){const int tick=std::array<int,7>{-1,0,9,10,11,60,INT_MAX}[variant%7];
                if constexpr(std::is_same_v<State,Original>)outcome=encode(state.tickParasites(objects,tick,sink));
                else outcome=encode(state.tickParasites(queries,tick,sink));
            } else if(operation==4){std::vector<int> active;switch(variant%5){case 0:active={7,8};break;case 1:active={7};break;case 2:active={8};break;case 3:active={8,7,7,-1};break;default:break;}
                const int tick=std::array<int,4>{-1,10,11,INT_MAX}[variant%4];
                if constexpr(std::is_same_v<State,Original>)outcome=encode(state.reconcileParasites(objects,active,tick,sink));
                else outcome=encode(state.reconcileParasites(queries,active,tick,sink));
            } else if(operation==5){const int id=std::array<int,7>{-1,11,12,21,22,999,0}[variant%7];std::vector<SpecialMechanismAction> actions(1);actions[0].owner="preexisting";
                std::string reason="original reason";auto* out=(variant&1)?&actions:nullptr;const int tick=variant%3?11:-1;
                if constexpr(std::is_same_v<State,Original>)state.onObjectRemoved(objects,id,tick,sink,reason,out);
                else state.onObjectRemoved(queries,id,tick,sink,reason,out);
                outcome=encode(actions)+encode(reason);
            } else if(operation==6){const int id=std::array<int,7>{-1,0,7,11,12,21,22}[variant%7];outcome=encode(state.isParasiteSourceLimboed(id))+encode(state.parasiteTargetForSource(id))+encode(state.isParasiteHostParalyzed(id));
            } else if(operation==7){state.reset();outcome=canonical();
            } else {State other;auto q=validLaunch(41,51,61);require(other.beginParasiteLaunch(q,{}).accepted,"swap seed");state.swap(other);
                outcome=encode(other.pendingParasiteLaunches())+encode(other.parasiteLinks())+encode(other.resolvedParasiteLaunches());}
        } catch(const Fault&){outcome="fault";}
        return outcome;
    }
};
struct Observation {std::vector<std::string> ports;bool fault=false,mutated=false,nested=false;};
Observation pair(int phase,int variation,int operation,int variant,int fail=-1,int mutation=-1,bool nested=false) {
    Side<Original> old;Side<RuntimeParasiteState> now;old.seed(phase,variation);now.seed(phase,variation);
    require(old.canonical()==now.canonical(),"initial complete three maps");require(encode(old.objects)==encode(now.objects),"initial complete world");
    const auto untouched=encode(now.objects);old.failAt=now.failAt=fail;old.mutateAt=now.mutateAt=mutation;old.nested=now.nested=nested;
    const auto a=old.run(operation,variant),b=now.run(operation,variant);const auto label=std::to_string(phase)+":"+std::to_string(variation)+":"+std::to_string(operation)+":"+std::to_string(variant);
    require(a==b,"result "+label);require(old.canonical()==now.canonical(),"complete state "+label);require(old.trace==now.trace,"partial state and port/event order "+label);
    require(encode(old.objects)==encode(now.objects),"all87 world fields "+label);require(old.hitFault==now.hitFault,"exception reached "+label);
    if(mutation<0)require(encode(now.objects)==untouched,"const world query authority "+label);
    require(old.mutated==now.mutated&&old.didNested==now.didNested,"live observation "+label);++states;
    if(now.hitFault){++faults;require(fail>0&&static_cast<std::size_t>(fail)<=now.ports.size(),"fault ordinal");faultPorts.insert(now.ports[fail-1]);}
    if(now.mutated)++mutations;if(now.didNested)++nestedRuns;
    return {now.ports,now.hitFault,now.mutated,now.didNested};
}
void capacities() {
    Original old;RuntimeParasiteState now;
    for(int i=0;i<4096;++i){const auto q=Side<Original>::validLaunch(i+1,i+10000,i+20000);
        const auto a=old.beginParasiteLaunch(q,{}),b=now.beginParasiteLaunch(q,{});require(encode(a)==encode(b)&&a.accepted,"capacity append");++capacityChecks;}
    const auto q=Side<Original>::validLaunch(5000,15000,25000);
    const auto a=old.beginParasiteLaunch(q,{}),b=now.beginParasiteLaunch(q,{});require(!a.accepted&&encode(a)==encode(b),"capacity4096 reject");++capacityChecks;
    require(encode(old.pendingParasiteLaunches())==encode(now.pendingParasiteLaunches()),"all4096 pending fields");
    for(int i=0;i<4096;++i){const auto hit=Side<Original>::validImpact(i+1,i+20000);
        const auto x=old.resolveParasiteImpact(hit,{}),y=now.resolveParasiteImpact(hit,{});require(x.accepted&&encode(x)==encode(y),"capacity attachment");++capacityChecks;}
    require(encode(old.pendingParasiteLaunches())==encode(now.pendingParasiteLaunches())&&encode(old.parasiteLinks())==encode(now.parasiteLinks())&&encode(old.resolvedParasiteLaunches())==encode(now.resolvedParasiteLaunches()),"all4096 linked and resolved fields");
    const auto later=Side<Original>::validLaunch(6000,16000,26000,11);require(encode(old.beginParasiteLaunch(later,{}))==encode(now.beginParasiteLaunch(later,{})),"resolved prune before validation/capacity");
    require(encode(old.resolvedParasiteLaunches())==encode(now.resolvedParasiteLaunches()),"allresolved pruned");
    const auto impact=Side<Original>::validImpact(6000,26000,11);require(encode(old.resolveParasiteImpact(impact,{}))==encode(now.resolveParasiteImpact(impact,{})),"full linkcapacity reject leaves pending");
    require(encode(old.pendingParasiteLaunches())==encode(now.pendingParasiteLaunches()),"capacity partial pending exact");capacityChecks+=3;
}
bool runContract() {
    try {
        verifyWorld();verifyValueEncoders();
        for(int phase=0;phase<7;++phase)for(int variation=0;variation<12;++variation)for(int operation=0;operation<9;++operation){
            const int variants=operation==0?28:operation==1?22:operation==2?14:operation==3?7:operation==4?20:operation==5?14:operation==6?7:1;
            for(int variant=0;variant<variants;++variant){const auto baseline=pair(phase,variation,operation,variant);
                for(int at=1;at<=static_cast<int>(baseline.ports.size());++at){const auto fault=pair(phase,variation,operation,variant,at);require(fault.fault,"every port fault reached");}
                if((phase==2||phase==4)&&variation==0&&(operation==3||operation==4||operation==5)&&!baseline.ports.empty()){
                    const auto alias=pair(phase,variation,operation,variant,-1,1);require(alias.mutated,"live world borrow mutation reached");}
                if(variation==0&&(operation==0||operation==1||operation==2)&&!baseline.ports.empty()){
                    const auto reentry=pair(phase,variation,operation,variant,-1,-1,true);if(!reentry.nested)require(baseline.ports.front().rfind("event:",0)!=0,"nested event not reached");}
            }
        }
        capacities();require(worldProbes==87&&valueProbes==112&&faults>1000&&faultPorts.size()>8&&mutations>50&&nestedRuns>10&&capacityChecks>8192,"complete coverage counters");
        std::cout<<"parasite-state checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" value-probes="<<valueProbes
            <<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" live-mutations="<<mutations<<" nested="<<nestedRuns<<" capacity-checks="<<capacityChecks
            <<" original-complete-state-event-action-order-and-partial-failure=exact failures=0\n";return true;
    } catch(const std::exception& e){activeCheckpoint={};std::cerr<<"parasite-state contract failed: "<<e.what()<<'\n';return false;}
}
}
