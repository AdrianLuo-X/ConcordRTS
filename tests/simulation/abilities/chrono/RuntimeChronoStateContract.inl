#include "simulation/abilities/chrono/RuntimeChronoState.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace chrono_state_model_detail {
int checks=0,states=0,worldProbes=0,chronoProbes=0,faults=0,accepted=0,rejected=0,events=0,lifetimes=0,aliases=0;
std::set<std::string> faultPorts;
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
std::function<void(const std::string&)> activeOperation;
void operation(const std::string& name){if(activeOperation)activeOperation(name);}
const UnitType* rawUnitTypeFor(
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
WorldObjectInstance* rawFindObject(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}
const UnitType* unitTypeFor(const RulesDatabase& rules,const WorldObjectInstance& object){operation("unit:"+encode(object));return rawUnitTypeFor(rules,object);}
bool rawCombatSourceAvailable(const std::vector<WorldObjectInstance>& objects,
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
bool specialCombatSourceAvailable(const std::vector<WorldObjectInstance>& objects,const WorldObjectInstance* source){operation("source:"+std::to_string(source?source->instanceId:-1));return rawCombatSourceAvailable(objects,source);}
#include "RuntimeChronoStateOriginal.inl"
WorldObjectInstance* OriginalChrono::findObject(std::vector<WorldObjectInstance>& objects,int id){operation("find:"+std::to_string(id));return rawFindObject(objects,id);}
struct RequestQueries final:RuntimeChronoStateRequestQueries{
 std::vector<WorldObjectInstance>& objects;const RulesDatabase& rules;
 RequestQueries(std::vector<WorldObjectInstance>& o,const RulesDatabase& r):objects(o),rules(r){}
 WorldObjectInstance* findObject(int id)const override{operation("find:"+std::to_string(id));return rawFindObject(objects,id);}
 const UnitType* unitType(const WorldObjectInstance& object)const override{return unitTypeFor(rules,object);}
};
struct TickQueries final:RuntimeChronoStateTickQueries{
 std::vector<WorldObjectInstance>& objects;
 explicit TickQueries(std::vector<WorldObjectInstance>& o):objects(o){}
 WorldObjectInstance* findObject(int id)const override{operation("find:"+std::to_string(id));return rawFindObject(objects,id);}
 bool combatSourceAvailable(const WorldObjectInstance* source)const override{return specialCombatSourceAvailable(objects,source);}
};
std::string recordImage(const std::map<int,ChronoMoveState>& records){std::string s;for(const auto& [key,value]:records)s+=encode(key)+encode(value);return s;}
std::string eventImage(const SpecialMechanismEvent& e){return encode(e.kind)+encode(e.tick)+encode(e.objectId)+encode(e.targetObjectId)+encode(e.value)+encode(e.cell)+encode(e.reason);}
const RulesDatabase& testRules(){static const RulesDatabase rules=[](){
 IniFile ini;require(ini.loadFromString("[InfantryTypes]\n0=TELE\n1=PLAIN\n[VehicleTypes]\n0=TELE\n1=PLAIN\n[AircraftTypes]\n0=TELE\n1=PLAIN\n[TELE]\nTeleporter=yes\nStrength=100\n[PLAIN]\nTeleporter=no\nStrength=100\n"),"test rules text");
 RulesDatabase out;require(out.loadFromIni(ini),"test rules parsed");return out;}();return rules;}
struct Fixture {
 bool old;OriginalChrono before;RuntimeChronoState after;std::vector<WorldObjectInstance> objects;
 int tick=100,hz=30,id=41,level=3,calls=0,faultAt=0,mode=0;bool faultAfter=false,legal=true,occupied=false,noLegal=false,noOccupied=false,noEmit=false;
 SpecialCell destination{9,12};std::string reason="retained reason";std::vector<std::string> trace,emitted;
 explicit Fixture(bool original):old(original){
  before.rules_.chronoDelay=3;before.rules_.chronoReinforcementDelay=5;before.rules_.chronoMinimumDelay=2;before.rules_.chronoTrigger=true;before.rules_.chronoRangeMinimum=128;before.rules_.chronoDistanceFactor=48;
  for(int key:{41,63,99,52}){WorldObjectInstance o;o.instanceId=key;o.kind=WorldObjectKind::Unit;o.type="TELE";o.owner=key==52?"enemy":"home";o.alive=true;o.health=100;o.x=2;o.y=4;o.cell=2050;o.level=1;o.mission="Move";o.hasRenderPosition=true;o.renderMoveActive=true;o.renderX=3.125;o.renderY=-8.75;o.renderLevel=2.5;o.authoredAircraft=true;objects.push_back(o);}
 }
 const std::map<int,ChronoMoveState>& records()const{return old?before.chronoByObject_:after.records();}
 ChronoMoveState* state(int key){return old?const_cast<ChronoMoveState*>(before.chronoMoveState(key)):const_cast<ChronoMoveState*>(after.chronoMoveState(key));}
 void seed(int key,int phase){WorldObjectInstance o;o.instanceId=key;o.alive=true;o.x=-13;o.y=17;o.level=4;
  if(old)before.beginChronosphereRecovery(o,80,30);else after.beginChronosphereRecovery(o,80,30,before.rules_);
  auto* s=state(key);require(s!=nullptr,"owned test record from original public arrival");s->objectId=key+7;s->phase=static_cast<ChronoPhase>(phase);s->source={-3,7};s->destination={11,19};s->sourceLevel=-2;s->destinationLevel=5;s->startTick=90;s->completeTick=99;s->cooldownUntilTick=103;
 }
 std::string& reasonArg(){return mode==5?objects[0].mission:mode==6?objects[0].tag:reason;}
 std::string image()const{return encode(objects)+recordImage(records())+encode(before.rules_.chronoDelay)+encode(before.rules_.chronoReinforcementDelay)+encode(before.rules_.chronoMinimumDelay)+encode(before.rules_.chronoTrigger)+encode(before.rules_.chronoRangeMinimum)+encode(before.rules_.chronoDistanceFactor)+encode(reason);}
 void op(const std::string& name){++calls;trace.push_back(name+"@"+image());const bool fault=faultAt==calls;
  if(fault&&!faultAfter)throw std::runtime_error("fault:"+std::to_string(calls));
  if(mode==1&&name.starts_with("legal:")){before.rules_.chronoMinimumDelay=19;before.rules_.chronoDistanceFactor=7;}
  if(mode==2&&name.starts_with("unit:")){objects[0].x=-17;objects[0].y=23;objects[0].level=8;}
  if(mode==3&&name.starts_with("occupied:")){objects[0].x=71;objects[0].level=-4;before.rules_.chronoTrigger=false;}
  if(mode==4&&name.starts_with("emit:")){if(auto* s=state(41))s->cooldownUntilTick+=7;objects[0].renderLevel=-0.0;objects[0].tag="live emit";}
  if(fault&&faultAfter)throw std::runtime_error("fault:"+std::to_string(calls));
 }
};
Fixture fixture(bool old,unsigned mask,int phase,int entry){Fixture f(old);
 if(entry>=3){f.seed(41,phase);f.seed(63,(phase+1)%5);f.seed(99,(phase+3)%5);}
 else if(mask&(1u<<9))f.seed(41,phase);
 if(mask&1u)f.id=-117;
 if(mask&(1u<<1))f.objects[0].kind=WorldObjectKind::Structure;
 if(mask&(1u<<2))f.objects[0].kind=WorldObjectKind::Infantry;
 if(mask&(1u<<3))f.objects[0].type="missing";
 if(mask&(1u<<4))f.objects[0].airborne=true;
 if(mask&(1u<<5))f.objects[0].authoredAircraft=false;
 if(mask&(1u<<6)){f.objects[0].alive=false;f.objects[0].mission="InsideTransport";f.objects[3].transportPassengerIds={41};}
 if(mask&(1u<<7))f.objects[0].dockedToObjectId=52;
 if(mask&(1u<<8))f.objects[0].transportPassengerIds={52};
 if(mask&(1u<<10))f.legal=false;
 if(mask&(1u<<11))f.occupied=true;
 if(mask&(1u<<12))f.noLegal=true;
 if(mask&(1u<<13))f.noOccupied=true;
 if(mask&(1u<<14))f.noEmit=true;
 if(mask&(1u<<15))f.objects[0].health=0;
 if(mask&(1u<<16))f.objects[3].alive=false;
 if(mask&(1u<<17))f.objects[3].health=0;
 if(mask&(1u<<18))f.objects[3].transportPassengerIds.clear();
 if(mask&(1u<<19))f.objects[0].mission="Guard";
 if(mask&(1u<<20))f.objects[2].instanceId=100;
 if(mask&(1u<<21)){auto duplicate=f.objects[0];duplicate.alive=true;duplicate.type="TELE";f.objects.push_back(duplicate);}
 if(mask&(1u<<22))f.objects[0].type="PLAIN";
 return f;
}
struct Outcome{bool result=false,threw=false;std::string error;};
Outcome execute(Fixture& f,int entry){Outcome o;auto prior=std::move(activeOperation);activeOperation=[&](const auto& n){f.op(n);};
 SpecialCellPredicate legal=f.noLegal?SpecialCellPredicate{}:SpecialCellPredicate{[&](int x,int y,int id){operation("legal:"+encode(x)+encode(y)+encode(id));return f.legal;}};
 SpecialCellPredicate occupied=f.noOccupied?SpecialCellPredicate{}:SpecialCellPredicate{[&](int x,int y,int id){operation("occupied:"+encode(x)+encode(y)+encode(id));return f.occupied;}};
 SpecialEventSink emit=f.noEmit?SpecialEventSink{}:SpecialEventSink{[&](const SpecialMechanismEvent& e){f.emitted.push_back(eventImage(e));operation("emit:"+eventImage(e));}};
 try{if(entry==0){if(f.old)f.before.beginChronosphereRecovery(f.objects[0],f.tick,f.hz);else f.after.beginChronosphereRecovery(f.objects[0],f.tick,f.hz,f.before.rules_);}
 else if(entry==1){if(f.old)f.before.beginChronoReinforcementRecovery(f.objects[0],f.tick,f.hz);else f.after.beginChronoReinforcementRecovery(f.objects[0],f.tick,f.hz,f.before.rules_);}
 else if(entry==2){if(f.old)o.result=f.before.requestChronoMove(f.objects,testRules(),f.id,f.destination,f.level,f.tick,legal,occupied,emit,f.reasonArg(),f.hz);else {RequestQueries q(f.objects,testRules());o.result=f.after.requestChronoMove(q,f.before.rules_,f.id,f.destination,f.level,f.tick,legal,occupied,emit,f.reasonArg(),f.hz);}}
 else if(entry==3){if(f.old)o.result=f.before.tickChronoMoves(f.objects,f.tick,legal,occupied,emit);else {TickQueries q(f.objects);o.result=f.after.tickChronoMoves(q,f.tick,legal,occupied,emit);}}
 else if(entry==4)o.result=f.old?f.before.cancelChronoMoveInTransit(f.id):f.after.cancelChronoMoveInTransit(f.id);
 else {o.result=f.old?f.before.isChronoInTransit(f.id):f.after.isChronoInTransit(f.id);o.error=encode(f.old?f.before.isChronoMovementLocked(f.id,f.tick):f.after.isChronoMovementLocked(f.id,f.tick));}
 }catch(const std::exception& e){o.threw=true;o.error=e.what();}activeOperation=std::move(prior);return o;
}
void compare(Fixture& a,Fixture& b,int entry){const auto x=execute(a,entry),y=execute(b,entry);
 require(x.result==y.result&&x.threw==y.threw&&x.error==y.error,"original/current return and exact exception");
 require(a.calls==b.calls&&a.trace==b.trace,"original ordered queries/effects and complete state at every callback");
 require(a.emitted==b.emitted,"all event fields and original event order");require(a.image()==b.image(),"full ordered record11/world87/live rules/reason exact");
 for(int key:{-117,41,52,63,99,100}){auto* left=a.before.chronoMoveState(key);auto* right=b.after.chronoMoveState(key);require((left==nullptr)==(right==nullptr)&&(!left||encode(*left)==encode(*right)),"borrowed state value/absence exact");}
 if(x.threw){++faults;if(a.faultAt>0){const auto& n=a.trace.at(static_cast<std::size_t>(a.faultAt-1));faultPorts.insert(n.substr(0,n.find(':')));}}
 else if(x.result)++accepted;else ++rejected;events+=static_cast<int>(a.emitted.size());++states;
}
void verifyLifetime(){RuntimeChronoState a;SpecialMechanismRules cfg;cfg.chronoDelay=7;WorldObjectInstance obj;obj.alive=true;obj.instanceId=41;a.beginChronosphereRecovery(obj,100,30,cfg);const auto* p=a.chronoMoveState(41);require(p!=nullptr,"owned record");const auto original=encode(*p);
 obj.instanceId=63;a.beginChronosphereRecovery(obj,100,30,cfg);require(a.chronoMoveState(41)==p&&encode(*p)==original,"map insertion keeps original borrow");++lifetimes;
 auto copy=a;require(copy.chronoMoveState(41)!=p&&encode(*copy.chronoMoveState(41))==original,"copy owns separate records");copy.erase(41);require(a.chronoMoveState(41)==p,"copy erasure independent");++lifetimes;
 RuntimeChronoState other;other.swap(a);require(other.chronoMoveState(41)==p&&a.empty(),"swap preserves element borrow and empty source");++lifetimes;
 RuntimeChronoState moved(std::move(other));require(moved.chronoMoveState(41)==p&&encode(*p)==original,"map move preserves existing borrow");++lifetimes;
 moved.erase(63);require(moved.chronoMoveState(41)==p,"other erase preserves borrow");moved.erase(-1);require(moved.chronoMoveState(41)==p,"missing erase preserves borrow");++lifetimes;
 moved.reset();require(moved.empty()&&moved.chronoMoveState(41)==nullptr,"reset empties records");++lifetimes;
 static_assert(sizeof(RuntimeChronoState)==sizeof(std::map<int,ChronoMoveState>));static_assert(alignof(RuntimeChronoState)==alignof(std::map<int,ChronoMoveState>));static_assert(sizeof(ChronoMoveState)==44);
}
bool runContract(){try{
 verifyWorld();verifyLifetime();
{ChronoMoveState zero;const auto before=encode(zero);
{auto v=zero;mutateEncoderField(v.objectId);require(encode(v)!=before,"chrono objectId omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.phase);require(encode(v)!=before,"chrono phase omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.source.x);require(encode(v)!=before,"chrono source.x omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.source.y);require(encode(v)!=before,"chrono source.y omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.destination.x);require(encode(v)!=before,"chrono destination.x omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.destination.y);require(encode(v)!=before,"chrono destination.y omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.sourceLevel);require(encode(v)!=before,"chrono sourceLevel omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.destinationLevel);require(encode(v)!=before,"chrono destinationLevel omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.startTick);require(encode(v)!=before,"chrono startTick omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.completeTick);require(encode(v)!=before,"chrono completeTick omitted");++chronoProbes;}
{auto v=zero;mutateEncoderField(v.cooldownUntilTick);require(encode(v)!=before,"chrono cooldownUntilTick omitted");++chronoProbes;}
}

 std::vector<unsigned> masks{0};for(unsigned bit=0;bit<23;++bit)masks.push_back(1u<<bit);
 for(unsigned a=0;a<23;++a)for(unsigned b=a+1;b<23;++b)masks.push_back((1u<<a)|(1u<<b));
 unsigned random=0x719ca13u;for(int n=0;n<128;++n){random=random*1664525u+1013904223u;masks.push_back(random&((1u<<23)-1));}
 for(unsigned mask:masks)for(int phase:{-1,0,1,2,3,4,5,17})for(int entry:{0,1,2,3,4,5}){auto a=fixture(true,mask,phase,entry),b=fixture(false,mask,phase,entry);compare(a,b,entry);const int calls=a.calls;
  if(entry==2||entry==3)for(int fault=1;fault<=calls;++fault)for(bool after:{false,true}){auto x=fixture(true,mask,phase,entry),y=fixture(false,mask,phase,entry);x.faultAt=y.faultAt=fault;x.faultAfter=y.faultAfter=after;compare(x,y,entry);}
 }
 for(int mode:{1,2,3,4,5,6})for(unsigned mask:{0u,1u<<6,1u<<9,1u<<10,1u<<11})for(int phase:{1,2,3,4})for(int entry:{2,3}){auto a=fixture(true,mask,phase,entry),b=fixture(false,mask,phase,entry);a.mode=b.mode=mode;compare(a,b,entry);++aliases;}
 for(int delay:{INT_MIN,-1,0,1,3,17,INT_MAX})for(int hz:{INT_MIN,-1,0,1,15,30,60,INT_MAX})for(int tick:{INT_MIN,-1,0,100,INT_MAX-1})for(int entry:{0,1,2}){auto a=fixture(true,0,0,entry),b=fixture(false,0,0,entry);a.before.rules_.chronoDelay=b.before.rules_.chronoDelay=delay;a.before.rules_.chronoReinforcementDelay=b.before.rules_.chronoReinforcementDelay=delay;a.before.rules_.chronoMinimumDelay=b.before.rules_.chronoMinimumDelay=delay;a.hz=b.hz=hz;a.tick=b.tick=tick;compare(a,b,entry);}
 for(int tick:{INT_MIN,-1,0,98,99,100,103,104,INT_MAX})for(int phase:{-1,0,1,2,3,4,17}){auto a=fixture(true,0,phase,3),b=fixture(false,0,phase,3);a.tick=b.tick=tick;compare(a,b,3);}
 require(worldProbes==87&&chronoProbes==11&&accepted>0&&rejected>0&&events>0&&faults>0&&faultPorts.size()==6&&aliases>0&&lifetimes==6,"full field/lifetime/branch/fault coverage required");
 std::cout<<"chrono-state checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" chrono-leaf-probes="<<chronoProbes<<" fault-ports="<<faultPorts.size()<<" faults="<<faults<<" accepted="<<accepted<<" rejected="<<rejected<<" events="<<events<<" live-aliases="<<aliases<<" lifetimes="<<lifetimes<<" full87World-11Chrono-and-ordered-callback-states=exact failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"chrono-state: "<<e.what()<<'\n';return false;}}
}
