// Actual stock actors are authored by the normal map decoder. No prototype push
// substitutes for vehicles, commands, path solving or complete writer capture.
#pragma once
#include <array>
#include <memory>
#include <limits>
#include <tuple>
#include <chrono>
namespace RuntimeStrictSlotVehicleContractDetail {
using namespace RuntimePlannerPassabilityContractDetail;
struct Counts { std::size_t boundaries=0,maps=0,commands=0,saves=0,admitted=0,original=0;
    std::size_t actors=0,mtnk=0,htnk=0,certifiedPoints=0,fallbackCommands=0;
    std::array<double,2> commandMilliseconds{},maxCommandMilliseconds{};
    RuntimeStrictSlotVehicleCertificateStats stats; };
bool boundaries(RuntimeWindowState& s,const std::vector<int>& ids,const RuntimeCellPoint goal,
    Counts& c,std::string& error) {
    const auto fail=[&](const std::string& why) { error="strict-slot vehicle boundary: "+why; return false; };
    std::unordered_set<int> moving(ids.begin(),ids.end());
    const auto occupancy=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
    const auto check=[&](bool expected,const auto& terrain,const auto& occupied,
        std::size_t vertices=4096,std::size_t edges=8192) {
        RuntimeStrictSlotVehicleCertificateStats stats;
        RuntimeStrictSlotVehicleStatsScope record(stats);
        const bool value=tryRuntimeStrictSlotVehicleCertificate(s,ids,goal,terrain,occupied,ids.front(),ids.size(),nullptr,vertices,edges);
        ++c.boundaries;
        if(value!=expected || stats.attempts!=1 || stats.admitted!=(expected?1U:0U)
            || stats.vertices>4096 || stats.orientedEdges>8192) {
            error="strict-slot vehicle boundary="+std::to_string(c.boundaries)+" expected="+std::to_string(expected)
                +" admitted="+std::to_string(stats.admitted)+" guard="+std::to_string(stats.guardFallbacks)
                +" layout="+std::to_string(stats.layoutFallbacks)+" slot="+std::to_string(stats.slotFallbacks)
                +" walk="+std::to_string(stats.walkFallbacks)+" reference="+std::to_string(stats.referenceFallbacks);
            return false;
        }
        c.stats.add(stats); return true;
    };
    for(const auto& [soft,tie,expected]:std::array<std::tuple<int,std::uint64_t,bool>,6>{{
            {-1,0,false},{0,31,true},{498,31,true},{499,31,false},{529,0,true},{530,0,false}}}) {
        ++c.boundaries;
        if(runtimeStrictSlotVehicleStrictWinner(soft,tie)!=expected) return fail("strict 530 lower-bound arithmetic");
    }
    RuntimeStrictSlotVehicleReferenceScope optimized(false);
    if(!check(true,s.terrainByCell,occupancy)) return false;
    { RuntimeStrictSlotVehicleReferenceScope reference(true);
        if(!check(false,s.terrainByCell,occupancy)) return false;
        { RuntimeStrictSlotVehicleReferenceScope inner(false); if(!check(true,s.terrainByCell,occupancy)) return false; }
        if(!check(false,s.terrainByCell,occupancy)) return false;
    }
    runtimeStrictSlotVehicleProofActive=true;
    const bool nested=check(false,s.terrainByCell,occupancy); runtimeStrictSlotVehicleProofActive=false;
    if(!nested) return false;
    auto* actor=findMutableObjectById(s,ids.front()); if(!actor) return fail("actual stock actor");
    const auto type=actor->type; actor->type="FV";
    const bool typeGuard=check(false,s.terrainByCell,occupancy); actor->type=type; if(!typeGuard) return false;
    actor->kind=WorldObjectKind::Infantry;
    const bool kindGuard=check(false,s.terrainByCell,occupancy); actor->kind=WorldObjectKind::Unit; if(!kindGuard) return false;
    const auto mission=actor->mission; actor->mission="Attack";
    const bool missionGuard=check(false,s.terrainByCell,occupancy); actor->mission=mission; if(!missionGuard) return false;
    if(!s.behaviors.empty()) return fail("fresh boundary behavior prerequisite");
    RuntimeObjectBehavior attack; attack.kind=RuntimeBehaviorKind::Attack; s.behaviors[ids.front()]=attack;
    const bool behaviorGuard=check(false,s.terrainByCell,occupancy); s.behaviors.clear(); if(!behaviorGuard) return false;
    const int oldX=actor->x; actor->x=1000;
    const bool originGuard=check(false,s.terrainByCell,occupancy); actor->x=oldX; if(!originGuard) return false;
    const auto duplicate=*actor; s.world.accessObjects().push_back(duplicate);
    const bool duplicateGuard=check(false,s.terrainByCell,occupancy); s.world.accessObjects().pop_back();
    s.objectIndex=RuntimeObjectIndex{}; invalidateRuntimeTickOccupancyCache(s);
    if(!duplicateGuard) return false;
    auto forged=occupancy; forged[runtimeCellKey(goal.x,goal.y)]=-123;
    if(!check(false,s.terrainByCell,forged)) return false;
    auto& metadata=s.terrainInfoByCell.at(runtimeCellKey(goal.x,goal.y)); const auto oldMetadata=metadata;
    metadata.cost=255;
    const bool scoreGuard=check(false,s.terrainByCell,occupancy); metadata=oldMetadata; if(!scoreGuard) return false;
    auto foreign=s.terrainByCell; foreign[runtimeCellKey(goal.x,goal.y)]=nullptr;
    if(!check(false,foreign,occupancy)) return false;
    TerrainCell alias; alias.x=1000; alias.y=0;
    foreign=s.terrainByCell; foreign[runtimeCellKey(0,1)]=&alias;
    if(!check(false,foreign,occupancy)) return false;
    foreign=s.terrainByCell; foreign.erase(runtimeCellKey(goal.x,goal.y));
    if(!check(false,foreign,occupancy) || !check(false,s.terrainByCell,occupancy,1,8192)
        || !check(false,s.terrainByCell,occupancy,4096,1) || !check(true,s.terrainByCell,occupancy)) return false;
    const auto* representative=findObjectById(s,ids.front());
    const auto* second=findObjectById(s,ids[1]);
    if(!representative || !second || representative->y!=second->y || second->x!=representative->x+1)
        return fail("actual adjacent authored origins for seed/edge veto");
    const RuntimeCellPoint secondPoint{second->x,second->y};
    auto& secondInfo=s.terrainInfoByCell.at(runtimeCellKey(secondPoint.x,secondPoint.y));
    const auto savedSecondInfo=secondInfo; secondInfo.passable=false;
    const bool seedVeto=check(false,s.terrainByCell,occupancy); secondInfo=savedSecondInfo;
    if(!seedVeto) return false;
    auto* secondTerrain=const_cast<TerrainCell*>(s.terrainByCell.at(runtimeCellKey(secondPoint.x,secondPoint.y)));
    if(!secondTerrain || !runtimeCellTransitionIsPassable(s,s.terrainByCell,occupancy,
            {representative->x,representative->y},secondPoint,*representative))
        return fail("original adjacent transition prerequisite");
    const int savedLevel=secondTerrain->level; secondTerrain->level=savedLevel+3;
    const bool actualEdgeVeto=!runtimeCellTransitionIsPassable(s,s.terrainByCell,occupancy,
        {representative->x,representative->y},secondPoint,*representative);
    const bool edgeVeto=check(false,s.terrainByCell,occupancy); secondTerrain->level=savedLevel;
    if(!actualEdgeVeto || !edgeVeto) return fail("original level transition and certificate must reject");
    return c.boundaries==27 ? true : fail("exact boundary count="+std::to_string(c.boundaries));
}
bool native(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& why) { error="strict-slot vehicle native: "+why; return false; };
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded
        || !assets->rules.findUnitType("UnitType","MTNK") || !assets->rules.findUnitType("UnitType","HTNK"))
        return fail("actual stock MTNK/HTNK assets");
    for(int side:{240,256}) for(int history:{0,10000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& s=*states[branch];
            s.config=config; s.report=assets->report; s.rules=assets->rules; s.art=assets->art; s.theater=assets->theater;
            s.rulesLoaded=assets->report.rulesLoaded; s.artLoaded=assets->report.artLoaded; s.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(s);
            if(!prepareRuntimeMassMovementWorld(s,48,ids[branch],error,false,true,false,side,side,true,false,true)) return false;
            s.config.defaultMapPath=s.report.map.path;
            if(ids[branch].size()!=48 || s.report.map.path!="MassMovement-v1.map"
                || s.report.map.units!=48 || s.report.map.infantry!=0
                || s.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side)) return fail("normal native decoder cardinality");
            std::size_t mtnk=0,htnk=0;
            for(int id:ids[branch]) { const auto* actor=findObjectById(s,id);
                if(!actor || actor->kind!=WorldObjectKind::Unit || actor->health<=0
                    || runtimeObjectMovementMode(s,*actor)!=PathMovementMode::Ground) return fail("real ground vehicle");
                if(actor->type=="MTNK") ++mtnk; else if(actor->type=="HTNK") ++htnk; else return fail("unexpected roster type"); }
            if(mtnk!=24 || htnk!=24) return fail("actual MTNK/HTNK roster readback");
            if(!branch) { counts.actors+=ids[branch].size(); counts.mtnk+=mtnk; counts.htnk+=htnk; }
            if(history) { const int first=nextRuntimeObjectInstanceId(s);
                if(first<=0 || first>std::numeric_limits<int>::max()-history) return fail("history ID range");
                s.world.accessObjects().reserve(s.world.getObjects().size()+history);
                for(int i=0;i<history;++i) { auto dead=object(first+i,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                    s.world.accessObjects().push_back(std::move(dead)); }
                auto metadata=std::move(s.terrainInfoByCell); s.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(s); s.terrainInfoByCell=std::move(metadata);
                s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false; invalidateRuntimeTickOccupancyCache(s);
            }
            ensureRuntimeSpecialMechanismsInitialized(s); (void)completeRuntimeWorldRefresh(s);
        }
        if(ids[0]!=ids[1]) return fail("actor order mismatch");
        const int mid=side/2; const RuntimeCellPoint goal{mid+22,mid+48};
        if(side==240 && !history) { auto& s=*states[1]; RuntimeSnapshotActiveStateScope active(s);
            if(!boundaries(s,ids[1],goal,counts,error)) return false;
            (void)completeRuntimeWorldRefresh(s); }
        // Compare real original complete labels at every certificate point. The
        // complete map output itself is not replaced or reconstructed in tests.
        { auto& s=*states[0]; RuntimeSnapshotActiveStateScope active(s);
            std::unordered_set<int> moving(ids[0].begin(),ids[0].end());
            const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
            const auto* representative=findObjectById(s,ids[0].front()); if(!representative) return fail("representative");
            const auto full=buildRuntimeMoveComponentByCell(s,*representative,s.terrainByCell,occupied);
            std::vector<RuntimeCellPoint> points; RuntimeStrictSlotVehicleReferenceScope optimized(false);
            Authority before,after; if(!capture(s,before,error)) return false;
            if(!tryRuntimeStrictSlotVehicleCertificate(s,ids[0],goal,s.terrainByCell,occupied,ids[0].front(),ids[0].size(),&points)
                || points.empty()) return fail("actual positive certificate absent");
            if(!capture(s,after,error) || before!=after) return fail("pure proof changed full authority/writer");
            const auto origin=full.find(runtimeCellKey(representative->x,representative->y));
            if(origin==full.end()) return fail("original origin unmapped");
            for(const auto& point:points) { const auto found=full.find(runtimeCellKey(point.x,point.y));
                if(found==full.end() || found->second!=origin->second) return fail("certificate not same original directed component");
                ++counts.certifiedPoints; }
            ++counts.maps;
        }
        bool continued=false;
        // Balance execution order over four cases. Timing below is diagnostic
        // command service only, not a portable FPS/whole-game gain assertion.
        const bool optimizedFirst=history!=0;
        for(int tick=1;tick<=24;++tick) {
            std::array<Authority,2> captures;
            std::array<std::map<int,RuntimeCellPoint>,2> destinationsByRoute;
            const bool commandTick=tick==1 || tick==9 || tick==17;
            for(int route=0;route<2;++route) {
                const int branch=optimizedFirst ? 1-route : route;
                auto& s=*states[branch]; s.tick=tick;
                RuntimeSnapshotActiveStateScope active(s); RuntimeStrictSlotVehicleReferenceScope reference(branch==0);
                if(commandTick) {
                    const RuntimeCellPoint target{mid+22,mid+(tick==1?48:tick==9?64:52)};
                    if(tick==17) {
                        // The normal scalar cost remains legal but violates
                        // strict desired-score dominance: force real fallback.
                        s.terrainInfoByCell.at(runtimeCellKey(target.x,target.y)).cost=255;
                        if(runtimeMoveTargetSoftPenalty(s,s.terrainByCell,
                                buildRuntimeOccupancyLookup(s),target)<530)
                            return fail("actual original high-cost fallback prerequisite");
                    }
                    auto& destinations=destinationsByRoute[branch];
                    RuntimeStrictSlotVehicleCertificateStats witness;
                    const auto started=std::chrono::steady_clock::now();
                    { RuntimeStrictSlotVehicleStatsScope collect(witness);
                        if(!issueRuntimeMassMovementOrder(s,ids[branch],target,destinations,error)) return false; }
                    const double duration=std::chrono::duration<double,std::milli>(
                        std::chrono::steady_clock::now()-started).count();
                    counts.commandMilliseconds[branch]+=duration;
                    counts.maxCommandMilliseconds[branch]=std::max(counts.maxCommandMilliseconds[branch],duration);
                    const bool admitted=branch && tick!=17;
                    if(s.lastCommand.acceptedCount!=48 || s.lastCommand.rejectedCount!=0 || destinations.size()!=48
                        || witness.attempts!=1 || witness.admitted!=(admitted?1U:0U)
                        || witness.referenceFallbacks!=(branch?0U:1U)
                        || (branch && tick==17 && witness.slotFallbacks!=1)) {
                        return fail("command route tick="+std::to_string(tick)+" branch="+std::to_string(branch)
                            +" accepted="+std::to_string(s.lastCommand.acceptedCount)+" attempts="+std::to_string(witness.attempts)
                            +" admitted="+std::to_string(witness.admitted)+" guard="+std::to_string(witness.guardFallbacks)
                            +" layout="+std::to_string(witness.layoutFallbacks)+" slot="+std::to_string(witness.slotFallbacks)
                            +" walk="+std::to_string(witness.walkFallbacks)); }
                    if(!branch) ++counts.original;
                    else if(admitted) ++counts.admitted; else ++counts.fallbackCommands;
                    if(tick!=17) for(std::size_t i=0;i<ids[branch].size();++i) {
                        const auto offset=formationOffsetForIndex(static_cast<int>(i),1);
                        const RuntimeCellPoint expectedSlot{target.x+offset.x,target.y+offset.y};
                        if(destinations.at(ids[branch][i])!=expectedSlot)
                            return fail("original strict desired winner not selected");
                    }
                    counts.stats.add(witness); ++counts.commands;
                }
                (void)advanceRuntimeSimulation(s); (void)completeRuntimeWorldRefresh(s);
                if(!capture(s,captures[branch],error)) return false;
                continued|=std::any_of(s.behaviors.begin(),s.behaviors.end(),[](const auto& v) {
                    return v.second.moveCooldownTicks>0 || !v.second.path.empty(); });
            }
            if(commandTick && destinationsByRoute[0]!=destinationsByRoute[1])
                return fail("actual positive/fallback destination/order mismatch");
            if(captures[0]!=captures[1])
                return fail("completed tick path/world/systems/full serialized writer mismatch");
            ++counts.saves;
        }
        if(!continued) return fail("normal path/render continuation was not exercised");
    }
    return true;
}
}
bool runRuntimeStrictSlotVehicleCertificateActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeStrictSlotVehicleContractDetail;
    Counts c; if(!native(config,c,error)) return false;
    if(c.boundaries!=27 || c.maps!=4 || c.commands!=24 || c.saves!=96 || c.admitted!=8 || c.original!=12 || c.fallbackCommands!=4
        || c.actors!=192 || c.mtnk!=96 || c.htnk!=96 || !c.certifiedPoints
        || !c.stats.vertices || !c.stats.orientedEdges || !c.stats.referenceFallbacks) {
        error="strict-slot vehicle: coverage boundaries="+std::to_string(c.boundaries)+" maps="+std::to_string(c.maps)
            +" commands="+std::to_string(c.commands)+" saves="+std::to_string(c.saves)
            +" admitted="+std::to_string(c.admitted)+" original="+std::to_string(c.original); return false;
    }
    std::cout<<"Strict-slot vehicle certificate: boundaries=27 originalWholeMaps=4 nativeCommands=24 admittedCommands=8 referenceCommands=12 actualFallbackCommands=4 completedTickFullSavePairs=96 nativeActors=192 actualMTNK=96 actualHTNK=96 certifiedPoints="
        <<c.certifiedPoints<<" proofVertices="<<c.stats.vertices<<" orientedEdges="<<c.stats.orientedEdges
        <<" native240/native256/H0/H10000/originalLabels/strictWinner/commands/paths/fullWriter=exact passed=yes\n";
    std::cout<<"Strict-slot command service (diagnostic only): originalCommands=12 certificateCommands=12 originalTotalMs="
        <<c.commandMilliseconds[0]<<" certificateTotalMs="<<c.commandMilliseconds[1]
        <<" originalMaxMs="<<c.maxCommandMilliseconds[0]<<" certificateMaxMs="<<c.maxCommandMilliseconds[1]
        <<" balancedCaseOrder=yes clocks=wholeCommand loadingExcluded=yes\n";
    return true;
}
