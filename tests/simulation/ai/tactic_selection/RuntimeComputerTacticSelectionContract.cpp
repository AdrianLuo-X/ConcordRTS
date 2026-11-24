#include "simulation/ai/RuntimeComputerTacticSelection.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

// Dependency models test only policy decisions and call phases. Actual World,
// original catalog/rules, save/replay and native consumers run separately.
namespace fixture {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;
namespace policy = concord::simulation::ai;
struct Economy { std::string owner = "Human"; int credits = 12345; int techLevel = 10; };
struct Authority { std::string canonicalOwner; bool localFacade = false; std::optional<Economy> remoteEconomy; };
struct Rules {
    ComputerTacticsRules tactics;
    std::map<std::string, BuildingType> buildings;
    const ComputerTacticsRules& getComputerTacticsRules() const { return tactics; }
};
struct World {
    std::vector<WorldObjectInstance> objects;
    const std::vector<WorldObjectInstance>& getObjects() const { return objects; }
};
struct SuperWeapons {
    std::vector<SuperWeaponSidebarSnapshot> snapshots;
    const std::vector<SuperWeaponSidebarSnapshot>& sidebarSnapshots(const std::string& owner);
};
struct Model {
    struct Config { int fixedTickHz = 30; bool superweapons = true; } config;
    int tick = 100;
    Rules rules;
    World world;
    Economy economy;
    RuntimeTriggerState triggers;
    std::unordered_map<std::string, RuntimeComputerAiState> computerAiByOwner;
    std::vector<Authority> humanHouseAuthorities;
    SuperWeapons superWeapons;
    std::map<std::string, RuntimeBuildCatalogItem> catalog;
    std::set<std::string> forbidden;
    std::vector<std::string> trace;
    int throwAt = -1;
    int ensureCalls = 0;
    int teamQueries = 0;
    int eraseTaskOnTeamQuery = -1;
    std::string taskToErase;
    RuntimeComputerAiState ai;
    RuntimeComputerTacticsState tactics;
    void visit(std::string text) {
        trace.push_back(std::move(text));
        if (static_cast<int>(trace.size()) == throwAt) throw std::runtime_error("dependency fault");
    }
};
thread_local Model* active = nullptr;
std::string canonical(const std::string& owner) { return toLowerAsciiLocal(trimAsciiLocal(owner)); }
int runtimeComputerMiningDifficultyIndex(const std::string& difficulty) {
    active->visit("difficulty:" + difficulty);
    const auto value = toUpperAsciiLocal(trimAsciiLocal(difficulty));
    return value == "EASY" ? 2 : (value == "HARD" || value == "BRUTAL") ? 0 : 1;
}
int runtimeLegacyScriptTicks(const Model& state, int ticks) {
    active->visit("legacy:" + std::to_string(ticks));
    return std::max(1, (std::max(1, ticks) * std::max(1, state.config.fixedTickHz) + 14) / 15);
}
int runtimeBasePlanningSideForCountry(const std::string& country) {
    active->visit("side:" + country);
    return country == "Russians" ? 1 : country == "YuriCountry" ? 2 : 0;
}
bool runtimeOwnerIsNeutralForAutoTarget(const std::string& owner) {
    active->visit("neutral:" + owner);
    const auto value = toUpperAsciiLocal(trimAsciiLocal(owner));
    return value.empty() || value == "NEUTRAL" || value == "CIVILIAN" || value == "SPECIAL";
}
bool runtimeOwnersAreHostileForAutoTarget(const Model&, const std::string& owner, const std::string& target) {
    active->visit("hostile:" + owner + ":" + target);
    // An explicit relation fixture, including a separately allied owner.
    const auto value = canonical(target);
    return canonical(owner) != value && value != "ally" && value != "neutral" && !value.empty();
}
bool runtimeComputerAiOwnerMatches(const WorldObjectInstance& object, const std::string& owner) {
    active->visit("owner:" + std::to_string(object.instanceId) + ":" + owner);
    return canonical(object.owner) == canonical(owner);
}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& object) {
    active->visit("cell:" + std::to_string(object.instanceId));
    return {object.x, object.y};
}
double runtimeCellDistance(const RuntimeCellPoint& first, const RuntimeCellPoint& second) {
    active->visit("distance:" + std::to_string(second.x) + ":" + std::to_string(second.y));
    const double dx = static_cast<double>(first.x) - second.x;
    const double dy = static_cast<double>(first.y) - second.y;
    return std::sqrt(dx * dx + dy * dy);
}
const BuildingType* findRuntimeBuildingTypeById(const Rules& rules, const std::string& id) {
    active->visit("power:" + id);
    const auto found = rules.buildings.find(id);
    return found == rules.buildings.end() ? nullptr : &found->second;
}
std::string runtimeCanonicalHumanHouseOwner(const std::string& owner) {
    active->visit("human:" + owner);
    return canonical(owner);
}
void ensureRuntimeSuperWeaponsInitialized(Model&) { active->visit("ensure-super"); ++active->ensureCalls; }
const std::vector<SuperWeaponSidebarSnapshot>& SuperWeapons::sidebarSnapshots(const std::string& owner) {
    active->visit("super:" + owner); return snapshots;
}
const RuntimeTeamType* findRuntimeTriggerTeamById(const Model& state, const std::string& id) {
    active->visit("team:" + id);
    if (++active->teamQueries == active->eraseTaskOnTeamQuery) active->triggers.taskForces.erase(active->taskToErase);
    const auto found = state.triggers.teams.find(id);
    return found == state.triggers.teams.end() ? nullptr : &found->second;
}
std::optional<RuntimeBuildCatalogItem> runtimeComputerAiBuildItem(const Model& state, const std::string& id, const std::string& owner) {
    active->visit("build:" + id + ":" + owner);
    const auto found = state.catalog.find(id);
    return found == state.catalog.end() ? std::nullopt : std::optional<RuntimeBuildCatalogItem>{found->second};
}
bool runtimeComputerAiItemAllowed(const Model& state, const RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item) {
    active->visit("allowed:" + ai.owner + ":" + item.typeId + ":" + item.requiredHouses + ":" + item.techTreeBlockedReason);
    return !state.forbidden.contains(item.typeId);
}
namespace original {
using RuntimeGameSessionState = Model;
#include "RuntimeComputerTacticSelectionOriginal.inl"
}
class Queries final : public policy::RuntimeComputerTacticSelectionQueries {
public:
    explicit Queries(const Model& state) : s(state) {}
    int tick() const override { return s.tick; }
    int fixedTickHz() const override { return s.config.fixedTickHz; }
    int techLevel() const override { return s.economy.techLevel; }
    bool superweaponsEnabled() const override { return s.config.superweapons; }
    const ComputerTacticsRules& rules() const override { return s.rules.getComputerTacticsRules(); }
    int difficultyIndex(const std::string& value) const override { return runtimeComputerMiningDifficultyIndex(value); }
    int legacyScriptTicks(int ticks) const override { return runtimeLegacyScriptTicks(s,ticks); }
    int countrySide(const std::string& value) const override { return runtimeBasePlanningSideForCountry(value); }
    const std::vector<WorldObjectInstance>& objects() const override { return s.world.getObjects(); }
    bool hostile(const std::string& first,const std::string& second) const override { return runtimeOwnersAreHostileForAutoTarget(s,first,second); }
    RuntimeCellPoint objectCell(const WorldObjectInstance& object) const override { return runtimeCellPointForObject(object); }
    double cellDistance(const RuntimeCellPoint& first,const RuntimeCellPoint& second) const override { return runtimeCellDistance(first,second); }
    bool neutral(const std::string& owner) const override { return runtimeOwnerIsNeutralForAutoTarget(owner); }
    bool ownerMatches(const WorldObjectInstance& object,const std::string& owner) const override { return runtimeComputerAiOwnerMatches(object,owner); }
    std::optional<int> buildingPower(const std::string& id) const override {
        const auto* building=findRuntimeBuildingTypeById(s.rules,id);
        return building ? std::optional<int>{building->power} : std::nullopt;
    }
    std::optional<int> enemyCredits(const std::string& enemy) const override {
        const auto other=s.computerAiByOwner.find(toLowerAsciiLocal(trimAsciiLocal(enemy)));
        if(other!=s.computerAiByOwner.end())return other->second.credits;
        for(const auto& authority:s.humanHouseAuthorities){
            if(runtimeCanonicalHumanHouseOwner(enemy)!=authority.canonicalOwner)continue;
            if(authority.localFacade)return s.economy.credits;
            return authority.remoteEconomy?std::optional<int>{authority.remoteEconomy->credits}:std::nullopt;
        }
        return enemy==s.economy.owner?std::optional<int>{s.economy.credits}:std::nullopt;
    }
    const std::map<std::string,RuntimeTaskForce>& taskForces() const override { return s.triggers.taskForces; }
    const std::map<std::string,RuntimeScriptType>& scripts() const override { return s.triggers.scripts; }
    const std::vector<RuntimeCampaignAiTriggerDefinition>& triggers() const override { return s.triggers.computerAiTriggers; }
    const RuntimeTeamType* teamType(const std::string& id) const override { return findRuntimeTriggerTeamById(s,id); }
    std::optional<RuntimeBuildCatalogItem> buildItem(const std::string& id,const std::string& owner) const override { return runtimeComputerAiBuildItem(s,id,owner); }
    bool itemAllowed(const RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& item) const override { return runtimeComputerAiItemAllowed(s,ai,item); }
private:const Model& s;
};
class Effects final : public policy::RuntimeComputerTacticSelectionEffects {
public:
    explicit Effects(Model& state):s(state){}
    void ensureSuperWeaponsInitialized() override { ensureRuntimeSuperWeaponsInitialized(s); }
    const std::vector<SuperWeaponSidebarSnapshot>& superWeaponSnapshots(const std::string& owner) override { return s.superWeapons.sidebarSnapshots(owner); }
private:Model& s;
};

