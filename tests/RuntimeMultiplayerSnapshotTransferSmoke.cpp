#include "RuntimeMultiplayerSnapshotTransfer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Runtime multiplayer snapshot transfer smoke failure: "
            << message << '\n';
    }
    return condition;
}

std::vector<std::uint8_t> deterministicBytes(std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    std::uint32_t state = 0x9e3779b9U;
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        bytes[index] = static_cast<std::uint8_t>(
            state ^ static_cast<std::uint32_t>(index)
        );
    }
    return bytes;
}

RuntimeMultiplayerSnapshotIdentity saveIdentity(
    std::uint64_t proposalId = 17U,
    std::uint64_t sourceEpoch = 4U,
    int tick = 900
) {
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = proposalId;
    identity.kind = RuntimeMultiplayerSnapshotKind::Save;
    identity.sourceTimelineEpoch = sourceEpoch;
    identity.barrierTick = tick;
    identity.snapshotTick = tick;
    identity.snapshotToken = "save-2026_08.22-A";
    identity.authoritativeStateHash = 0x123456789abcdef0ULL;
    identity.gameplaySetupFingerprint = 0x0fedcba987654321ULL;
    return identity;
}

bool makeDescriptor(
    const RuntimeMultiplayerSnapshotIdentity& identity,
    const std::vector<std::uint8_t>& bytes,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    const std::string& label,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
) {
    std::string error;
    return expect(
        makeRuntimeMultiplayerSnapshotDescriptor(
            identity,
            bytes,
            descriptor,
            error,
            maximumBytes
        ),
        label + " descriptor was not created: " + error
    );
}

std::size_t expectedChunkSize(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::uint32_t chunkIndex
) {
    const std::uint64_t offset =
        static_cast<std::uint64_t>(chunkIndex)
        * static_cast<std::uint64_t>(descriptor.chunkBytes);
    if (offset >= descriptor.byteCount) return 0U;
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        descriptor.chunkBytes,
        descriptor.byteCount - offset
    ));
}

RuntimeMultiplayerSnapshotChunk sourceChunk(
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t index
) {
    RuntimeMultiplayerSnapshotChunk chunk;
    chunk.proposalId = descriptor.identity.proposalId;
    chunk.sourceTimelineEpoch =
        descriptor.identity.sourceTimelineEpoch;
    chunk.chunkIndex = index;
    const std::size_t offset = static_cast<std::size_t>(
        static_cast<std::uint64_t>(index) * descriptor.chunkBytes
    );
    const std::size_t count = expectedChunkSize(descriptor, index);
    chunk.bytes.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count)
    );
    return chunk;
}

bool descriptorDecodeRejectedAtomically(
    const std::vector<std::uint8_t>& payload,
    const RuntimeMultiplayerSnapshotDescriptor& seed,
    const std::string& label,
    std::size_t maximumBytes =
        runtimeMultiplayerSnapshotDefaultMaximumBytes
) {
    RuntimeMultiplayerSnapshotDescriptor output = seed;
    output.identity.proposalId += 100U;
    const RuntimeMultiplayerSnapshotDescriptor before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerSnapshotDescriptor(
            payload,
            output,
            error,
            maximumBytes
        )
            && output == before && !error.empty(),
        label + " descriptor was not rejected transactionally"
    );
}

bool chunkDecodeRejectedAtomically(
    const std::vector<std::uint8_t>& payload,
    const RuntimeMultiplayerSnapshotChunk& seed,
    const std::string& label
) {
    RuntimeMultiplayerSnapshotChunk output = seed;
    output.chunkIndex += 7U;
    const RuntimeMultiplayerSnapshotChunk before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerSnapshotChunk(
            payload,
            output,
            error
        )
            && output == before && !error.empty(),
        label + " chunk was not rejected transactionally"
    );
}

bool ackDecodeRejectedAtomically(
    const std::vector<std::uint8_t>& payload,
    const RuntimeMultiplayerSnapshotChunkAck& seed,
    const std::string& label
) {
    RuntimeMultiplayerSnapshotChunkAck output = seed;
    output.chunkIndex += 7U;
    const RuntimeMultiplayerSnapshotChunkAck before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerSnapshotChunkAck(
            payload,
            output,
            error
        )
            && output == before && !error.empty(),
        label + " ack was not rejected transactionally"
    );
}

