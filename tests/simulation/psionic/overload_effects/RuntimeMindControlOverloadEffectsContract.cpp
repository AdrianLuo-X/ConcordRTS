#include "simulation/abilities/psionic/RuntimeMindControlOverloadEffects.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace domain=concord::simulation::psionic;
namespace {
int checks=0,states=0,faults=0;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(why);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
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
int encoderFieldProbes=0;
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
    else static_assert(sizeof(T)==0,"unprobed canonical WorldObject field");
}
void verifyWorldObjectEncoder() {
    const WorldObjectInstance baseline;
    const auto image=encode(baseline);
    {auto changed=baseline;mutateEncoderField(changed.instanceId);if(encode(changed)==image)throw std::runtime_error("world field invisible: instanceId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.kind);if(encode(changed)==image)throw std::runtime_error("world field invisible: kind");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.mapId);if(encode(changed)==image)throw std::runtime_error("world field invisible: mapId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.owner);if(encode(changed)==image)throw std::runtime_error("world field invisible: owner");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.type);if(encode(changed)==image)throw std::runtime_error("world field invisible: type");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.health);if(encode(changed)==image)throw std::runtime_error("world field invisible: health");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.x);if(encode(changed)==image)throw std::runtime_error("world field invisible: x");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.y);if(encode(changed)==image)throw std::runtime_error("world field invisible: y");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.cell);if(encode(changed)==image)throw std::runtime_error("world field invisible: cell");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.level);if(encode(changed)==image)throw std::runtime_error("world field invisible: level");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.facing);if(encode(changed)==image)throw std::runtime_error("world field invisible: facing");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.turretFacing);if(encode(changed)==image)throw std::runtime_error("world field invisible: turretFacing");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.bodyFacingClock);if(encode(changed)==image)throw std::runtime_error("world field invisible: bodyFacingClock");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.turretFacingClock);if(encode(changed)==image)throw std::runtime_error("world field invisible: turretFacingClock");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.subCell);if(encode(changed)==image)throw std::runtime_error("world field invisible: subCell");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.mission);if(encode(changed)==image)throw std::runtime_error("world field invisible: mission");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.tag);if(encode(changed)==image)throw std::runtime_error("world field invisible: tag");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.veterancy);if(encode(changed)==image)throw std::runtime_error("world field invisible: veterancy");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.veterancyExperience);if(encode(changed)==image)throw std::runtime_error("world field invisible: veterancyExperience");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.crateArmorMultiplier);if(encode(changed)==image)throw std::runtime_error("world field invisible: crateArmorMultiplier");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.crateFirepowerMultiplier);if(encode(changed)==image)throw std::runtime_error("world field invisible: crateFirepowerMultiplier");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.crateSpeedMultiplier);if(encode(changed)==image)throw std::runtime_error("world field invisible: crateSpeedMultiplier");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.crateCloakable);if(encode(changed)==image)throw std::runtime_error("world field invisible: crateCloakable");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.harvestAutoSearchSuspended);if(encode(changed)==image)throw std::runtime_error("world field invisible: harvestAutoSearchSuspended");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.hasBeenCaptured);if(encode(changed)==image)throw std::runtime_error("world field invisible: hasBeenCaptured");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.onBridge);if(encode(changed)==image)throw std::runtime_error("world field invisible: onBridge");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.authoredAircraft);if(encode(changed)==image)throw std::runtime_error("world field invisible: authoredAircraft");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.alive);if(encode(changed)==image)throw std::runtime_error("world field invisible: alive");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.rubble);if(encode(changed)==image)throw std::runtime_error("world field invisible: rubble");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.submerged);if(encode(changed)==image)throw std::runtime_error("world field invisible: submerged");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.cloaked);if(encode(changed)==image)throw std::runtime_error("world field invisible: cloaked");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.cloakCooldownTicks);if(encode(changed)==image)throw std::runtime_error("world field invisible: cloakCooldownTicks");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.submergeCooldownTicks);if(encode(changed)==image)throw std::runtime_error("world field invisible: submergeCooldownTicks");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.deployed);if(encode(changed)==image)throw std::runtime_error("world field invisible: deployed");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.deployStateTicksRemaining);if(encode(changed)==image)throw std::runtime_error("world field invisible: deployStateTicksRemaining");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.prone);if(encode(changed)==image)throw std::runtime_error("world field invisible: prone");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.proneTicksRemaining);if(encode(changed)==image)throw std::runtime_error("world field invisible: proneTicksRemaining");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.weaponCooldownTicksRemaining);if(encode(changed)==image)throw std::runtime_error("world field invisible: weaponCooldownTicksRemaining");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.spriteFiringStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: spriteFiringStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.radiationEruptionCooldownTicksRemaining);if(encode(changed)==image)throw std::runtime_error("world field invisible: radiationEruptionCooldownTicksRemaining");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.hasRenderPosition);if(encode(changed)==image)throw std::runtime_error("world field invisible: hasRenderPosition");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.renderX);if(encode(changed)==image)throw std::runtime_error("world field invisible: renderX");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.renderY);if(encode(changed)==image)throw std::runtime_error("world field invisible: renderY");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.renderMoveActive);if(encode(changed)==image)throw std::runtime_error("world field invisible: renderMoveActive");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.renderLevel);if(encode(changed)==image)throw std::runtime_error("world field invisible: renderLevel");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.renderOnBridge);if(encode(changed)==image)throw std::runtime_error("world field invisible: renderOnBridge");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.movementAnimationDistance);if(encode(changed)==image)throw std::runtime_error("world field invisible: movementAnimationDistance");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.buildAnimTicksTotal);if(encode(changed)==image)throw std::runtime_error("world field invisible: buildAnimTicksTotal");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.buildAnimTicksRemaining);if(encode(changed)==image)throw std::runtime_error("world field invisible: buildAnimTicksRemaining");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.structureRecoilStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: structureRecoilStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.chargeTurretStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: chargeTurretStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.workAnimationActive);if(encode(changed)==image)throw std::runtime_error("world field invisible: workAnimationActive");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.poweredOperational);if(encode(changed)==image)throw std::runtime_error("world field invisible: poweredOperational");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.powerAnimationPausedAtTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: powerAnimationPausedAtTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.powerAnimationPausedTicks);if(encode(changed)==image)throw std::runtime_error("world field invisible: powerAnimationPausedTicks");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.factoryExitAnimationStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: factoryExitAnimationStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.factoryExitAnimationEndTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: factoryExitAnimationEndTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.factoryExitAirborne);if(encode(changed)==image)throw std::runtime_error("world field invisible: factoryExitAirborne");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.factoryExitHostId);if(encode(changed)==image)throw std::runtime_error("world field invisible: factoryExitHostId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.specialCashNextTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: specialCashNextTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.specialCloneDeliveryObjectId);if(encode(changed)==image)throw std::runtime_error("world field invisible: specialCloneDeliveryObjectId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.transportPassengerIds);if(encode(changed)==image)throw std::runtime_error("world field invisible: transportPassengerIds");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.bioReactorAnimationStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: bioReactorAnimationStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.bioReactorAnimationPoweredStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: bioReactorAnimationPoweredStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.activeGunnerMode);if(encode(changed)==image)throw std::runtime_error("world field invisible: activeGunnerMode");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.activeGunnerWeaponIndex);if(encode(changed)==image)throw std::runtime_error("world field invisible: activeGunnerWeaponIndex");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.activeGunnerTurretIndex);if(encode(changed)==image)throw std::runtime_error("world field invisible: activeGunnerTurretIndex");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.tankBunkerHostId);if(encode(changed)==image)throw std::runtime_error("world field invisible: tankBunkerHostId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.tankBunkerOccupantId);if(encode(changed)==image)throw std::runtime_error("world field invisible: tankBunkerOccupantId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.tankBunkerAnimationStartTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: tankBunkerAnimationStartTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.tankBunkerLowering);if(encode(changed)==image)throw std::runtime_error("world field invisible: tankBunkerLowering");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.dockedToObjectId);if(encode(changed)==image)throw std::runtime_error("world field invisible: dockedToObjectId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.dockIndex);if(encode(changed)==image)throw std::runtime_error("world field invisible: dockIndex");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.airborne);if(encode(changed)==image)throw std::runtime_error("world field invisible: airborne");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.flightLevel);if(encode(changed)==image)throw std::runtime_error("world field invisible: flightLevel");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.aircraftDockFlightProgress);if(encode(changed)==image)throw std::runtime_error("world field invisible: aircraftDockFlightProgress");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.aircraftBuildingClearanceLevel);if(encode(changed)==image)throw std::runtime_error("world field invisible: aircraftBuildingClearanceLevel");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.wallConnectionMask);if(encode(changed)==image)throw std::runtime_error("world field invisible: wallConnectionMask");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.garrisonHostObjectId);if(encode(changed)==image)throw std::runtime_error("world field invisible: garrisonHostObjectId");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.garrisonOccupantIds);if(encode(changed)==image)throw std::runtime_error("world field invisible: garrisonOccupantIds");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.garrisonFireCursor);if(encode(changed)==image)throw std::runtime_error("world field invisible: garrisonFireCursor");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.currentAmmo);if(encode(changed)==image)throw std::runtime_error("world field invisible: currentAmmo");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.pendingAircraftAuxSounds);if(encode(changed)==image)throw std::runtime_error("world field invisible: pendingAircraftAuxSounds");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.turretSoundActivityTick);if(encode(changed)==image)throw std::runtime_error("world field invisible: turretSoundActivityTick");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.activeTurretSoundEvent);if(encode(changed)==image)throw std::runtime_error("world field invisible: activeTurretSoundEvent");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.pendingTurretSounds);if(encode(changed)==image)throw std::runtime_error("world field invisible: pendingTurretSounds");++encoderFieldProbes;}
    {auto changed=baseline;mutateEncoderField(changed.modExtensions);if(encode(changed)==image)throw std::runtime_error("world field invisible: modExtensions");++encoderFieldProbes;}
}