std::string image;
void save(const std::string& value) { image+=std::to_string(value.size())+":"+value+";"; }
void save(int value) { save(std::to_string(value)); }
void save(bool value) { save(value?1:0); }
void save(double value) { save(std::to_string(std::bit_cast<std::uint64_t>(value))); }
void save(RuntimeCellPoint value) { save(value.x);save(value.y); }
void save(const RuntimeComputerTacticHistory& value) { save(value.weight);save(value.executions);save(value.successes);save(value.failures); }
void save(const RuntimeComputerTacticProduction& value) { save(value.teamInstanceId);save(value.typeId);save(value.remainingTicks);save(value.paidCost);save(value.totalTicks); }
void save(const RuntimeComputerTacticsState::CaptureDecision& value) { save(value.controllerId);save(value.startedTick);save(value.decision); }
template<class T> void sequence(const T& values) { save(static_cast<int>(values.size()));for(const auto& value:values)save(value); }
void save(const RuntimeComputerTacticTeam& value) {
    save(value.instanceId);save(value.triggerId);save(value.teamTypeId);save(value.targetOwner);
    sequence(value.objectIds);sequence(value.joiningObjectIds);sequence(value.missingMembers);
    save(value.createdTick);save(value.lastProgressTick);save(value.missionInstanceId);
    save(value.secondary);save(value.baseDefense);save(value.launched);save(value.outcomeReported);
    save(value.missionFinished);save(value.achievedGreatSuccess);
}
template<class T> void mapping(const T& values) { save(static_cast<int>(values.size()));for(const auto& [key,value]:values){save(key);save(value);} }
void save(const RuntimeComputerTacticsState& value) {
    save(value.nextTriggerTick);save(value.selectionSerial);save(value.nextTeamInstanceId);
    sequence(value.production);mapping(value.teams);mapping(value.history);mapping(value.captureDecisions);
}
void save(const RuntimeComputerAiState& value) {
    save(value.initialized);
    save(value.owner);
    save(value.country);
    save(value.difficulty);
    save(value.baseCenter);
    save(value.credits);
    save(value.nextThinkTick);
    save(value.nextIncomeTick);
    save(value.nextBuildTick);
    save(value.nextAttackTick);
    save(value.nextRepairTick);
    save(value.productionSerial);
    save(value.incomeEarned);
    save(value.creditsSpent);
    save(value.structuresBuilt);
    save(value.infantryBuilt);
    save(value.unitsBuilt);
    save(value.freeUnitsReceived);
    save(value.attackWaves);
    save(value.attacksOrdered);
    save(value.defenseOrders);
    save(value.tacticalOrders);
    save(value.repairsPerformed);
    save(value.stalledBuilds);
    save(value.lastTargetObjectId);
    save(value.lastBuildType);
    save(value.status);
}

