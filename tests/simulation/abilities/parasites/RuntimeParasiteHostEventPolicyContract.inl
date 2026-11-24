#include "RuntimeParasiteHostEventModelPrefix.inl"
const std::map<int,ParasitePendingLaunchState>& FixtureCompleteState::pendingParasiteLaunches()const{observe("pending");const auto& p=launchState_.pendingParasiteLaunches();observe("pending",encode(p),true);return p;}
ParasiteOperationResult FixtureCompleteState::resolveParasiteImpact(const ParasiteImpactRequest& q,const SpecialEventSink& emit){return launchState_.resolveParasiteImpact(q,emit);}
std::string Model::impactSnapshot()const{return encode(hostPreflight)+encode(hostTarget)+encode(hostStun)+encode(hostEvent)+encode(hostResultMode)+encode(eventRequests)+encode(resolveMode)+encode(impacts)+encode(drains)+encode(nullWeapon)+encode(nullWarhead)+encode(canHit)+encode(armorMultiplier)+encode(armor)+encode(impactProjectile)+encode(impactRequests)+encode(replays);}
std::optional<RuntimeCellPoint> runtimeParasitePlacementCell(const FixtureState&,const WorldObjectInstance&,const SpecialCell&,const std::unordered_set<int>* = nullptr,int = -1);
namespace frozen_original {
using RuntimeProductionHostState=FixtureState;
using RuntimeGameSessionState=FixtureState;
bool handleRuntimeParasiteHostEvent(
    RuntimeProductionHostState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto link = state.specialMechanisms.parasiteLinks().find(
        targetObjectId
    );
    if (link == state.specialMechanisms.parasiteLinks().end()) {
        return true;
    }
    const int sourceObjectId = link->second.sourceObjectId;
    const WorldObjectInstance* target =
        findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(
            state,
            sourceObjectId
        );
    const RuntimeCellPoint targetCell = target == nullptr
        ? RuntimeCellPoint{}
        : runtimeCellPointForObject(*target);
    const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {targetCell.x, targetCell.y}
        );
    ParasiteHostEventRequest request;
    request.currentTick = state.tick;
    request.targetObjectId = targetObjectId;
    request.event = event;
    request.ejectCellAvailable = ejectCell.has_value();
    if (ejectCell.has_value()) {
        request.ejectCell = {ejectCell->x, ejectCell->y};
    }
    request.stunTicks = std::max(0, stunTicks);

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult handled =
        specialCandidate.handleParasiteHostEvent(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, handled.actions);
    if (!handled.accepted
        || !validateRuntimeParasiteActionBatch(state, prepared)) {
        state.simulationStatus = handled.reason.empty()
            ? "parasite host event rejected atomically"
            : "parasite host event rejected: " + handled.reason;
        return false;
    }
    state.specialMechanisms = std::move(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            state,
            prepared,
            {},
            sourceObjectId
        );
    if (!applied.accepted) {
        state.simulationStatus =
            "parasite host event commit rejected unexpectedly";
        return false;
    }
    return true;
}

