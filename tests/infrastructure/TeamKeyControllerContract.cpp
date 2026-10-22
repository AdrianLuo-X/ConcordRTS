#include "ui/input/RuntimeTeamKeyController.h"
#include "ui/input/RuntimePlayerCommands.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::size_t checks = 0, failures = 0, differentialCases = 0;
std::size_t bindingReads = 0, eligibilityReads = 0, traceEvents = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        if (failures <= 24) std::cerr << "FAIL " << label << '\n';
    }
}
std::string idsText(const std::vector<int>& ids) {
    std::ostringstream out;
    for (int id : ids) out << id << ',';
    return out.str();
}
struct Object {
    RuntimeTeamKeyCell cell{0,0};
    bool exists = true, alive = true, owned = true, limbo = false, visible = true;
    bool cellAvailable = true;
};
struct Fixture {
    RuntimePlayerLocalCommands local;
    std::vector<int> selection;
    std::map<int,Object> objects;
    std::map<std::string,std::int32_t> bindings;
    std::map<std::pair<int,int>,bool> eligibilityOverride;
    std::map<int,int> eligibilityCounts;
    bool selectionAllowed = true, selectedBeacon = true;
    std::string status = "old status";
    std::vector<std::string> trace;
    std::vector<int> lastApplied;
    std::vector<RuntimeTeamKeyCell> centers;
    std::size_t bindingCount = 0, eligibilityCount = 0, applyCount = 0;
    std::size_t clearCount = 0, assignedCount = 0, cellCount = 0;
    std::optional<std::size_t> replaceSelectionAtBinding;
    std::vector<int> replacementSelection;
    std::set<int> hideAtApply, removeCellAtApply;
    bool overwriteLastAtApply = false;
};
std::string lastText(const Fixture& f) {
    return std::to_string(f.local.lastSelectionKey) + "/"
        + std::to_string(f.local.lastSelectionAtMs);
}
class FixtureHost final : public RuntimeTeamKeyHost {
public:
    explicit FixtureHost(Fixture& fixture) : f(fixture) {}
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        ++f.bindingCount;
        const auto it = f.bindings.find(action);
        const auto value = it == f.bindings.end() ? fallback : it->second;
        emit("binding:" + action + ":" + std::to_string(fallback)
            + "=" + std::to_string(value));
        if (f.replaceSelectionAtBinding == f.bindingCount) {
            f.selection = f.replacementSelection;
            emit("binding-selection:" + idsText(f.selection));
        }
        return value;
    }
    bool selectable(int id) override {
        ++f.eligibilityCount;
        const int occurrence = ++f.eligibilityCounts[id];
        const auto it = f.objects.find(id);
        bool value = f.selectionAllowed && it != f.objects.end()
            && it->second.exists && it->second.alive && it->second.owned
            && !it->second.limbo && it->second.visible;
        if (const auto o = f.eligibilityOverride.find({id,occurrence});
            o != f.eligibilityOverride.end()) value = o->second;
        emit("eligible:" + std::to_string(id) + ":"
            + std::to_string(occurrence) + "=" + std::to_string(value));
        return value;
    }
    void applySelection(const std::vector<int>& ownedResult) override {
        ++f.applyCount;
        f.lastApplied = ownedResult;
        emit("apply-owned:" + idsText(ownedResult));
        for (int id : f.hideAtApply) f.objects.at(id).visible = false;
        // This contract host exposes the original two eligibility passes at the
        // narrow sink boundary. The actual Shell gate is covered separately.
        std::erase_if(f.selection, [&](int id) { return !selectable(id); });
        std::vector<int> eligible;
        for (int id : ownedResult) if (selectable(id)) eligible.push_back(id);
        f.selection = std::move(eligible);
        for (int id : f.removeCellAtApply) f.objects.at(id).cellAvailable = false;
        if (f.overwriteLastAtApply) {
            f.local.lastSelectionKey = 9876;
            f.local.lastSelectionAtMs = 777777;
        }
        emit("apply-result:" + idsText(f.selection));
    }
    void clearSelectedBeacon() override {
        ++f.clearCount; emit("beacon-clear"); f.selectedBeacon = false;
    }
    std::optional<RuntimeTeamKeyCell> objectCell(int id) override {
        ++f.cellCount;
        const auto it = f.objects.find(id);
        if (it == f.objects.end() || !it->second.exists || !it->second.cellAvailable) {
            emit("cell:" + std::to_string(id) + "=missing"); return std::nullopt;
        }
        const auto cell = it->second.cell;
        emit("cell:" + std::to_string(id) + "=" + std::to_string(cell.x)
            + "," + std::to_string(cell.y));
        return cell;
    }
    void centerCamera(RuntimeTeamKeyCell cell) override {
        emit("center:" + std::to_string(cell.x) + "," + std::to_string(cell.y));
        f.centers.push_back(cell);
    }
    void teamAssigned(std::int32_t number) override {
        ++f.assignedCount; emit("assigned:" + std::to_string(number));
        f.status = "team " + std::to_string(number) + " assigned";
    }
