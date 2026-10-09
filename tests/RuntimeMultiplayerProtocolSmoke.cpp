#include "RuntimeMultiplayerProtocol.h"
#include "RuntimeMultiplayerCompatibilityFingerprint.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "runtime multiplayer protocol smoke failure: "
            << message << '\n';
    }
    return condition;
}

RuntimeMultiplayerCompatibility compatibility() {
    RuntimeMultiplayerCompatibility result;
    result.protocolFingerprint = 0x1020304050607080ULL;
    result.contentFingerprint = 0xa1b2c3d4e5f60718ULL;
    return result;
}

RuntimeMultiplayerSessionConfig standardConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "room-alpha";
    config.hostPeerId = "host";
    config.hostDisplayName = "Host Player";
    config.compatibility = compatibility();
    config.maximumMembers = 4;
    config.maximumCommandPayloadBytes = 64;
    config.maximumBufferedCommands = 32;
    config.maximumBufferedCommandsPerPeer = 8;
    config.maximumStateHashFrames = 16;
    config.maximumSaveLoadHandshakes = 8;
    config.commandLeadWindowTicks = 50;
    config.stateHashLagWindowTicks = 40;
    config.stateHashPeriodTicks = 10;
    return config;
}

RuntimeMultiplayerJoinRequest joinRequest(
    std::string peerId = "alpha",
    std::string displayName = "Alpha Player"
) {
    RuntimeMultiplayerJoinRequest request;
    request.peerId = std::move(peerId);
    request.displayName = std::move(displayName);
    request.compatibility = compatibility();
    return request;
}