bool preflightRuntimeParasiteHostEvent(
    RuntimeGameSessionState& state,
    int targetObjectId,
    ParasiteHostEventKind event,
    int stunTicks) {
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto link = state.specialMechanisms.parasiteLinks().find(
        targetObjectId
    );
    if (link == state.specialMechanisms.parasiteLinks().end()) {
        return true;
    }
    const int sourceObjectId = link->second.sourceObjectId;
    const WorldObjectInstance* target =
        findRuntimeObjectByIdIncludingInactive(state, targetObjectId);
    const WorldObjectInstance* source =
        findRuntimeObjectByIdIncludingInactive(state, sourceObjectId);
    const RuntimeCellPoint targetCell = target == nullptr
        ? RuntimeCellPoint{}
        : runtimeCellPointForObject(*target);
    const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
        ? std::nullopt
        : runtimeParasitePlacementCell(
            state,
            *source,
            {targetCell.x, targetCell.y}
        );
    ParasiteHostEventRequest request;
    request.currentTick = state.tick;
    request.targetObjectId = targetObjectId;
    request.event = event;
    request.ejectCellAvailable = ejectCell.has_value();
    if (ejectCell.has_value()) {
        request.ejectCell = {ejectCell->x, ejectCell->y};
    }
    request.stunTicks = std::max(0, stunTicks);

    RuntimeSpecialMechanismState specialCandidate =
        state.specialMechanisms;
    const ParasiteOperationResult handled =
        specialCandidate.handleParasiteHostEvent(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, handled.actions);
    return handled.accepted
        && validateRuntimeParasiteActionBatch(state, prepared);
}

}
class HostEventQueries final:public current::RuntimeParasiteHostEventQueries {
 Model& m_;
public:
 explicit HostEventQueries(Model& m):m_(m){}
 const std::map<int,ParasiteLinkState>& links()const override{return m_.state.specialMechanisms.parasiteLinks();}
 const int& liveTick()const noexcept override{return m_.state.tick;}
 const std::unordered_set<int>& pendingExitIds()const noexcept override{return m_.state.pendingObjectExitIds;}
 const current::RuntimeParasiteActionQueries& actions()const noexcept override{return m_;}
 RuntimeCellPoint objectCell(const WorldObjectInstance& o)const override{return m_.objectCell(o);}
};
struct PreflightCopyOnlyFactory {
 FixtureCompleteState& canonical;
 Owned copyCandidate()const{return Owned(canonical);}
};
struct Outcome{bool result;std::string error,snapshot;std::vector<std::string> trace;int calls;};
Outcome executeEvent(Model& m,bool now,int targetOverride=INT_MIN){auto* previous=active;active=&m;m.currentLaunchVersion=now;bool result=false;std::string error;const int target=targetOverride==INT_MIN?m.hostTarget:targetOverride;
 try{if(now){ensureRuntimeSpecialMechanismsInitialized(m.state);HostEventQueries queries(m);
  if(m.hostPreflight){PreflightCopyOnlyFactory factory{m.state.specialMechanisms};result=current::preflightRuntimeParasiteHostEvent(queries,factory,target,m.hostEvent,m.hostStun);}
  else{Factory factory{m.state.specialMechanisms};result=current::handleRuntimeParasiteHostEvent(queries,m,factory,m.state.simulationStatus,target,m.hostEvent,m.hostStun);}
 }else if(m.hostPreflight)result=frozen_original::preflightRuntimeParasiteHostEvent(m.state,target,m.hostEvent,m.hostStun);
 else result=frozen_original::handleRuntimeParasiteHostEvent(m.state,target,m.hostEvent,m.hostStun);
 }catch(const std::exception& e){error=e.what();}
 active=previous;return {result,error,m.snapshot(),m.trace,m.calls};}
