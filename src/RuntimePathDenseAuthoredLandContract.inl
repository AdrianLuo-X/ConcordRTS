// NEW isolated actual contract. Include after DynamicPreparationContract.
// No existing observer or contract is removed or replaced.
#pragma once
#include "RuntimeSavedPsychicWorldContract.inl"
#include <array>
#include <algorithm>
#include <iostream>
#include <vector>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>

namespace RuntimePathDenseAuthoredLandContractDetail {
using Full=RuntimePathSnapshotConditionalQueriesContractDetail::Full;
using RuntimePathSnapshotConditionalQueriesContractDetail::capture;
struct NativeCounts {
    std::size_t cases=0,actors=0,typeReadbacks=0,warmTickPairs=0,commands=0,completedTickPairs=0;
    std::size_t comparisons=0,fieldCells=0,pureWriterPairs=0,trajectoryPairs=0,queueResults=0,queueStats=0;
    std::size_t denseOwners=0,denseCandidates=0,denseOmitted=0,referenceOwners=0,warmCachePairs=0;
    std::size_t valueWrites=0,structureWrites=0,copyPairs=0,loadPairs=0,postLoadSnapshots=0;
};
struct Observation {
    const void* expectedState=nullptr;const void* expectedMover=nullptr;
    std::size_t calls=0;bool foreign=false;RuntimePathDenseAuthoredLandPreparationStats last;
    static void append(void* raw,const void* state,const void* mover,int,
        const RuntimePathDenseAuthoredLandPreparationStats& value) noexcept {
        auto& result=*static_cast<Observation*>(raw);++result.calls;
        result.foreign|=state!=result.expectedState||mover!=result.expectedMover;result.last=value;
    }
};
bool fail(std::string& error,const std::string& message) {
    error="dense authored land native: "+message;return false;
}
bool sameFields(const std::vector<PathTerrainCell>& a,const std::vector<PathTerrainCell>& b,NativeCounts& counts) {
    if(a.size()!=b.size()||a.empty())return false;
    for(std::size_t index=0;index<a.size();++index) {
        if(RuntimePathSnapshotMetadataQueriesContractDetail::fields(a[index])
            !=RuntimePathSnapshotMetadataQueriesContractDetail::fields(b[index]))return false;
        ++counts.fieldCells;
    }
    return true;
}
bool compareQueues(const std::vector<PathTerrainCell>& a,const std::vector<PathTerrainCell>& b,NativeCounts& counts) {
    const auto original=RuntimePathDynamicPreparationContractDetail::queueProof(a);
    const auto dense=RuntimePathDynamicPreparationContractDetail::queueProof(b);
    if(original.size()!=dense.size())return false;
    for(std::size_t step=0;step<original.size();++step) {
        if(original[step].processed!=dense[step].processed
            ||!RuntimePathDynamicPreparationContractDetail::sameStats(original[step].stats,dense[step].stats)
            ||original[step].results.size()!=dense[step].results.size())return false;
        ++counts.queueStats;
        for(std::size_t result=0;result<original[step].results.size();++result) {
            if(!RuntimePathDynamicPreparationContractDetail::sameResult(original[step].results[result],dense[step].results[result]))return false;
            ++counts.queueResults;
        }
    }
    return true;
}
bool compareSnapshots(std::array<std::unique_ptr<RuntimeWindowState>,2>& states,int moverId,
    NativeCounts& counts,std::string& error,const std::string& label,bool requireDense=true,
    const RuntimeCellPoint* changedPoint=nullptr,int expectedPassable=-1) {
    std::array<Full,2> before,after;
    std::array<std::vector<PathTerrainCell>,2> values,warm;
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch];RuntimeSnapshotActiveStateScope active(state);
        RuntimePathDenseAuthoredLandReferenceScope reference(branch==0);
        if(!capture(state,before[branch],error))return fail(error,label+" before writer: "+error);
        state.pathTerrainCache.dynamicValid=false;
        const auto* actor=findObjectById(state,moverId);if(!actor)return fail(error,label+" actual mover absent");
        Observation observation;observation.expectedState=&state;observation.expectedMover=actor;
        {
            RuntimePathDenseAuthoredLandPreparationObserverScope observe(&Observation::append,&observation);
            values[branch]=buildRuntimePathTerrainSnapshot(state,*actor);
            warm[branch]=buildRuntimePathTerrainSnapshot(state,*actor);
        }
        if(observation.calls!=1||observation.foreign||observation.last.entries!=values[branch].size())
            return fail(error,label+" one actual cold builder and zero warm rebuilds");
        const auto& proof=observation.last;
        if(branch==0) {
            if(proof.arrayAcquired||proof.policyDispatched||proof.denseAdmitted)
                return fail(error,label+" reference must use original CURRENT node finder");
            ++counts.referenceOwners;
        } else if(requireDense) {
            if(!proof.eligible||!proof.arrayAcquired||!proof.policyDispatched||!proof.proofAdmitted||!proof.denseAdmitted
                ||proof.candidates==0||proof.candidates>=proof.entries||!state.terrainInfoByCell.authoredLandBuildCount()) {
                std::ostringstream detail;detail<<label<<" actual enabled dense admission"
                    <<" eligible="<<proof.eligible<<" acquired="<<proof.arrayAcquired<<" dispatched="<<proof.policyDispatched
                    <<" proof="<<proof.proofAdmitted<<" dense="<<proof.denseAdmitted
                    <<" entries="<<proof.entries<<" candidates="<<proof.candidates;
                return fail(error,detail.str());
            }
            ++counts.denseOwners;counts.denseCandidates+=proof.candidates;counts.denseOmitted+=proof.entries-proof.candidates;
        }
        if(!sameFields(values[branch],warm[branch],counts))return fail(error,label+" warm original complete 12 fields");
        if(changedPoint) {
            const auto found=std::find_if(values[branch].begin(),values[branch].end(),[&](const auto& value){return value.cell.x==changedPoint->x&&value.cell.y==changedPoint->y;});
            if(found==values[branch].end()||(expectedPassable>=0&&found->passable!=(expectedPassable!=0)))
                return fail(error,label+" actual current permission output did not change as required");
        }
        if(!capture(state,after[branch],error)||!(before[branch]==after[branch]))return fail(error,label+" pure snapshot changed full writer/paths/authority");
    }
    if(!(before[0]==before[1])||!(after[0]==after[1])||!sameFields(values[0],values[1],counts)
        ||!compareQueues(values[0],values[1],counts))return fail(error,label+" original vs enabled full writer/ordered12/queue results+all statistics");
    ++counts.comparisons;counts.pureWriterPairs+=2;++counts.warmCachePairs;
    return true;
}
bool trajectory(const RuntimeWindowState& a,const RuntimeWindowState& b,const std::vector<int>& ids) {
    for(int id:ids) {
        const auto* x=findObjectById(a,id);const auto* y=findObjectById(b,id);
        if(!x||!y||std::tie(x->alive,x->health,x->x,x->y,x->cell,x->onBridge,x->airborne,x->level,x->subCell,x->facing,x->mission,
                x->renderMoveActive,x->renderX,x->renderY)
            !=std::tie(y->alive,y->health,y->x,y->y,y->cell,y->onBridge,y->airborne,y->level,y->subCell,y->facing,y->mission,
                y->renderMoveActive,y->renderX,y->renderY))return false;
    }
    return true;
}
bool currentWrites(std::array<std::unique_ptr<RuntimeWindowState>,2>& states,int side,int mover,
    NativeCounts& counts,std::string& error) {
    const RuntimeCellPoint point{side/2+64,side/2+64};const int key=runtimeCellKey(point.x,point.y);
    std::array<RuntimeTerrainInfo,2> original;
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch];const auto* actor=findObjectById(state,mover);
        if(!actor||!state.terrainInfoByCell.contains(key))return fail(error,"current-value fixture requires real authored point/mover");
        original[branch]=state.terrainInfoByCell.at(key);
        const auto* water=state.rules.findLandType("Water");
        if(!water||runtimeLandTypePercentForMover(*water,state,*actor)>0.0
            ||!runtimeAuthoredLandTypeAllowsMover(state,original[branch],*actor))return fail(error,"stock original Water denial/Clear allowance prerequisite");
    }
    if(!compareSnapshots(states,mover,counts,error,"before current writes",true,&point,1))return false;
    for(int phase=0;phase<6;++phase) {
        std::array<std::uint64_t,2> generation{};
        std::array<std::size_t,2> builds{};
        for(int branch=0;branch<2;++branch) {
            auto& state=*states[branch];generation[branch]=state.terrainInfoByCell.generation();
            builds[branch]=state.terrainInfoByCell.authoredLandBuildCount();
            if(phase<4) {
                auto& retained=state.terrainInfoByCell.at(key);auto& retainedField=retained.authoredLandType;const auto* address=&retained;
                if(phase==0)retainedField=RuntimeTerrainLandType::Water;
                if(phase==1){auto whole=original[branch];whole.authoredLandType=RuntimeTerrainLandType::Unknown;retained=whole;}
                if(phase==2)retainedField=static_cast<RuntimeTerrainLandType>(127);
                if(phase==3)retained=original[branch];
                if(state.terrainInfoByCell.generation()!=generation[branch]||&state.terrainInfoByCell.at(key)!=address)
                    return fail(error,"retained field/whole-value write changed original node identity");
            } else if(phase==4)state.terrainInfoByCell.erase(key);
            else state.terrainInfoByCell.try_emplace(key,original[branch]);
            state.pathTerrainCache.dynamicValid=false;
        }
        const int expectedPassable=phase==0?0:1;
        if(!compareSnapshots(states,mover,counts,error,"current writes phase="+std::to_string(phase),true,&point,expectedPassable))return false;
        for(int branch=0;branch<2;++branch) {
            const auto& state=*states[branch];
            if(phase<4&&state.terrainInfoByCell.authoredLandBuildCount()!=builds[branch])return fail(error,"value write must update live array without cold rebuild");
            if(phase>=4&&(state.terrainInfoByCell.generation()==generation[branch]
                ||(branch==1&&state.terrainInfoByCell.authoredLandBuildCount()!=builds[branch]+1)))
                return fail(error,"structural write must really invalidate and rebuild current array");
        }
        if(phase<4)++counts.valueWrites;else ++counts.structureWrites;
    }
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch];auto copy=state.terrainInfoByCell;
        if(copy.authoredLandValuesValid())return fail(error,"actual map copy borrowed source ordinal storage");
        copy.at(key).authoredLandType=RuntimeTerrainLandType::Water;
        if(state.terrainInfoByCell.at(key).authoredLandType!=original[branch].authoredLandType)return fail(error,"detached copy wrote original map");
        copy.at(key)=original[branch];state.terrainInfoByCell=std::move(copy);state.pathTerrainCache.dynamicValid=false;
    }
    if(!compareSnapshots(states,mover,counts,error,"whole map copy/move current binding",true,&point,1))return false;
    ++counts.copyPairs;return true;
}
bool saveReadLoad(std::array<std::unique_ptr<RuntimeWindowState>,2>& states,const std::array<int,2>& movers,
    int caseIndex,NativeCounts& counts,std::string& error) {
    // Use the normal public atomic file writer and load transaction. The map
    // identity is the actual producer's report.path. No invented map filename.
    std::array<Full,2> before,after;
    std::array<std::filesystem::path,2> retainedSavePaths;
    for(int branch=0;branch<2;++branch) {
        auto& state=*states[branch];RuntimeSnapshotActiveStateScope active(state);RuntimePathDenseAuthoredLandReferenceScope reference(branch==0);
        if(!capture(state,before[branch],error))return false;
        const auto path=std::filesystem::temp_directory_path()/(
            "concord_dense_land_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64())
            +"_"+std::to_string(caseIndex)+"_"+std::to_string(branch)+".ini");
        retainedSavePaths[branch]=path;
        if(std::filesystem::exists(path))return fail(error,"fresh actual save path unexpectedly exists");
        if(!saveRuntimeState(state,pathString(path),error)||!loadRuntimeState(state,pathString(path),error))
            return fail(error,"normal save/read/load transaction: "+error);
        // Preserve these private proof files; no deletion and no overwritten
        // failure artefact. Do not retain any pre-load actor/Value references.
        std::cout<<"Dense authored land load fixture: "<<pathString(path)<<'\n';
        if(state.tick!=76||state.terrainInfoByCell.authoredLandValuesValid())return fail(error,"real load did not cold-invalidate map binding/table");
        if(!capture(state,after[branch],error))return false;
    }
    if(!(before[0]==before[1])||!(after[0]==after[1]))return fail(error,"normal load complete original vs enabled authority/writer diverged");
    // Offline load may legally normalize presentation. Both original routes
    // execute that exact load; this is not an assertion of no normalization.
    for(int mover:movers){if(!compareSnapshots(states,mover,counts,error,"post actual load current nodes"))return false;++counts.postLoadSnapshots;}
    if(caseIndex==1 && !runRuntimeSavedPsychicWorldNegativeLoads(*states[1],retainedSavePaths[1],error))return false;
    ++counts.loadPairs;return true;
}
} // namespace RuntimePathDenseAuthoredLandContractDetail

