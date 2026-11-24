namespace h023_private_qualification {
using namespace concord::runtime::session_detail;
class ChronoQualificationClock final:public RuntimeUiClock { public:RuntimeUiTimeMs nowMs()const noexcept override{return 0;} };
const ChronoQualificationClock chronoQualificationClock;
class ChronoIndependentRecoveryHost final:public RuntimeProductionHostState { public:ChronoIndependentRecoveryHost():RuntimeProductionHostState(chronoQualificationClock,chronoQualificationClock){} };
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
    else if constexpr (requires {v.first;v.second;}) {return encode(v.first)+encode(v.second);}
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
bool runtimePersonalChronoTransportPassenger(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger) {
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    return passenger.kind == WorldObjectKind::Infantry && type
        && type->teleporter && !type->harvester;
}
std::optional<bool> runtimeChronoTransportDestinationAdmission(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const RuntimeCellPoint& destination) {
    if (!original::runtimePersonalChronoTransportPassenger(state, passenger)) return std::nullopt;
    const auto pending = state.pendingTransportTargets.find(passenger.instanceId);
    if (pending == state.pendingTransportTargets.end()) return std::nullopt;
    const auto* host = findRuntimeObjectByIdIncludingInactive(state, pending->second);
    if (!host || host->kind != WorldObjectKind::Unit) return std::nullopt;
    const auto move = state.behaviors.find(passenger.instanceId);
    if (move == state.behaviors.end() || move->second.kind != RuntimeBehaviorKind::Move
        || move->second.pendingAllowedOccupiedObjectId != host->instanceId
        || move->second.moveTarget != destination || runtimeCellPointForObject(*host) != destination
        || host->airborne || host->renderMoveActive
        || !runtimeObjectCanEnterTransport(state, passenger, *host, true, true)) return false;
    const auto* warp = state.specialMechanisms.chronoMoveState(passenger.instanceId);
    if (warp && state.specialMechanisms.isChronoInTransit(passenger.instanceId)
        && (warp->destination.x != destination.x || warp->destination.y != destination.y
            || warp->destinationLevel != host->level)) return false;
    return true;
}
bool runtimeChronoTransportArrivalReady(
    const RuntimeGameSessionState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& host) {
    const auto* warp = state.specialMechanisms.chronoMoveState(passenger.instanceId);
    const auto admitted = original::runtimeChronoTransportDestinationAdmission(
        state, passenger, runtimeCellPointForObject(host));
    return admitted && *admitted && warp
        && (warp->phase == ChronoPhase::WarpIn || warp->phase == ChronoPhase::Cooldown)
        && warp->completeTick <= state.tick
        && warp->destination.x == host.x && warp->destination.y == host.y
        && warp->destinationLevel == host.level
        && runtimeCellPointForObject(passenger) == runtimeCellPointForObject(host)
        && passenger.level == host.level && !passenger.renderMoveActive;
}
}
int checks=0,states=0,personalTrue=0,admitTrue=0,admitFalse=0,admitAbsent=0,readyTrue=0;
std::string location;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(location+why);}
std::string image(RuntimeProductionHostState& s){std::string error;std::ostringstream out,mechanism;
 const bool wrote=writeRuntimeStateToStream(s,out,error);require(wrote,"full actual Root save: "+error);s.specialMechanisms.save(mechanism);
 return encode(out.str())+encode(mechanism.str())+encode(s.specialMechanisms.stateHash())+encode(s.world.getObjects())+encode(s.behaviors);
}
std::string exercise(bool reference,const std::string& mapPath){std::string all;
 for(int hz:{15,30,60})for(int scenario=0;scenario<35;++scenario){
  location="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" ";
  auto box=std::make_unique<ChronoIndependentRecoveryHost>();auto& s=*box;std::string error;
  const bool configured=configureRuntimeDetailSnapshotSmokeWorld(s,false,error);require(configured,"actual base "+error);RuntimeSnapshotActiveStateScope active(s);
  s.config.defaultMapPath=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
  s.tick=100;s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
  std::string rules=R"H23([InfantryTypes]
0=TELE_PASS
[VehicleTypes]
0=CARGO_HOST
[TELE_PASS]
Strength=100
Speed=6
Size=1
Teleporter=yes
Harvester=no
[CARGO_HOST]
Strength=200
Speed=6
Passengers=3
SizeLimit=6
)H23";
  if(scenario==13)rules.replace(rules.find("Teleporter=yes"),14,"Teleporter=no");
  if(scenario==14)rules.replace(rules.find("Harvester=no"),12,"Harvester=yes");
  if(scenario==28)rules.replace(rules.find("Passengers=3"),12,"Passengers=0");
  if(scenario==29)rules.replace(rules.find("Size=1"),6,"Size=4");
  IniFile ini;require(ini.loadFromString(rules)&&s.rules.loadFromIni(ini),"actual typed rules");s.rulesLoaded=true;
  const auto* typed=s.rules.findUnitType("InfantryType","TELE_PASS");require(typed&&typed->teleporter==(scenario!=13)&&typed->harvester==(scenario==14),"original canonical personal eligibility fields");
  s.world.accessObjects().clear();s.behaviors.clear();s.pendingTransportTargets.clear();s.wrecks.clear();s.buildingCollapses.clear();s.projectiles.clear();s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
  WorldObjectInstance p,h;p.instanceId=41;p.kind=WorldObjectKind::Infantry;p.type="TELE_PASS";p.owner="Americans";p.health=100;p.alive=true;p.x=5;p.y=6;p.level=2;p.cell=runtimeCellKey(5,6);p.mission="Guard";
  h.instanceId=52;h.kind=WorldObjectKind::Unit;h.type="CARGO_HOST";h.owner="Americans";h.health=200;h.alive=true;h.x=5;h.y=6;h.level=2;h.cell=runtimeCellKey(5,6);h.mission="Guard";
  if(scenario==9){p.x=4;p.cell=runtimeCellKey(p.x,p.y);}
  if(scenario==10)p.level=1;
  if(scenario==11)p.renderMoveActive=true;
  if(scenario==12)p.kind=WorldObjectKind::Unit;
  if(scenario==17)h.kind=WorldObjectKind::Structure;
  if(scenario==22)h.airborne=true;
  if(scenario==23)h.renderMoveActive=true;
  if(scenario==24){h.alive=false;h.health=0;}
  if(scenario==25){p.alive=false;p.health=0;}
  if(scenario==26)p.owner="Russians";
  if(scenario==27)p.dockedToObjectId=99;
  if(scenario==34)p.type="UNKNOWN_PASSENGER_TYPE";
  s.world.accessObjects().push_back(p);s.world.accessObjects().push_back(h);
  RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Move;b.state=RuntimeBehaviorState::Idle;b.pendingAllowedOccupiedObjectId=52;b.moveTarget={5,6};
  if(scenario==19)b.kind=RuntimeBehaviorKind::Attack;
  if(scenario==20)b.pendingAllowedOccupiedObjectId=53;
  if(scenario==21)b.moveTarget.x=7;
  if(scenario!=18)s.behaviors.emplace(41,b);
  if(scenario!=15)s.pendingTransportTargets.emplace(41,(scenario==16||scenario==32)?53:52);
  s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());s.specialMechanismsInitialized=true;
  if(scenario!=2){const std::string phase=scenario==1?"Cooldown":scenario==3||scenario==30||scenario==31?"WarpOut":scenario==4?"InTransit":scenario==5?"None":"WarpIn";
   const int dx=(scenario==7||scenario==30)?4:5,dl=(scenario==8||scenario==31)?3:2,complete=scenario==6?101:100;
   IniFile save;const auto text="[SpecialMechanisms]\nVersion=1\n[SpecialChrono]\nCount=1\n0=41|"+phase+"|5|6|"+std::to_string(dx)+"|6|2|"+std::to_string(dl)+"|90|"+std::to_string(complete)+"|200\n";
   const bool loaded=save.loadFromString(text)&&s.specialMechanisms.load(save,error);require(loaded,"original canonical chrono save loader "+error);
  }
  s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;
  s.superWeapons.reset(std::vector<SuperWeaponRule>{});require(s.superWeapons.alignEmptyStateTick(s.tick),"SuperWeapon clock");s.superWeaponsInitialized=true;
  auto& passenger=s.world.accessObjects()[0];auto& host=s.world.accessObjects()[1];const RuntimeCellPoint destination=scenario==33?RuntimeCellPoint{7,6}:RuntimeCellPoint{5,6};
  const auto before=image(s);
  const bool personal=reference?original::runtimePersonalChronoTransportPassenger(s,passenger):runtimePersonalChronoTransportPassenger(s,passenger);require(image(s)==before,"personal query read-only complete authority");
  const auto admitted=reference?original::runtimeChronoTransportDestinationAdmission(s,passenger,destination):runtimeChronoTransportDestinationAdmission(s,passenger,destination);require(image(s)==before,"tri-state admission read-only complete authority");
  const bool ready=reference?original::runtimeChronoTransportArrivalReady(s,passenger,host):runtimeChronoTransportArrivalReady(s,passenger,host);require(image(s)==before,"ready query read-only complete authority");
  const bool expectedPersonal=scenario!=12&&scenario!=13&&scenario!=14&&scenario!=34;
  require(personal==expectedPersonal,"actual typed personal classification");
  const bool expectAbsent=!expectedPersonal||scenario==15||scenario==16||scenario==17||scenario==32;
  const bool expectAdmitted=!expectAbsent&&(scenario<=11||scenario==33)&&scenario!=33;
  require(!admitted.has_value()==expectAbsent,"original noop versus reject distinction");
  if(admitted)require(*admitted==expectAdmitted,"actual original admission positive/negative branch");
  const bool expectedReady=scenario==0||scenario==1||scenario==33;require(ready==expectedReady,"real canonical phase/tick/pose readiness");
  if(personal)++personalTrue;if(!admitted)++admitAbsent;else if(*admitted)++admitTrue;else ++admitFalse;if(ready)++readyTrue;
  all+=encode(personal)+encode(admitted)+encode(ready)+image(s);
  for(int step=0;step<2;++step){++s.tick;require(s.particles.alignEmptyStateTick(s.tick)&&s.superWeapons.alignEmptyStateTick(s.tick),"empty canonical clock continuation");
   s.specialMechanisms.tickChronoMoves(s.world.accessObjects(),s.tick,[](int,int,int){return true;},[](int,int,int){return false;},SpecialEventSink{});all+=image(s);
  }
  ++states;
 }
 return all;
}
int run(const std::string& mapPath){try{const auto old=exercise(true,mapPath),now=exercise(false,mapPath);require(old==now,"full real Root save/world87/behavior49/canonicalchrono continuation differs");require(states==210&&personalTrue>0&&admitTrue>0&&admitFalse>0&&admitAbsent>0&&readyTrue>0,"all 35cases/3rates/old-current andall tri-state branches mandatory");
 std::cout<<"private-chrono-transport checks="<<checks<<" actual-root-states="<<states<<" personal="<<personalTrue<<" admitted="<<admitTrue<<" rejected="<<admitFalse<<" absent="<<admitAbsent<<" ready="<<readyTrue<<" fullsave-world87-behavior49-canonicalchrono-and-continuation=exact failures=0\n";return 0;
}catch(const std::exception& e){std::cerr<<location<<e.what()<<'\n';return 1;}}
}
