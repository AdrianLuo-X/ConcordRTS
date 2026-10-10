#pragma once

#include "network/session/RuntimeLanTransport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Internet play deliberately has no baked-in public service. The embedding
// application must provide an authenticated coordinator connection and the
// socket adapter that executes RuntimeInternetAction values.
inline constexpr std::uint16_t runtimeInternetControlProtocolVersion = 3;
inline constexpr std::uint16_t runtimeInternetControlPacketType = 0x0101;
inline constexpr std::size_t runtimeInternetMaximumControlBytes = 64U * 1024U;
inline constexpr std::size_t runtimeInternetMaximumIdentityBytes = 64;
inline constexpr std::size_t runtimeInternetMaximumRoomIdBytes = 96;
inline constexpr std::size_t runtimeInternetMaximumRoomNameBytes = 128;
inline constexpr std::size_t runtimeInternetMaximumFingerprintBytes = 256;
inline constexpr std::size_t runtimeInternetMaximumEndpointAddressBytes = 255;
inline constexpr std::size_t runtimeInternetMaximumCandidates = 8;
inline constexpr std::size_t runtimeInternetMaximumRooms = 128;
inline constexpr std::size_t runtimeInternetMinimumConnectionTokenBytes = 16;
inline constexpr std::size_t runtimeInternetMaximumConnectionTokenBytes = 64;
inline constexpr std::size_t runtimeInternetMaximumRelayTicketBytes = 512;
inline constexpr std::size_t runtimeInternetMaximumErrorBytes = 256;

enum class RuntimeInternetRole : std::uint8_t {
    Host = 1,
    Client = 2,
    Spectator = 3
};

enum class RuntimeInternetCandidateKind : std::uint8_t {
    Host = 1,
    ServerReflexive = 2
};

enum class RuntimeInternetRelayTransport : std::uint8_t {
    Udp = 1,
    Tcp = 2
};

struct RuntimeInternetEndpoint {
    std::string address;
    std::uint16_t port = 0;

    bool operator==(const RuntimeInternetEndpoint&) const = default;
};

struct RuntimeInternetCandidate {
    RuntimeInternetCandidateKind kind = RuntimeInternetCandidateKind::Host;
    RuntimeInternetEndpoint endpoint;
    std::uint32_t priority = 0;

    bool operator==(const RuntimeInternetCandidate&) const = default;
};

struct RuntimeInternetRoomRecord {
    std::string roomId;
    std::string roomName;
    std::string hostPeerId;
    std::string contentFingerprint;
    std::uint8_t playerCount = 1;
    std::uint8_t maximumPlayers = 2;
    bool passwordProtected = false;

    bool operator==(const RuntimeInternetRoomRecord&) const = default;
};

struct RuntimeInternetRegisterRoomBody {
    RuntimeInternetRoomRecord room;
    std::vector<RuntimeInternetCandidate> candidates;

    bool operator==(const RuntimeInternetRegisterRoomBody&) const = default;
};

struct RuntimeInternetRegisterAcceptedBody {
    std::uint32_t leaseMilliseconds = 0;

    bool operator==(const RuntimeInternetRegisterAcceptedBody&) const = default;
};

struct RuntimeInternetRoomListRequestBody {
    std::string contentFingerprint;

    bool operator==(const RuntimeInternetRoomListRequestBody&) const = default;
};

struct RuntimeInternetRoomListResultBody {
    std::vector<RuntimeInternetRoomRecord> rooms;

    bool operator==(const RuntimeInternetRoomListResultBody&) const = default;
};

struct RuntimeInternetJoinRoomBody {
    RuntimeInternetRole role = RuntimeInternetRole::Client;
    std::string contentFingerprint;
    std::vector<RuntimeInternetCandidate> candidates;

    bool operator==(const RuntimeInternetJoinRoomBody&) const = default;
};

struct RuntimeInternetPeerCandidatesBody {
    std::uint64_t connectionId = 0;
    std::string remotePeerId;
    RuntimeInternetRole remoteRole = RuntimeInternetRole::Host;
    std::vector<RuntimeInternetCandidate> candidates;
    std::vector<std::uint8_t> connectionToken;
    // A replacement route keeps canonical LAN membership, but receives a new
    // service route identity and fresh tickets. Zero denotes an initial join.
    std::uint64_t resumesConnectionId = 0;

    bool operator==(const RuntimeInternetPeerCandidatesBody&) const = default;
};

struct RuntimeInternetResumeRouteBody {
    std::uint64_t connectionId = 0;
    std::vector<std::uint8_t> connectionToken;

