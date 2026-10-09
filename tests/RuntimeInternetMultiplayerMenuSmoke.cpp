#include "RuntimeInternetMultiplayerMenu.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
};

constexpr const char* credentialHex =
    "00112233445566778899aabbccddeeff";
constexpr const char* leafHex =
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f";

std::string validConfigText() {
    return std::string(
        "[General]\n"
        "WindowWidth=1024\n"
        "\n"
        "[Internet]\n"
        "ServiceAddress=203.0.113.10\n"
        "ServicePort=443\n"
        "ServerName=match.example.test\n"
        "ServiceId=concord-production\n"
        "PeerId=peer-alice\n"
        "CredentialHex="
    ) + credentialHex
        + "\nLeafCertificateSha256=" + leafHex + "\n";
}

RuntimeMultiplayerCompatibility compatibility() {
    RuntimeMultiplayerCompatibility result;
    result.protocolVersion = runtimeMultiplayerProtocolVersion;
    result.protocolFingerprint = 0x1122334455667788ULL;
    result.contentFingerprint = 0xa1b2c3d4e5f60718ULL;
    return result;
}

RuntimeMultiplayerHostSelectionProjection hostSelection(
    const std::string& peerId,
    const std::string& displayName
) {
    RuntimeMultiplayerHostSelectionProjection result;
    result.localMapPath = "D:/fixtures/2-player.map";
    result.lobby.map.canonicalId = "multimd/2-player.map";
    result.lobby.map.contentFingerprint = 0x1020304050607080ULL;
    result.lobby.rules.gameMode = "battle";
    result.lobby.rules.rulesOverride = "mpbattlemd.ini";
    result.lobby.rules.randomSeed = 0x8877665544332211ULL;
    result.lobby.rules.fixedTickHz = 30;
    result.lobby.rules.startingCredits = 10000;
    result.lobby.rules.startingUnits = 10;
    result.lobby.rules.techLevel = 10;
    result.lobby.rules.gameSpeed = 2;
    result.lobby.hostMember.peerId = peerId;
    result.lobby.hostMember.displayName = displayName;
    result.lobby.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    result.lobby.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    result.lobby.slotCount = 2U;

    RuntimeMultiplayerMatchSlot host;
    host.slotIndex = 0U;
    host.controller = RuntimeMultiplayerMatchSlotController::Human;
    host.peerId = peerId;
    host.countryId = "americans";
    host.colorId = "gold";
    host.teamId = "-";
    host.difficultyId = "medium";
    host.startPosition = 1U;
    RuntimeMultiplayerMatchSlot remote;
    remote.slotIndex = 1U;
    remote.controller = RuntimeMultiplayerMatchSlotController::Closed;
    result.slots = {host, remote};
    return result;
}

RuntimeInternetMultiplayerMenuEntropy deterministicEntropy() {
    RuntimeInternetMultiplayerMenuEntropy result;
    result.queryTransactionId = 1U;
    result.hostTransactionId = 2U;
    result.joinTransactionId = 3U;
    result.matchRequestId = 4U;
    result.matchActivationTransactionId = 5U;
    result.matchHostTransactionId = 6U;
    result.matchJoinTransactionId = 7U;
    result.roomGeneration = 8U;
    result.gameplayRandomSeed = 9U;
    for (std::size_t index = 0U; index < result.roomIdBytes.size(); ++index) {
        result.roomIdBytes[index] = static_cast<std::uint8_t>(index + 1U);
    }
    return result;
}

bool containsInsensitive(std::string value, std::string needle) {
    std::transform(value.begin(), value.end(), value.begin(), [](char byte) {
        return byte >= 'A' && byte <= 'Z'
            ? static_cast<char>(byte - 'A' + 'a')
            : byte;
    });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](char byte) {
        return byte >= 'A' && byte <= 'Z'
            ? static_cast<char>(byte - 'A' + 'a')
            : byte;
    });
    return value.find(needle) != std::string::npos;
}