RuntimeMultiplayerMatchSetup matchSetupFor(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t revision
) {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = revision;
    setup.hostPeerId = session.config().hostPeerId;
    setup.map.canonicalId = "maps/official/protocol-smoke.map";
    setup.map.contentFingerprint =
        session.config().compatibility.contentFingerprint;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x123456789abcdef0ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    std::uint8_t slotIndex = 0;
    for (const RuntimeMultiplayerRoomMember& roomMember : session.members()) {
        RuntimeMultiplayerMatchMember setupMember;
        setupMember.peerId = roomMember.peerId;
        setupMember.displayName = roomMember.displayName;
        setupMember.identity = roomMember.participation
                == RuntimeMultiplayerParticipationRole::Combatant
            ? RuntimeMultiplayerMatchMemberIdentity::Combatant
            : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        setup.members.push_back(std::move(setupMember));
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
    while (setup.slots.size() < 2U) {
        RuntimeMultiplayerMatchSlot slot;
        slot.slotIndex = slotIndex;
        slot.controller = RuntimeMultiplayerMatchSlotController::Computer;
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

bool installMatchSetup(
    RuntimeMultiplayerSessionState& session,
    std::uint64_t revision,
    bool& success,
    const std::string& label
) {
    const RuntimeMultiplayerOperationResult result = session.setMatchSetup(
        session.config().hostPeerId,
        matchSetupFor(session, revision)
    );
    success &= expect(
        result.accepted && result.changed
            && session.matchSetup().has_value(),
        label + ": " + result.reason
    );
    return result.accepted;
}

RuntimeMultiplayerSaveLoadProposal saveLoadProposal(
    std::uint64_t timelineEpoch,
    std::uint64_t proposalId,
    RuntimeMultiplayerSaveLoadKind kind,
    int barrierTick,
    int snapshotTick,
    std::string token,
    std::uint64_t snapshotFingerprint,
    std::uint64_t authoritativeStateHash,
    std::uint64_t gameplaySetupFingerprint
) {
    RuntimeMultiplayerSaveLoadProposal proposal;
    proposal.timelineEpoch = timelineEpoch;
    proposal.proposalId = proposalId;
    proposal.kind = kind;
    proposal.proposerPeerId = "host";
    proposal.descriptor.identity.proposalId = proposalId;
    proposal.descriptor.identity.kind =
        kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    proposal.descriptor.identity.sourceTimelineEpoch = timelineEpoch;
    proposal.descriptor.identity.barrierTick = barrierTick;
    proposal.descriptor.identity.snapshotTick = snapshotTick;
    proposal.descriptor.identity.snapshotToken = std::move(token);
    proposal.descriptor.identity.authoritativeStateHash =
        authoritativeStateHash;
    proposal.descriptor.identity.gameplaySetupFingerprint =
        gameplaySetupFingerprint;
    proposal.descriptor.byteCount = 1;
    proposal.descriptor.chunkCount = 1;
    proposal.descriptor.snapshotFingerprint = snapshotFingerprint;
    proposal.descriptor.sha256[0] = 1;
    proposal.tick = snapshotTick;
    proposal.snapshotId = proposal.descriptor.identity.snapshotToken;
    proposal.snapshotFingerprint = snapshotFingerprint;
    return proposal;
}

RuntimeMultiplayerSaveLoadProposal saveLoadProposal(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    RuntimeMultiplayerSaveLoadKind kind,
    int snapshotTick,
    std::string token,
    std::uint64_t snapshotFingerprint
) {
    std::uint64_t setupFingerprint = 0;
    std::string error;
    if (session.matchSetup().has_value()) {
        runtimeMultiplayerGameplaySetupFingerprint(
            *session.matchSetup(),
            setupFingerprint,
            error
        );
    }
    return saveLoadProposal(
        session.timelineEpoch(),
        proposalId,
        kind,
        session.authoritativeTick() - 1,
        snapshotTick,
        std::move(token),
        snapshotFingerprint,
        0x6a09e667f3bcc909ULL ^ proposalId,
        setupFingerprint
    );
}

template <typename Body>
RuntimeMultiplayerMessage message(
    RuntimeMultiplayerMessageType type,
    Body body
) {
    RuntimeMultiplayerMessage result;
    result.type = type;
    result.body = std::move(body);
    return result;
}

RuntimeNetworkPacket encode(
    const RuntimeMultiplayerMessage& source,
    bool& success,
    const std::string& label
) {
    RuntimeNetworkPacket packet;
    std::string error;
    success &= expect(
        encodeRuntimeMultiplayerMessage(source, packet, error),
        label + " encodes: " + error
    );
    return packet;
}

RuntimeMultiplayerMessageApplyResult apply(
    RuntimeMultiplayerSessionState& session,
    const RuntimeMultiplayerMessage& source,
    std::string_view authenticatedPeerId,
    bool& success,
    const std::string& label
) {
    const RuntimeNetworkPacket packet = encode(source, success, label);
    return applyRuntimeMultiplayerMessage(
        packet,
        authenticatedPeerId,
        session
    );
}

RuntimeMultiplayerMessageApplyResult applyTrusted(
    RuntimeMultiplayerSessionState& session,
    const RuntimeMultiplayerMessage& source,
    bool& success,
    const std::string& label
) {
    const RuntimeNetworkPacket packet = encode(source, success, label);
    return applyTrustedCanonicalRuntimeMultiplayerMessage(packet, session);
}

std::vector<RuntimeMultiplayerMessage> roundTripMessages() {
    RuntimeMultiplayerCommandPacket command;
    command.tick = 0x01020304;
    command.peerId = "alpha";
    command.sequence = 0x0102030405060708ULL;
    command.payload = {0xa0U, 0xb1U, 0xc2U};

    RuntimeMultiplayerStateHashReport stateHash;
    stateHash.tick = 0x01020304;
    stateHash.peerId = "alpha";
    stateHash.stateHash = 0x1122334455667788ULL;

    RuntimeMultiplayerSaveLoadProposal proposal = saveLoadProposal(
        0x0102030405060708ULL,
        0x0102030405060708ULL,
        RuntimeMultiplayerSaveLoadKind::Load,
        0x01020304,
        0x01020303,
        "slot-1-generation-2",
        0x8877665544332211ULL,
        0x1122334455667788ULL,
        0x99aabbccddeeff00ULL
    );

    RuntimeMultiplayerSaveLoadResponseMessage response;
    response.proposalId = proposal.proposalId;
    response.peerId = "alpha";
    response.approved = true;
    response.observedFingerprint = proposal.snapshotFingerprint;
    response.timelineEpoch = proposal.timelineEpoch;
    response.observedAuthoritativeStateHash =
        proposal.descriptor.identity.authoritativeStateHash;

    RuntimeMultiplayerAdvanceTickMessage advance;
    advance.requestingPeerId = "host";
    advance.authoritativeTick = 0x01020304;

    RuntimeMultiplayerSaveLoadAbortMessage abort;
    abort.proposalId = proposal.proposalId;
    abort.requestingPeerId = "host";
    abort.timelineEpoch = proposal.timelineEpoch;

    RuntimeMultiplayerSaveLoadCommitMessage commit;
    commit.timelineEpoch = proposal.timelineEpoch;
    commit.proposalId = proposal.proposalId;
    commit.requestingPeerId = "host";
    RuntimeMultiplayerSaveLoadAppliedMessage applied;
    applied.timelineEpoch = proposal.timelineEpoch;
    applied.proposalId = proposal.proposalId;
    applied.peerId = "alpha";
    applied.observedAuthoritativeStateHash =
        proposal.descriptor.identity.authoritativeStateHash;
    RuntimeMultiplayerSnapshotChunk chunk;
    chunk.proposalId = proposal.proposalId;
    chunk.sourceTimelineEpoch = proposal.timelineEpoch;
    chunk.chunkIndex = 0;
    chunk.bytes = {0x10U, 0x20U, 0x30U, 0x40U};
    RuntimeMultiplayerSnapshotChunkAck chunkAck;
    chunkAck.proposalId = proposal.proposalId;
    chunkAck.sourceTimelineEpoch = proposal.timelineEpoch;
    chunkAck.chunkIndex = 0;
    RuntimeMultiplayerLobbySlotOptionsRequestMessage slotRequest;
    slotRequest.peerId = "alpha";
    slotRequest.requestId = 7;
    slotRequest.expectedSetupRevision = 11;
    slotRequest.slotIndex = 1;
    slotRequest.countryId = "america";
    slotRequest.colorId = "red";
    slotRequest.teamId = "none";
    slotRequest.difficultyId = "medium";
    slotRequest.startPosition = 2;
    RuntimeMultiplayerLobbySlotOptionsResultMessage slotResult;
    slotResult.peerId = "alpha";
    slotResult.requestId = slotRequest.requestId;
    slotResult.accepted = true;
    slotResult.currentSetupRevision = 12;
    RuntimeMultiplayerLobbyKickedMessage kicked;
    kicked.roomGeneration = 0x1122334455667788ULL;
    kicked.peerId = "alpha";
    kicked.reason = "removed by host";

    return {
        message(
            RuntimeMultiplayerMessageType::JoinRequest,
            joinRequest()
        ),
        message(
            RuntimeMultiplayerMessageType::Leave,
            RuntimeMultiplayerLeaveMessage{"alpha"}
        ),
        message(
            RuntimeMultiplayerMessageType::Ready,
            RuntimeMultiplayerReadyMessage{"alpha", true}
        ),
        message(
            RuntimeMultiplayerMessageType::Start,
            RuntimeMultiplayerStartMessage{"host", 0x01020304}
        ),
        message(RuntimeMultiplayerMessageType::Command, command),
        message(RuntimeMultiplayerMessageType::StateHash, stateHash),
        message(
            RuntimeMultiplayerMessageType::SaveLoadProposal,
            proposal
        ),
        message(
            RuntimeMultiplayerMessageType::SaveLoadResponse,
            response
        ),
        message(
            RuntimeMultiplayerMessageType::RoomConfig,
            standardConfig()
        ),
        message(
            RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
            advance
        ),
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            abort
        ),
        message(
            RuntimeMultiplayerMessageType::SaveLoadCommit,
            commit
        ),
        message(
            RuntimeMultiplayerMessageType::SaveLoadApplied,
            applied
        ),
        message(
            RuntimeMultiplayerMessageType::SnapshotChunk,
            chunk
        ),
        message(
            RuntimeMultiplayerMessageType::SnapshotChunkAck,
            chunkAck
        ),
        message(
            RuntimeMultiplayerMessageType::LobbySlotOptionsRequest,
            slotRequest
        ),
        message(
            RuntimeMultiplayerMessageType::LobbySlotOptionsResult,
            slotResult
        ),
        message(
            RuntimeMultiplayerMessageType::LobbyKicked,
            kicked
        )
    };
}

bool testRoundTripsAndByteOrder() {
    bool success = true;
    const std::vector<RuntimeMultiplayerMessage> messages =
        roundTripMessages();
    for (const RuntimeMultiplayerMessage& source : messages) {
        const RuntimeNetworkPacket packet = encode(
            source,
            success,
            "round-trip message"
        );
        RuntimeMultiplayerMessage decoded;
        std::string error;
        success &= expect(
            decodeRuntimeMultiplayerMessage(packet, decoded, error)
                && decoded == source,
            "every authoritative message round-trips: " + error
        );
        success &= expect(
            (packet.type >> 8U) == runtimeMultiplayerMessageVersion,
            "message type carries its schema version"
        );
    }

    const RuntimeMultiplayerMessage& commandMessage = messages[4];
    const RuntimeNetworkPacket commandPacket = encode(
        commandMessage,
        success,
        "byte-order command"
    );
    const std::vector<std::uint8_t>& bytes = commandPacket.payload;
    success &= expect(
        bytes.size() == 36U
            && bytes[0] == 0x01U
            && bytes[1] == 0x02U
            && bytes[2] == 0x03U
            && bytes[3] == 0x04U
            && bytes[4] == 0x00U
            && bytes[5] == 0x00U
            && bytes[6] == 0x00U
            && bytes[7] == 0x05U
            && bytes[13] == 0x01U
            && bytes[14] == 0x02U
            && bytes[15] == 0x03U
            && bytes[16] == 0x04U
            && bytes[17] == 0x05U
            && bytes[18] == 0x06U
            && bytes[19] == 0x07U
            && bytes[20] == 0x08U
            && bytes[28] == 0x01U
            && bytes[29] == 0x00U
            && bytes[30] == 0x00U
            && bytes[31] == 0x00U
            && bytes[32] == 0x03U,
        "fixed integers and length prefixes use big-endian byte order"
    );
    const RuntimeNetworkPacket chunkPacket = encode(
        messages[13],
        success,
        "snapshot chunk golden"
    );
    const std::vector<std::uint8_t>& chunkBytes = chunkPacket.payload;
    success &= expect(
        chunkBytes.size() == 38U
            && chunkBytes[0] == 0x00U
            && chunkBytes[1] == 0x00U
            && chunkBytes[2] == 0x00U
            && chunkBytes[3] == 0x22U
            && chunkBytes[4] == 0x43U
            && chunkBytes[5] == 0x53U
            && chunkBytes[6] == 0x4eU
            && chunkBytes[7] == 0x43U
            && chunkBytes[8] == 0x00U
            && chunkBytes[9] == 0x01U
            && chunkBytes[10] == 0x01U
            && chunkBytes[17] == 0x08U,
        "coordinator chunk wraps the canonical big-endian transfer body"
    );
    return success;
}

bool testRoomConfigBaseline() {
    bool success = true;
    const RuntimeMultiplayerSessionConfig config = standardConfig();
    const RuntimeMultiplayerMessage configMessage = message(
        RuntimeMultiplayerMessageType::RoomConfig,
        config
    );

    RuntimeMultiplayerSessionState host;
    success &= expect(
        host.beginHost(config).accepted,
        "host baseline begins"
    );
    RuntimeMultiplayerSessionState replica;
    const RuntimeMultiplayerMessageApplyResult baseline = applyTrusted(
        replica,
        configMessage,
        success,
        "trusted room config"
    );
    success &= expect(
        baseline.decoded && baseline.accepted && baseline.changed
            && replica.config() == config
            && replica.stateHash() == host.stateHash(),
        "trusted RoomConfig establishes the exact authoritative baseline"
    );

    const std::uint64_t beforeDuplicate = replica.stateHash();
    const RuntimeMultiplayerMessageApplyResult duplicate = applyTrusted(
        replica,
        configMessage,
        success,
        "duplicate room config"
    );
    success &= expect(
        duplicate.accepted && duplicate.duplicate && !duplicate.changed
            && replica.stateHash() == beforeDuplicate,
        "identical RoomConfig retransmission is idempotent"
    );

    success &= expect(
        applyTrusted(
            replica,
            message(
                RuntimeMultiplayerMessageType::JoinRequest,
                joinRequest()
            ),
            success,
            "progressed config join"
        ).accepted,
        "replica admits a member before installing match setup"
    );
    (void)installMatchSetup(
        replica,
        1U,
        success,
        "progressed config match setup"
    );
    success &= expect(
        applyTrusted(
                replica,
                message(
                    RuntimeMultiplayerMessageType::Ready,
                    RuntimeMultiplayerReadyMessage{"host", true}
                ),
                success,
                "progressed config host ready"
            ).accepted
            && applyTrusted(
                replica,
                message(
                    RuntimeMultiplayerMessageType::Ready,
                    RuntimeMultiplayerReadyMessage{"alpha", true}
                ),
                success,
                "progressed config alpha ready"
            ).accepted
            && applyTrusted(
                replica,
                message(
                    RuntimeMultiplayerMessageType::Start,
                    RuntimeMultiplayerStartMessage{"host", 100}
                ),
                success,
                "progressed config start"
            ).accepted
            && applyTrusted(
                replica,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    RuntimeMultiplayerCommandPacket{
                        120,
                        "alpha",
                        1,
                        {0x01U}
                    }
                ),
                success,
                "progressed config command"
            ).accepted,
        "replica progresses beyond its initial RoomConfig"
    );
    const std::uint64_t beforeProgressedDuplicate = replica.stateHash();
    const RuntimeMultiplayerMessageApplyResult progressedDuplicate =
        applyTrusted(
            replica,
            configMessage,
            success,
            "progressed duplicate room config"
        );
    success &= expect(
        progressedDuplicate.accepted && progressedDuplicate.duplicate
            && !progressedDuplicate.changed
            && replica.status() == RuntimeMultiplayerRoomStatus::Running
            && replica.commands().size() == 1U
            && replica.stateHash() == beforeProgressedDuplicate,
        "RoomConfig retry never resets progressed session state"
    );

    RuntimeMultiplayerSessionConfig conflict = config;
    conflict.roomId = "room-conflict";
    const RuntimeMultiplayerMessageApplyResult conflicting = applyTrusted(
        replica,
        message(RuntimeMultiplayerMessageType::RoomConfig, conflict),
        success,
        "conflicting room config"
    );
    success &= expect(
        conflicting.decoded && !conflicting.accepted
            && replica.stateHash() == beforeProgressedDuplicate,
        "different RoomConfig cannot replace an active baseline"
    );

    RuntimeMultiplayerSessionState untrustedTarget;
    const std::uint64_t beforeUntrusted = untrustedTarget.stateHash();
    const RuntimeMultiplayerMessageApplyResult untrusted = apply(
        untrustedTarget,
        configMessage,
        "host",
        success,
        "untrusted room config"
    );
    success &= expect(
        untrusted.decoded && !untrusted.accepted
            && untrustedTarget.stateHash() == beforeUntrusted,
        "direct connection ingress cannot install RoomConfig"
    );
    return success;
}

bool buildRunningSession(
    RuntimeMultiplayerSessionState& session,
    bool& success
) {
    const RuntimeMultiplayerMessageApplyResult configured = applyTrusted(
        session,
        message(
            RuntimeMultiplayerMessageType::RoomConfig,
            standardConfig()
        ),
        success,
        "running session config"
    );
    const RuntimeMultiplayerMessageApplyResult joined = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::JoinRequest,
            joinRequest()
        ),
        {},
        success,
        "running session join"
    );
    const bool setupInstalled = installMatchSetup(
        session,
        1U,
        success,
        "running session match setup"
    );
    const RuntimeMultiplayerMessageApplyResult hostReady = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::Ready,
            RuntimeMultiplayerReadyMessage{"host", true}
        ),
        "host",
        success,
        "host ready"
    );
    const RuntimeMultiplayerMessageApplyResult alphaReady = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::Ready,
            RuntimeMultiplayerReadyMessage{"alpha", true}
        ),
        "alpha",
        success,
        "alpha ready"
    );
    const RuntimeMultiplayerMessageApplyResult started = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::Start,
            RuntimeMultiplayerStartMessage{"host", 100}
        ),
        "host",
        success,
        "host start"
    );
    return configured.accepted
        && joined.accepted
        && setupInstalled
        && hostReady.accepted
        && alphaReady.accepted
        && started.accepted;
}

