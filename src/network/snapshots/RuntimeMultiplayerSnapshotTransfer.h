#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::uint32_t runtimeMultiplayerSnapshotDescriptorMagic =
    0x43534e50U; // "CSNP"
inline constexpr std::uint32_t runtimeMultiplayerSnapshotChunkMagic =
    0x43534e43U; // "CSNC"
inline constexpr std::uint32_t runtimeMultiplayerSnapshotAckMagic =
    0x43534e41U; // "CSNA"
inline constexpr std::uint16_t runtimeMultiplayerSnapshotTransferVersion = 1;
inline constexpr std::size_t runtimeMultiplayerSnapshotSha256Bytes = 32U;
inline constexpr std::size_t runtimeMultiplayerSnapshotMaximumTokenBytes = 96U;
inline constexpr std::size_t runtimeMultiplayerSnapshotChunkBytes =
    256U * 1024U;
inline constexpr std::size_t runtimeMultiplayerSnapshotDefaultMaximumBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t runtimeMultiplayerSnapshotHardMaximumBytes =
    128U * 1024U * 1024U;
inline constexpr std::size_t runtimeMultiplayerSnapshotMaximumChunks =
    runtimeMultiplayerSnapshotHardMaximumBytes
        / runtimeMultiplayerSnapshotChunkBytes;
inline constexpr int runtimeMultiplayerSnapshotMaximumTick = 2000000000;

using RuntimeMultiplayerSnapshotSha256 = std::array<
    std::uint8_t,
    runtimeMultiplayerSnapshotSha256Bytes
>;

enum class RuntimeMultiplayerSnapshotKind : std::uint8_t {
    Save = 1,
    Load = 2
};

// Compression is deliberately forbidden in v1. A future compressed schema
// must separately bound decoded bytes instead of weakening this contract.
enum class RuntimeMultiplayerSnapshotCompression : std::uint8_t {
    None = 0
};

struct RuntimeMultiplayerSnapshotIdentity {
    std::uint64_t proposalId = 0;
    RuntimeMultiplayerSnapshotKind kind =
        RuntimeMultiplayerSnapshotKind::Save;
    std::uint64_t sourceTimelineEpoch = 0;
    int barrierTick = -1;
    int snapshotTick = -1;
    std::string snapshotToken;
    std::uint64_t authoritativeStateHash = 0;
    std::uint64_t gameplaySetupFingerprint = 0;

    bool operator==(
        const RuntimeMultiplayerSnapshotIdentity&
    ) const = default;
};

struct RuntimeMultiplayerSnapshotDescriptor {
    RuntimeMultiplayerSnapshotIdentity identity;
    RuntimeMultiplayerSnapshotCompression compression =
        RuntimeMultiplayerSnapshotCompression::None;
    std::uint64_t byteCount = 0;
    std::uint32_t chunkBytes = static_cast<std::uint32_t>(
        runtimeMultiplayerSnapshotChunkBytes
    );
    std::uint32_t chunkCount = 0;
    std::uint64_t snapshotFingerprint = 0;
    RuntimeMultiplayerSnapshotSha256 sha256{};

    bool operator==(
        const RuntimeMultiplayerSnapshotDescriptor&
    ) const = default;
};

struct RuntimeMultiplayerSnapshotChunk {
    std::uint64_t proposalId = 0;
    std::uint64_t sourceTimelineEpoch = 0;
    std::uint32_t chunkIndex = 0;
    std::vector<std::uint8_t> bytes;

    bool operator==(const RuntimeMultiplayerSnapshotChunk&) const = default;
};

struct RuntimeMultiplayerSnapshotChunkAck {
    std::uint64_t proposalId = 0;
    std::uint64_t sourceTimelineEpoch = 0;
    std::uint32_t chunkIndex = 0;

    bool operator==(
        const RuntimeMultiplayerSnapshotChunkAck&
    ) const = default;
};

struct RuntimeMultiplayerSnapshotTransferResult {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    bool complete = false;
    std::string reason;
};

// Tokens are opaque identifiers, never filesystem paths. The grammar is
// ASCII alphanumeric at both ends, with alphanumeric, '-', '_', or single
// '.' characters internally. Path separators, drive separators, whitespace,
// dot segments, and consecutive dots are rejected.
bool runtimeMultiplayerSnapshotTokenIsSafe(std::string_view token);