void testStrictConfig(SmokeContext& context) {
    RuntimeInternetMultiplayerServiceConfig parsed;
    std::string error;
    context.expect(
        parseRuntimeInternetMultiplayerServiceConfigText(
            validConfigText(),
            parsed,
            error
        ),
        "complete [Internet] configuration should parse: " + error
    );
    context.expect(
        parsed.serviceAddress == "203.0.113.10"
            && parsed.servicePort == 443U
            && parsed.serverName == "match.example.test"
            && parsed.serviceId == "concord-production"
            && parsed.peerId == "peer-alice",
        "parsed service identity should match the authored values"
    );
    context.expect(
        parsed.credential.size() == 16U
            && parsed.credential.front() == 0x00U
            && parsed.credential.back() == 0xffU,
        "credential hex should decode exactly"
    );
    context.expect(
        parsed.leafCertificateSha256.has_value()
            && parsed.leafCertificateSha256->front() == 0x00U
            && parsed.leafCertificateSha256->back() == 0x1fU,
        "optional leaf certificate pin should decode to exactly SHA-256"
    );

    RuntimeInternetMultiplayerServiceConfig sentinel = parsed;
    sentinel.peerId = "unchanged-output";
    const std::vector<std::string> invalid = {
        "[Internet]\nServiceAddress=203.0.113.10\n",
        validConfigText() + "ServicePort=444\n",
        validConfigText() + "UnknownSecuritySwitch=yes\n",
        std::string(
            "[Internet]\nServiceAddress=https://example.test\n"
            "ServicePort=443\nServerName=example.test\n"
            "ServiceId=service\nPeerId=peer\nCredentialHex="
        ) + credentialHex + "\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=0\n"
            "ServerName=example.test\nServiceId=service\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=65536\n"
            "ServerName=example.test\nServiceId=service\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=443\n"
            "ServerName=*.example.test\nServiceId=service\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=443\n"
            "ServerName=example.test\nServiceId=bad/id\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "\n",
        "[Internet]\nServiceAddress=example.test\nServicePort=443\n"
        "ServerName=example.test\nServiceId=service\nPeerId=peer\n"
        "CredentialHex=001122\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=443\n"
            "ServerName=example.test\nServiceId=service\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "z\n",
        std::string(
            "[Internet]\nServiceAddress=example.test\nServicePort=443\n"
            "ServerName=example.test\nServiceId=service\nPeerId=peer\n"
            "CredentialHex="
        ) + credentialHex + "\nLeafCertificateSha256=abcd\n",
        validConfigText() + "[Internet]\nServicePort=443\n"
    };
    for (std::size_t index = 0U; index < invalid.size(); ++index) {
        RuntimeInternetMultiplayerServiceConfig output = sentinel;
        error.clear();
        context.expect(
            !parseRuntimeInternetMultiplayerServiceConfigText(
                invalid[index],
                output,
                error
            ),
            "invalid strict config should fail at case "
                + std::to_string(index)
        );
        context.expect(
            !error.empty(),
            "invalid strict config should explain case "
                + std::to_string(index)
        );
        context.expect(
            output == sentinel,
            "invalid strict config should leave output unchanged at case "
                + std::to_string(index)
        );
    }
}

