// Include after RuntimePlannerCellPassabilityContract.inl and the actual
// prepareRuntimeMassMovementWorld/spy/simulation/save helpers.
namespace RuntimeSpyDetectionNoopContractDetail {
using Action = std::tuple<RuntimeSpyActionKind,int,int,int,int,std::string,std::string,
    int,int,int,RuntimeSpyVeteranProduction,RuntimeSpySide,RuntimeSpyDisguiseIdentity,
    std::string,bool,bool,std::string>;
Action action(const RuntimeSpyAction& a) {
    return {a.kind,a.tick,a.sourceObjectId,a.targetObjectId,a.detectorObjectId,
        a.sourceOwner,a.targetOwner,a.amount,a.durationTicks,a.untilTick,
        a.veteranProduction,a.stolenTechnology,a.disguiseIdentity,a.value,
        a.active,a.changed,a.reason};
}
struct Observation {
    std::size_t built=0, skipped=0;
    std::vector<std::pair<std::size_t,std::vector<Action>>> updates;
    static void append(void* opaque,const RuntimeSpyDetectionUpdate& update,bool skip) {
        auto& self=*static_cast<Observation*>(opaque);
        if(skip) ++self.skipped; else ++self.built;
        std::vector<Action> actions;
        for(const auto& value:update.actions) actions.push_back(action(value));
        self.updates.emplace_back(update.detectedPairs,std::move(actions));
    }
};
struct Full {
    RuntimePlannerPassabilityContractDetail::Authority authority;
    std::uint64_t spy=0, special=0, persistent=0;
    std::string spySave;
    bool operator==(const Full&) const = default;
};
bool capture(RuntimeWindowState& state,Full& out,std::string& error) {
    RuntimeSnapshotActiveStateScope active(state);
    (void)completeRuntimeWorldRefresh(state);
    if(!RuntimePlannerPassabilityContractDetail::capture(state,out.authority,error)) return false;
    out.spy=state.spies.stateHash(); out.special=state.specialMechanisms.stateHash();
    out.persistent=runtimePersistentTransientStateHash(state);
    std::ostringstream saved; state.spies.save(saved); out.spySave=saved.str();
    return true;
}
}

