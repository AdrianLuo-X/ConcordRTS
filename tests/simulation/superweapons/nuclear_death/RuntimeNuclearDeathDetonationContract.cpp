#include "simulation/superweapons/RuntimeNuclearDeathDetonation.h"
#include "world/model/GameWorld.h"
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
namespace domain=concord::simulation::superweapons;
namespace {
using domain::RuntimeProjectileEffect;
using domain::RuntimeProjectileTargetPosition;
using domain::RuntimeProjectileLifecycleState;
int checks=0,states=0,faults=0;
void require(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
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
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}


template<class T> void seed(T& v,int& n){
 if constexpr(std::is_same_v<T,bool>)v=(++n%2)!=0;
 else if constexpr(std::is_integral_v<T>){++n;if constexpr(std::is_unsigned_v<T>)v=static_cast<T>(0xA022u+static_cast<unsigned>(n));else v=static_cast<T>(n*7-401);}
 else if constexpr(std::is_floating_point_v<T>){v=static_cast<T>((++n)*0.125);}
 else if constexpr(std::is_enum_v<T>)v=static_cast<T>((++n)%4);
 else if constexpr(std::is_same_v<T,std::string>)v="untouched-marker-"+std::to_string(++n);
 else if constexpr(requires{v.voxelDebris;v.parentActorId;v.deathWeaponRequestId;}){seed(v.voxelDebris,n);seed(v.homingInitialized,n);seed(v.homingYaw,n);seed(v.homingPitch,n);seed(v.actorId,n);seed(v.parentActorId,n);seed(v.generation,n);seed(v.attackerObjectId,n);seed(v.targetObjectId,n);seed(v.sourceAnchorObjectId,n);seed(v.owner,n);seed(v.fromCell,n);seed(v.toCell,n);seed(v.currentCell,n);seed(v.hasSourcePosition,n);seed(v.sourceX,n);seed(v.sourceY,n);seed(v.sourceZ,n);seed(v.sourceScreenOffsetX,n);seed(v.sourceScreenOffsetY,n);seed(v.sourceNativeFixedX,n);seed(v.sourceNativeFixedY,n);seed(v.hasTargetPosition,n);seed(v.targetX,n);seed(v.targetY,n);seed(v.targetZ,n);seed(v.fromLevel,n);seed(v.toLevel,n);seed(v.currentLevel,n);seed(v.attackerFacing,n);seed(v.targetFacing,n);seed(v.spawnTick,n);seed(v.impactTick,n);seed(v.elapsedTicks,n);seed(v.damage,n);seed(v.veteranDamageMultiplier,n);seed(v.spawnedMissileDamage,n);seed(v.spawnedMissileBaseDamage,n);seed(v.totalTicks,n);seed(v.remainingTicks,n);seed(v.lifecycle,n);seed(v.lifecycleState,n);seed(v.renderIntent,n);seed(v.weaponId,n);seed(v.weaponSlot,n);seed(v.projectileId,n);seed(v.projectileImageId,n);seed(v.warheadId,n);seed(v.weaponAnimId,n);seed(v.impactAnimId,n);seed(v.trailerAnimId,n);seed(v.trailerIntentId,n);seed(v.warheadImpactIntentId,n);seed(v.reportSoundId,n);seed(v.arcing,n);seed(v.vertical,n);seed(v.acceleration,n);seed(v.high,n);seed(v.veryHigh,n);seed(v.shadow,n);seed(v.animates,n);seed(v.aa,n);seed(v.ag,n);seed(v.cluster,n);seed(v.bright,n);seed(v.laser,n);seed(v.houseColorLaser,n);seed(v.electricBolt,n);seed(v.alternateElectricColor,n);seed(v.electricColor,n);seed(v.shrapnel,n);seed(v.shrapnelReleased,n);seed(v.laserDuration,n);seed(v.laserThickness,n);seed(v.cellSpread,n);seed(v.percentAtMax,n);seed(v.affectsAllies,n);seed(v.affectsEnemies,n);seed(v.particleId,n);seed(v.progress,n);seed(v.currentX,n);seed(v.currentY,n);seed(v.currentZ,n);seed(v.speedCellsPerTick,n);seed(v.rot,n);seed(v.deathWeaponCascadeId,n);seed(v.deathWeaponRequestId,n);seed(v.deathWeaponCascadeDepth,n);seed(v.launchSourceKind,n);seed(v.launchSourceType,n);seed(v.launchSourceAuthoredAircraft,n);seed(v.launchSourceValidated,n);seed(v.airstrikeStrikeId,n);seed(v.airstrikePlaneToken,n);seed(v.particleDamagePulse,n);seed(v.suppressWarheadParticleSpawn,n);}
 else if constexpr(requires{v.framesLeft;v.orientation;v.spin;}){seed(v.enabled,n);seed(v.framesLeft,n);seed(v.frameRemainder,n);seed(v.frameCount,n);seed(v.bounces,n);seed(v.elasticity,n);seed(v.position,n);seed(v.velocity,n);seed(v.orientation,n);seed(v.spin,n);}
 else if constexpr(requires{v.x;v.y;}){seed(v.x,n);seed(v.y,n);}
 else if constexpr(requires{v.begin();v.end();}){for(auto& x:v)seed(x,n);}
 else static_assert(sizeof(T)==0,"unseeded projectile value");
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

struct Trace {std::vector<std::string> calls;int visits=0,failAt=0;bool after=false;
 void enter(std::string x){calls.push_back(std::move(x));++visits;if(!after&&visits==failAt)throw std::runtime_error("before:"+std::to_string(visits));}
 void leave(){if(after&&visits==failAt)throw std::runtime_error("after:"+std::to_string(visits));}
};
struct Model {
 int tick=91,summary=-411,effectSerial=0,sizeReads=0,sizeReadAt=0;
 std::uint64_t rng=0xA33C924556ULL;
 RuntimeProjectileEffect projectile;
 WorldObjectInstance source;
 RuntimeProjectileTargetPosition center;
 std::vector<WorldObjectInstance> objects;
 std::vector<RuntimeProjectileEffect> projectiles;
 std::vector<std::string> replays;
 bool predicate=true,impactResult=true,sourceInObjects=false,aliasTickSummary=false;
 int predicateMutation=0;
 bool targetMutatesTick=false,spawnMutatesProjectile=false,impactMutatesProjectile=false,expireMutatesCount=false;
 Trace trace;
};
Model* active=nullptr;
WorldObjectInstance& source(Model& m){return m.sourceInObjects?m.objects.front():m.source;}
int& summary(Model& m){return m.aliasTickSummary?m.tick:m.summary;}
bool predicate(Model& m,const RuntimeProjectileEffect& p){m.trace.enter("predicate:"+encode(p));const bool result=m.predicate;if(m.predicateMutation==1)m.projectile.parentActorId=7;else if(m.predicateMutation==2)m.projectile.generation=5;m.trace.leave();return result;}
RuntimeProjectileTargetPosition target(Model& m,const WorldObjectInstance& s){m.trace.enter("position:"+encode(s));const auto result=m.center;if(m.targetMutatesTick)m.tick=7331;m.trace.leave();return result;}
std::string lifecycleName(Model& m,RuntimeProjectileLifecycleState s){m.trace.enter("name:"+encode(s));std::string out;switch(s){case RuntimeProjectileLifecycleState::Spawned:out="Spawned";break;case RuntimeProjectileLifecycleState::Traveling:out="Traveling";break;case RuntimeProjectileLifecycleState::Impacting:out="Impacting";break;case RuntimeProjectileLifecycleState::Expired:out="Expired";break;}if(out.empty())out="Expired";m.trace.leave();return out;}
void replay(Model& m,const RuntimeProjectileEffect& p,const std::string& kind){m.trace.enter("replay:"+encode(kind)+encode(p));m.replays.push_back(encode(kind)+encode(p));++m.effectSerial;if(kind=="ProjectileSpawned"&&m.spawnMutatesProjectile){m.projectile.damage=456;m.projectile.remainingTicks=13;m.projectile.renderIntent="callback-spawn";m.tick=5121;}if(kind=="ProjectileExpired"&&m.expireMutatesCount){m.projectiles.resize(5);m.tick=9113;m.projectile.currentX=-0.0;m.projectile.deathWeaponRequestId^=0xA55Au;}m.trace.leave();}
bool impact(Model& m,const RuntimeProjectileEffect& p){m.trace.enter("impact:"+encode(p));m.rng^=0xAA3251FEULL;++m.effectSerial;if(!m.objects.empty())m.objects.front().health-=3;if(m.impactMutatesProjectile){m.projectile.remainingTicks=29;m.projectile.damage=-72;m.projectile.currentY=std::bit_cast<double>(0x7ff800000000A55AULL);m.projectile.lifecycle=RuntimeProjectileLifecycleState::Traveling;m.projectile.lifecycleState="callback-impact";m.objects.reserve(m.objects.capacity()+3);WorldObjectInstance extra;extra.instanceId=830;extra.owner="created";extra.health=91;m.objects.push_back(extra);}const bool result=m.impactResult;m.trace.leave();return result;}
std::size_t count(Model& m){++m.sizeReads;m.sizeReadAt=m.trace.visits;return m.projectiles.size();}
struct Projectiles {Model& m;std::size_t size()const{return count(m);}};
struct Root {Model& m;int& tick;struct Summary{int& projectilesInFlight;}actionSummary;Projectiles projectiles;
 explicit Root(Model& value):m(value),tick(value.tick),actionSummary{summary(value)},projectiles{value}{}
};
bool runtimeProjectileIsNuclearDeathDetonation(const RuntimeProjectileEffect& p){return predicate(*active,p);}
RuntimeProjectileTargetPosition runtimeProjectileTargetPosition(const Root& r,const WorldObjectInstance& s){return target(r.m,s);}
void recordRuntimeProjectileReplayEvent(Root& r,const RuntimeProjectileEffect& p,const std::string& kind){replay(r.m,p,kind);}
std::string runtimeProjectileLifecycleName(RuntimeProjectileLifecycleState s){return lifecycleName(*active,s);}
bool applyRuntimeProjectileImpact(Root& r,const RuntimeProjectileEffect& p){return impact(r.m,p);}
namespace original {
using RuntimeProductionHostState=Root;
#include "RuntimeNuclearDeathDetonationOriginal.inl"
}
struct Queries final:domain::RuntimeNuclearDeathQueries {
 Model& m;explicit Queries(Model& x):m(x){}
 bool isNuclearDeath(const RuntimeProjectileEffect& p)const override{return predicate(m,p);}
 RuntimeProjectileTargetPosition targetPosition(const WorldObjectInstance& s)const override{return target(m,s);}
 std::string lifecycleName(RuntimeProjectileLifecycleState s)const override{return ::lifecycleName(m,s);}
 std::size_t projectileCount()const override{return count(m);}
};
struct Effects final:domain::RuntimeNuclearDeathEffects {
 Model& m;explicit Effects(Model& x):m(x){}
 void recordReplay(const RuntimeProjectileEffect& p,const std::string& kind)override{replay(m,p,kind);}
 bool applyImpact(const RuntimeProjectileEffect& p)override{return impact(m,p);}
};
struct Outcome{bool threw=false,result=false;std::string error;};
Outcome run(Model& m,bool reference){active=&m;try{if(reference){Root r(m);return{false,original::detonateRuntimeNuclearDeathWeapon(r,m.projectile,source(m)),{}};}const Queries q(m);Effects e(m);return{false,domain::detonateRuntimeNuclearDeathWeapon(m.tick,summary(m),q,e,m.projectile,source(m)),{}};}catch(const std::exception& e){return{true,false,e.what()};}}
std::string state(const Model& m){return encode(m.tick)+encode(m.summary)+encode(m.effectSerial)+encode(m.sizeReads)+encode(m.sizeReadAt)+encode(m.rng)+encode(m.projectile)+encode(m.source)+encode(m.center)+encode(m.objects)+encode(m.projectiles)+encode(m.replays)+encode(m.predicate)+encode(m.impactResult)+encode(m.sourceInObjects)+encode(m.aliasTickSummary)+encode(m.predicateMutation)+encode(m.targetMutatesTick)+encode(m.spawnMutatesProjectile)+encode(m.impactMutatesProjectile)+encode(m.expireMutatesCount)+encode(m.trace.calls)+encode(m.trace.visits)+encode(m.trace.failAt)+encode(m.trace.after);}
void compare(const Model& base){Model a=base,b=base;const auto x=run(a,true),y=run(b,false);++states;require(x.threw==y.threw&&x.result==y.result&&x.error==y.error,"result/error differ");require(state(a)==state(b),"complete controlled state/ordered arguments/partial commit differ");}
void qualify(const Model& base){Model observed=base;const auto result=run(observed,true);require(!result.threw,"normal original threw");const int visits=observed.trace.visits;compare(base);if(result.result){require(observed.sizeReads==1&&observed.sizeReadAt==7,"live count must follow both replay publications");require(observed.projectile.lifecycle==RuntimeProjectileLifecycleState::Expired,"expiry not published");require(summary(observed)==static_cast<int>(observed.projectiles.size()),"live final count mismatch");require(observed.replays.size()==2,"spawn/expired replay count");if(!base.spawnMutatesProjectile&&!base.impactMutatesProjectile)require(observed.projectile.totalTicks==1&&observed.projectile.remainingTicks==0&&observed.projectile.elapsedTicks==1&&observed.projectile.progress==1.0,"synchronous non-traveling impact timing");}else require(observed.sizeReads==0&&observed.replays.empty(),"rejected input mutated summary/replay");for(int site=1;site<=visits;++site)for(bool after:{false,true}){Model trial=base;trial.trace.failAt=site;trial.trace.after=after;compare(trial);++faults;}}
Model fixture(){Model m;int serial=0;seed(m.projectile,serial);m.projectile.parentActorId=-1;m.projectile.generation=0;m.projectile.voxelDebris.enabled=false;m.projectile.projectileImageId="NKMSLDN";m.projectile.voxelDebris.position[0]=std::bit_cast<float>(0x7fc01234U);m.source.instanceId=71;m.source.owner="source-owner";m.source.type="REACTOR";m.source.health=0;m.source.alive=false;m.source.x=17;m.source.y=-3;m.source.hasRenderPosition=true;m.source.renderX=-0.0;m.source.renderY=19.25;m.objects={m.source};m.objects.front().health=200;m.center={{12,-4},31.5,-0.0,8};m.projectiles.resize(2);return m;}
void cases(){for(bool valid:{false,true})for(int parent:{-9,-1,0,7})for(int generation:{-1,0,1}){auto m=fixture();m.predicate=valid;m.projectile.parentActorId=parent;m.projectile.generation=generation;qualify(m);}for(int bits=0;bits<128;++bits){auto m=fixture();m.impactResult=(bits&1)!=0;m.sourceInObjects=(bits&2)!=0;m.aliasTickSummary=(bits&4)!=0;m.targetMutatesTick=(bits&8)!=0;m.spawnMutatesProjectile=(bits&16)!=0;m.impactMutatesProjectile=(bits&32)!=0;m.expireMutatesCount=(bits&64)!=0;qualify(m);}for(int mutation:{1,2})for(bool valid:{false,true}){auto m=fixture();m.predicateMutation=mutation;m.predicate=valid;qualify(m);}for(int tick:{std::numeric_limits<int>::min(),-1,0,91,std::numeric_limits<int>::max()})for(int level:{std::numeric_limits<int>::min(),-1,0,19,std::numeric_limits<int>::max()}){auto m=fixture();m.tick=tick;m.center.level=level;m.center.x=std::bit_cast<double>(0x7ff800000000A22AULL);m.center.y=-0.0;qualify(m);}}
}
int main(){try{verifyWorldObjectEncoder();cases();std::cout<<"world-object-encoder-field-probes="<<encoderFieldProbes<<" fields=87 failures=0\n";std::cout<<"nuclear-death checks="<<checks<<" model-states="<<states<<" injected-faults="<<faults<<" full-projectile96-voxel10-world87=exact before-after-commands=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"nuclear-death failure after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