void testTransportTrust(SmokeContext& context) {
    RuntimeInternetMultiplayerServiceConfig service;
    std::string error;
    context.expect(
        parseRuntimeInternetMultiplayerServiceConfigText(
            validConfigText(),
            service,
            error
        ),
        "transport fixture config should parse"
    );
    RuntimeInternetServiceTransportConfig transport;
    context.expect(
        makeRuntimeInternetMultiplayerTransportConfig(
            service,
            transport,
            error
        ),
        "transport options should build: " + error
    );
    context.expect(
        transport.serviceId == service.serviceId
            && transport.peerId == service.peerId
            && transport.credential == service.credential,
        "application-authentication identity should be preserved"
    );
    context.expect(
        transport.coordinatorTls.endpoint.address == service.serviceAddress
            && transport.coordinatorTls.endpoint.port == service.servicePort
            && transport.coordinatorTls.serverName == service.serverName,
        "coordinator must use the explicitly configured TLS endpoint/name"
    );
    context.expect(
        transport.relayTls.serverName == service.serverName
            && transport.relayTls.leafCertificateSha256
                == service.leafCertificateSha256,
        "relay TLS must retain the same authenticated certificate policy"
    );
    context.expect(
        transport.coordinatorTls.checkCertificateRevocation
            && transport.relayTls.checkCertificateRevocation
            && transport.coordinatorTls.nativeCertificateChainEngine == nullptr
            && transport.relayTls.nativeCertificateChainEngine == nullptr,
        "system chain, hostname, and revocation checks must not be bypassed"
    );
}

void testEntropy(SmokeContext& context) {
    RuntimeInternetMultiplayerMenuEntropy first;
    RuntimeInternetMultiplayerMenuEntropy second;
    std::string error;
    context.expect(
        generateRuntimeInternetMultiplayerMenuEntropy(first, error),
        "first BCrypt entropy transaction should succeed: " + error
    );
    context.expect(
        generateRuntimeInternetMultiplayerMenuEntropy(second, error),
        "second BCrypt entropy transaction should succeed: " + error
    );
    const std::array<std::uint64_t, 9U> values = {
        first.queryTransactionId,
        first.hostTransactionId,
        first.joinTransactionId,
        first.matchRequestId,
        first.matchActivationTransactionId,
        first.matchHostTransactionId,
        first.matchJoinTransactionId,
        first.roomGeneration,
        first.gameplayRandomSeed
    };
    const std::set<std::uint64_t> unique(values.begin(), values.end());
    context.expect(
        unique.size() == values.size() && unique.find(0U) == unique.end(),
        "all transaction, generation, request, and seed values must be unique/nonzero"
    );
    context.expect(
        std::any_of(
            first.roomIdBytes.begin(),
            first.roomIdBytes.end(),
            [](std::uint8_t byte) { return byte != 0U; }
        ),
        "room id entropy must not be all zero"
    );
    context.expect(
        first != second,
        "independent UI actions must not reuse one entropy transaction"
    );
}