RuntimeMultiplayerCommandPacket command(
    int tick,
    std::string peerId,
    std::uint64_t sequence,
    std::initializer_list<std::uint8_t> payload
) {
    RuntimeMultiplayerCommandPacket result;
    result.tick = tick;
    result.peerId = std::move(peerId);
    result.sequence = sequence;
    result.payload.assign(payload.begin(), payload.end());
    return result;
}

bool testApplicationAndCanonicalOrdering() {
    bool success = true;

    RuntimeMultiplayerSessionState lobby;
    success &= expect(
        applyTrusted(
            lobby,
            message(
                RuntimeMultiplayerMessageType::RoomConfig,
                standardConfig()
            ),
            success,
            "lobby config"
        ).accepted,
        "lobby config applies"
    );
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::JoinRequest,
                joinRequest()
            ),
            {},
            success,
            "alpha join"
        ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::JoinRequest,
                    joinRequest("beta", "Beta Player")
                ),
                {},
                success,
                "beta join"
            ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::Leave,
                    RuntimeMultiplayerLeaveMessage{"beta"}
                ),
                "beta",
                success,
                "beta leave"
        ).accepted,
        "join and leave events dispatch to the authoritative session"
    );
    (void)installMatchSetup(
        lobby,
        1U,
        success,
        "application lobby match setup"
    );
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::Ready,
                RuntimeMultiplayerReadyMessage{"host", true}
            ),
            "host",
            success,
            "lobby host ready"
        ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::Ready,
                    RuntimeMultiplayerReadyMessage{"alpha", true}
                ),
                "alpha",
                success,
                "lobby alpha ready"
            ).accepted,
        "ready events dispatch"
    );

    const std::uint64_t beforeUnauthorizedStart = lobby.stateHash();
    const RuntimeMultiplayerMessageApplyResult unauthorizedStart =
        applyTrusted(
            lobby,
            message(
                RuntimeMultiplayerMessageType::Start,
                RuntimeMultiplayerStartMessage{"alpha", 100}
            ),
            success,
            "canonical unauthorized start"
        );
    success &= expect(
        unauthorizedStart.decoded && !unauthorizedStart.accepted
            && lobby.stateHash() == beforeUnauthorizedStart,
        "trusted canonical Start still uses session host authorization"
    );
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::Start,
                RuntimeMultiplayerStartMessage{"host", 100}
            ),
            "host",
            success,
            "authorized start"
        ).accepted,
        "authenticated host Start dispatches"
    );

    RuntimeMultiplayerCommandPacket locallyOversized =
        command(120, "alpha", 99, {0x01U});
    locallyOversized.payload.resize(65U, 0x01U);
    const std::uint64_t beforeLocalLimit = lobby.stateHash();
    const RuntimeMultiplayerMessageApplyResult localLimit = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::Command,
            locallyOversized
        ),
        "alpha",
        success,
        "session-local payload limit"
    );
    success &= expect(
        localLimit.decoded && !localLimit.accepted
            && lobby.stateHash() == beforeLocalLimit,
        "session config may reject a globally codec-valid command atomically"
    );

    RuntimeMultiplayerStateHashReport hostHash{110, "host", 0xabc1ULL};
    RuntimeMultiplayerStateHashReport alphaHash{110, "alpha", 0xabc1ULL};
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 110}
            ),
            "host",
            success,
            "advance to hash tick"
        ).accepted
            && apply(
            lobby,
            message(RuntimeMultiplayerMessageType::StateHash, alphaHash),
            "alpha",
            success,
            "alpha state hash"
        ).accepted
            && apply(
                lobby,
                message(RuntimeMultiplayerMessageType::StateHash, hostHash),
                "host",
                success,
                "host state hash"
            ).stateHashFrameComplete,
        "state hash reports dispatch and complete a frame"
    );

    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 120}
            ),
            "host",
            success,
            "advance to save barrier"
        ).accepted,
        "host reaches the save barrier"
    );
    RuntimeMultiplayerSaveLoadProposal proposal = saveLoadProposal(
        lobby,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        119,
        "slot-1-generation-1",
        0x9192939495969798ULL
    );
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::SaveLoadProposal,
                proposal
            ),
            "host",
            success,
            "save proposal"
        ).accepted,
        "host save/load proposal dispatches"
    );
    RuntimeMultiplayerSaveLoadResponseMessage hostResponse{
        1, "host", true, proposal.snapshotFingerprint, 1,
        proposal.descriptor.identity.authoritativeStateHash
    };
    RuntimeMultiplayerSaveLoadResponseMessage alphaResponse{
        1, "alpha", true, proposal.snapshotFingerprint, 1,
        proposal.descriptor.identity.authoritativeStateHash
    };
    const RuntimeMultiplayerMessageApplyResult firstResponse = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadResponse,
            hostResponse
        ),
        "host",
        success,
        "host save response"
    );
    const RuntimeMultiplayerMessageApplyResult secondResponse = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadResponse,
            alphaResponse
        ),
        "alpha",
        success,
        "alpha save response"
    );
    success &= expect(
        firstResponse.accepted && secondResponse.accepted
            && secondResponse.saveLoadStatus
                == RuntimeMultiplayerSaveLoadStatus::Prepared,
        "save/load prepare messages dispatch and reach Prepared"
    );

    const RuntimeMultiplayerMessageApplyResult commit = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadCommit,
            RuntimeMultiplayerSaveLoadCommitMessage{1, 1, "host"}
        ),
        "host",
        success,
        "save commit"
    );
    const RuntimeMultiplayerSaveLoadAppliedMessage hostApplied{
        1, 1, "host", true,
        proposal.descriptor.identity.authoritativeStateHash
    };
    const RuntimeMultiplayerSaveLoadAppliedMessage alphaApplied{
        1, 1, "alpha", true,
        proposal.descriptor.identity.authoritativeStateHash
    };
    success &= expect(
        commit.accepted
            && apply(lobby, message(
                RuntimeMultiplayerMessageType::SaveLoadApplied,
                hostApplied), "host", success, "host save applied").accepted
            && apply(lobby, message(
                RuntimeMultiplayerMessageType::SaveLoadApplied,
                alphaApplied), "alpha", success, "alpha save applied").accepted
            && lobby.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Committed,
        "host commit plus every applied ack reaches Committed"
    );

    RuntimeMultiplayerSaveLoadProposal stalled = saveLoadProposal(
        lobby,
        2,
        RuntimeMultiplayerSaveLoadKind::Save,
        119,
        "slot-2-stalled",
        0x8283848586878889ULL
    );
    const RuntimeMultiplayerCommandPacket barrierCommand =
        command(120, "alpha", 1, {0x51U});
    RuntimeMultiplayerStateHashReport barrierHash{
        120,
        "alpha",
        0x5151ULL
    };
    success &= expect(
        apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::Command,
                barrierCommand
            ),
            "alpha",
            success,
            "pre-barrier command"
        ).accepted
            && apply(
                lobby,
                message(RuntimeMultiplayerMessageType::StateHash, barrierHash),
                "alpha",
                success,
                "pre-barrier state hash"
            ).accepted
            && apply(
            lobby,
            message(
                RuntimeMultiplayerMessageType::SaveLoadProposal,
                stalled
            ),
            "host",
            success,
            "stalled save proposal"
        ).accepted,
        "a later save/load proposal enters pending state"
    );
    const std::uint64_t heldBarrierHash = lobby.stateHash();
    const RuntimeMultiplayerMessageApplyResult commandRetry = apply(
        lobby,
        message(RuntimeMultiplayerMessageType::Command, barrierCommand),
        "alpha",
        success,
        "pending command retry"
    );
    const RuntimeMultiplayerMessageApplyResult hashRetry = apply(
        lobby,
        message(RuntimeMultiplayerMessageType::StateHash, barrierHash),
        "alpha",
        success,
        "pending hash retry"
    );
    RuntimeMultiplayerStateHashReport newBarrierHash{
        120,
        "host",
        0x5151ULL
    };
    success &= expect(
        commandRetry.accepted && commandRetry.duplicate
            && hashRetry.accepted && hashRetry.duplicate
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(120, "alpha", 2, {0x52U})
                ),
                "alpha",
                success,
                "pending new command"
            ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::StateHash,
                    newBarrierHash
                ),
                "host",
                success,
                "pending new state hash"
            ).accepted
            && !apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                    RuntimeMultiplayerAdvanceTickMessage{"host", 130}
                ),
                "host",
                success,
                "pending tick advance"
            ).accepted
            && lobby.stateHash() != heldBarrierHash,
        "pending save/load admits in-flight wire messages while freezing ticks"
    );
    const std::uint64_t beforeInvalidAbort = lobby.stateHash();
    const RuntimeMultiplayerMessageApplyResult wrongAbort = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{99, "host"}
        ),
        "host",
        success,
        "wrong save abort"
    );
    const RuntimeMultiplayerMessageApplyResult nonHostAbort = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{2, "alpha"}
        ),
        "alpha",
        success,
        "non-host save abort"
    );
    success &= expect(
        wrongAbort.decoded && !wrongAbort.accepted
            && nonHostAbort.decoded && !nonHostAbort.accepted
            && lobby.stateHash() == beforeInvalidAbort,
        "wrong proposal and non-host abort are atomic"
    );
    const RuntimeMultiplayerMessageApplyResult aborted = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{2, "host"}
        ),
        "host",
        success,
        "host save abort"
    );
    const RuntimeMultiplayerMessageApplyResult abortRetry = apply(
        lobby,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{2, "host"}
        ),
        "host",
        success,
        "host save abort retry"
    );
    success &= expect(
        aborted.accepted && aborted.changed
            && aborted.saveLoadStatus
                == RuntimeMultiplayerSaveLoadStatus::Aborted
            && abortRetry.accepted && abortRetry.duplicate
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(120, "alpha", 2, {0x52U})
                ),
                "alpha",
                success,
                "command after abort"
            ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::StateHash,
                    newBarrierHash
                ),
                "host",
                success,
                "state hash after abort"
            ).accepted
            && apply(
                lobby,
                message(
                    RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                    RuntimeMultiplayerAdvanceTickMessage{"host", 130}
                ),
                "host",
                success,
                "advance after abort"
            ).accepted,
        "host abort is terminal, idempotent, and releases tick advancement"
    );

    RuntimeMultiplayerSessionState forward;
    RuntimeMultiplayerSessionState reverse;
    success &= expect(
        buildRunningSession(forward, success)
            && buildRunningSession(reverse, success),
        "canonical-order sessions start"
    );
    const std::vector<RuntimeMultiplayerCommandPacket> commands{
        command(130, "host", 2, {0x40U}),
        command(120, "alpha", 2, {0x30U}),
        command(120, "host", 1, {0x10U}),
        command(120, "alpha", 1, {0x20U})
    };
    for (const RuntimeMultiplayerCommandPacket& packet : commands) {
        success &= expect(
            apply(
                forward,
                message(RuntimeMultiplayerMessageType::Command, packet),
                packet.peerId,
                success,
                "forward command"
            ).accepted,
            "forward-order command applies"
        );
    }
    for (auto packet = commands.rbegin(); packet != commands.rend(); ++packet) {
        success &= expect(
            apply(
                reverse,
                message(RuntimeMultiplayerMessageType::Command, *packet),
                packet->peerId,
                success,
                "reverse command"
            ).accepted,
            "reverse-order command applies"
        );
    }
    success &= expect(
        forward.commands() == reverse.commands()
            && forward.stateHash() == reverse.stateHash(),
        "out-of-order command delivery converges to a stable session hash"
    );
    return success;
}

