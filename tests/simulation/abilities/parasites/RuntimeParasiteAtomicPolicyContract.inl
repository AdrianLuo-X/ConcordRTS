#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include "RuntimeParasiteAtomicFields.inl"
#include <sstream>
#include <memory>
#include "world/model/GameWorldSubCellOffset.h"
namespace parasite_atomic_model {
constexpr auto worldObjectSubCellOffset = [](
    const WorldObjectInstance& object
) -> WorldObjectCellOffset {
    return object.kind == WorldObjectKind::Infantry
        ? worldInfantrySubCellOffset(object.subCell) : WorldObjectCellOffset{};
};
namespace current=concord::simulation::abilities;
namespace fields=parasite_atomic_fields;
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
struct FixtureCompleteState {
 std::map<int,ParasiteLinkState> links_;
 std::array<std::vector<std::string>,17> wholeState_;
 bool transient=false;
 FixtureCompleteState()=default;
 FixtureCompleteState(const FixtureCompleteState& x):links_(x.links_),wholeState_(x.wholeState_),transient(active!=nullptr){observe("candidate-copy");}
 FixtureCompleteState(FixtureCompleteState&&)=delete;
 FixtureCompleteState& operator=(const FixtureCompleteState&)=default;
 FixtureCompleteState& operator=(FixtureCompleteState&& x){observe("candidate-commit");links_=std::move(x.links_);wholeState_=std::move(x.wholeState_);observe("candidate-commit",{},true);return *this;}
 ~FixtureCompleteState()noexcept{if(active&&transient){try{observe("candidate-destroy");}catch(...){std::terminate();}}}
 const std::map<int,ParasiteLinkState>& parasiteLinks()const{observe("links");observe("links",encode(links_),true);return links_;}
 ParasiteOperationResult handleParasiteHostEvent(const ParasiteHostEventRequest&,const SpecialEventSink&);
};
struct FixtureState {
 Model* model=nullptr;
 int tick=40;
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
 Model(){state.model=this;}
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
 std::string snapshot()const;
};
void observe(const std::string& port,const std::string& detail,bool after){if(active)active->step(port,detail,after);}
void Model::step(const std::string& port,const std::string& detail,bool after){
 if(!after){++calls;if(port=="candidate-copy"){++candidateCopies;++candidateDepth;maxCandidateDepth=std::max(maxCandidateDepth,candidateDepth);}if(port=="candidate-commit")++candidateCommits;if(port=="candidate-destroy"){--candidateDepth;++candidateDestroys;}}
 trace.push_back(encode(calls)+port+(after?".after":".before")+detail+"|complete-world="+encode(objects)+"|direct-reselect="+encode(std::set<int>(state.parasiteReselectAfterLimboObjectIds.begin(),state.parasiteReselectAfterLimboObjectIds.end()))+"|live-tick="+encode(state.tick));
 if(!after&&mutationAt==calls&&!inMutation){
  inMutation=true;
  if(mutationMode==0)++state.tick;
  if(mutationMode==1&&!objects.empty())objects.front().health=0;
  if(mutationMode==2)state.economy.owner="changed-live-owner";
  if(mutationMode==3)++inherited.parentDepth;
  if(mutationMode==4&&!inputs.empty())inputs.front().damage=777;
  if(mutationMode==5)state.specialMechanisms.wholeState_[8].push_back("live-global-candidate-mutation");
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
 return encode(objects)+encode(state.specialMechanisms.links_)+encode(state.specialMechanisms.wholeState_)+encode(state.pendingObjectExitIds)+encode(state.parasiteReselectAfterLimboObjectIds)+encode(stuns)+encode(state.projectiles)+encode(state.economy.owner)+encode(state.simulationStatus)+encode(state.navigationRefreshPending)+encode(state.tick)+encode(selection)+encode(teams)+encode(behaviors)+encode(occupiedInvalidations)+encode(projectionInvalidations)+cause(inherited)+encode(inputs)+encode(candidateDepth)+encode(maxCandidateDepth)+encode(candidateCopies)+encode(candidateCommits)+encode(candidateDestroys)+e;
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
#include "RuntimeParasiteAtomicOriginalFunction.inl"
struct Owned final:current::RuntimeParasiteHostDeathCandidate{
 FixtureCompleteState value_;
 explicit Owned(const FixtureCompleteState& x):value_(x){}
 Owned(const Owned&)=delete;Owned(Owned&&)=delete;
 const std::map<int,ParasiteLinkState>& links()const override{return value_.parasiteLinks();}
 ParasiteOperationResult handle(const ParasiteHostEventRequest& q)override{return value_.handleParasiteHostEvent(q,SpecialEventSink{});}
};
struct Factory{
 FixtureCompleteState& canonical;
 Owned copyCandidate()const{return Owned(canonical);}
 void commitCandidate(Owned& x){canonical=std::move(x.value_);}
};
struct Outcome{std::string value,failure;std::vector<std::string> trace;int calls;bool accepted,changed;};
Outcome execute(Model& m,bool currentVersion){
 Model* previous=active;active=&m;RuntimeParasiteActionApplyResult result;std::string failure;
 try{if(currentVersion){Factory f{m.state.specialMechanisms};result=current::applyRuntimeParasiteActionsAtomically(m,f,m.inputs,m.inherited,71,true);}else result=applyRuntimeParasiteActionsAtomically(m.state,m.inputs,m.inherited,71,true);}catch(const std::exception& e){failure=e.what();}
 active=previous;return {m.snapshot(),failure,m.trace,m.calls,result.accepted,result.changed};
}
std::unique_ptr<Model> seed(Kind kind,int variant){
 auto m=std::make_unique<Model>();m->objects.reserve(16);
 for(int id:{11,12,13,21,22}){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.owner=id<20?"BLUE":"RED";o.type="full-unit";o.mapId=std::string(48,'m');o.tag=std::string(37,'t');o.alive=id==11||id>=20;o.health=50;o.x=id;o.y=id+1;o.cell=o.y*512+o.x;o.mission=o.alive?"ParasiteParalyzed":"ParasiteLimbo";o.subCell=-1;o.hasRenderPosition=true;o.renderX=id+.125;o.renderY=id+.75;o.level=3;o.renderLevel=4.375;o.onBridge=true;o.renderOnBridge=false;o.renderMoveActive=true;o.dockedToObjectId=81;o.dockIndex=9;m->objects.push_back(o);}
 for(int i=0;i<17;++i)m->state.specialMechanisms.wholeState_[i]={std::string(50+i,char('a'+i)),std::string(37,char('A'+i))};
 m->selection={11};m->teams={11};m->behaviors={11,21};m->state.parasiteReselectAfterLimboObjectIds={11,12,13};m->state.parasiteStunUntilTickByObject.restoreValidated({{11,88},{12,99}});
 auto a=fields::action(kind);if(kind==Kind::ParasiteAttachHost||kind==Kind::ParasiteReturnFromLimbo||kind==Kind::ParasiteEject){m->objects.front().alive=false;m->objects.front().mission="ParasiteLimbo";}m->inputs={a};
 RuntimeProjectileEffect p;p.actorId=31;p.attackerObjectId=11;p.projectileId="parasite-leap";p.currentX=9.25;p.currentY=-3.75;p.currentZ=8.125;p.owner=std::string(72,'p');m->state.projectiles.push_back(p);
 if(variant==1)m->placement=false;if(variant==2)m->exclusive=true;if(variant==3)m->objects.front().owner="OTHER";if(variant==4)m->inputs.front().tick=-1;if(variant==5)m->inputs.front().projectileId=0;if(variant==6)m->objects.front().health=0;if(variant==7)m->objects[3].health=0;if(variant==8)m->inputs.front().paralyzes=0;if(variant==9)m->inputs.front().stunTicks=0;if(variant==10)m->immune=true;if(variant==11)m->authored=0;if(variant==12)m->authored=5;if(variant==13)m->authored=100;if(variant==14)m->allied=false;if(variant==15)m->reselect=false;if(variant==16)m->rejoin=true;
 if(variant>=17&&variant<=20)m->queueMode=variant-17;
 if(variant==21)m->objects[3].mission="Guard";if(variant==22)m->objects[3].alive=false;if(variant==23)m->inputs.front().sourceWasAttachedToHost=true;if(variant==24)m->state.projectiles.front().projectileId="other";
 if(variant==25)m->inputs.front().reason="";if(variant==26)m->state.pendingObjectExitIds.insert(11);if(variant==27)m->inputs.front().objectId=999;if(variant==28)m->inputs.front().targetObjectId=999;
 if(variant>=29&&variant<=34){m->inputs.front().kind=Kind::ParasiteDamage;m->inputs.front().damage=100;ParasiteLinkState l;l.sourceObjectId=12;l.targetObjectId=21;l.projectileId=31;l.owner="BLUE";m->state.specialMechanisms.links_[21]=l;if(variant==30)m->handleAccepted=false;if(variant==31)m->placement=false;if(variant==32)m->eraseLink=false;if(variant==33){m->replaceHost=true;m->inputs.front().damage=100;}if(variant==34){m->secondDeath=true;l.sourceObjectId=13;l.targetObjectId=22;m->state.specialMechanisms.links_[22]=l;}}
 if(variant==35){m->inputs.front().replacesTerminatingHost=true;m->inputs.front().stunTicks=0;m->inputs.front().cell={21,22};}
 if(variant==36)m->inputs.clear();if(variant==37)m->inputs.push_back(m->inputs.front());if(variant==38){m->inputs.front().owner="";m->inputs.front().damage=0;}if(variant==39)m->inputs.front().kind=static_cast<Kind>(254);
 if(variant>=40&&variant<=43){m->inputs.front().replacesTerminatingHost=true;m->inputs.front().stunTicks=0;m->inputs.front().cell={21,22};m->objects.front().kind=WorldObjectKind::Infantry;m->objects.front().subCell=2;m->objects[3].kind=kind==Kind::ParasiteReturnFromLimbo?WorldObjectKind::Infantry:WorldObjectKind::Unit;m->objects[3].subCell=4;if(variant==41)m->objects[3].renderMoveActive=false;if(variant==42)m->objects[3].hasRenderPosition=false;if(variant==43)m->inputs.front().cell={3,7};}
 if(variant==44){m->inputs.assign(4096,fields::action(Kind::TimedBombDetonate));}
 return m;
}
void compare(Kind k,int variant,int failAt=-1,bool after=false,int mutationAt=-1,int mutationMode=0){
 auto a=seed(k,variant),b=seed(k,variant);a->failAt=b->failAt=failAt;a->failAfter=b->failAfter=after;a->mutationAt=b->mutationAt=mutationAt;a->mutationMode=b->mutationMode=mutationMode;
 auto old=execute(*a,false),now=execute(*b,true);if(variant==44)capacityActions=static_cast<int>(a->inputs.size());
 std::string context="kind="+std::to_string(int(k))+" variant="+std::to_string(variant)+" fault="+std::to_string(failAt)+" phase="+std::to_string(after)+" mutation="+std::to_string(mutationAt)+"/"+std::to_string(mutationMode);
 require(old.failure==now.failure,"failure "+context);require(old.accepted==now.accepted&&old.changed==now.changed,"result "+context);require(old.value==now.value,"complete authority/UI/candidate/projectile/exit value "+context);std::string traceDifference;for(std::size_t i=0;i<std::max(old.trace.size(),now.trace.size());++i){auto x=i<old.trace.size()?old.trace[i]:"<end>",y=i<now.trace.size()?now.trace[i]:"<end>";if(x!=y){traceDifference=" at="+std::to_string(i)+" old="+x+" new="+y;break;}}require(old.trace==now.trace,"exact primitive/copy-commit-destruction order "+context+traceDifference);require(old.calls==now.calls,"call count "+context);++states;if(failAt>=0)++faults;
}
int run(){try{
 fields::verifyWorld();fields::verifyActions();verifyExitValues();require(fields::worldProbes==87&&fields::actionProbes==17,"complete field encoders");
 const std::array<Kind,9> kinds={Kind::ParasiteEnterLimbo,Kind::ParasiteAttachHost,Kind::ParasiteReturnFromLimbo,Kind::ParasiteEject,Kind::ParasiteDamage,Kind::ParasiteKillTarget,Kind::ParasiteKillParasite,Kind::ParasiteReleaseTarget,Kind::TimedBombDetonate};
 for(auto kind:kinds)for(int variant=0;variant<45;++variant)compare(kind,variant);
 for(auto kind:kinds){auto m=seed(kind,0);auto result=execute(*m,false);if(result.failure.empty()&&result.accepted)++positiveKinds;for(int at=1;at<=result.calls;++at)for(bool after:{false,true})compare(kind,0,at,after);}
 for(int variant:{29,30,31,32,33,34,35}){auto m=seed(Kind::ParasiteDamage,variant);auto result=execute(*m,false);for(int at=1;at<=result.calls;++at)for(bool after:{false,true})compare(Kind::ParasiteDamage,variant,at,after);}
 for(auto kind:{Kind::ParasiteEject,Kind::ParasiteDamage,Kind::ParasiteKillParasite}){auto m=seed(kind,0);auto result=execute(*m,false);for(int at=1;at<=result.calls;++at)for(int mode=0;mode<6;++mode)compare(kind,0,-1,false,at,mode);}
 {auto m=seed(Kind::ParasiteDamage,34);auto v=execute(*m,true);require(v.failure.empty()&&v.accepted&&m->maxCandidateDepth>=3&&m->candidateDepth==0&&m->candidateCommits==2,"positive depth2 recursive owned candidates/complete moved-from destruction");}
 {auto m=seed(Kind::ParasiteEnterLimbo,0);auto v=execute(*m,true);require(v.failure.empty()&&m->candidateCopies==1&&m->candidateDestroys==1&&m->candidateCommits==0,"exactly one complete copy and original destructor scope");}
 static_assert(!std::is_copy_constructible_v<Owned>&&!std::is_move_constructible_v<Owned>);
 require(capacityActions==4096,"unbounded original batch capacity witness");
 require(positiveKinds==9,"all nine positive kinds");require(hitPorts.contains("candidate-copy")&&hitPorts.contains("queue-exit")&&hitPorts.contains("activate")&&hitPorts.contains("handle")&&hitPorts.contains("candidate-commit"),"positive copy/commit/recursive host/activation/owned exit failure coverage");
 std::cout<<"parasite-full-atomic checks="<<checks<<" states="<<states<<" world-probes=87 action-probes=17 kinds="<<positiveKinds<<" fault-cases="<<faults<<" fault-ports="<<hitPorts.size()<<" capacity-actions="<<capacityActions<<" whole-owned-candidate-original294line-exact-fields-effects-queries-copycommit-recursion-destruction=exact failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"parasite-full-atomic failure: "<<e.what()<<"\n";return 1;}}
}
