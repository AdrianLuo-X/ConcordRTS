bool testCooperativePlayersAndMissionRules() {
    auto options = hostOptions(4);
    options.map.canonicalId = "multimd/c1a02md.map";
    options.rules.gameMode = "cooperative";
    options.rules.rulesOverride = "mpcoopmd.ini";
    options.rules.cooperativeMission = {"c1a02md", 0xC0010002,
        {"americans", "british", "french"}, {"yuricountry"}};
    options.computerSlotIndices = {2, 3};
    RuntimeMultiplayerLobbyModel host;
    std::string error;
    if (!expect(RuntimeMultiplayerLobbyModel::createHost(options, host, error), "cooperative host: " + error)) return false;
    bool ok = expect(host.setup().slots[1].controller == RuntimeMultiplayerMatchSlotController::Closed
        && host.setup().slots[2].countryId == "yuricountry" && host.setup().slots[3].startPosition == 4,
        "cooperative B reserved, C/D remain authored enemies");
    const std::vector<std::string> hostReady{"host"};
    ok &= expect(!host.canStart(hostReady, error), "cooperative cannot start before the second player joins");
    std::vector<RuntimeMultiplayerRoomMember> members{roomMember("host", "Host"), roomMember("guest", "Guest")};
    ok &= expect(host.syncMembers(members).accepted, "cooperative guest joins B");
    const auto& b = host.setup().slots[1];
    ok &= expect(b.peerId == "guest" && b.startPosition == 2 && b.teamId == "1" && b.countryId == "americans", "guest receives fixed allied slot and valid country");
    auto changed = host.setSlotOptions(1, "french", "blue", "1", "medium", 2);
    ok &= expect(changed.accepted, "guest may choose another mission-allowed country");
    const auto before = host.setup();
    ok &= expect(!host.setSlotOptions(1, "russians", "blue", "1", "medium", 2).accepted && host.setup() == before, "enemy country cannot replace cooperative partner");
    ok &= expect(!host.setSlotOptions(1, "french", "blue", "2", "medium", 2).accepted, "cooperative coalition cannot be changed");
    ok &= expect(!host.setSlotOptions(1, "french", "blue", "1", "medium", 0).accepted, "cooperative start cannot become random");
    ok &= expect(!host.setSlotController(2, RuntimeMultiplayerMatchSlotController::Closed).accepted, "original enemy cannot be closed");
    members.push_back(roomMember("third", "Third"));
    ok &= expect(!host.syncMembers(members).accepted && host.setup() == before, "third combatant cannot occupy enemy slot");
    std::vector<std::string> ready{"host", "guest"};
    ok &= expect(host.canStart(ready, error), "two cooperative players can start: " + error);
    std::vector<std::uint8_t> bytes;
    RuntimeMultiplayerMatchSetup decoded;
    ok &= expect(encodeRuntimeMultiplayerMatchSetup(host.setup(), bytes, error)
        && decodeRuntimeMultiplayerMatchSetup(bytes, decoded, error) && decoded == host.setup(), "mission identity and countries survive room transfer");
    RuntimeMultiplayerMatchLaunchDescription hostLaunch, guestLaunch;
    ok &= expect(makeRuntimeMultiplayerMatchLaunchDescription(decoded, "host", hostLaunch, error)
        && makeRuntimeMultiplayerMatchLaunchDescription(decoded, "guest", guestLaunch, error)
        && hostLaunch.slots[0].local && !hostLaunch.slots[1].local
        && !guestLaunch.slots[0].local && guestLaunch.slots[1].local,
        "both peers launch the same mission with their own local human slot");
    if (ok) std::cout << "Cooperative lobby: A/B human=yes C/D authored enemies=yes countries=yes fixed allies/starts=yes room transfer=yes two launch perspectives=yes\n";
    return ok;
}