bool testProtocolTickWindowsAndCapacity() {
    bool success = true;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        buildRunningSession(session, success),
        "tick-window protocol session starts"
    );

    RuntimeMultiplayerStateHashReport futureHash{110, "alpha", 0x7001ULL};
    const std::uint64_t beforePoison = session.stateHash();
    success &= expect(
        !apply(
            session,
            message(
                RuntimeMultiplayerMessageType::Command,
                command(151, "alpha", 1, {0x01U})
            ),
            "alpha",
            success,
            "future command poison"
        ).accepted
            && !apply(
                session,
                message(RuntimeMultiplayerMessageType::StateHash, futureHash),
                "alpha",
                success,
                "future hash poison"
            ).accepted
            && session.commands().empty()
            && session.stateHashFrames().empty()
            && session.stateHash() == beforePoison,
        "wire ingress outside tick windows cannot consume capacity"
    );

    for (std::uint64_t sequence = 1; sequence <= 8; ++sequence) {
        success &= expect(
            apply(
                session,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(
                        100 + static_cast<int>(sequence),
                        "alpha",
                        sequence,
                        {static_cast<std::uint8_t>(sequence)}
                    )
                ),
                "alpha",
                success,
                "per-peer command fill"
            ).accepted,
            "command within the per-peer quota applies"
        );
    }
    const std::uint64_t beforeQuotaPoison = session.stateHash();
    success &= expect(
        !apply(
            session,
            message(
                RuntimeMultiplayerMessageType::Command,
                command(109, "alpha", 9, {0x09U})
            ),
            "alpha",
            success,
            "per-peer command overflow"
        ).accepted
            && session.stateHash() == beforeQuotaPoison
            && apply(
                session,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(110, "host", 1, {0x10U})
                ),
                "host",
                success,
                "independent host command quota"
            ).accepted,
        "one peer cannot consume another peer's command quota"
    );

    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 110}
            ),
            "host",
            success,
            "advance command window"
        ).accepted
            && session.authoritativeTick() == 110
            && session.commands().size() == 1
            && session.commands().front().peerId == "host",
        "wire tick advance prunes expired commands deterministically"
    );
    const std::uint64_t beforeBackward = session.stateHash();
    success &= expect(
        !apply(
            session,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 109}
            ),
            "host",
            success,
            "backward tick advance"
        ).accepted
            && !apply(
                session,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(110, "alpha", 1, {0x01U})
                ),
                "alpha",
                success,
                "retired sequence replay"
            ).accepted
            && session.stateHash() == beforeBackward,
        "backward advance and retired sequence replay are atomic"
    );

    RuntimeMultiplayerStateHashReport reachedHash{110, "alpha", 0x7002ULL};
    success &= expect(
        apply(
            session,
            message(RuntimeMultiplayerMessageType::StateHash, reachedHash),
            "alpha",
            success,
            "reached hash report"
        ).accepted
            && session.stateHashFrames().size() == 1,
        "reached hash report passes the wire lag gate"
    );
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 151}
            ),
            "host",
            success,
            "advance beyond hash lag"
        ).accepted
            && session.commands().empty()
            && session.stateHashFrames().empty(),
        "wire tick advance prunes expired hash frames"
    );

    RuntimeMultiplayerSessionState reordered;
    success &= expect(
        buildRunningSession(reordered, success)
            && apply(
                reordered,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(100, "alpha", 8, {0x08U})
                ),
                "alpha",
                success,
                "high sequence before retirement"
            ).accepted
            && apply(
                reordered,
                message(
                    RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                    RuntimeMultiplayerAdvanceTickMessage{"host", 105}
                ),
                "host",
                success,
                "retire high sequence"
            ).accepted
            && apply(
                reordered,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(105, "alpha", 1, {0x01U})
                ),
                "alpha",
                success,
                "delayed lower sequence"
            ).accepted
            && !apply(
                reordered,
                message(
                    RuntimeMultiplayerMessageType::Command,
                    command(105, "alpha", 8, {0x88U})
                ),
                "alpha",
                success,
                "retired sequence reuse"
            ).accepted,
        "wire reorder window accepts an unseen delayed lower sequence and "
        "rejects reuse"
    );
    return success;
}

