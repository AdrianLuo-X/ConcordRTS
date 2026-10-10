#include "network/internet/RuntimeInternetMatchmaking.h"
#include "network/transport/RuntimeInternetMatchmakingProtocol.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "Runtime Internet matchmaking smoke failure: "
              << message << '\n';
    return false;
}

RuntimeInternetMatchmakingRequest request(
    std::uint64_t requestId,
    std::string fingerprint,
    std::int32_t rating,
    std::uint8_t playerCount = 2U,
    std::string region = "cn-east"
) {
    RuntimeInternetMatchmakingRequest value;
    value.requestId = requestId;
    value.queueId = "quick-battle";
    value.regionId = std::move(region);
    value.contentFingerprint = std::move(fingerprint);
    value.displayName = "Commander";
    value.playerCount = playerCount;
    value.skillRating = rating;
    return value;
}

const RuntimeInternetMatchmakingAction* actionFor(
    const std::vector<RuntimeInternetMatchmakingAction>& actions,
    const std::string& peerId
) {
    const auto found = std::find_if(
        actions.begin(),
        actions.end(),
        [&](const RuntimeInternetMatchmakingAction& action) {
            return action.targetPeerId == peerId;
        }
    );
    return found == actions.end() ? nullptr : &*found;
}

RuntimeNetworkPacket wirePacket(
    RuntimeInternetMatchmakingMessageType type,
    std::uint64_t transactionId,
    const std::string& peerId,
    RuntimeInternetMatchmakingMessageBody body
) {
    RuntimeInternetMatchmakingMessage message;
    message.type = type;
    message.transactionId = transactionId;
    message.serviceId = "concord-self-hosted";
    message.senderPeerId = peerId;
    message.body = std::move(body);
    RuntimeNetworkPacket packet;
    std::string error;
    if (!encodeRuntimeInternetMatchmakingMessage(message, packet, error)) {
        std::cerr << "could not encode smoke packet: " << error << '\n';
        return {};
    }
    return packet;
}

RuntimeInternetMatchmakingMessage decodeServiceAction(
    const RuntimeInternetMatchmakingServiceAction& action,
    bool& succeeded
) {
    RuntimeInternetMatchmakingMessage message;
    std::string error;
    succeeded = decodeRuntimeInternetMatchmakingMessage(
        action.packet,
        message,
        error
    );
    if (!succeeded) {
        std::cerr << "could not decode service action: " << error << '\n';
    }
    return message;
}

} // namespace

