#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kDescriptorMaximumPayloadBytes =
    106U + runtimeMultiplayerSnapshotMaximumTokenBytes;
constexpr std::size_t kChunkEnvelopeBytes = 30U;
constexpr std::size_t kAckMaximumPayloadBytes = 26U;

class ByteWriter {
public:
    void writeU8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void writeU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(shift)
            ));
        }
    }

    void writeString(const std::string& value) {
        writeU16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void writeByteVector(const std::vector<std::uint8_t>& value) {
        writeU32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    template <std::size_t Size>
    void writeArray(const std::array<std::uint8_t, Size>& value) {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    const std::vector<std::uint8_t>& bytes() const {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    bool readU8(std::uint8_t& value) {
        if (!require(1U)) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool readU16(std::uint16_t& value) {
        if (!require(2U)) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8U)
            | static_cast<std::uint16_t>(bytes_[offset_ + 1U])
        );
        offset_ += 2U;
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4U)) return false;
        value =
            (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3U]);
        offset_ += 4U;
        return true;
    }

    bool readU64(std::uint64_t& value) {
        if (!require(8U)) return false;
        value = 0U;
        for (std::size_t index = 0; index < 8U; ++index) {
            value = (value << 8U)
                | static_cast<std::uint64_t>(bytes_[offset_ + index]);
        }
        offset_ += 8U;
        return true;
    }

    bool readString(
        std::size_t maximumBytes,
        std::string& value,
        const char* fieldName
    ) {
        std::uint16_t length = 0U;
        if (!readU16(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("snapshot ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            static_cast<std::size_t>(length)
        );
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool readByteVector(
        std::size_t maximumBytes,
        std::vector<std::uint8_t>& value,
        const char* fieldName
    ) {
        std::uint32_t length = 0U;
        if (!readU32(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("snapshot ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.assign(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(
                offset_ + static_cast<std::size_t>(length)
            )
        );
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    template <std::size_t Size>
    bool readArray(std::array<std::uint8_t, Size>& value) {
        if (!require(Size)) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    Size,
                    value.begin());
        offset_ += Size;
        return true;
    }

    bool finished() const {
        return offset_ == bytes_.size();
    }

    bool fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    const std::string& error() const {
        return error_;
    }

private:
    bool require(std::size_t count) {
        if (offset_ <= bytes_.size()
            && count <= bytes_.size() - offset_) {
            return true;
        }
        return fail("snapshot payload is truncated");
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
    std::string error_;
};

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

RuntimeMultiplayerSnapshotTransferResult rejected(std::string reason) {
    RuntimeMultiplayerSnapshotTransferResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerSnapshotTransferResult accepted(
    bool changed,
    bool duplicate,
    bool complete
) {
    RuntimeMultiplayerSnapshotTransferResult result;
    result.accepted = true;
    result.changed = changed;
    result.duplicate = duplicate;
    result.complete = complete;
    return result;
}

bool validMaximumBytes(std::size_t maximumBytes) {
    return maximumBytes > 0U
        && maximumBytes <= runtimeMultiplayerSnapshotHardMaximumBytes;
}

bool validSequence(std::uint64_t value) {
    return value > 0U
        && value <= static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()
        );
}

bool validTick(int tick) {
    return tick >= 0 && tick <= runtimeMultiplayerSnapshotMaximumTick;
}

bool validKind(RuntimeMultiplayerSnapshotKind kind) {
    return kind == RuntimeMultiplayerSnapshotKind::Save
        || kind == RuntimeMultiplayerSnapshotKind::Load;
}

bool shaIsNonzero(const RuntimeMultiplayerSnapshotSha256& sha256) {
    return std::any_of(
        sha256.begin(),
        sha256.end(),
        [](std::uint8_t byte) { return byte != 0U; }
    );
}

std::uint32_t exactChunkCount(std::uint64_t byteCount) {
    if (byteCount == 0U) return 0U;
    const std::uint64_t chunkBytes = static_cast<std::uint64_t>(
        runtimeMultiplayerSnapshotChunkBytes
    );
    return static_cast<std::uint32_t>(
        (byteCount + chunkBytes - 1U) / chunkBytes
    );
}

std::size_t exactChunkSize(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::uint32_t chunkIndex
) {
    const std::uint64_t chunkBytes = descriptor.chunkBytes;
    const std::uint64_t offset =
        static_cast<std::uint64_t>(chunkIndex) * chunkBytes;
    if (offset >= descriptor.byteCount) return 0U;
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        chunkBytes,
        descriptor.byteCount - offset
    ));
}

bool validChunkEnvelope(
    const RuntimeMultiplayerSnapshotChunk& chunk,
    std::string& errorMessage
) {
    if (!validSequence(chunk.proposalId)) {
        return fail(errorMessage, "snapshot chunk proposal id is invalid");
    }
    if (!validSequence(chunk.sourceTimelineEpoch)) {
        return fail(errorMessage, "snapshot chunk timeline epoch is invalid");
    }
    if (chunk.chunkIndex >= runtimeMultiplayerSnapshotMaximumChunks) {
        return fail(errorMessage, "snapshot chunk index exceeds the limit");
    }
    if (chunk.bytes.empty()
        || chunk.bytes.size() > runtimeMultiplayerSnapshotChunkBytes) {
        return fail(errorMessage, "snapshot chunk byte length is invalid");
    }
    return true;
}

bool validAckEnvelope(
    const RuntimeMultiplayerSnapshotChunkAck& ack,
    std::string& errorMessage
) {
    if (!validSequence(ack.proposalId)) {
        return fail(errorMessage, "snapshot ack proposal id is invalid");
    }
    if (!validSequence(ack.sourceTimelineEpoch)) {
        return fail(errorMessage, "snapshot ack timeline epoch is invalid");
    }
    if (ack.chunkIndex >= runtimeMultiplayerSnapshotMaximumChunks) {
        return fail(errorMessage, "snapshot ack chunk index exceeds the limit");
    }
    return true;
}

constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32U - bits));
}