bool testKnownDigestsAndTokenGrammar() {
    bool success = true;
    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    const RuntimeMultiplayerSnapshotSha256 expectedSha{{
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
        0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
        0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU
    }};
    success &= expect(
        runtimeMultiplayerSnapshotSha256(abc) == expectedSha,
        "SHA-256 did not match the standard abc vector"
    );
    success &= expect(
        runtimeMultiplayerSnapshotFingerprint(abc) != 0U
            && runtimeMultiplayerSnapshotFingerprint(abc)
                == runtimeMultiplayerSnapshotFingerprint(abc)
            && runtimeMultiplayerSnapshotFingerprint(abc)
                != runtimeMultiplayerSnapshotFingerprint(
                    std::span<const std::uint8_t>()
                ),
        "64-bit snapshot fingerprint was not stable/domain-sensitive"
    );

    success &= expect(
        runtimeMultiplayerSnapshotTokenIsSafe("save-2026_08.22-A")
            && runtimeMultiplayerSnapshotTokenIsSafe("A1")
            && runtimeMultiplayerSnapshotTokenIsSafe("snapshot_7"),
        "safe opaque snapshot tokens were rejected"
    );
    const std::array<std::string, 13U> unsafeTokens{{
        "", ".", "..", "../save", "save/slot", "save\\slot",
        "C:save", "save token", ".hidden", "trailing.", "a..b",
        "-leading", "trailing-"
    }};
    for (const std::string& token : unsafeTokens) {
        success &= expect(
            !runtimeMultiplayerSnapshotTokenIsSafe(token),
            "unsafe/path-like token was accepted: " + token
        );
    }
    success &= expect(
        !runtimeMultiplayerSnapshotTokenIsSafe(
            std::string(
                runtimeMultiplayerSnapshotMaximumTokenBytes + 1U,
                'a'
            )
        ),
        "oversized snapshot token was accepted"
    );
    return success;
}

