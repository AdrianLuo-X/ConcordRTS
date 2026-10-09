bool runRuntimeNetworkCooperativeLanSmoke(const RuntimeConfig& config, std::string& error);

bool prepareRuntimeCooperativeSmokeRoom(const RuntimeConfig& config,
    const CooperativeScenarioDefinition& scenario, RuntimeMultiplayerMatchSetup& setup,
    std::filesystem::path& mapPath, std::string& error) {
    RuntimeSkirmishSelection selection;
    selection.accepted = true; selection.gameMode = "Cooperative";
    selection.gameModeRulesOverride = "MPCoopMD.ini";
    selection.startingCredits = 10000; selection.unitCount = 5;
    selection.computerAiEnabled = true;
    selection.mapPath = (std::filesystem::path(config.localDataRootPath)
        / "resources/mix_exports/multimd" / (toLowerAsciiLocal(scenario.mapId) + ".map")).string();
    selection.slots.resize(4);
    for (std::size_t index = 0; index < selection.slots.size(); ++index) {
        auto& slot = selection.slots[index]; slot.enabled = true; slot.human = index == 0;
        slot.country = index < 2 ? scenario.playerCountries.front()
            : scenario.enemyCountries[(index - 2) % scenario.enemyCountries.size()];
        slot.color = "Random"; slot.team = index < 2 ? "1" : "2";
        slot.difficulty = "Medium"; slot.startPosition = static_cast<int>(index + 1);
    }
    RuntimeMultiplayerHostSelectionProjection projection;
    if (!projectRuntimeMultiplayerHostSelection(config, selection, "Player A", "host", 71, projection, error)) return false;
    RuntimeMultiplayerLobbyModel lobby;
    if (!RuntimeMultiplayerLobbyModel::createHost(projection.lobby, lobby, error)) return false;
    const auto replaced = lobby.replaceSlots(projection.slots);
    if (!replaced.accepted) { error = replaced.reason; return false; }
    RuntimeMultiplayerRoomMember guest;
    guest.peerId = "guest"; guest.displayName = "Player B";
    guest.role = RuntimeMultiplayerRoomMemberRole::Client;
    guest.participation = RuntimeMultiplayerParticipationRole::Combatant;
    const std::vector<RuntimeMultiplayerRoomMember> members{projection.lobby.hostMember, guest};
    const auto joined = lobby.syncMembers(members);
    if (!joined.accepted) { error = joined.reason; return false; }
    setup = lobby.setup(); mapPath = projection.localMapPath;
    return true;
}

bool initializeRuntimeCooperativeSmokeWorld(RuntimeWindowState& state,
    const RuntimeConfig& config, const RuntimeMultiplayerMatchSetup& setup,
    const std::filesystem::path& mapPath, bool second, std::string& error) {
    RuntimeSnapshotActiveStateScope active(state);
    RuntimeMultiplayerLaunchConfigResult launch;
    if (!makeRuntimeMultiplayerLaunchConfig(setup, second ? "guest" : "host", mapPath.string(),
        setup.map.contentFingerprint, config, launch, error)) return false;
    state.config = launch.config;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world, &state.theater,
        &state.art, &state.rules, &state.mapFacts);
    state.theaterLoaded = state.report.theaterLoaded; state.artLoaded = state.report.artLoaded;
    state.rulesLoaded = state.report.rulesLoaded;
    if (!state.report.success) { error = "original map startup report failed"; return false; }
    if (!initializeRuntimeCooperativeSession(state)) { error = state.cooperativeSessionStatus; return false; }
    loadRuntimeSoundDatabase(state);
    initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
    if (!initializeRuntimeSkirmishStart(state)) { error = state.economy.skirmishStatus; return false; }
    if (!initializeRuntimeTriggers(state)) { error = state.triggers.status; return false; }
    state.visionEnabled = true; updateRuntimeVisionState(state);
    completeRuntimeWorldRefresh(state);
    if (state.humanHouseAuthorities.size() != 2) { error = "two independent human accounts missing"; return false; }
    return true;
}

bool compareRuntimeCooperativeSmokeWorlds(const RuntimeWindowState& first,
    const RuntimeWindowState& second, std::string& error) {
    const auto a = runtimeMultiplayerAuthoritativeStateHash(first, error);
    if (a == 0) { error = "tick=" + std::to_string(first.tick) + " " + error; return false; }
    const auto b = runtimeMultiplayerAuthoritativeStateHash(second, error);
    if (b == a) return true;
    recordRuntimeCooperativeSnapshotDifference(first, second);
    for (const auto& entry : {std::pair{"first", &first}, std::pair{"second", &second}}) {
        std::ofstream output(std::string("build/smoke/gameplay/06-world-probe/") + entry.first + "-houses.ini");
        std::string detail;
        writeRuntimeMultiplayerSnapshotHouseSections(*entry.second, output, detail);
    }
    error = "cooperative worlds differ at tick " + std::to_string(first.tick)
        + " domains=" + runtimeMultiplayerSnapshotAuthorityDifferenceSummary(first, second)
        + " " + error;
    return false;
}

