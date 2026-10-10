// Include after RuntimePlannerCellPassabilityContract.inl. Reuse its actual
// authored rules/world fixture and full authoritative writer, not a toy graph.
namespace {
struct RuntimePlannerQueryLeaseContractAccess {
    static std::unique_ptr<RuntimePlannerCellPassabilityReadOnlyLease> make(
        RuntimePlannerCellPassabilityScope& scope, const RuntimeWindowState& state,
        const WorldObjectInstance& mover, const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy) {
        return std::unique_ptr<RuntimePlannerCellPassabilityReadOnlyLease>(
            new RuntimePlannerCellPassabilityReadOnlyLease(&scope,state,mover,terrain,occupancy,true));
    }
};
} // namespace

bool runRuntimePlannerQueryLeaseContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    RuntimeComponentPassabilityReferenceScope memo(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrainMemo(false);
    RuntimeComponentQueryLeaseReferenceScope leaseEnabled(false);
    RuntimeComponentEmptyCensusReferenceScope censusEnabled(false);
    RuntimeComponentCrushAliasReferenceScope crushAliasEnabled(false);
    const auto fail = [&](const std::string& why) { error="planner query lease/census: "+why; return false; };
    const auto makeFixture = [&](RuntimeWindowState& state,const RuntimeConfig& fixtureConfig,
        int history,int phase,std::string& fixtureError) {
        if (!RuntimePlannerPassabilityContractDetail::make(state,fixtureConfig,history,phase,fixtureError)) return false;
        // Navigation initialization rebuilds this map from art. The synthetic
        // fixture has no art foundations, so author its intended footprints
        // AFTER initialization and verify the values the production API sees.
        // The existing C20 fixture and assertions remain unchanged.
        state.structureFootprints["SOLID"]={2,2,true};
        state.structureFootprints["LANE"]={3,2,true};
        state.structureFootprints["GATE"]={1,1,true};
        state.structureFootprints["REPAIR"]={2,2,true};
        state.structureFootprints["WALL"]={1,1,true};
        state.structureFootprints["YARD"]={2,2,true};
        invalidateRuntimeTickOccupancyCache(state);
        for (const auto& [type,expected] : state.structureFootprints) {
            auto probe=object(999,WorldObjectKind::Structure,type.c_str(),0,0);
            const auto actual=runtimeStructureFootprintForObject(state,probe);
            if (!actual.valid || actual.width!=expected.width || actual.height!=expected.height) {
                fixtureError="planner query lease/census: authored footprint readback type="+type;
                return false;
            }
        }
        return true;
    };
    std::size_t boundaries=0,edges=0,components=0,censusCells=0,proofs=0;
    std::size_t aliasCases=0,aliasPrimitiveScalarSaved=0;
    // Genuine cold/hot work reduction, while retaining the original observer
    // keys and both optional-mode-presence keys. Never warm the owner via the
    // oracle; its true scalar route bypasses the memo explicitly.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,0,0,error)) return false;
        const auto& mover=*findObjectById(state,1);
        const auto occupied=buildRuntimeOccupancyLookup(state);
        Authority before,after;
        if (!capture(state,before,error)) return false;
        RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
        auto lease=RuntimePlannerQueryLeaseContractAccess::make(scope,state,mover,state.terrainByCell,occupied);
        RuntimePlannerEmptyCellCensusScope census(state);
        if(!census.enabled() || state.foundationCollisionTick==state.tick)
            return fail("alias lazy original-foundation fixture must begin uncached");
        // The first real scalar initializes the original lazy table. The
        // optimization must not do so before that scalar would reach it.
        if(!lease->query(state,state.terrainByCell,occupied,{0,0},mover,-1,nullptr,true,PathMovementMode::Ground)
            || lease->crushAliases()!=0 || lease->aliasFoundationBorrows()!=0
            || state.foundationCollisionTick!=state.tick)
            return fail("alias eagerly initialized original foundation or changed initial cold scalar");
        Observations scalarCalls,aliasCalls;
        {
            RuntimeComponentPassabilityReferenceScope scalar(true);
            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&scalarCalls);
            for(bool crush : {true,false,true,false})
                if(!runtimeCellIsPassable(state,state.terrainByCell,occupied,{0,2},mover,-1,nullptr,crush,PathMovementMode::Ground))
                    return fail("empty alias scalar fixture must be actually traversable");
        }
        const auto cold=lease->scalarQueries();
        {
            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&aliasCalls);
            for(bool crush : {true,false,true,false})
                if(!lease->query(state,state.terrainByCell,occupied,{0,2},mover,-1,nullptr,crush,PathMovementMode::Ground))
                    return fail("empty alias changed original scalar permission");
        }
        if(aliasCalls.values!=scalarCalls.values || lease->scalarQueries()!=cold+1
            || lease->crushAliases()!=2 || lease->aliasQualificationChecks()!=1
            || lease->aliasFoundationBorrows()!=1 || lease->aliasEligibilityBytes()!=65536
            || scope.entries()!=2)
            return fail("true/false alias must save one real cold scalar and preserve original ordered keys");
        ++aliasCases; ++aliasPrimitiveScalarSaved;
        const auto rejectAlias=[&](const char* reason) {
            const auto count=lease->crushAliases();
            const bool expected=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,2},mover,
                -1,nullptr,true,PathMovementMode::Ground);
            const bool actual=lease->query(state,state.terrainByCell,occupied,{0,2},mover,
                -1,nullptr,true,PathMovementMode::Ground);
            ++aliasCases;
            if(actual!=expected || lease->crushAliases()!=count) return fail(reason);
            return true;
        };
        {
            RuntimePlannerEmptyCellCensusScope disabled(state,false);
            if(!rejectAlias("hot alias qualification escaped disabled nested census")) return false;
        }
        {
            auto foreign=std::make_unique<RuntimeWindowState>();
            RuntimePlannerEmptyCellCensusScope foreignCensus(*foreign);
            if(!rejectAlias("hot alias qualification escaped foreign nested census")) return false;
        }
        {
            RuntimeComponentEmptyCensusReferenceScope reference(true);
            if(!rejectAlias("hot alias qualification ignored current census reference")) return false;
        }
        {
            RuntimeComponentCrushAliasReferenceScope reference(true);
            if(!rejectAlias("independent alias reference failed to restore C21b key")) return false;
        }
        {
            RuntimePlannerCellPassabilityScope disabled(state,mover,state.terrainByCell,occupied,false);
            if(!rejectAlias("hot alias qualification escaped disabled memo owner")) return false;
        }
        const auto count=lease->crushAliases();
        if(!lease->query(state,state.terrainByCell,occupied,{0,2},mover,-1,nullptr,true,PathMovementMode::Ground)
            || lease->crushAliases()!=count+1)
            return fail("nested/reference scopes did not restore the outer empty alias");
        const std::unordered_set<int> emptySoft;
        const auto compareOriginal=[&](const auto& owner,const auto& actor,const auto& terrain,const auto& traffic,
            RuntimeCellPoint point,int allowed,const std::unordered_set<int>* soft) {
            const auto prior=lease->crushAliases();
            const bool expected=runtimeCellIsPassableUncached(owner,terrain,traffic,point,actor,allowed,soft,true);
            const bool actual=lease->query(owner,terrain,traffic,point,actor,allowed,soft,true);
            ++aliasCases;
            return expected==actual && lease->crushAliases()==prior;
        };
        auto foreignMover=mover; foreignMover.type="HOVER";
        auto foreignTerrain=state.terrainByCell;
        auto foreignOccupied=occupied;
        if(!compareOriginal(state,mover,state.terrainByCell,occupied,{0,2},-1,&emptySoft)
            || !compareOriginal(state,mover,state.terrainByCell,occupied,{0,2},-2,nullptr)
            || !compareOriginal(state,mover,state.terrainByCell,occupied,{0,2},31,nullptr)
            || !compareOriginal(state,foreignMover,state.terrainByCell,occupied,{0,2},-1,nullptr)
            || !compareOriginal(state,mover,foreignTerrain,occupied,{0,2},-1,nullptr)
            || !compareOriginal(state,mover,state.terrainByCell,foreignOccupied,{0,2},-1,nullptr)
            || !compareOriginal(state,mover,state.terrainByCell,occupied,{1000,0},-1,nullptr)
            || !compareOriginal(state,mover,state.terrainByCell,occupied,{-1,0},-1,nullptr)
            || !compareOriginal(state,mover,state.terrainByCell,occupied,{std::numeric_limits<int>::max(),0},-1,nullptr))
            return fail("alias allowed/soft/foreign/raw-legacy/negative/extreme boundary");
        ++state.tick;
        if(!rejectAlias("hot alias qualification crossed tick boundary")) return false;
        --state.tick;
        if(!capture(state,after,error) || !(before==after)) return fail("alias-only queries changed full authority/save");
        census.invalidate();
        if(!rejectAlias("explicit census invalidation retained hot qualification")) return false;
        // Explicitly close both owned sources BEFORE same-address edits.
        lease->invalidate();
        const auto aliases=lease->crushAliases();
        state.terrainInfoByCell.at(runtimeCellKey(0,2)).passable=false;
        const bool blocked=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,2},mover,
            -1,nullptr,true,PathMovementMode::Ground);
        if(blocked || lease->query(state,state.terrainByCell,occupied,{0,2},mover,
                -1,nullptr,true,PathMovementMode::Ground)!=blocked || lease->crushAliases()!=aliases)
            return fail("invalidated alias/owner returned a hot permission after same-address metadata mutation");
        ++aliasCases;
    }
    // All three conditions are necessary, and these exact counterexamples
    // show why neither fresh live coordinates nor a synthetic occupancy map
    // can replace the original predicate/table semantics.
    for(int variant=0;variant<3;++variant) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if(!makeFixture(state,config,0,0,error)) return false;
        const auto& mover=*findObjectById(state,1);
        auto occupied=buildRuntimeOccupancyLookup(state);
        RuntimeCellPoint point=variant==0?RuntimeCellPoint{8,7}:RuntimeCellPoint{0,0};
        const auto& foundations=runtimeSolidFoundationsByCell(state);
        if(variant==1) occupied[runtimeCellKey(0,0)]=35; // forged representative at an actually empty coordinate
        if(variant==2) {
            std::size_t wall=state.world.getObjects().size();
            for(std::size_t n=0;n<state.world.getObjects().size();++n)
                if(state.world.getObjects()[n].instanceId==34) { wall=n; break; }
            if(wall==state.world.getObjects().size()) return fail("stale original foundation fixture has no actual WALL34");
            // Keep the original cache's exact tick/data/count guard valid but
            // retain a stale WALL entry at empty (0,0). The old scalar DOES see
            // this entry and its true/false crush permissions differ.
            state.foundationCollisionByCell[runtimeCellKey(0,0)]={wall};
            if(&runtimeSolidFoundationsByCell(state)!=&foundations
                || !runtimeSolidFoundationsByCell(state).contains(runtimeCellKey(0,0)))
                return fail("stale original foundation did not survive its real cache-valid guard");
        }
        Authority before,after;
        if(!capture(state,before,error)) return false;
        RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
        auto lease=RuntimePlannerQueryLeaseContractAccess::make(scope,state,mover,state.terrainByCell,occupied);
        RuntimePlannerEmptyCellCensusScope census(state);
        if(!census.enabled() || census.provesEmpty(state,point)!=(variant!=0))
            return fail("occupied/forged/stale counterexample census precondition");
        const bool strict=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,point,mover,-1,nullptr,false);
        const bool crushing=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,point,mover,-1,nullptr,true);
        if(strict || (variant!=1 && !crushing) || (variant==1 && crushing))
            return fail("occupied/forged/stale original permissions do not establish the intended crush counterexample variant="
                +std::to_string(variant)+" strict="+std::to_string(strict)+" crush="+std::to_string(crushing));
        for(bool crush : {true,false,true,false})
            if(lease->query(state,state.terrainByCell,occupied,point,mover,-1,nullptr,crush)!=(crush?crushing:strict))
                return fail("alias lost original occupied/stale true-versus-false crush result variant="+std::to_string(variant));
        if(lease->crushAliases()!=0 || scope.entries()!=2 || lease->scalarQueries()!=2
            || lease->aliasEligibilityBytes()>65536 || lease->aliasFoundationBorrows()>1)
            return fail("occupied/forged/stale counterexample must retain two original keys/scalars");
        if(!capture(state,after,error) || !(before==after)) return fail("counterexample queries changed full authority/save");
        ++aliasCases;
    }
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,0,0,error)) return false;
        const auto& mover=*findObjectById(state,1);
        auto occupied=buildRuntimeOccupancyLookup(state);
        Authority before,after;
        if (!capture(state,before,error)) return false;
        RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
        auto lease=RuntimePlannerQueryLeaseContractAccess::make(scope,state,mover,state.terrainByCell,occupied);
        const auto verify=[&](const RuntimeWindowState& owner,const WorldObjectInstance& actor,
            const auto& cells,const auto& traffic,RuntimeCellPoint point,int allowed=-1,
            const std::unordered_set<int>* soft=nullptr,bool crush=false,
            std::optional<PathMovementMode> mode=std::nullopt) {
            ++boundaries;
            const auto scalar=runtimeCellIsPassableUncached(owner,cells,traffic,point,actor,allowed,soft,crush,mode);
            return lease->query(owner,cells,traffic,point,actor,allowed,soft,crush,mode)==scalar;
        };
        if (!verify(state,mover,state.terrainByCell,occupied,{0,0})
            || !verify(state,mover,state.terrainByCell,occupied,{0,0})
            || lease->fullValidations()!=1 || lease->queries()!=2 || scope.hits()!=1)
            return fail("one full constructor validation and actual cold/hot owned memo queries");
        const auto ownerHits=scope.hits(); const auto leased=lease->queries();
        {
            RuntimePlannerCellPassabilityScope disabled(state,mover,state.terrainByCell,occupied,false);
            if (!verify(state,mover,state.terrainByCell,occupied,{0,0})
                || disabled.entries()!=0 || scope.hits()!=ownerHits || lease->queries()!=leased)
                return fail("disabled nested scope did not mask the owned outer lease");
        }
        {
            RuntimePlannerCellPassabilityScope inner(state,mover,state.terrainByCell,occupied);
            if (!verify(state,mover,state.terrainByCell,occupied,{0,0})
                || !verify(state,mover,state.terrainByCell,occupied,{0,0})
                || inner.hits()!=1 || scope.hits()!=ownerHits || lease->queries()!=leased)
                return fail("enabled nested scope borrowed the captured outer lease");
        }
        if (!verify(state,mover,state.terrainByCell,occupied,{0,0}) || scope.hits()!=ownerHits+1)
            return fail("outer owned lease was not restored after nested scopes");
        auto foreignMover=mover; foreignMover.type="HOVER";
        if (runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{10,4},mover)
            || !runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{10,4},foreignMover)
            || !verify(state,foreignMover,state.terrainByCell,occupied,{10,4}))
            return fail("foreign mover must use its genuinely different water classification");
        auto foreignTerrain=state.terrainByCell; foreignTerrain[runtimeCellKey(0,0)]=nullptr;
        auto foreignOccupied=occupied; foreignOccupied[runtimeCellKey(0,0)]=30;
        if (!verify(state,mover,foreignTerrain,occupied,{0,0})
            || !verify(state,mover,state.terrainByCell,foreignOccupied,{0,0}))
            return fail("foreign terrain/occupancy containers borrowed the hot owned result");
        auto foreign=std::make_unique<RuntimeWindowState>();
        {
            RuntimeSnapshotActiveStateScope foreignActive(*foreign);
            if (!makeFixture(*foreign,config,0,0,error)) return false;
            if (!verify(*foreign,*findObjectById(*foreign,1),foreign->terrainByCell,
                    buildRuntimeOccupancyLookup(*foreign),{0,0})) return fail("foreign state borrowed lease");
        }
        const std::unordered_set<int> emptySoft, enemySoft{35};
        const auto beforeSoft=lease->queries();
        if (!verify(state,mover,state.terrainByCell,occupied,{8,8},-1,&emptySoft)
            || !verify(state,mover,state.terrainByCell,occupied,{8,8},-1,&enemySoft)
            || lease->queries()!=beforeSoft) return fail("nonnull soft inputs must use the general wrapper");
        {
            RuntimeComponentQueryLeaseReferenceScope reference(true);
            const auto count=lease->queries();
            if (!verify(state,mover,state.terrainByCell,occupied,{0,0}) || lease->queries()!=count)
                return fail("independent lease reference did not restore C20 wrapper");
        }
        {
            RuntimeComponentPassabilityReferenceScope reference(true);
            const auto count=lease->queries();
            if (!verify(state,mover,state.terrainByCell,occupied,{0,0}) || lease->queries()!=count)
                return fail("original scalar reference reused owned memo");
        }
        ++state.tick;
        const auto stale=lease->queries();
        if (!verify(state,mover,state.terrainByCell,occupied,{0,0}) || lease->queries()!=stale)
            return fail("tick boundary reused the old lease");
        --state.tick;
        if (!capture(state,after,error) || !(before==after)) return fail("boundary queries changed full authority/save");
        scope.invalidate();
        state.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=false;
        if (runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},mover)
            || !verify(state,mover,state.terrainByCell,occupied,{0,0}) || lease->queries()!=stale)
            return fail("explicit scope invalidation missed same-address metadata change");
        state.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=true;
        state.world.accessObjects().push_back(object(999,WorldObjectKind::Unit,"CAR",0,0));
        // The old mover reference may be invalid after append: bind its real
        // current address and read a fresh occupancy map after invalidation.
        auto changedOccupied=buildRuntimeOccupancyLookup(state);
        if (!verify(state,*findObjectById(state,1),state.terrainByCell,changedOccupied,{0,0})
            || lease->queries()!=stale) return fail("invalidated append/foreign mover address fallback");
    }
    // Kernel/lease invalidation must also disable its original memo owner,
    // otherwise the fallback wrapper could return an old hot boolean after
    // same-address mover/rules edits despite correctly rejecting the lease.
    for (bool oldLease : {false,true}) for (int change=0;change<2;++change) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,0,0,error)) return false;
        auto& mover=*findMutableObjectById(state,1);
        const auto occupied=buildRuntimeOccupancyLookup(state);
        RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
        RuntimeComponentQueryLeaseReferenceScope leaseReference(oldLease);
        auto lease=RuntimePlannerQueryLeaseContractAccess::make(scope,state,mover,state.terrainByCell,occupied);
        if (!lease->query(state,state.terrainByCell,occupied,{0,0},mover) || scope.entries()!=1)
            return fail("same-address invalidation fixture initial clear-cell permission");
        lease->invalidate();
        if(change==0) mover.type="BOAT";
        else {
            IniFile changed;
            if(!changed.loadFromString(std::string(rulesText)+"[Clear]\nTrack=0%\n")
                || !state.rules.loadFromIni(changed) || !state.rules.findLandType("Clear")
                || state.rules.findLandType("Clear")->track!=0)
                return fail("same-address rule reload did not author real Clear Track=0 permission");
        }
        const auto expected=runtimeCellIsPassableUncached(state,state.terrainByCell,occupied,{0,0},mover);
        if(expected || lease->query(state,state.terrainByCell,occupied,{0,0},mover)!=expected
            || lease->queries()!=(oldLease?0U:1U))
            return fail("invalidated owned memo reused hot value after actual mover/rule change leaseReference="
                +std::to_string(oldLease));
        ++boundaries;
    }
    // Actual original transition, including its short circuit query order,
    // repair veto, bridge/water/height tail and destination/corner crush keys.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,0,0,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        const std::vector<RuntimeCellPoint> starts{{0,0},{1,1},{4,4},{5,6},{5,7},
            {8,4},{8,7},{8,8},{9,4},{10,4},{10,5},{11,11},{-1,0},{1000,0},{0,1}};
        const std::unordered_set<int> emptySoft, enemySoft{35};
        Authority before,after;
        if (!capture(state,before,error)) return false;
        for (int id : {1,2,3,4,5,6,41}) {
            const auto& mover=*findObjectById(state,id);
            RuntimeBuildingDepartureReadOnlyQueryScope departures(state,mover);
            RuntimePlannerCellPassabilityScope scope(state,mover,state.terrainByCell,occupied);
            auto lease=RuntimePlannerQueryLeaseContractAccess::make(scope,state,mover,state.terrainByCell,occupied);
            for (const auto& current:starts) {
                auto nextPoints=std::vector<RuntimeCellPoint>{current,{current.x+2,current.y}};
                const auto neighbors=runtimeNeighborsOf(current);
                nextPoints.insert(nextPoints.end(),neighbors.begin(),neighbors.end());
                for (const auto& next:nextPoints) for (int allowed : {-1,31}) for (bool crush : {false,true})
                    for (const auto* soft : {static_cast<const std::unordered_set<int>*>(nullptr),&emptySoft,&enemySoft}) {
                        Observations oldQueries,newQueries;
                        bool expected=false,actual=false;
                        {
                            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&oldQueries);
                            expected=runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,current,next,
                                mover,allowed,soft,crush);
                        }
                        {
                            RuntimePlannerPassabilityObserverScope observe(&Observations::append,&newQueries);
                            actual=runtimeCellTransitionIsPassable(state,state.terrainByCell,occupied,current,next,
                                mover,allowed,soft,crush,std::nullopt,nullptr,lease.get());
                        }
                        ++edges;
                        if (expected!=actual || oldQueries.values!=newQueries.values)
                            return fail("directed transition result/ordered original keys id="+std::to_string(id)
                                +" from="+std::to_string(current.x)+","+std::to_string(current.y));
                    }
            }
            if (lease->fullValidations()!=1 || (id!=5 && lease->queries()==0))
                return fail("actual transition matrix never used its once-validated lease");
        }
        if (!capture(state,after,error) || !(before==after)) return fail("actual transition matrix changed full authority/save");
    }
    // Whole production BFS in scalar, C20, C21b and C22 modes. Check the exact
    // returned unordered iteration order too: the global nearest fallback's
    // equal-score winner observes that order. No reconstruction substitutes.
    for (int history : {0,1000,10000}) for (int phase : {0,1}) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,history,phase,error)) return false;
        const auto occupied=buildRuntimeOccupancyLookup(state);
        auto malformed=state.terrainByCell;
        malformed.emplace(-1,state.terrainByCell.at(runtimeCellKey(0,0)));
        malformed[runtimeCellKey(7,5)]=nullptr;
        malformed[runtimeCellKey(1000,0)]=state.terrainByCell.at(runtimeCellKey(4,6));
        malformed.emplace(std::numeric_limits<int>::max(),state.terrainByCell.at(runtimeCellKey(11,11)));
        for (const auto* cells : {&state.terrainByCell,&malformed}) for (int id : {1,2,3,4,5,6,41}) {
            const auto& mover=*findObjectById(state,id);
            Authority before,after;
            if (!capture(state,before,error)) return false;
            std::vector<std::pair<int,int>> referenceOrder;
            Observations referenceQueries;
            std::optional<RuntimeCellPoint> referenceFallback;
            for (int branch=0;branch<4;++branch) {
                RuntimeComponentPassabilityReferenceScope scalar(branch==0);
                RuntimeComponentKernelReferenceScope oldCost(branch==0);
                RuntimeComponentQueryLeaseReferenceScope oldLease(branch<2);
                RuntimeComponentEmptyCensusReferenceScope oldCensus(branch<2);
                RuntimeComponentCrushAliasReferenceScope oldAlias(branch!=3);
                Observations observed;
                std::unordered_map<int,int> result;
                {
                    RuntimePlannerPassabilityObserverScope observe(&Observations::append,&observed);
                    result=buildRuntimeMoveComponentByCell(state,mover,*cells,occupied);
                }
                const std::vector<std::pair<int,int>> order(result.begin(),result.end());
                int required=-1;
                if (const auto found=result.find(runtimeCellKey(mover.x,mover.y));found!=result.end()) required=found->second;
                const auto fallback=findNearestRuntimePassableCellWithOccupancy(state,mover,{100,100},-1,nullptr,
                    *cells,occupied,nullptr,&result,required);
                if (branch==0) { referenceOrder=order; referenceQueries=observed; referenceFallback=fallback; }
                else if (order!=referenceOrder || observed.values!=referenceQueries.values || fallback!=referenceFallback)
                    return fail("complete seed/label/iteration/candidate/fallback differs history="+std::to_string(history)
                        +" phase="+std::to_string(phase)+" mover="+std::to_string(id)+" branch="+std::to_string(branch));
                ++components;
                if (!capture(state,after,error) || !(before==after)) return fail("complete BFS changed full authority/save");
            }
        }
    }
    // Conservative census is an absence proof only. Adversarial residents,
    // health-zero/negative/duplicate IDs, dead host cargo, footprints, bridge
    // layers and reservations all use the same authored world above.
    {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,1000,1,error)) return false;
        const auto& infantry=*findObjectById(state,2);
        Authority before,after;
        if (!capture(state,before,error)) return false;
        RuntimePlannerEmptyCellCensusScope census(state);
        if (!census.enabled() || census.bytes()!=32768)
            return fail("actual resident census enabled="+std::to_string(census.enabled())
                +" bytes="+std::to_string(census.bytes())+" marked="+std::to_string(census.marked())
                +" rectangleWork="+std::to_string(census.rectangleWork()));
        if (!census.provesEmpty(state,{0,0})) return fail("actual resident census clear origin must be empty");
        for (const RuntimeCellPoint cell : {RuntimeCellPoint{6,6},{7,7},{7,8},{4,4},{9,9},{3,6},{4,6}})
            if (census.provesEmpty(state,cell))
                return fail("census missed actual resident union at="+std::to_string(cell.x)+","+std::to_string(cell.y));
        for (int y=-1;y<=12;++y) for (int x=-1;x<=12;++x) {
            const RuntimeCellPoint cell{x,y}; ++censusCells;
            const auto expectedHard=runtimeCellHasHardWholeCellBlocker(state,cell,2,-1,{},true);
            const auto hard=runtimeCellHasHardWholeCellBlocker(state,cell,2,-1,{});
            const auto expectedSlots=runtimeAvailableInfantrySubcells(state,infantry,cell,RuntimeGatePassageMode::Commit,true);
            const auto slots=runtimeAvailableInfantrySubcells(state,infantry,cell);
            if (hard!=expectedHard || slots!=expectedSlots)
                return fail("census original hard blocker/reservations/layer/slot order at="+std::to_string(x)+","+std::to_string(y));
        }
        {
            RuntimePlannerEmptyCellCensusScope disabled(state,false);
            if (runtimePlannerCensusProvesEmptyCell(state,{0,0})) return fail("disabled census borrowed outer proof");
        }
        if (!runtimePlannerCensusProvesEmptyCell(state,{0,0})) return fail("nested census did not restore outer scope");
        auto foreign=std::make_unique<RuntimeWindowState>();
        {
            RuntimePlannerEmptyCellCensusScope inner(*foreign);
            if (runtimePlannerCensusProvesEmptyCell(state,{0,0})
                || !runtimePlannerCensusProvesEmptyCell(*foreign,{0,0})) return fail("foreign nested census state identity");
        }
        if (census.provesEmpty(*foreign,{0,0}) || census.provesEmpty(state,{1000,0})
            || census.provesEmpty(state,{-1,0})) return fail("foreign/raw-alias/negative query must reject proof");
        {
            RuntimeComponentEmptyCensusReferenceScope reference(true);
            if (census.provesEmpty(state,{0,0})) return fail("census reference did not bypass existing scope");
        }
        ++state.tick;
        if (census.provesEmpty(state,{0,0})) return fail("census crossed tick boundary");
        --state.tick;
        proofs+=census.proofs();
        if (!census.hardProofs() || !census.availabilityProofs()
            || census.rectangleWork()>1048576) return fail("actual hard/availability census fast paths or work bound not exercised");
        if (!capture(state,after,error) || !(before==after)) return fail("census changed complete authority/save");
        census.invalidate();
        state.world.accessObjects().push_back(object(998,WorldObjectKind::Unit,"CAR",0,0));
        if (census.provesEmpty(state,{0,0})
            || !runtimeCellHasHardWholeCellBlocker(state,{0,0},2,-1,{},true)
            || runtimeCellHasHardWholeCellBlocker(state,{0,0},2,-1,{})
                !=runtimeCellHasHardWholeCellBlocker(state,{0,0},2,-1,{},true))
            return fail("invalidated census must read the newly added true hard blocker");
    }
    // Outside/negative/extreme footprints may extend into the legal map. They
    // disable the ENTIRE proof, never disappear via clamping or key decoding.
    for (int variant=0;variant<6;++variant) {
        auto box=std::make_unique<RuntimeWindowState>(); auto& state=*box;
        RuntimeSnapshotActiveStateScope active(state);
        if (!makeFixture(state,config,0,0,error)) return false;
        if (variant<3) {
            auto unusual=object(999,WorldObjectKind::Structure,"SOLID",variant==0?-1:variant==1?511:std::numeric_limits<int>::max(),0);
            state.world.accessObjects().push_back(unusual);
        } else if (variant<5) {
            WorldTerrainObject unusual; unusual.alive=true; unusual.health=100;
            unusual.occupationBits=1; unusual.x=variant==3?-1:0; unusual.y=0;
            unusual.footprintWidth=variant==3?2:std::numeric_limits<int>::max();
            state.world.accessTerrainObjects().push_back(unusual);
        } else {
            state.structureFootprints["HUGE"]={512,512,true};
            for (int n=0;n<10;++n)
                state.world.accessObjects().push_back(object(1000+n,WorldObjectKind::Structure,"HUGE",0,0));
            const auto actualFootprint=runtimeStructureFootprintForObject(state,state.world.getObjects().back());
            if (actualFootprint.width!=512 || actualFootprint.height!=512)
                return fail("work-bound fixture did not establish real 512x512 foundations");
        }
        RuntimePlannerEmptyCellCensusScope unsupported(state);
        if (unsupported.enabled() || unsupported.provesEmpty(state,{0,0}) || unsupported.bytes()>32768
            || unsupported.rectangleWork()>1048576)
            return fail("out-of-domain/overflow rectangle did not conservatively disable census variant="+std::to_string(variant));
        ++boundaries;
    }
    // Short actual stock-asset commands and continuation. The second command
    // selects twenty-four real infantry, making the genuine production census and
    // once-validated kernel paths observable without extending the old suite.
    std::size_t actualCommands=0,actualTicks=0,actualLeased=0,actualHardProofs=0,actualSlotsProofs=0;
    std::size_t actualAliases=0,actualQualified=0,actualFoundationBorrows=0,actualScalarSaved=0;
    struct Witness {
        std::size_t kernels=0,validations=0,leased=0,censuses=0,hard=0,slots=0;
        std::size_t aliases=0,qualified=0,foundationBorrows=0,scalarQueries=0;
        bool invalidValidationCount=false;
        static void kernel(void* raw,std::size_t validations,std::size_t queries,std::size_t) {
            auto& out=*static_cast<Witness*>(raw); ++out.kernels; out.validations+=validations; out.leased+=queries;
            if (queries && validations!=1) out.invalidValidationCount=true;
        }
        static void census(void* raw,bool enabled,std::size_t,std::size_t hard,std::size_t slots) {
            auto& out=*static_cast<Witness*>(raw); if(enabled) ++out.censuses; out.hard+=hard; out.slots+=slots;
        }
        static void alias(void* raw,std::size_t aliases,std::size_t qualified,std::size_t borrows,std::size_t scalar) {
            auto& out=*static_cast<Witness*>(raw);
            out.aliases+=aliases; out.qualified+=qualified; out.foundationBorrows+=borrows; out.scalarQueries+=scalar;
        }
    };
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("stock assets unavailable for actual command/save comparison");
    for (int history : {0,1000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,4> states;
        std::array<std::vector<int>,4> ids;
        for (int branch=0;branch<4;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>(); auto& state=*states[branch];
            state.config=config; state.report=assets->report; state.rules=assets->rules;
            state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if (!prepareRuntimeMassMovementWorld(state,48,ids[branch],error,false,true)) return false;
            state.config.defaultMapPath="PlannerQueryLeaseMassMovementContract.map";
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                for(int n=0;n<history;++n) {
                    auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans"; dead.alive=false; dead.health=0; dead.mission="Destroyed";
                    state.world.accessObjects().push_back(std::move(dead));
                }
                auto terrainInfo=std::move(state.terrainInfoByCell);
                state.objectIndex=RuntimeObjectIndex{}; initializeRuntimeNavigation(state);
                state.terrainInfoByCell=std::move(terrainInfo);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
                invalidateRuntimeTickOccupancyCache(state);
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1] || ids[0]!=ids[2] || ids[0]!=ids[3]) return fail("actual stock fixture actor order differs");
        if(ids[0].size()!=48 || findObjectById(*states[0],ids[0].front())->type!="MTNK")
            return fail("actual mixed fixture must contain 48 actors and the original first MTNK kernel representative");
        std::vector<int> infantryIds;
        for(int id:ids[0]) if(findObjectById(*states[0],id)->kind==WorldObjectKind::Infantry) infantryIds.push_back(id);
        if(infantryIds.size()!=24) return fail("actual compact infantry command must contain twenty-four real infantry actors");
        bool activeMovement=false;
        for(int n=1;n<=32;++n) {
            Authority referenceState;
            std::map<int,RuntimeCellPoint> referenceDestinations;
            Observations referenceCalls;
            std::size_t c21ScalarQueries=0;
            for(int branch=0;branch<4;++branch) {
                auto& state=*states[branch]; state.tick=n;
                RuntimeSnapshotActiveStateScope active(state);
                RuntimeComponentPassabilityReferenceScope scalar(branch==0);
                RuntimeComponentKernelReferenceScope oldKernel(branch==0);
                RuntimeComponentQueryLeaseReferenceScope oldLease(branch<2);
                RuntimeComponentEmptyCensusReferenceScope oldCensus(branch<2);
                RuntimeComponentCrushAliasReferenceScope oldAlias(branch!=3);
                if(n==1 || n==9 || n==17) {
                    const auto& selected=n==9 ? infantryIds : ids[branch];
                    std::map<int,RuntimeCellPoint> destinations; Observations calls; Witness witness;
                    {
                        RuntimePlannerKernelLeaseObserverScope observeKernel(&Witness::kernel,&witness);
                        RuntimePlannerEmptyCensusObserverScope observeCensus(&Witness::census,&witness);
                        RuntimePlannerPassabilityObserverScope observeQueries(&Observations::append,&calls);
                        RuntimePlannerCrushAliasObserverScope observeAlias(&Witness::alias,&witness);
                        if (!issueRuntimeMassMovementOrder(state,selected,
                                n==9 ? RuntimeCellPoint{22,48} : RuntimeCellPoint{76,32},destinations,error)) return false;
                    }
                    if(branch==0) { referenceDestinations=destinations; referenceCalls=calls; }
                    else if(destinations!=referenceDestinations || calls.values!=referenceCalls.values)
                        return fail("actual accepted destination IDs and full ordered command queries differ");
                    if(branch>=2) {
                        if(!witness.kernels || witness.validations!=witness.kernels || witness.invalidValidationCount
                            || !witness.leased || (n==9 && (!witness.censuses || !witness.hard || !witness.slots)))
                            return fail("actual command did not exercise exactly-one lease validation / infantry census fast paths");
                        actualLeased+=witness.leased; actualHardProofs+=witness.hard; actualSlotsProofs+=witness.slots;
                    } else if(witness.leased || witness.hard || witness.slots)
                        return fail("actual scalar/C20 ablation accidentally used C21 lease/census");
                    if(branch<3 && (witness.aliases || witness.qualified || witness.foundationBorrows))
                        return fail("C22 independent reference did not preserve scalar/C20/C21b crush keys");
                    if(branch==2) {
                        c21ScalarQueries=witness.scalarQueries;
                        if(n!=9 && witness.censuses)
                            return fail("C21b mixed first-MTNK reference unexpectedly enabled a ground census");
                    }
                    if(branch==3) {
                        if(!witness.aliases || !witness.qualified || !witness.foundationBorrows
                            || witness.foundationBorrows>witness.kernels || !witness.censuses
                            || witness.scalarQueries>=c21ScalarQueries)
                            return fail("actual mixed/infantry command did not save genuine C21b scalar work tick="
                                +std::to_string(n)+" c21Scalar="+std::to_string(c21ScalarQueries)
                                +" c22Scalar="+std::to_string(witness.scalarQueries)+" aliases="+std::to_string(witness.aliases));
                        actualAliases+=witness.aliases; actualQualified+=witness.qualified;
                        actualFoundationBorrows+=witness.foundationBorrows;
                        actualScalarSaved+=c21ScalarQueries-witness.scalarQueries;
                    }
                    ++actualCommands;
                }
                (void)advanceRuntimeSimulation(state);
                (void)completeRuntimeWorldRefresh(state);
                Authority current;
                if(!capture(state,current,error)) return false;
                if(branch==0) referenceState=current;
                else if(!(current==referenceState))
                    return fail("actual per-tick full authority/save differs history="+std::to_string(history)
                        +" tick="+std::to_string(n)+" branch="+std::to_string(branch));
                if(branch==3) for(int id:ids[branch]) {
                    const auto* actor=findObjectById(state,id);
                    activeMovement|=actor && actor->renderMoveActive;
                }
            }
            ++actualTicks;
        }
        if(!activeMovement) return fail("actual command continuation never produced active movement");
    }
    if (runtimeActivePlannerCellPassability || runtimeActivePlannerEmptyCellCensus
        || !boundaries || !edges || !components || !censusCells || !proofs)
        return fail("actual lease/census matrix incomplete or scope leaked");
    std::cout << "Planner query lease/census: boundaries=" << boundaries << " exactOrderedTransitions=" << edges
        << " completeOrderedComponents=" << components << " scalarCensusCells=" << censusCells
        << " actualEmptyProofs=" << proofs << " fullAuthorityAndSave=yes failedSeedAndGlobalFallback=yes pass\n";
    std::cout << "Planner query lease/census actual stock commands=" << actualCommands
        << " fourWayPerTickSaveHashes=" << actualTicks << " leasedQueries=" << actualLeased
        << " hardEmptyProofs=" << actualHardProofs << " reservedSlotProofs=" << actualSlotsProofs << " pass\n";
    std::cout << "Planner empty crush alias: actualAliases=" << actualAliases << " qualifiedCells=" << actualQualified
        << " originalFoundationBorrows=" << actualFoundationBorrows << " actualScalarQueriesSaved=" << actualScalarSaved
        << " firstMTNKMixedAndInfantry=yes primitiveCases=" << aliasCases
        << " primitiveScalarSaved=" << aliasPrimitiveScalarSaved << " pass\n";
    error.clear(); return true;
}
