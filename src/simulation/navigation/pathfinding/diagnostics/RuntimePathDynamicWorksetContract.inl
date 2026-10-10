// STAGED ONLY. Include after the existing Sparse/Batch native contracts. Run
// this ADDITIONAL contract after all unchanged metadata/conditional/sparse/batch
// contracts; never replace their 337/674/184 full-data assertions or observers.
#pragma once
#include <array>
#include <iostream>
#include <map>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace RuntimePathDynamicWorksetContractDetail {
using Workset = RuntimePathDynamicWorkset<PathTerrainCell>;
using Full = RuntimePathSnapshotConditionalQueriesContractDetail::Full;
using RuntimePathSnapshotConditionalQueriesContractDetail::capture;
using RuntimePathSnapshotMetadataQueriesContractDetail::fields;
using RuntimePathDynamicSparseQueriesContractDetail::sameSnapshot;
using RuntimePlannerPassabilityContractDetail::object;
struct Route {
    RuntimeSimulationQueryReuseReferenceScope reuse;
    RuntimePathSnapshotMetadataQueryReferenceScope metadata;
    RuntimePathSnapshotOccupancyQueryReferenceScope representative;
    RuntimePathDynamicSparseQueriesReferenceScope sparse;
    RuntimePathDynamicCellBatchReferenceScope batch;
    RuntimePathDynamicWorksetReferenceScope workset;
    explicit Route(int route) : reuse(route==0),metadata(route==0),representative(route==0),
        sparse(route==0),batch(route==0),workset(route!=2) {}
};
struct Observation {
    const void* expected=nullptr;
    bool foreign=false;
    std::vector<RuntimePathDynamicWorksetStats> calls;
    static void append(void* opaque,const void* state,const RuntimePathDynamicWorksetStats& value) {
        auto& self=*static_cast<Observation*>(opaque);self.foreign|=self.expected!=state;
        self.calls.push_back(value);
    }
};
struct Counts {
    std::size_t primitive=0,synthetic=0,fieldCells=0,queueResults=0,queueStats=0;
    std::size_t nativeScenarios=0,nativeCommands=0,nativeTickTriplets=0;
    std::size_t nativeAdmitted=0,nativeCandidates=0,nativeOmitted=0,nativeBodyVisits=0,observerFallbacks=0;
    std::size_t nativeSimulationOwners=0,nativeSimulationAdmitted=0,nativeSimulationOmitted=0;
    std::size_t independentFallbacks=0,currentValueBuilds=0;
};
bool sameResult(const PathResult& a, const PathResult& b) {
    if (std::tie(a.requestId, a.start.x, a.start.y, a.goal.x, a.goal.y,
            a.resolvedGoal.x, a.resolvedGoal.y, a.completed, a.reachable,
            a.cacheHit, a.cacheMiss, a.goalAdjusted, a.searchBudgetExhausted,
            a.queueBackpressure, a.fallbackTargetCacheHit, a.fallbackRing,
            a.mapQueryApproachUsed, a.sharedApproachCacheHit,
            a.collapsedFromEquivalentRequest, a.status, a.passabilityProfileHash,
            a.requestClass, a.expandedNodes, a.expandedNodeBudget,
            a.fallbackTargetsTried, a.fallbackCandidateCount, a.goalSlotIndex,
            a.noPathCooldownTicksRemaining, a.failureReason)
        != std::tie(b.requestId, b.start.x, b.start.y, b.goal.x, b.goal.y,
            b.resolvedGoal.x, b.resolvedGoal.y, b.completed, b.reachable,
            b.cacheHit, b.cacheMiss, b.goalAdjusted, b.searchBudgetExhausted,
            b.queueBackpressure, b.fallbackTargetCacheHit, b.fallbackRing,
            b.mapQueryApproachUsed, b.sharedApproachCacheHit,
            b.collapsedFromEquivalentRequest, b.status, b.passabilityProfileHash,
            b.requestClass, b.expandedNodes, b.expandedNodeBudget,
            b.fallbackTargetsTried, b.fallbackCandidateCount, b.goalSlotIndex,
            b.noPathCooldownTicksRemaining, b.failureReason)
        || a.path.size() != b.path.size()) return false;
    for (std::size_t i = 0; i < a.path.size(); ++i)
        if (a.path[i].x != b.path[i].x || a.path[i].y != b.path[i].y) return false;
    return true;
}

