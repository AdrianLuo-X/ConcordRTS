#include "RuntimeParasiteImpactQueries.inl"
const std::map<int,ParasitePendingLaunchState>& FixtureCompleteState::pendingParasiteLaunches()const{observe("pending");const auto& p=launchState_.pendingParasiteLaunches();observe("pending",encode(p),true);return p;}
ParasiteOperationResult FixtureCompleteState::resolveParasiteImpact(const ParasiteImpactRequest& q,const SpecialEventSink& emit){
 observe("impact-resolve",encode(q)+encode(bool(emit)));active->impactRequests.push_back(q);auto r=launchState_.resolveParasiteImpact(q,emit);
 switch(active->resolveMode){case 1:r.accepted=false;r.reason.clear();break;case 2:r.accepted=false;r.reason=std::string(93,'r');break;case 3:r.actions.clear();break;case 4:if(!r.actions.empty())r.actions.push_back(r.actions.front());break;case 5:if(!r.actions.empty())r.actions.front().objectId=-999;break;case 6:if(!r.actions.empty())r.actions.front().targetObjectId=-999;break;default:break;}
 observe("impact-resolve",encode(r),true);return r;
}
std::string Model::impactSnapshot()const{return encode(resolveMode)+encode(impacts)+encode(drains)+encode(nullWeapon)+encode(nullWarhead)+encode(canHit)+encode(armorMultiplier)+encode(armor)+encode(impactProjectile)+encode(impactRequests)+encode(replays);}
const WorldObjectInstance* findObjectById(const FixtureState& s,int id){auto* o=s.model->rawObject(id,"target-object");return o&&o->alive?o:nullptr;}
const WarheadType* findRuntimeWarheadType(const FixtureRules& r,const std::string& id){observe("warhead",encode(id));auto* v=r.model->nullWarhead?nullptr:&r.model->warhead;observe("warhead",v?encode(v->id):"null",true);return v;}
const WeaponType* findRuntimeWeaponType(const FixtureRules& r,const std::string& id){observe("weapon",encode(id));auto* v=r.model->nullWeapon?nullptr:&r.model->weapon;observe("weapon",v?encode(v->id):"null",true);return v;}
bool runtimeProjectileCanHitTarget(const FixtureState& s,const RuntimeProjectileEffect& p,const WorldObjectInstance& t){observe("can-hit",encode(p)+encode(t));bool v=s.model->canHit;observe("can-hit",encode(v),true);return v;}
std::string runtimeTargetArmor(const FixtureState& s,const WorldObjectInstance& o){observe("target-armor",encode(o));auto v=s.model->armor;observe("target-armor",encode(v),true);return v;}
double runtimeWarheadArmorMultiplier(const WarheadType* w,const std::string& a){observe("armor-multiplier",(w?encode(w->id):"null")+encode(a));auto v=active->armorMultiplier;observe("armor-multiplier",encode(v),true);return v;}
RuntimeDeathWeaponCause runtimeDeathWeaponCauseForProjectile(const RuntimeProjectileEffect& p){observe("projectile-cause",encode(p));RuntimeDeathWeaponCause v{p.deathWeaponCascadeId,p.deathWeaponRequestId,p.deathWeaponCascadeDepth};observe("projectile-cause",cause(v),true);return v;}
void recordRuntimeProjectileReplayEvent(FixtureState& s,const RuntimeProjectileEffect& p,const std::string& event){observe("replay",encode(p)+encode(event));s.model->replays.push_back(encode(p)+encode(event));observe("replay",encode(s.model->replays),true);}
bool drainRuntimeObjectExitQueue(FixtureState& s){observe("drain");++s.model->drains;observe("drain",encode(s.model->drains),true);return true;}
class ImpactQueries final:public current::RuntimeParasiteImpactQueries{
 Model& m_;Admission admission_;
public:
 explicit ImpactQueries(Model& m):m_(m),admission_(m){}
 const std::map<int,ParasitePendingLaunchState>& pendingLaunches()const override{return m_.state.specialMechanisms.pendingParasiteLaunches();}
 const WorldObjectInstance* findObject(int id)const override{return findObjectById(m_.state,id);}
 const WarheadType* warheadType(const std::string& id)const override{return findRuntimeWarheadType(m_.state.rules,id);}
 const WeaponType* weaponType(const std::string& id)const override{return findRuntimeWeaponType(m_.state.rules,id);}
 const UnitType* unitType(const WorldObjectInstance& o)const override{return findRuntimeUnitType(m_.state.rules,o);}
 bool projectileCanHitTarget(const RuntimeProjectileEffect& p,const WorldObjectInstance& o)const override{return runtimeProjectileCanHitTarget(m_.state,p,o);}
 std::string targetArmor(const WorldObjectInstance& o)const override{return runtimeTargetArmor(m_.state,o);}
 double warheadArmorMultiplier(const WarheadType* w,const std::string& a)const override{return runtimeWarheadArmorMultiplier(w,a);}
 const current::RuntimeParasiteAdmissionQueries& admission()const noexcept override{return admission_;}
 bool superWeaponsInitialized()const noexcept override{return m_.state.superWeaponsInitialized;}
 bool isObjectProtected(int id,int tick)const override{return m_.state.superWeapons.isObjectProtected(id,tick);}
 RuntimeDeathWeaponCause deathWeaponCause(const RuntimeProjectileEffect& p)const override{return runtimeDeathWeaponCauseForProjectile(p);}
 void recordReplay(const RuntimeProjectileEffect& p,const std::string& event)override{recordRuntimeProjectileReplayEvent(m_.state,p,event);}
 int& impactsThisTick()noexcept override{return m_.impacts;}
 void drainObjectExitQueue()override{(void)drainRuntimeObjectExitQueue(m_.state);}
 std::string& status()noexcept override{return m_.state.simulationStatus;}
};

