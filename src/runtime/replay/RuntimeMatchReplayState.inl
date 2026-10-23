#pragma once

// Private host recovery kernel. The existing recorder/viewer includes this same
// implementation; the recovery contract compiles it without adding a UI session.
// Authority includes every runtime hash domain, never only GameWorld.
std::uint64_t runtimeMatchReplayStateHash(const RuntimeWindowState& state,
    std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* trace=nullptr) {
    return runtimeStandaloneSimulationStateHash(state,error,trace);
}
std::string serializeRuntimeMatchReplayConfig(const RuntimeConfig& config) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out << "CRTS_CONFIG_2\n";
#define REPLAY_STRING(field) out << std::quoted(config.field) << '\n'
    REPLAY_STRING(defaultMapPath);REPLAY_STRING(modRootPath);REPLAY_STRING(gameMode);
    REPLAY_STRING(gameModeRulesOverride);REPLAY_STRING(campaignId);REPLAY_STRING(campaignMissionId);
    REPLAY_STRING(campaignSourceId);REPLAY_STRING(campaignDifficulty);REPLAY_STRING(campaignPlayerHouse);
    REPLAY_STRING(campaignNextMissionId);REPLAY_STRING(campaignBriefingKey);
    REPLAY_STRING(playerCountry);REPLAY_STRING(playerColor);REPLAY_STRING(multiplayerCanonicalMapId);
#undef REPLAY_STRING
    out << config.fixedTickHz << ' ' << config.multiplayerActive << ' '
        << config.multiplayerSetupHash << ' ' << config.multiplayerMapContentFingerprint << ' '
        << config.multiplayerCooperativeScenarioFingerprint << ' ' << config.multiplayerTimelineEpoch << ' '
        << config.simulationRandomSeed << '\n';
    out << config.startingCredits << ' ' << config.startingUnits << ' ' << config.techLevel << ' '
        << config.gameSpeed << ' ' << config.shortGame << ' ' << config.mcvRepacks << ' '
        << config.crates << ' ' << config.superweapons << ' ' << config.buildOffAlly << ' '
        << config.computerAiEnabled << ' ' << config.noCooldown << '\n' << config.playerSlots.size() << '\n';
    for (const auto& slot:config.playerSlots) {
        out << slot.enabled << ' ' << slot.human << ' ' << slot.local << ' ' << slot.startPosition << ' '
            << std::quoted(slot.peerId) << ' ' << std::quoted(slot.name) << ' '
            << std::quoted(slot.country) << ' ' << std::quoted(slot.color) << ' '
            << std::quoted(slot.team) << ' ' << std::quoted(slot.difficulty) << '\n';
    }
    return out.str();
}

bool deserializeRuntimeMatchReplayConfig(const std::string& text,
    RuntimeConfig& config, std::string& error) {
    auto candidate=config;
    std::istringstream in(text);in.imbue(std::locale::classic());
    std::string version;in>>version;
    const auto string=[&](std::string& value) {
        in>>std::ws;
        if(in.peek()!='"' || !(in>>std::quoted(value)) || value.size()>4096
            || value.find('\0')!=std::string::npos) in.setstate(std::ios::failbit);
    };
    const auto number=[&](std::uint64_t& value) {
        std::string token;
        if(!(in>>token) || !parseRuntimeUint64Strict(token,value)) in.setstate(std::ios::failbit);
    };
#define REPLAY_STRING(field) string(candidate.field)
    REPLAY_STRING(defaultMapPath);REPLAY_STRING(modRootPath);REPLAY_STRING(gameMode);
    REPLAY_STRING(gameModeRulesOverride);REPLAY_STRING(campaignId);REPLAY_STRING(campaignMissionId);
    REPLAY_STRING(campaignSourceId);REPLAY_STRING(campaignDifficulty);REPLAY_STRING(campaignPlayerHouse);
    REPLAY_STRING(campaignNextMissionId);REPLAY_STRING(campaignBriefingKey);
    REPLAY_STRING(playerCountry);REPLAY_STRING(playerColor);REPLAY_STRING(multiplayerCanonicalMapId);
#undef REPLAY_STRING
    in>>candidate.fixedTickHz>>candidate.multiplayerActive;
    number(candidate.multiplayerSetupHash);number(candidate.multiplayerMapContentFingerprint);
    number(candidate.multiplayerCooperativeScenarioFingerprint);number(candidate.multiplayerTimelineEpoch);
    number(candidate.simulationRandomSeed);
    in>>candidate.startingCredits>>candidate.startingUnits>>candidate.techLevel>>candidate.gameSpeed
        >>candidate.shortGame>>candidate.mcvRepacks>>candidate.crates>>candidate.superweapons
        >>candidate.buildOffAlly>>candidate.computerAiEnabled>>candidate.noCooldown;
    int count=-1;in>>count;
    if(version!="CRTS_CONFIG_2" || !in || count<0 || count>8 || candidate.fixedTickHz<1
        || candidate.fixedTickHz>240 || candidate.startingCredits<0
        || candidate.startingCredits>runtimeMaximumCreditBalance || candidate.startingUnits<0
        || candidate.startingUnits>10000 || candidate.techLevel<1 || candidate.techLevel>10
        || candidate.gameSpeed<0 || candidate.gameSpeed>6 || candidate.defaultMapPath.empty()) {
        error="invalid replay startup configuration";return false;
    }
    candidate.playerSlots.clear();
    for(int index=0;index<count;++index) {
        RuntimeConfig::PlayerSlot slot;
        in>>slot.enabled>>slot.human>>slot.local>>slot.startPosition;
        string(slot.peerId);string(slot.name);string(slot.country);string(slot.color);
        string(slot.team);string(slot.difficulty);
        if(!in || slot.startPosition<0 || slot.startPosition>8) {
            error="invalid replay player slot";return false;
        }
        candidate.playerSlots.push_back(std::move(slot));
    }
    in>>std::ws;
    if(!in.eof()) {error="trailing replay configuration";return false;}
    // Recorded configuration never supplies a destination for saves, progress,
    // preferences or scripts. Resource roots come from the local installation.
    candidate.campaignLoadSavePath.clear();candidate.skirmishLoadSavePath.clear();
    candidate.campaignSavePath.clear();candidate.campaignIntroVideoPath.clear();
    candidate.campaignPostScoreVideoPath.clear();
    config=std::move(candidate);error.clear();return true;
}

