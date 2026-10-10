#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::size_t runtimeInternetMatchmakingMaximumQueueIdBytes = 64;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumRegionIdBytes = 32;
inline constexpr std::size_t
    runtimeInternetMatchmakingMaximumContentFingerprintBytes = 256;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumPeerIdBytes = 64;
inline constexpr std::size_t
    runtimeInternetMatchmakingMaximumDisplayNameBytes = 64;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumQueuedPeers = 4096;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumActiveMatches = 1024;
inline constexpr std::size_t runtimeInternetMatchmakingMaximumProofBytes = 64;

struct RuntimeInternetMatchmakingRequest {
    // The authenticated client chooses a non-zero opaque request nonce. It
    // need not be numerically monotonic. Exact replay is idempotent while the
    // request is queued or assigned; a conflicting active request fails.
    std::uint64_t requestId = 0;
    std::string queueId;
    std::string regionId;
    std::string contentFingerprint;
    // Display metadata is not an authenticated identity and never affects
    // queue compatibility. It lets every assigned client validate the host's
    // canonical LAN admission challenge without guessing from an opaque peer
    // id.
    std::string displayName;
    std::uint8_t playerCount = 2;
    std::int32_t skillRating = 1000;

    bool operator==(const RuntimeInternetMatchmakingRequest&) const = default;
};

struct RuntimeInternetMatchmakingConfig {
    std::string serviceId;
    std::uint32_t queueLeaseMilliseconds = 30000;
    std::uint32_t matchLeaseMilliseconds = 60000;
    std::size_t maximumQueuedPeers = 1024;
    std::size_t maximumActiveMatches = 256;
    std::size_t admissionProofBytes = 32;
    std::int32_t baseSkillWindow = 100;
    std::int32_t skillWindowGrowthPerSecond = 25;
    std::int32_t maximumSkillWindow = 1000;

    // Both callbacks are required. nowMilliseconds must be monotonic and
    // secureRandomBytes must be backed by a CSPRNG in production.
    std::function<std::uint64_t()> nowMilliseconds;
    std::function<bool(std::uint8_t*, std::size_t)> secureRandomBytes;
};

struct RuntimeInternetMatchmakingAssignment {
    std::uint64_t requestId = 0;
    std::string matchId;
    std::string roomId;
    std::string peerId;
    std::string hostPeerId;
    std::string hostDisplayName;
    std::vector<std::string> peerIds;
    // Per-peer opaque proof. A coordinator admission callback must validate
    // it together with roomId and the authenticated peer id.
    std::vector<std::uint8_t> admissionProof;
    std::uint64_t expiresAtMilliseconds = 0;

    bool operator==(
        const RuntimeInternetMatchmakingAssignment&
    ) const = default;
};

struct RuntimeInternetMatchmakingAction {
    std::string targetPeerId;
    RuntimeInternetMatchmakingAssignment assignment;

    bool operator==(const RuntimeInternetMatchmakingAction&) const = default;
};

struct RuntimeInternetMatchmakingOperationResult {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    std::string reason;
};

// Authenticated, transport-independent quick-match authority. Identities are
// supplied out of band by a TLS/service adapter and are never accepted from a
// request payload. Compatible peers are selected FIFO; an oldest request's
// skill window expands with wait time up to a configured bound.
class RuntimeInternetMatchmaking {
public:
    RuntimeInternetMatchmaking();
    ~RuntimeInternetMatchmaking();
    RuntimeInternetMatchmaking(RuntimeInternetMatchmaking&&) noexcept;
    RuntimeInternetMatchmaking& operator=(
        RuntimeInternetMatchmaking&&
    ) noexcept;
    RuntimeInternetMatchmaking(const RuntimeInternetMatchmaking&) = delete;
    RuntimeInternetMatchmaking& operator=(
        const RuntimeInternetMatchmaking&
    ) = delete;

    bool open(
        RuntimeInternetMatchmakingConfig config,
        std::string& errorMessage
    );

    RuntimeInternetMatchmakingOperationResult enqueue(
        std::string_view authenticatedServiceId,
        std::string authenticatedPeerId,
        RuntimeInternetMatchmakingRequest request
    );
    RuntimeInternetMatchmakingOperationResult heartbeat(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId,
        std::uint64_t requestId
    );
    RuntimeInternetMatchmakingOperationResult cancel(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId,
        std::uint64_t requestId
    );

    // Transport lifecycle hook. A disconnected authenticated coordinator
    // peer is removed from the queue immediately. If it belongs to an active
    // or staged match, the complete match (including every admission proof)
    // is revoked atomically.
    RuntimeInternetMatchmakingOperationResult authenticatedPeerDisconnected(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId
    );

    // A new match is first disclosed only to its selected host. After that
    // host has registered the assigned room with the coordinator, this call
    // releases the remaining peer assignments. Exact host replay is
    // idempotent and re-emits the same assignments.
    RuntimeInternetMatchmakingOperationResult activateMatch(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedHostPeerId,
        std::string_view matchId,
        std::vector<RuntimeInternetMatchmakingAction>& actions
    );

    // Expires stale state, then creates every currently possible match and
    // emits only each selected host's assignment. If secure generation fails,
    // that candidate group remains queued and no partial assignment commits.
    std::vector<RuntimeInternetMatchmakingAction> poll(
        std::string& errorMessage
    );

    // Suitable for RuntimeInternetCoordinatorConfig::authorizePasswordJoin.
    // Proofs are reusable only for the assigned peer/room until the match
    // lease expires, which preserves idempotent coordinator transactions.
    bool authorizeAdmission(
        std::string_view roomId,
        std::string_view authenticatedPeerId,
        std::span<const std::uint8_t> admissionProof
    ) const;

    RuntimeInternetMatchmakingOperationResult closeMatch(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedHostPeerId,
        std::string_view matchId
    );

    bool isOpen() const;
    std::size_t queuedPeerCount() const;
    std::size_t activeMatchCount() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
