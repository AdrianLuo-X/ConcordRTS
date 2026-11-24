namespace h02302_private_qualification {
using namespace concord::runtime::session_detail;
using h023_private_qualification::encode;
int checks=0,states=0,held=0,notHeld=0,accepted=0,rejected=0,absent=0,effectProbes=0,airProbes=0,observedImpacts=0,externalBehaviorCases=0,generatedTerrainCases=0;
std::string location;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(location+why);}
namespace original {
bool holdRuntimeChronoRecovery(
    RuntimeGameSessionState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    if (!state.specialMechanismsInitialized
        || !state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick)) return false;
    clearRuntimeRenderPosition(object);
    behavior.state = RuntimeBehaviorState::Cooldown;
    if (!state.specialMechanisms.isChronoInTransit(object.instanceId)) object.mission = "ChronoFrozen";
    return true;
}
std::optional<bool> advanceRuntimeTeleporterMove(
    RuntimeProductionHostState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, RuntimeCellPoint destination) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    // Harvesters manage their drive-to-ore / blink-to-refinery cycle separately.
    if (unit == nullptr || !unit->teleporter || unit->harvester) return std::nullopt;
    const auto transportAdmission = runtimeChronoTransportDestinationAdmission(state, object, destination);
    if (transportAdmission && !*transportAdmission) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    if (runtimeCellPointForObject(object) == destination && !transportAdmission) return std::nullopt;
    const auto generatedTerrain = state.terrainByCell.empty()
        ? buildRuntimeTerrainLookup(state.world) : std::unordered_map<int, const TerrainCell*>{};
    const auto& terrain = state.terrainByCell.empty() ? generatedTerrain : state.terrainByCell;
    const auto occupied = buildRuntimeOccupancyLookup(state);
    const SpecialCellPredicate legal = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return *entry;
        return runtimeCellIsPassable(state, terrain, occupied, {x, y}, object, ignoredId);
    };
    const SpecialCellPredicate blocked = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return !*entry;
        return runtimeChronoCellIsOccupied(state,occupied,x,y,ignoredId);
    };
    std::string reason;
    const auto pendingHost = state.pendingTransportTargets.find(object.instanceId);
    const auto* host = transportAdmission && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    const int destinationLevel = host ? host->level : terrainLevelAtRuntimeCell(state, destination.x, destination.y);
    if (!requestRuntimeChronoMoveWithParasite(state, object.instanceId,
            {destination.x, destination.y}, destinationLevel,
            legal, blocked, reason)) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksRemaining = 0;
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Cooldown;
    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    clearRuntimeRenderPosition(object);
    return true;
}
}
std::string roofImage(const RuntimeParticleClearanceRoof& v){return encode(v.cellSum)+encode(v.level)+encode(v.authoredHeight)+encode(v.nativeSpriteHeight)+encode(v.rubble);}
std::string airborneImage(const RuntimeParticleAirbornePresentation& v){std::string out=encode(v.enabled)+encode(v.sourceCellSum)+encode(v.sourceLevel)+encode(v.factoryLiftCancellation)+encode(v.clearanceFraction)+encode(v.resolvedClearanceLevel)+encode(v.roofs.size());for(const auto& r:v.roofs)out+=roofImage(r);return out;}
std::string impactImage(const RuntimeImpactEffect& v){return encode(v.projectileActorId)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.targetIsStructure)+encode(v.owner)+encode(v.cell)+encode(v.level)+encode(v.hasWorldPosition)+encode(v.worldX)+encode(v.worldY)+encode(v.worldLevel)+encode(v.screenOffsetX)+encode(v.screenOffsetY)+encode(v.nativeFixedX)+encode(v.nativeFixedY)+encode(v.bodyCenterAnchored)+airborneImage(v.bodyCenterPresentation)+encode(v.bodyCenterSpriteOffsetY)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.radius)+encode(v.frameIndex)+encode(v.facing)+encode(v.muzzleAnchorX)+encode(v.muzzleAnchorY)+encode(v.muzzleAnchorLevel)+encode(v.muzzleFacing)+encode(v.animationFrameCount)+encode(v.animationFrameDelayTicks)+encode(v.animationStartFrame)+encode(v.animationLoopStartFrame)+encode(v.animationLoopEndFrame)+encode(v.animationLoopCount)+encode(v.animationEndFrame)+encode(v.animationSequenceSteps)+encode(v.animationMidpointWidth)+encode(v.animationMidpointHeight)+encode(v.lastSmudgeMidpointStep)+encode(v.deferredParticleAnimId)+encode(v.deferredParticleOwner)+encode(v.deferredParticleTicks)+encode(v.death)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.animId)+encode(v.warheadId)+encode(v.warheadImpactIntentId)+encode(v.trailerIntentId)+encode(v.particleId)+encode(v.damage)+encode(v.cellSpread)+encode(v.bright);}
std::string impactsImage(const RuntimeProductionHostState& s){std::string out=encode(s.impactEffects.size());for(const auto& e:s.impactEffects)out+=impactImage(e);return out;}
template<class T>void perturbEffect(T& v){if constexpr(std::is_same_v<T,bool>)v=!v;else if constexpr(std::is_enum_v<T>)v=static_cast<T>(static_cast<int>(v)==0?1:0);else if constexpr(std::is_integral_v<T>)v=v==0?1:0;else if constexpr(std::is_floating_point_v<T>)v=-17.25;else if constexpr(std::is_same_v<T,std::string>)v+="probe";else if constexpr(std::is_same_v<T,RuntimeParticleAirbornePresentation>)v.enabled=!v.enabled;else if constexpr(requires{v.emplace_back();})v.emplace_back();else if constexpr(requires{v.x;v.y;})v.x=v.x==0?1:0;else static_assert(sizeof(T)==0,"unprobed impact field");}
void verifyAirborneEncoder(){RuntimeParticleAirbornePresentation zero;const auto before=airborneImage(zero);
{auto v=zero;perturbEffect(v.enabled);require(airborneImage(v)!=before,"airborne enabled omitted");++airProbes;}
{auto v=zero;perturbEffect(v.sourceCellSum);require(airborneImage(v)!=before,"airborne sourceCellSum omitted");++airProbes;}
{auto v=zero;perturbEffect(v.sourceLevel);require(airborneImage(v)!=before,"airborne sourceLevel omitted");++airProbes;}
{auto v=zero;perturbEffect(v.factoryLiftCancellation);require(airborneImage(v)!=before,"airborne factoryLiftCancellation omitted");++airProbes;}
{auto v=zero;perturbEffect(v.clearanceFraction);require(airborneImage(v)!=before,"airborne clearanceFraction omitted");++airProbes;}
{auto v=zero;perturbEffect(v.resolvedClearanceLevel);require(airborneImage(v)!=before,"airborne resolvedClearanceLevel omitted");++airProbes;}
{auto v=zero;perturbEffect(v.roofs);require(airborneImage(v)!=before,"airborne roofs omitted");++airProbes;}
RuntimeParticleClearanceRoof roof;const auto roofBefore=roofImage(roof);
{auto v=roof;perturbEffect(v.cellSum);require(roofImage(v)!=roofBefore,"roof cellSum omitted");++airProbes;}
{auto v=roof;perturbEffect(v.level);require(roofImage(v)!=roofBefore,"roof level omitted");++airProbes;}
{auto v=roof;perturbEffect(v.authoredHeight);require(roofImage(v)!=roofBefore,"roof authoredHeight omitted");++airProbes;}
{auto v=roof;perturbEffect(v.nativeSpriteHeight);require(roofImage(v)!=roofBefore,"roof nativeSpriteHeight omitted");++airProbes;}
{auto v=roof;perturbEffect(v.rubble);require(roofImage(v)!=roofBefore,"roof rubble omitted");++airProbes;}
}
void verifyImpactEncoder(){RuntimeImpactEffect zero;const auto before=impactImage(zero);
{auto v=zero;perturbEffect(v.projectileActorId);require(impactImage(v)!=before,"impact projectileActorId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.attackerObjectId);require(impactImage(v)!=before,"impact attackerObjectId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.targetObjectId);require(impactImage(v)!=before,"impact targetObjectId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.targetIsStructure);require(impactImage(v)!=before,"impact targetIsStructure omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.owner);require(impactImage(v)!=before,"impact owner omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.cell);require(impactImage(v)!=before,"impact cell omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.level);require(impactImage(v)!=before,"impact level omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.hasWorldPosition);require(impactImage(v)!=before,"impact hasWorldPosition omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.worldX);require(impactImage(v)!=before,"impact worldX omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.worldY);require(impactImage(v)!=before,"impact worldY omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.worldLevel);require(impactImage(v)!=before,"impact worldLevel omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.screenOffsetX);require(impactImage(v)!=before,"impact screenOffsetX omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.screenOffsetY);require(impactImage(v)!=before,"impact screenOffsetY omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.nativeFixedX);require(impactImage(v)!=before,"impact nativeFixedX omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.nativeFixedY);require(impactImage(v)!=before,"impact nativeFixedY omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.bodyCenterAnchored);require(impactImage(v)!=before,"impact bodyCenterAnchored omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.bodyCenterPresentation);require(impactImage(v)!=before,"impact bodyCenterPresentation omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.bodyCenterSpriteOffsetY);require(impactImage(v)!=before,"impact bodyCenterSpriteOffsetY omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.totalTicks);require(impactImage(v)!=before,"impact totalTicks omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.remainingTicks);require(impactImage(v)!=before,"impact remainingTicks omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.radius);require(impactImage(v)!=before,"impact radius omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.frameIndex);require(impactImage(v)!=before,"impact frameIndex omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.facing);require(impactImage(v)!=before,"impact facing omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.muzzleAnchorX);require(impactImage(v)!=before,"impact muzzleAnchorX omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.muzzleAnchorY);require(impactImage(v)!=before,"impact muzzleAnchorY omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.muzzleAnchorLevel);require(impactImage(v)!=before,"impact muzzleAnchorLevel omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.muzzleFacing);require(impactImage(v)!=before,"impact muzzleFacing omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationFrameCount);require(impactImage(v)!=before,"impact animationFrameCount omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationFrameDelayTicks);require(impactImage(v)!=before,"impact animationFrameDelayTicks omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationStartFrame);require(impactImage(v)!=before,"impact animationStartFrame omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationLoopStartFrame);require(impactImage(v)!=before,"impact animationLoopStartFrame omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationLoopEndFrame);require(impactImage(v)!=before,"impact animationLoopEndFrame omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationLoopCount);require(impactImage(v)!=before,"impact animationLoopCount omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationEndFrame);require(impactImage(v)!=before,"impact animationEndFrame omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationSequenceSteps);require(impactImage(v)!=before,"impact animationSequenceSteps omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationMidpointWidth);require(impactImage(v)!=before,"impact animationMidpointWidth omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animationMidpointHeight);require(impactImage(v)!=before,"impact animationMidpointHeight omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.lastSmudgeMidpointStep);require(impactImage(v)!=before,"impact lastSmudgeMidpointStep omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.deferredParticleAnimId);require(impactImage(v)!=before,"impact deferredParticleAnimId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.deferredParticleOwner);require(impactImage(v)!=before,"impact deferredParticleOwner omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.deferredParticleTicks);require(impactImage(v)!=before,"impact deferredParticleTicks omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.death);require(impactImage(v)!=before,"impact death omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.lifecycleState);require(impactImage(v)!=before,"impact lifecycleState omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.renderIntent);require(impactImage(v)!=before,"impact renderIntent omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.weaponId);require(impactImage(v)!=before,"impact weaponId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.weaponSlot);require(impactImage(v)!=before,"impact weaponSlot omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.projectileId);require(impactImage(v)!=before,"impact projectileId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.projectileImageId);require(impactImage(v)!=before,"impact projectileImageId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.animId);require(impactImage(v)!=before,"impact animId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.warheadId);require(impactImage(v)!=before,"impact warheadId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.warheadImpactIntentId);require(impactImage(v)!=before,"impact warheadImpactIntentId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.trailerIntentId);require(impactImage(v)!=before,"impact trailerIntentId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.particleId);require(impactImage(v)!=before,"impact particleId omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.damage);require(impactImage(v)!=before,"impact damage omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.cellSpread);require(impactImage(v)!=before,"impact cellSpread omitted");++effectProbes;}
{auto v=zero;perturbEffect(v.bright);require(impactImage(v)!=before,"impact bright omitted");++effectProbes;}
}