private:
    Fixture& f;
    void emit(const std::string& event) { f.trace.push_back(event + "|last=" + lastText(f)); }
};

Fixture seed() {
    Fixture f;
    f.selection = {4,1,1,6,3,2};
    for (auto& team : f.local.teams) team = {2,1,1,3,4,5,6,7,404};
    f.objects[1].cell={6,8}; f.objects[2].cell={10,12};
    f.objects[3].alive=false; f.objects[4].owned=false;
    f.objects[5].limbo=true; f.objects[6].visible=false; f.objects[7].exists=false;
    f.local.planning=true; f.local.selectedWaypoint=123;
    f.local.followTargeting=true; f.local.rallyTargeting=true;
    f.local.cameraFollow=true; f.local.showCoordinates=true;
    f.local.lastSelectionKey='T'|runtimePlayerShiftKey;
    f.local.lastSelectionAtMs=12345; f.local.veterancyRank=2; f.local.healthRank=1;
    f.local.views[1]=RuntimeMultiplayerGameplayCell{-7,19};
    return f;
}
std::string stateText(const Fixture& f) {
    std::ostringstream out;
    for (const auto& team : f.local.teams) out << '[' << idsText(team) << ']';
    for (const auto& view : f.local.views) {
        if (view) out << view->x << ',' << view->y; else out << "none";
        out << ';';
    }
    out << f.local.planning << ',' << f.local.selectedWaypoint << ','
        << f.local.followTargeting << ',' << f.local.rallyTargeting << ','
        << f.local.cameraFollow << ',' << f.local.showCoordinates << ','
        << f.local.lastSelectionKey << ',' << f.local.lastSelectionAtMs << ','
        << f.local.veterancyRank << ',' << f.local.healthRank << '|'
        << idsText(f.selection) << '|' << f.selectedBeacon << '|' << f.status << '|'
        << idsText(f.lastApplied) << '|' << f.bindingCount << ',' << f.eligibilityCount
        << ',' << f.applyCount << ',' << f.clearCount << ',' << f.assignedCount
        << ',' << f.cellCount;
    for (const auto& c : f.centers) out << "|center=" << c.x << ',' << c.y;
    for (const auto& [id,o] : f.objects) out << "|object=" << id << ',' << o.cell.x
        << ',' << o.cell.y << ',' << o.exists << o.alive << o.owned << o.limbo
        << o.visible << o.cellAvailable;
    for (const auto& [id,count] : f.eligibilityCounts) out << "|reads=" << id << ',' << count;
    return out.str();
}

// Frozen test-only adapter of RuntimePlayerCommandsUi.inl's complete old Team
// loop. Only its native queries/sinks are replaced with the narrow host; the
// real teamCreate/teamSelect and original arithmetic remain independent calls.
bool frozenOldTeamLoop(const RuntimeTeamKeyInput& input, Fixture& state) {
    FixtureHost host(state);
    const auto code=input.code; const bool repeated=input.repeated; const auto now=input.nowMs;
    const auto matches=[&](const std::string& action,int fallback) {
        return code == host.binding(action,fallback);
    };
    const auto selectable=[&](int id) { return host.selectable(id); };
    auto& local=state.local;
    for (int number = 1; number <= 10; ++number) {
        const int digit = number == 10 ? '0' : '0' + number;
        const std::string suffix = "_" + std::to_string(number);
        const int index = number - 1;
        if (matches("TeamCreate" + suffix, digit | runtimePlayerControlKey)) {
            if (!repeated) local.teamCreate(index, state.selection, selectable);
            host.teamAssigned(number);
            return true;
        }
        const bool add = matches("TeamAddSelect" + suffix, digit | runtimePlayerShiftKey);
        const bool center = matches("TeamCenter" + suffix, digit | runtimePlayerAltKey);
        if (matches("TeamSelect" + suffix, digit) || add || center) {
            if (repeated) return true;
            const auto selected = local.teamSelect(index, state.selection, add, selectable);
            const bool twice = local.lastSelectionKey == digit && runtimeUiWithinDoubleSelection(now, local.lastSelectionAtMs);
            host.applySelection(selected);
            host.clearSelectedBeacon();
            if ((center || twice) && !selected.empty()) {
                long long x = 0, y = 0;
                for (int id : selected) if (const auto object = host.objectCell(id)) {
                    x += object->x; y += object->y;
                }
                host.centerCamera({static_cast<int>(x / selected.size()),
                    static_cast<int>(y / selected.size())});
            }
            local.lastSelectionKey = digit; local.lastSelectionAtMs = now;
            return true;
        }
    }
    return false;
}

