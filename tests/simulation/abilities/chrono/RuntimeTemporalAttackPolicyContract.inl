#include "simulation/abilities/chrono/RuntimeTemporalAttackPolicy.h"
#include "world/model/GameWorld.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/combat/targeting/RuntimeCombatDetails.h"
#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace temporal_attack_model_detail {
using namespace concord::simulation::abilities;
int checks=0,states=0,worldProbes=0,behaviorProbes=0,cursorProbes=0,recordProbes=0,eventProbes=0,profileProbes=0,faults=0;
void require(bool b,const std::string& s){++checks;if(!b)throw std::runtime_error(s);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (std::is_same_v<T,TemporalTargetState>) {return encode(v.targetObjectId)+encode(v.requiredProgress)+encode(v.frozen)+encode(v.progressByAttacker)+encode(v.lastAdvancedTick);}
    else if constexpr (std::is_same_v<T,SpecialMechanismEvent>) {return encode(v.kind)+encode(v.tick)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.value)+encode(v.cell)+encode(v.reason);}
    else if constexpr (requires {v.first;v.second;}) {return encode(v.first)+encode(v.second);}
    else if constexpr (requires { v.voxelDebris;v.parentActorId;v.deathWeaponRequestId; }) { return encode(v.voxelDebris)+encode(v.homingInitialized)+encode(v.homingYaw)+encode(v.homingPitch)+encode(v.actorId)+encode(v.parentActorId)+encode(v.generation)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.sourceAnchorObjectId)+encode(v.owner)+encode(v.fromCell)+encode(v.toCell)+encode(v.currentCell)+encode(v.hasSourcePosition)+encode(v.sourceX)+encode(v.sourceY)+encode(v.sourceZ)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasTargetPosition)+encode(v.targetX)+encode(v.targetY)+encode(v.targetZ)+encode(v.fromLevel)+encode(v.toLevel)+encode(v.currentLevel)+encode(v.attackerFacing)+encode(v.targetFacing)+encode(v.spawnTick)+encode(v.impactTick)+encode(v.elapsedTicks)+encode(v.damage)+encode(v.veteranDamageMultiplier)+encode(v.spawnedMissileDamage)+encode(v.spawnedMissileBaseDamage)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.lifecycle)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.warheadId)+encode(v.weaponAnimId)+encode(v.impactAnimId)+encode(v.trailerAnimId)+encode(v.trailerIntentId)+encode(v.warheadImpactIntentId)+encode(v.reportSoundId)+encode(v.arcing)+encode(v.vertical)+encode(v.acceleration)+encode(v.high)+encode(v.veryHigh)+encode(v.shadow)+encode(v.animates)+encode(v.aa)+encode(v.ag)+encode(v.cluster)+encode(v.bright)+encode(v.laser)+encode(v.houseColorLaser)+encode(v.electricBolt)+encode(v.alternateElectricColor)+encode(v.electricColor)+encode(v.shrapnel)+encode(v.shrapnelReleased)+encode(v.laserDuration)+encode(v.laserThickness)+encode(v.cellSpread)+encode(v.percentAtMax)+encode(v.affectsAllies)+encode(v.affectsEnemies)+encode(v.particleId)+encode(v.progress)+encode(v.currentX)+encode(v.currentY)+encode(v.currentZ)+encode(v.speedCellsPerTick)+encode(v.rot)+encode(v.deathWeaponCascadeId)+encode(v.deathWeaponRequestId)+encode(v.deathWeaponCascadeDepth)+encode(v.launchSourceKind)+encode(v.launchSourceType)+encode(v.launchSourceAuthoredAircraft)+encode(v.launchSourceValidated)+encode(v.airstrikeStrikeId)+encode(v.airstrikePlaneToken)+encode(v.particleDamagePulse)+encode(v.suppressWarheadParticleSpawn);}
    else if constexpr (requires { v.framesLeft;v.orientation;v.spin; }) {return encode(v.enabled)+encode(v.framesLeft)+encode(v.frameRemainder)+encode(v.frameCount)+encode(v.bounces)+encode(v.elasticity)+encode(v.position)+encode(v.velocity)+encode(v.orientation)+encode(v.spin);}
    else if constexpr (requires { v.cell;v.x;v.y;v.level; } && !requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.cell)+encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.lastTargetObjectId;v.radialSegment;v.sourceAnchorObjectId; }) {return encode(v.lastTargetObjectId)+encode(v.radialSegment)+encode(v.sourceAnchorObjectId);}
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
    else if constexpr(requires {v.has_value();v.reset();v.emplace();}) {if(v.has_value())v.reset();else v.emplace();}
    else if constexpr(requires {typename T::key_type;v.insert(1);v.clear();}) {if(v.empty())v.insert(1);else v.clear();}
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