std::string raw(RuntimeProductionHostState& s,const RuntimeObjectBehavior& external){std::ostringstream special;s.specialMechanisms.save(special);return encode(s.world.getObjects())+encode(s.behaviors)+encode(external)+encode(s.specialMechanismsInitialized)+encode(s.tick)+encode(s.specialMechanisms.stateHash())+encode(special.str())+impactsImage(s);}
std::string image(RuntimeProductionHostState& s,const RuntimeObjectBehavior& external){const auto before=raw(s,external);std::string error;std::ostringstream out;const bool wrote=writeRuntimeStateToStream(s,out,error);require(wrote,"full realRoot save: "+error);return before+encode(out.str())+raw(s,external);}
std::string exercise(bool reference,const std::string& mapPath){std::string all;
 for(int hz:{15,30,60})for(int scenario=0;scenario<40;++scenario)for(int entry:{0,1}){
  location="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" entry="+std::to_string(entry)+" ";
  auto box=std::make_unique<h023_private_qualification::ChronoIndependentRecoveryHost>();auto& s=*box;std::string error;
  const bool configured=configureRuntimeDetailSnapshotSmokeWorld(s,false,error);require(configured,"actual base "+error);RuntimeSnapshotActiveStateScope active(s);
  s.config.defaultMapPath=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
  s.tick=100;s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
  std::string rules=R"H23([General]
WarpOut=WARPOUT
WarpIn=WARPIN
[InfantryTypes]
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
  auto& passenger=s.world.accessObjects()[0];const RuntimeCellPoint destination=scenario==33?RuntimeCellPoint{7,6}:RuntimeCellPoint{5,6};

  if(scenario==35||scenario==38||scenario==39)s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());
  if(scenario==36)s.specialMechanismsInitialized=false;
  if(scenario==37){s.tick=201;require(s.particles.alignEmptyStateTick(s.tick)&&s.superWeapons.alignEmptyStateTick(s.tick),"expired clock fixture");}
  if(scenario==38){s.terrainByCell.clear();++generatedTerrainCases;}
  passenger.hasRenderPosition=true;passenger.renderX=13.25;passenger.renderY=-9.75;passenger.renderMoveActive=true;passenger.renderLevel=7.125;passenger.renderOnBridge=!passenger.onBridge;
  RuntimeObjectBehavior external;external.kind=RuntimeBehaviorKind::Attack;external.state=RuntimeBehaviorState::Idle;external.moveTarget=destination;
  auto& behavior=(scenario==18||scenario==35)?external:s.behaviors.at(41);
  if(&behavior==&external)++externalBehaviorCases;
  behavior.path={{1,2},{3,4},{5,6}};behavior.pathRequestPending=true;behavior.moveCooldownTicks=17;behavior.renderMoveTicksRemaining=19;behavior.blockedTicks=23;
  if(scenario==39)behavior.path.clear();
  if(scenario==2){passenger.x=1;passenger.y=2;passenger.cell=runtimeCellKey(1,2);}
  all+=raw(s,external);
  std::optional<bool> result;
  if(entry==0){const bool value=reference?original::holdRuntimeChronoRecovery(s,passenger,behavior):concord::runtime::session_detail::holdRuntimeChronoRecovery(s,passenger,behavior);result=value;if(value)++held;else ++notHeld;}
  else {result=reference?original::advanceRuntimeTeleporterMove(s,passenger,behavior,destination):concord::runtime::session_detail::advanceRuntimeTeleporterMove(s,passenger,behavior,destination);if(!result)++absent;else if(*result)++accepted;else ++rejected;}
  all+=encode(result)+image(s,external);observedImpacts+=static_cast<int>(s.impactEffects.size());
  for(int step=0;step<2;++step){++s.tick;require(s.particles.alignEmptyStateTick(s.tick)&&s.superWeapons.alignEmptyStateTick(s.tick),"empty canonical continuation clocks");s.specialMechanisms.tickChronoMoves(s.world.accessObjects(),s.tick,[](int,int,int){return true;},[](int,int,int){return false;},SpecialEventSink{});all+=image(s,external);}
  ++states;
 }
 return all;
}
int run(const std::string& mapPath){try{verifyImpactEncoder();verifyAirborneEncoder();const auto before=exercise(true,mapPath),current=exercise(false,mapPath);require(before==current,"real Root fullsave/world87/behavior49/effects andcanonicalcontinuation differ");require(states==480&&effectProbes==56&&airProbes==12&&held>0&&notHeld>0&&accepted>0&&rejected>0&&absent>0&&observedImpacts>0&&externalBehaviorCases>0&&generatedTerrainCases>0,"all40cases/3rates/2entries/old-current andall branches mandatory");
 std::cout<<"private-chrono-locomotion checks="<<checks<<" actual-root-states="<<states<<" held="<<held<<" not-held="<<notHeld<<" accepted="<<accepted<<" rejected="<<rejected<<" absent="<<absent<<" impact-probes="<<effectProbes<<" airborne-leaf-probes="<<airProbes<<" observed-impacts="<<observedImpacts<<" external-behavior="<<externalBehaviorCases<<" generated-terrain="<<generatedTerrainCases<<" real-Root-world87-behavior49-fullimpact-save-and-canonicalcontinuation=exact failures=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