#include "RuntimeParasiteImpactOriginalFunction.inl"
struct Outcome{bool result;std::string error,snapshot;std::vector<std::string> trace;int calls;};
Outcome executeImpact(Model& m,bool now,const RuntimeProjectileEffect* alias=nullptr){auto* previous=active;active=&m;m.currentLaunchVersion=now;bool result=false;std::string error;
 try{const auto& p=alias?*alias:m.impactProjectile;if(now){ImpactQueries queries(m);Factory factory{m.state.specialMechanisms};result=current::applyRuntimeParasiteProjectileImpactExclusive(queries,m,factory,p);}else result=applyRuntimeParasiteProjectileImpactExclusive(m.state,p);}catch(const std::exception& e){error=e.what();}
 active=previous;return {result,error,m.snapshot(),m.trace,m.calls};}
void nestedLaunch(Model& m,int mode){++m.nestedRuns;
 if(mode==0){auto p=m.impactProjectile;p.actorId=-222;auto out=executeImpact(m,m.currentLaunchVersion,&p);m.nestedResults.push_back(encode(out.result)+encode(out.error));}
 if(mode==1){auto out=executeImpact(m,m.currentLaunchVersion);m.nestedResults.push_back(encode(out.result)+encode(out.error));}
 if(mode==2)throw Fault("nested-impact-failure");
 if(mode==3){++m.state.tick;m.state.specialMechanisms.wholeState_[2].push_back("nested-canonical-state");}
}
std::unique_ptr<Model> impactSeed(int variant=0){auto m=std::make_unique<Model>();m->objects.reserve(16);
 for(int id:{11,21}){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.type=id==11?"DRONE":"HOST";o.owner=id==11?"BLUE":"RED";o.alive=id!=11;o.health=100;o.x=id;o.y=id+1;o.cell=o.y*512+o.x;o.mission=id==11?"ParasiteLimbo":"Guard";o.dockedToObjectId=-1;o.dockIndex=-1;o.garrisonHostObjectId=-1;m->objects.push_back(o);}
 for(int i=0;i<17;++i)m->state.specialMechanisms.wholeState_[i]={std::string(50+i,char('a'+i)),std::string(37,char('A'+i))};
 m->sourceType.strength=m->targetType.strength=100;m->targetType.parasiteable=true;m->weapon.id="PARA";m->weapon.limboLaunch=true;m->weapon.projectile="P";m->warhead.id="PARA_WH";m->warhead.parasite=true;m->alternateWarhead.id="ALT";
 auto& p=m->impactProjectile;p.actorId=100;p.attackerObjectId=11;p.targetObjectId=21;p.owner="BLUE";p.weaponId="PARA";p.warheadId="PARA_WH";p.damage=37;p.impactAnimId=std::string(70,'a');p.particleId=std::string(75,'p');p.reportSoundId=std::string(80,'s');p.cellSpread=1.25;p.bright=true;p.fromCell={11,12};p.toCell={21,22};p.deathWeaponCascadeId=0xF000000000000001ULL;p.deathWeaponRequestId=0x8000000000000003ULL;p.deathWeaponCascadeDepth=7;
 ParasiteLaunchRequest q;q.projectileId=100;q.currentTick=30;q.sourceObjectId=11;q.targetObjectId=21;q.sourceOwner="BLUE";q.sourceTypeId="DRONE";q.targetTypeId="HOST";q.weaponId="PARA";q.warheadId="PARA_WH";q.targetKind=ParasiteTargetKind::Vehicle;q.targetCell={21,22};q.damage=12;q.damageIntervalTicks=10;q.targetMaximumHealth=100;q.targetParasiteable=true;q.paralyzes=true;
 if(variant==2||variant==30||variant==31){q.targetKind=ParasiteTargetKind::Infantry;m->objects.back().kind=WorldObjectKind::Infantry;}
 if(variant==31)m->placement=false;
 auto created=m->state.specialMechanisms.launchState_.beginParasiteLaunch(q,SpecialEventSink{});require(created.accepted,"real pending launch fixture rejected");
 switch(variant){
 case 1:p.actorId=-100;break;case 3:m->canHit=false;break;case 4:m->objects.front().health=0;break;case 5:m->objects.front().alive=true;break;case 6:m->objects.front().mission="Guard";break;case 7:m->objects.front().type="OTHER";break;
 case 8:p.attackerObjectId=99;break;case 9:p.targetObjectId=99;break;case 10:p.owner="OTHER";break;case 11:p.weaponId="OTHER";break;case 12:p.warheadId="OTHER";break;
 case 13:m->nullWeapon=true;break;case 14:m->nullWarhead=true;break;case 15:m->warhead.parasite=false;break;case 16:m->objects.back().health=0;break;case 17:m->objects.back().alive=false;break;case 18:m->objects.back().type="OTHER";break;case 19:m->objects.back().kind=WorldObjectKind::Structure;break;case 20:m->objects.back().authoredAircraft=true;break;case 21:m->objects.back().airborne=true;break;case 22:m->nullTargetType=true;break;case 23:m->targetType.parasiteable=false;break;case 24:m->armorMultiplier=0;break;case 25:m->armorMultiplier=-1;break;case 26:m->exclusive=true;break;case 27:m->state.superWeaponsInitialized=true;m->protectedObject=true;break;case 28:m->objects.front().instanceId=111;break;case 29:m->placement=false;m->canHit=false;break;
 case 32:m->resolveMode=1;break;case 33:m->resolveMode=2;break;case 34:m->resolveMode=3;break;case 35:m->resolveMode=4;break;case 36:m->resolveMode=5;break;case 37:m->resolveMode=6;break;case 38:m->state.pendingObjectExitIds.insert(21);break;case 39:m->state.pendingObjectExitIds.insert(11);break;case 40:m->state.superWeaponsInitialized=true;break;case 41:m->state.tick=INT_MAX-1;break;case 42:m->objects.back().owner="BLUE";break;case 43:m->objects.back().health=1;break;case 44:m->targetType.strength=0;break;case 45:m->targetType.strength=-100;break;case 46:m->queueMode=1;break;case 47:m->queueMode=2;break;default:break;
 }
 m->selection.insert(11);m->teams.insert(11);m->behaviors.insert(11);m->state.projectiles.push_back(p);return m;
}
int impactStates=0,impactFaults=0,liveMutations=0,nestedChecks=0,requestLeaves=0,projectileLeaves=0;std::vector<int> terminalOrdinals;
Outcome compare(Model& a,Model& b,const std::string& label,bool alias=false){auto x=executeImpact(a,false,alias?&a.state.projectiles.front():nullptr);auto y=executeImpact(b,true,alias?&b.state.projectiles.front():nullptr);
 require(x.result==y.result,label+" result");require(x.error==y.error,label+" failure");require(x.snapshot==y.snapshot,label+" complete snapshot");require(x.trace==y.trace,label+" full ordered trace");require(x.calls==y.calls,label+" calls");++impactStates;impactFaults+=!x.error.empty();return x;}
