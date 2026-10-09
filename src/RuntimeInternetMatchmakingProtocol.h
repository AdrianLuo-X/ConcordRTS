#pragma once

#include "RuntimeInternetMatchmaking.h"
#include "RuntimeLanTransport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

inline constexpr std::uint16_t runtimeInternetMatchmakingWireVersion = 2;
inline constexpr std::uint16_t runtimeInternetMatchmakingPacketType = 0x0102;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumPacketBytes =
    64U * 1024U;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumErrorBytes = 256;

enum class RuntimeInternetMatchmakingMessageType : std::uint8_t {
    Enqueue = 1,
    Heartbeat = 2,
    Cancel = 3,
    CloseMatch = 4,
    OperationAck = 5,
    Assignment = 6,
    Error = 7,
    ActivateMatch = 8
};

enum class RuntimeInternetMatchmakingOperation : std::uint8_t {
    Enqueue = 1,
    Heartbeat = 2,
    Cancel = 3,
    CloseMatch = 4,
    ActivateMatch = 5
};

struct RuntimeInternetMatchmakingRequestIdBody {
    std::uint64_t requestId = 0;

    bool operator==(
        const RuntimeInternetMatchmakingRequestIdBody&
    ) const = default;
};

struct RuntimeInternetMatchmakingCloseBody {
    std::string matchId;

    bool operator==(const RuntimeInternetMatchmakingCloseBody&) const = default;
};

struct RuntimeInternetMatchmakingAckBody {
    RuntimeInternetMatchmakingOperation operation =
        RuntimeInternetMatchmakingOperation::Enqueue;
    std::uint64_t requestId = 0;
    bool changed = false;
    bool duplicate = false;
    std::uint32_t leaseMilliseconds = 0;

    bool operator==(const RuntimeInternetMatchmakingAckBody&) const = default;
};

struct RuntimeInternetMatchmakingErrorBody {
    RuntimeInternetMatchmakingOperation operation =
        RuntimeInternetMatchmakingOperation::Enqueue;
    std::uint64_t requestId = 0;
    std::string reason;

    bool operator==(const RuntimeInternetMatchmakingErrorBody&) const = default;
};

using RuntimeInternetMatchmakingMessageBody = std::variant<
    RuntimeInternetMatchmakingRequest,
    RuntimeInternetMatchmakingRequestIdBody,
    RuntimeInternetMatchmakingCloseBody,
    RuntimeInternetMatchmakingAckBody,
    RuntimeInternetMatchmakingAssignment,
    RuntimeInternetMatchmakingErrorBody
>;

struct RuntimeInternetMatchmakingMessage {
    RuntimeInternetMatchmakingMessageType type =
        RuntimeInternetMatchmakingMessageType::Error;
    std::uint64_t transactionId = 0;
    std::string serviceId;
    std::string senderPeerId;
    std::string targetPeerId;
    RuntimeInternetMatchmakingMessageBody body =
        RuntimeInternetMatchmakingErrorBody{};

    bool operator==(const RuntimeInternetMatchmakingMessage&) const = default;
};

bool encodeRuntimeInternetMatchmakingMessage(
    const RuntimeInternetMatchmakingMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
);

bool decodeRuntimeInternetMatchmakingMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetMatchmakingMessage& message,
    std::string& errorMessage
);

struct RuntimeInternetMatchmakingServiceEnvelope {
    std::string authenticatedServiceId;
    std::string authenticatedPeerId;
    RuntimeNetworkPacket packet;
};

struct RuntimeInternetMatchmakingServiceAction {
    std::string targetPeerId;
    RuntimeNetworkPacket packet;
};

// Secure service-side wire adapter. The outer transport supplies both
// authenticated identities (normally from mTLS); claimed message identities
// are checked against them before the authoritative queue is touched.
class RuntimeInternetMatchmakingService {
public:
    RuntimeInternetMatchmakingService();
    ~RuntimeInternetMatchmakingService();
    RuntimeInternetMatchmakingService(
        RuntimeInternetMatchmakingService&&
    ) noexcept;
    RuntimeInternetMatchmakingService& operator=(
        RuntimeInternetMatchmakingService&&
    ) noexcept;
    RuntimeInternetMatchmakingService(
        const RuntimeInternetMatchmakingService&
    ) = delete;
    RuntimeInternetMatchmakingService& operator=(
        const RuntimeInternetMatchmakingService&
    ) = delete;

    bool open(
        RuntimeInternetMatchmakingConfig config,
        std::string& errorMessage
    );
    bool receive(
        const RuntimeInternetMatchmakingServiceEnvelope& envelope,
        std::vector<RuntimeInternetMatchmakingServiceAction>& actions,
        std::string& errorMessage
    );
    std::vector<RuntimeInternetMatchmakingServiceAction> poll(
        std::string& errorMessage
    );
    bool authorizeAdmission(
        std::string_view roomId,
        std::string_view authenticatedPeerId,
        std::span<const std::uint8_t> admissionProof
    ) const;
    RuntimeInternetMatchmakingOperationResult authenticatedPeerDisconnected(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId
    );
    bool isOpen() const;
    std::size_t queuedPeerCount() const;
    std::size_t activeMatchCount() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class RuntimeInternetMatchmakingClientState : std::uint8_t {
    Idle,
    Enqueueing,
    Queued,
    Cancelling,
    Cancelled,
    Assigned,
    Failed,
    Closed
};

struct RuntimeInternetMatchmakingClientConfig {
    std::string serviceId;
    std::string localPeerId;
    std::uint32_t heartbeatMilliseconds = 5000;
};

// Small client state machine intended to share the authenticated coordinator
// channel used by RuntimeInternetLobbyFlow. It emits canonical packets only;
// the TLS adapter remains responsible for delivery and authenticated source.
class RuntimeInternetMatchmakingClient {
public:
    bool begin(
        RuntimeInternetMatchmakingClientConfig config,
        RuntimeInternetMatchmakingRequest request,
        std::uint64_t nowMilliseconds,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    bool receive(
        const RuntimeNetworkPacket& packet,
        std::string_view authenticatedServiceId,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool poll(
        std::uint64_t nowMilliseconds,
        RuntimeNetworkPacket& heartbeatPacket,
        bool& hasHeartbeat,
        std::string& errorMessage
    );
    bool requestCancel(
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    bool requestCloseMatch(
        std::uint64_t transactionId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    bool requestActivateMatch(
        std::uint64_t transactionId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );

    RuntimeInternetMatchmakingClientState state() const;
    const RuntimeInternetMatchmakingAssignment* assignment() const;
    const std::string& failureReason() const;
    void close();

private:
    bool makeRequest(
        RuntimeInternetMatchmakingMessageType type,
        std::uint64_t transactionId,
        RuntimeInternetMatchmakingMessageBody body,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    void fail(std::string reason);

    RuntimeInternetMatchmakingClientConfig config_;
    RuntimeInternetMatchmakingRequest request_;
    RuntimeInternetMatchmakingClientState state_ =
        RuntimeInternetMatchmakingClientState::Idle;
    std::uint32_t negotiatedLeaseMilliseconds_ = 0U;
    std::uint64_t nextHeartbeatMilliseconds_ = 0U;
    RuntimeInternetMatchmakingAssignment assignment_;
    std::uint64_t closeTransactionId_ = 0U;
    std::uint64_t activationTransactionId_ = 0U;
    bool matchActivated_ = false;
    std::string failureReason_;
};
