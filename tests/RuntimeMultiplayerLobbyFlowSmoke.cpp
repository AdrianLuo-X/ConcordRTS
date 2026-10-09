#include "RuntimeMultiplayerCompatibilityFingerprint.h"
#include "RuntimeMultiplayerLobbyFlow.h"
#include "RuntimeMultiplayerMapCache.h"
#include "RuntimeMultiplayerResumeFixture.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

RuntimeMultiplayerCompatibility compatibility() {
    RuntimeMultiplayerCompatibility value;
    value.protocolFingerprint = 0x1111222233334444ULL;
    value.contentFingerprint = 0x5555666677778888ULL;
    return value;
}

std::uint64_t mapFingerprint(
    const std::filesystem::path& path,
    const std::string& canonicalId,
    SmokeContext& context
) {
    RuntimeMultiplayerContentComponent component;
    std::string error;
    context.expect(
        loadRuntimeMultiplayerContentComponentFile(
            path,
            canonicalId,
            component,
            error
        ),
        "fixture map should load: " + error
    );
    std::uint64_t fingerprint = 0;
    context.expect(
        runtimeMultiplayerContentFingerprint(
            {component},
            fingerprint,
            error
        ),
        "fixture map should fingerprint: " + error
    );
    return fingerprint;
}

RuntimeMultiplayerLobbyHostCreateOptions lobbyOptions(
    std::uint64_t fingerprint
) {
    RuntimeMultiplayerLobbyHostCreateOptions options;
    options.map.canonicalId = "multimd/lobby-flow.map";
    options.map.contentFingerprint = fingerprint;
    options.rules.gameMode = "battle";
    options.rules.rulesOverride = "mpbattlemd.ini";
    options.rules.randomSeed = 0x1029384756ULL;
    options.rules.fixedTickHz = 30;
    options.rules.startingCredits = 10000;
    options.rules.startingUnits = 10;
    options.rules.techLevel = 10;
    options.rules.gameSpeed = 2;
    options.hostMember.peerId = "host-peer";
    options.hostMember.displayName = "Host";
    options.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    options.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.slotCount = 3;
    // Keep slot one Closed for a joining human while still publishing a
    // startable initial host+computer draft.
    options.computerSlotIndices = {2U};
    return options;
}

RuntimeMultiplayerLanHostOptions networkOptions() {
    RuntimeMultiplayerLanHostOptions options;
    options.session.roomId = "lobby-flow-room";
    options.session.hostPeerId = "host-peer";
    options.session.hostDisplayName = "Host";
    options.session.hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.session.compatibility = compatibility();
    options.session.stateHashPeriodTicks = 1;
    options.roomName = "Lobby Flow";
    options.discoveryPort = 0;
    options.sessionPort = 0;
    options.roomGeneration = 0x778899ULL;
    return options;
}

RuntimeLanRoomAdvertisement advertisementFor(
    const RuntimeMultiplayerLobbyFlow& host
) {
    RuntimeLanRoomAdvertisement room;
    room.roomId = "lobby-flow-room";
    room.roomName = "Lobby Flow";
    room.hostName = "Host";
    room.hostAddress = "127.0.0.1";
    room.contentFingerprint = "5555666677778888";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = host.bridge()->lan().sessionPort();
    room.playerCount = 1;
    room.maximumPlayers = 8;
    return room;
}

