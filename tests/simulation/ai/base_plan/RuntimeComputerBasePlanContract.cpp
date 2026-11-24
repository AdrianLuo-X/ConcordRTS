#include "simulation/ai/RuntimeComputerBasePlan.h"
#include "data/rules/RulesDatabase.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
namespace domain=concord::simulation::ai;
namespace {
using domain::RuntimeComputerAiBaseTargets;
using domain::RuntimeComputerAiStructureRole;
using Role=RuntimeComputerAiStructureRole;
using Targets=RuntimeComputerAiBaseTargets;
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;
using runtime_authority_hash::runtimeStableHashMix;
using runtime_authority_hash::runtimeStableHashMixString;
int checks=0,models=0,faults=0;
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
struct Trace{std::vector<std::string> calls;int failAt=0;void hit(const std::string& call){calls.push_back(call);if(static_cast<int>(calls.size())==failAt)throw std::runtime_error("fault:"+call);}};
struct Profile{int attackThreshold=6;};
struct Model{
    RuntimeComputerAiState ai;RuntimeComputerAiMetrics metrics;Profile profile;
    ComputerBaseBuildingRules base;ComputerMiningRules mining;
    std::map<std::string,UnitType> units;std::map<std::string,bool> allowed;
    bool superweapons=true;int difficulty=1,side=0;
    Targets authored;std::vector<Role> goals;
    bool mutateDefenseOnSide=false,mutateLimitsOnMining=false,mutateOwnerOnSide=false;
    int mutatedDefense=52;std::string mutatedOwner="late-owner";
    std::uint64_t rng=0x93281ULL;int tick=123,productionSerial=77;
    Trace trace;
};
Model* active=nullptr;
std::string allowedKey(const std::string& owner,const std::string& country){return owner+"|"+country;}
struct RulesProxy{Model& m;const ComputerBaseBuildingRules& getComputerBaseBuildingRules()const{m.trace.hit("base-rules");return m.base;}const ComputerMiningRules& getComputerMiningRules()const{m.trace.hit("mining-rules");if(m.mutateLimitsOnMining){m.superweapons=false;m.profile.attackThreshold=1000;}return m.mining;}};
struct ConfigProxy{bool& superweapons;};
struct Root{Model& model;RulesProxy rules;ConfigProxy config;explicit Root(Model& m):model(m),rules{m},config{m.superweapons}{}};
int runtimeComputerMiningDifficultyIndex(const std::string& value){active->trace.hit("difficulty:"+value);return active->difficulty;}
int runtimeBasePlanningSideForCountry(const std::string& value){active->trace.hit("side:"+value);if(active->mutateDefenseOnSide)active->base.alliedDefenseCounts[static_cast<std::size_t>(active->difficulty)]=active->mutatedDefense;if(active->mutateOwnerOnSide)active->ai.owner=active->mutatedOwner;return active->side;}
const UnitType* findRuntimeUnitTypeById(const RulesProxy& rules,const std::string& id){rules.m.trace.hit("unit:"+id);auto it=rules.m.units.find(id);return it==rules.m.units.end()?nullptr:&it->second;}
bool runtimeOwnerListAllowsHouse(const std::string& owner,const std::string& country){active->trace.hit("allow:"+allowedKey(owner,country));auto it=active->allowed.find(allowedKey(owner,country));return it!=active->allowed.end()&&it->second;}
namespace original{
using RuntimeGameSessionState=Root;using RuntimeComputerAiProfile=Profile;
#include "RuntimeComputerBasePlanOriginal.inl"
}
class Queries final:public domain::RuntimeComputerBasePlanQueries{
    Root& r;
public:
    explicit Queries(Root& value):r(value){}
    const ComputerBaseBuildingRules& baseBuildingRules()const override{return r.rules.getComputerBaseBuildingRules();}
    const ComputerMiningRules& miningRules()const override{return r.rules.getComputerMiningRules();}
    int difficultyIndex(const std::string& value)const override{return runtimeComputerMiningDifficultyIndex(value);}
    int planningSide(const std::string& value)const override{return runtimeBasePlanningSideForCountry(value);}
    const UnitType* unitTypeById(const std::string& value)const override{return findRuntimeUnitTypeById(r.rules,value);}
    bool ownerListAllowsHouse(const std::string& owner,const std::string& country)const override{return runtimeOwnerListAllowsHouse(owner,country);}
};
template<class T> std::string encode(const T& v){
    if constexpr(std::is_enum_v<T>)return "e"+std::to_string(static_cast<int>(v))+";";
    else if constexpr(std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr(std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr(std::is_same_v<T,RuntimeCellPoint>)return encode(v.x)+encode(v.y);
    else if constexpr(std::is_same_v<T,RuntimeComputerAiState>){return encode(v.initialized)+encode(v.owner)+encode(v.country)+encode(v.difficulty)+encode(v.baseCenter)+encode(v.credits)+encode(v.nextThinkTick)+encode(v.nextIncomeTick)+encode(v.nextBuildTick)+encode(v.nextAttackTick)+encode(v.nextRepairTick)+encode(v.productionSerial)+encode(v.incomeEarned)+encode(v.creditsSpent)+encode(v.structuresBuilt)+encode(v.infantryBuilt)+encode(v.unitsBuilt)+encode(v.freeUnitsReceived)+encode(v.attackWaves)+encode(v.attacksOrdered)+encode(v.defenseOrders)+encode(v.tacticalOrders)+encode(v.repairsPerformed)+encode(v.stalledBuilds)+encode(v.lastTargetObjectId)+encode(v.lastBuildType)+encode(v.status);}
    else if constexpr(std::is_same_v<T,RuntimeComputerAiMetrics>){return encode(v.structures)+encode(v.mobileUnits)+encode(v.combatUnits)+encode(v.constructionYards)+encode(v.powerPlants)+encode(v.powerProduced)+encode(v.powerUsed)+encode(v.refineries)+encode(v.ordinaryRefineries)+encode(v.slaveMiners)+encode(v.harvesters)+encode(v.barracks)+encode(v.vehicleFactories)+encode(v.shipyards)+encode(v.radars)+encode(v.techCenters)+encode(v.defenses)+encode(v.superweaponStructures);}
    else if constexpr(std::is_same_v<T,ComputerBaseBuildingRules>)return encode(v.alliedDefenseCounts)+encode(v.sovietDefenseCounts)+encode(v.thirdDefenseCounts)+encode(v.extraRefineries)+encode(v.refineryTypes)+encode(v.harvesterTypes);
    else if constexpr(std::is_same_v<T,ComputerMiningRules>)return encode(v.virtualPurifiers)+encode(v.harvestersPerRefinery)+encode(v.slaveMinerNumber);
    else if constexpr(std::is_same_v<T,UnitType>)return encode(v.owner); // Only the original readonly owner field is projected by this dependency model.
    else if constexpr(std::is_same_v<T,Targets>)return encode(v.defenses)+encode(v.refineries)+encode(v.slaveRefineries);
    else if constexpr(requires{v.first;v.second;})return encode(v.first)+encode(v.second);
    else {std::string out="n"+std::to_string(v.size())+":";for(const auto& item:v)out+=encode(item);return out;}
}
std::string snapshot(const Model& m){return encode(m.ai)+encode(m.metrics)+encode(m.profile.attackThreshold)+encode(m.base)+encode(m.mining)+encode(m.units)+encode(m.allowed)+encode(m.superweapons)+encode(m.difficulty)+encode(m.side)+encode(m.authored)+encode(m.goals)+encode(m.mutateDefenseOnSide)+encode(m.mutateLimitsOnMining)+encode(m.mutateOwnerOnSide)+encode(m.mutatedDefense)+encode(m.mutatedOwner)+encode(m.rng)+encode(m.tick)+encode(m.productionSerial);}
enum class Operation{Targets,Plan,Goals};
struct Outcome{Targets targets;std::vector<Role> plan;std::string error;bool operator==(const Outcome& other)const{return encode(targets)==encode(other.targets)&&plan==other.plan&&error==other.error;}};
Outcome invoke(Model& m,Operation op,bool old){Root root(m);Queries q(root);const domain::RuntimeComputerBasePlanLimits limits{m.superweapons,m.profile.attackThreshold};active=&m;Outcome out;try{
    if(op==Operation::Targets)out.targets=old?original::runtimeComputerAiBaseTargets(root,m.ai):domain::runtimeComputerAiBaseTargets(q,m.ai);
    else if(op==Operation::Plan)out.plan=old?original::runtimeComputerAiBasePlan(m.ai,m.authored):domain::runtimeComputerAiBasePlan(q,m.ai,m.authored);
    else if(old)original::appendRuntimeComputerAiBasePlanGoals(root,m.ai,m.profile,m.metrics,m.goals);
    else domain::appendRuntimeComputerAiBasePlanGoals(limits,q,m.ai,m.metrics,m.goals);
}catch(const std::runtime_error& e){out.error=e.what();}active=nullptr;return out;}
void compare(const std::string& name,const Model& seed,Operation op,const std::function<void(const Model&,const Outcome&)>& verify){Model probe=seed;probe.trace={};auto result=invoke(probe,op,true);require(result.error.empty(),name+" original normal error "+result.error);verify(probe,result);const int count=static_cast<int>(probe.trace.calls.size());
    for(int fault=0;fault<=count+1;++fault){Model a=seed,b=seed;a.trace={};b.trace={};a.trace.failAt=fault;b.trace.failAt=fault;const auto x=invoke(a,op,true),y=invoke(b,op,false);++models;if(fault>0&&fault<=count)++faults;
        require(x==y,name+" output/failure at "+std::to_string(fault));require(a.trace.calls==b.trace.calls,name+" ordered lazy reads at "+std::to_string(fault));require(snapshot(a)==snapshot(b),name+" complete owned scalar/config/output partial state at "+std::to_string(fault));require((fault>0&&fault<=count)?!x.error.empty():x.error.empty(),name+" fault reachability");
    }
}
Model seed(){Model m;m.ai.owner=" Player One ";m.ai.country="Americans";m.ai.difficulty="Medium";m.ai.initialized=true;m.ai.baseCenter={3,4};m.ai.credits=7500;m.ai.productionSerial=12;m.ai.status="original-owner";UnitType harv;harv.owner="soviet";m.units["HARV"]=harv;UnitType cmin;cmin.owner="allied";m.units["CMIN"]=cmin;m.allowed[allowedKey("allied","Americans")]=true;m.metrics.structures=20;m.metrics.combatUnits=9;m.authored={0,1,false};return m;}

std::vector<Role> opening(){return {Role::Power,Role::Refinery,Role::Barracks,Role::VehicleFactory,Role::Radar,Role::Tech,Role::Superweapon};}
void targetCases(){
    for(int side:{-1,0,1,2,7})for(int diff:{0,1,2}){auto m=seed();m.side=side;m.difficulty=diff;m.units.clear();const int defense=side==0?m.base.alliedDefenseCounts[static_cast<std::size_t>(diff)]:side==1?m.base.sovietDefenseCounts[static_cast<std::size_t>(diff)]:side==2?m.base.thirdDefenseCounts[static_cast<std::size_t>(diff)]:0;const int ref=m.mining.slaveMinerNumber[static_cast<std::size_t>(diff)];compare("targets side/diff "+std::to_string(side)+std::to_string(diff),m,Operation::Targets,[=](const auto&,const auto& r){require(r.targets.defenses==defense&&r.targets.refineries==ref&&r.targets.slaveRefineries,"authored side/difficulty selection");});}
    auto m=seed();compare("owned harvester disables slave exception",m,Operation::Targets,[](const auto&,const auto& r){require(r.targets.defenses==20&&r.targets.refineries==2&&!r.targets.slaveRefineries,"stock normal allied");});
    m=seed();m.base.harvesterTypes.clear();compare("empty harvester list is slave",m,Operation::Targets,[](const auto&,const auto& r){require(r.targets.slaveRefineries&&r.targets.refineries==3,"empty harvester exception");});
    m=seed();m.base.harvesterTypes={"missing","CMIN","HARV"};compare("missing lookup before accepted owner",m,Operation::Targets,[](const auto& v,const auto& r){require(!r.targets.slaveRefineries,"later allowed owner");require(v.trace.calls==std::vector<std::string>({"base-rules","difficulty:Medium","side:Americans","unit:missing","unit:CMIN","allow:allied|Americans"}),"first accepted masks remaining lookups and mining getter");});
    m=seed();m.allowed[allowedKey("soviet","Americans")]=true;compare("first eligible masks second owner",m,Operation::Targets,[](const auto& v,const auto& r){require(!r.targets.slaveRefineries&&std::find(v.trace.calls.begin(),v.trace.calls.end(),"unit:CMIN")==v.trace.calls.end(),"original owner short circuit");});
    for(int amount:{-8,0,1,2,19}){m=seed();m.units.clear();m.mining.slaveMinerNumber[1]=amount;compare("slave minimum "+std::to_string(amount),m,Operation::Targets,[=](const auto&,const auto& r){require(r.targets.refineries==std::max(1,amount),"original min one");});}
    for(int amount:{-3,-1,0,4,25}){m=seed();m.base.extraRefineries[1]=amount;compare("ordinary extra unchanged "+std::to_string(amount),m,Operation::Targets,[=](const auto&,const auto& r){require(r.targets.refineries==1+amount&&!r.targets.slaveRefineries,"no new ordinary clamp");});}
    m=seed();m.mutateDefenseOnSide=true;compare("live base rule borrowed through side call",m,Operation::Targets,[](const auto&,const auto& r){require(r.targets.defenses==52,"must observe original borrowed rule mutation after getter");});
    m=seed();m.base.refineryTypes={"unused","different"};compare("unused build refinery metadata preserved",m,Operation::Targets,[](const auto& v,const auto& r){require(v.base.refineryTypes==std::vector<std::string>({"unused","different"})&&r.targets.refineries==2,"unread metadata remains owned");});
    for(const std::string country:{"YuriCountry","Russians","Americans"," Unknown ",""}){m=seed();m.ai.country=country;compare("owner mask input untouched ["+country+"]",m,Operation::Targets,[country](const auto& v,const auto& r){const bool ordinary=country=="Americans";require(r.targets.slaveRefineries!=ordinary,"controlled original mask result");require(v.ai.country==country,"country no mutation");});}
}
void planCases(){
    auto m=seed();compare("base opening only",m,Operation::Plan,[](const auto&,const auto& r){require(r.plan==opening(),"original seven role opening");});
    for(int refin:{-2,0,1,2,3,7,12})for(int defense:{-3,0,1,2,6,25})for(int side:{-1,0,1,2}){m=seed();m.authored={defense,refin,false};m.side=side;compare("plan quantities "+std::to_string(refin)+","+std::to_string(defense)+","+std::to_string(side),m,Operation::Plan,[=](const auto& v,const auto& r){const auto extra=std::max(0,refin-1),def=std::max(0,defense);require(r.plan.size()==static_cast<std::size_t>(7+extra+def),"quantity additions exact");require(r.plan[0]==Role::Power&&r.plan[1]==Role::Refinery,"opening positions immutable");require(std::count(r.plan.begin(),r.plan.end(),Role::Refinery)==1+extra&&std::count(r.plan.begin(),r.plan.end(),Role::Defense)==def,"original role multiplicities");require(std::find(r.plan.begin(),r.plan.begin()+4,Role::Defense)==r.plan.begin()+4,"defense insertion starts after first four");require(v.rng==0x93281ULL&&v.productionSerial==77&&v.tick==123,"no global rng/tick/serial consumption");});}
    for(const std::string& owner:std::vector<std::string>{"Player One"," player one ","PLAYER ONE","other","",std::string("A\0B",3)}){m=seed();m.ai.owner=owner;m.authored={17,5,true};compare("owner salt ["+owner+"]",m,Operation::Plan,[owner](const auto& v,const auto& r){require(v.ai.owner==owner&&r.plan.size()==28,"owner input retained");});}
    m=seed();m.authored={25,4,false};auto n=m;n.ai.owner="PLAYER ONE";auto a=invoke(m,Operation::Plan,true),b=invoke(n,Operation::Plan,true);require(a.plan==b.plan,"trim/ASCII lower owner normalization");
    m=seed();m.authored={19,5,false};m.mutateOwnerOnSide=true;compare("owner hash read precedes side callback",m,Operation::Plan,[](const auto& v,const auto& r){require(v.ai.owner=="late-owner"&&r.plan.size()==30,"ordered identity callback mutation");});
    m=seed();m.authored={5,2,true};compare("plan needs identity reads only",m,Operation::Plan,[](const auto& v,const auto&){require(v.trace.calls==std::vector<std::string>({"side:Americans","difficulty:Medium"}),"no fake rules/root queries in no-Session API");});
}
void goalCases(){
    auto m=seed();compare("empty metrics goals",m,Operation::Goals,[](const auto& v,const auto&){require(v.goals.size()==8&&std::count(v.goals.begin(),v.goals.end(),Role::Defense)==1,"dedup multiple planned defenses");require(std::find(v.goals.begin(),v.goals.end(),Role::Shipyard)==v.goals.end(),"no new shipyard insertion");});
    for(bool super:{false,true})for(int structures:{0,7,8,20})for(int combat:{0,5,6,7}){m=seed();m.superweapons=super;m.metrics.structures=structures;m.metrics.combatUnits=combat;compare("super gate "+std::to_string(super)+std::to_string(structures)+std::to_string(combat),m,Operation::Goals,[=](const auto& v,const auto&){const bool wanted=super&&structures>=8&&combat>=6;require((std::find(v.goals.begin(),v.goals.end(),Role::Superweapon)!=v.goals.end())==wanted,"all three original super gates");});}
    m=seed();m.metrics.powerPlants=99;m.metrics.ordinaryRefineries=99;m.metrics.barracks=99;m.metrics.vehicleFactories=99;m.metrics.radars=99;m.metrics.defenses=99;m.metrics.techCenters=99;m.metrics.superweaponStructures=99;compare("all quotas consumed",m,Operation::Goals,[](const auto& v,const auto&){require(v.goals.empty(),"positive counters consume original ordered plan occurrences");});
    m=seed();m.goals={Role::Defense,Role::Power,Role::Power,static_cast<Role>(99)};compare("existing prefix and duplicate retained",m,Operation::Goals,[](const auto& v,const auto&){require(v.goals.size()==10&&std::vector<Role>(v.goals.begin(),v.goals.begin()+4)==std::vector<Role>({Role::Defense,Role::Power,Role::Power,static_cast<Role>(99)}),"never clear or dedup existing prefix");require(std::count(v.goals.begin(),v.goals.end(),Role::Power)==2&&std::count(v.goals.begin(),v.goals.end(),Role::Defense)==1,"avoid only newly appended duplicates");});
    m=seed();m.units.clear();m.metrics.ordinaryRefineries=99;m.metrics.slaveMiners=0;compare("slave quota uses mobile/deployed counter",m,Operation::Goals,[](const auto& v,const auto&){require(std::count(v.goals.begin(),v.goals.end(),Role::Refinery)==1,"ordinary refinery does not satisfy slave plan");});
    m=seed();m.metrics.ordinaryRefineries=2;m.metrics.slaveMiners=99;compare("ordinary quota ignores slave count",m,Operation::Goals,[](const auto& v,const auto&){require(std::find(v.goals.begin(),v.goals.end(),Role::Refinery)==v.goals.end(),"ordinary counter used exactly");});
    m=seed();m.metrics.powerPlants=-5;m.metrics.defenses=-2;compare("negative counters remain original",m,Operation::Goals,[](const auto& v,const auto&){require(std::count(v.goals.begin(),v.goals.end(),Role::Power)==1&&std::count(v.goals.begin(),v.goals.end(),Role::Defense)==1,"no new counter clamp");});
    m=seed();m.units.clear();m.mutateLimitsOnMining=true;compare("limits borrowed before lazy target reads",m,Operation::Goals,[](const auto& v,const auto&){require(!v.superweapons&&v.profile.attackThreshold==1000&&std::find(v.goals.begin(),v.goals.end(),Role::Superweapon)==v.goals.end(),"must observe live bool/threshold after mining getter");});
    for(int diff:{0,1,2})for(int side:{-1,0,1,2}){m=seed();m.difficulty=diff;m.side=side;m.metrics.defenses=1;m.metrics.ordinaryRefineries=1;compare("goal plan role order "+std::to_string(side)+std::to_string(diff),m,Operation::Goals,[](const auto& v,const auto&){std::vector<Role> sorted=v.goals;std::sort(sorted.begin(),sorted.end());require(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"new goals unique");});}
}
void liveBorrowAndAbi(){
    static_assert(std::is_same_v<Role,concord::runtime::session_detail::RuntimeComputerAiStructureRole>);
    static_assert(std::is_same_v<Targets,concord::runtime::session_detail::RuntimeComputerAiBaseTargets>);
    static_assert(std::is_same_v<std::underlying_type_t<Role>,int>);
    static_assert(sizeof(Role)==4&&alignof(Role)==4&&sizeof(Targets)==12&&alignof(Targets)==4);
    static_assert(std::is_standard_layout_v<Targets>&&std::is_trivially_copyable_v<Targets>);
    static_assert(offsetof(Targets,defenses)==0&&offsetof(Targets,refineries)==4&&offsetof(Targets,slaveRefineries)==8);
    Targets defaults;require(defaults.defenses==0&&defaults.refineries==1&&!defaults.slaveRefineries,"original target defaults");
    const std::array<Role,9> roles{Role::Power,Role::Refinery,Role::Barracks,Role::VehicleFactory,Role::Shipyard,Role::Radar,Role::Defense,Role::Tech,Role::Superweapon};for(std::size_t i=0;i<roles.size();++i)require(static_cast<int>(roles[i])==static_cast<int>(i),"all original enum values/order");
    auto m=seed();Root r(m);Queries q(r);const domain::RuntimeComputerBasePlanLimits limits{m.superweapons,m.profile.attackThreshold};active=&m;require(&q.baseBuildingRules()==&m.base&&&q.miningRules()==&m.mining,"actual model-owned rule references");m.base.extraRefineries[1]=8;auto t=domain::runtimeComputerAiBaseTargets(q,m.ai);require(t.refineries==9,"same query sees rule mutation");m.superweapons=false;domain::appendRuntimeComputerAiBasePlanGoals(limits,q,m.ai,m.metrics,m.goals);require(std::find(m.goals.begin(),m.goals.end(),Role::Superweapon)==m.goals.end(),"precreated limits see caller mutation");active=nullptr;
    std::cout<<"value-abi Role="<<sizeof(Role)<<"/"<<alignof(Role)<<" Targets="<<sizeof(Targets)<<"/"<<alignof(Targets)<<" offsets=0,4,8 enum=0..8 qualified-role="<<typeid(Role).name()<<" qualified-targets="<<typeid(Targets).name()<<'\n';
}
} // namespace
int main(){try{targetCases();planCases();goalCases();liveBorrowAndAbi();std::cout<<"base-plan checks="<<checks<<" model-states="<<models<<" injected-faults="<<faults<<" owned-state=exact lazy-order=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"base-plan contract: "<<e.what()<<'\n';return 1;}}