struct State;
void touch(State&,const std::string&);
bool referenceAdvance(State&);bool actualAdvance(State&);
void profileStarted(State&,const void*);
void profileMoved(State&,const void*,const void*);
void profileFinished(State&,const void*);
State* activeState=nullptr;
enum class RuntimeWeaponSlot {None,Primary,Secondary,ElitePrimary,EliteSecondary,Deploy,EliteDeploy,Occupy,EliteOccupy,OpenTransport,Indexed,EliteIndexed};
struct ProfileValues {
 bool hasWeaponSet=false,requiresDeployment=false,deployed=false,occupied=false,openTransport=false,hasTurret=false;
 const WeaponType* weapon=nullptr;const ProjectileType* projectile=nullptr;const WarheadType* warhead=nullptr;
 RuntimeWeaponSlot slot=RuntimeWeaponSlot::None;
 std::string weaponId,projectileId,warheadId,selectionReason;
 int indexedWeaponIndex=-1,occupantMuzzleIndex=-1;
};
struct RuntimeWeaponProfile:ProfileValues {
 State* owner=nullptr;bool live=false;
 RuntimeWeaponProfile():owner(activeState),live(owner!=nullptr){if(live)profileStarted(*owner,this);}
 RuntimeWeaponProfile(const RuntimeWeaponProfile& x):ProfileValues(x),owner(x.owner),live(x.live){if(live)profileStarted(*owner,this);}
 RuntimeWeaponProfile(RuntimeWeaponProfile&& x) noexcept:ProfileValues(std::move(x)),owner(x.owner),live(x.live){if(live)profileMoved(*owner,&x,this);x.live=false;}
 ~RuntimeWeaponProfile(){if(live)profileFinished(*owner,this);}
};
template<class T>struct ObservedMap {
 State* owner=nullptr;std::map<int,T> rows;std::string label;
 auto find(int id){touch(*owner,label+":"+std::to_string(id));return rows.find(id);}
 auto end(){return rows.end();}
};
struct ObservedRules {
 State* owner=nullptr;RulesDatabase db;
 decltype(auto) getCombatDetailRules(){touch(*owner,"warp-distance");return (db.getCombatDetailRules());}
};
struct World {std::vector<WorldObjectInstance> objects;auto& accessObjects(){return objects;}};
enum class RuntimeObjectExitKind {TemporalDestroyed};
struct RuntimeObjectExitRequest {
 int objectId=-1;RuntimeObjectExitKind kind=RuntimeObjectExitKind::TemporalDestroyed;int tick=0;
 std::string mission,reason;int sourceObjectId=-1;std::string sourceOwner;bool suppressDeathWeapon=false;
};
struct Mechanisms {
 State* owner=nullptr;RuntimeTemporalState value;
 const auto& temporalTargets()const{return value.records();}
 bool beginTemporalTick(int,int);
 void clearTemporalAttacker(int,int,int,const SpecialEventSink&,const std::string&);
 bool applyTemporalHit(std::vector<WorldObjectInstance>&,ObservedRules&,int,int,int,int,int,
    const std::function<bool(const std::string&,const std::string&)>&,const SpecialEventSink&,bool&,std::string&,bool);
};
struct Parameters {
 bool canAct=true,deck=false,validPassenger=true,hasUnit=true,hasWeapon=true,hasWarhead=true,visible=true,line=true;
 double range=4.0,distance=1.0;
};
struct Input {int scenario=1,hz=15,damage=3,required=21,attackers=1,mutation=0,faultAt=0;bool force=false;};
struct State {
 World world;ObservedRules rules;ObservedMap<RuntimeObjectBehavior> behaviors;struct Combat{ObservedMap<RuntimeCombatFireCursor> fireCursors;}combatDetails;
 Mechanisms specialMechanisms;bool specialMechanismsInitialized=true;int tick=30;struct Config{int fixedTickHz=15;}config;
 struct Economy{std::string owner="B";}economy;int runtimeLastUnitLostSoundTick=-100;
 WeaponType weapon;WarheadType warhead;ProjectileType projectile;UnitType unit;ProfileValues authored;
 Parameters p;std::vector<SpecialMechanismEvent> events;std::vector<RuntimeObjectExitRequest> queued,drained;std::vector<std::string> sounds;
 std::vector<std::string> trace,ports;std::vector<int> appliedProgress;Input input;int portIndex=0,liveProfiles=0,profileStarts=0,profileEnds=0;
 const RuntimeWeaponProfile* profile=nullptr;std::vector<const RuntimeWeaponProfile*> profileStack;bool mutationFired=false;std::string lastProfile;bool referenceMode=false;std::vector<int> nestedResults;int maxProfileDepth=0;
 State(){rules.owner=this;behaviors.owner=this;behaviors.label="order";combatDetails.fireCursors.owner=this;combatDetails.fireCursors.label="cursor";specialMechanisms.owner=this;}
};
std::string profileDump(const ProfileValues& p){
 return encode(p.hasWeaponSet)+encode(p.requiresDeployment)+encode(p.deployed)+encode(p.occupied)+encode(p.openTransport)+encode(p.hasTurret)
 +encode(p.weapon?p.weapon->id:std::string("null"))+encode(p.projectile?p.projectile->id:std::string("null"))+encode(p.warhead?p.warhead->id:std::string("null"))
 +encode(p.slot)+encode(p.weaponId)+encode(p.projectileId)+encode(p.warheadId)+encode(p.selectionReason)+encode(p.indexedWeaponIndex)+encode(p.occupantMuzzleIndex);
}
std::string exitsDump(const std::vector<RuntimeObjectExitRequest>& rows){std::string r;for(const auto& v:rows)r+=encode(v.objectId)+encode(v.kind)+encode(v.tick)+encode(v.mission)+encode(v.reason)+encode(v.sourceObjectId)+encode(v.sourceOwner)+encode(v.suppressDeathWeapon);return r;}
std::string snapshot(const State& s){
 return encode(s.world.objects)+encode(s.behaviors.rows)+encode(s.combatDetails.fireCursors.rows)+encode(s.specialMechanisms.value.records())
 +encode(s.events)+exitsDump(s.queued)+exitsDump(s.drained)+encode(s.sounds)+encode(s.appliedProgress)+encode(s.specialMechanismsInitialized)
 +encode(s.tick)+encode(s.config.fixedTickHz)+encode(s.economy.owner)+encode(s.runtimeLastUnitLostSoundTick)
 +encode(s.weapon.damage)+encode(s.weapon.minimumRange)+encode(s.weapon.range)+encode(s.warhead.temporal)+encode(s.unit.opportunityFire)+encode(s.unit.noMovingFire)
 +encode(s.p.canAct)+encode(s.p.deck)+encode(s.p.validPassenger)+encode(s.p.hasUnit)+encode(s.p.hasWeapon)+encode(s.p.hasWarhead)+encode(s.p.visible)+encode(s.p.line)+encode(s.p.range)+encode(s.p.distance)
 +encode(s.liveProfiles)+encode(s.profileStarts)+encode(s.profileEnds)+(s.profile?profileDump(*s.profile):"no-profile")+encode(s.lastProfile)+encode(s.mutationFired)+encode(s.nestedResults)+encode(s.maxProfileDepth);
}
void profileStarted(State& s,const void* p){++s.liveProfiles;++s.profileStarts;s.profile=static_cast<const RuntimeWeaponProfile*>(p);s.profileStack.push_back(s.profile);s.maxProfileDepth=std::max(s.maxProfileDepth,s.liveProfiles);s.trace.push_back("profile-start="+snapshot(s));}
void profileMoved(State& s,const void*,const void* p){s.profile=static_cast<const RuntimeWeaponProfile*>(p);if(s.profileStack.empty())throw std::runtime_error("moved ownedprofile stack");s.profileStack.back()=s.profile;}
void profileFinished(State& s,const void* p){require(s.profile==p&&s.liveProfiles>=1&&!s.profileStack.empty()&&s.profileStack.back()==p,"owned-profile LIFO scope identity");s.lastProfile=profileDump(*static_cast<const RuntimeWeaponProfile*>(p));--s.liveProfiles;++s.profileEnds;s.profileStack.pop_back();s.profile=s.profileStack.empty()?nullptr:s.profileStack.back();s.trace.push_back("profile-end="+snapshot(s));}
void touch(State& s,const std::string& label){
 const int thisPort=++s.portIndex;s.ports.push_back(label);s.trace.push_back(label+".before="+snapshot(s));
 if(!s.mutationFired&&s.input.mutation>0){bool done=false;
  if((s.input.mutation==9&&label=="apply-hit")||(s.input.mutation==10&&label=="clear-attacker")){s.mutationFired=true;const bool nested=s.referenceMode?referenceAdvance(s):actualAdvance(s);s.nestedResults.push_back(nested?1:0);done=true;}
  if(s.input.mutation==1&&label=="visible"){s.weapon.damage=INT_MAX;done=true;}
  if(s.input.mutation==2&&label=="begin-tick"){++s.tick;s.config.fixedTickHz=60;done=true;}
  if(s.input.mutation==3&&label=="emit-unit-lost"){s.tick+=3;s.economy.owner="A";done=true;}
  if(s.input.mutation==4&&label=="profile-object"){s.weapon.minimumRange=2.0;s.warhead.temporal=false;done=true;}
  if(s.input.mutation==5&&label=="unit-type"){s.unit.opportunityFire=true;s.unit.noMovingFire=false;done=true;}
  if(s.input.mutation==6&&label=="apply-hit"){s.economy.owner="A";done=true;}
  if(s.input.mutation==7&&label=="drain-exits"){s.runtimeLastUnitLostSoundTick=s.tick;done=true;}
  if(s.input.mutation==8&&label=="event"){auto& m=const_cast<std::map<int,TemporalTargetState>&>(s.specialMechanisms.value.records());TemporalTargetState row;row.targetObjectId=999;row.requiredProgress=101;m.emplace(999,row);done=true;}
  if(done)s.mutationFired=true;
 }
 s.trace.push_back(label+".after="+snapshot(s));
 if(s.input.faultAt==thisPort)throw std::runtime_error("fault@"+label);
}
const WorldObjectInstance* findObjectById(State& s,int id){touch(s,"target:"+std::to_string(id));for(const auto& x:s.world.objects)if(x.instanceId==id&&x.alive)return &x;return nullptr;}
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(State& s,int id){touch(s,"source:"+std::to_string(id));for(const auto& x:s.world.objects)if(x.instanceId==id)return &x;return nullptr;}
bool runtimeCombatCanAct(State& s,const WorldObjectInstance& x){touch(s,"can-act:"+std::to_string(x.instanceId));return s.p.canAct;}
bool runtimeCarrierAircraftHasDeckProtection(State& s,const WorldObjectInstance&){touch(s,"deck-protected");return s.p.deck;}
bool runtimeOwnersAreAllied(State& s,const std::string& a,const std::string& b){touch(s,"allied");return a==b||((a=="A"&&b=="ALLY")||(a=="ALLY"&&b=="A"));}
bool runtimeCombatValidPassenger(const WorldObjectInstance& source,const WorldObjectInstance& host){touch(*activeState,"valid-passenger");return activeState->p.validPassenger&&source.garrisonHostObjectId==host.instanceId;}
const UnitType* findRuntimeUnitType(ObservedRules& r,const WorldObjectInstance&){touch(*r.owner,"unit-type");return r.owner->p.hasUnit?&r.owner->unit:nullptr;}
RuntimeWeaponProfile makeProfile(State& s,const std::string& port){touch(s,port);RuntimeWeaponProfile p;static_cast<ProfileValues&>(p)=s.authored;p.weapon=s.p.hasWeapon?&s.weapon:nullptr;p.warhead=s.p.hasWarhead?&s.warhead:nullptr;p.projectile=&s.projectile;return p;}
RuntimeWeaponProfile runtimeWeaponProfileForObject(State& s,const WorldObjectInstance&,const WorldObjectInstance*,bool force){auto p=makeProfile(s,"profile-object");p.occupied=force;return p;}
RuntimeWeaponProfile runtimeCombatPassengerWeaponProfile(State& s,const WorldObjectInstance&,const WorldObjectInstance*,bool force){auto p=makeProfile(s,"profile-passenger");p.openTransport=true;p.occupied=force;return p;}
WorldObjectInstance runtimeCombatFiringPose(const WorldObjectInstance& source,const WorldObjectInstance* host){touch(*activeState,"firing-pose");auto p=source;if(host){p.x=host->x;p.y=host->y;p.cell=host->cell;p.level=host->level;p.onBridge=host->onBridge;p.hasRenderPosition=host->hasRenderPosition;p.renderX=host->renderX;p.renderY=host->renderY;p.renderLevel=host->renderLevel;p.renderOnBridge=host->renderOnBridge;p.facing=host->facing;p.airborne=host->airborne;p.subCell=-1;p.alive=true;}return p;}
double runtimeEffectiveWeaponRange(State& s,const WorldObjectInstance&,const WorldObjectInstance&,const WeaponType&){touch(s,"effective-range");return s.p.range;}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& x){return {x.x,x.y};}
double runtimeCellDistanceToObjectFootprint(State& s,RuntimeCellPoint,const WorldObjectInstance&){touch(s,"cell-distance");return s.p.distance;}
double runtimeWeaponDistanceToTarget(State& s,const WorldObjectInstance&,const WorldObjectInstance&,const WeaponType&){touch(s,"weapon-distance");return s.p.distance;}
bool runtimeObjectVisibleToOwner(State& s,const std::string&,const WorldObjectInstance&){touch(s,"visible");return s.p.visible;}
bool runtimeWeaponHasLineOfSight(State& s,const WorldObjectInstance&,const WorldObjectInstance&,const WeaponType&){touch(s,"line-of-sight");return s.p.line;}
SpecialEventSink runtimeSpecialEventSink(State& s){touch(s,"event-sink");return [&s](const SpecialMechanismEvent& e){s.events.push_back(e);touch(s,"event");};}
void queueRuntimeObjectExit(State& s,RuntimeObjectExitRequest exit){touch(s,"queue-exit");s.queued.push_back(std::move(exit));}
bool drainRuntimeObjectExitQueue(State& s){touch(s,"drain-exits");const bool changed=!s.queued.empty();for(const auto& e:s.queued){for(auto& o:s.world.objects)if(o.instanceId==e.objectId){o.health=0;o.alive=false;o.mission=e.mission;break;}s.drained.push_back(e);}s.queued.clear();return changed;}
constexpr const char* runtimeSoundIntentUnitLost="unit-lost";
void emitRuntimeSound(State& s,const std::string& intent){touch(s,"emit-unit-lost");s.sounds.push_back(intent);}
struct HitQueries final:RuntimeTemporalStateHitQueries{
 State& s;explicit HitQueries(State& x):s(x){}
 WorldObjectInstance* findObject(int id)const override{for(auto& o:s.world.objects)if(o.instanceId==id)return &o;return nullptr;}
 bool combatSourceAvailable(const WorldObjectInstance* o)const override{return o&&(o->alive||o->garrisonHostObjectId>=0);}
 bool targetImmune(const WorldObjectInstance&)const override{return false;}
};
bool Mechanisms::beginTemporalTick(int id,int tick){touch(*owner,"begin-tick");return value.beginTemporalTick(id,tick);}
void Mechanisms::clearTemporalAttacker(int a,int t,int tick,const SpecialEventSink& sink,const std::string& reason){touch(*owner,"clear-attacker");value.clearTemporalAttacker(a,t,tick,sink,reason);}
bool Mechanisms::applyTemporalHit(std::vector<WorldObjectInstance>&,ObservedRules&,int a,int t,int p,int req,int tick,
 const std::function<bool(const std::string&,const std::string&)>& allied,const SpecialEventSink& sink,bool& completed,std::string& reason,bool force){
 touch(*owner,"apply-hit");owner->appliedProgress.push_back(p);HitQueries q(*owner);return value.applyTemporalHit(q,a,t,p,req,tick,allied,sink,completed,reason,force);
}

