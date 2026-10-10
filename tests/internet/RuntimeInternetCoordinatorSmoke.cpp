#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/lobby/RuntimeRoomPassword.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* serviceId = "coord.self-hosted.example";
constexpr const char* fingerprintA = "rules+map:a-0123456789";
constexpr const char* fingerprintB = "rules+map:b-9876543210";

bool require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Runtime Internet coordinator smoke failed: "
                  << message << '\n';
        return false;
    }
    return true;
}

RuntimeInternetEndpoint endpoint(std::string address, std::uint16_t port) {
    return {std::move(address), port};
}

RuntimeInternetCandidate candidate(
    const std::string& address,
    std::uint16_t port
) {
    return {
        RuntimeInternetCandidateKind::ServerReflexive,
        endpoint(address, port),
        100U
    };
}

RuntimeInternetConnectionConfig connectionConfig(
    const std::string& peerId,
    RuntimeInternetRole role,
    std::uint64_t transactionId,
    const std::string& roomId,
    const std::string& fingerprint,
    const RuntimeInternetCandidate& localCandidate
) {
    RuntimeInternetConnectionConfig config;
    config.serviceId = serviceId;
    config.localPeerId = peerId;
    config.roomId = roomId;
    config.role = role;
    config.contentFingerprint = fingerprint;
    config.localCandidates = {localCandidate};
    config.transactionId = transactionId;
    config.retryMilliseconds = 100;
    config.coordinatorTimeoutMilliseconds = 1000;
    config.punchTimeoutMilliseconds = 300;
    config.relayTimeoutMilliseconds = 1000;
    config.maximumRetries = 8;
    return config;
}

RuntimeInternetRoomRecord roomRecord(
    const std::string& roomId,
    const std::string& hostPeerId,
    const std::string& fingerprint,
    bool passwordProtected = false
) {
    RuntimeInternetRoomRecord room;
    room.roomId = roomId;
    room.roomName = "Self-hosted room " + roomId;
    room.hostPeerId = hostPeerId;
    room.contentFingerprint = fingerprint;
    room.playerCount = 1;
    room.maximumPlayers = 3;
    room.passwordProtected = passwordProtected;
    return room;
}

RuntimeInternetServiceEnvelope coordinatorEnvelope(
    const std::string& peerId,
    const RuntimeNetworkPacket& packet
) {
    RuntimeInternetServiceEnvelope envelope;
    envelope.channel = RuntimeInternetServiceChannel::CoordinatorTls;
    envelope.authenticatedServiceId = serviceId;
    envelope.authenticatedPeerId = peerId;
    envelope.packet = packet;
    return envelope;
}

RuntimeInternetServiceEnvelope relayEnvelope(
    const std::string& peerId,
    RuntimeInternetRelayTransport transport,
    const RuntimeNetworkPacket& packet
) {
    RuntimeInternetServiceEnvelope envelope;
    envelope.channel = transport == RuntimeInternetRelayTransport::Udp
        ? RuntimeInternetServiceChannel::RelayUdp
        : RuntimeInternetServiceChannel::RelayTcp;
    envelope.authenticatedServiceId = serviceId;
    envelope.authenticatedPeerId = peerId;
    envelope.packet = packet;
    return envelope;
}

const RuntimeInternetCoordinatorAction* findAction(
    const std::vector<RuntimeInternetCoordinatorAction>& actions,
    RuntimeInternetCoordinatorActionKind kind,
    const std::string& targetPeerId
) {
    const auto iterator = std::find_if(
        actions.begin(),
        actions.end(),
        [&](const RuntimeInternetCoordinatorAction& action) {
            return action.kind == kind && action.targetPeerId == targetPeerId;
        }
    );
    return iterator == actions.end() ? nullptr : &*iterator;
}

bool decodeMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetControlMessage& message,
    std::string& errorMessage
) {
    if (!decodeRuntimeInternetControlMessage(packet, message, errorMessage)) {
        std::cerr << "Could not decode coordinator smoke packet: "
                  << errorMessage << '\n';
        return false;
    }
    return true;
}

bool encodeMessage(
    const RuntimeInternetControlMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (!encodeRuntimeInternetControlMessage(message, packet, errorMessage)) {
        std::cerr << "Could not encode coordinator smoke packet: "
                  << errorMessage << '\n';
        return false;
    }
    return true;
}

RuntimeInternetControlMessage peerMessage(
    RuntimeInternetControlType type,
    std::uint64_t transactionId,
    const std::string& roomId,
    const std::string& peerId,
    std::string targetPeerId = {}
) {
    RuntimeInternetControlMessage message;
    message.type = type;
    message.transactionId = transactionId;
    message.serviceId = serviceId;
    message.roomId = roomId;
    message.senderPeerId = peerId;
    message.targetPeerId = std::move(targetPeerId);
    return message;
}

struct Offer {
    std::uint64_t connectionId = 0;
    RuntimeInternetRole remoteRole = RuntimeInternetRole::Host;
    std::vector<std::uint8_t> token;
};

bool offerFromAction(
    const RuntimeInternetCoordinatorAction& action,
    Offer& offer,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message;
    if (!decodeMessage(action.packet, message, errorMessage)) {
        return false;
    }
    const auto* body = std::get_if<RuntimeInternetPeerCandidatesBody>(
        &message.body
    );
    if (message.type != RuntimeInternetControlType::PeerCandidates
        || body == nullptr) {
        errorMessage = "action did not contain peer candidates";
        return false;
    }
    offer.connectionId = body->connectionId;
    offer.remoteRole = body->remoteRole;
    offer.token = body->connectionToken;
    return true;
}

bool feedInitialCoordinatorRequest(
    RuntimeInternetCoordinator& coordinator,
    RuntimeInternetConnection& connection,
    const std::string& peerId,
    std::vector<RuntimeInternetCoordinatorAction>& response,
    RuntimeNetworkPacket& request,
    std::string& errorMessage
) {
    const std::vector<RuntimeInternetAction> initial = connection.poll(0);
    if (initial.size() != 1U
        || initial.front().kind != RuntimeInternetActionKind::SendCoordinator) {
        errorMessage = "connection did not emit exactly one initial request";
        return false;
    }
    request = initial.front().packet;
    return coordinator.receive(
        coordinatorEnvelope(peerId, request),
        response,
        errorMessage
    );
}

bool acceptCoordinatorAction(
    RuntimeInternetConnection& connection,
    const RuntimeInternetCoordinatorAction& action,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    return connection.receiveCoordinator(
        action.packet,
        serviceId,
        nowMilliseconds,
        errorMessage
    );
}

bool registerHost(
    RuntimeInternetCoordinator& coordinator,
    RuntimeInternetConnection& host,
    const RuntimeInternetConnectionConfig& config,
    const RuntimeInternetRoomRecord& room,
    RuntimeNetworkPacket& registrationPacket,
    std::string& errorMessage
) {
    if (!host.beginHost(config, room, 0, errorMessage)) {
        return false;
    }
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (!feedInitialCoordinatorRequest(
            coordinator,
            host,
            config.localPeerId,
            actions,
            registrationPacket,
            errorMessage
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* accepted = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        config.localPeerId
    );
    return accepted != nullptr
        && acceptCoordinatorAction(host, *accepted, 0, errorMessage)
        && host.state() == RuntimeInternetConnectionState::WaitingForPeer;
}

bool beginJoinAndOffer(
    RuntimeInternetCoordinator& coordinator,
    RuntimeInternetConnection& connection,
    const RuntimeInternetConnectionConfig& config,
    RuntimeNetworkPacket& joinPacket,
    RuntimeInternetCoordinatorAction& hostAction,
    RuntimeInternetCoordinatorAction& remoteAction,
    std::string& errorMessage,
    std::vector<std::uint8_t> admissionProof = {}
) {
    if (!connection.beginJoin(config, 0, errorMessage)) {
        return false;
    }
    const std::vector<RuntimeInternetAction> initial = connection.poll(0);
    if (initial.size() != 1U) {
        errorMessage = "join did not emit one coordinator request";
        return false;
    }
    joinPacket = initial.front().packet;
    RuntimeInternetServiceEnvelope envelope = coordinatorEnvelope(
        config.localPeerId,
        joinPacket
    );
    envelope.roomAdmissionProof = std::move(admissionProof);
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (!coordinator.receive(envelope, actions, errorMessage)) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* host = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "host-a"
    );
    if (host == nullptr) {
        host = findAction(
            actions,
            RuntimeInternetCoordinatorActionKind::SendCoordinator,
            "host-b"
        );
    }
    const RuntimeInternetCoordinatorAction* remote = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        config.localPeerId
    );
    if (host == nullptr || remote == nullptr) {
        errorMessage = "coordinator did not emit both candidate offers";
        return false;
    }
    hostAction = *host;
    remoteAction = *remote;
    return true;
}

