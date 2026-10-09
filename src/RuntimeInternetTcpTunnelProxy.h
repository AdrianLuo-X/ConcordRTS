#pragma once

#include "RuntimeInternetConnectivity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

inline constexpr std::size_t runtimeInternetTcpTunnelPreludeBytes = 32U;
inline constexpr std::size_t runtimeInternetTcpTunnelMaximumQueuedBytes =
    4U * 1024U * 1024U;

// The concrete stream is supplied only after an external TLS/mTLS adapter has
// authenticated the service and peer. A plain public TCP socket must never be
// labelled authenticated in production. Tests may use trusted loopback
// streams to exercise the byte path without shipping a certificate authority.
struct RuntimeInternetAuthenticatedTcpStreamIdentity {
    std::string authenticatedServiceId;
    std::string authenticatedPeerId;
    RuntimeInternetTcpRouteDescriptor route;

    bool operator==(
        const RuntimeInternetAuthenticatedTcpStreamIdentity&
    ) const = default;
};

enum class RuntimeInternetTcpStreamIoStatus : std::uint8_t {
    Progress,
    WouldBlock,
    Closed,
    Failed
};

struct RuntimeInternetTcpStreamIoResult {
    RuntimeInternetTcpStreamIoStatus status =
        RuntimeInternetTcpStreamIoStatus::WouldBlock;
    std::size_t bytesTransferred = 0;
    std::string error;
};

class RuntimeInternetAuthenticatedTcpStream {
public:
    virtual ~RuntimeInternetAuthenticatedTcpStream() = default;
    virtual const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const = 0;
    virtual RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t> destination
    ) = 0;
    virtual RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) = 0;
    virtual void close() noexcept = 0;
};

struct RuntimeInternetTcpTunnelProxyConfig {
    std::size_t maximumQueuedBytes =
        runtimeInternetTcpTunnelMaximumQueuedBytes;
    std::uint32_t connectTimeoutMilliseconds = 5000;
    // Zero disables byte-idle expiry. Production Internet tunnels use this
    // mode and are reclaimed by authenticated coordinator/relay heartbeats,
    // TCP closure, leases, and bounded capacity instead of gameplay traffic.
    std::uint32_t idleTimeoutMilliseconds = 0;
    // Authenticated carrier-only frames, stripped before the canonical LAN
    // session. Disabled for generic byte-proxy users and relay service pairs.
    std::uint32_t heartbeatMilliseconds = 0;
    std::uint32_t heartbeatTimeoutMilliseconds = 5000;
};

struct RuntimeInternetTcpTunnelClientEndpoint {
    RuntimeLanRoomAdvertisement room;
    std::vector<std::uint8_t> localProxyPrelude;
};

struct RuntimeInternetTcpTunnelPollResult {
    bool succeeded = true;
    bool closed = false;
    bool localConnected = false;
    bool localPreludeAccepted = false;
    // Host-side transport gate only: the first relayed frame's peer and role
    // matched the authenticated route. This is not JoinAccepted and must not
    // be reported as GameplaySessionConnected.
    bool remoteAdmissionVerified = false;
    bool backpressured = false;
    std::uint64_t bytesFromLocal = 0;
    std::uint64_t bytesFromTunnel = 0;
    std::string error;
};

// Endpoint-side transparent bridge. Starting/binding this bridge establishes
// only the TCP data route; gameplay is connected only after the ordinary LAN
// session reports its authenticated JoinAccepted. Host mode dials the private
// loopback LAN session server. Client/spectator mode creates a one-shot listener
// and exposes an advertisement/prelude that can be copied into existing
// RuntimeMultiplayerLanJoinOptions.
class RuntimeInternetTcpTunnelProxy {
public:
    RuntimeInternetTcpTunnelProxy();
    ~RuntimeInternetTcpTunnelProxy();
    RuntimeInternetTcpTunnelProxy(RuntimeInternetTcpTunnelProxy&&) noexcept;
    RuntimeInternetTcpTunnelProxy& operator=(
        RuntimeInternetTcpTunnelProxy&&
    ) noexcept;
    RuntimeInternetTcpTunnelProxy(
        const RuntimeInternetTcpTunnelProxy&
    ) = delete;
    RuntimeInternetTcpTunnelProxy& operator=(
        const RuntimeInternetTcpTunnelProxy&
    ) = delete;

    bool startHost(
        RuntimeInternetTcpTunnelProxyConfig config,
        RuntimeInternetTcpRouteDescriptor route,
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tunnel,
        std::uint16_t loopbackSessionPort,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool startClient(
        RuntimeInternetTcpTunnelProxyConfig config,
        RuntimeInternetTcpRouteDescriptor route,
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tunnel,
        RuntimeLanRoomAdvertisement canonicalRoom,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    RuntimeInternetTcpTunnelPollResult poll(
        std::uint64_t nowMilliseconds
    );
    RuntimeInternetTcpTunnelClientEndpoint clientEndpoint() const;
    const RuntimeInternetTcpRouteDescriptor* route() const;
    bool isOpen() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RuntimeInternetTcpRelayPairPollResult {
    bool succeeded = true;
    bool closed = false;
    bool backpressured = false;
    std::uint64_t bytesHostToRemote = 0;
    std::uint64_t bytesRemoteToHost = 0;
    std::string error;
};

// Service-side post-RelayBound splice. Both stream identities must describe
// the same service/room/connection and opposite authenticated principals.
class RuntimeInternetTcpRelayPair {
public:
    RuntimeInternetTcpRelayPair();
    ~RuntimeInternetTcpRelayPair();
    RuntimeInternetTcpRelayPair(RuntimeInternetTcpRelayPair&&) noexcept;
    RuntimeInternetTcpRelayPair& operator=(
        RuntimeInternetTcpRelayPair&&
    ) noexcept;
    RuntimeInternetTcpRelayPair(const RuntimeInternetTcpRelayPair&) = delete;
    RuntimeInternetTcpRelayPair& operator=(
        const RuntimeInternetTcpRelayPair&
    ) = delete;

    bool open(
        RuntimeInternetTcpTunnelProxyConfig config,
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> hostSide,
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> remoteSide,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    RuntimeInternetTcpRelayPairPollResult poll(
        std::uint64_t nowMilliseconds
    );
    bool isOpen() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
