namespace h022_private_qualification {
using namespace concord::runtime::session_detail;
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

namespace original {
bool runtimeMissionIsMcvDeployPending(const WorldObjectInstance& object) {
    const auto mission = toLowerAsciiLocal(object.mission);
    return mission == "mcvdeployturning" || mission == "mcvdeployturningunselected"
        || mission == "mcvdeployready" || mission == "mcvdeployreadyunselected";
}
bool runtimeMissionIsDeploying(const WorldObjectInstance& object) {
    return toLowerAsciiLocal(object.mission) == "deploying" || runtimeMissionIsMcvDeployPending(object);
}
bool runtimeMissionIsUndeploying(const WorldObjectInstance& object) {
    return toLowerAsciiLocal(object.mission) == "undeploying";
}
bool stationary(const RuntimeProductionHostState& state, int objectId) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr || !object->alive
            || object->dockedToObjectId >= 0
            || object->hasRenderPosition
            || object->deployed
            || runtimeMissionIsDeploying(*object)
            || runtimeMissionIsUndeploying(*object)) {
            return false;
        }
        const auto behavior = state.behaviors.find(objectId);
        if (behavior != state.behaviors.end()) {
            const auto& action = behavior->second;
            // Shooting from rest must retain logical camouflage. The weapon's
            // fake-blink timer reveals its body; pathing/chasing still unmasks it.
            return action.kind == RuntimeBehaviorKind::Attack
                && action.path.empty() && !action.pathRequestPending
                && action.renderMoveTicksRemaining <= 0
                && action.state != RuntimeBehaviorState::Moving
                && action.state != RuntimeBehaviorState::Chasing
                && action.state != RuntimeBehaviorState::Blocked;
        }
        const std::string mission = toLowerAsciiLocal(object->mission);
        return mission == "guard"
            || mission == "idle"
            || mission == "idle1"
            || mission == "idle2"
            || mission == "deployedidle";
    }
}
int checks=0,states=0,positive=0,negative=0;
std::string location;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(location+why);}
std::string image(RuntimeProductionHostState& s){std::string error;std::ostringstream save,mechanisms;
 const bool wrote=writeRuntimeStateToStream(s,save,error);require(wrote,"full actual save: "+error);s.specialMechanisms.save(mechanisms);
 std::string behavior;for(const auto& [id,b]:s.behaviors)behavior+=encode(id)+encode(b);
 return encode(save.str())+encode(mechanisms.str())+encode(s.specialMechanisms.stateHash())+encode(s.world.getObjects())+behavior;
}
std::string exercise(bool reference,const std::string& mapPath){std::string result;
 for(int hz:{15,30,60})for(int scenario=0;scenario<24;++scenario){
  location="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" ";
  auto box=std::make_unique<IndependentRecoveryHost>();auto& s=*box;std::string error;
  require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"actual base "+error);RuntimeSnapshotActiveStateScope scope(s);
  s.config.defaultMapPath=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";
  s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.tick=100;
  s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
  IniFile ini;require(ini.loadFromString(R"H22([General]
DisguiseDelay=2
DisguiseRearmDelay=2
DefaultMirageDisguises=TREE01
[VehicleTypes]
0=CAMO_TEST
[CAMO_TEST]
Strength=200
CanDisguise=yes
DisguiseWhenStill=yes
PermaDisguise=no
)H22")&&s.rules.loadFromIni(ini),"actual typed camouflage rules");s.rulesLoaded=true;
  const auto* type=s.rules.findUnitType("UnitType","CAMO_TEST");require(type&&type->canDisguise&&type->disguiseWhenStill&&!type->permaDisguise,"canonical eligibility fields parsed");
  s.world.accessObjects().clear();s.behaviors.clear();s.wrecks.clear();s.buildingCollapses.clear();s.projectiles.clear();s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
  WorldObjectInstance object;object.instanceId=41;object.mapId="H022ActualRoot";object.owner="Americans";object.type="CAMO_TEST";object.kind=WorldObjectKind::Unit;object.alive=true;object.health=200;
  object.x=5;object.y=5;object.cell=runtimeCellKey(5,5);object.mission="Guard";clearRuntimeRenderPosition(object);
  if(scenario==1)object.mission="Idle1";
  if(scenario==2)object.mission="Move";
  if(scenario==3)object.mission="Deploying";
  if(scenario==4)object.mission="MCVDeployTurningUnselected";
  if(scenario==5)object.mission="Undeploying";
  if(scenario==6)object.dockedToObjectId=99;
  if(scenario==7)object.hasRenderPosition=true;
  if(scenario==8)object.deployed=true;
  if(scenario==9)object.alive=false;
  if(scenario==23)object.mission=" guard ";
  if(scenario!=10)s.world.accessObjects().push_back(object);
  if(scenario>=11&&scenario<=22){RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Attack;b.state=RuntimeBehaviorState::Idle;
   if(scenario==12)b.state=RuntimeBehaviorState::Attacking;
   if(scenario==13)b.state=RuntimeBehaviorState::Cooldown;
   if(scenario==14)b.path.push_back({6,5});
   if(scenario==15)b.pathRequestPending=true;
   if(scenario==16)b.state=RuntimeBehaviorState::Moving;
   if(scenario==17)b.state=RuntimeBehaviorState::Chasing;
   if(scenario==18)b.state=RuntimeBehaviorState::Blocked;
   if(scenario==19)b.renderMoveTicksRemaining=1;
   if(scenario==20)b.kind=RuntimeBehaviorKind::Move;
   if(scenario==21)b.kind=RuntimeBehaviorKind::None;
   if(scenario==22)b.renderMoveTicksRemaining=std::numeric_limits<int>::min();
   s.behaviors.emplace(41,b);
  }
  s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;
  s.superWeapons.reset(std::vector<SuperWeaponRule>{});require(s.superWeapons.alignEmptyStateTick(s.tick),"empty SuperWeapon clock");s.superWeaponsInitialized=true;
  s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());s.specialMechanismsInitialized=true;
  const bool expected=scenario==0||scenario==1||scenario==11||scenario==12||scenario==13||scenario==22;
  const auto before=image(s);const bool stationary=reference?original::stationary(s,41):runtimeCamouflageObjectIsStationary(s,41);
  require(stationary==expected,"actual Root eligibility result");require(image(s)==before,"actual Root policy read-only complete save/world87/behavior49");
  if(stationary)++positive;else ++negative;
  std::vector<SpecialMechanismEvent> events;
  const auto sink=[&](const SpecialMechanismEvent& e){events.push_back(e);};
  const auto gate=[&](int id){return reference?original::stationary(s,id):runtimeCamouflageObjectIsStationary(s,id);};
  const bool changed=s.specialMechanisms.tickCamouflage(s.world.accessObjects(),s.rules,s.tick,gate,sink);
  require(s.specialMechanisms.isCamouflaged(41)==expected,"real canonical camouflage enter keeps classification");
  result+=encode(changed)+encode(stationary)+image(s);
  for(const auto& e:events)result+=encode(e.kind)+encode(e.tick)+encode(e.objectId)+encode(e.targetObjectId)+encode(e.value)+encode(e.cell)+encode(e.reason);
  if(expected){s.specialMechanisms.notifyActivity(41,s.tick,"fire",hz);require(s.specialMechanisms.isCamouflaged(41)&&s.specialMechanisms.isCamouflageBlinking(41,s.tick),"real fire preserves logical disguise and exposes sprite");}
  for(int step=1;step<=3;++step){++s.tick;s.specialMechanisms.tickCamouflage(s.world.accessObjects(),s.rules,s.tick,gate,SpecialEventSink{});require(s.specialMechanisms.isCamouflaged(41)==expected,"continued real camouflage state");result+=image(s);}
  ++states;
 }
 return result;
}
int run(const std::string& mapPath){try{const auto old=exercise(true,mapPath),now=exercise(false,mapPath);require(old==now,"full realRoot old/new images,events,fire,continuation differ");require(states==144&&positive==36&&negative==108,"all three rates and 24 old/new Root scenarios mandatory");
 std::cout<<"private-camouflage-stationary checks="<<checks<<" actual-root-states="<<states<<" positive="<<positive<<" negative="<<negative<<" fullsave-world87-behavior49-canonical-camouflage-fire-continuation=exact failures=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