bool testDescriptorContractAndCodec() {
    bool success = true;
    const std::vector<std::uint8_t> bytes = deterministicBytes(
        runtimeMultiplayerSnapshotChunkBytes * 2U + 17U
    );
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    success &= makeDescriptor(
        saveIdentity(),
        bytes,
        descriptor,
        "canonical"
    );
    success &= expect(
        descriptor.byteCount == bytes.size()
            && descriptor.chunkBytes
                == runtimeMultiplayerSnapshotChunkBytes
            && descriptor.chunkCount == 3U
            && expectedChunkSize(descriptor, 0U)
                == runtimeMultiplayerSnapshotChunkBytes
            && expectedChunkSize(descriptor, 1U)
                == runtimeMultiplayerSnapshotChunkBytes
            && expectedChunkSize(descriptor, 2U) == 17U,
        "ceil chunk count or exact last-chunk contract is wrong"
    );

    std::vector<std::uint8_t> encoded;
    std::string error;
    success &= expect(
        encodeRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            encoded,
            error
        ),
        "canonical descriptor did not encode: " + error
    );
    RuntimeMultiplayerSnapshotDescriptor decoded;
    success &= expect(
        decodeRuntimeMultiplayerSnapshotDescriptor(
            encoded,
            decoded,
            error
        )
            && decoded == descriptor,
        "canonical descriptor did not round-trip: " + error
    );
    std::vector<std::uint8_t> replay;
    success &= expect(
        encodeRuntimeMultiplayerSnapshotDescriptor(
            decoded,
            replay,
            error
        )
            && replay == encoded,
        "descriptor canonical wire image was not stable: " + error
    );

    for (std::size_t size = 0U; size < encoded.size(); ++size) {
        success &= descriptorDecodeRejectedAtomically(
            std::vector<std::uint8_t>(
                encoded.begin(),
                encoded.begin() + static_cast<std::ptrdiff_t>(size)
            ),
            descriptor,
            "truncated prefix " + std::to_string(size)
        );
    }
    std::vector<std::uint8_t> malformed = encoded;
    malformed.push_back(0xffU);
    success &= descriptorDecodeRejectedAtomically(
        malformed,
        descriptor,
        "trailing byte"
    );
    malformed = encoded;
    malformed[0U] ^= 0xffU;
    success &= descriptorDecodeRejectedAtomically(
        malformed,
        descriptor,
        "bad magic"
    );
    malformed = encoded;
    malformed[5U] = 2U;
    success &= descriptorDecodeRejectedAtomically(
        malformed,
        descriptor,
        "unsupported version"
    );
    malformed = encoded;
    malformed[7U] = 1U;
    success &= descriptorDecodeRejectedAtomically(
        malformed,
        descriptor,
        "compression flag"
    );

    RuntimeMultiplayerSnapshotDescriptor invalid = descriptor;
    invalid.chunkCount = 2U;
    std::vector<std::uint8_t> unchanged{0xdeU, 0xadU};
    const std::vector<std::uint8_t> before = unchanged;
    error.clear();
    success &= expect(
        !encodeRuntimeMultiplayerSnapshotDescriptor(
            invalid,
            unchanged,
            error
        )
            && unchanged == before && !error.empty(),
        "invalid chunk count changed descriptor encode output"
    );

    RuntimeMultiplayerSnapshotDescriptor output = descriptor;
    const RuntimeMultiplayerSnapshotDescriptor outputBefore = output;
    const std::vector<std::uint8_t> empty;
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerSnapshotDescriptor(
            saveIdentity(),
            empty,
            output,
            error
        )
            && output == outputBefore && !error.empty(),
        "zero-byte snapshot contract was not rejected transactionally"
    );

    error.clear();
    success &= expect(
        !validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            error,
            0U
        )
            && !error.empty(),
        "zero configured maximum was accepted"
    );
    error.clear();
    success &= expect(
        !validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            error,
            runtimeMultiplayerSnapshotHardMaximumBytes + 1U
        )
            && !error.empty(),
        "configured maximum above hard cap was accepted"
    );
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerSnapshotDescriptor(
            saveIdentity(),
            bytes,
            output,
            error,
            0U
        )
            && output == outputBefore && !error.empty(),
        "descriptor maker accepted zero configured maximum"
    );
    success &= descriptorDecodeRejectedAtomically(
        encoded,
        descriptor,
        "zero configured maximum",
        0U
    );

    invalid = descriptor;
    invalid.byteCount =
        runtimeMultiplayerSnapshotDefaultMaximumBytes + 1U;
    invalid.chunkCount = static_cast<std::uint32_t>(
        (invalid.byteCount + invalid.chunkBytes - 1U)
        / invalid.chunkBytes
    );
    error.clear();
    success &= expect(
        !validateRuntimeMultiplayerSnapshotDescriptor(invalid, error)
            && validateRuntimeMultiplayerSnapshotDescriptor(
                invalid,
                error,
                runtimeMultiplayerSnapshotHardMaximumBytes
            ),
        "default 64 MiB boundary was not enforced independently"
    );
    invalid.byteCount = runtimeMultiplayerSnapshotHardMaximumBytes + 1U;
    invalid.chunkCount = static_cast<std::uint32_t>(
        (invalid.byteCount + invalid.chunkBytes - 1U)
        / invalid.chunkBytes
    );
    error.clear();
    success &= expect(
        !validateRuntimeMultiplayerSnapshotDescriptor(
            invalid,
            error,
            runtimeMultiplayerSnapshotHardMaximumBytes
        )
            && !error.empty(),
        "hard 128 MiB boundary was not enforced"
    );

    RuntimeMultiplayerSnapshotIdentity badIdentity = saveIdentity();
    badIdentity.snapshotToken = "../machine/path.sav";
    output = descriptor;
    const RuntimeMultiplayerSnapshotDescriptor maliciousBefore = output;
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerSnapshotDescriptor(
            badIdentity,
            bytes,
            output,
            error
        )
            && output == maliciousBefore && !error.empty(),
        "path-like snapshot token was not rejected transactionally"
    );

    badIdentity = saveIdentity();
    badIdentity.snapshotTick -= 1;
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerSnapshotDescriptor(
            badIdentity,
            bytes,
            output,
            error
        ),
        "save descriptor accepted a snapshot tick before its barrier"
    );
    RuntimeMultiplayerSnapshotIdentity loadIdentity = saveIdentity();
    loadIdentity.kind = RuntimeMultiplayerSnapshotKind::Load;
    loadIdentity.snapshotTick = loadIdentity.barrierTick - 10;
    success &= makeDescriptor(
        loadIdentity,
        bytes,
        output,
        "load at a prior snapshot tick"
    );
    loadIdentity.snapshotTick = loadIdentity.barrierTick + 1;
    error.clear();
    success &= expect(
        !makeRuntimeMultiplayerSnapshotDescriptor(
            loadIdentity,
            bytes,
            output,
            error
        ),
        "load descriptor accepted a future snapshot tick"
    );
    return success;
}