void testFlowOptionProjection(SmokeContext& context) {
    RuntimeInternetMultiplayerServiceConfig service;
    std::string error;
    context.expect(
        parseRuntimeInternetMultiplayerServiceConfigText(
            validConfigText(),
            service,
            error
        ),
        "flow fixture config should parse"
    );
    const RuntimeMultiplayerCompatibility localCompatibility = compatibility();
    const RuntimeInternetMultiplayerMenuEntropy entropy =
        deterministicEntropy();
    context.expect(
        runtimeInternetMultiplayerContentFingerprint(localCompatibility)
            == "a1b2c3d4e5f60718",
        "directory fingerprint should be fixed-width lowercase hexadecimal"
    );

    RuntimeInternetLobbyQueryOptions query;
    context.expect(
        makeRuntimeInternetMultiplayerQueryOptions(
            service,
            localCompatibility,
            entropy.queryTransactionId,
            query,
            error
        ),
        "room query options should build: " + error
    );
    context.expect(
        query.connection.localCandidates.empty()
            && query.connection.transactionId == entropy.queryTransactionId
            && query.connection.serviceId == service.serviceId,
        "room query should carry only authenticated service metadata"
    );

    const RuntimeMultiplayerHostSelectionProjection selection =
        hostSelection(service.peerId, service.peerId);
    RuntimeInternetLobbyHostOptions host;
    context.expect(
        makeRuntimeInternetMultiplayerHostOptions(
            service,
            localCompatibility,
            selection,
            "Alice's Room",
            "",
            entropy,
            host,
            error
        ),
        "formal room host options should build: " + error
    );
    context.expect(
        host.connection.transactionId == entropy.hostTransactionId
            && host.network.roomGeneration == entropy.roomGeneration
            && host.room.roomId
                == "room-0102030405060708090a0b0c0d0e0f10",
        "host identifiers must come only from caller-supplied secure entropy"
    );
    context.expect(
        !host.network.enableDiscovery
            && host.network.discoveryPort == 0U
            && host.network.sessionPort == 0U
            && host.network.sessionBindScope
                == RuntimeTcpBindScope::LoopbackIpv4
            && !host.network.passwordProtected
            && host.proxy.idleTimeoutMilliseconds == 0U,
        "Internet host LAN server must remain private and use coordinator-owned liveness"
    );
    context.expect(
        host.room.maximumPlayers == 2U
            && host.network.session.maximumMembers
                == runtimeMultiplayerMaximumMembers
            && host.network.maximumCombatants == 2U
            && host.network.maximumSpectators
                == runtimeMultiplayerMaximumSpectators,
        "formal room must publish combatant slots separately from the combined spectator roster"
    );
    context.expect(
        host.connection.localCandidates.size() == 1U
            && host.connection.localCandidates.front().endpoint.address
                == "127.0.0.1",
        "legacy candidate schema should contain only the non-transmitted relay placeholder"
    );

    RuntimeInternetLobbyJoinOptions joining;
    context.expect(
        makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            host.room,
            "Alice",
            "",
            true,
            entropy.joinTransactionId,
            joining,
            error
        ),
        "spectator join options should build: " + error
    );
    context.expect(
        joining.connection.role == RuntimeInternetRole::Spectator
            && joining.network.participation
                == RuntimeMultiplayerParticipationRole::Spectator
            && joining.canonicalRoom.hostName == host.room.hostPeerId
            && joining.proxy.idleTimeoutMilliseconds == 0U,
        "formal join must bind spectator role and canonical host identity"
    );
    RuntimeInternetRoomRecord combatantsFull = host.room;
    combatantsFull.playerCount = combatantsFull.maximumPlayers;
    RuntimeInternetLobbyJoinOptions fullRoomJoining = joining;
    error.clear();
    context.expect(
        !makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            combatantsFull,
            "Alice",
            "",
            false,
            entropy.joinTransactionId,
            fullRoomJoining,
            error
        ),
        "combatant join must reject a full combatant roster"
    );
    error.clear();
    context.expect(
        makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            combatantsFull,
            "Alice",
            "",
            true,
            entropy.joinTransactionId,
            fullRoomJoining,
            error
        )
            && fullRoomJoining.connection.role
                == RuntimeInternetRole::Spectator,
        "spectator join must remain available when combatant slots are full: "
            + error
    );

    RuntimeMultiplayerHostSelectionProjection aiSelection = selection;
    aiSelection.lobby.slotCount = 8U;
    for (std::size_t slotIndex = 2U; slotIndex < 8U; ++slotIndex) {
        RuntimeMultiplayerMatchSlot computer;
        computer.slotIndex = static_cast<std::uint8_t>(slotIndex);
        computer.controller = RuntimeMultiplayerMatchSlotController::Computer;
        computer.countryId = "random";
        computer.colorId = "random";
        computer.teamId = "-";
        computer.difficultyId = "medium";
        computer.startPosition = static_cast<std::uint8_t>(slotIndex + 1U);
        aiSelection.slots.push_back(std::move(computer));
        aiSelection.lobby.computerSlotIndices.push_back(slotIndex);
    }
    RuntimeInternetLobbyHostOptions aiHost;
    error.clear();
    context.expect(
        makeRuntimeInternetMultiplayerHostOptions(
            service,
            localCompatibility,
            aiSelection,
            "AI Room",
            "",
            entropy,
            aiHost,
            error
        ) && aiHost.room.maximumPlayers == 2U
            && aiHost.network.maximumCombatants == 2U,
        "directory capacity must exclude six authored computer slots: " + error
    );
    RuntimeInternetLobbyHostOptions protectedHost;
    error.clear();
    context.expect(
        makeRuntimeInternetMultiplayerHostOptions(
            service,
            localCompatibility,
            selection,
            "Password Room",
            "correct horse battery staple",
            entropy,
            protectedHost,
            error
        )
            && protectedHost.room.passwordProtected
            && protectedHost.roomPasswordVerifier.size() == 32U
            && !protectedHost.network.passwordProtected
            && protectedHost.network.password.empty(),
        "ordinary Internet host password should become TLS-only verifier metadata: "
            + error
    );
    RuntimeInternetRoomRecord protectedRoom = protectedHost.room;
    RuntimeInternetLobbyJoinOptions unchanged = joining;
    error.clear();
    context.expect(
        !makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            protectedRoom,
            "Alice",
            "",
            false,
            entropy.joinTransactionId,
            joining,
            error
        ),
        "a protected match room must reject unauthenticated list joins"
    );
    context.expect(
        joining.connection.role == unchanged.connection.role
            && joining.room == unchanged.room,
        "rejected protected join must leave output unchanged"
    );
    RuntimeInternetLobbyJoinOptions protectedJoining;
    RuntimeInternetLobbyJoinOptions wrongPasswordJoining;
    error.clear();
    context.expect(
        makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            protectedRoom,
            "Alice",
            "correct horse battery staple",
            false,
            entropy.joinTransactionId,
            protectedJoining,
            error
        )
            && protectedJoining.roomAdmissionProof.size() == 32U,
        "protected room join should derive a bound admission proof: " + error
    );
    error.clear();
    context.expect(
        makeRuntimeInternetMultiplayerJoinOptions(
            service,
            localCompatibility,
            protectedRoom,
            "Alice",
            "wrong password",
            false,
            entropy.joinTransactionId,
            wrongPasswordJoining,
            error
        )
            && wrongPasswordJoining.roomAdmissionProof.size() == 32U
            && wrongPasswordJoining.roomAdmissionProof
                != protectedJoining.roomAdmissionProof,
        "different Internet passwords must produce different admission proofs: "
            + error
    );

    RuntimeMultiplayerHostSelectionProjection quickSelection =
        hostSelection(service.peerId, "Alice");
    RuntimeInternetLobbyMatchmakingOptions matching;
    context.expect(
        makeRuntimeInternetMultiplayerMatchmakingOptions(
            service,
            localCompatibility,
            quickSelection,
            "Alice",
            1200,
            entropy,
            matching,
            error
        ),
        "Quick Match options should build: " + error
    );
    context.expect(
        matching.request.requestId == entropy.matchRequestId
            && matching.request.playerCount == 2U
            && matching.request.displayName == "Alice"
            && matching.request.queueId == "quick-two-player",
        "Quick Match request must be two-player and publish the selected display name"
    );
    context.expect(
        matching.activationTransactionId
                == entropy.matchActivationTransactionId
            && matching.hostTemplate.connection.transactionId
                == entropy.matchHostTransactionId
            && matching.joinTemplate.connection.transactionId
                == entropy.matchJoinTransactionId,
        "all staged matchmaking correlations must use distinct secure values"
    );
    context.expect(
        matching.hostTemplate.network.session.hostDisplayName == "Alice"
            && matching.joinTemplate.network.displayName == "Alice"
            && !matching.hostTemplate.network.enableDiscovery
            && matching.hostTemplate.network.sessionPort == 0U
            && matching.hostTemplate.proxy.idleTimeoutMilliseconds == 0U
            && matching.joinTemplate.proxy.idleTimeoutMilliseconds == 0U,
        "assigned host/client templates should converge on one canonical display identity and private relay"
    );
}

