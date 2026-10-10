#include "network/internet/RuntimeInternetServiceHost.h"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t hostReadChunkBytes = 64U * 1024U;

std::uint64_t deadlineAfter(
    std::uint64_t nowMilliseconds,
    std::uint64_t durationMilliseconds
) noexcept {
    if (durationMilliseconds
        > (std::numeric_limits<std::uint64_t>::max)() - nowMilliseconds) {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return nowMilliseconds + durationMilliseconds;
}

void secureZero(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        data[index] = 0U;
    }
    bytes.clear();
}

bool validIdentity(std::string_view identity) noexcept {
    return !identity.empty()
        && identity.size() <= runtimeInternetMaximumIdentityBytes;
}

bool validHostLimits(
    const RuntimeInternetServiceHostLimits& limits
) noexcept {
    return limits.applicationAuthenticationTimeoutMilliseconds > 0U
        && limits.coordinatorIdleTimeoutMilliseconds > 0U
        && limits.relayHeartbeatMilliseconds > 0U
        && limits.pendingRelayTimeoutMilliseconds > 0U
        && limits.maximumQueuedBytes
            >= runtimeInternetServiceMaximumWireFrameBytes
        && limits.maximumQueuedBytes
            <= runtimeInternetServiceMaximumBufferedWireBytes
        && limits.maximumAuthenticatingConnections > 0U
        && limits.maximumAuthenticatingConnections
            <= runtimeInternetCoordinatorHardMaximumConnections
        && limits.maximumTotalConnections > 0U
        && limits.maximumTotalConnections
            <= runtimeInternetCoordinatorHardMaximumConnections
        && limits.maximumAuthenticatingConnections
            <= limits.maximumTotalConnections
        && limits.maximumCoordinatorConnections > 0U
        && limits.maximumCoordinatorConnections
            <= runtimeInternetTlsMaximumConnections
        && limits.maximumCoordinatorConnectionsPerPeer > 0U
        && limits.maximumCoordinatorConnectionsPerPeer
            <= limits.maximumCoordinatorConnections
        && limits.maximumPendingRelayLegs > 0U
        && limits.maximumPendingRelayLegs
            <= runtimeInternetCoordinatorHardMaximumRelayRoutes * 2U
        && limits.maximumActiveRelayPairs > 0U
        && limits.maximumActiveRelayPairs
            <= runtimeInternetCoordinatorHardMaximumRelayRoutes
        && limits.maximumFramesPerConnectionPoll > 0U
        && limits.maximumFramesPerConnectionPoll <= 256U
        && limits.maximumReplayNonces > 0U
        && limits.maximumReplayNonces
            <= runtimeInternetCoordinatorHardMaximumReplayTombstones * 4U
        && limits.maximumReplayNoncesPerPeer > 0U
        && limits.maximumReplayNoncesPerPeer
            < limits.maximumReplayNonces
        && limits.replayNonceRetentionMilliseconds >= 1000U;
}

class VerifiedIdentityStream final
    : public RuntimeInternetAuthenticatedTcpStream {
public:
    VerifiedIdentityStream(
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream,
        RuntimeInternetAuthenticatedTcpStreamIdentity identity
    ) : stream_(std::move(stream)), identity_(std::move(identity)) {}

    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const override {
        return identity_;
    }

    RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t> destination
    ) override {
        if (!stream_) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0U, {}};
        }
        return stream_->read(destination);
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) override {
        if (!stream_) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0U, {}};
        }
        return stream_->write(source);
    }

    void close() noexcept override {
        if (stream_) stream_->close();
        stream_.reset();
    }

private:
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream_;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity_;
};

enum class HostChannelReadKind : std::uint8_t {
    Message,
    WouldBlock,
    Closed,
    Failed
};

struct HostChannelReadResult {
    HostChannelReadKind kind = HostChannelReadKind::WouldBlock;
    RuntimeInternetServiceWireMessage message;
    std::string error;
};

class HostFramedChannel {
public:
    HostFramedChannel(
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream,
        std::size_t maximumQueuedBytes
    ) : stream_(std::move(stream)), maximumQueuedBytes_(maximumQueuedBytes) {}

    ~HostFramedChannel() { close(); }
    HostFramedChannel(HostFramedChannel&&) noexcept = default;
    HostFramedChannel& operator=(HostFramedChannel&&) noexcept = default;
    HostFramedChannel(const HostFramedChannel&) = delete;
    HostFramedChannel& operator=(const HostFramedChannel&) = delete;

    bool queue(
        const RuntimeInternetServiceWireMessage& message,
        std::string& errorMessage
    ) {
        std::vector<std::uint8_t> frame;
        if (!encodeRuntimeInternetServiceWireFrame(
                message,
                frame,
                errorMessage
            )) {
            return false;
        }
        if (!stream_ || frame.size() > maximumQueuedBytes_
            || queuedBytes_ > maximumQueuedBytes_ - frame.size()) {
            errorMessage = "Internet service host send queue is full";
            secureZero(frame);
            return false;
        }
        queuedBytes_ += frame.size();
        output_.push_back(std::move(frame));
        return true;
    }

    bool pumpWrite(bool& progressed, std::string& errorMessage) {
        progressed = false;
        if (!stream_) {
            errorMessage = "Internet service host channel is closed";
            return false;
        }
        for (int iteration = 0; iteration < 64 && !output_.empty();
             ++iteration) {
            std::vector<std::uint8_t>& frame = output_.front();
            const RuntimeInternetTcpStreamIoResult write = stream_->write(
                std::span<const std::uint8_t>(frame).subspan(outputOffset_)
            );
            if (write.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                return true;
            }
            if (write.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                errorMessage = "Internet service host channel closed";
                return false;
            }
            if (write.status == RuntimeInternetTcpStreamIoStatus::Failed) {
                errorMessage = write.error.empty()
                    ? "Internet service host channel write failed"
                    : write.error;
                return false;
            }
            if (write.bytesTransferred == 0U
                || write.bytesTransferred > frame.size() - outputOffset_) {
                errorMessage =
                    "Internet service host channel returned invalid write progress";
                return false;
            }
            outputOffset_ += write.bytesTransferred;
            queuedBytes_ -= write.bytesTransferred;
            progressed = true;
            if (outputOffset_ == frame.size()) {
                secureZero(frame);
                output_.pop_front();
                outputOffset_ = 0U;
            }
        }
        return true;
    }

    HostChannelReadResult readOne() {
        HostChannelReadResult result;
        if (!stream_) {
            result.kind = HostChannelReadKind::Closed;
            result.error = "Internet service host channel is closed";
            return result;
        }
        for (int iteration = 0; iteration < 16; ++iteration) {
            bool available = false;
            std::string decodeError;
            if (!decoder_.pop(result.message, available, decodeError)) {
                result.kind = HostChannelReadKind::Failed;
                result.error = std::move(decodeError);
                return result;
            }
            if (available) {
                result.kind = HostChannelReadKind::Message;
                return result;
            }
            std::array<std::uint8_t, hostReadChunkBytes> bytes{};
            const RuntimeInternetTcpStreamIoResult read = stream_->read(bytes);
            if (read.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                result.kind = HostChannelReadKind::WouldBlock;
                return result;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                result.kind = HostChannelReadKind::Closed;
                result.error = "Internet service host channel closed";
                return result;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Failed) {
                result.kind = HostChannelReadKind::Failed;
                result.error = read.error.empty()
                    ? "Internet service host channel read failed"
                    : read.error;
                return result;
            }
            if (read.bytesTransferred == 0U
                || read.bytesTransferred > bytes.size()) {
                result.kind = HostChannelReadKind::Failed;
                result.error =
                    "Internet service host channel returned invalid read progress";
                return result;
            }
            if (!decoder_.push(
                    std::span<const std::uint8_t>(
                        bytes.data(),
                        read.bytesTransferred
                    ),
                    decodeError
                )
                || decoder_.bufferedBytes() > maximumQueuedBytes_) {
                result.kind = HostChannelReadKind::Failed;
                result.error = decodeError.empty()
                    ? "Internet service host receive queue is full"
                    : std::move(decodeError);
                return result;
            }
        }
        return result;
    }