bool testChunkAndAckCodecs() {
    bool success = true;
    const std::vector<std::uint8_t> bytes = deterministicBytes(37U);
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    success &= makeDescriptor(
        saveIdentity(),
        bytes,
        descriptor,
        "chunk codec"
    );
    const RuntimeMultiplayerSnapshotChunk chunk = sourceChunk(
        descriptor,
        bytes,
        0U
    );
    std::vector<std::uint8_t> encoded;
    std::string error;
    success &= expect(
        encodeRuntimeMultiplayerSnapshotChunk(chunk, encoded, error),
        "snapshot chunk did not encode: " + error
    );
    RuntimeMultiplayerSnapshotChunk decoded;
    success &= expect(
        decodeRuntimeMultiplayerSnapshotChunk(encoded, decoded, error)
            && decoded == chunk,
        "snapshot chunk did not round-trip: " + error
    );
    std::vector<std::uint8_t> malformed = encoded;
    malformed.push_back(0U);
    success &= chunkDecodeRejectedAtomically(
        malformed,
        chunk,
        "trailing byte"
    );
    malformed = encoded;
    malformed[0U] ^= 0xffU;
    success &= chunkDecodeRejectedAtomically(
        malformed,
        chunk,
        "bad magic"
    );
    malformed = encoded;
    malformed[5U] = 2U;
    success &= chunkDecodeRejectedAtomically(
        malformed,
        chunk,
        "unsupported version"
    );
    malformed = encoded;
    malformed.pop_back();
    success &= chunkDecodeRejectedAtomically(
        malformed,
        chunk,
        "truncated payload"
    );

    RuntimeMultiplayerSnapshotChunk invalid = chunk;
    invalid.bytes.clear();
    std::vector<std::uint8_t> unchanged{1U, 2U, 3U};
    const std::vector<std::uint8_t> unchangedBefore = unchanged;
    error.clear();
    success &= expect(
        !encodeRuntimeMultiplayerSnapshotChunk(
            invalid,
            unchanged,
            error
        )
            && unchanged == unchangedBefore && !error.empty(),
        "empty chunk changed encoder output"
    );
    invalid = chunk;
    invalid.bytes.assign(
        runtimeMultiplayerSnapshotChunkBytes + 1U,
        0U
    );
    error.clear();
    success &= expect(
        !encodeRuntimeMultiplayerSnapshotChunk(
            invalid,
            unchanged,
            error
        )
            && unchanged == unchangedBefore && !error.empty(),
        "oversized chunk changed encoder output"
    );

    RuntimeMultiplayerSnapshotChunkAck ack;
    ack.proposalId = descriptor.identity.proposalId;
    ack.sourceTimelineEpoch =
        descriptor.identity.sourceTimelineEpoch;
    ack.chunkIndex = 0U;
    encoded.clear();
    success &= expect(
        encodeRuntimeMultiplayerSnapshotChunkAck(ack, encoded, error),
        "snapshot ack did not encode: " + error
    );
    RuntimeMultiplayerSnapshotChunkAck decodedAck;
    success &= expect(
        decodeRuntimeMultiplayerSnapshotChunkAck(
            encoded,
            decodedAck,
            error
        )
            && decodedAck == ack,
        "snapshot ack did not round-trip: " + error
    );
    malformed = encoded;
    malformed.push_back(0U);
    success &= ackDecodeRejectedAtomically(
        malformed,
        ack,
        "trailing byte"
    );
    malformed = encoded;
    malformed[0U] ^= 0xffU;
    success &= ackDecodeRejectedAtomically(malformed, ack, "bad magic");
    malformed = encoded;
    malformed[5U] = 2U;
    success &= ackDecodeRejectedAtomically(
        malformed,
        ack,
        "unsupported version"
    );
    malformed = encoded;
    malformed.pop_back();
    success &= ackDecodeRejectedAtomically(
        malformed,
        ack,
        "truncated payload"
    );
    return success;
}

