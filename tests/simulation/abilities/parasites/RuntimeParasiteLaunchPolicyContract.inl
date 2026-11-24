#include <cstdlib>
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include <climits>
#include "RuntimeParasiteLaunchFoundation.inl"

using runtime_save_text::trimAsciiLocal;
using current::runtimeParasiteWarheadIsExclusive;
using RuntimeSpecialMechanismState=FixtureCompleteState;
struct FixtureMissileProfile{};
std::string ruleValues(const Model& m){
 const auto& w=m.weapon;const auto& h=m.warhead;const auto& p=m.projectile;
 return encode(w.id)+encode(w.projectile)+encode(w.limboLaunch)+encode(w.burst)+encode(w.spawner)+encode(w.sonic)+encode(w.radBeam)+encode(w.electricBolt)+encode(w.magBeam)+encode(w.damage)+encode(w.rof)
 +encode(h.id)+encode(h.parasite)+encode(h.psychedelic)+encode(h.psychicDamage)+encode(h.ivanBomb)+encode(h.bombDisarm)+encode(h.airstrike)+encode(h.isLocomotor)+encode(h.mindControl)+encode(h.temporal)+encode(h.radiation)+encode(h.electricAssault)+encode(h.particle)+encode(h.particleSystem)+encode(h.paralyzes)+encode(h.culling)
 +encode(m.sourceType.organic)+encode(m.sourceType.strength)+encode(m.sourceType.parasiteable)+encode(m.targetType.organic)+encode(m.targetType.strength)+encode(m.targetType.parasiteable)
 +encode(p.airburst)+encode(p.shrapnelCount)+encode(p.airburstWeapon)+encode(p.shrapnelWeapon)+encode(m.alternateWeapon.id)+encode(m.alternateWeapon.projectile)+encode(m.alternateWeapon.limboLaunch)+encode(m.alternateWeapon.burst)+encode(m.alternateWeapon.spawner)+encode(m.alternateWeapon.sonic)+encode(m.alternateWeapon.radBeam)+encode(m.alternateWeapon.electricBolt)+encode(m.alternateWeapon.magBeam)+encode(m.alternateWeapon.damage)+encode(m.alternateWeapon.rof)+encode(m.alternateWarhead.id)+encode(m.alternateWarhead.parasite)+encode(m.alternateWarhead.psychedelic)+encode(m.alternateWarhead.psychicDamage)+encode(m.alternateWarhead.ivanBomb)+encode(m.alternateWarhead.bombDisarm)+encode(m.alternateWarhead.airstrike)+encode(m.alternateWarhead.isLocomotor)+encode(m.alternateWarhead.mindControl)+encode(m.alternateWarhead.temporal)+encode(m.alternateWarhead.radiation)+encode(m.alternateWarhead.electricAssault)+encode(m.alternateWarhead.particle)+encode(m.alternateWarhead.particleSystem)+encode(m.alternateWarhead.paralyzes)+encode(m.alternateWarhead.culling)+encode(m.alternateProjectile.airburst)+encode(m.alternateProjectile.shrapnelCount)+encode(m.alternateProjectile.airburstWeapon)+encode(m.alternateProjectile.shrapnelWeapon);
}
template<class T>int pointerRole(const T* value,const T* first,const T* second){if(value==nullptr)return 0;if(value==first)return 1;if(value==second)return 2;throw std::runtime_error("unencoded profile pointer alias");}
std::string profileValue(const Model& m,const FixtureWeaponProfile& p){
 return encode(p.hasWeaponSet)+encode(p.requiresDeployment)+encode(p.deployed)+encode(p.occupied)+encode(p.openTransport)+encode(p.hasTurret)
 +encode(pointerRole(p.weapon,&m.weapon,&m.alternateWeapon))+encode(pointerRole(p.projectile,&m.projectile,&m.alternateProjectile))+encode(pointerRole(p.warhead,&m.warhead,&m.alternateWarhead))+encode(p.slot)
 +encode(p.weaponId)+encode(p.projectileId)+encode(p.warheadId)+encode(p.selectionReason)+encode(p.indexedWeaponIndex)+encode(p.occupantMuzzleIndex)+ruleValues(m);
}
std::string Model::launchSnapshot()const{
 const auto& special=state.specialMechanisms;
 return encode(state.nextProjectileActorId)+encode(state.config.fixedTickHz)+encode(state.superWeaponsInitialized)+encode(state.mechanismsInitialized)
 +encode(special.launchRules_.conditionRed)+encode(special.launchRules_.conditionYellow)
 +encode(special.launchState_.pendingParasiteLaunches())+encode(special.launchState_.parasiteLinks())+encode(special.launchState_.resolvedParasiteLaunches())
 +ruleValues(*this)+profileValue(*this,profile)+encode(requests)+encode(spawnCalls)+encode(beginMode)+encode(spawnMode)
 +encode(nestedRuns)+encode(nestedResults)+encode(nullSourceType)+encode(nullTargetType)+encode(nullProjectile)+encode(missileProfile)+encode(weaponTargets)+encode(protectedObject);
}
ParasiteOperationResult FixtureCompleteState::beginParasiteLaunch(const ParasiteLaunchRequest& q,const SpecialEventSink& emit){
 observe("launch-begin",encode(q)+encode(bool(emit)));active->requests.push_back(q);
 auto result=launchState_.beginParasiteLaunch(q,emit);
 if(active->beginMode==1){result.accepted=false;result.reason.clear();}
 if(active->beginMode==2){result.accepted=false;result.reason=std::string(93,'r');}
 if(active->beginMode==3)result.idempotent=true;
 if(active->beginMode==4)result.actions.clear();
 if(active->beginMode==5&&!result.actions.empty())result.actions.push_back(result.actions.front());
 if(active->beginMode==6&&!result.actions.empty())result.actions.front().objectId=-999;
 observe("launch-begin",encode(result),true);return result;
}
void ensureRuntimeSpecialMechanismsInitialized(FixtureState& s){observe("ensure",encode(s.mechanismsInitialized));s.mechanismsInitialized=true;observe("ensure",encode(s.mechanismsInitialized),true);}
const UnitType* findRuntimeUnitType(const FixtureRules& rules,const WorldObjectInstance& o){
 auto& m=*rules.model;observe("unit-type",encode(o));const bool source=&o==&m.objects.front();
 const UnitType* result=(source?m.nullSourceType:m.nullTargetType)?nullptr:source?&m.sourceType:&m.targetType;
 observe("unit-type",result?encode(result->organic)+encode(result->strength)+encode(result->parasiteable):"null",true);return result;
}
const ProjectileType* findRuntimeProjectileType(const FixtureRules& rules,const std::string& id){
 auto& m=*rules.model;observe("projectile-type",encode(id));auto* result=m.nullProjectile?nullptr:&m.projectile;
 observe("projectile-type",result?encode(result->airburst)+encode(result->shrapnelCount)+encode(result->airburstWeapon)+encode(result->shrapnelWeapon):"null",true);return result;
}
const FixtureMissileProfile* runtimeSpawnedMissileProfileForWeapon(const FixtureState& s,const WorldObjectInstance& source,const WeaponType& w){
 observe("missile-profile",encode(source)+encode(w.id));static constexpr FixtureMissileProfile token{};const auto* result=s.model->missileProfile?&token:nullptr;observe("missile-profile",encode(result!=nullptr),true);return result;
}
bool runtimeWeaponCanTargetObject(const FixtureState& s,const WeaponType& w,const WorldObjectInstance& target){observe("weapon-target",encode(w.id)+encode(target));bool result=s.model->weaponTargets;observe("weapon-target",encode(result),true);return result;}
bool FixtureSuperWeapons::isObjectProtected(int id,int tick)const{observe("protected",encode(id)+encode(tick));bool result=model->protectedObject;observe("protected",encode(result),true);return result;}
int nextRuntimeProjectileActorId(FixtureState& s){observe("allocate-actor",encode(s.nextProjectileActorId));const int result=s.nextProjectileActorId;s.nextProjectileActorId=result==INT_MAX?INT_MIN:result+1;observe("allocate-actor",encode(result)+encode(s.nextProjectileActorId),true);return result;}
class Admission final:public current::RuntimeParasiteAdmissionQueries{
 Model& m_;std::vector<RuntimeRepairDepotRelation> repairs_;std::vector<RuntimeFactoryDeliveryLease> leases_;
public:
 explicit Admission(Model& m):m_(m){}
 const std::vector<WorldObjectInstance>& objects()const noexcept override{return m_.objects;}
 const RuntimeParasiteStunState& stunState()const noexcept override{return m_.state.parasiteStunUntilTickByObject;}
 const int& tick()const noexcept override{return m_.state.tick;}
 bool hasPendingEngineerCapture(int)const noexcept override{return m_.exclusive;}
 bool hasPendingOccupyTarget(int)const noexcept override{return false;}
 bool hasPendingTransportTarget(int)const noexcept override{return false;}
 bool hasPendingTimedBombIntent(int)const noexcept override{return false;}
 bool repairDepotsInitialized()const noexcept override{return false;}
 const std::vector<RuntimeRepairDepotRelation>& repairRelations()const noexcept override{return repairs_;}
 const std::vector<RuntimeFactoryDeliveryLease>& factoryLeases()const noexcept override{return leases_;}
 bool specialMechanismsInitialized()const noexcept override{return false;}
 bool isCarrierChild(int)const noexcept override{return false;}
 bool isCarrierParent(int)const noexcept override{return false;}
};
bool runtimeParasiteObjectHasExclusiveAuthority(const FixtureState& s,const WorldObjectInstance& o,bool host=false){Admission queries(*s.model);return current::runtimeParasiteObjectHasExclusiveAuthority(queries,o,host);}
void spawnRuntimeProjectile(FixtureState& s,const WorldObjectInstance& source,const WorldObjectInstance& target,const WeaponType& w,const FixtureWeaponProfile& profile,int damage,int p1,int p2,bool p3,int p4,double p5,int p6,int p7,int p8,bool p9,const FixtureMissileProfile* p10,int actorId){
 auto& m=*s.model;require(&w==&m.weapon&&&profile==&m.profile,"canonical spawn lost original complete weapon/profile identity");const auto signature=encode(source)+encode(target)+profileValue(m,profile)+encode(w.id)+encode(damage)+encode(p1)+encode(p2)+encode(p3)+encode(p4)+encode(p5)+encode(p6)+encode(p7)+encode(p8)+encode(p9)+encode(p10!=nullptr)+encode(actorId);
 observe("spawn",signature);m.spawnCalls.push_back(signature);
 RuntimeProjectileEffect effect;effect.actorId=actorId;effect.attackerObjectId=source.instanceId;effect.targetObjectId=target.instanceId;effect.owner=source.owner;effect.weaponId=w.id;effect.warheadId=profile.warhead?profile.warhead->id:"";effect.damage=damage;effect.fromCell={source.x,source.y};effect.toCell={target.x,target.y};s.projectiles.push_back(effect);
 if(m.spawnMode==1)s.pendingObjectExitIds.insert(source.instanceId);
 if(m.spawnMode==2&&!m.objects.empty())m.objects.front().health=0;
 if(m.spawnMode==3)s.tick+=3;
 observe("spawn",encode(s.projectiles),true);
}
class LaunchQueries final:public current::RuntimeParasiteLaunchQueries{
 Model& m_;Admission admission_;
public:
 explicit LaunchQueries(Model& m):m_(m),admission_(m){}
 const UnitType* unitType(const WorldObjectInstance& o)const override{return findRuntimeUnitType(m_.state.rules,o);}
 const ProjectileType* projectileType(const std::string& id)const override{return findRuntimeProjectileType(m_.state.rules,id);}
 bool hasSpawnedMissileProfile(const WorldObjectInstance& o,const WeaponType& w)const override{return runtimeSpawnedMissileProfileForWeapon(m_.state,o,w)!=nullptr;}
 bool weaponCanTargetObject(const WeaponType& w,const WorldObjectInstance& o)const override{return runtimeWeaponCanTargetObject(m_.state,w,o);}
 bool superWeaponsInitialized()const noexcept override{return m_.state.superWeaponsInitialized;}
 bool isObjectProtected(int id,int tick)const override{return m_.state.superWeapons.isObjectProtected(id,tick);}
 const current::RuntimeParasiteAdmissionQueries& admission()const noexcept override{return admission_;}
 int& nextActorId()noexcept override{return m_.state.nextProjectileActorId;}
 int allocateActorId()override{return nextRuntimeProjectileActorId(m_.state);}
 const int& fixedTickHz()const noexcept override{return m_.state.config.fixedTickHz;}
 const double& conditionRed()const noexcept override{return m_.state.specialMechanisms.rules().conditionRed;}
 const double& conditionYellow()const noexcept override{return m_.state.specialMechanisms.rules().conditionYellow;}
 std::string& status()noexcept override{return m_.state.simulationStatus;}
 void spawnProjectile(const WorldObjectInstance& source,const WorldObjectInstance& target,const WeaponType& w,int damage,int actorId)override{spawnRuntimeProjectile(m_.state,source,target,w,m_.profile,damage,-1,0,false,-1,-1.0,0,0,0,true,nullptr,actorId);}
};
#include "RuntimeParasiteLaunchOriginalTiming.inl"
#include "RuntimeParasiteLaunchOriginalFunction.inl"
struct LaunchOutcome{bool result;std::string failure,snapshot;std::vector<std::string> trace;int calls;};
LaunchOutcome executeLaunch(Model& m,bool now){
 auto* previous=active;active=&m;m.currentLaunchVersion=now;bool result=false;std::string error;
 try{if(now){ensureRuntimeSpecialMechanismsInitialized(m.state);const WarheadType* warhead=m.profile.warhead;LaunchQueries queries(m);Factory factory{m.state.specialMechanisms};result=current::launchRuntimeParasiteProjectile(queries,m,factory,m.objects.front(),m.objects.back(),m.weapon,warhead);}else result=launchRuntimeParasiteProjectile(m.state,m.objects.front(),m.objects.back(),m.weapon,m.profile);}catch(const std::exception& e){error=e.what();}
 active=previous;return {result,error,m.snapshot(),m.trace,m.calls};
}
std::unique_ptr<Model> launchSeed(){
 auto m=std::make_unique<Model>();m->objects.reserve(16);
 for(int id:{11,21}){WorldObjectInstance o;o.instanceId=id;o.kind=WorldObjectKind::Unit;o.type=id==11?"DRONE":"HOST";o.owner=id==11?"BLUE":"RED";o.alive=true;o.health=100;o.x=id;o.y=id+1;o.cell=o.y*512+o.x;o.mission="Guard";o.dockedToObjectId=-1;o.dockIndex=-1;o.garrisonHostObjectId=-1;m->objects.push_back(o);}
 for(int i=0;i<17;++i)m->state.specialMechanisms.wholeState_[i]={std::string(50+i,char('a'+i)),std::string(37,char('A'+i))};
 m->alternateWeapon.id="ALT";m->alternateWarhead.id="ALT_WH";m->alternateWarhead.parasite=true;m->sourceType.strength=100;m->targetType.strength=100;m->targetType.parasiteable=true;m->weapon.id="PARA";m->weapon.projectile="P";m->weapon.limboLaunch=true;m->weapon.damage=12;m->weapon.rof=10;m->warhead.id="PARA_WH";m->warhead.parasite=true;m->profile.hasWeaponSet=true;m->profile.weapon=&m->weapon;m->profile.projectile=&m->projectile;m->profile.warhead=&m->warhead;m->profile.weaponId=m->weapon.id;m->profile.projectileId=m->weapon.projectile;m->profile.warheadId=m->warhead.id;m->profile.selectionReason=std::string(99,'s');m->selection.insert(11);m->teams.insert(11);m->behaviors.insert(11);return m;
}
int requestLeafProbes=0,profileLeafProbes=0,ruleLeafProbes=0,pointerAliasProbes=0;
void verifyLaunchEncoders(){
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.projectileId);require(encode(q)!=before,"request projectileId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.currentTick);require(encode(q)!=before,"request currentTick omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.sourceObjectId);require(encode(q)!=before,"request sourceObjectId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetObjectId);require(encode(q)!=before,"request targetObjectId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.sourceOwner);require(encode(q)!=before,"request sourceOwner omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.sourceTypeId);require(encode(q)!=before,"request sourceTypeId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetTypeId);require(encode(q)!=before,"request targetTypeId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.weaponId);require(encode(q)!=before,"request weaponId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.warheadId);require(encode(q)!=before,"request warheadId omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetKind);require(encode(q)!=before,"request targetKind omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetCell.x);require(encode(q)!=before,"request targetCell.x omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetCell.y);require(encode(q)!=before,"request targetCell.y omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.damage);require(encode(q)!=before,"request damage omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.damageIntervalTicks);require(encode(q)!=before,"request damageIntervalTicks omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.paralyzes);require(encode(q)!=before,"request paralyzes omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetMaximumHealth);require(encode(q)!=before,"request targetMaximumHealth omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.targetParasiteable);require(encode(q)!=before,"request targetParasiteable omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.sourceOrganic);require(encode(q)!=before,"request sourceOrganic omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.sourceElite);require(encode(q)!=before,"request sourceElite omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.culling);require(encode(q)!=before,"request culling omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.normalCullingThresholdPermyriad);require(encode(q)!=before,"request normalCullingThresholdPermyriad omitted");++requestLeafProbes;}
{ParasiteLaunchRequest q;auto before=encode(q);fields::mutateEncoderField(q.eliteCullingThresholdPermyriad);require(encode(q)!=before,"request eliteCullingThresholdPermyriad omitted");++requestLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.hasWeaponSet);require(profileValue(*v,v->profile)!=before,"profile hasWeaponSet omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.requiresDeployment);require(profileValue(*v,v->profile)!=before,"profile requiresDeployment omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.deployed);require(profileValue(*v,v->profile)!=before,"profile deployed omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.occupied);require(profileValue(*v,v->profile)!=before,"profile occupied omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.openTransport);require(profileValue(*v,v->profile)!=before,"profile openTransport omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.hasTurret);require(profileValue(*v,v->profile)!=before,"profile hasTurret omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.slot);require(profileValue(*v,v->profile)!=before,"profile slot omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.weaponId);require(profileValue(*v,v->profile)!=before,"profile weaponId omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.projectileId);require(profileValue(*v,v->profile)!=before,"profile projectileId omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.warheadId);require(profileValue(*v,v->profile)!=before,"profile warheadId omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.selectionReason);require(profileValue(*v,v->profile)!=before,"profile selectionReason omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.indexedWeaponIndex);require(profileValue(*v,v->profile)!=before,"profile indexedWeaponIndex omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);fields::mutateEncoderField(v->profile.occupantMuzzleIndex);require(profileValue(*v,v->profile)!=before,"profile occupantMuzzleIndex omitted");++profileLeafProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);v->profile.weapon=nullptr;require(profileValue(*v,v->profile)!=before,"profile weapon null omitted");++profileLeafProbes;v->profile.weapon=&v->alternateWeapon;require(profileValue(*v,v->profile)!=before,"profile weapon alias omitted");++pointerAliasProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);v->profile.projectile=nullptr;require(profileValue(*v,v->profile)!=before,"profile projectile null omitted");++profileLeafProbes;v->profile.projectile=&v->alternateProjectile;require(profileValue(*v,v->profile)!=before,"profile projectile alias omitted");++pointerAliasProbes;}
{auto v=launchSeed();auto before=profileValue(*v,v->profile);v->profile.warhead=nullptr;require(profileValue(*v,v->profile)!=before,"profile warhead null omitted");++profileLeafProbes;v->profile.warhead=&v->alternateWarhead;require(profileValue(*v,v->profile)!=before,"profile warhead alias omitted");++pointerAliasProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.id);require(ruleValues(m)!=before,"rule m.weapon.id omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.projectile);require(ruleValues(m)!=before,"rule m.weapon.projectile omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.limboLaunch);require(ruleValues(m)!=before,"rule m.weapon.limboLaunch omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.burst);require(ruleValues(m)!=before,"rule m.weapon.burst omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.spawner);require(ruleValues(m)!=before,"rule m.weapon.spawner omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.sonic);require(ruleValues(m)!=before,"rule m.weapon.sonic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.radBeam);require(ruleValues(m)!=before,"rule m.weapon.radBeam omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.electricBolt);require(ruleValues(m)!=before,"rule m.weapon.electricBolt omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.magBeam);require(ruleValues(m)!=before,"rule m.weapon.magBeam omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.damage);require(ruleValues(m)!=before,"rule m.weapon.damage omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.weapon.rof);require(ruleValues(m)!=before,"rule m.weapon.rof omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.id);require(ruleValues(m)!=before,"rule m.warhead.id omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.parasite);require(ruleValues(m)!=before,"rule m.warhead.parasite omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.psychedelic);require(ruleValues(m)!=before,"rule m.warhead.psychedelic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.psychicDamage);require(ruleValues(m)!=before,"rule m.warhead.psychicDamage omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.ivanBomb);require(ruleValues(m)!=before,"rule m.warhead.ivanBomb omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.bombDisarm);require(ruleValues(m)!=before,"rule m.warhead.bombDisarm omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.airstrike);require(ruleValues(m)!=before,"rule m.warhead.airstrike omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.isLocomotor);require(ruleValues(m)!=before,"rule m.warhead.isLocomotor omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.mindControl);require(ruleValues(m)!=before,"rule m.warhead.mindControl omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.temporal);require(ruleValues(m)!=before,"rule m.warhead.temporal omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.radiation);require(ruleValues(m)!=before,"rule m.warhead.radiation omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.electricAssault);require(ruleValues(m)!=before,"rule m.warhead.electricAssault omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.particle);require(ruleValues(m)!=before,"rule m.warhead.particle omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.particleSystem);require(ruleValues(m)!=before,"rule m.warhead.particleSystem omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.paralyzes);require(ruleValues(m)!=before,"rule m.warhead.paralyzes omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.warhead.culling);require(ruleValues(m)!=before,"rule m.warhead.culling omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.sourceType.organic);require(ruleValues(m)!=before,"rule m.sourceType.organic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.sourceType.strength);require(ruleValues(m)!=before,"rule m.sourceType.strength omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.sourceType.parasiteable);require(ruleValues(m)!=before,"rule m.sourceType.parasiteable omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.targetType.organic);require(ruleValues(m)!=before,"rule m.targetType.organic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.targetType.strength);require(ruleValues(m)!=before,"rule m.targetType.strength omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.targetType.parasiteable);require(ruleValues(m)!=before,"rule m.targetType.parasiteable omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.projectile.airburst);require(ruleValues(m)!=before,"rule m.projectile.airburst omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.projectile.shrapnelCount);require(ruleValues(m)!=before,"rule m.projectile.shrapnelCount omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.projectile.airburstWeapon);require(ruleValues(m)!=before,"rule m.projectile.airburstWeapon omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.projectile.shrapnelWeapon);require(ruleValues(m)!=before,"rule m.projectile.shrapnelWeapon omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.id);require(ruleValues(m)!=before,"rule m.alternateWeapon.id omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.projectile);require(ruleValues(m)!=before,"rule m.alternateWeapon.projectile omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.limboLaunch);require(ruleValues(m)!=before,"rule m.alternateWeapon.limboLaunch omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.burst);require(ruleValues(m)!=before,"rule m.alternateWeapon.burst omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.spawner);require(ruleValues(m)!=before,"rule m.alternateWeapon.spawner omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.sonic);require(ruleValues(m)!=before,"rule m.alternateWeapon.sonic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.radBeam);require(ruleValues(m)!=before,"rule m.alternateWeapon.radBeam omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.electricBolt);require(ruleValues(m)!=before,"rule m.alternateWeapon.electricBolt omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.magBeam);require(ruleValues(m)!=before,"rule m.alternateWeapon.magBeam omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.damage);require(ruleValues(m)!=before,"rule m.alternateWeapon.damage omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWeapon.rof);require(ruleValues(m)!=before,"rule m.alternateWeapon.rof omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.id);require(ruleValues(m)!=before,"rule m.alternateWarhead.id omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.parasite);require(ruleValues(m)!=before,"rule m.alternateWarhead.parasite omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.psychedelic);require(ruleValues(m)!=before,"rule m.alternateWarhead.psychedelic omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.psychicDamage);require(ruleValues(m)!=before,"rule m.alternateWarhead.psychicDamage omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.ivanBomb);require(ruleValues(m)!=before,"rule m.alternateWarhead.ivanBomb omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.bombDisarm);require(ruleValues(m)!=before,"rule m.alternateWarhead.bombDisarm omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.airstrike);require(ruleValues(m)!=before,"rule m.alternateWarhead.airstrike omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.isLocomotor);require(ruleValues(m)!=before,"rule m.alternateWarhead.isLocomotor omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.mindControl);require(ruleValues(m)!=before,"rule m.alternateWarhead.mindControl omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.temporal);require(ruleValues(m)!=before,"rule m.alternateWarhead.temporal omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.radiation);require(ruleValues(m)!=before,"rule m.alternateWarhead.radiation omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.electricAssault);require(ruleValues(m)!=before,"rule m.alternateWarhead.electricAssault omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.particle);require(ruleValues(m)!=before,"rule m.alternateWarhead.particle omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.particleSystem);require(ruleValues(m)!=before,"rule m.alternateWarhead.particleSystem omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.paralyzes);require(ruleValues(m)!=before,"rule m.alternateWarhead.paralyzes omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateWarhead.culling);require(ruleValues(m)!=before,"rule m.alternateWarhead.culling omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateProjectile.airburst);require(ruleValues(m)!=before,"rule m.alternateProjectile.airburst omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateProjectile.shrapnelCount);require(ruleValues(m)!=before,"rule m.alternateProjectile.shrapnelCount omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateProjectile.airburstWeapon);require(ruleValues(m)!=before,"rule m.alternateProjectile.airburstWeapon omitted");++ruleLeafProbes;}
{auto value=launchSeed();Model& m=*value;auto before=ruleValues(m);fields::mutateEncoderField(m.alternateProjectile.shrapnelWeapon);require(ruleValues(m)!=before,"rule m.alternateProjectile.shrapnelWeapon omitted");++ruleLeafProbes;}
require(requestLeafProbes==22&&profileLeafProbes==16&&pointerAliasProbes==3&&ruleLeafProbes==68,"encoder leaf coverage absent");
}
std::unique_ptr<Model> configured(int variant,int hz=30){
 auto m=launchSeed();m->state.config.fixedTickHz=hz;
 switch(variant){
 case 0:break;
 case 1:m->profile.warhead=nullptr;break;
 case 2:m->nullSourceType=true;break;
 case 3:m->nullTargetType=true;break;
 case 4:m->weapon.limboLaunch=false;break;
 case 5:m->nullProjectile=true;break;
 case 6:m->missileProfile=true;break;
 case 7:m->projectile.airburst=true;break;
 case 8:m->projectile.shrapnelCount=1;break;
 case 9:m->projectile.airburstWeapon=std::string(97,'a');break;
 case 10:m->projectile.shrapnelWeapon=std::string(111,'s');break;
 case 11:m->objects.front().alive=false;break;
 case 12:m->objects.front().health=0;break;
 case 13:m->objects.back().alive=false;break;
 case 14:m->objects.back().health=0;break;
 case 15:m->objects.back().instanceId=m->objects.front().instanceId;break;
 case 16:m->objects.back().kind=WorldObjectKind::Structure;break;
 case 17:m->objects.back().authoredAircraft=true;break;
 case 18:m->objects.back().airborne=true;break;
 case 19:m->targetType.parasiteable=false;break;
 case 20:m->objects.front().garrisonHostObjectId=3;break;
 case 21:m->objects.back().dockedToObjectId=7;break;
 case 22:m->weaponTargets=false;break;
 case 23:m->state.superWeaponsInitialized=true;m->protectedObject=true;break;
 case 24:m->state.superWeaponsInitialized=true;break;
 case 25:m->objects.back().kind=WorldObjectKind::Infantry;m->targetType.parasiteable=false;break;
 case 26:m->sourceType.organic=true;break;
 case 27:m->objects.front().veterancy=2;break;
 case 28:m->weapon.damage=-6;break;
 case 29:m->weapon.rof=-3;break;
 case 30:m->warhead.paralyzes=-3;break;
 case 31:m->warhead.culling=true;m->state.specialMechanisms.launchRules_={-1,1.5};break;
 case 32:m->beginMode=1;break;
 case 33:m->beginMode=2;break;
 case 34:m->beginMode=3;break;
 case 35:m->beginMode=4;break;
 case 36:m->beginMode=5;break;
 case 37:m->beginMode=6;break;
 case 38:m->state.pendingObjectExitIds.insert(11);break;
 case 39:m->spawnMode=1;break;
 case 40:m->spawnMode=2;break;
 case 41:m->spawnMode=3;break;
 case 42:m->objects.back().garrisonOccupantIds={31};break;
 case 43:m->objects.front().transportPassengerIds={31};break;
 case 44:m->objects.back().transportPassengerIds={31};break;
 case 45:m->state.parasiteStunUntilTickByObject.restoreValidated({{11,m->state.tick+1}});break;
 case 46:m->state.parasiteStunUntilTickByObject.restoreValidated({{21,m->state.tick+1}});break;
 case 47:m->state.parasiteStunUntilTickByObject.restoreValidated({{11,m->state.tick}});break;
 case 48:m->exclusive=true;break;
 case 49:m->profile.warhead=&m->alternateWarhead;break;
 case 50:m->profile.hasWeaponSet=false;m->profile.requiresDeployment=true;m->profile.deployed=true;m->profile.occupied=true;m->profile.openTransport=true;m->profile.hasTurret=true;m->profile.slot=FixtureWeaponSlot::Secondary;m->profile.weapon=&m->alternateWeapon;m->profile.projectile=&m->alternateProjectile;m->profile.weaponId=std::string(63,'w');m->profile.projectileId=std::string(65,'p');m->profile.warheadId=std::string(67,'h');m->profile.indexedWeaponIndex=13;m->profile.occupantMuzzleIndex=7;break;
 case 51:m->projectile.airburstWeapon=std::string(97,' ');m->projectile.shrapnelWeapon="\t \r\n";break;
 case 52:m->warhead.particle="smoke";break;
 case 53:m->warhead.particleSystem="particle-system";break;
 case 54:m->weapon.burst=2;break;
 case 55:m->objects.front().kind=WorldObjectKind::Infantry;break;
 case 56:m->state.nextProjectileActorId=0;break;
 case 57:m->state.nextProjectileActorId=-1;break;
 case 58:m->state.nextProjectileActorId=INT_MAX;break;
 case 59:m->targetType.strength=0;break;
 case 60:m->objects.front().veterancy=INT_MIN;break;
 case 61:m->state.specialMechanisms.launchRules_={.125,.875};break;
 case 62:m->objects.front().dockIndex=0;break;
 case 63:m->objects.back().garrisonHostObjectId=0;break;
 default:throw std::runtime_error("unknown distinct launch fixture");
 }
 return m;
}
int launchStates=0,launchFaults=0,launchMutations=0,launchPositive=0,rollbackCases=0,postCommitCases=0,timeCases=0;
std::set<std::string> launchFalliblePorts;
LaunchOutcome compareLaunch(Model& a,Model& b,const std::string& label){
 auto x=executeLaunch(a,false),y=executeLaunch(b,true);++launchStates;
 if(x.result!=y.result||x.failure!=y.failure||x.calls!=y.calls||x.trace!=y.trace||x.snapshot!=y.snapshot){
  std::size_t at=0;while(at<x.trace.size()&&at<y.trace.size()&&x.trace[at]==y.trace[at])++at;
  std::cerr<<"launch mismatch "<<label<<" result="<<x.result<<","<<y.result<<" calls="<<x.calls<<","<<y.calls<<" trace-index="<<at<<" old-error="<<x.failure<<" new-error="<<y.failure<<" old-status="<<a.state.simulationStatus<<" new-status="<<b.state.simulationStatus<<"\n";
  throw std::runtime_error("original launch state/query/effect/exception/lifetime mismatch");
 }
 require(x.result==y.result&&x.snapshot==y.snapshot,"complete original launch mismatch");
 launchPositive+=x.result?1:0;launchFaults+=!x.failure.empty()?1:0;
 return x;
}
void timeMatrix(){
 const int frames[]={INT_MIN,-7,-1,0,1,2,7,10,15,16,36,99,257,10000,INT_MAX/1000,INT_MAX};
 const int hz[]={INT_MIN,-60,-1,0,1,2,6,15,16,30,60,120,1000,INT_MAX};
 for(int f:frames)for(int h:hz){volatile int vf=f,vh=h;require(runtimeParasiteRuleFramesToTicks(vf,vh)==current::runtimeParasiteRuleFramesToTicks(vf,vh),"original parasite round/min1/overflow-platform result changed");++timeCases;}
 require(runtimeParasiteRuleFramesToTicks(1,16)==1&&runtimeRuleFramesToTicks(1,16)==2&&runtimeParasiteRuleFramesToTicks(0,30)==1&&runtimeRuleFramesToTicks(0,30)==0,"non-equivalent generic timing owner incorrectly substituted");
}
int nestedChecks=0,capacityChecks=0,aliasChecks=0;std::vector<int> terminalProbeOrdinals;
void nestedLaunch(Model& parent,int mode){
 ++parent.nestedRuns;
 if(mode==3){auto child=executeLaunch(parent,parent.currentLaunchVersion);parent.nestedResults.push_back(encode(child.result)+child.failure+child.snapshot+encode(child.trace));return;}
 auto child=configured(mode==1?23:0);if(mode==2){child->failAt=3;child->failAfter=false;}
 auto result=executeLaunch(*child,parent.currentLaunchVersion);parent.nestedResults.push_back(encode(result.result)+result.failure+result.snapshot+encode(result.trace));
 if(mode==2&&!result.failure.empty())throw Fault("nested-propagated:"+result.failure);
}
void boundariesAndNested(){
 {auto a=configured(0),b=configured(0);a->objects.resize(1);b->objects.resize(1);auto out=compareLaunch(*a,*b,"same-source-target-borrow");require(!out.result&&a->state.nextProjectileActorId==100,"source/target alias rejection changed");++aliasChecks;}
 {auto a=configured(0),b=configured(0);
  for(auto* m:{a.get(),b.get()}){auto target=m->objects.back();m->objects.pop_back();m->objects.reserve(4099);for(int i=0;i<4097;++i){WorldObjectInstance o;o.instanceId=10000+i;o.kind=WorldObjectKind::Unit;o.type="OTHER";o.alive=false;o.health=0;o.transportPassengerIds={-10000-i};m->objects.push_back(std::move(o));}target.transportPassengerIds={11};m->objects.push_back(std::move(target));}
  auto out=compareLaunch(*a,*b,"last4099-world-containment");require(!out.result&&a->objects.size()==4099&&a->state.nextProjectileActorId==100,"last-unbounded-world object guard absent");++capacityChecks;
 }
 auto reference=configured(0);auto positive=executeLaunch(*reference,false);require(positive.result&&reference->requests.size()==1,"canonical request fixture absent");const auto request=reference->requests.front();
 {auto a=configured(0),b=configured(0);for(auto* m:{a.get(),b.get()}){auto v=m->state.specialMechanisms.launchState_.beginParasiteLaunch(request,SpecialEventSink{});require(v.accepted,"canonical pending identity seed rejected");}auto out=compareLaunch(*a,*b,"canonical-idempotent-reject");require(!out.result&&a->state.nextProjectileActorId==100&&a->spawnCalls.empty(),"canonical idempotent original rejection absent");++aliasChecks;}
 {auto a=configured(0),b=configured(0);
  for(auto* m:{a.get(),b.get()})for(int i=0;i<4096;++i){auto q=request;q.projectileId=1000+i;q.sourceObjectId=10000+i;q.targetObjectId=20000+i;auto v=m->state.specialMechanisms.launchState_.beginParasiteLaunch(q,SpecialEventSink{});require(v.accepted,"canonical4096-record fixture rejected");}
  auto out=compareLaunch(*a,*b,"canonical4096-record-limit");require(!out.result&&a->state.specialMechanisms.launchState_.pendingParasiteLaunches().size()==4096&&a->state.nextProjectileActorId==100,"original record-capacity/whole-copy rollback witness absent");++capacityChecks;
 }
 {auto a=configured(0),b=configured(0);auto baseline=compareLaunch(*a,*b,"nested-base");
  for(int at=1;at<=baseline.calls;++at)for(int mode=0;mode<4;++mode){if(mode==2&&a->observedPorts.at(static_cast<std::size_t>(at-1))=="candidate-destroy"){terminalProbeOrdinals.push_back(at);continue;}auto x=configured(0),y=configured(0);x->nestedAt=y->nestedAt=at;x->nestedMode=y->nestedMode=mode;compareLaunch(*x,*y,"nested"+std::to_string(at)+":"+std::to_string(mode));require(x->nestedRuns==y->nestedRuns,"nested count changed");++nestedChecks;}
 }
}
int runLaunch(){try{
 fields::verifyWorld();fields::verifyActions();require(fields::worldProbes==87&&fields::actionProbes==17,"full original field probes absent");verifyLaunchEncoders();timeMatrix();boundariesAndNested();
 for(int variant=0;variant<64;++variant)for(int hz:{1,16,30,60})for(bool organic:{false,true}){
  auto a=configured(variant,hz),b=configured(variant,hz);a->sourceType.organic=b->sourceType.organic=organic;
  auto out=compareLaunch(*a,*b,"case"+std::to_string(variant)+"hz"+std::to_string(hz)+"organic"+std::to_string(organic));
  if(variant==0){require(out.result&&out.failure.empty()&&a->candidateCopies==2&&a->candidateCommits==1&&a->candidateDestroys==2&&a->candidateDepth==0,"positive two-full-owned-candidates/copy-commit-scope witness absent");}
  if(variant>=32&&variant<=38){require(!out.result&&a->state.nextProjectileActorId==100&&a->spawnCalls.empty(),"only original structured-reject actor rollback differs");++rollbackCases;}
  if(variant==39){require(!out.result&&a->state.nextProjectileActorId==101&&a->state.specialMechanisms.launchState_.pendingParasiteLaunches().size()==1&&!a->spawnCalls.empty(),"spawn-beforecommit/atomic-reject/no-rollback original partial state absent");++postCommitCases;}
 }
 for(int variant:{0,23,24,32,33,35,36,37,38,39,50,51}){
  auto a=configured(variant),b=configured(variant);auto baseline=compareLaunch(*a,*b,"baseline"+std::to_string(variant));
  for(int at=1;at<=baseline.calls;++at)for(bool after:{false,true}){
   auto x=configured(variant),y=configured(variant);x->failAt=y->failAt=at;x->failAfter=y->failAfter=after;
   compareLaunch(*x,*y,"fault"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(after));
  }
 }
 for(int variant:{0,39}){
  auto a=configured(variant),b=configured(variant);auto baseline=compareLaunch(*a,*b,"live-base");
  for(int at=1;at<=baseline.calls;++at)for(int mode=0;mode<17;++mode){
   auto x=configured(variant),y=configured(variant);x->mutationAt=y->mutationAt=at;x->mutationMode=y->mutationMode=mode;
   compareLaunch(*x,*y,"live"+std::to_string(variant)+":"+std::to_string(at)+":"+std::to_string(mode));++launchMutations;
  }
 }
 require(launchPositive>0&&launchFaults>0&&launchMutations>0&&rollbackCases==56&&postCommitCases==8&&terminalProbeOrdinals.size()==2,"required original positive/rollback/partial/fault/live witnesses absent");
 std::cout<<"parasite-launch checks="<<checks<<" states="<<launchStates<<" world-probes=87 action-probes=17 request-probes="<<requestLeafProbes<<" profile-probes="<<profileLeafProbes<<" profile-pointer-aliases="<<pointerAliasProbes<<" rule-probes="<<ruleLeafProbes<<" time-cases="<<timeCases<<" positive="<<launchPositive<<" rollback="<<rollbackCases<<" postcommit-reject="<<postCommitCases<<" faults="<<launchFaults<<" fallible-ports="<<hitPorts.size()<<" live-mutations="<<launchMutations<<" nested="<<nestedChecks<<" capacity="<<capacityChecks<<" alias="<<aliasChecks<<" isolated-terminal-cases="<<terminalProbeOrdinals.size()<<" original155line-order-borrow-whole-candidate-temporary-failure-and-commit=exact failures=0\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"parasite-launch failure: "<<e.what()<<"\n";return 1;}}
}