using Pulse=MindControlOverloadDamageEvent;
struct Exit {int objectId=-1;int kind=0;int tick=0;std::string mission,reason;};
std::string eventImage(const SpecialMechanismEvent& v){return encode(v.kind)+encode(v.tick)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.value)+encode(v.cell)+encode(v.reason);}
std::string exitImage(const Exit& v){return encode(v.objectId)+encode(v.kind)+encode(v.tick)+encode(v.mission)+encode(v.reason);}
std::string pulseImage(const Pulse& p){return encode(p.controllerObjectId)+encode(p.controlledCount)+encode(p.damage)+encode(p.playSound);}
template<class T,class F>std::string images(const std::vector<T>& v,F f){std::string o=encode(v.size());for(const auto& x:v){auto p=f(x);o+=encode(p);}return o;}
struct RuleFields {std::string c4Warhead="C4",masterMindOverloadDeathSound="Overload";};
struct Injected {};
struct Model {
 int tick=57,hz=30,mutation=0,damageReturn=7;
 bool initialized=false,immune=false,protectedFlag=false,sinkPresent=true,drainReturn=false,queueReturn=false,revealReturn=false;
 RuleFields rules;
 std::vector<WorldObjectInstance> objects;
 std::vector<Pulse> pulses;
 std::vector<SpecialMechanismEvent> events;
 std::vector<Exit> queued,exitLog;
 std::vector<std::string> sounds,trace;
 RuntimeSpatialAudioPosition spatial{3.25,-0.0,0.125};
 int calls=0,failAt=0;bool failAfter=false;
 int kernelSerial=0,kernelStamp=0,revealSerial=0,drains=0,lastController=-1,depth=0;
 bool nestedUsed=false,badArguments=false;
};
int enter(Model& m,const std::string& text){const int n=++m.calls;m.trace.push_back("B"+std::to_string(n)+":"+text);if(n==m.failAt&&!m.failAfter)throw Injected{};return n;}
void leave(Model& m,int n){m.trace.push_back("E"+std::to_string(n));if(n==m.failAt&&m.failAfter)throw Injected{};}
WorldObjectInstance* rawFind(Model& m,int id){const auto p=std::find_if(m.objects.begin(),m.objects.end(),[id](const auto& x){return x.instanceId==id;});return p==m.objects.end()?nullptr:&*p;}
std::string snapshot(const Model& m){
 return encode(m.tick)+encode(m.hz)+encode(m.mutation)+encode(m.damageReturn)+encode(m.initialized)+encode(m.immune)+encode(m.protectedFlag)+encode(m.sinkPresent)+encode(m.drainReturn)+encode(m.queueReturn)+encode(m.revealReturn)
 +encode(m.rules.c4Warhead)+encode(m.rules.masterMindOverloadDeathSound)+encode(m.objects)+images(m.pulses,pulseImage)+images(m.events,eventImage)+images(m.queued,exitImage)+images(m.exitLog,exitImage)+encode(m.sounds)+encode(m.trace)+encode(m.spatial)
 +encode(m.calls)+encode(m.kernelSerial)+encode(m.kernelStamp)+encode(m.revealSerial)+encode(m.drains)+encode(m.lastController)+encode(m.depth)+encode(m.nestedUsed)+encode(m.badArguments);
}
struct Root;
struct FakeWorld {Model& m;const std::vector<WorldObjectInstance>& getObjects()const{return m.objects;}};
struct FakeRules {Model& m;const RuleFields& getSpecialMechanismRules()const{const int t=enter(m,"rules");leave(m,t);return m.rules;}};
struct FakeSpecial {
 Model& m;
 std::vector<Pulse> tickMindControlOverload(const std::vector<WorldObjectInstance>& objects,const FakeRules& rules,int tick,int hz){
  const int t=enter(m,"kernel:"+encode(objects)+encode(tick)+encode(hz)+encode(rules.m.rules.c4Warhead)+encode(rules.m.rules.masterMindOverloadDeathSound));
  m.badArguments |= &objects!=&m.objects || &rules.m!=&m;
  ++m.kernelSerial;m.kernelStamp=tick*97+hz;
  auto out=m.depth?std::vector<Pulse>{{3,9,73,true}}:m.pulses;
  if(m.mutation&1){m.tick+=11;m.rules.c4Warhead="after-kernel-C4";m.rules.masterMindOverloadDeathSound="after-kernel-sound";}
  leave(m,t);return out;
 }
};
struct FakeProtection {Model& m;bool isObjectProtected(int id,int tick){const int t=enter(m,"protected:"+encode(id)+encode(tick));const bool result=m.protectedFlag&&id==1;leave(m,t);return result;}};
struct FakeConfig {int& fixedTickHz;};
struct Root {
 Model& m;FakeWorld world;FakeRules rules;FakeSpecial specialMechanisms;FakeProtection superWeapons;FakeConfig config;int& tick;bool& superWeaponsInitialized;
 explicit Root(Model& v):m(v),world{v},rules{v},specialMechanisms{v},superWeapons{v},config{v.hz},tick(v.tick),superWeaponsInitialized(v.initialized){}
};
WorldObjectInstance* findMutableObjectById(Root& r,int id){const int t=enter(r.m,"find:"+encode(id));auto* p=rawFind(r.m,id);leave(r.m,t);return p;}
bool runtimeObjectIgnoresDamage(Root& r,const WorldObjectInstance& obj){
 auto& m=r.m;const int t=enter(m,"ignore:"+encode(obj));const bool result=m.immune&&obj.instanceId==1;
 if(m.mutation&2){m.initialized=!m.initialized;m.tick+=3;m.rules.c4Warhead="after-ignore-C4";}
 leave(m,t);return result;
}
int runtimeAuthoredDamageAgainst(Root& r,const WorldObjectInstance& obj,int damage,const std::string& warhead){
 auto& m=r.m;const int t=enter(m,"damage:"+encode(obj)+encode(damage)+encode(warhead));const int result=m.damageReturn;
 if(m.mutation&4){if(auto* p=rawFind(m,obj.instanceId))p->health=23;m.rules.masterMindOverloadDeathSound="after-damage-sound";m.tick+=2;}
 leave(m,t);return result;
}
bool runtimeRevealObjectAfterDamage(Root& r,WorldObjectInstance& obj,const std::string& reason){
 auto& m=r.m;const int t=enter(m,"reveal:"+encode(obj)+encode(reason));++m.revealSerial;
 if(m.mutation&8){obj.x+=5;obj.y-=2;obj.health=std::max(0,obj.health-3);obj.modExtensions.initialized=!obj.modExtensions.initialized;}
 const bool result=m.revealReturn;leave(m,t);return result;
}
RuntimeSpatialAudioPosition spatial(Model& m,const WorldObjectInstance& obj){
 const int t=enter(m,"position:"+encode(obj));const auto result=m.spatial;m.lastController=obj.instanceId;
 if(m.mutation&16){m.tick+=7;m.rules.masterMindOverloadDeathSound="after-position-sound";}
 leave(m,t);return result;
}
void emitRuntimeSound(Root& r,const std::string& token,RuntimeSpatialAudioPosition position){
 auto& m=r.m;const int t=enter(m,"sound:"+encode(token)+encode(position));m.sounds.push_back(encode(token)+encode(position));
 if(m.mutation&32){if(auto* p=rawFind(m,m.lastController)){p->instanceId+=100;p->x+=3;p->health=std::max(0,p->health-2);}m.tick+=5;m.pulses.clear();}
 leave(m,t);
}
enum class RuntimeObjectExitKind {Destroyed};
struct RuntimeObjectExitRequest {int objectId=-1;RuntimeObjectExitKind kind=RuntimeObjectExitKind::Destroyed;int tick=0;std::string mission,reason;};
bool queueRuntimeObjectExit(Root& r,RuntimeObjectExitRequest q){
 auto& m=r.m;const int t=enter(m,"queue:"+encode(q.objectId)+encode(q.kind)+encode(q.tick)+encode(q.mission)+encode(q.reason));
 Exit e{q.objectId,static_cast<int>(q.kind),q.tick,std::move(q.mission),std::move(q.reason)};m.queued.push_back(e);m.exitLog.push_back(e);
 if(m.mutation&128)if(auto* p=rawFind(m,e.objectId))p->alive=false;
 const bool result=m.queueReturn;leave(m,t);return result;
}
bool drainRuntimeObjectExitQueue(Root& r){
 auto& m=r.m;const int t=enter(m,"drain");++m.drains;
 for(const auto& q:m.queued)if(auto* p=rawFind(m,q.objectId)){p->alive=false;p->health=0;p->mission=q.mission;}
 m.queued.clear();const bool result=m.drainReturn;leave(m,t);return result;
}
struct Reference {
 Model& m;
 RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& obj){return spatial(m,obj);}
