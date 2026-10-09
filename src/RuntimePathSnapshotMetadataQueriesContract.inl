// Include after RuntimePlannerCellPassabilityContract.inl and the production
// snapshot builder. The actual snapshot comparison switches ONLY this query.
#pragma once

#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RuntimePathSnapshotMetadataQueriesContractDetail {
RuntimeTerrainInfo infoForNumber(std::size_t n) {
    RuntimeTerrainInfo info;
    constexpr std::size_t landCount = static_cast<std::size_t>(RuntimeTerrainLandType::Tunnel) + 1U;
    info.movementLandType = static_cast<RuntimeTerrainLandType>(n % landCount);
    info.movementRampType = static_cast<unsigned char>(n % 256);
    info.kind = static_cast<RuntimeTerrainKind>(n % 5);
    info.authoredLandType = static_cast<RuntimeTerrainLandType>((n + 3) % landCount);
    info.passable = (n & 1) != 0; info.water = (n & 2) != 0;
    info.shore = (n & 4) != 0; info.cliff = (n & 8) != 0;
    info.ramp = (n & 16) != 0; info.bridge = (n & 32) != 0;
    info.cost = static_cast<int>(n % 71) - 35;
    return info;
}
bool sameInfo(const RuntimeTerrainInfo& a, const RuntimeTerrainInfo& b) {
    return std::tie(a.movementLandType, a.movementRampType, a.kind,
        a.authoredLandType, a.passable, a.water, a.shore, a.cliff, a.ramp, a.bridge, a.cost)
        == std::tie(b.movementLandType, b.movementRampType, b.kind,
        b.authoredLandType, b.passable, b.water, b.shore, b.cliff, b.ramp, b.bridge, b.cost);
}
auto fields(const PathTerrainCell& cell) {
    return std::tie(cell.cell.x, cell.cell.y, cell.level, cell.passable, cell.water,
        cell.shore, cell.bridge, cell.ramp, cell.cliff, cell.resource, cell.cost, cell.hazardCost);
}
} // namespace RuntimePathSnapshotMetadataQueriesContractDetail