void sha256Transform(
    std::array<std::uint32_t, 8U>& state,
    const std::uint8_t* block
) {
    static constexpr std::array<std::uint32_t, 64U> constants{{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    }};

    std::array<std::uint32_t, 64U> schedule{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::size_t offset = index * 4U;
        schedule[index] =
            (static_cast<std::uint32_t>(block[offset]) << 24U)
            | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
            | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
            | static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < schedule.size(); ++index) {
        const std::uint32_t first = schedule[index - 15U];
        const std::uint32_t second = schedule[index - 2U];
        const std::uint32_t sigma0 = rotateRight(first, 7U)
            ^ rotateRight(first, 18U) ^ (first >> 3U);
        const std::uint32_t sigma1 = rotateRight(second, 17U)
            ^ rotateRight(second, 19U) ^ (second >> 10U);
        schedule[index] = schedule[index - 16U] + sigma0
            + schedule[index - 7U] + sigma1;
    }

    std::uint32_t a = state[0U];
    std::uint32_t b = state[1U];
    std::uint32_t c = state[2U];
    std::uint32_t d = state[3U];
    std::uint32_t e = state[4U];
    std::uint32_t f = state[5U];
    std::uint32_t g = state[6U];
    std::uint32_t h = state[7U];

    for (std::size_t index = 0U; index < schedule.size(); ++index) {
        const std::uint32_t sum1 = rotateRight(e, 6U)
            ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + sum1 + choose
            + constants[index] + schedule[index];
        const std::uint32_t sum0 = rotateRight(a, 2U)
            ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0U] += a;
    state[1U] += b;
    state[2U] += c;
    state[3U] += d;
    state[4U] += e;
    state[5U] += f;
    state[6U] += g;
    state[7U] += h;
}

} // namespace

bool runtimeMultiplayerSnapshotTokenIsSafe(std::string_view token) {
    if (token.empty()
        || token.size() > runtimeMultiplayerSnapshotMaximumTokenBytes) {
        return false;
    }
    const auto isAlphaNumeric = [](unsigned char byte) {
        return (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
    };
    if (!isAlphaNumeric(static_cast<unsigned char>(token.front()))
        || !isAlphaNumeric(static_cast<unsigned char>(token.back()))) {
        return false;
    }
    bool previousDot = false;
    for (const char character : token) {
        const unsigned char byte = static_cast<unsigned char>(character);
        const bool dot = byte == static_cast<unsigned char>('.');
        if (!isAlphaNumeric(byte)
            && byte != static_cast<unsigned char>('-')
            && byte != static_cast<unsigned char>('_')
            && !dot) {
            return false;
        }
        if (dot && previousDot) return false;
        previousDot = dot;
    }
    return true;
}

std::uint64_t runtimeMultiplayerSnapshotFingerprint(
    std::span<const std::uint8_t> bytes
) {
    static constexpr std::array<std::uint8_t, 28U> domain{{
        'C', 'o', 'n', 'c', 'o', 'r', 'd', 'R', 'T', 'S', '/',
        'S', 'n', 'a', 'p', 's', 'h', 'o', 't', 'B', 'y', 't',
        'e', 's', '/', 'v', '1', 0U
    }};
    std::uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= kFnvPrime;
    };
    for (const std::uint8_t byte : domain) mix(byte);
    const std::uint64_t size = static_cast<std::uint64_t>(bytes.size());
    for (int shift = 56; shift >= 0; shift -= 8) {
        mix(static_cast<std::uint8_t>(
            size >> static_cast<unsigned int>(shift)
        ));
    }
    for (const std::uint8_t byte : bytes) mix(byte);
    return hash;
}