void advanceRuntimeCooperativeSmokeWorld(RuntimeWindowState& state) {
    RuntimeSnapshotActiveStateScope active(state);
    ++state.tick; advanceRuntimeSimulation(state, false);
}

#include "RuntimeNetworkCooperativeOutcomeSmoke.inl"

bool runRuntimeNetworkCooperativeSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    IniFile ini; CooperativeScenarioDatabase scenarios;
    if (!ini.loadFromFile(runtimeCooperativeScenarioPath(config).generic_string()) || !scenarios.loadFromIni(ini)) {
        error = "cooperative original campaign unavailable"; return false;
    }
    int checked = 0;
    for (const auto& scenario : scenarios.scenarios()) {
        if (const char* selected = std::getenv("CONCORD_COOPERATIVE_ONE_MAP");
            selected && toLowerAsciiLocal(selected) != toLowerAsciiLocal(scenario.mapId)) continue;
        std::cout << "Cooperative world initializing: " << scenario.mapId << '\n';
        RuntimeMultiplayerMatchSetup setup; std::filesystem::path mapPath;
        if (!prepareRuntimeCooperativeSmokeRoom(config, scenario, setup, mapPath, error)) return false;
        auto first = std::make_unique<RuntimeWindowState>();
        auto second = std::make_unique<RuntimeWindowState>();
        if (!initializeRuntimeCooperativeSmokeWorld(*first, config, setup, mapPath, false, error)
            || !initializeRuntimeCooperativeSmokeWorld(*second, config, setup, mapPath, true, error)
            || !compareRuntimeCooperativeSmokeWorlds(*first, *second, error)) return false;
        for (int frame = 0; frame < 240; ++frame) {
            // Deliberately different presentation and selection on the two peers.
            first->selectedObjectIds.clear();
            second->selectedObjectIds = {second->economy.skirmishStartObjectId};
            advanceRuntimeCooperativeSmokeWorld(*first);
            advanceRuntimeCooperativeSmokeWorld(*second);
            // Use the production 30-tick comparison interval during startup;
            // the first ten resumed ticks below are still checked individually.
            if ((frame + 1) % 30 == 0
                && !compareRuntimeCooperativeSmokeWorlds(*first, *second, error)) return false;
            if (first->skirmishMatchFinished) { error = "cooperative match ended during normal startup"; return false; }
        }
        if (first->triggers.firedTriggers == 0 || first->triggers.unsupportedActions != 0
            || first->triggers.unsupportedEvents != 0) {
            error = scenario.mapId + " authored events incomplete: " + first->triggers.status
                + " actions=" + std::to_string(first->triggers.unsupportedActions)
                + " events=" + std::to_string(first->triggers.unsupportedEvents); return false;
        }
        RuntimeMultiplayerSnapshotIdentity identity;
        identity.proposalId = 1; identity.snapshotToken = "cooperative-world-progress";
        identity.sourceTimelineEpoch = first->config.multiplayerTimelineEpoch;
        identity.barrierTick = first->tick; identity.snapshotTick = first->tick;
        identity.gameplaySetupFingerprint = first->config.multiplayerSetupHash;
        identity.authoritativeStateHash = runtimeMultiplayerAuthoritativeStateHash(*first, error);
        identity.kind = RuntimeMultiplayerSnapshotKind::Load;
        RuntimeMultiplayerSnapshotMemoryImage image;
        RuntimeMultiplayerSnapshotStagedState staged;
        if (!exportRuntimeMultiplayerSnapshotV15(*first, identity, image, error)
            || !stageRuntimeMultiplayerSnapshot(*second, image.bytes, image.descriptor, staged, error)) {
            std::ofstream failed("build/smoke/gameplay/06-world-probe/failed-mission-snapshot.ini", std::ios::binary);
            if (!image.bytes.empty()) failed.write(reinterpret_cast<const char*>(image.bytes.data()), image.bytes.size());
            return false;
        }
        commitRuntimeMultiplayerSnapshotAuthority(*second, std::move(staged));
        if (toLowerAsciiLocal(scenario.mapId) == "c1a01md"
            && !runRuntimeCooperativeOutcomeSmoke(*first, *second, image, error)) return false;
        for (int frame = 0; frame < 10; ++frame) {
            advanceRuntimeCooperativeSmokeWorld(*first); advanceRuntimeCooperativeSmokeWorld(*second);
            if (!compareRuntimeCooperativeSmokeWorlds(*first, *second, error)) return false;
        }
        std::cout << "Cooperative world: " << scenario.mapId << " A/B independent=yes original events="
            << first->triggers.firedTriggers << " same simulation=yes progress snapshot/resume=yes\n";
        ++checked;
    }
    if (!checked) { error = "no cooperative mission checked"; return false; }
    std::cout << "Cooperative actual worlds: missions=" << checked << " PASS\n";
    return true;
}