bool writeRuntimeMatchReplayWorld(const RuntimeWindowState& state, std::string& data, std::string& error) {
    std::ostringstream out;out.imbue(std::locale::classic());
    if(!writeRuntimeStateToStream(state,out,error)
        || (!state.humanHouseAuthorities.empty() && !writeRuntimeMultiplayerSnapshotHouseSections(state,out,error))
        || !writeRuntimeMultiplayerSnapshotExtension(state,out,error)) return false;
    if(state.campaignMode) writeRuntimeCooperativeObservations(out,state,true);
    // Ordinary saves prepare/reconcile support skills for an immediate game
    // load. Replay startup must retain the exact pre-initialization state too.
    std::ostringstream superWeapons;state.superWeapons.save(superWeapons);
    out << "\n[ReplaySuperWeapons]\nSchema=1\nStateHex=" << iniHexEncode(superWeapons.str()) << '\n';
    if(!out) {error="replay state serialization failed";return false;}
    data=out.str();return true;
}

bool restoreRuntimeMatchReplayWorld(RuntimeWindowState& state, const std::string& configText,
    const std::string& data, int tick, std::uint64_t expectedHash, std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* failureTrace=nullptr,
    std::string* failureState=nullptr) {
    if(tick<0 || expectedHash==0) {
        error="invalid replay restore checkpoint";return false;
    }
    auto config=state.config;
    if(!deserializeRuntimeMatchReplayConfig(configText,config,error)) return false;
    // A load/reset belongs to this same map and rules session.
    if(config.defaultMapPath!=state.config.defaultMapPath || config.modRootPath!=state.config.modRootPath
        || config.gameMode!=state.config.gameMode || config.campaignSourceId!=state.config.campaignSourceId) {
        error="replay reset changes map or rules context";return false;
    }
    IniFile ini;if(!ini.loadFromString(data)) {error="invalid replay world state";return false;}
    auto scratch=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*scratch,state,false);
    scratch->config=std::move(config);
    // Unlike an in-session game load, a replay must not inherit whether the
    // previous viewer had vision initialized. Rebuild it at exact saved poses.
    if(!parseRuntimeStrictBool(ini.getValue("MultiplayerSubsystemFlags","Vision",""),scratch->visionEnabled)) {
        error="replay vision initialization flag is missing or invalid";return false;
    }
    if(!materializeRuntimeLoadStateFromIni(*scratch,ini,error,true)) return false;
    initializeRuntimeNavigation(*scratch);completeRuntimeWorldRefresh(*scratch);
    if(ini.getSection("MultiplayerHouses") && !materializeRuntimeMultiplayerSnapshotHouses(*scratch,ini,error)) return false;
    if(!materializeRuntimeMultiplayerSnapshotExtension(*scratch,ini,error)) return false;
    // Ordinary campaign loads reconstruct previous-frame observations. A
    // replay must preserve discovery, damage and selection edges exactly.
    if(scratch->campaignMode && !readRuntimeCooperativeObservations(ini,scratch->triggers,error,true)) return false;
    std::string superWeapons;IniFile superWeaponIni;
    if(ini.getValue("ReplaySuperWeapons","Schema","")!="1"
        || !iniHexDecode(ini.getValue("ReplaySuperWeapons","StateHex",""),superWeapons,runtimeMatchReplayMaximumRecordBytes/2)
        || !superWeaponIni.loadFromString(superWeapons) || !superWeaponIni.getSection("SuperWeapons")) {
        error="missing or invalid exact replay support-skill state";return false;
    }
    if(!scratch->superWeapons.load(superWeaponIni,&error)) return false;
    const auto restoredHash=runtimeMatchReplayStateHash(*scratch,error);
    if(scratch->tick!=tick || restoredHash==0 || restoredHash!=expectedHash) {
        if(failureTrace)runtimeMatchReplayStateHash(*scratch,error,failureTrace);
        if(failureState) {std::string diagnosticError;writeRuntimeMatchReplayWorld(*scratch,*failureState,diagnosticError);}
        error="replay initial/reset world checksum differs at tick "+std::to_string(tick)+": "+error;return false;
    }
    auto restoredConfig=std::move(scratch->config);
    static_assert(std::is_nothrow_move_assignable_v<RuntimeConfig>);
    commitRuntimeLoadedState(state,*scratch);
    state.config=std::move(restoredConfig);
    state.repaintRequested=true;
    return true;
}

