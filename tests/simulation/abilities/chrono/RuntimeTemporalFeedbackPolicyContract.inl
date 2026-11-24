#include "simulation/abilities/chrono/RuntimeTemporalAttackPolicy.h"
#include "simulation/abilities/chrono/RuntimeTemporalFeedbackPolicy.h"
#include "simulation/effects/RuntimeParticleSystem.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <unordered_map>
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
namespace temporal_feedback_model_detail {
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
    else if constexpr (requires { v.projectileActorId;v.trailerIntentId;v.bodyCenterPresentation; }) {return encode(v.projectileActorId)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.targetIsStructure)+encode(v.owner)+encode(v.cell)+encode(v.level)+encode(v.hasWorldPosition)+encode(v.worldX)+encode(v.worldY)+encode(v.worldLevel)+encode(v.screenOffsetX)+encode(v.screenOffsetY)+encode(v.nativeFixedX)+encode(v.nativeFixedY)+encode(v.bodyCenterAnchored)+encode(v.bodyCenterPresentation)+encode(v.bodyCenterSpriteOffsetY)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.radius)+encode(v.frameIndex)+encode(v.facing)+encode(v.muzzleAnchorX)+encode(v.muzzleAnchorY)+encode(v.muzzleAnchorLevel)+encode(v.muzzleFacing)+encode(v.animationFrameCount)+encode(v.animationFrameDelayTicks)+encode(v.animationStartFrame)+encode(v.animationLoopStartFrame)+encode(v.animationLoopEndFrame)+encode(v.animationLoopCount)+encode(v.animationEndFrame)+encode(v.animationSequenceSteps)+encode(v.animationMidpointWidth)+encode(v.animationMidpointHeight)+encode(v.lastSmudgeMidpointStep)+encode(v.deferredParticleAnimId)+encode(v.deferredParticleOwner)+encode(v.deferredParticleTicks)+encode(v.death)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.animId)+encode(v.warheadId)+encode(v.warheadImpactIntentId)+encode(v.trailerIntentId)+encode(v.particleId)+encode(v.damage)+encode(v.cellSpread)+encode(v.bright);}
    else if constexpr (requires { v.hasBeamEndpoints;v.maximumIntensity;v.superWeaponId; }) {return encode(v.kind)+encode(v.instanceId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.superWeaponId)+encode(v.sourceCell)+encode(v.targetCell)+encode(v.cell)+encode(v.cell2)+encode(v.sourceX)+encode(v.sourceY)+encode(v.targetX)+encode(v.targetY)+encode(v.sourceLevel)+encode(v.targetLevel)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasBeamEndpoints)+encode(v.missilePitch)+encode(v.missileFacing)+encode(v.radius)+encode(v.intensity)+encode(v.maximumIntensity)+encode(v.progress)+encode(v.requiredProgress)+encode(v.expireTick)+encode(v.owner)+encode(v.color)+encode(v.phase)+encode(v.resourceId);}
    else if constexpr (requires {v.sourceCellSum;v.clearanceFraction;v.roofs;}) {return encode(v.enabled)+encode(v.sourceCellSum)+encode(v.sourceLevel)+encode(v.factoryLiftCancellation)+encode(v.clearanceFraction)+encode(v.resolvedClearanceLevel)+encode(v.roofs);}
    else if constexpr (requires {v.cellSum;v.authoredHeight;v.nativeSpriteHeight;}) {return encode(v.cellSum)+encode(v.level)+encode(v.authoredHeight)+encode(v.nativeSpriteHeight)+encode(v.rubble);}
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
    else if constexpr(requires {v.enabled;v.clearanceFraction;v.roofs;}) v.enabled=!v.enabled;
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




struct EffectValues {
    int projectileActorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    bool targetIsStructure = false;
    std::string owner;
    RuntimeCellPoint cell;
    int level = 0;
    bool hasWorldPosition = false;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation{};
    int bodyCenterSpriteOffsetY = 0;
    int totalTicks = 8;
    int remainingTicks = 8;
    int radius = 10;
    int frameIndex = -1;
    int facing = -1;
    // Last resolved owner position for a moving passenger's muzzle. Together
    // with worldX/Y/Level it preserves the authored offset across snapshots.
    double muzzleAnchorX = 0.0;
    double muzzleAnchorY = 0.0;
    double muzzleAnchorLevel = 0.0;
    int muzzleFacing = -1;
    int animationFrameCount = 0;
    int animationFrameDelayTicks = 1;
    int animationStartFrame = 0;
    int animationLoopStartFrame = 0;
    int animationLoopEndFrame = 0;
    int animationLoopCount = 1;
    int animationEndFrame = 0;
    int animationSequenceSteps = 0;
    int animationMidpointWidth = 0;
    int animationMidpointHeight = 0;
    int lastSmudgeMidpointStep = -1;
    // Source ownership is independent of the victim's corpse remap color.
    std::string deferredParticleAnimId;
    std::string deferredParticleOwner;
    int deferredParticleTicks = -1;
    bool death = false;
    std::string lifecycleState = "Impact";
    std::string renderIntent = "impact";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string animId;
    std::string warheadId;
    std::string warheadImpactIntentId;
    std::string trailerIntentId;
    std::string particleId;
    int damage = 0;
    double cellSpread = 0.0;
    bool bright = false;
};
struct SpecialMechanismRenderSnapshot {
    std::string kind;
    int instanceId = -1;
    int objectId = -1;
    int targetObjectId = -1;
    std::string superWeaponId;
    SpecialCell sourceCell;
    SpecialCell targetCell;
    SpecialCell cell;
    SpecialCell cell2;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double targetX = 0.0;
    double targetY = 0.0;
    // Presentation-only weapon endpoints, resolved from the current FLH pose.
    double sourceLevel = 0.0;
    double targetLevel = 0.0;
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
    bool hasBeamEndpoints = false;
    double missilePitch = 0.0;
    int missileFacing = 0;
    int radius = 0;
    int intensity = 0;
    int maximumIntensity = 0;
    int progress = 0;
    int requiredProgress = 0;
    int expireTick = 0;
    std::string owner;
    std::string color;
    std::string phase;
    std::string resourceId;
};