bool expectMalformedAtomic(
    RuntimeMultiplayerSessionState& session,
    const RuntimeNetworkPacket& packet,
    const std::string& label
) {
    RuntimeMultiplayerMessage sentinel = message(
        RuntimeMultiplayerMessageType::Leave,
        RuntimeMultiplayerLeaveMessage{"sentinel"}
    );
    RuntimeMultiplayerMessage decoded = sentinel;
    std::string decodeError;
    const bool decodedPacket = decodeRuntimeMultiplayerMessage(
        packet,
        decoded,
        decodeError
    );
    const std::uint64_t before = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult result =
        applyRuntimeMultiplayerMessage(packet, "alpha", session);
    return expect(
        !decodedPacket && decoded == sentinel
            && !result.decoded && !result.accepted
            && session.stateHash() == before,
        label + " is rejected without changing decode output or session state"
    );
}

bool testMalformedPacketsAreAtomic() {
    bool success = true;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        buildRunningSession(session, success),
        "malformed-packet session starts"
    );

    const RuntimeMultiplayerMessage validReady = message(
        RuntimeMultiplayerMessageType::Ready,
        RuntimeMultiplayerReadyMessage{"alpha", true}
    );
    const RuntimeNetworkPacket readyPacket = encode(
        validReady,
        success,
        "malformed ready seed"
    );

    RuntimeNetworkPacket trailing = readyPacket;
    trailing.payload.push_back(0xffU);
    success &= expectMalformedAtomic(session, trailing, "trailing bytes");

    RuntimeNetworkPacket truncated = readyPacket;
    truncated.payload.pop_back();
    success &= expectMalformedAtomic(session, truncated, "truncated packet");

    RuntimeNetworkPacket invalidBoolean = readyPacket;
    invalidBoolean.payload.back() = 2U;
    success &= expectMalformedAtomic(
        session,
        invalidBoolean,
        "unknown boolean encoding"
    );

    RuntimeNetworkPacket unknownType = readyPacket;
    unknownType.type = 0x04ffU;
    success &= expectMalformedAtomic(session, unknownType, "unknown type");

    RuntimeNetworkPacket unknownVersion = readyPacket;
    unknownVersion.type = 0x0203U;
    success &= expectMalformedAtomic(
        session,
        unknownVersion,
        "unknown message version"
    );

    RuntimeNetworkPacket oversizedPeer;
    oversizedPeer.type = static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::Leave
    );
    oversizedPeer.payload = {
        0x00U,
        0x00U,
        0x00U,
        static_cast<std::uint8_t>(
            runtimeMultiplayerMaximumPeerIdBytes + 1U
        )
    };
    success &= expectMalformedAtomic(
        session,
        oversizedPeer,
        "oversized string prefix"
    );

    RuntimeMultiplayerSaveLoadProposal proposal = saveLoadProposal(
        1,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        119,
        119,
        "slot-1",
        0x1234ULL,
        0x5678ULL,
        0x9abcULL
    );
    RuntimeNetworkPacket unknownKind = encode(
        message(
            RuntimeMultiplayerMessageType::SaveLoadProposal,
            proposal
        ),
        success,
        "unknown-kind seed"
    );
    unknownKind.payload[16] = 0xffU;
    success &= expectMalformedAtomic(
        session,
        unknownKind,
        "unknown save/load enum"
    );

    RuntimeNetworkPacket zeroSnapshotFingerprint = encode(
        message(
            RuntimeMultiplayerMessageType::SaveLoadProposal,
            proposal
        ),
        success,
        "zero snapshot fingerprint seed"
    );
    std::fill(
        zeroSnapshotFingerprint.payload.end()
            - static_cast<std::ptrdiff_t>(
                runtimeMultiplayerSnapshotSha256Bytes
            ),
        zeroSnapshotFingerprint.payload.end(),
        static_cast<std::uint8_t>(0)
    );
    success &= expectMalformedAtomic(
        session,
        zeroSnapshotFingerprint,
        "zero snapshot SHA-256"
    );

    const RuntimeMultiplayerJoinRequest join = joinRequest("a", "A");
    RuntimeNetworkPacket zeroProtocolFingerprint = encode(
        message(RuntimeMultiplayerMessageType::JoinRequest, join),
        success,
        "zero protocol fingerprint seed"
    );
    const std::size_t protocolFingerprintOffset =
        4U + join.peerId.size()
        + 4U + join.displayName.size()
        + 4U;
    std::fill(
        zeroProtocolFingerprint.payload.begin()
            + static_cast<std::ptrdiff_t>(protocolFingerprintOffset),
        zeroProtocolFingerprint.payload.begin()
            + static_cast<std::ptrdiff_t>(
                protocolFingerprintOffset + 8U
            ),
        static_cast<std::uint8_t>(0)
    );
    success &= expectMalformedAtomic(
        session,
        zeroProtocolFingerprint,
        "zero protocol fingerprint"
    );

    RuntimeNetworkPacket oversizedCommand = encode(
        message(
            RuntimeMultiplayerMessageType::Command,
            command(120, "alpha", 7, {0x01U})
        ),
        success,
        "oversized command seed"
    );
    const std::size_t payloadLengthOffset =
        4U + 4U + std::string("alpha").size() + 8U + 8U;
    oversizedCommand.payload[payloadLengthOffset] = 0x00U;
    oversizedCommand.payload[payloadLengthOffset + 1U] = 0x01U;
    oversizedCommand.payload[payloadLengthOffset + 2U] = 0x00U;
    oversizedCommand.payload[payloadLengthOffset + 3U] = 0x01U;
    success &= expectMalformedAtomic(
        session,
        oversizedCommand,
        "oversized command payload prefix"
    );

    RuntimeMultiplayerSessionConfig insufficientHashCapacity =
        standardConfig();
    insufficientHashCapacity.maximumStateHashFrames = 4;
    RuntimeNetworkPacket unchangedConfig;
    unchangedConfig.type = 0x7777U;
    unchangedConfig.payload = {0x44U};
    std::string configError;
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(
                RuntimeMultiplayerMessageType::RoomConfig,
                insufficientHashCapacity
            ),
            unchangedConfig,
            configError
        )
            && unchangedConfig.type == 0x7777U
            && unchangedConfig.payload
                == std::vector<std::uint8_t>{0x44U},
        "RoomConfig rejects a hash capacity smaller than its lag window"
    );

    RuntimeNetworkPacket unchanged;
    unchanged.type = 0x7777U;
    unchanged.payload = {0x55U};
    std::string error;
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(
                RuntimeMultiplayerMessageType::Leave,
                RuntimeMultiplayerReadyMessage{"alpha", true}
            ),
            unchanged,
            error
        )
            && unchanged.type == 0x7777U
            && unchanged.payload == std::vector<std::uint8_t>{0x55U},
        "type/body mismatch is rejected without changing output packet"
    );

    RuntimeMultiplayerSnapshotChunk snapshotChunk;
    snapshotChunk.proposalId = 7;
    snapshotChunk.sourceTimelineEpoch = session.timelineEpoch();
    snapshotChunk.chunkIndex = 0;
    snapshotChunk.bytes = {0x01U, 0x02U, 0x03U};
    const RuntimeNetworkPacket snapshotChunkPacket = encode(
        message(
            RuntimeMultiplayerMessageType::SnapshotChunk,
            snapshotChunk
        ),
        success,
        "snapshot chunk seed"
    );
    RuntimeNetworkPacket truncatedSnapshotChunk = snapshotChunkPacket;
    truncatedSnapshotChunk.payload.pop_back();
    success &= expectMalformedAtomic(
        session,
        truncatedSnapshotChunk,
        "truncated nested snapshot chunk"
    );
    RuntimeNetworkPacket trailingSnapshotChunk = snapshotChunkPacket;
    trailingSnapshotChunk.payload.push_back(0xffU);
    success &= expectMalformedAtomic(
        session,
        trailingSnapshotChunk,
        "trailing snapshot chunk message"
    );
    const std::uint64_t beforeCoordinatorOnly = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult directChunk =
        applyRuntimeMultiplayerMessage(
            snapshotChunkPacket,
            "host",
            session
        );
    const RuntimeMultiplayerMessageApplyResult trustedChunk =
        applyTrustedCanonicalRuntimeMultiplayerMessage(
            snapshotChunkPacket,
            session
        );
    success &= expect(
        directChunk.decoded && !directChunk.accepted
            && trustedChunk.decoded && !trustedChunk.accepted
            && directChunk.reason.find("coordinator") != std::string::npos
            && trustedChunk.reason.find("session state transition")
                != std::string::npos
            && session.stateHash() == beforeCoordinatorOnly,
        "snapshot chunks are coordinator-only and atomically rejected by Session"
    );

    RuntimeMultiplayerSnapshotChunkAck snapshotAck;
    snapshotAck.proposalId = 7;
    snapshotAck.sourceTimelineEpoch = session.timelineEpoch();
    snapshotAck.chunkIndex = 0;
    const RuntimeNetworkPacket snapshotAckPacket = encode(
        message(
            RuntimeMultiplayerMessageType::SnapshotChunkAck,
            snapshotAck
        ),
        success,
        "snapshot ack seed"
    );
    const RuntimeMultiplayerMessageApplyResult directAck =
        applyRuntimeMultiplayerMessage(snapshotAckPacket, "alpha", session);
    const RuntimeMultiplayerMessageApplyResult trustedAck =
        applyTrustedCanonicalRuntimeMultiplayerMessage(
            snapshotAckPacket,
            session
        );
    success &= expect(
        directAck.decoded && !directAck.accepted
            && trustedAck.decoded && !trustedAck.accepted
            && session.stateHash() == beforeCoordinatorOnly,
        "snapshot acknowledgements are coordinator-only and atomic"
    );
    RuntimeMultiplayerSnapshotChunk oversizedSnapshotChunk = snapshotChunk;
    oversizedSnapshotChunk.bytes.assign(
        runtimeMultiplayerSnapshotChunkBytes + 1U,
        0x5aU
    );
    RuntimeNetworkPacket unchangedChunkPacket;
    unchangedChunkPacket.type = 0x7777U;
    unchangedChunkPacket.payload = {0x66U};
    error.clear();
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(
                RuntimeMultiplayerMessageType::SnapshotChunk,
                std::move(oversizedSnapshotChunk)
            ),
            unchangedChunkPacket,
            error
        )
            && unchangedChunkPacket.type == 0x7777U
            && unchangedChunkPacket.payload
                == std::vector<std::uint8_t>{0x66U},
        "oversized snapshot chunk is rejected without changing output"
    );
    return success;
}

