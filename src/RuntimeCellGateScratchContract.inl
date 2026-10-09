// New contract only; all original gate/planner contracts remain unchanged.
#pragma once
#include <array>
#include <limits>
#include <memory>
#include <tuple>
namespace RuntimeCellGateScratchContractDetail {
using namespace RuntimePlannerPassabilityContractDetail;
using Full=Authority;
using Queries=Observations;
struct Counts {
    std::size_t boundaries=0,scalarPairs=0,occupancyPairs=0,directedEdges=0,syntheticMaps=0;
    std::size_t maps=0,commands=0,tickPairs=0,nativeFastMaps=0,nativeFastCommands=0;
    RuntimeCellGateScratchStats totals;
};
void add(Counts& c,const RuntimeCellGateScratchStats& s) {
    c.totals.tailQueries+=s.tailQueries;c.totals.exactMisses+=s.exactMisses;
    c.totals.referenceOriginal+=s.referenceOriginal;c.totals.indexedOriginal+=s.indexedOriginal;
}
bool sane(const RuntimeCellGateScratchStats& s,bool reference) {
    return s.tailQueries==s.exactMisses+s.referenceOriginal+s.indexedOriginal
        && (reference ? !s.exactMisses&&!s.indexedOriginal : !s.referenceOriginal);
}
bool boundaries(Counts& c,std::string& error) {
    const auto fail=[&](const std::string& v){error="cell gate scratch boundary: "+v;return false;};
    auto a=std::make_unique<RuntimeWindowState>(),b=std::make_unique<RuntimeWindowState>();
    const int key=runtimeCellKey(0,1);
    if(key!=runtimeCellKey(1000,0))return fail("legacy aliases changed");
    RuntimeCellGateScratchReferenceScope optimized(false);
    RuntimeCellGateScratchStats outer;RuntimeCellGateScratchStatsScope collect(outer);
    const auto check=[&](const RuntimeWindowState& state,int k,bool expected){
        const bool actual=runtimeCellGateScratchExactMiss(state,k);++c.boundaries;
        if(actual!=expected){error="cell gate scratch: current gate-map miss mismatch boundary="+std::to_string(c.boundaries);return false;}
        return true;
    };
    if(!check(*a,key,true))return false;
    a->gateIdsByCell[key]={}; // Present empty vector must retain old helper route.
    if(!check(*a,key,false)||!check(*a,runtimeCellKey(1000,0),false))return false;
    a->gateIdsByCell[key]={-3,32,32,999999};
    if(!check(*a,key,false)||!check(*b,key,true))return false;
    {RuntimeCellGateScratchReferenceScope reference(true);
        if(!check(*a,key,false)||!check(*b,key,false))return false;
        {RuntimeCellGateScratchReferenceScope inner(false);if(!check(*b,key,true))return false;}
        if(!check(*b,key,false))return false;
    }
    if(!check(*b,key,true))return false;
    ++a->tick;if(!check(*a,key,false))return false;
    a->gateIdsByCell.erase(key);if(!check(*a,key,true))return false;
    a->gateIdsByCell[runtimeCellKey(512,0)]={32};
    if(!check(*a,runtimeCellKey(512,0),false)||!check(*a,key,true))return false;
    {RuntimeCellGateScratchStats inner;RuntimeCellGateScratchStatsScope nested(inner);
        if(!check(*a,key,true)||inner.exactMisses!=1||inner.tailQueries!=1)return fail("nested stats");}
    if(!check(*a,key,true))return false;
    if(c.boundaries!=16||outer.tailQueries!=15||outer.referenceOriginal!=3
        ||outer.indexedOriginal!=5||outer.exactMisses!=7)return fail("exact boundary accounting");
    add(c,outer);return true;
}
bool synthetic(const RuntimeConfig& config,Counts& c,std::string& error) {
    const auto fail=[&](const std::string& why){error="cell gate scratch synthetic: "+why;return false;};
    constexpr std::array<RuntimeCellPoint,16> points{{{-1,0},{0,-1},{1000,0},{0,1},{512,0},{0,0},
        {8,8},{8,7},{8,4},{10,5},{9,4},{8,5},{8,6},{5,8},{63,63},{300,300}}};
    const std::array<std::unordered_set<int>,3> soft{{{}, {35}, {35,40}}};
    for(int history:{0,10000})for(int phase:{0,1}) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
        if(!RuntimePlannerColdLookupContractDetail::largeFixture(s,config,history,phase,error))return false;
        const auto occupied=buildRuntimeOccupancyLookup(s);Full before,after;
        if(!capture(s,before,error))return false;
        for(int id:{1,2,3,4,5,6}) {
            const auto* mover=findObjectById(s,id);if(!mover)return fail("actual mover missing");
            for(const auto& point:points)for(int allowed:{-1,1,35,30,-3})for(int softness=0;softness<4;++softness)
                for(bool crush:{false,true}) {
                    const auto* softPtr=softness?&soft[softness-1]:nullptr;
                    bool expected=false;
                    for(int branch=0;branch<2;++branch) {
                        RuntimeCellGateScratchReferenceScope route(branch==0);RuntimeCellGateScratchStats stats;
                        RuntimeCellGateScratchStatsScope record(stats);
                        const bool value=runtimeCellIsPassableUncached(s,s.terrainByCell,occupied,point,*mover,allowed,softPtr,crush);
                        if(!branch)expected=value;else if(value!=expected)return fail("actual scalar full tail mismatch");
                        if(!sane(stats,branch==0))return fail("scalar stats partition");add(c,stats);
                    }
                    ++c.scalarPairs;
                }
            std::vector<bool> expectedEdges;std::vector<std::pair<int,int>> expectedMap;Queries expectedQueries;
            std::optional<RuntimeCellPoint> expectedTie;
            for(int branch=0;branch<2;++branch) {
                RuntimeCellGateScratchReferenceScope route(branch==0);RuntimeCellGateScratchStats stats;
                RuntimeCellGateScratchStatsScope record(stats);std::vector<bool> edges;
                {RuntimeBuildingDepartureReadOnlyQueryScope departure(s,*mover);
                    const bool inf=mover->kind==WorldObjectKind::Infantry;
                    RuntimeSubcellReadOnlyQueryScope subcells(s.world.getObjects(),inf);
                    RuntimePlannerInvisibleBuildingScope invisible(s,inf);
                    RuntimePlannerCellPassabilityScope memo(s,*mover,s.terrainByCell,occupied);
                    RuntimePlannerLandPermissionScope land(s,*mover);
                    RuntimePlannerConnectivityKernel kernel(s,*mover,s.terrainByCell,occupied,false,&memo);
                    RuntimePlannerEmptyCellCensusScope empty(s,(inf||(!runtimeComponentCrushAliasReference()&&kernel.queryLeaseEnabled()))
                        &&!runtimeComponentPassabilityReference()&&!runtimeComponentKernelReference()&&kernel.precomputedMode()!=PathMovementMode::Air);
                    for(int y=-1;y<=14;++y)for(int x=-1;x<=14;++x)for(const auto& next:runtimeNeighborsOf({x,y}))
                        edges.push_back(kernel.connects({x,y},next));}
                Queries queries;std::unordered_map<int,int> result;
                {RuntimePlannerPassabilityObserverScope observe(&Queries::append,&queries);
                    result=buildRuntimeMoveComponentByCell(s,*mover,s.terrainByCell,occupied);}
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;if(auto it=result.find(runtimeCellKey(mover->x,mover->y));it!=result.end())required=it->second;
                const auto tie=findNearestRuntimePassableCellWithOccupancy(s,*mover,{500,500},-1,nullptr,s.terrainByCell,occupied,nullptr,&result,required);
                if(!branch){expectedEdges=std::move(edges);expectedMap=order;expectedQueries=std::move(queries);expectedTie=tie;}
                else if(edges!=expectedEdges||order!=expectedMap||queries.values!=expectedQueries.values||tie!=expectedTie)
                    return fail("directed booleans/ordered predicates/complete map/global tie");
                if(!sane(stats,branch==0))return fail("graph stats partition");add(c,stats);
                ++c.syntheticMaps;c.directedEdges+=16*16*8;
            }
        }
        // Force the exact late occupancy tail on a genuinely clear original cell.
        // Supplied representative IDs remain authoritative even when forged/stale.
        const auto* clearMover=findObjectById(s,1);
        for(int occupant:{1,-3,runtimeTerrainOccupantId(0),runtimeTerrainOccupantId(1),35,36,999999,0})
            for(int allowed:{-1,1,35,30,-3})for(int softness=0;softness<4;++softness)for(bool crush:{false,true}) {
                auto supplied=occupied;supplied[runtimeCellKey(20,20)]=occupant;
                const auto* softPtr=softness?&soft[softness-1]:nullptr;bool expected=false;
                for(int branch=0;branch<2;++branch){RuntimeCellGateScratchReferenceScope route(branch==0);
                    RuntimeCellGateScratchStats stats;RuntimeCellGateScratchStatsScope record(stats);
                    const bool value=runtimeCellIsPassableUncached(s,s.terrainByCell,supplied,{20,20},*clearMover,allowed,softPtr,crush);
                    if(!branch)expected=value;else if(value!=expected)return fail("forced current representative tail");
                    const bool directExpected=occupant==1||(softPtr&&softPtr->contains(occupant)&&s.behaviors.contains(occupant));
                    if(value!=directExpected||stats.tailQueries!=1||!sane(stats,branch==0)
                        ||(branch?stats.exactMisses!=1:stats.referenceOriginal!=1))return fail("forced true occupancy/crush/soft ordering witness");
                    add(c,stats);
                }
                ++c.occupancyPairs;
            }
        if(!capture(s,after,error)||!(before==after))return fail("queries changed full writer authority");
        // All same-address content edits occur after every read-only scope ends.
        const int key=runtimeCellKey(20,20);auto& info=s.terrainInfoByCell.at(key);const auto originalInfo=info;
        for(int change=0;change<3;++change) {
            info=originalInfo;
            if(change==0)info.passable=!info.passable;
            else if(change==1)info.water=!info.water;
            else info.cost+=91;
            const auto* mover=findObjectById(s,1);bool expected=false;
            for(int branch=0;branch<2;++branch){RuntimeCellGateScratchReferenceScope route(branch==0);
                const bool value=runtimeCellIsPassableUncached(s,s.terrainByCell,occupied,{20,20},*mover);
                if(!branch)expected=value;else if(value!=expected)return fail("same-address current metadata");}
        }
    }
    return c.scalarPairs==4*6*16*5*4*2&&c.occupancyPairs==4*8*5*4*2&&c.syntheticMaps==4*6*2&&c.directedEdges==4*6*2*16*16*8
        ?true:fail("complete synthetic matrix counts");
}
bool native(const RuntimeConfig& config,Counts& counts,std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    const auto fail=[&](const std::string& why){error="cell gate scratch native: "+why;return false;};
    RuntimeComponentPassabilityReferenceScope passability(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrain(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    RuntimeComponentCrushAliasReferenceScope alias(false);
    RuntimeComponentIndexedTopologyReferenceScope indexed(true);
    RuntimeComponentColdLookupReferenceScope cold(false);
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded)return fail("stock assets unavailable");
    for(int side:{240,256})for(int history:{0,10000}) {
        const int population=side==240?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;std::array<std::vector<int>,2> ids;
        std::array<int,2> movers{-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& s=*states[branch];
            s.config=config;s.report=assets->report;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
            s.rulesLoaded=assets->report.rulesLoaded;s.artLoaded=assets->report.artLoaded;s.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(s);
            if(!prepareRuntimeMassMovementWorld(s,population,ids[branch],error,false,true,false,side,side,true,true))return false;
            s.config.defaultMapPath=s.report.map.path;
            if(s.report.map.path!="MassMovement-v1.map"||!s.mapFactsLoaded
                ||s.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side)
                ||ids[branch].size()!=static_cast<std::size_t>(population))return fail("native fixture identity/cardinality");
            if(history){
                const int first=nextRuntimeObjectInstanceId(s);
                if(first<=0||first>std::numeric_limits<int>::max()-history)return fail("history ID bound");
                s.world.accessObjects().reserve(s.world.getObjects().size()+history);
                for(int n=0;n<history;++n){auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";s.world.accessObjects().push_back(std::move(dead));}
                auto metadata=std::move(s.terrainInfoByCell);s.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(s);s.terrainInfoByCell=std::move(metadata);
                s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false;invalidateRuntimeTickOccupancyCache(s);
            }
            (void)completeRuntimeWorldRefresh(s);
            if(!branch)for(int id:ids[branch]){const auto* actor=findObjectById(s,id);if(!actor)return fail("native actor");
                if(runtimeObjectMovementMode(s,*actor)!=PathMovementMode::Ground)continue;
                if(actor->kind==WorldObjectKind::Unit&&movers[0]<0)movers[0]=id;
                if(actor->kind==WorldObjectKind::Infantry&&movers[1]<0)movers[1]=id;}
        }
        if(ids[0]!=ids[1]||movers[0]<0||movers[1]<0)return fail("native identical actor order");
        for(int id:movers){
            std::vector<std::pair<int,int>> expectedMap;Queries expectedQueries;std::optional<RuntimeCellPoint> expectedTie;
            Full expectedBefore;
            for(int branch=0;branch<2;++branch){auto& s=*states[branch];RuntimeSnapshotActiveStateScope active(s);
                RuntimeCellGateScratchReferenceScope route(branch==0);
                const auto* mover=findObjectById(s,id);if(!mover)return fail("component actor");
                const auto occupied=buildRuntimeOccupancyLookup(s);Full before,after;
                if(!capture(s,before,error))return false;
                if(!branch)expectedBefore=before;else if(!(before==expectedBefore))
                    return fail("native complete original initial writer identity");
                RuntimeCellGateScratchStats witness;Queries queries;std::unordered_map<int,int> result;
                {RuntimeCellGateScratchStatsScope owner(witness);
                    RuntimePlannerPassabilityObserverScope observe(&Queries::append,&queries);
                    result=buildRuntimeMoveComponentByCell(s,*mover,s.terrainByCell,occupied);}
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;if(const auto found=result.find(runtimeCellKey(mover->x,mover->y));found!=result.end())required=found->second;
                if(required<0||order.empty())return fail("native complete component not exercised");
                const auto tie=findNearestRuntimePassableCellWithOccupancy(s,*mover,{500,500},-1,nullptr,
                    s.terrainByCell,occupied,nullptr,&result,required);
                if(!tie)return fail("native actual global fallback winner absent");
                if(!branch){expectedMap=order;expectedQueries=std::move(queries);expectedTie=tie;}
                else if(order!=expectedMap||queries.values!=expectedQueries.values||tie!=expectedTie)
                    return fail("entire native ordered component/query/global equal-score winner");
                const bool groundUnit=mover->kind==WorldObjectKind::Unit;
                if(!sane(witness,branch==0)||(groundUnit && (!witness.tailQueries || (branch?!witness.exactMisses:!witness.referenceOriginal)))
                    ||(!groundUnit && witness.tailQueries))return fail("native ground/infantry exact cold route witness");
                if(branch&&groundUnit)++counts.nativeFastMaps;add(counts,witness);++counts.maps;
                if(!capture(s,after,error)||!(before==after))return fail("BFS changed complete full writer");
            }
        }
        bool moved=false;
        for(int tick=1;tick<=32;++tick){Full expected;std::map<int,RuntimeCellPoint> expectedDestinations;int accepted=-1,rejected=-1;
            for(int branch=0;branch<2;++branch){auto& s=*states[branch];s.tick=tick;
                RuntimeSnapshotActiveStateScope active(s);RuntimeCellGateScratchReferenceScope route(branch==0);
                if(tick==1||tick==9||tick==17){std::map<int,RuntimeCellPoint> destinations;
                    const int mid=side/2;const RuntimeCellPoint goal=tick==9?RuntimeCellPoint{mid+22,mid+48}:RuntimeCellPoint{mid+76,mid+32};
                    RuntimeCellGateScratchStats witness;
                    {RuntimeCellGateScratchStatsScope observe(witness);
                        if(!issueRuntimeMassMovementOrder(s,ids[branch],goal,destinations,error,true))return false;}
                    if(!branch){expectedDestinations=destinations;accepted=s.lastCommand.acceptedCount;rejected=s.lastCommand.rejectedCount;}
                    else if(destinations!=expectedDestinations||accepted!=s.lastCommand.acceptedCount||rejected!=s.lastCommand.rejectedCount)
                        return fail("native actual command destination/accepted/rejected trajectory");
                    if(!sane(witness,branch==0)||!witness.tailQueries||(branch?!witness.exactMisses:!witness.referenceOriginal))
                        return fail("native mixed command true cold/no-gate route witness");
                    if(branch)++counts.nativeFastCommands;add(counts,witness);++counts.commands;
                }
                // Complete the real tick before calling the original writer.
                (void)advanceRuntimeSimulation(s);(void)completeRuntimeWorldRefresh(s);
                Full current;if(!capture(s,current,error))return false;
                if(!branch)expected=std::move(current);else if(!(current==expected))return fail("native completed-tick all worlds/systems/save");
                moved|=std::any_of(s.behaviors.begin(),s.behaviors.end(),[](const auto& v){return v.second.moveCooldownTicks>0||!v.second.path.empty();});
            }
            ++counts.tickPairs;
        }
        if(!moved)return fail("native actual movement absent");
    }
    return counts.maps==16&&counts.commands==24&&counts.tickPairs==128&&counts.nativeFastMaps==4&&counts.nativeFastCommands==12
        ?true:fail("native full matrix/witness count");
}

} // namespace RuntimeCellGateScratchContractDetail
bool runRuntimeCellGateScratchActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeCellGateScratchContractDetail;
    RuntimeComponentPassabilityReferenceScope passability(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrain(false);
    RuntimeComponentMetadataBorrowReferenceScope metadata(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    RuntimeComponentCrushAliasReferenceScope alias(false);
    RuntimeComponentIndexedTopologyReferenceScope indexed(true);
    RuntimeComponentColdLookupReferenceScope cold(false);
    Counts c;
    if(!boundaries(c,error)||!synthetic(config,c,error))return false;
    for(bool reference:{true,false}) {
        RuntimeCellGateScratchReferenceScope route(reference);
        RuntimeCellGateScratchStats gateStats;
        {RuntimeCellGateScratchStatsScope record(gateStats);
            if(runRuntimeGateIntegrationSmokeTest()!=0){error="cell gate scratch: original actual gate integration failed";return false;}}
        if(!sane(gateStats,reference)||!gateStats.tailQueries
            ||(reference?!gateStats.referenceOriginal:!gateStats.indexedOriginal)) {
            error="cell gate scratch: original fully-open gate indexed cold route not executed";return false;}
        add(c,gateStats);
        if(!runRuntimePlannerTerrainQueriesDirectedContract(config,error))return false;
    }
    if(!native(config,c,error))return false;
    if(c.boundaries!=16||c.scalarPairs!=15360||c.occupancyPairs!=1280||c.directedEdges!=98304||c.syntheticMaps!=48
        ||c.maps!=16||c.commands!=24||c.tickPairs!=128||c.nativeFastMaps!=4||c.nativeFastCommands!=12
        ||!c.totals.exactMisses||!c.totals.referenceOriginal||!c.totals.indexedOriginal) {
        error="cell gate scratch: matrix/witness counts boundaries="+std::to_string(c.boundaries)
            +" scalarPairs="+std::to_string(c.scalarPairs)+" occupancyPairs="+std::to_string(c.occupancyPairs)+" directedEdges="+std::to_string(c.directedEdges)
            +" syntheticMaps="+std::to_string(c.syntheticMaps)+" nativeMaps="+std::to_string(c.maps)
            +" commands="+std::to_string(c.commands)+" tickPairs="+std::to_string(c.tickPairs)
            +" fastMaps="+std::to_string(c.nativeFastMaps)+" fastCommands="+std::to_string(c.nativeFastCommands)
            +" exactMisses="+std::to_string(c.totals.exactMisses)+" referenceOriginal="+std::to_string(c.totals.referenceOriginal)
            +" indexedOriginal="+std::to_string(c.totals.indexedOriginal);return false;
    }
    std::cout<<"Cell gate scratch: boundaries=16 exactScalarPairs=15360 exactOccupancyTailPairs=1280 exactDirectedEdges=98304 completeOrderedSyntheticMaps=48 nativeWholeMaps=16 nativeCommands=24 nativeTickFullSavePairs=128 nativeGroundFastMaps=4 nativeMixedFastCommands=12 oldGateIntegrationRoutes=2 oldDirectedRoutes=2 exactMisses="
        <<c.totals.exactMisses<<" referenceOriginal="<<c.totals.referenceOriginal<<" indexedOriginal="<<c.totals.indexedOriginal
        <<" omittedContainerConstructions="<<2*c.totals.exactMisses
        <<" currentGateKey/soft/crush/allowed/terrainNegative/metadata/orderedBFS/globalTie/native240/native256/H0/H10000/fullWriter=exact passed=yes\n";
    return true;
}