bool routeUsesUnpredictableToken(
    const RuntimeInternetCoordinatorAction& hostAction,
    const RuntimeInternetCoordinatorAction& remoteAction,
    const std::vector<std::uint8_t>& previousToken,
    Offer& remoteOffer,
    std::string& errorMessage
) {
    Offer hostOffer;
    if (!offerFromAction(hostAction, hostOffer, errorMessage)
        || !offerFromAction(remoteAction, remoteOffer, errorMessage)) {
        return false;
    }
    return hostOffer.connectionId == remoteOffer.connectionId
        && hostOffer.token == remoteOffer.token
        && remoteOffer.token.size() == 32U
        && std::any_of(
            remoteOffer.token.begin(),
            remoteOffer.token.end(),
            [](std::uint8_t value) { return value != 0U; }
        )
        && (previousToken.empty() || remoteOffer.token != previousToken);
}

RuntimeNetworkPacket relayRequestPacket(
    const std::string& peerId,
    const std::string& roomId,
    const Offer& offer,
    RuntimeInternetRelayTransport transport,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message = peerMessage(
        RuntimeInternetControlType::RelayRequest,
        offer.connectionId,
        roomId,
        peerId
    );
    message.body = RuntimeInternetRelayRequestBody{
        offer.connectionId,
        transport,
        offer.token
    };
    RuntimeNetworkPacket packet;
    encodeMessage(message, packet, errorMessage);
    return packet;
}

RuntimeNetworkPacket relayBindPacket(
    const std::string& peerId,
    const std::string& roomId,
    std::uint64_t connectionId,
    RuntimeInternetRelayTransport transport,
    const std::vector<std::uint8_t>& ticket,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message = peerMessage(
        RuntimeInternetControlType::RelayBind,
        connectionId,
        roomId,
        peerId,
        serviceId
    );
    message.body = RuntimeInternetRelayBindBody{
        connectionId,
        transport,
        ticket
    };
    RuntimeNetworkPacket packet;
    encodeMessage(message, packet, errorMessage);
    return packet;
}