void nestedHostEvent(Model& m,int mode){++m.nestedRuns;
 if(mode==0){auto out=executeEvent(m,m.currentLaunchVersion,-222);m.nestedResults.push_back(encode(out.result)+encode(out.error));}
 if(mode==1){auto out=executeEvent(m,m.currentLaunchVersion);m.nestedResults.push_back(encode(out.result)+encode(out.error));}
 if(mode==2)throw Fault("nested-host-event-failure");
 if(mode==3){++m.state.tick;m.state.specialMechanisms.wholeState_[2].push_back("nested-canonical-state");}
}
std::unique_ptr<Model> eventSeed(int variant=0,bool preflight=false,int event=0){auto m=std::make_unique<Model>();m->hostPreflight=preflight;m->hostEvent=static_cast<ParasiteHostEventKind>(event);m->objects.reserve(16);
 for(int id:{11,21}){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.type=id==11?"DRONE":"HOST";o.owner=id==11?"BLUE":"RED";o.alive=id!=11;o.health=100;o.x=id;o.y=id+1;o.cell=o.y*512+o.x;o.mission=id==11?"ParasiteLimbo":"Guard";o.dockedToObjectId=-1;o.dockIndex=-1;o.garrisonHostObjectId=-1;o.hasRenderPosition=true;o.renderX=id+.25;o.renderY=id-.5;o.renderLevel=3.25;m->objects.push_back(o);}
 for(int i=0;i<17;++i)m->state.specialMechanisms.wholeState_[i]={std::string(50+i,char('a'+i)),std::string(37,char('A'+i))};
 m->state.simulationStatus=std::string(83,'s');m->state.parasiteReselectAfterLimboObjectIds.insert(11);m->state.parasiteStunUntilTickByObject.restoreValidated({{99,110}});
 ParasiteLinkState link;link.sourceObjectId=11;link.targetObjectId=21;link.projectileId=100;link.owner="BLUE";link.sourceTypeId="DRONE";link.targetTypeId="HOST";link.weaponId="PARA";link.warheadId="PARA_WH";link.damage=12;link.attachedTick=30;link.nextDamageTick=50;link.damageIntervalTicks=10;link.paralyzes=3;link.targetMaximumHealth=100;link.sourceExpectedInLimbo=true;m->state.specialMechanisms.links_.emplace(21,link);
 m->sourceType.strength=m->targetType.strength=100;m->targetType.parasiteable=true;m->weapon.id="PARA";m->warhead.id="PARA_WH";m->alternateWarhead.id="ALT";
 switch(variant){
 case 1:m->state.specialMechanisms.links_.clear();break;
 case 2:m->objects.back().instanceId=999;break;
 case 3:m->objects.front().instanceId=999;break;
 case 4:m->placement=false;break;
 case 5:m->handleAccepted=false;break;
 case 6:case 7:case 8:case 9:case 10:case 11:case 12:case 13:m->hostResultMode=variant-5;break;
 case 14:m->state.pendingObjectExitIds.insert(11);break;
 case 15:m->state.pendingObjectExitIds.insert(21);break;
 case 16:m->hostStun=-15;break;
 case 17:m->hostStun=0;break;
 case 18:m->hostStun=1234567;break;
 case 19:m->objects.front().kind=WorldObjectKind::Infantry;m->objects.front().subCell=4;break;
 case 20:m->replaceHost=true;break;
 case 21:m->queueMode=1;m->hostResultMode=7;break;
 case 22:m->queueMode=2;m->hostResultMode=7;break;
 case 23:m->state.specialMechanisms.links_.at(21).sourceObjectId=21;break;
 case 24:m->eraseLink=false;break;
 case 25:m->allied=false;break;
 case 26:m->reselect=false;break;
 case 27:m->rejoin=true;break;
 case 28:m->state.tick=INT_MAX-20;m->hostStun=3;break;
 case 29:m->objects.front().health=0;break;
 case 30:m->objects.front().alive=true;break;
 case 31:m->objects.back().health=0;break;
 case 32:m->hostTarget=-99;break;
 case 33:m->state.simulationStatus.clear();break;
 case 34:m->hostEvent=static_cast<ParasiteHostEventKind>(127);break;
 case 35:m->hostResultMode=3;m->handleAccepted=false;break;
 default:break;
 }
 m->selection.insert(11);m->teams.insert(11);m->behaviors.insert(11);return m;
}
int eventStates=0,eventFaults=0,liveMutations=0,nestedChecks=0,requestLeaves=0;
Outcome compare(Model& a,Model& b,const std::string& label){auto x=executeEvent(a,false),y=executeEvent(b,true);
 require(x.result==y.result,label+" result");require(x.error==y.error,label+" failure");require(x.snapshot==y.snapshot,label+" complete snapshot");require(x.trace==y.trace,label+" full ordered trace");require(x.calls==y.calls,label+" calls");++eventStates;eventFaults+=!x.error.empty();return x;}
