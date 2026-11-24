#include "simulation/abilities/parasites/RuntimeParasiteAdmissionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteStunState.h"
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "RuntimeParasiteAdmissionFields.inl"
#include <climits>
#include <sstream>
#include <unordered_map>
namespace parasite_admission_model {
namespace current=concord::simulation::abilities;
namespace fields=parasite_admission_fields;
using fields::encode;
using runtime_save_text::trimAsciiLocal;
int checks=0,states=0,positiveContained=0,positiveAuthority=0,hostExceptions=0,positiveRules=0,capacityObjects=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Intent {int kind=0,target=-1;};
struct Data {
 WorldObjectInstance external;
 std::vector<WorldObjectInstance> objects;
 RuntimeParasiteStunState stuns;
 int tick=0;
 std::unordered_map<int,int> engineer,occupy,transport;
 std::unordered_map<int,Intent> bomb;
 bool repairInitialized=false,specialInitialized=false;
 std::vector<RuntimeRepairDepotRelation> repairs;
 std::vector<RuntimeFactoryDeliveryLease> leases;
 std::map<int,int> children,parents;
};
using Trace=std::vector<std::string>;
void record(Trace& trace,const std::string& key,int id=0){trace.push_back(key+encode(id));}
template<class V>struct Map {
 const std::unordered_map<int,V>& values;Trace& trace;const char* name;
 auto find(int id)const {record(trace,name,id);return values.find(id);}
 auto end()const{return values.end();}
};
struct World {
 const Data& data;Trace& trace;
 const auto& getObjects()const{record(trace,"objects");return data.objects;}
};
struct Stuns {
 const Data& data;Trace& trace;
 bool isActive(int id,const int& tick)const{record(trace,"stun");return data.stuns.isActive(id,tick);}
};
struct Repair {
 const Data& data;Trace& trace;
 const auto& relations()const{record(trace,"repair-relations");return data.repairs;}
};
struct Factory {
 const Data& data;Trace& trace;
 const auto& leases()const{record(trace,"factory-leases");return data.leases;}
};
struct ParentMap {
 const Data& data;Trace& trace;
 auto find(int id)const{record(trace,"carrier-parent",id);return data.parents.find(id);}
 auto end()const{return data.parents.end();}
};
struct Special {
 const Data& data;Trace& trace;ParentMap parents;
 Special(const Data& d,Trace& t):data(d),trace(t),parents{d,t}{}
 bool isCarrierChild(int id)const{record(trace,"carrier-child",id);return data.children.find(id)!=data.children.end();}
 const auto& carrierParents()const{return parents;}
};
struct FixtureState {
 World world;Stuns parasiteStunUntilTickByObject;const int& tick;
 Map<int> pendingEngineerCaptures,pendingOccupyTargets,pendingTransportTargets;
 Map<Intent> pendingTimedBombIntents;
 const bool& repairDepotsInitialized;Repair repairDepots;Factory factoryDeliveries;
 const bool& specialMechanismsInitialized;Special specialMechanisms;
 FixtureState(const Data& d,Trace& t):world{d,t},parasiteStunUntilTickByObject{d,t},tick(d.tick),
  pendingEngineerCaptures{d.engineer,t,"engineer"},pendingOccupyTargets{d.occupy,t,"occupy"},
  pendingTransportTargets{d.transport,t,"transport"},pendingTimedBombIntents{d.bomb,t,"bomb"},
  repairDepotsInitialized(d.repairInitialized),repairDepots{d,t},factoryDeliveries{d,t},
  specialMechanismsInitialized(d.specialInitialized),specialMechanisms(d,t){}
};
#include "RuntimeParasiteAdmissionOriginal.inl"
class Queries final:public current::RuntimeParasiteAdmissionQueries {
 const Data& d_;Trace& t_;
public:
 Queries(const Data& d,Trace& t):d_(d),t_(t){}
 const std::vector<WorldObjectInstance>& objects()const noexcept override{record(t_,"objects");return d_.objects;}
 const RuntimeParasiteStunState& stunState()const noexcept override{record(t_,"stun");return d_.stuns;}
 const int& tick()const noexcept override{return d_.tick;}
 bool hasPendingEngineerCapture(int id)const noexcept override{return Map<int>{d_.engineer,t_,"engineer"}.find(id)!=d_.engineer.end();}
 bool hasPendingOccupyTarget(int id)const noexcept override{return Map<int>{d_.occupy,t_,"occupy"}.find(id)!=d_.occupy.end();}
 bool hasPendingTransportTarget(int id)const noexcept override{return Map<int>{d_.transport,t_,"transport"}.find(id)!=d_.transport.end();}
 bool hasPendingTimedBombIntent(int id)const noexcept override{return Map<Intent>{d_.bomb,t_,"bomb"}.find(id)!=d_.bomb.end();}
 bool repairDepotsInitialized()const noexcept override{return d_.repairInitialized;}
 const std::vector<RuntimeRepairDepotRelation>& repairRelations()const noexcept override{record(t_,"repair-relations");return d_.repairs;}
 const std::vector<RuntimeFactoryDeliveryLease>& factoryLeases()const noexcept override{record(t_,"factory-leases");return d_.leases;}
 bool specialMechanismsInitialized()const noexcept override{return d_.specialInitialized;}
 bool isCarrierChild(int id)const noexcept override{record(t_,"carrier-child",id);return d_.children.find(id)!=d_.children.end();}
 bool isCarrierParent(int id)const noexcept override{record(t_,"carrier-parent",id);return d_.parents.find(id)!=d_.parents.end();}
};
template<class M>std::string sortedMap(const M& m){
 std::map<int,typename M::mapped_type> sorted(m.begin(),m.end());std::string out;
 for(const auto& [id,v]:sorted){
  out+=encode(id);
  if constexpr(std::is_same_v<typename M::mapped_type,Intent>)out+=encode(v.kind)+encode(v.target);
  else out+=encode(v);
 }
 return out;
}
std::string relationSnapshot(const RuntimeRepairDepotRelation& r){return encode(r.hostId)+encode(r.unitId)+encode(r.dockIndex)+encode(r.queueIndex)+encode(r.phase)+encode(r.dockCell.x)+encode(r.dockCell.y)+encode(r.dockCell.onBridge)+encode(r.queueCell.x)+encode(r.queueCell.y)+encode(r.queueCell.onBridge)+encode(r.hostOwner)+encode(r.unitOwner)+encode(r.hostType)+encode(r.unitType)+encode(r.reservedTick)+encode(r.attachedTick)+encode(r.hasRepaired)+encode(r.domain);}
std::string leaseSnapshot(const RuntimeFactoryDeliveryLease& l){return encode(l.canonicalOwner)+encode(l.hostId)+encode(l.dockIndex)+encode(l.lane)+encode(l.orderSerial)+encode(l.producedObjectId)+encode(l.acquiredTick)+encode(l.rallyCell.x)+encode(l.rallyCell.y)+encode(l.rallyCell.onBridge)+encode(l.domain);}
std::string snap(const Data& d){
 std::string out=encode(d.external)+encode(d.tick)+encode(d.repairInitialized)+encode(d.specialInitialized);
 for(const auto& o:d.objects)out+=encode(o);
 out+=sortedMap(d.stuns.entries())+sortedMap(d.engineer)+sortedMap(d.occupy)+sortedMap(d.transport)+sortedMap(d.bomb)+sortedMap(d.children)+sortedMap(d.parents);
 for(const auto& r:d.repairs)out+=relationSnapshot(r);
 for(const auto& l:d.leases)out+=leaseSnapshot(l);
 return out;
}
Data seed(unsigned mask,int id,int mode){
 Data d;d.tick=mode%3==0?23:mode%3==1?-7:INT_MAX;
 auto& o=d.external;o.instanceId=id;o.alive=(mode&1)!=0;o.health=mode-3;o.owner="red";o.type="borrowed-subject";
 o.x=mode;o.y=-mode;o.cell=mode*512;o.mission="ReadOnly";o.dockIndex=-1;o.dockedToObjectId=-1;o.garrisonHostObjectId=-1;
 if(mask&1)d.stuns.restoreValidated({{id,d.tick==INT_MAX?INT_MAX:d.tick+1}});
 if(mask&2)o.garrisonHostObjectId=mode%2==0?0:57;
 if(mask&4)o.garrisonOccupantIds={-1,0,67};
 if(mask&8)o.transportPassengerIds={-3,0,57};
 for(int i=0;i<3;++i){auto p=o;p.instanceId=100+i;p.garrisonHostObjectId=-1;p.garrisonOccupantIds.clear();p.transportPassengerIds=mode%2?std::vector<int>{}:std::vector<int>{-100,44};p.alive=false;p.health=0;p.dockedToObjectId=-1;p.dockIndex=-1;d.objects.push_back(p);}
 if(mask&16)d.objects[static_cast<std::size_t>(mode%3)].transportPassengerIds.push_back(id);
 if(mask&32)o.dockedToObjectId=0;
 if(mask&64)o.dockIndex=0;
 if(mask&128)d.engineer.emplace(id,99);
 if(mask&256)d.occupy.emplace(id,-1);
 if(mask&512)d.transport.emplace(id,73);
 if(mask&1024)d.bomb.emplace(id,Intent{mode%3,-43});
 d.repairInitialized=(mask&2048)!=0;d.specialInitialized=(mask&32768)!=0;
 for(int i=0;i<3;++i){
  RuntimeRepairDepotRelation r;r.unitId=1000+i;r.hostId=2000+i;r.phase=static_cast<RuntimeRepairDepotRelationPhase>(i);
  r.queueIndex=i;r.hostOwner=std::string(30,'h');r.unitOwner="blue";r.hostType="repair";r.unitType="unit";r.dockCell={-i,7+i,(i&1)!=0};r.queueCell={8+i,-i,(i&1)==0};r.reservedTick=-77;r.attachedTick=55;r.hasRepaired=true;r.domain=static_cast<RuntimeRepairDepotDomain>(mode%2);d.repairs.push_back(r);
  RuntimeFactoryDeliveryLease l;l.producedObjectId=3000+i;l.hostId=4000+i;l.canonicalOwner="green";l.dockIndex=i;l.lane=static_cast<RuntimeProductionLane>(i%3);l.orderSerial=37;l.acquiredTick=-7;l.rallyCell={i,-i,(i&1)!=0};l.domain=static_cast<RuntimeFactoryDeliveryDomain>(mode%2);d.leases.push_back(l);
 }
 if(mask&4096){if(mode%2)d.repairs[static_cast<std::size_t>(mode%3)].unitId=id;else d.repairs[static_cast<std::size_t>(mode%3)].hostId=id;}
 if(mask&8192){if(mode%2)d.leases[static_cast<std::size_t>(mode%3)].producedObjectId=id;else d.leases[static_cast<std::size_t>(mode%3)].hostId=id;}
 if(mask&16384)d.children.emplace(id,99);
 if(mask&65536)d.parents.emplace(id,-7);
 return d;
}
void compare(const Data& source,int subjectIndex,bool asHost){
 Data a=source,b=source;Trace oldTrace,newTrace;FixtureState old(a,oldTrace);Queries now(b,newTrace);
 const auto& oldObject=subjectIndex<0?a.external:a.objects.at(static_cast<std::size_t>(subjectIndex));
 const auto& newObject=subjectIndex<0?b.external:b.objects.at(static_cast<std::size_t>(subjectIndex));
 const auto beforeA=snap(a),beforeB=snap(b);require(beforeA==beforeB,"input clone differs");
 const bool oldContained=runtimeParasiteObjectIsContained(old,oldObject);
 const bool newContained=current::runtimeParasiteObjectIsContained(now,newObject);
 require(oldContained==newContained,"original containment result differs");require(oldTrace==newTrace,"containment query order differs");
 oldTrace.clear();newTrace.clear();
 const bool oldAuthority=runtimeParasiteObjectHasExclusiveAuthority(old,oldObject,asHost);
 const bool newAuthority=current::runtimeParasiteObjectHasExclusiveAuthority(now,newObject,asHost);
 require(oldAuthority==newAuthority,"original authority result differs");require(oldTrace==newTrace,"authority shortcircuit/lookup/range order differs");
 require(snap(a)==beforeA&&snap(b)==beforeB,"original borrowed state mutated");
 positiveContained+=newContained?1:0;positiveAuthority+=newAuthority?1:0;++states;
}
using WFlag=bool WeaponType::*;using HFlag=bool WarheadType::*;
constexpr std::array<WFlag,6> weaponFlags{{&WeaponType::limboLaunch,&WeaponType::spawner,&WeaponType::sonic,&WeaponType::radBeam,&WeaponType::electricBolt,&WeaponType::magBeam}};
constexpr std::array<HFlag,11> warheadFlags{{&WarheadType::parasite,&WarheadType::psychedelic,&WarheadType::psychicDamage,&WarheadType::ivanBomb,&WarheadType::bombDisarm,&WarheadType::airstrike,&WarheadType::isLocomotor,&WarheadType::mindControl,&WarheadType::temporal,&WarheadType::radiation,&WarheadType::electricAssault}};
std::string ruleInputs(const WeaponType& w,const WarheadType& h){
 std::string out=encode(w.burst)+encode(h.particle)+encode(h.particleSystem);
 for(auto field:weaponFlags)out+=encode(w.*field);
 for(auto field:warheadFlags)out+=encode(h.*field);
 return out;
}
void rules(){
 std::array<int,8> bursts{{INT_MIN,-9,-1,0,1,2,9,INT_MAX}};
 std::array<std::string,8> particles{{"", " \t\r\n", "x", std::string("\0x",2), std::string(96,' '),std::string(160,'x'),"\xC2\xA0", " x "}};
 for(int bit=-1;bit<17;++bit)for(int burst:bursts)for(std::size_t p=0;p<particles.size();++p)for(std::size_t q=0;q<particles.size();++q){
  WeaponType w;WarheadType h;w.limboLaunch=true;h.parasite=true;w.burst=burst;
  if(bit==0)w.limboLaunch=false;
  else if(bit>=1&&bit<=5)w.*weaponFlags[static_cast<std::size_t>(bit)]=true;
  else if(bit==6)h.parasite=false;
  else if(bit>=7)h.*warheadFlags[static_cast<std::size_t>(bit-6)]=true;
  h.particle=particles[p];h.particleSystem=particles[q];const auto before=ruleInputs(w,h);
  const bool a=runtimeParasiteWarheadIsExclusive(w,h),b=current::runtimeParasiteWarheadIsExclusive(w,h);
  require(a==b,"complete original rule predicate differs");require(ruleInputs(w,h)==before,"borrowed rule inputs mutated");positiveRules+=b?1:0;++states;
 }
}
void verifyBorrowedRecords(){
 RuntimeRepairDepotRelation r;const auto rbefore=relationSnapshot(r);
{auto v=r;fields::mutateEncoderField(v.hostId);require(relationSnapshot(v)!=rbefore,"borrowed repair field hostId omitted");}
{auto v=r;fields::mutateEncoderField(v.unitId);require(relationSnapshot(v)!=rbefore,"borrowed repair field unitId omitted");}
{auto v=r;fields::mutateEncoderField(v.dockIndex);require(relationSnapshot(v)!=rbefore,"borrowed repair field dockIndex omitted");}
{auto v=r;fields::mutateEncoderField(v.queueIndex);require(relationSnapshot(v)!=rbefore,"borrowed repair field queueIndex omitted");}
{auto v=r;fields::mutateEncoderField(v.phase);require(relationSnapshot(v)!=rbefore,"borrowed repair field phase omitted");}
{auto v=r;fields::mutateEncoderField(v.dockCell.x);require(relationSnapshot(v)!=rbefore,"borrowed repair field dockCell.x omitted");}
{auto v=r;fields::mutateEncoderField(v.dockCell.y);require(relationSnapshot(v)!=rbefore,"borrowed repair field dockCell.y omitted");}
{auto v=r;fields::mutateEncoderField(v.dockCell.onBridge);require(relationSnapshot(v)!=rbefore,"borrowed repair field dockCell.onBridge omitted");}
{auto v=r;fields::mutateEncoderField(v.queueCell.x);require(relationSnapshot(v)!=rbefore,"borrowed repair field queueCell.x omitted");}
{auto v=r;fields::mutateEncoderField(v.queueCell.y);require(relationSnapshot(v)!=rbefore,"borrowed repair field queueCell.y omitted");}
{auto v=r;fields::mutateEncoderField(v.queueCell.onBridge);require(relationSnapshot(v)!=rbefore,"borrowed repair field queueCell.onBridge omitted");}
{auto v=r;fields::mutateEncoderField(v.hostOwner);require(relationSnapshot(v)!=rbefore,"borrowed repair field hostOwner omitted");}
{auto v=r;fields::mutateEncoderField(v.unitOwner);require(relationSnapshot(v)!=rbefore,"borrowed repair field unitOwner omitted");}
{auto v=r;fields::mutateEncoderField(v.hostType);require(relationSnapshot(v)!=rbefore,"borrowed repair field hostType omitted");}
{auto v=r;fields::mutateEncoderField(v.unitType);require(relationSnapshot(v)!=rbefore,"borrowed repair field unitType omitted");}
{auto v=r;fields::mutateEncoderField(v.reservedTick);require(relationSnapshot(v)!=rbefore,"borrowed repair field reservedTick omitted");}
{auto v=r;fields::mutateEncoderField(v.attachedTick);require(relationSnapshot(v)!=rbefore,"borrowed repair field attachedTick omitted");}
{auto v=r;fields::mutateEncoderField(v.hasRepaired);require(relationSnapshot(v)!=rbefore,"borrowed repair field hasRepaired omitted");}
{auto v=r;fields::mutateEncoderField(v.domain);require(relationSnapshot(v)!=rbefore,"borrowed repair field domain omitted");}
 RuntimeFactoryDeliveryLease l;const auto lbefore=leaseSnapshot(l);
{auto v=l;fields::mutateEncoderField(v.canonicalOwner);require(leaseSnapshot(v)!=lbefore,"borrowed lease field canonicalOwner omitted");}
{auto v=l;fields::mutateEncoderField(v.hostId);require(leaseSnapshot(v)!=lbefore,"borrowed lease field hostId omitted");}
{auto v=l;fields::mutateEncoderField(v.dockIndex);require(leaseSnapshot(v)!=lbefore,"borrowed lease field dockIndex omitted");}
{auto v=l;fields::mutateEncoderField(v.lane);require(leaseSnapshot(v)!=lbefore,"borrowed lease field lane omitted");}
{auto v=l;fields::mutateEncoderField(v.orderSerial);require(leaseSnapshot(v)!=lbefore,"borrowed lease field orderSerial omitted");}
{auto v=l;fields::mutateEncoderField(v.producedObjectId);require(leaseSnapshot(v)!=lbefore,"borrowed lease field producedObjectId omitted");}
{auto v=l;fields::mutateEncoderField(v.acquiredTick);require(leaseSnapshot(v)!=lbefore,"borrowed lease field acquiredTick omitted");}
{auto v=l;fields::mutateEncoderField(v.rallyCell.x);require(leaseSnapshot(v)!=lbefore,"borrowed lease field rallyCell.x omitted");}
{auto v=l;fields::mutateEncoderField(v.rallyCell.y);require(leaseSnapshot(v)!=lbefore,"borrowed lease field rallyCell.y omitted");}
{auto v=l;fields::mutateEncoderField(v.rallyCell.onBridge);require(leaseSnapshot(v)!=lbefore,"borrowed lease field rallyCell.onBridge omitted");}
{auto v=l;fields::mutateEncoderField(v.domain);require(leaseSnapshot(v)!=lbefore,"borrowed lease field domain omitted");}
}
int run(){try{
 verifyBorrowedRecords();
 fields::verifyWorld();require(fields::worldProbes==87,"complete World87 field probe absent");
 const std::array<int,7> ids{{-1,0,17,44,100,INT_MIN,INT_MAX}};
 for(int id:ids)for(int mode=0;mode<6;++mode)for(bool host:{false,true}){
  compare(seed(0,id,mode),-1,host);
  for(unsigned bit=0;bit<17;++bit)compare(seed(1u<<bit,id,mode),-1,host);
  for(unsigned bit=0;bit<17;++bit)compare(seed(0x1FFFFu^(1u<<bit),id,mode),-1,host);
  for(unsigned mask:{4096u,2048u|4096u,16384u,65536u,32768u|16384u,32768u|65536u}){
   auto d=seed(mask,id,mode);compare(d,-1,host);compare(d,0,host);
  }
 }
 unsigned random=0x9137u;
 for(int i=0;i<512;++i){random=random*1664525u+1013904223u;auto d=seed(random&0x1FFFFu,ids[static_cast<std::size_t>(i)%ids.size()],i%6);compare(d,i%4-1,(i&1)!=0);}
 {auto d=seed(32768u|65536u,17,0);Trace t;Queries q(d,t);require(current::runtimeParasiteObjectHasExclusiveAuthority(q,d.external,false)&&!current::runtimeParasiteObjectHasExclusiveAuthority(q,d.external,true),"positive source-versus-host carrier-parent exception absent");++hostExceptions;}
 {auto d=seed(16,17,0);Trace t;Queries q(d,t);require(current::runtimeParasiteObjectIsContained(q,d.external),"inactive dead transport candidate incorrectly filtered");}
 {auto d=seed(0,17,0);d.objects.clear();d.repairs.clear();d.leases.clear();compare(d,-1,false);}
 {auto d=seed(0,17,0);for(int i=0;i<4096;++i){auto o=d.external;o.instanceId=10000+i;o.transportPassengerIds={-i-100000};d.objects.push_back(o);}d.objects.back().transportPassengerIds.push_back(17);compare(d,-1,false);capacityObjects=static_cast<int>(d.objects.size());require(capacityObjects==4099,"unbounded last-world-object transport witness absent");}
 rules();require(positiveContained>0&&positiveAuthority>0&&positiveRules>0&&hostExceptions>0&&positiveRules==45,"positive original witnesses absent");
 std::cout<<"parasite-admission checks="<<checks<<" states="<<states<<" world-probes=87 repair-probes=19 lease-probes=11 rule-fields=20 original-functions=3 original-lines=86 positive-contained="<<positiveContained<<" positive-authority="<<positiveAuthority<<" positive-rule="<<positiveRules<<" source-host-exceptions="<<hostExceptions<<" original-shortcircuit-borrow-order-readonly-and-rule-combinations=exact failures=0\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"parasite-admission failure: "<<e.what()<<"\n";return 1;}}
}
