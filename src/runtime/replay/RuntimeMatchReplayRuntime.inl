// Included after the shared simulation/command/save implementations.
// Increment this identity whenever deterministic simulation semantics change.
constexpr const char* runtimeMatchReplaySimulationVersion = "ConcordRTS/ReplaySimulation/45";

bool runtimeViewingMatchReplay(const RuntimeWindowState& state) {
    return state.matchReplayViewing;
}

bool runtimeUsesGameplayIntentRouter(const RuntimeWindowState& state) {
    return state.multiplayerBridge != nullptr
        || (!state.campaignMode && runtimeLocalObserver(state.config))
        || (state.matchReplay && state.matchReplay->recording)
        || runtimeViewingMatchReplay(state);
}

void markRuntimeMatchReplayReset(RuntimeWindowState& state) noexcept {
    if (state.matchReplay && state.matchReplay->recording)
        state.matchReplay->resetPending = true;
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

std::uint64_t runtimeMatchReplayCompatibility(const RuntimeConfig& config, std::string& error) {
    std::uint64_t hash=1469598103934665603ULL;
    runtimeStableHashMixString(hash,runtimeMatchReplaySimulationVersion);
    runtimeStableHashMix(hash,runtimeMultiplayerGameplayCommandVersion);
    // Hash the actual mounted INI text, including all rule fields, rather than
    // the smaller gameplay-detail cache. No timestamp/path-only comparison.
    for(auto kind:{RuntimeCampaignIniKind::Rules,RuntimeCampaignIniKind::Art,RuntimeCampaignIniKind::Ai}) {
        std::string text;
        if(!readRuntimeIniSource(config,kind,text) || text.empty()) {
            error="replay rules/art/AI resources are unavailable";return 0;
        }
        runtimeStableHashMixString(hash,text);
    }
    const auto mixFile=[&](const std::string& path) {
        std::ifstream in(std::filesystem::u8path(path),std::ios::binary);
        std::array<char,65536> buffer{};std::uint64_t bytes=0;
        if(!in) return false;
        while(in) {
            in.read(buffer.data(),buffer.size());
            const auto count=in.gcount();bytes+=static_cast<std::uint64_t>(count);
            if(bytes>128ULL*1024*1024) return false;
            for(std::streamsize i=0;i<count;++i) {hash^=static_cast<unsigned char>(buffer[i]);hash*=1099511628211ULL;}
        }
        runtimeMultiplayerAuthoritativeMixUnsigned(hash,bytes);
        return in.eof() && bytes>0;
    };
    if(!mixFile(config.defaultMapPath)) {error="replay map cannot be read";return 0;}
    const auto mode=resolveRuntimeModeRulesOverridePath(config);
    if(!mode.empty() && !mixFile(mode)) {error="replay game-mode rules cannot be read";return 0;}
    error.clear();return hash?hash:1;
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
    if(scratch->tick!=tick || runtimeMatchReplayStateHash(*scratch,error)!=expectedHash) {
        if(failureTrace)runtimeMatchReplayStateHash(*scratch,error,failureTrace);
        if(failureState) {std::string diagnosticError;writeRuntimeMatchReplayWorld(*scratch,*failureState,diagnosticError);}
        error="replay initial/reset world checksum differs at tick "+std::to_string(tick)+": "+error;return false;
    }
    auto restoredConfig=std::move(scratch->config);
    commitRuntimeLoadedState(state,*scratch);
    state.config=std::move(restoredConfig);
    state.repaintRequested=true;
    return true;
}

void failRuntimeMatchReplay(RuntimeWindowState& state, const std::string& error) {
    auto& replay=*state.matchReplay;
    replay.failed=true;replay.paused=true;replay.error=error;
    state.interactionStatus="录像停止："+error;state.repaintRequested=true;
    std::cerr<<"Match replay failed: "<<error<<'\n';
}

bool appendRuntimeMatchReplay(RuntimeWindowState& state, const RuntimeMatchReplayRecord& record) {
    auto& replay=*state.matchReplay;
    if(replay.failed || replay.writer.finished()) return false;
    std::string error;
    if(!replay.writer.append(record,error)) {failRuntimeMatchReplay(state,error);return false;}
    replay.recordedTick=record.tick;return true;
}

bool flushRuntimeMatchReplayReset(RuntimeWindowState& state) {
    auto& replay=*state.matchReplay;
    if(!replay.resetPending) return !replay.failed;
    completeRuntimeWorldRefresh(state);
    RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Reset;record.tick=state.tick;
    record.config=serializeRuntimeMatchReplayConfig(state.config);
    std::string error;record.stateHash=runtimeMatchReplayStateHash(state,error);
    if(!record.stateHash || !writeRuntimeMatchReplayWorld(state,record.state,error)) {
        failRuntimeMatchReplay(state,error);return false;
    }
    if(!appendRuntimeMatchReplay(state,record)) return false;
    replay.resetPending=false;replay.selection.clear();return true;
}

void recordRuntimeMatchReplaySelection(RuntimeWindowState& state) {
    if(!state.matchReplay || !state.matchReplay->recording || state.matchReplay->failed) return;
    if(!flushRuntimeMatchReplayReset(state)) return;
    auto& replay=*state.matchReplay;
    if(replay.selection==state.selectedObjectIds) return;
    RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Selection;
    record.tick=state.tick;record.selection=state.selectedObjectIds;
    if(appendRuntimeMatchReplay(state,record)) replay.selection=record.selection;
}

void recordRuntimeMatchReplayCommand(RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayCommand& command, bool applied) {
    if(!state.matchReplay || !state.matchReplay->recording || state.matchReplay->failed) return;
    RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Command;
    record.tick=state.tick;record.owner=owner;record.command=command;record.applied=applied;
    appendRuntimeMatchReplay(state,record);
}

bool applyRuntimeRecordedGameplayCommand(RuntimeWindowState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayCommand& command) {
    std::string reason;
    if(state.config.multiplayerActive && !prevalidateRuntimeMultiplayerGameplayCommand(state,owner,command,reason)) return false;
    return applyRuntimeMultiplayerGameplayCommand(state,owner,command);
}

bool captureRuntimeOfflineReplayCommand(RuntimeWindowState& state,
    const RuntimeMultiplayerGameplayCommand& command, std::string& error) {
    if(runtimeViewingMatchReplay(state)) {error="录像观看中不能下达游戏命令";return false;}
    recordRuntimeMatchReplaySelection(state);
    const std::string owner=state.economy.owner;
    const bool applied=applyRuntimeRecordedGameplayCommand(state,owner,command);
    recordRuntimeMatchReplayCommand(state,owner,command,applied);
    if(!applied) error="当前状态无法执行此操作";else error.clear();
    return applied;
}

void recordRuntimeMatchReplayStep(RuntimeWindowState& state, bool lightweight) {
    if(!state.matchReplay || !state.matchReplay->recording || state.matchReplay->failed) return;
    RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Step;
    record.tick=state.tick;record.lightweight=lightweight;
    if(state.tick%std::max(1,state.config.fixedTickHz*5)==0 || state.skirmishMatchFinished) {
        completeRuntimeWorldRefresh(state);std::string error;
        record.stateHash=runtimeMatchReplayStateHash(state,error);
        if(!record.stateHash) {failRuntimeMatchReplay(state,error);return;}
    }
    appendRuntimeMatchReplay(state,record);
}

bool beginRuntimeMatchRecording(RuntimeWindowState& state, const std::filesystem::path& path, std::string& error) {
    completeRuntimeWorldRefresh(state);
    auto session=std::make_unique<RuntimeMatchReplaySession>();
    auto& header=session->header;
    header.initialTick=state.tick;header.fixedTickHz=state.config.fixedTickHz;
    header.config=serializeRuntimeMatchReplayConfig(state.config);
    header.compatibility=runtimeMatchReplayCompatibility(state.config,error);
    if(!header.compatibility) return false;
    header.initialHash=runtimeMatchReplayStateHash(state,error);
    if(!header.initialHash || !writeRuntimeMatchReplayWorld(state,header.initialState,error)) return false;
    session->path=path;session->partialPath=path;session->partialPath+=".part";
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);
    if(ec || std::filesystem::exists(path,ec) || std::filesystem::exists(session->partialPath,ec)) {
        error="录像目录不可写或文件名已存在";return false;
    }
    session->output.open(session->partialPath,std::ios::binary|std::ios::out);
    if(!session->output || !session->writer.begin(session->output,header,error)) {
        if(error.empty()) error="无法创建录像文件";return false;
    }
    session->recording=true;session->recordedTick=state.tick;
    state.matchReplay=std::move(session);
    if(!state.multiplayerBridge) state.multiplayerIntentRouter.setCaptureCallback(
        [&state](const auto& command,std::string& reason){return captureRuntimeOfflineReplayCommand(state,command,reason);});
    recordRuntimeMatchReplaySelection(state);
    std::cout<<"Match recording: "<<path.string()<<'\n';
    return true;
}

