// Include after RuntimeVisionTerrainCoordinateActualContract.inl and the
// original command/authority helpers. Full saves are compared, not load replay.
#include <array>
#include <limits>
#include <memory>
#include <optional>
namespace RuntimeVisionTerrainCoordinateBatchContractDetail {
using RuntimeVisionTerrainCoordinateContractDetail::originalHash;
using RuntimeVisionTerrainCoordinateContractDetail::equalVision;
inline bool primitive(std::string& error) {
    const auto fail=[&](const std::string& why) {
        error="vision terrain-coordinate batch primitive: "+why; return false;
    };
    RuntimeVisionTerrainCoordinateReferenceScope oldCoordinate(false);
    RuntimeVisionTerrainCoordinateBatchReferenceScope optimized(false);
    GameWorld world;
    for(int y=0;y<24;++y) for(int x=0;x<24;++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance scout;
    scout.instanceId=1; scout.kind=WorldObjectKind::Infantry; scout.type="SCOUT";
    scout.owner="Alpha"; scout.x=scout.y=5; scout.alive=true; scout.health=100;
    world.accessObjects().push_back(scout);
    GameWorld expectedWorld=world;
    IniFile ini; RulesDatabase rules;
    if(!ini.loadFromString("[InfantryTypes]\n0=SCOUT\n[SCOUT]\nSight=3\nStrength=100\n")
        || !rules.loadFromIni(ini)) return fail("rules setup");
    RuntimeVisionSystem actual,expected;
    const std::vector<RuntimeVisionObserverDefinition> observers{{"Alpha",{"Alpha"}}};
    std::size_t borrowed=0;
    {
        RuntimeVisionTerrainCoordinateBatchScope batch(world);
        for(int tick=1;tick<=16;++tick) {
            const auto before=world.deterministicSimulationStateHash();
            {
                RuntimeVisionTerrainCoordinateInvocationScope invocation(world);
                if(!invocation.borrowed() || invocation.ownsLease()) return fail("16-tick invocation did not borrow");
                ++borrowed;
                (void)actual.update(world,&rules,observers,tick);
                (void)actual.update(world,&rules,observers,tick);
            }
            {
                RuntimeVisionTerrainCoordinateInvocationScope invocation(expectedWorld);
                if(invocation.borrowed() || !invocation.ownsLease()) return fail("foreign world borrowed");
                (void)expected.update(expectedWorld,&rules,observers,tick);
                (void)expected.update(expectedWorld,&rules,observers,tick);
            }
            std::string detail;
            if(!equalVision(actual,expected,tick,detail)
                || before!=world.deterministicSimulationStateHash()
                || before!=expectedWorld.deterministicSimulationStateHash())
                return fail("16-tick complete grids/stats/epochs/hash "+detail);
        }
        if(batch.originalHashes()!=1 || batch.reuses()!=31 || batch.fallbacks()!=0)
            return fail("16-tick cold=1/hot=31 exact witness");
    }
    std::size_t cold=0;
    const auto query=[&](const GameWorld& value) {
        RuntimeVisionTerrainCoordinateInvocationScope invocation(value);
        return runtimeVisionTerrainCoordinateSignature(value,[&] {
            ++cold; return originalHash(value);
        });
    };
    {
        RuntimeVisionTerrainCoordinateBatchScope outer(world);
        if(query(world)!=originalHash(world) || query(world)!=originalHash(world)
            || cold!=1 || outer.reuses()!=1) return fail("outer cold/hot");
        {
            RuntimeVisionTerrainCoordinateBatchReferenceScope reference(true);
            const auto before=outer.reuses();
            if(outer.mayBorrow(world) || query(world)!=originalHash(world)
                || query(world)!=originalHash(world) || outer.reuses()!=before || cold!=3)
                return fail("dynamic batch reference failed to restore per-invocation hashing");
        }
        {
            RuntimeVisionTerrainCoordinateBatchScope disabled(world,false);
            const auto before=outer.reuses();
            if(disabled.mayBorrow(world) || query(world)!=originalHash(world)
                || query(world)!=originalHash(world) || outer.reuses()!=before || cold!=5)
                return fail("disabled nested batch did not mask ancestor");
        }
        {
            RuntimeVisionTerrainCoordinateBatchScope inner(world);
            if(!inner.mayBorrow(world) || query(world)!=originalHash(world)
                || query(world)!=originalHash(world) || inner.originalHashes()!=1 || inner.reuses()!=1)
                return fail("enabled nested batch cold/hot");
        }
        if(!outer.mayBorrow(world) || query(world)!=originalHash(world) || cold!=6)
            return fail("unchanged outer restoration");
        {
            RuntimeVisionTerrainCoordinateBatchScope foreign(expectedWorld);
            const auto before=outer.reuses();
            if(foreign.mayBorrow(world) || query(world)!=originalHash(world)
                || outer.reuses()!=before || cold!=7) return fail("foreign batch mask");
            // Message reentry invalidates all SAME-world ancestors even under
            // an active foreign batch, before the actual in-place edit.
            runtimeInvalidateVisionTerrainBatchForMessage(world);
            if(outer.enabled() || !foreign.enabled()) return fail("message invalidation owner/ancestor");
            world.accessTerrainCells().front().x=511;
            if(query(world)!=originalHash(world) || cold!=8) return fail("message edit exact old hash");
        }
        if(outer.mayBorrow(world) || query(world)!=originalHash(world) || cold!=9)
            return fail("invalidated ancestor resurrected");
    }
    {
        RuntimeVisionTerrainCoordinateBatchReferenceScope reference(true);
        RuntimeVisionTerrainCoordinateBatchScope batch(world);
        RuntimeVisionTerrainCoordinateInvocationScope invocation(world);
        if(batch.enabled() || invocation.borrowed() || !invocation.ownsLease())
            return fail("reference-created batch");
    }
    {
        RuntimeVisionTerrainCoordinateReferenceScope reference(true);
        RuntimeVisionTerrainCoordinateBatchScope batch(world);
        RuntimeVisionTerrainCoordinateInvocationScope invocation(world);
        if(batch.enabled() || invocation.borrowed() || !invocation.ownsLease())
            return fail("original scalar reference priority");
    }
    {
        RuntimeVisionTerrainCoordinateBatchScope batch(world);
        if(query(world)!=originalHash(world)) return fail("shape warm");
        world.accessTerrainCells().push_back(world.getTerrainCells()[1]);
        if(query(world)!=originalHash(world) || batch.enabled()) return fail("missing-hook shape guard");
        world.accessTerrainCells().pop_back();
        if(batch.mayBorrow(world) || query(world)!=originalHash(world))
            return fail("restored shape resurrected batch");
    }
    // Exact packed-grid effects after the real message hook: no stale old
    // signature survives in-place coordinates or same-count order changes.
    expectedWorld=world;
    {
        RuntimeVisionTerrainCoordinateBatchScope batch(world);
        for(int tick=17;tick<=19;++tick) {
            if(tick==18) {
                runtimeInvalidateVisionTerrainBatchForMessage(world);
                if(batch.enabled()) return fail("message warm batch stayed enabled");
                world.accessTerrainCells().front().x=-1;
                expectedWorld.accessTerrainCells().front().x=-1;
            }
            if(tick==19) {
                std::reverse(world.accessTerrainCells().begin(),world.accessTerrainCells().end());
                std::reverse(expectedWorld.accessTerrainCells().begin(),expectedWorld.accessTerrainCells().end());
            }
            {
                RuntimeVisionTerrainCoordinateInvocationScope invocation(world);
                if(tick>17 && (invocation.borrowed() || !invocation.ownsLease()))
                    return fail("message-invalid invocation regained borrowed storage");
                (void)actual.update(world,&rules,observers,tick);
            }
            {
                RuntimeVisionTerrainCoordinateInvocationScope invocation(expectedWorld);
                (void)expected.update(expectedWorld,&rules,observers,tick);
            }
            std::string detail;
            if(!equalVision(actual,expected,tick,detail)
                || world.deterministicSimulationStateHash()!=expectedWorld.deterministicSimulationStateHash())
                return fail("message coordinate/order complete grids/hash "+detail);
        }
    }
    if(borrowed!=16) return fail("positive borrowed coverage");
    error.clear(); return true;
}
} // namespace RuntimeVisionTerrainCoordinateBatchContractDetail

bool runRuntimeVisionTerrainCoordinateBatchActualContract(const RuntimeConfig& config,std::string& error) {
    using RuntimePlannerPassabilityContractDetail::Authority;
    using RuntimePlannerPassabilityContractDetail::capture;
    using RuntimeVisionTerrainCoordinateContractDetail::equalVision;
    const auto fail=[&](const std::string& why) {
        error="vision terrain-coordinate batch actual: "+why; return false;
    };
    RuntimeVisionTerrainCoordinateReferenceScope oldCoordinate(false);
    RuntimeVisionTerrainCoordinateBatchReferenceScope optimized(false);
    if(!RuntimeVisionTerrainCoordinateBatchContractDetail::primitive(error)) return false;
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("actual stock assets unavailable");
    const auto prepare=[&](RuntimeWindowState& state,std::vector<int>& ids) {
        state.config=config; state.report=assets->report; state.rules=assets->rules;
        state.art=assets->art; state.theater=assets->theater;
        state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
        state.theaterLoaded=assets->report.theaterLoaded;
        RuntimeSnapshotActiveStateScope active(state);
        if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true)) return false;
        // Restore real source identity for the complete writer. Contents are
        // the explicitly generated fixture, not an assertion of load replay.
        state.config.defaultMapPath=config.defaultMapPath.empty()
            ? assets->report.map.path : config.defaultMapPath;
        if(state.config.defaultMapPath.empty()) return fail("actual writer map identity unavailable");
        state.visionEnabled=true;
        (void)completeRuntimeWorldRefresh(state); (void)updateRuntimeVisionState(state);
        if(!state.vision.hasObserver(state.economy.owner)) return fail("native observer missing");
        (void)state.vision.revealMap(state.economy.owner);
        return true;
    };
    std::size_t comparisons=0,commands=0,batches=0,hashes=0,reuses=0,commitChecks=0;
    for(int history:{0,10000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        std::array<std::vector<int>,2> ids;
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            if(!prepare(state,ids[branch])) return false;
            RuntimeSnapshotActiveStateScope active(state);
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                if(first<=0 || first>std::numeric_limits<int>::max()-history)
                    return fail("history object ID capacity");
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
        if(ids[0]!=ids[1] || ids[0].size()!=48) return fail("native actor identity/order");
        bool movement=false;
        for(int start:{1,17}) {
            RuntimeVisionTerrainCoordinateBatchScope batch(states[1]->world);
            for(int n=start;n<start+16;++n) {
                Authority expected;
                std::map<int,RuntimeCellPoint> expectedDestinations;
                for(int branch=0;branch<2;++branch) {
                    RuntimeVisionTerrainCoordinateBatchReferenceScope reference(branch==0);
                    auto& state=*states[branch]; state.tick=n;
                    RuntimeSnapshotActiveStateScope active(state);
                    // The reference uses the original per-invocation lease,
                    // masking the optimized world's longer-lived batch.
                    std::optional<RuntimeVisionTerrainCoordinateBatchScope> referenceBatch;
                    if(branch==0) referenceBatch.emplace(state.world);
                    if(n==1 || n==17) {
                        std::map<int,RuntimeCellPoint> destinations;
                        if(!issueRuntimeMassMovementOrder(state,ids[branch],
                            n==1?RuntimeCellPoint{76,32}:RuntimeCellPoint{22,48},destinations,error)) return false;
                        if(branch==0) expectedDestinations=destinations;
                        else if(destinations!=expectedDestinations) return fail("ordered native command destinations");
                        ++commands;
                    }
                    if(branch==1 && !batch.mayBorrow(state.world)) return fail("actual invocation cannot borrow");
                    (void)advanceRuntimeSimulation(state);
                    (void)completeRuntimeWorldRefresh(state);
                    Authority current;
                    if(!capture(state,current,error)) return false;
                    if(branch==0) expected=std::move(current);
                    else {
                        std::string detail;
                        if(!(current==expected) || !equalVision(state.vision,states[0]->vision,n,detail))
                            return fail("per-tick full-save/world/systems/vision history="+std::to_string(history)
                                +" tick="+std::to_string(n)+" "+detail);
                        for(int id:ids[1]) {
                            const auto* actor=findObjectById(state,id);
                            movement|=actor && actor->renderMoveActive;
                        }
                        ++comparisons;
                    }
                }
            }
            if(!batch.enabled() || batch.originalHashes()!=1 || batch.reuses()<16 || batch.fallbacks()!=0)
                return fail("actual 16-tick batch did not reuse one original hash history="+std::to_string(history));
            hashes+=batch.originalHashes(); reuses+=batch.reuses(); ++batches;
        }
        if(!movement) return fail("actual original move trajectory never advanced");
    }
    // Four real world commits and their rollback direction. Nested same-world
    // batches plus a foreign scratch warm both sides before pre-swap hooks.
    for(int boundary=0;boundary<4;++boundary) {
        std::array<std::array<std::unique_ptr<RuntimeWindowState>,2>,2> states;
        for(int branch=0;branch<2;++branch) for(int role=0;role<2;++role) {
            states[branch][role]=std::make_unique<RuntimeWindowState>();
            auto& state=*states[branch][role]; std::vector<int> ids;
            if(!prepare(state,ids)) return false;
            if(role) {
                invalidateRuntimeVisionTerrainCoordinateLeases(state.world);
                state.world.accessTerrainCells().front().x=511;
                initializeRuntimeNavigation(state);
                (void)completeRuntimeWorldRefresh(state); (void)updateRuntimeVisionState(state);
            }
        }
        std::array<std::array<Authority,2>,2> expected;
        std::array<std::array<RuntimeVisionSystem,2>,2> expectedVision;
        for(int branch=0;branch<2;++branch) {
            RuntimeVisionTerrainCoordinateBatchReferenceScope reference(branch==0);
            auto& live=*states[branch][0]; auto& peer=*states[branch][1];
            RuntimeSnapshotActiveStateScope active(live);
            RuntimeVisionTerrainCoordinateBatchScope outer(live.world);
            (void)updateRuntimeVisionState(live); (void)updateRuntimeVisionState(live);
            RuntimeVisionTerrainCoordinateBatchScope inner(live.world);
            (void)updateRuntimeVisionState(live); (void)updateRuntimeVisionState(live);
            RuntimeVisionTerrainCoordinateBatchScope scratch(peer.world);
            (void)updateRuntimeVisionState(peer); (void)updateRuntimeVisionState(peer);
            if(branch==1 && (outer.reuses()!=1 || inner.reuses()!=1 || scratch.reuses()!=1))
                return fail("actual commit batches were not warmed");
            const auto commit=[&] {
                switch(boundary) {
                case 0: commitRuntimeLoadedState(live,peer); break;
                case 1: {
                    RuntimeMultiplayerSnapshotStagedState staged;
                    staged.descriptor.identity.kind=RuntimeMultiplayerSnapshotKind::Load;
                    staged.scratch=std::move(states[branch][1]);
                    commitRuntimeMultiplayerSnapshotAuthority(live,std::move(staged));
                    states[branch][1]=std::move(staged.scratch); break;
                }
                case 2: commitRuntimeOwnerTransitionScratch(live,peer); break;
                case 3: commitRuntimeGrindingTransactionScratch(live,peer); break;
                }
            };
            for(int direction=0;direction<2;++direction) {
                commit();
                if(outer.enabled() || inner.enabled() || scratch.enabled())
                    return fail("actual pre-swap batch ancestor invalidation boundary="+std::to_string(boundary));
                for(int role=0;role<2;++role) {
                    auto& state=*states[branch][role]; RuntimeSnapshotActiveStateScope roleActive(state);
                    (void)completeRuntimeWorldRefresh(state); (void)updateRuntimeVisionState(state);
                    Authority current;
                    if(!capture(state,current,error)) return false;
                    if(branch==0) {
                        expected[direction][role]=current; expectedVision[direction][role]=state.vision;
                    } else {
                        std::string detail;
                        if(!(current==expected[direction][role])
                            || !equalVision(state.vision,expectedVision[direction][role],state.tick,detail))
                            return fail("actual commit/rollback writer/grids boundary="+std::to_string(boundary)
                                +" direction="+std::to_string(direction)+" role="+std::to_string(role)+" "+detail);
                    }
                }
                ++commitChecks;
            }
            if(branch==1 && (outer.reuses()!=1 || inner.reuses()!=1 || scratch.reuses()!=1))
                return fail("post-commit batch signature resurrected");
        }
    }
    if(comparisons!=64 || commands!=8 || batches!=4 || hashes!=4 || reuses<64 || commitChecks!=16)
        return fail("coverage or genuine reuse counts incomplete");
    std::cout<<"Vision terrain-coordinate batch: 16 primitive borrowed invocations; "
        <<comparisons<<" actual per-tick full-save/hash/grid comparisons, "<<commands
        <<" native commands, "<<batches<<" bounded batches, "<<hashes<<" original hashes, "
        <<reuses<<" reuses, "<<commitChecks<<" actual commit/rollback routes; H0/H10000 exact.\n";
    error.clear(); return true;
}
