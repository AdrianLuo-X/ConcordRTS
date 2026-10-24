#include "network/lockstep/RuntimeMultiplayerLockstep.h"

#include <algorithm>
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
        std::cerr << "runtime multiplayer lockstep smoke failure: "
                  << message << '\n';
    }
    return condition;
}

template <typename Body>
RuntimeMultiplayerGameplayCommand command(Body body) {
    RuntimeMultiplayerGameplayCommand result;
    result.body = std::move(body);
    return result;
}

RuntimeMultiplayerGameplayCommand sell(std::int32_t objectId) {
    return command(RuntimeMultiplayerGameplaySellCommand{objectId});
}

RuntimeMultiplayerGameplayCommand repair(std::int32_t objectId) {
    return command(RuntimeMultiplayerGameplayRepairCommand{objectId});
}

RuntimeMultiplayerGameplayCommand stop(std::vector<std::int32_t> objectIds) {
    return command(RuntimeMultiplayerGameplayStopCommand{
        std::move(objectIds)
    });
}

struct Fixture {
    RuntimeMultiplayerSessionState session;
    RuntimeMultiplayerLockstepDriver driver;
};

RuntimeMultiplayerMatchSetup matchSetupFor(
    const RuntimeMultiplayerSessionState& session
) {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = session.config().hostPeerId;
    setup.map.canonicalId = "maps/official/lockstep-smoke.map";
    setup.map.contentFingerprint =
        session.config().compatibility.contentFingerprint;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x0123456789abcdefULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    std::uint8_t slotIndex = 0;
    for (const RuntimeMultiplayerRoomMember& roomMember : session.members()) {
        RuntimeMultiplayerMatchMember member;
        member.peerId = roomMember.peerId;
        member.displayName = roomMember.displayName;
        member.identity = roomMember.participation
                == RuntimeMultiplayerParticipationRole::Combatant
            ? RuntimeMultiplayerMatchMemberIdentity::Combatant
            : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        setup.members.push_back(std::move(member));

        if (roomMember.participation
            == RuntimeMultiplayerParticipationRole::Spectator) {
            continue;
        }
        RuntimeMultiplayerMatchSlot slot;
        slot.slotIndex = slotIndex;
        slot.controller = RuntimeMultiplayerMatchSlotController::Human;
        slot.peerId = roomMember.peerId;
        slot.countryId = "random";
        slot.colorId = "color" + std::to_string(slotIndex);
        slot.teamId = "-";
        slot.difficultyId = "medium";
        slot.startPosition = static_cast<std::uint8_t>(slotIndex + 1U);
        setup.slots.push_back(std::move(slot));
        ++slotIndex;
    }
    return setup;
}

bool startSession(RuntimeMultiplayerSessionState& session) {
    RuntimeMultiplayerSessionConfig room;
    room.roomId = "lockstep-room";
    room.hostPeerId = "zulu";
    room.hostDisplayName = "Zulu";
    room.hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    room.compatibility.protocolFingerprint = 0x1111222233334444ULL;
    room.compatibility.contentFingerprint = 0xaaaabbbbccccddddULL;

    const RuntimeMultiplayerOperationResult began = session.beginHost(room);
    if (!began.accepted) return false;

    RuntimeMultiplayerJoinRequest alpha;
    alpha.peerId = "alpha";
    alpha.displayName = "Alpha";
    alpha.participation = RuntimeMultiplayerParticipationRole::Combatant;
    alpha.compatibility = room.compatibility;
    const RuntimeMultiplayerOperationResult joinedAlpha =
        session.joinMember(alpha);
    if (!joinedAlpha.accepted) return false;

    RuntimeMultiplayerJoinRequest spectator;
    spectator.peerId = "mid";
    spectator.displayName = "Middle Spectator";
    spectator.participation = RuntimeMultiplayerParticipationRole::Spectator;
    spectator.compatibility = room.compatibility;
    const RuntimeMultiplayerOperationResult joinedSpectator =
        session.joinMember(spectator);
    if (!joinedSpectator.accepted) return false;

    if (!session.setMatchSetup("zulu", matchSetupFor(session)).accepted) {
        return false;
    }

    if (!session.setMemberReady("zulu", true).accepted
        || !session.setMemberReady("alpha", true).accepted
        || !session.setMemberReady("mid", true).accepted) {
        return false;
    }
    return session.start("zulu", 10).accepted;
}

bool prepareFixture(
    Fixture& fixture,
    RuntimeMultiplayerLockstepConfig config = {}
) {
    return startSession(fixture.session)
        && fixture.driver.bind(fixture.session, config).accepted;
}

RuntimeMultiplayerSaveLoadProposal snapshotProposal(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    RuntimeMultiplayerSaveLoadKind kind,
    int snapshotTick,
    std::string token
) {
    RuntimeMultiplayerSaveLoadProposal result;
    result.timelineEpoch = session.timelineEpoch();
    result.proposalId = proposalId;
    result.kind = kind;
    result.proposerPeerId = session.config().hostPeerId;
    RuntimeMultiplayerSnapshotIdentity& identity =
        result.descriptor.identity;
    identity.proposalId = proposalId;
    identity.kind = kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = snapshotTick;
    identity.snapshotToken = std::move(token);
    identity.authoritativeStateHash = 0x13579bdf2468ace0ULL ^ proposalId;
    std::string error;
    if (session.matchSetup().has_value()) {
        runtimeMultiplayerGameplaySetupFingerprint(
            *session.matchSetup(),
            identity.gameplaySetupFingerprint,
            error
        );
    }
    result.descriptor.byteCount = 1;
    result.descriptor.chunkCount = 1;
    result.descriptor.snapshotFingerprint = 0xabcdef0000000000ULL
        ^ proposalId;
    result.descriptor.sha256[0] = 1;
    result.tick = snapshotTick;
    result.snapshotId = identity.snapshotToken;
    result.snapshotFingerprint = result.descriptor.snapshotFingerprint;
    return result;
}

