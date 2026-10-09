// New isolated contract. Existing complete BFS, gate, directed and strict-slot
// contracts remain intact; this checks actual mixed stock commands and writers.
#pragma once
#include <array>
#include <chrono>
#include <limits>
#include <memory>
namespace RuntimeMixedLocalComponentContractDetail {
using namespace RuntimePlannerPassabilityContractDetail;
struct Counts {
    std::size_t probeSchedules=0,boundaries=0,originalMaps=0,certifiedPoints=0;
    std::size_t cases=0,commands=0,tickTriplets=0,firstAdmitted=0;
    std::size_t forcedOutside=0,forcedNearest=0,orderedLateMaps=0;
    std::size_t nativeActors=0,nativeTypes=0;
    RuntimeMixedLocalComponentStats stats;
    std::array<double,3> commandMilliseconds{},maxCommandMilliseconds{};
};
void add(RuntimeMixedLocalComponentStats& a,const RuntimeMixedLocalComponentStats& b) {
    a.attempts+=b.attempts;a.admitted+=b.admitted;a.referenceFallbacks+=b.referenceFallbacks;
    a.guardFallbacks+=b.guardFallbacks;a.layoutFallbacks+=b.layoutFallbacks;
    a.walkFallbacks+=b.walkFallbacks;a.allocationFallbacks+=b.allocationFallbacks;
    a.layoutEntries+=b.layoutEntries;a.vertices+=b.vertices;a.orientedEdges+=b.orientedEdges;
    a.coveredBest+=b.coveredBest;a.connectedGoalHits+=b.connectedGoalHits;a.walkSteps+=b.walkSteps;
    a.detourExpansions+=b.detourExpansions;a.seedVetoes+=b.seedVetoes;a.edgeVetoes+=b.edgeVetoes;
    a.qualifiedChronoRecovery+=b.qualifiedChronoRecovery;a.idleActors+=b.idleActors;
    a.outsideFallbacks+=b.outsideFallbacks;a.nearestFallbacks+=b.nearestFallbacks;
    a.wholeMaps+=b.wholeMaps;a.lateWholeMaps+=b.lateWholeMaps;
    a.orderedMapsCompared+=b.orderedMapsCompared;a.orderedMapMismatches+=b.orderedMapMismatches;
    a.lateChronoWitness+=b.lateChronoWitness;a.lateAirWitness+=b.lateAirWitness;
    a.unmappedAttempts+=b.unmappedAttempts;a.unmappedProofs+=b.unmappedProofs;a.unmappedMemoHits+=b.unmappedMemoHits;
    a.incomingQueries+=b.incomingQueries;a.incomingMemoHits+=b.incomingMemoHits;a.incomingPositiveVetoes+=b.incomingPositiveVetoes;
    a.unmappedOriginalChecks+=b.unmappedOriginalChecks;a.unmappedOriginalMismatches+=b.unmappedOriginalMismatches;
}
std::vector<RuntimeCellPoint> originalProbeSchedule(RuntimeCellPoint desired) {
    std::vector<RuntimeCellPoint> out;std::deque<RuntimeCellPoint> frontier;
    std::unordered_set<int> visited;
    frontier.push_back(desired);visited.insert(runtimeCellKey(desired.x,desired.y));
    while(!frontier.empty() && visited.size()<320) {
        const auto current=frontier.front();frontier.pop_front();
        if(std::abs(current.x-desired.x)<=16 && std::abs(current.y-desired.y)<=16)out.push_back(current);
        for(const auto& next:runtimeNeighborsOf(current)) {
            if(std::abs(next.x-desired.x)>16 || std::abs(next.y-desired.y)>16)continue;
            const int key=runtimeCellKey(next.x,next.y);
            if(key<0 || visited.contains(key))continue;
            visited.insert(key);frontier.push_back(next);
        }
    }
    return out;
}
bool schedules(Counts& c,std::string& error) {
    for(const RuntimeCellPoint goal:std::array<RuntimeCellPoint,12>{{
            {142,168},{150,176},{0,0},{511,511},{0,511},{511,0},
            {-1,0},{0,-1},{1000,0},{0,1},{512,0},{256,256}}}) {
        const auto original=originalProbeSchedule(goal);
        if(original!=RuntimeMixedLocalComponentCertificate::probePointsForContract(goal)) {
            error="mixed local component: original FIFO/radius/visited-cap probe schedule mismatch";return false;
        }
        ++c.probeSchedules;
    }
    return true;
}
// A named slot per boundary prevents an omitted case being hidden by another
// repeated check. Each ordinary check constructs a fresh owned certificate.
enum Boundary { Positive,Reference,NestedOptimized,RestoredReference,NestedProof,
    OldObserver,OldReference,RepresentativeType,Mission,BehaviorKind,ReturnDeployed,
    BadOrigin,Dock,Cargo,DuplicateID,GateIndex,NullTerrain,AliasTerrain,MissingMetadata,
    VertexCap,EdgeCap,SeedVeto,EdgeVeto,CoveredDesired,OutsideDesired,TickFence,
    ReferenceFence,ExplicitInvalidate,FailedFirst,SingleUseRetry,FinalPositive,BoundaryCount };
bool boundaries(RuntimeWindowState& s,const std::vector<int>& ids,RuntimeCellPoint goal,
    Counts& c,std::string& error) {
    const auto fail=[&](const std::string& v){error="mixed local component boundary: "+v;return false;};
    std::array<bool,BoundaryCount> seen{};
    const auto record=[&](Boundary index,bool okay) {
        if(seen[index] || !okay) {error="mixed local component boundary index="+std::to_string(index);return false;}
        seen[index]=true;++c.boundaries;return true;
    };
    std::unordered_set<int> moving(ids.begin(),ids.end());
    const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
    RuntimeMixedLocalComponentReferenceScope optimized(false);
    const auto check=[&](Boundary index,bool expected,const auto& terrain,
        std::size_t vertices=4096,std::size_t edges=32768) {
        RuntimeMixedLocalComponentStats stats;RuntimeMixedLocalComponentStatsScope observe(stats);
        RuntimeMixedLocalComponentCertificate proof;
        const bool value=proof.prepare(s,ids,goal,terrain,occupied,ids.front(),ids.size(),vertices,edges);
        if(value!=expected || proof.enabled()!=expected || stats.attempts!=1
            || stats.admitted!=(expected?1U:0U) || stats.vertices>4096 || stats.orientedEdges>32768) {
            error="mixed local component boundary="+std::to_string(index)+" expected="+std::to_string(expected)
                +" value="+std::to_string(value)+" guard="+std::to_string(stats.guardFallbacks)
                +" layout="+std::to_string(stats.layoutFallbacks)+" walk="+std::to_string(stats.walkFallbacks);
            return false;
        }
        if(index==EdgeVeto) {
            const auto* isolated=findObjectById(s,ids[1]);
            if(!isolated || proof.certifiedKeys().contains(runtimeCellKey(isolated->x,isolated->y)))
                return fail("seed-tested but isolated endpoint must never be published connected");
        }
        return record(index,true);
    };
    if(!check(Positive,true,s.terrainByCell))return false;
    {RuntimeMixedLocalComponentReferenceScope reference(true);
        if(!check(Reference,false,s.terrainByCell))return false;
        {RuntimeMixedLocalComponentReferenceScope inner(false);if(!check(NestedOptimized,true,s.terrainByCell))return false;}
        if(!check(RestoredReference,false,s.terrainByCell))return false;}
    runtimeMixedLocalComponentProofActive=true;
    const bool nested=check(NestedProof,false,s.terrainByCell);runtimeMixedLocalComponentProofActive=false;
    if(!nested)return false;
    {Observations values;RuntimePlannerPassabilityObserverScope observer(&Observations::append,&values);
        if(!check(OldObserver,false,s.terrainByCell) || !values.values.empty())return fail("old observer must use original route");}
    {RuntimeComponentTerrainQueryReferenceScope reference(true);
        if(!check(OldReference,false,s.terrainByCell))return false;}
    auto* actor=findMutableObjectById(s,ids.front());if(!actor)return fail("actual representative missing");
    // FV is now an admitted stock Ground representative. The original Air
    // SCHP rules remain an exact representative-negative case.
    const auto type=actor->type;actor->type="SCHP";
    const bool typeCheck=check(RepresentativeType,false,s.terrainByCell);actor->type=type;if(!typeCheck)return false;
    const auto mission=actor->mission;actor->mission="Attack";
    const bool missionCheck=check(Mission,false,s.terrainByCell);actor->mission=mission;if(!missionCheck)return false;
    if(!s.behaviors.empty())return fail("fresh behavior precondition");
    RuntimeObjectBehavior behavior;behavior.kind=RuntimeBehaviorKind::Attack;s.behaviors[ids.front()]=behavior;
    const bool behaviorCheck=check(BehaviorKind,false,s.terrainByCell);s.behaviors.clear();if(!behaviorCheck)return false;
    behavior.kind=RuntimeBehaviorKind::Move;behavior.returnToDeployed=true;s.behaviors[ids.front()]=behavior;
    const bool returnCheck=check(ReturnDeployed,false,s.terrainByCell);s.behaviors.clear();if(!returnCheck)return false;
    const int x=actor->x;actor->x=1000;
    const bool originCheck=check(BadOrigin,false,s.terrainByCell);actor->x=x;if(!originCheck)return false;
    const int dock=actor->dockedToObjectId;actor->dockedToObjectId=123;
    const bool dockCheck=check(Dock,false,s.terrainByCell);actor->dockedToObjectId=dock;if(!dockCheck)return false;
    actor->transportPassengerIds.push_back(ids[1]);
    const bool cargoCheck=check(Cargo,false,s.terrainByCell);actor->transportPassengerIds.clear();if(!cargoCheck)return false;
    const auto duplicate=*actor;s.world.accessObjects().push_back(duplicate);
    const bool duplicateCheck=check(DuplicateID,false,s.terrainByCell);s.world.accessObjects().pop_back();
    s.objectIndex=RuntimeObjectIndex{};invalidateRuntimeTickOccupancyCache(s);if(!duplicateCheck)return false;
    s.gateIdsByCell[runtimeCellKey(goal.x,goal.y)]={};
    const bool gateCheck=check(GateIndex,false,s.terrainByCell);s.gateIdsByCell.clear();if(!gateCheck)return false;
    auto foreign=s.terrainByCell;foreign[runtimeCellKey(goal.x,goal.y)]=nullptr;
    if(!check(NullTerrain,false,foreign))return false;
    TerrainCell alias;alias.x=1000;alias.y=0;foreign=s.terrainByCell;foreign[runtimeCellKey(0,1)]=&alias;
    if(!check(AliasTerrain,false,foreign))return false;
    const int goalKey=runtimeCellKey(goal.x,goal.y);const auto metadata=s.terrainInfoByCell.at(goalKey);
    s.terrainInfoByCell.erase(goalKey);
    const bool missingCheck=check(MissingMetadata,false,s.terrainByCell);
    s.terrainInfoByCell.try_emplace(goalKey,metadata);if(!missingCheck)return false;
    if(!check(VertexCap,false,s.terrainByCell,1,32768) || !check(EdgeCap,false,s.terrainByCell,4096,1))return false;
    const auto* representative=findObjectById(s,ids.front());const auto* second=findObjectById(s,ids[1]);
    if(!representative || !second || representative->y!=second->y || second->x!=representative->x+1)
        return fail("actual adjacent original stock seed/edge precondition");
    const RuntimeCellPoint next{second->x,second->y};const int nextKey=runtimeCellKey(next.x,next.y);
    auto& nextInfo=s.terrainInfoByCell.at(nextKey);const auto savedInfo=nextInfo;nextInfo.passable=false;
    const bool seedCheck=check(SeedVeto,false,s.terrainByCell);nextInfo=savedInfo;if(!seedCheck)return false;
    auto* terrain=const_cast<TerrainCell*>(s.terrainByCell.at(nextKey));
    if(!terrain || !runtimeCellTransitionIsPassable(s,s.terrainByCell,occupied,
            {representative->x,representative->y},next,*representative))return fail("original adjacent edge precondition");
    const int level=terrain->level;terrain->level=level+3;
    const bool actualVeto=!runtimeCellTransitionIsPassable(s,s.terrainByCell,occupied,
        {representative->x,representative->y},next,*representative);
    const bool edgeCheck=check(EdgeVeto,false,s.terrainByCell);terrain->level=level;
    if(!actualVeto || !edgeCheck)return fail("exact original level veto");
    {RuntimeMixedLocalComponentStats stats;RuntimeMixedLocalComponentStatsScope observe(stats);
        RuntimeMixedLocalComponentCertificate proof;
        if(!proof.prepare(s,ids,goal,s.terrainByCell,occupied,ids.front(),ids.size()))return fail("lifetime positive proof");
        if(!record(CoveredDesired,proof.coversBest(goal))
            || !record(OutsideDesired,!proof.coversBest({goal.x+64,goal.y})))return false;
        const auto memoSteps=stats.walkSteps,memoEdges=stats.orientedEdges;
        if(!proof.coversBest(goal) || !proof.coversBest(goal)
            || stats.walkSteps!=memoSteps || stats.orientedEdges!=memoEdges)
            return fail("repeated covered desired must reuse the completed frontier without graph work");
        ++s.tick;const bool fenced=!proof.coversBest(goal);--s.tick;if(!record(TickFence,fenced))return false;
        {RuntimeMixedLocalComponentReferenceScope reference(true);if(!record(ReferenceFence,!proof.coversBest(goal)))return false;}
        proof.invalidate();if(!record(ExplicitInvalidate,!proof.coversBest(goal)))return false;
        const auto savedSteps=stats.walkSteps,savedEdges=stats.orientedEdges;
        if(!proof.enabled() && (proof.coversBest(goal) || stats.walkSteps!=savedSteps || stats.orientedEdges!=savedEdges))
            return fail("invalidated proof must not do new graph work");
        if(!stats.connectedGoalHits || !stats.walkSteps)
            return fail("connected goal reuse did not execute");}
    {RuntimeMixedLocalComponentStats stats;RuntimeMixedLocalComponentStatsScope observe(stats);
        RuntimeMixedLocalComponentCertificate proof;
        if(!record(FailedFirst,!proof.prepare(s,ids,goal,s.terrainByCell,occupied,ids.front(),ids.size(),4096,1)))return false;
        if(!record(SingleUseRetry,!proof.prepare(s,ids,goal,s.terrainByCell,occupied,ids.front(),ids.size())
                && !proof.enabled() && stats.attempts==2 && stats.admitted==0 && stats.guardFallbacks==1))return false;}
    if(!check(FinalPositive,true,s.terrainByCell))return false;
    return std::all_of(seen.begin(),seen.end(),[](bool v){return v;}) ? true : fail("named boundary omission");
}
bool prepareNative(RuntimeWindowState& s,const RuntimeWindowState& assets,const RuntimeConfig& config,
    int side,int history,bool footVeto,std::vector<int>& ids,std::string& error,int unitCount=48,int vehicleRosterOffset=0) {
    s.config=config;s.report=assets.report;s.rules=assets.rules;s.art=assets.art;s.theater=assets.theater;
    s.rulesLoaded=assets.report.rulesLoaded;s.artLoaded=assets.report.artLoaded;s.theaterLoaded=assets.report.theaterLoaded;
    RuntimeSnapshotActiveStateScope active(s);
    if(!prepareRuntimeMassMovementWorld(s,unitCount,ids,error,false,true,false,side,side,true,true,false,vehicleRosterOffset))return false;
    s.config.defaultMapPath=s.report.map.path;
    if(s.report.map.path!="MassMovement-v1.map" || ids.size()!=static_cast<std::size_t>(unitCount)
        || s.report.map.units!=unitCount/2 || s.report.map.infantry!=unitCount/2) {
        error="mixed local component: real normal decoder cardinality/map identity";return false;}
    if(history) {
        const int first=nextRuntimeObjectInstanceId(s);
        if(first<=0 || first>std::numeric_limits<int>::max()-history){error="mixed history ID range";return false;}
        s.world.accessObjects().reserve(s.world.getObjects().size()+history);
        for(int i=0;i<history;++i) {auto dead=object(first+i,WorldObjectKind::Unit,"MTNK",2,2);
            dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";
            s.world.accessObjects().push_back(std::move(dead));}
        auto metadata=std::move(s.terrainInfoByCell);s.objectIndex=RuntimeObjectIndex{};
        initializeRuntimeNavigation(s);s.terrainInfoByCell=std::move(metadata);
        s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false;invalidateRuntimeTickOccupancyCache(s);
    }
    if(footVeto) {
        auto* clear=const_cast<LandType*>(s.rules.findLandType("Clear"));
        if(!clear || clear->track<=0 || clear->foot<=0){error="mixed local component: native Clear permission prerequisite";return false;}
        clear->foot=0; // An explicit contract-only current-rule counterexample.
    }
    ensureRuntimeSpecialMechanismsInitialized(s);(void)completeRuntimeWorldRefresh(s);
    return true;
}
bool native(const RuntimeConfig& config,Counts& c,std::string& error) {
    const auto fail=[&](const std::string& v){error="mixed local component native: "+v;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)return fail("stock resources");
    constexpr std::array<const char*,12> types{{"MTNK","SREF","TTNK","FV","SCHP","HTNK","E1","CLEG","JUMPJET","SNIPE","GHOST","E2"}};
    for(int caseIndex=0;caseIndex<5;++caseIndex) {
        const int side=caseIndex==2 || caseIndex==3 ? 256 : 240;
        const int history=caseIndex==1 || caseIndex==3 ? 10000 : 0;
        const bool footVeto=caseIndex==4;
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        std::array<std::vector<int>,3> ids;
        for(int branch=0;branch<3;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();
            if(!prepareNative(*states[branch],*assets,config,side,history,footVeto,ids[branch],error))return false;
            std::array<std::size_t,12> roster{};
            for(int id:ids[branch]) {const auto* actor=findObjectById(*states[branch],id);
                if(!actor || !actor->alive || actor->health<=0)return fail("actual living roster");
                const auto type=std::find(types.begin(),types.end(),actor->type);
                if(type==types.end())return fail("unexpected decoded roster type");
                ++roster[static_cast<std::size_t>(type-types.begin())];}
            if(!std::all_of(roster.begin(),roster.end(),[](std::size_t n){return n==4;}))return fail("four each of twelve stock types");
        }
        if(ids[0]!=ids[1] || ids[0]!=ids[2])return fail("original actor ID order");
        c.nativeActors+=48;c.nativeTypes+=12;++c.cases;
        const int mid=side/2;const RuntimeCellPoint initialGoal{mid+22,mid+48};
        RuntimeStrictSlotVehicleReferenceScope keepStrictSeparate(true);
        if(caseIndex==0) {RuntimeSnapshotActiveStateScope active(*states[2]);
            if(!boundaries(*states[2],ids[2],initialGoal,c,error))return false;
            (void)completeRuntimeWorldRefresh(*states[2]);}
        {std::array<Authority,3> initial;
            for(int branch=0;branch<3;++branch) {RuntimeSnapshotActiveStateScope active(*states[branch]);
                if(!capture(*states[branch],initial[branch],error))return false;}
            if(initial[0]!=initial[1] || initial[0]!=initial[2])return fail("restored boundary/setup authority at completed tick zero");}
        if(footVeto) {auto& s=*states[0];RuntimeSnapshotActiveStateScope active(s);
            const auto e1=std::find_if(s.world.getObjects().begin(),s.world.getObjects().end(),[](const auto& actor){return actor.type=="E1";});
            std::unordered_set<int> moving(ids[0].begin(),ids[0].end());
            const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
            if(e1==s.world.getObjects().end() || runtimeCellIsPassable(s,s.terrainByCell,occupied,initialGoal,*e1))
                return fail("real original Foot=0 cell predicate must veto infantry before Nearest fixture");}
        bool pathContinuation=false;
        for(int tick=1;tick<=32;++tick) {
            const bool commandTick=tick==1 || tick==9 || tick==17;
            const RuntimeCellPoint target=tick==9 ? RuntimeCellPoint{mid+76,mid+32} : initialGoal;
            std::vector<std::pair<int,int>> originalOrder;
            std::array<Authority,3> full;
            std::array<std::map<int,RuntimeCellPoint>,3> destinations;
            std::array<std::pair<int,int>,3> acceptedRejected{};
            for(int branch=0;branch<3;++branch) {
                auto& s=*states[branch];s.tick=tick;RuntimeSnapshotActiveStateScope active(s);
                RuntimeMixedLocalComponentReferenceScope reference(branch==0);
                if(commandTick) {
                    if(branch==0) {
                        std::unordered_set<int> moving;
                        for(int id:ids[0]) {const auto* actor=findObjectById(s,id);
                            if(actor && runtimeObjectCanReceiveMoveCommand(s,*actor)
                                && runtimeObjectAllowsMoveToCell(s,*actor,target))moving.insert(id);}
                        const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
                        const auto* actor=findObjectById(s,ids[0].front());if(!actor)return fail("original representative");
                        const auto map=buildRuntimeMoveComponentByCell(s,*actor,s.terrainByCell,occupied);
                        originalOrder.assign(map.begin(),map.end());++c.originalMaps;
                        if(tick==1) {
                            RuntimeMixedLocalComponentReferenceScope optimized(false);
                            RuntimeMixedLocalComponentStats stats;RuntimeMixedLocalComponentStatsScope observe(stats);
                            RuntimeMixedLocalComponentCertificate proof;
                            if(!proof.prepare(s,ids[0],target,s.terrainByCell,occupied,ids[0].front(),ids[0].size()))
                                return fail("native initial original-label certificate absent case="+std::to_string(caseIndex));
                            const auto root=map.find(runtimeCellKey(actor->x,actor->y));
                            if(root==map.end())return fail("original root unmapped");
                            for(const auto& [key,bits]:proof.certifiedKeys()) {(void)bits;
                                const auto found=map.find(key);
                                if(found==map.end() || found->second!=root->second)return fail("original directed SCC label mismatch");
                                ++c.certifiedPoints;}
                        }
                    }
                    RuntimeMixedLocalComponentStats stats;
                    const std::size_t limited=branch==2 && tick==1 && !footVeto ? 21 : 256;
                    const auto started=std::chrono::steady_clock::now();
                    {RuntimeMixedLocalComponentStatsScope observe(stats,&originalOrder,limited);
                        if(footVeto) {
                            applySelection(s,ids[branch],false,false);
                            issueRuntimeMoveCommand(s,target,&destinations[branch]);
                            if(s.lastCommand.acceptedCount!=static_cast<int>(destinations[branch].size())
                                || s.lastCommand.rejectedCount!=48-static_cast<int>(destinations[branch].size()))
                                return fail("actual rule-veto command destination identity");
                        } else if(!issueRuntimeMassMovementOrder(s,ids[branch],target,destinations[branch],error,true))return false;
                    }
                    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                    c.commandMilliseconds[branch]+=ms;c.maxCommandMilliseconds[branch]=std::max(c.maxCommandMilliseconds[branch],ms);
                    acceptedRejected[branch]={s.lastCommand.acceptedCount,s.lastCommand.rejectedCount};
                    if(stats.attempts!=1 || stats.orderedMapMismatches || (stats.wholeMaps && stats.orderedMapsCompared!=stats.wholeMaps))
                        return fail("exact ordered original map/caller attempt case="+std::to_string(caseIndex)+" tick="
                            +std::to_string(tick)+" branch="+std::to_string(branch)+" attempts="+std::to_string(stats.attempts)
                            +" full="+std::to_string(stats.wholeMaps)+" compared="+std::to_string(stats.orderedMapsCompared)
                            +" mismatched="+std::to_string(stats.orderedMapMismatches));
                    if(branch==0 && (stats.referenceFallbacks!=1 || stats.wholeMaps!=1 || stats.admitted))
                        return fail("old complete graph reference route");
                    if(branch && tick==1) {
                        if(stats.admitted!=1 || !stats.connectedGoalHits || !stats.walkSteps || !stats.coveredBest)
                            return fail("actual new SCC/memo/Best positive missing case="+std::to_string(caseIndex)
                                +" branch="+std::to_string(branch)+" guard="+std::to_string(stats.guardFallbacks)
                                +" walk="+std::to_string(stats.walkFallbacks)+" layout="+std::to_string(stats.layoutFallbacks));
                        ++c.firstAdmitted;
                        if(footVeto) {
                            if(stats.nearestFallbacks!=1 || stats.lateWholeMaps!=1 || !s.lastCommand.rejectedCount)
                                return fail("genuine original Best failure and late Nearest fallback missing");
                            ++c.forcedNearest;
                        } else if(branch==2) {
                            if(stats.outsideFallbacks!=1 || stats.lateWholeMaps!=1 || !stats.lateChronoWitness || !stats.lateAirWitness)
                                return fail("coverage-limit late fallback after actual CLEG/Air assignment missing");
                            ++c.forcedOutside;
                        } else if(stats.wholeMaps) return fail("normal full certificate unexpectedly used complete graph");
                    }
                    c.orderedLateMaps+=stats.lateWholeMaps;add(c.stats,stats);++c.commands;
                }
                (void)advanceRuntimeSimulation(s);(void)completeRuntimeWorldRefresh(s);
                if(!capture(s,full[branch],error))return false;
                pathContinuation|=std::any_of(s.behaviors.begin(),s.behaviors.end(),[](const auto& b){
                    return b.second.moveCooldownTicks>0 || !b.second.path.empty();});
            }
            if(commandTick && (destinations[0]!=destinations[1] || destinations[0]!=destinations[2]
                    || acceptedRejected[0]!=acceptedRejected[1] || acceptedRejected[0]!=acceptedRejected[2]))
                return fail("actual command destination/order/acceptance mismatch case="+std::to_string(caseIndex)+" tick="+std::to_string(tick));
            if(full[0]!=full[1] || full[0]!=full[2])return fail("completed tick paths/world/systems/full-writer mismatch case="
                +std::to_string(caseIndex)+" tick="+std::to_string(tick));
            ++c.tickTriplets;
        }
        if(!pathContinuation)return fail("actual normal queued paths/render continuation not exercised");
    }
    return true;
}
struct WarmedCounts {
    std::size_t cases=0,commands=0,firstAdmitted=0,referenceCommands=0,limitedFirst=0;
    std::size_t warmTicks=0,preCommandSaves=0,fullSaves=0,trajectoryTriplets=0;
    std::size_t actors=0,types=0,idleReadbacks=0,firstIdleAdmissions=0,recoveryCommands=0;
    std::size_t firstSeedVetoes=0,firstDetourExpansions=0,orderedLateMaps=0;
    std::size_t laterAdmitted=0,laterGuardFallbacks=0,laterWalkFallbacks=0;
    RuntimeMixedLocalComponentStats stats;
};
// Full writer comparisons cover 32 completed ticks after 61, 16 after 301,
// and 16 after 541. Every intervening tick also compares world/subcells/gates/
// repair/MagBeam plus the original complete behavioral trajectory serializer.
Authority captureTrajectory(const RuntimeWindowState& s) {
    Authority out;out.world=s.world.deterministicSimulationStateHash();
    out.subcells=s.subcells.stateHash(s.world.getObjects(),s.world.getTerrainObjects());
    out.gates=s.gates.stateHash();out.repair=s.repairDepots.stateHash();out.mag=s.magBeams.stateHash();
    std::vector<int> order;for(const auto& [id,behavior]:s.behaviors){(void)behavior;order.push_back(id);}
    std::sort(order.begin(),order.end());std::ostringstream text;
    for(int id:order)text<<id<<'='<<runtimeMultiplayerSnapshotBehaviorRecord(id,s.behaviors.at(id))<<'\n';
    text<<"pending="<<runtimeMultiplayerSnapshotIntsText(s.pendingPathObjectIds,false);
    out.serialized=text.str();return out;
}
bool warmedNative(const RuntimeConfig& config,WarmedCounts& c,std::string& error,bool verifyKnownDefault=false) {
    const auto fail=[&](const std::string& v){error="mixed local warmed native: "+v;return false;};
    if(verifyKnownDefault && runtimeMixedKnownUnmappedReference())return fail("default Known branch disabled");
    std::size_t knownFlagChecks=0,knownDefaultCommands=0,knownDirectChecks=0;
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)return fail("stock resources");
    constexpr std::array<const char*,12> types{{"MTNK","SREF","TTNK","FV","SCHP","HTNK","E1","CLEG","JUMPJET","SNIPE","GHOST","E2"}};
    for(int caseIndex=0;caseIndex<6;++caseIndex) {
        const int side=caseIndex<2?240:256,history=caseIndex%2?10000:0,unitCount=caseIndex<4?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;std::array<std::vector<int>,3> ids;
        for(int branch=0;branch<3;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();
            if(!prepareNative(*states[branch],*assets,config,side,history,false,ids[branch],error,unitCount))return false;
            auto& s=*states[branch];std::array<std::size_t,12> census{};
            for(int id:ids[branch]) {const auto* actor=findObjectById(s,id);
                if(!actor || !actor->alive || actor->health<=0)return fail("actual living roster");
                const auto type=std::find(types.begin(),types.end(),actor->type);
                if(type==types.end())return fail("decoded stock roster type");++census[static_cast<std::size_t>(type-types.begin())];}
            for(std::size_t i=0;i<census.size();++i)
                if(census[i]!=static_cast<std::size_t>((unitCount/2+5-static_cast<int>(i%6))/6))
                    return fail("exact 48/256 original six-type hydration counts");
            if(s.world.getObjects().size()!=static_cast<std::size_t>(unitCount+9+history))return fail("genuine history/object cardinality");
            const auto* first=findObjectById(s,ids[branch].front());
            if(!first || first->type!="MTNK" || first->x!=side/2+4 || first->y!=side/2+20)
                return fail("exact performance first MTNK starting coordinate");
        }
        if(ids[0]!=ids[1] || ids[0]!=ids[2])return fail("actual ID order");
        ++c.cases;c.actors+=unitCount;c.types+=12;
        RuntimeStrictSlotVehicleReferenceScope keepStrictSeparate(true);
        for(int tick=1;tick<=556;++tick) {
            const bool command=tick==61 || tick==301 || tick==541;
            const bool fullSave=(tick>=61 && tick<=92) || (tick>=301 && tick<=316) || (tick>=541 && tick<=556);
            const RuntimeCellPoint target{side/2+(tick==301?22:76),side/2+(tick==301?48:32)};
            std::array<Authority,3> full,trajectory;
            std::array<std::map<int,RuntimeCellPoint>,3> destinations;
            std::array<std::pair<int,int>,3> acceptedRejected{};
            std::vector<std::pair<int,int>> originalOrder;
            for(int branch=0;branch<3;++branch) {
                auto& s=*states[branch];s.tick=tick;RuntimeSnapshotActiveStateScope active(s);
                RuntimeMixedLocalComponentReferenceScope reference(branch==0);
                if(command) {
                    if(tick==61) {
                        std::size_t idle=0;for(int id:ids[branch]) {const auto* actor=findObjectById(s,id);
                            idle+=actor && (actor->mission=="Idle1" || actor->mission=="Idle2")?1U:0U;}
                        if(!idle)return fail("real60-tick warmup must produce original Idle1/Idle2 state");
                        if(branch==0)c.idleReadbacks+=idle;
                    }
                    std::size_t recovery=0;for(int id:ids[branch]) {const auto* actor=findObjectById(s,id);
                        const auto* unit=actor?findRuntimeUnitType(s.rules,*actor):nullptr;
                        if(unit && unit->teleporter && s.specialMechanisms.isChronoMovementLocked(id,tick)
                            && !s.specialMechanisms.isChronoInTransit(id) && actor->mission=="ChronoFrozen")++recovery;}
                    if(branch==0) {
                        std::unordered_set<int> moving;for(int id:ids[0]) {const auto* actor=findObjectById(s,id);
                            if(actor && runtimeObjectCanReceiveMoveCommand(s,*actor)
                                && runtimeObjectAllowsMoveToCell(s,*actor,target))moving.insert(id);}
                        const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
                        const auto* actor=findObjectById(s,ids[0].front());if(!actor)return fail("real original representative");
                        const auto map=buildRuntimeMoveComponentByCell(s,*actor,s.terrainByCell,occupied);
                        originalOrder.assign(map.begin(),map.end());
                        if(tick==61) {
                            RuntimeMixedLocalComponentReferenceScope optimized(false);
                            RuntimeMixedLocalComponentStats stats;RuntimeMixedLocalComponentStatsScope observe(stats,verifyKnownDefault?&originalOrder:nullptr);
                            RuntimeMixedLocalComponentCertificate proof;
                            if(!proof.prepare(s,ids[0],target,s.terrainByCell,occupied,ids[0].front(),moving.size()))
                                return fail("warmed far-first certificate missing case="+std::to_string(caseIndex)
                                    +" reason="+std::to_string(stats.rejectionReason)+" actor="+std::to_string(stats.rejectionActor)
                                    +" guard="+std::to_string(stats.guardFallbacks)+" walk="+std::to_string(stats.walkFallbacks));
                            const auto root=map.find(runtimeCellKey(actor->x,actor->y));if(root==map.end())return fail("root unmapped");
                            for(const auto& [key,bits]:proof.certifiedKeys()) {(void)bits;const auto found=map.find(key);
                                if(found==map.end() || found->second!=root->second)return fail("detour vertex not in original directed root component");}
                            if(verifyKnownDefault) for(int key:proof.knownUnmappedKeys()) {
                                if(map.contains(key))return fail("default warm direct certificate published mapped key");
                                ++knownDirectChecks;
                            }
                            if(!stats.idleActors || !stats.seedVetoes || !stats.detourExpansions)
                                return fail("real idle and obstructed far-first mutual detour witness absent");
                            c.firstSeedVetoes+=stats.seedVetoes;c.firstDetourExpansions+=stats.detourExpansions;
                        }
                    }
                    RuntimeMixedLocalComponentStats stats;
                    const std::size_t limit=branch==2 && tick==61?21:256;
                    {RuntimeMixedLocalComponentStatsScope observe(stats,&originalOrder,limit);
                        if(!issueRuntimeMassMovementOrder(s,ids[branch],target,destinations[branch],error,true))return false;}
                    acceptedRejected[branch]={s.lastCommand.acceptedCount,s.lastCommand.rejectedCount};
                    if(verifyKnownDefault) {
                        if(runtimeMixedKnownUnmappedReference() || stats.unmappedOriginalMismatches
                            || stats.unmappedOriginalChecks!=stats.unmappedProofs)
                            return fail("default warm reference/complete-map absence check");
                        ++knownFlagChecks;if(branch!=0)++knownDefaultCommands;
                    }
                    if(stats.attempts!=1 || stats.orderedMapMismatches || (stats.wholeMaps && stats.wholeMaps!=stats.orderedMapsCompared))
                        return fail("exact original complete unordered map after actual warmed command");
                    if(branch==0) {if(stats.referenceFallbacks!=1 || stats.wholeMaps!=1 || stats.admitted)return fail("reference full graph route");++c.referenceCommands;}
                    else if(tick==61) {
                        if(stats.admitted!=1 || !stats.coveredBest || !stats.idleActors || !stats.connectedGoalHits)
                            return fail("actual warmed far-first positive command missing case="+std::to_string(caseIndex)
                                +" branch="+std::to_string(branch)+" reason="+std::to_string(stats.rejectionReason)
                                +" actor="+std::to_string(stats.rejectionActor)+" guard="+std::to_string(stats.guardFallbacks)
                                +" walk="+std::to_string(stats.walkFallbacks)+" layout="+std::to_string(stats.layoutFallbacks));
                        ++c.firstAdmitted;c.firstIdleAdmissions+=stats.idleActors;
                        if(branch==1 && stats.wholeMaps)return fail("full warmed far-first frontier unexpectedly materialized");
                        if(branch==2) {if(stats.outsideFallbacks!=1 || stats.lateWholeMaps!=1)return fail("forced warmed late fallback missing");++c.limitedFirst;}
                    } else {
                        // Current rejected CLEG remains an actual blocker. No
                        // assertion pretends that its Best frontier is passable.
                        if(recovery) {if(stats.qualifiedChronoRecovery!=recovery || stats.guardFallbacks)
                            return fail("narrow real recovery guard qualification missing reason="+std::to_string(stats.rejectionReason));
                            ++c.recoveryCommands;}
                        c.laterAdmitted+=stats.admitted;c.laterGuardFallbacks+=stats.guardFallbacks;c.laterWalkFallbacks+=stats.walkFallbacks;
                    }
                    c.orderedLateMaps+=stats.lateWholeMaps;add(c.stats,stats);++c.commands;
                }
                (void)advanceRuntimeSimulation(s);(void)completeRuntimeWorldRefresh(s);
                trajectory[branch]=captureTrajectory(s);
                if(fullSave || tick==60)if(!capture(s,full[branch],error))return false;
            }
            if(trajectory[0]!=trajectory[1] || trajectory[0]!=trajectory[2])return fail("every completed original path/world trajectory case="
                +std::to_string(caseIndex)+" tick="+std::to_string(tick));
            ++c.trajectoryTriplets;
            if(fullSave || tick==60) {
                if(full[0]!=full[1] || full[0]!=full[2])return fail("completed original full writer case="+std::to_string(caseIndex)+" tick="+std::to_string(tick));
                if(fullSave)++c.fullSaves;else ++c.preCommandSaves;
            }
            if(tick<=60)++c.warmTicks;
            if(command && (destinations[0]!=destinations[1] || destinations[0]!=destinations[2]
                || acceptedRejected[0]!=acceptedRejected[1] || acceptedRejected[0]!=acceptedRejected[2]))return fail("exact warmed acceptance/destinations");
        }
    }
    if(c.cases!=6 || c.commands!=54 || c.firstAdmitted!=12 || c.referenceCommands!=18 || c.limitedFirst!=6
        || c.warmTicks!=360 || c.preCommandSaves!=6 || c.fullSaves!=384 || c.trajectoryTriplets!=3336
        || c.actors!=704 || c.types!=72 || !c.idleReadbacks || !c.firstIdleAdmissions || !c.recoveryCommands
        || !c.firstSeedVetoes || !c.firstDetourExpansions || c.orderedLateMaps<6 || c.stats.orderedMapMismatches)
        return fail("exact warmed coverage cases="+std::to_string(c.cases)+" commands="+std::to_string(c.commands)
            +" first="+std::to_string(c.firstAdmitted)+" old="+std::to_string(c.referenceCommands)+" late="+std::to_string(c.limitedFirst)
            +" warm="+std::to_string(c.warmTicks)+" pre="+std::to_string(c.preCommandSaves)+" full="+std::to_string(c.fullSaves)
            +" trajectory="+std::to_string(c.trajectoryTriplets)+" actors="+std::to_string(c.actors)+" types="+std::to_string(c.types)
            +" idle="+std::to_string(c.idleReadbacks)+" recovery="+std::to_string(c.recoveryCommands)
            +" veto="+std::to_string(c.firstSeedVetoes)+" detour="+std::to_string(c.firstDetourExpansions));
    std::cout<<"Mixed local warmed performance coverage: nativeCases=6 nativeActors=704 nativeTypeReadbacks=72 warmupTicks=360 originalCommands=18 nativeCommands=54 firstAdmittedCommands=12 forcedLateMaps=6 preCommandFullSaveTriplets=6 completedFullSaveTriplets=384 completedTrajectoryTriplets=3336 idleReadbacks="
        <<c.idleReadbacks<<" firstIdleAdmissions="<<c.firstIdleAdmissions<<" recoveryQualifiedCommands="<<c.recoveryCommands
        <<" firstSeedVetoes="<<c.firstSeedVetoes<<" firstDetourExpansions="<<c.firstDetourExpansions<<" orderedLateMaps="<<c.orderedLateMaps
        <<" laterAdmitted="<<c.laterAdmitted<<" laterGuardFallbacks="<<c.laterGuardFallbacks<<" laterWalkFallbacks="<<c.laterWalkFallbacks
        <<" goals=far/near/far ticks=61/301/541 native48/native256/H0/H10000/Idle/ChronoRecovery/originalSCC/orderedWholeFallback/commands/paths/fullWriter=exact passed=yes\n";
    if(verifyKnownDefault) {
        if(knownFlagChecks!=54 || knownDefaultCommands!=36 || c.stats.unmappedOriginalMismatches
            || c.stats.unmappedOriginalChecks!=c.stats.unmappedProofs)
            return fail("default warm coverage/absence totals");
        std::cout<<"Known-unmapped default warmed native: nativeCases=6 nativeActors=704 nativeTypeReadbacks=72 warmupTicks=360 nativeCommands=54 defaultCommands=36 referenceCommands=18 flagChecks=54 firstAdmittedCommands=12 forcedLateMaps=6 preCommandFullSaveTriplets=6 completedFullSaveTriplets=384 completedTrajectoryTriplets=3336 directPublishedKeysChecked="
            <<knownDirectChecks<<" actualUnmappedProofs="<<c.stats.unmappedProofs
            <<" actualPublishedKeysChecked="<<c.stats.unmappedOriginalChecks<<" orderedLateMaps="<<c.orderedLateMaps
            <<" featureReference=false goals=far/near/far ticks=61/301/541 native48/native256/H0/H10000/originalAbsentKeys/Best/Nearest/commands/paths/fullWriter=exact passed=yes\n";
    }
    return true;
}
}
#include "RuntimeMixedLocalStockLeaderContract.inl"
#include "RuntimeMixedLocalKnownUnmappedContract.inl"
bool runRuntimeMixedLocalComponentCertificateActualContract(const RuntimeConfig& config,std::string& error) {
    RuntimeMixedBestFrontierClosureReferenceScope defaultFrontierClosure(false);
    RuntimeMixedKnownUnmappedReferenceScope preserveC37Contract(true);
    using namespace RuntimeMixedLocalComponentContractDetail;Counts c;
    if(!schedules(c,error) || !native(config,c,error))return false;

    if(c.probeSchedules!=12 || c.boundaries!=BoundaryCount || c.cases!=5 || c.originalMaps!=15
        || c.commands!=45 || c.tickTriplets!=160 || c.firstAdmitted!=10 || c.forcedOutside!=4
        || c.forcedNearest!=2 || c.nativeActors!=240 || c.nativeTypes!=60 || !c.certifiedPoints
        || c.orderedLateMaps<6 || !c.stats.connectedGoalHits || !c.stats.walkSteps || c.stats.orderedMapMismatches) {
        error="mixed local component: coverage schedules="+std::to_string(c.probeSchedules)+" boundaries="+std::to_string(c.boundaries)
            +" cases="+std::to_string(c.cases)+" maps="+std::to_string(c.originalMaps)+" commands="+std::to_string(c.commands)
            +" saves="+std::to_string(c.tickTriplets)+" admitted="+std::to_string(c.firstAdmitted)
            +" outside="+std::to_string(c.forcedOutside)+" nearest="+std::to_string(c.forcedNearest);return false;
    }
    std::cout<<"Mixed local component certificate: probeSchedules=12 boundaries=31 nativeCases=5 originalWholeMaps=15 nativeCommands=45 firstAdmittedCommands=10 actualOutsideFallbacks=4 actualNearestFallbacks=2 completedTickFullSaveTriplets=160 nativeActors=240 nativeTypeReadbacks=60 certifiedPoints="
        <<c.certifiedPoints<<" connectedGoalHits="<<c.stats.connectedGoalHits<<" walkSteps="<<c.stats.walkSteps
        <<" orderedLateMaps="<<c.orderedLateMaps<<" lateChronoWitness="<<c.stats.lateChronoWitness<<" lateAirWitness="<<c.stats.lateAirWitness
        <<" native240/native256/H0/H10000/stock12types/originalLabels/orderedWholeFallback/Best/Nearest/commands/paths/fullWriter=exact passed=yes\n";
    std::cout<<"Mixed local command service (diagnostic only): originalCommands=15 certificateCommands=15 limitedCertificateCommands=15 originalTotalMs="
        <<c.commandMilliseconds[0]<<" certificateTotalMs="<<c.commandMilliseconds[1]<<" limitedTotalMs="<<c.commandMilliseconds[2]
        <<" originalMaxMs="<<c.maxCommandMilliseconds[0]<<" certificateMaxMs="<<c.maxCommandMilliseconds[1]
        <<" limitedMaxMs="<<c.maxCommandMilliseconds[2]<<" clocks=wholeCommand loadingExcluded=yes\n";
    if(!runRuntimeMixedLocalKnownUnmappedContract(config,error))return false;
    {
        RuntimeMixedKnownUnmappedReferenceScope defaultKnown(false);
        WarmedCounts warmed;if(!warmedNative(config,warmed,error,true))return false;
        if(!stockLeaders(config,error,true))return false;
        if(runtimeMixedBestFrontierClosureReference()) {
            error="Best frontier closure: default full warmed/leader corpus was disabled";return false;
        }
        std::cout<<"Mixed local Best frontier closure default corpus: warmedNativeCases=6 leaderCases=4 nativeCommands=66 completedFullSaveTriplets=400 completedTrajectoryTriplets=3592 featureReference=false originalDefaultAssertions=retained passed=yes\n";
    }
    return true;
}
