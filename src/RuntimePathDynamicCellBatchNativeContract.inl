#pragma once

namespace RuntimePathDynamicCellBatchNativeContractDetail {
using namespace RuntimePathDynamicSparseQueriesContractDetail;
struct Counts {
    std::size_t snapshots=0, fields=0, groundEnabled=0, airRejected=0;
    std::size_t threatBuilders=0, radiationBuilders=0, commands=0, tickPairs=0;
};
bool sameStats(const RuntimePathDynamicSparseQueryStats& a, const RuntimePathDynamicSparseQueryStats& b) {
    return std::tie(a.enabled,a.retainedBytes,a.occupiedEntries,a.repairPoints,a.representativeBytesAvoided,
        a.proofs,a.proofFallbacks,a.representativeQueries,a.representativeSkipped,a.repairQueries,a.repairSkipped)
        == std::tie(b.enabled,b.retainedBytes,b.occupiedEntries,b.repairPoints,b.representativeBytesAvoided,
        b.proofs,b.proofFallbacks,b.representativeQueries,b.representativeSkipped,b.repairQueries,b.repairSkipped);
}
bool run(const RuntimeConfig& config, const RuntimeWindowState& assets, Counts& counts, std::string& error) {
    const auto fail=[&](const std::string& why){error="path dynamic cell batch: "+why;return false;};
    RuntimeSimulationQueryReuseReferenceScope reuse(false);
    RuntimePathSnapshotMetadataQueryReferenceScope metadata(false);
    RuntimePathSnapshotOccupancyQueryReferenceScope representative(false);
    RuntimePathDynamicSparseQueriesReferenceScope sparse(false);
    for (const int side : {240,256}) {
        const int population=side==240?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        std::array<int,3> movers{-1,-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& state=*states[branch];
            state.config=config;state.report=assets.report;state.rules=assets.rules;state.art=assets.art;state.theater=assets.theater;
            state.rulesLoaded=assets.report.rulesLoaded;state.artLoaded=assets.report.artLoaded;state.theaterLoaded=assets.report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,population,ids[branch],error,false,true,false,side,side,true,true))
                return fail("native producer side="+std::to_string(side)+": "+error);
            state.config.defaultMapPath=state.report.map.path;
            if(state.config.defaultMapPath!="MassMovement-v1.map" || ids[branch].size()!=static_cast<std::size_t>(population)
                ||state.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side))
                return fail("native producer/source cardinality side="+std::to_string(side));
            if(!branch) for(int id:ids[branch]) {
                const auto* actor=findObjectById(state,id);if(!actor) return fail("native selected ID disappeared");
                const auto mode=runtimeObjectMovementMode(state,*actor);
                if(mode==PathMovementMode::Ground&&actor->kind==WorldObjectKind::Unit&&movers[0]<0) movers[0]=id;
                if(mode==PathMovementMode::Ground&&actor->kind==WorldObjectKind::Infantry&&movers[1]<0) movers[1]=id;
                if(mode==PathMovementMode::Air&&movers[2]<0) movers[2]=id;
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]||std::any_of(movers.begin(),movers.end(),[](int id){return id<0;}))
            return fail("native identical stock Ground/Infantry/Air IDs");
        for(int index=0;index<3;++index) {
            Full before[2],after[2];std::vector<PathTerrainCell> values[2];Observation observations[2];
            for(int branch=0;branch<2;++branch) {
                auto& state=*states[branch];RuntimeSnapshotActiveStateScope active(state);
                RuntimePathDynamicCellBatchReferenceScope loop(branch==0);
                if(!capture(state,before[branch],error)) return fail("native admission before writer: "+error);
                const auto* actor=findObjectById(state,movers[index]);if(!actor) return fail("native probe mover");
                const auto threats=runtimePathThreatAreas(state,*actor,runtimePathThreatAvoidance(state,*actor));
                const bool radiation=std::any_of(state.specialMechanisms.radiationAreas().begin(),
                    state.specialMechanisms.radiationAreas().end(),[](const auto& entry){return entry.second.strength>0&&entry.second.remainingTicks>0;});
                if(!threats.empty()||radiation) return fail("ordinary native fixture unexpectedly contains threats/radiation");
                if(branch){counts.threatBuilders+=!threats.empty();counts.radiationBuilders+=radiation;}
                observations[branch].expected=&state;state.pathTerrainCache.dynamicValid=false;
                {
                    RuntimePathDynamicSparseQueryObserverScope observe(&Observation::append,&observations[branch]);
                    values[branch]=buildRuntimePathTerrainSnapshot(state,*actor);
                    std::size_t warmFields=0;
                    if(!sameSnapshot(values[branch],buildRuntimePathTerrainSnapshot(state,*actor),warmFields))
                        return fail("native warm ordered 12 fields");
                }
                if(!capture(state,after[branch],error)) return fail("native admission after writer: "+error);
                if(observations[branch].foreign||observations[branch].calls.size()!=1)
                    return fail("native exactly one cold admission owner");
                const auto& witness=observations[branch].calls.front();
                if(index<2) {
                    if(!witness.enabled || witness.proofs<values[branch].size()
                        ||!witness.representativeSkipped||!witness.repairSkipped)
                        return fail("native ordinary Ground/Infantry must genuinely elide queries side="+std::to_string(side));
                    if(branch)++counts.groundEnabled;
                } else {
                    if(witness.enabled||witness.proofs||witness.retainedBytes)
                        return fail("native Air must preserve original no-census branch");
                    if(branch)++counts.airRejected;
                }
            }
            if(!(before[0]==before[1])||!(after[0]==after[1])||!(before[0]==after[0])||!(before[1]==after[1]))
                return fail("native build full writer/world/system authority changed");
            if(!sameSnapshot(values[0],values[1],counts.fields)
                ||!sameStats(observations[0].calls.front(),observations[1].calls.front()))
                return fail("native single-call/per-cell-call ordered 12 fields and exact proof counts");
            ++counts.snapshots;
        }
        bool moved=false;
        for(int tick=1;tick<=32;++tick) {
            Full expected;std::map<int,RuntimeCellPoint> expectedDestinations;
            int expectedAccepted=-1,expectedRejected=-1;
            for(int branch=0;branch<2;++branch) {
                auto& state=*states[branch];state.tick=tick;RuntimeSnapshotActiveStateScope active(state);
                RuntimePathDynamicCellBatchReferenceScope loop(branch==0);
                if(tick==1||tick==9||tick==17) {
                    const int offset=side/2;
                    const RuntimeCellPoint goal=tick==9?RuntimeCellPoint{offset+22,offset+48}:RuntimeCellPoint{offset+76,offset+32};
                    std::map<int,RuntimeCellPoint> destinations;
                    if(!issueRuntimeMassMovementOrder(state,ids[branch],goal,destinations,error,true))
                        return fail("native actual normal command side="+std::to_string(side)+": "+error);
                    if(!branch){expectedDestinations=destinations;expectedAccepted=state.lastCommand.acceptedCount;expectedRejected=state.lastCommand.rejectedCount;}
                    else if(destinations!=expectedDestinations||state.lastCommand.acceptedCount!=expectedAccepted||state.lastCommand.rejectedCount!=expectedRejected)
                        return fail("native actual destination/acceptance/recovery mismatch");
                    ++counts.commands;
                }
                // No test observer is installed here: exercise CollectStats=false,
                // matching the normal actual benchmark's instantiated hot body.
                (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                Full current;if(!capture(state,current,error)) return fail("native actual completed-tick writer: "+error);
                if(!branch)expected=std::move(current);else if(!(current==expected))
                    return fail("native actual paths/full writer/world/systems side="+std::to_string(side)+" tick="+std::to_string(tick));
                moved|=std::any_of(state.behaviors.begin(),state.behaviors.end(),[](const auto& entry){return entry.second.moveCooldownTicks>0||!entry.second.path.empty();});
            }
            ++counts.tickPairs;
        }
        if(!moved)return fail("native actual command corpus never moved");
    }
    if(counts.snapshots!=6||counts.groundEnabled!=4||counts.airRejected!=2||counts.commands!=12||counts.tickPairs!=64
        ||!counts.fields||counts.threatBuilders||counts.radiationBuilders)
        return fail("native complete counts/absence/elision witnesses");
    return true;
}
}
