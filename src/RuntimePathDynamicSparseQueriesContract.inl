// Append after the existing metadata/conditional and cold-lookup contracts.
// Original production snapshot vectors are compared; no mirrored passability
// algorithm substitutes for either branch. References change only this census.
#pragma once

namespace RuntimePathDynamicSparseQueriesContractDetail {
using Occupants = RuntimePathDynamicSparseQueries::Occupants;
using Full = RuntimePathSnapshotConditionalQueriesContractDetail::Full;
using RuntimePathSnapshotConditionalQueriesContractDetail::capture;
using RuntimePathSnapshotMetadataQueriesContractDetail::fields;
using RuntimePlannerPassabilityContractDetail::object;
struct Observation {
    const void* expected=nullptr;
    bool foreign=false;
    std::vector<RuntimePathDynamicSparseQueryStats> calls;
    static void append(void* raw,const void* state,const RuntimePathDynamicSparseQueryStats& stats) {
        auto& out=*static_cast<Observation*>(raw); out.foreign|=out.expected!=state;
        out.calls.push_back(stats);
    }
};
const int* scalarOccupant(const Occupants& occupants,int key) {
    const auto at=occupants.find(key); return at==occupants.end()?nullptr:std::addressof(at->second);
}
std::uint64_t repairKey(int x,int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))<<32U)
        |static_cast<std::uint32_t>(y);
}
bool sameSnapshot(const std::vector<PathTerrainCell>& a,const std::vector<PathTerrainCell>& b,std::size_t& cells) {
    if(a.size()!=b.size()) return false;
    for(std::size_t n=0;n<a.size();++n) {if(fields(a[n])!=fields(b[n])) return false;++cells;}
    return true;
}
}