#include "RuntimeMindControlOverloadEffectsOriginal.inl"
};
struct Queries final:domain::RuntimeMindControlOverloadQueries {
 Model& m;Root root;explicit Queries(Model& x):m(x),root(x){}
 std::vector<Pulse> collectPulses()override{return root.specialMechanisms.tickMindControlOverload(root.world.getObjects(),root.rules,root.tick,root.config.fixedTickHz);}
 domain::RuntimeMindControlOverloadRulesView rulesView()override{const auto& r=root.rules.getSpecialMechanismRules();return {r.c4Warhead,r.masterMindOverloadDeathSound};}
 WorldObjectInstance* findController(int id)override{return findMutableObjectById(root,id);}
 bool ignoresDamage(const WorldObjectInstance& o)override{return runtimeObjectIgnoresDamage(root,o);}
 bool protectionInitialized()override{return root.superWeaponsInitialized;}
 bool isProtected(int id,int tick)override{return root.superWeapons.isObjectProtected(id,tick);}
 int damageAgainst(const WorldObjectInstance& o,int amount,const std::string& wh)override{return runtimeAuthoredDamageAgainst(root,o,amount,wh);}
 RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance& o)override{return spatial(m,o);}
};
struct Effects final:domain::RuntimeMindControlOverloadEffects {
 Root root;explicit Effects(Model& m):root(m){}
 void revealAfterDamage(WorldObjectInstance& o,const std::string& why)override{(void)runtimeRevealObjectAfterDamage(root,o,why);}
 void emitSound(const std::string& token,RuntimeSpatialAudioPosition p)override{emitRuntimeSound(root,token,p);}
 void queueDestroyedExit(int id,int tick)override{RuntimeObjectExitRequest q;q.objectId=id;q.kind=RuntimeObjectExitKind::Destroyed;q.tick=tick;q.mission="Destroyed";q.reason="mind-control overload";(void)queueRuntimeObjectExit(root,std::move(q));}
 bool drainExits()override{return drainRuntimeObjectExitQueue(root);}
};
bool invoke(Model& m,const SpecialEventSink& sink,bool reference){
 if(reference){Root r(m);Reference old{m};return old.advanceRuntimeMindControlOverload(r,sink);}
 Queries q(m);Effects e(m);return domain::advanceRuntimeMindControlOverloadEffects(m.tick,sink,q,e);
}
std::string exercise(Model& m,bool reference){
 SpecialEventSink sink;
 if(m.sinkPresent)sink=[&](const SpecialMechanismEvent& event){
  const int t=enter(m,"sink:"+eventImage(event));m.events.push_back(event);
  if(m.mutation&64){if(auto* p=rawFind(m,event.objectId)){p->health=0;p->y+=6;}m.tick+=13;m.rules.c4Warhead="after-sink-C4";}
  if((m.mutation&256)&&!m.nestedUsed){m.nestedUsed=true;struct Depth{int& x;explicit Depth(int& v):x(v){++x;}~Depth(){--x;}} d(m.depth);const SpecialEventSink empty;(void)invoke(m,empty,reference);}
  leave(m,t);
 };
 bool result=false;bool injected=false;
 try{result=invoke(m,sink,reference);}catch(const Injected&){injected=true;}
 require(!m.badArguments,"canonical fake-owner argument binding");
 return encode(result)+encode(injected)+snapshot(m);
}
void compare(const Model& seed){
 Model a=seed,b=seed;const auto left=exercise(a,true),right=exercise(b,false);++states;
 if(seed.failAt)++faults;
 require(left==right,"complete before/after trace/state mismatch state="+std::to_string(states)+" failAt="+std::to_string(seed.failAt)+" phase="+std::to_string(seed.failAfter));
}
Model fixture(int preset,int gates,int amount,int soundMode,bool sink){
 Model m;m.initialized=(gates&2)!=0;m.immune=(gates&1)!=0;m.protectedFlag=(gates&4)!=0;m.damageReturn=amount;m.sinkPresent=sink;m.drainReturn=(preset&1)!=0;m.queueReturn=(gates&1)!=0;m.revealReturn=(gates&4)!=0;
 m.rules.masterMindOverloadDeathSound=soundMode==0?"":"Overload";m.spatial.level=std::bit_cast<double>(0x7ff8000000009251ULL);
 for(int id=1;id<=3;++id){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.owner="Owner"+std::to_string(id);o.type="MODEL";o.health=id==3?57:100;o.alive=id!=2;o.x=id*3;o.y=-id;o.cell=o.y*1000+o.x;o.level=id;o.hasRenderPosition=true;o.renderX=std::bit_cast<double>(0x7ff8000000000000ULL+static_cast<std::uint64_t>(id));o.renderY=-0.0;o.transportPassengerIds={11,12};o.pendingAircraftAuxSounds.emplace_back();o.pendingTurretSounds.emplace_back();m.objects.push_back(o);}
 const bool play=soundMode!=1;
 const Pulse a{1,4,50,play},b{3,11,500,play},missing{99,-3,-4,play},dead{2,3,0,play};
 switch(preset){case 0:m.queued.push_back({3,0,3,"Destroyed","preexisting"});break;case 1:m.pulses={a};break;case 2:m.pulses={missing};break;case 3:m.pulses={dead};break;case 4:m.pulses={missing,dead,a,b};break;case 5:m.pulses={a,a,b};break;default:m.pulses={b,a};m.objects[0].health=0;break;}
 return m;
}
void cases(){
 for(int preset=0;preset<7;++preset)for(int gates=0;gates<8;++gates)for(int amount:{0,7,100,-4})for(int sound=0;sound<3;++sound)for(bool sink:{false,true})compare(fixture(preset,gates,amount,sound,sink));
 const std::array<int,10> aliases{1,2,4,8,16,32,64,128,256,511};
 for(int bit:aliases)for(int preset:{1,4,5}){auto m=fixture(preset,0,7,2,true);m.mutation=bit;compare(m);}
 for(int i=0;i<128;++i){auto m=fixture(i%7,(i/7)%8,(i%3)==0?-4:(i%3)==1?0:7,i%3,(i&1)!=0);m.tick=i%11-3;m.hz=std::array<int,3>{15,30,60}[static_cast<std::size_t>(i)%3];m.mutation=i%10==0?511:0;m.objects[0].health=17+i;m.spatial.x=std::bit_cast<double>(0x8000000000000000ULL+static_cast<std::uint64_t>(i));compare(m);}
 std::vector<Model> faultSeeds;
 for(int preset:{0,1,4,5,6})faultSeeds.push_back(fixture(preset,0,7,2,true));
 for(int gates:{1,2,6})faultSeeds.push_back(fixture(4,gates,7,2,true));
 for(int bit:aliases){auto m=fixture(4,0,7,2,true);m.mutation=bit;faultSeeds.push_back(m);}
 for(const auto& seed:faultSeeds){auto observed=seed;(void)exercise(observed,true);for(int at=1;at<=observed.calls;++at)for(bool after:{false,true}){auto m=seed;m.failAt=at;m.failAfter=after;compare(m);}}
 // Extreme defined integer results; preserve the existing subtraction/clamp.
 for(int value:{0,1,std::numeric_limits<int>::max()}){auto m=fixture(1,0,value,2,true);m.objects[0].health=std::numeric_limits<int>::max();compare(m);}
}
}
int main(){try{verifyWorldObjectEncoder();cases();std::cout<<"mind-overload-effects checks="<<checks<<" model-states="<<states<<" injected-faults="<<faults<<" world-fields=87 encoder-probes="<<encoderFieldProbes<<" ordered-inputs-effects=exact live-borrows=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"mind-overload failure after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