struct Expected {
    std::optional<bool> handled;
    std::optional<std::size_t> bindings, assigned, applied, centered;
    std::optional<int> lastKey;
    std::optional<RuntimeUiTimeMs> lastAt;
};
Fixture compareCase(const std::string& label,const Fixture& start,
    const RuntimeTeamKeyInput& input,const Expected& expected={}) {
    Fixture actual=start,old=start;
    FixtureHost host(actual);
    const bool result=handleRuntimeTeamKey(input,actual.local,actual.selection,host);
    const bool oldResult=frozenOldTeamLoop(input,old);
    ++differentialCases;
    bindingReads+=actual.bindingCount-start.bindingCount;
    eligibilityReads+=actual.eligibilityCount-start.eligibilityCount;
    traceEvents+=actual.trace.size()-start.trace.size();
    check(result==oldResult,label+" handled differential");
    check(actual.trace==old.trace,label+" full ordered callback trace");
    check(stateText(actual)==stateText(old),label+" full owner/selection/host state");
    if(expected.handled)check(result==*expected.handled,label+" expected handled");
    if(expected.bindings)check(actual.bindingCount-start.bindingCount==*expected.bindings,label+" exact binding count");
    if(expected.assigned)check(actual.assignedCount-start.assignedCount==*expected.assigned,label+" assigned count");
    if(expected.applied)check(actual.applyCount-start.applyCount==*expected.applied,label+" apply count");
    if(expected.centered)check(actual.centers.size()-start.centers.size()==*expected.centered,label+" center count");
    if(expected.lastKey)check(actual.local.lastSelectionKey==*expected.lastKey,label+" last key");
    if(expected.lastAt)check(actual.local.lastSelectionAtMs==*expected.lastAt,label+" last time");
    return actual;
}
Expected actionExpected(int number,int action,bool repeated) {
    const int digit=number==10?'0':'0'+number;
    Expected e; e.handled=action>=0;
    e.bindings=action<0?40:static_cast<std::size_t>((number-1)*4+(action==0?1:4));
    e.assigned=action==0?1:0;
    e.applied=action>0&&!repeated?1:0;
    e.centered=action==3&&!repeated?1:0;
    if(action>0&&!repeated){e.lastKey=digit;e.lastAt=50000;}
    return e;
}
const char* const actionNames[]={"TeamCreate","TeamSelect","TeamAddSelect","TeamCenter"};
int digitFor(int number){return number==10?'0':'0'+number;}