bool completeSnapshotHandshake(
    RuntimeMultiplayerSessionState& session,
    const RuntimeMultiplayerSaveLoadProposal& proposal
) {
    if (!session.proposeSaveLoad(proposal).accepted) return false;
    for (const std::string& peer : {"zulu", "alpha", "mid"}) {
        RuntimeMultiplayerSaveLoadResponse prepared;
        prepared.timelineEpoch = session.timelineEpoch();
        prepared.proposalId = proposal.proposalId;
        prepared.peerId = peer;
        prepared.prepared = true;
        prepared.observedSnapshotFingerprint =
            proposal.descriptor.snapshotFingerprint;
        prepared.observedAuthoritativeStateHash =
            proposal.descriptor.identity.authoritativeStateHash;
        if (!session.prepareSaveLoad(prepared).accepted) return false;
    }
    const std::uint64_t sourceEpoch = proposal.timelineEpoch;
    if (!session.issueSaveLoadCommit(
            sourceEpoch,
            proposal.proposalId,
            "zulu"
        ).accepted) {
        return false;
    }
    for (const std::string& peer : {"zulu", "alpha", "mid"}) {
        RuntimeMultiplayerSaveLoadApplied applied;
        applied.timelineEpoch = session.timelineEpoch();
        applied.proposalId = proposal.proposalId;
        applied.peerId = peer;
        applied.observedAuthoritativeStateHash =
            proposal.descriptor.identity.authoritativeStateHash;
        if (!session.acknowledgeSaveLoadApplied(applied).accepted) {
            return false;
        }
    }
    return true;
}

bool encodeFrame(
    const RuntimeMultiplayerGameplayFrame& frame,
    std::vector<std::uint8_t>& payload,
    std::size_t maximumCommands =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame,
    std::size_t maximumBytes =
        runtimeMultiplayerLockstepMaximumFrameBytes
) {
    std::string error;
    return encodeRuntimeMultiplayerGameplayFrame(
        frame,
        payload,
        error,
        maximumCommands,
        maximumBytes
    );
}

RuntimeMultiplayerCommandPacket packet(
    int tick,
    std::string peerId,
    std::uint64_t sequence,
    const RuntimeMultiplayerGameplayFrame& frame
) {
    RuntimeMultiplayerCommandPacket result;
    result.tick = tick;
    result.peerId = std::move(peerId);
    result.sequence = sequence;
    std::string error;
    if (!encodeRuntimeMultiplayerGameplayFrame(
            frame,
            result.payload,
            error
        )) {
        result.payload.clear();
    }
    return result;
}

void writeU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void writeU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

bool decodeRejectedAtomically(
    const std::vector<std::uint8_t>& payload,
    const std::string& label,
    std::size_t maximumCommands =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame,
    std::size_t maximumBytes =
        runtimeMultiplayerLockstepMaximumFrameBytes
) {
    RuntimeMultiplayerGameplayFrame output{{repair(777)}};
    const RuntimeMultiplayerGameplayFrame before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerGameplayFrame(
            payload,
            output,
            error,
            maximumCommands,
            maximumBytes
        )
            && output == before && !error.empty(),
        label + " is rejected without changing frame output"
    );
}

bool testFrameCodecAndGoldenBytes() {
    bool success = true;

    const RuntimeMultiplayerGameplayFrame empty;
    std::vector<std::uint8_t> emptyBytes;
    success &= expect(
        encodeFrame(empty, emptyBytes)
            && emptyBytes == std::vector<std::uint8_t>({
                0x43U, 0x47U, 0x4cU, 0x46U,
                0x00U, 0x01U, 0x00U, 0x00U
            }),
        "empty frame has the canonical CGLF big-endian header"
    );
    RuntimeMultiplayerGameplayFrame decodedEmpty{{sell(99)}};
    std::string error;
    success &= expect(
        decodeRuntimeMultiplayerGameplayFrame(
            emptyBytes,
            decodedEmpty,
            error
        )
            && decodedEmpty == empty,
        "empty frame round-trips: " + error
    );

    const RuntimeMultiplayerGameplayFrame golden{{sell(42)}};
    std::vector<std::uint8_t> goldenBytes;
    success &= expect(
        encodeFrame(golden, goldenBytes),
        "golden sell frame encodes"
    );
    // The original version-3 bytes stay exact as a rejected legacy fixture.
    // Current version 20 is already fixed by GameplayCommandSmoke's independent
    // golden/static_assert; production encoding is unchanged by this repair.
    static_assert(runtimeMultiplayerGameplayCommandVersion == 20U);
    const std::vector<std::uint8_t> legacyVersion3Golden{
        0x43U, 0x47U, 0x4cU, 0x46U,
        0x00U, 0x01U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x0cU,
        0x43U, 0x47U, 0x43U, 0x4dU,
        0x00U, 0x03U, 0x00U, 0x07U,
        0x00U, 0x00U, 0x00U, 0x2aU
    };
    const std::vector<std::uint8_t> expectedVersion20Golden{
        0x43U, 0x47U, 0x4cU, 0x46U,
        0x00U, 0x01U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x0cU,
        0x43U, 0x47U, 0x43U, 0x4dU,
        0x00U, 0x14U, 0x00U, 0x07U,
        0x00U, 0x00U, 0x00U, 0x2aU
    };
    success &= expect(
        goldenBytes == expectedVersion20Golden,
        "frame and nested command lengths use the golden big-endian layout"
    );
    success &= decodeRejectedAtomically(
        legacyVersion3Golden,
        "legacy nested command version 3"
    );

    RuntimeMultiplayerGameplayFrame authored{{stop({9, 1, 9}), sell(5)}};
    const RuntimeMultiplayerGameplayFrame authoredBefore = authored;
    std::vector<std::uint8_t> authoredBytes;
    success &= expect(
        encodeFrame(authored, authoredBytes)
            && authored == authoredBefore,
        "frame encode canonicalizes a copy without changing command order"
    );
    RuntimeMultiplayerGameplayFrame canonical;
    success &= expect(
        decodeRuntimeMultiplayerGameplayFrame(
            authoredBytes,
            canonical,
            error
        )
            && canonical.commands.size() == 2U
            && std::get<RuntimeMultiplayerGameplayStopCommand>(
                canonical.commands[0].body
            ).objectIds == std::vector<std::int32_t>({1, 9})
            && std::holds_alternative<RuntimeMultiplayerGameplaySellCommand>(
                canonical.commands[1].body
            ),
        "nested commands canonicalize while frame order is preserved: "
            + error
    );

    RuntimeMultiplayerGameplayFrame explicitlyCanonical = authored;
    success &= expect(
        canonicalizeRuntimeMultiplayerGameplayFrame(
            explicitlyCanonical,
            error
        )
            && explicitlyCanonical == canonical,
        "public frame canonicalization is deterministic: " + error
    );
    return success;
}