bool referenceAdvance(State& state) {
    if (state.specialMechanisms.temporalTargets().empty()) return false;
    // Work from IDs: completion and interruption mutate the relationship map.
    const auto targets = state.specialMechanisms.temporalTargets();
    const auto sink = runtimeSpecialEventSink(state);
    bool changed = false;
    bool playerObjectLost = false;
    for (const auto& [targetId, temporal] : targets) {
        const int previousTick = temporal.lastAdvancedTick < 0
            ? std::max(0, state.tick - 1) : temporal.lastAdvancedTick;
        if (!state.specialMechanisms.beginTemporalTick(targetId, state.tick)) continue;
        const auto frames = runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz)
            - runtimeRuleFrameAtTick(previousTick, state.config.fixedTickHz);
        for (const auto& [attackerId, unusedProgress] : temporal.progressByAttacker) {
            const auto* target = findObjectById(state, targetId);
            const auto* source = findRuntimeObjectByIdIncludingInactive(state, attackerId);
            const WorldObjectInstance* host = nullptr;
            const auto explicitOrder = state.behaviors.find(attackerId);
            const bool forceFire = explicitOrder != state.behaviors.end()
                && explicitOrder->second.kind == RuntimeBehaviorKind::Attack
                && explicitOrder->second.targetObjectId == targetId && explicitOrder->second.forceFire;
            bool valid = source && target && target->health > 0 && runtimeCombatCanAct(state, *source)
                && !runtimeCarrierAircraftHasDeckProtection(state, *target)
                && (forceFire || !runtimeOwnersAreAllied(state, source->owner, target->owner));
            if (valid && !source->alive) {
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                if (cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId)
                    host = findObjectById(state, cursor->second.sourceAnchorObjectId);
                valid = host && runtimeCombatValidPassenger(*source, *host) && runtimeCombatCanAct(state, *host);
            } else if (valid) {
                const auto order = state.behaviors.find(attackerId);
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                const auto* unit = findRuntimeUnitType(state.rules, *source);
                const bool independentMoveFire = unit && unit->opportunityFire && !unit->noMovingFire
                    && order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Move
                    && cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId;
                valid = order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
                    && order->second.targetObjectId == targetId;
                valid = valid || independentMoveFire;
            }
            const auto hostOrder = host == nullptr ? state.behaviors.end()
                : state.behaviors.find(host->instanceId);
            const bool profileForceFire = forceFire
                || (hostOrder != state.behaviors.end()
                    && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                    && hostOrder->second.targetObjectId == targetId
                    && hostOrder->second.forceFire);
            const auto profile = !valid ? RuntimeWeaponProfile{}
                : host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
                : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
            valid = valid && profile.weapon && profile.warhead && profile.warhead->temporal;
            if (valid) {
                const auto pose = runtimeCombatFiringPose(*source, host);
                const double range = host ? state.rules.getCombatDetailRules().openToppedWarpDistance
                    : runtimeEffectiveWeaponRange(state, pose, *target, *profile.weapon);
                const double distance = host
                    ? runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*host), *target)
                    : runtimeWeaponDistanceToTarget(state, pose, *target, *profile.weapon);
                valid = distance <= range && distance + 0.001 >= profile.weapon->minimumRange
                    && runtimeObjectVisibleToOwner(state, source->owner, *target)
                    && runtimeWeaponHasLineOfSight(state, pose, *target, *profile.weapon);
            }
            if (!valid) {
                state.specialMechanisms.clearTemporalAttacker(attackerId, targetId, state.tick,
                    sink, "temporal source or target no longer available");
                changed = true;
                continue;
            }
            if (frames <= 0) continue;
            // Temporal ignores armor verses, difficulty health and ordinary
            // firepower bonuses. ElitePrimary supplies its own authored Damage.
            const int progress = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), std::max(0, profile.weapon->damage) * frames));
            bool completed = false;
            std::string reason;
            changed = state.specialMechanisms.applyTemporalHit(state.world.accessObjects(), state.rules,
                attackerId, targetId, progress, temporal.requiredProgress, state.tick,
                [&state](const std::string& a, const std::string& b) { return runtimeOwnersAreAllied(state, a, b); },
                sink, completed, reason, forceFire) || changed;
            if (completed) {
                playerObjectLost = playerObjectLost || (target->kind != WorldObjectKind::Structure && target->owner == state.economy.owner);
                RuntimeObjectExitRequest exit;
                exit.objectId = targetId;
                exit.kind = RuntimeObjectExitKind::TemporalDestroyed;
                exit.tick = state.tick;
                exit.mission = "TemporalRemoved";
                exit.reason = "temporal continuous erasure complete";
                exit.sourceObjectId = attackerId;
                exit.sourceOwner = source->owner;
                exit.suppressDeathWeapon = true;
                queueRuntimeObjectExit(state, std::move(exit));
                break;
            }
        }
    }
    changed = drainRuntimeObjectExitQueue(state) || changed;
    if (playerObjectLost && state.tick - state.runtimeLastUnitLostSoundTick
            >= std::max(1, state.config.fixedTickHz * 2)) {
        emitRuntimeSound(state, runtimeSoundIntentUnitLost);
        state.runtimeLastUnitLostSoundTick = state.tick;
    }
    return changed;
}