bool runRuntimePathDynamicSparseQueriesPrimitiveContract(std::size_t& checks,std::string& error) {
    using namespace RuntimePathDynamicSparseQueriesContractDetail;
    const auto fail=[&](const std::string& why){error="path dynamic sparse queries: primitive "+why;return false;};
    RuntimePathDynamicSparseQueriesReferenceScope optimized(false);
    int state=1,mover=2,tick=70,foreign=3;
    Occupants occupied{{0,std::numeric_limits<int>::min()},{1000,-7},{511511,std::numeric_limits<int>::max()},
        {1002,0},{512,4},{std::numeric_limits<int>::max(),5},{-1,9}};
    std::unordered_map<std::uint64_t,unsigned char> repairs{{repairKey(0,0),1},{repairKey(0,1),2},
        {repairKey(12,15),3},{repairKey(1000,0),1},{repairKey(512,0),1},{repairKey(-1,0),1}};
    const auto visit=[&](auto&& add) {for(const auto& [key,layer]:repairs) {
        (void)layer;add(static_cast<std::int32_t>(key>>32U),static_cast<std::int32_t>(key));}};
    const auto repairAt=[&](int x,int y) {const auto at=repairs.find(repairKey(x,y));
        return at!=repairs.end() && (at->second&(((x+y)&1)?2:1))!=0;};
    RuntimePathDynamicSparseQueries owner(&state,&mover,tick,occupied,262144,true,visit);
    const auto view=owner.viewFor(&state,&mover,tick,occupied);
    if(!view || owner.stats().retainedBytes!=262144) return fail("canonical bounded admission");
    for(int y=0;y<512;++y) for(int x=0;x<512;++x) {
        const auto inputs=view->possibleInputs(x,y);
        if(view->representative(inputs,[&]{return scalarOccupant(occupied,runtimeCellKey(x,y));})
            !=scalarOccupant(occupied,runtimeCellKey(x,y))) return fail("all-domain original pointer/value/presence");
        if(view->repairReserved(inputs,[&]{return repairAt(x,y);})!=repairAt(x,y)) return fail("all-domain original repair/layer");
        checks+=2;
    }
    for(const auto point:{RuntimeCellPoint{-1,0},{0,-1},{512,0},{0,512},{1000,0},{511,511},{1002,0}}) {
        const auto inputs=view->possibleInputs(point.x,point.y);
        if(view->representative(inputs,[&]{return scalarOccupant(occupied,runtimeCellKey(point.x,point.y));})
            !=scalarOccupant(occupied,runtimeCellKey(point.x,point.y))
            ||view->repairReserved(inputs,[&]{return repairAt(point.x,point.y);})!=repairAt(point.x,point.y))
            return fail("negative/noncanonical/raw aliases must retain original queries");
        checks+=2;
    }
    auto copiedOccupants=occupied;int equalTick=tick;
    if(owner.viewFor(&foreign,&mover,tick,occupied)||owner.viewFor(&state,&foreign,tick,occupied)
        ||owner.viewFor(&state,&mover,equalTick,occupied)||owner.viewFor(&state,&mover,tick,copiedOccupants))
        return fail("foreign state/mover/tick-address/occupancy identity");checks+=4;
    {
        RuntimePathDynamicSparseQueries disabled(&state,&mover,tick,occupied,262144,false,visit);
        if(disabled.enabled()||owner.viewFor(&state,&mover,tick,occupied)
            ||view->possibleInputs(33,33)!=RuntimePathDynamicSparseQueries::Original) return fail("disabled nested masking");++checks;
    }
    {
        RuntimePathDynamicSparseQueries nested(&foreign,&mover,tick,occupied,262144,true,visit);
        if(!nested.viewFor(&foreign,&mover,tick,occupied)
            ||view->possibleInputs(33,33)!=RuntimePathDynamicSparseQueries::Original) return fail("foreign nested masking");++checks;
    }
    {
        RuntimePathDynamicSparseQueriesReferenceScope reference(true);
        if(owner.viewFor(&state,&mover,tick,occupied)||view->possibleInputs(33,33)!=RuntimePathDynamicSparseQueries::Original)
            return fail("reference masks existing views");
        RuntimePathDynamicSparseQueries disabled(&state,&mover,tick,occupied,262144,true,visit);
        if(disabled.enabled()||disabled.stats().retainedBytes) return fail("reference must allocate nothing");checks+=2;
    }
    ++tick;if(view->possibleInputs(33,33)!=RuntimePathDynamicSparseQueries::Original) return fail("tick boundary");--tick;++checks;
    owner.invalidate();occupied[runtimeCellKey(33,33)]=17;
    const auto invalidInputs=view->possibleInputs(33,33);
    if(invalidInputs!=RuntimePathDynamicSparseQueries::Original
        ||view->representative(invalidInputs,[&]{return scalarOccupant(occupied,runtimeCellKey(33,33));})
            !=scalarOccupant(occupied,runtimeCellKey(33,33))) return fail("invalidate before same-address edit");++checks;
    for(const auto count:{0U,4095U,4096U,262145U}) {
        RuntimePathDynamicSparseQueries candidate(&state,&mover,tick,occupied,count,true,visit);
        if(candidate.enabled()!=(count>=4096)||candidate.stats().retainedBytes>262144) return fail("query minimum / fixed byte cap");checks+=2;
    }
    Occupants empty;const auto noRepairs=[](auto&&){};
    RuntimePathDynamicSparseQueries zero(&state,&mover,tick,empty,4096,true,noRepairs);
    if(!zero.enabled()||zero.stats().retainedBytes) return fail("empty input needs no allocation");++checks;
    if(!owner.stats().representativeSkipped||!owner.stats().repairSkipped||!owner.stats().proofFallbacks)
        return fail("positive actual proof/elision/fallback witness");++checks;
    for(const std::size_t count:{512U,513U,4096U,4097U}) {
        Occupants populated;for(std::size_t n=0;n<count;++n) populated.emplace(static_cast<int>(n),-static_cast<int>(n));
        const std::size_t queries=count<=513?4096:65536;
        RuntimePathDynamicSparseQueries candidate(&state,&mover,tick,populated,queries,true,noRepairs);
        const bool admitted=count<=4096 && count<=queries/8;
        if(candidate.enabled()!=admitted || (!admitted && candidate.stats().retainedBytes))
            return fail("high-density admission before allocation");++checks;
    }
    {
        RuntimePathDynamicSparseQueries noCounters(&state,&mover,tick,occupied,4096,true,visit);
        auto borrowed=noCounters.viewFor(&state,&mover,tick,occupied);
        if(!borrowed || noCounters.collectStats()) return fail("ordinary counter-free view");
        const auto flags=borrowed->possibleInputs<false>(33,33);
        if(borrowed->representative<false>(flags,[&]{return scalarOccupant(occupied,runtimeCellKey(33,33));})
            !=scalarOccupant(occupied,runtimeCellKey(33,33))
            ||borrowed->repairReserved<false>(flags,[&]{return repairAt(33,33);})!=repairAt(33,33)
            ||noCounters.stats().proofs||noCounters.stats().representativeQueries||noCounters.stats().repairQueries)
            return fail("counter-free calls preserve answers without per-query counter work");checks+=2;
    }
    if(checks!=524328) return fail("strong primitive count="+std::to_string(checks));
    return true;
}