bool testMalformedAndCodecBounds() {
    bool success = true;
    const RuntimeMultiplayerGameplayFrame source{{stop({1, 2})}};
    std::vector<std::uint8_t> bytes;
    success &= expect(encodeFrame(source, bytes), "malformed seed encodes");

    for (std::size_t size = 0; size < bytes.size(); ++size) {
        success &= decodeRejectedAtomically(
            std::vector<std::uint8_t>(
                bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(size)
            ),
            "truncated frame prefix " + std::to_string(size)
        );
    }

    std::vector<std::uint8_t> malformed = bytes;
    malformed.push_back(0xffU);
    success &= decodeRejectedAtomically(malformed, "trailing frame byte");

    malformed = bytes;
    malformed[0] ^= 0xffU;
    success &= decodeRejectedAtomically(malformed, "bad frame magic");

    malformed = bytes;
    writeU16(malformed, 4, 2);
    success &= decodeRejectedAtomically(malformed, "unsupported frame version");

    malformed = bytes;
    writeU16(
        malformed,
        6,
        static_cast<std::uint16_t>(
            runtimeMultiplayerLockstepMaximumCommandsPerFrame + 1U
        )
    );
    success &= decodeRejectedAtomically(malformed, "oversized frame count");

    malformed = bytes;
    writeU32(malformed, 8, 0);
    success &= decodeRejectedAtomically(malformed, "zero command length");

    malformed = bytes;
    writeU32(
        malformed,
        8,
        static_cast<std::uint32_t>(
            runtimeMultiplayerGameplayMaximumPayloadBytes + 1U
        )
    );
    success &= decodeRejectedAtomically(
        malformed,
        "oversized nested command length"
    );

    malformed = bytes;
    malformed[12] ^= 0xffU;
    success &= decodeRejectedAtomically(malformed, "invalid nested command");

    // Frame header (8), command length (4), command header (8), object count
    // (2), then two big-endian object ids at offsets 22 and 26.
    malformed = bytes;
    writeU32(malformed, 22, 2);
    writeU32(malformed, 26, 1);
    success &= decodeRejectedAtomically(
        malformed,
        "noncanonical nested object ids"
    );

    RuntimeMultiplayerGameplayFrame exactByteDecoded;
    std::string exactByteError;
    success &= expect(
        decodeRuntimeMultiplayerGameplayFrame(
            bytes,
            exactByteDecoded,
            exactByteError,
            runtimeMultiplayerLockstepMaximumCommandsPerFrame,
            bytes.size()
        )
            && exactByteDecoded == source,
        "exact frame byte limit is accepted: " + exactByteError
    );
    success &= decodeRejectedAtomically(
        bytes,
        "frame above configured byte limit",
        runtimeMultiplayerLockstepMaximumCommandsPerFrame,
        bytes.size() - 1U
    );

    RuntimeMultiplayerGameplayFrame exactCount;
    for (std::size_t index = 0;
            index < runtimeMultiplayerLockstepMaximumCommandsPerFrame;
            ++index) {
        exactCount.commands.push_back(sell(
            static_cast<std::int32_t>(index + 1U)
        ));
    }
    std::vector<std::uint8_t> exactCountBytes;
    success &= expect(
        encodeFrame(exactCount, exactCountBytes),
        "exact maximum command count is accepted"
    );
    RuntimeMultiplayerGameplayFrame exactCountDecoded;
    std::string error;
    success &= expect(
        decodeRuntimeMultiplayerGameplayFrame(
            exactCountBytes,
            exactCountDecoded,
            error
        )
            && exactCountDecoded == exactCount,
        "exact maximum command count round-trips: " + error
    );
    exactCount.commands.push_back(sell(1000));
    std::vector<std::uint8_t> unchanged{0xdeU, 0xadU};
    const std::vector<std::uint8_t> unchangedBefore = unchanged;
    success &= expect(
        !encodeRuntimeMultiplayerGameplayFrame(
            exactCount,
            unchanged,
            error
        )
            && unchanged == unchangedBefore && !error.empty(),
        "maximum command count plus one is rejected transactionally"
    );

    const RuntimeMultiplayerGameplayFrame twoCommands{{sell(1), repair(2)}};
    const RuntimeMultiplayerGameplayFrame threeCommands{
        {sell(1), repair(2), sell(3)}
    };
    std::vector<std::uint8_t> twoBytes;
    success &= expect(
        encodeFrame(
            twoCommands,
            twoBytes,
            2,
            runtimeMultiplayerLockstepMaximumFrameBytes
        ),
        "configured exact command limit is accepted"
    );
    unchanged = {0xcaU, 0xfeU};
    const std::vector<std::uint8_t> secondBefore = unchanged;
    success &= expect(
        !encodeRuntimeMultiplayerGameplayFrame(
            threeCommands,
            unchanged,
            error,
            2,
            runtimeMultiplayerLockstepMaximumFrameBytes
        )
            && unchanged == secondBefore,
        "configured command limit plus one is rejected"
    );
    std::vector<std::uint8_t> threeBytes;
    success &= expect(
        encodeFrame(threeCommands, threeBytes),
        "three-command frame encodes under the protocol maximum"
    );
    success &= decodeRejectedAtomically(
        threeBytes,
        "decoded frame above configured command limit",
        2,
        runtimeMultiplayerLockstepMaximumFrameBytes
    );
    return success;
}

struct OrderedPackets {
    RuntimeMultiplayerCommandPacket alpha12;
    RuntimeMultiplayerCommandPacket zulu12;
    RuntimeMultiplayerCommandPacket alpha13;
    RuntimeMultiplayerCommandPacket zulu13;
    RuntimeMultiplayerCommandPacket alpha14;
    RuntimeMultiplayerCommandPacket zulu14;
};