    bool outputEmpty() const noexcept { return output_.empty(); }

    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> takeStream() {
        if (!stream_ || !output_.empty()) return {};
        // Relay clients cannot legally send opaque bytes before receiving the
        // activation confirmation, so framed bytes left here are a protocol
        // violation rather than relay data that may be silently reinterpreted.
        if (!decoder_.takeBufferedBytes().empty()) {
            stream_->close();
            stream_.reset();
            return {};
        }
        return std::move(stream_);
    }

    void close() noexcept {
        for (std::vector<std::uint8_t>& frame : output_) secureZero(frame);
        output_.clear();
        outputOffset_ = 0U;
        queuedBytes_ = 0U;
        decoder_.reset();
        if (stream_) stream_->close();
        stream_.reset();
    }

private:
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream_;
    std::size_t maximumQueuedBytes_ = 0U;
    std::deque<std::vector<std::uint8_t>> output_;
    std::size_t outputOffset_ = 0U;
    std::size_t queuedBytes_ = 0U;
    RuntimeInternetServiceWireDecoder decoder_;
};

bool complementaryRoutes(
    const RuntimeInternetTcpRouteDescriptor& host,
    const RuntimeInternetTcpRouteDescriptor& remote
) noexcept {
    return host.kind == RuntimeInternetTcpRouteKind::Relay
        && remote.kind == RuntimeInternetTcpRouteKind::Relay
        && host.connectionId == remote.connectionId
        && host.serviceId == remote.serviceId
        && host.roomId == remote.roomId
        && host.localRole == RuntimeInternetRole::Host
        && remote.localRole != RuntimeInternetRole::Host
        && host.localPeerId == remote.remotePeerId
        && host.remotePeerId == remote.localPeerId
        && host.localRole == remote.remoteRole
        && host.remoteRole == remote.localRole
        && host.endpoint == remote.endpoint;
}

} // namespace

struct RuntimeInternetServiceHost::Impl {
    enum class ConnectionStage : std::uint8_t {
        Authenticating,
        Rejecting,
        ConfirmingCoordinator,
        Coordinator,
        WaitingRelayActivation,
        ConfirmingRelay
    };

    struct Connection {
        std::uint64_t tlsConnectionId = 0U;
        std::string peerAddress;
        HostFramedChannel channel;
        ConnectionStage stage = ConnectionStage::Authenticating;
        std::uint64_t deadlineMilliseconds = 0U;
        RuntimeInternetServiceVerifiedIdentity verified;
        RuntimeInternetServiceNonce authenticationNonce{};
        RuntimeInternetTcpRouteDescriptor relayRoute;
        bool relayBoundInCoordinator = false;

        Connection(
            std::uint64_t id,
            std::string address,
            HostFramedChannel framed,
            std::uint64_t deadline
        ) : tlsConnectionId(id),
            peerAddress(std::move(address)),
            channel(std::move(framed)),
            deadlineMilliseconds(deadline) {}

        Connection(Connection&&) noexcept = default;
        Connection& operator=(Connection&&) noexcept = default;
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
    };

    struct RelayRoute {
        std::uint64_t connectionId = 0U;
        std::string roomId;
        std::string hostPeerId;
        std::string remotePeerId;
        RuntimeInternetRole remoteRole = RuntimeInternetRole::Client;
        std::optional<std::uint64_t> hostConnection;
        std::optional<std::uint64_t> remoteConnection;
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> hostStream;
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> remoteStream;
        RuntimeInternetTcpRelayPair pair;
        bool activated = false;
        std::uint64_t deadlineMilliseconds = 0U;
        std::uint64_t nextHeartbeatMilliseconds = 0U;
    };

    RuntimeInternetServiceHostConfig config;
    RuntimeInternetTlsServer tls;
    RuntimeInternetCoordinator coordinator;
    RuntimeInternetMatchmakingService matchmaking;
    RuntimeInternetCommunityService community;
    std::unique_ptr<RuntimeInternetServiceNonceReplayWindow> replay;
    std::map<
        std::string,
        std::unique_ptr<RuntimeInternetServiceNonceReplayWindow>
    > peerReplay;
    std::map<std::uint64_t, Connection> connections;
    std::map<std::uint64_t, RelayRoute> relayRoutes;
    std::set<std::uint64_t> scheduledDisconnects;
    std::map<std::string, std::uint64_t> pendingPeerDisconnects;
    std::function<bool(
        const std::string&,
        const std::string&,
        const std::vector<std::uint8_t>&
    )> externalAdmission;
    bool opened = false;

    std::uint64_t now() const { return config.nowMilliseconds(); }

