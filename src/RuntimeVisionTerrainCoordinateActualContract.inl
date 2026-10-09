// Include beside RuntimePlannerQueryLeaseContract.inl. Requires the original
// mass-movement fixture, authoritative writer, simulation and commit functions.
#include "RuntimeVisionTerrainCoordinateContract.inl"

bool runRuntimeVisionTerrainCoordinateActualContract(const RuntimeConfig& config,std::string& error) {
    using RuntimePlannerPassabilityContractDetail::Authority;
    using RuntimePlannerPassabilityContractDetail::capture;
    using RuntimeVisionTerrainCoordinateContractDetail::equalVision;
    const auto fail=[&](const std::string& why) {
        error="vision terrain-coordinate actual contract: "+why; return false;
    };
    if(!RuntimeVisionTerrainCoordinateContractDetail::primitive(error)) return false;
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("stock assets unavailable");
    const auto prepare=[&](RuntimeWindowState& state,std::vector<int>& ids) {
        state.config=config; state.report=assets->report; state.rules=assets->rules;
        state.art=assets->art; state.theater=assets->theater;
        state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
        state.theaterLoaded=assets->report.theaterLoaded;
        RuntimeSnapshotActiveStateScope active(state);
        if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true)) return false;
        state.config.defaultMapPath="VisionTerrainCoordinateContract.map";
        state.visionEnabled=true;
        (void)completeRuntimeWorldRefresh(state);
        (void)updateRuntimeVisionState(state);
        // Normal native exploration ensures both authored movement goals are
        // legal; commands still execute the complete production planner.
        if(!state.vision.hasObserver(state.economy.owner)) return fail("native observer setup");
        (void)state.vision.revealMap(state.economy.owner);
        return true;
    };
    struct Witness {
        const GameWorld* world=nullptr;
        std::size_t invocations=0,hashes=0,reuses=0,fallbacks=0;
        bool foreign=false;
        static void observe(void* raw,const GameWorld& world,bool,
            std::size_t hashes,std::size_t reuses,std::size_t fallbacks) {
            auto& out=*static_cast<Witness*>(raw);
            if(out.world!=&world) { out.foreign=true; return; }
            ++out.invocations; out.hashes+=hashes; out.reuses+=reuses; out.fallbacks+=fallbacks;
        }
    };
    std::size_t actualTicks=0,actualCommands=0,actualReuses=0,commitChecks=0;
    for(int history:{0,1000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        for(int branch=0;branch<2;++branch) {
            RuntimeVisionTerrainCoordinateReferenceScope reference(branch==0);
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            if(!prepare(state,ids[branch])) return false;
            RuntimeSnapshotActiveStateScope active(state);
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                for(int n=0;n<history;++n) {
                    auto dead=RuntimePlannerPassabilityContractDetail::object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                    state.world.accessObjects().push_back(std::move(dead));
                }
                auto terrainInfo=std::move(state.terrainInfoByCell);
                state.objectIndex=RuntimeObjectIndex{}; initializeRuntimeNavigation(state);
                state.terrainInfoByCell=std::move(terrainInfo);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
                invalidateRuntimeTickOccupancyCache(state);
                (void)completeRuntimeWorldRefresh(state);
            }
        }
        if(ids[0]!=ids[1] || ids[0].size()!=48) return fail("actual stock actor identity/order");
        bool moved=false;
        for(int n=1;n<=32;++n) {
            Authority expected;
            std::map<int,RuntimeCellPoint> expectedDestinations;
            for(int branch=0;branch<2;++branch) {
                RuntimeVisionTerrainCoordinateReferenceScope reference(branch==0);
                auto& state=*states[branch]; state.tick=n;
                RuntimeSnapshotActiveStateScope active(state);
                if(n==1 || n==17) {
                    std::map<int,RuntimeCellPoint> destinations;
                    if(!issueRuntimeMassMovementOrder(state,ids[branch],
                        n==1?RuntimeCellPoint{76,32}:RuntimeCellPoint{22,48},destinations,error)) return false;
                    if(branch==0) expectedDestinations=destinations;
                    else if(destinations!=expectedDestinations) return fail("actual ordered command destination map");
                    ++actualCommands;
                }
                Witness witness; witness.world=&state.world;
                {
                    RuntimeVisionTerrainCoordinateLeaseObserverScope observe(&Witness::observe,&witness);
                    (void)advanceRuntimeSimulation(state);
                }
                if(witness.foreign || witness.invocations!=1) return fail("simulation did not own exactly one world lease");
                if(branch==0 && (witness.hashes || witness.reuses || witness.fallbacks<2))
                    return fail("forced old simulation used a cached terrain signature");
                if(branch==1 && (witness.hashes!=1 || witness.reuses<1 || witness.fallbacks))
                    return fail("actual simulation never reused its original cold signature tick="+std::to_string(n));
                actualReuses+=witness.reuses;
                (void)completeRuntimeWorldRefresh(state);
                Authority current;
                if(!capture(state,current,error)) return false;
                if(branch==0) expected=current;
                else {
                    std::string detail;
                    if(!(current==expected) || !equalVision(state.vision,states[0]->vision,n,detail))
                        return fail("per-tick full writer/world/system/grid comparison history="+std::to_string(history)
                            +" tick="+std::to_string(n)+" "+detail);
                    for(int id:ids[branch]) {
                        const auto* actor=findObjectById(state,id);
                        moved|=actor && actor->renderMoveActive;
                    }
                }
            }
            ++actualTicks;
        }
        if(!moved) return fail("actual continuation never produced movement");
    }
    // Exercise the FOUR actual commit functions, including a reverse commit
    // used as rollback. Both inputs are independently prepared, complete
    // native states, not partially seeded parser/grinder scratch fixtures.
    // Distinct same-count layouts make stale pointer/count-only reuse visible.
    for(int boundary=0;boundary<4;++boundary) {
        std::array<std::array<std::unique_ptr<RuntimeWindowState>,2>,2> states;
        for(int branch=0;branch<2;++branch) for(int role=0;role<2;++role) {
            RuntimeVisionTerrainCoordinateReferenceScope reference(branch==0);
            states[branch][role]=std::make_unique<RuntimeWindowState>();
            auto& state=*states[branch][role]; std::vector<int> ids;
            if(!prepare(state,ids)) return false;
            if(role) {
                invalidateRuntimeVisionTerrainCoordinateLeases(state.world);
                state.world.accessTerrainCells().front().x=511;
                initializeRuntimeNavigation(state);
                (void)completeRuntimeWorldRefresh(state);
                (void)updateRuntimeVisionState(state);
            }
        }
        std::array<std::array<Authority,2>,2> expected;
        std::array<std::array<RuntimeVisionSystem,2>,2> expectedVision;
        for(int branch=0;branch<2;++branch) {
            RuntimeVisionTerrainCoordinateReferenceScope reference(branch==0);
            auto& live=*states[branch][0]; auto& peer=*states[branch][1];
            RuntimeSnapshotActiveStateScope active(live);
            RuntimeVisionTerrainCoordinateLeaseScope outer(live.world);
            (void)updateRuntimeVisionState(live); (void)updateRuntimeVisionState(live);
            RuntimeVisionTerrainCoordinateLeaseScope inner(live.world);
            (void)updateRuntimeVisionState(live); (void)updateRuntimeVisionState(live);
            RuntimeVisionTerrainCoordinateLeaseScope scratch(peer.world);
            (void)updateRuntimeVisionState(peer); (void)updateRuntimeVisionState(peer);
            if(branch==1 && (outer.reuses()<1 || inner.reuses()<1 || scratch.reuses()<1))
                return fail("commit fixture did not warm all ancestors/sides");
            const auto commit=[&] {
                switch(boundary) {
                case 0: commitRuntimeLoadedState(live,peer); break;
                case 1: {
                    RuntimeMultiplayerSnapshotStagedState staged;
                    staged.descriptor.identity.kind=RuntimeMultiplayerSnapshotKind::Load;
                    staged.scratch=std::move(states[branch][1]);
                    commitRuntimeMultiplayerSnapshotAuthority(live,std::move(staged));
                    states[branch][1]=std::move(staged.scratch);
                    break;
                }
                case 2: commitRuntimeOwnerTransitionScratch(live,peer); break;
                case 3: commitRuntimeGrindingTransactionScratch(live,peer); break;
                }
            };
            for(int direction=0;direction<2;++direction) {
                commit();
                if(outer.enabled() || inner.enabled() || scratch.enabled())
                    return fail("actual commit failed to invalidate all ancestors/sides boundary="+std::to_string(boundary));
                for(int role=0;role<2;++role) {
                    auto& state=*states[branch][role]; RuntimeSnapshotActiveStateScope roleActive(state);
                    (void)completeRuntimeWorldRefresh(state); (void)updateRuntimeVisionState(state);
                    Authority current;
                    if(!capture(state,current,error)) return false;
                    // Keep full authority and complete vision checkpoints for
                    // BOTH directions and BOTH worlds, not only final rollback.
                    if(branch==0) {
                        expected[direction][role]=current;
                        expectedVision[direction][role]=state.vision;
                    }
                    if(branch==1) {
                        std::string detail;
                        if(!(current==expected[direction][role])
                            || !equalVision(state.vision,expectedVision[direction][role],state.tick,detail))
                            return fail("actual commit/rollback full writer/grid boundary="
                                +std::to_string(boundary)+" direction="+std::to_string(direction)
                                +" role="+std::to_string(role)+" "+detail);
                    }
                }
                ++commitChecks;
            }
            if(branch==1 && (outer.reuses()!=1 || inner.reuses()!=1 || scratch.reuses()!=1))
                return fail("post-commit cached signature resurrected");
        }
    }
    if(!actualReuses || actualTicks!=64 || actualCommands!=8 || commitChecks!=16)
        return fail("actual route/commit coverage counters incomplete");
    std::cout<<"Vision terrain-coordinate lease: exact complete grids/stats/revisions/epochs; "
        <<actualTicks<<" actual per-tick full-save/hash comparisons, "<<actualCommands
        <<" native commands, "<<commitChecks<<" actual commit/rollback routes, "<<actualReuses
        <<" repeated original terrain hashes reused.\n";
    error.clear(); return true;
}