RuntimeMultiplayerSnapshotSha256 runtimeMultiplayerSnapshotSha256(
    std::span<const std::uint8_t> bytes
) {
    std::array<std::uint32_t, 8U> state{{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    }};

    std::size_t offset = 0U;
    while (bytes.size() - offset >= 64U) {
        sha256Transform(state, bytes.data() + offset);
        offset += 64U;
    }

    std::array<std::uint8_t, 128U> tail{};
    const std::size_t remaining = bytes.size() - offset;
    if (remaining != 0U) {
        std::copy_n(bytes.data() + offset, remaining, tail.begin());
    }
    tail[remaining] = 0x80U;
    const std::size_t blockCount = remaining < 56U ? 1U : 2U;
    const std::uint64_t bitLength =
        static_cast<std::uint64_t>(bytes.size()) * 8U;
    const std::size_t lengthOffset = blockCount * 64U - 8U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        const unsigned int shift = static_cast<unsigned int>(
            (7U - index) * 8U
        );
        tail[lengthOffset + index] = static_cast<std::uint8_t>(
            bitLength >> shift
        );
    }
    for (std::size_t index = 0U; index < blockCount; ++index) {
        sha256Transform(state, tail.data() + index * 64U);
    }

    RuntimeMultiplayerSnapshotSha256 digest{};
    for (std::size_t word = 0U; word < state.size(); ++word) {
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            const unsigned int shift = static_cast<unsigned int>(
                (3U - byte) * 8U
            );
            digest[word * 4U + byte] = static_cast<std::uint8_t>(
                state[word] >> shift
            );
        }
    }
    return digest;
}

bool validateRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes
) {
    errorMessage.clear();
    if (!validMaximumBytes(maximumBytes)) {
        return fail(errorMessage, "snapshot configured byte limit is invalid");
    }
    const RuntimeMultiplayerSnapshotIdentity& identity = descriptor.identity;
    if (!validSequence(identity.proposalId)) {
        return fail(errorMessage, "snapshot proposal id is invalid");
    }
    if (!validKind(identity.kind)) {
        return fail(errorMessage, "snapshot kind is invalid");
    }
    if (!validSequence(identity.sourceTimelineEpoch)) {
        return fail(errorMessage, "snapshot source timeline epoch is invalid");
    }
    if (!validTick(identity.barrierTick)
        || !validTick(identity.snapshotTick)) {
        return fail(errorMessage, "snapshot tick is invalid");
    }
    if (identity.kind == RuntimeMultiplayerSnapshotKind::Save
        && identity.snapshotTick != identity.barrierTick) {
        return fail(
            errorMessage,
            "save snapshot tick must equal the barrier tick"
        );
    }
    if (identity.kind == RuntimeMultiplayerSnapshotKind::Load
        && identity.snapshotTick > identity.barrierTick) {
        return fail(
            errorMessage,
            "load snapshot tick may not be ahead of the barrier"
        );
    }
    if (!runtimeMultiplayerSnapshotTokenIsSafe(identity.snapshotToken)) {
        return fail(errorMessage, "snapshot token is not a safe opaque id");
    }
    if (identity.authoritativeStateHash == 0U) {
        return fail(errorMessage, "snapshot authoritative state hash is zero");
    }
    if (identity.gameplaySetupFingerprint == 0U) {
        return fail(errorMessage, "snapshot gameplay setup fingerprint is zero");
    }
    if (descriptor.compression
        != RuntimeMultiplayerSnapshotCompression::None) {
        return fail(errorMessage, "snapshot compression is unsupported");
    }
    if (descriptor.byteCount == 0U
        || descriptor.byteCount > static_cast<std::uint64_t>(maximumBytes)
        || descriptor.byteCount > static_cast<std::uint64_t>(
            runtimeMultiplayerSnapshotHardMaximumBytes
        )) {
        return fail(errorMessage, "snapshot byte count exceeds the limit");
    }
    if (descriptor.chunkBytes != runtimeMultiplayerSnapshotChunkBytes) {
        return fail(errorMessage, "snapshot chunk size is not canonical");
    }
    const std::uint32_t expectedChunks = exactChunkCount(
        descriptor.byteCount
    );
    if (descriptor.chunkCount == 0U
        || descriptor.chunkCount != expectedChunks
        || descriptor.chunkCount > runtimeMultiplayerSnapshotMaximumChunks) {
        return fail(errorMessage, "snapshot chunk count is inconsistent");
    }
    if (descriptor.snapshotFingerprint == 0U) {
        return fail(errorMessage, "snapshot fingerprint is zero");
    }
    if (!shaIsNonzero(descriptor.sha256)) {
        return fail(errorMessage, "snapshot SHA-256 is zero");
    }
    return true;
}

