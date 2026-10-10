#include "network/transport/RuntimeInternetConnectivity.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "Runtime Internet connectivity smoke failed: " << message << "\n";
    return false;
}

RuntimeInternetEndpoint endpoint(std::string address, std::uint16_t port) {
    return {std::move(address), port};
}

RuntimeInternetCandidate candidate(
    std::string address,
    std::uint16_t port,
    RuntimeInternetCandidateKind kind = RuntimeInternetCandidateKind::ServerReflexive
) {
    return {kind, endpoint(std::move(address), port), 100};
}

RuntimeInternetConnectionConfig config(
    std::string peerId,
    RuntimeInternetRole role,
    std::uint64_t transaction,
    std::uint16_t port,
    std::string roomId = "room-7"
) {
    RuntimeInternetConnectionConfig result;
    result.serviceId = "coord.prod.example";
    result.localPeerId = std::move(peerId);
    result.roomId = std::move(roomId);
    result.role = role;
    result.contentFingerprint = "rules+map:0123456789abcdef";
    result.localCandidates = {candidate("198.51.100.10", port)};
    result.transactionId = transaction;
    result.retryMilliseconds = 100;
    result.coordinatorTimeoutMilliseconds = 1000;
    result.punchTimeoutMilliseconds = 500;
    result.relayTimeoutMilliseconds = 500;
    result.maximumRetries = 4;
    return result;
}

RuntimeInternetRoomRecord room() {
    RuntimeInternetRoomRecord value;
    value.roomId = "room-7";
    value.roomName = "Ranked test room";
    value.hostPeerId = "host";
    value.contentFingerprint = "rules+map:0123456789abcdef";
    value.playerCount = 1;
    value.maximumPlayers = 4;
    return value;
}

std::vector<std::uint8_t> token(std::uint8_t seed = 0x40U) {
    std::vector<std::uint8_t> result(32);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::uint8_t>(seed + index);
    }
    return result;
}

RuntimeNetworkPacket encode(const RuntimeInternetControlMessage& message) {
    RuntimeNetworkPacket packet;
    std::string error;
    if (!encodeRuntimeInternetControlMessage(message, packet, error)) {
        std::cerr << "test message encode failed: " << error << "\n";
    }
    return packet;
}

RuntimeInternetControlMessage serviceMessage(
    RuntimeInternetControlType type,
    std::uint64_t transaction,
    std::string target,
    std::string roomId = "room-7"
) {
    RuntimeInternetControlMessage message;
    message.type = type;
    message.transactionId = transaction;
    message.serviceId = "coord.prod.example";
    message.roomId = std::move(roomId);
    message.senderPeerId = "coord.prod.example";
    message.targetPeerId = std::move(target);
    return message;
}

bool hasAction(
    const std::vector<RuntimeInternetAction>& actions,
    RuntimeInternetActionKind kind
) {
    return std::any_of(actions.begin(), actions.end(), [&](const auto& action) {
        return action.kind == kind;
    });
}

