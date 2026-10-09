#pragma once
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

// Additional actual contract. All existing Workset/Sparse/Batch contracts stay
// byte-for-byte unchanged and are still independently required by Check.
namespace RuntimePathDynamicResourceFusionContractDetail {
using Base=RuntimePathDynamicWorksetContractDetail::Counts;
using Full=RuntimePathDynamicWorksetContractDetail::Full;
using QueueStep=RuntimePathDynamicWorksetContractDetail::QueueStep;
using RuntimePathDynamicWorksetContractDetail::capture;
using RuntimePathDynamicWorksetContractDetail::sameSnapshot;
using RuntimePathDynamicWorksetContractDetail::queueProof;
using RuntimePathDynamicWorksetContractDetail::compareQueue;
using RuntimePathDynamicWorksetContractDetail::object;
using Workset=RuntimePathDynamicWorkset<PathTerrainCell>;
struct Route {
    RuntimeSimulationQueryReuseReferenceScope reuse;
    RuntimePathSnapshotMetadataQueryReferenceScope metadata;
    RuntimePathSnapshotOccupancyQueryReferenceScope representative;
    RuntimePathDynamicSparseQueriesReferenceScope sparse;
    RuntimePathDynamicCellBatchReferenceScope batch;
    RuntimePathDynamicWorksetReferenceScope workset;
    RuntimePathDynamicPreparationReferenceScope preparation;
    RuntimePathDynamicResourceFusionReferenceScope fusion;
    explicit Route(int route,bool late=false):reuse(route==0),metadata(route==0),representative(route==0),
        sparse(route==0),batch(route==0),workset(route==0),preparation(late),fusion(route!=2) {}
};
struct Observation {
    const void* expected=nullptr;bool foreign=false;
    std::vector<RuntimePathDynamicResourceFusionStats> calls;
    static void append(void* opaque,const void* state,const RuntimePathDynamicResourceFusionStats& stats) {
        auto& value=*static_cast<Observation*>(opaque);value.foreign|=state!=value.expected;value.calls.push_back(stats);
    }
};
struct Counts:Base {
    std::size_t primitive=0,syntheticCases=0,nativeCases=0,positiveNative=0;
    std::size_t resourceVisits=0,bodyVisits=0,resourceRows=0,nativeResourceRows=0;
    std::size_t observers=0,earlyLate=0,finiteFallbacks=0,currentLandValues=0;
    std::size_t simulationOwners=0,simulationResources=0,simulationBodyVisits=0;
};
bool primitive(Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path resource fusion primitive: "+why;return false;};
    RuntimePathDynamicWorksetReferenceScope normal(false);RuntimePathDynamicResourceFusionReferenceScope fusion(false);
    int state=1,mover=2,tick=70,foreign=4;
    std::vector<PathTerrainCell> cells(128*128);
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){auto& c=cells[y*128+x];c.cell={x,y};c.resource=true;c.passable=true;c.cost=10;}
    cells.push_back(cells[585]); // Authored duplicate remains an independent row.
    const auto original=cells;
    Workset::Occupants occupied{{1001,-7},{2002,mover}};
    std::unordered_map<int,RuntimeTerrainInfo> info;info[3003].authoredLandType=RuntimeTerrainLandType::Rock;
    const auto find=[&](int key)->const RuntimeTerrainInfo*{const auto at=info.find(key);return at==info.end()?nullptr:&at->second;};
    const auto allows=[](const RuntimeTerrainInfo& value){return value.authoredLandType!=RuntimeTerrainLandType::Rock;};
    const auto repair=[](auto&& mark){mark(4,4);};
    cells[5*128+5].hazardCost=-1;cells[6*128+6].water=true;cells[7*128+7].passable=false;
    const auto input=cells;
    {
        Workset proof(&state,&mover,tick,cells,occupied,true,true,false,true,repair,find,allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});
        if(!proof.viewFor(&state,&mover,tick,cells,occupied)||!proof.resourceFusionEnabled()
            ||proof.indicesForDiagnostics()!=std::vector<std::size_t>{129,258,387,516,645,774,903}
            ||proof.resourceIndicesForDiagnostics().size()!=cells.size()-7
            ||proof.resourceIndicesForDiagnostics().back()!=cells.size()-1)
            return fail("neutral resource union/negative ID/self/repair/permission/hazard/water/blocked/duplicate");
        for(std::size_t n=0;n<cells.size();++n)if(RuntimePathSnapshotMetadataQueriesContractDetail::fields(cells[n])
            !=RuntimePathSnapshotMetadataQueriesContractDetail::fields(input[n]))return fail("proof construction wrote output");
        if(proof.viewFor(&foreign,&mover,tick,cells,occupied)||proof.viewFor(&state,&foreign,tick,cells,occupied)
            ||proof.viewFor(&state,&mover,tick+1,cells,occupied))return fail("foreign/tick guard");
        {RuntimePathDynamicResourceFusionReferenceScope reference(true);
         if(proof.viewFor(&state,&mover,tick,cells,occupied))return fail("current independent reference guard");}
        Workset nested(&state,&mover,tick,cells,occupied,true,true,false,true,repair,find,allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});
        if(nested.viewFor(&state,&mover,tick,cells,occupied)||proof.viewFor(&state,&mover,tick,cells,occupied))return fail("nested mask");
        ++counts.primitive;
    }
    for(double percent:{0.0,-70.0,70.0,90.0,100.0,0.01,10000.0}) {
        auto current=original;Workset::Occupants empty;
        Workset proof(&state,&mover,tick,current,empty,true,true,false,true,[](auto&&){},find,
            [](const RuntimeTerrainInfo&){return true;},std::false_type{},RuntimePathDynamicResourceFusionPolicy{percent});
        if(!proof.viewFor(&state,&mover,tick,current,empty)||!proof.resourceFusionEnabled()
            ||proof.resourceIndicesForDiagnostics().size()!=current.size()||!proof.indicesForDiagnostics().empty())return fail("finite math admission");
        for(std::size_t index:proof.resourceIndicesForDiagnostics()) {
            auto& value=current[index];if(percent<=0)value.passable=false;else value.cost=runtimeLandTypeCost(value.cost,percent);
            const int expected=percent<=0?10:runtimeLandTypeCost(10,percent);
            if(value.cost!=expected||value.passable!=(percent>0))return fail("original percentage/clamp/zero formula");
        }
        ++counts.primitive;
    }
    for(double percent:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto current=original;Workset::Occupants empty;
        Workset proof(&state,&mover,tick,current,empty,true,true,false,true,[](auto&&){},find,allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{percent});
        if(proof.resourceFusionEnabled()||proof.viewFor(&state,&mover,tick,current,empty)
            ||proof.stats().reason!=RuntimePathDynamicWorksetReason::CandidateDensity)return fail("nonfinite original full fallback");
        ++counts.finiteFallbacks;++counts.primitive;
    }
    for(int negative=0;negative<5;++negative) {
        auto current=original;Workset::Occupants entries;
        if(negative==0)entries.emplace(-1,-7);
        if(negative==1)current[8000].cell={1000,0};
        if(negative==2)for(int n=0;n<4097;++n)entries.emplace(runtimeCellKey(n%128,n/128),-7);
        if(negative==3)for(int n=0;n<4097;++n)current[n].hazardCost=1;
        const auto before=current;
        Workset proof(&state,&mover,tick,current,entries,negative!=4,true,false,true,[](auto&&){},find,allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});
        if(proof.viewFor(&state,&mover,tick,current,entries)||proof.resourceFusionEnabled()
            ||!proof.resourceIndicesForDiagnostics().empty())return fail("rejection must publish no resources");
        for(std::size_t n=0;n<before.size();++n)if(RuntimePathSnapshotMetadataQueriesContractDetail::fields(current[n])
            !=RuntimePathSnapshotMetadataQueriesContractDetail::fields(before[n]))return fail("fallback double cost/output mutation");
        ++counts.primitive;
    }
    {
        std::vector<PathTerrainCell> full(Workset::slots);Workset::Occupants empty;
        for(std::size_t n=0;n<full.size();++n){full[n].cell={static_cast<int>(n%512),static_cast<int>(n/512)};full[n].resource=true;full[n].passable=true;}
        Workset proof(&state,&mover,tick,full,empty,true,true,false,true,[](auto&&){},
            [](int)->const RuntimeTerrainInfo*{return nullptr;},allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});
        if(!proof.viewFor(&state,&mover,tick,full,empty)||!proof.resourceFusionEnabled()
            ||!proof.indicesForDiagnostics().empty()||proof.resourceIndicesForDiagnostics().size()!=Workset::slots
            ||proof.resourceIndicesForDiagnostics().capacity()>Workset::slots)
            return fail("complete512-squared authored resource/capacity bound");
        ++counts.primitive;
    }
    const void* previous=runtimeActivePathDynamicWorkset;
    {
        auto current=original;Workset::Occupants empty;std::size_t lookups=0;
        const auto before=current;
        Workset proof(&state,&mover,tick,current,empty,true,true,false,true,[](auto&&){},
            [&](int)->const RuntimeTerrainInfo*{if(++lookups==1000)throw std::bad_alloc{};return nullptr;},allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});
        if(proof.viewFor(&state,&mover,tick,current,empty)||proof.resourceFusionEnabled()
            ||!proof.resourceIndicesForDiagnostics().empty()||proof.stats().reason!=RuntimePathDynamicWorksetReason::Allocation)
            return fail("partially prepared resource allocation failure publishes nothing");
        for(std::size_t n=0;n<before.size();++n)if(RuntimePathSnapshotMetadataQueriesContractDetail::fields(current[n])
            !=RuntimePathSnapshotMetadataQueriesContractDetail::fields(before[n]))return fail("partial proof failure changed output");
        ++counts.primitive;
    }
    try {
        auto current=original;Workset::Occupants empty;
        Workset proof(&state,&mover,tick,current,empty,true,true,false,true,[](auto&&){},
            [](int)->const RuntimeTerrainInfo*{throw std::runtime_error("test only");},allows,
            std::false_type{},RuntimePathDynamicResourceFusionPolicy{70.0});return fail("exception not propagated");
    }catch(const std::runtime_error&){}
    if(runtimeActivePathDynamicWorkset!=previous)return fail("throw restores outer TLS");
    ++counts.primitive;return true;
}
// Test-only helpers for the C36 failure investigation. They do not participate
// in production caching or replace any existing equality assertion.
bool staticIndexMatchesAuthoredRows(const RuntimePathTerrainSnapshotCache& cache) {
    const auto& rows=cache.staticCells;
    if(cache.staticNextIndexByCell.size()!=rows.size())return false;
    std::unordered_map<int,std::size_t> first;
    std::vector<std::size_t> next(rows.size(),std::numeric_limits<std::size_t>::max());
    for(std::size_t index=0;index<rows.size();++index) {
        const auto [at,inserted]=first.emplace(runtimeCellKey(rows[index].cell.x,rows[index].cell.y),index);
        if(!inserted){next[index]=next[at->second];next[at->second]=index;}
    }
    return first==cache.staticFirstIndexByCell && next==cache.staticNextIndexByCell;
}
std::string fullDifference(const Full& a,const Full& b) {
#define RRF_FULL_FIELD(field) if(a.field!=b.field)return #field
    RRF_FULL_FIELD(authority.world);RRF_FULL_FIELD(authority.subcells);
    RRF_FULL_FIELD(authority.gates);RRF_FULL_FIELD(authority.repair);RRF_FULL_FIELD(authority.mag);
    RRF_FULL_FIELD(special);RRF_FULL_FIELD(persistent);
#undef RRF_FULL_FIELD
    if(a.authority.serialized!=b.authority.serialized) {
        const auto count=std::min(a.authority.serialized.size(),b.authority.serialized.size());
        std::size_t index=0;
        while(index<count && a.authority.serialized[index]==b.authority.serialized[index])++index;
        return "authority.serialized offset="+std::to_string(index)+" originalSize="
            +std::to_string(a.authority.serialized.size())+" actualSize="+std::to_string(b.authority.serialized.size());
    }
    return "none";
}
std::string snapshotDifference(const std::vector<PathTerrainCell>& a,const std::vector<PathTerrainCell>& b) {
    if(a.size()!=b.size())return "size original="+std::to_string(a.size())+" actual="+std::to_string(b.size());
    constexpr const char* names[]{"x","y","level","passable","water","shore","bridge","ramp","cliff","resource","cost","hazardCost"};
    for(std::size_t index=0;index<a.size();++index) {
        const auto values=[](const PathTerrainCell& cell) {
            return std::array<long long,12>{cell.cell.x,cell.cell.y,cell.level,cell.passable,cell.water,
                cell.shore,cell.bridge,cell.ramp,cell.cliff,cell.resource,cell.cost,cell.hazardCost};
        };
        const auto original=values(a[index]),actual=values(b[index]);
        for(std::size_t field=0;field<original.size();++field)if(original[field]!=actual[field])
            return "offset="+std::to_string(index)+" cell="+std::to_string(a[index].cell.x)+","
                +std::to_string(a[index].cell.y)+" field="+names[field]+" original="
                +std::to_string(original[field])+" actual="+std::to_string(actual[field]);
    }
    return "none";
}
std::string queueDifference(const std::vector<QueueStep>& a,const std::vector<QueueStep>& b) {
    if(a.size()!=b.size())return "steps size";
    for(std::size_t index=0;index<a.size();++index) {
        const auto prefix="step="+std::to_string(index)+" ";
        if(a[index].processed!=b[index].processed)return prefix+"processed";
        const auto& original=a[index].stats;const auto& actual=b[index].stats;
#define RRF_QUEUE_STAT(field) if(original.field!=actual.field)return prefix+"stats." #field
        RRF_QUEUE_STAT(pendingCount);RRF_QUEUE_STAT(cacheHits);RRF_QUEUE_STAT(cacheMisses);
        RRF_QUEUE_STAT(budgetExhaustedResults);RRF_QUEUE_STAT(lastPumpBudgetExhausted);
        RRF_QUEUE_STAT(backpressureRejectedRequests);RRF_QUEUE_STAT(requestOverflowRejectedRequests);
        RRF_QUEUE_STAT(overflowRejectedResults);RRF_QUEUE_STAT(collapsedPendingRequests);
        RRF_QUEUE_STAT(massAttackCollapsedRequests);RRF_QUEUE_STAT(sharedApproachCacheHits);
        RRF_QUEUE_STAT(sharedApproachCacheMisses);RRF_QUEUE_STAT(repeatedFailureSuppressed);
        RRF_QUEUE_STAT(noPathCooldownHits);RRF_QUEUE_STAT(cooldownNoPathResults);RRF_QUEUE_STAT(realNoPathResults);
        RRF_QUEUE_STAT(equivalentStartBucketCollapses);RRF_QUEUE_STAT(goalSlotAssignments);
        RRF_QUEUE_STAT(fallbackCandidatesConsidered);RRF_QUEUE_STAT(fallbackRingRequests);
        RRF_QUEUE_STAT(fallbackTargetCacheHits);RRF_QUEUE_STAT(fallbackTargetCacheMisses);
        RRF_QUEUE_STAT(mapQueryApproachRequests);RRF_QUEUE_STAT(mapQueryApproachUsedResults);
        RRF_QUEUE_STAT(lastPumpExpandedNodes);RRF_QUEUE_STAT(cachedPathCount);RRF_QUEUE_STAT(fallbackTargetCacheEntries);
        RRF_QUEUE_STAT(fallbackDistanceQueries);RRF_QUEUE_STAT(fallbackComponentCellChecks);
        RRF_QUEUE_STAT(heuristicEvaluations);RRF_QUEUE_STAT(usesDenseTerrainLookup);RRF_QUEUE_STAT(usesShortHashKeys);
        RRF_QUEUE_STAT(mapQueryApproachAvailable);RRF_QUEUE_STAT(usesBlockedComponentApproachFallback);
#undef RRF_QUEUE_STAT
        if(a[index].results.size()!=b[index].results.size())return prefix+"results size";
        for(std::size_t result=0;result<a[index].results.size();++result) {
            const auto& x=a[index].results[result];const auto& y=b[index].results[result];
            const auto at=prefix+"result="+std::to_string(result)+" ";
#define RRF_QUEUE_RESULT(field) if(x.field!=y.field)return at+#field
            RRF_QUEUE_RESULT(requestId);RRF_QUEUE_RESULT(start.x);RRF_QUEUE_RESULT(start.y);
            RRF_QUEUE_RESULT(goal.x);RRF_QUEUE_RESULT(goal.y);RRF_QUEUE_RESULT(resolvedGoal.x);RRF_QUEUE_RESULT(resolvedGoal.y);
            RRF_QUEUE_RESULT(completed);RRF_QUEUE_RESULT(reachable);RRF_QUEUE_RESULT(cacheHit);RRF_QUEUE_RESULT(cacheMiss);
            RRF_QUEUE_RESULT(goalAdjusted);RRF_QUEUE_RESULT(searchBudgetExhausted);RRF_QUEUE_RESULT(queueBackpressure);
            RRF_QUEUE_RESULT(fallbackTargetCacheHit);RRF_QUEUE_RESULT(fallbackRing);RRF_QUEUE_RESULT(mapQueryApproachUsed);
            RRF_QUEUE_RESULT(sharedApproachCacheHit);RRF_QUEUE_RESULT(collapsedFromEquivalentRequest);RRF_QUEUE_RESULT(status);
            RRF_QUEUE_RESULT(passabilityProfileHash);RRF_QUEUE_RESULT(requestClass);RRF_QUEUE_RESULT(expandedNodes);
            RRF_QUEUE_RESULT(expandedNodeBudget);RRF_QUEUE_RESULT(fallbackTargetsTried);RRF_QUEUE_RESULT(fallbackCandidateCount);
            RRF_QUEUE_RESULT(goalSlotIndex);RRF_QUEUE_RESULT(noPathCooldownTicksRemaining);RRF_QUEUE_RESULT(failureReason);
#undef RRF_QUEUE_RESULT
            if(x.path.size()!=y.path.size())return at+"path size";
            for(std::size_t cell=0;cell<x.path.size();++cell)
                if(x.path[cell].x!=y.path[cell].x||x.path[cell].y!=y.path[cell].y)return at+"path cell="+std::to_string(cell);
        }
    }
    return "none";
}