using concord::runtime::session_detail::RuntimeProjectileTargetPosition;
using runtime_save_text::toLowerAsciiLocal;
struct State;
State* activeState = nullptr;
void profileStarted(State&, const void*);
void profileMoved(State&, const void*, const void*);
void profileFinished(State&, const void*);
void effectStarted(State&, const void*);
void effectFinished(State&, const void*);
void touch(State&, const std::string&);
void referenceBeam(State&);
void actualBeam(State&);
void referenceErase(State&, const SpecialMechanismEvent&);
void actualErase(State&, const SpecialMechanismEvent&);
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
struct RuntimeImpactEffect:EffectValues {
 State* trackingState=nullptr;bool live=false;
 RuntimeImpactEffect():trackingState(activeState),live(trackingState!=nullptr){if(live)effectStarted(*trackingState,this);}
 RuntimeImpactEffect(const RuntimeImpactEffect& x):EffectValues(x),trackingState(nullptr),live(false){}
 RuntimeImpactEffect(RuntimeImpactEffect&& x) noexcept:EffectValues(std::move(x)),trackingState(nullptr),live(false){}
 ~RuntimeImpactEffect(){if(live)effectFinished(*trackingState,this);}
};
using RuntimeProjectileSourcePosition=RuntimeTemporalBeamMuzzle;
struct SpecialRules {std::string warpAwayAnim="ERASE";};
struct ObservedRules {
 State* owner=nullptr;SpecialRules special;std::string color="128,200,255";
 std::string getAudioVisualValue(const std::string&) const;
 const SpecialRules& getSpecialMechanismRules() const;
};
struct Art {std::string reportSound="EraseReport";};
struct ObservedArt {State* owner=nullptr;bool present=true;Art value;const Art* findArtType(const std::string&) const;};
struct Input {bool erase=false;int scenario=0,snapshots=1,mutation=0,faultAt=0,seed=0;};
struct State {
 Input input;bool referenceMode=false,mutationFired=false;int portIndex=0,facingReads=0,starts=0,ends=0,effectStarts=0,effectEnds=0,lifeErrors=0,maxDepth=0,nested=0;
 bool validPassenger=true,hasWeapon=true,hasWarhead=true;
 std::vector<WorldObjectInstance> objects;
 std::unordered_map<int,RuntimeObjectBehavior> behaviors;
 struct {std::map<int,RuntimeCombatFireCursor> fireCursors;} combatDetails;
 std::vector<SpecialMechanismRenderSnapshot> specialMechanismRenderSnapshots;
 WeaponType weapon;WarheadType warhead;ProjectileType projectile;
 ObservedRules rules;ObservedArt art;
 std::vector<RuntimeImpactEffect> impactEffects;
 std::vector<const RuntimeWeaponProfile*> profiles;
 std::vector<const RuntimeImpactEffect*> effects;
 std::vector<std::string> sounds,ports,trace;
 std::string lastProfile,lastEffect;
 SpecialMechanismEvent event;
};
std::string profileDump(const ProfileValues& p){
 return encode(p.hasWeaponSet)+encode(p.requiresDeployment)+encode(p.deployed)+encode(p.occupied)+encode(p.openTransport)+encode(p.hasTurret)
 +encode(p.weapon?p.weapon->id:std::string("null"))+encode(p.projectile?p.projectile->id:std::string("null"))+encode(p.warhead?p.warhead->id:std::string("null"))
 +encode(p.slot)+encode(p.weaponId)+encode(p.projectileId)+encode(p.warheadId)+encode(p.selectionReason)+encode(p.indexedWeaponIndex)+encode(p.occupantMuzzleIndex);
}
std::string snapshot(const State& s){
 std::string scopes;for(const auto* p:s.profiles)scopes+=profileDump(*p);for(const auto* e:s.effects)scopes+=encode(*e);
 return encode(s.objects)+encode(s.behaviors)+encode(s.combatDetails.fireCursors)+encode(s.specialMechanismRenderSnapshots)+encode(s.impactEffects)
 +encode(s.event)+encode(s.rules.color)+encode(s.rules.special.warpAwayAnim)+encode(s.art.present)+encode(s.art.value.reportSound)
 +encode(s.weapon.radBeam)+encode(s.warhead.temporal)+encode(s.validPassenger)+encode(s.hasWeapon)+encode(s.hasWarhead)
 +encode(s.facingReads)+encode(s.starts)+encode(s.ends)+encode(s.effectStarts)+encode(s.effectEnds)+encode(s.lifeErrors)
 +encode(s.mutationFired)+encode(s.nested)+encode(s.maxDepth)+encode(s.sounds)+encode(s.lastProfile)+encode(s.lastEffect)+scopes;
}
void profileStarted(State& s,const void* p){++s.starts;s.profiles.push_back(static_cast<const RuntimeWeaponProfile*>(p));s.maxDepth=std::max(s.maxDepth,static_cast<int>(s.profiles.size()+s.effects.size()));s.trace.push_back("profile-start="+snapshot(s));}
void profileMoved(State& s,const void* from,const void* to){if(s.profiles.empty()||s.profiles.back()!=from){++s.lifeErrors;return;}s.profiles.back()=static_cast<const RuntimeWeaponProfile*>(to);}
void profileFinished(State& s,const void* p){if(s.profiles.empty()||s.profiles.back()!=p){++s.lifeErrors;return;}s.lastProfile=profileDump(*s.profiles.back());s.profiles.pop_back();++s.ends;s.trace.push_back("profile-end="+snapshot(s));}
void effectStarted(State& s,const void* p){++s.effectStarts;s.effects.push_back(static_cast<const RuntimeImpactEffect*>(p));s.maxDepth=std::max(s.maxDepth,static_cast<int>(s.profiles.size()+s.effects.size()));s.trace.push_back("effect-start="+snapshot(s));}
void effectFinished(State& s,const void* p){if(s.effects.empty()||s.effects.back()!=p){++s.lifeErrors;return;}s.lastEffect=encode(*s.effects.back());s.effects.pop_back();++s.effectEnds;s.trace.push_back("effect-end="+snapshot(s));}
void touch(State& s,const std::string& label){
 const int entry=++s.portIndex;s.ports.push_back(label);s.trace.push_back(label+".before="+snapshot(s));
 if(!s.mutationFired&&s.input.mutation>0){bool done=false;auto& target=s.objects.at(1);
  if(s.input.mutation==1&&label=="source:41"){s.specialMechanismRenderSnapshots.front().targetObjectId=60;done=true;}
  if(s.input.mutation==2&&label=="object:52"){s.objects.front().owner="live-query-owner";done=true;}
  if(s.input.mutation==3&&label=="profile-object"){s.specialMechanismRenderSnapshots.front().targetObjectId=70;done=true;}
  if(s.input.mutation==4&&label=="pose"){s.specialMechanismRenderSnapshots.front().kind="changed-after-guard";target.facing=19;done=true;}
  if(s.input.mutation==5&&label=="facing"){target.facing+=7;done=true;}
  if(s.input.mutation==6&&label=="align"){if(!s.profiles.empty())const_cast<RuntimeWeaponProfile*>(s.profiles.back())->selectionReason="live-full-owned-profile";done=true;}
  if(s.input.mutation==7&&label=="target-position"){s.rules.color="live-color";done=true;}
  if(s.input.mutation==8&&label=="color"){s.objects.front().owner="late-owner";s.specialMechanismRenderSnapshots.front().sourceX=-991.5;done=true;}
  if(s.input.mutation==9&&(label=="facing"||label=="timeline")){
   s.mutationFired=true;++s.nested;SpecialMechanismEvent e=s.event;e.targetObjectId=60;
   if(s.referenceMode)referenceErase(s,e);else actualErase(s,e);done=true;
  }
  if(s.input.mutation==10&&label=="subcell"){target.renderX+=0.125;target.renderY+=0.25;done=true;}
  if(s.input.mutation==11&&label=="render-x"){target.renderY+=1.0;done=true;}
  if(s.input.mutation==12&&label=="render-y"){target.facing+=11;done=true;}
  if(s.input.mutation==13&&label=="draw-level"){s.rules.special.warpAwayAnim="late-animation";done=true;}
  if(s.input.mutation==14&&label=="timeline"){if(!s.effects.empty())const_cast<RuntimeImpactEffect*>(s.effects.back())->animId="none";done=true;}
  if(s.input.mutation==15&&label=="art"){if(!s.effects.empty())const_cast<RuntimeImpactEffect*>(s.effects.back())->worldX+=0.5;done=true;}
  if(s.input.mutation==16&&label=="spatial"){target.owner="spatial-owner";done=true;}
  if(s.input.mutation==17&&label=="sound"){s.rules.special.warpAwayAnim="after-sound";done=true;}
  if(done)s.mutationFired=true;
 }
 s.trace.push_back(label+".after="+snapshot(s));
 if(s.input.faultAt==entry)throw std::runtime_error("fault@"+label);
}
std::string ObservedRules::getAudioVisualValue(const std::string&)const{touch(*owner,"color");return color;}
const SpecialRules& ObservedRules::getSpecialMechanismRules()const{touch(*owner,"warp-animation");return special;}
const Art* ObservedArt::findArtType(const std::string&)const{touch(*owner,"art");return present?&value:nullptr;}
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(State& s,int id){touch(s,"source:"+std::to_string(id));for(const auto& x:s.objects)if(x.instanceId==id)return &x;return nullptr;}
const WorldObjectInstance* findObjectById(State& s,int id){touch(s,"object:"+std::to_string(id));for(const auto& x:s.objects)if(x.instanceId==id&&x.alive)return &x;return nullptr;}
bool runtimeCombatValidPassenger(const WorldObjectInstance& source,const WorldObjectInstance& host){touch(*activeState,"valid-passenger");return activeState->validPassenger&&source.garrisonHostObjectId==host.instanceId;}
RuntimeWeaponProfile makeProfile(State& s,bool passenger,bool force){touch(s,passenger?"profile-passenger":"profile-object");RuntimeWeaponProfile p;p.hasWeaponSet=true;p.requiresDeployment=force;p.deployed=passenger;p.occupied=passenger;p.hasTurret=!force;p.weapon=s.hasWeapon?&s.weapon:nullptr;p.warhead=s.hasWarhead?&s.warhead:nullptr;p.projectile=&s.projectile;p.slot=passenger?RuntimeWeaponSlot::OpenTransport:RuntimeWeaponSlot::Primary;p.weaponId="owned-weapon-"+std::to_string(force);p.projectileId="owned-projectile";p.warheadId="owned-warhead";p.selectionReason=passenger?"passenger-full-profile":"ordinary-full-profile";p.indexedWeaponIndex=5;p.occupantMuzzleIndex=3;return p;}
RuntimeWeaponProfile runtimeCombatPassengerWeaponProfile(State& s,const WorldObjectInstance&,const WorldObjectInstance*,bool force){return makeProfile(s,true,force);}
RuntimeWeaponProfile runtimeWeaponProfileForObject(State& s,const WorldObjectInstance&,const WorldObjectInstance*,bool force){return makeProfile(s,false,force);}
WorldObjectInstance runtimeCombatFiringPose(const WorldObjectInstance& source,const WorldObjectInstance* host){touch(*activeState,"pose");auto pose=host?*host:source;pose.owner=source.owner;return pose;}
int runtimeWeaponFiringFacing(State& s,const WorldObjectInstance&,const WorldObjectInstance& target){touch(s,"facing");return target.facing+(++s.facingReads)*13;}
RuntimeProjectileSourcePosition runtimeProjectileSourcePosition(State& s,const WorldObjectInstance& pose,const RuntimeWeaponProfile& p,int facing){touch(s,"source-position");require(!s.profiles.empty()&&s.profiles.back()==&p,"complete owned profile alive at source");return {pose.x+0.125,pose.y+0.25,pose.level+0.5,facing,-facing,p.occupantMuzzleIndex,p.indexedWeaponIndex};}
void alignRuntimeWeaponMuzzleToSprite(State& s,const WorldObjectInstance&,const RuntimeWeaponProfile& p,int facing,RuntimeProjectileSourcePosition& m){touch(s,"align");require(!s.profiles.empty()&&s.profiles.back()==&p,"complete owned profile alive at align");m.x+=0.5;m.y-=0.25;m.z+=0.125;m.screenOffsetX+=facing;m.screenOffsetY-=facing;m.nativeFixedX+=7;m.nativeFixedY-=9;}
RuntimeProjectileTargetPosition runtimeProjectileTargetPosition(State& s,const WorldObjectInstance& target){touch(s,"target-position");return {{target.x,target.y},target.x+0.625,target.y+0.875,target.level+2};}
WorldObjectCellOffset worldObjectSubCellOffset(const WorldObjectInstance& target){touch(*activeState,"subcell");return {target.subCell*0.125,target.subCell*-0.25};}
double runtimeObjectRenderX(const WorldObjectInstance& o){touch(*activeState,"render-x");return o.hasRenderPosition?o.renderX:o.x;}
double runtimeObjectRenderY(const WorldObjectInstance& o){touch(*activeState,"render-y");return o.hasRenderPosition?o.renderY:o.y;}
double runtimeDrawLevelForObject(const WorldObjectInstance& o){touch(*activeState,"draw-level");return o.hasRenderPosition?o.renderLevel:o.level;}
void initializeRuntimeImpactAnimationTimeline(State& s,RuntimeImpactEffect& e,bool force){touch(s,"timeline");require(force,"original timeline force true");e.totalTicks=23;e.remainingTicks=17;e.animationFrameCount=6;e.animationFrameDelayTicks=3;e.animationEndFrame=5;e.bright=true;}
RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForImpact(const RuntimeImpactEffect& e){touch(*activeState,"spatial");return {e.worldX,e.worldY,e.worldLevel};}
void emitRuntimeSound(State& s,const std::string& intent,RuntimeSpatialAudioPosition p,bool force){touch(s,"sound");s.sounds.push_back(encode(intent)+encode(p)+encode(force));}
void referenceBeam(State& state) {
    for (auto& snapshot : state.specialMechanismRenderSnapshots) {
        if (snapshot.kind != "temporalBeam") continue;
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, snapshot.objectId);
        const auto* target = findObjectById(state, snapshot.targetObjectId);
        if (source == nullptr || target == nullptr) continue;
        const WorldObjectInstance* host = nullptr;
        if (!source->alive) {
            const auto cursor = state.combatDetails.fireCursors.find(source->instanceId);
            if (cursor != state.combatDetails.fireCursors.end())
                host = findObjectById(state, cursor->second.sourceAnchorObjectId);
            if (!host || !runtimeCombatValidPassenger(*source, *host)) continue;
        }
        const auto sourceOrder = state.behaviors.find(snapshot.objectId);
        const auto hostOrder = host == nullptr ? state.behaviors.end()
            : state.behaviors.find(host->instanceId);
        const bool profileForceFire = (sourceOrder != state.behaviors.end()
            && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
            && sourceOrder->second.targetObjectId == snapshot.targetObjectId
            && sourceOrder->second.forceFire)
            || (hostOrder != state.behaviors.end()
                && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                && hostOrder->second.targetObjectId == snapshot.targetObjectId
                && hostOrder->second.forceFire);
        auto profile = host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
            : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
        profile.openTransport = host != nullptr;
        if (profile.weapon == nullptr || !profile.weapon->radBeam
            || profile.warhead == nullptr || !profile.warhead->temporal) continue;
        const auto pose = runtimeCombatFiringPose(*source, host);
        auto muzzle = runtimeProjectileSourcePosition(state, pose, profile,
            runtimeWeaponFiringFacing(state, pose, *target));
        alignRuntimeWeaponMuzzleToSprite(state, pose, profile, runtimeWeaponFiringFacing(state, pose, *target), muzzle);
        const auto endpoint = runtimeProjectileTargetPosition(state, *target);
        snapshot.sourceX = muzzle.x;
        snapshot.sourceY = muzzle.y;
        snapshot.sourceLevel = muzzle.z;
        snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
        snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
        snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
        snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
        snapshot.targetX = endpoint.x;
        snapshot.targetY = endpoint.y;
        snapshot.targetLevel = endpoint.level;
        snapshot.color = state.rules.getAudioVisualValue("ChronoBeamColor");
        if (snapshot.color.empty()) snapshot.color = "128,200,255";
        snapshot.owner = source->owner;
        snapshot.hasBeamEndpoints = true;
    }
}

