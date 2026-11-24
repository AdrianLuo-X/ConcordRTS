#include "simulation/abilities/parasites/RuntimeParasiteLaunchPolicy.h"
#include <cstdlib>
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include <climits>
#include "RuntimeParasiteHostEventModelFoundation.inl"

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
