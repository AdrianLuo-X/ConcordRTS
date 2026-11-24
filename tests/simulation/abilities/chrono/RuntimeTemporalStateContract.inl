#include "simulation/abilities/chrono/RuntimeTemporalState.h"
#include "world/model/GameWorld.h"
#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/buildings/RuntimeStructureRepack.h"
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
#include <vector>
namespace temporal_state_model_detail {
int checks=0,states=0,worldProbes=0,recordProbes=0,eventProbes=0,faults=0;
int positives=0,negatives=0,progressEvents=0,completeEvents=0,interruptEvents=0,clearEvents=0;
std::set<std::string> faultPorts;
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


using SpecialAlliancePredicate=std::function<bool(const std::string&,const std::string&)>;
std::function<void(const std::string&)> activeCheckpoint;
void checkpoint(const std::string& s){if(activeCheckpoint)activeCheckpoint(s);}
const UnitType* unitTypeFor(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (object.kind == WorldObjectKind::Infantry) {
        return rules.findUnitType("Infantry", object.type);
    }
    if (object.kind == WorldObjectKind::Structure) {
        return nullptr;
    }
    return object.airborne
        ? rules.findUnitType("Aircraft", object.type)
        : rules.findUnitType("Vehicle", object.type);
}

const BuildingType* buildingTypeFor(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    return object.kind == WorldObjectKind::Structure
        ? rules.findBuildingType(object.type)
        : nullptr;
}


bool rawTargetImmuneToTemporal(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (const BuildingType* building = buildingTypeFor(rules, object)) {
        return building->immuneToTemporal;
    }
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->immuneToTemporal;
}


bool targetImmuneToTemporal(const RulesDatabase& r,const WorldObjectInstance& o){checkpoint("immune:"+std::to_string(o.instanceId));return rawTargetImmuneToTemporal(r,o);}
bool rawSpecialCombatSourceAvailable(const std::vector<WorldObjectInstance>& objects,
    const WorldObjectInstance* source, const RulesDatabase* rules = nullptr,
    const SpecialAlliancePredicate* allied = nullptr) {
    if (!source || source->health <= 0) return false;
    if (source->alive) return true;
    if (source->mission != "InsideTransport") return false;
    return std::any_of(objects.begin(), objects.end(), [&](const auto& host) {
        if (!host.alive || host.health <= 0
            || std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(),
                source->instanceId) == host.transportPassengerIds.end()) return false;
        if (rules && host.owner != source->owner
            && (!allied || !*allied || !(*allied)(host.owner, source->owner))) return false;
        const auto* type = rules ? unitTypeFor(*rules, host) : nullptr;
        return !rules || (type && type->openTopped);
    });
}