bool testLargeStopAndWaitTransfer() {
    bool success = true;
    const std::vector<std::uint8_t> bytes = deterministicBytes(
        5U * 1024U * 1024U + 123U
    );
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    success &= makeDescriptor(
        saveIdentity(),
        bytes,
        descriptor,
        "large transfer"
    );
    success &= expect(
        descriptor.chunkCount == 21U
            && expectedChunkSize(descriptor, 20U) == 123U,
        ">4 MiB transfer did not use exact 256 KiB chunks"
    );

    RuntimeMultiplayerSnapshotChunkProducer producer;
    RuntimeMultiplayerSnapshotChunkAssembler assembler;
    RuntimeMultiplayerSnapshotTransferResult result = producer.begin(
        descriptor,
        bytes
    );
    success &= expect(
        result.accepted && result.changed && !result.complete
            && producer.active() && !producer.complete()
            && producer.descriptor() != nullptr,
        "producer did not begin the large transfer: " + result.reason
    );
    result = assembler.begin(descriptor);
    success &= expect(
        result.accepted && result.changed && !result.complete
            && assembler.active() && !assembler.complete()
            && assembler.receivedBytes() == 0U
            && assembler.descriptor() != nullptr,
        "assembler did not begin the large transfer: " + result.reason
    );

    RuntimeMultiplayerSnapshotDescriptor secondDescriptor = descriptor;
    ++secondDescriptor.identity.proposalId;
    result = producer.begin(secondDescriptor, bytes);
    success &= expect(
        !result.accepted
            && producer.descriptor() != nullptr
            && *producer.descriptor() == descriptor,
        "producer accepted a second simultaneous transfer"
    );
    result = assembler.begin(secondDescriptor);
    success &= expect(
        !result.accepted && assembler.receivedBytes() == 0U
            && assembler.descriptor() != nullptr
            && *assembler.descriptor() == descriptor,
        "assembler accepted a second simultaneous transfer"
    );

    std::vector<std::uint8_t> unavailable{0xaaU};
    const std::vector<std::uint8_t> unavailableBefore = unavailable;
    std::string error;
    success &= expect(
        !assembler.takeCompletedBytes(unavailable, error)
            && unavailable == unavailableBefore && !error.empty(),
        "assembler exposed bytes before completion"
    );

    RuntimeMultiplayerSnapshotChunk current;
    success &= expect(
        producer.currentChunk(current, error),
        "producer did not expose its first chunk: " + error
    );
    RuntimeMultiplayerSnapshotChunk future = current;
    future.chunkIndex = 1U;
    result = assembler.acceptChunk(future);
    success &= expect(
        !result.accepted && assembler.receivedBytes() == 0U,
        "out-of-order first chunk changed assembler state"
    );
    RuntimeMultiplayerSnapshotChunk foreign = current;
    ++foreign.proposalId;
    result = assembler.acceptChunk(foreign);
    success &= expect(
        !result.accepted && assembler.receivedBytes() == 0U,
        "cross-proposal chunk changed assembler state"
    );
    foreign = current;
    ++foreign.sourceTimelineEpoch;
    result = assembler.acceptChunk(foreign);
    success &= expect(
        !result.accepted && assembler.receivedBytes() == 0U,
        "cross-epoch chunk changed assembler state"
    );

    RuntimeMultiplayerSnapshotChunkAck wrongAck;
    wrongAck.proposalId = descriptor.identity.proposalId + 1U;
    wrongAck.sourceTimelineEpoch =
        descriptor.identity.sourceTimelineEpoch;
    wrongAck.chunkIndex = 0U;
    result = producer.acknowledge(wrongAck);
    RuntimeMultiplayerSnapshotChunk stillCurrent;
    success &= expect(
        !result.accepted
            && producer.currentChunk(stillCurrent, error)
            && stillCurrent == current,
        "cross-proposal ack advanced producer state"
    );
    wrongAck.proposalId = descriptor.identity.proposalId;
    ++wrongAck.sourceTimelineEpoch;
    result = producer.acknowledge(wrongAck);
    success &= expect(
        !result.accepted
            && producer.currentChunk(stillCurrent, error)
            && stillCurrent == current,
        "cross-epoch ack advanced producer state"
    );
    wrongAck.sourceTimelineEpoch =
        descriptor.identity.sourceTimelineEpoch;
    wrongAck.chunkIndex = 1U;
    result = producer.acknowledge(wrongAck);
    success &= expect(
        !result.accepted
            && producer.currentChunk(stillCurrent, error)
            && stillCurrent == current,
        "out-of-order ack advanced producer state"
    );

    for (std::uint32_t index = 0U;
        index < descriptor.chunkCount;
        ++index) {
        current = RuntimeMultiplayerSnapshotChunk{};
        error.clear();
        success &= expect(
            producer.currentChunk(current, error)
                && current.chunkIndex == index
                && current.bytes.size()
                    == expectedChunkSize(descriptor, index),
            "producer emitted wrong chunk " + std::to_string(index)
                + ": " + error
        );
        const std::size_t receivedBefore = assembler.receivedBytes();
        result = assembler.acceptChunk(current);
        success &= expect(
            result.accepted && result.changed && !result.duplicate
                && result.complete
                    == (index + 1U == descriptor.chunkCount)
                && assembler.receivedBytes()
                    == receivedBefore + current.bytes.size(),
            "assembler rejected canonical chunk "
                + std::to_string(index) + ": " + result.reason
        );
        result = assembler.acceptChunk(current);
        success &= expect(
            result.accepted && !result.changed && result.duplicate
                && assembler.receivedBytes()
                    == receivedBefore + current.bytes.size(),
            "identical duplicate chunk was not idempotent"
        );
        RuntimeMultiplayerSnapshotChunk conflicting = current;
        conflicting.bytes[0U] ^= 1U;
        result = assembler.acceptChunk(conflicting);
        success &= expect(
            !result.accepted
                && assembler.receivedBytes()
                    == receivedBefore + current.bytes.size(),
            "conflicting duplicate chunk changed assembler state"
        );

        RuntimeMultiplayerSnapshotChunkAck ack;
        error.clear();
        success &= expect(
            assembler.latestAcknowledgement(ack, error)
                && ack.proposalId == descriptor.identity.proposalId
                && ack.sourceTimelineEpoch
                    == descriptor.identity.sourceTimelineEpoch
                && ack.chunkIndex == index,
            "assembler emitted wrong ack: " + error
        );
        std::vector<std::uint8_t> ackWire;
        RuntimeMultiplayerSnapshotChunkAck ackRoundTrip;
        success &= expect(
            encodeRuntimeMultiplayerSnapshotChunkAck(
                ack,
                ackWire,
                error
            )
                && decodeRuntimeMultiplayerSnapshotChunkAck(
                    ackWire,
                    ackRoundTrip,
                    error
                )
                && ackRoundTrip == ack,
            "stop-and-wait ack codec failed: " + error
        );
        result = producer.acknowledge(ackRoundTrip);
        success &= expect(
            result.accepted && result.changed && !result.duplicate
                && result.complete
                    == (index + 1U == descriptor.chunkCount),
            "producer rejected canonical ack " + std::to_string(index)
                + ": " + result.reason
        );
        result = producer.acknowledge(ackRoundTrip);
        success &= expect(
            result.accepted && !result.changed && result.duplicate,
            "identical duplicate ack was not idempotent"
        );
    }

    success &= expect(
        producer.complete() && assembler.complete(),
        "large transfer did not reach completion"
    );
    RuntimeMultiplayerSnapshotChunk noChunk = current;
    const RuntimeMultiplayerSnapshotChunk noChunkBefore = noChunk;
    error.clear();
    success &= expect(
        !producer.currentChunk(noChunk, error)
            && noChunk == noChunkBefore && !error.empty(),
        "complete producer exposed or changed another chunk"
    );
    std::vector<std::uint8_t> completed;
    success &= expect(
        assembler.takeCompletedBytes(completed, error)
            && completed == bytes
            && !assembler.active() && !assembler.complete()
            && assembler.receivedBytes() == 0U
            && assembler.descriptor() == nullptr,
        "assembler did not atomically hand out verified bytes: " + error
    );
    result = producer.begin(secondDescriptor, bytes);
    success &= expect(
        !result.accepted,
        "complete producer accepted another transfer before reset"
    );
    producer.reset();
    success &= expect(
        !producer.active() && !producer.complete()
            && producer.descriptor() == nullptr,
        "producer reset did not release its transfer"
    );
    producer.reset();
    assembler.reset();
    success &= expect(
        !assembler.active() && !assembler.complete()
            && assembler.receivedBytes() == 0U
            && assembler.descriptor() == nullptr,
        "assembler reset was not idempotent"
    );
    return success;
}

