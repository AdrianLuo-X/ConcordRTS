#pragma once

#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "network/transport/RuntimeInternetTlsTransport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

inline constexpr std::uint16_t runtimeInternetServiceWireVersion = 1;
inline constexpr std::size_t runtimeInternetServiceNonceBytes = 32U;
inline constexpr std::size_t runtimeInternetServiceMinimumCredentialBytes = 16U;
inline constexpr std::size_t runtimeInternetServiceMaximumCredentialBytes = 4096U;
inline constexpr std::size_t runtimeInternetServiceMaximumWireFrameBytes =
    runtimeLanMaximumPacketBytes + 16U * 1024U;
inline constexpr std::size_t runtimeInternetServiceMaximumBufferedWireBytes =
    4U * 1024U * 1024U;

using RuntimeInternetServiceNonce =
    std::array<std::uint8_t, runtimeInternetServiceNonceBytes>;

enum class RuntimeInternetServiceWireChannel : std::uint8_t {
    Coordinator = 1,
    RelayTcp = 2
};

struct RuntimeInternetServiceAuthenticationRequest {
    RuntimeInternetServiceWireChannel channel =
        RuntimeInternetServiceWireChannel::Coordinator;
    RuntimeInternetServiceNonce nonce{};
    std::string serviceId;
    std::string peerId;
    std::vector<std::uint8_t> credential;
    // Present and validated only for RelayTcp.
    RuntimeInternetTcpRouteDescriptor route;
    RuntimeNetworkPacket relayBindPacket;

    bool operator==(
        const RuntimeInternetServiceAuthenticationRequest&
    ) const = delete;
};

struct RuntimeInternetServiceAuthenticationConfirmation {
    RuntimeInternetServiceWireChannel channel =
        RuntimeInternetServiceWireChannel::Coordinator;
    RuntimeInternetServiceNonce nonce{};
    std::string serviceId;
    std::string peerId;
    // Relay confirmations must echo the complete requested route. Coordinator
    // confirmations leave this descriptor at its default value.
    RuntimeInternetTcpRouteDescriptor route;

    bool operator==(
        const RuntimeInternetServiceAuthenticationConfirmation&
    ) const = default;
};

struct RuntimeInternetServiceAuthenticationRejection {
    RuntimeInternetServiceWireChannel channel =
        RuntimeInternetServiceWireChannel::Coordinator;
    RuntimeInternetServiceNonce nonce{};

    bool operator==(
        const RuntimeInternetServiceAuthenticationRejection&
    ) const = default;
};

struct RuntimeInternetServiceCoordinatorPacket {
    // Deployment-issued password-room proof. It remains outside the legacy
    // control packet and is sent only inside the authenticated TLS channel.
    std::vector<std::uint8_t> roomAdmissionProof;
    RuntimeNetworkPacket packet;

    bool operator==(
        const RuntimeInternetServiceCoordinatorPacket&
    ) const = delete;
};

using RuntimeInternetServiceWireBody = std::variant<
    RuntimeInternetServiceAuthenticationRequest,
    RuntimeInternetServiceAuthenticationConfirmation,
    RuntimeInternetServiceAuthenticationRejection,
    RuntimeInternetServiceCoordinatorPacket
>;

struct RuntimeInternetServiceWireMessage {
    RuntimeInternetServiceWireBody body =
        RuntimeInternetServiceAuthenticationRejection{};

    bool operator==(const RuntimeInternetServiceWireMessage&) const = default;
};

// Length-prefixed canonical codec shared by the client adapter and a future
// coordinator/relay host. Authentication credentials are never included in
// rejection or confirmation frames.
bool encodeRuntimeInternetServiceWireFrame(
    const RuntimeInternetServiceWireMessage& message,
    std::vector<std::uint8_t>& framedBytes,
    std::string& errorMessage
);