    bool operator==(const RuntimeInternetResumeRouteBody&) const = default;
};

struct RuntimeInternetPunchBody {
    std::uint64_t connectionId = 0;
    std::vector<std::uint8_t> connectionToken;

    bool operator==(const RuntimeInternetPunchBody&) const = default;
};

struct RuntimeInternetRelayRequestBody {
    std::uint64_t connectionId = 0;
    RuntimeInternetRelayTransport transport = RuntimeInternetRelayTransport::Udp;
    std::vector<std::uint8_t> connectionToken;

    bool operator==(const RuntimeInternetRelayRequestBody&) const = default;
};

struct RuntimeInternetRelayLeaseBody {
    std::uint64_t connectionId = 0;
    RuntimeInternetRelayTransport transport = RuntimeInternetRelayTransport::Udp;
    RuntimeInternetEndpoint endpoint;
    std::vector<std::uint8_t> ticket;

    bool operator==(const RuntimeInternetRelayLeaseBody&) const = default;
};

struct RuntimeInternetRelayBindBody {
    std::uint64_t connectionId = 0;
    RuntimeInternetRelayTransport transport = RuntimeInternetRelayTransport::Udp;
    std::vector<std::uint8_t> ticket;

    bool operator==(const RuntimeInternetRelayBindBody&) const = default;
};

struct RuntimeInternetRelayBoundBody {
    std::uint64_t connectionId = 0;
    RuntimeInternetRelayTransport transport = RuntimeInternetRelayTransport::Udp;

    bool operator==(const RuntimeInternetRelayBoundBody&) const = default;
};

// SealRoom deliberately carries no mutable room data. Its envelope must use
// the original RegisterRoom transaction id, so the coordinator can bind the
// idempotent operation to the exact host registration.
struct RuntimeInternetSealRoomBody {
    bool operator==(const RuntimeInternetSealRoomBody&) const = default;
};

struct RuntimeInternetRoomSealedBody {
    bool operator==(const RuntimeInternetRoomSealedBody&) const = default;
};

struct RuntimeInternetCoordinatorHeartbeatBody {
    std::uint64_t sequence = 0;

    bool operator==(
        const RuntimeInternetCoordinatorHeartbeatBody&
    ) const = default;
};

struct RuntimeInternetCoordinatorHeartbeatAckBody {
    std::uint64_t sequence = 0;

    bool operator==(
        const RuntimeInternetCoordinatorHeartbeatAckBody&
    ) const = default;
};

struct RuntimeInternetErrorBody {
    std::string reason;

    bool operator==(const RuntimeInternetErrorBody&) const = default;
};

enum class RuntimeInternetControlType : std::uint8_t {
    RegisterRoom = 1,
    RegisterAccepted = 2,
    RoomListRequest = 3,
    RoomListResult = 4,
    JoinRoom = 5,
    PeerCandidates = 6,
    PunchProbe = 7,
    PunchAck = 8,
    RelayRequest = 9,
    RelayLease = 10,
    RelayBind = 11,
    RelayBound = 12,
    Error = 13,
    SealRoom = 14,
    RoomSealed = 15,
    CoordinatorHeartbeat = 16,
    CoordinatorHeartbeatAck = 17,
    ResumeRoute = 18
};

using RuntimeInternetControlBody = std::variant<
    RuntimeInternetRegisterRoomBody,
    RuntimeInternetRegisterAcceptedBody,
    RuntimeInternetRoomListRequestBody,
    RuntimeInternetRoomListResultBody,
    RuntimeInternetJoinRoomBody,
    RuntimeInternetPeerCandidatesBody,
    RuntimeInternetPunchBody,
    RuntimeInternetRelayRequestBody,
    RuntimeInternetRelayLeaseBody,
    RuntimeInternetRelayBindBody,
    RuntimeInternetRelayBoundBody,
    RuntimeInternetSealRoomBody,
    RuntimeInternetRoomSealedBody,
    RuntimeInternetCoordinatorHeartbeatBody,
    RuntimeInternetCoordinatorHeartbeatAckBody,
    RuntimeInternetResumeRouteBody,
    RuntimeInternetErrorBody
>;

struct RuntimeInternetControlMessage {
    RuntimeInternetControlType type = RuntimeInternetControlType::Error;
    std::uint64_t transactionId = 0;
    std::string serviceId;
    std::string roomId;
    std::string senderPeerId;
    std::string targetPeerId;
    RuntimeInternetControlBody body = RuntimeInternetErrorBody{};

    bool operator==(const RuntimeInternetControlMessage&) const = default;
};

