#include "simulation/superweapons/RuntimeSupportFlightPersistence.h"
#include "core/io/RuntimeStrictSaveDouble.h"
#include "core/io/IniFile.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
namespace support_flight_pose_model_detail {
int checks=0,states=0,encoderFieldProbes=0,writerStates=0,restoreStates=0,parserStates=0,positive=0,negative=0,streamFaultStates=0,partialRejectStates=0;
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
namespace original {
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::parseRuntimeInt;
using runtime_save_text::splitRuntimeSaveFields;
// Literal original writer projected onto its canonical borrowed object vector; no test session/world type.
bool parseRuntimeFiniteDouble(const std::string& value, double& output) {
    try {
        const std::string trimmed = trimAsciiLocal(value);
        std::size_t consumed = 0;
        const double parsed = std::stod(trimmed, &consumed);
        if (consumed != trimmed.size() || !std::isfinite(parsed)) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}
bool runtimeHasSupportFlightPose(const WorldObjectInstance& object) {
    return object.alive && object.airborne && (object.mission=="ParadropDrop"
        || object.mission=="SupportFlightEast" || object.mission=="SupportFlightWest");
}
void writeRuntimeSupportFlightPoses(const std::vector<WorldObjectInstance>& objects, std::ostream& output) {
    const auto precision=output.precision(); output<<std::setprecision(17);
    output<<"\n[SupportFlightPoses]\nVersion=1\nCount="
        <<std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose)<<"\n";
    int index=0;
    for (const auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
        output<<index++<<"="<<object.instanceId<<"|"<<object.renderX<<"|"<<object.renderY
            <<"|"<<object.renderLevel<<"|"<<object.movementAnimationDistance<<"\n";
    }
    output.precision(precision);
}
bool restoreRuntimeSupportFlightPoses(const IniFile& ini,
    std::vector<WorldObjectInstance>& objects, std::string& error) {
    const auto reject=[&]() {error="invalid saved support aircraft/paratrooper pose";return false;};
    if (!ini.getSection("SupportFlightPoses")) {
        // Earlier saves retain integer world coordinates but no sub-cell
        // pose. Resume those flights from that last known position.
        for (auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
            object.hasRenderPosition=object.renderMoveActive=true;
            object.renderX=object.x; object.renderY=object.y; object.renderLevel=object.level;
        }
        return true;
    }
    int version=0,count=-1;
    const auto expected=std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose);
    if (!parseRuntimeInt(ini.getValue("SupportFlightPoses","Version",""),version) || version!=1
        || !parseRuntimeInt(ini.getValue("SupportFlightPoses","Count",""),count)
        || count!=expected) return reject();
    std::unordered_set<int> restored;
    for (int i=0;i<count;++i) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("SupportFlightPoses",std::to_string(i),""));
        int id=-1; double x=0,y=0,z=0,distance=0;
        if (fields.size()!=5 || !parseRuntimeInt(fields[0],id) || !restored.insert(id).second
            || !parseRuntimeFiniteDouble(fields[1],x) || !parseRuntimeFiniteDouble(fields[2],y)
            || !parseRuntimeFiniteDouble(fields[3],z) || !parseRuntimeFiniteDouble(fields[4],distance)
            || distance<0.0 || distance>1e9) return reject();
        const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& candidate){return candidate.instanceId==id;});
        if (object==objects.end() || !runtimeHasSupportFlightPose(*object)
            || std::abs(x-object->x)>0.500001 || std::abs(y-object->y)>0.500001
            || std::abs(z-object->level)>0.500001 || object->flightLevel!=object->level) return reject();
        object->hasRenderPosition=object->renderMoveActive=true;
        object->renderX=x; object->renderY=y; object->renderLevel=z;
        object->movementAnimationDistance=distance;
    }
    return true;
}
}
namespace current=concord::simulation::superweapons;
std::vector<WorldObjectInstance> seedObjects(int seed) {
 std::vector<WorldObjectInstance> result;
 for(int i=0;i<(seed==0?0:3);++i){WorldObjectInstance o;o.instanceId=100+i;o.kind=static_cast<WorldObjectKind>(i%3);o.mapId="map:pose";o.owner=i%2?"Beta":"Alpha";o.type="pose:type:"+std::to_string(seed);o.alive=(i!=1 || seed%2==0);o.airborne=(i!=1 || seed%3==0);o.mission=std::array<std::string,3>{"ParadropDrop","SupportFlightEast","SupportFlightWest"}[i];o.x=10+i;o.y=20-i;o.level=3;o.flightLevel=3;o.renderX=o.x+0.25;o.renderY=o.y-0.25;o.renderLevel=3.125;o.movementAnimationDistance=17.125+i;o.hasRenderPosition=seed%2;o.renderMoveActive=seed%3;o.health=77+i;
  if(seed==2)o.mission=i==0?"paradropdrop":"Guard";
  if(seed==3 && i==2)o.flightLevel=2;
  if(seed==4)o.instanceId=-1-i;
  if(seed==5 && i==1){o.instanceId=100;o.alive=false;o.airborne=true;}
  if(seed==6){o.renderX=std::bit_cast<double>(std::uint64_t(0x7ff8000000000031ULL+i));o.renderY=-0.0;}
  if(seed==7){o.movementAnimationDistance=std::numeric_limits<double>::infinity();o.renderLevel=-0.0;}
  result.push_back(o);
 }
 if(seed==5 && result.size()==3)std::swap(result[0],result[1]);
 return result;
}
struct LimitBuffer:std::streambuf {
 std::string bytes;std::size_t limit;
 explicit LimitBuffer(std::size_t n):limit(n){}
 std::streamsize xsputn(const char* s,std::streamsize n)override {const auto available=std::min<std::size_t>(static_cast<std::size_t>(n),limit-bytes.size());bytes.append(s,available);return static_cast<std::streamsize>(available);}
 int_type overflow(int_type ch)override {if(traits_type::eq_int_type(ch,traits_type::eof()))return traits_type::not_eof(ch);if(bytes.size()==limit)return traits_type::eof();bytes.push_back(traits_type::to_char_type(ch));return ch;}
};
std::string writeObserved(bool old,const std::vector<WorldObjectInstance>& objects,std::size_t limit,int precision,int style,int width,bool exceptions) {
 LimitBuffer buffer(limit);std::ostream output(&buffer);output.precision(precision);output.fill('#');
 if(style==1)output<<std::scientific;else if(style==2)output<<std::hexfloat;
 output.width(width);if(exceptions)output.exceptions(std::ios::badbit|std::ios::failbit);
 std::string thrown;
 try {if(old){original::writeRuntimeSupportFlightPoses(objects,output);}else current::writeSupportFlightPoses(objects,output);}
 catch(const std::exception& error){thrown=std::string(typeid(error).name())+":"+error.what();}
 return encode(buffer.bytes)+encode(thrown)+encode(output.precision())+encode(output.width())+encode(static_cast<unsigned long long>(output.flags()))+encode(static_cast<unsigned long long>(output.rdstate()))+encode(static_cast<unsigned long long>(output.exceptions()));
}
std::string makeDocument(const std::vector<WorldObjectInstance>& objects,int mode,int target) {
 if(mode==0)return "[Earlier]\nCount=0\n";
 std::vector<const WorldObjectInstance*> eligible;for(const auto& o:objects)if(original::runtimeHasSupportFlightPose(o))eligible.push_back(&o);
 int count=static_cast<int>(eligible.size());std::string version=mode==1?"":mode==2?"2":"1";
 std::string text="[SupportFlightPoses]\nVersion="+version+"\nCount="+(mode==3?"-1":mode==4?std::to_string(count+1):mode==5?"2147483648":std::to_string(count))+"\n";
 for(int i=0;i<count;++i){const auto& o=*eligible[i];std::array<std::string,5> fields;
  fields[0]=std::to_string(o.instanceId);
  for(int k=1;k<5;++k){std::ostringstream s;s<<std::setprecision(17)<<(k==1?double(o.x):k==2?double(o.y):k==3?double(o.level):17.125);fields[k]=s.str();}
  if(i==target){switch(mode){case 6:fields[0]="not-id";break;case 7:fields[0]="2147483648";break;case 8:fields[0]="999";break;case 9:if(i)fields[0]=std::to_string(eligible[0]->instanceId);break;case 10:fields[1]="nan";break;case 11:fields[2]="inf";break;case 12:fields[3]="1e999";break;case 13:fields[4]="-1";break;case 14:fields[4]="1000000001";break;case 15:fields[4]="1000000000";break;case 16:fields[4]="-0";break;case 17:fields[1]+="tail";break;case 18:fields[2]+="tail";break;case 19:fields[3]+="tail";break;case 20:fields[4]+="tail";break;case 21:fields[1]=std::to_string(o.x+0.500001);break;case 22:fields[1]=std::to_string(o.x+0.500002);break;case 23:fields[2]=std::to_string(o.y-0.500001);break;case 24:fields[2]=std::to_string(o.y-0.500002);break;case 25:fields[3]=std::to_string(o.level+0.500001);break;case 26:fields[3]=std::to_string(o.level+0.500002);break;case 27:fields[4]="0x1p-1074";break;case 28:fields[4]=" 17.125 ";break;case 29:fields[0]="+"+fields[0];break;case 30:fields[4]="";break;default:break;}}
  if(mode==31 && i==target)continue;
  text+=std::to_string(i)+"=";for(int k=0;k<5;++k){if(k)text+='|';text+=fields[k];}if(mode==32 && i==target)text+="|extra";text+='\n';
 }
 if(mode==33)text+="999=unused|nan|bad|extra|record\n";
 return text;
}
bool runContract(){try{
 verifyWorldObjectEncoder();require(encoderFieldProbes==87,"87 real World fields must remain observable");
 std::vector<std::string> tokens={""," ","0","-0","+0","1","-1","+17.125","1e-300","1e300","0x1p-1074","0x1.4p3","1e999","-1e999","nan","NAN","nan(31)","inf","-INFINITY","1tail","1 2","17.125tail"," 17.125 ","\t2\r\n","1e",".","+","1,5",std::string("1\0tail",6)};
 for(const auto& token:tokens)for(double initial:std::array<double,5>{-0.0,17.125,-31.5,std::numeric_limits<double>::infinity(),std::bit_cast<double>(std::uint64_t(0x7ff8000000000042ULL))}){double a=initial,b=initial;const bool x=original::parseRuntimeFiniteDouble(token,a),y=runtime_save_text::parseRuntimeFiniteDouble(token,b);require(x==y && encode(a)==encode(b),"strict parser whole-token/output bits");++parserStates;++states;}
 const std::array<std::string,15> missions={"ParadropDrop","SupportFlightEast","SupportFlightWest","paradropdrop","supportflighteast","SUPPORTFLIGHTWEST"," ParadropDrop","ParadropDrop ","Guard","Attack","","ParadropDropX","SupportFlightEast\n","Moving","Deploying"};
 for(bool alive:{false,true})for(bool airborne:{false,true})for(const auto& mission:missions){WorldObjectInstance o;o.alive=alive;o.airborne=airborne;o.mission=mission;const auto image=encode(o);require(original::runtimeHasSupportFlightPose(o)==current::hasSupportFlightPose(o) && encode(o)==image,"predicate and borrowed object unchanged");++states;}
 for(int seed=0;seed<8;++seed){auto source=seedObjects(seed);const auto originalImage=encode(source);
  std::ostringstream complete;current::writeSupportFlightPoses(source,complete);const auto length=complete.str().size();
  const std::array<std::size_t,7> limits={0,1,17,length/2,length?length-1:0,length,std::numeric_limits<std::size_t>::max()};
  for(int precision:{0,4,17})for(int style:{0,1,2})for(int width:{0,12})for(bool exceptions:{false,true})for(auto limit:limits){const auto a=writeObserved(true,source,limit,precision,style,width,exceptions),b=writeObserved(false,source,limit,precision,style,width,exceptions);require(a==b,"writer bytes/precision/flags/failure state/exception exact");require(encode(source)==originalImage,"writer canonical source unchanged");++writerStates;++states;if(limit<length)++streamFaultStates;}
  for(int mode=0;mode<34;++mode)for(int target:{0,1,2})for(const std::string& priorError:std::array<std::string,2>{"","preexisting-error"}){const auto text=makeDocument(source,mode,target);IniFile ini;require(ini.loadFromString(text),"canonical Ini load");auto a=source,b=source;std::string ae=priorError,be=priorError;const bool x=original::restoreRuntimeSupportFlightPoses(ini,a,ae),y=current::restoreSupportFlightPoses(ini,b,be);require(x==y && ae==be && encode(a)==encode(b),"restore full87 object vector/order/error/partial-write exact");require(encode(source)==originalImage,"source vector unchanged by test copies");++restoreStates;++states;if(y)++positive;else {++negative;if(encode(b)!=originalImage)++partialRejectStates;}}
 }
 require(positive>0 && negative>0 && streamFaultStates>0 && partialRejectStates>0,"positive/negative/stream faults and partial rejection must all execute");
 std::cout<<"support-flight-persistence checks="<<checks<<" states="<<states<<" world-probes="<<encoderFieldProbes<<" parser="<<parserStates<<" writer="<<writerStates<<" restore="<<restoreStates<<" positive="<<positive<<" negative="<<negative<<" stream-faults="<<streamFaultStates<<" partial-reject-mutated="<<partialRejectStates<<" complete87-and-stream-partial-failure=exact failures=0\n";return true;
}catch(const std::exception& e){std::cerr<<"support-flight-persistence failure: "<<e.what()<<'\n';return false;}}
}
