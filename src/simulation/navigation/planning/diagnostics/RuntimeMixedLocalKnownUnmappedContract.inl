// Appended to, never replacing, the complete frozen C37 contract.
// All values below are expected coverage; runtime/compilation remain pending.
#pragma once
namespace {
struct RuntimeMixedLocalKnownUnmappedContractAccess {
    static bool bind(RuntimeMixedLocalComponentCertificate& proof,
        const RuntimeWindowState& state,const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupied,RuntimeMixedLocalComponentStats& stats,
        std::size_t points=4096,std::size_t edges=32768) {
        if(mover.kind!=WorldObjectKind::Unit || runtimeObjectMovementMode(state,mover)!=PathMovementMode::Ground)
            return false;
        for(const auto& [key,value]:terrain)
            if(!value || !proof.canonical({value->x,value->y})
                || key!=runtimeCellKey(value->x,value->y))return false;
        proof.state_=&state;proof.terrain_=&terrain;proof.occupancy_=&occupied;
        proof.mover_=mover;proof.tick_=state.tick;proof.stats_=&stats;
        proof.vertexLimit_=points;proof.edgeLimit_=edges;proof.knownUnmappedEnabled_=true;
        return true;
    }
    static bool prove(RuntimeMixedLocalComponentCertificate& proof,RuntimeCellPoint point) {
        return proof.knownUnmapped(point);
    }
    static bool listed(const RuntimeMixedLocalComponentCertificate& proof,RuntimeCellPoint point) {
        return proof.desiredKeys_.contains(runtimeCellKey(point.x,point.y));
    }
    static std::size_t setClosureLimit(RuntimeMixedLocalComponentCertificate& proof,std::size_t limit) {
        const auto previous=proof.frontierClosureLimit_;proof.frontierClosureLimit_=limit;return previous;
    }
    static void poisonMutualFailure(RuntimeMixedLocalComponentCertificate& proof) {
        // No directed truth is written. This deliberately forces the old mutual
        // cache to be unusable as evidence, even when ORIGINAL incoming is true.
        for(const auto& from:runtimeNeighborsOf({8,8}))
            proof.failedEdges_[runtimeCellKey(from.x,from.y)]=255;
    }
};
}
namespace RuntimeMixedLocalKnownUnmappedContractDetail {
using namespace RuntimeMixedLocalComponentContractDetail;
struct Counts {
    std::size_t boundaries=0,originalMaps=0,seedOracles=0,incomingOracles=0;
    std::size_t publishedChecked=0,memoWitnesses=0,crushVetoes=0,filterChecks=0;
    std::size_t nativeCases=0,commands=0,admittedCommands=0,warmTicks=0;
    std::size_t preSaves=0,fullSaves=0,trajectoryTriplets=0,ghostReadbacks=0;
    std::size_t actualUnmappedProofs=0,actualIncomingQueries=0,actualPublishedChecked=0;
    std::size_t closureCentres=0,closureReferenceVetoes=0,closureMembershipAdmissions=0,closureMemoWitnesses=0;
    std::size_t closureIncompleteVetoes=0,closureCanonicalVetoes=0,closureBudgetVetoes=0,closureFrozenChecks=0;
    std::size_t actualClosureAdmissions=0,actualClosurePoints=0;
    std::size_t initialPopulationReadbacks=0,finalPopulationReadbacks=0,stockTypeReadbacks=0;
};
bool originalAbsence(RuntimeWindowState& state,const WorldObjectInstance& mover,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupied,
    const RuntimeMixedLocalComponentCertificate& proof,Counts& counts,std::string& error) {
    RuntimeComponentKernelReferenceScope originalKernel(true);
    RuntimeComponentPassabilityReferenceScope originalCells(true);
    const auto complete=buildRuntimeMoveComponentByCell(state,mover,terrain,occupied);
    ++counts.originalMaps;
    for(int key:proof.knownUnmappedKeys()) {
        if(complete.contains(key)) {
            error="known unmapped: original complete map contains published key="+std::to_string(key);return false;
        }
        ++counts.publishedChecked;
    }
    return true;
}
bool leaf(const RuntimeConfig& config,Counts& counts,std::string& error) {
    const auto fail=[&](const std::string& text){error="known unmapped leaf: "+text;return false;};
    // Twelve named predicate/budget/reference/current-value cases, followed by
    // two distinct whole-layout rejection cases. No random or repeated filler.
    for(int variant=0;variant<12;++variant) {
        auto state=std::make_unique<RuntimeWindowState>();
        if(!make(*state,config,0,0,error))return false;
        auto& s=*state;RuntimeSnapshotActiveStateScope active(s);
        s.structureFootprints["WALL"]={1,1,true};
        s.foundationCollisionTick=-1;
        const auto* mover=findObjectById(s,1);if(!mover)return fail("actual CAR representative");
        auto terrain=s.terrainByCell;
        auto occupied=buildRuntimeOccupancyLookup(s,mover->instanceId);
        RuntimeCellPoint point{0,0};
        bool expected=variant<=4;
        if(variant==0 || variant==8 || variant==9 || variant==10 || variant==11)
            s.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=false;
        if(variant==1)point={10,4};
        if(variant==2)terrain.erase(runtimeCellKey(0,0));
        if(variant==3)point={511,511};
        if(variant==4)point={3,6};
        if(variant==6 || variant==7)point={8,8};
        RuntimeMixedKnownUnmappedReferenceScope reference(variant==8);
        RuntimeMixedLocalComponentStats stats;
        RuntimeMixedLocalComponentCertificate proof;
        if(!RuntimeMixedLocalKnownUnmappedContractAccess::bind(proof,s,*mover,terrain,occupied,stats,
                variant==10?0:4096,variant==9?1:32768))return fail("leaf binding");
        if(variant==7)RuntimeMixedLocalKnownUnmappedContractAccess::poisonMutualFailure(proof);
        if(variant==11) {
            if(!RuntimeMixedLocalKnownUnmappedContractAccess::prove(proof,point)
                || !originalAbsence(s,*mover,terrain,occupied,proof,counts,error))return fail("warm old current-value proof");
            s.terrainInfoByCell.at(runtimeCellKey(0,0)).passable=true;
            // A new lexical owner, never a shape/generation claim about values.
            RuntimeMixedLocalComponentCertificate fresh;
            RuntimeMixedLocalComponentStats freshStats;
            if(!RuntimeMixedLocalKnownUnmappedContractAccess::bind(fresh,s,*mover,terrain,occupied,freshStats)
                || RuntimeMixedLocalKnownUnmappedContractAccess::prove(fresh,point)
                || !fresh.knownUnmappedKeys().empty())return fail("same-address value edit fresh-owner readback");
        }
        const bool seed=runtimeCellIsPassable(s,terrain,occupied,point,*mover,-1,nullptr,false);
        ++counts.seedOracles;bool incoming=false;
        for(const auto& from:runtimeNeighborsOf(point)) {
            const bool transition=runtimeCellTransitionIsPassable(s,terrain,occupied,from,point,*mover);
            const bool original=runtimeCellTraversalCost(s,terrain,occupied,from,point,*mover).has_value();
            if(transition!=original)return fail("non-Air original callback equivalence");
            incoming|=original;++counts.incomingOracles;
        }
        if(variant==6 || variant==7) {
            if(seed || !incoming
                || !runtimeCellTransitionIsPassable(s,terrain,occupied,{8,9},point,*mover)
                || !runtimeCellTransitionIsPassable(s,terrain,occupied,point,{8,9},*mover))
                return fail("actual enemy crushable infantry requires false seed and both directed edges true");
            ++counts.crushVetoes;
        }
        if(variant<=4 && (seed || incoming))return fail("positive nine-predicate absence prerequisites variant="+std::to_string(variant));
        // The former owner in variant11 is deliberately never reused after its
        // source edit. Only the fresh owner above supplied the negative result.
        if(variant!=11) {
            const auto world=s.world.deterministicSimulationStateHash();
            const auto gates=s.gates.stateHash();
            const bool actual=RuntimeMixedLocalKnownUnmappedContractAccess::prove(proof,point);
            if(actual!=expected || (expected && proof.knownUnmappedKeys().size()!=1)
                || (!expected && !proof.knownUnmappedKeys().empty())
                || world!=s.world.deterministicSimulationStateHash() || gates!=s.gates.stateHash())
                return fail("exact result/authority variant="+std::to_string(variant));
            if(variant==0) {
                const auto queries=stats.incomingQueries;
                if(!RuntimeMixedLocalKnownUnmappedContractAccess::prove(proof,point)
                    || stats.incomingQueries!=queries || stats.unmappedMemoHits!=1)return fail("published-key memo");
                ++counts.memoWitnesses;
            }
            if(!originalAbsence(s,*mover,terrain,occupied,proof,counts,error))return false;
        } else {
            RuntimeMixedLocalComponentCertificate fresh;RuntimeMixedLocalComponentStats freshStats;
            if(!RuntimeMixedLocalKnownUnmappedContractAccess::bind(fresh,s,*mover,terrain,occupied,freshStats)
                || !originalAbsence(s,*mover,terrain,occupied,fresh,counts,error))return false;
        }
        ++counts.boundaries;
    }
    for(int variant=0;variant<2;++variant) {
        auto s=std::make_unique<RuntimeWindowState>();if(!make(*s,config,0,0,error))return false;
        const auto* mover=findObjectById(*s,1);if(!mover)return fail("layout mover");
        auto terrain=s->terrainByCell;TerrainCell bad;bad.x=variant==0?1000:2;bad.y=0;
        terrain[runtimeCellKey(0,1)]=&bad;
        const auto occupied=buildRuntimeOccupancyLookup(*s,mover->instanceId);
        RuntimeMixedLocalComponentCertificate proof;RuntimeMixedLocalComponentStats stats;
        if(RuntimeMixedLocalKnownUnmappedContractAccess::bind(proof,*s,*mover,terrain,occupied,stats)
            || !proof.knownUnmappedKeys().empty())return fail("alias/payload mismatch must reject whole admission");
        ++counts.boundaries;
    }
    return true;
}
// The default two cases are retained. The third, independent history case is
// used only to require a second REAL command closure admission, because the
// side256 default case may legitimately use exclusively prelisted centres.
bool native(const RuntimeConfig& config,Counts& counts,std::string& error,bool extraHistory=false) {
    const auto fail=[&](const std::string& text){error="known unmapped native: "+text;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)return fail("actual stock assets");
    RuntimeStrictSlotVehicleReferenceScope separateStrict(true);
    for(int caseIndex=0;caseIndex<(extraHistory?1:2);++caseIndex) {
        const int side=extraHistory?240:(caseIndex?256:240);
        const int history=extraHistory?10000:0;
        const RuntimeCellPoint goal{side/2+22,side/2+48};
        std::array<std::unique_ptr<RuntimeWindowState>,3> states;
        std::array<std::vector<int>,3> ids;
        for(int branch=0;branch<3;++branch) {
            states[branch]=std::make_unique<RuntimeWindowState>();
            if(!prepareNative(*states[branch],*assets,config,side,history,false,ids[branch],error))return false;
            if(extraHistory) {
                auto& s=*states[branch];const auto& objects=s.world.getObjects();
                const auto live=std::count_if(objects.begin(),objects.end(),[](const auto& actor){return actor.alive;});
                const auto genuineHistory=std::count_if(objects.begin(),objects.end(),[](const auto& actor){
                    return !actor.alive && actor.health==0 && actor.kind==WorldObjectKind::Unit
                        && actor.type=="MTNK" && actor.owner=="Americans" && actor.mission=="Destroyed";});
                if(objects.size()!=10057 || live!=57 || genuineHistory!=10000 || ids[branch].size()!=48)
                    return fail("extra history real10000/10057/57/48 population prerequisite");
                ++counts.initialPopulationReadbacks;
                constexpr std::array<const char*,12> types{{"MTNK","SREF","TTNK","FV","SCHP","HTNK","E1","CLEG","JUMPJET","SNIPE","GHOST","E2"}};
                std::array<std::size_t,12> roster{};
                for(int id:ids[branch]) {
                    const auto* actor=findObjectById(s,id);
                    if(!actor || !actor->alive || actor->health<=0)return fail("extra history actual living selection");
                    const auto type=std::find(types.begin(),types.end(),actor->type);
                    if(type==types.end())return fail("extra history actual stock type");
                    ++roster[static_cast<std::size_t>(type-types.begin())];
                }
                if(!std::all_of(roster.begin(),roster.end(),[](std::size_t n){return n==4;}))
                    return fail("extra history four of each twelve normally decoded stock types");
                counts.stockTypeReadbacks+=types.size();
            }
        }
        for(int tick=1;tick<=64;++tick) {
            std::array<Authority,3> trajectories,full;
            std::array<std::map<int,RuntimeCellPoint>,3> destinations;
            std::array<std::pair<int,int>,3> acceptedRejected{};
            std::vector<std::pair<int,int>> originalOrder;
            for(int branch=0;branch<3;++branch) {
                auto& s=*states[branch];s.tick=tick;RuntimeSnapshotActiveStateScope active(s);
                RuntimeMixedLocalComponentReferenceScope fullReference(branch==0);
                RuntimeMixedKnownUnmappedReferenceScope c37Reference(branch!=2);
                if(tick==60) {
                    auto& info=s.terrainInfoByCell.at(runtimeCellKey(goal.x,goal.y));
                    info.passable=true;info.water=true;info.shore=info.bridge=info.ramp=info.cliff=false;
                    info.authoredLandType=info.movementLandType=RuntimeTerrainLandType::Water;
                    // Explicit shared fixture counterexample, never a production
                    // rule change: an amphibious GHOST may use this water cell,
                    // while the representative Ground MTNK may not.
                    auto* water=const_cast<LandType*>(s.rules.findLandType("Water"));
                    if(!water)return fail("actual Water rule");water->foot=100;
                    s.pathTerrainCache.valid=s.pathTerrainCache.dynamicValid=false;
                }
                if(tick==61) {
                    std::unordered_set<int> moving;
                    for(int id:ids[branch]) {const auto* actor=findObjectById(s,id);
                        if(actor && runtimeObjectCanReceiveMoveCommand(s,*actor)
                            && runtimeObjectAllowsMoveToCell(s,*actor,goal))moving.insert(id);}
                    const auto occupied=buildRuntimeOccupancyLookup(s,-1,nullptr,&moving);
                    const auto* root=findObjectById(s,ids[branch].front());
                    const auto ghost=std::find_if(s.world.getObjects().begin(),s.world.getObjects().end(),
                        [](const auto& actor){return actor.alive && actor.type=="GHOST";});
                    if(!root || root->type!="MTNK" || ghost==s.world.getObjects().end()
                        || runtimeCellIsPassable(s,s.terrainByCell,occupied,goal,*root)
                        || !runtimeCellIsPassable(s,s.terrainByCell,occupied,goal,*ghost))return fail("real Ground/GHOST different predicates");
                    if(branch==0) {
                        const auto map=buildRuntimeMoveComponentByCell(s,*root,s.terrainByCell,occupied);
                        originalOrder.assign(map.begin(),map.end());++counts.originalMaps;
                        if(map.contains(runtimeCellKey(goal.x,goal.y)))return fail("original water key must be absent");
                    }
                    if(branch==2) {
                        const std::unordered_map<int,int> map(originalOrder.begin(),originalOrder.end());
                        const auto rootLabel=map.find(runtimeCellKey(root->x,root->y));
                        RuntimeMixedLocalComponentCertificate proof;RuntimeMixedLocalComponentStats values;
                        {RuntimeMixedLocalComponentStatsScope observer(values);
                            if(!proof.prepare(s,ids[branch],goal,s.terrainByCell,occupied,root->instanceId,moving.size()))return fail("new actual certificate admission");}
                        if(rootLabel==map.end() || !proof.coversBest(goal) || !proof.bestUnmappedFilter()
                            || !proof.knownUnmappedKeys().contains(runtimeCellKey(goal.x,goal.y)))return fail("published actual missing key/frontier");
                        for(int key:proof.knownUnmappedKeys()) {
                            if(map.contains(key))return fail("every actual published absent key checked against complete original map");
                            ++counts.publishedChecked;
                        }
                        for(const auto& [key,bits]:proof.certifiedKeys()) {(void)bits;
                            const auto found=map.find(key);
                            if(found==map.end() || found->second!=rootLabel->second)return fail("actual mapped certificate labels");}
                        // The first unused formation centre is deliberately not
                        // prelisted. A complete original frontier must already
                        // be in the frozen SCC/absence sets before it is accepted.
                        const auto extraOffset=formationOffsetForIndex(static_cast<int>(ids[branch].size()),1);
                        const RuntimeCellPoint extra{goal.x+extraOffset.x,goal.y+extraOffset.y};
                        if(RuntimeMixedLocalKnownUnmappedContractAccess::listed(proof,extra))return fail("closure centre must be unlisted");
                        const auto extraProbes=RuntimeMixedLocalComponentCertificate::probePointsForContract(extra);
                        if(extraProbes.empty())return fail("closure complete original probe schedule");
                        for(const auto& point:extraProbes) {
                            const int key=runtimeCellKey(point.x,point.y);
                            if(!proof.certifiedKeys().contains(key) && !proof.knownUnmappedKeys().contains(key))
                                return fail("closure positive requires every preproved frontier point");
                            const auto label=map.find(key);
                            if(proof.knownUnmappedKeys().contains(key)?label!=map.end()
                                :label==map.end() || label->second!=rootLabel->second)
                                return fail("closure frozen membership against complete original map");
                        }
                        ++counts.closureCentres;
                        const auto beforeWorld=s.world.deterministicSimulationStateHash();
                        const auto beforeSeeds=values.unmappedAttempts,beforeIncoming=values.incomingQueries;
                        const auto beforeWalk=values.walkSteps,beforeEdges=values.orientedEdges;
                        {RuntimeMixedBestFrontierClosureReferenceScope reference(true);
                            if(proof.coversBest(extra))return fail("closure independent reference veto");}
                        ++counts.closureReferenceVetoes;
                        const auto previousLimit=RuntimeMixedLocalKnownUnmappedContractAccess::setClosureLimit(proof,0);
                        if(proof.coversBest(extra))return fail("closure zero-budget veto");
                        RuntimeMixedLocalKnownUnmappedContractAccess::setClosureLimit(proof,previousLimit);
                        ++counts.closureBudgetVetoes;
                        if(!proof.coversBest(extra) || values.frontierClosureAdmissions!=1
                            || values.frontierClosurePoints!=extraProbes.size())return fail("closure complete membership admission");
                        ++counts.closureMembershipAdmissions;
                        const auto points=values.frontierClosurePoints;
                        if(!proof.coversBest(extra) || values.frontierClosurePoints!=points
                            || values.frontierClosureMemoHits!=1)return fail("closure memo does not rerun schedule");
                        ++counts.closureMemoWitnesses;
                        {RuntimeMixedBestFrontierClosureReferenceScope reference(true);
                            if(proof.coversBest(extra))return fail("closure reference also fences successful memo");}
                        ++counts.closureReferenceVetoes;
                        if(proof.coversBest({goal.x+32,goal.y+32}))return fail("closure incomplete frontier veto");
                        ++counts.closureIncompleteVetoes;
                        if(proof.coversBest({-1,goal.y}))return fail("closure noncanonical veto");
                        ++counts.closureCanonicalVetoes;
                        if(beforeWorld!=s.world.deterministicSimulationStateHash() || beforeSeeds!=values.unmappedAttempts
                            || beforeIncoming!=values.incomingQueries || beforeWalk!=values.walkSteps
                            || beforeEdges!=values.orientedEdges)return fail("closure adds no graph queries or world writes");
                        ++counts.closureFrozenChecks;
                        const auto original=findBestRuntimeFormationTargetCellWithOccupancy(s,*ghost,goal,-1,nullptr,
                            s.terrainByCell,occupied,nullptr,&map,rootLabel->second);
                        const auto masked=findBestRuntimeFormationTargetCellWithOccupancy(s,*ghost,goal,-1,nullptr,
                            s.terrainByCell,occupied,nullptr,nullptr,0,nullptr,proof.bestUnmappedFilter());
                        const auto bypass=findBestRuntimeFormationTargetCellWithOccupancy(s,*ghost,goal,-1,nullptr,
                            s.terrainByCell,occupied,nullptr,nullptr,-1,nullptr,proof.bestUnmappedFilter());
                        if(!original || original!=masked || *original==goal || !bypass || *bypass!=goal)
                            return fail("original positive-component rejection and negative-component bypass");
                        counts.filterChecks+=3;++counts.ghostReadbacks;
                        {RuntimeMixedKnownUnmappedReferenceScope reference(true);
                            if(proof.coversBest(goal) || proof.coversBest(extra) || proof.bestUnmappedFilter())return fail("reference fences published absence");}
                        ++s.tick;if(proof.coversBest(goal) || proof.coversBest(extra) || proof.bestUnmappedFilter())return fail("tick fences published absence");--s.tick;
                        proof.invalidate();if(proof.coversBest(goal) || proof.coversBest(extra) || proof.bestUnmappedFilter())return fail("invalidate fences published absence");
                    }
                    RuntimeMixedLocalComponentStats values;
                    {RuntimeMixedLocalComponentStatsScope observer(values,&originalOrder);
                        if(!issueRuntimeMassMovementOrder(s,ids[branch],goal,destinations[branch],error,true))return false;}
                    if(values.orderedMapMismatches || (values.wholeMaps && values.orderedMapsCompared!=values.wholeMaps))return fail("ordered complete fallback");
                    if(branch==2) {
                        // side240 is the independently observed unlisted-centre
                        // command. side256 can legitimately need no new closure;
                        // all of its absence/no-whole-map/output assertions stay.
                        if(values.admitted!=1 || !values.unmappedProofs || !values.incomingQueries
                            || (side==240 && !values.frontierClosureAdmissions) || values.wholeMaps
                            || values.unmappedOriginalChecks!=values.unmappedProofs || values.unmappedOriginalMismatches)
                            return fail("new actual command must prove absence and avoid whole map"
                                " caseIndex="+std::to_string(caseIndex)+" side="+std::to_string(side)
                                +" tick="+std::to_string(tick)+" branch="+std::to_string(branch)
                                +" selected="+std::to_string(ids[branch].size())
                                +" accepted="+std::to_string(s.lastCommand.acceptedCount)
                                +" rejected="+std::to_string(s.lastCommand.rejectedCount)
                                +" destinations="+std::to_string(destinations[branch].size())
                                +" attempts="+std::to_string(values.attempts)
                                +" admitted="+std::to_string(values.admitted)
                                +" unmappedProofs="+std::to_string(values.unmappedProofs)
                                +" incomingQueries="+std::to_string(values.incomingQueries)
                                +" wholeMaps="+std::to_string(values.wholeMaps)
                                +" originalChecks="+std::to_string(values.unmappedOriginalChecks)
                                +" originalMismatches="+std::to_string(values.unmappedOriginalMismatches)
                                +" outsideFallbacks="+std::to_string(values.outsideFallbacks)
                                +" nearestFallbacks="+std::to_string(values.nearestFallbacks)
                                +" lateWholeMaps="+std::to_string(values.lateWholeMaps)
                                +" rejectionReason="+std::to_string(values.rejectionReason)
                                +" rejectionActor="+std::to_string(values.rejectionActor)
                                +" referenceFallbacks="+std::to_string(values.referenceFallbacks)
                                +" guardFallbacks="+std::to_string(values.guardFallbacks)
                                +" layoutFallbacks="+std::to_string(values.layoutFallbacks)
                                +" walkFallbacks="+std::to_string(values.walkFallbacks)
                                +" allocationFallbacks="+std::to_string(values.allocationFallbacks)
                                +" coveredBest="+std::to_string(values.coveredBest)
                                +" certifiedPoints="+std::to_string(values.vertices)
                                +" unmappedAttempts="+std::to_string(values.unmappedAttempts)
                                +" unmappedMemoHits="+std::to_string(values.unmappedMemoHits)
                                +" incomingMemoHits="+std::to_string(values.incomingMemoHits)
                                +" incomingPositiveVetoes="+std::to_string(values.incomingPositiveVetoes)
                                +" orderedMapsCompared="+std::to_string(values.orderedMapsCompared)
                                +" orderedMapMismatches="+std::to_string(values.orderedMapMismatches)
                                +" lateChronoWitness="+std::to_string(values.lateChronoWitness)
                                +" lateAirWitness="+std::to_string(values.lateAirWitness)
                                +" frontierClosureAttempts="+std::to_string(values.frontierClosureAttempts)
                                +" frontierClosureAdmissions="+std::to_string(values.frontierClosureAdmissions)
                                +" frontierClosureRejected="+std::to_string(values.frontierClosureRejected)
                                +" frontierClosurePoints="+std::to_string(values.frontierClosurePoints)
                                +" frontierClosureMemoHits="+std::to_string(values.frontierClosureMemoHits)
                                +" frontierClosureAllocationFallbacks="+std::to_string(values.frontierClosureAllocationFallbacks));
                        ++counts.admittedCommands;counts.actualUnmappedProofs+=values.unmappedProofs;
                        counts.actualIncomingQueries+=values.incomingQueries;
                        counts.actualPublishedChecked+=values.unmappedOriginalChecks;
                        counts.actualClosureAdmissions+=values.frontierClosureAdmissions;
                        counts.actualClosurePoints+=values.frontierClosurePoints;
                    } else if(values.wholeMaps!=1 || values.admitted)return fail("full/C37 routes genuinely build original map");
                    acceptedRejected[branch]={s.lastCommand.acceptedCount,s.lastCommand.rejectedCount};++counts.commands;
                }
                (void)advanceRuntimeSimulation(s);
                trajectories[branch]=captureTrajectory(s);
                if(tick>=60 && !capture(s,full[branch],error))return false;
                if(extraHistory && tick==64) {
                    const auto& objects=s.world.getObjects();
                    const auto live=std::count_if(objects.begin(),objects.end(),[](const auto& actor){return actor.alive;});
                    const auto genuineHistory=std::count_if(objects.begin(),objects.end(),[](const auto& actor){
                        return !actor.alive && actor.health==0 && actor.kind==WorldObjectKind::Unit
                            && actor.type=="MTNK" && actor.owner=="Americans" && actor.mission=="Destroyed";});
                    if(objects.size()!=10057 || live!=57 || genuineHistory!=10000 || ids[branch].size()!=48)
                        return fail("extra history completed population must remain real10000/10057/57/48");
                    for(int id:ids[branch]) {const auto* actor=findObjectById(s,id);
                        if(!actor || !actor->alive || actor->health<=0)return fail("extra history completed army48");}
                    ++counts.finalPopulationReadbacks;
                }
            }
            if(trajectories[0]!=trajectories[1] || trajectories[0]!=trajectories[2])return fail("all completed trajectories");
            ++counts.trajectoryTriplets;
            if(tick<=60)++counts.warmTicks;
            if(tick>=60) {
                if(full[0]!=full[1] || full[0]!=full[2])return fail("completed full writer triplet");
                if(tick==60)++counts.preSaves;else ++counts.fullSaves;
            }
            if(tick==61 && (destinations[0]!=destinations[1] || destinations[0]!=destinations[2]
                || acceptedRejected[0]!=acceptedRejected[1] || acceptedRejected[0]!=acceptedRejected[2]))return fail("actual command destinations/counts");
        }
        ++counts.nativeCases;
    }
    return true;
}
}
bool runRuntimeMixedLocalKnownUnmappedContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeMixedLocalKnownUnmappedContractDetail;
    RuntimeMixedKnownUnmappedReferenceScope optimized(false);
    RuntimeMixedBestFrontierClosureReferenceScope closureDefault(false);
    RuntimeMixedLocalKnownUnmappedContractDetail::Counts counts;
    if(!leaf(config,counts,error) || !native(config,counts,error))return false;
    if(counts.boundaries!=14 || counts.originalMaps!=15 || counts.seedOracles!=12 || counts.incomingOracles!=96
        || counts.memoWitnesses!=1 || counts.crushVetoes!=2 || counts.filterChecks!=6 || counts.nativeCases!=2
        || counts.commands!=6 || counts.admittedCommands!=2 || counts.warmTicks!=120
        || counts.preSaves!=2 || counts.fullSaves!=8 || counts.trajectoryTriplets!=128 || counts.ghostReadbacks!=2
        || !counts.publishedChecked || !counts.actualUnmappedProofs || !counts.actualIncomingQueries
        || counts.actualPublishedChecked!=counts.actualUnmappedProofs) {
        error="known unmapped: exact coverage boundaries="+std::to_string(counts.boundaries)
            +" maps="+std::to_string(counts.originalMaps)+" seeds="+std::to_string(counts.seedOracles)
            +" incoming="+std::to_string(counts.incomingOracles)+" published="+std::to_string(counts.publishedChecked)
            +" commands="+std::to_string(counts.commands)+" admitted="+std::to_string(counts.admittedCommands)
            +" saves="+std::to_string(counts.fullSaves);return false;
    }
    std::cout<<"Mixed local known-unmapped certificate: boundaries=14 originalWholeMaps=15 seedOracles=12 incomingOracles=96 memoWitnesses=1 crushVetoes=2 filterChecks=6 nativeCases=2 nativeCommands=6 admittedCommands=2 warmupTicks=120 preCommandFullSaveTriplets=2 completedFullSaveTriplets=8 completedTrajectoryTriplets=128 ghostPredicateReadbacks=2 publishedKeysChecked="
        <<counts.publishedChecked<<" actualUnmappedProofs="<<counts.actualUnmappedProofs<<" actualIncomingQueries="<<counts.actualIncomingQueries
        <<" actualPublishedKeysChecked="<<counts.actualPublishedChecked
        <<" seedFalse/eightIncomingFalse/independentDirectedCache/BestComponentMask/negativeComponentBypass/native240/native256/originalMaps/commands/paths/fullWriter=exact passed=yes\n";
    RuntimeMixedLocalKnownUnmappedContractDetail::Counts historyCounts;
    if(!native(config,historyCounts,error,true))return false;
    if(historyCounts.nativeCases!=1 || historyCounts.originalMaps!=1 || historyCounts.filterChecks!=3
        || historyCounts.commands!=3 || historyCounts.admittedCommands!=1 || historyCounts.warmTicks!=60
        || historyCounts.preSaves!=1 || historyCounts.fullSaves!=4 || historyCounts.trajectoryTriplets!=64
        || historyCounts.ghostReadbacks!=1 || historyCounts.initialPopulationReadbacks!=3
        || historyCounts.finalPopulationReadbacks!=3 || historyCounts.stockTypeReadbacks!=36
        || !historyCounts.publishedChecked || !historyCounts.actualUnmappedProofs || !historyCounts.actualIncomingQueries
        || historyCounts.actualPublishedChecked!=historyCounts.actualUnmappedProofs
        || historyCounts.closureCentres!=1 || historyCounts.closureReferenceVetoes!=2
        || historyCounts.closureMembershipAdmissions!=1 || historyCounts.closureMemoWitnesses!=1
        || historyCounts.closureIncompleteVetoes!=1 || historyCounts.closureCanonicalVetoes!=1
        || historyCounts.closureBudgetVetoes!=1 || historyCounts.closureFrozenChecks!=1
        || !historyCounts.actualClosureAdmissions || historyCounts.actualClosurePoints!=252*historyCounts.actualClosureAdmissions) {
        error="Best frontier closure history: exact coverage commands="+std::to_string(historyCounts.commands)
            +" admitted="+std::to_string(historyCounts.admittedCommands)
            +" initialReadbacks="+std::to_string(historyCounts.initialPopulationReadbacks)
            +" completedReadbacks="+std::to_string(historyCounts.finalPopulationReadbacks)
            +" preSaves="+std::to_string(historyCounts.preSaves)+" fullSaves="+std::to_string(historyCounts.fullSaves)
            +" trajectories="+std::to_string(historyCounts.trajectoryTriplets)
            +" closureAdmissions="+std::to_string(historyCounts.actualClosureAdmissions)
            +" closurePoints="+std::to_string(historyCounts.actualClosurePoints);return false;
    }
    std::cout<<"Mixed local Best frontier closure H10000 coverage: nativeCases=1 nativeWorldObjects=10057 nativeAliveObjects=57 finalArmy=48 initialPopulationReadbacks=3 finalPopulationReadbacks=3 stockTypeReadbacks=36 nativeCommands=3 admittedCommands=1 warmupTicks=60 preCommandFullSaveTriplets=1 completedFullSaveTriplets=4 completedTrajectoryTriplets=64 directOriginalWholeMaps=1 publishedKeysChecked="
        <<historyCounts.publishedChecked<<" actualUnmappedProofs="<<historyCounts.actualUnmappedProofs
        <<" actualIncomingQueries="<<historyCounts.actualIncomingQueries
        <<" actualPublishedKeysChecked="<<historyCounts.actualPublishedChecked
        <<" actualClosureAdmissions="<<historyCounts.actualClosureAdmissions<<" actualClosurePoints="<<historyCounts.actualClosurePoints
        <<" original/reference/default/native240/H10000/stock12types/absence/wholeMaps0/commands/paths/fullWriter=exact passed=yes\n";
    // Retain every original two-case direct control. Only their former demand
    // for TWO real closure users changes: side256 may use prelisted centres,
    // while the added independent H10000 command must supply another real user.
    const auto actualClosureAdmissions=counts.actualClosureAdmissions+historyCounts.actualClosureAdmissions;
    const auto actualClosurePoints=counts.actualClosurePoints+historyCounts.actualClosurePoints;
    if(counts.closureCentres!=2 || counts.closureReferenceVetoes!=4 || counts.closureMembershipAdmissions!=2
        || counts.closureMemoWitnesses!=2 || counts.closureIncompleteVetoes!=2 || counts.closureCanonicalVetoes!=2
        || counts.closureBudgetVetoes!=2 || counts.closureFrozenChecks!=2
        || !counts.actualClosureAdmissions || counts.actualClosurePoints!=252*counts.actualClosureAdmissions
        || actualClosureAdmissions<2 || actualClosurePoints!=252*actualClosureAdmissions) {
        error="Best frontier closure: exact controls/default command witness counts baselineAdmissions="
            +std::to_string(counts.actualClosureAdmissions)+" historyAdmissions="+std::to_string(historyCounts.actualClosureAdmissions)
            +" combinedAdmissions="+std::to_string(actualClosureAdmissions)+" combinedPoints="+std::to_string(actualClosurePoints);return false;
    }
    std::cout<<"Mixed local Best frontier closure: nativeCases=3 unlistedCentres=3 referenceVetoes=6 membershipAdmissions=3 memoWitnesses=3 incompleteFrontierVetoes=3 canonicalVetoes=3 zeroBudgetVetoes=3 frozenProofChecks=3 actualCommandAdmissions=3 actualClosureAdmissions="
        <<actualClosureAdmissions<<" actualClosurePoints="<<actualClosurePoints
        <<" baselineActualClosureAdmissions="<<counts.actualClosureAdmissions
        <<" historyActualClosureAdmissions="<<historyCounts.actualClosureAdmissions
        <<" predicateQueriesAdded=0 wholeMaps=0 originalMasks/completeFrontiers/frozenMembership/memo/reference/budget/native240/native256/H10000/commands/paths/fullWriter=exact passed=yes\n";
    return true;
}