bool encodeRuntimeInternetControlMessage(
    const RuntimeInternetControlMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
);

bool decodeRuntimeInternetControlMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetControlMessage& message,
    std::string& errorMessage
);

enum class RuntimeInternetConnectionState : std::uint8_t {
    Idle,
    RegisteringRoom,
    QueryingRooms,
    JoiningRoom,
    WaitingForPeer,
    Punching,
    RequestingRelayUdp,
    BindingRelayUdp,
    RequestingRelayTcp,
    BindingRelayTcp,
    // A valid UDP Punch/Ack proves only that one UDP candidate is reachable.
    // It is never a usable state for the TCP-framed multiplayer session.
    UdpCandidateVerified,
    ConnectedRelayUdp,
    // The authenticated TCP tunnel has been paired by the relay. Gameplay is
    // still not connected until the LAN admission flow reaches JoinAccepted.
    TcpTunnelBound,
    GameplaySessionConnected,
    RoomListReady,
    Failed,
    Closed
};

enum class RuntimeInternetTcpRouteKind : std::uint8_t {
    Relay = 1
};

// A copy-only route identity. Relay tickets and mutable retry state are never
// exposed through this descriptor.
struct RuntimeInternetTcpRouteDescriptor {
    RuntimeInternetTcpRouteKind kind = RuntimeInternetTcpRouteKind::Relay;
    std::uint64_t connectionId = 0;
    std::string serviceId;
    std::string roomId;
    std::string localPeerId;
    RuntimeInternetRole localRole = RuntimeInternetRole::Client;
    std::string remotePeerId;
    RuntimeInternetRole remoteRole = RuntimeInternetRole::Host;
    RuntimeInternetEndpoint endpoint;

    bool operator==(const RuntimeInternetTcpRouteDescriptor&) const = default;
};

enum class RuntimeInternetActionKind : std::uint8_t {
    SendCoordinator,
    SendPeerUdp,
    OpenRelayUdp,
    OpenRelayTcp
};

struct RuntimeInternetAction {
    RuntimeInternetActionKind kind = RuntimeInternetActionKind::SendCoordinator;
    RuntimeInternetEndpoint endpoint;
    RuntimeNetworkPacket packet;

    bool operator==(const RuntimeInternetAction&) const = default;
};

struct RuntimeInternetConnectionConfig {
    std::string serviceId;
    std::string localPeerId;
    std::string roomId;
    RuntimeInternetRole role = RuntimeInternetRole::Client;
    std::string contentFingerprint;
    std::vector<RuntimeInternetCandidate> localCandidates;
    // Caller-generated, unpredictable, non-zero request correlation value.
    std::uint64_t transactionId = 0;
    std::uint32_t coordinatorHeartbeatMilliseconds = 30000;
    std::uint32_t retryMilliseconds = 250;
    std::uint32_t coordinatorTimeoutMilliseconds = 5000;
    std::uint32_t punchTimeoutMilliseconds = 3000;
    std::uint32_t relayTimeoutMilliseconds = 5000;
    std::uint8_t maximumRetries = 8;
};