bool pumpUntil(
    RuntimeMultiplayerLobbyFlow& host,
    RuntimeMultiplayerLobbyFlow& client,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLobbyFlowPollResult hostPoll = host.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientPoll = client.poll();
        if (!hostPoll.succeeded || !clientPoll.succeeded) {
            context.expect(
                false,
                description + ": poll failed: "
                    + (!hostPoll.succeeded ? hostPoll.error : clientPoll.error)
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

void runLobbyFlow(SmokeContext& context) {
    const std::filesystem::path mapPath =
        std::filesystem::temp_directory_path()
        / ("concord_lobby_flow_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            )
            + ".map");
    {
        std::ofstream output(mapPath, std::ios::binary | std::ios::trunc);
        output << "[Map]\nTheater=TEMPERATE\n[IsoMapPack5]\n1=fixture\n";
    }
    const std::uint64_t fingerprint = mapFingerprint(
        mapPath,
        "multimd/lobby-flow.map",
        context
    );

    RuntimeMultiplayerLobbyFlow host;
    RuntimeMultiplayerLobbyFlow client;
    std::string error;
    context.expect(
        host.startHost(
            networkOptions(),
            lobbyOptions(fingerprint),
            error
        ),
        "host lobby flow should start: " + error
    );
    context.expect(
        host.canonicalSetupPublished(),
        "host+computer setup should publish before a remote join"
    );
    error.clear();
    context.expect(
        host.verifyLocalMap(mapPath, error),
        "host map should verify: " + error
    );

    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = "client-peer";
    join.displayName = "Client";
    join.participation = RuntimeMultiplayerParticipationRole::Combatant;
    join.compatibility = compatibility();
    join.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        client.join(join, error),
        "client lobby flow should join: " + error
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.canonicalSetupPublished()
                    && client.canonicalSetupPublished()
                    && host.lobby().setup().members.size() == 2U
                    && client.lobby().setup().members.size() == 2U
                    && host.lobby().setup() == client.lobby().setup();
            },
            context,
            "roster and setup publication"
        )) {
        host.close();
        client.close();
        std::filesystem::remove(mapPath);
        return;
    }

    context.expect(
        !host.localMapVerified(),
        "roster revision should conservatively invalidate host verification"
    );
    context.expect(
        !host.setReady(true).succeeded,
        "unverified host must not become ready"
    );
    error.clear();
    context.expect(
        host.verifyLocalMap(mapPath, error),
        "host map should reverify after roster publication: " + error
    );
    error.clear();
    context.expect(
        client.verifyLocalMap(mapPath, error),
        "client map should verify: " + error
    );

    const RuntimeMultiplayerLanOperationResult hostReady = host.setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.setReady(true);
    context.expect(
        hostReady.succeeded && hostReady.applied,
        "host ready should apply: " + hostReady.reason
    );
    context.expect(
        clientReady.succeeded && clientReady.queued,
        "client ready should queue: " + clientReady.reason
    );
    pumpUntil(
        host,
        client,
        [&host, &client]() {
            std::string hostError;
            return host.canStart(hostError)
                && host.bridge()->lan().session().allMembersReady()
                && client.bridge()->lan().session().allMembersReady();
        },
        context,
        "ready consensus"
    );

    const RuntimeMultiplayerLanOperationResult start = host.startMatch(1);
    context.expect(
        start.succeeded && start.applied,
        "host should start verified ready room: " + start.reason
    );
    pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.bridge()->lan().phase()
                    == RuntimeMultiplayerLanPhase::Running
                && client.bridge()->lan().phase()
                    == RuntimeMultiplayerLanPhase::Running;
        },
        context,
        "running transition"
    );

    RuntimeConfig base;
    base.resourceRootPath = "host/resources";
    base.localDataRootPath = "host/local";
    RuntimeMultiplayerLaunchConfigResult hostLaunch;
    RuntimeMultiplayerLaunchConfigResult clientLaunch;
    error.clear();
    context.expect(
        host.makeLaunchConfig(base, hostLaunch, error),
        "host launch should project: " + error
    );
    error.clear();
    context.expect(
        client.makeLaunchConfig(base, clientLaunch, error),
        "client launch should project: " + error
    );
    context.expect(
        hostLaunch.authority == clientLaunch.authority
            && hostLaunch.config.multiplayerCanonicalMapId
                == "multimd/lobby-flow.map"
            && hostLaunch.config.multiplayerMapContentFingerprint
                == fingerprint
            && hostLaunch.localPeerIsHost
            && !clientLaunch.localPeerIsHost
            && hostLaunch.config.defaultMapPath == mapPath.string()
            && clientLaunch.config.defaultMapPath == mapPath.string(),
        "both launches should share authority and retain local-only map paths"
    );

    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> hostBridge =
        host.takeRunningBridge(error);
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> clientBridge =
        client.takeRunningBridge(error);
    context.expect(
        hostBridge != nullptr && clientBridge != nullptr,
        "running bridges should transfer into RuntimeShell ownership"
    );
    if (hostBridge) hostBridge->lan().close();
    if (clientBridge) clientBridge->lan().close();
    std::filesystem::remove(mapPath);
}