bool testLastChunkAndDigestFailures() {
    bool success = true;
    const std::vector<std::uint8_t> bytes = deterministicBytes(
        runtimeMultiplayerSnapshotChunkBytes + 17U
    );
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    success &= makeDescriptor(
        saveIdentity(51U, 9U, 1200),
        bytes,
        descriptor,
        "last chunk"
    );

    RuntimeMultiplayerSnapshotChunkAssembler assembler;
    RuntimeMultiplayerSnapshotTransferResult result = assembler.begin(
        descriptor
    );
    success &= expect(result.accepted, "last-chunk assembler did not begin");
    const RuntimeMultiplayerSnapshotChunk first = sourceChunk(
        descriptor,
        bytes,
        0U
    );
    result = assembler.acceptChunk(first);
    success &= expect(result.accepted, "first exact chunk was rejected");
    RuntimeMultiplayerSnapshotChunk last = sourceChunk(
        descriptor,
        bytes,
        1U
    );
    last.bytes.pop_back();
    const std::size_t receivedBefore = assembler.receivedBytes();
    result = assembler.acceptChunk(last);
    success &= expect(
        !result.accepted
            && assembler.active() && !assembler.complete()
            && assembler.receivedBytes() == receivedBefore,
        "short last chunk changed assembler state"
    );
    last = sourceChunk(descriptor, bytes, 1U);
    last.bytes.push_back(0U);
    result = assembler.acceptChunk(last);
    success &= expect(
        !result.accepted && assembler.receivedBytes() == receivedBefore,
        "long last chunk changed assembler state"
    );
    last = sourceChunk(descriptor, bytes, 1U);
    result = assembler.acceptChunk(last);
    success &= expect(
        result.accepted && result.complete && assembler.complete(),
        "exact last chunk was rejected: " + result.reason
    );
    assembler.reset();

    RuntimeMultiplayerSnapshotDescriptor badDigest = descriptor;
    badDigest.sha256[0U] ^= 1U;
    result = assembler.begin(badDigest);
    success &= expect(
        result.accepted,
        "syntactically valid damaged digest descriptor was rejected early"
    );
    result = assembler.acceptChunk(first);
    success &= expect(result.accepted, "bad-digest first chunk was rejected");
    last = sourceChunk(descriptor, bytes, 1U);
    result = assembler.acceptChunk(last);
    success &= expect(
        !result.accepted
            && !assembler.active() && !assembler.complete()
            && assembler.receivedBytes() == 0U
            && assembler.descriptor() == nullptr,
        "damaged digest did not atomically abort and release transfer"
    );
    std::vector<std::uint8_t> unavailable{0x55U};
    const std::vector<std::uint8_t> unavailableBefore = unavailable;
    std::string error;
    success &= expect(
        !assembler.takeCompletedBytes(unavailable, error)
            && unavailable == unavailableBefore,
        "digest failure exposed partial snapshot bytes"
    );

    RuntimeMultiplayerSnapshotChunkProducer producer;
    result = producer.begin(badDigest, bytes);
    success &= expect(
        !result.accepted && !producer.active()
            && producer.descriptor() == nullptr,
        "producer accepted bytes that disagreed with damaged digest"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testKnownDigestsAndTokenGrammar();
    success &= testDescriptorContractAndCodec();
    success &= testChunkAndAckCodecs();
    success &= testLargeStopAndWaitTransfer();
    success &= testLastChunkAndDigestFailures();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer snapshot transfer smoke: version=1"
        << " chunkKiB=256 defaultMiB=64 hardMiB=128"
        << " sha256=yes stopAndWait=yes atomic=yes\n";
    return 0;
}
