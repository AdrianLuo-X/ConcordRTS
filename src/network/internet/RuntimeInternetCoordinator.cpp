#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/lobby/RuntimeRoomPassword.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace {

bool validText(
    const std::string& value,
    std::size_t maximum,
    bool allowEmpty = false
) {
    if ((!allowEmpty && value.empty()) || value.size() > maximum) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x20U && ch != 0x7fU;
    });
}

bool validEndpoint(const RuntimeInternetEndpoint& endpoint) {
    return endpoint.port != 0
        && validText(
            endpoint.address,
            runtimeInternetMaximumEndpointAddressBytes
        );
}

bool validRelay(RuntimeInternetRelayTransport transport) {
    return transport == RuntimeInternetRelayTransport::Udp
        || transport == RuntimeInternetRelayTransport::Tcp;
}

bool packetEquals(
    const RuntimeNetworkPacket& left,
    const RuntimeNetworkPacket& right
) {
    return left.type == right.type && left.payload == right.payload;
}

std::uint64_t saturatingAdd(
    std::uint64_t value,
    std::uint64_t increment
) {
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value + increment;
}

std::size_t relayIndex(RuntimeInternetRelayTransport transport) {
    return transport == RuntimeInternetRelayTransport::Udp ? 0U : 1U;
}

RuntimeInternetRelayTransport relayTransport(std::size_t index) {
    return index == 0U
        ? RuntimeInternetRelayTransport::Udp
        : RuntimeInternetRelayTransport::Tcp;
}

RuntimeInternetEndpoint relayEndpoint(
    const RuntimeInternetCoordinatorConfig& config,
    RuntimeInternetRelayTransport transport
) {
    return transport == RuntimeInternetRelayTransport::Udp
        ? config.udpRelayEndpoint
        : config.tcpRelayEndpoint;
}

bool allZero(const std::vector<std::uint8_t>& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) {
        return value == 0U;
    });
}

} // namespace

struct RuntimeInternetCoordinator::Impl {
    struct RelayState {
        std::vector<std::uint8_t> hostTicket;
        std::vector<std::uint8_t> remoteTicket;
        bool hostBound = false;
        bool remoteBound = false;
        bool active = false;
        std::uint64_t expiresAtMilliseconds = 0;

        bool hasLease() const {
            return !hostTicket.empty() || !remoteTicket.empty();
        }
    };

    struct Connection {
        std::uint64_t connectionId = 0;
        std::string roomId;
        std::string hostPeerId;
        std::string remotePeerId;
        RuntimeInternetRole remoteRole = RuntimeInternetRole::Client;
        std::uint64_t hostTransactionId = 0;
        std::uint64_t remoteTransactionId = 0;
        std::vector<RuntimeInternetCandidate> hostCandidates;
        std::vector<RuntimeInternetCandidate> remoteCandidates;
        std::vector<std::uint8_t> connectionToken;
        std::vector<std::uint8_t> admissionProof;
        RuntimeNetworkPacket joinPacket;
        std::uint64_t expiresAtMilliseconds = 0;
        bool tcpRelayRequired = false;
        bool gameplayRouteActivated = false;
        std::uint64_t resumesConnectionId = 0;
        std::uint64_t reconnectDeadlineMilliseconds = 0;
        std::array<RelayState, 2> relay;

        ~Connection() {
            runtimeRoomPasswordSecureClear(admissionProof);
        }
    };

    struct Room {
        RuntimeInternetRoomRecord record;
        std::vector<RuntimeInternetCandidate> hostCandidates;
        RuntimeRoomPasswordVerifier passwordVerifier{};
        bool hasPasswordVerifier = false;
        std::uint64_t registerTransactionId = 0;
        RuntimeNetworkPacket registerPacket;
        std::uint64_t expiresAtMilliseconds = 0;
        bool sealed = false;
        bool reconnectLeaseHeld = false;

        ~Room() {
            runtimeRoomPasswordSecureClear(passwordVerifier);
        }
    };

    struct ReplayTombstone {
        std::string peerId;
        std::uint64_t transactionId = 0;
        RuntimeInternetControlType type = RuntimeInternetControlType::Error;
        std::uint64_t expiresAtMilliseconds = 0;
    };

    struct ResumeAlias {
        std::uint64_t replacementId = 0;
        std::uint64_t expiresAtMilliseconds = 0;
        std::string roomId;
        std::string hostPeerId;
        std::string remotePeerId;
        std::vector<std::uint8_t> previousToken;
        ~ResumeAlias() { runtimeRoomPasswordSecureClear(previousToken); }
    };

    RuntimeInternetCoordinatorConfig config;
    bool opened = false;
    std::uint64_t lastNowMilliseconds = 0;
    std::uint64_t nextConnectionId = 1;
    std::map<std::string, Room> rooms;
    std::map<std::uint64_t, Connection> connections;
    std::vector<ReplayTombstone> tombstones;
    std::map<std::uint64_t, ResumeAlias> resumeAliases;

    std::uint64_t now() {
        const std::uint64_t observed = config.nowMilliseconds();
        lastNowMilliseconds = std::max(lastNowMilliseconds, observed);
        return lastNowMilliseconds;
    }

    bool isTombstoned(
        const std::string& peerId,
        std::uint64_t transactionId,
        RuntimeInternetControlType type
    ) const {
        return std::any_of(
            tombstones.begin(),
            tombstones.end(),
            [&](const ReplayTombstone& tombstone) {
                return tombstone.peerId == peerId
                    && tombstone.transactionId == transactionId
                    && tombstone.type == type;
            }
        );
    }

    void addTombstone(
        std::string peerId,
        std::uint64_t transactionId,
        RuntimeInternetControlType type,
        std::uint64_t nowMilliseconds
    ) {
        const auto existing = std::find_if(
            tombstones.begin(),
            tombstones.end(),
            [&](const ReplayTombstone& tombstone) {
                return tombstone.peerId == peerId
                    && tombstone.transactionId == transactionId
                    && tombstone.type == type;
            }
        );
        const std::uint64_t expiry = saturatingAdd(
            nowMilliseconds,
            config.replayRetentionMilliseconds
        );
        if (existing != tombstones.end()) {
            existing->expiresAtMilliseconds = std::max(
                existing->expiresAtMilliseconds,
                expiry
            );
            return;
        }
        tombstones.push_back({
            std::move(peerId),
            transactionId,
            type,
            expiry
        });
    }

    std::size_t roomCountForPeer(const std::string& peerId) const {
        return static_cast<std::size_t>(std::count_if(
            rooms.begin(),
            rooms.end(),
            [&](const auto& entry) {
                return entry.second.record.hostPeerId == peerId;
            }
        ));
    }