bool runRuntimePathSnapshotMetadataQueriesPrimitiveContract(std::string& error) {
    using namespace RuntimePathSnapshotMetadataQueriesContractDetail;
    const auto fail = [&](const std::string& why) {
        error = "path snapshot metadata queries: " + why; return false;
    };
    RuntimePathSnapshotMetadataQueryReferenceScope optimized(false);
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    state.tick = 57;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), infoForNumber(y * 64 + x));
    for (const int key : {runtimeCellKey(511,511), runtimeCellKey(512,0),
            runtimeCellKey(3,512), -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
        state.terrainInfoByCell.emplace(key, infoForNumber(701 + state.terrainInfoByCell.size()));
    const int missing = runtimeCellKey(9,9);
    state.terrainInfoByCell.erase(missing);
    std::size_t addressChecks = 0, valueChecks = 0;
    {
        RuntimePathSnapshotMetadataQueries queries(state, 4096);
        const auto view = queries.viewFor(state);
        if (!view || queries.denseBytes() != 262144 * sizeof(const RuntimeTerrainInfo*)
            || queries.sourceEntriesExamined() != state.terrainInfoByCell.size())
            return fail("eligible table bytes/constructor scan count");
        for (const auto& [key, info] : state.terrainInfoByCell) {
            const auto* found = view->find(key);
            if (found != std::addressof(info)) return fail("exact original map-node address");
            ++addressChecks;
            if (!sameInfo(*found, info)) return fail("all eleven metadata fields");
            ++valueChecks;
        }
        for (int y = 0; y < 66; ++y) for (int x = 0; x < 66; ++x) {
            const int key = runtimeCellKey(x,y);
            if (view->find(key) != RuntimePathSnapshotMetadataQueries::originalFind(state,key))
                return fail("complete present/missing grid");
            ++addressChecks;
        }
        const auto mapSize = state.terrainInfoByCell.size();
        for (int repeat = 0; repeat < 4; ++repeat) {
            if (view->find(missing) || view->find(runtimeCellKey(511,510)))
                return fail("missing must remain nullptr, without metadata computation");
            addressChecks += 2;
        }
        if (state.terrainInfoByCell.size() != mapSize) return fail("missing query inserted metadata");
        if (runtimeCellKey(1001,2) != runtimeCellKey(1,3)
            || view->find(runtimeCellKey(1001,2)) != view->find(runtimeCellKey(1,3)))
            return fail("legacy coordinate alias must retain original integer-key identity");
        auto foreign = std::make_unique<RuntimeWindowState>(); foreign->tick = state.tick;
        if (queries.viewFor(*foreign)) return fail("foreign state reused pointer table");
        {
            RuntimePathSnapshotMetadataQueryReferenceScope reference(true);
            RuntimePathSnapshotMetadataQueries original(state,4096);
            if (original.viewFor(state) || original.denseBytes() || original.retainedEntries()
                || original.sourceEntriesExamined() || queries.viewFor(state))
                return fail("reference must allocate/scan/reuse nothing");
            {
                RuntimePathSnapshotMetadataQueryReferenceScope nested(false);
                RuntimePathSnapshotMetadataQueries enabled(state,4096);
                if (!enabled.viewFor(state)) return fail("nested optimized scope");
            }
            if (!runtimePathSnapshotMetadataQueryReference()) return fail("nested scope did not restore reference");
        }
        if (runtimePathSnapshotMetadataQueryReference()) return fail("scope did not restore optimized");
        for (const bool enabled : {false,true}) {
            RuntimePathSnapshotMetadataQueries smallOrDisabled(state,enabled ? 4095 : 4096,enabled);
            if (smallOrDisabled.viewFor(state) || smallOrDisabled.denseBytes()
                || smallOrDisabled.sourceEntriesExamined()) return fail("small/Air/disabled allocated or scanned");
        }
    }
    {
        // Reuse must survive ticks and retain live values, while every map
        // replacement must end all borrowed views, even at identical shape.
        const auto builds = state.terrainInfoByCell.denseBuildCount();
        for (int n = 0; n < 32; ++n) {
            ++state.tick;
            RuntimePathSnapshotMetadataQueries reused(state,4096);
            if (!reused.viewFor(state) || reused.sourceEntriesExamined() != 0)
                return fail("cross-tick reuse scanned metadata again");
        }
        if (state.terrainInfoByCell.denseBuildCount() != builds)
            return fail("cross-tick table rebuilt");
        RuntimePathSnapshotMetadataQueries beforeValue(state,4096);
        const int key = runtimeCellKey(2,2);
        state.terrainInfoByCell.at(key) = infoForNumber(2345);
        const auto live = beforeValue.viewFor(state);
        if (!live || !sameInfo(*live->find(key),infoForNumber(2345)))
            return fail("in-place values were copied rather than borrowed");
        using MetadataMap = decltype(state.terrainInfoByCell);
        static_assert(std::is_nothrow_swappable_v<MetadataMap>);
        MetadataMap copied(state.terrainInfoByCell);
        if (copied.denseValid()) return fail("copy inherited borrowed pointers");
        const auto& copiedTable = copied.densePointers();
        if (copiedTable[2*512+2] != std::addressof(copied.at(key))
            || copiedTable[2*512+2] == live->find(key))
            return fail("copy table points into another map");
        std::vector<std::pair<int,RuntimeTerrainInfo>> entries(
            state.terrainInfoByCell.begin(),state.terrainInfoByCell.end());
        const auto count = state.terrainInfoByCell.size();
        state.terrainInfoByCell.clear();
        for (const auto& [entryKey,info] : entries)
            state.terrainInfoByCell.emplace(entryKey,info);
        if (state.terrainInfoByCell.size()!=count || beforeValue.viewFor(state))
            return fail("same-shape clear/reinsert retained stale view");
        RuntimePathSnapshotMetadataQueries replaced(state,4096);
        if (!replaced.viewFor(state) || replaced.sourceEntriesExamined()!=count)
            return fail("same-shape replacement did not rebuild");
        state.terrainInfoByCell = copied;
        if (replaced.viewFor(state) || state.terrainInfoByCell.denseValid())
            return fail("copy assignment retained stale table");
        RuntimePathSnapshotMetadataQueries assigned(state,4096);
        copied.at(key)=infoForNumber(6789);
        using std::swap;
        swap(state.terrainInfoByCell,copied);
        if (assigned.viewFor(state) || state.terrainInfoByCell.denseValid() || copied.denseValid())
            return fail("load-style swap retained either pointer table");
        RuntimePathSnapshotMetadataQueries swapped(state,4096);
        if (!swapped.viewFor(state) || !sameInfo(*swapped.viewFor(state)->find(key),infoForNumber(6789)))
            return fail("swapped table points into previous map");
        MetadataMap moved(std::move(state.terrainInfoByCell));
        if (swapped.viewFor(state) || moved.denseValid() || state.terrainInfoByCell.denseValid())
            return fail("move constructor retained borrowed source table");
        state.terrainInfoByCell = std::move(moved);
        if (state.terrainInfoByCell.denseValid() || moved.denseValid())
            return fail("move assignment retained borrowed table");
        RuntimePathSnapshotMetadataQueries afterMove(state,4096);
        if (!afterMove.viewFor(state) || !sameInfo(*afterMove.viewFor(state)->find(key),infoForNumber(6789)))
            return fail("move assignment table did not own its nodes");
        RuntimePathSnapshotMetadataQueries beforeInsert(state,4096);
        state.terrainInfoByCell[missing] = infoForNumber(3579);
        if (beforeInsert.viewFor(state)) return fail("insertion retained cached-null view");
        RuntimePathSnapshotMetadataQueries inserted(state,4096);
        if (!inserted.viewFor(state) || !sameInfo(*inserted.viewFor(state)->find(missing),infoForNumber(3579)))
            return fail("inserted node still cached as missing");
        RuntimePathSnapshotMetadataQueries beforeErase(state,4096);
        state.terrainInfoByCell.erase(missing);
        if (beforeErase.viewFor(state)) return fail("erase retained dangling node view");
        RuntimePathSnapshotMetadataQueries erased(state,4096);
        if (!erased.viewFor(state) || erased.viewFor(state)->find(missing))
            return fail("erased node remains in new view");
    }
    {
        auto sparseState = std::make_unique<RuntimeWindowState>(); sparseState->tick = state.tick;
        for (int n = 0; n < 4095; ++n)
            sparseState->terrainInfoByCell.emplace(runtimeCellKey(n % 64,n / 64),infoForNumber(n));
        RuntimePathSnapshotMetadataQueries query(*sparseState,4096);
        if (query.viewFor(*sparseState) || query.denseBytes() || query.sourceEntriesExamined())
            return fail("small metadata map allocated or scanned");
    }
    {
        RuntimePathSnapshotMetadataQueries old(state,4096);
        ++state.tick;
        if (old.viewFor(state)) return fail("changed tick reused view");
        // Explicitly end the read-only lifetime before same-count value/node edits.
        old.invalidate();
        const int key = runtimeCellKey(2,2);
        state.terrainInfoByCell.at(key) = infoForNumber(999);
        state.terrainInfoByCell.erase(missing + 1);
        state.terrainInfoByCell.emplace(missing + 1,infoForNumber(1001));
        if (old.viewFor(state)) return fail("invalidated owner accepted edited map");
        RuntimePathSnapshotMetadataQueries fresh(state,4096);
        const auto view = fresh.viewFor(state);
        if (!view || view->find(key) != std::addressof(state.terrainInfoByCell.at(key))
            || !sameInfo(*view->find(key),infoForNumber(999))) return fail("new lifetime did not see edits");
    }
    {
        // The actual 512^2 limit exercises every slot and bounds both storage
        // and the constructor's input scan. No A* or game loop is run here.
        auto maximum = std::make_unique<RuntimeWindowState>(); maximum->tick = 61;
        maximum->terrainInfoByCell.reserve(262145);
        for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x)
            maximum->terrainInfoByCell.emplace(runtimeCellKey(x,y),infoForNumber(y * 512 + x));
        {
            RuntimePathSnapshotMetadataQueries full(*maximum,262144);
            const auto view = full.viewFor(*maximum);
            if (!view || full.denseBytes() != 262144 * sizeof(const RuntimeTerrainInfo*)
                || full.retainedEntries() != 262144 || full.sourceEntriesExamined() != 262144)
                return fail("full-domain 512^2 storage/scan bound");
            for (const auto& [key, info] : maximum->terrainInfoByCell) {
                if (view->find(key) != std::addressof(info) || !sameInfo(*view->find(key),info))
                    return fail("full-domain exact address/fields");
                ++addressChecks; ++valueChecks;
            }
        }
        maximum->terrainInfoByCell.emplace(std::numeric_limits<int>::max(),infoForNumber(99));
        RuntimePathSnapshotMetadataQueries oversized(*maximum,262145);
        if (oversized.viewFor(*maximum) || oversized.denseBytes() || oversized.retainedEntries()
            || oversized.sourceEntriesExamined()) return fail("oversized metadata must use exact old lookup without allocation/scan");
    }
    if (addressChecks < 270000 || valueChecks < 266000) return fail("primitive coverage count");
    std::cout << "Path snapshot metadata queries: exactAddresses=" << addressChecks
        << " fullMetadataValues=" << valueChecks
        << " fields=11 boundedSlots=262144 maxLogicalBytes=" << 262144 * sizeof(const RuntimeTerrainInfo*)
        << " missingSkip/legacyAliases/outside/foreign/reference/freshLifetime=yes"
        << " crossTickReuse/liveValues/sameShapeReplace/copy/move/loadSwap/insert/erase=yes passed=yes\n";
    error.clear(); return true;
}