bool makeRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotIdentity& identity,
    std::span<const std::uint8_t> bytes,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes
) {
    errorMessage.clear();
    if (!validMaximumBytes(maximumBytes)) {
        return fail(errorMessage, "snapshot configured byte limit is invalid");
    }
    if (bytes.empty()
        || bytes.size() > maximumBytes
        || bytes.size() > runtimeMultiplayerSnapshotHardMaximumBytes) {
        return fail(errorMessage, "snapshot byte count exceeds the limit");
    }

    RuntimeMultiplayerSnapshotDescriptor candidate;
    candidate.identity = identity;
    candidate.byteCount = static_cast<std::uint64_t>(bytes.size());
    candidate.chunkBytes = static_cast<std::uint32_t>(
        runtimeMultiplayerSnapshotChunkBytes
    );
    candidate.chunkCount = exactChunkCount(candidate.byteCount);
    // Validate identity, tick, token, shape, and limits before hashing a
    // potentially large (but bounded) image. The sentinels are replaced
    // immediately after this structural pass.
    candidate.snapshotFingerprint = 1U;
    candidate.sha256[0U] = 1U;
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            candidate,
            errorMessage,
            maximumBytes
        )) {
        return false;
    }
    candidate.snapshotFingerprint =
        runtimeMultiplayerSnapshotFingerprint(bytes);
    candidate.sha256 = runtimeMultiplayerSnapshotSha256(bytes);
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            candidate,
            errorMessage,
            maximumBytes
        )) {
        return false;
    }
    descriptor = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerSnapshotDescriptor(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage,
    std::size_t maximumBytes
) {
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            errorMessage,
            maximumBytes
        )) {
        return false;
    }
    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerSnapshotDescriptorMagic);
    writer.writeU16(runtimeMultiplayerSnapshotTransferVersion);
    writer.writeU8(static_cast<std::uint8_t>(descriptor.identity.kind));
    writer.writeU8(static_cast<std::uint8_t>(descriptor.compression));
    writer.writeU64(descriptor.identity.proposalId);
    writer.writeU64(descriptor.identity.sourceTimelineEpoch);
    writer.writeU32(static_cast<std::uint32_t>(
        descriptor.identity.barrierTick
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        descriptor.identity.snapshotTick
    ));
    writer.writeString(descriptor.identity.snapshotToken);
    writer.writeU64(descriptor.identity.authoritativeStateHash);
    writer.writeU64(descriptor.identity.gameplaySetupFingerprint);
    writer.writeU64(descriptor.byteCount);
    writer.writeU32(descriptor.chunkBytes);
    writer.writeU32(descriptor.chunkCount);
    writer.writeU64(descriptor.snapshotFingerprint);
    writer.writeArray(descriptor.sha256);
    if (writer.bytes().size() > kDescriptorMaximumPayloadBytes) {
        return fail(errorMessage, "snapshot descriptor payload is too large");
    }
    payload = writer.bytes();
    errorMessage.clear();
    return true;
}