bool sameStats(const PathRequestQueueLiveStats& a, const PathRequestQueueLiveStats& b) {
    return std::tie(a.pendingCount, a.cacheHits, a.cacheMisses, a.budgetExhaustedResults,
        a.lastPumpBudgetExhausted, a.backpressureRejectedRequests,
        a.requestOverflowRejectedRequests, a.overflowRejectedResults,
        a.collapsedPendingRequests, a.massAttackCollapsedRequests,
        a.sharedApproachCacheHits, a.sharedApproachCacheMisses,
        a.repeatedFailureSuppressed, a.noPathCooldownHits, a.cooldownNoPathResults,
        a.realNoPathResults, a.equivalentStartBucketCollapses, a.goalSlotAssignments,
        a.fallbackCandidatesConsidered, a.fallbackRingRequests,
        a.fallbackTargetCacheHits, a.fallbackTargetCacheMisses,
        a.mapQueryApproachRequests, a.mapQueryApproachUsedResults,
        a.lastPumpExpandedNodes, a.cachedPathCount, a.fallbackTargetCacheEntries,
        a.fallbackDistanceQueries, a.fallbackComponentCellChecks,
        a.heuristicEvaluations, a.usesDenseTerrainLookup, a.usesShortHashKeys,
        a.mapQueryApproachAvailable, a.usesBlockedComponentApproachFallback)
        == std::tie(b.pendingCount, b.cacheHits, b.cacheMisses, b.budgetExhaustedResults,
        b.lastPumpBudgetExhausted, b.backpressureRejectedRequests,
        b.requestOverflowRejectedRequests, b.overflowRejectedResults,
        b.collapsedPendingRequests, b.massAttackCollapsedRequests,
        b.sharedApproachCacheHits, b.sharedApproachCacheMisses,
        b.repeatedFailureSuppressed, b.noPathCooldownHits, b.cooldownNoPathResults,
        b.realNoPathResults, b.equivalentStartBucketCollapses, b.goalSlotAssignments,
        b.fallbackCandidatesConsidered, b.fallbackRingRequests,
        b.fallbackTargetCacheHits, b.fallbackTargetCacheMisses,
        b.mapQueryApproachRequests, b.mapQueryApproachUsedResults,
        b.lastPumpExpandedNodes, b.cachedPathCount, b.fallbackTargetCacheEntries,
        b.fallbackDistanceQueries, b.fallbackComponentCellChecks,
        b.heuristicEvaluations, b.usesDenseTerrainLookup, b.usesShortHashKeys,
        b.mapQueryApproachAvailable, b.usesBlockedComponentApproachFallback);
}
struct QueueStep { int processed=0; std::vector<PathResult> results; PathRequestQueueLiveStats stats; };
std::vector<QueueStep> queueProof(const std::vector<PathTerrainCell>& values) {
    PathRequestQueue queue(values);std::vector<QueueStep> out;
    std::vector<PathCell> passable;
    for(const auto& cell:values)if(cell.passable&&cell.cost>0) {
        passable.push_back(cell.cell);if(passable.size()==16)break;
    }
    if(passable.empty()) {passable.push_back(values.front().cell);}
    const PathCell start=passable.front(),goal=passable.back();
    queue.submit(start,goal);queue.submit(start,goal);
    for(const auto budget:{8,4096,4096}) {
        QueueStep step;step.processed=queue.pumpWithBudget(16,budget);
        step.results=queue.getResults();step.stats=queue.liveStats();out.push_back(std::move(step));
        if(budget==8)queue.submit(start,goal);
    }
    // Identical repeated requests exercise the original path/failure caches,
    // status, queue collapse and cooldown semantics, including blocked corpora.
    queue.submit(start,goal);QueueStep tail;tail.processed=queue.pumpWithBudget(16,4096);
    tail.results=queue.getResults();tail.stats=queue.liveStats();out.push_back(std::move(tail));
    return out;
}
bool compareQueue(const std::vector<QueueStep>& a,const std::vector<QueueStep>& b,Counts& counts) {
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n) {
        if(a[n].processed!=b[n].processed||!sameStats(a[n].stats,b[n].stats)
            ||a[n].results.size()!=b[n].results.size())return false;
        ++counts.queueStats;
        for(std::size_t r=0;r<a[n].results.size();++r) {
            if(!sameResult(a[n].results[r],b[n].results[r])||a[n].results[r].passabilityProfileHash.empty())return false;
            ++counts.queueResults;
        }
    }
    return true;
}
bool primitive(Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path dynamic workset primitive: "+why;return false;};
    RuntimePathDynamicWorksetReferenceScope normal(false);
    int state=1,mover=2,tick=70,foreign=3;
    std::vector<PathTerrainCell> cells(4096);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)cells[static_cast<std::size_t>(y)*64+x].cell={x,y};
    Workset::Occupants occupied{{1001,-7},{2002,mover}};
    std::unordered_map<int,RuntimeTerrainInfo> infos;
    infos[3003].authoredLandType=RuntimeTerrainLandType::Rock;
    const auto find=[&](int key)->const RuntimeTerrainInfo* {const auto at=infos.find(key);return at==infos.end()?nullptr:&at->second;};
    const auto allows=[](const RuntimeTerrainInfo& info){return info.authoredLandType!=RuntimeTerrainLandType::Rock;};
    const auto none=[](auto&&){};
    const auto repair=[](auto&& mark){mark(4,4);};
    cells[5*64+5].resource=true;cells[6*64+6].hazardCost=-3;
    cells[7*64+7].water=true;cells[8*64+8].passable=false;
    cells[9*64+9]=cells[1*64+1]; // duplicate authored row must also replay
    const auto original=cells;
    {
        Workset proof(&state,&mover,tick,cells,occupied,true,true,false,true,repair,find,allows);
        const std::vector<std::size_t> expected{65,130,195,260,325,390,455,585};
        const auto view=proof.viewFor(&state,&mover,tick,cells,occupied);
        if(!view||proof.indicesForDiagnostics()!=expected||proof.stats().examined!=4096
            ||proof.stats().omitted!=4096-expected.size()||proof.stats().retainedBytes>4096*sizeof(std::size_t))
            return fail("complete candidate union/authored duplicates/negative IDs/base hazard/land proof");
        std::size_t position=0;for(auto entry=view->begin();entry!=view->end();++entry) {
            if(std::addressof(*entry)!=std::addressof(cells[expected[position++]]))return fail("iterator original row address/order");
            ++counts.primitive;
        }
        if(proof.viewFor(&foreign,&mover,tick,cells,occupied)||proof.viewFor(&state,&foreign,tick,cells,occupied)
            ||proof.viewFor(&state,&mover,tick+1,cells,occupied))return fail("foreign/tick identity");
        const auto copied=cells;const auto otherOccupants=occupied;
        if(proof.viewFor(&state,&mover,tick,copied,occupied)||proof.viewFor(&state,&mover,tick,cells,otherOccupants))return fail("copied containers");
        {
            Workset disabled(&state,&mover,tick,cells,occupied,false,true,false,true,none,find,allows);
            if(disabled.viewFor(&state,&mover,tick,cells,occupied)||proof.viewFor(&state,&mover,tick,cells,occupied))return fail("disabled nested mask");
        }
        if(!proof.viewFor(&state,&mover,tick,cells,occupied))return fail("parent restored");
        {RuntimePathDynamicWorksetReferenceScope reference(true);if(proof.viewFor(&state,&mover,tick,cells,occupied))return fail("mid-scope reference");}
        proof.invalidate();if(proof.viewFor(&state,&mover,tick,cells,occupied))return fail("explicit pre-mutation invalidation");
        counts.primitive+=9;
    }
    for(std::size_t n=0;n<cells.size();++n)if(fields(cells[n])!=fields(original[n]))return fail("proof modified copied output");
    const auto reject=[&](std::vector<PathTerrainCell>& values,const Workset::Occupants& occupants,
        auto&& repairs,bool enabled=true) {
        const auto before=values;
        Workset proof(&state,&mover,tick,values,occupants,enabled,true,false,true,repairs,find,allows);
        if(proof.viewFor(&state,&mover,tick,values,occupants)||proof.stats().admitted||proof.stats().retainedBytes)return false;
        for(std::size_t n=0;n<values.size();++n)if(fields(values[n])!=fields(before[n]))return false;
        ++counts.primitive;return true;
    };
    Workset::Occupants noOccupants,negative{{-1,-7}},alias{{1000*512,3}},dense;
    for(int n=0;n<513;++n)dense.emplace((n/64)*1000+n%64,-n);
    auto abnormal=cells;abnormal[4095].cell={1001,0};
    auto outside=cells;outside[4095].cell={-1,0};
    auto resources=cells;for(auto& cell:resources)cell.resource=true;
    auto tiny=cells;tiny.resize(4095);
    auto large=cells;large.resize(262145);
    if(!reject(abnormal,occupied,none)||!reject(outside,occupied,none)||!reject(cells,negative,none)
        ||!reject(cells,alias,none)||!reject(cells,dense,none)||!reject(resources,noOccupants,none)
        ||!reject(tiny,noOccupants,none)||!reject(large,noOccupants,none)
        ||!reject(cells,noOccupants,[](auto&& add){add(512,0);})||!reject(cells,noOccupants,none,false))
        return fail("rejection/cap/raw alias/outside/density before output modification");
    {
        RuntimePathDynamicWorksetReferenceScope reference(true);
        if(!reject(cells,occupied,none))return fail("reference no workset");
    }
    {
        Workset allocation(&state,&mover,tick,cells,noOccupants,true,true,false,false,none,
            [](int)->const RuntimeTerrainInfo*{throw std::bad_alloc{};},allows);
        if(allocation.viewFor(&state,&mover,tick,cells,noOccupants)
            ||allocation.stats().reason!=RuntimePathDynamicWorksetReason::Allocation)return fail("allocation fallback");
        ++counts.primitive;
    }
    const void* prior=runtimeActivePathDynamicWorkset;
    try {
        Workset exception(&state,&mover,tick,cells,noOccupants,true,true,false,false,none,
            [](int)->const RuntimeTerrainInfo*{throw std::runtime_error("test-only");},allows);
        return fail("exception not propagated");
    }catch(const std::runtime_error&){}
    if(runtimeActivePathDynamicWorkset!=prior)return fail("constructor exception TLS restoration");
    ++counts.primitive;
    return true;
}