OrderedPackets makeOrderedPackets() {
    OrderedPackets packets;
    packets.alpha12 = packet(
        12,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(12), repair(13)}}
    );
    packets.zulu12 = packet(
        12,
        "zulu",
        1,
        RuntimeMultiplayerGameplayFrame{{stop({9, 3})}}
    );
    packets.alpha13 = packet(
        13,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    packets.zulu13 = packet(
        13,
        "zulu",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    packets.alpha14 = packet(
        14,
        "alpha",
        3,
        RuntimeMultiplayerGameplayFrame{}
    );
    packets.zulu14 = packet(
        14,
        "zulu",
        3,
        RuntimeMultiplayerGameplayFrame{}
    );
    return packets;
}

bool submitAccepted(
    RuntimeMultiplayerLockstepDriver& driver,
    const RuntimeMultiplayerCommandPacket& packetValue,
    const std::string& label
) {
    const RuntimeMultiplayerOperationResult result =
        driver.submitFrame(packetValue);
    return expect(result.accepted && result.changed, label + " is accepted");
}

bool testBarrierOrderingHashAndCallbacks() {
    bool success = true;
    Fixture left;
    Fixture right;
    success &= expect(
        prepareFixture(left) && prepareFixture(right),
        "two deterministic lockstep fixtures bind"
    );
    const OrderedPackets packets = makeOrderedPackets();

    success &= submitAccepted(left.driver, packets.zulu12, "left zulu tick 12");
    success &= submitAccepted(left.driver, packets.zulu13, "left zulu tick 13");
    success &= submitAccepted(left.driver, packets.alpha12, "left alpha tick 12");
    success &= submitAccepted(left.driver, packets.alpha13, "left alpha tick 13");
    success &= submitAccepted(left.driver, packets.alpha14, "left alpha tick 14");

    success &= submitAccepted(right.driver, packets.alpha12, "right alpha tick 12");
    success &= submitAccepted(right.driver, packets.alpha13, "right alpha tick 13");
    success &= submitAccepted(right.driver, packets.alpha14, "right alpha tick 14");
    success &= submitAccepted(right.driver, packets.zulu12, "right zulu tick 12");
    success &= submitAccepted(right.driver, packets.zulu13, "right zulu tick 13");

    success &= expect(
        left.driver.currentBarrierTick() == 10
            && right.driver.currentBarrierTick() == 10
            && left.driver.firstNetworkInputTick() == 12
            && right.driver.firstNetworkInputTick() == 12
            && left.session.authoritativeTick() == 10
            && right.session.authoritativeTick() == 10,
        "input delay starts at the session tick without skipping simulation"
    );
    success &= expect(
        left.driver.inputHorizonReady()
            && right.driver.inputHorizonReady(),
        "tick 10 may advance because every tick 12 horizon frame is buffered"
    );
    success &= expect(
        left.driver.stateHash() == right.driver.stateHash()
            && left.session.stateHash() == right.session.stateHash(),
        "different cross-peer arrival order has an identical buffered hash"
    );

    const RuntimeMultiplayerLockstepBatchCallback requireEmpty =
        [](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
           std::string&) {
            return batch.empty();
        };
    std::vector<RuntimeMultiplayerLockstepApplyCommand> warmupApply{{
        99,
        "sentinel",
        9,
        0,
        sell(99)
    }};
    std::string error;
    success &= expect(
        left.driver.frameBarrierReady(10)
            && left.driver.canonicalApplyList(10, warmupApply, error)
            && warmupApply.empty(),
        "first warm-up tick is an automatic empty-input barrier: " + error
    );
    success &= expect(
        !left.driver.advanceHost("zulu", 10).accepted
            && left.session.authoritativeTick() == 10,
        "even a warm-up barrier must be applied before host advance"
    );
    success &= expect(
        left.driver.applyReadyTick(
            10,
            requireEmpty,
            requireEmpty
        ).accepted
            && right.driver.applyReadyTick(
                10,
                requireEmpty,
                requireEmpty
            ).accepted,
        "both peers apply warm-up tick 10 as an empty deterministic batch"
    );
    const std::uint64_t beforePrematureAcknowledge =
        right.driver.stateHash();
    success &= expect(
        !right.driver.acknowledgeAuthoritativeAdvance(10).accepted
            && right.driver.stateHash() == beforePrematureAcknowledge,
        "client cannot acknowledge before the replicated Session advance"
    );
    success &= expect(
        !left.driver.advanceHost("alpha", 10).accepted
            && left.session.authoritativeTick() == 10,
        "non-host cannot advance an applied warm-up barrier"
    );
    success &= expect(
        left.driver.advanceHost("zulu", 10).accepted
            && left.driver.currentBarrierTick() == 11
            && left.session.authoritativeTick() == 11,
        "host path commits warm-up tick 10 and opens tick 11"
    );
    const RuntimeMultiplayerOperationResult replicated10 =
        right.session.advanceAuthoritativeTick("zulu", 11);
    const RuntimeMultiplayerOperationResult acknowledged10 =
        right.driver.acknowledgeAuthoritativeAdvance(10);
    const RuntimeMultiplayerOperationResult duplicateAcknowledgement10 =
        right.driver.acknowledgeAuthoritativeAdvance(10);
    success &= expect(
        replicated10.accepted && replicated10.changed
            && acknowledged10.accepted && acknowledged10.changed
            && duplicateAcknowledgement10.accepted
            && duplicateAcknowledgement10.duplicate
            && !duplicateAcknowledgement10.changed
            && right.driver.currentBarrierTick() == 11
            && right.session.authoritativeTick() == 11
            && left.driver.stateHash() == right.driver.stateHash()
            && left.session.stateHash() == right.session.stateHash(),
        "client acknowledgement shares host pruning and is idempotent"
    );

    warmupApply = {{99, "sentinel", 9, 0, sell(99)}};
    success &= expect(
        left.driver.frameBarrierReady(11)
            && right.driver.frameBarrierReady(11)
            && left.driver.canonicalApplyList(11, warmupApply, error)
            && warmupApply.empty()
            && left.driver.applyReadyTick(
                11,
                requireEmpty,
                requireEmpty
            ).accepted
            && right.driver.applyReadyTick(
                11,
                requireEmpty,
                requireEmpty
            ).accepted,
        "second warm-up tick is also applied, never skipped"
    );
    success &= expect(
        left.driver.advanceHost("zulu", 11).accepted
            && right.session.advanceAuthoritativeTick("zulu", 12).accepted
            && right.driver.acknowledgeAuthoritativeAdvance(11).accepted
            && left.driver.currentBarrierTick() == 12
            && right.driver.currentBarrierTick() == 12
            && left.session.authoritativeTick() == 12
            && right.session.authoritativeTick() == 12
            && left.driver.stateHash() == right.driver.stateHash(),
        "warm-up advances one simulation tick at a time to network input"
    );

    std::vector<RuntimeMultiplayerLockstepApplyCommand> leftApply;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> rightApply;
    success &= expect(
        left.driver.canonicalApplyList(12, leftApply, error)
            && right.driver.canonicalApplyList(12, rightApply, error)
            && leftApply == rightApply
            && left.driver.stateHash() == right.driver.stateHash()
            && left.session.stateHash() == right.session.stateHash(),
        "arrival order does not change apply order or deterministic hashes: "
            + error
    );
    success &= expect(
        leftApply.size() == 3U
            && leftApply[0].peerId == "alpha"
            && leftApply[0].commandIndex == 0U
            && leftApply[1].peerId == "alpha"
            && leftApply[1].commandIndex == 1U
            && leftApply[2].peerId == "zulu"
            && leftApply[2].commandIndex == 0U
            && std::get<RuntimeMultiplayerGameplayStopCommand>(
                leftApply[2].command.body
            ).objectIds == std::vector<std::int32_t>({3, 9}),
        "apply list is peer-id ordered and preserves in-frame command order"
    );

    const std::uint64_t beforeFailedApply = left.driver.stateHash();
    int prevalidateCalls = 0;
    int applyCalls = 0;
    const RuntimeMultiplayerLockstepBatchCallback rejectPrevalidate =
        [&prevalidateCalls](
            std::span<const RuntimeMultiplayerLockstepApplyCommand>,
            std::string& callbackError
        ) {
            ++prevalidateCalls;
            callbackError = "fixture prevalidation rejection";
            return false;
        };
    const RuntimeMultiplayerLockstepBatchCallback countApply =
        [&applyCalls](
            std::span<const RuntimeMultiplayerLockstepApplyCommand>,
            std::string&
        ) {
            ++applyCalls;
            return true;
        };
    success &= expect(
        !left.driver.applyReadyTick(
            12,
            rejectPrevalidate,
            countApply
        ).accepted
            && prevalidateCalls == 1 && applyCalls == 0
            && !left.driver.tickCompleted(12)
            && left.driver.stateHash() == beforeFailedApply,
        "prevalidation failure is atomic and skips apply"
    );

    const RuntimeMultiplayerLockstepBatchCallback acceptPrevalidate =
        [](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
           std::string&) {
            return batch.size() == 3U;
        };
    const RuntimeMultiplayerLockstepBatchCallback rejectApply =
        [&applyCalls](
            std::span<const RuntimeMultiplayerLockstepApplyCommand>,
            std::string& callbackError
        ) {
            ++applyCalls;
            callbackError = "fixture apply rejection";
            return false;
        };
    success &= expect(
        !left.driver.applyReadyTick(
            12,
            acceptPrevalidate,
            rejectApply
        ).accepted
            && applyCalls == 1 && !left.driver.tickCompleted(12)
            && left.driver.stateHash() == beforeFailedApply,
        "apply callback failure does not complete or mutate the driver tick"
    );

    const RuntimeMultiplayerLockstepBatchCallback acceptApply =
        [](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
           std::string&) {
            return batch.size() == 3U;
        };
    success &= expect(
        left.driver.applyReadyTick(
            12,
            acceptPrevalidate,
            acceptApply
        ).accepted
            && left.driver.tickCompleted(12),
        "validated batch applies once"
    );
    success &= expect(
        right.driver.applyReadyTick(
            12,
            acceptPrevalidate,
            acceptApply
        ).accepted
            && left.driver.stateHash() == right.driver.stateHash(),
        "client applies the same canonical tick 12 batch"
    );
    success &= expect(
        !left.driver.submitFrame(packets.alpha12).accepted,
        "a completed tick cannot be replayed"
    );
    const std::uint64_t beforeMissingHorizonAdvance =
        left.driver.stateHash();
    success &= expect(
        !left.driver.inputHorizonReady()
            && !left.driver.advanceHost("zulu", 12).accepted
            && left.session.authoritativeTick() == 12
            && left.driver.currentBarrierTick() == 12
            && left.driver.tickCompleted(12)
            && left.driver.stateHash() == beforeMissingHorizonAdvance,
        "missing slow-peer tick 14 input holds the applied tick 12 advance"
    );
    success &= submitAccepted(
        left.driver,
        packets.zulu14,
        "left slow-peer tick 14 horizon frame"
    );
    success &= submitAccepted(
        right.driver,
        packets.zulu14,
        "right slow-peer tick 14 horizon frame"
    );
    success &= expect(
        left.driver.inputHorizonReady()
            && right.driver.inputHorizonReady()
            && left.driver.advanceHost("zulu", 12).accepted
            && left.session.authoritativeTick() == 13
            && left.driver.currentBarrierTick() == 13
            && right.session.advanceAuthoritativeTick("zulu", 13).accepted
            && right.driver.acknowledgeAuthoritativeAdvance(12).accepted
            && right.driver.currentBarrierTick() == 13
            && left.driver.stateHash() == right.driver.stateHash(),
        "host and replicated-client paths converge after tick 12"
    );

    std::size_t emptyPrevalidations = 0;
    std::size_t emptyApplications = 0;
    success &= expect(
        left.driver.frameBarrierReady(13)
            && left.driver.applyReadyTick(
                13,
                [&emptyPrevalidations](
                    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
                    std::string&
                ) {
                    ++emptyPrevalidations;
                    return batch.empty();
                },
                [&emptyApplications](
                    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
                    std::string&
                ) {
                    ++emptyApplications;
                    return batch.empty();
                }
            ).accepted
            && emptyPrevalidations == 1U && emptyApplications == 1U
            && right.driver.applyReadyTick(
                13,
                requireEmpty,
                requireEmpty
            ).accepted,
        "two explicit empty combatant frames satisfy the network barrier"
    );
    success &= expect(
        !left.driver.inputHorizonReady()
            && !right.driver.inputHorizonReady()
            && !left.driver.advanceHost("zulu", 13).accepted
            && left.session.authoritativeTick() == 13,
        "the next unfilled input horizon holds an already-applied empty frame"
    );
    const RuntimeMultiplayerCommandPacket alpha15 = packet(
        15,
        "alpha",
        4,
        RuntimeMultiplayerGameplayFrame{}
    );
    const RuntimeMultiplayerCommandPacket zulu15 = packet(
        15,
        "zulu",
        4,
        RuntimeMultiplayerGameplayFrame{}
    );
    success &= submitAccepted(left.driver, alpha15, "left alpha tick 15");
    success &= submitAccepted(left.driver, zulu15, "left zulu tick 15");
    success &= submitAccepted(right.driver, alpha15, "right alpha tick 15");
    success &= submitAccepted(right.driver, zulu15, "right zulu tick 15");
    success &= expect(
        left.driver.inputHorizonReady()
            && right.driver.inputHorizonReady()
            && left.driver.advanceHost("zulu", 13).accepted
            && left.session.authoritativeTick() == 14
            && right.session.advanceAuthoritativeTick("zulu", 14).accepted
            && right.driver.acknowledgeAuthoritativeAdvance(13).accepted
            && right.session.authoritativeTick() == 14
            && left.driver.stateHash() == right.driver.stateHash(),
        "host and client both prune the completed empty-frame barrier"
    );
    std::vector<RuntimeMultiplayerLockstepApplyCommand> spectatorFreeApply{{
        99,
        "sentinel",
        9,
        0,
        sell(99)
    }};
    success &= expect(
        left.driver.frameBarrierReady(14)
            && left.driver.canonicalApplyList(
                14,
                spectatorFreeApply,
                error
            )
            && spectatorFreeApply.empty(),
        "both combatants' empty frames suffice; spectator needs no tick 14 frame"
    );
    return success;
}

bool testMissingCombatantBarrier() {
    bool success = true;
    RuntimeMultiplayerLockstepConfig noDelay;
    noDelay.inputDelayTicks = 0;
    Fixture fixture;
    success &= expect(
        prepareFixture(fixture, noDelay)
            && fixture.driver.currentBarrierTick() == 10
            && fixture.driver.firstNetworkInputTick() == 10,
        "zero-delay fixture begins with a network-input barrier"
    );
    success &= submitAccepted(
        fixture.driver,
        packet(10, "zulu", 1, RuntimeMultiplayerGameplayFrame{}),
        "single zero-delay combatant frame"
    );

    std::vector<RuntimeMultiplayerLockstepApplyCommand> unchanged{{
        99,
        "sentinel",
        9,
        0,
        sell(99)
    }};
    const auto unchangedBefore = unchanged;
    std::string error;
    success &= expect(
        !fixture.driver.frameBarrierReady(10)
            && !fixture.driver.canonicalApplyList(10, unchanged, error)
            && unchanged == unchangedBefore
            && !fixture.driver.advanceHost("zulu", 10).accepted
            && fixture.session.authoritativeTick() == 10,
        "one missing combatant holds the barrier and output atomically"
    );
    return success;
}

bool testRunningSpectatorDepartureKeepsBinding() {
    RuntimeMultiplayerLockstepConfig noDelay;
    noDelay.inputDelayTicks = 0;
    Fixture fixture;
    bool success = expect(
        prepareFixture(fixture, noDelay),
        "spectator-departure fixture binds"
    );
    success &= expect(
        fixture.session.leaveMember("mid").accepted
            && fixture.session.members().size() == 2U
            && fixture.session.departedSpectatorPeerIds()
                == std::vector<std::string>{"mid"},
        "running spectator departure is recorded canonically"
    );
    success &= submitAccepted(
        fixture.driver,
        packet(10, "alpha", 1, RuntimeMultiplayerGameplayFrame{}),
        "post-departure alpha frame"
    );
    success &= submitAccepted(
        fixture.driver,
        packet(10, "zulu", 1, RuntimeMultiplayerGameplayFrame{}),
        "post-departure zulu frame"
    );
    const RuntimeMultiplayerLockstepBatchCallback requireEmpty =
        [](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
           std::string&) {
            return batch.empty();
        };
    success &= expect(
        fixture.driver.frameBarrierReady(10)
            && fixture.driver.inputHorizonReady()
            && fixture.driver.applyReadyTick(
                10,
                requireEmpty,
                requireEmpty
            ).accepted
            && fixture.driver.advanceHost("zulu", 10).accepted
            && fixture.driver.currentBarrierTick() == 11
            && fixture.session.authoritativeTick() == 11,
        "spectator-only roster contraction preserves subsequent lockstep"
    );
    return success;
}

bool testReplicatedAdvanceHorizonGuard() {
    bool success = true;
    Fixture fixture;
    success &= expect(
        prepareFixture(fixture),
        "replicated horizon-guard fixture binds"
    );
    success &= submitAccepted(
        fixture.driver,
        packet(12, "alpha", 1, RuntimeMultiplayerGameplayFrame{}),
        "guard alpha tick 12"
    );
    success &= submitAccepted(
        fixture.driver,
        packet(12, "zulu", 1, RuntimeMultiplayerGameplayFrame{}),
        "guard zulu tick 12"
    );
    const RuntimeMultiplayerLockstepBatchCallback requireEmpty =
        [](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
           std::string&) {
            return batch.empty();
        };
    success &= expect(
        fixture.driver.inputHorizonReady()
            && fixture.driver.applyReadyTick(
                10,
                requireEmpty,
                requireEmpty
            ).accepted
            && fixture.session.advanceAuthoritativeTick(
                "zulu",
                11
            ).accepted
            && fixture.driver.acknowledgeAuthoritativeAdvance(10).accepted,
        "client accepts a replicated advance with a complete horizon"
    );
    success &= expect(
        fixture.driver.applyReadyTick(
            11,
            requireEmpty,
            requireEmpty
        ).accepted
            && !fixture.driver.inputHorizonReady(),
        "client exposes the missing tick 13 horizon before Session mutation"
    );
    success &= expect(
        fixture.session.advanceAuthoritativeTick("zulu", 12).accepted,
        "fixture simulates an incorrectly ordered replicated advance"
    );
    const std::uint64_t beforeRejectedAcknowledge =
        fixture.driver.stateHash();
    success &= expect(
        !fixture.driver.acknowledgeAuthoritativeAdvance(11).accepted
            && fixture.driver.currentBarrierTick() == 11
            && fixture.driver.tickCompleted(11)
            && fixture.driver.stateHash() == beforeRejectedAcknowledge,
        "client acknowledgement atomically rejects an incomplete horizon"
    );
    return success;
}

bool testObserveAlreadyAdmittedFrames() {
    bool success = true;
    RuntimeMultiplayerSessionState hostSession;
    RuntimeMultiplayerSessionState clientSession;
    RuntimeMultiplayerLockstepDriver hostDriver;
    RuntimeMultiplayerLockstepDriver clientDriver;
    success &= expect(
        startSession(hostSession) && startSession(clientSession),
        "observer-bound host and client sessions start"
    );
    const RuntimeMultiplayerSessionState& constHostSession = hostSession;
    const RuntimeMultiplayerSessionState& constClientSession = clientSession;
    success &= expect(
        hostDriver.bind(constHostSession).accepted
            && clientDriver.bind(constClientSession).accepted,
        "lockstep drivers bind read-only LAN session views"
    );

    const RuntimeMultiplayerCommandPacket alpha12 = packet(
        12,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(12)}}
    );
    const RuntimeMultiplayerCommandPacket zulu12 = packet(
        12,
        "zulu",
        1,
        RuntimeMultiplayerGameplayFrame{}
    );
    const std::uint64_t beforeReadOnlyMisuse = hostDriver.stateHash();
    success &= expect(
        !hostDriver.submitFrame(alpha12).accepted
            && !hostDriver.advanceHost("zulu", 10).accepted
            && hostSession.commands().empty()
            && hostDriver.stateHash() == beforeReadOnlyMisuse,
        "read-only LAN binding rejects direct submit and direct host advance"
    );

    success &= expect(
        hostSession.submitCommand(zulu12).accepted
            && hostDriver.observeAdmittedFrame(zulu12).accepted
            && hostSession.submitCommand(alpha12).accepted
            && hostDriver.observeAdmittedFrame(alpha12).accepted,
        "host driver observes Session-admitted empty and non-empty frames"
    );
    success &= expect(
        clientSession.submitCommand(alpha12).accepted
            && clientDriver.observeAdmittedFrame(alpha12).accepted
            && clientSession.submitCommand(zulu12).accepted
            && clientDriver.observeAdmittedFrame(zulu12).accepted
            && hostSession.stateHash() == clientSession.stateHash()
            && hostDriver.stateHash() == clientDriver.stateHash()
            && hostDriver.inputHorizonReady()
            && clientDriver.inputHorizonReady(),
        "different LAN arrival order converges after admitted-frame observe"
    );
    const RuntimeMultiplayerOperationResult duplicate =
        hostDriver.observeAdmittedFrame(alpha12);
    success &= expect(
        duplicate.accepted && duplicate.duplicate && !duplicate.changed,
        "re-observing an identical admitted envelope is idempotent"
    );

    const RuntimeMultiplayerCommandPacket admittedAlpha13 = packet(
        13,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    RuntimeMultiplayerCommandPacket differentAlpha13 = packet(
        13,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{{repair(13)}}
    );
    success &= expect(
        hostSession.submitCommand(admittedAlpha13).accepted,
        "different-envelope seed is admitted to Session"
    );
    const std::uint64_t beforeDifferentObserve = hostDriver.stateHash();
    success &= expect(
        !hostDriver.observeAdmittedFrame(differentAlpha13).accepted
            && hostDriver.stateHash() == beforeDifferentObserve
            && hostDriver.observeAdmittedFrame(admittedAlpha13).accepted,
        "absent/different envelope is rejected before exact admitted observe"
    );
    const RuntimeMultiplayerCommandPacket absentZulu13 = packet(
        13,
        "zulu",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    const std::uint64_t beforeAbsentObserve = hostDriver.stateHash();
    success &= expect(
        !hostDriver.observeAdmittedFrame(absentZulu13).accepted
            && hostDriver.stateHash() == beforeAbsentObserve,
        "an envelope absent from Session cannot be observed"
    );

    RuntimeMultiplayerSessionState malformedSession;
    RuntimeMultiplayerLockstepDriver malformedDriver;
    success &= expect(
        startSession(malformedSession),
        "malformed admitted-frame session starts"
    );
    const RuntimeMultiplayerSessionState& constMalformedSession =
        malformedSession;
    success &= expect(
        malformedDriver.bind(constMalformedSession).accepted,
        "malformed admitted-frame observer binds"
    );
    RuntimeMultiplayerCommandPacket malformed;
    malformed.tick = 12;
    malformed.peerId = "alpha";
    malformed.sequence = 1;
    malformed.payload = {0x43U, 0x47U};
    success &= expect(
        malformedSession.submitCommand(malformed).accepted,
        "opaque Session admits malformed gameplay-frame bytes"
    );
    const std::uint64_t beforeMalformedObserve =
        malformedDriver.stateHash();
    success &= expect(
        !malformedDriver.observeAdmittedFrame(malformed).accepted
            && malformedDriver.stateHash() == beforeMalformedObserve
            && !malformedDriver.inputHorizonReady(),
        "malformed admitted payload is atomically rejected by the driver"
    );
    return success;
}

bool testAdmissionConflictsAndConfiguredBounds() {
    bool success = true;
    RuntimeMultiplayerLockstepConfig limits;
    limits.inputDelayTicks = 2;
    limits.maximumCommandsPerFrame = 2;
    limits.maximumFrameBytes = 4096;
    Fixture fixture;
    success &= expect(
        prepareFixture(fixture, limits),
        "bounded conflict fixture binds"
    );

    const RuntimeMultiplayerCommandPacket spectator = packet(
        12,
        "mid",
        1,
        RuntimeMultiplayerGameplayFrame{}
    );
    std::uint64_t before = fixture.driver.stateHash();
    const std::uint64_t sessionBefore = fixture.session.stateHash();
    success &= expect(
        !fixture.driver.submitFrame(spectator).accepted
            && fixture.driver.stateHash() == before
            && fixture.session.stateHash() == sessionBefore,
        "spectator frame injection is rejected atomically"
    );

    const RuntimeMultiplayerCommandPacket tooEarly = packet(
        11,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{}
    );
    success &= expect(
        !fixture.driver.submitFrame(tooEarly).accepted
            && fixture.driver.stateHash() == before,
        "frame before the fixed input-delay tick is rejected"
    );

    RuntimeMultiplayerCommandPacket malformed;
    malformed.tick = 12;
    malformed.peerId = "alpha";
    malformed.sequence = 1;
    malformed.payload = {0x43U, 0x47U};
    success &= expect(
        !fixture.driver.submitFrame(malformed).accepted
            && fixture.driver.stateHash() == before
            && fixture.session.commands().empty(),
        "bad frame payload does not consume session envelope sequence"
    );

    const RuntimeMultiplayerCommandPacket alpha12 = packet(
        12,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(1)}}
    );
    success &= submitAccepted(fixture.driver, alpha12, "valid alpha frame");
    before = fixture.driver.stateHash();
    const std::size_t commandsBeforeDuplicate =
        fixture.session.commands().size();
    const RuntimeMultiplayerOperationResult duplicate =
        fixture.driver.submitFrame(alpha12);
    success &= expect(
        duplicate.accepted && duplicate.duplicate && !duplicate.changed
            && fixture.driver.stateHash() == before
            && fixture.session.commands().size() == commandsBeforeDuplicate,
        "identical frame retransmission is idempotent"
    );

    const RuntimeMultiplayerCommandPacket conflict = packet(
        12,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{{repair(1)}}
    );
    success &= expect(
        !fixture.driver.submitFrame(conflict).accepted
            && fixture.driver.stateHash() == before
            && fixture.session.commands().size() == commandsBeforeDuplicate,
        "different second frame for one peer/tick is atomically rejected"
    );

    const RuntimeMultiplayerCommandPacket alpha13 = packet(
        13,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    success &= submitAccepted(
        fixture.driver,
        alpha13,
        "sequence rejected by conflict remains available"
    );
    const RuntimeMultiplayerCommandPacket repeatedSequence = packet(
        14,
        "alpha",
        2,
        RuntimeMultiplayerGameplayFrame{}
    );
    before = fixture.driver.stateHash();
    success &= expect(
        !fixture.driver.submitFrame(repeatedSequence).accepted
            && fixture.driver.stateHash() == before,
        "per-peer frame sequence is strictly increasing"
    );

    const RuntimeMultiplayerGameplayFrame tooMany{
        {sell(1), repair(2), sell(3)}
    };
    const RuntimeMultiplayerCommandPacket tooManyPacket = packet(
        12,
        "zulu",
        1,
        tooMany
    );
    before = fixture.driver.stateHash();
    success &= expect(
        !fixture.driver.submitFrame(tooManyPacket).accepted
            && fixture.driver.stateHash() == before,
        "driver rejects configured command count plus one"
    );
    const RuntimeMultiplayerCommandPacket exactCommands = packet(
        12,
        "zulu",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(2), repair(3)}}
    );
    success &= submitAccepted(
        fixture.driver,
        exactCommands,
        "driver accepts exact configured command count"
    );

    RuntimeMultiplayerLockstepConfig byteLimits;
    byteLimits.inputDelayTicks = 2;
    byteLimits.maximumCommandsPerFrame = 2;
    const RuntimeMultiplayerCommandPacket exactBytePacket = packet(
        12,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(42)}}
    );
    byteLimits.maximumFrameBytes = exactBytePacket.payload.size();
    Fixture byteFixture;
    success &= expect(
        prepareFixture(byteFixture, byteLimits)
            && byteFixture.driver.submitFrame(exactBytePacket).accepted,
        "driver accepts exact configured frame byte limit"
    );
    RuntimeMultiplayerCommandPacket overBytePacket = packet(
        12,
        "zulu",
        1,
        RuntimeMultiplayerGameplayFrame{{sell(1), sell(2)}}
    );
    before = byteFixture.driver.stateHash();
    success &= expect(
        overBytePacket.payload.size() > byteLimits.maximumFrameBytes
            && !byteFixture.driver.submitFrame(overBytePacket).accepted
            && byteFixture.driver.stateHash() == before,
        "driver rejects configured frame byte limit plus data"
    );

    RuntimeMultiplayerSessionState invalidDelaySession;
    RuntimeMultiplayerLockstepDriver invalidDelayDriver;
    success &= expect(
        startSession(invalidDelaySession),
        "invalid-delay session starts"
    );
    RuntimeMultiplayerLockstepConfig invalidDelay;
    invalidDelay.inputDelayTicks =
        invalidDelaySession.config().commandLeadWindowTicks + 1;
    success &= expect(
        !invalidDelayDriver.bind(
            invalidDelaySession,
            invalidDelay
        ).accepted
            && !invalidDelayDriver.bound(),
        "input delay above the session lead window is rejected"
    );
    invalidDelay.inputDelayTicks = -1;
    success &= expect(
        !invalidDelayDriver.bind(
            invalidDelaySession,
            invalidDelay
        ).accepted
            && !invalidDelayDriver.bound(),
        "negative input delay is rejected"
    );

    RuntimeMultiplayerLockstepConfig exactDelay;
    exactDelay.inputDelayTicks =
        invalidDelaySession.config().commandLeadWindowTicks;
    Fixture exactDelayFixture;
    success &= expect(
        prepareFixture(exactDelayFixture, exactDelay)
            && exactDelayFixture.driver.currentBarrierTick()
                == 10
            && exactDelayFixture.driver.firstNetworkInputTick()
                == 10 + exactDelay.inputDelayTicks
            && exactDelayFixture.driver.submitFrame(packet(
                10 + exactDelay.inputDelayTicks,
                "alpha",
                1,
                RuntimeMultiplayerGameplayFrame{}
            )).accepted,
        "exact lead-window delay admits its first frame after warm-up"
    );

    RuntimeMultiplayerSessionState lateSession;
    RuntimeMultiplayerLockstepDriver lateDriver;
    success &= expect(
        startSession(lateSession)
            && lateSession.advanceAuthoritativeTick("zulu", 11).accepted
            && !lateDriver.bind(lateSession).accepted
            && !lateDriver.bound(),
        "binding after the session start tick cannot fabricate warm-up history"
    );
    return success;
}

