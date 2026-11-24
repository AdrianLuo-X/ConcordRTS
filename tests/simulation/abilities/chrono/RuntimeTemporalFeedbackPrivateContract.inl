// Test-only frozen original handlers and actual original production consumers.
namespace h02403_private_qualification {
using namespace concord::runtime::session_detail;
using h023_private_qualification::encode;
int checks=0,states=0,positiveBeam=0,positiveTimeline=0,positiveSound=0,authorityChecks=0;
std::string where;
void require(bool b,const std::string& why){++checks;if(!b)throw std::runtime_error(where+why);}
namespace original {
void resolveRuntimeTemporalBeamEndpoints(RuntimeProductionHostState& state) {
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

void spawnRuntimeTemporalEraseEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event) {
    const auto* target = findObjectById(state, event.targetObjectId);
    if (target == nullptr) return;
    RuntimeImpactEffect effect;
    effect.attackerObjectId = event.objectId;
    effect.targetObjectId = target->instanceId;
    effect.owner = target->owner;
    effect.cell = {target->x, target->y};
    effect.level = target->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*target);
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

}
std::string snapshotImage(const SpecialMechanismRenderSnapshot& v){return encode(v.kind)+encode(v.instanceId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.superWeaponId)+encode(v.sourceCell)+encode(v.targetCell)+encode(v.cell)+encode(v.cell2)+encode(v.sourceX)+encode(v.sourceY)+encode(v.targetX)+encode(v.targetY)+encode(v.sourceLevel)+encode(v.targetLevel)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasBeamEndpoints)+encode(v.missilePitch)+encode(v.missileFacing)+encode(v.radius)+encode(v.intensity)+encode(v.maximumIntensity)+encode(v.progress)+encode(v.requiredProgress)+encode(v.expireTick)+encode(v.owner)+encode(v.color)+encode(v.phase)+encode(v.resourceId);}