bool specialCombatSourceAvailable(const std::vector<WorldObjectInstance>& objects,const WorldObjectInstance* source,const RulesDatabase* rules=nullptr,const SpecialAlliancePredicate* allied=nullptr){checkpoint("source:"+std::to_string(source?source->instanceId:-1));return rawSpecialCombatSourceAvailable(objects,source,rules,allied);}
struct Reference {
 std::map<int,TemporalTargetState> temporalByTarget_;
 const auto& records()const{return temporalByTarget_;}
 void reset(){temporalByTarget_.clear();}
 void swap(Reference& o)noexcept{temporalByTarget_.swap(o.temporalByTarget_);}
 bool empty()const{return temporalByTarget_.empty();}
 static WorldObjectInstance* findObject(std::vector<WorldObjectInstance>& objects,int id){checkpoint("find:"+std::to_string(id));for(auto& o:objects)if(o.instanceId==id)return &o;return nullptr;}
 bool applyTemporalHit(std::vector<WorldObjectInstance>& objects,const RulesDatabase& rules,int attackerObjectId,int targetObjectId,int progress,int requiredProgress,int tick,const SpecialAlliancePredicate& areAllied,const SpecialEventSink& emit,bool& completed,std::string& reason,bool allowAlliedTarget=false);
 bool isTemporalFrozen(int objectId)const;
 bool hasTemporalAuthorityForObject(int objectId)const;
 void clearTemporalAttacker(int attackerObjectId,int targetObjectId,int tick,const SpecialEventSink& emit,const std::string& reason);
 void clearTemporalTarget(int targetObjectId,int tick,const SpecialEventSink& emit,const std::string& reason);
 bool hasTemporalAttacker(int attackerObjectId,int targetObjectId)const{const auto target=temporalByTarget_.find(targetObjectId);return target!=temporalByTarget_.end()&&target->second.progressByAttacker.contains(attackerObjectId);}
 bool beginTemporalTick(int targetObjectId,int tick){const auto target=temporalByTarget_.find(targetObjectId);if(target==temporalByTarget_.end()||tick<=target->second.lastAdvancedTick)return false;target->second.lastAdvancedTick=tick;return true;}
 void onObjectRemoved(int objectId,int tick,const SpecialEventSink& emit,const std::string& reason){
    clearTemporalTarget(objectId, tick, emit, reason);
    std::vector<int> affectedTemporalTargets;
    for (const auto& [targetId, temporal] : temporalByTarget_) {
        if (temporal.progressByAttacker.contains(objectId)) affectedTemporalTargets.push_back(targetId);
    }
    for (int targetId : affectedTemporalTargets) {
        clearTemporalAttacker(objectId, targetId, tick, emit, reason);
    }
}
 void reconcile(std::vector<WorldObjectInstance>& objects){
    for (auto it = temporalByTarget_.begin(); it != temporalByTarget_.end();) {
        if (findObject(objects, it->first) == nullptr
            || !findObject(objects, it->first)->alive) {
            it = temporalByTarget_.erase(it);
        } else {
            ++it;
        }
    }
}
};
bool Reference::applyTemporalHit(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int attackerObjectId,
    int targetObjectId,
    int progress,
    int requiredProgress,
    int tick,
    const SpecialAlliancePredicate& areAllied,
    const SpecialEventSink& emit,
    bool& completed,
    std::string& reason,
    bool allowAlliedTarget
) {
    completed = false;
    WorldObjectInstance* attacker = findObject(objects, attackerObjectId);
    WorldObjectInstance* target = findObject(objects, targetObjectId);
    if (attacker == nullptr || target == nullptr
        || !specialCombatSourceAvailable(objects, attacker, &rules, &areAllied) || !target->alive) {
        reason = "object unavailable";
        return false;
    }
    // Ordinary acquisition keeps its alliance filter. The host may admit
    // an explicit force-fire order against a distinct friendly target.
    if (attackerObjectId == targetObjectId || (!allowAlliedTarget && areAllied != nullptr
        && areAllied(attacker->owner, target->owner))) {
        reason = "allied target";
        return false;
    }
    if (targetImmuneToTemporal(rules, *target)) {
        reason = "target immune to temporal";
        return false;
    }

    auto [targetState, inserted] = temporalByTarget_.try_emplace(targetObjectId);
    TemporalTargetState& state = targetState->second;
    if (inserted) state.lastAdvancedTick = tick;
    state.targetObjectId = targetObjectId;
    state.requiredProgress = std::max(
        state.requiredProgress,
        std::max(1, requiredProgress)
    );
    state.frozen = true;
    auto& contribution = state.progressByAttacker[attackerObjectId];
    contribution = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(contribution) + std::max(0, progress)));
    // Preserve the saved folding mission while its buildup clock is frozen.
    if (!runtimeStructureIsRepacking(*target)) target->mission = "Temporal";
    std::int64_t totalProgress = 0;
    for (const auto& [unusedAttacker, value] : state.progressByAttacker) {
        (void)unusedAttacker;
        totalProgress += value;
    }
    reason = "temporal progress";
    if (emit != nullptr) {
        emit({
            "TemporalProgress",
            tick,
            attackerObjectId,
            targetObjectId,
            static_cast<int>(std::min<std::int64_t>(totalProgress, std::numeric_limits<int>::max())),
            {target->x, target->y},
            reason
        });
    }
    if (totalProgress >= state.requiredProgress) {
        completed = true;
        target->health = 0;
        target->mission = "TemporalRemoved";
        if (emit != nullptr) {
            emit({
                "TemporalCompleted",
                tick,
                attackerObjectId,
                targetObjectId,
                static_cast<int>(std::min<std::int64_t>(totalProgress, std::numeric_limits<int>::max())),
                {target->x, target->y},
                "temporal progress complete"
            });
        }
        temporalByTarget_.erase(targetObjectId);
    }
    return true;
}

