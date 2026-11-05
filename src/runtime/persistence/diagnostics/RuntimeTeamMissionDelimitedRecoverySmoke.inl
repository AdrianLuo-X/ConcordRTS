template<class Session>
bool runRuntimeTeamMissionDelimitedRecoverySmoke(std::string& error) try {
    const char* configured=std::getenv("CONCORD_TEAM_MISSION_RECOVERY_OUTPUT");
    if(!configured || !*configured) {error="mission recovery requires explicit output";return false;}
    const std::filesystem::path output(configured);std::filesystem::create_directories(output);
    if(!std::filesystem::is_empty(output)) {error="mission recovery output must be a new empty directory";return false;}
    int checks=0,observations=0;
    const auto need=[&](bool condition,const std::string& why) {++checks;if(!condition)error="mission real recovery: "+why;return condition;};
    const auto write=[&](const std::string& name,const std::string& bytes) {
        std::ofstream file(output/name,std::ios::binary);file<<bytes;file.close();return need(static_cast<bool>(file),"write "+name);
    };
    const auto make=[&]() {
        auto state=makeRuntimeRecoveryConsistencyFixture<Session>(error);
        if(!state)return state;
        IniFile definitions;
        definitions.loadFromString("[MISSION_RECOVERY_SCRIPT]\nName=Mission recovery\n0=5,1\n");
        state->triggers.scripts["MISSION_RECOVERY_SCRIPT"]=parseRuntimeScriptType(definitions,"MISSION_RECOVERY_SCRIPT");
        state->triggers.loaded=true;state->triggers.teamMissions.clear();state->triggers.nextTeamMissionInstanceId=5;
        return state;
    };
    const auto missionBytes=[](const auto& state) {std::ostringstream out;writeRuntimeTeamMissionState(state.triggers,out);return out.str();};
    const auto fullHash=[&](const auto& state) {
        std::string why;std::vector<std::pair<std::string,std::uint64_t>> domains;
        const auto value=runtimeStandaloneSimulationStateHash(state,why,&domains);
        if(!value || !why.empty())throw std::runtime_error("full mission authority: "+why);
        std::ostringstream out;out<<"tick="<<state.tick<<"\nfull="<<value<<'\n';
        for(const auto& [name,hash]:domains)out<<name<<'='<<hash<<'\n';
        return out.str();
    };
    auto source=make();if(!need(static_cast<bool>(source),"original complete nonempty fixture "+error))return false;
    std::vector<int> members;
    for(const auto& object:source->world.getObjects())if(object.alive && object.owner=="SkirmishPlayer1")members.push_back(object.instanceId);
    std::sort(members.begin(),members.end());if(!need(members.size()>=2,"actual living member ids"))return false;members.resize(2);
    RuntimeTeamMissionState mission;mission.instanceId=4;mission.teamTypeId="MISSION_RECOVERY_TEAM";mission.scriptId="MISSION_RECOVERY_SCRIPT";
    mission.objectIds=members;mission.stepIndex=0;mission.stepStartedTick=source->tick;mission.stepIssued=true;
    mission.targetObjectId=members.front();mission.targetCell={7,12};mission.completed=false;
    mission.sourceAiTriggerId="";mission.sourceAiBuildId=-1;mission.aiOutcomeReported=false;mission.achievedGreatSuccess=true;
    mission.targetInteractionCompleted=true;mission.status="moving; // retained mission status";
    mission.transportReturnCells[members[0]]={7,12};mission.transportReturnCells[members[1]]={8,12};
    mission.regrouping=true;mission.regroupCell={7,12};mission.regroupStartedTick=source->tick;
    mission.regroupDestinations[members[0]]={7,12};mission.regroupDestinations[members[1]]={8,12};
    source->triggers.teamMissions[4]=mission;
    const auto expectedMission=missionBytes(*source);const auto expectedAuthority=fullHash(*source);
    std::ostringstream ordinary;if(!writeRuntimeStateToStream(*source,ordinary,error))return false;
    std::string replay;if(!writeRuntimeMatchReplayWorld(*source,replay,error))return false;
    const auto config=serializeRuntimeMatchReplayConfig(source->config);const auto replayHash=runtimeMatchReplayStateHash(*source,error);
    RuntimeMultiplayerSnapshotMemoryImage image;auto identity=runtimeMultiplayerSnapshotSmokeIdentity(*source,29,"mission-delimited-recovery",RuntimeMultiplayerSnapshotKind::Load);
    if(!exportRuntimeMultiplayerSnapshotV15(*source,identity,image,error))return false;
    if(!write("reference-missions.ini",expectedMission)||!write("reference-authority.txt",expectedAuthority)
        ||!write("world-v14.ini",ordinary.str())||!write("world-replay.ini",replay)
        ||!write("world-v15.ini",std::string(image.bytes.begin(),image.bytes.end())))return false;
    // Exercise the actual old General parser + real materializer to retain
    // the pre-fix loss in a genuine World, alongside the four original pure1.
    auto oldGeneral=make();if(!oldGeneral)return false;IniFile oldDocument;
    if(!oldDocument.loadFromString(replay) || !materializeRuntimeLoadStateFromIni(*oldGeneral,oldDocument,error,true))return false;
    if(!need(missionBytes(*oldGeneral)!=expectedMission,"old General must reproduce actual original mission loss")
        ||!write("old-general-missions.ini",missionBytes(*oldGeneral)))return false;
    auto local=make(),played=make(),network=make(),savedNetwork=make();if(!local||!played||!network||!savedNetwork)return false;
    if(!loadRuntimeState(*local,pathString(output/"world-v14.ini"),error)) {error="mission local: "+error;return false;}
    if(!restoreRuntimeMatchReplayWorld(*played,config,replay,source->tick,replayHash,error)) {error="mission replay: "+error;return false;}
    RuntimeMultiplayerSnapshotStagedState staged;
    if(!stageRuntimeMultiplayerSnapshot(*network,image.bytes,image.descriptor,staged,error)) {error="mission network: "+error;return false;}
    network->camera.worldOffsetX=727;network->selectedObjectIds=members;
    commitRuntimeMultiplayerSnapshotAuthority(*network,std::move(staged));
    if(!need(network->camera.worldOffsetX==727 && network->selectedObjectIds==members,"network retains local presentation"))return false;
    auto saveIdentity=runtimeMultiplayerSnapshotSmokeIdentity(*source,30,"mission-delimited-save",RuntimeMultiplayerSnapshotKind::Save);
    RuntimeMultiplayerSnapshotMemoryImage savedImage,rebound;
    if(!exportRuntimeMultiplayerSnapshotV15(*source,saveIdentity,savedImage,error)
        ||!rebindRuntimeMultiplayerSaveAsLoad(*savedNetwork,savedImage.bytes,31,savedNetwork->config.multiplayerTimelineEpoch,savedNetwork->tick,rebound,error)) {
        error="mission save-rebind: "+error;return false;
    }
    if(!write("world-v15-save.ini",std::string(savedImage.bytes.begin(),savedImage.bytes.end()))
        ||!write("world-v15-rebound.ini",std::string(rebound.bytes.begin(),rebound.bytes.end())))return false;
    RuntimeMultiplayerSnapshotStagedState savedStaged;
    if(!stageRuntimeMultiplayerSnapshot(*savedNetwork,rebound.bytes,rebound.descriptor,savedStaged,error))return false;
    commitRuntimeMultiplayerSnapshotAuthority(*savedNetwork,std::move(savedStaged));
    std::array states{source.get(),local.get(),played.get(),network.get(),savedNetwork.get()};
    for(std::size_t route=1;route<states.size();++route) {
        if(!write("route-"+std::to_string(route)+"-missions.ini",missionBytes(*states[route]))
            ||!write("route-"+std::to_string(route)+"-authority.txt",fullHash(*states[route])))return false;
        if(!need(missionBytes(*states[route])==expectedMission,"all mission fields route "+std::to_string(route))
            ||!need(fullHash(*states[route])==expectedAuthority,"complete original authority route "+std::to_string(route)))return false;
    }
    std::array<std::ostringstream,5> traces;
    for(int step=0;step<32;++step) {
        std::string reference;
        for(std::size_t route=0;route<states.size();++route) {
            auto& state=*states[route];++state.tick;advanceRuntimeSimulation(state,false);completeRuntimeWorldRefresh(state);
            const auto actual=fullHash(state);traces[route]<<actual<<'\n';
            if(route==0)reference=actual;else if(!need(actual==reference,"actual full production continuation route "+std::to_string(route)+" step "+std::to_string(step)))return false;
            ++observations;
        }
    }
    for(std::size_t route=0;route<traces.size();++route)if(!write("trace-"+std::to_string(route)+".txt",traces[route].str()))return false;
    std::ostringstream result;result<<"checks="<<checks<<"\nfull-fixedtick-observations="<<observations
        <<"\noriginal-General-loss=observed\nlocal-v14=passed\nnetwork-v15=passed\nexact-replay=passed\nsave-rebind=passed\n"
        <<"independent-W028=not-built/not-run\n";
    if(!write("result.txt",result.str()))return false;
    error.clear();std::cout<<"Mission real recovery passed: "<<checks<<" checks, "<<observations<<" full fixedtick observations, oldGeneralLoss=yes file/network/replay=yes\n";
    return true;
} catch(const std::exception& exception) {error="mission real recovery: "+std::string(exception.what());return false;}