std::string authority(RuntimeProductionHostState& s){std::ostringstream special;s.specialMechanisms.save(special);return encode(s.world.getObjects())+encode(s.behaviors)+encode(s.specialMechanisms.stateHash())+encode(special.str());}
std::string feedbackImage(RuntimeProductionHostState& s){auto all=h02402_private_qualification::temporalImage(s);for(const auto& v:s.specialMechanismRenderSnapshots)all+=snapshotImage(v);return all;}
std::unique_ptr<h023_private_qualification::ChronoIndependentRecoveryHost> scene(const std::string& mapPath,const std::filesystem::path& output,int hz,bool erase,int scenario,int count){
 auto box=h02402_private_qualification::scene(mapPath,hz,0,count);auto& s=*box;
 // The prior advance-only fixture authored RadBeam; endpoint gating uses the parsed IsRadBeam field.
 auto* beam=const_cast<WeaponType*>(s.rules.findWeaponType("H24_BEAM"));require(beam!=nullptr,"actual typed authored beam");beam->radBeam=true;
 s.config.localDataRootPath=output.generic_string();s.config.resourceRootPath=output.generic_string();s.theaterLoaded=false;s.renderCache=RuntimeMapRenderCache{};
 IniFile art;require(art.loadFromString("[H02403_ERASE]\nImage=h02305_warp\nRate=31\nShadow=no\nReport=H02403_REPORT\n")&&s.art.loadFromIni(art),"actual ART erase/report");s.artLoaded=true;
 auto& tuning=const_cast<SpecialMechanismRules&>(s.rules.getSpecialMechanismRules());tuning.warpAwayAnim="H02403_ERASE";
 auto* source=findMutableObjectById(s,41);auto* target=findMutableObjectById(s,52);auto* host=findMutableObjectById(s,60);require(source&&target&&host,"actual real identities");
 target->subCell=3;target->hasRenderPosition=true;target->renderX=6.25;target->renderY=6.625;target->renderLevel=1.125;target->facing=17;
 s.specialMechanismRenderSnapshots=s.specialMechanisms.renderSnapshots(s.tick,s.config.fixedTickHz);
 require(static_cast<int>(std::count_if(s.specialMechanismRenderSnapshots.begin(),s.specialMechanismRenderSnapshots.end(),[](const auto& p){return p.kind=="temporalBeam";}))==count,"canonical production beam snapshots count");
 if(erase){switch(scenario){
 case 1:target->alive=false;break;
 case 2:tuning.warpAwayAnim.clear();break;
 case 3:tuning.warpAwayAnim="none";break;
 case 4:tuning.warpAwayAnim="NoNe";break;
 case 5:tuning.warpAwayAnim=" none ";break;
 case 6:tuning.warpAwayAnim="MISSING_ART";break;
 case 7:const_cast<ArtType*>(s.art.findArtType("H02403_ERASE"))->reportSound.clear();break;
 case 8:const_cast<ArtType*>(s.art.findArtType("H02403_ERASE"))->reportSound=" Report With Spaces ";break;
 case 9:target->hasRenderPosition=false;break;
 case 10:target->airborne=true;target->flightLevel=4.0;break;
 case 11:target->kind=WorldObjectKind::Structure;target->type="H24_BUILD";break;
 case 12:target->subCell=-1;break;
 case 13:target->onBridge=true;target->level=2;break;
 case 14:target->facing=-1;break;
 }}else{switch(scenario){
 case 1:for(auto& v:s.specialMechanismRenderSnapshots)v.kind="camouflage";break;
 case 2:for(auto& v:s.specialMechanismRenderSnapshots)v.objectId=999;break;
 case 3:for(auto& v:s.specialMechanismRenderSnapshots)v.targetObjectId=999;break;
 case 4:target->alive=false;break;
 case 5:case 6:case 7:case 8:case 14:{host->transportPassengerIds.clear();for(int n=0;n<count;++n){auto* p=findMutableObjectById(s,41+n);p->alive=false;p->garrisonHostObjectId=scenario==8?999:60;p->mission="InsideTransport";host->transportPassengerIds.push_back(41+n);if(scenario==6)s.combatDetails.fireCursors[41+n].sourceAnchorObjectId=999;if(scenario==7)s.combatDetails.fireCursors.erase(41+n);}if(scenario==14)s.behaviors.at(60).forceFire=true;break;}
 case 9:for(int n=0;n<count;++n)findMutableObjectById(s,41+n)->type="UNKNOWN";break;
 case 10:const_cast<WeaponType*>(s.rules.findWeaponType("H24_BEAM"))->radBeam=false;break;
 case 11:case 12:const_cast<WarheadType*>(s.rules.findWarheadType("H24_WARHEAD"))->temporal=false;break;
 case 13:for(int n=0;n<count;++n)s.behaviors.at(41+n).forceFire=true;break;
 case 15:for(int n=0;n<count;++n){s.behaviors.at(41+n).kind=RuntimeBehaviorKind::Move;s.behaviors.at(41+n).forceFire=true;}break;
 case 16:for(int n=0;n<count;++n)s.behaviors.at(41+n).targetObjectId=70;break;
 case 17:for(int n=0;n<count;++n)s.behaviors.erase(41+n);break;
 case 18:target->kind=WorldObjectKind::Structure;target->type="H24_BUILD";break;
 case 19:source->hasRenderPosition=true;source->renderX=4.25;source->renderY=5.75;source->renderLevel=2.5;break;
 case 20:target->renderX=-1000.25;target->renderY=1000.75;break;
 case 21:for(auto& v:s.specialMechanismRenderSnapshots){v.hasBeamEndpoints=true;v.sourceX=-123.125;v.color="stale";}break;
 }}return box;
}
std::string exercise(bool reference,const std::string& mapPath,const std::filesystem::path& output,int hz,bool erase,int scenario,int count){
 where="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" erase="+std::to_string(erase)+" scenario="+std::to_string(scenario)+" count="+std::to_string(count)+" ";auto box=scene(mapPath,output,hz,erase,scenario,count);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);const auto before=authority(s);std::string all=feedbackImage(s);
 if(erase){SpecialMechanismEvent e;e.kind="TemporalCompleted";e.objectId=41;e.targetObjectId=52;e.tick=s.tick;
  if(reference)original::spawnRuntimeTemporalEraseEffect(s,e);else spawnRuntimeTemporalEraseEffect(s,e);
  const bool expected=scenario!=1&&scenario!=2&&scenario!=3&&scenario!=4;require(s.impactEffects.size()==static_cast<std::size_t>(expected),"actual original no-trim animation guards");if(expected&&scenario!=5&&scenario!=6){require(s.impactEffects.back().animationFrameCount>0,"actual positive animation timeline");++positiveTimeline;}
  if(scenario==0){require(s.runtimeSoundEvents>0||!s.runtimeSoundIntentHistory.empty()||!s.lastRuntimeSound.empty(),"actual erase report sound feedback");++positiveSound;}
 }else{
  if(reference)original::resolveRuntimeTemporalBeamEndpoints(s);else resolveRuntimeTemporalBeamEndpoints(s);
  if(scenario==0||scenario==5||scenario==13||scenario==14){const auto n=std::count_if(s.specialMechanismRenderSnapshots.begin(),s.specialMechanismRenderSnapshots.end(),[](const auto& p){return p.kind=="temporalBeam"&&p.hasBeamEndpoints;});require(n==count,"actual positive original live/passenger/force beam endpoints");positiveBeam+=static_cast<int>(n);}
  all+=feedbackImage(s);if(reference)original::resolveRuntimeTemporalBeamEndpoints(s);else resolveRuntimeTemporalBeamEndpoints(s);
 }
 require(authority(s)==before,"presentation must not modify canonical authority");++authorityChecks;all+=feedbackImage(s);
 // Actual owner call sites: canonical snapshot refresh and TemporalCompleted sink.
 if(scenario==0){if(erase){SpecialMechanismEvent e;e.kind="TemporalCompleted";e.objectId=41;e.targetObjectId=52;e.tick=s.tick;runtimeSpecialEventSink(s)(e);require(s.impactEffects.size()==2,"actual original World event consumer reached");}else{refreshRuntimeSpecialMechanismRenderSnapshots(s);require(std::count_if(s.specialMechanismRenderSnapshots.begin(),s.specialMechanismRenderSnapshots.end(),[](const auto& p){return p.kind=="temporalBeam"&&p.hasBeamEndpoints;})==count,"actual original World snapshot consumer reached");}all+=feedbackImage(s);}
 ++states;return all;
}
int run(const std::string& mapPath,const std::filesystem::path& output){try{
 h02305_private_qualification::writeAnimationFixture(output,mapPath);
 for(bool erase:{false,true})for(int hz:{15,30,60})for(int scenario=0;scenario<(erase?15:22);++scenario)for(int count:{1,3}){const auto a=exercise(true,mapPath,output,hz,erase,scenario,count),b=exercise(false,mapPath,output,hz,erase,scenario,count);require(a==b,"actual full save/world87/behavior49/snapshot32/effect56/audio/cache differs");}
 require(states==444&&authorityChecks==444&&positiveBeam>0&&positiveTimeline>0&&positiveSound>0,"full actual original caller matrix and mandatory positive branches");
 std::cout<<"private-temporal-feedback checks="<<checks<<" actual-root-states="<<states<<" positive-beam="<<positiveBeam<<" positive-timeline="<<positiveTimeline<<" positive-sound="<<positiveSound<<" authority-checks="<<authorityChecks<<" snapshot-fields=32 effect-fields=56 fullsave-world87-behavior49-canonical-and-original-callers=exact failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"private-temporal-feedback FAIL "<<e.what()<<'\n';return 1;}}
}