bool compareBuild(std::array<std::unique_ptr<RuntimeWindowState>,3>& states,int moverId,int allowed,
    Counts& counts,std::string& error,const std::string& label,bool requirePositive=false,bool force=false) {
    const auto fail=[&](const std::string& why){error="path dynamic workset: "+label+" "+why;return false;};
    std::array<Full,3> before,after;
    std::array<std::vector<PathTerrainCell>,3> values;
    std::array<Observation,3> observations;
    std::array<std::vector<QueueStep>,3> queues;
    for(int route=0;route<3;++route) {
        auto& state=*states[route];RuntimeSnapshotActiveStateScope active(state);Route reference(route);
        if(!capture(state,before[route],error))return fail("before full writer: "+error);
        state.pathTerrainCache.dynamicValid=false;observations[route].expected=&state;
        {
            RuntimePathDynamicWorksetObserverScope observer(&Observation::append,&observations[route]);
            const auto* actor=findObjectById(state,moverId);if(!actor)return fail("missing actual mover");
            values[route]=buildRuntimePathTerrainSnapshot(state,*actor,allowed,nullptr,force);
            if(!sameSnapshot(values[route],buildRuntimePathTerrainSnapshot(state,*actor,allowed,nullptr,force),counts.fieldCells))
                return fail("original warm complete cache/order/12 fields");
        }
        if(values[route].empty()||observations[route].foreign||observations[route].calls.size()!=1)
            return fail("exactly one actual cold owner and no warm proof rebuild");
        const auto& proof=observations[route].calls.front();
        if(route<2) {
            if(proof.admitted||proof.bodyVisits!=values[route].size()||proof.retainedBytes)return fail("reference must execute complete original body");
        } else if(proof.admitted) {
            if(proof.examined!=values[route].size()||proof.candidates+proof.omitted!=values[route].size()
                ||proof.bodyVisits!=proof.candidates||proof.candidates>4096||proof.retainedBytes>4096*sizeof(std::size_t))
                return fail("actual workset/body-count/bounded admission");
            if(requirePositive) {
                if(!proof.candidates||!proof.omitted)return fail("native positive actual body omission");
                ++counts.nativeAdmitted;counts.nativeCandidates+=proof.candidates;
                counts.nativeOmitted+=proof.omitted;counts.nativeBodyVisits+=proof.bodyVisits;
            }
        } else {
            if(proof.bodyVisits!=values[route].size()||proof.retainedBytes||requirePositive)return fail("required admission or exact full fallback");
        }
        queues[route]=queueProof(values[route]);
        if(label.find("variant=6 ")!=std::string::npos) {
            const auto blocked=std::find_if(values[route].begin(),values[route].end(),[](const auto& value){return value.cell.x==2&&value.cell.y==2;});
            const auto hazard=std::find_if(values[route].begin(),values[route].end(),[](const auto& value){return value.cell.x==63&&value.cell.y==63;});
            const auto* actor=findObjectById(state,moverId);if(!actor)return fail("blocked-resource mover");
            if(blocked==values[route].end()||!blocked->resource||hazard==values[route].end()||hazard->hazardCost!=0
                ||(runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Air&&(blocked->cost!=-7||blocked->passable)))
                return fail("actual blocked resource cost/base hazard reset witness");
        }
        if(!capture(state,after[route],error))return fail("after full writer: "+error);
        if(!(before[route]==after[route]))return fail("read-only build changed complete world/save/special/persistent");
    }
    for(int route=1;route<3;++route)if(!(before[0]==before[route])||!(after[0]==after[route])
        ||!sameSnapshot(values[0],values[route],counts.fieldCells)||!compareQueue(queues[0],queues[route],counts))
        return fail("three routes complete authority/order/12fields/Queue results+all stats");
    return true;
}

