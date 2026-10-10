#pragma once

#include "network/internet/RuntimeInternetCommunity.h"
#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/transport/RuntimeInternetMatchmakingProtocol.h"
#include "network/transport/RuntimeInternetServiceTransport.h"
#include "network/transport/RuntimeInternetTcpTunnelProxy.h"
#include "network/transport/RuntimeInternetTlsTransport.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

struct RuntimeInternetServiceCredentialRequest {
    RuntimeInternetServiceWireChannel channel =
        RuntimeInternetServiceWireChannel::Coordinator;
    RuntimeInternetServiceNonce nonce{};
    std::string peerAddress;
    std::string claimedServiceId;
    std::string claimedPeerId;
    std::span<const std::uint8_t> credential;
};

struct RuntimeInternetServiceVerifiedIdentity {
    std::string serviceId;
    std::string peerId;
};

using RuntimeInternetServiceCredentialVerifier = std::function<bool(
    const RuntimeInternetServiceCredentialRequest& request,
    RuntimeInternetServiceVerifiedIdentity& verifiedIdentity,
    std::string& errorMessage
)>;

struct RuntimeInternetServiceHostLimits {
    std::uint32_t applicationAuthenticationTimeoutMilliseconds = 5000U;
    std::uint32_t coordinatorIdleTimeoutMilliseconds = 120000U;
    // Zero disables byte-idle expiry for active relay pairs. Authenticated
    // relay heartbeats, TCP close, leases, and capacity still reclaim them.
    std::uint32_t relayPairIdleTimeoutMilliseconds = 0U;
    std::uint32_t relayHeartbeatMilliseconds = 5000U;
    std::uint32_t pendingRelayTimeoutMilliseconds = 10000U;
    std::size_t maximumQueuedBytes =
        runtimeInternetServiceMaximumBufferedWireBytes;
    std::size_t maximumAuthenticatingConnections = 64U;
    std::size_t maximumTotalConnections = 768U;
    std::size_t maximumCoordinatorConnections = 256U;
    std::size_t maximumCoordinatorConnectionsPerPeer = 1U;
    std::size_t maximumPendingRelayLegs = 512U;
    std::size_t maximumActiveRelayPairs = 256U;
    std::size_t maximumFramesPerConnectionPoll = 32U;
    std::size_t maximumReplayNonces = 8192U;
    // A single authenticated peer may consume only this strict subset of the
    // shared replay window, preserving capacity for other accounts.
    std::size_t maximumReplayNoncesPerPeer = 128U;
    std::uint64_t replayNonceRetentionMilliseconds = 60000U;
};

struct RuntimeInternetServiceHostConfig {
    std::string serviceId;
    RuntimeInternetTlsServerConfig tls;
    RuntimeInternetCoordinatorConfig coordinator;
    RuntimeInternetMatchmakingConfig matchmaking;
    RuntimeInternetCommunityServiceConfig community;
    RuntimeInternetTcpTunnelProxyConfig relay;
    RuntimeInternetServiceHostLimits limits;

    // This clock must be monotonic and should be the same clock supplied to
    // coordinator and matchmaking. It is also used for authentication and
    // relay resource deadlines.
    std::function<std::uint64_t()> nowMilliseconds;

    // The callback authenticates the opaque credential and returns the only
    // service/peer identity trusted by the host. Wire identity claims are
    // checked against this result, but are never used as authentication.
    RuntimeInternetServiceCredentialVerifier verifyCredential;
};

struct RuntimeInternetServiceHostPollResult {
    bool succeeded = true;
    bool progressed = false;
    std::size_t rejectedConnections = 0U;
    std::size_t closedConnections = 0U;
    std::string error;
};

struct RuntimeInternetServiceHostStats {
    std::size_t tlsConnections = 0U;
    std::size_t authenticatingConnections = 0U;
    std::size_t coordinatorConnections = 0U;
    std::size_t pendingRelayLegs = 0U;
    std::size_t activeRelayPairs = 0U;
    std::size_t replayNonces = 0U;
    std::size_t rooms = 0U;
    std::size_t coordinatorConnectionsInRooms = 0U;
    std::size_t coordinatorRelayRoutes = 0U;
    std::size_t queuedMatchmakingPeers = 0U;
    std::size_t activeMatches = 0U;
    std::size_t registeredCommunityAccounts = 0U;
    std::size_t onlineCommunityAccounts = 0U;
    std::size_t registeredCommunityClans = 0U;
};

// Self-hosted TCP/TLS Internet control and relay service. This class does not
// open, advertise, or forward UDP. Relay data streams are released only after
// both independently authenticated TCP legs have been accepted by the
// coordinator and it emits ActivateRelayRoute.
class RuntimeInternetServiceHost {
public:
    RuntimeInternetServiceHost();
    ~RuntimeInternetServiceHost();
    RuntimeInternetServiceHost(RuntimeInternetServiceHost&&) noexcept;
    RuntimeInternetServiceHost& operator=(
        RuntimeInternetServiceHost&&
    ) noexcept;
    RuntimeInternetServiceHost(const RuntimeInternetServiceHost&) = delete;
    RuntimeInternetServiceHost& operator=(
        const RuntimeInternetServiceHost&
    ) = delete;

    bool open(
        RuntimeInternetServiceHostConfig config,
        std::string& errorMessage
    );
    RuntimeInternetServiceHostPollResult poll();
    RuntimeInternetServiceHostStats stats() const;
    bool isOpen() const;
    std::uint16_t port() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