bool decodeRuntimeMultiplayerSnapshotDescriptor(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage,
    std::size_t maximumBytes
) {
    errorMessage.clear();
    if (payload.size() > kDescriptorMaximumPayloadBytes) {
        return fail(errorMessage, "snapshot descriptor payload is too large");
    }
    ByteReader reader(payload);
    RuntimeMultiplayerSnapshotDescriptor candidate;
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint8_t kind = 0U;
    std::uint8_t compression = 0U;
    std::uint32_t barrierTick = 0U;
    std::uint32_t snapshotTick = 0U;
    if (!reader.readU32(magic) || !reader.readU16(version)) {
        return fail(errorMessage, reader.error());
    }
    if (magic != runtimeMultiplayerSnapshotDescriptorMagic) {
        return fail(errorMessage, "snapshot descriptor magic is invalid");
    }
    if (version != runtimeMultiplayerSnapshotTransferVersion) {
        return fail(errorMessage, "snapshot descriptor version is unsupported");
    }
    if (!reader.readU8(kind)
        || !reader.readU8(compression)
        || !reader.readU64(candidate.identity.proposalId)
        || !reader.readU64(candidate.identity.sourceTimelineEpoch)
        || !reader.readU32(barrierTick)
        || !reader.readU32(snapshotTick)
        || !reader.readString(
            runtimeMultiplayerSnapshotMaximumTokenBytes,
            candidate.identity.snapshotToken,
            "token"
        )
        || !reader.readU64(candidate.identity.authoritativeStateHash)
        || !reader.readU64(candidate.identity.gameplaySetupFingerprint)
        || !reader.readU64(candidate.byteCount)
        || !reader.readU32(candidate.chunkBytes)
        || !reader.readU32(candidate.chunkCount)
        || !reader.readU64(candidate.snapshotFingerprint)
        || !reader.readArray(candidate.sha256)) {
        return fail(errorMessage, reader.error());
    }
    if (barrierTick
            > static_cast<std::uint32_t>(
                runtimeMultiplayerSnapshotMaximumTick
            )
        || snapshotTick
            > static_cast<std::uint32_t>(
                runtimeMultiplayerSnapshotMaximumTick
            )) {
        return fail(errorMessage, "snapshot descriptor tick is out of range");
    }
    candidate.identity.kind =
        static_cast<RuntimeMultiplayerSnapshotKind>(kind);
    candidate.compression =
        static_cast<RuntimeMultiplayerSnapshotCompression>(compression);
    candidate.identity.barrierTick = static_cast<int>(barrierTick);
    candidate.identity.snapshotTick = static_cast<int>(snapshotTick);
    if (!reader.finished()) {
        return fail(errorMessage, "snapshot descriptor has trailing bytes");
    }
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            candidate,
            errorMessage,
            maximumBytes
        )) {
        return false;
    }
    descriptor = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerSnapshotChunk(
    const RuntimeMultiplayerSnapshotChunk& chunk,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!validChunkEnvelope(chunk, errorMessage)) return false;
    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerSnapshotChunkMagic);
    writer.writeU16(runtimeMultiplayerSnapshotTransferVersion);
    writer.writeU64(chunk.proposalId);
    writer.writeU64(chunk.sourceTimelineEpoch);
    writer.writeU32(chunk.chunkIndex);
    writer.writeByteVector(chunk.bytes);
    payload = writer.bytes();
    return true;
}

bool decodeRuntimeMultiplayerSnapshotChunk(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotChunk& chunk,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (payload.size()
        > runtimeMultiplayerSnapshotChunkBytes + kChunkEnvelopeBytes) {
        return fail(errorMessage, "snapshot chunk payload exceeds the limit");
    }
    ByteReader reader(payload);
    RuntimeMultiplayerSnapshotChunk candidate;
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    if (!reader.readU32(magic) || !reader.readU16(version)) {
        return fail(errorMessage, reader.error());
    }
    if (magic != runtimeMultiplayerSnapshotChunkMagic) {
        return fail(errorMessage, "snapshot chunk magic is invalid");
    }
    if (version != runtimeMultiplayerSnapshotTransferVersion) {
        return fail(errorMessage, "snapshot chunk version is unsupported");
    }
    if (!reader.readU64(candidate.proposalId)
        || !reader.readU64(candidate.sourceTimelineEpoch)
        || !reader.readU32(candidate.chunkIndex)
        || !reader.readByteVector(
            runtimeMultiplayerSnapshotChunkBytes,
            candidate.bytes,
            "chunk bytes"
        )) {
        return fail(errorMessage, reader.error());
    }
    if (!reader.finished()) {
        return fail(errorMessage, "snapshot chunk has trailing bytes");
    }
    if (!validChunkEnvelope(candidate, errorMessage)) return false;
    chunk = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerSnapshotChunkAck(
    const RuntimeMultiplayerSnapshotChunkAck& ack,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!validAckEnvelope(ack, errorMessage)) return false;
    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerSnapshotAckMagic);
    writer.writeU16(runtimeMultiplayerSnapshotTransferVersion);
    writer.writeU64(ack.proposalId);
    writer.writeU64(ack.sourceTimelineEpoch);
    writer.writeU32(ack.chunkIndex);
    payload = writer.bytes();
    return true;
}