void testViewProjection(SmokeContext& context) {
    RuntimeInternetRoomRecord open;
    open.roomId = "room-open";
    open.roomName = "Open Room";
    open.hostPeerId = "peer-host";
    open.contentFingerprint = "a1b2c3d4e5f60718";
    open.playerCount = 1U;
    open.maximumPlayers = 2U;
    RuntimeInternetRoomRecord protectedRoom = open;
    protectedRoom.roomId = "room-match";
    protectedRoom.roomName = "Assigned Match";
    protectedRoom.passwordProtected = true;
    const auto roomRows = projectRuntimeInternetMultiplayerRoomRows(
        std::array<RuntimeInternetRoomRecord, 2U>{open, protectedRoom}
    );
    context.expect(
        roomRows.size() == 2U && roomRows[0].joinable
            && roomRows[1].joinable
            && containsInsensitive(roomRows[1].text, "password required"),
        "room projection should expose password rooms as joinable with credentials"
    );
    RuntimeInternetRoomRecord full = open;
    full.playerCount = full.maximumPlayers;
    const auto combatantRows = projectRuntimeInternetMultiplayerRoomRows(
        std::array<RuntimeInternetRoomRecord, 1U>{full},
        false
    );
    const auto spectatorRows = projectRuntimeInternetMultiplayerRoomRows(
        std::array<RuntimeInternetRoomRecord, 1U>{full},
        true
    );
    context.expect(
        combatantRows.size() == 1U && !combatantRows.front().joinable
            && spectatorRows.size() == 1U
            && spectatorRows.front().joinable
            && containsInsensitive(
                spectatorRows.front().text,
                "spectator join available"
            ),
        "full combatant rooms should remain visibly joinable in spectator mode"
    );

    RuntimeMultiplayerRoomMember host;
    host.peerId = "peer-host";
    host.displayName = "Host";
    host.role = RuntimeMultiplayerRoomMemberRole::Host;
    host.participation = RuntimeMultiplayerParticipationRole::Combatant;
    host.ready = true;
    RuntimeMultiplayerRoomMember guest;
    guest.peerId = "peer-guest";
    guest.displayName = "Guest";
    guest.role = RuntimeMultiplayerRoomMemberRole::Client;
    guest.participation = RuntimeMultiplayerParticipationRole::Combatant;
    RuntimeMultiplayerMatchSetup setup;
    RuntimeMultiplayerMatchSlot hostSlot;
    hostSlot.slotIndex = 0U;
    hostSlot.controller = RuntimeMultiplayerMatchSlotController::Human;
    hostSlot.peerId = host.peerId;
    hostSlot.countryId = "americans";
    hostSlot.colorId = "gold";
    hostSlot.teamId = "1";
    hostSlot.difficultyId = "medium";
    hostSlot.startPosition = 1U;
    RuntimeMultiplayerMatchSlot guestSlot = hostSlot;
    guestSlot.slotIndex = 1U;
    guestSlot.peerId = guest.peerId;
    guestSlot.countryId = "russians";
    guestSlot.colorId = "red";
    guestSlot.teamId = "2";
    guestSlot.startPosition = 2U;
    setup.slots = {hostSlot, guestSlot};
    const auto roster = projectRuntimeInternetMultiplayerRosterRows(
        std::array<RuntimeMultiplayerRoomMember, 2U>{host, guest},
        &setup,
        host.peerId,
        true
    );
    context.expect(
        roster.size() == 2U && roster[0].local && !roster[0].kickable
            && roster[1].kickable,
        "canonical roster should mark only remote members kickable by the host"
    );
    context.expect(
        containsInsensitive(roster[0].text, "americans")
            && containsInsensitive(roster[0].text, "ready")
            && containsInsensitive(roster[1].text, "start 2"),
        "roster projection should expose canonical slot and ready state"
    );

    const std::array<std::string, 4U> statuses = {
        projectRuntimeInternetMultiplayerConnectionStatus(
            false,
            RuntimeInternetLobbyFlowMode::Idle,
            RuntimeInternetConnectionState::Idle
        ),
        projectRuntimeInternetMultiplayerConnectionStatus(
            true,
            RuntimeInternetLobbyFlowMode::RoomListReady,
            RuntimeInternetConnectionState::RoomListReady
        ),
        projectRuntimeInternetMultiplayerConnectionStatus(
            true,
            RuntimeInternetLobbyFlowMode::Joining,
            RuntimeInternetConnectionState::BindingRelayTcp
        ),
        projectRuntimeInternetMultiplayerConnectionStatus(
            true,
            RuntimeInternetLobbyFlowMode::Lobby,
            RuntimeInternetConnectionState::GameplaySessionConnected
        )
    };
    for (const std::string& status : statuses) {
        context.expect(
            !containsInsensitive(status, "udp")
                && !containsInsensitive(status, "forward")
                && !containsInsensitive(status, "direct connect"),
            "Internet UI status must not advertise direct/UDP/port-forwarding paths"
        );
    }
    context.expect(
        containsInsensitive(statuses.front(), "tls")
            && containsInsensitive(statuses.back(), "authenticated tcp relay"),
        "connection state should visibly distinguish TLS setup and authenticated relay readiness"
    );
}