bool testCoordinatorFlow() {
    std::uint64_t clockMilliseconds = 0;
    std::uint64_t randomState = 0x9e3779b97f4a7c15ULL;
    RuntimeInternetCoordinatorConfig coordinatorConfig;
    coordinatorConfig.serviceId = serviceId;
    coordinatorConfig.udpRelayEndpoint = endpoint("relay.self-hosted.example", 46000);
    coordinatorConfig.tcpRelayEndpoint = endpoint("relay.self-hosted.example", 47000);
    coordinatorConfig.roomLeaseMilliseconds = 5000;
    coordinatorConfig.connectionLeaseMilliseconds = 4000;
    coordinatorConfig.relayLeaseMilliseconds = 1500;
    coordinatorConfig.replayRetentionMilliseconds = 60000;
    coordinatorConfig.maximumRooms = 3;
    coordinatorConfig.maximumRoomsPerPeer = 1;
    coordinatorConfig.maximumConnections = 6;
    coordinatorConfig.maximumConnectionsPerRoom = 4;
    coordinatorConfig.maximumSpectatorsPerRoom = 1;
    coordinatorConfig.maximumConnectionsPerPeer = 8;
    coordinatorConfig.maximumRelayRoutes = 2;
    coordinatorConfig.maximumReplayTombstones = 16;
    coordinatorConfig.nowMilliseconds = [&]() { return clockMilliseconds; };
    coordinatorConfig.secureRandomBytes = [randomState](
        std::uint8_t* destination,
        std::size_t size
    ) mutable {
        for (std::size_t index = 0; index < size; ++index) {
            randomState ^= randomState >> 12U;
            randomState ^= randomState << 25U;
            randomState ^= randomState >> 27U;
            const std::uint64_t value = randomState
                * 0x2545f4914f6cdd1dULL;
            destination[index] = static_cast<std::uint8_t>(value >> 56U);
        }
        return true;
    };
    coordinatorConfig.authorizePasswordJoin = [](
        const std::string& roomId,
        const std::string& peerId,
        const std::vector<std::uint8_t>& proof
    ) {
        return roomId == "room-b" && peerId == "guest-b"
            && proof == std::vector<std::uint8_t>({0x41U, 0x42U, 0x43U});
    };

    RuntimeInternetCoordinator coordinator;
    std::string error;
    if (!require(
            coordinator.open(std::move(coordinatorConfig), error),
            "coordinator did not open: " + error
        )) {
        return false;
    }

    const RuntimeInternetCandidate hostCandidate = candidate(
        "198.51.100.10",
        41000
    );
    const RuntimeInternetConnectionConfig hostConfig = connectionConfig(
        "host-a",
        RuntimeInternetRole::Host,
        1001,
        "room-a",
        fingerprintA,
        hostCandidate
    );
    RuntimeInternetConnection host;
    RuntimeNetworkPacket roomARegistration;
    if (!require(
            registerHost(
                coordinator,
                host,
                hostConfig,
                roomRecord("room-a", "host-a", fingerprintA),
                roomARegistration,
                error
            ),
            "room A registration failed: " + error
        )) {
        return false;
    }

    std::vector<RuntimeInternetCoordinatorAction> actions;
    RuntimeInternetServiceEnvelope forgedService = coordinatorEnvelope(
        "host-a",
        roomARegistration
    );
    forgedService.authenticatedServiceId = "different.service";
    if (!require(
            !coordinator.receive(forgedService, actions, error)
                && coordinator.roomCount() == 1U,
            "forged service identity changed registration state"
        )) {
        return false;
    }
    RuntimeInternetServiceEnvelope forgedPeer = coordinatorEnvelope(
        "attacker",
        roomARegistration
    );
    if (!require(
            !coordinator.receive(forgedPeer, actions, error)
                && coordinator.roomCount() == 1U,
            "forged peer identity changed registration state"
        )) {
        return false;
    }
    clockMilliseconds = 1000;
    if (!require(
            coordinator.receive(
                coordinatorEnvelope("host-a", roomARegistration),
                actions,
                error
            )
                && actions.size() == 1U
                && coordinator.roomCount() == 1U,
            "exact host lease refresh was not idempotent: " + error
        )) {
        return false;
    }

    const RuntimeInternetConnectionConfig clientAConfig = connectionConfig(
        "client-a",
        RuntimeInternetRole::Client,
        2001,
        "room-a",
        fingerprintA,
        candidate("198.51.100.20", 42000)
    );
    RuntimeInternetConnection clientA;
    RuntimeNetworkPacket clientAJoin;
    RuntimeInternetCoordinatorAction clientAHostOffer;
    RuntimeInternetCoordinatorAction clientARemoteOffer;
    if (!require(
            beginJoinAndOffer(
                coordinator,
                clientA,
                clientAConfig,
                clientAJoin,
                clientAHostOffer,
                clientARemoteOffer,
                error
            ),
            "client A join failed: " + error
        )) {
        return false;
    }
    Offer clientAOffer;
    if (!require(
            routeUsesUnpredictableToken(
                clientAHostOffer,
                clientARemoteOffer,
                {},
                clientAOffer,
                error
            ),
            "client A candidate token was not shared, bounded, and non-zero"
        )) {
        return false;
    }

    RuntimeInternetConnection hostRouteA;
    if (!require(
            hostRouteA.beginPeerRouteFromCoordinator(
                hostConfig,
                clientAHostOffer.packet,
                serviceId,
                0,
                error
            )
                && acceptCoordinatorAction(
                    clientA,
                    clientARemoteOffer,
                    0,
                    error
                ),
            "direct candidate offers were rejected: " + error
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> hostPunch = hostRouteA.poll(0);
    const std::vector<RuntimeInternetAction> clientPunch = clientA.poll(0);
    if (!require(
            hostPunch.size() == 1U && clientPunch.size() == 1U
                && clientA.receivePeerUdp(
                    hostPunch.front().packet,
                    hostCandidate.endpoint,
                    0,
                    error
                )
                && hostRouteA.receivePeerUdp(
                    clientPunch.front().packet,
                    clientAConfig.localCandidates.front().endpoint,
                    0,
                    error
                )
                && clientA.state()
                    == RuntimeInternetConnectionState::RequestingRelayTcp
                && hostRouteA.state()
                    == RuntimeInternetConnectionState::RequestingRelayTcp,
            "authenticated UDP candidates did not transition both peers to the required TCP relay: "
                + error
        )) {
        return false;
    }

    if (!require(
            coordinator.receive(
                coordinatorEnvelope("client-a", clientAJoin),
                actions,
                error
            )
                && actions.size() == 2U
                && coordinator.connectionCount() == 1U,
            "exact join retry was not idempotent: " + error
        )) {
        return false;
    }
    RuntimeInternetControlMessage alteredJoin;
    if (!decodeMessage(clientAJoin, alteredJoin, error)) {
        return false;
    }
    auto alteredBody = std::get<RuntimeInternetJoinRoomBody>(alteredJoin.body);
    alteredBody.candidates.front().endpoint.port = 42999;
    alteredJoin.body = alteredBody;
    RuntimeNetworkPacket alteredJoinPacket;
    if (!encodeMessage(alteredJoin, alteredJoinPacket, error)) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("client-a", alteredJoinPacket),
                actions,
                error
            )
                && coordinator.connectionCount() == 1U,
            "join transaction replay with altered candidates was accepted"
        )) {
        return false;
    }

    const RuntimeInternetConnectionConfig clientBConfig = connectionConfig(
        "client-b",
        RuntimeInternetRole::Client,
        2002,
        "room-a",
        fingerprintA,
        candidate("198.51.100.21", 42001)
    );
    RuntimeInternetConnection clientB;
    RuntimeNetworkPacket clientBJoin;
    RuntimeInternetCoordinatorAction clientBHostOffer;
    RuntimeInternetCoordinatorAction clientBRemoteOffer;
    if (!require(
            beginJoinAndOffer(
                coordinator,
                clientB,
                clientBConfig,
                clientBJoin,
                clientBHostOffer,
                clientBRemoteOffer,
                error
            ),
            "client B join failed: " + error
        )) {
        return false;
    }
    Offer clientBOffer;
    if (!require(
            routeUsesUnpredictableToken(
                clientBHostOffer,
                clientBRemoteOffer,
                clientAOffer.token,
                clientBOffer,
                error
            ),
            "client B did not receive an isolated connection token"
        )) {
        return false;
    }

    const RuntimeInternetConnectionConfig spectatorConfig = connectionConfig(
        "spectator-a",
        RuntimeInternetRole::Spectator,
        2003,
        "room-a",
        fingerprintA,
        candidate("198.51.100.30", 43000)
    );
    RuntimeInternetConnection spectator;
    RuntimeNetworkPacket spectatorJoin;
    RuntimeInternetCoordinatorAction spectatorHostOffer;
    RuntimeInternetCoordinatorAction spectatorRemoteOffer;
    if (!require(
            beginJoinAndOffer(
                coordinator,
                spectator,
                spectatorConfig,
                spectatorJoin,
                spectatorHostOffer,
                spectatorRemoteOffer,
                error
            ),
            "spectator join failed: " + error
        )) {
        return false;
    }
    Offer spectatorOffer;
    if (!require(
            routeUsesUnpredictableToken(
                spectatorHostOffer,
                spectatorRemoteOffer,
                clientBOffer.token,
                spectatorOffer,
                error
            )
                && spectatorOffer.remoteRole == RuntimeInternetRole::Host,
            "spectator route role or token was incorrect"
        )) {
        return false;
    }
    Offer hostViewOfSpectator;
    if (!require(
            offerFromAction(spectatorHostOffer, hostViewOfSpectator, error)
                && hostViewOfSpectator.remoteRole
                    == RuntimeInternetRole::Spectator,
            "host candidate offer lost spectator role binding"
        )) {
        return false;
    }

    RuntimeInternetConnection overCapacityClient;
    const RuntimeInternetConnectionConfig overCapacityConfig = connectionConfig(
        "client-c",
        RuntimeInternetRole::Client,
        2004,
        "room-a",
        fingerprintA,
        candidate("198.51.100.22", 42002)
    );
    if (!overCapacityClient.beginJoin(overCapacityConfig, 0, error)) {
        return false;
    }
    const RuntimeNetworkPacket overCapacityPacket =
        overCapacityClient.poll(0).front().packet;
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("client-c", overCapacityPacket),
                actions,
                error
            ),
            "player joined a full room"
        )) {
        return false;
    }
    RuntimeInternetConnection secondSpectator;
    const RuntimeInternetConnectionConfig secondSpectatorConfig = connectionConfig(
        "spectator-b",
        RuntimeInternetRole::Spectator,
        2005,
        "room-a",
        fingerprintA,
        candidate("198.51.100.31", 43001)
    );
    if (!secondSpectator.beginJoin(secondSpectatorConfig, 0, error)) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope(
                    "spectator-b",
                    secondSpectator.poll(0).front().packet
                ),
                actions,
                error
            ),
            "spectator quota was not enforced"
        )) {
        return false;
    }

    const RuntimeInternetCandidate hostBCandidate = candidate(
        "203.0.113.10",
        44000
    );
    RuntimeInternetConnection hostB;
    const RuntimeInternetConnectionConfig hostBConfig = connectionConfig(
        "host-b",
        RuntimeInternetRole::Host,
        3001,
        "room-b",
        fingerprintB,
        hostBCandidate
    );
    RuntimeInternetRoomRecord roomB = roomRecord(
        "room-b",
        "host-b",
        fingerprintB,
        true
    );
    roomB.maximumPlayers = 2;
    RuntimeNetworkPacket roomBRegistration;
    if (!require(
            registerHost(
                coordinator,
                hostB,
                hostBConfig,
                roomB,
                roomBRegistration,
                error
            ),
            "room B registration failed: " + error
        )) {
        return false;
    }

    RuntimeInternetConnection extraHostRoom;
    const RuntimeInternetConnectionConfig extraHostConfig = connectionConfig(
        "host-a",
        RuntimeInternetRole::Host,
        1002,
        "room-c",
        fingerprintA,
        candidate("198.51.100.10", 41001)
    );
    if (!extraHostRoom.beginHost(
            extraHostConfig,
            roomRecord("room-c", "host-a", fingerprintA),
            0,
            error
        )) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope(
                    "host-a",
                    extraHostRoom.poll(0).front().packet
                ),
                actions,
                error
            ),
            "per-host room quota was not enforced"
        )) {
        return false;
    }

    RuntimeInternetConnection guestBadProof;
    const RuntimeInternetConnectionConfig guestConfig = connectionConfig(
        "guest-b",
        RuntimeInternetRole::Client,
        3002,
        "room-b",
        fingerprintB,
        candidate("203.0.113.20", 44001)
    );
    if (!guestBadProof.beginJoin(guestConfig, 0, error)) {
        return false;
    }
    RuntimeInternetServiceEnvelope deniedGuest = coordinatorEnvelope(
        "guest-b",
        guestBadProof.poll(0).front().packet
    );
    deniedGuest.roomAdmissionProof = {0x00U};
    if (!require(
            !coordinator.receive(deniedGuest, actions, error),
            "invalid password-room admission proof was accepted"
        )) {
        return false;
    }
    RuntimeInternetConnection guest;
    RuntimeNetworkPacket guestJoin;
    RuntimeInternetCoordinatorAction guestHostOffer;
    RuntimeInternetCoordinatorAction guestRemoteOffer;
    if (!require(
            beginJoinAndOffer(
                coordinator,
                guest,
                guestConfig,
                guestJoin,
                guestHostOffer,
                guestRemoteOffer,
                error,
                {0x41U, 0x42U, 0x43U}
            ),
            "valid password-room admission failed: " + error
        )) {
        return false;
    }
    Offer guestOffer;
    if (!offerFromAction(guestRemoteOffer, guestOffer, error)) {
        return false;
    }

    RuntimeInternetConnection browser;
    RuntimeInternetConnectionConfig browserConfig = connectionConfig(
        "browser",
        RuntimeInternetRole::Client,
        4001,
        {},
        fingerprintA,
        candidate("192.0.2.50", 45000)
    );
    browserConfig.localCandidates.clear();
    if (!browser.beginRoomQuery(browserConfig, 0, error)) {
        return false;
    }
    const RuntimeNetworkPacket query = browser.poll(0).front().packet;
    if (!require(
            coordinator.receive(
                coordinatorEnvelope("browser", query),
                actions,
                error
            ),
            "fingerprint directory query failed: " + error
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* queryResult = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "browser"
    );
    if (!require(
            queryResult != nullptr
                && acceptCoordinatorAction(browser, *queryResult, 0, error)
                && browser.rooms().size() == 1U
                && browser.rooms().front().roomId == "room-a"
                && browser.rooms().front().playerCount == 3U,
            "directory did not filter fingerprint or compute player count"
        )) {
        return false;
    }

    RuntimeNetworkPacket crossRoomRequest = relayRequestPacket(
        "client-b",
        "room-b",
        clientBOffer,
        RuntimeInternetRelayTransport::Tcp,
        error
    );
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("client-b", crossRoomRequest),
                actions,
                error
            ),
            "cross-room relay request was accepted"
        )) {
        return false;
    }

    RuntimeInternetConnection hostRouteB;
    if (!require(
            hostRouteB.beginPeerRouteFromCoordinator(
                hostConfig,
                clientBHostOffer.packet,
                serviceId,
                0,
                error
            )
                && acceptCoordinatorAction(
                    clientB,
                    clientBRemoteOffer,
                    0,
                    error
                ),
            "client B route setup failed: " + error
        )) {
        return false;
    }
    RuntimeInternetConnection hostRouteSpectator;
    if (!require(
            hostRouteSpectator.beginPeerRouteFromCoordinator(
                hostConfig,
                spectatorHostOffer.packet,
                serviceId,
                0,
                error
            )
                && acceptCoordinatorAction(
                    spectator,
                    spectatorRemoteOffer,
                    0,
                    error
                ),
            "spectator route setup failed: " + error
        )) {
        return false;
    }

    clockMilliseconds = 1300;
    const std::vector<RuntimeInternetAction> hostBRelayRequest =
        hostRouteB.poll(300);
    const std::vector<RuntimeInternetAction> clientBRelayRequest =
        clientB.poll(300);
    if (!require(
            hostBRelayRequest.size() == 1U
                && clientBRelayRequest.size() == 1U,
            "NAT timeout did not request TCP relay for both peers"
        )) {
        return false;
    }
    std::vector<RuntimeInternetCoordinatorAction> hostBLeaseActions;
    std::vector<RuntimeInternetCoordinatorAction> clientBLeaseActions;
    if (!require(
            coordinator.receive(
                coordinatorEnvelope(
                    "host-a",
                    hostBRelayRequest.front().packet
                ),
                hostBLeaseActions,
                error
            )
                && coordinator.receive(
                    coordinatorEnvelope(
                        "client-b",
                        clientBRelayRequest.front().packet
                    ),
                    clientBLeaseActions,
                    error
                ),
            "TCP relay leases were not issued: " + error
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* hostBLease = findAction(
        hostBLeaseActions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "host-a"
    );
    const RuntimeInternetCoordinatorAction* clientBLease = findAction(
        clientBLeaseActions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "client-b"
    );
    if (!require(
            hostBLease != nullptr && clientBLease != nullptr,
            "coordinator omitted one side of the TCP relay lease"
        )) {
        return false;
    }
    RuntimeInternetControlMessage hostBLeaseMessage;
    RuntimeInternetControlMessage clientBLeaseMessage;
    if (!decodeMessage(hostBLease->packet, hostBLeaseMessage, error)
        || !decodeMessage(clientBLease->packet, clientBLeaseMessage, error)) {
        return false;
    }
    const auto hostBLeaseBody =
        std::get<RuntimeInternetRelayLeaseBody>(hostBLeaseMessage.body);
    const auto clientBLeaseBody =
        std::get<RuntimeInternetRelayLeaseBody>(clientBLeaseMessage.body);
    if (!require(
            hostBLeaseBody.ticket.size() == 32U
                && clientBLeaseBody.ticket.size() == 32U
                && hostBLeaseBody.ticket != clientBLeaseBody.ticket
                && std::any_of(
                    hostBLeaseBody.ticket.begin(),
                    hostBLeaseBody.ticket.end(),
                    [](std::uint8_t value) { return value != 0U; }
                )
                && std::any_of(
                    clientBLeaseBody.ticket.begin(),
                    clientBLeaseBody.ticket.end(),
                    [](std::uint8_t value) { return value != 0U; }
                ),
            "relay tickets were not unique, bounded, and non-zero"
        )) {
        return false;
    }
    if (!require(
            acceptCoordinatorAction(
                    hostRouteB,
                    *hostBLease,
                    300,
                    error
                )
                && acceptCoordinatorAction(
                    clientB,
                    *clientBLease,
                    300,
                    error
                ),
            "TCP relay clients rejected coordinator leases: " + error
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> hostBBind = hostRouteB.poll(300);
    const std::vector<RuntimeInternetAction> clientBBind = clientB.poll(300);
    if (!require(
            hostBBind.size() == 1U && clientBBind.size() == 1U
                && coordinator.receive(
                    relayEnvelope(
                        "host-a",
                        RuntimeInternetRelayTransport::Tcp,
                        hostBBind.front().packet
                    ),
                    actions,
                    error
                )
                && actions.empty(),
            "first TCP relay bind was not staged: " + error
        )) {
        return false;
    }
    if (!require(
            coordinator.receive(
                relayEnvelope(
                    "host-a",
                    RuntimeInternetRelayTransport::Tcp,
                    hostBBind.front().packet
                ),
                actions,
                error
            )
                && actions.empty(),
            "pre-activation bind retry was not idempotent"
        )) {
        return false;
    }
    if (!require(
            coordinator.receive(
                relayEnvelope(
                    "client-b",
                    RuntimeInternetRelayTransport::Tcp,
                    clientBBind.front().packet
                ),
                actions,
                error
            )
                && actions.size() == 3U
                && actions.front().kind
                    == RuntimeInternetCoordinatorActionKind::ActivateRelayRoute,
            "second TCP relay bind did not activate one route: " + error
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* hostBBound = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendRelay,
        "host-a"
    );
    const RuntimeInternetCoordinatorAction* clientBBound = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendRelay,
        "client-b"
    );
    if (!require(
            hostBBound != nullptr && clientBBound != nullptr
                && hostRouteB.receiveRelay(
                    hostBBound->packet,
                    endpoint("relay.self-hosted.example", 47000),
                    serviceId,
                    300,
                    error
                )
                && clientB.receiveRelay(
                    clientBBound->packet,
                    endpoint("relay.self-hosted.example", 47000),
                    serviceId,
                    300,
                    error
                )
                && hostRouteB.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound
                && clientB.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound,
            "TCP relay-bound acknowledgement did not bind both tunnels: " + error
        )) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                relayEnvelope(
                    "client-b",
                    RuntimeInternetRelayTransport::Tcp,
                    clientBBind.front().packet
                ),
                actions,
                error
            ),
            "TCP relay bind ticket replay after activation was accepted"
        )) {
        return false;
    }

    const std::vector<RuntimeInternetAction> hostSpectatorTcpRequest =
        hostRouteSpectator.poll(300);
    const std::vector<RuntimeInternetAction> spectatorTcpRequest =
        spectator.poll(300);
    std::vector<RuntimeInternetCoordinatorAction> hostSpectatorTcpLeaseActions;
    std::vector<RuntimeInternetCoordinatorAction> spectatorTcpLeaseActions;
    if (!require(
            hostSpectatorTcpRequest.size() == 1U
                && spectatorTcpRequest.size() == 1U
                && coordinator.receive(
                    coordinatorEnvelope(
                        "host-a",
                        hostSpectatorTcpRequest.front().packet
                    ),
                    hostSpectatorTcpLeaseActions,
                    error
                ),
            "host TCP relay lease was not issued: " + error
        )) {
        return false;
    }
    if (!require(
            coordinator.receive(
                    coordinatorEnvelope(
                        "spectator-a",
                        spectatorTcpRequest.front().packet
                    ),
                    spectatorTcpLeaseActions,
                    error
                ),
            "spectator TCP relay lease was not issued: "
                + error
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* hostSpectatorTcpLease = findAction(
        hostSpectatorTcpLeaseActions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "host-a"
    );
    const RuntimeInternetCoordinatorAction* spectatorTcpLease = findAction(
        spectatorTcpLeaseActions,
        RuntimeInternetCoordinatorActionKind::SendCoordinator,
        "spectator-a"
    );
    if (!require(
            hostSpectatorTcpLease != nullptr && spectatorTcpLease != nullptr
                && acceptCoordinatorAction(
                    hostRouteSpectator,
                    *hostSpectatorTcpLease,
                    300,
                    error
                )
                && acceptCoordinatorAction(
                    spectator,
                    *spectatorTcpLease,
                    300,
                    error
                ),
            "TCP relay clients rejected coordinator leases: " + error
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> hostSpectatorTcpBind =
        hostRouteSpectator.poll(300);
    const std::vector<RuntimeInternetAction> spectatorTcpBind =
        spectator.poll(300);
    if (!require(
            coordinator.receive(
                relayEnvelope(
                    "host-a",
                    RuntimeInternetRelayTransport::Tcp,
                    hostSpectatorTcpBind.front().packet
                ),
                actions,
                error
            )
                && actions.empty()
                && coordinator.receive(
                    relayEnvelope(
                        "spectator-a",
                        RuntimeInternetRelayTransport::Tcp,
                        spectatorTcpBind.front().packet
                    ),
                    actions,
                    error
                )
                && actions.size() == 3U,
            "TCP spectator relay did not bind both principals: " + error
        )) {
        return false;
    }
    const RuntimeInternetCoordinatorAction* hostSpectatorBound = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendRelay,
        "host-a"
    );
    const RuntimeInternetCoordinatorAction* spectatorBound = findAction(
        actions,
        RuntimeInternetCoordinatorActionKind::SendRelay,
        "spectator-a"
    );
    if (!require(
            hostSpectatorBound != nullptr && spectatorBound != nullptr
                && hostRouteSpectator.receiveRelay(
                    hostSpectatorBound->packet,
                    endpoint("relay.self-hosted.example", 47000),
                    serviceId,
                    300,
                    error
                )
                && spectator.receiveRelay(
                    spectatorBound->packet,
                    endpoint("relay.self-hosted.example", 47000),
                    serviceId,
                    300,
                    error
                )
                && spectator.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound
                && spectator.reportGameplaySessionJoined(
                    spectatorOffer.connectionId,
                    error
                )
                && spectator.state()
                    == RuntimeInternetConnectionState::GameplaySessionConnected
                && coordinator.relayRouteCount() == 2U,
            "spectator TCP relay binding and gameplay admission failed: "
                + error
        )) {
        return false;
    }

    const RuntimeNetworkPacket guestRelay = relayRequestPacket(
        "guest-b",
        "room-b",
        guestOffer,
        RuntimeInternetRelayTransport::Udp,
        error
    );
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("guest-b", guestRelay),
                actions,
                error
            ),
            "relay route quota was not enforced"
        )) {
        return false;
    }

    const RuntimeNetworkPacket stolenTicket = relayBindPacket(
        "client-a",
        "room-a",
        clientBOffer.connectionId,
        RuntimeInternetRelayTransport::Tcp,
        clientBLeaseBody.ticket,
        error
    );
    if (!require(
            !coordinator.receive(
                relayEnvelope(
                    "client-a",
                    RuntimeInternetRelayTransport::Tcp,
                    stolenTicket
                ),
                actions,
                error
            ),
            "relay ticket was not bound to its authenticated principal"
        )) {
        return false;
    }

    RuntimeNetworkPacket malformed = roomARegistration;
    malformed.payload.push_back(0U);
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("host-a", malformed),
                actions,
                error
            )
                && coordinator.roomCount() == 2U,
            "non-canonical attack packet changed coordinator state"
        )) {
        return false;
    }

    clockMilliseconds = 2800;
    actions = coordinator.poll();
    if (!require(
            actions.size() == 2U
                && std::all_of(
                    actions.begin(),
                    actions.end(),
                    [](const RuntimeInternetCoordinatorAction& action) {
                        return action.kind
                            == RuntimeInternetCoordinatorActionKind::DeactivateRelayRoute;
                    }
                )
                && coordinator.relayRouteCount() == 0U,
            "relay lease expiration did not deactivate both routes"
        )) {
        return false;
    }

    clockMilliseconds = 5000;
    actions = coordinator.poll();
    if (!require(
            coordinator.connectionCount() == 0U
                && coordinator.roomCount() == 2U,
            "connection leases did not expire independently of room leases"
        )) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("client-a", clientAJoin),
                actions,
                error
            ),
            "expired join transaction replay was accepted"
        )) {
        return false;
    }

    clockMilliseconds = 6000;
    actions = coordinator.poll();
    if (!require(
            coordinator.roomCount() == 0U,
            "room leases did not expire without host refresh"
        )) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("host-a", roomARegistration),
                actions,
                error
            ),
            "expired room registration replay was accepted"
        )) {
        return false;
    }

    coordinator.close();
    return require(
        !coordinator.isOpen()
            && coordinator.roomCount() == 0U
            && coordinator.connectionCount() == 0U,
        "coordinator close did not clear authoritative state"
    );
}