    void prunePeerReplay(std::uint64_t nowMilliseconds) {
        for (auto iterator = peerReplay.begin();
             iterator != peerReplay.end();) {
            iterator->second->prune(nowMilliseconds);
            if (iterator->second->size() == 0U) {
                iterator = peerReplay.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::size_t connectionCount(ConnectionStage stage) const noexcept {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [stage](const auto& entry) {
                return entry.second.stage == stage;
            }
        ));
    }

    std::size_t coordinatorCount() const noexcept {
        return connectionCount(ConnectionStage::ConfirmingCoordinator)
            + connectionCount(ConnectionStage::Coordinator);
    }

    std::size_t coordinatorCountForPeer(
        std::string_view peerId
    ) const noexcept {
        return static_cast<std::size_t>(std::count_if(
            connections.begin(),
            connections.end(),
            [&](const auto& entry) {
                return (entry.second.stage
                            == ConnectionStage::ConfirmingCoordinator
                        || entry.second.stage
                            == ConnectionStage::Coordinator)
                    && entry.second.verified.peerId == peerId;
            }
        ));
    }

    static bool isCoordinatorStage(ConnectionStage stage) noexcept {
        return stage == ConnectionStage::ConfirmingCoordinator
            || stage == ConnectionStage::Coordinator;
    }

    bool disconnectPeerNow(const std::string& authenticatedPeerId, std::string& errorMessage) {
            std::vector<RuntimeInternetCoordinatorAction> actions;
            if (!coordinator.authenticatedPeerDisconnected(
                    config.serviceId,
                    authenticatedPeerId,
                    actions,
                    errorMessage
                )
                || !applyCoordinatorActions(
                    std::move(actions),
                    errorMessage
                )) {
                return false;
            }
            const RuntimeInternetMatchmakingOperationResult removed =
                matchmaking.authenticatedPeerDisconnected(
                    config.serviceId,
                    authenticatedPeerId
                );
            if (!removed.accepted) {
                errorMessage = removed.reason.empty()
                    ? "matchmaking disconnect cleanup failed"
                    : removed.reason;
                return false;
            }
            std::vector<RuntimeInternetCommunityServiceAction>
                communityActions;
            // Closing the TLS coordinator is also the normal menu logout and
            // must not inflate the WOL profile's gameplay-disconnect count.
            if (!community.authenticatedPeerDisconnected(
                    config.serviceId,
                    authenticatedPeerId,
                    false,
                    communityActions,
                    errorMessage
                )
                || !applyCommunityActions(
                    std::move(communityActions),
                    errorMessage
                )) {
                return false;
            }
        return true;
    }

    bool eraseApplicationConnection(
        std::uint64_t connectionId,
        std::string& errorMessage
    ) {
        const auto found = connections.find(connectionId);
        if (found == connections.end()) return true;
        const bool coordinatorConnection =
            isCoordinatorStage(found->second.stage);
        std::string authenticatedPeerId;
        authenticatedPeerId = found->second.verified.peerId;
        const bool finalCoordinatorConnection = coordinatorConnection
            && !authenticatedPeerId.empty()
            && coordinatorCountForPeer(authenticatedPeerId) == 1U;
        if (finalCoordinatorConnection) {
            const auto retainedUntil = coordinator.retainRunningPeerForReconnect(config.serviceId, authenticatedPeerId);
            if (retainedUntil != 0) {
                pendingPeerDisconnects.try_emplace(authenticatedPeerId, retainedUntil);
            } else if (!disconnectPeerNow(authenticatedPeerId, errorMessage)) return false;
        }
        found->second.channel.close();
        connections.erase(found);
        errorMessage.clear();
        return true;
    }

    std::size_t pendingRelayCount() const noexcept {
        return connectionCount(ConnectionStage::WaitingRelayActivation)
            + connectionCount(ConnectionStage::ConfirmingRelay);
    }

    std::size_t activePairCount() const noexcept {
        return static_cast<std::size_t>(std::count_if(
            relayRoutes.begin(),
            relayRoutes.end(),
            [](const auto& entry) { return entry.second.pair.isOpen(); }
        ));
    }

    std::size_t totalApplicationConnectionCount() const noexcept {
        std::size_t count = connections.size();
        for (const auto& entry : relayRoutes) {
            if (entry.second.pair.isOpen()) {
                count += 2U;
            } else {
                count += entry.second.hostStream ? 1U : 0U;
                count += entry.second.remoteStream ? 1U : 0U;
            }
        }
        return count;
    }

    bool queueMessage(
        Connection& connection,
        RuntimeInternetServiceWireBody body,
        std::string& errorMessage
    ) {
        RuntimeInternetServiceWireMessage message;
        message.body = std::move(body);
        return connection.channel.queue(message, errorMessage);
    }

    bool queueCoordinatorPacket(
        std::string_view targetPeerId,
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) {
        for (auto& entry : connections) {
            Connection& connection = entry.second;
            if ((connection.stage == ConnectionStage::Coordinator
                    || connection.stage
                        == ConnectionStage::ConfirmingCoordinator)
                && connection.verified.peerId == targetPeerId) {
                RuntimeInternetServiceCoordinatorPacket envelope;
                envelope.packet = packet;
                if (queueMessage(
                        connection,
                        std::move(envelope),
                        errorMessage
                    )) {
                    return true;
                }
                // A slow peer may exhaust only its own bounded send queue; it
                // must not turn a local backpressure condition into a service-
                // wide failure or block delivery to another authenticated
                // connection for the same account.
                connection.channel.close();
                scheduledDisconnects.insert(entry.first);
                errorMessage.clear();
            }
        }
        // A service action for an offline peer is intentionally dropped. Its
        // authoritative lease remains bounded and the client may retry.
        errorMessage.clear();
        return true;
    }

    void closeRelayRoute(std::uint64_t connectionId) noexcept {
        const auto found = relayRoutes.find(connectionId);
        if (found == relayRoutes.end()) return;
        RelayRoute& route = found->second;
        route.pair.close();
        if (route.hostStream) route.hostStream->close();
        if (route.remoteStream) route.remoteStream->close();
        const std::optional<std::uint64_t> host = route.hostConnection;
        const std::optional<std::uint64_t> remote = route.remoteConnection;
        relayRoutes.erase(found);
        if (host.has_value()) {
            const auto connection = connections.find(*host);
            if (connection != connections.end()) {
                connection->second.channel.close();
                connections.erase(connection);
            }
        }
        if (remote.has_value() && remote != host) {
            const auto connection = connections.find(*remote);
            if (connection != connections.end()) {
                connection->second.channel.close();
                connections.erase(connection);
            }
        }
    }

    void revokeRelayRoute(
        std::uint64_t connectionId,
        std::string_view authenticatedPeerId
    ) noexcept {
        std::vector<RuntimeInternetCoordinatorAction> actions;
        std::string ignored;
        (void)coordinator.relayChannelClosed(
            connectionId,
            RuntimeInternetRelayTransport::Tcp,
            config.serviceId,
            std::string(authenticatedPeerId),
            actions,
            ignored
        );
        (void)applyCoordinatorActions(std::move(actions), ignored);
        closeRelayRoute(connectionId);
    }

    bool activateRoute(
        const RuntimeInternetCoordinatorAction& action,
        std::string& errorMessage
    ) {
        if (action.relayTransport != RuntimeInternetRelayTransport::Tcp) {
            errorMessage = "self-hosted service does not implement UDP relay";
            return false;
        }
        const auto found = relayRoutes.find(action.connectionId);
        if (found == relayRoutes.end()) {
            errorMessage = "coordinator activated an unknown TCP relay route";
            return false;
        }
        RelayRoute& route = found->second;
        if (route.roomId != action.roomId
            || route.hostPeerId != action.hostPeerId
            || route.remotePeerId != action.remotePeerId
            || route.remoteRole != action.remoteRole
            || !route.hostConnection.has_value()
            || !route.remoteConnection.has_value()) {
            errorMessage = "coordinator activation did not match both relay legs";
            return false;
        }
        if (activePairCount() >= config.limits.maximumActiveRelayPairs) {
            errorMessage = "active TCP relay pair capacity reached";
            return false;
        }
        route.activated = true;
        route.nextHeartbeatMilliseconds = deadlineAfter(
            now(),
            config.limits.relayHeartbeatMilliseconds
        );
        for (const std::uint64_t connectionId : {
                 *route.hostConnection,
                 *route.remoteConnection
             }) {
            const auto connection = connections.find(connectionId);
            if (connection == connections.end()
                || connection->second.stage
                    != ConnectionStage::WaitingRelayActivation) {
                errorMessage = "activated TCP relay leg is unavailable";
                return false;
            }
            RuntimeInternetServiceAuthenticationConfirmation confirmation;
            confirmation.channel = RuntimeInternetServiceWireChannel::RelayTcp;
            confirmation.nonce = RuntimeInternetServiceNonce{};
            // The request nonce was intentionally retained only in the
            // connection's confirmation value below.
            confirmation.serviceId = connection->second.verified.serviceId;
            confirmation.peerId = connection->second.verified.peerId;
            confirmation.route = connection->second.relayRoute;
            confirmation.nonce = connection->second.authenticationNonce;
            if (!queueMessage(
                    connection->second,
                    std::move(confirmation),
                    errorMessage
                )) {
                return false;
            }
            connection->second.stage = ConnectionStage::ConfirmingRelay;
        }
        return true;
    }

    bool applyCoordinatorActions(
        std::vector<RuntimeInternetCoordinatorAction> actions,
        std::string& errorMessage
    ) {
        for (const RuntimeInternetCoordinatorAction& action : actions) {
            switch (action.kind) {
            case RuntimeInternetCoordinatorActionKind::SendCoordinator: {
                RuntimeInternetControlMessage control;
                if (!decodeRuntimeInternetControlMessage(action.packet, control, errorMessage)) return false;
                if (control.type == RuntimeInternetControlType::RoomSealed) {
                    std::vector<RuntimeInternetCommunityServiceAction> rankedActions;
                    if (!community.rankedRoomStarted(control.roomId, rankedActions, errorMessage)
                        || !applyCommunityActions(std::move(rankedActions), errorMessage)) return false;
                }
                if (!queueCoordinatorPacket(
                        action.targetPeerId,
                        action.packet,
                        errorMessage
                    )) {
                    return false;
                }
                break;
            }
            case RuntimeInternetCoordinatorActionKind::SendRelay:
                if (action.relayTransport
                    != RuntimeInternetRelayTransport::Tcp) {
                    errorMessage =
                        "self-hosted service rejected a UDP relay action";
                    return false;
                }
                // RelayBound is represented by the delayed, route-exact
                // application confirmation. The production client converts
                // that confirmation into its RelayPacket event before it
                // exposes the opaque stream.
                break;
            case RuntimeInternetCoordinatorActionKind::ActivateRelayRoute:
                if (!activateRoute(action, errorMessage)) return false;
                break;
            case RuntimeInternetCoordinatorActionKind::DeactivateRelayRoute:
                if (action.relayTransport
                    == RuntimeInternetRelayTransport::Tcp) {
                    closeRelayRoute(action.connectionId);
                }
                break;
            }
        }
        return true;
    }

    bool applyMatchmakingActions(
        std::vector<RuntimeInternetMatchmakingServiceAction> actions,
        std::string& errorMessage
    ) {
        for (const RuntimeInternetMatchmakingServiceAction& action : actions) {
            RuntimeInternetMatchmakingMessage message;
            std::string decodeError;
            if (!decodeRuntimeInternetMatchmakingMessage(
                    action.packet,
                    message,
                    decodeError
                )) {
                errorMessage = std::move(decodeError);
                return false;
            }
            if (message.type
                == RuntimeInternetMatchmakingMessageType::Assignment) {
                const auto* assignment = std::get_if<
                    RuntimeInternetMatchmakingAssignment
                >(&message.body);
                if (assignment == nullptr
                    || !community.authorizeQuickMatch(
                        assignment->matchId,
                        assignment->peerIds,
                        errorMessage,
                        assignment->roomId
                    )) {
                    if (errorMessage.empty()) {
                        errorMessage =
                            "matchmaking assignment omitted community ladder authority";
                    }
                    return false;
                }
            }
            if (!queueCoordinatorPacket(
                    action.targetPeerId,
                    action.packet,
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    bool applyCommunityActions(
        std::vector<RuntimeInternetCommunityServiceAction> actions,
        std::string& errorMessage
    ) {
        for (const RuntimeInternetCommunityServiceAction& action : actions) {
            if (!queueCoordinatorPacket(
                    action.targetPeerId,
                    action.packet,
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    bool rejectAuthentication(
        Connection& connection,
        RuntimeInternetServiceWireChannel channel,
        const RuntimeInternetServiceNonce& nonce,
        RuntimeInternetServiceHostPollResult& result
    ) {
        RuntimeInternetServiceAuthenticationRejection rejection;
        rejection.channel = channel;
        rejection.nonce = nonce;
        std::string ignored;
        if (!queueMessage(connection, std::move(rejection), ignored)) {
            return false;
        }
        connection.stage = ConnectionStage::Rejecting;
        ++result.rejectedConnections;
        return true;
    }

    bool validRelayRequest(
        const RuntimeInternetServiceAuthenticationRequest& request,
        const RuntimeInternetServiceVerifiedIdentity& verified,
        std::string& errorMessage
    ) const {
        const RuntimeInternetTcpRouteDescriptor& route = request.route;
        if (route.kind != RuntimeInternetTcpRouteKind::Relay
            || route.connectionId == 0U
            || route.serviceId != verified.serviceId
            || route.localPeerId != verified.peerId
            || route.endpoint.port != tls.port()
            || route.endpoint.address != config.coordinator.tcpRelayEndpoint.address) {
            errorMessage = "relay route does not match authenticated identity or listener";
            return false;
        }
        RuntimeInternetControlMessage bind;
        if (!decodeRuntimeInternetControlMessage(
                request.relayBindPacket,
                bind,
                errorMessage
            )
            || bind.type != RuntimeInternetControlType::RelayBind
            || bind.transactionId != route.connectionId
            || bind.serviceId != verified.serviceId
            || bind.roomId != route.roomId
            || bind.senderPeerId != verified.peerId
            || bind.targetPeerId != verified.serviceId) {
            errorMessage = "relay bind does not match authenticated route";
            return false;
        }
        const auto* body = std::get_if<RuntimeInternetRelayBindBody>(
            &bind.body
        );
        if (body == nullptr || body->connectionId != route.connectionId
            || body->transport != RuntimeInternetRelayTransport::Tcp) {
            errorMessage = "only an exact TCP relay bind is accepted";
            return false;
        }
        return true;
    }

    bool reserveRelayLeg(
        Connection& connection,
        const RuntimeInternetServiceAuthenticationRequest& request,
        std::vector<RuntimeInternetCoordinatorAction>& actions,
        std::string& errorMessage
    ) {
        if (pendingRelayCount() >= config.limits.maximumPendingRelayLegs) {
            errorMessage = "pending TCP relay leg capacity reached";
            return false;
        }
        const RuntimeInternetTcpRouteDescriptor& descriptor = request.route;
        const bool hostSide =
            descriptor.localRole == RuntimeInternetRole::Host;
        if (!hostSide && descriptor.localRole != RuntimeInternetRole::Client
            && descriptor.localRole != RuntimeInternetRole::Spectator) {
            errorMessage = "TCP relay route role is invalid";
            return false;
        }
        auto routeIterator = relayRoutes.find(descriptor.connectionId);
        bool createdRoute = false;
        if (routeIterator == relayRoutes.end()) {
            RelayRoute route;
            route.connectionId = descriptor.connectionId;
            route.roomId = descriptor.roomId;
            route.hostPeerId = hostSide
                ? descriptor.localPeerId
                : descriptor.remotePeerId;
            route.remotePeerId = hostSide
                ? descriptor.remotePeerId
                : descriptor.localPeerId;
            route.remoteRole = hostSide
                ? descriptor.remoteRole
                : descriptor.localRole;
            route.deadlineMilliseconds = deadlineAfter(
                now(),
                config.limits.pendingRelayTimeoutMilliseconds
            );
            routeIterator = relayRoutes.emplace(
                descriptor.connectionId,
                std::move(route)
            ).first;
            createdRoute = true;
        }
        RelayRoute& route = routeIterator->second;
        if (route.activated) {
            errorMessage = "TCP relay route is already activated";
            return false;
        }
        if ((hostSide && route.hostConnection.has_value())
            || (!hostSide && route.remoteConnection.has_value())) {
            errorMessage = "duplicate TCP relay leg was rejected";
            return false;
        }
        const auto oppositeId = hostSide
            ? route.remoteConnection
            : route.hostConnection;
        if (oppositeId.has_value()) {
            const auto opposite = connections.find(*oppositeId);
            if (opposite == connections.end()) {
                errorMessage = "opposite TCP relay leg disappeared";
                return false;
            }
            const RuntimeInternetTcpRouteDescriptor& hostRoute = hostSide
                ? descriptor
                : opposite->second.relayRoute;
            const RuntimeInternetTcpRouteDescriptor& remoteRoute = hostSide
                ? opposite->second.relayRoute
                : descriptor;
            if (!complementaryRoutes(hostRoute, remoteRoute)) {
                errorMessage = "TCP relay legs are not complementary";
                return false;
            }
            if (activePairCount()
                >= config.limits.maximumActiveRelayPairs) {
                errorMessage = "active TCP relay pair capacity reached";
                return false;
            }
        }

        RuntimeInternetServiceEnvelope envelope;
        envelope.channel = RuntimeInternetServiceChannel::RelayTcp;
        envelope.authenticatedServiceId = connection.verified.serviceId;
        envelope.authenticatedPeerId = connection.verified.peerId;
        envelope.packet = request.relayBindPacket;
        if (!coordinator.receive(envelope, actions, errorMessage)) {
            if (createdRoute) relayRoutes.erase(routeIterator);
            return false;
        }
        connection.relayRoute = descriptor;
        connection.relayBoundInCoordinator = true;
        connection.deadlineMilliseconds = route.deadlineMilliseconds;
        connection.stage = ConnectionStage::WaitingRelayActivation;
        if (hostSide) {
            route.hostConnection = connection.tlsConnectionId;
        } else {
            route.remoteConnection = connection.tlsConnectionId;
        }
        return true;
    }

    bool authenticate(
        Connection& connection,
        RuntimeInternetServiceHostPollResult& result,
        std::string& fatalError
    ) {
        HostChannelReadResult read = connection.channel.readOne();
        if (read.kind == HostChannelReadKind::WouldBlock) return true;
        if (read.kind == HostChannelReadKind::Closed
            || read.kind == HostChannelReadKind::Failed) {
            return false;
        }
        auto* request = std::get_if<
            RuntimeInternetServiceAuthenticationRequest
        >(&read.message.body);
        if (request == nullptr) return false;

        RuntimeInternetServiceVerifiedIdentity verified;
        RuntimeInternetServiceCredentialRequest verification;
        verification.channel = request->channel;
        verification.nonce = request->nonce;
        verification.peerAddress = connection.peerAddress;
        verification.claimedServiceId = request->serviceId;
        verification.claimedPeerId = request->peerId;
        verification.credential = request->credential;
        std::string authenticationError;
        const bool credentialAccepted = config.verifyCredential(
            verification,
            verified,
            authenticationError
        );
        secureZero(request->credential);
        const bool identityAccepted = credentialAccepted
            && validIdentity(verified.serviceId)
            && validIdentity(verified.peerId)
            && verified.serviceId == config.serviceId
            && request->serviceId == verified.serviceId
            && request->peerId == verified.peerId
            && verified.peerId != verified.serviceId;
        if (!identityAccepted) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }

        // Reject requests that can never be admitted before reserving a
        // replay nonce. Otherwise one valid credential could exhaust the
        // global window using requests guaranteed to fail channel or quota
        // checks (for example a second coordinator leg).
        const bool coordinatorRequest = request->channel
            == RuntimeInternetServiceWireChannel::Coordinator;
        const bool relayRequest = request->channel
            == RuntimeInternetServiceWireChannel::RelayTcp;
        std::vector<std::uint64_t> staleCoordinators;
        if (coordinatorRequest && coordinator.hasPendingReconnect(config.serviceId, verified.peerId)) {
            for (const auto& [id, existing] : connections) {
                if (existing.stage == ConnectionStage::Coordinator && existing.verified.peerId == verified.peerId
                    && existing.deadlineMilliseconds >= config.limits.coordinatorIdleTimeoutMilliseconds
                    && now() >= existing.deadlineMilliseconds - config.limits.coordinatorIdleTimeoutMilliseconds + 5000U)
                    staleCoordinators.push_back(id);
            }
        }
        const bool coordinatorPreflight = coordinatorRequest
            && coordinatorCount() - staleCoordinators.size()
                < config.limits.maximumCoordinatorConnections
            && coordinatorCountForPeer(verified.peerId) - staleCoordinators.size()
                < config.limits.maximumCoordinatorConnectionsPerPeer;
        const bool relayPreflight = relayRequest
            && pendingRelayCount()
                < config.limits.maximumPendingRelayLegs
            && validRelayRequest(*request, verified, authenticationError);
        if (!coordinatorPreflight && !relayPreflight) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }

        const std::uint64_t authenticationNow = now();
        prunePeerReplay(authenticationNow);
        auto peerReplayIterator = peerReplay.find(verified.peerId);
        if (peerReplayIterator == peerReplay.end()) {
            peerReplayIterator = peerReplay.emplace(
                verified.peerId,
                std::make_unique<
                    RuntimeInternetServiceNonceReplayWindow
                >(
                    config.limits.maximumReplayNoncesPerPeer,
                    config.limits.replayNonceRetentionMilliseconds
                )
            ).first;
        }
        RuntimeInternetServiceNonceReplayWindow& peerWindow =
            *peerReplayIterator->second;
        if (peerWindow.size()
            >= config.limits.maximumReplayNoncesPerPeer) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }
        std::string replayError;
        const bool nonceAccepted = replay
            && replay->accept(
                request->nonce,
                authenticationNow,
                replayError
            )
            && peerWindow.accept(
                request->nonce,
                authenticationNow,
                replayError
            );
        if (!nonceAccepted) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }

        // Only a fresh, verified account authentication may replace an old
        // unresponsive channel, and only while its combatant route is held
        // for reconnect. Healthy duplicate logins retain the normal quota.
        for (const auto id : staleCoordinators)
            if (!eraseApplicationConnection(id, fatalError)) return false;

        connection.verified = std::move(verified);
        connection.authenticationNonce = request->nonce;
        if (request->channel
            == RuntimeInternetServiceWireChannel::Coordinator) {
            RuntimeInternetServiceAuthenticationConfirmation confirmation;
            confirmation.channel = request->channel;
            confirmation.nonce = request->nonce;
            confirmation.serviceId = connection.verified.serviceId;
            confirmation.peerId = connection.verified.peerId;
            if (!queueMessage(
                    connection,
                    std::move(confirmation),
                    fatalError
                )) {
                return false;
            }
            connection.stage = ConnectionStage::ConfirmingCoordinator;
            return true;
        }

        if (request->channel != RuntimeInternetServiceWireChannel::RelayTcp
            || !validRelayRequest(*request, connection.verified, authenticationError)) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }
        std::vector<RuntimeInternetCoordinatorAction> actions;
        if (!reserveRelayLeg(
                connection,
                *request,
                actions,
                authenticationError
            )) {
            return rejectAuthentication(
                connection,
                request->channel,
                request->nonce,
                result
            );
        }
        return applyCoordinatorActions(std::move(actions), fatalError);
    }

    bool receiveCoordinatorPacket(
        Connection& connection,
        const RuntimeInternetServiceCoordinatorPacket& packet,
        std::string& errorMessage
    ) {
        if (packet.packet.type == runtimeInternetCommunityPacketType) {
            if (!packet.roomAdmissionProof.empty()) {
                errorMessage =
                    "community packet cannot carry room admission proof";
                return false;
            }
            RuntimeInternetCommunityServiceEnvelope envelope;
            envelope.authenticatedServiceId = connection.verified.serviceId;
            envelope.authenticatedPeerId = connection.verified.peerId;
            envelope.packet = packet.packet;
            std::vector<RuntimeInternetCommunityServiceAction> actions;
            if (!community.receive(envelope, actions, errorMessage)) {
                return false;
            }
            return applyCommunityActions(
                std::move(actions),
                errorMessage
            );
        }
        if (packet.packet.type == runtimeInternetMatchmakingPacketType) {
            if (!packet.roomAdmissionProof.empty()) {
                errorMessage =
                    "matchmaking packet cannot carry room admission proof";
                return false;
            }
            RuntimeInternetMatchmakingServiceEnvelope envelope;
            envelope.authenticatedServiceId = connection.verified.serviceId;
            envelope.authenticatedPeerId = connection.verified.peerId;
            envelope.packet = packet.packet;
            std::vector<RuntimeInternetMatchmakingServiceAction> actions;
            if (!matchmaking.receive(envelope, actions, errorMessage)) {
                return false;
            }
            return applyMatchmakingActions(
                std::move(actions),
                errorMessage
            );
        }

        RuntimeInternetControlMessage control;
        std::string decodeError;
        if (decodeRuntimeInternetControlMessage(
                packet.packet,
                control,
                decodeError
            )) {
            if (control.type == RuntimeInternetControlType::RelayRequest) {
                const auto* relay = std::get_if<RuntimeInternetRelayRequestBody>(
                    &control.body
                );
                if (relay != nullptr
                    && relay->transport != RuntimeInternetRelayTransport::Tcp) {
                    errorMessage =
                        "self-hosted service does not implement UDP relay";
                    return false;
                }
            }
        }
        RuntimeInternetServiceEnvelope envelope;
        envelope.channel = RuntimeInternetServiceChannel::CoordinatorTls;
        envelope.authenticatedServiceId = connection.verified.serviceId;
        envelope.authenticatedPeerId = connection.verified.peerId;
        envelope.roomAdmissionProof = packet.roomAdmissionProof;
        envelope.packet = packet.packet;
        std::vector<RuntimeInternetCoordinatorAction> actions;
        if (!coordinator.receive(envelope, actions, errorMessage)) {
            return false;
        }
        return applyCoordinatorActions(std::move(actions), errorMessage);
    }

    bool completeRelayConnection(
        std::uint64_t tlsConnectionId,
        std::string& errorMessage
    ) {
        const auto found = connections.find(tlsConnectionId);
        if (found == connections.end()) return true;
        Connection& connection = found->second;
        auto rawStream = connection.channel.takeStream();
        if (!rawStream) {
            errorMessage =
                "activated relay stream retained unexpected framed data";
            return false;
        }
        RuntimeInternetAuthenticatedTcpStreamIdentity identity;
        identity.authenticatedServiceId = connection.verified.serviceId;
        identity.authenticatedPeerId = connection.verified.peerId;
        identity.route = connection.relayRoute;
        auto verifiedStream = std::make_unique<VerifiedIdentityStream>(
            std::move(rawStream),
            std::move(identity)
        );
        const std::uint64_t routeId = connection.relayRoute.connectionId;
        const bool hostSide = connection.relayRoute.localRole
            == RuntimeInternetRole::Host;
        connections.erase(found);
        const auto routeIterator = relayRoutes.find(routeId);
        if (routeIterator == relayRoutes.end()
            || !routeIterator->second.activated) {
            verifiedStream->close();
            errorMessage = "activated relay route disappeared";
            return false;
        }
        RelayRoute& route = routeIterator->second;
        if (hostSide) {
            route.hostConnection.reset();
            route.hostStream = std::move(verifiedStream);
        } else {
            route.remoteConnection.reset();
            route.remoteStream = std::move(verifiedStream);
        }
        if (route.hostStream && route.remoteStream) {
            RuntimeInternetTcpTunnelProxyConfig relayConfig = config.relay;
            relayConfig.maximumQueuedBytes = std::min(
                relayConfig.maximumQueuedBytes,
                config.limits.maximumQueuedBytes
            );
            relayConfig.idleTimeoutMilliseconds =
                config.limits.relayPairIdleTimeoutMilliseconds;
            if (!route.pair.open(
                    relayConfig,
                    std::move(route.hostStream),
                    std::move(route.remoteStream),
                    now(),
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    RuntimeInternetServiceHostPollResult pollConnections() {
        RuntimeInternetServiceHostPollResult result;
        const std::uint64_t current = now();
        const std::vector<RuntimeInternetTlsServerEvent> tlsEvents =
            tls.poll(current);
        for (const RuntimeInternetTlsServerEvent& event : tlsEvents) {
            result.progressed = true;
            if (event.kind
                == RuntimeInternetTlsServerEventKind::HandshakeComplete) {
                const std::size_t authenticating =
                    connectionCount(ConnectionStage::Authenticating)
                    + connectionCount(ConnectionStage::Rejecting);
                if (authenticating
                        >= config.limits.maximumAuthenticatingConnections
                    || totalApplicationConnectionCount()
                        >= config.limits.maximumTotalConnections) {
                    (void)tls.disconnect(event.connectionId);
                    ++result.rejectedConnections;
                    continue;
                }
                RuntimeInternetAuthenticatedTcpStreamIdentity quarantine;
                quarantine.authenticatedServiceId = config.serviceId;
                quarantine.authenticatedPeerId =
                    "pending-" + std::to_string(event.connectionId);
                std::string error;
                auto stream = tls.takeApplicationAuthenticatedStream(
                    event.connectionId,
                    std::move(quarantine),
                    error
                );
                if (!stream) {
                    result.succeeded = false;
                    result.error = std::move(error);
                    return result;
                }
                HostFramedChannel channel(
                    std::move(stream),
                    config.limits.maximumQueuedBytes
                );
                connections.emplace(
                    event.connectionId,
                    Connection{
                        event.connectionId,
                        event.peerAddress,
                        std::move(channel),
                        deadlineAfter(
                            current,
                            config.limits
                                .applicationAuthenticationTimeoutMilliseconds
                        )
                    }
                );
            } else if (event.kind
                == RuntimeInternetTlsServerEventKind::Rejected) {
                ++result.rejectedConnections;
            } else {
                ++result.closedConnections;
            }
        }

        std::vector<std::uint64_t> ids;
        ids.reserve(connections.size());
        for (const auto& entry : connections) ids.push_back(entry.first);
        for (const std::uint64_t id : ids) {
            auto found = connections.find(id);
            if (found == connections.end()) continue;
            Connection& connection = found->second;
            bool wrote = false;
            std::string error;
            if (!connection.channel.pumpWrite(wrote, error)) {
                const bool notify = connection.relayBoundInCoordinator;
                const std::uint64_t routeId =
                    connection.relayRoute.connectionId;
                const std::string peerId = connection.verified.peerId;
                if (!eraseApplicationConnection(id, error)) {
                    result.succeeded = false;
                    result.error = std::move(error);
                    return result;
                }
                ++result.closedConnections;
                result.progressed = true;
                if (notify) revokeRelayRoute(routeId, peerId);
                continue;
            }
            result.progressed = result.progressed || wrote;
            if ((connection.stage == ConnectionStage::Authenticating
                    || connection.stage
                        == ConnectionStage::ConfirmingCoordinator
                    || connection.stage == ConnectionStage::Coordinator
                    || connection.stage
                        == ConnectionStage::WaitingRelayActivation
                    || connection.stage == ConnectionStage::Rejecting)
                && current >= connection.deadlineMilliseconds) {
                const bool notify = connection.relayBoundInCoordinator;
                const std::uint64_t routeId =
                    connection.relayRoute.connectionId;
                const std::string peerId = connection.verified.peerId;
                if (!eraseApplicationConnection(id, error)) {
                    result.succeeded = false;
                    result.error = std::move(error);
                    return result;
                }
                ++result.closedConnections;
                result.progressed = true;
                if (notify) revokeRelayRoute(routeId, peerId);
                continue;
            }
            if (connection.stage == ConnectionStage::Rejecting) {
                if (connection.channel.outputEmpty()) {
                    if (!eraseApplicationConnection(id, error)) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    ++result.closedConnections;
                }
                continue;
            }
            if (connection.stage == ConnectionStage::Authenticating) {
                if (!authenticate(connection, result, error)) {
                    if (!eraseApplicationConnection(id, error)) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    ++result.closedConnections;
                    if (!error.empty()) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                } else if (!error.empty()) {
                    result.succeeded = false;
                    result.error = std::move(error);
                    return result;
                } else {
                    result.progressed = true;
                }
                continue;
            }
            if (connection.stage
                == ConnectionStage::ConfirmingCoordinator) {
                if (connection.channel.outputEmpty()) {
                    connection.stage = ConnectionStage::Coordinator;
                    pendingPeerDisconnects.erase(connection.verified.peerId);
                    connection.deadlineMilliseconds = deadlineAfter(
                        current,
                        config.limits.coordinatorIdleTimeoutMilliseconds
                    );
                    std::vector<RuntimeInternetCommunityServiceAction>
                        communityActions;
                    if (!community.authenticatedPeerConnected(
                            config.serviceId,
                            connection.verified.peerId,
                            communityActions,
                            error
                        )
                        || !applyCommunityActions(
                            std::move(communityActions),
                            error
                        )) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    result.progressed = true;
                }
                continue;
            }
            if (connection.stage == ConnectionStage::ConfirmingRelay) {
                if (connection.channel.outputEmpty()) {
                    if (!completeRelayConnection(id, error)) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    result.progressed = true;
                }
                continue;
            }
            if (connection.stage
                == ConnectionStage::WaitingRelayActivation) {
                HostChannelReadResult read = connection.channel.readOne();
                if (read.kind == HostChannelReadKind::Closed
                    || read.kind == HostChannelReadKind::Failed
                    || read.kind == HostChannelReadKind::Message) {
                    const std::uint64_t routeId =
                        connection.relayRoute.connectionId;
                    const std::string peerId = connection.verified.peerId;
                    revokeRelayRoute(routeId, peerId);
                    ++result.closedConnections;
                }
                continue;
            }
            for (std::size_t frame = 0U;
                 frame < config.limits.maximumFramesPerConnectionPoll;
                 ++frame) {
                HostChannelReadResult read = connection.channel.readOne();
                if (read.kind == HostChannelReadKind::WouldBlock) break;
                if (read.kind == HostChannelReadKind::Closed
                    || read.kind == HostChannelReadKind::Failed) {
                    if (!eraseApplicationConnection(id, error)) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    ++result.closedConnections;
                    result.progressed = true;
                    break;
                }
                const auto* packet = std::get_if<
                    RuntimeInternetServiceCoordinatorPacket
                >(&read.message.body);
                if (packet == nullptr
                    || !receiveCoordinatorPacket(
                        connection,
                        *packet,
                        error
                    )) {
                    if (!eraseApplicationConnection(id, error)) {
                        result.succeeded = false;
                        result.error = std::move(error);
                        return result;
                    }
                    ++result.rejectedConnections;
                    result.progressed = true;
                    break;
                }
                connection.deadlineMilliseconds = deadlineAfter(
                    current,
                    config.limits.coordinatorIdleTimeoutMilliseconds
                );
                result.progressed = true;
            }
        }
        std::string scheduledDisconnectError;
        for (const std::uint64_t id : scheduledDisconnects) {
            const auto found = connections.find(id);
            if (found != connections.end()) {
                if (!eraseApplicationConnection(
                        id,
                        scheduledDisconnectError
                    )) {
                    result.succeeded = false;
                    result.error = std::move(scheduledDisconnectError);
                    return result;
                }
                ++result.closedConnections;
                result.progressed = true;
            }
        }
        scheduledDisconnects.clear();
        return result;
    }

    bool pollAuthorities(
        RuntimeInternetServiceHostPollResult& result
    ) {
        std::string error;
        for (auto pending = pendingPeerDisconnects.begin(); pending != pendingPeerDisconnects.end();) {
            if (now() < pending->second) { ++pending; continue; }
            const auto peerId = pending->first;
            pending = pendingPeerDisconnects.erase(pending);
            if (coordinatorCountForPeer(peerId) == 0 && !disconnectPeerNow(peerId, error)) {
                result.succeeded = false;result.error = std::move(error);return false;
            }
        }
        if (!applyMatchmakingActions(matchmaking.poll(error), error)) {
            result.succeeded = false;
            result.error = std::move(error);
            return false;
        }
        if (!error.empty()) {
            result.succeeded = false;
            result.error = std::move(error);
            return false;
        }
        if (!applyCoordinatorActions(coordinator.poll(), error)) {
            result.succeeded = false;
            result.error = std::move(error);
            return false;
        }
        if (replay) replay->prune(now());
        std::vector<RuntimeInternetCommunityServiceAction> rankedActions;
        if (!community.poll(rankedActions, error)
            || !applyCommunityActions(std::move(rankedActions), error)) {
            result.succeeded = false;
            result.error = std::move(error);
            return false;
        }
        return true;
    }

    bool pollRelayPairs(RuntimeInternetServiceHostPollResult& result) {
        const std::uint64_t current = now();
        std::vector<std::uint64_t> routeIds;
        routeIds.reserve(relayRoutes.size());
        for (const auto& entry : relayRoutes) {
            routeIds.push_back(entry.first);
        }
        for (const std::uint64_t routeId : routeIds) {
            auto found = relayRoutes.find(routeId);
            if (found == relayRoutes.end()) continue;
            RelayRoute& route = found->second;
            if (!route.pair.isOpen()) continue;
            RuntimeInternetTcpRelayPairPollResult pair =
                route.pair.poll(current);
            result.progressed = result.progressed
                || pair.bytesHostToRemote > 0U
                || pair.bytesRemoteToHost > 0U;
            if (!pair.succeeded || pair.closed) {
                const std::string peerId = route.hostPeerId;
                revokeRelayRoute(routeId, peerId);
                ++result.closedConnections;
                continue;
            }
            if (current >= route.nextHeartbeatMilliseconds) {
                const std::string hostPeerId = route.hostPeerId;
                const std::string remotePeerId = route.remotePeerId;
                const std::uint64_t connectionId = route.connectionId;
                std::vector<RuntimeInternetCoordinatorAction> actions;
                std::string error;
                const bool hostTouched = coordinator.touchRelayRoute(
                    connectionId,
                    RuntimeInternetRelayTransport::Tcp,
                    config.serviceId,
                    hostPeerId,
                    actions,
                    error
                );
                if (!hostTouched
                    || !applyCoordinatorActions(std::move(actions), error)) {
                    revokeRelayRoute(connectionId, hostPeerId);
                    ++result.closedConnections;
                    continue;
                }
                found = relayRoutes.find(routeId);
                if (found == relayRoutes.end()) continue;
                actions.clear();
                const bool remoteTouched = coordinator.touchRelayRoute(
                    connectionId,
                    RuntimeInternetRelayTransport::Tcp,
                    config.serviceId,
                    remotePeerId,
                    actions,
                    error
                );
                if (!remoteTouched
                    || !applyCoordinatorActions(std::move(actions), error)) {
                    revokeRelayRoute(connectionId, remotePeerId);
                    ++result.closedConnections;
                    continue;
                }
                found = relayRoutes.find(routeId);
                if (found == relayRoutes.end()) continue;
                found->second.nextHeartbeatMilliseconds = deadlineAfter(
                    current,
                    config.limits.relayHeartbeatMilliseconds
                );
            }
        }
        return true;
    }

    void close() noexcept {
        for (auto& entry : connections) entry.second.channel.close();
        connections.clear();
        for (auto& entry : relayRoutes) {
            entry.second.pair.close();
            if (entry.second.hostStream) entry.second.hostStream->close();
            if (entry.second.remoteStream) entry.second.remoteStream->close();
        }
        relayRoutes.clear();
        pendingPeerDisconnects.clear();
        scheduledDisconnects.clear();
        tls.close();
        coordinator.close();
        matchmaking.close();
        community.close();
        replay.reset();
        peerReplay.clear();
        opened = false;
    }
};

RuntimeInternetServiceHost::RuntimeInternetServiceHost()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetServiceHost::~RuntimeInternetServiceHost() { close(); }

RuntimeInternetServiceHost::RuntimeInternetServiceHost(
    RuntimeInternetServiceHost&&
) noexcept = default;

RuntimeInternetServiceHost& RuntimeInternetServiceHost::operator=(
    RuntimeInternetServiceHost&&
) noexcept = default;

bool RuntimeInternetServiceHost::open(
    RuntimeInternetServiceHostConfig config,
    std::string& errorMessage
) {
    errorMessage.clear();
    close();
    impl_ = std::make_unique<Impl>();
    if (!validIdentity(config.serviceId)
        || !config.nowMilliseconds || !config.verifyCredential
        || !validHostLimits(config.limits)
        || config.tls.maximumConnections == 0U
        || config.tls.maximumConnections
            > runtimeInternetTlsMaximumConnections
        || config.relay.maximumQueuedBytes
            < runtimeLanMaximumPacketBytes + 12U
        || config.relay.maximumQueuedBytes
            > runtimeInternetTcpTunnelMaximumQueuedBytes
        || config.relay.connectTimeoutMilliseconds == 0U
        || config.coordinator.udpRelayEndpoint.address.empty()
        || config.coordinator.udpRelayEndpoint.port == 0U
        || config.coordinator.tcpRelayEndpoint.address.empty()) {
        errorMessage = "invalid self-hosted Internet service configuration";
        return false;
    }
    if (!config.coordinator.serviceId.empty()
        && config.coordinator.serviceId != config.serviceId) {
        errorMessage = "coordinator service identity does not match host";
        return false;
    }
    if (!config.matchmaking.serviceId.empty()
        && config.matchmaking.serviceId != config.serviceId) {
        errorMessage = "matchmaking service identity does not match host";
        return false;
    }
    if (!config.community.serviceId.empty()
        && config.community.serviceId != config.serviceId) {
        errorMessage = "community service identity does not match host";
        return false;
    }
    config.coordinator.serviceId = config.serviceId;
    config.matchmaking.serviceId = config.serviceId;
    config.community.serviceId = config.serviceId;
    if (!config.community.nowMilliseconds) {
        config.community.nowMilliseconds = config.nowMilliseconds;
    }
    impl_->config = std::move(config);
    if (!impl_->tls.open(impl_->config.tls, errorMessage)) return false;
    if (impl_->config.coordinator.tcpRelayEndpoint.port == 0U) {
        impl_->config.coordinator.tcpRelayEndpoint.port = impl_->tls.port();
    }
    if (impl_->config.coordinator.tcpRelayEndpoint.port != impl_->tls.port()) {
        errorMessage = "TCP relay endpoint does not match the TLS listener";
        impl_->close();
        return false;
    }
    if (!impl_->matchmaking.open(
            impl_->config.matchmaking,
            errorMessage
        )) {
        impl_->close();
        return false;
    }
    if (!impl_->community.open(
            impl_->config.community,
            errorMessage
        )) {
        impl_->close();
        return false;
    }
    impl_->externalAdmission =
        impl_->config.coordinator.authorizePasswordJoin;
    Impl* const stableImpl = impl_.get();
    impl_->config.coordinator.authorizePasswordJoin = [stableImpl](
        const std::string& roomId,
        const std::string& peerId,
        const std::vector<std::uint8_t>& proof
    ) {
        if (stableImpl->matchmaking.authorizeAdmission(
                roomId,
                peerId,
                proof
            )) {
            return true;
        }
        return stableImpl->externalAdmission
            && stableImpl->externalAdmission(roomId, peerId, proof);
    };
    if (!impl_->coordinator.open(
            impl_->config.coordinator,
            errorMessage
        )) {
        impl_->close();
        return false;
    }
    impl_->replay =
        std::make_unique<RuntimeInternetServiceNonceReplayWindow>(
            impl_->config.limits.maximumReplayNonces,
            impl_->config.limits.replayNonceRetentionMilliseconds
        );
    impl_->opened = true;
    return true;
}

RuntimeInternetServiceHostPollResult RuntimeInternetServiceHost::poll() {
    RuntimeInternetServiceHostPollResult result;
    if (!impl_ || !impl_->opened) {
        result.succeeded = false;
        result.error = "self-hosted Internet service is not open";
        return result;
    }
    result = impl_->pollConnections();
    if (!result.succeeded) return result;
    if (!impl_->pollAuthorities(result)) return result;
    (void)impl_->pollRelayPairs(result);
    return result;
}

RuntimeInternetServiceHostStats RuntimeInternetServiceHost::stats() const {
    RuntimeInternetServiceHostStats result;
    if (!impl_) return result;
    result.tlsConnections = impl_->tls.connectionCount()
        + impl_->totalApplicationConnectionCount();
    result.authenticatingConnections =
        impl_->connectionCount(Impl::ConnectionStage::Authenticating)
        + impl_->connectionCount(Impl::ConnectionStage::Rejecting);
    result.coordinatorConnections = impl_->coordinatorCount();
    result.pendingRelayLegs = impl_->pendingRelayCount();
    result.activeRelayPairs = impl_->activePairCount();
    result.replayNonces = impl_->replay ? impl_->replay->size() : 0U;
    result.rooms = impl_->coordinator.roomCount();
    result.coordinatorConnectionsInRooms =
        impl_->coordinator.connectionCount();
    result.coordinatorRelayRoutes = impl_->coordinator.relayRouteCount();
    result.queuedMatchmakingPeers = impl_->matchmaking.queuedPeerCount();
    result.activeMatches = impl_->matchmaking.activeMatchCount();
    const RuntimeInternetCommunityServiceStats community =
        impl_->community.stats();
    result.registeredCommunityAccounts = community.registeredAccounts;
    result.onlineCommunityAccounts = community.onlineAccounts;
    result.registeredCommunityClans = community.registeredClans;
    return result;
}

bool RuntimeInternetServiceHost::isOpen() const {
    return impl_ && impl_->opened && impl_->tls.isOpen();
}

std::uint16_t RuntimeInternetServiceHost::port() const {
    return impl_ ? impl_->tls.port() : 0U;
}

void RuntimeInternetServiceHost::close() noexcept {
    if (impl_) impl_->close();
}