std::uint64_t runtimeMultiplayerSnapshotFingerprint(
    std::span<const std::uint8_t> bytes
);

RuntimeMultiplayerSnapshotSha256 runtimeMultiplayerSnapshotSha256(
    std::span<const std::uint8_t> bytes
);

bool validateRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
);

// Computes all byte-derived descriptor fields. The identity must already be
// valid. Both outputs are unchanged on failure.
bool makeRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotIdentity& identity,
    std::span<const std::uint8_t> bytes,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
);

// Canonical fixed-width big-endian codecs. Strings/byte arrays carry explicit
// big-endian lengths. Decoders reject truncation, trailing bytes, noncanonical
// descriptor values, and configured/hard limits transactionally.
bool encodeRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
);

bool decodeRuntimeMultiplayerSnapshotDescriptor(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
);

bool encodeRuntimeMultiplayerSnapshotChunk(
    const RuntimeMultiplayerSnapshotChunk& chunk,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
);

bool decodeRuntimeMultiplayerSnapshotChunk(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotChunk& chunk,
    std::string& errorMessage
);

bool encodeRuntimeMultiplayerSnapshotChunkAck(
    const RuntimeMultiplayerSnapshotChunkAck& ack,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
);

bool decodeRuntimeMultiplayerSnapshotChunkAck(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotChunkAck& ack,
    std::string& errorMessage
);

// Owns exactly one canonical byte image. currentChunk() keeps returning the
// same value until its exact acknowledgement is accepted, enforcing one
// in-flight 256 KiB chunk per peer/coordinator instance.
class RuntimeMultiplayerSnapshotChunkProducer {
public:
    RuntimeMultiplayerSnapshotTransferResult begin(
        RuntimeMultiplayerSnapshotDescriptor descriptor,
        std::vector<std::uint8_t> bytes,
        std::size_t maximumBytes =
            runtimeMultiplayerSnapshotDefaultMaximumBytes
    );

    bool currentChunk(
        RuntimeMultiplayerSnapshotChunk& chunk,
        std::string& errorMessage
    ) const;

    RuntimeMultiplayerSnapshotTransferResult acknowledge(
        const RuntimeMultiplayerSnapshotChunkAck& ack
    );

    void reset() noexcept;
    bool active() const;
    bool complete() const;
    const RuntimeMultiplayerSnapshotDescriptor* descriptor() const;

private:
    RuntimeMultiplayerSnapshotDescriptor descriptor_;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t awaitingChunkIndex_ = 0;
    std::uint32_t lastAcknowledgedChunkIndex_ = 0;
    bool active_ = false;
    bool complete_ = false;
    bool haveLastAcknowledgement_ = false;
};

// Receives one descriptor at a time. No byte image is observable until every
// exact chunk has arrived and both the 64-bit fingerprint and SHA-256 match.
class RuntimeMultiplayerSnapshotChunkAssembler {
public:
    RuntimeMultiplayerSnapshotTransferResult begin(
        RuntimeMultiplayerSnapshotDescriptor descriptor,
        std::size_t maximumBytes =
            runtimeMultiplayerSnapshotDefaultMaximumBytes
    );

    RuntimeMultiplayerSnapshotTransferResult acceptChunk(
        const RuntimeMultiplayerSnapshotChunk& chunk
    );

    bool latestAcknowledgement(
        RuntimeMultiplayerSnapshotChunkAck& ack,
        std::string& errorMessage
    ) const;

    // Moves the verified image to output and resets the assembler. Output is
    // unchanged unless the transfer is complete.
    bool takeCompletedBytes(
        std::vector<std::uint8_t>& bytes,
        std::string& errorMessage
    );

    void reset() noexcept;
    bool active() const;
    bool complete() const;
    std::size_t receivedBytes() const;
    const RuntimeMultiplayerSnapshotDescriptor* descriptor() const;

private:
    bool lastChunkMatches(
        const RuntimeMultiplayerSnapshotChunk& chunk
    ) const;

    RuntimeMultiplayerSnapshotDescriptor descriptor_;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t nextChunkIndex_ = 0;
    std::uint32_t lastAcceptedChunkIndex_ = 0;
    std::size_t lastAcceptedOffset_ = 0;
    std::size_t lastAcceptedSize_ = 0;
    bool active_ = false;
    bool complete_ = false;
    bool haveLastAcceptedChunk_ = false;
};