void referenceClear(
    State& state,
    int attackerObjectId,
    const RuntimeObjectBehavior& behavior,
    const std::string& reason) {
    if (!state.specialMechanismsInitialized
        || behavior.kind != RuntimeBehaviorKind::Attack
        || behavior.targetObjectId < 0) {
        return;
    }
    state.specialMechanisms.clearTemporalAttacker(
        attackerObjectId,
        behavior.targetObjectId,
        state.tick,
        runtimeSpecialEventSink(state),
        reason
    );
}

namespace {
class SessionTemporalAttackQueries final : public concord::simulation::abilities::RuntimeTemporalAttackQueries {
public:
    explicit SessionTemporalAttackQueries(State& value) : state(value) {}
    const WorldObjectInstance* target(int id) const override { return findObjectById(state,id); }
    const WorldObjectInstance* sourceIncludingInactive(int id) const override { return findRuntimeObjectByIdIncludingInactive(state,id); }
    const RuntimeObjectBehavior* order(int id) const override {
        const auto found=state.behaviors.find(id);return found==state.behaviors.end()?nullptr:&found->second;
    }
    const RuntimeCombatFireCursor* cursor(int id) const override {
        const auto found=state.combatDetails.fireCursors.find(id);return found==state.combatDetails.fireCursors.end()?nullptr:&found->second;
    }
    bool canAct(const WorldObjectInstance& object) const override { return runtimeCombatCanAct(state,object); }
    bool deckProtected(const WorldObjectInstance& object) const override { return runtimeCarrierAircraftHasDeckProtection(state,object); }
    bool allied(const std::string& a,const std::string& b) const override { return runtimeOwnersAreAllied(state,a,b); }
    bool validPassenger(const WorldObjectInstance& source,const WorldObjectInstance& host) const override { return runtimeCombatValidPassenger(source,host); }
    const UnitType* unitType(const WorldObjectInstance& source) const override { return findRuntimeUnitType(state.rules,source); }
    concord::simulation::abilities::RuntimeTemporalAttackWeaponView selectWeapon(bool valid,
        const WorldObjectInstance* source,const WorldObjectInstance* target,
        const WorldObjectInstance* host,bool forceFire) override {
        profile.emplace(!valid ? RuntimeWeaponProfile{}
            : host ? runtimeCombatPassengerWeaponProfile(state,*source,target,forceFire)
            : runtimeWeaponProfileForObject(state,*source,target,forceFire));
        return {profile->weapon,profile->warhead};
    }
    void finishWeaponScope() noexcept override { profile.reset(); }
    WorldObjectInstance firingPose(const WorldObjectInstance& source,const WorldObjectInstance* host) const override { return runtimeCombatFiringPose(source,host); }
    double openToppedWarpDistance() const override { return state.rules.getCombatDetailRules().openToppedWarpDistance; }
    double effectiveWeaponRange(const WorldObjectInstance& pose,const WorldObjectInstance& target,const WeaponType& weapon) const override { return runtimeEffectiveWeaponRange(state,pose,target,weapon); }
    double cellDistance(const WorldObjectInstance& host,const WorldObjectInstance& target) const override { return runtimeCellDistanceToObjectFootprint(state,runtimeCellPointForObject(host),target); }
    double weaponDistance(const WorldObjectInstance& pose,const WorldObjectInstance& target,const WeaponType& weapon) const override { return runtimeWeaponDistanceToTarget(state,pose,target,weapon); }
    bool visible(const std::string& owner,const WorldObjectInstance& target) const override { return runtimeObjectVisibleToOwner(state,owner,target); }
    bool lineOfSight(const WorldObjectInstance& pose,const WorldObjectInstance& target,const WeaponType& weapon) const override { return runtimeWeaponHasLineOfSight(state,pose,target,weapon); }
private:
    State& state;
    std::optional<RuntimeWeaponProfile> profile;
};
class SessionTemporalAttackEffects final : public concord::simulation::abilities::RuntimeTemporalAttackEffects {
public:
    explicit SessionTemporalAttackEffects(State& value) : state(value) {}
    SpecialEventSink eventSink() override { return runtimeSpecialEventSink(state); }
    bool beginTick(int targetId,int tick) override { return state.specialMechanisms.beginTemporalTick(targetId,tick); }
    void clearAttacker(int attackerId,int targetId,int tick,const SpecialEventSink& sink,const std::string& reason) override {
        state.specialMechanisms.clearTemporalAttacker(attackerId,targetId,tick,sink,reason);
    }
    bool applyHit(int attackerId,int targetId,int progress,int required,int tick,
        const SpecialEventSink& sink,bool& completed,std::string& reason,bool forceFire) override {
        return state.specialMechanisms.applyTemporalHit(state.world.accessObjects(),state.rules,
            attackerId,targetId,progress,required,tick,
            [this](const std::string& a,const std::string& b){return runtimeOwnersAreAllied(state,a,b);},
            sink,completed,reason,forceFire);
    }
    void queueTemporalExit(int targetId,int attackerId,const WorldObjectInstance& source) override {
        RuntimeObjectExitRequest exit;
        exit.objectId=targetId;
        exit.kind=RuntimeObjectExitKind::TemporalDestroyed;
        exit.tick=state.tick;
        exit.mission="TemporalRemoved";
        exit.reason="temporal continuous erasure complete";
        exit.sourceObjectId=attackerId;
        exit.sourceOwner=source.owner;
        exit.suppressDeathWeapon=true;
        queueRuntimeObjectExit(state,std::move(exit));
    }
    bool drainExitQueue() override { return drainRuntimeObjectExitQueue(state); }
    void emitUnitLostSound() override { emitRuntimeSound(state,runtimeSoundIntentUnitLost); }
private:
    State& state;
};
}
bool actualAdvance(State& state) {
    SessionTemporalAttackQueries queries(state);SessionTemporalAttackEffects effects(state);
    return concord::simulation::abilities::advanceRuntimeTemporalAttacks(
        {state.specialMechanisms.temporalTargets(),state.tick,state.config.fixedTickHz,
            state.economy.owner,state.runtimeLastUnitLostSoundTick},queries,effects);
}

