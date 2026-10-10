#pragma once

#include "network/transport/RuntimeInternetTcpTunnelProxy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline constexpr std::size_t runtimeInternetTlsSha256Bytes = 32U;
inline constexpr std::size_t runtimeInternetTlsMaximumConnections = 256U;

using RuntimeInternetTlsSha256 =
    std::array<std::uint8_t, runtimeInternetTlsSha256Bytes>;

enum class RuntimeInternetTlsState : std::uint8_t {
    Idle,
    TcpConnecting,
    Handshaking,
    Open,
    Closed,
    Failed
};

struct RuntimeInternetTlsLimits {
    std::uint32_t connectTimeoutMilliseconds = 5000;
    std::uint32_t handshakeTimeoutMilliseconds = 10000;
};

struct RuntimeInternetTlsClientConfig {
    RuntimeInternetEndpoint endpoint;
    // Certificate validation always uses this name, never endpoint.address.
    // It is required even when endpoint.address is an IP literal.
    std::string serverName;
    RuntimeInternetTlsLimits limits;
    // Pinning is an additional restriction.  A matching pin never bypasses
    // the Windows system certificate chain or hostname checks.
    std::optional<RuntimeInternetTlsSha256> leafCertificateSha256;
    bool checkCertificateRevocation = true;
    // Optional Windows HCERTCHAINENGINE for an explicitly configured private
    // PKI. nullptr uses the normal system chain engine. The caller retains
    // ownership until the handshake completes; hostname checks remain active.
    const void* nativeCertificateChainEngine = nullptr;
};

struct RuntimeInternetTlsPollResult {
    RuntimeInternetTlsState state = RuntimeInternetTlsState::Idle;
    bool progressed = false;
    std::string error;
};

// Incremental outbound TCP + TLS connector.  Network sockets remain
// nonblocking after begin(); poll() advances connect and Schannel handshakes.
// The identity supplied here must already describe the authenticated local
// service/account route maintained by the embedding service adapter.
class RuntimeInternetTlsClient {
public:
    RuntimeInternetTlsClient();
    ~RuntimeInternetTlsClient();
    RuntimeInternetTlsClient(RuntimeInternetTlsClient&&) noexcept;
    RuntimeInternetTlsClient& operator=(
        RuntimeInternetTlsClient&&
    ) noexcept;
    RuntimeInternetTlsClient(const RuntimeInternetTlsClient&) = delete;
    RuntimeInternetTlsClient& operator=(
        const RuntimeInternetTlsClient&
    ) = delete;

    bool begin(
        RuntimeInternetTlsClientConfig config,
        RuntimeInternetAuthenticatedTcpStreamIdentity identity,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    RuntimeInternetTlsPollResult poll(std::uint64_t nowMilliseconds);
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> takeStream(
        std::string& errorMessage
    );
    RuntimeInternetTlsState state() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class RuntimeInternetTlsBindScope : std::uint8_t {
    AnyIpv4,
    LoopbackIpv4
};

struct RuntimeInternetTlsServerConfig {
    std::uint16_t port = 0;
    std::size_t maximumConnections = 64U;
    RuntimeInternetTlsBindScope bindScope =
        RuntimeInternetTlsBindScope::AnyIpv4;
    RuntimeInternetTlsLimits limits;
    // On Windows this is a borrowed PCCERT_CONTEXT. open() duplicates it, so
    // the caller may release its reference immediately after open returns.
    // The context must have an accessible private key.
    const void* nativeCertificateContext = nullptr;
};

enum class RuntimeInternetTlsServerEventKind : std::uint8_t {
    HandshakeComplete,
    Rejected,
    Disconnected
};

struct RuntimeInternetTlsServerEvent {
    RuntimeInternetTlsServerEventKind kind =
        RuntimeInternetTlsServerEventKind::Rejected;
    std::uint64_t connectionId = 0;
    std::string peerAddress;
    std::string error;
};

// Incremental TLS listener for a coordinator/relay service.  A completed TLS
// handshake authenticates the server to the client, but it does not by itself
// authenticate a player.  The service must verify its application credential
// and route before calling takeApplicationAuthenticatedStream().
class RuntimeInternetTlsServer {
public:
    RuntimeInternetTlsServer();
    ~RuntimeInternetTlsServer();
    RuntimeInternetTlsServer(RuntimeInternetTlsServer&&) noexcept;
    RuntimeInternetTlsServer& operator=(
        RuntimeInternetTlsServer&&
    ) noexcept;
    RuntimeInternetTlsServer(const RuntimeInternetTlsServer&) = delete;
    RuntimeInternetTlsServer& operator=(
        const RuntimeInternetTlsServer&
    ) = delete;

    bool open(
        RuntimeInternetTlsServerConfig config,
        std::string& errorMessage
    );
    std::vector<RuntimeInternetTlsServerEvent> poll(
        std::uint64_t nowMilliseconds
    );
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>
        takeApplicationAuthenticatedStream(
            std::uint64_t connectionId,
            RuntimeInternetAuthenticatedTcpStreamIdentity identity,
            std::string& errorMessage
        );
    bool disconnect(std::uint64_t connectionId) noexcept;
    bool isOpen() const;
    std::uint16_t port() const;
    std::size_t connectionCount() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