bool testInvalidConfiguration() {
    RuntimeInternetCoordinator coordinator;
    RuntimeInternetCoordinatorConfig config;
    config.serviceId = serviceId;
    config.udpRelayEndpoint = endpoint("relay.example", 46000);
    config.tcpRelayEndpoint = endpoint("relay.example", 47000);
    config.maximumRooms = runtimeInternetCoordinatorHardMaximumRooms + 1U;
    config.nowMilliseconds = []() { return 0U; };
    config.secureRandomBytes = [](std::uint8_t*, std::size_t) { return true; };
    std::string error;
    return require(
        !coordinator.open(std::move(config), error)
            && !coordinator.isOpen(),
        "hard coordinator capacity limit was not enforced"
    );
}

bool testFullRuntimeRosterUsesProductionPeerQuota() {
    RuntimeInternetCoordinatorConfig config;
    config.serviceId = serviceId;
    config.udpRelayEndpoint = endpoint("relay.capacity.invalid", 49000U);
    config.tcpRelayEndpoint = endpoint("relay.capacity.invalid", 49001U);
    config.nowMilliseconds = []() { return 0U; };
    std::uint8_t randomByte = 1U;
    config.secureRandomBytes = [&randomByte](
        std::uint8_t* destination,
        std::size_t size
    ) {
        for (std::size_t index = 0U; index < size; ++index) {
            destination[index] = randomByte++;
            if (randomByte == 0U) randomByte = 1U;
        }
        return true;
    };
    if (!require(
            config.maximumConnectionsPerPeer >= 16U,
            "production peer quota cannot host a 16-member runtime room"
        )) {
        return false;
    }

    RuntimeInternetCoordinator coordinator;
    std::string error;
    if (!require(
            coordinator.open(std::move(config), error),
            "capacity coordinator did not open: " + error
        )) {
        return false;
    }

    RuntimeInternetControlMessage registration = peerMessage(
        RuntimeInternetControlType::RegisterRoom,
        7001U,
        "capacity-room",
        "capacity-host"
    );
    RuntimeInternetRoomRecord room = roomRecord(
        "capacity-room",
        "capacity-host",
        fingerprintA
    );
    room.maximumPlayers = 8U;
    registration.body = RuntimeInternetRegisterRoomBody{
        room,
        {candidate("198.51.100.80", 48000U)}
    };
    RuntimeNetworkPacket packet;
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (!require(
            encodeMessage(registration, packet, error)
                && coordinator.receive(
                    coordinatorEnvelope("capacity-host", packet),
                    actions,
                    error
                ),
            "capacity room registration failed: " + error
        )) {
        return false;
    }

    const auto join = [&](const std::string& peerId,
                          RuntimeInternetRole role,
                          std::uint64_t transactionId) {
        RuntimeInternetControlMessage request = peerMessage(
            RuntimeInternetControlType::JoinRoom,
            transactionId,
            "capacity-room",
            peerId
        );
        request.body = RuntimeInternetJoinRoomBody{
            role,
            fingerprintA,
            {candidate("198.51.100.81", 48001U)}
        };
        RuntimeNetworkPacket requestPacket;
        actions.clear();
        return encodeMessage(request, requestPacket, error)
            && coordinator.receive(
                coordinatorEnvelope(peerId, requestPacket),
                actions,
                error
            );
    };

    for (std::size_t index = 0U; index < 7U; ++index) {
        const std::string peerId = "capacity-client-"
            + std::to_string(index);
        if (!require(
                join(peerId, RuntimeInternetRole::Client, 7100U + index),
                "full-roster combatant join failed: " + error
            )) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        const std::string peerId = "capacity-spectator-"
            + std::to_string(index);
        if (!require(
                join(peerId, RuntimeInternetRole::Spectator, 7200U + index),
                "full-roster spectator join failed: " + error
            )) {
            return false;
        }
    }
    if (!require(
            coordinator.connectionCount() == 15U,
            "8+8 runtime roster did not reserve exactly 15 remote routes"
        )) {
        return false;
    }

    error.clear();
    if (!require(
            !join(
                "capacity-spectator-overflow",
                RuntimeInternetRole::Spectator,
                7300U
            )
                && error == "room spectator capacity reached"
                && coordinator.connectionCount() == 15U,
            "full roster was not rejected by the canonical spectator limit"
        )) {
        return false;
    }

    error.clear();
    if (!require(
            coordinator.authenticatedPeerDisconnected(
                serviceId,
                "capacity-spectator-0",
                actions,
                error
            )
                && coordinator.connectionCount() == 14U
                && join(
                    "capacity-spectator-replacement",
                    RuntimeInternetRole::Spectator,
                    7301U
                )
                && coordinator.connectionCount() == 15U,
            "disconnect did not reclaim full-roster peer quota: " + error
        )) {
        return false;
    }
    coordinator.close();
    return true;
}

