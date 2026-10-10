#include "RuntimeMultiplayerResumeFixture.h"

void runResumeLobby(SmokeContext& context) {
    const auto path = std::filesystem::temp_directory_path() / ("concord_resume_lobby_"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".map");
    struct RemoveFixture { std::filesystem::path path;~RemoveFixture() { std::error_code e;std::filesystem::remove(path, e); } } cleanup{path};
    { std::ofstream out(path, std::ios::binary);out << "[Map]\nTheater=TEMPERATE\nResume=1\n"; }
    auto options = lobbyOptions(mapFingerprint(path, "multimd/lobby-flow.map", context));
    RuntimeMultiplayerMatchSetup original;
    original.revision = 17;original.hostPeerId = "old-host";
    original.map = options.map;original.rules = options.rules;
    original.members = {{"old-host", "Host", RuntimeMultiplayerMatchMemberIdentity::Combatant},
        {"old-client", "Client", RuntimeMultiplayerMatchMemberIdentity::Combatant}};
    original.slots = {{0}, {1, RuntimeMultiplayerMatchSlotController::Computer, "", "germans", "green", "none", "medium", 3},
        {2, RuntimeMultiplayerMatchSlotController::Human, "old-host", "americans", "gold", "1", "medium", 1},
        {3, RuntimeMultiplayerMatchSlotController::Human, "old-client", "russians", "red", "2", "medium", 2}};
    std::string error;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(original, error)) { context.expect(false, error);return; }
    RuntimeMultiplayerResumeSave save;
    if (!makeResumeSaveFixture(original, compatibility(), save, error)) { context.expect(false, error);return; }
    RuntimeMultiplayerLobbyFlow host, client;
    if (!host.startHost(networkOptions(), options, error, &save)) { context.expect(false, "resume host: " + error);return; }
    context.expect(host.resuming() && host.lobby().setup().slots[2].peerId == "host-peer"
        && host.lobby().setup().slots[0].controller == RuntimeMultiplayerMatchSlotController::Closed
        && host.lobby().setup().slots[1] == original.slots[1], "restore nonzero host seat, AI and authored Closed hole");
    context.expect(!host.resumeReady(error) && !host.startMatch().succeeded, "missing saved human blocks start even with an AI");
    const auto frozen = host.lobby().setup();
    context.expect(!host.setMapAndRules(options.map, options.rules, error).accepted
        && !host.setSlotOptions(2, "russians", "gold", "1", "medium", 1, error).accepted
        && !host.setSlotController(0, RuntimeMultiplayerMatchSlotController::Human, "host-peer", error).accepted
        && host.lobby().setup() == frozen, "restore settings and Closed seats cannot be edited");
    if (!client.join(multiClientJoin(host, "new-client", "Client"), error)) { context.expect(false, error);return; }
    bool observedIncomplete = false;
    if (!pumpUntil(host, client, [&]() {
        if (client.bridge()->lan().lobbyResumeDescriptor() && !client.bridge()->lan().lobbyResumeSnapshotReady()) {
            observedIncomplete = true;
            context.expect(!client.setReady(true).succeeded && !host.startMatch().succeeded, "partial image cannot become ready or start");
        }
        return host.resumeReady(error) && client.resumeReady(error) && host.bridge()->lan().lobbyResumeAllPeersReady();
    }, context, "multi-chunk cold save transfer")) return;
    context.expect(observedIncomplete && client.bridge()->lan().lobbyResumeSnapshotBytes() == save.bytes,
        "all chunks must be digest verified before exposing the identical save");
    context.expect(host.lobby().setup().slots[3].peerId == "new-client" && host.lobby().setup() == client.lobby().setup(),
        "new identity rebinds original client seat");
    context.expect(!client.requestLocalSlotOptions("germans", "red", "2", "medium", 2).succeeded, "client cannot modify saved seat options");
    context.expect(host.setSlotController(3, RuntimeMultiplayerMatchSlotController::Human, "host-peer", error).accepted,
        "host may exchange Human seat identities without changing saved gameplay");
    if (!pumpUntil(host, client, [&]() { return host.lobby().setup() == client.lobby().setup(); }, context, "resume seat exchange")) return;
    context.expect(host.lobby().setup().slots[2].peerId == "new-client" && host.lobby().setup().slots[2].countryId == "americans"
        && host.lobby().setup().slots[3].countryId == "russians", "seat exchange leaves armies/options at original indices");
    context.expect(host.verifyLocalMap(path, error) && client.verifyLocalMap(path, error), "resume local map verification: " + error);
    context.expect(host.setReady(true).succeeded && client.setReady(true).succeeded, "verified resume members become ready");
    if (!pumpUntil(host, client, [&]() { return host.canStart(error); }, context, "resume readiness")) return;
    context.expect(!host.bridge()->lan().startMatch(1).succeeded, "transport refuses a fresh tick for a saved room");
    context.expect(host.startMatch().succeeded, "resume starts at saved next tick");
    if (!pumpUntil(host, client, [&]() { return client.bridge()->lan().phase() == RuntimeMultiplayerLanPhase::Running; }, context, "resume start")) return;
    RuntimeMultiplayerLaunchConfigResult hostLaunch, clientLaunch;
    context.expect(host.makeLaunchConfig({}, hostLaunch, error) && client.makeLaunchConfig({}, clientLaunch, error)
        && hostLaunch.config.simulationRandomSeed == original.rules.randomSeed
        && hostLaunch.config.multiplayerSetupHash == save.descriptor.identity.gameplaySetupFingerprint
        && host.bridge()->lan().session().authoritativeTick() == 901
        && client.bridge()->lan().session().authoritativeTick() == 901
        && host.bridge()->lan().session().timelineEpoch() == 1, "fresh session preserves original seed/recipe and saved tick without old epoch");
    auto hostBridge = host.takeRunningBridge(error), clientBridge = client.takeRunningBridge(error);
    context.expect(hostBridge && clientBridge && hostBridge->lan().lobbyResumeSnapshotBytes() == save.bytes
        && clientBridge->lan().lobbyResumeSnapshotBytes() == save.bytes, "cold save survives menu ownership handoff");
    std::cout << "Resume lobby: original seats/seed/map, frozen options, partial transfer gating, SHA-256 bytes, seat exchange and saved tick passed.\n";
}