bool testCodec() {
    std::string error;
    RuntimeInternetControlMessage original;
    original.type = RuntimeInternetControlType::RegisterRoom;
    original.transactionId = 41;
    original.serviceId = "coord.prod.example";
    original.roomId = "room-7";
    original.senderPeerId = "host";
    original.body = RuntimeInternetRegisterRoomBody{
        room(),
        {
            candidate("10.0.0.4", 30000, RuntimeInternetCandidateKind::Host),
            candidate("198.51.100.10", 40000)
        }
    };
    RuntimeNetworkPacket packet;
    RuntimeInternetControlMessage decoded;
    if (!require(
            encodeRuntimeInternetControlMessage(original, packet, error)
                && decodeRuntimeInternetControlMessage(packet, decoded, error)
                && decoded == original,
            "strict codec round trip failed: " + error
        )) return false;

    RuntimeNetworkPacket wrongType = packet;
    wrongType.type = 0x0201;
    if (!require(
            !decodeRuntimeInternetControlMessage(wrongType, decoded, error),
            "wrong outer protocol version was accepted"
        )) return false;

    RuntimeNetworkPacket wrongVersion = packet;
    wrongVersion.payload[5] = 0xffU;
    if (!require(
            !decodeRuntimeInternetControlMessage(wrongVersion, decoded, error),
            "wrong inner protocol version was accepted"
        )) return false;

    RuntimeNetworkPacket truncated = packet;
    truncated.payload.pop_back();
    if (!require(
            !decodeRuntimeInternetControlMessage(truncated, decoded, error),
            "truncated payload was accepted"
        )) return false;

    RuntimeNetworkPacket trailing = packet;
    trailing.payload.push_back(0);
    if (!require(
            !decodeRuntimeInternetControlMessage(trailing, decoded, error),
            "trailing payload was accepted"
        )) return false;

    auto invalid = original;
    auto invalidBody = std::get<RuntimeInternetRegisterRoomBody>(invalid.body);
    invalidBody.candidates.resize(runtimeInternetMaximumCandidates + 1);
    invalid.body = invalidBody;
    if (!require(
            !encodeRuntimeInternetControlMessage(invalid, packet, error),
            "oversized candidate list was accepted"
        )) return false;

    invalid = original;
    invalid.senderPeerId.assign(runtimeInternetMaximumIdentityBytes + 1, 'x');
    if (!require(
            !encodeRuntimeInternetControlMessage(invalid, packet, error),
            "oversized identity was accepted"
        )) return false;

    invalid = original;
    invalid.transactionId = 0;
    if (!require(
            !encodeRuntimeInternetControlMessage(invalid, packet, error),
            "zero correlation identity was accepted"
        )) return false;

    RuntimeInternetControlMessage invalidJoin;
    invalidJoin.type = RuntimeInternetControlType::JoinRoom;
    invalidJoin.transactionId = 7;
    invalidJoin.serviceId = "coord.prod.example";
    invalidJoin.roomId = "room-7";
    invalidJoin.senderPeerId = "attacker";
    invalidJoin.body = RuntimeInternetJoinRoomBody{
        RuntimeInternetRole::Host,
        "rules+map:0123456789abcdef",
        {candidate("203.0.113.7", 40000)}
    };
    if (!require(
        !encodeRuntimeInternetControlMessage(invalidJoin, packet, error),
        "host role was accepted on join path"
    )) {
        return false;
    }

    RuntimeInternetControlMessage seal;
    seal.type = RuntimeInternetControlType::SealRoom;
    seal.transactionId = 41U;
    seal.serviceId = "coord.prod.example";
    seal.roomId = "room-7";
    seal.senderPeerId = "host";
    seal.body = RuntimeInternetSealRoomBody{};
    if (!require(
            encodeRuntimeInternetControlMessage(seal, packet, error)
                && decodeRuntimeInternetControlMessage(
                    packet,
                    decoded,
                    error
                )
                && decoded == seal,
            "SealRoom v2 codec round trip failed"
        )) {
        return false;
    }
    RuntimeInternetControlMessage sealed = serviceMessage(
        RuntimeInternetControlType::RoomSealed,
        41U,
        "host"
    );
    sealed.body = RuntimeInternetRoomSealedBody{};
    return require(
        encodeRuntimeInternetControlMessage(sealed, packet, error)
            && decodeRuntimeInternetControlMessage(packet, decoded, error)
            && decoded == sealed,
        "RoomSealed v2 codec round trip failed"
    );
}

bool testResumeProtocol() {
    std::string error;
    RuntimeInternetControlMessage request;
    request.type = RuntimeInternetControlType::ResumeRoute;
    request.transactionId = 120;
    request.serviceId = "coord.prod.example";
    request.roomId = "room-7";
    request.senderPeerId = "client";
    request.targetPeerId = request.serviceId;
    request.body = RuntimeInternetResumeRouteBody{120, token()};
    RuntimeNetworkPacket packet;
    RuntimeInternetControlMessage decoded;
    if (!require(encodeRuntimeInternetControlMessage(request, packet, error)
        && decodeRuntimeInternetControlMessage(packet, decoded, error) && decoded == request,
        "resume request round trip: " + error)) return false;
    auto truncated = packet; truncated.payload.pop_back();
    if (!require(!decodeRuntimeInternetControlMessage(truncated, decoded, error),
        "truncated resume token accepted")) return false;
    auto oldVersion = packet; oldVersion.payload[5] = 2;
    if (!require(!decodeRuntimeInternetControlMessage(oldVersion, decoded, error),
        "pre-resume wire version accepted")) return false;
    request.transactionId = 121;
    if (!require(!encodeRuntimeInternetControlMessage(request, packet, error),
        "resume correlation must name the old route")) return false;
    auto offered = serviceMessage(RuntimeInternetControlType::PeerCandidates, 77, "client");
    offered.body = RuntimeInternetPeerCandidatesBody{121, "host", RuntimeInternetRole::Host,
        {candidate("127.0.0.1", 25000)}, token(0x70), 120};
    if (!require(encodeRuntimeInternetControlMessage(offered, packet, error)
        && decodeRuntimeInternetControlMessage(packet, decoded, error) && decoded == offered,
        "replacement route did not preserve its predecessor")) return false;
    std::get<RuntimeInternetPeerCandidatesBody>(offered.body).resumesConnectionId = 121;
    return require(!encodeRuntimeInternetControlMessage(offered, packet, error),
        "resume must rotate the service route identity");
}