void referenceErase(
    State& state, const SpecialMechanismEvent& event) {
    const auto* target = findObjectById(state, event.targetObjectId);
    if (target == nullptr) return;
    RuntimeImpactEffect effect;
    effect.attackerObjectId = event.objectId;
    effect.targetObjectId = target->instanceId;
    effect.owner = target->owner;
    effect.cell = {target->x, target->y};
    effect.level = target->level;
    effect.hasWorldPosition = true;
    const auto offset = temporal_feedback_model_detail::worldObjectSubCellOffset(*target);
    effect.worldX = runtimeObjectRenderX(*target) + offset.x;
    effect.worldY = runtimeObjectRenderY(*target) + offset.y;
    effect.worldLevel = runtimeDrawLevelForObject(*target);
    effect.facing = target->facing;
    effect.lifecycleState = "TemporalErase";
    effect.renderIntent = "temporalErase";
    effect.animId = state.rules.getSpecialMechanismRules().warpAwayAnim;
    if (effect.animId.empty() || toLowerAsciiLocal(effect.animId) == "none") return;
    initializeRuntimeImpactAnimationTimeline(state, effect, true);
    if (const auto* art = state.art.findArtType(effect.animId)) {
        if (!art->reportSound.empty()) emitRuntimeSound(state, "sound:" + art->reportSound, runtimeSpatialAudioPositionForImpact(effect), true);
    }
    state.impactEffects.push_back(std::move(effect));
}