bool decodeRuntimeMultiplayerSnapshotChunkAck(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerSnapshotChunkAck& ack,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (payload.size() > kAckMaximumPayloadBytes) {
        return fail(errorMessage, "snapshot ack payload exceeds the limit");
    }
    ByteReader reader(payload);
    RuntimeMultiplayerSnapshotChunkAck candidate;
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    if (!reader.readU32(magic) || !reader.readU16(version)) {
        return fail(errorMessage, reader.error());
    }
    if (magic != runtimeMultiplayerSnapshotAckMagic) {
        return fail(errorMessage, "snapshot ack magic is invalid");
    }
    if (version != runtimeMultiplayerSnapshotTransferVersion) {
        return fail(errorMessage, "snapshot ack version is unsupported");
    }
    if (!reader.readU64(candidate.proposalId)
        || !reader.readU64(candidate.sourceTimelineEpoch)
        || !reader.readU32(candidate.chunkIndex)) {
        return fail(errorMessage, reader.error());
    }
    if (!reader.finished()) {
        return fail(errorMessage, "snapshot ack has trailing bytes");
    }
    if (!validAckEnvelope(candidate, errorMessage)) return false;
    ack = candidate;
    errorMessage.clear();
    return true;
}

RuntimeMultiplayerSnapshotTransferResult
RuntimeMultiplayerSnapshotChunkProducer::begin(
    RuntimeMultiplayerSnapshotDescriptor descriptor,
    std::vector<std::uint8_t> bytes,
    std::size_t maximumBytes
) {
    if (active_) {
        return rejected("another snapshot producer transfer is active");
    }
    std::string errorMessage;
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            errorMessage,
            maximumBytes
        )) {
        return rejected(std::move(errorMessage));
    }
    if (bytes.size() != static_cast<std::size_t>(descriptor.byteCount)) {
        return rejected("snapshot producer byte count does not match");
    }
    if (runtimeMultiplayerSnapshotFingerprint(bytes)
            != descriptor.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes) != descriptor.sha256) {
        return rejected("snapshot producer bytes do not match the descriptor");
    }

    descriptor_ = std::move(descriptor);
    bytes_ = std::move(bytes);
    awaitingChunkIndex_ = 0U;
    lastAcknowledgedChunkIndex_ = 0U;
    active_ = true;
    complete_ = false;
    haveLastAcknowledgement_ = false;
    return accepted(true, false, false);
}

bool RuntimeMultiplayerSnapshotChunkProducer::currentChunk(
    RuntimeMultiplayerSnapshotChunk& chunk,
    std::string& errorMessage
) const {
    errorMessage.clear();
    if (!active_) {
        return fail(errorMessage, "snapshot producer is inactive");
    }
    if (complete_) {
        return fail(errorMessage, "snapshot producer is complete");
    }
    const std::size_t size = exactChunkSize(
        descriptor_,
        awaitingChunkIndex_
    );
    if (size == 0U) {
        return fail(errorMessage, "snapshot producer chunk state is invalid");
    }
    const std::size_t offset = static_cast<std::size_t>(
        static_cast<std::uint64_t>(awaitingChunkIndex_)
            * descriptor_.chunkBytes
    );
    try {
        RuntimeMultiplayerSnapshotChunk candidate;
        candidate.proposalId = descriptor_.identity.proposalId;
        candidate.sourceTimelineEpoch =
            descriptor_.identity.sourceTimelineEpoch;
        candidate.chunkIndex = awaitingChunkIndex_;
        candidate.bytes.assign(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset + size)
        );
        chunk = std::move(candidate);
        return true;
    } catch (...) {
        return fail(errorMessage, "snapshot producer could not copy a chunk");
    }
}