std::string snapshot(const Model& s) {
    image.clear();save(s.ai);save(s.tactics);save(s.ensureCalls);save(s.teamQueries);
    save(static_cast<int>(s.triggers.taskForces.size()));
    for(const auto& [id,task]:s.triggers.taskForces){save(id);save(task.id);save(task.name);save(task.group);save(static_cast<int>(task.members.size()));for(const auto& member:task.members){save(member.count);save(member.unitId);}}
    sequence(s.trace);return image;
}
Model basic() {
    Model s;
    s.ai.owner="AI";s.ai.country="Americans";s.ai.difficulty="Hard";s.ai.baseCenter={1,1};s.ai.credits=401;s.ai.status="unchanged until publication";
    s.ai.initialized=true;s.ai.nextThinkTick=23;s.ai.nextIncomeTick=24;s.ai.nextBuildTick=25;s.ai.nextAttackTick=26;s.ai.nextRepairTick=27;
    s.ai.productionSerial=11;s.ai.incomeEarned=111;s.ai.creditsSpent=112;s.ai.structuresBuilt=12;s.ai.infantryBuilt=13;s.ai.unitsBuilt=14;
    s.ai.freeUnitsReceived=15;s.ai.attackWaves=16;s.ai.attacksOrdered=17;s.ai.defenseOrders=18;s.ai.tacticalOrders=19;s.ai.repairsPerformed=20;s.ai.stalledBuilds=21;s.ai.lastTargetObjectId=22;s.ai.lastBuildType="unchanged type";
    s.rules.tactics.teamDelays={7,8,9};s.rules.tactics.minimumDefensiveTeams={0,0,0};s.rules.tactics.maximumDefensiveTeams={10,10,10};
    WorldObjectInstance target;target.instanceId=20;target.alive=true;target.kind=WorldObjectKind::Structure;target.owner="Human";target.type="HQ";target.x=10;target.y=10;
    s.world.objects.push_back(target);target.instanceId=21;target.owner="Other";target.kind=WorldObjectKind::Unit;target.x=2;target.y=2;s.world.objects.push_back(target);
    BuildingType building;building.power=-50;s.rules.buildings["HQ"]=building;
    RuntimeTaskForce task;task.id="task";task.name="original task";task.members={{2,"TANK"},{1,"INF"}};s.triggers.taskForces[task.id]=task;
    RuntimeScriptType script;script.id="script";script.steps={{4,123},{49,0}};s.triggers.scripts[script.id]=script;
    RuntimeTeamType team;team.id="primary";team.taskForceId=task.id;team.scriptId=script.id;team.max=3;s.triggers.teams[team.id]=team;
    team.id="secondary";team.max=4;team.isBaseDefense=true;s.triggers.teams[team.id]=team;
    RuntimeCampaignAiTriggerDefinition trigger;trigger.id="trigger";trigger.name="Authored team";trigger.primaryTeamId="primary";trigger.owner="<all>";
    trigger.valid=trigger.enabled=trigger.availableInSkirmish=true;trigger.currentWeight=30.25;trigger.minimumWeight=0;trigger.maximumWeight=100;
    s.triggers.computerAiTriggers.push_back(trigger);
    RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.typeId="TANK";item.cost=123;item.requiredHouses="full original eligibility value";item.techTreeBlockedReason="sentinel fields retained";
    item.owner="AI";item.forbiddenHouses="forbidden";item.prerequisite="FACT";item.prerequisiteOverride="OVERRIDE";item.requiredFactory="FACT";item.buildLimit=7;item.label="Tank";item.techTreeKind="vehicle";item.buildTicks=777;item.powerDelta=-5;item.techLevel=10;item.secretLabInjected=true;
    s.catalog[item.typeId]=item;item.typeId="INF";item.kind=RuntimeBuildKind::Infantry;s.catalog[item.typeId]=item;
    SuperWeaponSidebarSnapshot sw;sw.superWeaponId="IronCurtainSpecial";sw.name="Iron Curtain";sw.state=SuperWeaponRuntimeState::Ready;sw.progress=0.7;s.superWeapons.snapshots.push_back(sw);
    sw.superWeaponId="ChronoSphereSpecial";sw.name="Chronosphere";s.superWeapons.snapshots.push_back(sw);
    s.tactics.selectionSerial=17;s.tactics.nextTeamInstanceId=41;
    s.tactics.production[1]={777,"reserved",19,300,400};
    s.tactics.captureDecisions[16]={19,20,4};
    s.tactics.history["unrelated"]={-0.0,7,8,9};
    return s;
}
struct Outcome { std::string result;std::string error;std::string state;std::vector<std::string> trace; RuntimeComputerAiState ai; RuntimeComputerTacticsState tactics; };
int checks=0,scenarios=0,faults=0;
void require(bool value,const std::string& label) {++checks;if(!value)throw std::runtime_error(label);}
Outcome execute(Model s,bool old,int mode,const std::string& enemy="Human") {
    active=&s;Queries queries(s);Effects effects(s);Outcome result;
    try {
        if(mode==0)result.result=(old?original::selectRuntimeComputerTactic(s,s.ai,s.tactics):policy::selectRuntimeComputerTactic(queries,effects,s.ai,s.tactics))?"true":"false";
        else if(mode==1)result.result=old?original::runtimeComputerTacticEnemy(s,s.ai):policy::runtimeComputerTacticEnemy(queries,s.ai);
        else if(mode==2)result.result=(old?original::runtimeComputerTacticCondition(s,s.ai,s.triggers.computerAiTriggers.front(),enemy):policy::runtimeComputerTacticCondition(queries,effects,s.ai,s.triggers.computerAiTriggers.front(),enemy))?"true":"false";
        else result.result=(old?original::runtimeComputerTacticTemplateBuildable(s,s.ai,s.triggers.teams.at("primary")):policy::runtimeComputerTacticTemplateBuildable(queries,s.ai,s.triggers.teams.at("primary")))?"true":"false";
    }catch(const std::out_of_range& e){result.error=std::string("out_of_range:")+e.what();}
    catch(const std::runtime_error& e){result.error=std::string("runtime_error:")+e.what();}
    result.state=snapshot(s);result.trace=s.trace;result.ai=s.ai;result.tactics=s.tactics;active=nullptr;return result;
}
Outcome compare(const std::string& label,const Model& source,int mode=0,const std::string& enemy="Human") {
    const auto old=execute(source,true,mode,enemy),now=execute(source,false,mode,enemy);++scenarios;
    require(old.result==now.result,label+": result");require(old.error==now.error,label+": exception");
    require(old.trace==now.trace,label+": dependency order");require(old.state==now.state,label+": entire AI/tactics/publication state");
    return now;
}
void faultMatrix(const std::string& label,const Model& source,int mode=0) {
    const auto complete=compare(label,source,mode);
    for(int n=1;n<=static_cast<int>(complete.trace.size());++n){auto s=source;s.throwAt=n;const auto result=compare(label+" fault "+std::to_string(n),s,mode);require(!result.error.empty(),label+": missing injected fault");++faults;}
}
void run() {
    for(int actual:{-10,0,10})for(int expected:{-10,0,10})for(int op=-2;op<=7;++op)
        require(original::runtimeComputerTacticCompare(actual,expected,op)==policy::runtimeComputerTacticCompare(actual,expected,op),"comparators");
    auto base=basic();faultMatrix("original full choice",base);
    const auto selected=compare("successful selection",base);require(selected.result=="true","expected original primary");
    require(selected.tactics.teams.size()==1 && selected.tactics.teams.begin()->second.missingMembers==std::vector<std::string>{"TANK","TANK","INF"} && selected.tactics.selectionSerial==18 && selected.ai.status=="assembling Authored team","original publication and exact member order");
    auto s=base;s.tactics.nextTriggerTick=101;require(compare("not due",s).trace.empty(),"early return reads no dependency");
    s=base;s.world.objects.clear();require(compare("no enemy writes interval",s).result=="false","no enemy");
    s=base;s.world.objects.front().alive=false;compare("only distant mobile enemy",s,1);
    s=base;s.world.objects.front().owner="Ally";compare("allied structure excluded",s,1);
    s=base;s.world.objects.front().owner="Neutral";compare("neutral structure excluded",s,1);
    s=base;auto tied=s.world.objects.front();tied.instanceId=4;tied.owner="TieWinner";s.world.objects.push_back(tied);
    require(compare("equal distance lower instance",s,1).result=="TieWinner","tie id");
    std::reverse(s.world.objects.begin(),s.world.objects.end());require(compare("reverse tie traversal",s,1).result=="TieWinner","tie traversal");
    for(int type=-2;type<=9;++type)for(int comparator=-1;comparator<=6;++comparator){
        s=base;auto& trigger=s.triggers.computerAiTriggers.front();trigger.conditionType=type;trigger.conditionComparator=comparator;trigger.conditionNumber=1;trigger.conditionObject=" hq ";
        compare("every condition and comparator",s,2);
        trigger.conditionObject="hq";compare("exact type normalization without trim",s,2);
        compare("condition empty enemy",s,2,"");
    }
    for(int condition:{0,1,7}){s=base;s.triggers.computerAiTriggers.front().conditionType=condition;s.triggers.computerAiTriggers.front().conditionObject="HQ";faultMatrix("count condition",s,2);}
    for(int condition:{2,3})for(int power:{-5,0,5}){s=base;s.triggers.computerAiTriggers.front().conditionType=condition;s.rules.buildings["HQ"].power=power;compare("power sign",s,2);}
    s=base;s.triggers.computerAiTriggers.front().conditionType=4;s.triggers.computerAiTriggers.front().conditionNumber=10000;
    compare("credits exact fallback",s,2);compare("credits fallback stays case-sensitive",s,2,"human");
    s.humanHouseAuthorities.push_back({"human",false,std::nullopt});compare("missing remote stops fallback",s,2);
    s.humanHouseAuthorities.back().remoteEconomy=Economy{"Human",1,10};compare("remote authority credits",s,2);
    s.humanHouseAuthorities.back().localFacade=true;compare("local human facade credits",s,2);
    s.computerAiByOwner["human"].credits=10001;compare("computer precedes human authority",s,2);
    s=base;s.triggers.computerAiTriggers.front().conditionType=4;s.humanHouseAuthorities={{"other",false,std::nullopt},{"human",true,std::nullopt}};faultMatrix("authority traversal",s,2);
    for(int condition:{5,6}){
        s=base;s.triggers.computerAiTriggers.front().conditionType=condition;faultMatrix("lazy superweapon condition",s,2);
        s.config.superweapons=false;require(compare("superweapon disabled",s,2).trace.empty(),"no eager superweapon init");
        s.config.superweapons=true;s.superWeapons.snapshots.clear();compare("empty actual sidebar",s,2);
        for(double progress:{0.699999,0.7,0.700001,std::numeric_limits<double>::quiet_NaN()}){
            s=base;s.triggers.computerAiTriggers.front().conditionType=condition;for(auto& sw:s.superWeapons.snapshots)sw.progress=progress;compare("minor ready threshold",s,2);
            for(auto& sw:s.superWeapons.snapshots)sw.state=SuperWeaponRuntimeState::Unavailable;compare("unavailable high progress",s,2);
        }
    }
    for(int variant=0;variant<18;++variant){
        s=base;auto& task=s.triggers.taskForces.at("task");auto& team=s.triggers.teams.at("primary");
        switch(variant){case 0:team.max=0;break;case 1:team.taskForceId="missing";break;case 2:task.members.clear();break;case 3:team.scriptId="missing";break;case 4:s.triggers.scripts.at("script").steps.clear();break;
        case 5:task.members[0].count=0;break;case 6:task.members[0].count=-1;break;case 7:task.members[0].count=101;break;case 8:s.catalog.erase("TANK");break;case 9:s.catalog["TANK"].kind=RuntimeBuildKind::Building;break;case 10:s.catalog["TANK"].kind=RuntimeBuildKind::Defense;break;case 11:s.forbidden.insert("TANK");break;
        case 12:task.members={{100,"TANK"},{100,"TANK"},{56,"TANK"}};break;case 13:task.members={{100,"TANK"},{100,"TANK"},{57,"TANK"}};break;case 14:team.max=-1;break;case 15:task.members={{0,"missing"}};break;case 16:team.max=1;break;case 17:task.members={{1,"INF"}};break;}
        if(variant==5){const auto invalid=compare("invalid member still does build lookup",s,3);require(invalid.trace==std::vector<std::string>{"build:TANK:AI"},"original eager lookup precedes invalid count");}
        faultMatrix("template branch "+std::to_string(variant),s,3);compare("selection template branch "+std::to_string(variant),s);
    }
    for(int gate=0;gate<21;++gate){
        s=base;auto& trigger=s.triggers.computerAiTriggers.front();
        switch(gate){case 0:trigger.valid=false;break;case 1:trigger.enabled=false;break;case 2:trigger.availableInSkirmish=false;break;case 3:trigger.difficultyEnabled[2]=false;break;case 4:trigger.techLevel=11;break;case 5:trigger.owner=" Russians ";break;case 6:trigger.owner=" AMERICANS ";break;
        case 7:trigger.side=2;break;case 8:trigger.side=1;break;case 9:trigger.currentWeight=0;break;case 10:trigger.currentWeight=-1;break;case 11:s.tactics.history[trigger.id]={0,9,8,7};break;case 12:trigger.conditionType=99;break;
        case 13:trigger.primaryTeamId="missing";break;case 14:trigger.secondaryTeamId="missing";break;case 15:trigger.secondaryTeamId=" <NoNe> ";break;case 16:s.rules.tactics.totalTeamCap={0,0,0};break;case 17:trigger.baseDefense=true;s.rules.tactics.maximumDefensiveTeams={0,0,0};break;
        case 18:trigger.currentWeight=std::numeric_limits<double>::quiet_NaN();break;case 19:trigger.currentWeight=std::numeric_limits<double>::infinity();break;case 20:s.tactics.history[trigger.id]={12.125,5,4,3};break;}
        compare("candidate gate "+std::to_string(gate),s);
    }
    for(const std::string difficulty:{"Easy","Medium","Hard","Brutal"," hard ","unknown"})for(int hz:{0,1,15,30,60}){
        s=base;s.ai.difficulty=difficulty;s.config.fixedTickHz=hz;compare("difficulty tick conversion",s);
        s.catalog.clear();compare("no-choice retry tick",s);
    }
    s=base;s.triggers.computerAiTriggers.front().secondaryTeamId="secondary";faultMatrix("two original teams",s);
    s=base;s.triggers.computerAiTriggers.front().secondaryTeamId="primary";s.triggers.teams["primary"].max=1;require(compare("same template max preflight and create",s).result=="true","same template primary still selected");
    s=base;s.triggers.computerAiTriggers.front().secondaryTeamId="secondary";s.eraseTaskOnTeamQuery=4;s.taskToErase="task";
    const auto partial=compare("secondary at throws after primary publication",s);require(!partial.error.empty(),"expected original at exception");
    require(partial.tactics.teams.size()==1 && partial.tactics.nextTeamInstanceId==43 && partial.tactics.history.at("trigger").executions==1 && partial.tactics.selectionSerial==18 && partial.ai.status==base.ai.status,"original secondary failure keeps primary/history/id and delays status");
    s=base;s.triggers.computerAiTriggers.front().secondaryTeamId="secondary";s.eraseTaskOnTeamQuery=3;s.taskToErase="task";compare("primary at throws after history and id",s);
    s=base;RuntimeComputerTacticTeam live;live.instanceId=1;live.teamTypeId="primary";live.objectIds={1,2};live.joiningObjectIds={3};live.missingMembers={"pending"};live.createdTick=99;live.launched=true;live.achievedGreatSuccess=true;
    s.tactics.teams[1]=live;s.triggers.teams["primary"].max=1;compare("existing template max",s);
    for(int existing=0;existing<=2;++existing){s=base;s.rules.tactics.useMinimumDefenseRule=true;s.rules.tactics.minimumDefensiveTeams={1,1,1};
        auto defense=s.triggers.computerAiTriggers.front();defense.id="defense";defense.baseDefense=true;defense.currentWeight=0.125;s.triggers.computerAiTriggers.push_back(defense);
        for(int n=0;n<existing;++n){live.instanceId=n+1;live.teamTypeId="unrelated";live.baseDefense=true;s.tactics.teams[live.instanceId]=live;}
        compare("defense priority and existing cap",s);}
    for(int serial=0;serial<64;++serial){s=base;s.tactics.selectionSerial=serial;s.tick=serial*11;s.ai.owner=serial%2?" AI ":"AI";
        for(int n=0;n<4;++n){auto t=s.triggers.computerAiTriggers.front();t.id="weighted-"+std::to_string(n);t.name=t.id;t.currentWeight=n+0.125;s.triggers.computerAiTriggers.push_back(t);}
        compare("ordered weighted stable hash",s);}
}
}
int main(){try{fixture::run();std::cout<<"Computer tactic selection: checks="<<fixture::checks<<" complete-states="<<fixture::scenarios<<" injected-faults="<<fixture::faults<<" original-body/reference=yes query-order=yes no-assets=yes model-only=yes\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