void testCommunityProjection(SmokeContext& context) {
    RuntimeInternetCommunitySnapshot snapshot;
    snapshot.registeredProfile = true;
    snapshot.profile.peerId = "peer-alice";
    snapshot.profile.displayName = "CommanderAlice";
    snapshot.profile.wins = 12U;
    snapshot.profile.losses = 4U;
    snapshot.profile.disconnects = 1U;
    snapshot.profile.rank = 2U;
    snapshot.profile.points = 1340;
    snapshot.selectedChannelId = "ra2";
    snapshot.channels = {
        {"lobby", "Command & Conquer", "General lobby", 7U},
        {"ra2", "Red Alert 2", "Custom matches", 3U}
    };
    RuntimeInternetCommunityProfile participant = snapshot.profile;
    participant.online = true;
    snapshot.participants.push_back(participant);
    snapshot.chatHistory.push_back({
        1U,
        10U,
        "ra2",
        "peer-bob",
        "CommanderBob",
        "Ready to play?"
    });
    snapshot.friends.push_back({
        "peer-bob",
        "CommanderBob",
        1280,
        5U,
        true
    });
    snapshot.ladder.push_back({
        1U,
        "peer-bob",
        "CommanderBob",
        1400,
        20U,
        5U,
        0U,
        true
    });
    snapshot.news.push_back({
        "welcome",
        10U,
        "Community Update",
        "News body"
    });
    snapshot.clanMember = true;
    snapshot.clan = {
        "allies",
        "Allied Command",
        "AC",
        "peer-alice",
        10U,
        8U,
        2U,
        1210,
        1U,
        2U
    };
    snapshot.clanMembers.push_back({
        "peer-alice",
        "CommanderAlice",
        10U,
        12U,
        4U,
        1U,
        1340,
        true,
        true
    });
    snapshot.clanMembers.push_back({
        "peer-bob",
        "CommanderBob",
        11U,
        20U,
        5U,
        0U,
        1400,
        false,
        true
    });
    snapshot.clanLadder.push_back(snapshot.clan);

    const std::string profile =
        projectRuntimeInternetCommunityProfileSummary(snapshot);
    const auto channels = projectRuntimeInternetCommunityChannelRows(snapshot);
    const auto chat = projectRuntimeInternetCommunityChatRows(snapshot);
    const auto participants =
        projectRuntimeInternetCommunityParticipantRows(snapshot);
    const auto friends = projectRuntimeInternetCommunityFriendRows(snapshot);
    const auto ladder = projectRuntimeInternetCommunityLadderRows(snapshot);
    const std::string clanSummary =
        projectRuntimeInternetCommunityClanSummary(snapshot);
    const auto clanMembers =
        projectRuntimeInternetCommunityClanMemberRows(snapshot);
    const auto clanLadder =
        projectRuntimeInternetCommunityClanLadderRows(snapshot);
    const auto news = projectRuntimeInternetCommunityNewsRows(snapshot);
    context.expect(
        containsInsensitive(profile, "wins 12")
            && containsInsensitive(profile, "losses 4")
            && containsInsensitive(profile, "disconnects 1")
            && containsInsensitive(profile, "rank #2")
            && containsInsensitive(profile, "points 1340"),
        "community profile should expose the original WOL history fields"
    );
    context.expect(
        channels.size() == 2U
            && channels[1].rfind("> ", 0U) == 0U
            && containsInsensitive(channels[1], "[3]")
            && chat.size() == 1U
            && containsInsensitive(chat.front(), "commanderbob: ready")
            && participants.size() == 1U,
        "community channel, player, and chat projections should be complete"
    );
    context.expect(
        friends.size() == 1U
            && containsInsensitive(friends.front(), "online")
            && ladder.size() == 1U
            && containsInsensitive(ladder.front(), "#1")
            && containsInsensitive(ladder.front(), "w 20 / l 5")
            && news == std::vector<std::string>{"Community Update"},
        "Buddy, ladder, and news projections should retain service state"
    );
    context.expect(
        containsInsensitive(clanSummary, "[ac] allied command")
            && containsInsensitive(clanSummary, "rank #1")
            && containsInsensitive(clanSummary, "w 8 / l 2")
            && clanMembers.size() == 2U
            && containsInsensitive(clanMembers.front(), "owner")
            && containsInsensitive(clanMembers.back(), "member")
            && clanLadder.size() == 1U
            && containsInsensitive(clanLadder.front(), "#1 [ac]")
            && containsInsensitive(clanLadder.front(), "1210 pts"),
        "Clan summary, member roster, and Clan ladder projections should retain service state"
    );

    RuntimeInternetCommunitySnapshot invited;
    invited.clanInvitations.push_back(snapshot.clan);
    const auto invitationRows =
        projectRuntimeInternetCommunityClanInvitationRows(invited);
    context.expect(
        containsInsensitive(
            projectRuntimeInternetCommunityClanSummary(invited),
            "pending invitations: 1"
        )
            && invitationRows.size() == 1U
            && containsInsensitive(invitationRows.front(), "[ac]"),
        "Clan invitation projection should remain actionable without membership"
    );
}

} // namespace