bool runRuntimePathSnapshotMetadataQueriesActualContract(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimePathSnapshotMetadataQueriesContractDetail;
    using RuntimePlannerPassabilityContractDetail::Authority;
    const auto fail = [&](const std::string& why) {
        error = "path snapshot metadata actual: " + why; return false;
    };
    const auto prepare = [&](RuntimeWindowState& state, int history) {
        RuntimeSnapshotActiveStateScope active(state);
        if (!RuntimePlannerPassabilityContractDetail::make(state,config,history,0,error)) return false;
        IniFile ini;
        const std::string authoredRules = std::string(RuntimePlannerPassabilityContractDetail::rulesText)
            + "[Clear]\nTrack=100%\nFoot=100%\nFloat=0%\nHover=100%\n"
              "[Water]\nTrack=0%\nFoot=0%\nFloat=100%\nHover=100%\n"
              "[Rock]\nTrack=0%\nFoot=0%\nFloat=0%\nHover=0%\n"
              "[Tiberium]\nTrack=75%\nFoot=50%\nFloat=0%\nHover=100%\n";
        if (!ini.loadFromString(authoredRules) || !state.rules.loadFromIni(ini)
            || !state.rules.findLandType("Rock") || !state.rules.findLandType("Water"))
            return fail("authored movement permissions were not loaded");
        auto& cells = state.world.accessTerrainCells();
        cells.clear(); cells.reserve(4104);
        const auto append = [&](int x,int y) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.level = x >= 0 && x < 64 && y >= 0 && y < 64 ? x / 32 : 2;
            cell.hasOverlay=cell.overlayIsOre=x==60; cell.overlayIsGem=x==61;
            if (cell.overlayIsGem) cell.hasOverlay=true;
            cells.push_back(cell);
        };
        for (int y=0;y<64;++y) for(int x=0;x<64;++x) append(x,y);
        const std::array<RuntimeCellPoint,8> extra{{{8,8},{1008,7},{-1,2},{0,-1},
            {511,511},{512,3},{3,512},{900,900}}};
        for (const auto point : extra) append(point.x,point.y);
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        state.terrainInfoByCell.clear();
        for (std::size_t index=0;index<cells.size();++index) {
            const auto& cell=cells[index]; RuntimeTerrainInfo info;
            info.authoredLandType=info.movementLandType=cell.x==10 || cell.x>=50
                ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
            info.movementRampType=static_cast<unsigned char>(index%256);
            info.kind=RuntimeTerrainKind::Land; info.cost=8+static_cast<int>(index%19);
            info.water=cell.x==10 || cell.x>=50; info.shore=cell.x==9 || cell.x==49;
            info.bridge=(cell.x==10 || cell.x==50) && cell.y==5;
            info.ramp=(cell.x==31 || cell.x==32) && cell.y==5;
            info.cliff=cell.x==31 && cell.y==6; info.passable=!info.cliff;
            if (cell.x==20 && cell.y==20) info.authoredLandType=RuntimeTerrainLandType::Rock;
            state.terrainInfoByCell.try_emplace(runtimeCellKey(cell.x,cell.y),info);
        }
        state.terrainInfoByCell.erase(runtimeCellKey(22,22));
        state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
        state.foundationCollisionTick=-1; invalidateRuntimeTickOccupancyCache(state);
        return cells.size()==4104 && state.terrainInfoByCell.size()>=4096;
    };
    std::size_t cases=0, comparedCells=0, warmCells=0, authorityPairs=0;
    // All other optimizations take the same path in both authorities.
    RuntimeSimulationQueryReuseReferenceScope sameSimulation(false);
    RuntimePathSnapshotOccupancyQueryReferenceScope sameOccupancy(false);
    for (int history : {0,1000}) {
        auto oldBox=std::make_unique<RuntimeWindowState>(), newBox=std::make_unique<RuntimeWindowState>();
        auto& oldState=*oldBox; auto& newState=*newBox;
        if (!prepare(oldState,history) || !prepare(newState,history)) return false;
        for (int phase=0;phase<6;++phase) {
            for (auto* state : {&oldState,&newState}) {
                state->tick=70+phase;
                // Same-count/type/value edits are seen only by a NEW builder.
                if (phase==1) state->terrainInfoByCell.erase(runtimeCellKey(20,20));
                if (phase==2) state->terrainInfoByCell[runtimeCellKey(20,20)].authoredLandType=RuntimeTerrainLandType::Rock;
                if (phase==3) state->terrainInfoByCell[runtimeCellKey(20,20)].authoredLandType=RuntimeTerrainLandType::Clear;
                if (phase==4) {
                    auto& cells=state->world.accessTerrainCells(); std::swap(cells[0],cells[cells.size()-2]);
                    state->terrainByCell=buildRuntimeTerrainLookup(state->world);
                }
                if (phase==5) {
                    IniFile reloaded;
                    const std::string rules=std::string(RuntimePlannerPassabilityContractDetail::rulesText)
                        + "[Clear]\nTrack=0%\nFoot=100%\nFloat=0%\nHover=100%\n"
                          "[Water]\nTrack=0%\nFoot=0%\nFloat=100%\nHover=100%\n"
                          "[Rock]\nTrack=100%\nFoot=0%\nFloat=0%\nHover=0%\n"
                          "[Tiberium]\nTrack=50%\nFoot=100%\nFloat=0%\nHover=100%\n";
                    if (!reloaded.loadFromString(rules) || !state->rules.loadFromIni(reloaded)) return fail("rules reload");
                }
                state->pathTerrainCache.valid=state->pathTerrainCache.dynamicValid=false;
                state->foundationCollisionTick=-1; invalidateRuntimeTickOccupancyCache(*state);
            }
            Authority beforeOld,beforeNew,afterOld,afterNew;
            { RuntimeSnapshotActiveStateScope active(oldState);
              if (!RuntimePlannerPassabilityContractDetail::capture(oldState,beforeOld,error)) return false; }
            { RuntimeSnapshotActiveStateScope active(newState);
              if (!RuntimePlannerPassabilityContractDetail::capture(newState,beforeNew,error)) return false; }
            if (!(beforeOld==beforeNew)) return fail("independent prepared authorities differ");
            {
                RuntimePathSnapshotMetadataQueryReferenceScope optimized(false);
                const bool wasReady = newState.terrainInfoByCell.denseValid();
                RuntimePathSnapshotMetadataQueries eligible(newState,4104);
                if (!eligible.viewFor(newState)
                    || eligible.denseBytes()!=262144*sizeof(const RuntimeTerrainInfo*)
                    || eligible.sourceEntriesExamined()!=(wasReady ? 0 : newState.terrainInfoByCell.size()))
                    return fail("actual large-map table did not meet eligibility/allocation bounds");
            }
            const auto buildsBeforeMatrix = newState.terrainInfoByCell.denseBuildCount();
            for (int moverId : {1,2,3,4,5,6}) for (int allowedId : {-1,30,33}) {
                const std::unordered_set<int> ignored=phase%2 ? std::unordered_set<int>{30,31,32,33,34} : std::unordered_set<int>{};
                const auto build = [&](RuntimeWindowState& state,bool reference,bool cold) {
                    RuntimeSnapshotActiveStateScope active(state);
                    RuntimePathSnapshotMetadataQueryReferenceScope branch(reference);
                    if (cold) state.pathTerrainCache.dynamicValid=false;
                    return std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(
                        state,*findObjectById(state,moverId),allowedId,&ignored,false));
                };
                const auto oldCells=build(oldState,true,true), newCells=build(newState,false,true);
                const auto oldWarm=build(oldState,true,false), newWarm=build(newState,false,false);
                if (oldCells.size()!=4104 || newCells.size()!=oldCells.size()
                    || oldWarm.size()!=oldCells.size() || newWarm.size()!=oldCells.size()) return fail("actual snapshot size/order");
                for (std::size_t index=0;index<oldCells.size();++index) {
                    if (fields(oldCells[index])!=fields(newCells[index])) return fail("actual cold all-12-fields offset="+std::to_string(index));
                    if (fields(oldWarm[index])!=fields(newWarm[index]) || fields(oldCells[index])!=fields(oldWarm[index]))
                        return fail("actual warm cache all-12-fields offset="+std::to_string(index));
                    ++comparedCells; ++warmCells;
                }
                ++cases;
            }
            if (newState.terrainInfoByCell.denseBuildCount()!=buildsBeforeMatrix)
                return fail("actual repeated mover/host snapshot rescanned unchanged metadata");
            { RuntimeSnapshotActiveStateScope active(oldState);
              if (!RuntimePlannerPassabilityContractDetail::capture(oldState,afterOld,error)) return false; }
            { RuntimeSnapshotActiveStateScope active(newState);
              if (!RuntimePlannerPassabilityContractDetail::capture(newState,afterNew,error)) return false; }
            if (!(afterOld==afterNew) || !(beforeOld==afterOld) || !(beforeNew==afterNew))
                return fail("world/subcells/gates/repair/mag/full serialized authority changed");
            ++authorityPairs;
        }
    }
    if (cases!=216 || comparedCells!=216*4104 || warmCells!=comparedCells || authorityPairs!=12)
        return fail("actual matrix coverage count");
    std::cout << "Path snapshot metadata actual: isolatedCases=" << cases
        << " coldFullFieldCells=" << comparedCells << " warmFullFieldCells=" << warmCells
        << " fields=12 independentAuthorityPairs=" << authorityPairs
        << " H0/H1000/types/allowedHosts/missing/duplicates/legacyAliases/outside/rulesReload=yes"
        << " fullSerializedAuthorities=yes passed=yes\n";
    error.clear(); return true;
}

bool runRuntimePathSnapshotMetadataQueriesFullContract(const RuntimeConfig& config,std::string& error) {
    return runRuntimePathSnapshotMetadataQueriesPrimitiveContract(error)
        && runRuntimePathSnapshotMetadataQueriesActualContract(config,error);
}