bool compareBuild(std::array<std::unique_ptr<RuntimeWindowState>,3>& states,int moverId,int allowed,
    Counts& counts,std::string& error,const std::string& label,bool positive=false,bool late=false) {
    const auto fail=[&](const std::string& why){error="path resource fusion "+label
        +" mover="+std::to_string(moverId)+" allowed="+std::to_string(allowed)+": "+why;return false;};
    std::array<Full,3> before,after;std::array<std::vector<PathTerrainCell>,3> values;
    std::array<std::vector<QueueStep>,3> queues;
    for(int route=0;route<3;++route) {
        auto& state=*states[route];RuntimeSnapshotActiveStateScope active(state);Route refs(route,late);
        if(!capture(state,before[route],error))return fail("before complete writer: "+error);
        state.pathTerrainCache.dynamicValid=false;Observation observe;observe.expected=&state;
        {
            RuntimePathDynamicResourceFusionObserverScope diagnostic(&Observation::append,&observe);
            const auto* actor=findObjectById(state,moverId);if(!actor)return fail("actual mover");
            values[route]=buildRuntimePathTerrainSnapshot(state,*actor,allowed);
            if(!sameSnapshot(values[route],buildRuntimePathTerrainSnapshot(state,*actor,allowed),counts.fieldCells))return fail("warm full cache fields/order");
        }
        if(values[route].empty()||observe.foreign||observe.calls.size()!=1)return fail("one cold owner/no warm reconstruction");
        const auto& proof=observe.calls.front();
        if(route<2&&(proof.admitted||proof.resourceRows||proof.resourceVisits))return fail("references never fuse resources");
        if(route==2&&proof.admitted) {
            if(proof.examined!=values[route].size()||proof.resourceVisits!=proof.resourceRows
                ||proof.bodyVisits!=proof.bodyCandidates||proof.bodyCandidates>4096
                ||proof.retainedResourceBytes>values[route].size()*sizeof(std::size_t))return fail("actual visit/allocation counts");
            counts.resourceVisits+=proof.resourceVisits;counts.resourceRows+=proof.resourceRows;counts.bodyVisits+=proof.bodyVisits;
        }
        if(positive&&route==2) {
            if(!proof.admitted||proof.resourceRows<10000||proof.resourceRows!=proof.resourceVisits)return fail("native >=10k actual resource math calls");
            ++counts.positiveNative;counts.nativeResourceRows+=proof.resourceRows;
            const auto& base=state.pathTerrainCache.staticCells;bool changedCost=false;
            for(std::size_t n=0;n<base.size();++n)changedCost|=base[n].resource&&values[route][n].passable&&base[n].cost!=values[route][n].cost;
            if(!changedCost)return fail("actual nonidentity 70/90 resource cost witness");
        }
        queues[route]=queueProof(values[route]);
        if(!capture(state,after[route],error))return fail("route="+std::to_string(route)+" after complete writer: "+error);
        if(!(before[route]==after[route]))return fail("route="+std::to_string(route)
            +" read-only full writer/authority "+fullDifference(before[route],after[route]));
    }
    for(int route=1;route<3;++route) {
        const auto prefix="route="+std::to_string(route)+" vs original ";
        if(!(before[0]==before[route]))return fail(prefix+"before full writer "+fullDifference(before[0],before[route]));
        if(!(after[0]==after[route]))return fail(prefix+"after full writer "+fullDifference(after[0],after[route]));
        if(!sameSnapshot(values[0],values[route],counts.fieldCells))
            return fail(prefix+"ordered all12fields "+snapshotDifference(values[0],values[route]));
        if(!compareQueue(queues[0],queues[route],counts))
            return fail(prefix+"complete Queue Result+Stats/cache "+queueDifference(queues[0],queues[route]));
    }
    return true;
}
bool synthetic(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path resource fusion synthetic: "+why;return false;};
    for(int variant=0;variant<18;++variant) {
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        for(auto& box:states) {
            box=std::make_unique<RuntimeWindowState>();auto& state=*box;RuntimeSnapshotActiveStateScope active(state);
            if(!RuntimePlannerColdLookupContractDetail::largeFixture(state,config,variant/6==0?0:variant/6==1?1000:10000,0,error))return false;
            IniFile ini;const double percent=variant==12?0:variant==13?-20:variant==14?0.01:variant==15?10000:70;
            std::string rules=std::string(RuntimePlannerPassabilityContractDetail::rulesText)
                +"[Clear]\nTrack=100%\nFoot=100%\nFloat=0%\nHover=100%\n"
                 "[Rock]\nTrack=0%\nFoot=0%\nFloat=0%\nHover=0%\n"
                 "[Tiberium]\nTrack="+std::to_string(percent)+"%\nFoot=90%\nFloat=0%\nHover=100%\n";
            if(variant==5)rules+="[CAR]\nThreatAvoidanceCoefficient=1\n[MAN]\nThreatPosed=25\nPrimary=FusionGun\n"
                "[WeaponTypes]\n0=FusionGun\n[FusionGun]\nDamage=10\nRange=6\nROF=1\nProjectile=FusionBullet\nSpeed=100\nWarhead=FusionWH\n"
                "[FusionBullet]\nImage=none\n[Warheads]\n0=FusionWH\n[FusionWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n";
            if(!ini.loadFromString(rules)||!state.rules.loadFromIni(ini))return fail("rules");
            state.capabilityCache.profilesByType.clear();
            for(auto& cell:state.world.accessTerrainCells())if(cell.x>=0&&cell.y>=0&&cell.x<512&&cell.y<512)setRuntimeResourceTerrainBails(cell,"TIB",1);
            if(variant==1||variant==16)state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=RuntimeTerrainLandType::Rock;
            if(variant==2||variant==17)state.terrainInfoByCell.erase(runtimeCellKey(3,3));
            if(variant==3)for(auto& [key,info]:state.terrainInfoByCell){(void)key;info.authoredLandType=RuntimeTerrainLandType::Rock;}
            if(variant==4) {
                state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                const int id=state.specialMechanisms.spawnRadiationArea(1,"Player",{2,2},4,100,1,"",state.tick,true,SpecialEventSink{});
                const auto at=state.specialMechanisms.radiationAreas().find(id);
                if(id<0||at==state.specialMechanisms.radiationAreas().end()||at->second.strength<=0||at->second.remainingTicks<=0)return fail("actual radiation");
                state.specialMechanismsInitialized=true;
            }
            if(variant==6) {
                auto& cells=state.world.accessTerrainCells();cells.reserve(cells.size()+2);
                for(const auto p:{RuntimeCellPoint{-1,2},RuntimeCellPoint{1001,0}}){TerrainCell cell;cell.x=p.x;cell.y=p.y;cells.push_back(cell);}
                state.terrainByCell=buildRuntimeTerrainLookup(state.world);
            }
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;state.foundationCollisionTick=-1;
            invalidateRuntimeTickOccupancyCache(state);(void)completeRuntimeWorldRefresh(state);(void)runtimeStaticPathTerrainSnapshot(state);
            if(variant==7)for(auto& cell:state.pathTerrainCache.staticCells){cell.resource=true;cell.cost=-7;cell.passable=false;}
            if(variant==8)for(auto& cell:state.pathTerrainCache.staticCells)cell.hazardCost=-13;
            if(variant==9)for(auto& cell:state.pathTerrainCache.staticCells){cell.water=true;cell.shore=false;cell.bridge=false;}
            if(variant==10)for(auto& cell:state.pathTerrainCache.staticCells)cell.bridge=true;
            if(variant==11) {
                // The C36 failing fixture reversed private cache rows while
                // retaining their old index. Keep that corruption as an
                // explicit negative witness; it is not a passing equivalence
                // case or a supported production mutation.
                auto& cache=state.pathTerrainCache;
                if(!staticIndexMatchesAuthoredRows(cache))return fail("reverse baseline index invariant");
                const auto original=cache.staticCells;
                const auto found=cache.staticFirstIndexByCell.find(runtimeCellKey(6,6));
                if(found==cache.staticFirstIndexByCell.end()||found->second>=original.size())return fail("stale fixture foundation index witness missing");
                const auto offset=found->second;
                std::reverse(cache.staticCells.begin(),cache.staticCells.end());
                if(staticIndexMatchesAuthoredRows(cache)||!runtimePathSnapshotHasCellIndex(cache,cache.staticCells)
                    ||offset!=78||original[offset].cell.x!=6||original[offset].cell.y!=6
                    ||cache.staticCells[offset].cell.x!=49||cache.staticCells[offset].cell.y!=62)
                    return fail("private reversed cache must demonstrate stale index (6,6)@78 -> (49,62)");
                std::cout<<"Path resource fusion invalid-private-cache witness: cachedKey=6006 offset="<<offset
                    <<" original=6,6 reversed=49,62 sizeOnlyGuard=yes equivalence=not-tested\n";
                // Reorder the real authored source, then use the original
                // builder to regenerate rows and ALL first/next indices.
                std::reverse(state.world.accessTerrainCells().begin(),state.world.accessTerrainCells().end());
                state.terrainByCell=buildRuntimeTerrainLookup(state.world);
                cache.valid=cache.dynamicValid=false;cache.terrainCellCount=0;
                state.foundationCollisionTick=-1;invalidateRuntimeTickOccupancyCache(state);
                (void)completeRuntimeWorldRefresh(state);
                (void)runtimeStaticPathTerrainSnapshot(state);
                if(!staticIndexMatchesAuthoredRows(cache)||cache.staticCells.size()!=original.size())
                    return fail("reordered authored world original builder index invariant");
                const auto& worldRows=state.world.getTerrainCells();
                for(std::size_t n=0;n<original.size();++n) {
                    if(RuntimePathSnapshotMetadataQueriesContractDetail::fields(cache.staticCells[n])
                        !=RuntimePathSnapshotMetadataQueriesContractDetail::fields(original[original.size()-1-n])
                        ||cache.staticCells[n].cell.x!=worldRows[n].x||cache.staticCells[n].cell.y!=worldRows[n].y)
                        return fail("original builder must retain reverse author order and every static field offset="+std::to_string(n));
                }
            }
            if(variant==5){const auto* actor=findObjectById(state,1);if(!actor||runtimePathThreatAreas(state,*actor,runtimePathThreatAvoidance(state,*actor)).empty())return fail("actual threat");}
        }
        for(int mover:{1,2,3,4,5,6})for(int allowed:{-1,30,33}) {
            if(!compareBuild(states,mover,allowed,counts,error,"variant="+std::to_string(variant)))return false;++counts.syntheticCases;
        }
        if(variant==0) {
            auto& state=*states[2];RuntimeSnapshotActiveStateScope active(state);Route refs(2);
            for(int kind=0;kind<4;++kind) {
                Observation observe;observe.expected=&state;state.pathTerrainCache.dynamicValid=false;
                RuntimePathDynamicWorksetContractDetail::Observation workset;workset.expected=&state;
                RuntimePathDynamicPreparationContractDetail::PreparationObservation preparation;preparation.expected=&state;
                RuntimePathDynamicSparseQueriesContractDetail::Observation sparse;sparse.expected=&state;
                RuntimePathSnapshotConditionalQueriesContractDetail::Observation conditional;conditional.expected=&state;
                {
                    RuntimePathDynamicResourceFusionObserverScope own(&Observation::append,&observe);
                    RuntimePathDynamicWorksetObserverScope a(kind==0?&RuntimePathDynamicWorksetContractDetail::Observation::append:nullptr,&workset);
                    RuntimePathDynamicPreparationObserverScope b(kind==1?&RuntimePathDynamicPreparationContractDetail::PreparationObservation::append:nullptr,&preparation);
                    RuntimePathDynamicSparseQueryObserverScope c(kind==2?&RuntimePathDynamicSparseQueriesContractDetail::Observation::append:nullptr,&sparse);
                    RuntimePathSnapshotConditionalQueryObserverScope d(kind==3?&RuntimePathSnapshotConditionalQueriesContractDetail::Observation::observe:nullptr,&conditional);
                    const auto* actor=findObjectById(state,1);if(!actor)return fail("observer actor");
                    (void)buildRuntimePathTerrainSnapshot(state,*actor);
                }
                if(observe.foreign||observe.calls.size()!=1||observe.calls[0].admitted||observe.calls[0].resourceVisits)return fail("all old observers reject fusion");
                if((kind==0&&(workset.foreign||workset.calls.size()!=1))
                    ||(kind==1&&(preparation.foreign||preparation.calls.size()!=1))
                    ||(kind==2&&(sparse.foreign||sparse.calls.size()!=1))
                    ||(kind==3&&(conditional.foreign||conditional.calls.size()!=1)))return fail("original diagnostic callbacks remain real");
                ++counts.observers;
            }
            for(int preparationRef=0;preparationRef<2;++preparationRef) {
                if(!compareBuild(states,1,-1,counts,error,"early/late",false,preparationRef!=0))return false;++counts.earlyLate;
            }
            for(int permission=0;permission<2;++permission) {
                for(auto& box:states){auto& current=*box;auto& info=current.terrainInfoByCell.at(runtimeCellKey(63,62));
                    const auto generation=current.terrainInfoByCell.generation();const auto* pointer=&info;
                    info.authoredLandType=permission?RuntimeTerrainLandType::Clear:RuntimeTerrainLandType::Rock;
                    if(generation!=current.terrainInfoByCell.generation()||pointer!=&current.terrainInfoByCell.at(runtimeCellKey(63,62)))return fail("same-address current land precondition");
                    current.pathTerrainCache.dynamicValid=false;}
                if(!compareBuild(states,1,-1,counts,error,"current metadata"))return false;++counts.currentLandValues;
            }
        }
    }
    return true;
}
bool native(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why){error="path resource fusion native: "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded)return fail("actual stock assets");
    const auto* tib=assets->rules.findLandType("Tiberium");if(!tib||tib->track!=70.0||tib->foot!=90.0)return fail("actual stock Track70/Foot90");
    for(int side:{240,256})for(int history:{0,10000}) {
        const int population=side==240?48:256;std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        std::array<std::vector<int>,3> ids;std::array<int,2> movers{-1,-1};
        for(int route=0;route<3;++route) {
            states[route]=std::make_unique<RuntimeWindowState>();auto& state=*states[route];state.config=config;
            state.report=assets->report;state.rules=assets->rules;state.art=assets->art;state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded;state.artLoaded=assets->report.artLoaded;state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,population,ids[route],error,false,true,false,side,side,true,true))return fail("normal producer: "+error);
            state.config.defaultMapPath=state.report.map.path;
            if(state.config.defaultMapPath!="MassMovement-v1.map"||ids[route].size()!=static_cast<std::size_t>(population)
                ||state.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side))return fail("native source/cardinality");
            if(route==0)for(int id:ids[route]){const auto* actor=findObjectById(state,id);if(!actor)return fail("native ID");
                if(runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Ground)continue;
                if(actor->kind==WorldObjectKind::Unit&&movers[0]<0)movers[0]=id;
                if(actor->kind==WorldObjectKind::Infantry&&movers[1]<0)movers[1]=id;}
            for(auto& cell:state.world.accessTerrainCells())setRuntimeResourceTerrainBails(cell,"TIB",1);
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);if(first<=0||first>std::numeric_limits<int>::max()-history)return fail("history capacity");
                state.world.accessObjects().reserve(state.world.getObjects().size()+history);
                for(int n=0;n<history;++n){auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";state.world.accessObjects().push_back(std::move(dead));}
                auto metadata=std::move(state.terrainInfoByCell);state.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(state);state.terrainInfoByCell=std::move(metadata);
            }
            invalidateRuntimeTickOccupancyCache(state);state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]||ids[0]!=ids[2]||movers[0]<0||movers[1]<0)return fail("same Ground/Infantry IDs");
        for(int mover:movers){if(!compareBuild(states,mover,-1,counts,error,"large ore",true))return false;++counts.nativeCases;}
        std::vector<int> infantry;for(int id:ids[0]){const auto* actor=findObjectById(*states[0],id);if(actor&&actor->kind==WorldObjectKind::Infantry)infantry.push_back(id);}
        bool moved=false;std::size_t positiveOwners=0;
        for(int tick=1;tick<=16;++tick) {
            std::array<Full,3> current;std::map<int,RuntimeCellPoint> gold;int accepted=-1,rejected=-1;
            for(int route=0;route<3;++route) {
                auto& state=*states[route];state.tick=tick;RuntimeSnapshotActiveStateScope active(state);Route refs(route);
                Observation observe;observe.expected=&state;
                {
                    RuntimePathDynamicResourceFusionObserverScope diagnostic(&Observation::append,&observe);
                    if(tick==1||tick==9){const RuntimeCellPoint goal=tick==9?RuntimeCellPoint{side/2+22,side/2+48}:RuntimeCellPoint{side/2+76,side/2+32};
                        std::map<int,RuntimeCellPoint> destinations;
                        if(!issueRuntimeMassMovementOrder(state,tick==9?infantry:ids[route],goal,destinations,error,true))return fail("normal command: "+error);
                        if(route==0){gold=destinations;accepted=state.lastCommand.acceptedCount;rejected=state.lastCommand.rejectedCount;}
                        else if(destinations!=gold||accepted!=state.lastCommand.acceptedCount||rejected!=state.lastCommand.rejectedCount)return fail("actual targets/acceptance/stock chrono");
                        ++counts.nativeCommands;}
                    (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                }
                if(observe.foreign)return fail("simulation owner");
                for(const auto& proof:observe.calls){if(route<2&&(proof.admitted||proof.resourceVisits))return fail("simulation old refs no fusion");
                    if(route==2){++counts.simulationOwners;if(proof.admitted){if(proof.resourceVisits!=proof.resourceRows||proof.bodyVisits!=proof.bodyCandidates)return fail("simulation real math/body counts");
                        counts.simulationResources+=proof.resourceVisits;counts.simulationBodyVisits+=proof.bodyVisits;positiveOwners+=proof.resourceRows>0;}}}
                if(!capture(state,current[route],error))return fail("completed tick full writer: "+error);
                moved|=std::any_of(state.behaviors.begin(),state.behaviors.end(),[](const auto& value){return value.second.moveCooldownTicks>0||!value.second.path.empty();});
            }
            if(!(current[0]==current[1])||!(current[0]==current[2]))return fail("three routes actual per-tick authority/full save/paths/systems");
            ++counts.nativeTickTriplets;
        }
        if(!moved||!positiveOwners)return fail("actual gameplay must move and fuse positive ore rows");
    }
    return true;
}
} // namespace
bool runRuntimePathDynamicResourceFusionActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePathDynamicResourceFusionContractDetail;Counts counts;
    if(!primitive(counts,error)||!synthetic(config,counts,error)||!native(config,counts,error))return false;
    if(counts.syntheticCases!=324||counts.nativeCases!=8||counts.positiveNative!=8||counts.nativeCommands!=24||counts.nativeTickTriplets!=64
        ||counts.observers!=4||counts.earlyLate!=2||counts.finiteFallbacks!=3||counts.currentLandValues!=2||counts.primitive!=19
        ||!counts.fieldCells||!counts.queueResults||!counts.queueStats||!counts.resourceRows||counts.resourceRows!=counts.resourceVisits
        ||counts.nativeResourceRows<80000||!counts.simulationOwners||!counts.simulationResources||!counts.simulationBodyVisits) {
        error="path resource fusion: complete coverage/math/actual writer witnesses";return false;
    }
    std::cout<<"Path dynamic resource fusion: syntheticCases="<<counts.syntheticCases<<" nativeScenarios="<<counts.nativeCases
        <<" fields=12 fullFieldCells="<<counts.fieldCells<<" queueResults="<<counts.queueResults<<" queueStats="<<counts.queueStats
        <<" nativeCommands="<<counts.nativeCommands<<" nativeTickTriplets="<<counts.nativeTickTriplets<<" positiveNative="<<counts.positiveNative
        <<" nativeResourceRows="<<counts.nativeResourceRows<<" resourceRows="<<counts.resourceRows<<" resourceVisits="<<counts.resourceVisits
        <<" bodyVisits="<<counts.bodyVisits<<" observerFallbacks="<<counts.observers<<" earlyLate="<<counts.earlyLate
        <<" nonfiniteFallbacks="<<counts.finiteFallbacks<<" currentLandValues="<<counts.currentLandValues
        <<" simulationOwners="<<counts.simulationOwners<<" simulationResources="<<counts.simulationResources
        <<" simulationBodyVisits="<<counts.simulationBodyVisits
        <<" original/C35/fusion/order/profile/cache/paths/fullWriter/resource70/resource90/native240/native256/H0/H10000=exact passed=yes\n";
    error.clear();return true;
}