bool finishRuntimeMatchRecording(RuntimeWindowState& state, std::string& error) {
    if(!state.matchReplay || !state.matchReplay->recording) return true;
    auto& replay=*state.matchReplay;
    if(replay.writer.finished()) return !replay.failed;
    if(replay.failed) {error=replay.error;return false;}
    recordRuntimeMatchReplaySelection(state);
    if(replay.failed) {error=replay.error;return false;}
    completeRuntimeWorldRefresh(state);
    RuntimeMatchReplayRecord record;record.kind=RuntimeMatchReplayRecordKind::Finish;record.tick=state.tick;
    record.stateHash=runtimeMatchReplayStateHash(state,error);
    if(!record.stateHash || !appendRuntimeMatchReplay(state,record)) {
        if(error.empty()) error=replay.error;return false;
    }
    replay.output.close();
    if(replay.output.fail() || !MoveFileExW(replay.partialPath.c_str(),replay.path.c_str(),MOVEFILE_WRITE_THROUGH)) {
        error="录像提交失败，临时文件已保留";failRuntimeMatchReplay(state,error);return false;
    }
    std::cout<<"Match recording saved: "<<replay.path.string()<<'\n';return true;
}

bool openRuntimeMatchReplay(const std::filesystem::path& path, RuntimeConfig& config,
    std::unique_ptr<RuntimeMatchReplaySession>& session, std::string& error) {
    auto candidate=std::make_unique<RuntimeMatchReplaySession>();candidate->path=path;
    candidate->input.open(path,std::ios::binary);
    std::uint64_t steps=0,commands=0;
    if(!candidate->input || !validateRuntimeMatchReplay(candidate->input,candidate->header,steps,commands,error)) {
        if(error.empty()) error="无法打开录像文件";return false;
    }
    auto restoredConfig=config;
    if(!deserializeRuntimeMatchReplayConfig(candidate->header.config,restoredConfig,error)) return false;
    if(restoredConfig.fixedTickHz!=candidate->header.fixedTickHz
        || runtimeMatchReplayCompatibility(restoredConfig,error)!=candidate->header.compatibility) {
        error="录像版本或地图、规则内容与当前安装不一致："+error;return false;
    }
    candidate->input.clear();candidate->input.seekg(0);
    RuntimeMatchReplayHeader checked;
    if(!candidate->reader.begin(candidate->input,checked,error) || checked!=candidate->header) return false;
    candidate->viewing=true;
    config=std::move(restoredConfig);session=std::move(candidate);return true;
}