RuntimeMultiplayerSnapshotTransferResult
RuntimeMultiplayerSnapshotChunkProducer::acknowledge(
    const RuntimeMultiplayerSnapshotChunkAck& ack
) {
    if (!active_) return rejected("snapshot producer is inactive");
    std::string errorMessage;
    if (!validAckEnvelope(ack, errorMessage)) {
        return rejected(std::move(errorMessage));
    }
    if (ack.proposalId != descriptor_.identity.proposalId
        || ack.sourceTimelineEpoch
            != descriptor_.identity.sourceTimelineEpoch) {
        return rejected("snapshot ack belongs to another transfer");
    }
    if (haveLastAcknowledgement_
        && ack.chunkIndex == lastAcknowledgedChunkIndex_) {
        return accepted(false, true, complete_);
    }
    if (complete_) {
        return rejected("snapshot producer received an ack after completion");
    }
    if (ack.chunkIndex != awaitingChunkIndex_) {
        return rejected("snapshot ack is out of order");
    }

    lastAcknowledgedChunkIndex_ = ack.chunkIndex;
    haveLastAcknowledgement_ = true;
    ++awaitingChunkIndex_;
    complete_ = awaitingChunkIndex_ == descriptor_.chunkCount;
    return accepted(true, false, complete_);
}

void RuntimeMultiplayerSnapshotChunkProducer::reset() noexcept {
    descriptor_ = RuntimeMultiplayerSnapshotDescriptor{};
    std::vector<std::uint8_t>().swap(bytes_);
    awaitingChunkIndex_ = 0U;
    lastAcknowledgedChunkIndex_ = 0U;
    active_ = false;
    complete_ = false;
    haveLastAcknowledgement_ = false;
}

bool RuntimeMultiplayerSnapshotChunkProducer::active() const {
    return active_;
}

bool RuntimeMultiplayerSnapshotChunkProducer::complete() const {
    return active_ && complete_;
}

const RuntimeMultiplayerSnapshotDescriptor*
RuntimeMultiplayerSnapshotChunkProducer::descriptor() const {
    return active_ ? &descriptor_ : nullptr;
}

RuntimeMultiplayerSnapshotTransferResult
RuntimeMultiplayerSnapshotChunkAssembler::begin(
    RuntimeMultiplayerSnapshotDescriptor descriptor,
    std::size_t maximumBytes
) {
    if (active_) {
        return rejected("another snapshot assembler transfer is active");
    }
    std::string errorMessage;
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            errorMessage,
            maximumBytes
        )) {
        return rejected(std::move(errorMessage));
    }
    std::vector<std::uint8_t> storage;
    try {
        storage.reserve(static_cast<std::size_t>(descriptor.byteCount));
    } catch (...) {
        return rejected("snapshot assembler could not reserve bounded storage");
    }

    descriptor_ = std::move(descriptor);
    bytes_ = std::move(storage);
    nextChunkIndex_ = 0U;
    lastAcceptedChunkIndex_ = 0U;
    lastAcceptedOffset_ = 0U;
    lastAcceptedSize_ = 0U;
    active_ = true;
    complete_ = false;
    haveLastAcceptedChunk_ = false;
    return accepted(true, false, false);
}

bool RuntimeMultiplayerSnapshotChunkAssembler::lastChunkMatches(
    const RuntimeMultiplayerSnapshotChunk& chunk
) const {
    if (!haveLastAcceptedChunk_
        || chunk.chunkIndex != lastAcceptedChunkIndex_
        || chunk.bytes.size() != lastAcceptedSize_
        || lastAcceptedOffset_ > bytes_.size()
        || lastAcceptedSize_ > bytes_.size() - lastAcceptedOffset_) {
        return false;
    }
    return std::equal(
        chunk.bytes.begin(),
        chunk.bytes.end(),
        bytes_.begin() + static_cast<std::ptrdiff_t>(lastAcceptedOffset_)
    );
}

