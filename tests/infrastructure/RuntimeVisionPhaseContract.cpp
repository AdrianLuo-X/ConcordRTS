#include "simulation/vision/RuntimeVisionPhase.h"
#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "world/queries/RuntimeHistoryQuery.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include "core/io/IniFile.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINSOCKAPI_)
#error H036 standard phase contract included a Windows surface
#endif
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::toLowerAsciiLocal;

// Targeted phase differential with real production World/Rules/Vision values.
// Recording owner services do not represent a complete production Session.
struct FixtureSession {
    GameWorld world;
    RulesDatabase rules;
    bool rulesLoaded=true, visionEnabled=true;
    RuntimeVisionSystem vision;
    RuntimeConfig config;
    struct Economy { std::string owner=" Alpha "; } economy;
    struct Triggers { std::map<int,RuntimePublicBuildingLocation> publicBuildingLocations; } triggers;
    struct Performance { double lastSimVisionMs=7.0; } perf;
    int tick=1;
    bool landRush=false, share=false;
    RuntimeMultiplayerGameplayCell playerLastRadarCell{};
    std::string radarTerrainKey="old-terrain";
    bool projectedObjectsCacheValid=true, repaintRequested=false;
};
struct RecordingOperations final : RuntimeVisionPhaseOperations {
    FixtureSession& s;
    int failAt, calls=0;
    std::vector<std::string> trace;
    RecordingOperations(FixtureSession& value,int fail) : s(value),failAt(fail) {}
    void mark(std::string event) {
        trace.push_back(std::move(event));
        if (++calls==failAt) throw std::runtime_error("planned original helper boundary");
    }
    std::string ownerForSlot(std::size_t slot) override { mark("slot:"+std::to_string(slot)); return "Slot"+std::to_string(slot); }
    bool isLandRushMode() override { mark("land-rush"); return s.landRush; }
    bool ownersAreAllied(const std::string& left,const std::string& right) override {
        mark("alliance:"+left+":"+right); return s.share || left==right;
    }
    std::string canonicalHumanOwner(const std::string& owner) override {
        mark("canonical:"+owner);
        // Supplied deterministic service data, not a second production
        // canonical-owner algorithm. Actual helpers stay native owner-bound.
        const std::map<std::string,std::string> names{{" Alpha ","alpha"},{"Alpha","alpha"},{"Beta","beta"},{"Neutral","neutral"}};
        const auto found=names.find(owner);return found==names.end()?owner:found->second;
    }
    void notifyPublicBuildingLocation(const RuntimeCellPoint& cell) noexcept override {
        s.playerLastRadarCell={cell.x,cell.y};s.repaintRequested=true;
    }
    void invalidateLocalProjection() noexcept override {
        s.radarTerrainKey.clear();s.projectedObjectsCacheValid=false;s.repaintRequested=true;
    }
};
namespace original_phase {
static RecordingOperations* bound=nullptr;
std::string runtimeSkirmishOwnerForSlot(std::size_t slot) { return bound->ownerForSlot(slot); }
bool runtimeIsLandRushMode(const FixtureSession&) { return bound->isLandRushMode(); }
bool runtimeOwnersAreAllied(const FixtureSession&,const std::string& a,const std::string& b) { return bound->ownersAreAllied(a,b); }
std::string runtimeCanonicalHumanHouseOwner(const std::string& owner) { return bound->canonicalHumanOwner(owner); }
bool runtimeVisionOwnerIsNeutral(const std::string& owner) {
    const std::string normalized =
        toUpperAsciiLocal(trimAsciiLocal(owner));
    return normalized.empty()
        || normalized == "NEUTRAL"
        || normalized == "CIVILIAN"
        || normalized == "SPECIAL";
}

bool updateRuntimePublicBuildingLocations(FixtureSession& state,
    const std::vector<RuntimeVisionObserverDefinition>& observers) {
    if (!state.rulesLoaded) return false;
    bool changed = false;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0 || object.rubble
            || object.kind != WorldObjectKind::Structure || object.buildAnimTicksRemaining > 0) continue;
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || !building->revealToAll) continue;
        const auto owner = runtimeCanonicalHumanHouseOwner(object.owner);
        if (owner.empty()) continue;
        const RuntimeCellPoint cell{object.x, object.y};
        auto found = state.triggers.publicBuildingLocations.find(object.instanceId);
        if (found == state.triggers.publicBuildingLocations.end()
            || found->second.owner != owner || found->second.cell != cell) {
            RuntimePublicBuildingLocation location;
            location.owner = owner; location.cell = cell; location.noticeTick = state.tick;
            state.triggers.publicBuildingLocations[object.instanceId] = location;
            if (owner != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
                state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{cell.x, cell.y};
                state.repaintRequested = true;
            }
            changed = true;
        }
        // Retail exposes CurrentSight + 3 cells and reapplies public locations
        // after reshrouding. This is exploration, not continuing unit sight.
        const int radius = std::clamp(building->sight, 0, 509) + 3;
        for (const auto& observer : observers) {
            if (runtimeCanonicalHumanHouseOwner(observer.owner) == owner) continue;
            changed = state.vision.exploreArea(observer.owner, cell.x, cell.y, radius) || changed;
        }
    }
    return changed;
}