bool synthetic(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path dynamic workset synthetic: "+why;return false;};
    for(int variant=0;variant<10;++variant) {
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        for(auto& box:states) {
            box=std::make_unique<RuntimeWindowState>();auto& state=*box;RuntimeSnapshotActiveStateScope active(state);
            if(!RuntimePlannerColdLookupContractDetail::largeFixture(state,config,0,0,error))return false;
            IniFile ini;std::string rules=std::string(RuntimePlannerPassabilityContractDetail::rulesText)
                +"[Clear]\nTrack=100%\nFoot=100%\nFloat=0%\nHover=100%\n"
                 "[Rock]\nTrack=0%\nFoot=0%\nFloat=0%\nHover=0%\n"
                 "[Tiberium]\nTrack=75%\nFoot=50%\nFloat=0%\nHover=100%\n";
            if(variant==8)rules+="[CAR]\nThreatAvoidanceCoefficient=1\n[MAN]\nThreatPosed=25\nPrimary=WorksetGun\n"
                "[WeaponTypes]\n0=WorksetGun\n[WorksetGun]\nDamage=10\nRange=6\nROF=1\nProjectile=WorksetBullet\nSpeed=100\nWarhead=WorksetWH\n"
                "[WorksetBullet]\nImage=none\n[Warheads]\n0=WorksetWH\n[WorksetWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n";
            if(!ini.loadFromString(rules)||!state.rules.loadFromIni(ini))return fail("rules reload");
            state.capabilityCache.profilesByType.clear();
            if(variant==1)state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=RuntimeTerrainLandType::Rock;
            if(variant==2)state.terrainInfoByCell.erase(runtimeCellKey(3,3));
            if(variant==3)state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=static_cast<RuntimeTerrainLandType>(127);
            if(variant==4)for(auto& [key,info]:state.terrainInfoByCell){(void)key;info.authoredLandType=RuntimeTerrainLandType::Rock;}
            if(variant==7) {
                state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                const int id=state.specialMechanisms.spawnRadiationArea(1,"Player",{2,2},4,100,1,"",state.tick,true,SpecialEventSink{});
                const auto at=state.specialMechanisms.radiationAreas().find(id);
                if(id<0||at==state.specialMechanisms.radiationAreas().end()||at->second.strength<=0||at->second.remainingTicks<=0)return fail("actual radiation positive");
                state.specialMechanismsInitialized=true;
            }
            if(variant==9) {
                auto& worldCells=state.world.accessTerrainCells();worldCells.reserve(worldCells.size()+2);
                for(const auto point:{RuntimeCellPoint{-1,2},RuntimeCellPoint{1001,0}}){TerrainCell cell;cell.x=point.x;cell.y=point.y;worldCells.push_back(cell);}
                state.terrainByCell=buildRuntimeTerrainLookup(state.world);
            }
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;state.foundationCollisionTick=-1;
            invalidateRuntimeTickOccupancyCache(state);(void)completeRuntimeWorldRefresh(state);
            // Directly authored base values are intentional: proof must inspect
            // actual copied values, not static index shape or metadata revision.
            (void)runtimeStaticPathTerrainSnapshot(state);
            if(variant==5)for(auto& cell:state.pathTerrainCache.staticCells)cell.resource=true;
            if(variant==6) {
                auto& base=state.pathTerrainCache.staticCells;bool blockedResource=false;
                for(auto& cell:base) {
                    if(cell.cell.x==2&&cell.cell.y==2){cell.resource=true;cell.cost=-7;cell.passable=false;blockedResource=true;}
                    if(cell.cell.x==63&&cell.cell.y==63)cell.hazardCost=-13;
                }
                if(!blockedResource)return fail("blocked-resource authored base witness");
            }
            if(variant==8){const auto* actor=findObjectById(state,1);if(!actor||runtimePathThreatAreas(state,*actor,runtimePathThreatAvoidance(state,*actor)).empty())return fail("actual threat positive");}
        }
        for(int mover:{1,2,3,4,5,6})for(int allowed:{-1,30,33}) {
            if(!compareBuild(states,mover,allowed,counts,error,"variant="+std::to_string(variant)+" mover="+std::to_string(mover)))return false;
            ++counts.synthetic;
        }
        // New independent observer cannot forge the old observers' counts.
        // Each existing diagnostic forces a genuine complete original loop.
        if(variant==0)for(int mode=0;mode<2;++mode) {
            auto& state=*states[2];RuntimeSnapshotActiveStateScope active(state);Route route(2);
            Observation witness;witness.expected=&state;state.pathTerrainCache.dynamicValid=false;
            RuntimePathDynamicSparseQueriesContractDetail::Observation oldSparse;oldSparse.expected=&state;
            RuntimePathSnapshotConditionalQueriesContractDetail::Observation oldConditional;oldConditional.expected=&state;
            {
                RuntimePathDynamicWorksetObserverScope observe(&Observation::append,&witness);
                RuntimePathDynamicSparseQueryObserverScope sparse(mode==0?&RuntimePathDynamicSparseQueriesContractDetail::Observation::append:nullptr,&oldSparse);
                RuntimePathSnapshotConditionalQueryObserverScope conditional(mode==1?&RuntimePathSnapshotConditionalQueriesContractDetail::Observation::observe:nullptr,&oldConditional);
                const auto* actor=findObjectById(state,1);if(!actor)return fail("observer mover");
                const auto& output=buildRuntimePathTerrainSnapshot(state,*actor);
                if(output.empty())return fail("observer output");
            }
            if(witness.foreign||witness.calls.size()!=1||witness.calls[0].admitted
                ||witness.calls[0].bodyVisits!=state.pathTerrainCache.dynamicCells.size())return fail("old observer must visit actual full body");
            if(mode==0&&(oldSparse.foreign||oldSparse.calls.size()!=1||oldSparse.calls[0].proofs<state.pathTerrainCache.dynamicCells.size()))return fail("old sparse proof statistics retained");
            if(mode==1&&(oldConditional.foreign||oldConditional.calls.size()!=1
                ||oldConditional.calls[0].metadataLookups+oldConditional.calls[0].metadataSkipped!=state.pathTerrainCache.dynamicCells.size()
                ||oldConditional.calls[0].occupantLookups+oldConditional.calls[0].occupantSkipped!=state.pathTerrainCache.dynamicCells.size()))return fail("old conditional exact query counts retained");
            ++counts.observerFallbacks;
        }
        if(variant==0) {
            // Independent old-reference/forced-occupancy guards must keep the
            // complete real body, without relying on Route0's combined flags.
            auto& state=*states[2];RuntimeSnapshotActiveStateScope active(state);Route route(2);
            const auto* actor=findObjectById(state,1);if(!actor)return fail("independent reference mover");
            state.pathTerrainCache.dynamicValid=false;
            RuntimePathDynamicWorksetReferenceScope full(true);
            const auto gold=std::vector<PathTerrainCell>(buildRuntimePathTerrainSnapshot(state,*actor));
            for(int variantRef=0;variantRef<6;++variantRef) {
                RuntimePathDynamicWorksetReferenceScope newDefault(false);
                RuntimeSimulationQueryReuseReferenceScope reuse(variantRef==0);
                RuntimePathSnapshotMetadataQueryReferenceScope metadata(variantRef==1);
                RuntimePathSnapshotOccupancyQueryReferenceScope occupancy(variantRef==2);
                RuntimePathDynamicSparseQueriesReferenceScope sparse(variantRef==3);
                RuntimePathDynamicCellBatchReferenceScope batch(variantRef==4);
                Observation witness;witness.expected=&state;state.pathTerrainCache.dynamicValid=false;
                std::vector<PathTerrainCell> values;
                {RuntimePathDynamicWorksetObserverScope observe(&Observation::append,&witness);
                 values=buildRuntimePathTerrainSnapshot(state,*actor,-1,nullptr,variantRef==5);}
                if(witness.foreign||witness.calls.size()!=1||witness.calls.front().admitted
                    ||witness.calls.front().bodyVisits!=values.size()||!sameSnapshot(gold,values,counts.fieldCells))
                    return fail("independent old reference/force original fallback "+std::to_string(variantRef));
                ++counts.independentFallbacks;
            }
            // Current node values change under the SAME generation and node
            // address. A fresh builder must observe the new authored permission.
            for(int change=0;change<2;++change) {
                for(auto& box:states) {
                    auto& current=*box;const auto generation=current.terrainInfoByCell.generation();
                    auto& info=current.terrainInfoByCell.at(runtimeCellKey(63,62));const auto* address=&info;
                    info.authoredLandType=change==0?RuntimeTerrainLandType::Rock:RuntimeTerrainLandType::Clear;
                    if(current.terrainInfoByCell.generation()!=generation
                        ||&current.terrainInfoByCell.at(runtimeCellKey(63,62))!=address)return fail("in-place value proof precondition");
                    current.pathTerrainCache.dynamicValid=false;
                }
                if(!compareBuild(states,1,-1,counts,error,"same-node current permission="+std::to_string(change)))return false;
                for(const auto& box:states) {
                    const auto& output=box->pathTerrainCache.dynamicCells;
                    const auto value=std::find_if(output.begin(),output.end(),[](const auto& v){return v.cell.x==63&&v.cell.y==62;});
                    if(value==output.end()||value->passable!=(change!=0))return fail("actual same-node land permission output changed");
                }
                ++counts.currentValueBuilds;
            }
        }
    }
    return true;
}

