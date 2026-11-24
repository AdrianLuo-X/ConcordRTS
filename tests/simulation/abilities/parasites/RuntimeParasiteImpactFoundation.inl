#include "simulation/abilities/parasites/RuntimeParasiteImpactPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteState.h"
#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include "RuntimeParasiteImpactFields.inl"
#include <sstream>
#include <memory>
#include "world/model/GameWorldSubCellOffset.h"
namespace parasite_impact_model {
constexpr auto worldObjectSubCellOffset = [](
    const WorldObjectInstance& object
) -> WorldObjectCellOffset {
    return object.kind == WorldObjectKind::Infantry
        ? worldInfantrySubCellOffset(object.subCell) : WorldObjectCellOffset{};
};
namespace current=concord::simulation::abilities;
namespace fields=parasite_impact_fields;
using concord::runtime::session_detail::RuntimeParasiteActionApplyResult;
using concord::runtime::session_detail::RuntimeObjectExitRequest;
using concord::runtime::session_detail::RuntimeObjectExitQueueResult;
using concord::runtime::session_detail::RuntimeObjectExitKind;
using concord::runtime::session_detail::RuntimeProjectileEffect;
using Kind=SpecialMechanismActionKind;
using fields::encode;
int checks=0,states=0,faults=0,positiveKinds=0,capacityActions=0;
std::set<std::string> hitPorts;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
struct Model;
void nestedLaunch(Model&,int);
thread_local Model* active=nullptr;
void observe(const std::string& port,const std::string& detail={},bool after=false);
std::string cause(const RuntimeDeathWeaponCause& c){return encode(c.cascadeId)+encode(c.parentRequestId)+encode(c.parentDepth);}
std::string exitValue(const RuntimeObjectExitRequest& x){
 std::string position=x.unitDeathSoundPosition?encode(x.unitDeathSoundPosition->x)+encode(x.unitDeathSoundPosition->y)+encode(x.unitDeathSoundPosition->level):"none";
 return encode(x.objectId)+encode(x.kind)+encode(x.tick)+encode(x.mission)+encode(x.reason)+encode(x.sourceObjectId)+encode(x.sourceOwner)+cause(x.deathWeaponCause)+encode(x.suppressDeathWeapon)+encode(x.destructionEffectsSpawned)+encode(x.unitDeathSoundRequested)+encode(x.unitDeathSoundHandled)+position+encode(x.suppressCrew)+encode(x.c4Explosion)+encode(x.warheadId)+encode(x.wasAliveBeforeDamage);
}
void verifyExitValues(){RuntimeObjectExitRequest zero;const auto before=exitValue(zero);
{auto v=zero;fields::mutateEncoderField(v.objectId);require(exitValue(v)!=before,"exit objectId omitted");}
{auto v=zero;fields::mutateEncoderField(v.kind);require(exitValue(v)!=before,"exit kind omitted");}
{auto v=zero;fields::mutateEncoderField(v.tick);require(exitValue(v)!=before,"exit tick omitted");}
{auto v=zero;fields::mutateEncoderField(v.mission);require(exitValue(v)!=before,"exit mission omitted");}
{auto v=zero;fields::mutateEncoderField(v.reason);require(exitValue(v)!=before,"exit reason omitted");}
{auto v=zero;fields::mutateEncoderField(v.sourceObjectId);require(exitValue(v)!=before,"exit sourceObjectId omitted");}
{auto v=zero;fields::mutateEncoderField(v.sourceOwner);require(exitValue(v)!=before,"exit sourceOwner omitted");}
{auto v=zero;fields::mutateEncoderField(v.suppressDeathWeapon);require(exitValue(v)!=before,"exit suppressDeathWeapon omitted");}
{auto v=zero;fields::mutateEncoderField(v.destructionEffectsSpawned);require(exitValue(v)!=before,"exit destructionEffectsSpawned omitted");}
{auto v=zero;fields::mutateEncoderField(v.unitDeathSoundRequested);require(exitValue(v)!=before,"exit unitDeathSoundRequested omitted");}
{auto v=zero;fields::mutateEncoderField(v.unitDeathSoundHandled);require(exitValue(v)!=before,"exit unitDeathSoundHandled omitted");}
{auto v=zero;fields::mutateEncoderField(v.suppressCrew);require(exitValue(v)!=before,"exit suppressCrew omitted");}
{auto v=zero;fields::mutateEncoderField(v.c4Explosion);require(exitValue(v)!=before,"exit c4Explosion omitted");}
{auto v=zero;fields::mutateEncoderField(v.warheadId);require(exitValue(v)!=before,"exit warheadId omitted");}
{auto v=zero;fields::mutateEncoderField(v.wasAliveBeforeDamage);require(exitValue(v)!=before,"exit wasAliveBeforeDamage omitted");}
{auto v=zero;v.unitDeathSoundPosition=RuntimeSpatialAudioPosition{.25,-.5,3.75};require(exitValue(v)!=before,"optional fullposition omitted");}
{auto v=zero;++v.deathWeaponCause.cascadeId;require(exitValue(v)!=before,"ownedcause cascadeId omitted");}
{auto v=zero;++v.deathWeaponCause.parentRequestId;require(exitValue(v)!=before,"ownedcause parentRequestId omitted");}
{auto v=zero;++v.deathWeaponCause.parentDepth;require(exitValue(v)!=before,"ownedcause parentDepth omitted");}
{auto v=zero;v.unitDeathSoundPosition=RuntimeSpatialAudioPosition{};const auto origin=exitValue(v);{auto w=v;w.unitDeathSoundPosition->x=1.25;require(exitValue(w)!=origin,"optional position x omitted");}{auto w=v;w.unitDeathSoundPosition->y=1.25;require(exitValue(w)!=origin,"optional position y omitted");}{auto w=v;w.unitDeathSoundPosition->level=1.25;require(exitValue(w)!=origin,"optional position level omitted");}}
}
struct Fault:std::exception{std::string text;explicit Fault(std::string x):text(std::move(x)){}const char* what()const noexcept override{return text.c_str();}};
enum class FixtureWeaponSlot { None, Primary, Secondary, Indexed };
struct FixtureWeaponProfile {
 bool hasWeaponSet=false,requiresDeployment=false,deployed=false,occupied=false,openTransport=false,hasTurret=false;
 const WeaponType* weapon=nullptr;const ProjectileType* projectile=nullptr;const WarheadType* warhead=nullptr;
 FixtureWeaponSlot slot=FixtureWeaponSlot::None;
 std::string weaponId,projectileId,warheadId,selectionReason;
 int indexedWeaponIndex=-1,occupantMuzzleIndex=-1;
};
struct FixtureRules {Model* model=nullptr;};
struct FixtureSuperWeapons {Model* model=nullptr;bool isObjectProtected(int,int)const;};
struct FixtureLaunchRules {double conditionRed=.25,conditionYellow=.5;};
struct FixtureCompleteState {
 std::map<int,ParasiteLinkState> links_;
 std::array<std::vector<std::string>,17> wholeState_;
 RuntimeParasiteState launchState_;FixtureLaunchRules launchRules_;
 const FixtureLaunchRules& rules()const noexcept{return launchRules_;}
 ParasiteOperationResult beginParasiteLaunch(const ParasiteLaunchRequest&,const SpecialEventSink&);
 ParasiteOperationResult resolveParasiteImpact(const ParasiteImpactRequest&,const SpecialEventSink&);
 const std::map<int,ParasitePendingLaunchState>& pendingParasiteLaunches()const;
 bool transient=false;
 FixtureCompleteState()=default;
 FixtureCompleteState(const FixtureCompleteState& x):links_(x.links_),wholeState_(x.wholeState_),launchState_(x.launchState_),launchRules_(x.launchRules_),transient(active!=nullptr){observe("candidate-copy");}
 FixtureCompleteState(FixtureCompleteState&&)=delete;
 FixtureCompleteState& operator=(const FixtureCompleteState&)=default;
 FixtureCompleteState& operator=(FixtureCompleteState&& x){observe("candidate-commit");links_=std::move(x.links_);wholeState_=std::move(x.wholeState_);launchState_=std::move(x.launchState_);launchRules_=x.launchRules_;observe("candidate-commit",{},true);return *this;}
 ~FixtureCompleteState()noexcept{if(active&&transient){try{observe("candidate-destroy");}catch(...){std::terminate();}}}
 const std::map<int,ParasiteLinkState>& parasiteLinks()const{observe("links");observe("links",encode(links_),true);return links_;}
 ParasiteOperationResult handleParasiteHostEvent(const ParasiteHostEventRequest&,const SpecialEventSink&);
};
struct FixtureState {
 Model* model=nullptr;
 int tick=40,nextProjectileActorId=100;
 struct Config{int fixedTickHz=30;} config;
 FixtureRules rules;FixtureSuperWeapons superWeapons;bool superWeaponsInitialized=false,mechanismsInitialized=false;
 FixtureCompleteState specialMechanisms;
 std::unordered_set<int> pendingObjectExitIds;
 std::unordered_set<int> parasiteReselectAfterLimboObjectIds;
 RuntimeParasiteStunState parasiteStunUntilTickByObject;
 std::vector<RuntimeProjectileEffect> projectiles;
 struct Economy{std::string owner="BLUE";} economy;
 std::string simulationStatus;
 bool navigationRefreshPending=false;
};
struct Model final:current::RuntimeParasiteAtomicPorts {
 FixtureState state;
 UnitType sourceType,targetType;WeaponType weapon,alternateWeapon;WarheadType warhead,alternateWarhead;ProjectileType projectile,alternateProjectile;FixtureWeaponProfile profile;
 bool nullSourceType=false,nullTargetType=false,nullProjectile=false,missileProfile=false,weaponTargets=true,protectedObject=false;
 std::string diagnosticLastPort;std::vector<std::string> observedPorts;
 int nestedAt=-1,nestedMode=0,nestedRuns=0;bool currentLaunchVersion=false;std::vector<std::string> nestedResults;
 int beginMode=0,spawnMode=0; int resolveMode=0,impacts=0,drains=0;bool nullWeapon=false,nullWarhead=false,canHit=true;double armorMultiplier=1;std::string armor="heavy";RuntimeProjectileEffect impactProjectile;std::vector<ParasiteImpactRequest> impactRequests;std::vector<std::string> replays;std::vector<ParasiteLaunchRequest> requests;std::vector<std::string> spawnCalls;
 std::vector<WorldObjectInstance> objects;
 std::set<int> selection,teams,behaviors;
 std::vector<RuntimeObjectExitRequest> exits;
 std::vector<std::string> trace;
 std::vector<SpecialMechanismAction> inputs;
 RuntimeDeathWeaponCause inherited{0x8000000000000001ULL,0xF000000000000002ULL,9};
 bool placement=true,exclusive=false,reselect=true,rejoin=false,allied=true,immune=false,handleAccepted=true,eraseLink=true;
 bool replaceHost=false,secondDeath=false;
 int authored=-1,queueMode=0,failAt=-1,mutationAt=-1,mutationMode=0;
 bool failAfter=false,inMutation=false;
 int calls=0,occupiedInvalidations=0,projectionInvalidations=0;
 int candidateDepth=0,maxCandidateDepth=0,candidateCopies=0,candidateCommits=0,candidateDestroys=0;
 Model(){state.model=this;state.rules.model=this;state.superWeapons.model=this;}
 void step(const std::string& port,const std::string& detail={},bool after=false);
 WorldObjectInstance* rawObject(int id,const std::string& port)const;
 std::optional<RuntimeCellPoint> rawPlacement(const WorldObjectInstance&,const SpecialCell&,const std::unordered_set<int>*,int)const;
 bool rawImmune(const WorldObjectInstance&,int,int)const;
 int rawDamage(const WorldObjectInstance&,int,const std::string&,double,double)const;
 void rawEffect(const std::string&,int);
 RuntimeObjectExitQueueResult rawExit(RuntimeObjectExitRequest);
 const WorldObjectInstance* objectIncludingInactive(int id)const override{return rawObject(id,"object");}
 WorldObjectInstance* mutableObjectIncludingInactive(int id)override{return rawObject(id,"mutable-object");}
 std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance& o,const SpecialCell& p,const std::unordered_set<int>* r,int host)const override{return rawPlacement(o,p,r,host);}
 int cellKey(int x,int y)const override{observe("cell-key",encode(x)+encode(y));int v=y*512+x;observe("cell-key",encode(v),true);return v;}
 std::string canonicalOwner(const std::string& s)const override{observe("owner",encode(s));std::string v=s;observe("owner",encode(v),true);return v;}
 bool exclusiveAuthority(const WorldObjectInstance& o,bool host)const override{observe("exclusive",encode(o)+encode(host));bool v=exclusive;observe("exclusive",encode(v),true);return v;}
 bool damageIsTypeImmune(const WorldObjectInstance& t,int s,int d)const override{return rawImmune(t,s,d);}
 int authoredDamage(const WorldObjectInstance& t,int d,const std::string& w,double distance,double modifier)const override{return rawDamage(t,d,w,distance,modifier);}
 RuntimeCellPoint objectCell(const WorldObjectInstance& o)const override{observe("cell",encode(o));RuntimeCellPoint p{o.x,o.y};observe("cell",encode(p),true);return p;}
 const int& liveTick()const noexcept override{return state.tick;}
 const std::unordered_set<int>& pendingExitIds()const noexcept override{return state.pendingObjectExitIds;}
 RuntimeParasiteStunState& stuns()noexcept override{return state.parasiteStunUntilTickByObject;}
 const std::vector<RuntimeProjectileEffect>& projectiles()const noexcept override{return state.projectiles;}
 const std::string& localOwner()const noexcept override{return state.economy.owner;}
 void setStatus(const char* v)override{state.simulationStatus=v;}
 bool selected(int id)const override{observe("selected",encode(id));bool v=selection.contains(id);observe("selected",encode(v),true);return v;}
 std::size_t eraseReselect(int id)override{return state.parasiteReselectAfterLimboObjectIds.erase(id);}
 void insertReselect(int id)override{state.parasiteReselectAfterLimboObjectIds.insert(id);}
 bool reselectsAfterLimbo(const WorldObjectInstance& o)const override{observe("reselect",encode(o));bool v=reselect;observe("reselect",encode(v),true);return v;}
 void removeSelection(int id)override{rawEffect("remove-selection",id);}
 bool rejoinsTeamAfterLimbo(const WorldObjectInstance& o)const override{observe("rejoin",encode(o));bool v=rejoin;observe("rejoin",encode(v),true);return v;}
 void detachFromTeam(int id)override{rawEffect("detach-team",id);}
 void eraseBehavior(int id)override{rawEffect("erase-behavior",id);}
 void clearRenderPosition(WorldObjectInstance& o)override{observe("clear-render",encode(o));o.hasRenderPosition=false;o.renderMoveActive=false;observe("clear-render",encode(o),true);}
 void cancelPendingOccupy(int id,const std::string& why)override{observe("cancel-occupy",encode(id)+encode(why));observe("cancel-occupy",{},true);}
 void activateObject(WorldObjectInstance& o)override{observe("activate",encode(o));o.alive=true;observe("activate",encode(o),true);}
 void setObjectCell(WorldObjectInstance& o,const RuntimeCellPoint& p,bool reset)override{observe("set-cell",encode(o)+encode(p)+encode(reset));o.x=p.x;o.y=p.y;o.cell=p.y*512+p.x;if(reset)o.hasRenderPosition=false;observe("set-cell",encode(o),true);}
 WorldObjectCellOffset subcellOffset(const WorldObjectInstance& o)const noexcept override{return worldObjectSubCellOffset(o);}
 void assignInfantrySubcell(WorldObjectInstance& o)override{observe("assign-subcell",encode(o));if(o.kind==WorldObjectKind::Infantry)o.subCell=2;observe("assign-subcell",encode(o),true);}
 double renderX(const WorldObjectInstance& o)const override{observe("render-x",encode(o));double v=o.hasRenderPosition?o.renderX:o.x;observe("render-x",encode(v),true);return v;}
 double renderY(const WorldObjectInstance& o)const override{observe("render-y",encode(o));double v=o.hasRenderPosition?o.renderY:o.y;observe("render-y",encode(v),true);return v;}
 double drawLevel(const WorldObjectInstance& o)const override{observe("draw-level",encode(o));double v=o.hasRenderPosition?o.renderLevel:o.level;observe("draw-level",encode(v),true);return v;}
 bool ownersAllied(const std::string& a,const std::string& b)const override{observe("allied",encode(a)+encode(b));bool v=allied;observe("allied",encode(v),true);return v;}
 void addSelection(int id)override{rawEffect("add-selection",id);}
 RuntimeObjectExitQueueResult queueExit(RuntimeObjectExitRequest&& x)override{return rawExit(std::move(x));}
 RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance& o)const override{observe("spatial",encode(o));RuntimeSpatialAudioPosition p{double(o.x)+.25,double(o.y)-.5,double(o.level)+.75};observe("spatial",encode(p.x)+encode(p.y)+encode(p.level),true);return p;}
 bool projectileIsParasiteLeap(const RuntimeProjectileEffect& p)const override{observe("leap",encode(p));bool v=p.projectileId=="parasite-leap";observe("leap",encode(v),true);return v;}
 void invalidateTickOccupancy()override{observe("invalidate-occupancy");++occupiedInvalidations;observe("invalidate-occupancy",{},true);}
 void invalidateObjectProjection()override{observe("invalidate-projection");++projectionInvalidations;observe("invalidate-projection",{},true);}
 void requestNavigationRefresh()override{state.navigationRefreshPending=true;}
 std::string impactSnapshot()const;
 std::string launchSnapshot()const;
 std::string snapshot()const;
};
void observe(const std::string& port,const std::string& detail,bool after){if(active)active->step(port,detail,after);}
void Model::step(const std::string& port,const std::string& detail,bool after){
 diagnosticLastPort=port;if(!after)observedPorts.push_back(port);
 if(!after){++calls;if(port=="candidate-copy"){++candidateCopies;++candidateDepth;maxCandidateDepth=std::max(maxCandidateDepth,candidateDepth);}if(port=="candidate-commit")++candidateCommits;if(port=="candidate-destroy"){--candidateDepth;++candidateDestroys;}}
 trace.push_back(encode(calls)+port+(after?".after":".before")+detail+"|complete-world="+encode(objects)+"|direct-reselect="+encode(std::set<int>(state.parasiteReselectAfterLimboObjectIds.begin(),state.parasiteReselectAfterLimboObjectIds.end()))+"|live-tick="+encode(state.tick));
 if(!after&&nestedAt==calls&&!inMutation){inMutation=true;try{nestedLaunch(*this,nestedMode);}catch(...){inMutation=false;throw;}inMutation=false;}
 if(!after&&mutationAt==calls&&!inMutation){
  inMutation=true;
  if(mutationMode==0)++state.tick;
  if(mutationMode==1&&!objects.empty())objects.front().health=0;
  if(mutationMode==2)state.economy.owner="changed-live-owner";
  if(mutationMode==3)++inherited.parentDepth;
  if(mutationMode==4&&!inputs.empty())inputs.front().damage=777;
  if(mutationMode==5)state.specialMechanisms.wholeState_[8].push_back("live-global-candidate-mutation");
  if(mutationMode==6)profile.warhead=&alternateWarhead;
  if(mutationMode==7)weapon.damage+=7;
  if(mutationMode==8&&objects.size()>1)objects.back().health=0;
  if(mutationMode==9)sourceType.organic=!sourceType.organic;
  if(mutationMode==10)targetType.strength+=37;
  if(mutationMode==11)warhead.id+="-live";
  if(mutationMode==12&&!objects.empty())objects.front().veterancy=2;
  if(mutationMode==13)state.specialMechanisms.launchRules_.conditionRed=.75;
  if(mutationMode==14)++state.nextProjectileActorId;
  if(mutationMode==15)state.simulationStatus+="late-status";
  if(mutationMode==16&&!objects.empty())state.pendingObjectExitIds.insert(objects.front().instanceId);
  if(mutationMode==17)state.specialMechanisms.launchState_.reset();
  if(mutationMode==18)impactProjectile.damage+=31;
  if(mutationMode==19)++resolveMode;
  inMutation=false;
 }
 if(port!="candidate-destroy"&&failAt==calls&&failAfter==after){hitPorts.insert(port);throw Fault(encode(calls)+port+(after?".after":".before"));}
}
WorldObjectInstance* Model::rawObject(int id,const std::string& port)const{
 observe(port,encode(id));auto& o=const_cast<Model*>(this)->objects;auto it=std::find_if(o.begin(),o.end(),[&](const auto& x){return x.instanceId==id;});auto* p=it==o.end()?nullptr:&*it;observe(port,p?encode(*p):"null",true);return p;
}
std::optional<RuntimeCellPoint> Model::rawPlacement(const WorldObjectInstance& o,const SpecialCell& p,const std::unordered_set<int>* reserved,int host)const{
 std::string keys=reserved?encode(std::set<int>(reserved->begin(),reserved->end())):"null";
 observe("placement",encode(o)+encode(p)+keys+encode(host));std::optional<RuntimeCellPoint> v;
 if(placement){RuntimeCellPoint q{p.x,p.y};while(reserved&&reserved->contains(q.y*512+q.x))++q.x;v=q;}observe("placement",encode(v),true);return v;
}
bool Model::rawImmune(const WorldObjectInstance& t,int source,int d)const{
 const auto* actor=rawObject(source,"immune-source");observe("immune",encode(t)+(actor?encode(*actor):"null")+encode(d));bool v=immune;observe("immune",encode(v),true);return v;
}
int Model::rawDamage(const WorldObjectInstance& t,int d,const std::string& w,double distance,double modifier)const{observe("authored",encode(t)+encode(d)+encode(w)+encode(distance)+encode(modifier));int v=authored<0?d:authored;observe("authored",encode(v),true);return v;}
void Model::rawEffect(const std::string& port,int id){observe(port,encode(id));if(port=="remove-selection")selection.erase(id);if(port=="add-selection")selection.insert(id);if(port=="detach-team")teams.erase(id);if(port=="erase-behavior")behaviors.erase(id);observe(port,{},true);}
RuntimeObjectExitQueueResult Model::rawExit(RuntimeObjectExitRequest x){observe("queue-exit",exitValue(x));auto v=static_cast<RuntimeObjectExitQueueResult>(queueMode);if(v==RuntimeObjectExitQueueResult::Queued){state.pendingObjectExitIds.insert(x.objectId);exits.push_back(std::move(x));}observe("queue-exit",encode(v),true);return v;}
ParasiteOperationResult FixtureCompleteState::handleParasiteHostEvent(const ParasiteHostEventRequest& q,const SpecialEventSink& emit){
 observe("handle",encode(q)+encode(bool(emit)));ParasiteOperationResult v;v.accepted=active->handleAccepted;v.reason="full candidate handler";if(v.accepted){
  auto it=links_.find(q.targetObjectId);if(it!=links_.end()){SpecialMechanismAction a=fields::action(Kind::ParasiteEject,it->second.sourceObjectId,q.targetObjectId);a.tick=q.currentTick;a.cell=q.ejectCell;a.stunTicks=q.stunTicks;a.replacesTerminatingHost=active->replaceHost;a.sourceWasAttachedToHost=true;v.actions.push_back(a);if(active->eraseLink)links_.erase(it);}
  if(q.targetObjectId==21&&active->secondDeath){auto a=fields::action(Kind::ParasiteDamage,11,22);a.damage=100;v.actions.push_back(a);}
 }observe("handle",encode(v),true);return v;
}
std::string Model::snapshot()const{
 std::map<int,int> stuns(state.parasiteStunUntilTickByObject.entries().begin(),state.parasiteStunUntilTickByObject.entries().end());std::string e;for(const auto& x:exits)e+=exitValue(x);
 return impactSnapshot()+launchSnapshot()+encode(objects)+encode(state.specialMechanisms.links_)+encode(state.specialMechanisms.wholeState_)+encode(state.pendingObjectExitIds)+encode(state.parasiteReselectAfterLimboObjectIds)+encode(stuns)+encode(state.projectiles)+encode(state.economy.owner)+encode(state.simulationStatus)+encode(state.navigationRefreshPending)+encode(state.tick)+encode(selection)+encode(teams)+encode(behaviors)+encode(occupiedInvalidations)+encode(projectionInvalidations)+cause(inherited)+encode(inputs)+encode(candidateDepth)+encode(maxCandidateDepth)+encode(candidateCopies)+encode(candidateCommits)+encode(candidateDestroys)+e;
}
// Both versions share only the synchronous fixture primitives. The frozen
// complete original Root algorithm and all original value probes remain intact.
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(const FixtureState& s,int id){return s.model->rawObject(id,"object");}
WorldObjectInstance* findMutableRuntimeObjectByIdIncludingInactive(FixtureState& s,int id){return s.model->rawObject(id,"mutable-object");}
const WorldObjectInstance* runtimeTypeImmuneSource(const FixtureState& s,int id){return s.model->rawObject(id,"immune-source");}
bool runtimeDamageIsTypeImmune(const FixtureState& s,const WorldObjectInstance& t,const WorldObjectInstance* actor,int damage){observe("immune",encode(t)+(actor?encode(*actor):"null")+encode(damage));bool v=s.model->immune;observe("immune",encode(v),true);return v;}
int runtimeAuthoredDamageAgainst(const FixtureState& s,const WorldObjectInstance& t,int d,const std::string& w,double distance,double modifier){return s.model->rawDamage(t,d,w,distance,modifier);}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& o){return active->objectCell(o);}
std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(const FixtureState& s,const WorldObjectInstance& o,const SpecialCell& p,const std::unordered_set<int>* reserved,int host){return s.model->rawPlacement(o,p,reserved,host);}
std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(const FixtureState& s,const std::vector<SpecialMechanismAction>& a){return current::prepareRuntimeParasiteActions(a,*s.model);}
bool validateRuntimeParasiteActionBatch(const FixtureState& s,const std::vector<SpecialMechanismAction>& a){return current::validateRuntimeParasiteActionBatch(a,s.pendingObjectExitIds,*s.model);}
bool selectionContains(const FixtureState& s,int id){return s.model->selected(id);}
bool runtimeObjectReselectsAfterParasiteLimbo(const FixtureState& s,const WorldObjectInstance& o){return s.model->reselectsAfterLimbo(o);}
void removeRuntimeSelection(FixtureState& s,int id){s.model->removeSelection(id);}
bool runtimeObjectRejoinsTeamAfterParasiteLimbo(const FixtureState& s,const WorldObjectInstance& o){return s.model->rejoinsTeamAfterLimbo(o);}
void detachRuntimeObjectFromTeamMembership(FixtureState& s,int id){s.model->detachFromTeam(id);}
void eraseRuntimeBehavior(FixtureState& s,int id){s.model->eraseBehavior(id);}
void clearRuntimeRenderPosition(WorldObjectInstance& o){active->clearRenderPosition(o);}
void cancelRuntimePendingOccupyForTarget(FixtureState& s,int id,const std::string& why){s.model->cancelPendingOccupy(id,why);}
void activateRuntimeWorldObject(FixtureState& s,WorldObjectInstance& o){s.model->activateObject(o);}
void setRuntimeObjectCell(FixtureState& s,WorldObjectInstance& o,const RuntimeCellPoint& p,bool reset){s.model->setObjectCell(o,p,reset);}
void assignRuntimeInfantrySubcellAtCurrentLocation(FixtureState& s,WorldObjectInstance& o){s.model->assignInfantrySubcell(o);}
double runtimeObjectRenderX(const WorldObjectInstance& o){return active->renderX(o);}
double runtimeObjectRenderY(const WorldObjectInstance& o){return active->renderY(o);}
double runtimeDrawLevelForObject(const WorldObjectInstance& o){return active->drawLevel(o);}
bool runtimeOwnersAreAllied(const FixtureState& s,const std::string& a,const std::string& b){return s.model->ownersAllied(a,b);}
void addSelectedObject(FixtureState& s,int id){s.model->addSelection(id);}
RuntimeObjectExitQueueResult queueRuntimeObjectExit(FixtureState& s,RuntimeObjectExitRequest x){return s.model->rawExit(std::move(x));}
RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& o){return active->spatialPosition(o);}
bool runtimeProjectileIsParasiteLeap(const FixtureState& s,const RuntimeProjectileEffect& p){return s.model->projectileIsParasiteLeap(p);}
void invalidateRuntimeTickOccupancyCache(FixtureState& s){s.model->invalidateTickOccupancy();}
void invalidateRuntimeObjectProjectionCache(FixtureState& s){s.model->invalidateObjectProjection();}
#include "RuntimeParasiteAtomicOriginalHostDeath.inl"
RuntimeParasiteActionApplyResult applyRuntimeParasiteActionsAtomically(FixtureState&, const std::vector<SpecialMechanismAction>&, const RuntimeDeathWeaponCause&, int, bool = false);
#include "RuntimeParasiteAtomicOriginalFunction.inl"
struct Owned final:current::RuntimeParasiteHostDeathCandidate{
 FixtureCompleteState value_;
 explicit Owned(const FixtureCompleteState& x):value_(x){}
 Owned(const Owned&)=delete;Owned(Owned&&)=delete;
 const std::map<int,ParasiteLinkState>& links()const override{return value_.parasiteLinks();}
 ParasiteOperationResult handle(const ParasiteHostEventRequest& q)override{return value_.handleParasiteHostEvent(q,SpecialEventSink{});}
 ParasiteOperationResult beginParasiteLaunch(const ParasiteLaunchRequest& q,const SpecialEventSink& emit){return value_.beginParasiteLaunch(q,emit);}
 ParasiteOperationResult resolveParasiteImpact(const ParasiteImpactRequest& q,const SpecialEventSink& emit){return value_.resolveParasiteImpact(q,emit);}
};
struct Factory{
 FixtureCompleteState& canonical;
 Owned copyCandidate()const{return Owned(canonical);}
 void commitCandidate(Owned& x){canonical=std::move(x.value_);}
};