bool updateRuntimeVisionState(FixtureSession& state) {
    RuntimePerfTraceScope trace("updateRuntimeVisionState", state.tick);
    if (!state.visionEnabled) {
        return false;
    }
    struct VisionStageTimer {
        double& elapsed;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~VisionStageTimer() {
            elapsed += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    } visionStageTimer{state.perf.lastSimVisionMs};

    std::map<std::string, std::string> ownersByCanonicalId;
    const auto addOwner = [&ownersByCanonicalId](const std::string& owner) {
        const std::string trimmed = trimAsciiLocal(owner);
        if (trimmed.empty() || runtimeVisionOwnerIsNeutral(trimmed)) {
            return;
        }
        ownersByCanonicalId.try_emplace(
            toLowerAsciiLocal(trimmed),
            trimmed
        );
    };
    addOwner(state.economy.owner);
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        if (state.config.playerSlots[slotIndex].enabled) {
            addOwner(runtimeSkirmishOwnerForSlot(slotIndex));
        }
    }
    runtimeForEachDistinctObjectOwner(state.world.getObjects(), addOwner);

    std::vector<RuntimeVisionObserverDefinition> observers;
    observers.reserve(ownersByCanonicalId.size());
    std::vector<std::string> openingRevealOwners;
    const bool landRush = runtimeIsLandRushMode(state);
    for (const auto& [ownerKey, owner] : ownersByCanonicalId) {
        (void)ownerKey;
        if (landRush && !state.vision.hasObserver(owner)) {
            openingRevealOwners.push_back(owner);
        }
        RuntimeVisionObserverDefinition observer;
        observer.owner = owner;
        for (const auto& [candidateKey, candidate] : ownersByCanonicalId) {
            (void)candidateKey;
            if (runtimeOwnersAreAllied(state, owner, candidate)) {
                observer.sharedVisionOwners.push_back(candidate);
            }
        }
        observers.push_back(std::move(observer));
    }

    const std::uint64_t previousLocalRevision =
        state.vision.revisionFor(state.economy.owner);
    bool changed = state.vision.update(
        state.world,
        state.rulesLoaded ? &state.rules : nullptr,
        observers,
        state.tick
    );
    // Land Rush removes the opening shroud for every house, including remote
    // humans. Existing observers keep their saved exploration and later shroud
    // changes. Rebuild current sight after revealing so Gap Generators retain
    // their normal behavior and a reveal never grants permanent nearby sight.
    for (const std::string& owner : openingRevealOwners) {
        changed = state.vision.revealMap(owner) || changed;
    }
    if (!openingRevealOwners.empty()) {
        changed = state.vision.update(state.world,
            state.rulesLoaded ? &state.rules : nullptr, observers, state.tick) || changed;
    }
    changed = updateRuntimePublicBuildingLocations(state, observers) || changed;
    const std::uint64_t localRevision =
        state.vision.revisionFor(state.economy.owner);
    if (localRevision != previousLocalRevision) {
        state.radarTerrainKey.clear();
        state.projectedObjectsCacheValid = false;
        state.repaintRequested = true;
    }
    return changed;
}
}
int checks=0, failures=0;
void expect(bool yes,const char* label) { ++checks;if(!yes) {++failures;std::cerr<<"H036 contract failed: "<<label<<'\n';} }
bool equalStats(const RuntimeVisionObserverStats& a,const RuntimeVisionObserverStats& b) {
    return std::tie(a.validCells,a.visibleCells,a.exploredCells,a.visionSources,a.spySatSources,a.gapGeneratorSources,a.gapShroudedCells,a.lastUpdateTick,a.revision)
        ==std::tie(b.validCells,b.visibleCells,b.exploredCells,b.visionSources,b.spySatSources,b.gapGeneratorSources,b.gapShroudedCells,b.lastUpdateTick,b.revision);
}
void compareVision(const RuntimeVisionSystem& a,const RuntimeVisionSystem& b,int tick) {
    const auto owners=a.observerOwners();expect(owners==b.observerOwners(),"complete observer owner order");
    expect(a.serializeFireReveals(tick)==b.serializeFireReveals(tick),"complete fire-reveal state");
    expect(a.sourceMemoHitsForDiagnostics()==b.sourceMemoHitsForDiagnostics(),"original source memo counters");
    for(const auto& owner:owners) {
        expect(a.exploredCellIndices(owner)==b.exploredCellIndices(owner),"all serialized exploration indices");
        const auto* x=a.statsFor(owner);const auto* y=b.statsFor(owner);
        expect(x && y && equalStats(*x,*y),"all public observer stats including revisions/tick");
        RuntimeVisionGridView v,w;const bool va=a.gridViewFor(owner,v),vb=b.gridViewFor(owner,w);
        expect(va==vb && v.cellCount==w.cellCount && v.revision==w.revision && v.layoutEpoch==w.layoutEpoch,"full grid metadata/revision/epoch");
        if(va && vb && v.cellCount==w.cellCount) {
            for(const auto& pair:{std::pair{v.validCells,w.validCells},std::pair{v.visibleCells,w.visibleCells},std::pair{v.exploredCells,w.exploredCells},std::pair{v.gapShroudedCells,w.gapShroudedCells}})
                expect((pair.first==nullptr)==(pair.second==nullptr) && (!pair.first || std::memcmp(pair.first,pair.second,v.cellCount)==0),"all original grid bytes");
        }
    }
}
std::string publicLocations(const std::map<int,RuntimePublicBuildingLocation>& values) {
    std::ostringstream result;
    for(const auto& [id,v]:values) result<<id<<':'<<v.owner.size()<<':'<<v.owner<<':'<<v.cell.x<<','<<v.cell.y<<':'<<v.noticeTick<<';';
    return result.str();
}
FixtureSession makeState(unsigned scenario) {
    FixtureSession s;s.visionEnabled=(scenario%9)!=0;s.rulesLoaded=(scenario%7)!=0;
    s.landRush=(scenario&1)!=0;s.share=(scenario&2)!=0;s.config.playerSlots.resize(3);
    for(std::size_t i=0;i<s.config.playerSlots.size();++i)s.config.playerSlots[i].enabled=(scenario+i)%2==0;
    IniFile ini;
    if(!ini.loadFromString("[InfantryTypes]\n0=E1\n[BuildingTypes]\n0=PUBLIC\n1=GAP\n[E1]\nStrength=100\nSight=2\nSpeed=4\n[PUBLIC]\nStrength=500\nSight=3\nRevealToAll=yes\n[GAP]\nStrength=500\nSight=1\nGapGenerator=yes\nGapRadiusInCells=3\nPowered=no\n") || !s.rules.loadFromIni(ini))
        throw std::runtime_error("original standard Rules fixture failed");
    const auto* publicType=s.rules.findBuildingType("PUBLIC");
    if(!publicType || !publicType->revealToAll)throw std::runtime_error("public building fixture missing authored rule");
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {TerrainCell t;t.x=x;t.y=y;t.cellIndex512=y*512+x;s.world.accessTerrainCells().push_back(t);}
    for(int id=1;id<=4;++id) {
        WorldObjectInstance object;object.instanceId=id;object.alive=true;object.health=100;object.x=3+id;object.y=4+id;
        object.kind=id<=2?WorldObjectKind::Infantry:WorldObjectKind::Structure;
        object.type=id<=2?"E1":(id==3?"PUBLIC":"GAP");object.owner=id==1?"Alpha":(id==2?"Neutral":"Beta");
        if(id==3 && (scenario&4)!=0)object.buildAnimTicksRemaining=2;
        s.world.accessObjects().push_back(object);
    }
    return s;
}
int main() {
    std::string error;expect(runRuntimeVisionContractCheck(error),"entire original production vision core contract");
    if(!error.empty())std::cerr<<error<<'\n';
    std::size_t observations=0;
    for(unsigned scenario=0;scenario<32;++scenario) {
        FixtureSession old=makeState(scenario),now=old;
        const int fail=(scenario%5)==0?static_cast<int>(scenario%13)+1:-1;
        RecordingOperations oldOps(old,fail),nowOps(now,fail);original_phase::bound=&oldOps;
        for(int step=0;step<4;++step) {
            bool a=false,b=false,throwA=false,throwB=false;
            try {a=original_phase::updateRuntimeVisionState(old);}catch(const std::runtime_error&){throwA=true;}
            RuntimeVisionPhaseContext context{now.world,now.rules,now.rulesLoaded,now.visionEnabled,now.vision,now.config,now.economy.owner,now.tick,now.triggers.publicBuildingLocations,now.perf.lastSimVisionMs};
            try {b=updateRuntimeVisionState(context,nowOps);}catch(const std::runtime_error&){throwB=true;}
            expect(a==b && throwA==throwB,"original changed/exception semantics");
            expect(oldOps.trace==nowOps.trace,"exact original owner/alliance/canonical helper order");
            compareVision(old.vision,now.vision,old.tick);
            expect(publicLocations(old.triggers.publicBuildingLocations)==publicLocations(now.triggers.publicBuildingLocations),"all public location authority fields");
            expect(old.world.deterministicSimulationStateHash()==now.world.deterministicSimulationStateHash(),"real complete GameWorld hash");
            expect(std::tie(old.playerLastRadarCell.x,old.playerLastRadarCell.y,old.radarTerrainKey,old.projectedObjectsCacheValid,old.repaintRequested)==std::tie(now.playerLastRadarCell.x,now.playerLastRadarCell.y,now.radarTerrainKey,now.projectedObjectsCacheValid,now.repaintRequested),"exact original presentation invalidation values");
            expect(old.perf.lastSimVisionMs>=7.0 && now.perf.lastSimVisionMs>=7.0,"original diagnostic timer accounts on return/unwind");
            if(!old.visionEnabled)expect(old.perf.lastSimVisionMs==7.0 && now.perf.lastSimVisionMs==7.0,"disabled phase does not start original timer");
            ++observations;if(throwA || throwB)break;
            ++old.tick;++now.tick;
            old.radarTerrainKey=now.radarTerrainKey="next-key";old.projectedObjectsCacheValid=now.projectedObjectsCacheValid=true;old.repaintRequested=now.repaintRequested=false;
            if(step==0) {old.world.accessObjects()[2].buildAnimTicksRemaining=now.world.accessObjects()[2].buildAnimTicksRemaining=0;}
            if(step==1) {old.world.accessObjects()[2].owner=now.world.accessObjects()[2].owner="Alpha";}
            if(step==2) {old.world.accessObjects()[2].alive=now.world.accessObjects()[2].alive=false;}
        }
    }
    std::cout<<"H036 original vision phase: checks="<<checks<<" observations="<<observations<<" scenarios=32 failures="<<failures<<"\n";
    return failures?1:0;
}
