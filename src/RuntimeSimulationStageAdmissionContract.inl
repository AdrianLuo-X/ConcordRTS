#pragma once
#include "RuntimeSimulationStageAdmissionLegacySubcell.inl"
#include "RuntimeSimulationStageAdmissionLegacyAircraft.inl"
#include "RuntimeSimulationStageAdmissionLegacyShell.inl"

namespace RuntimeSimulationStageAdmissionContractDetail {
struct Observation {
    std::array<std::size_t,4> calls{}, admitted{}, omitted{}, originalReserve{}, selectedReserve{};
    std::size_t references=0;
    static void append(const RuntimeSimulationStageAdmissionStats& s,void* raw) noexcept {
        auto& o=*static_cast<Observation*>(raw);const auto i=static_cast<std::size_t>(s.kind);
        ++o.calls[i];o.admitted[i]+=s.admitted;o.omitted[i]+=s.omittedRangeEntries;
        o.originalReserve[i]+=s.originalReserveEntries;o.selectedReserve[i]+=s.selectedReserveEntries;
        o.references+=s.reference;
    }
};
WorldObjectInstance object(int id,WorldObjectKind kind,const char* type,int x,int y) {
    auto o=RuntimePlannerPassabilityContractDetail::object(id,kind,type,x,y);
    o.poweredOperational=true;return o;
}
bool sameReport(const RuntimeSubcellReconcileReport& a,const RuntimeSubcellReconcileReport& b) {
    if(std::tie(a.retainedInfantry,a.reassignedInfantry,a.needsMoveOut,a.droppedReservations)
        !=std::tie(b.retainedInfantry,b.reassignedInfantry,b.needsMoveOut,b.droppedReservations)
        || a.actions.size()!=b.actions.size())return false;
    for(std::size_t i=0;i<a.actions.size();++i){const auto& x=a.actions[i];const auto& y=b.actions[i];
        if(std::tie(x.kind,x.objectId,x.location.x,x.location.y,x.location.cell,x.location.onBridge,x.previousSubCell,x.subCell,x.reason)
            !=std::tie(y.kind,y.objectId,y.location.x,y.location.y,y.location.cell,y.location.onBridge,y.previousSubCell,y.subCell,y.reason))return false;}
    return true;
}
bool sameReport(const RuntimeAircraftServiceReconcileReport& a,const RuntimeAircraftServiceReconcileReport& b) {
    return std::tie(a.contextValid,a.retainedHosts,a.addedHosts,a.removedHosts,a.resetHosts,
        a.retainedBindings,a.addedBindings,a.removedBindings,a.duplicateDockBindings,a.duplicateAircraftBindings,
        a.skippedHostsByLimit,a.skippedBindingsByLimit)
        ==std::tie(b.contextValid,b.retainedHosts,b.addedHosts,b.removedHosts,b.resetHosts,
        b.retainedBindings,b.addedBindings,b.removedBindings,b.duplicateDockBindings,b.duplicateAircraftBindings,
        b.skippedHostsByLimit,b.skippedBindingsByLimit);
}
bool sameActions(const std::vector<RuntimeAircraftServiceAction>& a,const std::vector<RuntimeAircraftServiceAction>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){const auto& x=a[i];const auto& y=b[i];
        if(std::tie(x.kind,x.hostId,x.aircraftId,x.dockIndex,x.eventTick,x.owner,x.ammoBefore,x.ammoAfter,
            x.healthBefore,x.healthAfter,x.healedHitPoints,x.creditCost,x.reason)
            !=std::tie(y.kind,y.hostId,y.aircraftId,y.dockIndex,y.eventTick,y.owner,y.ammoBefore,y.ammoAfter,
            y.healthBefore,y.healthAfter,y.healedHitPoints,y.creditCost,y.reason))return false;}
    return true;
}
template<class State> std::string aircraftSave(const State& s){std::ostringstream out;s.save(out);return out.str();}
bool reservePrimitive(std::string& error) {
    const auto fail=[&](const std::string& why){error="simulation stage admission reserve: "+why;return false;};
    std::size_t pairs=0,growth=0;
    for(std::size_t n:{std::size_t{0},std::size_t{1},std::size_t{255},std::size_t{256},std::size_t{257},std::size_t{4096},std::size_t{10057},std::size_t{20000}}) {
        std::array<std::vector<int>,2> ordered;std::array<std::unordered_set<int>,2> membership;
        for(int r=0;r<2;++r){const auto hint=runtimeSimulationStageInitialReserve(n,r==0);
            if(hint!=(r==0?n:std::min(n,std::size_t{256})))return fail("exact eager hint");
            ordered[r].reserve(hint);membership[r].reserve(hint);
            for(std::size_t i=0;i<n;++i){ordered[r].push_back(static_cast<int>(i));membership[r].insert(static_cast<int>(i));}}
        if(ordered[0]!=ordered[1] || membership[0]!=membership[1] || ordered[1].size()!=n || membership[1].size()!=n)return fail("growth must retain every ordered value/membership");
        ++pairs;growth+=n>256;
    }
    const bool previous=runtimeSimulationStageAdmissionReference();
    {RuntimeSimulationStageAdmissionReferenceScope outer(false);if(runtimeSimulationStageAdmissionReference())return fail("outer reference false");
        {RuntimeSimulationStageAdmissionReferenceScope inner(true);if(!runtimeSimulationStageAdmissionReference())return fail("nested reference true");}
        if(runtimeSimulationStageAdmissionReference())return fail("nested reference restoration");}
    if(runtimeSimulationStageAdmissionReference()!=previous || pairs!=8 || growth!=4)return fail("reserve/reference coverage");
    std::cout<<"Simulation stage admission reserve: sizes=8 completePairs=8 growthCases=4 eagerCap=256 nestedReferenceRestore=yes orderedValues/membership/unlimitedGrowth=exact passed=yes\n";
    return true;
}
bool subcellPrimitive(std::string& error) {
    const auto fail=[&](const std::string& why){error="simulation stage admission subcell: "+why;return false;};
    std::size_t comparisons=0,positive=0,negative=0;
    for(int variant=0;variant<24;++variant) {
        std::vector<WorldObjectInstance> original{
            object(1,WorldObjectKind::Infantry,"E1",5,5),object(2,WorldObjectKind::Infantry,"E1",5,5),
            object(3,WorldObjectKind::Infantry,"E1",5,5),object(4,WorldObjectKind::Unit,"MTNK",9,9),
            object(5,WorldObjectKind::Structure,"GATE",6,5),object(6,WorldObjectKind::Unit,"HTK",12,12)};
        original[2].subCell=0;original[5].alive=false;original[5].transportPassengerIds={3};
        std::vector<WorldTerrainObject> terrain;RuntimeSubcellContext context;
        switch(variant) {
        case 1:original[5].transportPassengerIds.clear();break;
        case 2:original[0].health=0;break;
        case 3:original[1].airborne=true;break;
        case 4:original[1].mission="InsideTransport";break;
        case 5:original[1].garrisonHostObjectId=5;break;
        case 6:original[1].onBridge=true;break;
        case 7:original[1].x=original[1].y=-1;original[1].cell=runtimeCellKey(5,5);break;
        case 8:original[1].x=original[1].y=original[1].cell=-1;break;
        case 9:original[1].instanceId=1;break;
        case 10:original[1].instanceId=-7;break;
        case 11:std::reverse(original.begin(),original.end());break;
        case 12:context.ignoredWholeCellObjectIds={5,4};break;
        case 13:original[4].x=original[4].y=5;context.ignoredWholeCellObjectIds={5};break;
        case 14:original[3].x=original[3].y=5;context.ignoredWholeCellObjectIds={4};break;
        case 15:original[5].transportPassengerIds={1,1,-7,3};break;
        case 16:original[0].subCell=4;original[1].subCell=3;break;
        case 17:original[0].alive=false;break;
        case 18:original[5].transportPassengerIds={2};original[5].health=0;break;
        case 19:original[1].mission="Garrisoned";break;
        case 20:original[1].mission="Occupied";break;
        case 21:original[1].subCell=-1;break;
        case 22:for(int i=0;i<10000;++i){auto dead=object(7+i,WorldObjectKind::Unit,"MTNK",2,2);dead.alive=false;dead.health=0;original.push_back(std::move(dead));}break;
        case 23:original.push_back(original[0]);break;
        default:break;
        }
        if(variant>=12 && variant<=16){WorldTerrainObject t;t.alive=true;t.health=100;t.x=5;t.y=5;
            t.footprintWidth=2;t.footprintHeight=2;t.occupationBits=variant==16?7:0;t.occupiedSubCells={2};terrain.push_back(t);}
        std::array<std::vector<WorldObjectInstance>,3> objects{original,original,original};
        // This contract compares reconciliation to the frozen original. Give
        // allocation a separate slot-2 input, so the new slot-preserving policy
        // does not alter its reservation precondition. Reconcile the untouched
        // original data below, including invalid/duplicate/source-4/3 slots.
        auto allocationObjects=original;
        for(auto& mover:allocationObjects)
            if(mover.kind==WorldObjectKind::Infantry && !mover.airborne) mover.subCell=2;
        RuntimeSimulationStageAdmissionLegacySubcell::RuntimeSubcellSystemState legacy;
        std::array<RuntimeSubcellSystemState,2> states;
        const std::vector<RuntimeSubcellMoveRequest> requests{{1,{8,8,runtimeCellKey(8,8),false}},
            {2,{8,8,runtimeCellKey(8,8),false}},{3,{8,8,runtimeCellKey(8,8),true}}};
        (void)legacy.reserveBatch(allocationObjects,terrain,requests,context);
        for(int branch=0;branch<2;++branch)(void)states[branch].reserveBatch(allocationObjects,terrain,requests,context);
        const auto expected=legacy.reconcile(objects[0],terrain,context);
        GameWorld oldWorld;oldWorld.accessObjects()=objects[0];oldWorld.accessTerrainObjects()=terrain;
        for(int branch=0;branch<2;++branch) {
            Observation observation;RuntimeSubcellReconcileReport actual;
            {RuntimeSimulationStageAdmissionReferenceScope reference(branch==0);
                RuntimeSimulationStageAdmissionObserverScope observe(&Observation::append,&observation);
                actual=states[branch].reconcile(objects[branch+1],terrain,context);}
            GameWorld actualWorld;actualWorld.accessObjects()=objects[branch+1];actualWorld.accessTerrainObjects()=terrain;
            if(!sameReport(expected,actual) || legacy.stateHash(objects[0],terrain)!=states[branch].stateHash(objects[branch+1],terrain)
                || oldWorld.deterministicSimulationStateHash()!=actualWorld.deterministicSimulationStateHash())
                return fail("frozen full original data/report/actions/reservations variant="+std::to_string(variant));
            if(observation.calls[1]!=1 || observation.admitted[1]!=(branch==1?1U:0U)
                || observation.omitted[1]!=(branch==1?original.size()*2:0))return fail("real fusion/reference range witness");
            ++comparisons;positive+=actual.reassignedInfantry>0;negative+=actual.needsMoveOut>0;
        }
    }
    if(comparisons!=48 || !positive || !negative)return fail("complete primitive coverage totals");
    std::cout<<"Simulation stage admission subcell: cases=24 legacyComparisons=48 reassignmentWitness="<<positive
        <<" needsMoveOutWitness="<<negative<<" deadCargo/duplicates/negativeIDs/health0/air/mission/bridge/gate/terrain/reservations/fullData/actions/order=exact passed=yes\n";
    return true;
}
bool aircraftPrimitive(std::string& error) {
    const auto fail=[&](const std::string& why){error="simulation stage admission aircraft: "+why;return false;};
    IniFile ini;RulesDatabase rules;
    if(!ini.loadFromString("[General]\nPadAircraft=JET\nReloadRate=.0001\nRepairPercent=10%\nRepairStep=10\n[BuildingTypes]\n0=FIELD\n[AircraftTypes]\n0=JET\n[FIELD]\nStrength=1000\nUnitReload=yes\nNumberOfDocks=2\n[JET]\nStrength=100\nAmmo=4\nInitialAmmo=1\nManualReload=yes\nReloadRate=.0001\nRepairable=yes\nAirportBound=yes\nDock=FIELD\nSpeed=10\n") || !rules.loadFromIni(ini))return fail("original real INI parser");
    std::size_t comparisons=0,noHost=0,callbacks=0,actions=0;
    for(int variant=0;variant<16;++variant){
        std::vector<WorldObjectInstance> objects{object(1,WorldObjectKind::Structure,"FIELD",5,5),
            object(2,WorldObjectKind::Unit,"JET",5,5),object(3,WorldObjectKind::Unit,"JET",5,5)};
        for(int i:{1,2}){objects[i].authoredAircraft=true;objects[i].currentAmmo=1;objects[i].health=60;
            objects[i].dockedToObjectId=1;objects[i].dockIndex=i-1;}
        std::array<std::vector<std::string>,3> calls;
        std::array<RuntimeAircraftServiceContext,3> context;
        for(int r=0;r<3;++r){context[r].rules=&rules;context[r].objects=&objects;
            context[r].areAllied=[&,r](const auto& a,const auto& b){calls[r].push_back("allied:"+a+":"+b);return true;};
            context[r].creditBalance=[&,r](const auto& owner){calls[r].push_back("credit:"+owner);return 1000;};
            context[r].canonicalOwnerId=[&,r](const auto& owner){calls[r].push_back("owner:"+owner);return owner;};
            context[r].maximumHealth=[&,r](const auto& actor){calls[r].push_back("health:"+std::to_string(actor.instanceId));return 100;};}
        RuntimeSimulationStageAdmissionLegacyAircraft::RuntimeAircraftServiceSystemState legacy;
        std::array<RuntimeAircraftServiceSystemState,2> states;
        (void)legacy.reconcile(context[0]);for(int r=0;r<2;++r)(void)states[r].reconcile(context[r+1]);
        // Change the already-bound original world to exercise stale removal.
        switch(variant){case 1:objects[0].alive=false;break;case 2:objects[0].health=0;break;
        case 3:objects[0].instanceId=-7;break;case 4:objects[0].kind=WorldObjectKind::Unit;break;
        case 5:objects[0].type="MISSING";break;case 6:objects[0].rubble=true;break;
        case 7:objects[1].dockIndex=0;objects[2].dockIndex=0;break;case 8:objects[2].instanceId=2;break;
        case 9:objects.insert(objects.begin(),objects[0]);objects.front().alive=false;break;
        case 10:objects[1].instanceId=-3;break;case 11:objects[1].alive=false;break;
        case 12:objects[1].airborne=true;break;case 13:objects[1].dockedToObjectId=999;break;
        case 14:objects[1].dockIndex=65536;break;case 15:objects.clear();break;default:break;}
        for(auto& v:calls)v.clear();
        const auto expected=legacy.reconcile(context[0]);
        const auto expectedActions=legacy.tick(context[0],10);
        for(int r=0;r<2;++r){Observation observation;RuntimeAircraftServiceReconcileReport report;std::vector<RuntimeAircraftServiceAction> actualActions;
            {RuntimeSimulationStageAdmissionReferenceScope reference(r==0);RuntimeSimulationStageAdmissionObserverScope observer(&Observation::append,&observation);
                report=states[r].reconcile(context[r+1]);actualActions=states[r].tick(context[r+1],10);}
            if(!sameReport(expected,report) || !sameActions(expectedActions,actualActions)
                || aircraftSave(legacy)!=aircraftSave(states[r]) || legacy.stateHash()!=states[r].stateHash()
                || legacy.lastAdvancedTick()!=states[r].lastAdvancedTick() || calls[0]!=calls[r+1])return fail("full original state/actions/callback order variant="+std::to_string(variant));
            if(observation.calls[0]!=2)return fail("actual reconcile and tick-reconcile calls");
            if(r==0 && observation.admitted[0])return fail("reference must keep old full candidates");
            if(r==1 && legacy.hosts().empty()){if(observation.admitted[0]!=2 || observation.omitted[0]!=objects.size()*2 || !calls[r+1].empty())return fail("noHost exact zero callbacks, retained original binding cleanup");++noHost;}
            ++comparisons;callbacks+=calls[r+1].size();actions+=actualActions.size();
        }
    }
    // The public positive-host API still executes arbitrary alliance callbacks
    // at the original point. This deliberately edits a later candidate's dock.
    std::array<std::vector<WorldObjectInstance>,2> mutableWorlds;
    std::array<RuntimeAircraftServiceContext,2> mutableContexts;
    std::array<std::vector<int>,2> mutableCalls;
    RuntimeSimulationStageAdmissionLegacyAircraft::RuntimeAircraftServiceSystemState mutableOld;
    RuntimeAircraftServiceSystemState mutableNew;
    for(int r=0;r<2;++r){mutableWorlds[r]={object(1,WorldObjectKind::Structure,"FIELD",5,5),
        object(2,WorldObjectKind::Unit,"JET",5,5),object(3,WorldObjectKind::Unit,"JET",5,5)};
        mutableWorlds[r][0].owner="HostOwner";
        for(int i:{1,2}){mutableWorlds[r][i].owner="AircraftOwner";mutableWorlds[r][i].authoredAircraft=true;mutableWorlds[r][i].dockedToObjectId=1;mutableWorlds[r][i].dockIndex=i-1;}
        mutableContexts[r].rules=&rules;mutableContexts[r].objects=&mutableWorlds[r];
        mutableContexts[r].areAllied=[&,r](const auto&,const auto&){mutableCalls[r].push_back(mutableWorlds[r][2].dockIndex);mutableWorlds[r][2].dockIndex=5;return true;};}
    const auto mutableExpected=mutableOld.reconcile(mutableContexts[0]);
    {RuntimeSimulationStageAdmissionReferenceScope reference(false);
        if(!sameReport(mutableExpected,mutableNew.reconcile(mutableContexts[1])))return fail("public mutable callback report");}
    if(mutableCalls[0]!=mutableCalls[1] || mutableCalls[0].size()!=1 || aircraftSave(mutableOld)!=aircraftSave(mutableNew))return fail("public mutable callback original sequence/save");
    if(comparisons!=32 || noHost!=7 || !callbacks || !actions)return fail("complete positive/negative primitive counts");
    std::cout<<"Simulation stage admission aircraft: cases=16 legacyComparisons=32 noHostCases=7 callbackEvents="<<callbacks
        <<" actionEvents="<<actions<<" mutableCallbackComparisons=1 staleBindings/deadFirst/negativeID/duplicates/air/dockLimits/allCallbacks/fullSave/report/actions/tickClock/order=exact passed=yes\n";
    return true;
}
bool actual(const RuntimeConfig& config,std::string& error) {
    const auto fail=[&](const std::string& why){error="simulation stage admission actual: "+why;return false;};
    using Authority=RuntimePlannerPassabilityContractDetail::Authority;
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)return fail("actual stock decoder");
    const std::array<std::array<int,3>,4> cases{{{{48,240,0}},{{48,240,10000}},{{256,256,0}},{{256,256,10000}}}};
    std::size_t fullPairs=0,trajectoryPairs=0,commands=0,genericPairs=0,noHostStockPairs=0,cloakLegacyPairs=0;
    Observation totals;
    for(const auto& setting:cases){
        std::array<std::unique_ptr<RuntimeWindowState>,2> states{std::make_unique<RuntimeWindowState>(),std::make_unique<RuntimeWindowState>()};
        std::array<std::vector<int>,2> ids;
        for(int r=0;r<2;++r)if(!RuntimeMixedLocalComponentContractDetail::prepareNative(*states[r],*assets,config,setting[1],setting[2],false,ids[r],error,setting[0]))return false;
        if(ids[0]!=ids[1])return fail("same normal decoder selection");
        for(const auto& s:states){std::size_t alive=0;for(const auto& o:s->world.getObjects())alive+=o.alive;
            if(s->world.getObjects().size()!=static_cast<std::size_t>(setting[0]+9+setting[2])
                || alive!=static_cast<std::size_t>(setting[0]+9))return fail("actual stock base/live army and genuine dead history readback");}
        std::set<std::string> types;for(int id:ids[0]){const auto* o=findObjectById(*states[0],id);if(!o)return fail("actual selected object");types.insert(o->type);}
        if(types.size()!=12)return fail("stock mixed twelve-type readback");
        std::array<Authority,2> full,trajectory;std::array<Observation,2> observed;
        std::array<bool,2> cloakChanged;
        for(int r=0;r<2;++r){auto& s=*states[r];RuntimeSnapshotActiveStateScope active(s);
            RuntimeSimulationStageAdmissionReferenceScope reference(false);
            cloakChanged[r]=r==0?legacyCloak(s):advanceRuntimeCloakState(s);
            if(!RuntimePlannerPassabilityContractDetail::capture(s,full[r],error))return false;}
        if(cloakChanged[0]!=cloakChanged[1] || full[0]!=full[1])return fail("literal original complete cloak body/full writer");
        ++cloakLegacyPairs;
        for(int tick=1;tick<=32;++tick){
            std::array<std::map<int,RuntimeCellPoint>,2> destinations;
            std::array<std::pair<int,int>,2> accepted;
            for(int r=0;r<2;++r){auto& s=*states[r];RuntimeSnapshotActiveStateScope active(s);
                RuntimeSimulationStageAdmissionReferenceScope reference(r==0);
                RuntimeSimulationStageAdmissionObserverScope observer(&Observation::append,&observed[r]);
                s.tick=tick;
                if(tick==1 || tick==17){const RuntimeCellPoint goal{setting[1]/2+22,setting[1]/2+48};
                    if(!issueRuntimeMassMovementOrder(s,ids[r],goal,destinations[r],error,true))return false;
                    accepted[r]={s.lastCommand.acceptedCount,s.lastCommand.rejectedCount};++commands;}
                // Explicit real tick-cache caller: compare contents with the
                // unchanged generic builder and literal C38 original body.
                invalidateRuntimeTickOccupancyCache(s);
                const auto& tickMap=runtimeTickOccupancyLookup(s);
                std::unordered_map<int,int> originalCounts,currentCounts;
                const auto expected=legacyOccupancy(s,-1,nullptr,nullptr,true,&originalCounts,RuntimeStructureOccupancyMode::VehicleNavigation);
                const auto generic=buildRuntimeOccupancyLookup(s,-1,nullptr,nullptr,true,&currentCounts,RuntimeStructureOccupancyMode::VehicleNavigation);
                if(tickMap!=expected || s.tickGroundOccupantCountByCell!=originalCounts || generic!=expected || currentCounts!=originalCounts
                    || generic.bucket_count()!=expected.bucket_count()
                    || std::vector<std::pair<int,int>>(generic.begin(),generic.end())!=std::vector<std::pair<int,int>>(expected.begin(),expected.end()))return fail("generic original reserve/order and tick exact values/counts");
                ++genericPairs;
                (void)advanceRuntimeSimulation(s);(void)completeRuntimeWorldRefresh(s);
                trajectory[r]=RuntimeMixedLocalComponentContractDetail::captureTrajectory(s);
                if(!RuntimePlannerPassabilityContractDetail::capture(s,full[r],error))return false;
            }
            std::string visionError;
            if(destinations[0]!=destinations[1] || accepted[0]!=accepted[1] || full[0]!=full[1] || trajectory[0]!=trajectory[1]
                || !RuntimeVisionTerrainCoordinateContractDetail::equalVision(states[1]->vision,states[0]->vision,tick,visionError))
                return fail("completed commands/path trajectories/all systems/full writer/public vision tick="+std::to_string(tick)+" "+visionError);
            ++fullPairs;++trajectoryPairs;
        }
        for(std::size_t i=0;i<4;++i){if(!observed[0].calls[i] || !observed[1].calls[i] || observed[0].admitted[i])return fail("four actual source callsites/reference routing");
            totals.calls[i]+=observed[1].calls[i];totals.admitted[i]+=observed[1].admitted[i];totals.omitted[i]+=observed[1].omitted[i];
            totals.originalReserve[i]+=observed[1].originalReserve[i];totals.selectedReserve[i]+=observed[1].selectedReserve[i];}
        if(!observed[1].admitted[1] || !observed[1].admitted[2] || !observed[1].admitted[3]
            || !observed[1].omitted[1])return fail("normal stock fusion/capped caller positive witnesses");
        if(setting[2] && (observed[1].selectedReserve[2]>=observed[1].originalReserve[2]
            || observed[1].selectedReserve[3]>=observed[1].originalReserve[3]))return fail("H10000 genuine capped initial reserve");
        // AMRADR can be a real host in the unmodified native base. Do not
        // claim the normal scene has noHost. Explicitly kill only actual
        // stock reload hosts in both authorities, then use the real caller.
        for(int r=0;r<2;++r){auto& s=*states[r];RuntimeSnapshotActiveStateScope active(s);
            for(auto& o:s.world.accessObjects())if(o.kind==WorldObjectKind::Structure){const auto* b=s.rules.findBuildingType(o.type);
                if(b && b->unitReload && b->numberOfDocks>0){o.alive=false;o.health=0;}}
            s.navigationRefreshPending=true;s.powerRefreshPending=true;(void)completeRuntimeWorldRefresh(s);
            RuntimeSimulationStageAdmissionReferenceScope reference(r==0);Observation observedNoHost;
            {RuntimeSimulationStageAdmissionObserverScope observer(&Observation::append,&observedNoHost);
                (void)advanceRuntimeAircraftServices(s);}
            if(!observedNoHost.calls[0] || observedNoHost.admitted[0]!=(r==1?observedNoHost.calls[0]:0U))return fail("separate real dead-stock-host noHost witness");
            (void)completeRuntimeWorldRefresh(s);if(!RuntimePlannerPassabilityContractDetail::capture(s,full[r],error))return false;
        }
        if(full[0]!=full[1])return fail("separate dead-stock-host full writer");++noHostStockPairs;
    }
    if(fullPairs!=128 || trajectoryPairs!=128 || commands!=16 || genericPairs!=256 || noHostStockPairs!=4 || cloakLegacyPairs!=4)return fail("strict actual coverage totals");
    std::cout<<"Simulation stage admission actual: nativeCases=4 nativeActors=608 stockTypeReadbacks=48 historyObjects=20000 completedTickFullWriterPairs=128 completedTrajectoryPairs=128 commands=16 genericOriginalOrderedPairs=256 deadStockHostFullWriterPairs=4 literalCloakFullWriterPairs=4 aircraftCalls="<<totals.calls[0]
        <<" nativeNoHostAdmissions="<<totals.admitted[0]<<" subcellCalls="<<totals.calls[1]<<" fusedCalls="<<totals.admitted[1]
        <<" cloakCalls="<<totals.calls[2]<<" tickOccupancyCalls="<<totals.calls[3]<<" cargoInfRangeSlotsOmitted="<<totals.omitted[1]
        <<" H0/H10000/native48/native256/map240/map256/mixed12/originalGenericDefault/growth/commands/paths/fullWriter=exact passed=yes\n";
    return true;
}
}
bool runRuntimeSimulationStageAdmissionContract(const RuntimeConfig& config,std::string& error) {
    for(bool reference:{true,false}) {
        RuntimeSimulationStageAdmissionReferenceScope original(reference);
        if(runRuntimeAircraftServiceIntegrationSmokeTest()!=0 || runRuntimeSubcellIntegrationSmokeTest()!=0) {
            error="simulation stage admission: original full aircraft/subcell integration contract failed";
            return false;
        }
    }
    std::cout<<"Simulation stage admission original integrations: aircraftRoutes=2 subcellRoutes=2 unchangedContracts=yes passed=yes\n";
    return RuntimeSimulationStageAdmissionContractDetail::reservePrimitive(error)
        && RuntimeSimulationStageAdmissionContractDetail::subcellPrimitive(error)
        && RuntimeSimulationStageAdmissionContractDetail::aircraftPrimitive(error)
        && RuntimeSimulationStageAdmissionContractDetail::actual(config,error);
}