void actualClear(State& state,int attackerObjectId,
    const RuntimeObjectBehavior& behavior,const std::string& reason) {
    SessionTemporalAttackEffects effects(state);
    concord::simulation::abilities::clearRuntimeTemporalAttack(state.specialMechanismsInitialized,
        attackerObjectId,behavior,state.tick,effects,reason);
}


std::unique_ptr<State> makeState(const Input& in){
 auto s=std::make_unique<State>();s->input=in;s->config.fixedTickHz=in.hz;s->weapon.id="W|main";s->weapon.damage=in.damage;s->weapon.minimumRange=0.0;s->weapon.range=4.0;s->warhead.id="WH|main";s->warhead.temporal=true;s->projectile.id="P|main";s->unit.opportunityFire=false;s->unit.noMovingFire=false;
 s->authored.hasWeaponSet=true;s->authored.requiresDeployment=true;s->authored.deployed=true;s->authored.hasTurret=true;s->authored.slot=RuntimeWeaponSlot::ElitePrimary;s->authored.weaponId="owned-weapon";s->authored.projectileId="owned-projectile";s->authored.warheadId="owned-warhead";s->authored.selectionReason="owned-selection-string-long-enough-to-leave-small-string-storage";s->authored.indexedWeaponIndex=17;s->authored.occupantMuzzleIndex=4;
 auto make=[](int id,const std::string& owner){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.type="BASIC";o.owner=owner;o.health=100;o.alive=true;o.x=id+4;o.y=9-id;o.level=id%3;o.cell=o.y*512+o.x;o.mission="Attack";o.tag="probe|%";o.facing=id*7;o.turretFacing=id*11;o.renderX=double(id)+0.25;o.renderY=double(id)-0.125;o.subCell=id%4;return o;};
 s->world.objects={make(1,"A"),make(2,"B"),make(3,"A"),make(4,"A"),make(5,"B"),make(6,"A")};
 for(int id:{1,3,6,4}){RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Attack;b.state=RuntimeBehaviorState::Attacking;b.targetObjectId=2;b.forceFire=in.force;b.moveTarget={5,6};b.path={{1,2},{3,4}};b.pendingIgnoredObjectIds={7,9};b.renderFromOffsetX=-0.125;b.renderFromOffsetY=0.375;b.renderArrivalDistanceAfter=0.75;s->behaviors.rows.emplace(id,b);s->combatDetails.fireCursors.rows[id]={2,5,4};}
 TemporalTargetState t;t.targetObjectId=2;t.requiredProgress=in.required;t.frozen=true;t.lastAdvancedTick=s->tick-1;t.progressByAttacker[1]=2;if(in.attackers>1)t.progressByAttacker[3]=3;if(in.attackers>2)t.progressByAttacker[6]=4;
 auto& records=const_cast<std::map<int,TemporalTargetState>&>(s->specialMechanisms.value.records());records.emplace(2,t);
 auto& src=s->world.objects[0];auto& target=s->world.objects[1];auto passenger=[&]{src.alive=false;src.garrisonHostObjectId=4;src.mission="InsideTransport";s->world.objects[3].transportPassengerIds={1};};
 switch(in.scenario){
 case 0:records.clear();break;
 case 2:target.health=0;break;
 case 3:target.instanceId=2002;break;
 case 4:src.instanceId=1001;break;
 case 5:s->p.canAct=false;break;
 case 6:s->p.deck=true;break;
 case 7:target.owner="A";break;
 case 8:passenger();break;
 case 9:passenger();s->combatDetails.fireCursors.rows[1].lastTargetObjectId=9;break;
 case 10:passenger();s->p.validPassenger=false;break;
 case 11:s->behaviors.rows[1].kind=RuntimeBehaviorKind::Move;s->unit.opportunityFire=true;break;
 case 12:s->behaviors.rows[1].kind=RuntimeBehaviorKind::None;break;
 case 13:s->p.hasWeapon=false;break;
 case 14:s->p.hasWarhead=false;break;
 case 15:s->warhead.temporal=false;break;
 case 16:s->p.range=0.5;break;
 case 17:s->weapon.minimumRange=2.0;break;
 case 18:s->p.visible=false;break;
 case 19:s->p.line=false;break;
 case 20:s->tick=1;s->config.fixedTickHz=1000;records.at(2).lastAdvancedTick=0;break;
 case 21:records.at(2).lastAdvancedTick=s->tick;break;
 case 22:s->tick=0;records.at(2).lastAdvancedTick=-1;break;
 case 23:t.targetObjectId=5;t.progressByAttacker={{1,2},{3,3}};records.emplace(5,t);break;
 case 24:s->p.hasUnit=false;s->behaviors.rows[1].kind=RuntimeBehaviorKind::Move;break;
 case 25:s->p.distance=std::numeric_limits<double>::quiet_NaN();break;
 case 26:s->weapon.damage=-3;break;
 case 27:records.at(2).requiredProgress=1;break;
 case 28:records.at(2).requiredProgress=1;target.kind=WorldObjectKind::Structure;break;
 case 29:records.at(2).requiredProgress=1;s->runtimeLastUnitLostSoundTick=s->tick;break;
 case 30:passenger();s->behaviors.rows[1].forceFire=false;s->behaviors.rows[4].forceFire=true;break;
 case 31:s->weapon.minimumRange=1.001;break;
 case 32:src.kind=WorldObjectKind::Infantry;break;
 case 33:target.alive=false;break;
 case 34:passenger();s->world.objects[3].alive=false;break;
 case 35:t.targetObjectId=5;t.requiredProgress=1;t.progressByAttacker={{1,2},{3,3}};records.emplace(5,t);records.at(2).requiredProgress=1;break;
 }
 if(in.mutation==10)s->p.hasWeapon=false;
 return s;
}
struct Output {std::string state,exception;bool result=false;std::vector<std::string> trace,ports;int starts=0,ends=0;bool mutationReached=false;int maxDepth=0;};
struct ActiveGuard{State* previous;explicit ActiveGuard(State& s):previous(activeState){activeState=&s;}~ActiveGuard(){activeState=previous;}};
Output execute(const Input& input,bool reference){
 auto state=makeState(input);state->referenceMode=reference;Output out;
 {ActiveGuard active(*state);try{out.result=reference?referenceAdvance(*state):actualAdvance(*state);}catch(const std::exception& e){out.exception=e.what();}}
 require(state->liveProfiles==0&&state->profile==nullptr,"profile must be destroyed on every return and exception");require(state->profileStarts==state->profileEnds,"owned profile start/end pair");
 out.state=snapshot(*state);out.trace=std::move(state->trace);out.ports=std::move(state->ports);out.starts=state->profileStarts;out.ends=state->profileEnds;out.mutationReached=state->mutationFired;out.maxDepth=state->maxProfileDepth;return out;
}
int positives=0,negatives=0,pairedProfiles=0,nestedRuns=0,nestedDepth=0;std::set<std::string> faultPortLabels;std::set<int> reachedMutations;
Output compare(const Input& in){
 const auto a=execute(in,true),b=execute(in,false);++states;
 if(a.state!=b.state||a.trace!=b.trace||a.ports!=b.ports||a.result!=b.result||a.exception!=b.exception){
  std::size_t at=0;while(at<a.trace.size()&&at<b.trace.size()&&a.trace[at]==b.trace[at])++at;
  throw std::runtime_error("advance mismatch scenario="+std::to_string(in.scenario)+" hz="+std::to_string(in.hz)+" damage="+std::to_string(in.damage)+" required="+std::to_string(in.required)+" attackers="+std::to_string(in.attackers)+" force="+std::to_string(in.force)+" mutation="+std::to_string(in.mutation)+" fault="+std::to_string(in.faultAt)+" trace="+std::to_string(at)+"/"+std::to_string(a.trace.size())+"/"+std::to_string(b.trace.size())+" oldException="+a.exception+" newException="+b.exception);
 }
 require(a.state==b.state,"complete state");require(a.trace==b.trace,"every complete callback snapshot and lifetime");require(a.ports==b.ports,"ordered original ports");require(a.result==b.result&&a.exception==b.exception,"result and failure point");
 pairedProfiles+=a.starts;if(a.result)++positives;else ++negatives;
 if(in.mutation&&a.mutationReached)reachedMutations.insert(in.mutation);
 if(!a.exception.empty()){}else if(!a.ports.empty()&&a.maxDepth>=2){++nestedRuns;nestedDepth=std::max(nestedDepth,a.maxDepth);}
 return a;
}
void verifyOtherFields(){
 RuntimeObjectBehavior behavior;const auto b=encode(behavior);
{auto x=behavior;mutateEncoderField(x.kind);require(encode(x)!=b,"behavior kind");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.state);require(encode(x)!=b,"behavior state");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.moveTarget);require(encode(x)!=b,"behavior moveTarget");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.targetObjectId);require(encode(x)!=b,"behavior targetObjectId");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.autoAcquired);require(encode(x)!=b,"behavior autoAcquired");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.forceFire);require(encode(x)!=b,"behavior forceFire");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.attackGround);require(encode(x)!=b,"behavior attackGround");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.attackBridge);require(encode(x)!=b,"behavior attackBridge");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.attackBridgeCell);require(encode(x)!=b,"behavior attackBridgeCell");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.path);require(encode(x)!=b,"behavior path");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pathRequestPending);require(encode(x)!=b,"behavior pathRequestPending");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingPathTowardRange);require(encode(x)!=b,"behavior pendingPathTowardRange");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingPathStart);require(encode(x)!=b,"behavior pendingPathStart");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingPathGoal);require(encode(x)!=b,"behavior pendingPathGoal");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingHasSharedGoal);require(encode(x)!=b,"behavior pendingHasSharedGoal");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingSharedGoal);require(encode(x)!=b,"behavior pendingSharedGoal");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingAllowedOccupiedObjectId);require(encode(x)!=b,"behavior pendingAllowedOccupiedObjectId");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingTargetObjectId);require(encode(x)!=b,"behavior pendingTargetObjectId");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingRangeCells);require(encode(x)!=b,"behavior pendingRangeCells");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingAllowNearbyTarget);require(encode(x)!=b,"behavior pendingAllowNearbyTarget");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pendingIgnoredObjectIds);require(encode(x)!=b,"behavior pendingIgnoredObjectIds");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.repackMoveGroup);require(encode(x)!=b,"behavior repackMoveGroup");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.moveCooldownTicks);require(encode(x)!=b,"behavior moveCooldownTicks");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.moveSegmentFrom);require(encode(x)!=b,"behavior moveSegmentFrom");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.moveIntervalRemainderQ16);require(encode(x)!=b,"behavior moveIntervalRemainderQ16");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.moveStartupDistanceQ16);require(encode(x)!=b,"behavior moveStartupDistanceQ16");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderStartupAccelerationDistance);require(encode(x)!=b,"behavior renderStartupAccelerationDistance");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.factoryExitClearanceHostId);require(encode(x)!=b,"behavior factoryExitClearanceHostId");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.transportTransferHostId);require(encode(x)!=b,"behavior transportTransferHostId");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.transportTransferKind);require(encode(x)!=b,"behavior transportTransferKind");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.weaponCooldownTicks);require(encode(x)!=b,"behavior weaponCooldownTicks");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.blockedTicks);require(encode(x)!=b,"behavior blockedTicks");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.pathReplans);require(encode(x)!=b,"behavior pathReplans");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.nextRepathTick);require(encode(x)!=b,"behavior nextRepathTick");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.returnToDeployed);require(encode(x)!=b,"behavior returnToDeployed");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.approachSlotIndex);require(encode(x)!=b,"behavior approachSlotIndex");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderFrom);require(encode(x)!=b,"behavior renderFrom");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderTo);require(encode(x)!=b,"behavior renderTo");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderFromOffsetX);require(encode(x)!=b,"behavior renderFromOffsetX");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderFromOffsetY);require(encode(x)!=b,"behavior renderFromOffsetY");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderToOffsetX);require(encode(x)!=b,"behavior renderToOffsetX");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderToOffsetY);require(encode(x)!=b,"behavior renderToOffsetY");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderFromLevel);require(encode(x)!=b,"behavior renderFromLevel");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderToLevel);require(encode(x)!=b,"behavior renderToLevel");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderFromOnBridge);require(encode(x)!=b,"behavior renderFromOnBridge");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderToOnBridge);require(encode(x)!=b,"behavior renderToOnBridge");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderMoveTicksTotal);require(encode(x)!=b,"behavior renderMoveTicksTotal");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderMoveTicksRemaining);require(encode(x)!=b,"behavior renderMoveTicksRemaining");++behaviorProbes;}
{auto x=behavior;mutateEncoderField(x.renderArrivalDistanceAfter);require(encode(x)!=b,"behavior renderArrivalDistanceAfter");++behaviorProbes;}
 RuntimeCombatFireCursor cursor;const auto c=encode(cursor);
 #define CURSOR_PROBE(f) {auto x=cursor;mutateEncoderField(x.f);require(encode(x)!=c,"cursor " #f);++cursorProbes;}
 CURSOR_PROBE(lastTargetObjectId) CURSOR_PROBE(radialSegment) CURSOR_PROBE(sourceAnchorObjectId)
 #undef CURSOR_PROBE
 TemporalTargetState record;const auto t=encode(record);
 #define RECORD_PROBE(f) {auto x=record;mutateEncoderField(x.f);require(encode(x)!=t,"temporal " #f);++recordProbes;}
 RECORD_PROBE(targetObjectId) RECORD_PROBE(requiredProgress) RECORD_PROBE(frozen) RECORD_PROBE(progressByAttacker) RECORD_PROBE(lastAdvancedTick)
 #undef RECORD_PROBE
 SpecialMechanismEvent event;const auto e=encode(event);
 #define EVENT_PROBE(f) {auto x=event;mutateEncoderField(x.f);require(encode(x)!=e,"event " #f);++eventProbes;}
 EVENT_PROBE(kind) EVENT_PROBE(tick) EVENT_PROBE(objectId) EVENT_PROBE(targetObjectId) EVENT_PROBE(value) EVENT_PROBE(cell) EVENT_PROBE(reason)
 #undef EVENT_PROBE
 ProfileValues profile;const auto p=profileDump(profile);
 #define PROFILE_PROBE(f) {auto x=profile;mutateEncoderField(x.f);require(profileDump(x)!=p,"profile " #f);++profileProbes;}
 PROFILE_PROBE(hasWeaponSet) PROFILE_PROBE(requiresDeployment) PROFILE_PROBE(deployed) PROFILE_PROBE(occupied) PROFILE_PROBE(openTransport) PROFILE_PROBE(hasTurret)
 PROFILE_PROBE(slot) PROFILE_PROBE(weaponId) PROFILE_PROBE(projectileId) PROFILE_PROBE(warheadId) PROFILE_PROBE(selectionReason) PROFILE_PROBE(indexedWeaponIndex) PROFILE_PROBE(occupantMuzzleIndex)
 #undef PROFILE_PROBE
 WeaponType w;w.id="W";ProjectileType pr;pr.id="P";WarheadType wh;wh.id="WH";
 {auto x=profile;x.weapon=&w;require(profileDump(x)!=p,"profile weapon");++profileProbes;}
 {auto x=profile;x.projectile=&pr;require(profileDump(x)!=p,"profile projectile");++profileProbes;}
 {auto x=profile;x.warhead=&wh;require(profileDump(x)!=p,"profile warhead");++profileProbes;}
 require(worldProbes==87&&behaviorProbes==49&&cursorProbes==3&&recordProbes==5&&eventProbes==7&&profileProbes==16,"every complete field counted");
}
void verifyClear(){
 for(bool initialized:{false,true})for(auto kind:{RuntimeBehaviorKind::None,RuntimeBehaviorKind::Move,RuntimeBehaviorKind::Attack})for(int target:{-1,2,99})for(int attacker:{1,3})for(int alias:{0,1,2}){
  Input input;input.attackers=3;auto run=[&](bool reference,int fault=0){auto s=makeState(input);s->input.faultAt=fault;s->specialMechanismsInitialized=initialized;RuntimeObjectBehavior b;b.kind=kind;b.targetObjectId=target;
   const std::string& reason=alias==1?s->economy.owner:alias==2?s->world.objects[1].mission:std::string("clear-original-reason");std::string failure;
   {ActiveGuard active(*s);try{if(reference)referenceClear(*s,attacker,b,reason);else actualClear(*s,attacker,b,reason);}catch(const std::exception& ex){failure=ex.what();}}
   return Output{snapshot(*s),failure,false,s->trace,s->ports,s->profileStarts,s->profileEnds};};
  const auto a=run(true),b=run(false);++states;require(a.state==b.state&&a.trace==b.trace&&a.ports==b.ports&&a.exception==b.exception,"complete clear guard/reason alias/state/event");
  for(int at=1;at<=static_cast<int>(a.ports.size());++at){const auto c=run(true,at),d=run(false,at);++states;require(c.state==d.state&&c.trace==d.trace&&c.ports==d.ports&&c.exception==d.exception,"complete clear injectedfailure snapshot/order/alias");require(c.exception.starts_with("fault@"),"clear fault reaches exact original port");++faults;faultPortLabels.insert(a.ports[at-1]);}
 }
}
bool runContract(){
 try{
  verifyWorld();verifyOtherFields();
  int corpus=0;
  for(int scenario=0;scenario<36;++scenario)for(int hz:{15,30,60})for(int damage:{0,7,INT_MAX})for(int required:{1,31,INT_MAX})for(int attackers:{1,3})for(bool force:{false,true}){Input in;in.scenario=scenario;in.hz=hz;in.damage=damage;in.required=required;in.attackers=attackers;in.force=force;compare(in);++corpus;}
  require(corpus==3888,"complete fixed matrix3888");
  for(int scenario:{1,8,11,20,27,30,35})for(int mutation=1;mutation<=10;++mutation)for(int hz:{15,30,60}){Input in;in.scenario=scenario;in.hz=hz;in.damage=7;in.required=21;in.attackers=3;in.mutation=mutation;compare(in);}
  for(int scenario:{1,8,11,13,16,18,20,27,35}){
   Input in;in.scenario=scenario;in.damage=7;in.required=scenario==27?1:21;in.attackers=3;auto normal=compare(in);
   for(int at=1;at<=static_cast<int>(normal.ports.size());++at){in.faultAt=at;auto failed=compare(in);require(failed.exception.starts_with("fault@"),"fault must reach exact original port");++faults;faultPortLabels.insert(normal.ports[at-1]);}
  }
  for(int mutation:{9,10}){Input in;in.scenario=23;in.damage=7;in.required=21;in.attackers=3;in.mutation=mutation;auto normal=compare(in);for(int at=1;at<=static_cast<int>(normal.ports.size());++at){in.faultAt=at;auto failed=compare(in);require(failed.exception.starts_with("fault@"),"nested fault reaches exact port");++faults;faultPortLabels.insert(normal.ports[at-1]);}}
  verifyClear();require(positives>0&&negatives>0&&pairedProfiles>0&&faults>0,"all outcomes,owned lifetimes,andfaults reached");require(reachedMutations.size()==10&&nestedRuns>0&&nestedDepth>=2,"all10live mutations andtrue nested owned-profile lifetime reached");
  std::cout<<"temporal-attack checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" behavior-probes="<<behaviorProbes<<" cursor-probes="<<cursorProbes<<" record-probes="<<recordProbes<<" event-probes="<<eventProbes<<" profile-probes="<<profileProbes<<" nested-runs="<<nestedRuns<<" max-depth="<<nestedDepth<<" mutations="<<reachedMutations.size()<<" faults="<<faults<<" fault-ports="<<faultPortLabels.size()<<" owned-profile-pairs="<<pairedProfiles<<" full-original105-18-state-trace-profile-lifetime=exact failures=0\n";return true;
 }catch(const std::exception& e){activeState=nullptr;std::cerr<<"temporal-attack FAIL "<<e.what()<<'\n';return false;}
}
} // namespace temporal_attack_model_detail