bool testOrdinaryPasswordRoomProof() {
    std::uint64_t clockMilliseconds = 0U;
    std::uint8_t randomByte = 1U;
    int externalAuthorizations = 0;
    RuntimeInternetCoordinatorConfig config;
    config.serviceId = serviceId;
    config.udpRelayEndpoint = endpoint("relay.password.invalid", 48100U);
    config.tcpRelayEndpoint = endpoint("relay.password.invalid", 48101U);
    config.roomLeaseMilliseconds = 5000U;
    config.connectionLeaseMilliseconds = 5000U;
    config.relayLeaseMilliseconds = 5000U;
    config.replayRetentionMilliseconds = 10000U;
    config.maximumRooms = 2U;
    config.maximumRoomsPerPeer = 1U;
    config.maximumConnections = 4U;
    config.maximumConnectionsPerRoom = 2U;
    config.maximumSpectatorsPerRoom = 1U;
    config.maximumConnectionsPerPeer = 2U;
    config.maximumRelayRoutes = 4U;
    config.maximumReplayTombstones = 8U;
    config.nowMilliseconds = [&]() { return clockMilliseconds; };
    config.secureRandomBytes = [&randomByte](
        std::uint8_t* destination,
        std::size_t size
    ) {
        for (std::size_t index = 0U; index < size; ++index) {
            destination[index] = randomByte++;
            if (randomByte == 0U) randomByte = 1U;
        }
        return true;
    };
    config.authorizePasswordJoin = [&externalAuthorizations](
        const std::string&,
        const std::string&,
        const std::vector<std::uint8_t>&
    ) {
        ++externalAuthorizations;
        return true;
    };

    RuntimeInternetCoordinator coordinator;
    std::string error;
    if (!require(
            coordinator.open(std::move(config), error),
            "password coordinator did not open: " + error
        )) {
        return false;
    }

    constexpr std::uint64_t registrationTransaction = 8101U;
    RuntimeInternetConnection host;
    RuntimeInternetRoomRecord room = roomRecord(
        "ordinary-password-room",
        "password-host",
        fingerprintA,
        true
    );
    const RuntimeInternetConnectionConfig hostConfig = connectionConfig(
        "password-host",
        RuntimeInternetRole::Host,
        registrationTransaction,
        room.roomId,
        fingerprintA,
        candidate("198.51.100.81", 48110U)
    );
    if (!host.beginHost(hostConfig, room, 0U, error)) return false;
    const RuntimeNetworkPacket registration = host.poll(0U).front().packet;
    RuntimeRoomPasswordVerifier verifier = runtimeRoomPasswordVerifier(
        room.roomId,
        "correct horse battery staple"
    );
    const RuntimeRoomPasswordVerifier expectedVerifier{{
        0x95U, 0xedU, 0x64U, 0x27U, 0xc6U, 0x96U, 0xe4U, 0x70U,
        0x74U, 0x0eU, 0x61U, 0xd6U, 0x86U, 0xaaU, 0xb4U, 0xbfU,
        0xd7U, 0x9cU, 0x48U, 0x2aU, 0xd8U, 0x52U, 0xf5U, 0x93U,
        0xbdU, 0x07U, 0x49U, 0x2eU, 0xdcU, 0x11U, 0xf4U, 0x2dU
    }};
    if (!require(
            verifier == expectedVerifier,
            "room password verifier diverged from the independent SHA-256 vector"
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        return false;
    }
    RuntimeInternetServiceEnvelope registrationEnvelope =
        coordinatorEnvelope("password-host", registration);
    registrationEnvelope.roomAdmissionProof.assign(
        verifier.begin(),
        verifier.end()
    );
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (!require(
            coordinator.receive(
                registrationEnvelope,
                actions,
                error
            ) && coordinator.roomCount() == 1U,
            "ordinary password room registration failed: " + error
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        return false;
    }
    error.clear();
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("password-host", registration),
                actions,
                error
            ) && coordinator.roomCount() == 1U,
            "password registration replay without its verifier was accepted"
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        return false;
    }

    constexpr std::uint64_t joinTransaction = 8201U;
    RuntimeInternetConnection guest;
    const RuntimeInternetConnectionConfig guestConfig = connectionConfig(
        "password-guest",
        RuntimeInternetRole::Client,
        joinTransaction,
        room.roomId,
        fingerprintA,
        candidate("203.0.113.81", 48120U)
    );
    if (!guest.beginJoin(guestConfig, 0U, error)) {
        runtimeRoomPasswordSecureClear(verifier);
        return false;
    }
    const RuntimeNetworkPacket join = guest.poll(0U).front().packet;
    RuntimeRoomPasswordVerifier wrongVerifier = runtimeRoomPasswordVerifier(
        room.roomId,
        "wrong password"
    );
    RuntimeRoomPasswordProof wrongProof =
        runtimeInternetRoomPasswordJoinProof(
            wrongVerifier,
            room.roomId,
            guestConfig.localPeerId,
            joinTransaction
        );
    RuntimeInternetServiceEnvelope wrongEnvelope =
        coordinatorEnvelope("password-guest", join);
    wrongEnvelope.roomAdmissionProof.assign(
        wrongProof.begin(),
        wrongProof.end()
    );
    error.clear();
    if (!require(
            !coordinator.receive(wrongEnvelope, actions, error)
                && coordinator.connectionCount() == 0U
                && externalAuthorizations == 0,
            "wrong ordinary room password was accepted or bypassed locally"
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        runtimeRoomPasswordSecureClear(wrongVerifier);
        runtimeRoomPasswordSecureClear(wrongProof);
        return false;
    }

    RuntimeRoomPasswordProof correctProof =
        runtimeInternetRoomPasswordJoinProof(
            verifier,
            room.roomId,
            guestConfig.localPeerId,
            joinTransaction
        );
    const RuntimeRoomPasswordProof expectedProof{{
        0x9fU, 0x68U, 0x69U, 0x8eU, 0xebU, 0x7fU, 0xf3U, 0xb2U,
        0xb8U, 0x80U, 0x10U, 0xf4U, 0x44U, 0x6dU, 0xcfU, 0x1fU,
        0xcdU, 0x09U, 0xebU, 0x7cU, 0x37U, 0xb5U, 0x60U, 0xd9U,
        0x2bU, 0x13U, 0xbfU, 0xadU, 0x4eU, 0x37U, 0x57U, 0xb7U
    }};
    if (!require(
            correctProof == expectedProof,
            "room admission proof diverged from the independent HMAC-SHA-256 vector"
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        runtimeRoomPasswordSecureClear(wrongVerifier);
        runtimeRoomPasswordSecureClear(wrongProof);
        runtimeRoomPasswordSecureClear(correctProof);
        return false;
    }
    RuntimeInternetServiceEnvelope correctEnvelope =
        coordinatorEnvelope("password-guest", join);
    correctEnvelope.roomAdmissionProof.assign(
        correctProof.begin(),
        correctProof.end()
    );
    error.clear();
    const bool admitted = require(
        coordinator.receive(correctEnvelope, actions, error)
            && coordinator.connectionCount() == 1U
            && actions.size() == 2U
            && externalAuthorizations == 0,
        "correct ordinary room password proof was rejected: " + error
    );
    error.clear();
    const bool replayBound = require(
        !coordinator.receive(
            coordinatorEnvelope("password-guest", join),
            actions,
            error
        ) && coordinator.connectionCount() == 1U,
        "authorized join replay dropped its bound password proof"
    );
    runtimeRoomPasswordSecureClear(verifier);
    runtimeRoomPasswordSecureClear(wrongVerifier);
    runtimeRoomPasswordSecureClear(wrongProof);
    runtimeRoomPasswordSecureClear(correctProof);
    coordinator.close();
    return admitted && replayBound;
}

bool testSealedRoomAndDisconnectLifecycle() {
    std::uint64_t clockMilliseconds = 0U;
    std::uint8_t randomByte = 1U;
    RuntimeInternetCoordinatorConfig config;
    config.serviceId = serviceId;
    config.udpRelayEndpoint = endpoint("relay.lifecycle.invalid", 48000U);
    config.tcpRelayEndpoint = endpoint("relay.lifecycle.invalid", 48001U);
    config.roomLeaseMilliseconds = 1000U;
    config.connectionLeaseMilliseconds = 10000U;
    config.relayLeaseMilliseconds = 10000U;
    config.replayRetentionMilliseconds = 20000U;
    config.maximumRooms = 3U;
    config.maximumRoomsPerPeer = 1U;
    config.maximumConnections = 6U;
    config.maximumConnectionsPerRoom = 3U;
    config.maximumSpectatorsPerRoom = 1U;
    config.maximumConnectionsPerPeer = 4U;
    config.maximumRelayRoutes = 4U;
    config.maximumReplayTombstones = 16U;
    config.nowMilliseconds = [&]() { return clockMilliseconds; };
    config.secureRandomBytes = [&randomByte](
        std::uint8_t* destination,
        std::size_t size
    ) {
        for (std::size_t index = 0U; index < size; ++index) {
            destination[index] = randomByte++;
            if (randomByte == 0U) randomByte = 1U;
        }
        return true;
    };

    RuntimeInternetCoordinator coordinator;
    std::string error;
    if (!require(
            coordinator.open(std::move(config), error),
            "lifecycle coordinator did not open: " + error
        )) {
        return false;
    }

    constexpr std::uint64_t registrationTransaction = 5101U;
    RuntimeInternetControlMessage registration = peerMessage(
        RuntimeInternetControlType::RegisterRoom,
        registrationTransaction,
        "sealed-room",
        "seal-host"
    );
    RuntimeInternetRoomRecord lifecycleRoom = roomRecord(
        "sealed-room",
        "seal-host",
        fingerprintA
    );
    lifecycleRoom.maximumPlayers = 4U;
    registration.body = RuntimeInternetRegisterRoomBody{
        lifecycleRoom,
        {candidate("198.51.100.71", 47100U)}
    };
    RuntimeNetworkPacket registrationPacket;
    if (!encodeMessage(registration, registrationPacket, error)) {
        return false;
    }
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (!require(
            coordinator.receive(
                coordinatorEnvelope("seal-host", registrationPacket),
                actions,
                error
            )
                && coordinator.roomCount() == 1U,
            "lifecycle room registration failed: " + error
        )) {
        return false;
    }

    std::uint64_t queryTransaction = 5200U;
    const auto listedRooms = [&](std::size_t expected) {
        RuntimeInternetControlMessage query = peerMessage(
            RuntimeInternetControlType::RoomListRequest,
            ++queryTransaction,
            {},
            "browser"
        );
        query.body = RuntimeInternetRoomListRequestBody{fingerprintA};
        RuntimeNetworkPacket packet;
        if (!encodeMessage(query, packet, error)
            || !coordinator.receive(
                coordinatorEnvelope("browser", packet),
                actions,
                error
            )) {
            return false;
        }
        const RuntimeInternetCoordinatorAction* result = findAction(
            actions,
            RuntimeInternetCoordinatorActionKind::SendCoordinator,
            "browser"
        );
        RuntimeInternetControlMessage decoded;
        return result != nullptr
            && decodeMessage(result->packet, decoded, error)
            && decoded.type == RuntimeInternetControlType::RoomListResult
            && std::get<RuntimeInternetRoomListResultBody>(decoded.body)
                    .rooms.size() == expected;
    };
    if (!require(listedRooms(1U), "unsealed room was not discoverable")) {
        return false;
    }

    RuntimeInternetControlMessage heartbeat = peerMessage(
        RuntimeInternetControlType::CoordinatorHeartbeat,
        5250U,
        {},
        "browser"
    );
    heartbeat.body = RuntimeInternetCoordinatorHeartbeatBody{77U};
    RuntimeNetworkPacket heartbeatPacket;
    if (!encodeMessage(heartbeat, heartbeatPacket, error)
        || !require(
            coordinator.receive(
                coordinatorEnvelope("browser", heartbeatPacket),
                actions,
                error
            )
                && actions.size() == 1U,
            "authenticated coordinator heartbeat was not acknowledged"
        )) {
        return false;
    }
    RuntimeInternetControlMessage heartbeatAck;
    if (!require(
            decodeMessage(actions.front().packet, heartbeatAck, error)
                && heartbeatAck.type
                    == RuntimeInternetControlType::CoordinatorHeartbeatAck
                && heartbeatAck.transactionId == heartbeat.transactionId,
            "coordinator heartbeat acknowledgement was not exact"
        )) {
        return false;
    }

    const auto joinPeer = [&](
        const std::string& peerId,
        std::uint64_t transactionId,
        std::uint16_t port,
        Offer& offer
    ) {
        RuntimeInternetControlMessage join = peerMessage(
            RuntimeInternetControlType::JoinRoom,
            transactionId,
            "sealed-room",
            peerId
        );
        join.body = RuntimeInternetJoinRoomBody{
            RuntimeInternetRole::Client,
            fingerprintA,
            {candidate("198.51.100.72", port)}
        };
        RuntimeNetworkPacket packet;
        if (!encodeMessage(join, packet, error)
            || !coordinator.receive(
                coordinatorEnvelope(peerId, packet),
                actions,
                error
            )) {
            return false;
        }
        const RuntimeInternetCoordinatorAction* remote = findAction(
            actions,
            RuntimeInternetCoordinatorActionKind::SendCoordinator,
            peerId
        );
        return remote != nullptr
            && offerFromAction(*remote, offer, error);
    };
    Offer firstOffer;
    Offer secondOffer;
    if (!require(
            joinPeer("seal-client-a", 5301U, 47201U, firstOffer)
                && joinPeer("seal-client-b", 5302U, 47202U, secondOffer)
                && coordinator.connectionCount() == 2U,
            "pre-seal clients did not join: " + error
        )) {
        return false;
    }

    const auto activateTcp = [&](const std::string& peerId, const Offer& offer) {
        std::vector<RuntimeInternetCoordinatorAction> hostLeaseActions;
        std::vector<RuntimeInternetCoordinatorAction> peerLeaseActions;
        const RuntimeNetworkPacket hostRequest = relayRequestPacket(
            "seal-host",
            "sealed-room",
            offer,
            RuntimeInternetRelayTransport::Tcp,
            error
        );
        const RuntimeNetworkPacket peerRequest = relayRequestPacket(
            peerId,
            "sealed-room",
            offer,
            RuntimeInternetRelayTransport::Tcp,
            error
        );
        if (!coordinator.receive(
                coordinatorEnvelope("seal-host", hostRequest),
                hostLeaseActions,
                error
            )
            || !coordinator.receive(
                coordinatorEnvelope(peerId, peerRequest),
                peerLeaseActions,
                error
            )) {
            return false;
        }
        const RuntimeInternetCoordinatorAction* hostLease = findAction(
            hostLeaseActions,
            RuntimeInternetCoordinatorActionKind::SendCoordinator,
            "seal-host"
        );
        const RuntimeInternetCoordinatorAction* peerLease = findAction(
            peerLeaseActions,
            RuntimeInternetCoordinatorActionKind::SendCoordinator,
            peerId
        );
        if (hostLease == nullptr || peerLease == nullptr) return false;
        RuntimeInternetControlMessage hostLeaseMessage;
        RuntimeInternetControlMessage peerLeaseMessage;
        if (!decodeMessage(hostLease->packet, hostLeaseMessage, error)
            || !decodeMessage(peerLease->packet, peerLeaseMessage, error)) {
            return false;
        }
        const auto hostBody = std::get<RuntimeInternetRelayLeaseBody>(
            hostLeaseMessage.body
        );
        const auto peerBody = std::get<RuntimeInternetRelayLeaseBody>(
            peerLeaseMessage.body
        );
        const RuntimeNetworkPacket hostBind = relayBindPacket(
            "seal-host",
            "sealed-room",
            offer.connectionId,
            RuntimeInternetRelayTransport::Tcp,
            hostBody.ticket,
            error
        );
        const RuntimeNetworkPacket peerBind = relayBindPacket(
            peerId,
            "sealed-room",
            offer.connectionId,
            RuntimeInternetRelayTransport::Tcp,
            peerBody.ticket,
            error
        );
        return coordinator.receive(
                relayEnvelope(
                    "seal-host",
                    RuntimeInternetRelayTransport::Tcp,
                    hostBind
                ),
                actions,
                error
            )
            && coordinator.receive(
                relayEnvelope(
                    peerId,
                    RuntimeInternetRelayTransport::Tcp,
                    peerBind
                ),
                actions,
                error
            )
            && std::any_of(
                actions.begin(),
                actions.end(),
                [](const RuntimeInternetCoordinatorAction& action) {
                    return action.kind
                        == RuntimeInternetCoordinatorActionKind::
                            ActivateRelayRoute;
                }
            );
    };
    if (!require(
            activateTcp("seal-client-a", firstOffer)
                && activateTcp("seal-client-b", secondOffer)
                && coordinator.relayRouteCount() == 2U,
            "pre-seal TCP routes did not activate: " + error
        )) {
        return false;
    }

    RuntimeInternetControlMessage seal = peerMessage(
        RuntimeInternetControlType::SealRoom,
        registrationTransaction,
        "sealed-room",
        "seal-host"
    );
    seal.body = RuntimeInternetSealRoomBody{};
    RuntimeNetworkPacket sealPacket;
    if (!encodeMessage(seal, sealPacket, error)) return false;
    if (!require(
            coordinator.receive(
                coordinatorEnvelope("seal-host", sealPacket),
                actions,
                error
            )
                && actions.size() == 1U,
            "host-authenticated SealRoom failed: " + error
        )) {
        return false;
    }
    RuntimeInternetControlMessage sealAcknowledgement;
    if (!require(
            decodeMessage(
                actions.front().packet,
                sealAcknowledgement,
                error
            )
                && sealAcknowledgement.type
                    == RuntimeInternetControlType::RoomSealed
                && coordinator.receive(
                    coordinatorEnvelope("seal-host", sealPacket),
                    actions,
                    error
                ),
            "SealRoom exact replay was not idempotent"
        )) {
        return false;
    }
    RuntimeInternetControlMessage wrongTransaction = seal;
    wrongTransaction.transactionId = registrationTransaction + 1U;
    RuntimeNetworkPacket wrongTransactionPacket;
    if (!encodeMessage(
            wrongTransaction,
            wrongTransactionPacket,
            error
        )) {
        return false;
    }
    if (!require(
            !coordinator.receive(
                coordinatorEnvelope("seal-host", wrongTransactionPacket),
                actions,
                error
            )
                && !coordinator.receive(
                    coordinatorEnvelope("attacker", sealPacket),
                    actions,
                    error
                ),
            "wrong transaction or forged SealRoom identity was accepted"
        )) {
        return false;
    }
    if (!require(listedRooms(0U), "sealed room remained discoverable")) {
        return false;
    }
    Offer rejectedOffer;
    if (!require(
            !joinPeer("late-client", 5303U, 47203U, rejectedOffer)
                && coordinator.connectionCount() == 2U,
            "sealed room accepted a new join"
        )) {
        return false;
    }

    for (const std::uint64_t time : {600U, 1200U, 1800U, 2400U}) {
        clockMilliseconds = time;
        if (!require(
                coordinator.receive(
                    coordinatorEnvelope("seal-host", registrationPacket),
                    actions,
                    error
                ),
                "sealed room lease heartbeat failed: " + error
            )) {
            return false;
        }
    }
    if (!require(
            coordinator.roomCount() == 1U
                && coordinator.connectionCount() == 2U
                && coordinator.relayRouteCount() == 2U
                && listedRooms(0U),
            "sealed heartbeat reopened listing or broke existing TCP routes"
        )) {
        return false;
    }

    if (!require(
            coordinator.relayChannelClosed(
                firstOffer.connectionId,
                RuntimeInternetRelayTransport::Tcp,
                serviceId,
                "seal-client-a",
                actions,
                error
            )
                && coordinator.roomCount() == 1U
                && coordinator.connectionCount() == 2U
                && coordinator.relayRouteCount() == 1U,
            "sealed combatant route was not retained without its old relay"
        )) {
        return false;
    }
    RuntimeInternetControlMessage resume = peerMessage(RuntimeInternetControlType::ResumeRoute,
        firstOffer.connectionId, "sealed-room", "seal-client-a", serviceId);
    resume.body = RuntimeInternetResumeRouteBody{firstOffer.connectionId, firstOffer.token};
    RuntimeNetworkPacket resumePacket;
    if (!encodeMessage(resume, resumePacket, error)) return false;
    if (!require(!coordinator.receive(coordinatorEnvelope("intruder", resumePacket), actions, error),
            "another account resumed a retained route")) return false;
    if (!require(coordinator.receive(coordinatorEnvelope("seal-client-a", resumePacket), actions, error),
            "retained route resume failed: " + error)) return false;
    const auto* resumedAction = findAction(actions, RuntimeInternetCoordinatorActionKind::SendCoordinator, "seal-client-a");
    Offer resumed;
    RuntimeInternetControlMessage resumedMessage;
    if (!require(resumedAction && offerFromAction(*resumedAction, resumed, error)
            && decodeMessage(resumedAction->packet, resumedMessage, error)
            && std::get<RuntimeInternetPeerCandidatesBody>(resumedMessage.body).resumesConnectionId == firstOffer.connectionId
            && resumed.connectionId != firstOffer.connectionId && resumed.token != firstOffer.token,
            "resume reused the previous route identity or token")) return false;
    if (!require(coordinator.receive(coordinatorEnvelope("seal-client-a", resumePacket), actions, error)
            && coordinator.connectionCount() == 2U, "resume request retry duplicated membership")) return false;
    if (!require(activateTcp("seal-client-a", resumed), "fresh resumed relay did not activate: " + error)) return false;
    if (!require(coordinator.relayChannelClosed(resumed.connectionId, RuntimeInternetRelayTransport::Tcp,
            serviceId, "seal-client-a", actions, error), "resumed relay could not enter a second outage")) return false;
    if (!require(
            coordinator.authenticatedPeerDisconnected(
                serviceId,
                "seal-host",
                actions,
                error
            )
                && actions.size() == 1U
                && actions.front().kind
                    == RuntimeInternetCoordinatorActionKind::
                        DeactivateRelayRoute
                && coordinator.roomCount() == 0U
                && coordinator.connectionCount() == 0U
                && coordinator.relayRouteCount() == 0U,
            "coordinator disconnect did not synchronously reclaim room/routes"
        )) {
        return false;
    }
    coordinator.close();
    return true;
}

} // namespace

int main() {
    if (!testInvalidConfiguration()
        || !testFullRuntimeRosterUsesProductionPeerQuota()
        || !testCoordinatorFlow()
        || !testOrdinaryPasswordRoomProof()
        || !testSealedRoomAndDisconnectLifecycle()) {
        return 1;
    }
    std::cout
        << "Runtime Internet coordinator smoke passed: "
        << "directory=leased fingerprint-filtered "
        << "candidates=multi-peer+spectator "
        << "relay=udp+tcp-two-party "
        << "security=identity+token+ticket+replay+quota "
        << "capacity=8-combatants+8-spectators "
        << "deployment=adapter-owned-tls-and-sockets\n";
    return 0;
}
