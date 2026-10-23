#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void need(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}
WorldObjectInstance object(int id, WorldObjectKind kind, const std::string& type) {
    WorldObjectInstance o;
    o.instanceId=id; o.kind=kind; o.type=type; o.owner=id<10?"Source":"Target";
    o.alive=true; o.health=100; o.x=id; o.y=3; o.mission="Guard";
    return o;
}
std::string saved(const RuntimeSpecialMechanismState& state) {
    std::ostringstream out; state.save(out); return out.str();
}
void trace(const std::string& label, const RuntimeSpecialMechanismState& state) {
    std::cout << "STATE|" << label << '|' << state.stateHash() << '\n' << saved(state);
    for(const auto& s:state.renderSnapshots(107,15))
        std::cout<<"RENDER|"<<s.kind<<'|'<<s.objectId<<'|'<<s.targetObjectId<<'|'
                 <<s.owner<<'|'<<s.phase<<'|'<<s.progress<<'|'<<s.requiredProgress
                 <<'|'<<s.expireTick<<'|'<<s.resourceId<<'\n';
}
void actions(const std::vector<SpecialMechanismAction>& values) {
    for(const auto& a:values)
        std::cout<<"ACTION|"<<static_cast<int>(a.kind)<<'|'<<a.tick<<'|'
                 <<a.projectileId<<'|'<<a.objectId<<'|'<<a.targetObjectId<<'|'
                 <<a.owner<<'|'<<a.damage<<'|'<<a.warheadId<<'|'
                 <<a.forceDestruction<<'|'<<a.paralyzes<<'|'<<a.culling<<'|'
                 <<a.suppressDeathWeapon<<'|'<<a.stunTicks<<'|'<<a.cell.x<<'|'
                 <<a.cell.y<<'|'<<a.reason<<'|'<<a.replacesTerminatingHost<<'|'
                 <<a.sourceWasAttachedToHost<<'\n';
}
}
int main() try {
    IniFile ini;
    need(ini.loadFromString(
        "[General]\n[CombatDamage]\nC4Delay=.01\nC4Warhead=Super\n"
        "IvanTimedDelay=9\nIvanDamage=450\nIvanWarhead=IvanWH\n"
        "[InfantryTypes]\n0=C4UNIT\n1=IVAN\n2=ENGINEER\n3=VET\n"
        "[VehicleTypes]\n0=TANK\n[AircraftTypes]\n[BuildingTypes]\n0=BUILDING\n"
        "[Warheads]\n0=Super\n1=IvanWH\n2=DisarmWH\n"
        "[C4UNIT]\nStrength=100\nC4=yes\n[IVAN]\nStrength=100\nIvan=yes\n"
        "[ENGINEER]\nStrength=100\nEngineer=yes\n"
        "[VET]\nStrength=100\nVeteranAbilities=C4\n"
        "[TANK]\nStrength=100\nBombable=yes\n"
        "[BUILDING]\nStrength=1000\nCanC4=no\nBombable=no\n"
        "[Super]\nVerses=100%\n[IvanWH]\nVerses=100%\n"
        "[DisarmWH]\nBombDisarm=yes\n"),"fixture INI");
    RulesDatabase rules;
    need(rules.loadFromIni(ini),"fixture rules");
    std::vector<WorldObjectInstance> objects{
        object(2,WorldObjectKind::Infantry,"IVAN"),
        object(1,WorldObjectKind::Infantry,"C4UNIT"),
        object(3,WorldObjectKind::Infantry,"ENGINEER"),
        object(4,WorldObjectKind::Infantry,"VET"),
        object(30,WorldObjectKind::Structure,"BUILDING"),
        object(20,WorldObjectKind::Unit,"TANK"),
        object(10,WorldObjectKind::Unit,"TANK")};
    const SpecialEventSink sink=[](const SpecialMechanismEvent& e) {
        std::cout<<"EVENT|"<<e.kind<<'|'<<e.tick<<'|'<<e.objectId<<'|'
                 <<e.targetObjectId<<'|'<<e.value<<'|'<<e.cell.x<<'|'<<e.cell.y<<'|'<<e.reason<<'\n';
    };
    const auto reset=[&] { RuntimeSpecialMechanismState s; s.reset(rules.getSpecialMechanismRules()); return s; };
    std::string why;
    auto s=reset();
    need(s.tryPlantC4(objects,rules,1,30,100,15,sink,why),"C4 seed");
    need(s.tryAttachIvanBomb(objects,rules,2,20,100,15,sink,why),"Ivan20 seed");
    need(s.tryAttachIvanBomb(objects,rules,2,10,100,15,sink,why),"Ivan10 seed");
    need(s.timedBombs().at(10).detonateTick==109 && s.timedBombs().at(30).detonateTick==109,"shared deadline");
    need(!s.tryPlantC4(objects,rules,1,30,100,15,sink,why),"duplicate rejected");
    std::cout<<"REJECT|"<<why<<'\n';
    need(!s.tryDisarmTimedBomb(objects,rules,3,20,"IvanWH",101,sink,why),"wrong disarm rejected");
    std::cout<<"REJECT|"<<why<<'\n';
    trace("three-active",s);
    const auto initial=saved(s);
    auto restored=reset(); IniFile snapshot;
    need(snapshot.loadFromString(initial) && restored.load(snapshot,why),"current save restores");
    need(saved(restored)==initial && restored.stateHash()==s.stateHash(),"complete save/hash roundtrip");
    for(int tick=101;tick<=110;++tick) {
        if(tick==105) {
            need(s.tryDisarmTimedBomb(objects,rules,3,20,"DisarmWH",tick,sink,why),"early disarm");
            need(restored.tryDisarmTimedBomb(objects,rules,3,20,"DisarmWH",tick,{},why),"restored early disarm");
            s.onOwnerChanged(objects,2,"CapturedSource",tick,sink);
            restored.onOwnerChanged(objects,2,"CapturedSource",tick,{});
            s.onObjectRemoved(objects,2,tick,sink,"source removed");
            restored.onObjectRemoved(objects,2,tick,{},"source removed");
        }
        const auto a=s.tickTimedBombs(tick,sink);
        const auto b=restored.tickTimedBombs(tick,{});
        need(a.size()==b.size() && s.stateHash()==restored.stateHash() && saved(s)==saved(restored),"continuation state");
        if(tick==109) need(a.size()==2 && a[0].targetObjectId==10 && a[1].targetObjectId==30,"same tick ascending ID");
        else need(a.empty(),"early or repeated detonation");
        actions(a); trace("tick-"+std::to_string(tick),s);
    }
    for(int mode=0;mode<5;++mode) {
        auto r=reset(); auto world=objects;
        need(r.tryPlantC4(world,rules,1,30,0,15,sink,why),"removal C4");
        need(r.tryAttachIvanBomb(world,rules,2,20,0,15,sink,why),"removal Ivan");
        std::vector<SpecialMechanismAction> a;
        if(mode<2) {
            r.onObjectRemoved(world,30,1,sink,"C4 host removed",mode?nullptr:&a);
            r.onObjectRemoved(world,20,1,sink,"Ivan host removed",mode?nullptr:&a);
            need(a.size()==(mode?0U:1U),"death output pointer semantics");
        } else {
            if(mode==2) {world[4].alive=false;world[5].health=0;}
            if(mode==3) {std::erase_if(world,[](const auto& o){return o.instanceId==30 || o.instanceId==20;});}
            if(mode==4) {world[4].alive=false;world[5].alive=false;}
            r.reconcile(world,1,sink,mode==4?nullptr:&a);
            need(a.size()==(mode==2?1U:0U),"missing/dead reconciliation");
        }
        need(r.timedBombs().empty(),"removal clears records");
        actions(a);trace("removal-"+std::to_string(mode),r);
    }
    for(int version=1;version<=3;++version) {
        auto r=reset(); IniFile legacy;
        const std::string text="[SpecialMechanisms]\nVersion="+std::to_string(version)+
            "\n[SpecialTimedBombs]\nCount=2\n"
            "0=30|1|Source|C4|100|109|2147483647|Super|yes\n"
            "1=10|2|Source|Ivan|100|109|450|IvanWH|no\n"
            "[SpecialParasitePending]\nCount=0\n[SpecialParasites]\nCount=0\n"
            "[SpecialParasiteResolved]\nCount=0\n";
        need(legacy.loadFromString(text) && r.load(legacy,why),"legacy version "+std::to_string(version));
        trace("legacy-"+std::to_string(version),r);
        need(r.tickTimedBombs(108,sink).empty(),"legacy early quiet");
        const auto a=r.tickTimedBombs(109,sink);
        need(a.size()==2 && a[0].targetObjectId==10 && a[1].targetObjectId==30,"legacy same tick");
        actions(a);trace("legacy-after-"+std::to_string(version),r);
    }
    auto atomic=reset();
    need(atomic.tryAttachIvanBomb(objects,rules,2,10,100,15,sink,why),"atomic seed");
    atomic.spawnRadiationArea(2,"Source",{4,4},2,100,2,"IvanWH",100,true,{});
    const auto atomicBytes=saved(atomic); const auto atomicHash=atomic.stateHash();
    const std::vector<std::string> malformed{
        "[SpecialTimedBombs]\nCount=bad\n",
        "[SpecialTimedBombs]\nCount=1\n0=10|2|Source|Bad|100|109|450|IvanWH|no\n",
        "[SpecialTimedBombs]\nCount=2\n0=10|2|Source|Ivan|100|109|450|IvanWH|no\n1=10|2|Source|Ivan|100|109|450|IvanWH|no\n",
        "[SpecialTimedBombs]\nCount=1\n0=10|2|Source|Ivan|100|109|450|IvanWH|no\n[SpecialParasitePending]\nCount=invalid\n"};
    for(std::size_t i=0;i<malformed.size();++i) {
        IniFile invalid; need(invalid.loadFromString("[SpecialMechanisms]\nVersion=2\n"+malformed[i]),"bad INI parse");
        need(!atomic.load(invalid,why) && !why.empty(),"bad load rejection");
        need(saved(atomic)==atomicBytes && atomic.stateHash()==atomicHash,"whole SpecialMechanisms atomic rollback");
        std::cout<<"ATOMIC|"<<i<<'|'<<why<<'\n';trace("atomic-"+std::to_string(i),atomic);
    }
    auto vet=reset();objects[3].veterancy=1;
    need(vet.tryPlantC4(objects,rules,4,30,0,15,sink,why),"veteran C4 eligibility");
    trace("veteran",vet);
    std::cout<<"TimedBomb compatibility: same-tick order,early disarm,source removal,owner change,missing/dead targets,null action sink,legacy1/2/3,full save/hash,late failure rollback passed\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"TimedBomb compatibility failed: "<<e.what()<<'\n';return 1;}