#include "RuntimeParasiteImpactProjectileFields.inl"
void verifyImpactEncoders(){ParasiteImpactRequest q;auto baseline=encode(q);
#define REQUEST_FIELD(field) {auto v=q;fields::mutateEncoderField(v.field);require(encode(v)!=baseline,"impact request " #field " omitted");++requestLeaves;}
 REQUEST_FIELD(projectileId) REQUEST_FIELD(currentTick) REQUEST_FIELD(impact) REQUEST_FIELD(targetObjectId) REQUEST_FIELD(targetTypeId) REQUEST_FIELD(targetKind) REQUEST_FIELD(targetPresent) REQUEST_FIELD(targetAlive) REQUEST_FIELD(targetParasiteable) REQUEST_FIELD(targetMaximumHealth) REQUEST_FIELD(impactCell.x) REQUEST_FIELD(impactCell.y) REQUEST_FIELD(returnCellAvailable) REQUEST_FIELD(returnCell.x) REQUEST_FIELD(returnCell.y)
#undef REQUEST_FIELD
}
int runImpact(){try{fields::verifyWorld();fields::verifyActions();verifyExitValues();verifyImpactEncoders();verifyProjectileEncoders();
 for(int variant=0;variant<48;++variant){auto a=impactSeed(variant),b=impactSeed(variant);auto out=compare(*a,*b,"case"+std::to_string(variant));
  if(variant==0){require(out.result&&out.error.empty()&&a->impacts==1&&a->drains==1&&a->replays.size()==1&&a->candidateCopies==2&&a->candidateCommits==1&&a->candidateDestroys==2&&a->candidateDepth==0,"positive wholecandidate/scope/commit/replay/counter/drain witness absent");auto clean=a->impactProjectile;clean.impactAnimId.clear();clean.particleId.clear();clean.damage=0;clean.cellSpread=0;clean.bright=false;clean.reportSoundId.clear();require(a->replays.front()==encode(clean)+encode(std::string("ProjectileImpact")),"complete replay copy/sixfield sanitize differed");}
  if(variant==1)require(!out.result&&a->candidateCopies==0&&a->impactRequests.empty()&&a->observedPorts==std::vector<std::string>{"pending","pending"},"missingpending performed extra work/init");
  if(variant==32||variant==33)require(out.result&&a->candidateCopies==1&&a->candidateCommits==0&&a->impacts==0&&a->drains==0,"rejected resolve mustconsume impact andpreserve canonical");
 }
 for(int variant:{0,1,2,3,9,28,29,30,31,32,33,34,35,36,37,38,39}){auto a=impactSeed(variant),b=impactSeed(variant);auto base=compare(*a,*b,"fault-base");for(int at=1;at<=base.calls;++at)for(bool after:{false,true}){auto x=impactSeed(variant),y=impactSeed(variant);x->failAt=y->failAt=at;x->failAfter=y->failAfter=after;compare(*x,*y,"fault"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(after));}}
 for(int variant:{0,2,32}){auto a=impactSeed(variant),b=impactSeed(variant);auto base=compare(*a,*b,"live-base");for(int at=1;at<=base.calls;++at)for(int mode=0;mode<20;++mode){if(mode==17&&at<=2)continue;auto x=impactSeed(variant),y=impactSeed(variant);x->mutationAt=y->mutationAt=at;x->mutationMode=y->mutationMode=mode;compare(*x,*y,"live"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(mode));++liveMutations;}}
 {auto a=impactSeed(),b=impactSeed();auto base=compare(*a,*b,"nested-base");for(int at=1;at<=base.calls;++at)for(int mode=0;mode<4;++mode){if(at<=2&&(mode==1||mode==3))continue;if(mode==2&&a->observedPorts.at(static_cast<std::size_t>(at-1))=="candidate-destroy"){terminalOrdinals.push_back(at);continue;}auto x=impactSeed(),y=impactSeed();x->nestedAt=y->nestedAt=at;x->nestedMode=y->nestedMode=mode;compare(*x,*y,"nested"+std::to_string(at)+":"+std::to_string(mode));++nestedChecks;}}
 for(int variant:{0,2,3,32,38}){auto a=impactSeed(variant),b=impactSeed(variant);compare(*a,*b,"projectilealias"+std::to_string(variant),true);}
 require(fields::worldProbes==87&&fields::actionProbes==17&&requestLeaves==15&&projectileLeaves==118&&impactFaults>0&&liveMutations>0&&!terminalOrdinals.empty(),"required field/fault/live/terminal witnesses absent");
 std::cout<<"parasite-impact checks="<<checks<<" states="<<impactStates<<" world-probes=87 action-probes=17 request-leaves="<<requestLeaves<<" projectile-top-fields=96 projectile-leaves="<<projectileLeaves<<" faults="<<impactFaults<<" fallible-ports="<<hitPorts.size()<<" live-mutations="<<liveMutations<<" nested="<<nestedChecks<<" isolated-terminal="<<terminalOrdinals.size()<<" frozen153line-order-wholecopy-namedprepare-failure-replay-counter-drain=exact failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"parasite-impact failure: "<<e.what()<<"\n";return 1;}}
}