namespace {
class SessionTemporalBeamQueries final : public concord::simulation::abilities::RuntimeTemporalBeamQueries {
public:
    explicit SessionTemporalBeamQueries(State& value) : state(value) {}
    const WorldObjectInstance* sourceIncludingInactive(int id) const override { return findRuntimeObjectByIdIncludingInactive(state, id); }
    const WorldObjectInstance* object(int id) const override { return findObjectById(state, id); }
    bool validPassenger(const WorldObjectInstance& source, const WorldObjectInstance& host) const override { return runtimeCombatValidPassenger(source, host); }
    concord::simulation::abilities::RuntimeTemporalBeamWeaponView selectWeapon(const WorldObjectInstance& source,
        const WorldObjectInstance* target, const WorldObjectInstance* host, bool forceFire) override {
        profile.emplace(host ? runtimeCombatPassengerWeaponProfile(state, source, target, forceFire)
            : runtimeWeaponProfileForObject(state, source, target, forceFire));
        profile->openTransport = host != nullptr;
        return {profile->weapon, profile->warhead};
    }
    void finishWeaponScope() noexcept override { profile.reset(); }
    WorldObjectInstance firingPose(const WorldObjectInstance& source, const WorldObjectInstance* host) const override { return runtimeCombatFiringPose(source, host); }
    int firingFacing(const WorldObjectInstance& pose, const WorldObjectInstance& target) const override { return runtimeWeaponFiringFacing(state, pose, target); }
    concord::simulation::abilities::RuntimeTemporalBeamMuzzle sourcePosition(const WorldObjectInstance& pose, int facing) const override {
        const auto value = runtimeProjectileSourcePosition(state, pose, *profile, facing);
        return {value.x, value.y, value.z, value.screenOffsetX, value.screenOffsetY, value.nativeFixedX, value.nativeFixedY};
    }
    void alignMuzzle(const WorldObjectInstance& pose, int facing, concord::simulation::abilities::RuntimeTemporalBeamMuzzle& muzzle) const override {
        RuntimeProjectileSourcePosition value{muzzle.x, muzzle.y, muzzle.z, muzzle.screenOffsetX, muzzle.screenOffsetY, muzzle.nativeFixedX, muzzle.nativeFixedY};
        alignRuntimeWeaponMuzzleToSprite(state, pose, *profile, facing, value);
        muzzle = {value.x, value.y, value.z, value.screenOffsetX, value.screenOffsetY, value.nativeFixedX, value.nativeFixedY};
    }
    RuntimeProjectileTargetPosition targetPosition(const WorldObjectInstance& target) const override { return runtimeProjectileTargetPosition(state, target); }
    std::string beamColor() const override { return state.rules.getAudioVisualValue("ChronoBeamColor"); }
private:
    State& state;
    std::optional<RuntimeWeaponProfile> profile;
};
class SessionTemporalEraseQueries final : public concord::simulation::abilities::RuntimeTemporalEraseQueries {
public:
    explicit SessionTemporalEraseQueries(State& value) : state(value) {}
    const WorldObjectInstance* object(int id) const override { return findObjectById(state, id); }
    WorldObjectCellOffset subcellOffset(const WorldObjectInstance& object) const override { return temporal_feedback_model_detail::worldObjectSubCellOffset(object); }
    double renderX(const WorldObjectInstance& object) const override { return runtimeObjectRenderX(object); }
    double renderY(const WorldObjectInstance& object) const override { return runtimeObjectRenderY(object); }
    double drawLevel(const WorldObjectInstance& object) const override { return runtimeDrawLevelForObject(object); }
    const std::string& warpAwayAnimation() const override { return state.rules.getSpecialMechanismRules().warpAwayAnim; }
    const std::string* reportSound(const std::string& animation) const override {
        const auto* art = state.art.findArtType(animation);
        return art ? &art->reportSound : nullptr;
    }
private:
    State& state;
};
class SessionTemporalEraseFeedback final : public concord::simulation::abilities::RuntimeTemporalEraseFeedback {
public:
    explicit SessionTemporalEraseFeedback(State& value) : state(value) {}
    void beginEffect(int attackerId, const WorldObjectInstance& target) override {
        effect.emplace();
        effect->attackerObjectId = attackerId;
        effect->targetObjectId = target.instanceId;
        effect->owner = target.owner;
        effect->cell = {target.x, target.y};
        effect->level = target.level;
        effect->hasWorldPosition = true;
    }
    void worldX(double value) noexcept override { effect->worldX = value; }
    void worldY(double value) noexcept override { effect->worldY = value; }
    void worldLevel(double value) noexcept override { effect->worldLevel = value; }
    void facingAndLifecycle(int facing, const char* lifecycle, const char* intent) override {
        effect->facing = facing;
        effect->lifecycleState = lifecycle;
        effect->renderIntent = intent;
    }
    void animation(const std::string& id) override { effect->animId = id; }
    const std::string& animation() const noexcept override { return effect->animId; }
    void initializeTimeline() override { initializeRuntimeImpactAnimationTimeline(state, *effect, true); }
    RuntimeSpatialAudioPosition spatialPosition() const override { return runtimeSpatialAudioPositionForImpact(*effect); }
    void sound(const std::string& intent, RuntimeSpatialAudioPosition position, bool force) override { emitRuntimeSound(state, intent, position, force); }
    void appendEffect() override { state.impactEffects.push_back(std::move(*effect)); }
private:
    State& state;
    std::optional<RuntimeImpactEffect> effect;
};
}
void actualBeam(State& state) {
    SessionTemporalBeamQueries queries(state);
    for (auto& snapshot : state.specialMechanismRenderSnapshots) {
        concord::simulation::abilities::resolveRuntimeTemporalBeamEndpoints(
            {snapshot.kind, snapshot.objectId, snapshot.targetObjectId,
                snapshot.sourceX, snapshot.sourceY, snapshot.sourceLevel,
                snapshot.sourceScreenOffsetX, snapshot.sourceNativeFixedX,
                snapshot.sourceScreenOffsetY, snapshot.sourceNativeFixedY,
                snapshot.targetX, snapshot.targetY, snapshot.targetLevel,
                snapshot.color, snapshot.owner, snapshot.hasBeamEndpoints},
            state.behaviors, state.combatDetails.fireCursors, queries);
    }
}
void actualErase(State& state, const SpecialMechanismEvent& event) {
    SessionTemporalEraseQueries queries(state);
    SessionTemporalEraseFeedback feedback(state);
    concord::simulation::abilities::spawnRuntimeTemporalEraseEffect(event, queries, feedback);
}


