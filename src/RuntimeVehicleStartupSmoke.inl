bool runRuntimeVehicleStartupSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Vehicle startup: " + why; return false; };
    const std::string drive = "{4A582741-9839-11d1-B709-00A024DDAFD1}";
    const std::string hover = "{4A582742-9839-11d1-B709-00A024DDAFD1}";
    const std::string ship = "{2BEA74E1-7CCA-11d3-BE14-00104B62A16C}";
    IniFile ini; RulesDatabase rules;
    std::string authored = "[General]\nHoverAcceleration=.02\nTrackedUphill=.5\nWheeledUphill=.5\n"
        "[VehicleTypes]\n0=STARTTRACK\n1=STARTWHEEL\n2=STARTHOVER\n3=STARTSHIP\n4=STARTNO\n5=STARTZERO\n"
        "[InfantryTypes]\n0=STARTFOOT\n[BuildingTypes]\n0=STARTFACTORY\n"
        "[STARTFACTORY]\nStrength=1000\nFactory=UnitType\nWeaponsFactory=yes\n"
        "[STARTFOOT]\nStrength=200\nSpeed=7\nSpeedType=Foot\nMovementZone=Infantry\n"
        "[Clear]\nTrack=100%\nWheel=100%\n[Water]\nFloat=100%\nHover=100%\n"
        "[Rough]\nTrack=50%\nWheel=50%\n";
    for (const auto& spec : std::array<std::pair<const char*, const char*>,6>{{
        {"STARTTRACK","Track"},{"STARTWHEEL","Wheel"},{"STARTHOVER","Hover"},
        {"STARTSHIP","Float"},{"STARTNO","Track"},{"STARTZERO","Track"}}}) {
        const bool naval=std::string(spec.first)=="STARTSHIP", airborneHover=std::string(spec.first)=="STARTHOVER";
        authored += "["+std::string(spec.first)+"]\nStrength=200\nSpeed=7\nROT=20\nAccelerationFactor="
            + (std::string(spec.first)=="STARTZERO"?"0":".03") + "\nSpeedType="+spec.second
            + "\nMovementZone="+(naval?"Water":airborneHover?"Amphibious":"Normal")
            + "\nLocomotor="+(naval?ship:airborneHover?hover:drive)+"\nAccelerates="
            + (std::string(spec.first)=="STARTNO"?"no":"yes")+"\n";
        if(naval) authored+="Naval=yes\nWaterBound=yes\n";
    }
    if(!ini.loadFromString(authored)||!rules.loadFromIni(ini)) return fail("fixture rules");
    const auto make = [&](const char* type, int hz, bool rough=false, bool slope=false) {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        state.config=live.config; state.config.fixedTickHz=hz;
        state.config.computerAiEnabled=state.config.multiplayerActive=state.config.crates=false;
        state.config.superweapons=state.config.noCooldown=false;
        state.config.playerCountry=state.economy.owner="Player";
        state.economy.skirmishStartApplied=state.triggers.bootstrapComplete=true;
        state.runtimeSoundsEnabled=state.runtimePreferencePersistenceEnabled=false;
        state.runtimeSuppressPlayerAudioFeedback=true; state.visionEnabled=false;
        state.rules=rules; state.rulesLoaded=true;
        for(int y=0;y<20;++y) for(int x=0;x<36;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
            cell.level=slope && x>=10?1:0;state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance actor; actor.instanceId=1;actor.type=type;actor.owner="Player";
        actor.kind=std::string(type)=="STARTFOOT"?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        actor.x=4;actor.y=6;actor.cell=runtimeCellKey(4,6);actor.subCell=actor.kind==WorldObjectKind::Infantry?2:-1;
        actor.health=200;actor.alive=true;actor.mission="Guard";actor.facing=runtimeFacingFromDelta(1,0);
        state.world.accessObjects().push_back(actor); initializeRuntimeNavigation(state);
        const bool water=std::string(type)=="STARTSHIP";
        for(auto& [key,info]:state.terrainInfoByCell) {
            const int x=key%512; info=RuntimeTerrainInfo{};
            info.water=water;info.kind=water?RuntimeTerrainKind::Water:RuntimeTerrainKind::Land;
            info.movementLandType=info.authoredLandType=water?RuntimeTerrainLandType::Water
                :rough?RuntimeTerrainLandType::Rough:RuntimeTerrainLandType::Clear;
            if(slope && (x==9 || x==10)) {info.ramp=true;info.movementRampType=1;}
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(state); state.selectedObjectIds={1};
        return storage;
    };
    const auto body = [](const RuntimeWindowState& state) {
        const auto* actor=findObjectById(state,1);
        return std::array<double,2>{runtimeObjectRenderX(*actor),runtimeObjectRenderY(*actor)};
    };
    const auto step = [](RuntimeWindowState& state) {++state.tick;advanceRuntimeSimulation(state);};
    int cases=0;
    for(int hz:{15,30,60}) for(const char* type:{"STARTTRACK","STARTWHEEL","STARTHOVER","STARTSHIP"}) {
        auto storage=make(type,hz);auto& state=*storage;
        issueRuntimeMoveCommand(state,{22,6});if(state.lastCommand.acceptedCount!=1)return fail("ordinary move rejected");
        int firstDuration=0,cruiseSegments=0;std::vector<double> startupSteps;bool complete=false;
        double firstRamp=0;auto previous=body(state);
        for(int tick=0;tick<hz*100;++tick) {
            const auto prior=state.behaviors.find(1);
            const bool starting=prior!=state.behaviors.end() && prior->second.renderStartupAccelerationDistance>0
                && runtimeVehicleStartupDistanceBefore(prior->second,1.0)==0;
            step(state);const auto current=body(state);
            const double displacement=std::hypot(current[0]-previous[0],current[1]-previous[1]);previous=current;
            if(starting && displacement>0)startupSteps.push_back(displacement);
            const auto motion=state.behaviors.find(1);
            if(motion==state.behaviors.end()){complete=true;break;}
            const auto& move=motion->second;
            if(move.renderMoveTicksTotal>0 && move.renderMoveTicksRemaining==move.renderMoveTicksTotal) {
                if(firstDuration==0){firstDuration=move.moveCooldownTicks;firstRamp=move.renderStartupAccelerationDistance;}
                else if(move.renderStartupAccelerationDistance>0 && runtimeVehicleStartupDistanceBefore(move,
                    std::hypot(double(move.renderTo.x-move.renderFrom.x),double(move.renderTo.y-move.renderFrom.y)))==0)
                    return fail("startup restarted at a cell boundary");
                if(move.renderStartupAccelerationDistance==0 && move.renderArrivalDistanceAfter<0) {
                    ++cruiseSegments;
                    const auto* actor=findObjectById(state,1);
                    const double expected=runtimeMovementSegmentIntervalQ16(state,*actor,move.renderFrom,move.renderTo)/65536.0;
                    if(std::abs(move.moveCooldownTicks-expected)>=1.0)return fail("cruise timing changed");
                }
            }
        }
        const auto* actor=findObjectById(state,1);
        if(!complete || runtimeCellPointForObject(*actor)!=RuntimeCellPoint{22,6} || actor->renderMoveActive
            || firstRamp<=0 || cruiseSegments==0 || startupSteps.size()<3
            || !(startupSteps[0]<startupSteps[2]*.6)
            || firstDuration<=runtimeMovementSegmentIntervalTicks(state,*actor,{4,6},{5,6}))
            return fail(std::string(type)+" did not exercise real slow startup, unchanged cruise and final arrival");
        std::cout<<"Vehicle startup "<<type<<" "<<hz<<"Hz firstDuration="<<firstDuration
            <<" firstSteps="<<startupSteps[0]<<','<<startupSteps[1]<<','<<startupSteps[2]<<" cruise="<<cruiseSegments<<" PASS\n";
        ++cases;
    }
    // Real active orders retain their speed profile, while Stop starts a fresh
    // route from the body's preserved fractional position.
    for(const char* type:{"STARTTRACK","STARTSHIP"}) {
        auto storage=make(type,30);auto& state=*storage; issueRuntimeMoveCommand(state,{22,6});
        for(int i=0;i<6;++i)step(state);
        const auto before=body(state);const auto prior=state.behaviors.at(1);
        issueRuntimeMoveCommand(state,{24,8});
        const auto& inherited=state.behaviors.at(1);
        if(body(state)!=before || inherited.moveCooldownTicks!=prior.moveCooldownTicks
            || inherited.moveStartupDistanceQ16!=prior.moveStartupDistanceQ16
            || inherited.renderStartupAccelerationDistance!=prior.renderStartupAccelerationDistance)
            return fail("continuous retarget discarded the current startup segment");
        issueRuntimeStopCommand(state);const auto stopped=body(state);
        issueRuntimeMoveCommand(state,{24,6});
        if(body(state)!=stopped || !state.behaviors.contains(1) || state.behaviors.at(1).moveStartupDistanceQ16!=0)
            return fail("Stop/Move inherited cruise momentum or snapped the stopped body");
        bool restarted=false;
        for(int i=0;i<300 && state.behaviors.contains(1);++i) {
            step(state);const auto move=state.behaviors.find(1);
            if(move!=state.behaviors.end() && move->second.renderStartupAccelerationDistance>0){restarted=true;break;}
        }
        if(!restarted)return fail("Stop/Move did not restart acceleration");++cases;
    }
    for(const char* type:{"STARTNO","STARTFOOT"}) {
        auto storage=make(type,30);auto& state=*storage;const auto& actor=*findObjectById(state,1);
        RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;move.moveTarget={22,6};
        const int interval=consumeRuntimeMovementSegmentIntervalTicks(state,actor,move,{4,6},{5,6});
        if(runtimeObjectUsesVehicleStartup(state,actor)||move.moveStartupDistanceQ16!=0
            ||move.renderStartupAccelerationDistance!=0 || interval!=runtimeMovementSegmentIntervalTicks(state,actor,{4,6},{5,6}))
            return fail("explicit Accelerates=no or infantry changed");++cases;
    }
    auto zero=make("STARTZERO",30);
    issueRuntimeMoveCommand(*zero,{22,6});
    if(!runtimeVehicleStartupCannotMove(*zero,*findObjectById(*zero,1))||zero->lastCommand.acceptedCount!=0)
        return fail("zero acceleration accepted movement");++cases;
    // Freshly blocked motion starts again; ordinary Moving segments retain
    // their prior distance. Terrain scales preserve the rule-frame factor.
    for(bool slope:{false,true}) {
        auto storage=make("STARTTRACK",30,!slope,slope);auto& state=*storage;auto& actor=*findMutableObjectById(state,1);
        RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;move.state=RuntimeBehaviorState::Blocked;
        move.moveTarget={22,6};move.moveStartupDistanceQ16=RuntimeVehicleStartupMotion::distanceQ16(100);
        const RuntimeCellPoint from=slope?RuntimeCellPoint{9,6}:RuntimeCellPoint{4,6};
        const RuntimeCellPoint to{slope?10:5,6};
        const double multiplier=runtimeTerrainMovementSpeedMultiplier(state,actor,from,to);
        const double ramp=runtimeVehicleStartupAccelerationDistance(state,actor)*multiplier;
        const double scale=prepareRuntimeVehicleStartup(state,actor,move,from,to,1.0);
        if(move.renderStartupAccelerationDistance!=ramp || move.moveStartupDistanceQ16!=65536 || scale<=1
            ||runtimeVehicleStartupDistanceBefore(move,1.0)!=0)return fail("blocked restart or terrain ramp differs");
        if(!slope && std::abs(multiplier-.5)>1e-12)return fail("rough land fixture lost original 50% speed");
        move.state=RuntimeBehaviorState::Moving;move.moveCooldownTicks=0;
        (void)prepareRuntimeVehicleStartup(state,actor,move,to,{to.x+1,to.y},1.0);
        if(runtimeVehicleStartupDistanceBefore(move,1.0)==0)return fail("Moving segment incorrectly restarted");++cases;
    }
    auto factory=make("STARTTRACK",30);factory->config.noCooldown=true;
    WorldObjectInstance host;host.instanceId=2;host.kind=WorldObjectKind::Structure;host.type="STARTFACTORY";
    host.owner="Player";host.alive=true;host.health=1000;host.x=20;host.y=12;host.cell=runtimeCellKey(20,12);
    factory->world.accessObjects().push_back(host);refreshRuntimeObjectOccupancyAfterLocalChange(*factory);
    RuntimeObjectBehavior exit;exit.kind=RuntimeBehaviorKind::Move;exit.moveTarget={22,6};exit.factoryExitClearanceHostId=2;
    if(!runtimeFactoryEgressBypassesArrival(*factory,*findObjectById(*factory,1),exit))return fail("factory bypass fixture");
    if(prepareRuntimeVehicleStartup(*factory,*findObjectById(*factory,1),exit,{4,6},{5,6},1.0)!=1.0
        ||exit.renderStartupAccelerationDistance!=0 ||exit.moveStartupDistanceQ16<=65536)
        return fail("ForceTrack/fast factory exit retained an acceleration curve");++cases;
    std::cout<<"Vehicle startup: "<<cases<<" full-simulation routes and order/class/terrain/factory contracts passed\n";
    return true;
}
