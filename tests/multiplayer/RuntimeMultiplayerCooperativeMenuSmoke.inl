void testOriginalCooperativeHostSelections(SmokeContext& context) {
    const auto root = fs::path(__FILE__).parent_path().parent_path().parent_path();
    RuntimeConfig config;
    config.resourceRootPath = (root / "RedAlert2").string();
    config.localDataRootPath = (root / "local_data").string();
    IniFile ini;
    CooperativeScenarioDatabase scenarios;
    if (!ini.loadFromFile((root / "local_data/resources/mix_exports/ra2md/localmd/coopcampmd.ini").string())
        || !scenarios.loadFromIni(ini)) {
        context.expect(false, "original cooperative campaign data is unavailable");
        return;
    }
    context.expect(scenarios.scenarioCount() == 10, "all ten original cooperative missions are present");
    for (const auto& scenario : scenarios.scenarios()) {
        RuntimeSkirmishSelection selection;
        selection.accepted = true;
        selection.gameMode = "Cooperative";
        selection.gameModeRulesOverride = "MPCoopMD.ini";
        auto mapId = scenario.mapId;
        std::transform(mapId.begin(), mapId.end(), mapId.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        selection.mapPath = (root / "local_data/resources/mix_exports/multimd" / (mapId + ".map")).string();
        selection.slots.resize(8);
        for (std::size_t index = 0; index < selection.slots.size(); ++index) {
            auto& slot = selection.slots[index];
            slot.enabled = index < 4;
            slot.human = index == 0;
            if (!slot.enabled) continue;
            slot.country = index < 2 ? scenario.playerCountries.front() : scenario.enemyCountries.front();
            slot.color = "Random";
            slot.team = index < 2 ? "1" : "2";
            slot.difficulty = "Medium";
            slot.startPosition = static_cast<int>(index + 1);
        }
        RuntimeMultiplayerHostSelectionProjection projection;
        std::string error;
        if (!projectRuntimeMultiplayerHostSelection(config, selection, "Host", "host", 71, projection, error)) {
            context.expect(false, scenario.mapId + " cooperative host projection: " + error);
            continue;
        }
        context.expect(projection.slots.size() == 4 && projection.slots[1].controller == RuntimeMultiplayerMatchSlotController::Closed
            && projection.slots[2].controller == RuntimeMultiplayerMatchSlotController::Computer
            && projection.slots[3].controller == RuntimeMultiplayerMatchSlotController::Computer
            && projection.lobby.rules.cooperativeMission.scenarioFingerprint == scenario.fingerprint,
            scenario.mapId + " reserves B and binds original C/D and scenario identity");
        RuntimeMultiplayerLobbyModel lobby;
        if (!RuntimeMultiplayerLobbyModel::createHost(projection.lobby, lobby, error)) {
            context.expect(false, scenario.mapId + " create projected lobby: " + error); continue;
        }
        context.expect(lobby.replaceSlots(projection.slots).accepted, scenario.mapId + " selected setup retained");
        auto host = projection.lobby.hostMember;
        RuntimeMultiplayerRoomMember guest;
        guest.peerId = "guest"; guest.displayName = "Guest";
        guest.role = RuntimeMultiplayerRoomMemberRole::Client;
        guest.participation = RuntimeMultiplayerParticipationRole::Combatant;
        const std::vector<RuntimeMultiplayerRoomMember> members{host, guest};
        const auto joined = lobby.syncMembers(members);
        context.expect(joined.accepted && lobby.setup().slots[1].peerId == "guest", scenario.mapId + " second player joins B: " + joined.reason);
        const std::vector<std::string> ready{"host", "guest"};
        context.expect(lobby.canStart(ready, error), scenario.mapId + " both players can start: " + error);
    }
    if (context.failures == 0)
        std::cout << "Cooperative menu stock: missions=10 original countries=yes B reserved=yes second human joins=yes C/D preserved=yes ready to launch=yes\n";
}