#include "RuntimePathDynamicCellBatchNativeContract.inl"

bool runRuntimePathDynamicSparseQueriesActualContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimePathDynamicSparseQueriesContractDetail;
    const auto fail=[&](const std::string& why){error="path dynamic sparse queries: "+why;return false;};
    RuntimeSimulationQueryReuseReferenceScope reusable(false);
    RuntimePathSnapshotMetadataQueryReferenceScope metadata(false);
    RuntimePathSnapshotOccupancyQueryReferenceScope representative(false);
    RuntimePathDynamicSparseQueriesReferenceScope sparse(false);
    std::size_t primitive=0,snapshotCases=0,fullFieldCells=0,commands=0,ticks=0;
    std::size_t batchLoopComparisons=0;
    std::size_t enabled=0,repSkipped=0,repairSkipped=0,referenceCases=0,fallbackProofs=0,bytesSaved=0;
    if(!runRuntimePathDynamicSparseQueriesPrimitiveContract(primitive,error)) return false;
    const auto compare=[&](RuntimeWindowState& old,RuntimeWindowState& current,int moverId,int allowed,
        const std::string& label,bool forceOldOccupancy=false) {
        Full before[2],after[2];std::vector<PathTerrainCell> snapshots[2];Observation observations[2];
        for(int branch=0;branch<2;++branch) {
            auto& state=branch?current:old;RuntimeSnapshotActiveStateScope active(state);
            if(!capture(state,before[branch],error)) return fail(label+" before full writer: "+error);
            state.pathTerrainCache.dynamicValid=false;
            observations[branch].expected=&state;
            {
                RuntimePathDynamicCellBatchReferenceScope loopReference(branch==0); RuntimePathDynamicSparseQueriesReferenceScope reference(branch==0);
                RuntimePathDynamicSparseQueryObserverScope observe(&Observation::append,&observations[branch]);
                const auto* mover=findObjectById(state,moverId);if(!mover) return fail(label+" missing mover");
                snapshots[branch]=buildRuntimePathTerrainSnapshot(state,*mover,allowed,nullptr,forceOldOccupancy);
                // A same-key warm query must retain the exact original dynamic
                // cache behavior, rather than building a second proof/index.
                const auto& warm=buildRuntimePathTerrainSnapshot(state,*mover,allowed,nullptr,forceOldOccupancy);
                std::size_t warmCells=0;
                if(!sameSnapshot(snapshots[branch],warm,warmCells)) return fail(label+" warm full fields/order");
                // Compare the opposite loop-call discipline on the SAME real
                // builder inputs, in every old scalar/sparse/reference case.
                // This observer masks the original one without extending its
                // exactly-one-cold + zero-warm requirement.
                state.pathTerrainCache.dynamicValid=false;
                RuntimePathDynamicCellBatchReferenceScope oppositeLoop(branch!=0);
                Observation opposite;opposite.expected=&state;
                {
                    RuntimePathDynamicSparseQueryObserverScope observeOpposite(&Observation::append,&opposite);
                    const auto oppositeValues=buildRuntimePathTerrainSnapshot(state,*mover,allowed,nullptr,forceOldOccupancy);
                    std::size_t oppositeFields=0;
                    if(!sameSnapshot(snapshots[branch],oppositeValues,oppositeFields)
                        ||!sameSnapshot(oppositeValues,buildRuntimePathTerrainSnapshot(state,*mover,allowed,nullptr,forceOldOccupancy),oppositeFields))
                        return fail(label+" opposite loop ordered 12 fields + original warm result");
                }
                if(observations[branch].calls.size()!=1||opposite.foreign||opposite.calls.size()!=1
                    ||!RuntimePathDynamicCellBatchNativeContractDetail::sameStats(observations[branch].calls.front(),opposite.calls.front()))
                    return fail(label+" opposite loop cold/warm/exact query proof counts");
                ++batchLoopComparisons;
            }
            if(!capture(state,after[branch],error)) return fail(label+" after full writer: "+error);
        }
        if(!(before[0]==before[1])||!(after[0]==after[1])||!(before[0]==after[0])||!(before[1]==after[1]))
            return fail(label+" complete authority/full save/special/persistent changed");
        if(snapshots[0].empty()||!sameSnapshot(snapshots[0],snapshots[1],fullFieldCells))
            return fail(label+" complete ordered 12-field snapshot");
        for(int branch=0;branch<2;++branch) {
            if(observations[branch].foreign||observations[branch].calls.size()!=1)
                return fail(label+" exact cold owner + zero warm rebuild count="+std::to_string(observations[branch].calls.size()));
            const auto& witness=observations[branch].calls.front();
            if(witness.retainedBytes>262144 || (!branch && (witness.enabled||witness.retainedBytes||witness.proofs)))
                return fail(label+" scalar reference allocation/query proof");
            if(!branch) {++referenceCases;continue;}
            enabled+=witness.enabled;repSkipped+=witness.representativeSkipped;repairSkipped+=witness.repairSkipped;
            fallbackProofs+=witness.proofFallbacks;
            if(witness.enabled) {
                if(witness.proofs<snapshots[branch].size())
                    return fail(label+" admitted actual production proof traversal incomplete");
                bytesSaved+=witness.representativeBytesAvoided;
            } else if(witness.proofs||witness.representativeQueries||witness.repairQueries)
                return fail(label+" fallback loop must add no proof/counter queries");
        }
        ++snapshotCases;return true;
    };
    // Preserve meaningful authored gates/foundations/repair/subcells, stale
    // metadata, duplicate IDs and dead hosts. Only enlarge this new fixture to
    // the existing 4096-query admission minimum. Both sides get identical input.
    for(int history:{0,1000,10000}) for(int phase=0;phase<6;++phase) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& state=*states[branch];
            RuntimeSnapshotActiveStateScope active(state);
            if(!RuntimePlannerColdLookupContractDetail::largeFixture(state,config,history,phase==5?1:0,error))
                return fail("synthetic enlarged original fixture: "+error);
            if(state.repairDepots.relations().size()!=1||!runtimeCellReservedByOtherRepairUnit(state,{5,7},1)
                ||!state.terrainInfoByCell.at(runtimeCellKey(10,5)).bridge) return fail("repair/bridge original readback");
            IniFile rules;std::string text=RuntimePlannerPassabilityContractDetail::rulesText;
            text+="[Tiberium]\nTrack=75%\nFoot=50%\nFloat=0%\nHover=100%\n";
            if(phase==5) text+="[Clear]\nTrack=0%\n[CAR]\nThreatAvoidanceCoefficient=1\n"
                "[MAN]\nThreatPosed=25\nPrimary=SparseGun\n[WeaponTypes]\n0=SparseGun\n"
                "[SparseGun]\nDamage=10\nRange=6\nROF=1\nProjectile=SparseBullet\nSpeed=100\nWarhead=SparseWH\n"
                "[SparseBullet]\nImage=none\n[Warheads]\n0=SparseWH\n[SparseWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n";
            if(!rules.loadFromString(text)||!state.rules.loadFromIni(rules)) return fail("actual rules load/reload");
            state.capabilityCache.profilesByType.clear();
            if(phase==1) {
                for(auto& cell:state.world.accessTerrainCells()) if(cell.x==2&&cell.y==2) {
                    cell.hasOverlay=cell.overlayIsOre=true;cell.overlayIsGem=false;
                }
                state.terrainInfoByCell.at(runtimeCellKey(3,3)).authoredLandType=RuntimeTerrainLandType::Rock;
            }
            if(phase==2) {
                state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                const int area=state.specialMechanisms.spawnRadiationArea(1,"Player",{2,2},4,100,1,"",state.tick,true,SpecialEventSink{});
                const auto at=state.specialMechanisms.radiationAreas().find(area);
                if(area<0||at==state.specialMechanisms.radiationAreas().end()||at->second.strength<=0||at->second.remainingTicks<=0)
                    return fail("positive actual radiation readback");
                state.specialMechanismsInitialized=true;
            }
            if(phase==3) for(auto& [key,info]:state.terrainInfoByCell) {(void)key;info.passable=false;}
            if(phase==4) {
                for(const auto point:{RuntimeCellPoint{1000,0},{-1,0},{512,0},{0,512},{1002,0}}) {
                    TerrainCell cell;cell.x=point.x;cell.y=point.y;cell.cellIndex512=-1;state.world.accessTerrainCells().push_back(cell);
                }
                // End all borrowed pointers before vector growth and rebuild.
                state.terrainByCell=buildRuntimeTerrainLookup(state.world);
            }
            if(phase==5) {
                state.terrainInfoByCell.erase(runtimeCellKey(2,2));
                auto* changed=findMutableObjectById(state,1);if(!changed||!changed->alive) return fail("mutable source actor");
                changed->facing=173;
                if(!state.rules.findWeaponType("SparseGun")
                    ||runtimePathThreatAreas(state,*changed,runtimePathThreatAvoidance(state,*changed)).empty())
                    return fail("actual armed enemy/threat avoidance positive readback");
            }
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            state.foundationCollisionTick=-1;invalidateRuntimeTickOccupancyCache(state);
            (void)completeRuntimeWorldRefresh(state);
        }
        for(int mover:{1,2,3,4,5,6}) for(int allowed:{-1,30,33})
            if(!compare(*states[0],*states[1],mover,allowed,"history="+std::to_string(history)
                +" phase="+std::to_string(phase)+" mover="+std::to_string(mover)+" allowed="+std::to_string(allowed))) return false;
        if(phase==2) {
            auto& state=*states[1];RuntimeSnapshotActiveStateScope active(state);state.pathTerrainCache.dynamicValid=false;
            const auto values=buildRuntimePathTerrainSnapshot(state,*findObjectById(state,1));
            if(std::none_of(values.begin(),values.end(),[](const auto& value){return value.hazardCost>0;}))
                return fail("native radiation never reached actual output hazard");
        }
    }
    // Each old independent reference must restore its own original lookup and
    // also bypass this census, avoiding a misleading composed reference branch.
    {
        auto a=std::make_unique<RuntimeWindowState>(),b=std::make_unique<RuntimeWindowState>();
        for(auto* state:{a.get(),b.get()}) {RuntimeSnapshotActiveStateScope active(*state);
            if(!RuntimePlannerColdLookupContractDetail::largeFixture(*state,config,0,0,error)) return false;}
        for(int variant=0;variant<4;++variant) {
            RuntimeSimulationQueryReuseReferenceScope reuse(variant==0);
            RuntimePathSnapshotMetadataQueryReferenceScope metadataRef(variant==1);
            RuntimePathSnapshotOccupancyQueryReferenceScope occupancyRef(variant==2);
            const auto beforeEnabled=enabled;
            if(!compare(*a,*b,1,-1,"orthogonal old reference="+std::to_string(variant),variant==3)) return false;
            if(enabled!=beforeEnabled) return fail("old reference admitted a new census");
        }
    }
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success||!assets->report.rulesLoaded||!assets->report.artLoaded) return fail("actual stock assets unavailable");
    std::size_t actualEnabled=0,actualRepSkipped=0,actualRepairSkipped=0;
    for(int history:{0,1000,10000}) {
        std::array<std::unique_ptr<RuntimeWindowState>,2> states;std::array<std::vector<int>,2> ids;
        std::array<int,3> movers{-1,-1,-1};
        for(int branch=0;branch<2;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();auto& state=*states[branch];
            state.config=config;state.report=assets->report;state.rules=assets->rules;state.art=assets->art;state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded;state.artLoaded=assets->report.artLoaded;state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state);
            if(!prepareRuntimeMassMovementWorld(state,48,ids[branch],error,false,true,false,96,96,true,true))
                return fail("actual native96 stock48: "+error);
            state.config.defaultMapPath=state.report.map.path;
            if(state.config.defaultMapPath!="MassMovement-v1.map"||ids[branch].size()!=48||state.world.getTerrainCells().size()<4096)
                return fail("actual native map/source identity/cardinality");
            if(!branch) for(int id:ids[branch]) {const auto* actor=findObjectById(state,id);if(!actor) return fail("native ID missing");
                const auto mode=runtimeObjectMovementMode(state,*actor);
                if(mode==PathMovementMode::Ground&&actor->kind==WorldObjectKind::Unit&&movers[0]<0) movers[0]=id;
                if(mode==PathMovementMode::Ground&&actor->kind==WorldObjectKind::Infantry&&movers[1]<0) movers[1]=id;
                if(mode==PathMovementMode::Air&&movers[2]<0) movers[2]=id;}
            if(history) {
                const int first=nextRuntimeObjectInstanceId(state);
                if(first<=0||first>std::numeric_limits<int>::max()-history) return fail("history ID capacity");
                for(int n=0;n<history;++n) {auto dead=object(first+n,WorldObjectKind::Unit,"MTNK",2,2);
                    dead.owner="Americans";dead.alive=false;dead.health=0;dead.mission="Destroyed";state.world.accessObjects().push_back(std::move(dead));}
                auto metadataCopy=std::move(state.terrainInfoByCell);state.objectIndex=RuntimeObjectIndex{};
                initializeRuntimeNavigation(state);state.terrainInfoByCell=std::move(metadataCopy);
                state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;invalidateRuntimeTickOccupancyCache(state);
            }
            (void)completeRuntimeWorldRefresh(state);
        }
        if(ids[0]!=ids[1]||std::any_of(movers.begin(),movers.end(),[](int id){return id<0;}))
            return fail("actual independent stock Ground/Infantry/Air IDs");
        for(int mover:movers) if(!compare(*states[0],*states[1],mover,-1,
            "native history="+std::to_string(history)+" mover="+std::to_string(mover))) return false;
        std::vector<int> infantry;
        for(int id:ids[0]) {const auto* actor=findObjectById(*states[0],id);if(actor&&actor->kind==WorldObjectKind::Infantry) infantry.push_back(id);}
        if(infantry.size()!=24) return fail("actual infantry-only selection size");
        bool moving=false;
        for(int tick=1;tick<=32;++tick) {
            Full expected;std::map<int,RuntimeCellPoint> expectedDestinations;int expectedAccepted=-1,expectedRejected=-1;
            for(int branch=0;branch<2;++branch) {
                auto& state=*states[branch];state.tick=tick;RuntimeSnapshotActiveStateScope active(state);
                RuntimePathDynamicCellBatchReferenceScope loopReference(branch==0); RuntimePathDynamicSparseQueriesReferenceScope reference(branch==0);Observation observations;observations.expected=&state;
                {
                    RuntimePathDynamicSparseQueryObserverScope observe(&Observation::append,&observations);
                    if(tick==1||tick==9||tick==17) {
                        const auto& selected=tick==9?infantry:ids[branch];std::map<int,RuntimeCellPoint> destinations;
                        if(!issueRuntimeMassMovementOrder(state,selected,tick==9?RuntimeCellPoint{70,96}:RuntimeCellPoint{124,80},destinations,error,true))
                            return fail("actual normal command: "+error);
                        if(!branch) {expectedDestinations=destinations;expectedAccepted=state.lastCommand.acceptedCount;expectedRejected=state.lastCommand.rejectedCount;}
                        else if(destinations!=expectedDestinations||state.lastCommand.acceptedCount!=expectedAccepted||state.lastCommand.rejectedCount!=expectedRejected)
                            return fail("actual destinations/acceptance/recovery tick="+std::to_string(tick));
                        ++commands;
                    }
                    (void)advanceRuntimeSimulation(state);(void)completeRuntimeWorldRefresh(state);
                }
                if(observations.foreign) return fail("actual loop foreign census observer");
                for(const auto& witness:observations.calls) {
                    if(!branch&&(witness.enabled||witness.retainedBytes||witness.proofs)) return fail("actual reference built/used new proof");
                    if(branch) {actualEnabled+=witness.enabled;actualRepSkipped+=witness.representativeSkipped;actualRepairSkipped+=witness.repairSkipped;}
                }
                Full current;if(!capture(state,current,error)) return fail("actual perTick full writer: "+error);
                if(!branch) expected=std::move(current);else if(!(current==expected))
                    return fail("actual complete world/save/paths/systems history="+std::to_string(history)+" tick="+std::to_string(tick));
                moving|=std::any_of(state.behaviors.begin(),state.behaviors.end(),[](const auto& item){return item.second.moveCooldownTicks>0||!item.second.path.empty();});
            }
            ++ticks;
        }
        if(!moving) return fail("actual command corpus never moved");
    }
    if(batchLoopComparisons!=674||primitive!=524328||snapshotCases!=337||commands!=18||ticks!=96||referenceCases!=337
        ||!enabled||!repSkipped||!repairSkipped||!fallbackProofs||!bytesSaved
        ||!actualEnabled||!actualRepSkipped||!actualRepairSkipped||!fullFieldCells)
        return fail("exact matrix/actual production elision witnesses incomplete");
    RuntimePathDynamicCellBatchNativeContractDetail::Counts nativeBatch;
    if(!RuntimePathDynamicCellBatchNativeContractDetail::run(config,*assets,nativeBatch,error))return false;
    std::cout<<"Path dynamic sparse queries: primitiveExact="<<primitive<<" fullSnapshotCases="<<snapshotCases
        <<" fullFieldCells="<<fullFieldCells<<" fields=12 actualCommands="<<commands<<" perTickFullSaves="<<ticks
        <<" enabled="<<enabled<<" representativeSkipped="<<repSkipped<<" repairSkipped="<<repairSkipped
        <<" scalarFallbackProofs="<<fallbackProofs<<" actualEnabled="<<actualEnabled
        <<" actualRepresentativeSkipped="<<actualRepSkipped<<" actualRepairSkipped="<<actualRepairSkipped
        <<" representativeBytesAvoided="<<bytesSaved
        <<" ordered/world/fullSave/repair/gates/subcells/bridge/resource/radiation/aliases/history=native passed=yes\n";
    std::cout<<"Path dynamic cell batch: oppositeLoopSnapshots="<<batchLoopComparisons
        <<" originalSnapshotCases=337 originalCommands=18 originalTickPairs=96 nativeLargeSnapshotCases="<<nativeBatch.snapshots
        <<" nativeGroundEnabled="<<nativeBatch.groundEnabled<<" nativeAirRejected="<<nativeBatch.airRejected
        <<" nativeThreatBuilders="<<nativeBatch.threatBuilders<<" nativeRadiationBuilders="<<nativeBatch.radiationBuilders
        <<" nativeLargeCommands="<<nativeBatch.commands<<" nativeLargeTickPairs="<<nativeBatch.tickPairs
        <<" ordered12Fields/fullWriter/paths/acceptance/normalNoStats=exact passed=yes\n";
    error.clear();return true;
}
