bool runRuntimeAircraftTurnSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Aircraft turn: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original assets missing");
    assets->rulesLoaded=assets->report.rulesLoaded; assets->artLoaded=assets->report.artLoaded;
    assets->theaterLoaded=assets->report.theaterLoaded;
    RuntimeAircraftFlights legacy;
    if (!deserializeRuntimeAircraftFlights("AircraftFlights 1 1\n7 65536 131072 4 1\n",legacy)
        || legacy.at(7).heading!=-1 || legacy.at(7).x!=65536 || !legacy.at(7).moving)
        return fail("legacy flight heading import");
    const bool diagnostic = std::getenv("CONCORD_AIRCRAFT_TURN_DIAGNOSTIC") != nullptr;
    const char* output = std::getenv("CONCORD_AIRCRAFT_MOTION_OUTPUT");
    int cases = 0, saves = 0, paired = 0;
    for (const std::string type : {"ORCA", "BEAG"}) for (int hz : {15,30,60}) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, *assets, false); s.world = assets->world;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = false;
        s.config.playerSlots.clear(); s.config.playerCountry = type == "BEAG" ? "Alliance" : "Americans";
        s.economy.owner = s.config.playerCountry; s.economy.skirmishStartApplied = true;
        // This fixture has no skirmish slots, so explicitly supply the local
        // facade used by the real UI command executor's house scope.
        s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = s.economy.owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.country = s.config.playerCountry; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        auto* airport = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAAIRC", {44,76});
        if (!airport) return fail("airport missing"); const int host = airport->instanceId;
        airport->buildAnimTicksRemaining = airport->buildAnimTicksTotal = 0;
        auto* plane = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, type, {46,78});
        if (!plane) return fail("plane missing"); const int id = plane->instanceId;
        applyRuntimeAircraftDockPose(s, *plane, *findObjectById(s, host), 0);
        initializeRuntimeNavigation(s); RuntimeSnapshotActiveStateScope scope(s);
        const RuntimeCellPoint destination{106,84}; s.selectedObjectIds = {id};
        const auto* initialPlane = findObjectById(s, id);
        if (!initialPlane || !findRuntimeTerrainCellAt(s, {initialPlane->x, initialPlane->y})
            || !findRuntimeTerrainCellAt(s, destination))
            return fail(type+" hz="+std::to_string(hz)
                +" fixture requires airport/start and destination (106,84) on map: "
                +s.config.defaultMapPath);
        const bool moveDispatched = dispatchRuntimeTargetOrderFromUi(
            s, RuntimeMultiplayerGameplayTargetOrderKind::Move, destination);
        if (!moveDispatched || s.lastCommand.acceptedCount != 1) {
            const auto* rejectedPlane = findObjectById(s, id);
            return fail(type+" hz="+std::to_string(hz)+" move order rejected: dispatched="
                +std::to_string(moveDispatched)+" valid="+std::to_string(s.lastCommand.valid)
                +" selected="+std::to_string(s.lastCommand.selectedCount)
                +" accepted="+std::to_string(s.lastCommand.acceptedCount)
                +" rejected="+std::to_string(s.lastCommand.rejectedCount)
                +" canMove="+std::to_string(rejectedPlane
                    && runtimeObjectCanReceiveMoveCommand(s, *rejectedPlane))
                +" planeOwner="+(rejectedPlane ? rejectedPlane->owner : "missing")
                +" commandOwner="+s.economy.owner+" status="+s.interactionStatus);
        }
        const auto position = [&](const RuntimeWindowState& state) {
            const auto* p = findObjectById(state,id);
            return std::array<double,2>{p->hasRenderPosition ? p->renderX : p->x,
                p->hasRenderPosition ? p->renderY : p->y};
        };
        const auto step = [](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope active(state); ++state.tick;
            advanceRuntimeSimulation(state, false); completeRuntimeWorldRefresh(state);
        };
        const auto origin = position(s);
        const double ax = destination.x-origin[0], ay = destination.y-origin[1], length = std::hypot(ax,ay);
        // Original ORCA/BEAG Speed=14 is 35 leptons/rule frame. Do not
        // derive the expected cruise from the implementation under test.
        const double stockCruise = 35.0*15.0/(256.0*hz);
        const double stockTurnRadius = 35.0/(3.0*6.2831853071795864769);
        const int turnLimit = static_cast<int>(std::ceil(3.0*15/hz))+1;
        int maxTurn = 0, pauses = 0, turnFrames = 0, returnTick = -1;
        double maxStep = 0, overshoot = 0, lateral = 0;
        bool reached = false, landed = false; std::set<int> headings;
        std::ofstream csv;
        if (output) {
            csv.open(std::filesystem::path(output)/(type+"-return-"+std::to_string(hz)+".csv"));
            csv << "tick,x,y,facing,step,turn,returning\n";
        }
        auto clone = std::unique_ptr<RuntimeWindowState>{};
        for (int n=0;n<hz*100;++n) {
            const auto before = position(s); const auto* p = findObjectById(s,id);
            const int facing = p->facing; const bool airborne = p->airborne;
            step(s); const auto after = position(s); p = findObjectById(s,id);
            if (!p) return fail("plane disappeared");
            if (clone) {
                step(*clone); ++paired;
                if (position(*clone) != after || findObjectById(*clone,id)->facing != p->facing
                    || serializeRuntimeActiveAircraftFlights(s) != serializeRuntimeActiveAircraftFlights(*clone))
                    return fail("saved turning flight diverged");
            }
            const double moved = std::hypot(after[0]-before[0],after[1]-before[1]);
            const int turn = runtimeFacingAbsDifference(facing,p->facing);
            if (airborne && p->airborne) {
                maxStep = std::max(maxStep,moved); maxTurn = std::max(maxTurn,turn);
                // A dock approach deliberately stops horizontally while
                // native Fly height and PoseDir continue toward touchdown.
                if (moved<1e-8 && p->aircraftDockFlightProgress<0.0) ++pauses;
                if (!diagnostic && (moved>stockCruise+0.0001 || turn>turnLimit))
                    return fail(type+" hz="+std::to_string(hz)+" speed/heading jumped step="
                        +std::to_string(moved)+" retail="+std::to_string(stockCruise)+" turn="+std::to_string(turn));
            }
            if (std::hypot(after[0]-destination.x,after[1]-destination.y)<0.001) reached = true;
            const auto behavior = s.behaviors.find(id);
            const bool returning = behavior!=s.behaviors.end()
                && behavior->second.pendingAllowedOccupiedObjectId==host;
            if (returning && returnTick<0) returnTick=s.tick;
            if (returning && p->airborne && s.tick-returnTick<hz*4) {
                headings.insert(p->facing); if (turn) ++turnFrames;
                const double x=after[0]-destination.x,y=after[1]-destination.y;
                overshoot=std::max(overshoot,(x*ax+y*ay)/length);
                lateral=std::max(lateral,std::abs(x*ay-y*ax)/length);
                if (!clone && s.tick-returnTick>=hz/2) {
                    std::ostringstream save; IniFile ini; clone=std::make_unique<RuntimeWindowState>();
                    seedRuntimeLoadScratch(*clone,s,false);
                    if (!writeRuntimeStateToStream(s,save,error)||!ini.loadFromString(save.str())
                        ||!materializeRuntimeLoadStateFromIni(*clone,ini,error)) return fail("turn save: "+error);
                    ++saves;
                }
                if (output && hz==30 && (s.tick-returnTick==hz/2 || s.tick-returnTick==hz || s.tick-returnTick==hz*2)) {
                    const auto file=std::filesystem::path(output)/(type+"-turn-"+std::to_string(s.tick-returnTick)+".bmp");
                    const auto stats=drawRuntimeStateOffscreenAtCell(s,destination,800,600,error,file.string(),false);
                    if (!stats || stats->visibleVoxelBodies<1 || stats->missingVoxelProxies!=0)
                        return fail("native turn frame: "+error);
                }
            }
            if(csv)csv<<s.tick<<','<<std::setprecision(12)<<after[0]<<','<<after[1]<<','<<p->facing
                <<','<<moved<<','<<turn<<','<<returning<<'\n';
            if (!p->airborne && p->mission=="Dock") {landed=true;break;}
        }
        std::cout<<"Aircraft turn: "<<type<<" hz="<<hz<<" ticks="<<s.tick<<" maxStep="<<maxStep
            <<" maxTurn="<<maxTurn<<" overshoot="<<overshoot<<" lateral="<<lateral
            <<" headings="<<headings.size()<<" turnFrames="<<turnFrames<<" pauses="<<pauses<<'\n';
        if (!landed || !reached) return fail("did not pass the requested point and return");
        if (!diagnostic && (overshoot<stockTurnRadius*.5 || lateral<stockTurnRadius
            || headings.size()<20 || turnFrames<10 || pauses!=0))
            return fail("return did not form a continuous forward arc");
        ++cases;
    }
    std::cout<<"Aircraft turn: "<<(diagnostic?"baseline":"passed")<<" cases="<<cases
        <<" saves="<<saves<<" continuedTicks="<<paired<<'\n';
    return true;
}