// One call processes the commands before the next frame and advances at most
// one simulation tick. There is no network bridge or user-command execution.
bool advanceRuntimeMatchReplay(RuntimeWindowState& state) {
    if(!state.matchReplay || !runtimeViewingMatchReplay(state)) return false;
    auto& replay=*state.matchReplay;
    if(replay.paused || replay.failed || replay.ended) return false;
    for(std::size_t work=0;work<4096;++work) {
        RuntimeMatchReplayRecord record;std::string error;
        if(!replay.reader.next(record,error)) {failRuntimeMatchReplay(state,error);return false;}
        bool advanced=false;
        switch(record.kind) {
        case RuntimeMatchReplayRecordKind::Selection:
            state.selectedObjectIds=record.selection;break;
        case RuntimeMatchReplayRecordKind::Command:
            if(applyRuntimeRecordedGameplayCommand(state,record.owner,record.command)!=record.applied) {
                failRuntimeMatchReplay(state,"命令结果与录像不同，帧 "+std::to_string(state.tick));return false;
            }
            break;
        case RuntimeMatchReplayRecordKind::Reset:
            if(!restoreRuntimeMatchReplayWorld(state,record.config,record.state,record.tick,record.stateHash,error)) {
                failRuntimeMatchReplay(state,error);return false;
            }
            break;
        case RuntimeMatchReplayRecordKind::Step:
            ++state.tick;advanceRuntimeSimulation(state,record.lightweight);advanced=true;break;
        case RuntimeMatchReplayRecordKind::Finish:
            replay.ended=true;replay.paused=true;state.interactionStatus="录像播放完毕，Esc 返回菜单";break;
        default:failRuntimeMatchReplay(state,"录像中出现意外的开局记录");return false;
        }
        if(record.stateHash) {
            completeRuntimeWorldRefresh(state);
            if(runtimeMatchReplayStateHash(state,error)!=record.stateHash) {
                failRuntimeMatchReplay(state,"状态校验失败，帧 "+std::to_string(state.tick)+": "+error);return false;
            }
        }
        state.repaintRequested=true;
        if(advanced || replay.ended) return advanced;
    }
    // Keep the window responsive even for a legitimate long run of commands.
    return false;
}

double runtimeMatchReplaySpeed(const RuntimeWindowState& state) {
    constexpr std::array<double,6> speeds{0.25,0.5,1.0,2.0,4.0,8.0};
    return state.matchReplay?speeds[std::clamp(state.matchReplay->speedIndex,0,5)]:1.0;
}

std::wstring runtimeMatchReplayWindowTitle(const RuntimeWindowState& state) {
    const auto& replay=*state.matchReplay;
    const std::wstring status=replay.failed?L"校验失败":replay.ended?L"播放完毕":replay.paused?L"暂停":L"播放";
    std::wostringstream speed;speed<<runtimeMatchReplaySpeed(state);
    return L"ConcordRTS 录像回放 | "+status+L" | "+speed.str()
        +L" 倍 | 帧 "+std::to_wstring(state.tick)+L" | 空格 暂停 / +/- 速度 / 方向键 移动视角 / Esc 返回";
}

std::filesystem::path chooseRuntimeMatchReplayFile(HWND owner, const RuntimeConfig& config) {
    std::array<wchar_t,32768> path{};
    const auto folder=std::filesystem::u8path(config.localDataRootPath)/"replays";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
    dialog.lpstrFile=path.data();dialog.nMaxFile=static_cast<DWORD>(path.size());
    dialog.lpstrInitialDir=folder.c_str();dialog.lpstrTitle=L"选择对局录像";
    dialog.lpstrFilter=L"ConcordRTS 对局录像 (*.creplay)\0*.creplay\0\0";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    return GetOpenFileNameW(&dialog)?std::filesystem::path(path.data()):std::filesystem::path{};
}