    std::size_t connectionCountForPeer(const std::string& peerId) const {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return entry.second.hostPeerId == peerId
                    || entry.second.remotePeerId == peerId;
            }
        ));
    }

    std::size_t connectionCountForRoom(const std::string& roomId) const {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return entry.second.roomId == roomId;
            }
        ));
    }

    std::size_t clientCountForRoom(const std::string& roomId) const {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return entry.second.roomId == roomId
                    && entry.second.remoteRole == RuntimeInternetRole::Client;
            }
        ));
    }

    std::size_t spectatorCountForRoom(const std::string& roomId) const {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return entry.second.roomId == roomId
                    && entry.second.remoteRole == RuntimeInternetRole::Spectator;
            }
        ));
    }

    std::size_t relayRouteCount() const {
        std::size_t count = 0;
        for (const auto& entry : connections) {
            for (const RelayState& relayState : entry.second.relay) {
                count += relayState.hasLease() ? 1U : 0U;
            }
        }
        return count;
    }

    bool randomValueExists(const std::vector<std::uint8_t>& value) const {
        for (const auto& [id, alias] : resumeAliases)
            if (alias.previousToken == value) return true;
        for (const auto& entry : connections) {
            const Connection& connection = entry.second;
            if (connection.connectionToken == value) {
                return true;
            }
            for (const RelayState& relayState : connection.relay) {
                if (relayState.hostTicket == value
                    || relayState.remoteTicket == value) {
                    return true;
                }
            }
        }
        return false;
    }

    bool makeUniqueRandom(
        std::size_t size,
        std::vector<std::uint8_t>& value,
        std::string& errorMessage
    ) const {
        for (int attempt = 0; attempt < 16; ++attempt) {
            std::vector<std::uint8_t> candidate(size);
            if (!config.secureRandomBytes(candidate.data(), candidate.size())) {
                errorMessage = "secure random source failed";
                return false;
            }
            if (!allZero(candidate) && !randomValueExists(candidate)) {
                value = std::move(candidate);
                return true;
            }
        }
        errorMessage = "secure random source repeated or returned only zero bytes";
        return false;
    }

    bool encodeAction(
        RuntimeInternetCoordinatorActionKind kind,
        const std::string& targetPeerId,
        RuntimeInternetRelayTransport transport,
        const RuntimeInternetControlMessage& message,
        RuntimeInternetCoordinatorAction& action,
        std::string& errorMessage
    ) const {
        RuntimeNetworkPacket packet;
        if (!encodeRuntimeInternetControlMessage(
                message,
                packet,
                errorMessage
            )) {
            return false;
        }
        action.kind = kind;
        action.targetPeerId = targetPeerId;
        action.relayTransport = transport;
        action.relayEndpoint = kind == RuntimeInternetCoordinatorActionKind::SendRelay
            ? ::relayEndpoint(config, transport)
            : RuntimeInternetEndpoint{};
        action.packet = std::move(packet);
        return true;
    }

    RuntimeInternetControlMessage serviceMessage(
        RuntimeInternetControlType type,
        std::uint64_t transactionId,
        const std::string& roomId,
        const std::string& targetPeerId
    ) const {
        RuntimeInternetControlMessage message;
        message.type = type;
        message.transactionId = transactionId;
        message.serviceId = config.serviceId;
        message.roomId = roomId;
        message.senderPeerId = config.serviceId;
        message.targetPeerId = targetPeerId;
        return message;
    }

    bool buildCandidateActions(
        const Connection& connection,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeInternetControlMessage toHost = serviceMessage(
            RuntimeInternetControlType::PeerCandidates,
            connection.hostTransactionId,
            connection.roomId,
            connection.hostPeerId
        );
        toHost.body = RuntimeInternetPeerCandidatesBody{
            connection.connectionId,
            connection.remotePeerId,
            connection.remoteRole,
            connection.remoteCandidates,
            connection.connectionToken,
            connection.resumesConnectionId
        };
        RuntimeInternetCoordinatorAction hostAction;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                connection.hostPeerId,
                RuntimeInternetRelayTransport::Udp,
                toHost,
                hostAction,
                errorMessage
            )) {
            return false;
        }

        RuntimeInternetControlMessage toRemote = serviceMessage(
            RuntimeInternetControlType::PeerCandidates,
            connection.remoteTransactionId,
            connection.roomId,
            connection.remotePeerId
        );
        toRemote.body = RuntimeInternetPeerCandidatesBody{
            connection.connectionId,
            connection.hostPeerId,
            RuntimeInternetRole::Host,
            connection.hostCandidates,
            connection.connectionToken,
            connection.resumesConnectionId
        };
        RuntimeInternetCoordinatorAction remoteAction;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                connection.remotePeerId,
                RuntimeInternetRelayTransport::Udp,
                toRemote,
                remoteAction,
                errorMessage
            )) {
            return false;
        }
        actions.push_back(std::move(hostAction));
        actions.push_back(std::move(remoteAction));
        return true;
    }

    RuntimeInternetRoomRecord visibleRoomRecord(const Room& room) const {
        RuntimeInternetRoomRecord result = room.record;
        const std::size_t connectedPlayers = 1U
            + clientCountForRoom(room.record.roomId);
        const std::size_t visiblePlayers = std::max<std::size_t>(
            result.playerCount,
            connectedPlayers
        );
        result.playerCount = static_cast<std::uint8_t>(std::min<std::size_t>(
            result.maximumPlayers,
            visiblePlayers
        ));
        return result;
    }

    void appendDeactivateAction(
        const Connection& connection,
        std::size_t route,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::uint64_t nowMilliseconds
    ) const {
        const RelayState& relayState = connection.relay[route];
        if (!relayState.active) {
            return;
        }
        RuntimeInternetCoordinatorAction action;
        action.kind = RuntimeInternetCoordinatorActionKind::DeactivateRelayRoute;
        action.relayTransport = ::relayTransport(route);
        action.relayEndpoint = ::relayEndpoint(config, action.relayTransport);
        action.connectionId = connection.connectionId;
        action.roomId = connection.roomId;
        action.hostPeerId = connection.hostPeerId;
        action.remotePeerId = connection.remotePeerId;
        action.remoteRole = connection.remoteRole;
        action.expiresAtMilliseconds = nowMilliseconds;
        actions.push_back(std::move(action));
    }

    void removeConnection(
        std::map<std::uint64_t, Connection>::iterator iterator,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions
    ) {
        Connection connection = std::move(iterator->second);
        for (std::size_t route = 0; route < connection.relay.size(); ++route) {
            appendDeactivateAction(connection, route, actions, nowMilliseconds);
        }
        connections.erase(iterator);
        addTombstone(
            std::move(connection.remotePeerId),
            connection.remoteTransactionId,
            RuntimeInternetControlType::JoinRoom,
            nowMilliseconds
        );
    }

    void removeRoom(
        std::map<std::string, Room>::iterator iterator,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions
    ) {
        const std::string roomId = iterator->first;
        std::vector<std::uint64_t> connectionIds;
        for (const auto& entry : connections) {
            if (entry.second.roomId == roomId) {
                connectionIds.push_back(entry.first);
            }
        }
        for (std::uint64_t connectionId : connectionIds) {
            const auto connection = connections.find(connectionId);
            if (connection != connections.end()) {
                removeConnection(connection, nowMilliseconds, actions);
            }
        }
        Room room = std::move(iterator->second);
        rooms.erase(iterator);
        addTombstone(
            std::move(room.record.hostPeerId),
            room.registerTransactionId,
            RuntimeInternetControlType::RegisterRoom,
            nowMilliseconds
        );
    }

    void reap(
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions
    ) {
        for (auto alias = resumeAliases.begin(); alias != resumeAliases.end();) {
            if (alias->second.expiresAtMilliseconds <= nowMilliseconds
                || !connections.contains(alias->second.replacementId)) alias = resumeAliases.erase(alias);
            else ++alias;
        }
        tombstones.erase(
            std::remove_if(
                tombstones.begin(),
                tombstones.end(),
                [&](const ReplayTombstone& tombstone) {
                    return tombstone.expiresAtMilliseconds <= nowMilliseconds;
                }
            ),
            tombstones.end()
        );

        std::vector<std::string> expiredRooms;
        for (const auto& entry : rooms) {
            if (entry.second.expiresAtMilliseconds <= nowMilliseconds) {
                expiredRooms.push_back(entry.first);
            }
        }
        for (const std::string& roomId : expiredRooms) {
            const auto room = rooms.find(roomId);
            if (room != rooms.end()) {
                bool retained = false;
                if (room->second.sealed && !room->second.reconnectLeaseHeld) {
                    for (auto& [id, connection] : connections) {
                        if (connection.roomId == roomId && runningCombatantConnection(connection)) {
                            suspendRelay(connection, nowMilliseconds, actions);
                            retained = true;
                        }
                    }
                    room->second.reconnectLeaseHeld = retained;
                }
                if (retained) continue;
                removeRoom(room, nowMilliseconds, actions);
            }
        }

        std::vector<std::uint64_t> expiredConnections;
        for (const auto& entry : connections) {
            if (entry.second.expiresAtMilliseconds <= nowMilliseconds
                || (entry.second.reconnectDeadlineMilliseconds != 0
                    && entry.second.reconnectDeadlineMilliseconds <= nowMilliseconds)) {
                expiredConnections.push_back(entry.first);
            }
        }
        for (std::uint64_t connectionId : expiredConnections) {
            const auto connection = connections.find(connectionId);
            if (connection != connections.end()) {
                removeConnection(connection, nowMilliseconds, actions);
            }
        }

        for (auto& entry : connections) {
            Connection& connection = entry.second;
            for (std::size_t route = 0; route < connection.relay.size(); ++route) {
                RelayState& relayState = connection.relay[route];
                if (relayState.hasLease()
                    && relayState.expiresAtMilliseconds <= nowMilliseconds) {
                    appendDeactivateAction(
                        connection,
                        route,
                        actions,
                        nowMilliseconds
                    );
                    relayState = {};
                }
            }
        }
    }

    std::optional<std::uint64_t> availableConnectionId() const {
        std::uint64_t candidate = nextConnectionId;
        for (std::size_t attempt = 0; attempt <= connections.size() + resumeAliases.size(); ++attempt) {
            if (candidate == 0) {
                candidate = 1;
            }
            if (candidate != 0 && !connections.contains(candidate) && !resumeAliases.contains(candidate)) {
                return candidate;
            }
            ++candidate;
        }
        return std::nullopt;
    }

    bool handleRegister(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetRegisterRoomBody& body,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        if ((!body.room.passwordProtected
                && !envelope.roomAdmissionProof.empty())
            || (!envelope.roomAdmissionProof.empty()
                && envelope.roomAdmissionProof.size()
                    != runtimeRoomPasswordVerifierBytes)) {
            errorMessage =
                "room password verifier does not match the registration";
            return false;
        }
        if (body.room.maximumPlayers
                > config.maximumConnectionsPerRoom + 1U) {
            errorMessage = "room maximum player count exceeds coordinator quota";
            return false;
        }
        if (isTombstoned(
                envelope.authenticatedPeerId,
                message.transactionId,
                RuntimeInternetControlType::RegisterRoom
            )) {
            errorMessage = "expired room registration transaction was replayed";
            return false;
        }

        const auto existing = rooms.find(message.roomId);
        if (existing != rooms.end()) {
            const Room& room = existing->second;
            const bool hasVerifier =
                !envelope.roomAdmissionProof.empty();
            const bool verifierMatches =
                room.hasPasswordVerifier == hasVerifier
                && (!hasVerifier
                    || runtimeRoomPasswordConstantTimeEqual(
                        std::span<const std::uint8_t>(
                            room.passwordVerifier
                        ),
                        std::span<const std::uint8_t>(
                            envelope.roomAdmissionProof
                        )
                    ));
            if (room.record.hostPeerId != envelope.authenticatedPeerId
                || room.registerTransactionId != message.transactionId
                || !packetEquals(room.registerPacket, envelope.packet)
                || !verifierMatches) {
                errorMessage = "room identifier or registration transaction is already bound";
                return false;
            }
            RuntimeInternetControlMessage accepted = serviceMessage(
                RuntimeInternetControlType::RegisterAccepted,
                message.transactionId,
                message.roomId,
                envelope.authenticatedPeerId
            );
            accepted.body = RuntimeInternetRegisterAcceptedBody{
                config.roomLeaseMilliseconds
            };
            RuntimeInternetCoordinatorAction action;
            if (!encodeAction(
                    RuntimeInternetCoordinatorActionKind::SendCoordinator,
                    envelope.authenticatedPeerId,
                    RuntimeInternetRelayTransport::Udp,
                    accepted,
                    action,
                    errorMessage
                )) {
                return false;
            }
            existing->second.expiresAtMilliseconds = saturatingAdd(
                nowMilliseconds,
                config.roomLeaseMilliseconds
            );
            existing->second.reconnectLeaseHeld = false;
            actions.push_back(std::move(action));
            return true;
        }

        if (rooms.size() >= config.maximumRooms) {
            errorMessage = "coordinator room capacity reached";
            return false;
        }
        if (roomCountForPeer(envelope.authenticatedPeerId)
                >= config.maximumRoomsPerPeer) {
            errorMessage = "host room quota reached";
            return false;
        }
        if (tombstones.size() + rooms.size() + connections.size()
                >= config.maximumReplayTombstones) {
            errorMessage = "coordinator replay retention capacity reached";
            return false;
        }

        RuntimeInternetControlMessage accepted = serviceMessage(
            RuntimeInternetControlType::RegisterAccepted,
            message.transactionId,
            message.roomId,
            envelope.authenticatedPeerId
        );
        accepted.body = RuntimeInternetRegisterAcceptedBody{
            config.roomLeaseMilliseconds
        };
        RuntimeInternetCoordinatorAction action;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                envelope.authenticatedPeerId,
                RuntimeInternetRelayTransport::Udp,
                accepted,
                action,
                errorMessage
            )) {
            return false;
        }

        Room room;
        room.record = body.room;
        room.hostCandidates = body.candidates;
        room.registerTransactionId = message.transactionId;
        room.registerPacket = envelope.packet;
        if (!envelope.roomAdmissionProof.empty()) {
            std::copy(
                envelope.roomAdmissionProof.begin(),
                envelope.roomAdmissionProof.end(),
                room.passwordVerifier.begin()
            );
            room.hasPasswordVerifier = true;
        }
        room.expiresAtMilliseconds = saturatingAdd(
            nowMilliseconds,
            config.roomLeaseMilliseconds
        );
        rooms.emplace(message.roomId, std::move(room));
        actions.push_back(std::move(action));
        return true;
    }

    bool handleRoomList(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetRoomListRequestBody& body,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) const {
        std::vector<RuntimeInternetRoomRecord> records;
        records.reserve(rooms.size());
        for (const auto& entry : rooms) {
            if (!entry.second.sealed
                && (body.contentFingerprint.empty()
                || entry.second.record.contentFingerprint
                    == body.contentFingerprint)) {
                records.push_back(visibleRoomRecord(entry.second));
            }
        }

        RuntimeInternetCoordinatorAction action;
        while (true) {
            RuntimeInternetControlMessage result = serviceMessage(
                RuntimeInternetControlType::RoomListResult,
                message.transactionId,
                {},
                envelope.authenticatedPeerId
            );
            result.body = RuntimeInternetRoomListResultBody{records};
            std::string encodeError;
            if (encodeAction(
                    RuntimeInternetCoordinatorActionKind::SendCoordinator,
                    envelope.authenticatedPeerId,
                    RuntimeInternetRelayTransport::Udp,
                    result,
                    action,
                    encodeError
                )) {
                break;
            }
            if (records.empty()) {
                errorMessage = std::move(encodeError);
                return false;
            }
            records.pop_back();
        }
        actions.push_back(std::move(action));
        return true;
    }

    bool handleSealRoom(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        const auto room = rooms.find(message.roomId);
        if (room == rooms.end()) {
            errorMessage = "room seal references an unknown room";
            return false;
        }
        if (room->second.record.hostPeerId
                != envelope.authenticatedPeerId
            || room->second.registerTransactionId
                != message.transactionId) {
            errorMessage =
                "room seal identity or registration transaction mismatch";
            return false;
        }
        RuntimeInternetControlMessage sealed = serviceMessage(
            RuntimeInternetControlType::RoomSealed,
            message.transactionId,
            message.roomId,
            envelope.authenticatedPeerId
        );
        sealed.body = RuntimeInternetRoomSealedBody{};
        RuntimeInternetCoordinatorAction action;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                envelope.authenticatedPeerId,
                RuntimeInternetRelayTransport::Udp,
                sealed,
                action,
                errorMessage
            )) {
            return false;
        }
        // Exact replay is idempotent. The registration heartbeat path updates
        // only the lease and therefore cannot make a sealed room visible.
        room->second.sealed = true;
        actions.push_back(std::move(action));
        return true;
    }

    bool handleHeartbeat(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetCoordinatorHeartbeatBody& heartbeat,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeInternetControlMessage acknowledgement = serviceMessage(
            RuntimeInternetControlType::CoordinatorHeartbeatAck,
            message.transactionId,
            message.roomId,
            envelope.authenticatedPeerId
        );
        acknowledgement.body = RuntimeInternetCoordinatorHeartbeatAckBody{
            heartbeat.sequence
        };
        RuntimeInternetCoordinatorAction action;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                envelope.authenticatedPeerId,
                RuntimeInternetRelayTransport::Udp,
                acknowledgement,
                action,
                errorMessage
            )) {
            return false;
        }
        actions.push_back(std::move(action));
        return true;
    }

    bool handleJoin(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetJoinRoomBody& body,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        if (isTombstoned(
                envelope.authenticatedPeerId,
                message.transactionId,
                RuntimeInternetControlType::JoinRoom
            )) {
            errorMessage = "expired room join transaction was replayed";
            return false;
        }

        for (const auto& entry : connections) {
            const Connection& connection = entry.second;
            if (connection.remotePeerId == envelope.authenticatedPeerId
                && connection.remoteTransactionId == message.transactionId) {
                if (connection.roomId != message.roomId
                    || !packetEquals(connection.joinPacket, envelope.packet)
                    || !runtimeRoomPasswordConstantTimeEqual(
                        std::span<const std::uint8_t>(
                            connection.admissionProof
                        ),
                        std::span<const std::uint8_t>(
                            envelope.roomAdmissionProof
                        )
                    )) {
                    errorMessage = "join transaction was replayed with different input";
                    return false;
                }
                return buildCandidateActions(connection, actions, errorMessage);
            }
        }

        const auto roomIterator = rooms.find(message.roomId);
        if (roomIterator == rooms.end()) {
            errorMessage = "room does not exist or its lease expired";
            return false;
        }
        const Room& room = roomIterator->second;
        if (room.sealed) {
            errorMessage = "room is sealed and no longer accepts joins";
            return false;
        }
        if (room.record.hostPeerId == envelope.authenticatedPeerId) {
            errorMessage = "room host cannot join its own room as a remote peer";
            return false;
        }
        if (room.record.contentFingerprint != body.contentFingerprint) {
            errorMessage = "room content fingerprint mismatch";
            return false;
        }
        if (room.record.passwordProtected) {
            bool authorized = false;
            if (room.hasPasswordVerifier
                && envelope.roomAdmissionProof.size()
                    == runtimeRoomPasswordVerifierBytes) {
                RuntimeRoomPasswordProof expected =
                    runtimeInternetRoomPasswordJoinProof(
                        room.passwordVerifier,
                        message.roomId,
                        envelope.authenticatedPeerId,
                        message.transactionId
                    );
                authorized = runtimeRoomPasswordConstantTimeEqual(
                    std::span<const std::uint8_t>(expected),
                    std::span<const std::uint8_t>(
                        envelope.roomAdmissionProof
                    )
                );
                runtimeRoomPasswordSecureClear(expected);
            } else if (!room.hasPasswordVerifier
                && !envelope.roomAdmissionProof.empty()
                && envelope.roomAdmissionProof.size()
                    <= runtimeInternetCoordinatorMaximumAdmissionProofBytes
                && config.authorizePasswordJoin) {
                authorized = config.authorizePasswordJoin(
                    message.roomId,
                    envelope.authenticatedPeerId,
                    envelope.roomAdmissionProof
                );
            }
            if (!authorized) {
                errorMessage = "password-protected room admission was denied";
                return false;
            }
        } else if (!envelope.roomAdmissionProof.empty()) {
            errorMessage = "unexpected room admission proof";
            return false;
        }

        if (connections.size() >= config.maximumConnections
            || connectionCountForRoom(message.roomId)
                >= config.maximumConnectionsPerRoom) {
            errorMessage = "coordinator connection capacity reached";
            return false;
        }
        if (connectionCountForPeer(envelope.authenticatedPeerId)
                >= config.maximumConnectionsPerPeer
            || connectionCountForPeer(room.record.hostPeerId)
                >= config.maximumConnectionsPerPeer) {
            errorMessage = "host or remote peer connection quota reached";
            return false;
        }
        const bool duplicatePeer = std::any_of(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return entry.second.roomId == message.roomId
                    && entry.second.remotePeerId == envelope.authenticatedPeerId;
            }
        );
        if (duplicatePeer) {
            errorMessage = "peer is already joined to this room";
            return false;
        }
        const std::size_t currentPlayerCount = std::max<std::size_t>(
            room.record.playerCount,
            1U + clientCountForRoom(message.roomId)
        );
        if (body.role == RuntimeInternetRole::Client
            && currentPlayerCount >= room.record.maximumPlayers) {
            errorMessage = "room player capacity reached";
            return false;
        }
        if (body.role == RuntimeInternetRole::Spectator
            && spectatorCountForRoom(message.roomId)
                >= config.maximumSpectatorsPerRoom) {
            errorMessage = "room spectator capacity reached";
            return false;
        }
        if (tombstones.size() + rooms.size() + connections.size()
                >= config.maximumReplayTombstones) {
            errorMessage = "coordinator replay retention capacity reached";
            return false;
        }

        const std::optional<std::uint64_t> connectionId = availableConnectionId();
        if (!connectionId.has_value()) {
            errorMessage = "connection identifier space exhausted";
            return false;
        }
        Connection connection;
        connection.connectionId = *connectionId;
        connection.roomId = message.roomId;
        connection.hostPeerId = room.record.hostPeerId;
        connection.remotePeerId = envelope.authenticatedPeerId;
        connection.remoteRole = body.role;
        connection.hostTransactionId = room.registerTransactionId;
        connection.remoteTransactionId = message.transactionId;
        connection.hostCandidates = room.hostCandidates;
        connection.remoteCandidates = body.candidates;
        connection.admissionProof = envelope.roomAdmissionProof;
        connection.joinPacket = envelope.packet;
        connection.expiresAtMilliseconds = saturatingAdd(
            nowMilliseconds,
            config.connectionLeaseMilliseconds
        );
        if (!makeUniqueRandom(
                config.connectionTokenBytes,
                connection.connectionToken,
                errorMessage
            )) {
            return false;
        }

        std::vector<RuntimeInternetCoordinatorAction> candidateActions;
        if (!buildCandidateActions(
                connection,
                candidateActions,
                errorMessage
            )) {
            return false;
        }
        connections.emplace(connection.connectionId, std::move(connection));
        nextConnectionId = *connectionId + 1U;
        if (nextConnectionId == 0) {
            nextConnectionId = 1;
        }
        actions.insert(
            actions.end(),
            std::make_move_iterator(candidateActions.begin()),
            std::make_move_iterator(candidateActions.end())
        );
        return true;
    }

    bool runningCombatantConnection(const Connection& connection) const {
        const auto room = rooms.find(connection.roomId);
        return room != rooms.end() && room->second.sealed
            && connection.remoteRole == RuntimeInternetRole::Client
            && connection.gameplayRouteActivated;
    }

    void suspendRelay(Connection& connection, std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions) {
        if (connection.reconnectDeadlineMilliseconds == 0)
            connection.reconnectDeadlineMilliseconds = saturatingAdd(nowMilliseconds, config.reconnectGraceMilliseconds);
        for (std::size_t route = 0; route < connection.relay.size(); ++route)
            appendDeactivateAction(connection, route, actions, nowMilliseconds);
        connection.relay = {};
        connection.expiresAtMilliseconds = connection.reconnectDeadlineMilliseconds;
        const auto room = rooms.find(connection.roomId);
        if (room != rooms.end()) room->second.expiresAtMilliseconds = std::max(
            room->second.expiresAtMilliseconds, connection.reconnectDeadlineMilliseconds);
    }

    bool handleResumeRoute(const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message, const RuntimeInternetResumeRouteBody& body,
        std::uint64_t nowMilliseconds, std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage) {
        const auto identityMatches = [&](const auto& route, const auto& token) {
            return message.roomId == route.roomId
                && (envelope.authenticatedPeerId == route.hostPeerId || envelope.authenticatedPeerId == route.remotePeerId)
                && runtimeRoomPasswordConstantTimeEqual(body.connectionToken, token);
        };
        if (const auto previous = resumeAliases.find(body.connectionId); previous != resumeAliases.end()) {
            const auto replacement = connections.find(previous->second.replacementId);
            if (!identityMatches(previous->second, previous->second.previousToken)
                || replacement == connections.end()) {
                errorMessage = "Internet resume identity or retained route differs";return false;
            }
            Connection repeated = replacement->second;
            repeated.resumesConnectionId = body.connectionId;
            return buildCandidateActions(repeated, actions, errorMessage);
        }
        const auto found = connections.find(body.connectionId);
        if (found == connections.end() || !identityMatches(found->second, found->second.connectionToken)
            || !runningCombatantConnection(found->second)) {
            errorMessage = "Internet resume requires the original sealed combatant route";return false;
        }
        if (resumeAliases.size() >= config.maximumReplayTombstones) {
            errorMessage = "Internet resume retention capacity reached";return false;
        }
        const auto freshId = availableConnectionId();
        if (!freshId) {errorMessage = "Internet resume route identity exhausted";return false;}
        Connection replacement = found->second;
        replacement.connectionId = *freshId;
        replacement.resumesConnectionId = body.connectionId;
        replacement.relay = {};
        replacement.tcpRelayRequired = true;
        if (replacement.reconnectDeadlineMilliseconds == 0)
            replacement.reconnectDeadlineMilliseconds = saturatingAdd(nowMilliseconds, config.reconnectGraceMilliseconds);
        replacement.expiresAtMilliseconds = replacement.reconnectDeadlineMilliseconds;
        if (!makeUniqueRandom(config.connectionTokenBytes, replacement.connectionToken, errorMessage)) return false;
        std::vector<RuntimeInternetCoordinatorAction> offered;
        if (!buildCandidateActions(replacement, offered, errorMessage)) return false;
        ResumeAlias alias;
        alias.replacementId = *freshId;
        alias.expiresAtMilliseconds = replacement.reconnectDeadlineMilliseconds;
        alias.roomId = replacement.roomId;alias.hostPeerId = replacement.hostPeerId;
        alias.remotePeerId = replacement.remotePeerId;alias.previousToken = found->second.connectionToken;
        suspendRelay(found->second, nowMilliseconds, actions);
        for (auto& [id, previous] : resumeAliases)
            if (previous.replacementId == body.connectionId) previous.replacementId = *freshId;
        resumeAliases.emplace(body.connectionId, std::move(alias));
        connections.erase(found);
        connections.emplace(*freshId, std::move(replacement));
        nextConnectionId = *freshId + 1;
        if (nextConnectionId == 0) nextConnectionId = 1;
        actions.insert(actions.end(), std::make_move_iterator(offered.begin()), std::make_move_iterator(offered.end()));
        errorMessage.clear();return true;
    }

    bool handleRelayRequest(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetRelayRequestBody& body,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        const auto connectionIterator = connections.find(body.connectionId);
        if (connectionIterator == connections.end()) {
            errorMessage = "unknown or expired relay connection";
            return false;
        }
        const Connection& original = connectionIterator->second;
        const bool isHost = envelope.authenticatedPeerId == original.hostPeerId;
        const bool isRemote = envelope.authenticatedPeerId == original.remotePeerId;
        if ((!isHost && !isRemote)
            || message.roomId != original.roomId
            || message.transactionId != original.connectionId
            || body.connectionToken != original.connectionToken) {
            errorMessage = "relay request identity, room, role, or token mismatch";
            return false;
        }
        if (std::any_of(
                original.relay.begin(),
                original.relay.end(),
                [](const RelayState& relayState) { return relayState.active; }
            )) {
            errorMessage = "relay request replayed after route activation";
            return false;
        }
        if (body.transport == RuntimeInternetRelayTransport::Udp
            && original.tcpRelayRequired) {
            errorMessage = "stale UDP relay request replayed after TCP fallback";
            return false;
        }

        Connection updated = original;
        updated.tcpRelayRequired = updated.tcpRelayRequired
            || body.transport == RuntimeInternetRelayTransport::Tcp;
        const std::size_t selected = relayIndex(body.transport);
        const std::size_t other = selected == 0U ? 1U : 0U;
        const bool removedOtherRoute = updated.relay[other].hasLease();
        if (removedOtherRoute) {
            updated.relay[other] = {};
        }
        RelayState& relayState = updated.relay[selected];
        const bool createsRoute = !relayState.hasLease();
        std::size_t prospectiveRoutes = relayRouteCount();
        prospectiveRoutes -= removedOtherRoute ? 1U : 0U;
        prospectiveRoutes += createsRoute ? 1U : 0U;
        if (prospectiveRoutes > config.maximumRelayRoutes) {
            errorMessage = "relay route quota reached";
            return false;
        }

        std::vector<std::uint8_t>& ticket = isHost
            ? relayState.hostTicket
            : relayState.remoteTicket;
        if (ticket.empty()
            && !makeUniqueRandom(config.relayTicketBytes, ticket, errorMessage)) {
            return false;
        }
        relayState.expiresAtMilliseconds = saturatingAdd(
            nowMilliseconds,
            config.relayLeaseMilliseconds
        );
        updated.expiresAtMilliseconds = std::max(
            updated.expiresAtMilliseconds,
            relayState.expiresAtMilliseconds
        );

        RuntimeInternetControlMessage lease = serviceMessage(
            RuntimeInternetControlType::RelayLease,
            original.connectionId,
            original.roomId,
            envelope.authenticatedPeerId
        );
        lease.body = RuntimeInternetRelayLeaseBody{
            original.connectionId,
            body.transport,
            ::relayEndpoint(config, body.transport),
            ticket
        };
        RuntimeInternetCoordinatorAction action;
        if (!encodeAction(
                RuntimeInternetCoordinatorActionKind::SendCoordinator,
                envelope.authenticatedPeerId,
                body.transport,
                lease,
                action,
                errorMessage
            )) {
            return false;
        }
        connectionIterator->second = std::move(updated);
        actions.push_back(std::move(action));
        return true;
    }

    bool buildRelayBoundAction(
        const Connection& connection,
        RuntimeInternetRelayTransport transport,
        const std::string& targetPeerId,
        RuntimeInternetCoordinatorAction& action,
        std::string& errorMessage
    ) const {
        RuntimeInternetControlMessage bound = serviceMessage(
            RuntimeInternetControlType::RelayBound,
            connection.connectionId,
            connection.roomId,
            targetPeerId
        );
        bound.body = RuntimeInternetRelayBoundBody{
            connection.connectionId,
            transport
        };
        if (!encodeAction(
            RuntimeInternetCoordinatorActionKind::SendRelay,
            targetPeerId,
            transport,
            bound,
            action,
            errorMessage
        )) {
            return false;
        }
        // A host can own several routes over the same authenticated relay
        // transport. targetPeerId alone is therefore not a routable key.
        action.connectionId = connection.connectionId;
        action.roomId = connection.roomId;
        action.hostPeerId = connection.hostPeerId;
        action.remotePeerId = connection.remotePeerId;
        action.remoteRole = connection.remoteRole;
        action.expiresAtMilliseconds =
            connection.relay[relayIndex(transport)].expiresAtMilliseconds;
        return true;
    }

    bool handleRelayBind(
        const RuntimeInternetServiceEnvelope& envelope,
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetRelayBindBody& body,
        RuntimeInternetRelayTransport channelTransport,
        std::uint64_t nowMilliseconds,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        if (body.transport != channelTransport) {
            errorMessage = "relay ticket was presented on the wrong transport";
            return false;
        }
        const auto connectionIterator = connections.find(body.connectionId);
        if (connectionIterator == connections.end()) {
            errorMessage = "unknown or expired relay binding";
            return false;
        }
        const Connection& original = connectionIterator->second;
        const bool isHost = envelope.authenticatedPeerId == original.hostPeerId;
        const bool isRemote = envelope.authenticatedPeerId == original.remotePeerId;
        if ((!isHost && !isRemote)
            || message.roomId != original.roomId
            || message.transactionId != original.connectionId
            || message.targetPeerId != config.serviceId) {
            errorMessage = "relay binding identity, room, role, or service mismatch";
            return false;
        }

        const std::size_t selected = relayIndex(body.transport);
        const RelayState& originalRelay = original.relay[selected];
        const std::vector<std::uint8_t>& expectedTicket = isHost
            ? originalRelay.hostTicket
            : originalRelay.remoteTicket;
        const bool alreadyBound = isHost
            ? originalRelay.hostBound
            : originalRelay.remoteBound;
        if (expectedTicket.empty() || body.ticket != expectedTicket) {
            errorMessage = "relay ticket is unknown or bound to another principal";
            return false;
        }
        if (originalRelay.expiresAtMilliseconds <= nowMilliseconds) {
            errorMessage = "relay ticket expired";
            return false;
        }
        if (originalRelay.active) {
            errorMessage = "relay bind ticket replayed after activation";
            return false;
        }
        if (alreadyBound) {
            // A pre-activation retry is idempotent because the client polls its
            // bind until the other side arrives. It cannot activate twice.
            return true;
        }

        Connection updated = original;
        RelayState& relayState = updated.relay[selected];
        if (isHost) {
            relayState.hostBound = true;
        } else {
            relayState.remoteBound = true;
        }
        updated.expiresAtMilliseconds = std::max(
            updated.expiresAtMilliseconds,
            relayState.expiresAtMilliseconds
        );

        std::vector<RuntimeInternetCoordinatorAction> routeActions;
        if (relayState.hostBound && relayState.remoteBound) {
            relayState.active = true;
            updated.gameplayRouteActivated = updated.gameplayRouteActivated
                || body.transport == RuntimeInternetRelayTransport::Tcp;
            updated.reconnectDeadlineMilliseconds = 0;
            RuntimeInternetCoordinatorAction activate;
            activate.kind = RuntimeInternetCoordinatorActionKind::ActivateRelayRoute;
            activate.relayTransport = body.transport;
            activate.relayEndpoint = ::relayEndpoint(config, body.transport);
            activate.connectionId = original.connectionId;
            activate.roomId = original.roomId;
            activate.hostPeerId = original.hostPeerId;
            activate.remotePeerId = original.remotePeerId;
            activate.remoteRole = original.remoteRole;
            activate.expiresAtMilliseconds = relayState.expiresAtMilliseconds;
            routeActions.push_back(std::move(activate));

            RuntimeInternetCoordinatorAction hostBound;
            RuntimeInternetCoordinatorAction remoteBound;
            if (!buildRelayBoundAction(
                    updated,
                    body.transport,
                    updated.hostPeerId,
                    hostBound,
                    errorMessage
                )
                || !buildRelayBoundAction(
                    updated,
                    body.transport,
                    updated.remotePeerId,
                    remoteBound,
                    errorMessage
                )) {
                return false;
            }
            routeActions.push_back(std::move(hostBound));
            routeActions.push_back(std::move(remoteBound));
        }
        connectionIterator->second = std::move(updated);
        actions.insert(
            actions.end(),
            std::make_move_iterator(routeActions.begin()),
            std::make_move_iterator(routeActions.end())
        );
        return true;
    }
};