int main() {
    std::uint64_t nowMilliseconds = 1000U;
    std::uint8_t randomByte = 1U;
    bool randomAvailable = true;

    RuntimeInternetMatchmakingConfig config;
    config.serviceId = "concord-self-hosted";
    config.queueLeaseMilliseconds = 10000U;
    config.matchLeaseMilliseconds = 20000U;
    config.maximumQueuedPeers = 16U;
    config.maximumActiveMatches = 4U;
    config.admissionProofBytes = 24U;
    config.baseSkillWindow = 100;
    config.skillWindowGrowthPerSecond = 25;
    config.maximumSkillWindow = 500;
    config.nowMilliseconds = [&]() { return nowMilliseconds; };
    config.secureRandomBytes = [&](std::uint8_t* output, std::size_t size) {
        if (!randomAvailable) return false;
        for (std::size_t index = 0U; index < size; ++index) {
            output[index] = randomByte++;
        }
        return true;
    };

    RuntimeInternetMatchmaking matchmaking;
    std::string error;
    if (!require(matchmaking.open(config, error), error)
        || !require(matchmaking.isOpen(), "service did not open")) {
        return 1;
    }

    const RuntimeInternetMatchmakingRequest aliceRequest =
        request(100U, "content-a", 1000);
    const RuntimeInternetMatchmakingRequest bobRequest =
        request(100U, "content-a", 1170);
    if (!require(
            !matchmaking.enqueue(
                "wrong-service", "alice", aliceRequest
            ).accepted,
            "wrong authenticated service was accepted"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId, "alice", aliceRequest
            ).accepted,
            "alice enqueue failed"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId, "alice", aliceRequest
            ).duplicate,
            "exact queued replay was not idempotent"
        )
        || !require(
            !matchmaking.enqueue(
                config.serviceId,
                "alice",
                request(2U, "content-a", 1000)
            ).accepted,
            "one peer entered the queue twice"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId, "bob", bobRequest
            ).accepted,
            "bob enqueue failed"
        )) {
        return 1;
    }

    std::vector<RuntimeInternetMatchmakingAction> actions =
        matchmaking.poll(error);
    if (!require(error.empty(), error)
        || !require(
            actions.empty(),
            "skill-incompatible peers matched before the wait window expanded"
        )) {
        return 1;
    }

    nowMilliseconds += 3000U;
    actions = matchmaking.poll(error);
    const RuntimeInternetMatchmakingAction* alice = actionFor(actions, "alice");
    if (!require(error.empty(), error)
        || !require(actions.size() == 1U, "host assignment was not staged")
        || !require(alice != nullptr,
            "oldest peer did not receive the host assignment")
        || !require(
            alice->assignment.hostPeerId == "alice",
            "oldest queued peer was not selected as host"
        )
        || !require(
            alice->assignment.hostDisplayName == "Commander",
            "host display metadata was not preserved"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId, "alice", aliceRequest
            ).duplicate,
            "assigned request replay was not idempotent"
        )) {
        return 1;
    }

    const std::string firstMatchId = alice->assignment.matchId;
    const std::string firstRoomId = alice->assignment.roomId;
    const std::vector<std::uint8_t> firstProof =
        alice->assignment.admissionProof;
    std::vector<RuntimeInternetMatchmakingAction> activatedActions;
    if (!require(
            !matchmaking.activateMatch(
                config.serviceId,
                "bob",
                firstMatchId,
                activatedActions
            ).accepted,
            "non-host activated a staged match"
        )
        || !require(activatedActions.empty(),
            "failed activation leaked a remote assignment")
        || !require(
            !matchmaking.authorizeAdmission(
                firstRoomId,
                "alice",
                firstProof
            ),
            "admission proof became valid before host activation"
        )
        || !require(
            matchmaking.activateMatch(
                config.serviceId,
                "alice",
                firstMatchId,
                activatedActions
            ).accepted,
            "assigned host could not activate the match"
        )
        || !require(
            activatedActions.size() == 1U
                && activatedActions.front().targetPeerId == "bob",
            "activation did not release the remote assignment"
        )) {
        return 1;
    }
    const RuntimeInternetMatchmakingAction bobAction =
        activatedActions.front();
    const RuntimeInternetMatchmakingAssignment& bobAssignment =
        bobAction.assignment;
    if (!require(
            bobAssignment.roomId == alice->assignment.roomId
                && bobAssignment.matchId == alice->assignment.matchId
                && bobAssignment.hostDisplayName == "Commander"
                && bobAssignment.peerIds
                    == std::vector<std::string>({"alice", "bob"}),
            "pair did not receive one canonical assignment"
        )
        || !require(
            alice->assignment.admissionProof
                != bobAssignment.admissionProof,
            "admission proofs were not peer-specific"
        )
        || !require(
            matchmaking.authorizeAdmission(
                bobAssignment.roomId,
                "bob",
                bobAssignment.admissionProof
            ),
            "activated proof was rejected"
        )
        || !require(
            !matchmaking.authorizeAdmission(
                bobAssignment.roomId,
                "alice",
                bobAssignment.admissionProof
            ),
            "proof was usable by another authenticated peer"
        )) {
        return 1;
    }
    if (!require(
            !matchmaking.closeMatch(
                config.serviceId, "bob", firstMatchId
            ).accepted,
            "non-host closed a match"
        )
        || !require(
            matchmaking.closeMatch(
                config.serviceId, "alice", firstMatchId
            ).accepted,
            "assigned host could not close the match"
        )
        || !require(
            !matchmaking.authorizeAdmission(
                firstRoomId, "alice", firstProof
            ),
            "closed match proof remained valid"
        )) {
        return 1;
    }

    if (!require(
            matchmaking.enqueue(
                config.serviceId,
                "alice",
                request(1U, "content-a", 1000)
            ).accepted,
            "smaller fresh opaque request id was rejected after match close"
        )
        || !require(
            matchmaking.cancel(config.serviceId, "alice", 1U).accepted,
            "queued request cancellation failed"
        )
        || !require(
            !matchmaking.heartbeat(config.serviceId, "alice", 1U).accepted,
            "cancelled request accepted a heartbeat"
        )) {
        return 1;
    }

    // Exact compatibility criteria keep different content and regions apart.
    if (!require(
            matchmaking.enqueue(
                config.serviceId,
                "carol",
                request(1U, "content-a", 900)
            ).accepted,
            "carol enqueue failed"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId,
                "dave",
                request(1U, "content-b", 900)
            ).accepted,
            "dave enqueue failed"
        )
        || !require(
            matchmaking.enqueue(
                config.serviceId,
                "erin",
                request(1U, "content-a", 900, 2U, "eu-west")
            ).accepted,
            "erin enqueue failed"
        )
        || !require(
            matchmaking.poll(error).empty(),
            "incompatible queue criteria were matched"
        )) {
        return 1;
    }

    // Heartbeat renews the lease without changing FIFO age.
    nowMilliseconds += 9000U;
    if (!require(
            matchmaking.heartbeat(config.serviceId, "carol", 1U).accepted,
            "queue heartbeat failed"
        )) {
        return 1;
    }
    nowMilliseconds += 2000U;
    actions = matchmaking.poll(error);
    if (!require(error.empty(), error)
        || !require(
            matchmaking.queuedPeerCount() == 1U,
            "queue expiry did not retain only the renewed peer"
        )) {
        return 1;
    }

    if (!require(
            matchmaking.enqueue(
                config.serviceId,
                "frank",
                request(1U, "content-a", 900)
            ).accepted,
            "frank enqueue failed"
        )) {
        return 1;
    }
    randomAvailable = false;
    actions = matchmaking.poll(error);
    if (!require(actions.empty(), "random failure emitted a partial match")
        || !require(!error.empty(), "random failure was not reported")
        || !require(
            matchmaking.queuedPeerCount() == 2U
                && matchmaking.activeMatchCount() == 0U,
            "random failure partially committed queue state"
        )) {
        return 1;
    }
    randomAvailable = true;
    actions = matchmaking.poll(error);
    if (!require(error.empty(), error)
        || !require(actions.size() == 1U,
            "queue did not emit a staged host after random source resumed")
        || !require(matchmaking.activeMatchCount() == 1U,
            "recovered match was not retained")) {
        return 1;
    }

    nowMilliseconds += 20000U;
    actions = matchmaking.poll(error);
    if (!require(error.empty(), error)
        || !require(
            matchmaking.activeMatchCount() == 0U,
            "expired match reservation was not pruned"
        )) {
        return 1;
    }

    if (!require(
            matchmaking.enqueue(
                config.serviceId,
                "disconnect-queued",
                request(1U, "disconnect-queue", 1000, 3U)
            ).accepted
                && matchmaking.queuedPeerCount() == 1U,
            "disconnect queue fixture was not staged"
        )
        || !require(
            matchmaking.authenticatedPeerDisconnected(
                config.serviceId,
                "disconnect-queued"
            ).accepted
                && matchmaking.queuedPeerCount() == 0U,
            "coordinator disconnect did not reclaim queued peer"
        )) {
        return 1;
    }
    if (!require(
            matchmaking.enqueue(
                config.serviceId,
                "disconnect-host",
                request(1U, "disconnect-match", 1000)
            ).accepted
                && matchmaking.enqueue(
                    config.serviceId,
                    "disconnect-remote",
                    request(1U, "disconnect-match", 1010)
                ).accepted,
            "disconnect match fixture did not enqueue"
        )) {
        return 1;
    }
    actions = matchmaking.poll(error);
    const RuntimeInternetMatchmakingAction* disconnectHost = actionFor(
        actions,
        "disconnect-host"
    );
    if (!require(
            disconnectHost != nullptr
                && matchmaking.activateMatch(
                    config.serviceId,
                    "disconnect-host",
                    disconnectHost->assignment.matchId,
                    activatedActions
                ).accepted
                && activatedActions.size() == 1U,
            "disconnect match fixture did not activate"
        )) {
        return 1;
    }
    const RuntimeInternetMatchmakingAssignment disconnectHostAssignment =
        disconnectHost->assignment;
    const RuntimeInternetMatchmakingAssignment disconnectRemoteAssignment =
        activatedActions.front().assignment;
    if (!require(
            matchmaking.authorizeAdmission(
                disconnectRemoteAssignment.roomId,
                "disconnect-remote",
                disconnectRemoteAssignment.admissionProof
            ),
            "disconnect proof fixture was not active"
        )
        || !require(
            matchmaking.authenticatedPeerDisconnected(
                config.serviceId,
                "disconnect-remote"
            ).accepted
                && matchmaking.activeMatchCount() == 0U
                && !matchmaking.authorizeAdmission(
                    disconnectHostAssignment.roomId,
                    "disconnect-host",
                    disconnectHostAssignment.admissionProof
                )
                && !matchmaking.authorizeAdmission(
                    disconnectRemoteAssignment.roomId,
                    "disconnect-remote",
                    disconnectRemoteAssignment.admissionProof
                ),
            "peer disconnect did not revoke the full match and proofs"
        )) {
        return 1;
    }

    // The production-facing wire adapter binds every claimed sender to the
    // identity supplied by the authenticated outer service channel.
    nowMilliseconds = 50000U;
    RuntimeInternetMatchmakingService service;
    if (!require(service.open(config, error), error)) return 1;
    RuntimeInternetMatchmakingRequest wireAlice =
        request(10U, "content-wire", 1000);
    RuntimeInternetMatchmakingRequest wireBob =
        request(10U, "content-wire", 1050);
    RuntimeInternetMatchmakingServiceEnvelope envelope;
    envelope.authenticatedServiceId = config.serviceId;
    envelope.authenticatedPeerId = "mallory";
    envelope.packet = wirePacket(
        RuntimeInternetMatchmakingMessageType::Enqueue,
        wireAlice.requestId,
        "wire-alice",
        wireAlice
    );
    std::vector<RuntimeInternetMatchmakingServiceAction> wireActions;
    if (!require(
            !service.receive(envelope, wireActions, error),
            "spoofed wire sender identity was accepted"
        )
        || !require(
            service.queuedPeerCount() == 0U,
            "spoofed wire request changed queue state"
        )) {
        return 1;
    }

    envelope.authenticatedPeerId = "wire-alice";
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(wireActions.size() == 1U,
            "enqueue acknowledgement was not emitted")) {
        return 1;
    }
    bool decoded = false;
    RuntimeInternetMatchmakingMessage response =
        decodeServiceAction(wireActions.front(), decoded);
    if (!require(decoded, "enqueue acknowledgement did not decode")
        || !require(
            response.type
                == RuntimeInternetMatchmakingMessageType::OperationAck
                && response.senderPeerId == config.serviceId
                && response.targetPeerId == "wire-alice",
            "enqueue acknowledgement direction was invalid"
        )) {
        return 1;
    }

    envelope.authenticatedPeerId = "wire-bob";
    envelope.packet = wirePacket(
        RuntimeInternetMatchmakingMessageType::Enqueue,
        wireBob.requestId,
        "wire-bob",
        wireBob
    );
    if (!require(service.receive(envelope, wireActions, error), error)) {
        return 1;
    }
    wireActions = service.poll(error);
    if (!require(error.empty(), error)
        || !require(wireActions.size() == 1U,
            "wire service did not stage the host assignment")) {
        return 1;
    }

    RuntimeInternetMatchmakingAssignment wireAliceAssignment;
    RuntimeInternetMatchmakingAssignment wireBobAssignment;
    for (const RuntimeInternetMatchmakingServiceAction& action : wireActions) {
        response = decodeServiceAction(action, decoded);
        if (!require(decoded, "wire assignment did not decode")
            || !require(
                response.type
                    == RuntimeInternetMatchmakingMessageType::Assignment,
                "wire service emitted a non-assignment"
            )) {
            return 1;
        }
        const auto assignment =
            std::get<RuntimeInternetMatchmakingAssignment>(response.body);
        wireAliceAssignment = assignment;
    }
    if (!require(
            wireAliceAssignment.peerId == "wire-alice",
            "wire host assignment did not preserve target identity"
        )) {
        return 1;
    }

    envelope.authenticatedPeerId = "wire-alice";
    envelope.packet = wirePacket(
        RuntimeInternetMatchmakingMessageType::ActivateMatch,
        12U,
        "wire-alice",
        RuntimeInternetMatchmakingCloseBody{
            wireAliceAssignment.matchId
        }
    );
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(wireActions.size() == 2U,
            "activation did not return ack plus remote assignment")) {
        return 1;
    }
    for (const RuntimeInternetMatchmakingServiceAction& action : wireActions) {
        response = decodeServiceAction(action, decoded);
        if (!require(decoded, "activation response did not decode")) return 1;
        if (response.type
            == RuntimeInternetMatchmakingMessageType::Assignment) {
            wireBobAssignment =
                std::get<RuntimeInternetMatchmakingAssignment>(
                    response.body
                );
        }
    }
    if (!require(
            wireBobAssignment.peerId == "wire-bob"
                && wireAliceAssignment.roomId
                    == wireBobAssignment.roomId,
            "activation did not release the remote wire assignment"
        )
        || !require(
            service.authorizeAdmission(
                wireBobAssignment.roomId,
                "wire-bob",
                wireBobAssignment.admissionProof
            ),
            "wire assignment proof was not connected to service authority"
        )) {
        return 1;
    }

    envelope.authenticatedPeerId = "wire-bob";
    envelope.packet = wirePacket(
        RuntimeInternetMatchmakingMessageType::CloseMatch,
        11U,
        "wire-bob",
        RuntimeInternetMatchmakingCloseBody{
            wireBobAssignment.matchId
        }
    );
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(wireActions.size() == 1U,
            "non-host close rejection was not returned")) {
        return 1;
    }
    response = decodeServiceAction(wireActions.front(), decoded);
    if (!require(
            decoded
                && response.type
                    == RuntimeInternetMatchmakingMessageType::Error,
            "non-host close did not produce a canonical error"
        )) {
        return 1;
    }

    envelope.authenticatedPeerId = "wire-alice";
    envelope.packet = wirePacket(
        RuntimeInternetMatchmakingMessageType::CloseMatch,
        11U,
        "wire-alice",
        RuntimeInternetMatchmakingCloseBody{
            wireAliceAssignment.matchId
        }
    );
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(
            !service.authorizeAdmission(
                wireAliceAssignment.roomId,
                "wire-alice",
                wireAliceAssignment.admissionProof
            ),
            "wire host close left admission proof active"
        )) {
        return 1;
    }

    RuntimeNetworkPacket corrupt = envelope.packet;
    corrupt.payload.push_back(0U);
    envelope.packet = std::move(corrupt);
    if (!require(
            !service.receive(envelope, wireActions, error),
            "trailing wire bytes were accepted"
        )) {
        return 1;
    }
    service.close();

    // The client state machine shares this packet channel, renews its lease,
    // and accepts an assignment only from the authenticated service.
    nowMilliseconds = 80000U;
    if (!require(service.open(config, error), error)) return 1;
    RuntimeInternetMatchmakingClientConfig clientConfig;
    clientConfig.serviceId = config.serviceId;
    clientConfig.heartbeatMilliseconds = 2000U;
    RuntimeInternetMatchmakingClient clientAlice;
    RuntimeInternetMatchmakingClient clientBob;
    RuntimeNetworkPacket clientPacket;
    clientConfig.localPeerId = "client-alice";
    if (!require(
            clientAlice.begin(
                clientConfig,
                request(20U, "client-content", 1000),
                nowMilliseconds,
                clientPacket,
                error
            ),
            error
        )) {
        return 1;
    }
    envelope.authenticatedServiceId = config.serviceId;
    envelope.authenticatedPeerId = clientConfig.localPeerId;
    envelope.packet = clientPacket;
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(
            clientAlice.receive(
                wireActions.front().packet,
                config.serviceId,
                nowMilliseconds,
                error
            ),
            error
        )
        || !require(
            clientAlice.state()
                == RuntimeInternetMatchmakingClientState::Queued,
            "client did not enter queued state"
        )) {
        return 1;
    }

    bool hasHeartbeat = false;
    if (!require(
            clientAlice.poll(
                nowMilliseconds + 1000U,
                clientPacket,
                hasHeartbeat,
                error
            ),
            error
        )
        || !require(!hasHeartbeat, "client heartbeat was sent too early")
        || !require(
            clientAlice.poll(
                nowMilliseconds + 2000U,
                clientPacket,
                hasHeartbeat,
                error
            ),
            error
        )
        || !require(hasHeartbeat, "client heartbeat was not scheduled")) {
        return 1;
    }
    nowMilliseconds += 2000U;
    const RuntimeNetworkPacket inFlightAliceHeartbeat = clientPacket;
    envelope.packet = inFlightAliceHeartbeat;
    if (!require(service.receive(envelope, wireActions, error), error)) {
        return 1;
    }
    const RuntimeNetworkPacket delayedAliceHeartbeatAck =
        wireActions.front().packet;

    clientConfig.localPeerId = "client-bob";
    if (!require(
            clientBob.begin(
                clientConfig,
                request(20U, "client-content", 1030),
                nowMilliseconds,
                clientPacket,
                error
            ),
            error
        )) {
        return 1;
    }
    envelope.authenticatedPeerId = clientConfig.localPeerId;
    envelope.packet = clientPacket;
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(
            clientBob.receive(
                wireActions.front().packet,
                config.serviceId,
                nowMilliseconds,
                error
            ),
            error
        )) {
        return 1;
    }

    wireActions = service.poll(error);
    if (!require(error.empty(), error)
        || !require(wireActions.size() == 1U,
            "client state machine did not stage its host")) {
        return 1;
    }
    const RuntimeInternetMatchmakingServiceAction stagedAliceAssignment =
        wireActions.front();
    envelope.authenticatedPeerId = "client-alice";
    envelope.packet = inFlightAliceHeartbeat;
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(
            wireActions.size() == 1U,
            "late heartbeat did not receive one authoritative response"
        )) {
        return 1;
    }
    const RuntimeNetworkPacket delayedAliceHeartbeatError =
        wireActions.front().packet;
    if (!require(
            clientAlice.receive(
                stagedAliceAssignment.packet,
                config.serviceId,
                nowMilliseconds,
                error
            ),
            error
        )
        || !require(
            clientAlice.receive(
                delayedAliceHeartbeatAck,
                config.serviceId,
                nowMilliseconds + 1U,
                error
            )
                && clientAlice.state()
                    == RuntimeInternetMatchmakingClientState::Assigned,
            "delayed heartbeat acknowledgement invalidated assignment"
        )
        || !require(
            clientAlice.receive(
                delayedAliceHeartbeatError,
                config.serviceId,
                nowMilliseconds + 2U,
                error
            )
                && clientAlice.state()
                    == RuntimeInternetMatchmakingClientState::Assigned,
            "delayed heartbeat error invalidated assignment"
        )
        || !require(
            clientAlice.requestActivateMatch(22U, clientPacket, error),
            error
        )) {
        return 1;
    }
    envelope.authenticatedPeerId = "client-alice";
    envelope.packet = clientPacket;
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(wireActions.size() == 2U,
            "client activation did not return ack plus remote assignment")) {
        return 1;
    }
    for (const RuntimeInternetMatchmakingServiceAction& action : wireActions) {
        RuntimeInternetMatchmakingClient* target =
            action.targetPeerId == "client-alice"
            ? &clientAlice
            : &clientBob;
        if (!require(
                target->receive(
                    action.packet,
                    config.serviceId,
                    nowMilliseconds,
                    error
                ),
                error
            )) {
            return 1;
        }
    }
    if (!require(
            clientAlice.state()
                == RuntimeInternetMatchmakingClientState::Assigned
                && clientBob.state()
                    == RuntimeInternetMatchmakingClientState::Assigned,
            "both clients did not accept their assignment"
        )
        || !require(
            !clientBob.requestCloseMatch(21U, clientPacket, error),
            "non-host client constructed a close request"
        )
        || !require(
            clientAlice.requestCloseMatch(21U, clientPacket, error),
            error
        )) {
        return 1;
    }
    envelope.authenticatedPeerId = "client-alice";
    envelope.packet = clientPacket;
    if (!require(service.receive(envelope, wireActions, error), error)
        || !require(
            clientAlice.receive(
                wireActions.front().packet,
                config.serviceId,
                nowMilliseconds,
                error
            ),
            error
        )
        || !require(
            clientAlice.state()
                == RuntimeInternetMatchmakingClientState::Closed,
            "host client did not close after acknowledgement"
        )) {
        return 1;
    }
    service.close();

    matchmaking.close();
    if (!require(!matchmaking.isOpen(), "service did not close")) return 1;
    std::cout << "Runtime Internet matchmaking smoke passed\n";
    return 0;
}