void matrixCases() {
    for(int number=1;number<=10;++number) for(int mask=0;mask<8;++mask) for(bool repeated:{false,true}) {
        const auto label="default-n"+std::to_string(number)+"-m"+std::to_string(mask)+"-r"+std::to_string(repeated);
        const int code=runtimePlayerKeyCode(digitFor(number),(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        const int action=mask==2?0:mask==0?1:mask==1?2:mask==4?3:-1;
        const auto actual=compareCase(label,seed(),{code,repeated,50000},actionExpected(number,action,repeated));
        if(action==0) {
            check(actual.status=="team "+std::to_string(number)+" assigned",label+" assigned text including repeat");
            if(!repeated)check(actual.local.teams[static_cast<std::size_t>(number-1)]==std::vector<int>({1,2}),label+" real teamCreate sorted eligible IDs");
            else check(actual.eligibilityCount==0,label+" repeat create does not query eligibility");
        }
        if(action>0&&repeated)check(actual.eligibilityCount==0&&actual.selectedBeacon,label+" repeat select has no state/callback effects");
        if(action>0&&!repeated)check(actual.lastApplied==std::vector<int>({1,2}),label+" real teamSelect owned sorted result");
    }
    for(int number=1;number<=10;++number) for(int action=0;action<4;++action)
        for(int mask=0;mask<8;++mask) for(bool repeated:{false,true}) {
            auto f=seed();
            const int code=runtimePlayerKeyCode(0x80+number,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
            f.bindings[std::string(actionNames[action])+"_"+std::to_string(number)]=code;
            compareCase("rebound-n"+std::to_string(number)+"-a"+std::to_string(action)+"-m"+std::to_string(mask)+"-r"+std::to_string(repeated),f,{code,repeated,50000},actionExpected(number,action,repeated));
        }
    // Normalization itself remains in the real caller before Previous/Next.
    // Exercise its frozen value expression here; do not move it into controller.
    for(int number=1;number<=10;++number) for(int mask=0;mask<8;++mask) for(bool repeated:{false,true}) {
        const int low=number==10?0x60:0x60+number;
        const int raw=runtimePlayerKeyCode(low,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        const int normalized=(raw&~0xff)|('0'+(raw&0xff)-0x60);
        check((normalized&0xff)==digitFor(number),"keypad value keeps digit and modifiers");
        check((normalized&~0xff)==(raw&~0xff),"keypad value modifier bits retained");
        const int action=mask==2?0:mask==0?1:mask==1?2:mask==4?3:-1;
        compareCase("normalized-keypad-n"+std::to_string(number)+"-m"+std::to_string(mask)+"-r"+std::to_string(repeated),seed(),{normalized,repeated,50000},actionExpected(number,action,repeated));
        compareCase("raw-keypad-not-normalized-here",seed(),{raw,repeated,50000},actionExpected(number,-1,repeated));
    }
}
void collisionAndBorrowCases() {
    for(int number=1;number<=10;++number) for(bool repeated:{false,true}) {
        auto f=seed(); const int code=0x7c;
        const std::string suffix="_"+std::to_string(number);
        for(const auto* name:actionNames)f.bindings[std::string(name)+suffix]=code;
        compareCase("all-four-collision-create-first-n"+std::to_string(number),f,{code,repeated,50000},actionExpected(number,0,repeated));
        f.bindings.erase("TeamCreate"+suffix);
        Expected e=actionExpected(number,3,repeated);
        const auto selected=compareCase("add-center-select-all-read-n"+std::to_string(number),f,{code,repeated,50000},e);
        check(selected.bindingCount==static_cast<std::size_t>(number*4),"add match never skips center or select binding query");
    }
    auto f=seed(); f.bindings["TeamCreate_1"]=0x7e;f.bindings["TeamSelect_10"]=0x7e;
    compareCase("earlier-number-priority",f,{0x7e,false,50000},actionExpected(1,0,false));
    f=seed();f.local.teams[0]={1};f.selection={2};
    f.bindings["TeamAddSelect_1"]=0x7e;f.bindings["TeamCenter_1"]=0x7e;f.bindings["TeamSelect_1"]=0x7e;
    const auto combined=compareCase("add-and-center-collision",f,{0x7e,false,50000},actionExpected(1,3,false));
    check(combined.lastApplied==std::vector<int>({1,2})&&combined.centers.size()==1,"colliding add remains additive while center remains true");
    f=seed();f.selection.clear();f.replaceSelectionAtBinding=1;f.replacementSelection={2,2};
    const auto created=compareCase("binding-lazy-mutation-current-borrow",f,{'1'|runtimePlayerControlKey,false,50000},actionExpected(1,0,false));
    check(created.local.teams[0]==std::vector<int>({2}),"create reads current vector after its binding query");
    f=seed();f.local.teams[0]={1};f.selection.clear();f.replaceSelectionAtBinding=2;f.replacementSelection={2};
    const auto added=compareCase("binding-add-current-borrow",f,{'1'|runtimePlayerShiftKey,false,50000},actionExpected(1,2,false));
    check(added.lastApplied==std::vector<int>({1,2}),"add uses changed current selection rather than entry snapshot");
    f=seed();f.local.teams[0]={1,2};f.selection.clear();f.eligibilityOverride[{1,2}]=false;
    const auto filtered=compareCase("second-eligibility-fresh-owned-result",f,{'1'|runtimePlayerAltKey,false,50000},actionExpected(1,3,false));
    check(filtered.lastApplied==std::vector<int>({1,2})&&filtered.selection==std::vector<int>({2}),"owned select output survives second filtering unchanged");
    check(filtered.centers.size()==1&&filtered.centers[0].x==8&&filtered.centers[0].y==10,"camera uses original owned IDs after apply filtering");
    f=seed();f.selection.clear();f.local.teams[0].clear();
    Expected empty=actionExpected(1,3,false);empty.centered=0;
    const auto cleared=compareCase("empty-team-still-apply-beacon-last",f,{'1'|runtimePlayerAltKey,false,50000},empty);
    check(cleared.selection.empty()&&!cleared.selectedBeacon&&cleared.clearCount==1,"empty result keeps real sink ordering");
    for(bool repeated:{false,true}) {
        f=seed();f.selectionAllowed=false;
        Expected observer=actionExpected(1,1,repeated);observer.centered=0;
        const auto refused=compareCase("eligibility-owner-refuses-all",f,{'1',repeated,50000},observer);
        if(!repeated)check(refused.selection.empty()&&refused.local.teams[0].empty(),"all ineligible team entries pruned by existing owner");
    }
    for(const int code:{std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),0,-1}) {
        f=seed();compareCase("unknown-exact-code-no-sanitize",f,{code,false,50000},actionExpected(1,-1,false));
        f.bindings["TeamCenter_10"]=code;
        compareCase("rebound-exact-extreme-code",f,{code,false,50000},actionExpected(10,3,false));
    }
}
void timingAndSharedOwnerCases() {
    struct TimeCase {RuntimeUiTimeMs now,last;bool twice;};
    constexpr auto max=std::numeric_limits<RuntimeUiTimeMs>::max();
    const TimeCase cases[]={{1499,1000,true},{1500,1000,true},{1501,1000,false},
        {0,0,true},{99,100,false},{0,max,true},{499,max,true},{500,max,false},
        {max,0,false},{max,max,true},{0,max-499,true},{1,max-499,false}};
    for(int number=1;number<=10;++number) for(const auto& t:cases) {
        auto f=seed();const int digit=digitFor(number);f.selection.clear();
        f.local.teams[static_cast<std::size_t>(number-1)]={1,2};
        f.local.lastSelectionKey=digit;f.local.lastSelectionAtMs=t.last;
        Expected e=actionExpected(number,1,false);e.centered=t.twice?1:0;e.lastAt=t.now;
        compareCase("double-selection-n"+std::to_string(number)+"-now"+std::to_string(t.now)+"-last"+std::to_string(t.last),f,{digit,false,t.now},e);
        f.local.lastSelectionKey=digit|runtimePlayerControlKey;e.centered=0;
        compareCase("last-team-compares-digit-not-full-code",f,{digit,false,t.now},e);
    }
    auto f=seed();f.local.teams[0]={1,2};f.selection.clear();
    f.local.lastSelectionKey='1';f.local.lastSelectionAtMs=1000;f.overwriteLastAtApply=true;
    Expected e=actionExpected(1,1,false);e.lastAt=1500;e.centered=1;
    const auto callbacks=compareCase("twice-before-sink-last-write-after-sink",f,{'1',false,1500},e);
    check(std::find(callbacks.trace.begin(),callbacks.trace.end(),"apply-owned:1,2,|last=49/1000")!=callbacks.trace.end(),"apply callback observes original last fields");
    check(std::find(callbacks.trace.begin(),callbacks.trace.end(),"center:8,10|last=9876/777777")!=callbacks.trace.end(),"twice decision precedes callback mutation while final fields follow camera");
    // Use the existing owner quickSelect, then the caller's shared last fields.
    // This is an owner-level cross-consumer check; full quickSelect UI remains
    // outside this controller and is exercised by the separate native fixture.
    for(int quickCode:{int('T'),int('T'|runtimePlayerShiftKey),int('1')}) {
        f=seed();f.local.teams[0]={1,2};f.selection={1};
        const std::vector<RuntimePlayerSelectionCandidate> candidates={
            {1,"TYPE",0,true,true,100,100},{2,"TYPE",0,true,true,100,100}};
        f.selection=f.local.quickSelect(RuntimePlayerQuickSelection::Type,f.selection,candidates,false,false,false);
        f.local.lastSelectionKey=quickCode;f.local.lastSelectionAtMs=1000;
        f.trace.push_back("existing-quick-owner:last="+lastText(f));
        e=actionExpected(1,1,false);e.lastAt=1500;e.centered=quickCode=='1'?1:0;
        auto first=compareCase("shared-quick-last-code"+std::to_string(quickCode),f,{'1',false,1500},e);
        first.overwriteLastAtApply=false;e.lastAt=2000;e.centered=1;
        compareCase("shared-team-next-500",first,{'1',false,2000},e);
    }
}
void arithmeticCases() {
    check(sizeof(int)==4&&sizeof(long long)==8&&sizeof(std::size_t)==8,"current production x64 arithmetic profile");
    auto make=[](std::vector<RuntimeTeamKeyCell> cells) {
        Fixture f=seed();f.selection.clear();f.local.teams[0].clear();f.objects.clear();
        int id=0;for(const auto& cell:cells){++id;f.objects[id].cell=cell;f.local.teams[0].push_back(id);}return f;
    };
    struct CellCase {const char* name;std::vector<RuntimeTeamKeyCell> cells;RuntimeTeamKeyCell expected;};
    const auto min=std::numeric_limits<std::int32_t>::min();
    const auto max=std::numeric_limits<std::int32_t>::max();
    const std::vector<CellCase> cases={
        {"positive-truncate",{{0,1},{5,6}}, {2,3}},
        {"negative-unsigned-denominator",{{-1,-9},{0,-4}},{-1,-7}},
        {"three-negative-current-narrowing",{{-1,-1},{-1,-1},{-1,-1}},{1431655764,1431655764}},
        {"int-max-sum-no-signed64-overflow",{{max,max},{max,max}},{max,max}},
        {"int-min-sum-current-narrowing",{{min,min},{min,min}},{min,min}},
        {"mixed-extrema-current-narrowing",{{min,max},{max,min}},{-1,-1}},
        {"zeros",{{0,0},{0,0}},{0,0}}
    };
    for(const auto& c:cases) {
        const auto f=make(c.cells);
        const auto result=compareCase(std::string("original-center-")+c.name,f,{'1'|runtimePlayerAltKey,false,50000},actionExpected(1,3,false));
        check(result.centers.size()==1&&result.centers[0].x==c.expected.x&&result.centers[0].y==c.expected.y,std::string(c.name)+" independent fixed expected cell");
    }
    auto f=make({{6,8},{10,12}});f.removeCellAtApply={2};
    auto missing=compareCase("missing-keeps-selected-denominator",f,{'1'|runtimePlayerAltKey,false,50000},actionExpected(1,3,false));
    check(missing.lastApplied==std::vector<int>({1,2})&&missing.centers.size()==1&&missing.centers[0].x==3&&missing.centers[0].y==4,"one missing cell still divided by selected size two");
    f.removeCellAtApply={1,2};
    missing=compareCase("all-missing-still-center-zero",f,{'1'|runtimePlayerAltKey,false,50000},actionExpected(1,3,false));
    check(missing.centers.size()==1&&missing.centers[0].x==0&&missing.centers[0].y==0,"nonempty owned IDs all missing yields original zero center");
    f=make({});Expected empty=actionExpected(1,3,false);empty.centered=0;
    compareCase("empty-never-divides",f,{'1'|runtimePlayerAltKey,false,50000},empty);
}
} // namespace

int main() {
    matrixCases();collisionAndBorrowCases();timingAndSharedOwnerCases();arithmeticCases();
    if(failures){std::cerr<<"Team-key contract failed: "<<failures<<"/"<<checks<<"\n";return 1;}
    std::cout<<"Team-key contract passed: checks="<<checks<<" differentialCases="<<differentialCases
        <<" bindingReads="<<bindingReads<<" eligibilityReads="<<eligibilityReads
        <<" traceEvents="<<traceEvents
        <<" groups=10 actions=4 modifierCombinations=8 sameProductionCpp=1 existingTeamOwner=1"
        <<" secondEligibilityHostBoundary=1 sharedQuickOwner=1 originalUnsignedCentroid=1"
        <<" nativeWindowRouting=not-covered nativePhysicalModifierMatrix=not-run"
        <<" signedSumOverflow=not-covered otherArithmeticProfiles=not-run\n";
    return 0;
}
