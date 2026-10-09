// Include after RuntimePlannerQueryLeaseContract.inl. Exercise the ORIGINAL
// production transition and complete BFS; no mirrored graph is its reference.
#include <array>
#include <limits>
#include <memory>
#include <optional>
namespace RuntimePlannerIndexedTopologyContractDetail {
using namespace RuntimePlannerPassabilityContractDetail;
struct Witness {
    std::size_t owners=0,enabled=0,slots=0,pairs=0,fallbacks=0;
    static void append(void* context,bool active,std::size_t count,std::size_t used,std::size_t fallback) {
        auto& out=*static_cast<Witness*>(context);
        ++out.owners; out.enabled+=active?1:0; out.slots+=count; out.pairs+=used; out.fallbacks+=fallback;
    }
};
bool fixture(RuntimeWindowState& state,const RuntimeConfig& config,int history,int phase,std::string& error) {
    if(!make(state,config,history,phase,error)) return false;
    // Navigation initialization replaces synthetic art foundations. Author
    // only this new fixture after it returns, as in the original lease test.
    state.structureFootprints["SOLID"]={2,2,true}; state.structureFootprints["LANE"]={3,2,true};
    state.structureFootprints["GATE"]={1,1,true}; state.structureFootprints["REPAIR"]={2,2,true};
    state.structureFootprints["WALL"]={1,1,true}; state.structureFootprints["YARD"]={2,2,true};
    invalidateRuntimeTickOccupancyCache(state);
    return true;
}
bool transition(const RuntimeWindowState& state,const std::unordered_map<int,int>& occupied,
    const RuntimeCellPoint& current,const RuntimeCellPoint& next,const WorldObjectInstance& mover,
    int allowed,const std::unordered_set<int>* soft,bool crush,std::optional<PathMovementMode> mode,
    const RuntimePlannerIndexedTopology* topology) {
    return runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,current,next,mover,
        allowed,soft,crush,mode,nullptr,nullptr,topology);
}
} // namespace RuntimePlannerIndexedTopologyContractDetail

bool runRuntimePlannerIndexedTopologyActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePlannerIndexedTopologyContractDetail;
    RuntimeComponentPassabilityReferenceScope memo(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrainMemo(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    RuntimeComponentCrushAliasReferenceScope alias(false);
    RuntimeComponentIndexedTopologyReferenceScope indexed(false);
    const auto fail=[&](const std::string& reason) {
        error="planner indexed topology geometry: "+reason; return false;
    };
    std::size_t edgeChecks=0,geometryChecks=0,componentChecks=0,boundaries=0,commandChecks=0,tickChecks=0,usedPairs=0;
    // Exhaust every coordinate in the meaningful authored 12x12 fixture and
    // every neighbor, plus original invalid-length/same-point short circuits.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!fixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        Authority before,after; if(!capture(state,before,error)) return false;
        std::vector<RuntimeCellPoint> starts;
        for(int y=0;y<12;++y) for(int x=0;x<12;++x) starts.push_back({x,y});
        starts.insert(starts.end(),{{-1,0},{12,11},{1000,0},{0,1}});
        const std::unordered_set<int> emptySoft,enemySoft{35};
        const std::array<std::optional<PathMovementMode>,6> modes{std::nullopt,PathMovementMode::Ground,
            PathMovementMode::Naval,PathMovementMode::Amphibious,PathMovementMode::Air,
            static_cast<PathMovementMode>(99)};
        for(int id:{1,2,3,4,5,6,41}) {
            const auto* mover=findObjectById(state,id); if(!mover) return fail("directed mover missing");
            RuntimeBuildingDepartureReadOnlyQueryScope departures(state,*mover);
            RuntimePlannerCellPassabilityScope scope(state,*mover,state.terrainByCell,occupied);
            RuntimePlannerIndexedTopology topology(state,*mover,state.terrainByCell,occupied);
            if(!topology.enabled() || topology.retainedSlots()!=144
                || topology.retainedBytes()>RuntimePlannerIndexedTopology::maximumSlots*sizeof(RuntimePlannerIndexedTopology::Geometry))
                return fail("canonical metadata-complete synthetic admission/byte bound");
            for(const auto& current:starts) {
                auto nexts=std::vector<RuntimeCellPoint>{current,{current.x+2,current.y}};
                const auto adjacent=runtimeNeighborsOf(current); nexts.insert(nexts.end(),adjacent.begin(),adjacent.end());
                for(const auto& next:nexts) for(auto mode:modes) for(int allowed:{-1,31}) for(bool crush:{false,true})
                    for(const auto* soft:{static_cast<const std::unordered_set<int>*>(nullptr),&emptySoft,&enemySoft}) {
                        Observations oldQueries,newQueries; bool oldValue=false,newValue=false;
                        {
                            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&oldQueries);
                            oldValue=transition(state,occupied,current,next,*mover,allowed,soft,crush,mode,nullptr);
                        }
                        {
                            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&newQueries);
                            newValue=transition(state,occupied,current,next,*mover,allowed,soft,crush,mode,&topology);
                        }
                        if(oldValue!=newValue || oldQueries.values!=newQueries.values)
                            return fail("directed result/complete ordered dynamic keys id="+std::to_string(id)
                                +" from="+std::to_string(current.x)+","+std::to_string(current.y)
                                +" to="+std::to_string(next.x)+","+std::to_string(next.y));
                        ++edgeChecks;
                    }
            }
            usedPairs+=topology.pairs();
        }
        if(!capture(state,after,error) || !(after==before)) return fail("directed oracle mutated full authority/save");
    }
    // Prove the exact cached payload and high-bridge height formula, including
    // its authored-string/placeholder alternatives. The source is edited only
    // BETWEEN owners; each query still runs the complete original transition.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!fixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const auto* mover=findObjectById(state,1); if(!mover) return fail("geometry payload mover missing");
        const RuntimeCellPoint from{0,0},to{1,0};
        auto* current=const_cast<TerrainCell*>(state.terrainByCell.at(runtimeCellKey(0,0)));
        auto* next=const_cast<TerrainCell*>(state.terrainByCell.at(runtimeCellKey(1,0)));
        for(int overlayCase=0;overlayCase<9;++overlayCase) for(int level=-4;level<=4;++level)
            for(int flags=0;flags<32;++flags) {
                current->level=-1; next->level=level;
                next->hasOverlay=overlayCase!=0; next->overlayType.clear(); next->overlayImage.clear();
                next->overlayIndex=overlayCase>=1 && overlayCase<=4
                    ? std::array<int,4>{24,25,237,238}[static_cast<std::size_t>(overlayCase-1)] : -1;
                if(overlayCase==5) next->overlayIndex=100;
                if(overlayCase==6) next->overlayType="BRIDGE";
                if(overlayCase==7) next->overlayImage="lobrdg27";
                if(overlayCase==8) next->overlayType="Plain";
                auto& a=state.terrainInfoByCell.at(runtimeCellKey(0,0));
                auto& b=state.terrainInfoByCell.at(runtimeCellKey(1,0));
                a.water=false; a.shore=(flags&1)!=0; a.bridge=(flags&2)!=0; a.ramp=(flags&4)!=0;
                b.water=(flags&8)!=0; b.shore=false; b.bridge=false; b.ramp=(flags&16)!=0;
                RuntimePlannerIndexedTopology owner(state,*mover,state.terrainByCell,occupied);
                const auto pair=owner.pairFor(state,*mover,state.terrainByCell,occupied,from,to);
                if(!pair || pair->current->terrain!=current || pair->next->terrain!=next
                    || pair->current->effectiveLevel!=runtimeEffectiveTerrainLevel(*current)
                    || pair->next->effectiveLevel!=runtimeEffectiveTerrainLevel(*next)
                    || pair->current->water!=a.water || pair->current->shore!=a.shore
                    || pair->current->bridge!=a.bridge || pair->current->ramp!=a.ramp
                    || pair->next->water!=b.water || pair->next->shore!=b.shore
                    || pair->next->bridge!=b.bridge || pair->next->ramp!=b.ramp)
                    return fail("exact cached geometry/high-bridge payload");
                Authority before,after; if(!capture(state,before,error)) return false;
                for(auto mode:{PathMovementMode::Ground,PathMovementMode::Amphibious})
                    if(transition(state,occupied,from,to,*mover,-1,nullptr,true,mode,&owner)
                        !=transition(state,occupied,from,to,*mover,-1,nullptr,true,mode,nullptr))
                        return fail("authored geometry transition overlay="+std::to_string(overlayCase)
                            +" level="+std::to_string(level)+" flags="+std::to_string(flags));
                if(!capture(state,after,error) || !(before==after)) return fail("geometry payload oracle mutated authority");
                usedPairs+=owner.pairs(); ++geometryChecks;
            }
    }
    // Every malformed input must disable the ENTIRE topology, not silently
    // reinterpret legacy-key aliases. Read same-address edits only after an
    // explicit invalidation, and restore a fresh owner afterwards.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!fixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const auto* mover=findObjectById(state,1); if(!mover) return fail("boundary mover missing");
        const RuntimeCellPoint from{0,0},to{1,0};
        RuntimePlannerIndexedTopology outer(state,*mover,state.terrainByCell,occupied);
        if(!outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to)) return fail("boundary warm pair");
        {
            RuntimeComponentIndexedTopologyReferenceScope reference(true);
            if(outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to)) return fail("dynamic reference borrowed");
            ++boundaries;
        }
        {
            RuntimePlannerIndexedTopology disabled(state,*mover,state.terrainByCell,occupied,false);
            if(disabled.enabled() || outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to))
                return fail("disabled nested owner did not mask outer");
            ++boundaries;
        }
        {
            RuntimePlannerIndexedTopology inner(state,*mover,state.terrainByCell,occupied);
            if(!inner.pairFor(state,*mover,state.terrainByCell,occupied,from,to)
                || outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to)) return fail("nested owned masking");
            ++boundaries;
        }
        auto foreignTerrain=state.terrainByCell; auto foreignOccupancy=occupied;
        const auto foreignMover=*mover;
        if(outer.pairFor(state,*mover,foreignTerrain,occupied,from,to)
            || outer.pairFor(state,*mover,state.terrainByCell,foreignOccupancy,from,to)
            || outer.pairFor(state,foreignMover,state.terrainByCell,occupied,from,to)
            || outer.pairFor(state,*mover,state.terrainByCell,occupied,{1000,0},{1001,0}))
            return fail("foreign/address/raw-alias boundary borrowed");
        boundaries+=4;
        ++state.tick;
        if(outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to)) return fail("tick boundary borrowed");
        ++boundaries; --state.tick;
        outer.invalidate();
        if(outer.pairFor(state,*mover,state.terrainByCell,occupied,from,to)) return fail("invalidated owner borrowed");
        ++boundaries;
        for(int variant=0;variant<6;++variant) {
            auto malformed=state.terrainByCell;
            if(variant==0) malformed.emplace(-1,malformed.begin()->second);
            if(variant==1) malformed.begin()->second=nullptr;
            if(variant==2) malformed[runtimeCellKey(1000,0)]=state.terrainByCell.at(runtimeCellKey(4,6));
            if(variant==3) malformed.emplace(std::numeric_limits<int>::max(),malformed.begin()->second);
            if(variant==4) malformed.clear();
            std::optional<RuntimeTerrainInfo> removed;
            const int metadataKey=runtimeCellKey(1,0);
            if(variant==5) { removed=state.terrainInfoByCell.at(metadataKey); state.terrainInfoByCell.erase(metadataKey); }
            {
                RuntimePlannerIndexedTopology rejected(state,*mover,malformed,occupied);
                if(rejected.enabled()) return fail("anomalous whole-map admission variant="+std::to_string(variant));
            }
            if(removed) state.terrainInfoByCell.emplace(metadataKey,*removed);
            ++boundaries;
        }
        {
            auto sparse=state.terrainByCell; sparse.erase(runtimeCellKey(1,0));
            RuntimePlannerIndexedTopology owner(state,*mover,sparse,occupied);
            const auto pair=owner.pairFor(state,*mover,sparse,occupied,from,to);
            if(!owner.enabled() || !pair || pair->next->terrain
                || runtimeCellTransitionIsPassable(state,sparse,occupied,from,to,*mover,-1,nullptr,true,
                    std::nullopt,nullptr,nullptr,&owner)
                    !=runtimeCellTransitionIsPassable(state,sparse,occupied,from,to,*mover))
                return fail("canonical sparse hole was reinterpreted as terrain");
            ++boundaries;
        }
        {
            auto* cell=const_cast<TerrainCell*>(state.terrainByCell.at(runtimeCellKey(1,0)));
            const int originalLevel=cell->level; cell->level=std::numeric_limits<int>::max();
            RuntimePlannerIndexedTopology rejected(state,*mover,state.terrainByCell,occupied);
            if(rejected.enabled()) return fail("extreme level was eagerly narrowed/overflowed");
            cell->level=originalLevel; ++boundaries;
        }
        // This actual kernel owns BOTH geometry and the hot passability memo.
        // Invalidate it before the same-address authority edit; fallback must
        // not consult the now-stale memo or effective-level record.
        {
            RuntimePlannerCellPassabilityScope scope(state,*mover,state.terrainByCell,occupied);
            RuntimePlannerConnectivityKernel owned(state,*mover,state.terrainByCell,occupied,false,&scope);
            const bool old=owned.connects(from,to);
            if(!old) return fail("actual level-mutation fixture must initially connect");
            owned.invalidate();
            auto* cell=const_cast<TerrainCell*>(state.terrainByCell.at(runtimeCellKey(1,0)));
            cell->level=3;
            const bool scalar=runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,from,to,*mover);
            if(scalar || owned.connects(from,to)!=scalar) return fail("same-address level edit stale geometry/memo");
            ++boundaries;
        }
        {
            RuntimePlannerIndexedTopology fresh(state,*mover,state.terrainByCell,occupied);
            if(!fresh.enabled() || transition(state,occupied,from,to,*mover,-1,nullptr,true,std::nullopt,&fresh)
                !=transition(state,occupied,from,to,*mover,-1,nullptr,true,std::nullopt,nullptr))
                return fail("fresh post-edit owner geometry equivalence");
            fresh.invalidate();
            state.terrainInfoByCell.at(runtimeCellKey(1,0)).water=true;
            if(fresh.pairFor(state,*mover,state.terrainByCell,occupied,from,to)
                || transition(state,occupied,from,to,*mover,-1,nullptr,true,std::nullopt,&fresh)
                    !=transition(state,occupied,from,to,*mover,-1,nullptr,true,std::nullopt,nullptr))
                return fail("same-address metadata value edit after invalidation");
            ++boundaries;
        }
    }
    for(int history:{0,1000,10000}) for(int phase:{0,1}) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!fixture(state,config,history,phase,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        auto malformed=state.terrainByCell; malformed.emplace(-1,malformed.begin()->second);
        for(const auto* terrain:{&state.terrainByCell,&malformed}) for(int id:{1,2,3,4,5,6,41}) {
            const auto* mover=findObjectById(state,id); if(!mover) return fail("whole-component mover missing");
            Authority before,after; if(!capture(state,before,error)) return false;
            Observations oldQueries; std::vector<std::pair<int,int>> expected;
            std::optional<RuntimeCellPoint> expectedFallback;
            for(int branch=0;branch<2;++branch) {
                RuntimeComponentIndexedTopologyReferenceScope reference(branch==0);
                Observations queries; Witness witness; std::unordered_map<int,int> result;
                {
                    RuntimePlannerPassabilityObserverScope observe(&Observations::append,&queries);
                    RuntimePlannerIndexedTopologyObserverScope observeOwner(&Witness::append,&witness);
                    result=buildRuntimeMoveComponentByCell(state,*mover,*terrain,occupied);
                }
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;
                if(const auto found=result.find(runtimeCellKey(mover->x,mover->y));found!=result.end()) required=found->second;
                const auto fallback=findNearestRuntimePassableCellWithOccupancy(state,*mover,{100,100},-1,nullptr,
                    *terrain,occupied,nullptr,&result,required);
                if(!branch) { expected=order; oldQueries=std::move(queries); expectedFallback=fallback; }
                else if(order!=expected || queries.values!=oldQueries.values || fallback!=expectedFallback)
                    return fail("complete labels/unordered order/directed seed/dynamic queries/global tie differs");
                if(branch==0 && (witness.enabled || witness.pairs)) return fail("reference accidentally used topology");
                if(branch && terrain==&state.terrainByCell && runtimeObjectMovementMode(state,*mover)!=PathMovementMode::Air
                    && witness.enabled!=1) return fail("whole canonical component did not enable exactly one owner");
                if(branch && terrain!=&state.terrainByCell && witness.enabled) return fail("malformed component did not fully fall back");
                usedPairs+=witness.pairs; ++componentChecks;
                if(!capture(state,after,error) || !(after==before)) return fail("whole component mutated full authority/save");
            }
        }
    }
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded) return fail("actual stock assets unavailable");
    for(int history:{0,1000,10000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            state.config=config; state.report=assets->report; state.rules=assets->rules; state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded; state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,48,ids[branch],error,false,true,false,240,240,true,true)) return false;
            state.config.defaultMapPath=config.defaultMapPath.empty()?assets->report.map.path:config.defaultMapPath;
            if(state.config.defaultMapPath.empty()) return fail("actual writer map identity missing");
            if(state.world.getTerrainCells().size()!=114960 || ids[branch].size()!=48) return fail("actual native240/48 cardinality");
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                if(first<=0 || first>std::numeric_limits<int>::max()-history) return fail("history ID capacity");
                for(int n=0;n<history;++n) {
                    auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                    state.world.accessObjects().push_back(std::move(dead));
                }
                auto metadata=std::move(state.terrainInfoByCell); state.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(state); state.terrainInfoByCell=std::move(metadata);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false; invalidateRuntimeTickOccupancyCache(state);
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]) return fail("actual roster identity/order");
        std::vector<int> infantry;
        for(int id:ids[0]) { const auto* actor=findObjectById(*states[0],id); if(actor && actor->kind==WorldObjectKind::Infantry) infantry.push_back(id); }
        if(infantry.size()!=24) return fail("actual infantry-only command census");
        bool moving=false;
        for(int tick=1;tick<=32;++tick) {
            Authority expected; Observations expectedQueries; std::map<int,RuntimeCellPoint> expectedDestinations;
            int expectedAccepted=-1,expectedRejected=-1;
            for(int branch=0;branch<2;++branch) {
                RuntimeComponentIndexedTopologyReferenceScope reference(branch==0);
                auto& state=*states[branch]; state.tick=tick; RuntimeSnapshotActiveStateScope active(state);
                if(tick==1 || tick==9 || tick==17) {
                    const auto& selected=tick==9?infantry:ids[branch];
                    Observations queries; Witness witness; std::map<int,RuntimeCellPoint> destinations;
                    {
                        RuntimePlannerPassabilityObserverScope observe(&Observations::append,&queries);
                        RuntimePlannerIndexedTopologyObserverScope observeOwner(&Witness::append,&witness);
                        // Stock CLEG may still be in its original chrono recovery.
                        // This existing helper permits ONLY IDs locked before the
                        // command; every other missing destination is a failure.
                        if(!issueRuntimeMassMovementOrder(state,selected,tick==9?RuntimeCellPoint{142,168}:RuntimeCellPoint{196,152},destinations,error,true)) return false;
                    }
                    if(!branch) {
                        expectedQueries=std::move(queries); expectedDestinations=destinations;
                        expectedAccepted=state.lastCommand.acceptedCount; expectedRejected=state.lastCommand.rejectedCount;
                    }
                    else if(queries.values!=expectedQueries.values || destinations!=expectedDestinations
                        || state.lastCommand.acceptedCount!=expectedAccepted || state.lastCommand.rejectedCount!=expectedRejected)
                        return fail("actual full ordered dynamic command queries/destinations tick="+std::to_string(tick));
                    if(!branch && (witness.enabled || witness.pairs)) return fail("actual reference borrowed geometry");
                    if(branch && (!witness.enabled || !witness.pairs)) return fail("actual native command did not use topology geometry");
                    usedPairs+=witness.pairs; ++commandChecks;
                }
                (void)advanceRuntimeSimulation(state); (void)completeRuntimeWorldRefresh(state);
                Authority current; if(!capture(state,current,error)) return false;
                if(!branch) expected=std::move(current);
                else if(!(current==expected)) return fail("actual per-tick complete writer/world/systems/path differs history="
                    +std::to_string(history)+" tick="+std::to_string(tick));
                if(branch) for(int id:ids[1]) { const auto* actor=findObjectById(state,id); moving|=actor && actor->renderMoveActive; }
            }
            ++tickChecks;
        }
        if(!moving) return fail("actual path/render trajectory never advanced");
    }
    if(edgeChecks!=745920 || geometryChecks!=2592 || componentChecks!=168 || boundaries<19 || commandChecks!=18 || tickChecks!=96 || !usedPairs
        || runtimeActivePlannerIndexedTopology || runtimeActivePlannerCellPassability)
        return fail("coverage/positive use or restored TLS incomplete");
    std::cout<<"Planner indexed topology geometry: exactOrderedTransitions="<<edgeChecks
        <<" exactGeometryPayloads="<<geometryChecks
        <<" completeOrderedComponents="<<componentChecks<<" boundaryCases="<<boundaries
        <<" actualNative240Commands="<<commandChecks<<" perTickFullSaveHashes="<<tickChecks
        <<" geometryPairs="<<usedPairs<<" H0/H1000/H10000=yes originalDynamicOrderAndGlobalTie=yes pass\n";
    error.clear(); return true;
}