int effectProbes=0,snapshotProbes=0,mutationsReached=0,nestedRuns=0,pairedProfiles=0,pairedEffects=0,positiveBeam=0,positiveErase=0;
std::set<std::string> faultLabels;
struct ActiveGuard {State* old;explicit ActiveGuard(State& s):old(activeState){activeState=&s;}~ActiveGuard(){activeState=old;}};
std::unique_ptr<State> seed(Input in){
 auto box=std::make_unique<State>();auto& s=*box;s.input=in;s.rules.owner=&s;s.art.owner=&s;
 s.weapon.id="beam";s.weapon.radBeam=true;s.warhead.id="temporal";s.warhead.temporal=true;s.projectile.id="projectile";
 s.objects.resize(3);auto& a=s.objects[0];a.instanceId=41;a.owner="A";a.type="SOURCE";a.kind=WorldObjectKind::Infantry;a.alive=true;a.health=100;a.x=5+in.seed;a.y=6;a.level=2;a.facing=7;a.subCell=3;a.hasRenderPosition=true;a.renderX=5.25;a.renderY=6.5;a.renderLevel=2.75;a.garrisonHostObjectId=60;
 auto& t=s.objects[1];t=a;t.instanceId=52;t.owner="B";t.type="TARGET";t.kind=WorldObjectKind::Unit;t.x=9;t.y=8;t.level=4;t.facing=11;t.renderX=9.125;t.renderY=8.625;t.renderLevel=4.5;
 auto& h=s.objects[2];h=a;h.instanceId=60;h.kind=WorldObjectKind::Unit;h.type="HOST";h.x=3;h.y=4;h.level=1;h.transportPassengerIds={41};
 RuntimeObjectBehavior order;order.kind=RuntimeBehaviorKind::Attack;order.targetObjectId=52;s.behaviors.emplace(41,order);s.behaviors.emplace(60,order);
 s.combatDetails.fireCursors[41].sourceAnchorObjectId=60;s.combatDetails.fireCursors[41].lastTargetObjectId=52;
 for(int n=0;n<in.snapshots;++n){SpecialMechanismRenderSnapshot v;v.kind="temporalBeam";v.objectId=41;v.targetObjectId=52;v.instanceId=100+n;v.superWeaponId="preserve";v.phase="untouched-phase";v.resourceId="untouched-resource";v.sourceX=-10.25;v.sourceY=-20.125;v.sourceLevel=-1.5;v.targetX=-30.75;v.targetY=-40.5;v.targetLevel=-2.5;v.sourceScreenOffsetX=901;v.sourceScreenOffsetY=-902;v.sourceNativeFixedX=-903;v.sourceNativeFixedY=904;v.owner="old-owner";v.color="old-color";v.missilePitch=-0.0;v.radius=17;v.progress=29;s.specialMechanismRenderSnapshots.push_back(std::move(v));}
 s.event.kind="TemporalCompleted";s.event.objectId=41;s.event.targetObjectId=52;s.event.tick=101;s.event.reason="original-completed";
 if(in.erase){switch(in.scenario){
 case 1:s.event.targetObjectId=999;break;
 case 2:s.rules.special.warpAwayAnim.clear();break;
 case 3:s.rules.special.warpAwayAnim="none";break;
 case 4:s.rules.special.warpAwayAnim="NoNe";break;
 case 5:s.rules.special.warpAwayAnim=" none ";break;
 case 6:s.art.present=false;break;
 case 7:s.art.value.reportSound.clear();break;
 case 8:s.art.value.reportSound=" Report With Spaces ";break;
 case 9:t.hasRenderPosition=false;break;
 case 10:t.airborne=true;t.flightLevel=6;break;
 case 11:t.kind=WorldObjectKind::Structure;break;
 case 12:t.subCell=-1;break;
 case 13:t.alive=false;break;
 case 14:s.event.objectId=s.event.targetObjectId;break;
 }}else{auto& v=s.specialMechanismRenderSnapshots.front();switch(in.scenario){
 case 1:for(auto& p:s.specialMechanismRenderSnapshots)p.kind="other";break;
 case 2:for(auto& p:s.specialMechanismRenderSnapshots)p.objectId=999;break;
 case 3:for(auto& p:s.specialMechanismRenderSnapshots)p.targetObjectId=999;break;
 case 4:t.alive=false;break;
 case 5:a.alive=false;break;
 case 6:a.alive=false;s.combatDetails.fireCursors[41].sourceAnchorObjectId=999;break;
 case 7:a.alive=false;s.combatDetails.fireCursors.clear();break;
 case 8:a.alive=false;s.validPassenger=false;break;
 case 9:s.hasWeapon=false;break;
 case 10:s.weapon.radBeam=false;break;
 case 11:s.hasWarhead=false;break;
 case 12:s.warhead.temporal=false;break;
 case 13:s.behaviors.at(41).forceFire=true;break;
 case 14:a.alive=false;s.behaviors.at(60).forceFire=true;break;
 case 15:s.behaviors.at(41).kind=RuntimeBehaviorKind::Move;s.behaviors.at(41).forceFire=true;break;
 case 16:s.behaviors.at(41).targetObjectId=99;s.behaviors.at(41).forceFire=true;break;
 case 17:s.behaviors.erase(41);break;
 case 18:s.rules.color.clear();break;
 case 19:s.rules.color=" authored,Chrono-Color ";break;
 case 20:for(auto& p:s.specialMechanismRenderSnapshots)p.targetObjectId=41;break;
 case 21:a.alive=false;s.behaviors.erase(41);s.behaviors.erase(60);break;
 case 22:v.hasBeamEndpoints=true;v.color="already-populated";break;
 }}return box;
}
struct Output {std::string image,error;std::vector<std::string> ports,trace;int starts=0,ends=0,effects=0,effectEnds=0,depth=0,nested=0,beams=0,appended=0;bool mutation=false;};
Output exercise(Input in,bool reference){auto box=seed(in);auto& s=*box;s.referenceMode=reference;ActiveGuard active(s);Output out;
 try{if(in.erase){if(reference)referenceErase(s,s.event);else actualErase(s,s.event);}else{if(reference)referenceBeam(s);else actualBeam(s);}}
 catch(const std::exception& e){out.error=e.what();}
 require(s.profiles.empty()&&s.effects.empty()&&s.starts==s.ends&&s.effectStarts==s.effectEnds&&s.lifeErrors==0,"all full owned scopes destroyed in original LIFO including exception");
 out.image=snapshot(s);out.ports=s.ports;out.trace=s.trace;out.starts=s.starts;out.ends=s.ends;out.effects=s.effectStarts;out.effectEnds=s.effectEnds;out.depth=s.maxDepth;out.nested=s.nested;out.mutation=s.mutationFired;out.appended=static_cast<int>(s.impactEffects.size());for(const auto& x:s.specialMechanismRenderSnapshots)out.beams+=x.hasBeamEndpoints?1:0;++states;return out;
}
void compare(Input in,bool faultsRequired){const auto before=exercise(in,true),now=exercise(in,false);
 require(before.error==now.error,"reference/candidate exception differs");require(before.ports==now.ports,"original query order differs");require(before.trace==now.trace,"full-state per-port and owned-scope lifetime differs");require(before.image==now.image,"full world87/snapshot32/effect56/profile16 differs");
 pairedProfiles+=before.starts+now.starts;pairedEffects+=before.effects+now.effects;positiveBeam+=now.beams;positiveErase+=now.appended;
 if(now.mutation){++mutationsReached;if(now.nested){++nestedRuns;require(now.depth>=2,"real nested original scopes overlap");}}
 if(faultsRequired){for(int at=1;at<=static_cast<int>(before.ports.size());++at){auto f=in;f.faultAt=at;const auto a=exercise(f,true),b=exercise(f,false);require(!a.error.empty()&&a.error==b.error,"fault differs");require(a.ports==b.ports&&a.trace==b.trace&&a.image==b.image,"fault full state/order/lifetime differs");faultLabels.insert(a.ports.back());++faults;}}
}
void verifyFields(){verifyWorld();
{EffectValues zero;const auto before=encode(zero);
{auto v=zero;mutateEncoderField(v.projectileActorId);require(encode(v)!=before,"EffectValues projectileActorId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.attackerObjectId);require(encode(v)!=before,"EffectValues attackerObjectId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.targetObjectId);require(encode(v)!=before,"EffectValues targetObjectId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.targetIsStructure);require(encode(v)!=before,"EffectValues targetIsStructure omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.owner);require(encode(v)!=before,"EffectValues owner omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.cell);require(encode(v)!=before,"EffectValues cell omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.level);require(encode(v)!=before,"EffectValues level omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.hasWorldPosition);require(encode(v)!=before,"EffectValues hasWorldPosition omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldX);require(encode(v)!=before,"EffectValues worldX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldY);require(encode(v)!=before,"EffectValues worldY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldLevel);require(encode(v)!=before,"EffectValues worldLevel omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.screenOffsetX);require(encode(v)!=before,"EffectValues screenOffsetX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.screenOffsetY);require(encode(v)!=before,"EffectValues screenOffsetY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.nativeFixedX);require(encode(v)!=before,"EffectValues nativeFixedX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.nativeFixedY);require(encode(v)!=before,"EffectValues nativeFixedY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterAnchored);require(encode(v)!=before,"EffectValues bodyCenterAnchored omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterPresentation);require(encode(v)!=before,"EffectValues bodyCenterPresentation omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterSpriteOffsetY);require(encode(v)!=before,"EffectValues bodyCenterSpriteOffsetY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.totalTicks);require(encode(v)!=before,"EffectValues totalTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.remainingTicks);require(encode(v)!=before,"EffectValues remainingTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.radius);require(encode(v)!=before,"EffectValues radius omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.frameIndex);require(encode(v)!=before,"EffectValues frameIndex omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.facing);require(encode(v)!=before,"EffectValues facing omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorX);require(encode(v)!=before,"EffectValues muzzleAnchorX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorY);require(encode(v)!=before,"EffectValues muzzleAnchorY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorLevel);require(encode(v)!=before,"EffectValues muzzleAnchorLevel omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleFacing);require(encode(v)!=before,"EffectValues muzzleFacing omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationFrameCount);require(encode(v)!=before,"EffectValues animationFrameCount omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationFrameDelayTicks);require(encode(v)!=before,"EffectValues animationFrameDelayTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationStartFrame);require(encode(v)!=before,"EffectValues animationStartFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopStartFrame);require(encode(v)!=before,"EffectValues animationLoopStartFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopEndFrame);require(encode(v)!=before,"EffectValues animationLoopEndFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopCount);require(encode(v)!=before,"EffectValues animationLoopCount omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationEndFrame);require(encode(v)!=before,"EffectValues animationEndFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationSequenceSteps);require(encode(v)!=before,"EffectValues animationSequenceSteps omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationMidpointWidth);require(encode(v)!=before,"EffectValues animationMidpointWidth omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationMidpointHeight);require(encode(v)!=before,"EffectValues animationMidpointHeight omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.lastSmudgeMidpointStep);require(encode(v)!=before,"EffectValues lastSmudgeMidpointStep omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleAnimId);require(encode(v)!=before,"EffectValues deferredParticleAnimId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleOwner);require(encode(v)!=before,"EffectValues deferredParticleOwner omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleTicks);require(encode(v)!=before,"EffectValues deferredParticleTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.death);require(encode(v)!=before,"EffectValues death omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.lifecycleState);require(encode(v)!=before,"EffectValues lifecycleState omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.renderIntent);require(encode(v)!=before,"EffectValues renderIntent omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.weaponId);require(encode(v)!=before,"EffectValues weaponId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.weaponSlot);require(encode(v)!=before,"EffectValues weaponSlot omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.projectileId);require(encode(v)!=before,"EffectValues projectileId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.projectileImageId);require(encode(v)!=before,"EffectValues projectileImageId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animId);require(encode(v)!=before,"EffectValues animId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.warheadId);require(encode(v)!=before,"EffectValues warheadId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.warheadImpactIntentId);require(encode(v)!=before,"EffectValues warheadImpactIntentId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.trailerIntentId);require(encode(v)!=before,"EffectValues trailerIntentId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.particleId);require(encode(v)!=before,"EffectValues particleId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.damage);require(encode(v)!=before,"EffectValues damage omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.cellSpread);require(encode(v)!=before,"EffectValues cellSpread omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bright);require(encode(v)!=before,"EffectValues bright omitted");++effectProbes;}
}
{SpecialMechanismRenderSnapshot zero;const auto before=encode(zero);
{auto v=zero;mutateEncoderField(v.kind);require(encode(v)!=before,"SpecialMechanismRenderSnapshot kind omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.instanceId);require(encode(v)!=before,"SpecialMechanismRenderSnapshot instanceId omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.objectId);require(encode(v)!=before,"SpecialMechanismRenderSnapshot objectId omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.targetObjectId);require(encode(v)!=before,"SpecialMechanismRenderSnapshot targetObjectId omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.superWeaponId);require(encode(v)!=before,"SpecialMechanismRenderSnapshot superWeaponId omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceCell);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceCell omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.targetCell);require(encode(v)!=before,"SpecialMechanismRenderSnapshot targetCell omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.cell);require(encode(v)!=before,"SpecialMechanismRenderSnapshot cell omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.cell2);require(encode(v)!=before,"SpecialMechanismRenderSnapshot cell2 omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceX);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceX omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceY);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceY omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.targetX);require(encode(v)!=before,"SpecialMechanismRenderSnapshot targetX omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.targetY);require(encode(v)!=before,"SpecialMechanismRenderSnapshot targetY omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceLevel);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceLevel omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.targetLevel);require(encode(v)!=before,"SpecialMechanismRenderSnapshot targetLevel omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceScreenOffsetX);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceScreenOffsetX omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceScreenOffsetY);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceScreenOffsetY omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceNativeFixedX);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceNativeFixedX omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.sourceNativeFixedY);require(encode(v)!=before,"SpecialMechanismRenderSnapshot sourceNativeFixedY omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.hasBeamEndpoints);require(encode(v)!=before,"SpecialMechanismRenderSnapshot hasBeamEndpoints omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.missilePitch);require(encode(v)!=before,"SpecialMechanismRenderSnapshot missilePitch omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.missileFacing);require(encode(v)!=before,"SpecialMechanismRenderSnapshot missileFacing omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.radius);require(encode(v)!=before,"SpecialMechanismRenderSnapshot radius omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.intensity);require(encode(v)!=before,"SpecialMechanismRenderSnapshot intensity omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.maximumIntensity);require(encode(v)!=before,"SpecialMechanismRenderSnapshot maximumIntensity omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.progress);require(encode(v)!=before,"SpecialMechanismRenderSnapshot progress omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.requiredProgress);require(encode(v)!=before,"SpecialMechanismRenderSnapshot requiredProgress omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.expireTick);require(encode(v)!=before,"SpecialMechanismRenderSnapshot expireTick omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.owner);require(encode(v)!=before,"SpecialMechanismRenderSnapshot owner omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.color);require(encode(v)!=before,"SpecialMechanismRenderSnapshot color omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.phase);require(encode(v)!=before,"SpecialMechanismRenderSnapshot phase omitted");++snapshotProbes;}
{auto v=zero;mutateEncoderField(v.resourceId);require(encode(v)!=before,"SpecialMechanismRenderSnapshot resourceId omitted");++snapshotProbes;}
}
{ProfileValues zero;State s;const auto before=profileDump(zero);
{auto v=zero;mutateEncoderField(v.hasWeaponSet);require(profileDump(v)!=before,"profile hasWeaponSet omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.requiresDeployment);require(profileDump(v)!=before,"profile requiresDeployment omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.deployed);require(profileDump(v)!=before,"profile deployed omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.occupied);require(profileDump(v)!=before,"profile occupied omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.openTransport);require(profileDump(v)!=before,"profile openTransport omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.hasTurret);require(profileDump(v)!=before,"profile hasTurret omitted");++profileProbes;}
{auto v=zero;v.weapon=&s.weapon;require(profileDump(v)!=before,"profile weapon omitted");++profileProbes;}
{auto v=zero;v.projectile=&s.projectile;require(profileDump(v)!=before,"profile projectile omitted");++profileProbes;}
{auto v=zero;v.warhead=&s.warhead;require(profileDump(v)!=before,"profile warhead omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.slot);require(profileDump(v)!=before,"profile slot omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.weaponId);require(profileDump(v)!=before,"profile weaponId omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.projectileId);require(profileDump(v)!=before,"profile projectileId omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.warheadId);require(profileDump(v)!=before,"profile warheadId omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.selectionReason);require(profileDump(v)!=before,"profile selectionReason omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.indexedWeaponIndex);require(profileDump(v)!=before,"profile indexedWeaponIndex omitted");++profileProbes;}
{auto v=zero;mutateEncoderField(v.occupantMuzzleIndex);require(profileDump(v)!=before,"profile occupantMuzzleIndex omitted");++profileProbes;}
}
}

bool runContract(){try{verifyFields();
 for(bool erase:{false,true})for(int scenario=0;scenario<(erase?15:23);++scenario)for(int count:{1,3})for(int seedValue:{0,1,2})compare({erase,scenario,count,0,0,seedValue},scenario==0||scenario==5||scenario==14);
 for(int mutation=1;mutation<=17;++mutation){const bool erase=mutation>=10;compare({erase,0,1,mutation,0,1},true);}
 compare({true,0,1,9,0,1},true);
 require(worldProbes==87&&effectProbes==56&&snapshotProbes==32&&profileProbes==16,"all complete field omission probes");require(mutationsReached==18&&nestedRuns>=2&&positiveBeam>0&&positiveErase>0,"all live mutations/nested/positive cases reached");require(pairedProfiles>0&&pairedEffects>0&&faults>0&&faultLabels.size()>=15,"complete scopes and fault ports exercised");
 std::cout<<"temporal-feedback-policy checks="<<checks<<" states="<<states<<" query-faults="<<faults<<" fault-ports="<<faultLabels.size()<<" world-probes="<<worldProbes<<" snapshot-probes="<<snapshotProbes<<" effect-probes="<<effectProbes<<" profile-probes="<<profileProbes<<" live-mutations="<<mutationsReached<<" nested-runs="<<nestedRuns<<" owned-profile-pairs="<<pairedProfiles<<" owned-effect-pairs="<<pairedEffects<<" original-beam50-erase26/full-state/order/lifetime=exact failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"temporal-feedback-policy FAIL "<<e.what()<<'\n';return false;}}
}