bool Reference::isTemporalFrozen(int objectId) const {
    const auto found = temporalByTarget_.find(objectId);
    return found != temporalByTarget_.end() && found->second.frozen;
}

bool Reference::hasTemporalAuthorityForObject(
    int objectId
) const {
    if (temporalByTarget_.find(objectId) != temporalByTarget_.end()) {
        return true;
    }
    return std::any_of(
        temporalByTarget_.begin(),
        temporalByTarget_.end(),
        [objectId](const auto& entry) {
            return entry.second.progressByAttacker.find(objectId)
                != entry.second.progressByAttacker.end();
        }
    );
}

void Reference::clearTemporalAttacker(
    int attackerObjectId,
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    const auto targetIt = temporalByTarget_.find(targetObjectId);
    if (targetIt == temporalByTarget_.end()) {
        return;
    }
    auto& contributors = targetIt->second.progressByAttacker;
    const auto leaving = contributors.find(attackerObjectId);
    if (leaving == contributors.end()) return;
    const int accrued = leaving->second;
    contributors.erase(leaving);
    // Erasure belongs to the target. A remaining firer inherits completed
    // work; only the final disconnected beam releases stasis and resets it.
    if (!contributors.empty()) {
        auto& retained = contributors.begin()->second;
        retained = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max(), static_cast<std::int64_t>(retained) + accrued));
    }
    if (targetIt->second.progressByAttacker.empty()) {
        temporalByTarget_.erase(targetIt);
    }
    if (emit != nullptr) {
        emit({
            "TemporalInterrupted",
            tick,
            attackerObjectId,
            targetObjectId,
            0,
            {},
            reason
        });
    }
}

void Reference::clearTemporalTarget(
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    if (temporalByTarget_.erase(targetObjectId) == 0) {
        return;
    }
    if (emit != nullptr) {
        emit({
            "TemporalTargetCleared",
            tick,
            -1,
            targetObjectId,
            0,
            {},
            reason
        });
    }
}


struct Queries final:RuntimeTemporalStateHitQueries{
 std::vector<WorldObjectInstance>& objects;const RulesDatabase& rules;const SpecialAlliancePredicate& allied;
 Queries(std::vector<WorldObjectInstance>& o,const RulesDatabase& r,const SpecialAlliancePredicate& a):objects(o),rules(r),allied(a){}
 WorldObjectInstance* findObject(int id)const override{return Reference::findObject(objects,id);}
 bool combatSourceAvailable(const WorldObjectInstance* p)const override{return specialCombatSourceAvailable(objects,p,&rules,&allied);}
 bool targetImmune(const WorldObjectInstance& o)const override{return targetImmuneToTemporal(rules,o);}
};