bool testTimelineEpochInvalidationAndLoadRebind() {
    bool success = true;
    Fixture fixture;
    success &= expect(
        prepareFixture(fixture)
            && fixture.driver.timelineEpoch() == 1
            && fixture.driver.currentBarrierTick() == 10,
        "lockstep captures initial timeline epoch and start barrier"
    );

    const RuntimeMultiplayerSaveLoadProposal save = snapshotProposal(
        fixture.session,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        9,
        "lockstep-save"
    );
    success &= expect(
        completeSnapshotHandshake(fixture.session, save)
            && fixture.session.timelineEpoch() == 1
            && fixture.driver.timelineEpoch() == 1
            && fixture.driver.frameBarrierReady(10),
        "Save commit preserves the live lockstep binding"
    );

    const RuntimeMultiplayerSaveLoadProposal load = snapshotProposal(
        fixture.session,
        2,
        RuntimeMultiplayerSaveLoadKind::Load,
        9,
        "lockstep-load"
    );
    success &= expect(
        completeSnapshotHandshake(fixture.session, load)
            && fixture.session.timelineEpoch() == 2
            && fixture.session.authoritativeTick() == 10
            && !fixture.driver.frameBarrierReady(10),
        "Load commit advances epoch and makes the old driver stale"
    );
    RuntimeMultiplayerCommandPacket stale = packet(
        12,
        "alpha",
        1,
        RuntimeMultiplayerGameplayFrame{}
    );
    const std::uint64_t staleHash = fixture.driver.stateHash();
    success &= expect(
        !fixture.driver.submitFrame(stale).accepted
            && fixture.driver.stateHash() == staleHash,
        "old-epoch gameplay frame is rejected atomically"
    );
    RuntimeMultiplayerLockstepDriver accidentalInitialBind;
    success &= expect(
        !accidentalInitialBind.bind(fixture.session).accepted
            && fixture.driver.rebindAfterLoadCommit(
                fixture.session
            ).accepted
            && fixture.driver.timelineEpoch() == 2
            && fixture.driver.currentBarrierTick() == 10
            && fixture.driver.firstNetworkInputTick() == 12,
        "post-load state requires explicit rebind at snapshotTick plus one"
    );
    stale.timelineEpoch = 2;
    success &= expect(
        fixture.driver.submitFrame(stale).accepted,
        "rebound driver accepts only the new timeline epoch"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testFrameCodecAndGoldenBytes();
    success &= testMalformedAndCodecBounds();
    success &= testBarrierOrderingHashAndCallbacks();
    success &= testMissingCombatantBarrier();
    success &= testRunningSpectatorDepartureKeepsBinding();
    success &= testReplicatedAdvanceHorizonGuard();
    success &= testObserveAlreadyAdmittedFrames();
    success &= testAdmissionConflictsAndConfiguredBounds();
    success &= testTimelineEpochInvalidationAndLoadRebind();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer lockstep smoke: frameVersion=1"
        << " combatants=2 spectators=1 canonicalOrder=peer+authored"
        << " emptyBarrier=yes callbacks=prevalidate+transactionalApply"
        << " spectatorDeparture=monotonic validation=strict\n";
    return 0;
}