int main() {
    SmokeContext context;
    testStrictConfig(context);
    testTransportTrust(context);
    testEntropy(context);
    testFlowOptionProjection(context);
    testViewProjection(context);
    testCommunityProjection(context);
    RuntimeInternetRankedMatch ranked;
    std::string rankedError;
    const RuntimeInternetRankedPolicy policy;
    context.expect(RuntimeInternetRankedMatch::authorize("ranked-menu", "ranked-room", {"alice", "bob"}, policy,
        1, 1700000000000ULL, ranked, rankedError), "ranked display fixture");
    ranked.start(policy, 2, 1700000001000ULL);
    ranked.report("alice", RuntimeInternetRankedResult::Win, policy, 3, 1700000002000ULL);
    const auto pendingDetail = projectRuntimeInternetRankedHistoryDetail(ranked.record, "alice");
    context.expect(pendingDetail.find("UTC") != std::string::npos && pendingDetail.find("ranked-menu") != std::string::npos
        && pendingDetail.find("截止") != std::string::npos, "pending history includes identity and fixed UTC deadline");
    ranked.expire(ranked.deadlineMilliseconds, 1700000122000ULL);
    const auto finalDetail = projectRuntimeInternetRankedHistoryDetail(ranked.record, "alice");
    context.expect(finalDetail.find("未计分") != std::string::npos && finalDetail.find("结案") != std::string::npos
        && finalDetail.find("截止") == std::string::npos, "void display has final date and no score");
    if (context.failures != 0) {
        std::cerr << "Runtime Internet multiplayer menu smoke failed: "
                  << context.failures << " check(s)\n";
        return 1;
    }
    std::cout << "Runtime Internet multiplayer menu smoke passed\n";
    return 0;
}