class RuntimeInternetServiceWireDecoder {
public:
    bool push(
        std::span<const std::uint8_t> bytes,
        std::string& errorMessage
    );
    bool pop(
        RuntimeInternetServiceWireMessage& message,
        bool& available,
        std::string& errorMessage
    );
    std::vector<std::uint8_t> takeBufferedBytes();
    std::size_t bufferedBytes() const noexcept;
    void reset() noexcept;

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

// Bounded replay cache for application-authentication nonces. Service hosts
// must share one window across accepted TLS connections for a service realm.
class RuntimeInternetServiceNonceReplayWindow {
public:
    explicit RuntimeInternetServiceNonceReplayWindow(
        std::size_t maximumEntries = 8192U,
        std::uint64_t retentionMilliseconds = 60000U
    );
    ~RuntimeInternetServiceNonceReplayWindow();
    RuntimeInternetServiceNonceReplayWindow(
        RuntimeInternetServiceNonceReplayWindow&&
    ) noexcept;
    RuntimeInternetServiceNonceReplayWindow& operator=(
        RuntimeInternetServiceNonceReplayWindow&&
    ) noexcept;
    RuntimeInternetServiceNonceReplayWindow(
        const RuntimeInternetServiceNonceReplayWindow&
    ) = delete;
    RuntimeInternetServiceNonceReplayWindow& operator=(
        const RuntimeInternetServiceNonceReplayWindow&
    ) = delete;

    bool accept(
        const RuntimeInternetServiceNonce& nonce,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    void prune(std::uint64_t nowMilliseconds) noexcept;
    std::size_t size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RuntimeInternetServiceTransportConfig {
    std::string serviceId;
    std::string peerId;
    std::vector<std::uint8_t> credential;
    RuntimeInternetTlsClientConfig coordinatorTls;
    // endpoint is replaced with each coordinator-issued relay endpoint. Name,
    // trust, pin and TLS limits are retained from this template.
    RuntimeInternetTlsClientConfig relayTls;
    std::uint32_t applicationAuthenticationTimeoutMilliseconds = 5000U;
    std::size_t maximumQueuedBytes =
        runtimeInternetServiceMaximumBufferedWireBytes;
    std::size_t maximumPendingRelays = 32U;
};

// Production client for RuntimeInternetLobbyFlow. A TCP relay stream is kept
// private until TLS and the exact application-level route confirmation both
// succeed; only then are canonical RelayBound and TcpStreamOpened events
// emitted, in that order.
class RuntimeInternetServiceTransport final
    : public RuntimeInternetServiceAdapter {
public:
    RuntimeInternetServiceTransport();
    ~RuntimeInternetServiceTransport() override;
    RuntimeInternetServiceTransport(
        RuntimeInternetServiceTransport&&
    ) noexcept;
    RuntimeInternetServiceTransport& operator=(
        RuntimeInternetServiceTransport&&
    ) noexcept;
    RuntimeInternetServiceTransport(
        const RuntimeInternetServiceTransport&
    ) = delete;
    RuntimeInternetServiceTransport& operator=(
        const RuntimeInternetServiceTransport&
    ) = delete;

    bool open(
        RuntimeInternetServiceTransportConfig config,
        std::string& errorMessage
    );

    std::string_view authenticatedServiceId() const noexcept override;
    std::string_view authenticatedPeerId() const noexcept override;
    bool sendCoordinator(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) override;
    bool sendCoordinatorWithAdmissionProof(
        const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> roomAdmissionProof,
        std::string& errorMessage
    ) override;
    // Compatibility alias for callers that use the transport directly.
    bool sendCoordinatorAuthenticated(
        const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> roomAdmissionProof,
        std::string& errorMessage
    );
    bool openAuthenticatedTcpRelay(
        const RuntimeInternetServiceTcpOpenRequest& request,
        std::string& errorMessage
    ) override;
    std::vector<RuntimeInternetServiceEvent> poll(
        int timeoutMilliseconds
    ) override;
    void close() noexcept override;

    bool coordinatorAuthenticated() const noexcept;
    bool reauthenticateCoordinator(std::string& errorMessage) override;
    std::size_t pendingRelayCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