RulesDatabase modelRules(){
 IniFile ini;RulesDatabase r;
 require(ini.loadFromString("[VehicleTypes]\n0=BASIC\n1=IMMUNE\n2=HOST\n3=CLOSED\n[InfantryTypes]\n0=INF\n1=IMMUNEINF\n[AircraftTypes]\n0=AIR\n[BuildingTypes]\n0=BASIC\n1=IMMUNEB\n[BASIC]\nStrength=1000\n[IMMUNE]\nStrength=1000\nImmuneToTemporal=yes\n[HOST]\nStrength=1000\nOpenTopped=yes\n[CLOSED]\nStrength=1000\nOpenTopped=no\n[INF]\nStrength=100\n[IMMUNEINF]\nStrength=100\nImmuneToTemporal=yes\n[AIR]\nStrength=1000\n[IMMUNEB]\nStrength=1000\nImmuneToTemporal=yes\n"),"rules ini");
 require(r.loadFromIni(ini),"rules load");return r;
}
void verifyRecordsAndEvents(){
 TemporalTargetState v;const auto a=encode(v);
 #define PROBE_RECORD(f) {auto x=v;mutateEncoderField(x.f);require(encode(x)!=a,"record " #f);++recordProbes;}
 PROBE_RECORD(targetObjectId) PROBE_RECORD(requiredProgress) PROBE_RECORD(frozen)
 PROBE_RECORD(progressByAttacker) PROBE_RECORD(lastAdvancedTick)
 #undef PROBE_RECORD
 SpecialMechanismEvent e;const auto b=encode(e);
 #define PROBE_EVENT(f) {auto x=e;mutateEncoderField(x.f);require(encode(x)!=b,"event " #f);++eventProbes;}
 PROBE_EVENT(kind) PROBE_EVENT(tick) PROBE_EVENT(objectId) PROBE_EVENT(targetObjectId)
 PROBE_EVENT(value) PROBE_EVENT(cell) PROBE_EVENT(reason)
 #undef PROBE_EVENT
 require(worldProbes==87&&recordProbes==5&&eventProbes==7,"complete encoders");
}
WorldObjectInstance object(int id,const std::string& owner,const std::string& type){
 WorldObjectInstance o;o.instanceId=id;o.owner=owner;o.type=type;o.kind=WorldObjectKind::Unit;
 o.health=100;o.alive=true;o.x=5+id;o.y=9-id;o.level=id%3;o.mission="Guard";
 o.mapId="fixture|%";o.tag="tag";o.facing=7;o.turretFacing=19;o.subCell=2;return o;
}
std::vector<WorldObjectInstance> worldFor(int scenario){
 auto w=std::vector<WorldObjectInstance>{object(1,"A","BASIC"),object(2,"B","BASIC"),object(3,"A","BASIC"),object(4,"ALLY","HOST")};
 w[3].transportPassengerIds={1};
 switch(scenario){
 case 1:w[0].instanceId=101;break;
 case 2:w[1].instanceId=102;break;
 case 3:w[0].health=0;break;
 case 4:w[1].alive=false;break;
 case 5:w[1].owner="A";break;
 case 6:w[1].type="IMMUNE";break;
 case 7:w[1].kind=WorldObjectKind::Structure;w[1].type="IMMUNEB";break;
 case 8:w[1].kind=WorldObjectKind::Structure;w[1].mission="Repacking";break;
 case 9:w[0].alive=false;w[0].mission="InsideTransport";break;
 case 10:w[0].alive=false;w[0].mission="InsideTransport";w[3].type="CLOSED";break;
 case 11:{auto duplicate=w[1];duplicate.health=99;duplicate.owner="X";w.insert(w.begin()+2,duplicate);w[1].alive=false;break;}
 case 12:w[1].kind=WorldObjectKind::Infantry;w[1].type="IMMUNEINF";break;
 case 13:w[1].airborne=true;w[1].type="AIR";break;
 case 14:w[1].health=0;break;
 case 15:w[0].alive=false;w[0].mission="InsideTransport";w[3].alive=false;break;
 }
 return w;
}
std::map<int,TemporalTargetState> initial(int seed){
 std::map<int,TemporalTargetState> m;if(!seed)return m;
 TemporalTargetState t;t.targetObjectId=2;t.requiredProgress=100;t.frozen=(seed%2)!=0;
 t.lastAdvancedTick=seed==2?INT_MAX:-1;t.progressByAttacker={{1,seed==2?INT_MAX:5},{3,7}};m.emplace(2,t);
 t.targetObjectId=4;t.requiredProgress=9;t.progressByAttacker={{1,9}};m.emplace(4,t);
 t.targetObjectId=100;t.lastAdvancedTick=INT_MIN;t.progressByAttacker={{1,-5}};m.emplace(100,t);
 t.targetObjectId=1;t.progressByAttacker={{3,1}};m.emplace(1,t);return m;
}
struct Input{int scenario=0,progress=1,required=9,alliance=1,alias=0,seed=0,hook=0,faultAt=0;bool force=false,sameObject=false,silent=false,clearFirst=false;};
struct Output{std::vector<std::string> trace,ports;std::string finalState,outcome;};
template<class State> Output execute(const Input& in,const RulesDatabase& rules){
 State state;const_cast<std::map<int,TemporalTargetState>&>(state.records())=initial(in.seed);
 auto w=worldFor(in.scenario);bool done=true;std::string why="sentinel|%";
 bool& completed=in.alias==1?w[0].alive:in.alias==2?w[1].alive:done;
 std::string& reason=in.alias==1?w[0].owner:in.alias==2?w[1].mission:why;
 Output out;int ordinal=0;bool hookFired=false;
 auto snapshot=[&]{return encode(w)+encode(state.records())+encode(completed)+encode(reason);};
 auto after=[&](const std::string& label){out.trace.push_back(label+":"+snapshot());};
 SpecialEventSink emit;
 activeCheckpoint=[&](const std::string& label){
  ++ordinal;out.ports.push_back(label);out.trace.push_back(label+":"+snapshot());
  if(in.faultAt==ordinal)throw std::runtime_error("fault@"+label);
  if(!hookFired){
   if(in.hook==1&&label=="find:2"){hookFired=true;w[0].owner="B";}
   if(in.hook==2&&label=="source:1"){hookFired=true;w[1].alive=false;}
   if(in.hook==3&&label=="immune:2"){hookFired=true;w[1].mission="Repacking";w[1].kind=WorldObjectKind::Structure;}
   if(in.hook==4&&label=="event:TemporalProgress"){
    hookFired=true;auto& m=const_cast<std::map<int,TemporalTargetState>&>(state.records());auto it=m.find(2);if(it!=m.end())it->second.requiredProgress=1;
   }
   if(in.hook==5&&label=="event:TemporalProgress"){
    hookFired=true;w[1].facing=31;auto& m=const_cast<std::map<int,TemporalTargetState>&>(state.records());auto it=m.find(2);if(it!=m.end())it->second.progressByAttacker[3]=19;
   }
   if(in.hook==6&&label=="event:TemporalInterrupted"){hookFired=true;state.clearTemporalTarget(4,777,emit,"nested-clear");}
  }
 };
 SpecialAlliancePredicate allied;
 if(in.alliance)allied=[&](const std::string& a,const std::string& b){checkpoint("allied:"+a+":"+b);return a==b||(in.alliance==2&&a!="B"&&b!="B");};
 emit=[&](const SpecialMechanismEvent& e){
  checkpoint("event:"+e.kind);out.trace.push_back("payload:"+encode(e));
  if(e.kind=="TemporalProgress")++progressEvents;if(e.kind=="TemporalCompleted")++completeEvents;
  if(e.kind=="TemporalInterrupted")++interruptEvents;if(e.kind=="TemporalTargetCleared")++clearEvents;
 };
 if(in.silent)emit={};
 Queries q(w,rules,allied);
 auto apply=[&](int a,int p,int r,int tick){
  bool result=false;
  if constexpr(std::is_same_v<State,Reference>)result=state.applyTemporalHit(w,rules,a,2,p,r,tick,allied,emit,completed,reason,in.force);
  else result=state.applyTemporalHit(q,a,2,p,r,tick,allied,emit,completed,reason,in.force);
  if(result)++positives;else ++negatives;after("apply:"+encode(result));
 };
 try{
  after("initial");
  if(in.clearFirst){state.clearTemporalAttacker(1,2,6,emit,"saturated-transfer");require(state.records().at(2).progressByAttacker.at(3)==INT_MAX,"ordered retained progress saturates");after("clear-first");}
  apply(in.sameObject?2:1,in.progress,in.required,7);
  for(int id:{-1,1,2,3,99,INT_MAX})after("query:"+encode(state.isTemporalFrozen(id))+encode(state.hasTemporalAuthorityForObject(id))+encode(state.hasTemporalAttacker(1,id)));
  for(int tick:{7,8,8,INT_MIN,INT_MAX})after("begin:"+encode(state.beginTemporalTick(2,tick)));
  apply(3,7,in.required,8);
  state.clearTemporalAttacker(1,2,9,emit,"detach");after("detach");
  state.clearTemporalTarget(100,10,emit,"target-clear");after("clear");
  state.clearTemporalTarget(100,10,emit,"target-clear-again");after("clear-again");
  state.onObjectRemoved(1,11,emit,"removed");after("removed");
  if constexpr(std::is_same_v<State,Reference>)state.reconcile(w);else state.reconcile(q);after("reconcile");
  State peer;const_cast<std::map<int,TemporalTargetState>&>(peer.records())=initial(2);
  const auto* owned=&state.records();state.swap(peer);require(&state.records()==owned,"map borrow lifetime across swap");after("swap:"+encode(peer.records()));
  state.reset();require(state.empty(),"reset empty");after("reset");out.outcome="normal";
 }catch(const std::exception& e){out.outcome=e.what();}
 out.finalState=snapshot();activeCheckpoint={};return out;
}
Output compare(const Input& in,const RulesDatabase& rules){
 auto a=execute<Reference>(in,rules);auto b=execute<RuntimeTemporalState>(in,rules);
 require(a.trace==b.trace,"all World87/Temporal5/outs/query-event checkpoints differ scenario="+std::to_string(in.scenario));
 require(a.ports==b.ports,"ordered observations differ");require(a.outcome==b.outcome,"exception result differs");require(a.finalState==b.finalState,"complete final state differs");
 ++states;return a;
}
bool runContract(){
 try{
  static_assert(sizeof(RuntimeTemporalState)==sizeof(std::map<int,TemporalTargetState>));
  static_assert(alignof(RuntimeTemporalState)==alignof(std::map<int,TemporalTargetState>));
  static_assert(!std::is_polymorphic_v<RuntimeTemporalState>);
  static_assert(std::is_same_v<decltype(std::declval<const RuntimeTemporalState&>().records()),const std::map<int,TemporalTargetState>&>);
  verifyWorld();verifyRecordsAndEvents();const auto rules=modelRules();
  for(int scenario=0;scenario<16;++scenario)for(int p:{-5,0,1,7,INT_MAX})for(int r:{-1,0,1,9,INT_MAX})for(int alliance=0;alliance<3;++alliance)for(int alias=0;alias<3;++alias)for(int seed:{0,1}){
   Input in;in.scenario=scenario;in.progress=p;in.required=r;in.alliance=alliance;in.alias=alias;in.seed=seed;in.force=((static_cast<long long>(scenario)+p+alias)&1LL)!=0;in.silent=((scenario+seed+alias)&1)!=0;compare(in,rules);
  }
  for(int scenario:{0,5,8,9,11,14})for(int hook=0;hook<=6;++hook){
   Input in;in.scenario=scenario;in.seed=1;in.hook=hook;in.alliance=2;in.progress=7;in.required=100;
   auto normal=compare(in,rules);
   for(int at=1;at<=static_cast<int>(normal.ports.size());++at){in.faultAt=at;auto failed=compare(in,rules);require(failed.outcome.starts_with("fault@"),"fault must reach exact observed port");++faults;faultPorts.insert(normal.ports[at-1]);}
  }
  for(int seed:{0,1,2})for(int alias=0;alias<3;++alias){Input in;in.seed=seed;in.alias=alias;in.sameObject=true;in.clearFirst=seed==2;compare(in,rules);}
  require(positives>0&&negatives>0&&progressEvents>0&&completeEvents>0&&interruptEvents>0&&clearEvents>0,"all outcomes/events reached");
  require(faults>0&&faultPorts.size()>=4,"all observed failure observations exercised");
  std::cout<<"temporal-state checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" record-probes="<<recordProbes<<" event-probes="<<eventProbes<<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" full-World-record-event-query-alias-lifecycle=exact failures=0\n";
  return true;
 }catch(const std::exception& e){activeCheckpoint={};std::cerr<<"temporal-state FAIL "<<e.what()<<'\n';return false;}
}
} // namespace temporal_state_model_detail