RuntimeInternetCoordinator::RuntimeInternetCoordinator()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetCoordinator::~RuntimeInternetCoordinator() = default;
RuntimeInternetCoordinator::RuntimeInternetCoordinator(
    RuntimeInternetCoordinator&&
) noexcept = default;
RuntimeInternetCoordinator& RuntimeInternetCoordinator::operator=(
    RuntimeInternetCoordinator&&
) noexcept = default;

bool RuntimeInternetCoordinator::open(
    RuntimeInternetCoordinatorConfig config,
    std::string& errorMessage
) {
    if (impl_->opened) {
        errorMessage = "Internet coordinator is already open";
        return false;
    }
    const bool validCapacity = config.maximumRooms > 0
        && config.maximumRooms <= runtimeInternetCoordinatorHardMaximumRooms
        && config.maximumRoomsPerPeer > 0
        && config.maximumRoomsPerPeer <= config.maximumRooms
        && config.maximumConnections > 0
        && config.maximumConnections
            <= runtimeInternetCoordinatorHardMaximumConnections
        && config.maximumConnectionsPerRoom > 0
        && config.maximumConnectionsPerRoom
            <= runtimeInternetCoordinatorHardMaximumConnectionsPerRoom
        && config.maximumConnectionsPerRoom <= config.maximumConnections
        && config.maximumSpectatorsPerRoom
            <= runtimeInternetCoordinatorHardMaximumSpectatorsPerRoom
        && config.maximumSpectatorsPerRoom
            <= config.maximumConnectionsPerRoom
        && config.maximumConnectionsPerPeer > 0
        && config.maximumConnectionsPerPeer
            <= runtimeInternetCoordinatorHardMaximumConnectionsPerPeer
        && config.maximumRelayRoutes > 0
        && config.maximumRelayRoutes
            <= runtimeInternetCoordinatorHardMaximumRelayRoutes
        && config.maximumReplayTombstones
            >= config.maximumConnections + config.maximumRooms
        && config.maximumReplayTombstones
            <= runtimeInternetCoordinatorHardMaximumReplayTombstones;
    const bool validDurations = config.roomLeaseMilliseconds >= 1000U
        && config.connectionLeaseMilliseconds >= 1000U
        && config.relayLeaseMilliseconds >= 1000U
        && config.replayRetentionMilliseconds >= 1000U
        && config.reconnectGraceMilliseconds >= 1000U
        && config.reconnectGraceMilliseconds <= 120000U;
    const bool validSizes = config.connectionTokenBytes
            >= runtimeInternetMinimumConnectionTokenBytes
        && config.connectionTokenBytes
            <= runtimeInternetMaximumConnectionTokenBytes
        && config.relayTicketBytes >= 16U
        && config.relayTicketBytes <= runtimeInternetMaximumRelayTicketBytes;
    if (!validText(config.serviceId, runtimeInternetMaximumIdentityBytes)
        || !validEndpoint(config.udpRelayEndpoint)
        || !validEndpoint(config.tcpRelayEndpoint)
        || !validCapacity || !validDurations || !validSizes
        || !config.nowMilliseconds || !config.secureRandomBytes) {
        errorMessage = "invalid Internet coordinator configuration";
        return false;
    }

    impl_->config = std::move(config);
    impl_->rooms.clear();
    impl_->connections.clear();
    impl_->tombstones.clear();
    impl_->resumeAliases.clear();
    impl_->nextConnectionId = 1;
    impl_->lastNowMilliseconds = impl_->config.nowMilliseconds();
    impl_->opened = true;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCoordinator::receive(
    const RuntimeInternetServiceEnvelope& envelope,
    std::vector<RuntimeInternetCoordinatorAction>& actions,
    std::string& errorMessage
) {
    actions.clear();
    if (!impl_->opened) {
        errorMessage = "Internet coordinator is not open";
        return false;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->reap(nowMilliseconds, actions);

    RuntimeInternetControlMessage message;
    if (!decodeRuntimeInternetControlMessage(
            envelope.packet,
            message,
            errorMessage
        )) {
        return false;
    }
    if (envelope.authenticatedServiceId != impl_->config.serviceId
        || message.serviceId != impl_->config.serviceId
        || envelope.authenticatedPeerId != message.senderPeerId
        || envelope.authenticatedPeerId == impl_->config.serviceId
        || !validText(
            envelope.authenticatedPeerId,
            runtimeInternetMaximumIdentityBytes
        )) {
        errorMessage = "authenticated service or peer identity mismatch";
        return false;
    }
    if (envelope.channel != RuntimeInternetServiceChannel::CoordinatorTls
        && envelope.channel != RuntimeInternetServiceChannel::RelayUdp
        && envelope.channel != RuntimeInternetServiceChannel::RelayTcp) {
        errorMessage = "unknown Internet service ingress channel";
        return false;
    }
    if (envelope.roomAdmissionProof.size()
        > runtimeInternetCoordinatorMaximumAdmissionProofBytes) {
        errorMessage = "room admission proof exceeds service envelope limit";
        return false;
    }

    if (envelope.channel == RuntimeInternetServiceChannel::CoordinatorTls) {
        if (!envelope.roomAdmissionProof.empty()
            && message.type != RuntimeInternetControlType::JoinRoom
            && message.type != RuntimeInternetControlType::RegisterRoom) {
            errorMessage = "room admission proof is invalid for this request";
            return false;
        }
        switch (message.type) {
        case RuntimeInternetControlType::ResumeRoute: {
            const auto* body = std::get_if<RuntimeInternetResumeRouteBody>(&message.body);
            return body && impl_->handleResumeRoute(envelope, message, *body, nowMilliseconds, actions, errorMessage);
        }
        case RuntimeInternetControlType::RegisterRoom: {
            const auto* body = std::get_if<RuntimeInternetRegisterRoomBody>(
                &message.body
            );
            return body != nullptr && impl_->handleRegister(
                envelope,
                message,
                *body,
                nowMilliseconds,
                actions,
                errorMessage
            );
        }
        case RuntimeInternetControlType::RoomListRequest: {
            const auto* body = std::get_if<RuntimeInternetRoomListRequestBody>(
                &message.body
            );
            return body != nullptr && impl_->handleRoomList(
                envelope,
                message,
                *body,
                actions,
                errorMessage
            );
        }
        case RuntimeInternetControlType::SealRoom: {
            const auto* body = std::get_if<RuntimeInternetSealRoomBody>(
                &message.body
            );
            return body != nullptr && impl_->handleSealRoom(
                envelope,
                message,
                actions,
                errorMessage
            );
        }
        case RuntimeInternetControlType::CoordinatorHeartbeat: {
            const auto* body = std::get_if<
                RuntimeInternetCoordinatorHeartbeatBody
            >(&message.body);
            return body != nullptr && impl_->handleHeartbeat(
                envelope,
                message,
                *body,
                actions,
                errorMessage
            );
        }
        case RuntimeInternetControlType::JoinRoom: {
            const auto* body = std::get_if<RuntimeInternetJoinRoomBody>(
                &message.body
            );
            return body != nullptr && impl_->handleJoin(
                envelope,
                message,
                *body,
                nowMilliseconds,
                actions,
                errorMessage
            );
        }
        case RuntimeInternetControlType::RelayRequest: {
            const auto* body = std::get_if<RuntimeInternetRelayRequestBody>(
                &message.body
            );
            return body != nullptr && impl_->handleRelayRequest(
                envelope,
                message,
                *body,
                nowMilliseconds,
                actions,
                errorMessage
            );
        }
        default:
            errorMessage = "message type is not accepted on coordinator ingress";
            return false;
        }
    }

    if (!envelope.roomAdmissionProof.empty()) {
        errorMessage = "room admission proof is invalid on relay ingress";
        return false;
    }

    const RuntimeInternetRelayTransport channelTransport =
        envelope.channel == RuntimeInternetServiceChannel::RelayUdp
            ? RuntimeInternetRelayTransport::Udp
            : RuntimeInternetRelayTransport::Tcp;
    const auto* body = std::get_if<RuntimeInternetRelayBindBody>(&message.body);
    if (message.type != RuntimeInternetControlType::RelayBind || body == nullptr) {
        errorMessage = "only relay-bind messages are accepted on relay ingress";
        return false;
    }
    return impl_->handleRelayBind(
        envelope,
        message,
        *body,
        channelTransport,
        nowMilliseconds,
        actions,
        errorMessage
    );
}

bool RuntimeInternetCoordinator::touchRelayRoute(
    std::uint64_t connectionId,
    RuntimeInternetRelayTransport transport,
    const std::string& authenticatedServiceId,
    const std::string& authenticatedPeerId,
    std::vector<RuntimeInternetCoordinatorAction>& actions,
    std::string& errorMessage
) {
    actions.clear();
    if (!impl_->opened) {
        errorMessage = "Internet coordinator is not open";
        return false;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->reap(nowMilliseconds, actions);
    if (authenticatedServiceId != impl_->config.serviceId
        || connectionId == 0 || !validRelay(transport)) {
        errorMessage = "relay heartbeat service or route is invalid";
        return false;
    }
    const auto connection = impl_->connections.find(connectionId);
    if (connection == impl_->connections.end()) {
        errorMessage = "relay heartbeat references an unknown route";
        return false;
    }
    const bool isHost = authenticatedPeerId == connection->second.hostPeerId;
    const bool isRemote =
        authenticatedPeerId == connection->second.remotePeerId;
    Impl::RelayState& relayState =
        connection->second.relay[relayIndex(transport)];
    const bool sideBound = isHost
        ? relayState.hostBound
        : (isRemote && relayState.remoteBound);
    if ((!isHost && !isRemote) || !relayState.active || !sideBound) {
        errorMessage =
            "relay heartbeat identity is not bound to the active route";
        return false;
    }
    relayState.expiresAtMilliseconds = saturatingAdd(
        nowMilliseconds,
        impl_->config.relayLeaseMilliseconds
    );
    connection->second.expiresAtMilliseconds = std::max(
        connection->second.expiresAtMilliseconds,
        relayState.expiresAtMilliseconds
    );
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCoordinator::hasPendingReconnect(const std::string& authenticatedServiceId,
    const std::string& authenticatedPeerId) const {
    if (!impl_->opened || authenticatedServiceId != impl_->config.serviceId) return false;
    const auto now = impl_->now();
    return std::any_of(impl_->connections.begin(), impl_->connections.end(), [&](const auto& entry) {
        const auto& route = entry.second;
        return impl_->runningCombatantConnection(route) && route.reconnectDeadlineMilliseconds > now
            && (route.hostPeerId == authenticatedPeerId || route.remotePeerId == authenticatedPeerId);
    });
}

std::uint64_t RuntimeInternetCoordinator::retainRunningPeerForReconnect(
    const std::string& authenticatedServiceId,
    const std::string& authenticatedPeerId
) {
    if (!impl_->opened || authenticatedServiceId != impl_->config.serviceId) return 0;
    const auto now = impl_->now();
    std::uint64_t retainedUntil = 0;
    for (auto& [id, connection] : impl_->connections) {
        if (!impl_->runningCombatantConnection(connection)
            || (connection.hostPeerId != authenticatedPeerId && connection.remotePeerId != authenticatedPeerId)) continue;
        // Retention does not break a still healthy gameplay stream. Relay loss
        // and explicit resume revoke its tickets through suspendRelay().
        if (connection.reconnectDeadlineMilliseconds == 0)
            connection.reconnectDeadlineMilliseconds = saturatingAdd(now, impl_->config.reconnectGraceMilliseconds);
        connection.expiresAtMilliseconds = std::max(connection.expiresAtMilliseconds, connection.reconnectDeadlineMilliseconds);
        auto& room = impl_->rooms.at(connection.roomId);
        room.expiresAtMilliseconds = std::max(room.expiresAtMilliseconds, connection.reconnectDeadlineMilliseconds);
        retainedUntil = std::max(retainedUntil, connection.reconnectDeadlineMilliseconds);
    }
    return retainedUntil;
}

bool RuntimeInternetCoordinator::authenticatedPeerDisconnected(
    const std::string& authenticatedServiceId,
    const std::string& authenticatedPeerId,
    std::vector<RuntimeInternetCoordinatorAction>& actions,
    std::string& errorMessage
) {
    actions.clear();
    if (!impl_->opened) {
        errorMessage = "Internet coordinator is not open";
        return false;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->reap(nowMilliseconds, actions);
    if (authenticatedServiceId != impl_->config.serviceId
        || !validText(
            authenticatedPeerId,
            runtimeInternetMaximumIdentityBytes
        )) {
        errorMessage = "coordinator disconnect identity is invalid";
        return false;
    }

    std::vector<std::string> hostedRoomIds;
    for (const auto& entry : impl_->rooms) {
        if (entry.second.record.hostPeerId == authenticatedPeerId) {
            hostedRoomIds.push_back(entry.first);
        }
    }
    for (const std::string& roomId : hostedRoomIds) {
        const auto room = impl_->rooms.find(roomId);
        if (room != impl_->rooms.end()) {
            impl_->removeRoom(room, nowMilliseconds, actions);
        }
    }

    std::vector<std::uint64_t> remoteConnectionIds;
    for (const auto& entry : impl_->connections) {
        if (entry.second.remotePeerId == authenticatedPeerId) {
            remoteConnectionIds.push_back(entry.first);
        }
    }
    for (const std::uint64_t connectionId : remoteConnectionIds) {
        const auto connection = impl_->connections.find(connectionId);
        if (connection != impl_->connections.end()) {
            impl_->removeConnection(connection, nowMilliseconds, actions);
        }
    }
    errorMessage.clear();
    return true;
}

bool RuntimeInternetCoordinator::relayChannelClosed(
    std::uint64_t connectionId,
    RuntimeInternetRelayTransport transport,
    const std::string& authenticatedServiceId,
    const std::string& authenticatedPeerId,
    std::vector<RuntimeInternetCoordinatorAction>& actions,
    std::string& errorMessage
) {
    actions.clear();
    if (!impl_->opened) {
        errorMessage = "Internet coordinator is not open";
        return false;
    }
    const std::uint64_t nowMilliseconds = impl_->now();
    impl_->reap(nowMilliseconds, actions);
    if (authenticatedServiceId != impl_->config.serviceId
        || connectionId == 0 || !validRelay(transport)) {
        errorMessage = "relay close service or route is invalid";
        return false;
    }
    const auto connection = impl_->connections.find(connectionId);
    if (connection == impl_->connections.end()) {
        errorMessage = "relay close references an unknown route";
        return false;
    }
    const bool isHost = authenticatedPeerId == connection->second.hostPeerId;
    const bool isRemote =
        authenticatedPeerId == connection->second.remotePeerId;
    const Impl::RelayState& relayState =
        connection->second.relay[relayIndex(transport)];
    const bool ownsChannel = isHost
        ? !relayState.hostTicket.empty()
        : (isRemote && !relayState.remoteTicket.empty());
    if ((!isHost && !isRemote) || !ownsChannel) {
        errorMessage =
            "relay close identity is not bound to the selected route";
        return false;
    }
    if (transport == RuntimeInternetRelayTransport::Tcp
        && impl_->runningCombatantConnection(connection->second)) {
        impl_->suspendRelay(connection->second, nowMilliseconds, actions);
    } else {
        impl_->removeConnection(connection, nowMilliseconds, actions);
    }
    errorMessage.clear();
    return true;
}

std::vector<RuntimeInternetCoordinatorAction> RuntimeInternetCoordinator::poll() {
    std::vector<RuntimeInternetCoordinatorAction> actions;
    if (impl_->opened) {
        impl_->reap(impl_->now(), actions);
    }
    return actions;
}

void RuntimeInternetCoordinator::close() {
    impl_->opened = false;
    impl_->rooms.clear();
    impl_->connections.clear();
    impl_->resumeAliases.clear();
    impl_->tombstones.clear();
    impl_->nextConnectionId = 1;
    impl_->lastNowMilliseconds = 0;
}

bool RuntimeInternetCoordinator::isOpen() const {
    return impl_->opened;
}

std::size_t RuntimeInternetCoordinator::roomCount() const {
    return impl_->rooms.size();
}

std::size_t RuntimeInternetCoordinator::connectionCount() const {
    return impl_->connections.size();
}

std::size_t RuntimeInternetCoordinator::relayRouteCount() const {
    return impl_->relayRouteCount();
}