void verifyEventEncoders(){ParasiteHostEventRequest q;const auto baseline=encode(q);
#define REQUEST_FIELD(field) {auto v=q;fields::mutateEncoderField(v.field);require(encode(v)!=baseline,"host event request " #field " omitted");++requestLeaves;}
 REQUEST_FIELD(currentTick) REQUEST_FIELD(targetObjectId) REQUEST_FIELD(event) REQUEST_FIELD(ejectCellAvailable) REQUEST_FIELD(ejectCell.x) REQUEST_FIELD(ejectCell.y) REQUEST_FIELD(stunTicks)
#undef REQUEST_FIELD
}
int runEvents(){try{fields::verifyWorld();fields::verifyActions();verifyExitValues();verifyEventEncoders();
 for(bool preflight:{false,true})for(int event=0;event<5;++event)for(int variant=0;variant<36;++variant){auto a=eventSeed(variant,preflight,event),b=eventSeed(variant,preflight,event);auto out=compare(*a,*b,"case"+std::to_string(preflight)+":"+std::to_string(event)+":"+std::to_string(variant));
  if(variant==0){require(out.result&&out.error.empty()&&a->candidateCopies==(preflight?1:2)&&a->candidateCommits==(preflight?0:1)&&a->candidateDestroys==(preflight?1:2)&&a->candidateDepth==0,"positive candidate copy/commit/scope witness absent");require(a->state.simulationStatus==std::string(83,'s'),"success rewrote status");if(preflight)require(!a->objects.front().alive&&a->state.specialMechanisms.links_.size()==1,"preflight committed world/candidate");}
  if(variant==1||variant==32)require(out.result&&a->candidateCopies==0&&a->eventRequests.empty()&&a->observedPorts==std::vector<std::string>{"ensure","links","links"},"missing link must only ensure and two getters");
  if(variant==6||variant==7){require(!out.result&&a->candidateCopies==1&&a->candidateCommits==0,"rejected event committed");require(std::count(a->observedPorts.begin(),a->observedPorts.end(),"placement")>=2,"rejection skipped unconditional named prepare");if(!preflight)require(a->state.simulationStatus==(variant==6?"parasite host event rejected atomically":"parasite host event rejected: "+std::string(93,'r')),"rejection status changed");}
 }
 for(bool preflight:{false,true})for(int variant:{0,1,2,3,4,5,6,7,8,9,10,11,12,14,15,19,23,28}){auto a=eventSeed(variant,preflight),b=eventSeed(variant,preflight);auto base=compare(*a,*b,"fault-base");for(int at=1;at<=base.calls;++at)for(bool after:{false,true}){auto x=eventSeed(variant,preflight),y=eventSeed(variant,preflight);x->failAt=y->failAt=at;x->failAfter=y->failAfter=after;compare(*x,*y,"fault"+std::to_string(preflight)+":"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(after));}}
 for(bool preflight:{false,true})for(int variant:{0,6,19}){auto a=eventSeed(variant,preflight),b=eventSeed(variant,preflight);auto base=compare(*a,*b,"live-base");for(int at=1;at<=base.calls;++at)for(int mode=0;mode<24;++mode){if(mode==21&&at<=3)continue;auto x=eventSeed(variant,preflight),y=eventSeed(variant,preflight);x->mutationAt=y->mutationAt=at;x->mutationMode=y->mutationMode=mode;compare(*x,*y,"live"+std::to_string(preflight)+":"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(mode));++liveMutations;}}
 for(bool preflight:{false,true}){auto a=eventSeed(0,preflight),b=eventSeed(0,preflight);auto base=compare(*a,*b,"nested-base");for(int at=1;at<=base.calls;++at)for(int mode=0;mode<4;++mode){if(mode==1&&at==3)continue;if(mode==2&&a->observedPorts.at(static_cast<std::size_t>(at-1))=="candidate-destroy")continue;auto x=eventSeed(0,preflight),y=eventSeed(0,preflight);x->nestedAt=y->nestedAt=at;x->nestedMode=y->nestedMode=mode;compare(*x,*y,"nested"+std::to_string(preflight)+":"+std::to_string(at)+":"+std::to_string(mode));++nestedChecks;}
  auto x=eventSeed(0,preflight),y=eventSeed(0,preflight);x->mutationAt=y->mutationAt=4;x->mutationMode=y->mutationMode=20;auto witness=compare(*x,*y,"link-source-copied-before-target-query");require(witness.error.empty()&&x->eventRequests.size()==1&&x->eventRequests.front().ejectCellAvailable,"source id was reread after target query");
 }
 {auto baseline=eventSeed();(void)executeEvent(*baseline,false);auto found=std::find(baseline->observedPorts.begin(),baseline->observedPorts.end(),"candidate-commit");require(found!=baseline->observedPorts.end(),"no commit port");const int at=1+static_cast<int>(std::distance(baseline->observedPorts.begin(),found));auto a=eventSeed(12),b=eventSeed(12);a->mutationAt=b->mutationAt=at;a->mutationMode=b->mutationMode=16;auto out=compare(*a,*b,"partial-commit-then-atomic-reject");require(!out.result&&out.error.empty()&&a->candidateCommits==1&&a->state.specialMechanisms.links_.empty()&&a->state.simulationStatus=="parasite host event commit rejected unexpectedly","unexpected atomic failure rolled back or changed status");}
 require(fields::worldProbes==87&&fields::actionProbes==17&&requestLeaves==7&&eventFaults>0&&liveMutations>0&&nestedChecks>0,"required probes absent");
 std::cout<<"parasite-host-event checks="<<checks<<" states="<<eventStates<<" world-probes=87 action-probes=17 request-leaves="<<requestLeaves<<" faults="<<eventFaults<<" fallible-ports="<<hitPorts.size()<<" live-mutations="<<liveMutations<<" nested="<<nestedChecks<<" frozen71+49line-order-wholecopy-unconditional-namedprepare-preflight-no-commit=exact mock-handler-only failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"parasite-host-event failure: "<<e.what()<<"\n";return 1;}}
}