bool runRuntimePathDenseAuthoredLandActualContract(const RuntimeConfig& config,std::string& error) {
    namespace D=RuntimePathDenseAuthoredLandContractDetail;
    if(!runRuntimeSavedPsychicWorldPrimitiveContract(error))return false;
    D::NativeCounts counts;
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded)return D::fail(error,"actual stock assets");
    // Every other optimizer follows the SAME default in both independent states.
    RuntimePathDynamicPreparationReferenceScope preparation(false);RuntimePathDynamicWorksetReferenceScope workset(false);
    RuntimePathDynamicResourceFusionReferenceScope resources(false);RuntimePathDynamicCellBatchReferenceScope batch(false);
    RuntimeSimulationQueryReuseReferenceScope reuse(false);RuntimePathSnapshotMetadataQueryReferenceScope metadata(false);
    RuntimePathSnapshotOccupancyQueryReferenceScope occupancy(false);RuntimePathDynamicSparseQueriesReferenceScope sparse(false);
    constexpr std::array<const char*,12> types{{"MTNK","SREF","TTNK","FV","SCHP","HTNK","E1","CLEG","JUMPJET","SNIPE","GHOST","E2"}};
    int caseIndex=0;
    for(int side:{240,256})for(int history:{0,10000}) {
        const int population=side==240?48:256;
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;std::array<std::vector<int>,2> ids;
        std::array<int,2> movers{-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& state=*states[branch];RuntimePathDenseAuthoredLandReferenceScope reference(branch==0);
            if(!RuntimeMixedLocalComponentContractDetail::prepareNative(state,*assets,config,side,history,false,ids[branch],error,population))return false;
            if(state.tick!=0||state.world.getObjects().size()!=static_cast<std::size_t>(population+9+history)
                ||state.world.getTerrainCells().size()!=static_cast<std::size_t>(2*side*side-side)
                ||state.config.defaultMapPath!=state.report.map.path)return D::fail(error,"normally decoded native geometry/population/identity");
            std::array<std::size_t,12> roster{};
            for(int id:ids[branch]) {
                const auto* actor=findObjectById(state,id);if(!actor||!actor->alive||actor->health<=0)return D::fail(error,"real living selected ID");
                const auto at=std::find(types.begin(),types.end(),actor->type);if(at==types.end())return D::fail(error,"unexpected stock roster type");
                ++roster[static_cast<std::size_t>(at-types.begin())];
                if(branch==0&&runtimeObjectMovementMode(state,*actor)==PathMovementMode::Ground) {
                    if(actor->kind==WorldObjectKind::Unit&&movers[0]<0)movers[0]=id;
                    if(actor->kind==WorldObjectKind::Infantry&&movers[1]<0)movers[1]=id;
                }
            }
            if(!std::all_of(roster.begin(),roster.end(),[](auto count){return count>0;}))return D::fail(error,"all twelve genuinely hydrated types required");
        }
        if(ids[0]!=ids[1]||movers[0]<0||movers[1]<0)return D::fail(error,"independent native selected order/Ground/Infantry mismatch");
        ++counts.cases;counts.actors+=population;counts.typeReadbacks+=12;
        for(int mover:movers)if(!D::compareSnapshots(states,mover,counts,error,"cold native case="+std::to_string(caseIndex)))return false;
        bool moved=false;
        for(int tick=1;tick<=76;++tick) {
            std::array<D::Full,2> completed;
            std::map<int,RuntimeCellPoint> gold;int accepted=-1,rejected=-1;
            for(int branch=0;branch<2;++branch) {
                auto& state=*states[branch];RuntimeSnapshotActiveStateScope active(state);RuntimePathDenseAuthoredLandReferenceScope reference(branch==0);state.tick=tick;
                if(tick==61) {
                    const RuntimeCellPoint goal{side/2+76,side/2+32};std::map<int,RuntimeCellPoint> destinations;
                    if(!issueRuntimeMassMovementOrder(state,ids[branch],goal,destinations,error,true))return D::fail(error,"real warm mixed command: "+error);
                    if(branch==0){gold=destinations;accepted=state.lastCommand.acceptedCount;rejected=state.lastCommand.rejectedCount;}
                    else if(gold!=destinations||accepted!=state.lastCommand.acceptedCount||rejected!=state.lastCommand.rejectedCount)return D::fail(error,"original command exact destinations/accept/reject/order");
                    ++counts.commands;
                }
                (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                moved|=std::any_of(state.behaviors.begin(),state.behaviors.end(),[](const auto& entry){return !entry.second.path.empty()||entry.second.moveCooldownTicks>0;});
                if(tick==60&&!D::capture(state,completed[branch],error))return false;
                if(tick>=61&&!D::capture(state,completed[branch],error))return false;
            }
            if(!D::trajectory(*states[0],*states[1],ids[0]))return D::fail(error,"per-tick original selected trajectory/presentation/mission");
            ++counts.trajectoryPairs;
            if(tick<=60)++counts.warmTickPairs;
            if(tick==60&&!(completed[0]==completed[1]))return D::fail(error,"real60 warm completed full writer");
            if(tick>=61) {
                if(!(completed[0]==completed[1]))return D::fail(error,"completed-tick full writer/paths/all systems");
                ++counts.completedTickPairs;
                if(!D::compareSnapshots(states,movers[static_cast<std::size_t>(tick%2)],counts,error,"completed tick="+std::to_string(tick)))return false;
            }
        }
        if(!moved)return D::fail(error,"real stock movement/path continuation never occurred");
        if(!D::currentWrites(states,side,movers[0],counts,error))return false;
        if(side==240&&!D::saveReadLoad(states,movers,caseIndex,counts,error))return false;
        ++caseIndex;
    }
    // 4*(2 cold+16 completed+1 prewrite+6 writes+1 copy)+2*2 post-load.
    if(counts.cases!=4||counts.actors!=608||counts.typeReadbacks!=48||counts.warmTickPairs!=240||counts.commands!=8
        ||counts.completedTickPairs!=64||counts.trajectoryPairs!=304||counts.comparisons!=108
        ||counts.denseOwners!=108||counts.referenceOwners!=108||counts.warmCachePairs!=108||counts.pureWriterPairs!=216
        ||counts.valueWrites!=16||counts.structureWrites!=8||counts.copyPairs!=4||counts.loadPairs!=2||counts.postLoadSnapshots!=4
        ||!counts.fieldCells||!counts.denseCandidates||!counts.denseOmitted||!counts.queueResults||!counts.queueStats)
        return D::fail(error,"complete enabled coverage counts cases="+std::to_string(counts.cases)+" comparisons="+std::to_string(counts.comparisons)+" denseOwners="+std::to_string(counts.denseOwners));
    std::cout<<"Dense authored land actual: cases="<<counts.cases<<" actors="<<counts.actors<<" stockTypes="<<counts.typeReadbacks
        <<" warmTickPairs="<<counts.warmTickPairs<<" commands="<<counts.commands<<" completedTickPairs="<<counts.completedTickPairs
        <<" trajectoryPairs="<<counts.trajectoryPairs<<" snapshotPairs="<<counts.comparisons<<" fields=12 fullFieldCells="<<counts.fieldCells
        <<" denseAdmitted="<<counts.denseOwners<<" pointerReferenceOwners="<<counts.referenceOwners<<" warmCachePairs="<<counts.warmCachePairs
        <<" pureWriterPairs="<<counts.pureWriterPairs<<" denseCandidates="<<counts.denseCandidates<<" denseOmitted="<<counts.denseOmitted
        <<" valueWrites="<<counts.valueWrites<<" structureWrites="<<counts.structureWrites<<" copyPairs="<<counts.copyPairs
        <<" loadPairs="<<counts.loadPairs<<" postLoadSnapshots="<<counts.postLoadSnapshots<<" queueResults="<<counts.queueResults
        <<" queueStats="<<counts.queueStats<<" enabled-current/aliases/unknown127/holes/copy/load/paths/fullWriter=yes passed=yes\n";
    error.clear();return true;
}