RuntimeMultiplayerLobbyHostCreateOptions multiClientLobbyOptions(
    std::uint64_t fingerprint
) {
    RuntimeMultiplayerLobbyHostCreateOptions options = lobbyOptions(
        fingerprint
    );
    options.slotCount = 4;
    options.computerSlotIndices = {3U};
    return options;
}

RuntimeMultiplayerLanJoinOptions multiClientJoin(
    const RuntimeMultiplayerLobbyFlow& host,
    std::string peerId,
    std::string displayName,
    RuntimeMultiplayerParticipationRole participation =
        RuntimeMultiplayerParticipationRole::Combatant
) {
    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = std::move(peerId);
    join.displayName = std::move(displayName);
    join.participation = participation;
    join.compatibility = compatibility();
    join.connectTimeoutMilliseconds = 2000;
    return join;
}

bool pumpThreeUntil(
    RuntimeMultiplayerLobbyFlow& host,
    RuntimeMultiplayerLobbyFlow& clientA,
    RuntimeMultiplayerLobbyFlow& clientB,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLobbyFlowPollResult hostPoll = host.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientAPoll =
            clientA.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientBPoll =
            clientB.poll();
        if (!hostPoll.succeeded || !clientAPoll.succeeded
            || !clientBPoll.succeeded) {
            context.expect(
                false,
                description + ": poll failed: "
                    + (!hostPoll.succeeded
                        ? hostPoll.error
                        : (!clientAPoll.succeeded
                            ? clientAPoll.error
                            : clientBPoll.error))
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

std::size_t humanSlotFor(
    const RuntimeMultiplayerLobbyFlow& flow,
    const std::string& peerId
) {
    const auto slot = std::find_if(
        flow.lobby().setup().slots.begin(),
        flow.lobby().setup().slots.end(),
        [&](const RuntimeMultiplayerMatchSlot& candidate) {
            return candidate.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && candidate.peerId == peerId;
        }
    );
    return slot == flow.lobby().setup().slots.end()
        ? flow.lobby().setup().slots.size()
        : static_cast<std::size_t>(slot->slotIndex);
}

struct SlotResultProbe {
    bool found = false;
    RuntimeMultiplayerLobbySlotOptionsResultMessage result;
};

bool waitForSlotResult(
    RuntimeMultiplayerLobbyFlow& host,
    RuntimeMultiplayerLobbyFlow& clientA,
    RuntimeMultiplayerLobbyFlow& clientB,
    RuntimeMultiplayerLobbyFlow& requester,
    std::uint64_t requestId,
    SlotResultProbe& probe,
    SmokeContext& context,
    const std::string& description
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLobbyFlowPollResult hostPoll = host.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientAPoll =
            clientA.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientBPoll =
            clientB.poll();
        if (!hostPoll.succeeded || !clientAPoll.succeeded
            || !clientBPoll.succeeded) {
            context.expect(false, description + ": lobby poll failed");
            return false;
        }
        const RuntimeMultiplayerLobbyFlowPollResult* requesterPoll =
            &clientAPoll;
        if (&requester == &clientB) requesterPoll = &clientBPoll;
        for (const RuntimeMultiplayerLanEvent& event :
                requesterPoll->events) {
            if (event.kind
                    == RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult
                && event.lobbySlotOptionsResult.has_value()
                && event.lobbySlotOptionsResult->requestId == requestId) {
                probe.found = true;
                probe.result = *event.lobbySlotOptionsResult;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": result timed out");
    return false;
}

void runFormalLobbyControls(SmokeContext& context) {
    const std::filesystem::path mapPath =
        std::filesystem::temp_directory_path()
        / ("concord_formal_lobby_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            )
            + ".map");
    {
        std::ofstream output(mapPath, std::ios::binary | std::ios::trunc);
        output << "[Map]\nTheater=TEMPERATE\n[IsoMapPack5]\n1=formal\n";
    }
    const std::uint64_t fingerprint = mapFingerprint(
        mapPath,
        "multimd/lobby-flow.map",
        context
    );

    RuntimeMultiplayerLobbyFlow host;
    RuntimeMultiplayerLobbyFlow clientA;
    RuntimeMultiplayerLobbyFlow clientB;
    std::string error;
    context.expect(
        host.startHost(
            networkOptions(),
            multiClientLobbyOptions(fingerprint),
            error
        ),
        "formal lobby host should start: " + error
    );
    error.clear();
    context.expect(
        clientA.join(
            multiClientJoin(host, "client-a", "Client A"),
            error
        ),
        "formal lobby client A should join: " + error
    );
    error.clear();
    context.expect(
        clientB.join(
            multiClientJoin(host, "client-b", "Client B"),
            error
        ),
        "formal lobby client B should join: " + error
    );
    if (!pumpThreeUntil(
            host,
            clientA,
            clientB,
            [&]() {
                return host.lobby().setup().members.size() == 3U
                    && clientA.lobby().initialized()
                    && clientB.lobby().initialized()
                    && host.lobby().setup() == clientA.lobby().setup()
                    && host.lobby().setup() == clientB.lobby().setup();
            },
            context,
            "formal lobby three-player roster"
        )) {
        host.close();
        clientA.close();
        clientB.close();
        std::filesystem::remove(mapPath);
        return;
    }

    const std::size_t slotA = humanSlotFor(host, "client-a");
    const std::size_t slotB = humanSlotFor(host, "client-b");
    context.expect(
        slotA < host.lobby().setup().slots.size()
            && slotB < host.lobby().setup().slots.size()
            && slotA != slotB,
        "each client should own one distinct Human slot"
    );

    error.clear();
    context.expect(host.verifyLocalMap(mapPath, error), "formal host map");
    error.clear();
    context.expect(clientA.verifyLocalMap(mapPath, error), "formal A map");
    error.clear();
    context.expect(clientB.verifyLocalMap(mapPath, error), "formal B map");
    context.expect(host.setReady(true).succeeded, "formal host ready");
    context.expect(clientA.setReady(true).succeeded, "formal A ready");
    context.expect(clientB.setReady(true).succeeded, "formal B ready");
    pumpThreeUntil(
        host,
        clientA,
        clientB,
        [&]() {
            return host.bridge()->lan().session().allMembersReady();
        },
        context,
        "formal lobby initial ready consensus"
    );

    std::uint64_t revision = host.lobby().setup().revision;
    RuntimeMultiplayerLanOperationResult request =
        clientA.requestLocalSlotOptions(
            "america",
            "red",
            "none",
            "medium",
            1
        );
    context.expect(
        request.succeeded && request.queued,
        "client A slot request should queue: " + request.reason
    );
    SlotResultProbe outcome;
    waitForSlotResult(
        host,
        clientA,
        clientB,
        clientA,
        request.requestId,
        outcome,
        context,
        "client A accepted slot request"
    );
    context.expect(
        outcome.found && outcome.result.accepted
            && outcome.result.currentSetupRevision == revision + 1U
            && host.lobby().setup().slots[slotA].colorId == "red",
        "client A should receive accepted result at the new revision"
    );
    context.expect(
        std::none_of(
            host.bridge()->lan().session().members().begin(),
            host.bridge()->lan().session().members().end(),
            [](const RuntimeMultiplayerRoomMember& member) {
                return member.ready;
            }
        ),
        "accepted slot change should clear every ready flag"
    );

    revision = host.lobby().setup().revision;
    request = clientB.requestLocalSlotOptions(
        "korea",
        "blue",
        "none",
        "medium",
        2
    );
    outcome = {};
    waitForSlotResult(
        host,
        clientA,
        clientB,
        clientB,
        request.requestId,
        outcome,
        context,
        "client B accepted slot request"
    );
    context.expect(
        outcome.found && outcome.result.accepted
            && outcome.result.currentSetupRevision == revision + 1U
            && host.lobby().setup().slots[slotB].colorId == "blue",
        "client B should receive accepted result at the new revision"
    );

    const auto rejectedRequest = [&](std::uint64_t requestId,
                                     std::uint64_t expectedRevision,
                                     std::size_t targetSlot,
                                     std::string color,
                                     std::uint8_t start,
                                     std::string_view reasonFragment,
                                     const std::string& label) {
        RuntimeMultiplayerLobbySlotOptionsRequestMessage explicitRequest;
        explicitRequest.peerId = "client-a";
        explicitRequest.requestId = requestId;
        explicitRequest.expectedSetupRevision = expectedRevision;
        explicitRequest.slotIndex = static_cast<std::uint8_t>(targetSlot);
        explicitRequest.countryId = "america";
        explicitRequest.colorId = std::move(color);
        explicitRequest.teamId = "none";
        explicitRequest.difficultyId = "medium";
        explicitRequest.startPosition = start;
        RuntimeMultiplayerLanOperationResult queued =
            clientA.bridge()->lan().sendLobbySlotOptionsRequest(
                std::move(explicitRequest)
            );
        context.expect(queued.succeeded, label + " should queue");
        SlotResultProbe rejected;
        waitForSlotResult(
            host,
            clientA,
            clientB,
            clientA,
            requestId,
            rejected,
            context,
            label
        );
        context.expect(
            rejected.found && !rejected.result.accepted
                && rejected.result.reason.find(reasonFragment)
                    != std::string::npos,
            label + " should return a bounded explicit rejection"
        );
    };

    revision = host.lobby().setup().revision;
    rejectedRequest(
        100,
        revision,
        slotB,
        "green",
        3,
        "only its authenticated Human",
        "cross-slot request"
    );
    rejectedRequest(
        101,
        revision - 1U,
        slotA,
        "green",
        3,
        "revision is stale",
        "old-revision request"
    );
    rejectedRequest(
        101,
        revision,
        slotA,
        "green",
        3,
        "replayed or stale",
        "replayed request id"
    );
    rejectedRequest(
        102,
        revision,
        slotA,
        "blue",
        3,
        "color is duplicated",
        "conflicting color request"
    );
    rejectedRequest(
        103,
        revision,
        slotA,
        "green",
        2,
        "start position is duplicated",
        "conflicting start request"
    );
    context.expect(
        clientA.bridge()->lan().phase() == RuntimeMultiplayerLanPhase::Lobby
            && clientB.bridge()->lan().phase()
                == RuntimeMultiplayerLanPhase::Lobby,
        "rejected slot requests must not disconnect either client"
    );

    context.expect(
        !host.kickPeer("host-peer").succeeded
            && !host.kickPeer("unknown-peer").succeeded,
        "host may not kick itself or an unknown peer"
    );
    const RuntimeMultiplayerLanOperationResult kicked = host.kickPeer(
        "client-b",
        "host removed this player"
    );
    context.expect(
        kicked.succeeded && kicked.applied,
        "host should apply an admitted lobby kick: " + kicked.reason
    );
    bool sawKicked = false;
    const auto kickDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < kickDeadline
        && (!sawKicked
            || clientB.bridge()->lan().phase()
                != RuntimeMultiplayerLanPhase::Kicked
            || host.lobby().setup().members.size() != 2U
            || clientA.lobby().setup().members.size() != 2U)) {
        const RuntimeMultiplayerLobbyFlowPollResult hostPoll = host.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientAPoll =
            clientA.poll();
        const RuntimeMultiplayerLobbyFlowPollResult clientBPoll =
            clientB.poll();
        context.expect(
            hostPoll.succeeded && clientAPoll.succeeded
                && clientBPoll.succeeded,
            "kick propagation should remain a valid lobby transition"
        );
        if (!hostPoll.succeeded || !clientAPoll.succeeded
            || !clientBPoll.succeeded) {
            break;
        }
        sawKicked = sawKicked || std::any_of(
            clientBPoll.events.begin(),
            clientBPoll.events.end(),
            [](const RuntimeMultiplayerLanEvent& event) {
                return event.kind == RuntimeMultiplayerLanEventKind::Kicked
                    && event.reason == "host removed this player";
            }
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        sawKicked
            && clientB.bridge()->lan().phase()
                == RuntimeMultiplayerLanPhase::Kicked
            && host.lobby().setup().members.size() == 2U
            && clientA.lobby().setup().members.size() == 2U
            && host.lobby().setup() == clientA.lobby().setup(),
        "kicked client should be distinguishable and remaining roster canonical"
    );

    host.close();
    clientA.close();
    clientB.close();
    std::filesystem::remove(mapPath);
}

void runAdmissionCapacityCase(
    SmokeContext& context,
    std::string caseName,
    std::size_t slotCount,
    std::vector<std::size_t> computerSlots,
    std::size_t expectedMaximumMembers,
    bool duplicateIdentity = false
) {
    RuntimeMultiplayerLobbyHostCreateOptions lobby = lobbyOptions(
        0x5555666677778888ULL
    );
    lobby.slotCount = slotCount;
    lobby.computerSlotIndices = std::move(computerSlots);

    RuntimeMultiplayerLobbyFlow host;
    RuntimeMultiplayerLobbyFlow admitted;
    RuntimeMultiplayerLobbyFlow excess;
    std::string error;
    const bool hostStarted = host.startHost(networkOptions(), lobby, error);
    context.expect(
        hostStarted,
        caseName + ": host should start: " + error
    );
    if (!host.bridge()) return;
    context.expect(
        host.bridge()->lan().session().config().maximumMembers
            == expectedMaximumMembers,
        caseName + ": canonical member capacity should follow lobby slots"
    );

    error.clear();
    context.expect(
        admitted.join(
            multiClientJoin(host, caseName + "-accepted", "Accepted"),
            error
        ),
        caseName + ": first remote combatant should connect: " + error
    );
    if (!pumpUntil(
            host,
            admitted,
            [&host, &admitted]() {
                return host.lobby().setup().members.size() == 2U
                    && admitted.lobby().initialized();
            },
            context,
            caseName + ": first combatant admission"
        )) {
        host.close();
        admitted.close();
        return;
    }

    error.clear();
    context.expect(
        excess.join(
            multiClientJoin(
                host,
                duplicateIdentity
                    ? caseName + "-accepted"
                    : caseName + "-excess",
                "Rejected"
            ),
            error
        ),
        caseName + ": rejected connection should reach formal admission: "
            + error
    );
    bool rejected = false;
    std::string rejectionReason;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !rejected) {
        const RuntimeMultiplayerLobbyFlowPollResult hostPoll = host.poll();
        const RuntimeMultiplayerLobbyFlowPollResult admittedPoll =
            admitted.poll();
        const RuntimeMultiplayerLobbyFlowPollResult excessPoll = excess.poll();
        if (!hostPoll.succeeded || !admittedPoll.succeeded) {
            context.expect(
                false,
                caseName + ": excess join damaged the existing room: "
                    + (!hostPoll.succeeded
                        ? hostPoll.error
                        : admittedPoll.error)
            );
            break;
        }
        for (const RuntimeMultiplayerLanEvent& event : excessPoll.events) {
            if (event.kind == RuntimeMultiplayerLanEventKind::JoinRejected) {
                rejected = true;
                rejectionReason = event.reason;
            }
        }
        rejected = rejected || (excess.bridge()
            && excess.bridge()->lan().phase()
                == RuntimeMultiplayerLanPhase::JoinRejected);
        if (!rejected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        rejected && !rejectionReason.empty(),
        caseName + ": rejected connection should receive a reasoned rejection"
    );
    context.expect(
        host.bridge()->lan().phase() == RuntimeMultiplayerLanPhase::Lobby
            && admitted.bridge()->lan().phase()
                == RuntimeMultiplayerLanPhase::Lobby
            && host.lobby().setup().members.size() == 2U
            && admitted.lobby().setup().members.size() == 2U
            && host.lobby().setup() == admitted.lobby().setup(),
        caseName + ": rejected combatant must not alter the canonical room"
    );

    excess.close();
    RuntimeMultiplayerLobbyFlow spectator;
    error.clear();
    context.expect(
        spectator.join(
            multiClientJoin(
                host,
                caseName + "-spectator",
                "Spectator",
                RuntimeMultiplayerParticipationRole::Spectator
            ),
            error
        ),
        caseName
            + ": spectator should connect after combatant capacity is full: "
            + error
    );
    if (pumpThreeUntil(
            host,
            admitted,
            spectator,
            [&host, &admitted, &spectator]() {
                return host.lobby().setup().members.size() == 3U
                    && admitted.lobby().setup().members.size() == 3U
                    && spectator.lobby().setup().members.size() == 3U;
            },
            context,
            caseName + ": spectator admission beyond combatant capacity"
        )) {
        const RuntimeMultiplayerMatchSetup& setup = host.lobby().setup();
        const std::size_t humanSlots = static_cast<std::size_t>(
            std::count_if(
                setup.slots.begin(),
                setup.slots.end(),
                [](const RuntimeMultiplayerMatchSlot& slot) {
                    return slot.controller
                        == RuntimeMultiplayerMatchSlotController::Human;
                }
            )
        );
        const std::size_t spectators = static_cast<std::size_t>(
            std::count_if(
                setup.members.begin(),
                setup.members.end(),
                [](const RuntimeMultiplayerMatchMember& member) {
                    return member.identity
                        == RuntimeMultiplayerMatchMemberIdentity::Spectator;
                }
            )
        );
        context.expect(
            humanSlots == 2U && spectators == 1U
                && setup == admitted.lobby().setup()
                && setup == spectator.lobby().setup(),
            caseName
                + ": spectator must join the roster without consuming a combatant slot"
        );
    }

    host.close();
    admitted.close();
    spectator.close();
}

void runAdmissionCapacity(SmokeContext& context) {
    runAdmissionCapacityCase(context, "two-slot", 2U, {}, 10U);
    runAdmissionCapacityCase(
        context,
        "ai-filled",
        8U,
        {2U, 3U, 4U, 5U, 6U, 7U},
        10U
    );
    runAdmissionCapacityCase(
        context,
        "duplicate-identity",
        4U,
        {},
        12U,
        true
    );
}

#include "RuntimeMultiplayerResumeLobbySmoke.inl"
#include "RuntimeMultiplayerMapLobbySmoke.inl"

} // namespace

int main() {
    SmokeContext context;
    runLobbyFlow(context);
    runFormalLobbyControls(context);
    runAdmissionCapacity(context);
    runResumeLobby(context);
    runMissingMapLobby(context);
    if (context.failures != 0) {
        std::cerr << "Runtime multiplayer lobby flow smoke failed with "
                  << context.failures << " failure(s).\n";
        return 1;
    }
    std::cout << "Runtime multiplayer lobby flow smoke passed.\n";
    return 0;
}