RuntimeMultiplayerSnapshotTransferResult
RuntimeMultiplayerSnapshotChunkAssembler::acceptChunk(
    const RuntimeMultiplayerSnapshotChunk& chunk
) {
    if (!active_) return rejected("snapshot assembler is inactive");
    std::string errorMessage;
    if (!validChunkEnvelope(chunk, errorMessage)) {
        return rejected(std::move(errorMessage));
    }
    if (chunk.proposalId != descriptor_.identity.proposalId
        || chunk.sourceTimelineEpoch
            != descriptor_.identity.sourceTimelineEpoch) {
        return rejected("snapshot chunk belongs to another transfer");
    }
    if (haveLastAcceptedChunk_
        && chunk.chunkIndex == lastAcceptedChunkIndex_) {
        if (!lastChunkMatches(chunk)) {
            return rejected("snapshot duplicate chunk conflicts with accepted data");
        }
        return accepted(false, true, complete_);
    }
    if (complete_) {
        return rejected("snapshot assembler received data after completion");
    }
    if (chunk.chunkIndex != nextChunkIndex_) {
        return rejected("snapshot chunk is out of order");
    }
    const std::size_t expectedSize = exactChunkSize(
        descriptor_,
        chunk.chunkIndex
    );
    if (expectedSize == 0U || chunk.bytes.size() != expectedSize) {
        return rejected("snapshot chunk length is inconsistent");
    }

    const std::size_t oldSize = bytes_.size();
    try {
        bytes_.insert(bytes_.end(), chunk.bytes.begin(), chunk.bytes.end());
    } catch (...) {
        bytes_.resize(oldSize);
        return rejected("snapshot assembler could not append a chunk");
    }
    lastAcceptedChunkIndex_ = chunk.chunkIndex;
    lastAcceptedOffset_ = oldSize;
    lastAcceptedSize_ = chunk.bytes.size();
    haveLastAcceptedChunk_ = true;
    ++nextChunkIndex_;

    if (nextChunkIndex_ == descriptor_.chunkCount) {
        const bool valid =
            bytes_.size() == static_cast<std::size_t>(descriptor_.byteCount)
            && runtimeMultiplayerSnapshotFingerprint(bytes_)
                == descriptor_.snapshotFingerprint
            && runtimeMultiplayerSnapshotSha256(bytes_)
                == descriptor_.sha256;
        if (!valid) {
            reset();
            return rejected(
                "completed snapshot bytes failed fingerprint or SHA-256"
            );
        }
        complete_ = true;
    }
    return accepted(true, false, complete_);
}

bool RuntimeMultiplayerSnapshotChunkAssembler::latestAcknowledgement(
    RuntimeMultiplayerSnapshotChunkAck& ack,
    std::string& errorMessage
) const {
    errorMessage.clear();
    if (!active_ || !haveLastAcceptedChunk_) {
        return fail(errorMessage, "snapshot assembler has no accepted chunk");
    }
    RuntimeMultiplayerSnapshotChunkAck candidate;
    candidate.proposalId = descriptor_.identity.proposalId;
    candidate.sourceTimelineEpoch =
        descriptor_.identity.sourceTimelineEpoch;
    candidate.chunkIndex = lastAcceptedChunkIndex_;
    ack = candidate;
    return true;
}

bool RuntimeMultiplayerSnapshotChunkAssembler::takeCompletedBytes(
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!active_ || !complete_) {
        return fail(errorMessage, "snapshot assembler is not complete");
    }
    bytes = std::move(bytes_);
    descriptor_ = RuntimeMultiplayerSnapshotDescriptor{};
    nextChunkIndex_ = 0U;
    lastAcceptedChunkIndex_ = 0U;
    lastAcceptedOffset_ = 0U;
    lastAcceptedSize_ = 0U;
    active_ = false;
    complete_ = false;
    haveLastAcceptedChunk_ = false;
    return true;
}

void RuntimeMultiplayerSnapshotChunkAssembler::reset() noexcept {
    descriptor_ = RuntimeMultiplayerSnapshotDescriptor{};
    std::vector<std::uint8_t>().swap(bytes_);
    nextChunkIndex_ = 0U;
    lastAcceptedChunkIndex_ = 0U;
    lastAcceptedOffset_ = 0U;
    lastAcceptedSize_ = 0U;
    active_ = false;
    complete_ = false;
    haveLastAcceptedChunk_ = false;
}

bool RuntimeMultiplayerSnapshotChunkAssembler::active() const {
    return active_;
}

bool RuntimeMultiplayerSnapshotChunkAssembler::complete() const {
    return active_ && complete_;
}

std::size_t RuntimeMultiplayerSnapshotChunkAssembler::receivedBytes() const {
    return bytes_.size();
}

const RuntimeMultiplayerSnapshotDescriptor*
RuntimeMultiplayerSnapshotChunkAssembler::descriptor() const {
    return active_ ? &descriptor_ : nullptr;
}