bool expectSpoofRejected(
    RuntimeMultiplayerSessionState& session,
    const RuntimeMultiplayerMessage& source,
    std::string_view authenticatedPeerId,
    bool& success,
    const std::string& label
) {
    const std::uint64_t before = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult result = apply(
        session,
        source,
        authenticatedPeerId,
        success,
        label
    );
    return expect(
        result.decoded && !result.accepted
            && session.stateHash() == before,
        label + " cannot spoof another session identity"
    );
}

bool testAuthenticatedSourceBinding() {
    bool success = true;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        applyTrusted(
            session,
            message(
                RuntimeMultiplayerMessageType::RoomConfig,
                standardConfig()
            ),
            success,
            "source-binding config"
        ).accepted
            && apply(
                session,
                message(
                    RuntimeMultiplayerMessageType::JoinRequest,
                    joinRequest()
                ),
                {},
                success,
                "source-binding join"
        ).accepted,
        "source-binding lobby starts"
    );
    (void)installMatchSetup(
        session,
        1U,
        success,
        "source-binding match setup"
    );

    const RuntimeMultiplayerMessage duplicateJoin = message(
        RuntimeMultiplayerMessageType::JoinRequest,
        joinRequest()
    );
    const std::uint64_t beforeJoinReplay = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult unboundReplay = apply(
        session,
        duplicateJoin,
        {},
        success,
        "unbound duplicate join"
    );
    const RuntimeMultiplayerMessageApplyResult boundRetry = apply(
        session,
        duplicateJoin,
        "alpha",
        success,
        "bound duplicate join"
    );
    success &= expect(
        unboundReplay.decoded && !unboundReplay.accepted
            && boundRetry.accepted && boundRetry.duplicate
            && session.stateHash() == beforeJoinReplay,
        "only the existing bound connection may retry a duplicate JoinRequest"
    );

    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::JoinRequest,
            joinRequest()
        ),
        "intruder",
        success,
        "pre-bound JoinRequest"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::Leave,
            RuntimeMultiplayerLeaveMessage{"alpha"}
        ),
        "host",
        success,
        "Leave"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::Ready,
            RuntimeMultiplayerReadyMessage{"alpha", true}
        ),
        "host",
        success,
        "Ready"
    );
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::Ready,
                RuntimeMultiplayerReadyMessage{"host", true}
            ),
            "host",
            success,
            "source-binding host ready"
        ).accepted
            && apply(
                session,
                message(
                    RuntimeMultiplayerMessageType::Ready,
                    RuntimeMultiplayerReadyMessage{"alpha", true}
                ),
                "alpha",
                success,
                "source-binding alpha ready"
            ).accepted,
        "correctly bound peers can become ready"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::Start,
            RuntimeMultiplayerStartMessage{"alpha", 100}
        ),
        "alpha",
        success,
        "non-host self Start"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::Start,
            RuntimeMultiplayerStartMessage{"host", 100}
        ),
        "alpha",
        success,
        "Start"
    );
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::Start,
                RuntimeMultiplayerStartMessage{"host", 100}
            ),
            "host",
            success,
            "source-binding authorized start"
        ).accepted,
        "correctly bound host can start after spoof rejection"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
            RuntimeMultiplayerAdvanceTickMessage{"host", 120}
        ),
        "alpha",
        success,
        "AdvanceAuthoritativeTick"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
            RuntimeMultiplayerAdvanceTickMessage{"alpha", 120}
        ),
        "alpha",
        success,
        "non-host self AdvanceAuthoritativeTick"
    );
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick,
                RuntimeMultiplayerAdvanceTickMessage{"host", 120}
            ),
            "host",
            success,
            "source-binding authoritative advance"
        ).accepted,
        "correctly bound host can advance authoritative tick"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::Command,
            command(120, "alpha", 1, {0x01U})
        ),
        "host",
        success,
        "Command"
    );
    RuntimeMultiplayerStateHashReport report{110, "alpha", 0x1001ULL};
    success &= expectSpoofRejected(
        session,
        message(RuntimeMultiplayerMessageType::StateHash, report),
        "host",
        success,
        "StateHash"
    );

    RuntimeMultiplayerSaveLoadProposal proposal = saveLoadProposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Load,
        110,
        "slot-1",
        0x1002ULL
    );
    proposal.proposerPeerId = "alpha";
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadProposal,
            proposal
        ),
        "alpha",
        success,
        "non-host SaveLoadProposal"
    );
    proposal.proposerPeerId = "host";
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::SaveLoadProposal,
                proposal
            ),
            "host",
            success,
            "source-binding host proposal"
        ).accepted,
        "correctly bound host can create save/load proposal"
    );
    RuntimeMultiplayerSaveLoadResponseMessage response{
        1,
        "alpha",
        true,
        0x1002ULL,
        1,
        proposal.descriptor.identity.authoritativeStateHash
    };
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadResponse,
            response
        ),
        "host",
        success,
        "SaveLoadResponse"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadCommit,
            RuntimeMultiplayerSaveLoadCommitMessage{1, 1, "host"}
        ),
        "alpha",
        success,
        "SaveLoadCommit"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadApplied,
            RuntimeMultiplayerSaveLoadAppliedMessage{
                1, 1, "alpha", true,
                proposal.descriptor.identity.authoritativeStateHash
            }
        ),
        "host",
        success,
        "SaveLoadApplied"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{1, "host"}
        ),
        "alpha",
        success,
        "SaveLoadAbort"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::SaveLoadAbort,
            RuntimeMultiplayerSaveLoadAbortMessage{1, "alpha"}
        ),
        "alpha",
        success,
        "non-host self SaveLoadAbort"
    );
    success &= expect(
        apply(
            session,
            message(
                RuntimeMultiplayerMessageType::SaveLoadAbort,
                RuntimeMultiplayerSaveLoadAbortMessage{1, "host"}
            ),
            "host",
            success,
            "source-binding save abort"
        ).accepted,
        "correctly bound host can abort a pending save/load handshake"
    );
    success &= expectSpoofRejected(
        session,
        message(
            RuntimeMultiplayerMessageType::RoomConfig,
            standardConfig()
        ),
        "host",
        success,
        "direct RoomConfig"
    );
    return success;
}