class RuntimeInternetConnection {
public:
    bool beginHost(
        RuntimeInternetConnectionConfig config,
        RuntimeInternetRoomRecord room,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool beginJoin(
        RuntimeInternetConnectionConfig config,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool beginRoomQuery(
        RuntimeInternetConnectionConfig config,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    // A host keeps its beginHost instance in WaitingForPeer so the room lease
    // continues to refresh. Its coordinator dispatcher creates one additional
    // instance per offered client/spectator route with this method, allowing a
    // room to serve multiple remote peers without sharing punch/relay state.
    bool beginPeerRouteFromCoordinator(
        RuntimeInternetConnectionConfig config,
        const RuntimeNetworkPacket& candidatePacket,
        const std::string& authenticatedServiceId,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );

    // Hosts call this on their long-lived registration carrier after the
    // canonical lobby enters Started. The carrier remains WaitingForPeer and
    // continues RegisterRoom lease heartbeats while this independently
    // retried request waits for RoomSealed.
    bool requestRoomSeal(
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );

    std::vector<RuntimeInternetAction> poll(std::uint64_t nowMilliseconds);

    // authenticatedServiceId comes from the embedding application's mutually
    // authenticated TLS/service channel, not from the packet itself.
    bool receiveCoordinator(
        const RuntimeNetworkPacket& packet,
        const std::string& authenticatedServiceId,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool receivePeerUdp(
        const RuntimeNetworkPacket& packet,
        const RuntimeInternetEndpoint& source,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    // Relay packets must be delivered only from the endpoint selected by the
    // lease. authenticatedRelayServiceId is supplied by TLS for TCP and by the
    // relay adapter's authenticated datagram channel for UDP.
    bool receiveRelay(
        const RuntimeNetworkPacket& packet,
        const RuntimeInternetEndpoint& source,
        const std::string& authenticatedRelayServiceId,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool reportRelayOpenFailure(
        RuntimeInternetRelayTransport transport,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    // Called by the tunnel/LAN adapter only after the canonical JoinAccepted
    // for this exact route. A mere UDP probe or RelayBound cannot satisfy it.
    bool reportGameplaySessionJoined(
        std::uint64_t connectionId,
        std::string& errorMessage
    );
    bool makeResumeRouteRequest(RuntimeNetworkPacket& packet,
        std::string& errorMessage) const;
    void coordinatorReauthenticated(std::uint64_t nowMilliseconds);

    void close();
    RuntimeInternetConnectionState state() const;
    RuntimeInternetRole role() const;
    const std::string& remotePeerId() const;
    std::optional<RuntimeInternetTcpRouteDescriptor> tcpRoute() const;
    bool tcpTunnelBound() const;
    bool gameplaySessionConnected() const;
    bool roomSealRequested() const;
    bool roomSealed() const;
    const std::vector<RuntimeInternetRoomRecord>& rooms() const;
    const std::string& failureReason() const;

private:
    RuntimeInternetConnectionConfig config_;
    RuntimeInternetRoomRecord hostedRoom_;
    RuntimeInternetConnectionState state_ = RuntimeInternetConnectionState::Idle;
    RuntimeNetworkPacket pendingCoordinatorPacket_;
    std::uint64_t stateStartedMilliseconds_ = 0;
    std::uint64_t nextRetryMilliseconds_ = 0;
    std::uint64_t hostLeaseRefreshMilliseconds_ = 0;
    RuntimeNetworkPacket sealRoomPacket_;
    std::uint64_t sealStartedMilliseconds_ = 0;
    std::uint64_t nextSealRetryMilliseconds_ = 0;
    std::uint8_t sealRetryCount_ = 0;
    bool sealRequested_ = false;
    bool roomSealed_ = false;
    RuntimeNetworkPacket coordinatorHeartbeatPacket_;
    std::uint64_t heartbeatStartedMilliseconds_ = 0;
    std::uint64_t nextHeartbeatMilliseconds_ = 0;
    std::uint64_t nextHeartbeatRetryMilliseconds_ = 0;
    std::uint8_t heartbeatRetryCount_ = 0;
    bool awaitingHeartbeatAcknowledgement_ = false;
    std::uint64_t nextHeartbeatSequence_ = 1U;
    std::uint64_t awaitingHeartbeatSequence_ = 0U;
    std::uint64_t acknowledgedHeartbeatSequence_ = 0U;
    std::optional<RuntimeNetworkPacket> acceptedRegistrationResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRoomListResponse_;
    std::optional<RuntimeNetworkPacket> acceptedPeerCandidatesResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRelayLeaseUdpResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRelayLeaseTcpResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRoomSealedResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRelayBoundUdpResponse_;
    std::optional<RuntimeNetworkPacket> acceptedRelayBoundTcpResponse_;
    std::uint8_t retryCount_ = 0;
    std::uint64_t connectionId_ = 0;
    std::string remotePeerId_;
    RuntimeInternetRole remoteRole_ = RuntimeInternetRole::Host;
    std::vector<RuntimeInternetCandidate> remoteCandidates_;
    std::vector<std::uint8_t> connectionToken_;
    RuntimeInternetEndpoint relayEndpoint_;
    std::vector<std::uint8_t> relayTicket_;
    RuntimeInternetRelayTransport requestedRelay_ = RuntimeInternetRelayTransport::Udp;
    std::optional<RuntimeInternetTcpRouteDescriptor> tcpRoute_;
    RuntimeNetworkPacket relayBindPacket_;
    std::vector<RuntimeInternetAction> queuedActions_;
    std::vector<RuntimeInternetRoomRecord> rooms_;
    std::string failureReason_;

    bool start(
        RuntimeInternetConnectionConfig config,
        RuntimeInternetConnectionState initialState,
        RuntimeInternetControlMessage request,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool acceptCandidates(
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetPeerCandidatesBody& body,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    void requestRelay(
        RuntimeInternetRelayTransport transport,
        std::uint64_t nowMilliseconds
    );
    void fail(std::string reason);
};