bool native(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path dynamic workset native: "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded)return fail("actual stock assets");
    for(int side:{240,256})for(int history:{0,10000}) {
        const int population=side==240?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;std::array<std::vector<int>,3> ids;
        std::array<int,2> movers{-1,-1};
        for(int route=0;route<3;++route) {
            states[route]=std::make_unique<RuntimeWindowState>();auto& state=*states[route];
            state.config=config;state.report=assets->report;state.rules=assets->rules;state.art=assets->art;state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded;state.artLoaded=assets->report.artLoaded;state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,population,ids[route],error,false,true,false,side,side,true,true))return fail("normal producer: "+error);
            state.config.defaultMapPath=state.report.map.path;
            if(state.config.defaultMapPath!="MassMovement-v1.map"||ids[route].size()!=static_cast<std::size_t>(population)
                ||state.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side))return fail("native cardinality/source");
            if(route==0)for(int id:ids[route]) {
                const auto* actor=findObjectById(state,id);if(!actor)return fail("native selected ID");
                if(runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Ground)continue;
                if(actor->kind==WorldObjectKind::Unit&&movers[0]<0)movers[0]=id;
                if(actor->kind==WorldObjectKind::Infantry&&movers[1]<0)movers[1]=id;
            }
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);if(first<=0||first>std::numeric_limits<int>::max()-history)return fail("history capacity");
                state.world.accessObjects().reserve(state.world.getObjects().size()+history);
                for(int n=0;n<history;++n){auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";state.world.accessObjects().push_back(std::move(dead));}
                auto info=std::move(state.terrainInfoByCell);state.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(state);state.terrainInfoByCell=std::move(info);
                invalidateRuntimeTickOccupancyCache(state);state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]||ids[0]!=ids[2]||movers[0]<0||movers[1]<0)return fail("native identical Ground/Infantry IDs");
        for(int mover:movers) {
            if(!compareBuild(states,mover,-1,counts,error,"side="+std::to_string(side)+" history="+std::to_string(history),true))return false;
            ++counts.nativeScenarios;
        }
        std::vector<int> infantry;
        for(int id:ids[0]){const auto* actor=findObjectById(*states[0],id);if(actor&&actor->kind==WorldObjectKind::Infantry)infantry.push_back(id);}
        if(infantry.empty())return fail("native actual infantry selection");
        bool moved=false;std::size_t admittedDuringSimulation=0;
        for(int tick=1;tick<=16;++tick) {
            std::array<Full,3> current;std::map<int,RuntimeCellPoint> destinationsGold;int accepted=-1,rejected=-1;
            for(int route=0;route<3;++route) {
                auto& state=*states[route];state.tick=tick;RuntimeSnapshotActiveStateScope active(state);Route references(route);
                Observation observe;observe.expected=&state;
                {
                    RuntimePathDynamicWorksetObserverScope diagnostic(&Observation::append,&observe);
                    if(tick==1||tick==9) {
                        const RuntimeCellPoint goal=tick==9?RuntimeCellPoint{side/2+22,side/2+48}:RuntimeCellPoint{side/2+76,side/2+32};
                        std::map<int,RuntimeCellPoint> destinations;
                        if(!issueRuntimeMassMovementOrder(state,tick==9?infantry:ids[route],goal,destinations,error,true))return fail("normal command: "+error);
                        if(route==0){destinationsGold=destinations;accepted=state.lastCommand.acceptedCount;rejected=state.lastCommand.rejectedCount;}
                        else if(destinations!=destinationsGold||accepted!=state.lastCommand.acceptedCount||rejected!=state.lastCommand.rejectedCount)return fail("normal destinations/accepted/rejected/stockChrono recovery");
                        ++counts.nativeCommands;
                    }
                    (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                }
                if(observe.foreign)return fail("simulation observer owner");
                for(const auto& proof:observe.calls) {
                    if(route==2)++counts.nativeSimulationOwners;
                    if(route<2&&(proof.admitted||proof.bodyVisits!=proof.entries))return fail("simulation reference actual full body");
                    if(route==2&&proof.admitted) {
                        if(proof.bodyVisits!=proof.candidates||proof.candidates+proof.omitted!=proof.entries)return fail("simulation actual candidate-body count");
                        admittedDuringSimulation+=proof.omitted>0;
                        counts.nativeSimulationAdmitted+=proof.omitted>0;
                        counts.nativeSimulationOmitted+=proof.omitted;
                    }
                }
                if(!capture(state,current[route],error))return fail("completed tick full writer: "+error);
                moved|=std::any_of(state.behaviors.begin(),state.behaviors.end(),[](const auto& item){return item.second.moveCooldownTicks>0||!item.second.path.empty();});
            }
            if(!(current[0]==current[1])||!(current[0]==current[2]))return fail("three routes per-tick full world/save/paths/systems");
            ++counts.nativeTickTriplets;
        }
        if(!moved||!admittedDuringSimulation)return fail("actual normal gameplay must move and use workset");
    }
    return true;
}
} // namespace RuntimePathDynamicWorksetContractDetail

bool runRuntimePathDynamicWorksetActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePathDynamicWorksetContractDetail;
    Counts counts;
    if(!primitive(counts,error)||!synthetic(config,counts,error)||!native(config,counts,error))return false;
    if(counts.synthetic!=180||counts.nativeScenarios!=8||counts.nativeCommands!=24||counts.nativeTickTriplets!=64
        ||counts.nativeAdmitted!=8||counts.observerFallbacks!=2||!counts.primitive||!counts.fieldCells
        ||counts.independentFallbacks!=6||counts.currentValueBuilds!=2
        ||!counts.nativeSimulationOwners||!counts.nativeSimulationAdmitted||!counts.nativeSimulationOmitted
        ||!counts.queueResults||!counts.queueStats||!counts.nativeCandidates||!counts.nativeOmitted
        ||counts.nativeCandidates!=counts.nativeBodyVisits) {
        error="path dynamic workset: complete coverage/real-body witness counts";return false;
    }
    std::cout<<"Path dynamic workset: syntheticCases="<<counts.synthetic<<" nativeScenarios="<<counts.nativeScenarios
        <<" fullFieldCells="<<counts.fieldCells<<" fields=12 queueResults="<<counts.queueResults<<" queueStats="<<counts.queueStats
        <<" nativeCommands="<<counts.nativeCommands<<" nativeTickTriplets="<<counts.nativeTickTriplets
        <<" nativeAdmitted="<<counts.nativeAdmitted<<" nativeCandidates="<<counts.nativeCandidates
        <<" nativeOmitted="<<counts.nativeOmitted<<" nativeBodyVisits="<<counts.nativeBodyVisits
        <<" observerFallbacks="<<counts.observerFallbacks
        <<" independentFallbacks="<<counts.independentFallbacks<<" currentValueBuilds="<<counts.currentValueBuilds
        <<" nativeSimulationOwners="<<counts.nativeSimulationOwners
        <<" nativeSimulationAdmitted="<<counts.nativeSimulationAdmitted<<" nativeSimulationOmitted="<<counts.nativeSimulationOmitted
        <<" original/C31/workset/order/profile/cache/paths/fullWriter/native240/native256/H0/H10000=exact passed=yes\n";
    error.clear();return true;
}
