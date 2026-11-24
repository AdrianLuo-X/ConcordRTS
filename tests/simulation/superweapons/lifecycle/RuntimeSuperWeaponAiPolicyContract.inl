#include "simulation/superweapons/RuntimeSuperWeaponAiPolicy.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
namespace superweapon_ai_model_detail {
int checks=0,states=0,encoderFieldProbes=0,aiProbes=0,eventProbes=0,snapshotProbes=0,baseCases=0,injectedFaults=0,positiveDefense=0,positiveOrdinary=0,continuations=0;
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
std::string rec_RuntimeComputerAiState(const RuntimeComputerAiState& v){return encode(v.initialized)+encode(v.owner)+encode(v.country)+encode(v.difficulty)+encode(v.baseCenter)+encode(v.credits)+encode(v.nextThinkTick)+encode(v.nextIncomeTick)+encode(v.nextBuildTick)+encode(v.nextAttackTick)+encode(v.nextRepairTick)+encode(v.productionSerial)+encode(v.incomeEarned)+encode(v.creditsSpent)+encode(v.structuresBuilt)+encode(v.infantryBuilt)+encode(v.unitsBuilt)+encode(v.freeUnitsReceived)+encode(v.attackWaves)+encode(v.attacksOrdered)+encode(v.defenseOrders)+encode(v.tacticalOrders)+encode(v.repairsPerformed)+encode(v.stalledBuilds)+encode(v.lastTargetObjectId)+encode(v.lastBuildType)+encode(v.status);}
std::string rec_SuperWeaponEvent(const SuperWeaponEvent& v){return encode(v.tick)+encode(v.instanceId)+encode(v.activeToken)+encode(v.sourceBuildingId)+encode(v.soundSourceObjectId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.value)+encode(v.owner)+encode(v.superWeaponId)+encode(v.kind)+encode(v.state)+encode(v.reason)+encode(v.commandVerb)+encode(v.soundIntent)+encode(v.evaIntent)+encode(v.animationIntent)+encode(v.targetCell)+encode(v.targetCell2);}
std::string rec_SuperWeaponSidebarSnapshot(const SuperWeaponSidebarSnapshot& v){return encode(v.instanceId)+encode(v.owner)+encode(v.superWeaponId)+encode(v.uiName)+encode(v.name)+encode(v.sidebarImage)+encode(v.state)+encode(v.progress)+encode(v.remainingTicks)+encode(v.rechargeTicks)+encode(v.stateStartTick)+encode(v.launchTick)+encode(v.flashSidebarTabFrames)+encode(v.targetRange)+encode(v.targetLineMultiplier)+encode(v.showTimer)+encode(v.selected)+encode(v.targetSelectionActive)+encode(v.firstTargetAccepted)+encode(v.firstTargetCell)+encode(v.chargePaused);}
std::string rec_SuperWeaponRenderSnapshot(const SuperWeaponRenderSnapshot& v){return encode(v.instanceId)+encode(v.tick)+encode(v.owner)+encode(v.superWeaponId)+encode(v.kind)+encode(v.phase)+encode(v.resourceId)+encode(v.sourceObjectId)+encode(v.targetObjectId)+encode(v.cell)+encode(v.cell2)+encode(v.radius)+encode(v.sourceLevel)+encode(v.intensity)+encode(v.progress)+encode(v.requiredProgress)+encode(v.expireTick)+encode(v.visibleToAll);}
std::string rec_SuperWeaponActiveLifecycleSnapshot(const SuperWeaponActiveLifecycleSnapshot& v){return encode(v.instanceId)+encode(v.activeToken)+encode(v.superWeaponId)+encode(v.superWeaponType)+encode(v.sourceOwner)+encode(v.sourceObjectId)+encode(v.cell)+encode(v.startedTick)+encode(v.activeUntilTick);}
void verifyExtraEncoders(){
{RuntimeComputerAiState base;const auto expected=rec_RuntimeComputerAiState(base);
{auto v=base;mutateEncoderField(v.initialized);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.initialized");++aiProbes;}
{auto v=base;mutateEncoderField(v.owner);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.owner");++aiProbes;}
{auto v=base;mutateEncoderField(v.country);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.country");++aiProbes;}
{auto v=base;mutateEncoderField(v.difficulty);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.difficulty");++aiProbes;}
{auto v=base;mutateEncoderField(v.baseCenter.x);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.baseCenter.x");++aiProbes;}
{auto v=base;mutateEncoderField(v.baseCenter.y);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.baseCenter.y");++aiProbes;}
{auto v=base;mutateEncoderField(v.credits);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.credits");++aiProbes;}
{auto v=base;mutateEncoderField(v.nextThinkTick);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.nextThinkTick");++aiProbes;}
{auto v=base;mutateEncoderField(v.nextIncomeTick);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.nextIncomeTick");++aiProbes;}
{auto v=base;mutateEncoderField(v.nextBuildTick);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.nextBuildTick");++aiProbes;}
{auto v=base;mutateEncoderField(v.nextAttackTick);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.nextAttackTick");++aiProbes;}
{auto v=base;mutateEncoderField(v.nextRepairTick);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.nextRepairTick");++aiProbes;}
{auto v=base;mutateEncoderField(v.productionSerial);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.productionSerial");++aiProbes;}
{auto v=base;mutateEncoderField(v.incomeEarned);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.incomeEarned");++aiProbes;}
{auto v=base;mutateEncoderField(v.creditsSpent);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.creditsSpent");++aiProbes;}
{auto v=base;mutateEncoderField(v.structuresBuilt);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.structuresBuilt");++aiProbes;}
{auto v=base;mutateEncoderField(v.infantryBuilt);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.infantryBuilt");++aiProbes;}
{auto v=base;mutateEncoderField(v.unitsBuilt);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.unitsBuilt");++aiProbes;}
{auto v=base;mutateEncoderField(v.freeUnitsReceived);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.freeUnitsReceived");++aiProbes;}
{auto v=base;mutateEncoderField(v.attackWaves);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.attackWaves");++aiProbes;}
{auto v=base;mutateEncoderField(v.attacksOrdered);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.attacksOrdered");++aiProbes;}
{auto v=base;mutateEncoderField(v.defenseOrders);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.defenseOrders");++aiProbes;}
{auto v=base;mutateEncoderField(v.tacticalOrders);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.tacticalOrders");++aiProbes;}
{auto v=base;mutateEncoderField(v.repairsPerformed);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.repairsPerformed");++aiProbes;}
{auto v=base;mutateEncoderField(v.stalledBuilds);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.stalledBuilds");++aiProbes;}
{auto v=base;mutateEncoderField(v.lastTargetObjectId);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.lastTargetObjectId");++aiProbes;}
{auto v=base;mutateEncoderField(v.lastBuildType);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.lastBuildType");++aiProbes;}
{auto v=base;mutateEncoderField(v.status);require(rec_RuntimeComputerAiState(v)!=expected,"invisible RuntimeComputerAiState.status");++aiProbes;}
}
{SuperWeaponEvent base;const auto expected=rec_SuperWeaponEvent(base);
{auto v=base;mutateEncoderField(v.tick);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.tick");++eventProbes;}
{auto v=base;mutateEncoderField(v.instanceId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.instanceId");++eventProbes;}
{auto v=base;mutateEncoderField(v.activeToken);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.activeToken");++eventProbes;}
{auto v=base;mutateEncoderField(v.sourceBuildingId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.sourceBuildingId");++eventProbes;}
{auto v=base;mutateEncoderField(v.soundSourceObjectId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.soundSourceObjectId");++eventProbes;}
{auto v=base;mutateEncoderField(v.objectId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.objectId");++eventProbes;}
{auto v=base;mutateEncoderField(v.targetObjectId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.targetObjectId");++eventProbes;}
{auto v=base;mutateEncoderField(v.value);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.value");++eventProbes;}
{auto v=base;mutateEncoderField(v.owner);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.owner");++eventProbes;}
{auto v=base;mutateEncoderField(v.superWeaponId);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.superWeaponId");++eventProbes;}
{auto v=base;mutateEncoderField(v.kind);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.kind");++eventProbes;}
{auto v=base;mutateEncoderField(v.state);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.state");++eventProbes;}
{auto v=base;mutateEncoderField(v.reason);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.reason");++eventProbes;}
{auto v=base;mutateEncoderField(v.commandVerb);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.commandVerb");++eventProbes;}
{auto v=base;mutateEncoderField(v.soundIntent);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.soundIntent");++eventProbes;}
{auto v=base;mutateEncoderField(v.evaIntent);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.evaIntent");++eventProbes;}
{auto v=base;mutateEncoderField(v.animationIntent);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.animationIntent");++eventProbes;}
{auto v=base;mutateEncoderField(v.targetCell.x);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.targetCell.x");++eventProbes;}
{auto v=base;mutateEncoderField(v.targetCell.y);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.targetCell.y");++eventProbes;}
{auto v=base;mutateEncoderField(v.targetCell2.x);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.targetCell2.x");++eventProbes;}
{auto v=base;mutateEncoderField(v.targetCell2.y);require(rec_SuperWeaponEvent(v)!=expected,"invisible SuperWeaponEvent.targetCell2.y");++eventProbes;}
}
{SuperWeaponSidebarSnapshot base;const auto expected=rec_SuperWeaponSidebarSnapshot(base);
{auto v=base;mutateEncoderField(v.instanceId);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.instanceId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.owner);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.owner");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.superWeaponId);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.superWeaponId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.uiName);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.uiName");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.name);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.name");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.sidebarImage);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.sidebarImage");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.state);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.state");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.progress);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.progress");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.remainingTicks);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.remainingTicks");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.rechargeTicks);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.rechargeTicks");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.stateStartTick);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.stateStartTick");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.launchTick);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.launchTick");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.flashSidebarTabFrames);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.flashSidebarTabFrames");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.targetRange);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.targetRange");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.targetLineMultiplier);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.targetLineMultiplier");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.showTimer);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.showTimer");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.selected);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.selected");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.targetSelectionActive);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.targetSelectionActive");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.firstTargetAccepted);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.firstTargetAccepted");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.firstTargetCell.x);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.firstTargetCell.x");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.firstTargetCell.y);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.firstTargetCell.y");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.chargePaused);require(rec_SuperWeaponSidebarSnapshot(v)!=expected,"invisible SuperWeaponSidebarSnapshot.chargePaused");++snapshotProbes;}
}
{SuperWeaponRenderSnapshot base;const auto expected=rec_SuperWeaponRenderSnapshot(base);
{auto v=base;mutateEncoderField(v.instanceId);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.instanceId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.tick);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.tick");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.owner);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.owner");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.superWeaponId);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.superWeaponId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.kind);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.kind");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.phase);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.phase");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.resourceId);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.resourceId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.sourceObjectId);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.sourceObjectId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.targetObjectId);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.targetObjectId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell.x);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.cell.x");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell.y);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.cell.y");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell2.x);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.cell2.x");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell2.y);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.cell2.y");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.radius);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.radius");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.sourceLevel);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.sourceLevel");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.intensity);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.intensity");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.progress);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.progress");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.requiredProgress);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.requiredProgress");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.expireTick);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.expireTick");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.visibleToAll);require(rec_SuperWeaponRenderSnapshot(v)!=expected,"invisible SuperWeaponRenderSnapshot.visibleToAll");++snapshotProbes;}
}
{SuperWeaponActiveLifecycleSnapshot base;const auto expected=rec_SuperWeaponActiveLifecycleSnapshot(base);
{auto v=base;mutateEncoderField(v.instanceId);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.instanceId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.activeToken);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.activeToken");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.superWeaponId);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.superWeaponId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.superWeaponType);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.superWeaponType");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.sourceOwner);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.sourceOwner");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.sourceObjectId);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.sourceObjectId");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell.x);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.cell.x");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.cell.y);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.cell.y");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.startedTick);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.startedTick");++snapshotProbes;}
{auto v=base;mutateEncoderField(v.activeUntilTick);require(rec_SuperWeaponActiveLifecycleSnapshot(v)!=expected,"invisible SuperWeaponActiveLifecycleSnapshot.activeUntilTick");++snapshotProbes;}
}
}
namespace original {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
int runtimeAiSuperDefenseProbabilityIndex(const std::string& difficulty) {
    const std::string key = toUpperAsciiLocal(trimAsciiLocal(difficulty));
    if (key == "HARD" || key == "BRUTAL") {
        return 0;
    }
    if (key == "EASY") {
        return 2;
    }
    return 1;
}
bool runtimeIsForceShieldSuperWeaponRule(const SuperWeaponRule* rule) {
    if (rule == nullptr) {
        return false;
    }
    const std::string identity = toLowerAsciiLocal(
        trimAsciiLocal(rule->type) + " " + trimAsciiLocal(rule->action)
    );
    return identity.find("forceshield") != std::string::npos
        || identity.find("force shield") != std::string::npos;
}
bool execute(bool computerAiEnabled,
    std::unordered_map<std::string, RuntimeComputerAiState>& computerAiByOwner,
    const RulesDatabase& rules, SuperWeaponSystem& superWeapons,
    const SuperWeaponWorldContext& context, const int& tick,
    const bool& campaignMode, const std::string& humanOwner,
    concord::simulation::superweapons::RuntimeSuperWeaponAiBindings& bindings) {
    bool changed = false;
    std::vector<std::string> computerOwners;
    computerOwners.reserve(computerAiByOwner.size());
    for (const auto& [ownerKey, ai] : computerAiByOwner) {
        (void)ai;
        computerOwners.push_back(ownerKey);
    }
    std::sort(computerOwners.begin(), computerOwners.end());
    const SpecialMechanismRules& mechanismRules =
        rules.getSpecialMechanismRules();
    for (const std::string& ownerKey : computerOwners) {
        if (!computerAiEnabled) {
            break;
        }
        auto found = computerAiByOwner.find(ownerKey);
        if (found == computerAiByOwner.end()
            || !found->second.initialized
            || trimAsciiLocal(found->second.owner).empty()) {
            continue;
        }
        RuntimeComputerAiState& ai = found->second;
        bindings.updateBaseCenter(ai);
        const int probabilityIndex =
            runtimeAiSuperDefenseProbabilityIndex(ai.difficulty);
        std::string reason;
        if (superWeapons.fireAiDefense(
                ai.owner,
                {ai.baseCenter.x, ai.baseCenter.y},
                mechanismRules.aiSuperDefense.probabilityPercent[
                    static_cast<std::size_t>(probabilityIndex)
                ],
                mechanismRules.aiSuperDefense.alertFrames,
                mechanismRules.aiSuperDefense.distanceCells,
                context,
                &reason
            )) {
            changed = true;
        }
    }

    // The runtime does not have a separate computer-player command queue
    // yet.  Fire ready enemy super weapons at a fixed cadence through the
    // same validated command path used by the player, so AI effects remain
    // deterministic and replay-visible instead of bypassing the system.
    if (computerAiEnabled
        && tick > 0
        && (tick % std::max(1, context.fixedTickHz)) == 0) {
        std::vector<std::pair<std::string, std::string>> aiWeapons;
        for (const SuperWeaponSidebarSnapshot& snapshot :
                superWeapons.sidebarSnapshots()) {
            const SuperWeaponRule* rule = rules.findSuperWeaponType(
                snapshot.superWeaponId
            );
            if (snapshot.state == SuperWeaponRuntimeState::Ready
                && !runtimeIsForceShieldSuperWeaponRule(rule)
                && !bindings.reservesSuperWeapon(snapshot.owner, rule)
                && (campaignMode || computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))))
                && toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(humanOwner))) {
                aiWeapons.emplace_back(snapshot.owner, snapshot.superWeaponId);
            }
        }
        for (const auto& [owner, superWeaponId] : aiWeapons) {
            std::string reason;
            if (superWeapons.fireAi(owner, superWeaponId, context, &reason)) {
                changed = true;
            }
        }
    }
    return changed;
}
}
namespace current=concord::simulation::superweapons;
struct Fixture {
 RulesDatabase rules;std::vector<SuperWeaponRule> defs;std::vector<WorldObjectInstance> objects;
 Fixture(){IniFile ini;require(ini.loadFromString(R"INI([General]
AISuperDefenseProbability=100,50,0
AISuperDefenseFrames=50
AISuperDefenseDistance=12
[SuperWeaponTypes]
0=DefenseThreat
1=DefenseShield
2=OrdinaryReveal
[DefenseThreat]
Type=MultiMissile
Action=MultiMissile
AIDefendAgainst=yes
[DefenseShield]
Type=ForceShield
Action=ForceShield
[OrdinaryReveal]
Type=PsychicReveal
Action=PsychicReveal
)INI") && rules.loadFromIni(ini),"realRules load");
  require(rules.getSuperWeaponTypes().size()==3,"allthree realdefinitions");
  auto threat=*rules.findSuperWeaponType("DefenseThreat");threat.targetMode="Area";threat.aiDefendAgainst=true;threat.targetEnemy=true;threat.targetFriendly=false;
  auto shield=*rules.findSuperWeaponType("DefenseShield");shield.targetMode="Area";shield.targetEnemy=false;shield.targetFriendly=true;shield.targetInfantry=false;shield.targetUnits=false;shield.targetBuildings=true;
  auto reveal=*rules.findSuperWeaponType("OrdinaryReveal");reveal.targetMode="Area";reveal.targetEnemy=true;reveal.targetFriendly=false;defs={threat,shield,reveal};
  require(rules.getSpecialMechanismRules().aiSuperDefense.probabilityPercent==std::array<int,3>{100,50,0},"real probability tuple parsed");
  for(int i=0;i<5;++i){WorldObjectInstance o;o.instanceId=801+i;o.kind=WorldObjectKind::Structure;o.mapId="canonical-ai";o.owner=std::array<std::string,5>{"defender","ally","attacker","beta","human"}[i];o.type="HQ";o.health=1000;o.alive=true;o.x=i==0?2:i==1?12:11+i;o.y=i==0?2:10;objects.push_back(o);}
 }
};
struct Run;
struct Bindings final:current::RuntimeSuperWeaponAiBindings {
 Run& run;explicit Bindings(Run& r):run(r){}
 void updateBaseCenter(RuntimeComputerAiState&)override;
 bool reservesSuperWeapon(const std::string&,const SuperWeaponRule*)const override;
};
struct Run {
 const Fixture& fixture;SuperWeaponSystem system;std::vector<WorldObjectInstance> objects;
 std::unordered_map<std::string,RuntimeComputerAiState> ais;SuperWeaponWorldContext context;Bindings bindings;
 int tick,mode,scenario,faultAt=0,calls=0,updates=0;bool campaign,enabled,changed=false,threw=false;std::string human="human",error;std::vector<std::string> trace;std::vector<SuperWeaponEvent> events;
 Run(const Fixture& f,int sc,int mapSeed,bool en,bool camp,int t,int m):fixture(f),objects(f.objects),bindings(*this),tick(t),mode(m),scenario(sc),campaign(camp),enabled(en){
  system.reset(f.defs);context.tick=std::max(0,t-5);context.fixedTickHz=30;context.mapWidth=context.mapHeight=32;context.objects=&objects;
  context.areAllied=[](const std::string& a,const std::string& b){return a==b||(a=="defender"&&b=="ally")||(a=="ally"&&b=="defender");};context.cellLegal=[](int x,int y,SuperWeaponTargetMode){return x>=0&&y>=0&&x<32&&y<32;};
  for(const std::string& owner:{"defender","beta","human"})require(system.grant(owner,"OrdinaryReveal") && system.forceReady(owner,"OrdinaryReveal"),"ordinary setup");
  require(system.grant("defender","DefenseShield") && system.forceReady("defender","DefenseShield"),"shieldsetup");
  if(sc!=0){std::string reason;require(system.grant("attacker","DefenseThreat") && system.forceReady("attacker","DefenseThreat") && system.beginTargetSelection("attacker","DefenseThreat",context,&reason) && system.submitTarget("attacker","DefenseThreat",{{13,10},-1},context,&reason),"real hostilethreatsetup:"+reason);}
  if(sc==2)require(system.disable("defender","DefenseShield"),"disabled shieldsetup");
  context.tick=t;
  auto add=[&](const std::string& key,const std::string& owner,const std::string& difficulty,bool initialized){RuntimeComputerAiState ai;ai.initialized=initialized;ai.owner=owner;ai.country="country";ai.difficulty=difficulty;ai.credits=431;ai.status="original-status";ais.emplace(key,ai);};
  if(mapSeed==0){add("defender","defender","Hard",true);add("beta","beta","Medium",true);add("empty","  ","Easy",true);add("uninit","attacker","Brutal",false);}
  if(mapSeed==1){add("DEFENDER","defender","Brutal",true);add("beta","beta","Easy",false);}
  if(mapSeed==3){add("defender","  ","Medium",true);add("beta","beta"," Easy ",true);add("zz","defender"," BRUTAL ",true);}
 }
 void touch(const std::string& tag){trace.push_back(tag);++calls;if(faultAt==calls)throw std::runtime_error("injected-policy-boundary-"+std::to_string(calls));}
 void installCallbacks(){
  context.areAllied=[this](const std::string& a,const std::string& b){touch("A:"+a+":"+b);return a==b||(a=="defender"&&b=="ally")||(a=="ally"&&b=="defender");};
  context.cellLegal=[this](int x,int y,SuperWeaponTargetMode modeValue){touch("L:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(static_cast<int>(modeValue)));return x>=0&&y>=0&&x<32&&y<32;};
  context.ownerPowered=[this](const std::string& owner){touch("P:"+owner);return true;};
  context.aiTargetVisible=[this](const std::string& owner,const WorldObjectInstance& object){touch("V:"+owner+":"+std::to_string(object.instanceId));return scenario!=3;};
  context.emit=[this](const SuperWeaponEvent& e){touch("E:"+rec_SuperWeaponEvent(e));events.push_back(e);};
 }
 void execute(bool old,int fault){faultAt=fault;installCallbacks();try{
  changed=old?original::execute(enabled,ais,fixture.rules,system,context,tick,campaign,human,bindings):current::advanceRuntimeSuperWeaponAiPolicy(enabled,ais,fixture.rules,system,context,tick,campaign,human,bindings);
 }catch(const std::exception& e){threw=true;error=e.what();}}
 std::string aiImage()const {std::string out;for(const auto& [key,ai]:ais)out+=encode(key)+rec_RuntimeComputerAiState(ai);return out;}
 std::string eventImage()const {std::string out;for(const auto& e:events)out+=rec_SuperWeaponEvent(e);return out;}
 std::string systemImage()const {std::ostringstream out;system.save(out);std::string snapshots;
  for(const auto& v:system.sidebarSnapshots())snapshots+=rec_SuperWeaponSidebarSnapshot(v);
  for(const auto& v:system.renderSnapshots())snapshots+=rec_SuperWeaponRenderSnapshot(v);
  for(const auto& v:system.activeLifecycleSnapshots())snapshots+=rec_SuperWeaponActiveLifecycleSnapshot(v);
  return encode(out.str())+encode(system.stateHash())+encode(snapshots);
 }
};
void Bindings::updateBaseCenter(RuntimeComputerAiState& ai){run.touch("U:"+ai.owner);++run.updates;ai.baseCenter={ai.owner=="beta"?11:10,10};ai.credits+=3;++ai.defenseOrders;ai.status="base-center-updated";if(run.mode==3 && run.updates==1){run.campaign=!run.campaign;run.human="beta";run.tick=30;}}
bool Bindings::reservesSuperWeapon(const std::string& owner,const SuperWeaponRule* rule)const {run.touch("R:"+owner+":"+(rule?rule->id:"<null>"));return run.mode==1 || (run.mode==2 && owner=="beta");}
void compare(const Run& a,const Run& b){require(a.changed==b.changed && a.threw==b.threw && a.error==b.error,"return/exception exact");require(a.trace==b.trace && a.calls==b.calls,"all ordered borrowed queries/effects exact");require(a.aiImage()==b.aiImage(),"complete27AI fields/order and28leafs exact");require(encode(a.objects)==encode(b.objects),"complete87worldfields exact");require(a.systemImage()==b.systemImage(),"complete canonical systemsave/RNG/hash/sidebar/render/lifecycle exact");require(a.eventImage()==b.eventImage(),"all21eventleafs exact");require(a.tick==b.tick && a.campaign==b.campaign && a.human==b.human,"borrowed liveflag references exact");}
bool runContract(){try{
 verifyWorldObjectEncoder();verifyExtraEncoders();require(encoderFieldProbes==87 && aiProbes==28 && eventProbes==21 && snapshotProbes==52,"all complete encoders must have real individual fieldprobes");
 for(const std::string& difficulty:std::array<std::string,9>{"Hard"," hard ","BRUTAL","Easy"," easy ","Medium","unknown",""," \t"})require(original::runtimeAiSuperDefenseProbabilityIndex(difficulty)==current::runtimeAiSuperDefenseProbabilityIndex(difficulty),"originaldifficulty classification");
 require(!current::runtimeIsForceShieldSuperWeaponRule(nullptr),"null classifier");
 for(const std::string& type:std::array<std::string,5>{"ForceShield"," force shield ","Other","","ForceShieldX"})for(const std::string& action:std::array<std::string,4>{"ForceShield","force shield","Other",""}){SuperWeaponRule rule;rule.type=type;rule.action=action;require(original::runtimeIsForceShieldSuperWeaponRule(&rule)==current::runtimeIsForceShieldSuperWeaponRule(&rule),"original exact classifier");}
 Fixture fixture;
 for(int scenario=0;scenario<4;++scenario)for(int mapSeed=0;mapSeed<4;++mapSeed)for(bool enabled:{false,true})for(bool campaign:{false,true})for(int tick:{0,30,31,60})for(int mode=0;mode<4;++mode){
  Run baseline(fixture,scenario,mapSeed,enabled,campaign,tick,mode),actual(fixture,scenario,mapSeed,enabled,campaign,tick,mode);baseline.execute(true,0);actual.execute(false,0);compare(baseline,actual);require(!baseline.threw,"no injection must finish");++baseCases;++states;
  if(actual.system.stateOf("defender","DefenseShield")!=SuperWeaponRuntimeState::Ready && scenario!=2)++positiveDefense;
  if(actual.system.stateOf("beta","OrdinaryReveal")!=SuperWeaponRuntimeState::Ready)++positiveOrdinary;
  const int count=baseline.calls;require(count<600,"bounded realmodel branch");
  for(int fault=1;fault<=count;++fault){Run oldFault(fixture,scenario,mapSeed,enabled,campaign,tick,mode),newFault(fixture,scenario,mapSeed,enabled,campaign,tick,mode);oldFault.execute(true,fault);newFault.execute(false,fault);compare(oldFault,newFault);require(oldFault.threw && newFault.threw,"every query/effect fault must execute");++injectedFaults;++states;}
  baseline.faultAt=actual.faultAt=0;
  for(int step=1;step<=3;++step){++baseline.context.tick;++actual.context.tick;baseline.system.tick(baseline.context);actual.system.tick(actual.context);compare(baseline,actual);++continuations;}
 }
 require(positiveDefense>0 && positiveOrdinary>0 && injectedFaults>0,"real defense/ordinary/fault coverage mandatory");
 std::cout<<"superweapon-ai-policy checks="<<checks<<" states="<<states<<" base="<<baseCases<<" faults="<<injectedFaults<<" world-probes="<<encoderFieldProbes<<" ai-leaf-probes="<<aiProbes<<" event-leaf-probes="<<eventProbes<<" snapshot-leaf-probes="<<snapshotProbes<<" positive-defense="<<positiveDefense<<" positive-ordinary="<<positiveOrdinary<<" continuation="<<continuations<<" full-canonical-save-RNG-order-and-borrows=exact failures=0\n";return true;
}catch(const std::exception& e){std::cerr<<"superweapon-ai-policy failure: "<<e.what()<<'\n';return false;}}
}