bool testSpectatorWireAuthority() {
    bool success = true;
    RuntimeMultiplayerJoinRequest spectator =
        joinRequest("observer", "Observer");
    spectator.participation =
        RuntimeMultiplayerParticipationRole::Spectator;

    const RuntimeMultiplayerMessage spectatorMessage = message(
        RuntimeMultiplayerMessageType::JoinRequest,
        spectator
    );
    const RuntimeNetworkPacket spectatorPacket = encode(
        spectatorMessage,
        success,
        "spectator join"
    );
    RuntimeMultiplayerMessage decodedSpectator;
    std::string decodeError;
    success &= expect(
        decodeRuntimeMultiplayerMessage(
            spectatorPacket,
            decodedSpectator,
            decodeError
        )
            && decodedSpectator == spectatorMessage,
        "spectator role round-trips in a join request: " + decodeError
    );

    RuntimeMultiplayerJoinAdmissionRequestMessage admission;
    admission.roomGeneration = 0x1111U;
    admission.admissionToken = 0x2222U;
    admission.request = spectator;
    const RuntimeMultiplayerMessage admissionMessage = message(
        RuntimeMultiplayerMessageType::JoinAdmissionRequest,
        admission
    );
    const RuntimeNetworkPacket admissionPacket = encode(
        admissionMessage,
        success,
        "spectator admission"
    );
    RuntimeMultiplayerMessage decodedAdmission;
    decodeError.clear();
    success &= expect(
        decodeRuntimeMultiplayerMessage(
            admissionPacket,
            decodedAdmission,
            decodeError
        )
            && decodedAdmission == admissionMessage,
        "admission proof preserves spectator participation: " + decodeError
    );

    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(standardConfig()).accepted
            && session.joinMember(joinRequest("fighter", "Fighter")).accepted
            && session.joinMember(spectator).accepted,
        "spectator protocol fixture builds a lobby"
    );
    (void)installMatchSetup(
        session,
        1U,
        success,
        "spectator protocol match setup"
    );
    RuntimeMultiplayerLobbySnapshotMessage snapshot;
    snapshot.roomGeneration = 0x1111U;
    snapshot.snapshot.config = session.config();
    snapshot.snapshot.members = session.members();
    snapshot.snapshot.latestMatchSetupRevision =
        session.latestMatchSetupRevision();
    snapshot.snapshot.matchSetup = session.matchSetup();
    const RuntimeMultiplayerMessage snapshotMessage = message(
        RuntimeMultiplayerMessageType::LobbySnapshot,
        snapshot
    );
    const RuntimeNetworkPacket snapshotPacket = encode(
        snapshotMessage,
        success,
        "spectator lobby snapshot"
    );
    RuntimeMultiplayerMessage decodedSnapshot;
    decodeError.clear();
    success &= expect(
        decodeRuntimeMultiplayerMessage(
            snapshotPacket,
            decodedSnapshot,
            decodeError
        )
            && decodedSnapshot == snapshotMessage,
        "canonical lobby snapshot preserves spectator role and setup: "
            + decodeError
    );

    RuntimeMultiplayerSessionState replica;
    const RuntimeMultiplayerMessageApplyResult copied = applyTrusted(
        replica,
        snapshotMessage,
        success,
        "setup-bearing lobby snapshot"
    );
    success &= expect(
        copied.decoded && copied.accepted && copied.changed
            && replica.matchSetup() == session.matchSetup()
            && replica.latestMatchSetupRevision()
                == session.latestMatchSetupRevision()
            && replica.members() == session.members()
            && replica.stateHash() == session.stateHash(),
        "trusted client copies complete setup and identical lobby hash"
    );

    RuntimeMultiplayerLobbySnapshotMessage inconsistentRevision = snapshot;
    ++inconsistentRevision.snapshot.latestMatchSetupRevision;
    RuntimeNetworkPacket unchangedPacket;
    unchangedPacket.type = 0x7777U;
    unchangedPacket.payload = {0x55U};
    std::string snapshotError;
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(
                RuntimeMultiplayerMessageType::LobbySnapshot,
                inconsistentRevision
            ),
            unchangedPacket,
            snapshotError
        )
            && unchangedPacket.type == 0x7777U
            && unchangedPacket.payload
                == std::vector<std::uint8_t>{0x55U},
        "snapshot setup revision mismatch is rejected transactionally"
    );

    RuntimeMultiplayerLobbySnapshotMessage rosterMismatch = snapshot;
    for (RuntimeMultiplayerRoomMember& roomMember :
            rosterMismatch.snapshot.members) {
        if (roomMember.peerId == "fighter") {
            roomMember.displayName = "Changed Fighter";
        }
    }
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(
                RuntimeMultiplayerMessageType::LobbySnapshot,
                rosterMismatch
            ),
            unchangedPacket,
            snapshotError
        ),
        "snapshot setup/member identity mismatch is rejected"
    );

    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("fighter", true).accepted
            && session.setMemberReady("observer", true).accepted
            && session.start("host", 0).accepted,
        "spectator wire fixture starts after every member is ready"
    );
    const std::uint64_t beforeCommand = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult spectatorCommand = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::Command,
            command(0, "observer", 1, {0x77U})
        ),
        "observer",
        success,
        "spectator gameplay command"
    );
    success &= expect(
        spectatorCommand.decoded
            && !spectatorCommand.accepted
            && session.commands().empty()
            && session.stateHash() == beforeCommand,
        "authenticated spectator gameplay ingress is rejected atomically"
    );

    RuntimeMultiplayerSessionState canonicalReplica = session;
    const RuntimeMultiplayerMessage leaveMessage = message(
        RuntimeMultiplayerMessageType::Leave,
        RuntimeMultiplayerLeaveMessage{"observer"}
    );
    const RuntimeNetworkPacket leavePacket = encode(
        leaveMessage,
        success,
        "running spectator leave"
    );
    const std::uint64_t beforeForgedLeave = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult forgedLeave =
        applyRuntimeMultiplayerMessage(
            leavePacket,
            "fighter",
            session
        );
    success &= expect(
        forgedLeave.decoded && !forgedLeave.accepted
            && session.stateHash() == beforeForgedLeave,
        "another connection cannot forge a running spectator leave"
    );
    const RuntimeMultiplayerMessageApplyResult authenticatedLeave =
        applyRuntimeMultiplayerMessage(
            leavePacket,
            "observer",
            session
        );
    const RuntimeMultiplayerMessageApplyResult canonicalLeave =
        applyTrustedCanonicalRuntimeMultiplayerMessage(
            leavePacket,
            canonicalReplica
        );
    const RuntimeMultiplayerMessageApplyResult duplicateCanonicalLeave =
        applyTrustedCanonicalRuntimeMultiplayerMessage(
            leavePacket,
            canonicalReplica
        );
    success &= expect(
        authenticatedLeave.decoded && authenticatedLeave.accepted
            && authenticatedLeave.changed
            && canonicalLeave.decoded && canonicalLeave.accepted
            && canonicalLeave.changed
            && duplicateCanonicalLeave.accepted
            && duplicateCanonicalLeave.duplicate
            && session.status() == RuntimeMultiplayerRoomStatus::Running
            && session.members().size() == 2U
            && canonicalReplica.stateHash() == session.stateHash(),
        "bound running spectator leave applies canonically and duplicate host "
        "echo is idempotent"
    );
    const std::uint64_t afterCanonicalLeave = session.stateHash();
    const RuntimeMultiplayerMessageApplyResult replayedHash = apply(
        session,
        message(
            RuntimeMultiplayerMessageType::StateHash,
            RuntimeMultiplayerStateHashReport{
                0,
                "observer",
                0xabcU
            }
        ),
        "observer",
        success,
        "removed spectator state-hash replay"
    );
    success &= expect(
        replayedHash.decoded && !replayedHash.accepted
            && session.stateHash() == afterCanonicalLeave,
        "removed spectator cannot replay state-hash evidence"
    );

    RuntimeMultiplayerJoinRequest invalid = spectator;
    invalid.participation =
        static_cast<RuntimeMultiplayerParticipationRole>(0xffU);
    RuntimeNetworkPacket ignored;
    std::string encodeError;
    success &= expect(
        !encodeRuntimeMultiplayerMessage(
            message(RuntimeMultiplayerMessageType::JoinRequest, invalid),
            ignored,
            encodeError
        ),
        "unknown participation role cannot be encoded"
    );

    RuntimeNetworkPacket malformed = spectatorPacket;
    const std::size_t roleOffset = 4U + spectator.peerId.size()
        + 4U + spectator.displayName.size();
    success &= expect(
        roleOffset < malformed.payload.size(),
        "spectator role byte exists in the bounded join payload"
    );
    if (roleOffset < malformed.payload.size()) {
        malformed.payload[roleOffset] = 0xffU;
        RuntimeMultiplayerMessage unchanged = message(
            RuntimeMultiplayerMessageType::Leave,
            RuntimeMultiplayerLeaveMessage{"sentinel"}
        );
        const RuntimeMultiplayerMessage sentinel = unchanged;
        decodeError.clear();
        success &= expect(
            !decodeRuntimeMultiplayerMessage(
                malformed,
                unchanged,
                decodeError
            )
                && unchanged == sentinel,
            "unknown wire participation is rejected without changing output"
        );
    }
    return success;
}

#include "RuntimeMultiplayerMapProtocolSmoke.inl"

} // namespace

int main() {
    bool success = true;
    success &= testRoundTripsAndByteOrder();
    success &= testRoomConfigBaseline();
    success &= testApplicationAndCanonicalOrdering();
    success &= testProtocolTickWindowsAndCapacity();
    success &= testMalformedPacketsAreAtomic();
    success &= testAuthenticatedSourceBinding();
    success &= testSpectatorWireAuthority();
    success &= testMapTransferProtocol();
    if (!success) return 1;
    std::cout
        << "Runtime multiplayer protocol smoke: codec=versioned"
        << " sourceBinding=strict ticks=windowed"
        << " saveLoad=abortable limits=strict\n";
    return 0;
}