bool runRuntimeSpyDetectionNoopContract(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeSpyDetectionNoopContractDetail;
    const auto fail=[&](const std::string& reason) { error="spy idle detection context: "+reason; return false; };
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->config.playerSlots.clear(); assets->config.gameModeRulesOverride.clear();
    assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
        &assets->art,&assets->rules,&assets->mapFacts);
    const auto* spyRule=assets->rules.findUnitType("InfantryType","SPY");
    const auto* dogRule=assets->rules.findUnitType("InfantryType","DOG");
    const auto* transportRule=assets->rules.findUnitType("UnitType","HTK");
    const auto* detectorRule=assets->rules.findBuildingType("NAPSIS");
    const auto* powerRule=assets->rules.findBuildingType("NAPOWR");
    if(!assets->report.success || !assets->report.rulesLoaded)
        return fail("stock startup report: success="+std::to_string(assets->report.success)
            +" rulesLoaded="+std::to_string(assets->report.rulesLoaded)+" error="+assets->report.errorMessage);
    if(!spyRule || !spyRule->canDisguise || !spyRule->permaDisguise)
        return fail("stock SPY: present="+std::to_string(spyRule!=nullptr)
            +" CanDisguise="+std::to_string(spyRule && spyRule->canDisguise)
            +" PermaDisguise="+std::to_string(spyRule && spyRule->permaDisguise));
    // Stock DOG personally identifies a disguise; it does not author a
    // positive DetectDisguiseRange for house-wide detection pairs.
    if(!dogRule || !dogRule->detectDisguise || dogRule->detectDisguiseRange!=0)
        return fail("stock DOG: present="+std::to_string(dogRule!=nullptr)
            +" DetectDisguise="+std::to_string(dogRule && dogRule->detectDisguise)
            +" DetectDisguiseRange="+std::to_string(dogRule?dogRule->detectDisguiseRange:-1));
    if(!transportRule || transportRule->passengers<=0)
        return fail("stock HTK: present="+std::to_string(transportRule!=nullptr)
            +" Passengers="+std::to_string(transportRule?transportRule->passengers:-1));
    if(!detectorRule || !detectorRule->detectDisguise || !detectorRule->powered
        || detectorRule->detectDisguiseRange!=15 || !powerRule || powerRule->power<=0
        || detectorRule->power+powerRule->power<0)
        return fail("stock NAPSIS/NAPOWR: detector="+std::to_string(detectorRule!=nullptr)
            +" DetectDisguise="+std::to_string(detectorRule && detectorRule->detectDisguise)
            +" Powered="+std::to_string(detectorRule && detectorRule->powered)
            +" Range="+std::to_string(detectorRule?detectorRule->detectDisguiseRange:-1)
            +" detectorPower="+std::to_string(detectorRule?detectorRule->power:0)
            +" powerPlant="+std::to_string(powerRule!=nullptr)
            +" suppliedPower="+std::to_string(powerRule?powerRule->power:0));
    if(trimAsciiLocal(assets->config.defaultMapPath).empty())
        return fail("stock startup source map identity absent");
    std::size_t fullChecks=0,actionChecks=0,builtWitness=0,skippedWitness=0;
    std::size_t dogPersonalWitness=0,sensorPairWitness=0;
    // history0/1k/10k: four complete actual simulation ticks and both normal
    // spy sites each tick. Stock roster contains no disguise-capable actors.
    for(int history:{0,1000,10000}) {
        std::unique_ptr<RuntimeWindowState> states[2];
        for(int side=0;side<2;++side) {
            states[side]=std::make_unique<RuntimeWindowState>(); auto& state=*states[side];
            state.config=assets->config; state.report=assets->report; state.rules=assets->rules;
            state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state); std::vector<int> ids;
            if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true,false,96,96,true,true))
                return fail("actual history setup: "+error);
            // The native generated fixture intentionally replaces the map
            // content. Keep its real startup source identity for the complete
            // writer; this contract compares saves and does not reload them.
            state.config.defaultMapPath=assets->config.defaultMapPath;
            int nextId=nextRuntimeObjectInstanceId(state);
            for(int index=0;index<history;++index) {
                WorldObjectInstance dead; dead.instanceId=nextId+index; dead.kind=WorldObjectKind::Unit;
                dead.type="MTNK"; dead.owner="Americans"; dead.alive=false; dead.health=0;
                dead.mission="Destroyed"; dead.x=dead.y=2; dead.cell=runtimeCellKey(2,2);
                state.world.accessObjects().push_back(std::move(dead));
            }
            auto info=std::move(state.terrainInfoByCell); state.objectIndex=RuntimeObjectIndex{};
            initializeRuntimeNavigation(state); state.terrainInfoByCell=std::move(info);
            state.pathTerrainCache.valid=state.pathTerrainCache.dynamicValid=false;
            invalidateRuntimeTickOccupancyCache(state); (void)completeRuntimeWorldRefresh(state);
            state.tick=1; const auto* mover=findObjectById(state,ids.front());
            if(!mover) return fail("actual first mover absent");
            const RuntimeCellPoint goal{mover->x+8,mover->y+8};
            (void)state.vision.exploreArea(state.economy.owner,goal.x,goal.y,8);
            applySelection(state,{ids.front()},false,false); issueRuntimeMoveCommand(state,goal);
            if(state.lastCommand.acceptedCount!=1 || state.behaviors.empty())
                return fail("actual normal move failed to keep both simulation spy sites reachable");
            (void)completeRuntimeWorldRefresh(state);
        }
        for(int step=0;step<4;++step) {
            Observation observations[2]; bool changed[2]{}; Full full[2];
            for(int side=0;side<2;++side) {
                auto& state=*states[side]; RuntimeSnapshotActiveStateScope active(state);
                RuntimeSpyDetectionNoopReferenceScope reference(side==0);
                RuntimeSpyDetectionNoopObserverScope observer(&Observation::append,&observations[side]);
                changed[side]=advanceRuntimeSimulation(state);
                if(!capture(state,full[side],error)) return fail("history full capture: "+error);
            }
            if(changed[0]!=changed[1] || !(full[0]==full[1])
                || observations[0].updates!=observations[1].updates)
                return fail("actual history tick/save/actions diverged history="+std::to_string(history)
                    +" step="+std::to_string(step));
            if(observations[0].built!=2 || observations[0].skipped!=0
                || observations[1].built!=0 || observations[1].skipped!=2)
                return fail("actual two-caller owner-build witness absent history="+std::to_string(history)
                    +" step="+std::to_string(step)+" old="+std::to_string(observations[0].built)
                    +" newSkipped="+std::to_string(observations[1].skipped));
            builtWitness+=observations[0].built; skippedWitness+=observations[1].skipped;
            ++fullChecks; actionChecks+=observations[0].updates.size();
            ++states[0]->tick; ++states[1]->tick;
        }
    }
    // Actual initialized disguise plus real detector yields a positive old
    // pair. Deliberately retaining that pair after changing active/rearm is a
    // cleanup fixture: the optimization must execute Conceal, never early-out.
    for(int phase=0;phase<5;++phase) {
        std::unique_ptr<RuntimeWindowState> states[2];
        int spyIds[2]{};
        for(int side=0;side<2;++side) {
            states[side]=std::make_unique<RuntimeWindowState>(); auto& state=*states[side];
            state.config=assets->config; state.report=assets->report; state.rules=assets->rules;
            state.art=assets->art; state.theater=assets->theater;
            state.rulesLoaded=assets->report.rulesLoaded; state.artLoaded=assets->report.artLoaded;
            state.theaterLoaded=assets->report.theaterLoaded;
            RuntimeSnapshotActiveStateScope active(state); std::vector<int> ids;
            if(!prepareRuntimeMassMovementWorld(state,48,ids,error,false,true,false,96,96,true,true))
                return fail("SPY actual setup: "+error);
            state.config.defaultMapPath=assets->config.defaultMapPath;
            state.tick=100; spyIds[side]=nextRuntimeObjectInstanceId(state);
            if(!state.terrainByCell.contains(runtimeCellKey(80,80))
                || !state.terrainByCell.contains(runtimeCellKey(74,80))
                || !state.terrainByCell.contains(runtimeCellKey(70,82)))
                return fail("native SPY/DOG/transport/NAPSIS/NAPOWR tiles must exist");
            for(int actor=0;actor<5;++actor) {
                WorldObjectInstance value; value.instanceId=spyIds[side]+actor;
                value.kind=actor>=3?WorldObjectKind::Structure:
                    actor==2?WorldObjectKind::Unit:WorldObjectKind::Infantry;
                value.type=actor==4?"NAPOWR":actor==3?"NAPSIS":actor==2?"HTK":actor==1?"DOG":"SPY";
                value.owner=actor==1 || actor>=3?"Russians":"Americans";
                value.health=actor==4?powerRule->strength:actor==3?detectorRule->strength:
                    actor==2?transportRule->strength:actor==1?dogRule->strength:spyRule->strength;
                value.alive=true; value.x=value.y=80; value.cell=runtimeCellKey(80,80);
                if(actor>=3) {
                    value.x=actor==3?74:70; value.y=actor==3?80:82;
                    value.cell=runtimeCellKey(value.x,value.y);
                }
                value.subCell=actor==1?4:2;
                value.mission="Guard"; state.world.accessObjects().push_back(std::move(value));
            }
            auto info=std::move(state.terrainInfoByCell); state.objectIndex=RuntimeObjectIndex{};
            initializeRuntimeNavigation(state); state.terrainInfoByCell=std::move(info);
            invalidateRuntimeTickOccupancyCache(state); (void)completeRuntimeWorldRefresh(state);
            rebuildRuntimeEconomyPower(state);
            ensureRuntimeSpiesInitialized(state);
            const auto context=makeRuntimeSpyContext(state);
            const auto* sensor=findObjectById(state,spyIds[side]+3);
            if(!sensor || !sensor->poweredOperational)
                return fail("stock NAPSIS native same-owner power grid must be operational phase="+std::to_string(phase));
            // An empty detection-pair copy isolates DOG's PERSONAL capability
            // from NAPSIS's published house-wide detection result.
            auto personalOracle=state.spies;
            const_cast<std::vector<RuntimeSpyDetectionState>&>(personalOracle.detectionStates()).clear();
            if(context.areAllied("Russians","Americans")
                || !personalOracle.isDisguised(spyIds[side])
                || !personalOracle.canObserverSeeThroughDisguise(context,spyIds[side]+1,spyIds[side]))
                return fail("stock zero-range DOG personal disguise oracle phase="+std::to_string(phase));
            ++dogPersonalWitness;
            const auto initial=state.spies.updateDetection(context,state.tick);
            (void)applyRuntimeSpyActions(state,initial.actions);
            if(!state.spies.isDisguised(spyIds[side]) || initial.detectedPairs==0
                || std::none_of(state.spies.detectionStates().begin(),state.spies.detectionStates().end(),
                    [&](const auto& pair) { return pair.targetObjectId==spyIds[side]
                        && pair.detectorObjectId==spyIds[side]+3; }))
                return fail("actual powered stock NAPSIS must produce the SPY prior pair phase="+std::to_string(phase)
                    +" pairs="+std::to_string(initial.detectedPairs));
            ++sensorPairWitness;
            if(phase==0 || phase==1 || phase==2) {
                auto& disguise=const_cast<RuntimeSpyDisguiseState&>(*state.spies.disguiseState(spyIds[side]));
                disguise.active=false; disguise.rearmUntilTick=phase==2?102:1000;
                if(phase==0) const_cast<std::vector<RuntimeSpyDetectionState>&>(state.spies.detectionStates()).clear();
            } else {
                auto* spy=findMutableObjectById(state,spyIds[side]);
                if(!spy) return fail("actual spy fixture address absent");
                if(phase==3) { spy->alive=false; spy->health=0; spy->mission="Destroyed"; }
                else {
                    // Match actual passenger retention: dormant, positive HP,
                    // retained by an alive authored transport with capacity.
                    spy->alive=false; spy->mission="InsideTransport";
                    auto* transport=findMutableObjectById(state,spyIds[side]+2);
                    if(!transport) return fail("actual stock transport fixture missing");
                    transport->transportPassengerIds.push_back(spyIds[side]);
                    if(!makeRuntimeSpyContext(state).isObjectRetained(spyIds[side]))
                        return fail("transport fixture must satisfy original retention callback");
                }
                invalidateRuntimeTickOccupancyCache(state);
                state.navigationRefreshPending=true;
            }
            // A timed house state proves advance/actions is retained even in
            // the idle-detection route. It expires at the reactivation tick.
            auto& owners=const_cast<std::vector<RuntimeSpyOwnerState>&>(state.spies.ownerStates());
            RuntimeSpyOwnerState owner; owner.owner="Russians"; owner.blackoutUntilTick=102;
            owners.push_back(owner);
            // Exercise the actual electrical state, rather than merely a
            // timed Spy owner record. The original clear-blackout action at
            // tick102 must subsequently restore this powered stock sensor.
            rebuildRuntimeEconomyPower(state);
            const auto* blackedOutSensor=findObjectById(state,spyIds[side]+3);
            if(!blackedOutSensor || blackedOutSensor->poweredOperational
                || state.spies.blackoutUntilTick("Russians")!=102)
                return fail("native NAPSIS must actually lose power on blackout setup phase="+std::to_string(phase));
            (void)completeRuntimeWorldRefresh(state);
        }
        for(int tick:{101,102,103}) {
            Observation observations[2]; Full full[2]; bool changed[2]{};
            for(int side=0;side<2;++side) {
                auto& state=*states[side]; state.tick=tick; RuntimeSnapshotActiveStateScope active(state);
                RuntimeSpyDetectionNoopReferenceScope reference(side==0);
                RuntimeSpyDetectionNoopObserverScope observer(&Observation::append,&observations[side]);
                changed[side]=advanceRuntimeSpies(state);
                if(!capture(state,full[side],error)) return fail("SPY phase full capture: "+error);
            }
            if(changed[0]!=changed[1] || !(full[0]==full[1])
                || observations[0].updates!=observations[1].updates)
                return fail("SPY cleanup/rearm/retention full authority or actions diverged phase="
                    +std::to_string(phase)+" tick="+std::to_string(tick));
            for(int side=0;side<2;++side) {
                const auto* sensor=findObjectById(*states[side],spyIds[side]+3);
                if(!sensor || sensor->poweredOperational!=(tick>=102))
                    return fail("native sensor blackout/recovery power state phase="+std::to_string(phase)
                        +" tick="+std::to_string(tick)+" side="+std::to_string(side));
            }
            const bool expectedSkip=phase<2 || (phase==2 && tick==101);
            if(observations[0].built!=1 || observations[1].skipped!=(expectedSkip?1u:0u)
                || observations[1].built!=(expectedSkip?0u:1u))
                return fail("SPY active/rearm decision witness wrong phase="+std::to_string(phase)
                    +" tick="+std::to_string(tick));
            if(phase==1 && tick==101) {
                const auto& actions=observations[1].updates.front().second;
                if(actions.empty() || std::get<0>(actions.front())!=RuntimeSpyActionKind::ConcealDisguiseFromOwner)
                    return fail("nonempty old detections lost original Conceal cleanup");
            }
            if(tick>=102 && (states[0]->spies.blackoutUntilTick("Russians")!=0
                || states[1]->spies.blackoutUntilTick("Russians")!=0))
                return fail("idle context bypass skipped blackout expiration");
            if(phase==2 && tick==102) {
                for(int side=0;side<2;++side) {
                    const auto* sensor=findObjectById(*states[side],spyIds[side]+3);
                    if(!states[side]->spies.isDisguised(spyIds[side]) || !sensor || !sensor->poweredOperational
                        || std::none_of(states[side]->spies.detectionStates().begin(),states[side]->spies.detectionStates().end(),
                            [&](const auto& pair) { return pair.targetObjectId==spyIds[side]
                                && pair.detectorObjectId==spyIds[side]+3; }))
                        return fail("rearm must rebuild real context and restore powered NAPSIS pair side="+std::to_string(side));
                }
            }
            ++fullChecks; actionChecks+=observations[0].updates.size();
        }
    }
    if(fullChecks!=27 || actionChecks!=39 || builtWitness!=24 || skippedWitness!=24
        || dogPersonalWitness!=10 || sensorPairWitness!=10)
        return fail("complete original captures/caller witnesses and stock detector coverage incomplete");
    std::cout<<"Spy idle detection context: fullWorld/save/spy/actions/statistics="<<fullChecks
        <<" detectionUpdates="<<actionChecks<<" oldOwnerContextBuilds="<<builtWitness
        <<" optimizedOwnerContextSkips="<<skippedWitness<<" histories=0/1000/10000 passed\n";
    std::cout<<"Spy stock detector oracle: dogPersonalOracleChecks="<<dogPersonalWitness
        <<" poweredNAPSISPairChecks="<<sensorPairWitness<<" blackout101/recovery102=yes\n";
    return true;
}