bool testDirectory() {
    std::string error;
    RuntimeInternetConnection browser;
    auto browseConfig = config("browser", RuntimeInternetRole::Client, 90, 41000, "");
    browseConfig.localCandidates.clear();
    browseConfig.coordinatorHeartbeatMilliseconds = 1000U;
    if (!require(
            browser.beginRoomQuery(browseConfig, 0, error),
            "room query did not start: " + error
        )) return false;
    auto actions = browser.poll(0);
    if (!require(
            actions.size() == 1
                && actions.front().kind == RuntimeInternetActionKind::SendCoordinator,
            "room query did not emit one coordinator request"
        )) return false;
    if (!require(browser.poll(99).empty(), "room query retried too early")) return false;
    if (!require(
            browser.poll(100).size() == 1,
            "room query did not retry on schedule"
        )) return false;

    auto result = serviceMessage(
        RuntimeInternetControlType::RoomListResult,
        90,
        "browser",
        ""
    );
    result.body = RuntimeInternetRoomListResultBody{{room()}};
    if (!require(
            browser.receiveCoordinator(
                encode(result),
                "coord.prod.example",
                120,
                error
            )
                && browser.state() == RuntimeInternetConnectionState::RoomListReady
                && browser.rooms().size() == 1
                && browser.rooms().front() == room(),
            "room list response was not accepted: " + error
        )) return false;
    if (!require(
            browser.receiveCoordinator(
                encode(result),
                "coord.prod.example",
                121U,
                error
            )
                && browser.state()
                    == RuntimeInternetConnectionState::RoomListReady,
            "exact delayed room-list response was not idempotent"
        )) {
        return false;
    }
    auto changedResult = result;
    changedResult.body = RuntimeInternetRoomListResultBody{};
    if (!require(
            browser.receiveCoordinator(
                encode(changedResult),
                "coord.prod.example",
                122U,
                error
            )
                && browser.rooms().size() == 1U
                && browser.rooms().front() == room(),
            "a later dynamic room-list snapshot replaced the first accepted result"
        )) {
        return false;
    }
    if (!require(
            browser.poll(999U).empty()
                && browser.poll(1000U).size() == 1U,
            "ready directory did not emit a coordinator heartbeat"
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> heartbeatRetry = browser.poll(
        1100U
    );
    if (!require(
            heartbeatRetry.size() == 1U,
            "unacknowledged coordinator heartbeat did not retry"
        )) {
        return false;
    }
    RuntimeInternetControlMessage heartbeatRequest;
    if (!require(
            decodeRuntimeInternetControlMessage(
                heartbeatRetry.front().packet,
                heartbeatRequest,
                error
            )
                && heartbeatRequest.type
                    == RuntimeInternetControlType::CoordinatorHeartbeat,
            "heartbeat wire message was not canonical"
        )) {
        return false;
    }
    auto heartbeatAck = serviceMessage(
        RuntimeInternetControlType::CoordinatorHeartbeatAck,
        browseConfig.transactionId,
        "browser",
        ""
    );
    heartbeatAck.body = RuntimeInternetCoordinatorHeartbeatAckBody{
        std::get<RuntimeInternetCoordinatorHeartbeatBody>(
            heartbeatRequest.body
        ).sequence
    };
    if (!require(
            browser.receiveCoordinator(
                encode(heartbeatAck),
                "coord.prod.example",
                1110U,
                error
            )
                && browser.state()
                    == RuntimeInternetConnectionState::RoomListReady,
            "authenticated heartbeat acknowledgement changed directory state"
        )) {
        return false;
    }
    if (!require(
            browser.receiveCoordinator(
                encode(heartbeatAck),
                "coord.prod.example",
                1111U,
                error
            ),
            "exact delayed heartbeat acknowledgement was not idempotent"
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> secondHeartbeat = browser.poll(
        2110U
    );
    RuntimeInternetControlMessage secondHeartbeatRequest;
    if (!require(
            secondHeartbeat.size() == 1U
                && decodeRuntimeInternetControlMessage(
                    secondHeartbeat.front().packet,
                    secondHeartbeatRequest,
                    error
                ),
            "second sequenced heartbeat was not emitted"
        )) {
        return false;
    }
    if (!require(
            browser.receiveCoordinator(
                encode(heartbeatAck),
                "coord.prod.example",
                2111U,
                error
            )
                && browser.poll(2210U).size() == 1U,
            "old heartbeat acknowledgement confirmed a newer sequence"
        )) {
        return false;
    }
    const auto secondSequence = std::get<
        RuntimeInternetCoordinatorHeartbeatBody
    >(secondHeartbeatRequest.body).sequence;
    heartbeatAck.body = RuntimeInternetCoordinatorHeartbeatAckBody{
        secondSequence
    };
    if (!require(
            browser.receiveCoordinator(
                encode(heartbeatAck),
                "coord.prod.example",
                2211U,
                error
            ),
            "current heartbeat sequence acknowledgement was rejected"
        )) {
        return false;
    }

    RuntimeInternetConnection timeout;
    auto timeoutConfig = browseConfig;
    timeoutConfig.transactionId = 91;
    timeoutConfig.coordinatorTimeoutMilliseconds = 300;
    timeoutConfig.maximumRetries = 8;
    if (!require(
            timeout.beginRoomQuery(timeoutConfig, 0, error),
            "timeout query did not start"
        )) return false;
    timeout.poll(0);
    timeout.poll(300);
    return require(
        timeout.state() == RuntimeInternetConnectionState::Failed
            && !timeout.failureReason().empty(),
        "coordinator timeout did not fail explicitly"
    );
}

bool testDirectSpectatorConnection() {
    std::string error;
    RuntimeInternetConnection host;
    RuntimeInternetConnection spectator;
    auto hostConfig = config("host", RuntimeInternetRole::Host, 1001, 42000);
    auto spectatorConfig = config(
        "spectator",
        RuntimeInternetRole::Spectator,
        1002,
        43000
    );
    if (!require(
            host.beginHost(hostConfig, room(), 0, error)
                && spectator.beginJoin(spectatorConfig, 0, error),
            "host/spectator startup failed: " + error
        )) return false;
    if (!require(
            host.poll(0).size() == 1 && spectator.poll(0).size() == 1,
            "initial host/join requests were not emitted"
        )) return false;

    auto accepted = serviceMessage(
        RuntimeInternetControlType::RegisterAccepted,
        1001,
        "host"
    );
    accepted.body = RuntimeInternetRegisterAcceptedBody{4000};
    if (!require(
            !host.receiveCoordinator(encode(accepted), "evil.service", 10, error)
                && host.state() == RuntimeInternetConnectionState::RegisteringRoom,
            "unauthenticated coordinator response changed state"
        )) return false;
    if (!require(
            host.receiveCoordinator(
                encode(accepted),
                "coord.prod.example",
                10,
                error
            )
                && host.state() == RuntimeInternetConnectionState::WaitingForPeer,
            "host registration acknowledgement failed: " + error
        )) return false;
    if (!require(
            host.receiveCoordinator(
                encode(accepted),
                "coord.prod.example",
                11U,
                error
            ),
            "exact delayed RegisterAccepted was not idempotent"
        )) {
        return false;
    }
    auto changedAccepted = accepted;
    changedAccepted.body = RuntimeInternetRegisterAcceptedBody{5000U};
    if (!require(
            !host.receiveCoordinator(
                encode(changedAccepted),
                "coord.prod.example",
                12U,
                error
            )
                && host.state()
                    == RuntimeInternetConnectionState::WaitingForPeer,
            "changed delayed RegisterAccepted was accepted"
        )) {
        return false;
    }
    if (!require(
            host.requestRoomSeal(20U, error)
                && host.roomSealRequested()
                && !host.roomSealed()
                && host.state()
                    == RuntimeInternetConnectionState::WaitingForPeer,
            "host registration carrier did not stage a room seal: " + error
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> initialSeal = host.poll(20U);
    RuntimeInternetControlMessage sealRequest;
    if (!require(
            initialSeal.size() == 1U
                && initialSeal.front().kind
                    == RuntimeInternetActionKind::SendCoordinator
                && decodeRuntimeInternetControlMessage(
                    initialSeal.front().packet,
                    sealRequest,
                    error
                )
                && sealRequest.type == RuntimeInternetControlType::SealRoom
                && sealRequest.transactionId == hostConfig.transactionId
                && sealRequest.roomId == hostConfig.roomId,
            "room seal request was not exact"
        )) {
        return false;
    }
    if (!require(
            host.poll(119U).empty() && host.poll(120U).size() == 1U,
            "room seal did not retry independently"
        )) {
        return false;
    }
    auto sealed = serviceMessage(
        RuntimeInternetControlType::RoomSealed,
        hostConfig.transactionId,
        "host"
    );
    sealed.body = RuntimeInternetRoomSealedBody{};
    if (!require(
            host.receiveCoordinator(
                encode(sealed),
                "coord.prod.example",
                130U,
                error
            )
                && host.roomSealed()
                && host.state()
                    == RuntimeInternetConnectionState::WaitingForPeer,
            "RoomSealed acknowledgement changed or failed the carrier"
        )) {
        return false;
    }
    if (!require(
            host.receiveCoordinator(
                encode(sealed),
                "coord.prod.example",
                131U,
                error
            ),
            "exact delayed RoomSealed was not idempotent"
        )) {
        return false;
    }
    auto changedSealed = sealed;
    changedSealed.transactionId = hostConfig.transactionId + 1U;
    if (!require(
            !host.receiveCoordinator(
                encode(changedSealed),
                "coord.prod.example",
                132U,
                error
            ),
            "changed delayed RoomSealed was accepted"
        )) {
        return false;
    }
    if (!require(
            host.poll(2010).empty() && host.poll(2011).size() == 1,
            "host room lease did not refresh at half-life"
        )) return false;
    if (!require(
            host.receiveCoordinator(
                encode(accepted),
                "coord.prod.example",
                2012U,
                error
            )
                && host.roomSealed()
                && host.state()
                    == RuntimeInternetConnectionState::WaitingForPeer,
            "sealed host registration heartbeat reopened or failed the room"
        )) {
        return false;
    }

    const auto sharedToken = token();
    constexpr std::uint64_t connectionId = 0xabc001U;
    auto hostCandidates = serviceMessage(
        RuntimeInternetControlType::PeerCandidates,
        1001,
        "host"
    );
    hostCandidates.body = RuntimeInternetPeerCandidatesBody{
        connectionId,
        "spectator",
        RuntimeInternetRole::Spectator,
        spectatorConfig.localCandidates,
        sharedToken
    };
    auto spectatorCandidates = serviceMessage(
        RuntimeInternetControlType::PeerCandidates,
        1002,
        "spectator"
    );
    spectatorCandidates.body = RuntimeInternetPeerCandidatesBody{
        connectionId,
        "host",
        RuntimeInternetRole::Host,
        hostConfig.localCandidates,
        sharedToken
    };
    RuntimeInternetConnection hostRoute;
    if (!require(
            hostRoute.beginPeerRouteFromCoordinator(
                hostConfig,
                encode(hostCandidates),
                "coord.prod.example",
                2100,
                error
            )
                && host.state() == RuntimeInternetConnectionState::WaitingForPeer
                && spectator.receiveCoordinator(
                    encode(spectatorCandidates),
                    "coord.prod.example",
                    2100,
                    error
                )
                && hostRoute.state() == RuntimeInternetConnectionState::Punching
                && spectator.state() == RuntimeInternetConnectionState::Punching,
            "candidate exchange failed: " + error
        )) return false;
    if (!require(
            spectator.receiveCoordinator(
                encode(spectatorCandidates),
                "coord.prod.example",
                2101U,
                error
            )
                && spectator.state()
                    == RuntimeInternetConnectionState::Punching,
            "exact delayed PeerCandidates was not idempotent"
        )) {
        return false;
    }
    auto changedCandidates = spectatorCandidates;
    auto changedCandidatesBody =
        std::get<RuntimeInternetPeerCandidatesBody>(
            changedCandidates.body
        );
    changedCandidatesBody.connectionToken.front() ^= 1U;
    changedCandidates.body = changedCandidatesBody;
    if (!require(
            !spectator.receiveCoordinator(
                encode(changedCandidates),
                "coord.prod.example",
                2102U,
                error
            ),
            "changed delayed PeerCandidates was accepted"
        )) {
        return false;
    }

    auto secondOffer = hostCandidates;
    auto secondOfferBody = std::get<RuntimeInternetPeerCandidatesBody>(secondOffer.body);
    secondOfferBody.connectionId = connectionId + 1;
    secondOfferBody.remotePeerId = "client-two";
    secondOfferBody.remoteRole = RuntimeInternetRole::Client;
    secondOfferBody.connectionToken = token(0x55U);
    secondOffer.body = secondOfferBody;
    RuntimeInternetConnection secondHostRoute;
    if (!require(
            secondHostRoute.beginPeerRouteFromCoordinator(
                hostConfig,
                encode(secondOffer),
                "coord.prod.example",
                2100,
                error
            )
                && secondHostRoute.state() == RuntimeInternetConnectionState::Punching
                && secondHostRoute.remotePeerId() == "client-two"
                && host.state() == RuntimeInternetConnectionState::WaitingForPeer,
            "host could not create an independent second peer route: " + error
        )) return false;

    auto forgedCandidates = spectatorCandidates;
    auto forgedBody = std::get<RuntimeInternetPeerCandidatesBody>(forgedCandidates.body);
    forgedBody.remoteRole = RuntimeInternetRole::Client;
    forgedCandidates.body = forgedBody;
    RuntimeInternetConnection forgedTarget;
    auto forgedConfig = spectatorConfig;
    forgedConfig.transactionId = 2002;
    if (!require(forgedTarget.beginJoin(forgedConfig, 0, error), "forged target setup failed")) {
        return false;
    }
    forgedTarget.poll(0);
    forgedCandidates.transactionId = 2002;
    if (!require(
            !forgedTarget.receiveCoordinator(
                encode(forgedCandidates),
                "coord.prod.example",
                1,
                error
            )
                && forgedTarget.state() == RuntimeInternetConnectionState::JoiningRoom,
            "non-host remote role was accepted by spectator"
        )) return false;

    const auto hostPunches = hostRoute.poll(2100);
    if (!require(
            hostPunches.size() == 1
                && hostPunches.front().kind == RuntimeInternetActionKind::SendPeerUdp,
            "host did not emit NAT punch probe"
        )) return false;
    RuntimeNetworkPacket tampered = hostPunches.front().packet;
    RuntimeInternetControlMessage decoded;
    if (!require(
            decodeRuntimeInternetControlMessage(tampered, decoded, error),
            "test could not decode punch packet"
        )) return false;
    auto punchBody = std::get<RuntimeInternetPunchBody>(decoded.body);
    punchBody.connectionToken[0] ^= 1U;
    decoded.body = punchBody;
    tampered = encode(decoded);
    if (!require(
            !spectator.receivePeerUdp(
                tampered,
                hostConfig.localCandidates.front().endpoint,
                2110,
                error
            )
                && spectator.state() == RuntimeInternetConnectionState::Punching,
            "forged punch token changed connection state"
        )) return false;

    if (!require(
            spectator.receivePeerUdp(
                hostPunches.front().packet,
                hostConfig.localCandidates.front().endpoint,
                2110,
                error
            )
                && spectator.state()
                    == RuntimeInternetConnectionState::RequestingRelayTcp,
            "spectator did not convert the authenticated UDP probe into a TCP relay request: "
                + error
        )) return false;
    const auto ack = spectator.poll(2110);
    if (!require(
            hasAction(ack, RuntimeInternetActionKind::SendPeerUdp)
                && hasAction(ack, RuntimeInternetActionKind::SendCoordinator),
            "spectator did not preserve the punch ACK while requesting TCP relay"
        )) return false;
    const auto ackPacket = std::find_if(
        ack.begin(),
        ack.end(),
        [](const RuntimeInternetAction& action) {
            return action.kind == RuntimeInternetActionKind::SendPeerUdp;
        }
    );
    if (!require(
            ackPacket != ack.end()
                && hostRoute.receivePeerUdp(
                    ackPacket->packet,
                    spectatorConfig.localCandidates.front().endpoint,
                    2120,
                    error
                )
                && hostRoute.state()
                    == RuntimeInternetConnectionState::RequestingRelayTcp
                && hostRoute.remotePeerId() == "spectator",
            "host did not convert the punch acknowledgement into a TCP relay request: "
                + error
        )) return false;
    return require(
        spectator.role() == RuntimeInternetRole::Spectator,
        "spectator role was not retained"
    );
}

bool testRelayFallback() {
    std::string error;
    RuntimeInternetConnection client;
    auto clientConfig = config("client", RuntimeInternetRole::Client, 3001, 44000);
    clientConfig.coordinatorHeartbeatMilliseconds = 1000U;
    if (!require(client.beginJoin(clientConfig, 0, error), "relay client did not start")) {
        return false;
    }
    client.poll(0);
    auto candidates = serviceMessage(
        RuntimeInternetControlType::PeerCandidates,
        3001,
        "client"
    );
    constexpr std::uint64_t connectionId = 0xfeed55U;
    candidates.body = RuntimeInternetPeerCandidatesBody{
        connectionId,
        "host",
        RuntimeInternetRole::Host,
        {candidate("203.0.113.8", 45000)},
        token(0x20U)
    };
    if (!require(
            client.receiveCoordinator(
                encode(candidates),
                "coord.prod.example",
                10,
                error
            ),
            "relay test candidates failed: " + error
        )) return false;
    client.poll(10);
    const auto tcpRequest = client.poll(510);
    if (!require(
            client.state() == RuntimeInternetConnectionState::RequestingRelayTcp
                && hasAction(tcpRequest, RuntimeInternetActionKind::SendCoordinator),
            "punch timeout did not request the reliable TCP relay"
        )) return false;

    auto staleLease = serviceMessage(
        RuntimeInternetControlType::RelayLease,
        connectionId + 1,
        "client"
    );
    staleLease.body = RuntimeInternetRelayLeaseBody{
        connectionId,
        RuntimeInternetRelayTransport::Tcp,
        endpoint("relay.example", 46000),
        token(0x60U)
    };
    if (!require(
            !client.receiveCoordinator(
                encode(staleLease),
                "coord.prod.example",
                520,
                error
            )
                && client.state() == RuntimeInternetConnectionState::RequestingRelayTcp,
            "stale relay lease changed state"
        )) return false;

    auto tcpLease = serviceMessage(
        RuntimeInternetControlType::RelayLease,
        connectionId,
        "client"
    );
    tcpLease.body = RuntimeInternetRelayLeaseBody{
        connectionId,
        RuntimeInternetRelayTransport::Tcp,
        endpoint("relay.example", 47000),
        token(0x70U)
    };
    if (!require(
            client.receiveCoordinator(
                encode(tcpLease),
                "coord.prod.example",
                550,
                error
            ),
            "TCP relay lease failed: " + error
        )) return false;
    if (!require(
            client.receiveCoordinator(
                encode(tcpLease),
                "coord.prod.example",
                551U,
                error
            )
                && client.state()
                    == RuntimeInternetConnectionState::BindingRelayTcp,
            "exact delayed RelayLease was not idempotent"
        )) {
        return false;
    }
    auto changedLease = tcpLease;
    auto changedLeaseBody = std::get<RuntimeInternetRelayLeaseBody>(
        changedLease.body
    );
    changedLeaseBody.ticket.front() ^= 1U;
    changedLease.body = changedLeaseBody;
    if (!require(
            !client.receiveCoordinator(
                encode(changedLease),
                "coord.prod.example",
                552U,
                error
            ),
            "changed delayed RelayLease was accepted"
        )) {
        return false;
    }
    const auto tcpOpen = client.poll(553U);
    if (!require(
            client.state() == RuntimeInternetConnectionState::BindingRelayTcp
                && hasAction(tcpOpen, RuntimeInternetActionKind::OpenRelayTcp),
            "TCP relay lease did not open relay route"
        )) return false;

    auto bound = serviceMessage(
        RuntimeInternetControlType::RelayBound,
        connectionId,
        "client"
    );
    bound.body = RuntimeInternetRelayBoundBody{
        connectionId,
        RuntimeInternetRelayTransport::Tcp
    };
    if (!require(
            !client.receiveRelay(
                encode(bound),
                endpoint("relay.example", 47001),
                "coord.prod.example",
                560,
                error
            )
                && client.state() == RuntimeInternetConnectionState::BindingRelayTcp,
            "wrong relay endpoint changed state"
        )) return false;
    if (!require(
            client.receiveRelay(
            encode(bound),
            endpoint("relay.example", 47000),
            "coord.prod.example",
            560,
            error
        )
                && client.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound,
            "TCP relay acknowledgement did not bind the gameplay tunnel: "
                + error
        )) return false;
    if (!require(
            client.receiveRelay(
                encode(bound),
                endpoint("relay.example", 47000),
                "coord.prod.example",
                561U,
                error
            )
                && client.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound,
            "exact delayed RelayBound was not idempotent"
        )) {
        return false;
    }
    auto changedBound = bound;
    changedBound.transactionId = connectionId + 1U;
    if (!require(
            !client.receiveRelay(
                encode(changedBound),
                endpoint("relay.example", 47000),
                "coord.prod.example",
                562U,
                error
            ),
            "changed delayed RelayBound was accepted"
        )) {
        return false;
    }
    if (!require(
            !client.reportGameplaySessionJoined(connectionId + 1U, error)
                && client.state()
                    == RuntimeInternetConnectionState::TcpTunnelBound,
            "mismatched gameplay admission completed the Internet connection"
        )) return false;
    if (!require(
            client.reportGameplaySessionJoined(connectionId, error)
                && client.state()
                    == RuntimeInternetConnectionState::GameplaySessionConnected,
            "canonical JoinAccepted did not complete the Internet connection: "
                + error
        )) {
        return false;
    }
    if (!require(
            client.reportGameplaySessionJoined(connectionId, error)
                && client.state()
                    == RuntimeInternetConnectionState::GameplaySessionConnected,
            "duplicate canonical gameplay admission was not idempotent"
        )) {
        return false;
    }
    const std::vector<RuntimeInternetAction> gameplayHeartbeat = client.poll(
        1000U
    );
    RuntimeInternetControlMessage heartbeat;
    if (!require(
            gameplayHeartbeat.size() == 1U
                && decodeRuntimeInternetControlMessage(
                    gameplayHeartbeat.front().packet,
                    heartbeat,
                    error
                )
                && heartbeat.type
                    == RuntimeInternetControlType::CoordinatorHeartbeat,
            "GameplaySessionConnected did not keep its coordinator leg alive"
        )) {
        return false;
    }
    auto ack = serviceMessage(
        RuntimeInternetControlType::CoordinatorHeartbeatAck,
        clientConfig.transactionId,
        "client"
    );
    ack.body = RuntimeInternetCoordinatorHeartbeatAckBody{
        std::get<RuntimeInternetCoordinatorHeartbeatBody>(heartbeat.body)
            .sequence
    };
    return require(
        client.receiveCoordinator(
            encode(ack),
            "coord.prod.example",
            1010U,
            error
        )
            && client.state()
                == RuntimeInternetConnectionState::GameplaySessionConnected,
        "gameplay heartbeat acknowledgement changed connected state"
    );
}

} // namespace

int main() {
    if (!testCodec()
        || !testResumeProtocol()
        || !testDirectory()
        || !testDirectSpectatorConnection()
        || !testRelayFallback()) {
        return 1;
    }
    std::cout
        << "Runtime Internet connectivity smoke passed: "
        << "directory=strict roles=host/client/spectator "
        << "nat=authenticated relay=udp-to-tcp timeouts=bounded\n";
    return 0;
}
