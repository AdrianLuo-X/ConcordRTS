#include "simulation/navigation/occupancy/RuntimeNearestSubcellSelection.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace nearest_subcell_model_detail {
int checks=0,states=0,encoderFieldProbes=0;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
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
int originalNearest(const std::vector<int>& available, WorldObjectInstance& grounded,
    int x, int y, double desiredX, double desiredY) {
    int selected = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (int slot : available) {
        grounded.subCell = slot;
        const auto offset = worldObjectSubCellOffset(grounded);
        const double distance = std::pow(x + offset.x - desiredX, 2)
            + std::pow(y + offset.y - desiredY, 2);
        if (distance < nearest) { nearest = distance; selected = slot; }
    }
    return selected;
}
bool runContract() {
 try {
    checks=states=encoderFieldProbes=0;verifyWorldObjectEncoder();require(encoderFieldProbes==87,"complete canonical World encoder");
    WorldObjectInstance base;
    mutateEncoderField(base.instanceId);
    mutateEncoderField(base.kind);
    mutateEncoderField(base.mapId);
    mutateEncoderField(base.owner);
    mutateEncoderField(base.type);
    mutateEncoderField(base.health);
    mutateEncoderField(base.x);
    mutateEncoderField(base.y);
    mutateEncoderField(base.cell);
    mutateEncoderField(base.level);
    mutateEncoderField(base.facing);
    mutateEncoderField(base.turretFacing);
    mutateEncoderField(base.bodyFacingClock);
    mutateEncoderField(base.turretFacingClock);
    mutateEncoderField(base.subCell);
    mutateEncoderField(base.mission);
    mutateEncoderField(base.tag);
    mutateEncoderField(base.veterancy);
    mutateEncoderField(base.veterancyExperience);
    mutateEncoderField(base.crateArmorMultiplier);
    mutateEncoderField(base.crateFirepowerMultiplier);
    mutateEncoderField(base.crateSpeedMultiplier);
    mutateEncoderField(base.crateCloakable);
    mutateEncoderField(base.harvestAutoSearchSuspended);
    mutateEncoderField(base.hasBeenCaptured);
    mutateEncoderField(base.onBridge);
    mutateEncoderField(base.authoredAircraft);
    mutateEncoderField(base.alive);
    mutateEncoderField(base.rubble);
    mutateEncoderField(base.submerged);
    mutateEncoderField(base.cloaked);
    mutateEncoderField(base.cloakCooldownTicks);
    mutateEncoderField(base.submergeCooldownTicks);
    mutateEncoderField(base.deployed);
    mutateEncoderField(base.deployStateTicksRemaining);
    mutateEncoderField(base.prone);
    mutateEncoderField(base.proneTicksRemaining);
    mutateEncoderField(base.weaponCooldownTicksRemaining);
    mutateEncoderField(base.spriteFiringStartTick);
    mutateEncoderField(base.radiationEruptionCooldownTicksRemaining);
    mutateEncoderField(base.hasRenderPosition);
    mutateEncoderField(base.renderX);
    mutateEncoderField(base.renderY);
    mutateEncoderField(base.renderMoveActive);
    mutateEncoderField(base.renderLevel);
    mutateEncoderField(base.renderOnBridge);
    mutateEncoderField(base.movementAnimationDistance);
    mutateEncoderField(base.buildAnimTicksTotal);
    mutateEncoderField(base.buildAnimTicksRemaining);
    mutateEncoderField(base.structureRecoilStartTick);
    mutateEncoderField(base.chargeTurretStartTick);
    mutateEncoderField(base.workAnimationActive);
    mutateEncoderField(base.poweredOperational);
    mutateEncoderField(base.powerAnimationPausedAtTick);
    mutateEncoderField(base.powerAnimationPausedTicks);
    mutateEncoderField(base.factoryExitAnimationStartTick);
    mutateEncoderField(base.factoryExitAnimationEndTick);
    mutateEncoderField(base.factoryExitAirborne);
    mutateEncoderField(base.factoryExitHostId);
    mutateEncoderField(base.specialCashNextTick);
    mutateEncoderField(base.specialCloneDeliveryObjectId);
    mutateEncoderField(base.transportPassengerIds);
    mutateEncoderField(base.bioReactorAnimationStartTick);
    mutateEncoderField(base.bioReactorAnimationPoweredStartTick);
    mutateEncoderField(base.activeGunnerMode);
    mutateEncoderField(base.activeGunnerWeaponIndex);
    mutateEncoderField(base.activeGunnerTurretIndex);
    mutateEncoderField(base.tankBunkerHostId);
    mutateEncoderField(base.tankBunkerOccupantId);
    mutateEncoderField(base.tankBunkerAnimationStartTick);
    mutateEncoderField(base.tankBunkerLowering);
    mutateEncoderField(base.dockedToObjectId);
    mutateEncoderField(base.dockIndex);
    mutateEncoderField(base.airborne);
    mutateEncoderField(base.flightLevel);
    mutateEncoderField(base.aircraftDockFlightProgress);
    mutateEncoderField(base.aircraftBuildingClearanceLevel);
    mutateEncoderField(base.wallConnectionMask);
    mutateEncoderField(base.garrisonHostObjectId);
    mutateEncoderField(base.garrisonOccupantIds);
    mutateEncoderField(base.garrisonFireCursor);
    mutateEncoderField(base.currentAmmo);
    mutateEncoderField(base.pendingAircraftAuxSounds);
    mutateEncoderField(base.turretSoundActivityTick);
    mutateEncoderField(base.activeTurretSoundEvent);
    mutateEncoderField(base.pendingTurretSounds);
    mutateEncoderField(base.modExtensions);
    base.instanceId=42;base.kind=WorldObjectKind::Infantry;
    base.airborne=base.authoredAircraft=false;
    using concord::simulation::navigation::selectNearestAvailableSubCell;
    for(const auto& expected:std::array<std::pair<std::pair<double,double>,int>,4>{{{{.25,-.25},2},{{.25,.25},4},{{-.25,.25},3},{{0.,0.},2}}}) {
        auto local=base;require(selectNearestAvailableSubCell({2,4,3},local,0,0,expected.first.first,expected.first.second)==expected.second,"known canonical quarter-slot and first-tie result");
    }
    {auto local=base;const int initial=local.subCell;require(selectNearestAvailableSubCell({},local,0,0,0,0)==-1&&local.subCell==initial,"empty availability retains original local");}
    {auto local=base;require(selectNearestAvailableSubCell({3,4,2},local,0,0,0,0)==3&&local.subCell==2,"strict tie preserves supplied order and last local slot");}
    std::vector<std::vector<int>> lists{{}};const std::array<int,3> legal{2,4,3};
    for(int mask=1;mask<8;++mask){std::vector<int> list;for(int i=0;i<3;++i)if(mask&(1<<i))list.push_back(legal[i]);std::sort(list.begin(),list.end());do{lists.push_back(list);}while(std::next_permutation(list.begin(),list.end()));}
    lists.push_back({2,2});lists.push_back({4,2,4,3});lists.push_back({3,3,4});lists.push_back({2,4,3,2});require(lists.size()==20,"all legal subsets/permutations and repeated candidates");
    const double inf=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
    const std::array<std::pair<double,double>,15> desired{{{0,0},{.25,-.25},{.25,.25},{-.25,.25},{.5,0},{-.5,0},{0,.5},{0,-.5},{-0.0,0.0},{std::nextafter(0.0,1.0),0},{1e300,-1e300},{inf,0},{nan,0},{0,nan},{-inf,inf}}};
    const std::array<std::pair<int,int>,4> cells{{{0,0},{89,97},{-112,-54},{INT_MAX,INT_MIN}}};
    const std::array<WorldObjectKind,4> kinds{WorldObjectKind::Infantry,WorldObjectKind::Unit,WorldObjectKind::Structure,static_cast<WorldObjectKind>(99)};
    for(const auto& available:lists)for(auto kind:kinds)for(int pose=0;pose<4;++pose)for(const auto& cell:cells)for(const auto& aim:desired) {
        auto source=base;source.kind=kind;source.airborne=(pose&1)!=0;source.authoredAircraft=(pose&2)!=0;source.subCell=-17;
        const auto sourceImage=encode(source);auto reference=source,actual=source;
        const double dx=cell.first+aim.first,dy=cell.second+aim.second;
        const int a=originalNearest(available,reference,cell.first,cell.second,dx,dy);
        const int b=selectNearestAvailableSubCell(available,actual,cell.first,cell.second,dx,dy);
        ++states;require(a==b,"original nearest result");require(encode(reference)==encode(actual),"complete original local mutation including last candidate/NaN");require(encode(source)==sourceImage,"borrowed source object preserved");
    }
    require(states==19200,"complete unfiltered matrix");
    std::cout<<"nearest-subcell checks="<<checks<<" states="<<states<<" world-fields=87 world-probes="<<encoderFieldProbes<<" ordered-kernel-full-local-and-source=exact failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"nearest-subcell failure: "<<e.what()<<"\n";return false;}
}
}
