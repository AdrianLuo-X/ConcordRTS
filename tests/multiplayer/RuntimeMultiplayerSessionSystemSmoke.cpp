#include "network/session/RuntimeMultiplayerSessionSystem.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "runtime multiplayer session smoke failure: "
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
    config.maximumCommandPayloadBytes = 16;
    config.maximumBufferedCommands = 16;
    config.maximumBufferedCommandsPerPeer = 4;
    config.maximumStateHashFrames = 8;
    config.maximumSaveLoadHandshakes = 4;
    config.commandLeadWindowTicks = 50;
    config.stateHashLagWindowTicks = 40;
    config.stateHashPeriodTicks = 10;
    return config;
}

RuntimeMultiplayerJoinRequest joinRequest(
    std::string peerId,
    std::string displayName
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
    setup.map.canonicalId = "maps/official/session-smoke.map";
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
    const std::string& label
) {
    const RuntimeMultiplayerOperationResult result = session.setMatchSetup(
        session.config().hostPeerId,
        matchSetupFor(session, revision)
    );
    return expect(
        result.accepted && result.changed
            && session.matchSetup().has_value()
            && session.latestMatchSetupRevision() == revision,
        label + ": " + result.reason
    );
}

bool testConfigurationCompatibilityAndLobby() {
    bool success = true;
    RuntimeMultiplayerSessionState session;

    RuntimeMultiplayerSessionConfig invalid = standardConfig();
    invalid.compatibility.protocolFingerprint = 0;
    const std::uint64_t inactiveHash = session.stateHash();
    success &= expect(
        !session.beginHost(invalid).accepted
            && !session.active()
            && session.stateHash() == inactiveHash,
        "invalid host fingerprint is rejected atomically"
    );

    RuntimeMultiplayerSessionConfig insufficientHashCapacity =
        standardConfig();
    insufficientHashCapacity.maximumStateHashFrames = 4;
    RuntimeMultiplayerSessionConfig insufficientCommandCapacity =
        standardConfig();
    insufficientCommandCapacity.maximumBufferedCommands = 15;
    success &= expect(
        !session.beginHost(insufficientHashCapacity).accepted
            && !session.beginHost(insufficientCommandCapacity).accepted
            && !session.active()
            && session.stateHash() == inactiveHash,
        "configured buffers must reserve every peer and lag-window slot"
    );

    const RuntimeMultiplayerOperationResult begun =
        session.beginHost(standardConfig());
    success &= expect(
        begun.accepted && begun.changed
            && session.status() == RuntimeMultiplayerRoomStatus::Lobby
            && session.members().size() == 1
            && session.members().front().role
                == RuntimeMultiplayerRoomMemberRole::Host
            && !session.members().front().ready,
        "host creates a one-member unready lobby"
    );

    RuntimeMultiplayerCompatibility wrongProtocol = compatibility();
    wrongProtocol.protocolVersion += 1;
    RuntimeMultiplayerCompatibility wrongProtocolFingerprint = compatibility();
    wrongProtocolFingerprint.protocolFingerprint ^= 1U;
    RuntimeMultiplayerCompatibility wrongContent = compatibility();
    wrongContent.contentFingerprint ^= 1U;
    success &= expect(
        !session.validateCompatibility(wrongProtocol).compatible
            && !session.validateCompatibility(wrongProtocolFingerprint).compatible
            && !session.validateCompatibility(wrongContent).compatible
            && session.validateCompatibility(compatibility()).compatible,
        "protocol version, protocol fingerprint, and content fingerprint match"
    );

    RuntimeMultiplayerJoinRequest incompatible =
        joinRequest("bad-content", "Bad Content");
    incompatible.compatibility.contentFingerprint ^= 1U;
    const std::uint64_t beforeBadJoin = session.stateHash();
    success &= expect(
        !session.joinMember(incompatible).accepted
            && session.members().size() == 1
            && session.stateHash() == beforeBadJoin,
        "incompatible client join is rejected atomically"
    );

    RuntimeMultiplayerJoinRequest oversized = joinRequest(
        std::string(runtimeMultiplayerMaximumPeerIdBytes + 1U, 'p'),
        "Oversized"
    );
    success &= expect(
        !session.joinMember(oversized).accepted,
        "oversized peer identity is rejected"
    );

    const RuntimeMultiplayerJoinRequest alpha =
        joinRequest("alpha", "Alpha Player");
    const RuntimeMultiplayerJoinRequest beta =
        joinRequest("beta", "Beta Player");
    success &= expect(
        session.joinMember(beta).accepted
            && session.joinMember(alpha).accepted
            && session.members().size() == 3
            && session.members()[0].peerId == "host"
            && session.members()[1].peerId == "alpha"
            && session.members()[2].peerId == "beta",
        "clients join and canonical member order is host then peer id"
    );
    const RuntimeMultiplayerOperationResult duplicate =
        session.joinMember(alpha);
    RuntimeMultiplayerJoinRequest conflict = alpha;
    conflict.displayName = "Different Alpha";
    success &= expect(
        duplicate.accepted && duplicate.duplicate && !duplicate.changed
            && !session.joinMember(conflict).accepted,
        "idempotent join retry is accepted and conflicting peer reuse is rejected"
    );

    success &= expect(
        !session.leaveMember("host").accepted
            && session.leaveMember("beta").accepted
            && session.members().size() == 2
            && session.joinMember(beta).accepted,
        "clients may leave and rejoin while the host remains authoritative"
    );

    success &= expect(
        !session.start("host", 100).accepted,
        "a room without a formal match setup cannot start"
    );
    success &= installMatchSetup(session, 1U, "initial lobby setup installs");
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && !session.start("host", 100).accepted
            && session.setMemberReady("beta", true).accepted
            && session.allMembersReady()
            && !session.start("alpha", 100).accepted,
        "start requires every member ready and host authority"
    );
    const RuntimeMultiplayerOperationResult started = session.start("host", 100);
    const RuntimeMultiplayerOperationResult duplicateStart =
        session.start("host", 100);
    success &= expect(
        started.accepted && started.changed
            && duplicateStart.accepted && duplicateStart.duplicate
            && session.status() == RuntimeMultiplayerRoomStatus::Running
            && session.startTick() == 100
            && session.authoritativeTick() == 100
            && !session.joinMember(joinRequest("late", "Late Player")).accepted
            && !session.leaveMember("alpha").accepted
            && !session.setMemberReady("alpha", false).accepted,
        "running room is immutable and duplicate start is idempotent"
    );
    return success;
}

RuntimeMultiplayerSessionState buildRunningSession(
    bool reverseOrder,
    bool& success
) {
    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(standardConfig()).accepted,
        "deterministic session host begins"
    );
    const RuntimeMultiplayerJoinRequest alpha =
        joinRequest("alpha", "Alpha Player");
    const RuntimeMultiplayerJoinRequest beta =
        joinRequest("beta", "Beta Player");
    if (reverseOrder) {
        success &= expect(
            session.joinMember(beta).accepted
                && session.joinMember(alpha).accepted,
            "reverse-order clients join"
        );
        success &= installMatchSetup(
            session,
            1U,
            "reverse-order match setup installs"
        );
        success &= expect(
            session.setMemberReady("beta", true).accepted
                && session.setMemberReady("host", true).accepted
                && session.setMemberReady("alpha", true).accepted,
            "reverse-order clients become ready"
        );
    } else {
        success &= expect(
            session.joinMember(alpha).accepted
                && session.joinMember(beta).accepted,
            "forward-order clients join"
        );
        success &= installMatchSetup(
            session,
            1U,
            "forward-order match setup installs"
        );
        success &= expect(
            session.setMemberReady("alpha", true).accepted
                && session.setMemberReady("beta", true).accepted
                && session.setMemberReady("host", true).accepted,
            "forward-order clients become ready"
        );
    }
    success &= expect(
        session.start("host", 100).accepted,
        "deterministic session starts"
    );
    return session;
}

RuntimeMultiplayerCommandPacket command(
    int tick,
    std::string peerId,
    std::uint64_t sequence,
    std::initializer_list<std::uint8_t> payload
) {
    RuntimeMultiplayerCommandPacket packet;
    packet.tick = tick;
    packet.peerId = std::move(peerId);
    packet.sequence = sequence;
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

bool testDeterministicCommands(
    RuntimeMultiplayerSessionState& forward,
    RuntimeMultiplayerSessionState& reverse
) {
    bool success = true;
    const std::vector<RuntimeMultiplayerCommandPacket> packets{
        command(120, "beta", 2, {0x20U, 0x21U}),
        command(110, "host", 1, {0x10U}),
        command(120, "alpha", 4, {0x40U}),
        command(120, "alpha", 3, {0x30U})
    };
    for (const RuntimeMultiplayerCommandPacket& packet : packets) {
        success &= expect(
            forward.submitCommand(packet).accepted,
            "forward command accepted"
        );
    }
    for (auto packet = packets.rbegin(); packet != packets.rend(); ++packet) {
        success &= expect(
            reverse.submitCommand(*packet).accepted,
            "reverse command accepted"
        );
    }

    const RuntimeMultiplayerOperationResult duplicate =
        forward.submitCommand(packets[2]);
    RuntimeMultiplayerCommandPacket conflict = packets[3];
    conflict.payload = {0xffU};
    const std::uint64_t beforeRejected = forward.stateHash();
    RuntimeMultiplayerCommandPacket oversized =
        command(130, "host", 7, {0x01U});
    oversized.payload.resize(17U, 0x01U);
    success &= expect(
        duplicate.accepted && duplicate.duplicate && !duplicate.changed
            && !forward.submitCommand(conflict).accepted
            && !forward.submitCommand(command(99, "host", 8, {0x01U})).accepted
            && !forward.submitCommand(command(130, "ghost", 1, {0x01U})).accepted
            && !forward.submitCommand(command(130, "host", 0, {0x01U})).accepted
            && !forward.submitCommand(command(130, "host", 8, {})).accepted
            && !forward.submitCommand(oversized).accepted
            && forward.stateHash() == beforeRejected,
        "command duplicates and invalid identity/tick/sequence/payload are atomic"
    );

    const std::vector<RuntimeMultiplayerCommandPacket>& ordered =
        forward.commands();
    const std::vector<RuntimeMultiplayerCommandPacket> tick120 =
        forward.commandsAtTick(120);
    success &= expect(
        ordered.size() == 4
            && ordered[0].tick == 110 && ordered[0].peerId == "host"
            && ordered[1].peerId == "alpha" && ordered[1].sequence == 3
            && ordered[2].peerId == "alpha" && ordered[2].sequence == 4
            && ordered[3].peerId == "beta" && ordered[3].sequence == 2
            && tick120.size() == 3
            && forward.commands() == reverse.commands()
            && forward.stateHash() == reverse.stateHash(),
        "commands have stable tick/peer/sequence order independent of arrival"
    );
    return success;
}

RuntimeMultiplayerSaveLoadProposal proposal(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    RuntimeMultiplayerSaveLoadKind kind,
    int snapshotTick,
    std::string snapshotId,
    std::uint64_t fingerprint
) {
    RuntimeMultiplayerSaveLoadProposal result;
    result.timelineEpoch = session.timelineEpoch();
    result.proposalId = proposalId;
    result.kind = kind;
    result.proposerPeerId = "host";
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = proposalId;
    identity.kind = kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = snapshotTick;
    identity.snapshotToken = std::move(snapshotId);
    identity.authoritativeStateHash = 0x6a09e667f3bcc909ULL ^ proposalId;
    std::string setupError;
    if (session.matchSetup().has_value()) {
        runtimeMultiplayerGameplaySetupFingerprint(
            *session.matchSetup(),
            identity.gameplaySetupFingerprint,
            setupError
        );
    }
    result.descriptor.identity = identity;
    result.descriptor.byteCount = 1;
    result.descriptor.chunkCount = 1;
    result.descriptor.snapshotFingerprint = fingerprint;
    result.descriptor.sha256[0] = 1;
    result.tick = snapshotTick;
    result.snapshotId = result.descriptor.identity.snapshotToken;
    result.snapshotFingerprint = fingerprint;
    return result;
}

bool submitResponses(
    RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    std::uint64_t fingerprint,
    bool reverseOrder,
    bool rejectBeta,
    bool mismatchAlpha
) {
    const std::vector<std::string> peers = reverseOrder
        ? std::vector<std::string>{"beta", "alpha", "host"}
        : std::vector<std::string>{"host", "alpha", "beta"};
    bool success = true;
    for (const std::string& peer : peers) {
        const bool approve = !(rejectBeta && peer == "beta");
        const std::uint64_t observed = mismatchAlpha && peer == "alpha"
            ? fingerprint ^ 0x55U
            : fingerprint;
        const RuntimeMultiplayerSaveLoadResult response =
            session.respondToSaveLoad(
                proposalId,
                peer,
                approve,
                observed
            );
        success &= expect(response.accepted, "save/load response accepted");
    }
    return success;
}

bool submitAppliedAcks(
    RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    bool reverseOrder
) {
    const RuntimeMultiplayerSaveLoadHandshake& handshake =
        session.saveLoadHandshakes().back();
    const std::vector<std::string> peers = reverseOrder
        ? std::vector<std::string>{"beta", "alpha", "host"}
        : std::vector<std::string>{"host", "alpha", "beta"};
    bool success = true;
    for (const std::string& peer : peers) {
        RuntimeMultiplayerSaveLoadApplied applied;
        applied.timelineEpoch = session.timelineEpoch();
        applied.proposalId = proposalId;
        applied.peerId = peer;
        applied.observedAuthoritativeStateHash =
            handshake.proposal.descriptor.identity.authoritativeStateHash;
        success &= expect(
            session.acknowledgeSaveLoadApplied(applied).accepted,
            "save/load applied ack accepted"
        );
    }
    return success;
}

bool testSaveLoadConsensus(
    RuntimeMultiplayerSessionState& forward,
    RuntimeMultiplayerSessionState& reverse
) {
    bool success = true;
    success &= expect(
        forward.advanceAuthoritativeTick("host", 130).accepted
            && reverse.advanceAuthoritativeTick("host", 130).accepted
            && forward.authoritativeTick() == 130
            && reverse.authoritativeTick() == 130,
        "host advances both replicas to the save barrier"
    );
    constexpr std::uint64_t saveFingerprint = 0xf00d1234ULL;
    const RuntimeMultiplayerSaveLoadProposal save = proposal(
        forward,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        129,
        "slot-1-generation-7",
        saveFingerprint
    );
    RuntimeMultiplayerSaveLoadProposal unauthorizedSave = save;
    unauthorizedSave.proposalId = 9;
    unauthorizedSave.proposerPeerId = "alpha";
    success &= expect(
        !forward.proposeSaveLoad(unauthorizedSave).accepted,
        "only the host may coordinate a save/load handshake"
    );
    success &= expect(
        forward.proposeSaveLoad(save).accepted
            && reverse.proposeSaveLoad(save).accepted,
        "save proposal begins on every peer"
    );
    const RuntimeMultiplayerSaveLoadResult duplicate =
        forward.proposeSaveLoad(save);
    success &= expect(
        duplicate.accepted && duplicate.duplicate
            && !forward.proposeSaveLoad(proposal(
                forward,
                2,
                RuntimeMultiplayerSaveLoadKind::Load,
                120,
                "blocked-while-pending",
                0x1111U
            )).accepted,
        "proposal retry is idempotent and concurrent handshake is rejected"
    );
    success &= submitResponses(
        forward,
        1,
        saveFingerprint,
        false,
        false,
        false
    );
    success &= submitResponses(
        reverse,
        1,
        saveFingerprint,
        true,
        false,
        false
    );
    const RuntimeMultiplayerSaveLoadResult forwardCommit =
        forward.issueSaveLoadCommit(1, 1, "host");
    const RuntimeMultiplayerSaveLoadResult reverseCommit =
        reverse.issueSaveLoadCommit(1, 1, "host");
    success &= expect(
        forwardCommit.accepted && reverseCommit.accepted,
        "unanimous prepare permits only an explicit host commit: "
            + forwardCommit.reason + " / " + reverseCommit.reason
            + " status=" + std::to_string(static_cast<int>(
                reverse.saveLoadHandshakes().back().status
            ))
            + " responses=" + std::to_string(
                reverse.saveLoadHandshakes().back().responses.size()
            )
    );
    success &= submitAppliedAcks(forward, 1, false);
    success &= submitAppliedAcks(reverse, 1, true);
    success &= expect(
        forward.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Committed
            && reverse.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Committed
            && forward.saveLoadHandshakes()
                == reverse.saveLoadHandshakes()
            && forward.stateHash() == reverse.stateHash(),
        "unanimous matching save confirmations commit independent of arrival"
    );

    constexpr std::uint64_t loadFingerprint = 0xbeef9876ULL;
    success &= expect(
        forward.advanceAuthoritativeTick("host", 140).accepted
            && reverse.advanceAuthoritativeTick("host", 140).accepted,
        "committed save releases the tick barrier"
    );
    const RuntimeMultiplayerSaveLoadProposal load = proposal(
        forward,
        2,
        RuntimeMultiplayerSaveLoadKind::Load,
        120,
        "slot-1-generation-7",
        loadFingerprint
    );
    success &= expect(
        forward.proposeSaveLoad(load).accepted
            && reverse.proposeSaveLoad(load).accepted,
        "load proposal begins after committed save handshake"
    );
    const RuntimeMultiplayerSaveLoadResult rejectedForward =
        forward.respondToSaveLoad(2, "beta", false, loadFingerprint);
    const RuntimeMultiplayerSaveLoadResult rejectedReverse =
        reverse.respondToSaveLoad(2, "beta", false, loadFingerprint);
    const RuntimeMultiplayerSaveLoadResult responseRetry =
        forward.respondToSaveLoad(
            2,
            "beta",
            false,
            loadFingerprint
        );
    success &= expect(
        forward.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Rejected
            && reverse.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Rejected
            && rejectedForward.accepted && rejectedReverse.accepted
            && responseRetry.accepted && responseRetry.duplicate
            && !forward.respondToSaveLoad(
                2,
                "beta",
                true,
                loadFingerprint
            ).accepted
            && forward.saveLoadHandshakes()
                == reverse.saveLoadHandshakes()
            && forward.stateHash() == reverse.stateHash(),
        "denial or fingerprint mismatch rejects load consistently"
    );

    const RuntimeMultiplayerSaveLoadProposal stalled = proposal(
        forward,
        3,
        RuntimeMultiplayerSaveLoadKind::Save,
        139,
        "stalled-save",
        0xaabbccddULL
    );
    success &= expect(
        forward.proposeSaveLoad(stalled).accepted
            && reverse.proposeSaveLoad(stalled).accepted,
        "a later save/load handshake may enter pending state"
    );
    const std::uint64_t beforeInvalidAbort = forward.stateHash();
    success &= expect(
        !forward.abortSaveLoad(99, "host").accepted
            && !forward.abortSaveLoad(3, "alpha").accepted
            && !forward.advanceAuthoritativeTick("host", 150).accepted
            && forward.stateHash() == beforeInvalidAbort,
        "wrong proposal, non-host abort, and pending tick advance are atomic"
    );
    const RuntimeMultiplayerSaveLoadResult aborted =
        forward.abortSaveLoad(3, "host");
    const RuntimeMultiplayerSaveLoadResult duplicateAbort =
        forward.abortSaveLoad(3, "host");
    success &= expect(
        aborted.accepted && aborted.changed
            && aborted.status == RuntimeMultiplayerSaveLoadStatus::Aborted
            && duplicateAbort.accepted && duplicateAbort.duplicate
            && duplicateAbort.status
                == RuntimeMultiplayerSaveLoadStatus::Aborted
            && reverse.abortSaveLoad(3, "host").accepted
            && !forward.respondToSaveLoad(
                3,
                "alpha",
                true,
                stalled.snapshotFingerprint
            ).accepted
            && forward.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Aborted
            && forward.saveLoadHandshakes()
                == reverse.saveLoadHandshakes()
            && forward.stateHash() == reverse.stateHash(),
        "host abort is terminal, idempotent, and releases a stalled handshake"
    );
    success &= expect(
        forward.advanceAuthoritativeTick("host", 150).accepted
            && reverse.advanceAuthoritativeTick("host", 150).accepted,
        "aborted save/load releases authoritative tick advancement"
    );

    RuntimeMultiplayerSaveLoadProposal invalid = proposal(
        forward,
        4,
        RuntimeMultiplayerSaveLoadKind::Save,
        149,
        std::string(runtimeMultiplayerMaximumSnapshotIdBytes + 1U, 's'),
        0x1234U
    );
    const std::uint64_t beforeInvalid = forward.stateHash();
    success &= expect(
        !forward.proposeSaveLoad(invalid).accepted
            && !forward.proposeSaveLoad(proposal(
                forward,
                4,
                RuntimeMultiplayerSaveLoadKind::Save,
                99,
                "bad-tick",
                0x1234U
            )).accepted
            && !forward.respondToSaveLoad(99, "host", true, 0x1234U).accepted
            && forward.stateHash() == beforeInvalid,
        "invalid snapshot length, tick, and proposal identity are atomic"
    );
    return success;
}

bool submitHashReports(
    RuntimeMultiplayerSessionState& session,
    int tick,
    bool reverseOrder,
    bool mismatchBeta
) {
    const std::vector<std::string> peers = reverseOrder
        ? std::vector<std::string>{"beta", "alpha", "host"}
        : std::vector<std::string>{"host", "alpha", "beta"};
    bool success = true;
    for (const std::string& peer : peers) {
        RuntimeMultiplayerStateHashReport report;
        report.tick = tick;
        report.peerId = peer;
        report.stateHash = mismatchBeta && peer == "beta"
            ? 0x22222222ULL
            : 0x11111111ULL;
        success &= expect(
            session.submitStateHash(report).accepted,
            "periodic state hash report accepted"
        );
    }
    return success;
}

bool testInitialStateHashAdmissionAndMismatch() {
    bool success = true;
    RuntimeMultiplayerSessionState matching =
        buildRunningSession(false, success);

    RuntimeMultiplayerStateHashReport host{99, "host", 0x51515151ULL};
    RuntimeMultiplayerStateHashReport alpha{99, "alpha", 0x51515151ULL};
    RuntimeMultiplayerStateHashReport beta{99, "beta", 0x51515151ULL};
    const RuntimeMultiplayerStateHashResult hostResult =
        matching.submitStateHash(host);
    const RuntimeMultiplayerStateHashResult alphaResult =
        matching.submitStateHash(alpha);
    const RuntimeMultiplayerStateHashResult betaResult =
        matching.submitStateHash(beta);
    success &= expect(
        hostResult.accepted && !hostResult.frameComplete
            && alphaResult.accepted && !alphaResult.frameComplete
            && betaResult.accepted && betaResult.frameComplete
            && !betaResult.desync
            && matching.stateHashFrames().size() == 1
            && matching.stateHashFrames().front().tick == 99
            && matching.stateHashFrames().front().complete,
        "initial completed-world hash is admitted before the first tick"
    );

    RuntimeMultiplayerSessionState mismatching =
        buildRunningSession(false, success);
    beta.stateHash ^= 1U;
    success &= expect(
        mismatching.submitStateHash(host).accepted
            && mismatching.submitStateHash(alpha).accepted,
        "initial hash mismatch waits for every room member"
    );
    const RuntimeMultiplayerStateHashResult mismatchResult =
        mismatching.submitStateHash(beta);
    success &= expect(
        mismatchResult.accepted && mismatchResult.frameComplete
            && mismatchResult.desync
            && mismatching.desyncDetected()
            && mismatching.firstDesyncTick() == 99,
        "initial completed-world mismatch is detected before simulation"
    );
    return success;
}

bool testStateHashConsensus(
    RuntimeMultiplayerSessionState& forward,
    RuntimeMultiplayerSessionState& reverse
) {
    bool success = true;
    RuntimeMultiplayerStateHashReport offPeriod;
    offPeriod.tick = 115;
    offPeriod.peerId = "host";
    offPeriod.stateHash = 1;
    const std::uint64_t beforeInvalid = forward.stateHash();
    success &= expect(
        !forward.submitStateHash(offPeriod).accepted
            && forward.stateHash() == beforeInvalid,
        "off-period state hash report is rejected atomically"
    );

    success &= submitHashReports(forward, 110, false, false);
    success &= submitHashReports(reverse, 110, true, false);
    RuntimeMultiplayerStateHashReport duplicate;
    duplicate.tick = 110;
    duplicate.peerId = "alpha";
    duplicate.stateHash = 0x11111111ULL;
    const RuntimeMultiplayerStateHashResult duplicateResult =
        forward.submitStateHash(duplicate);
    RuntimeMultiplayerStateHashReport conflict = duplicate;
    conflict.stateHash ^= 1U;
    success &= expect(
        duplicateResult.accepted && duplicateResult.duplicate
            && duplicateResult.frameComplete && !duplicateResult.desync
            && !forward.submitStateHash(conflict).accepted
            && forward.stateHashFrames().size() == 1
            && forward.stateHashFrames().front().complete
            && !forward.stateHashFrames().front().desync
            && forward.stateHash() == reverse.stateHash(),
        "matching periodic hashes complete without desync and retry safely"
    );

    success &= submitHashReports(forward, 120, false, true);
    success &= submitHashReports(reverse, 120, true, true);
    success &= expect(
        forward.desyncDetected() && reverse.desyncDetected()
            && forward.firstDesyncTick() == 120
            && reverse.firstDesyncTick() == 120
            && forward.stateHashFrames().back().complete
            && forward.stateHashFrames().back().desync
            && forward.stateHashFrames().back().hostStateHash
                == 0x11111111ULL
            && forward.stateHashFrames()
                == reverse.stateHashFrames()
            && forward.stateHash() == reverse.stateHash(),
        "mismatched periodic hash identifies deterministic first desync tick"
    );
    success &= expect(
        forward.advanceAuthoritativeTick("host", 161).accepted
            && reverse.advanceAuthoritativeTick("host", 161).accepted
            && forward.stateHashFrames().empty()
            && reverse.stateHashFrames().empty()
            && forward.desyncDetected() && reverse.desyncDetected()
            && forward.firstDesyncTick() == 120
            && reverse.firstDesyncTick() == 120
            && forward.stateHash() == reverse.stateHash(),
        "expired hash frames are pruned while first desync remains latched"
    );
    return success;
}

bool testFirstDesyncTracksLateEarlierFrame() {
    bool success = true;
    RuntimeMultiplayerSessionState session =
        buildRunningSession(false, success);
    success &= expect(
        session.advanceAuthoritativeTick("host", 130).accepted
            && submitHashReports(session, 130, false, true)
            && session.firstDesyncTick() == 130,
        "first completed desync frame is latched"
    );
    success &= expect(
        submitHashReports(session, 120, true, true)
            && session.firstDesyncTick() == 120
            && session.stateHashFrames().size() == 2
            && session.stateHashFrames().front().tick == 120,
        "a later-completed earlier desync frame moves the latch backward"
    );
    return success;
}

bool testAuthoritativeTickWindowsAndCapacity() {
    bool success = true;
    RuntimeMultiplayerSessionConfig config = standardConfig();
    config.maximumMembers = 3;
    config.maximumBufferedCommands = 6;
    config.maximumBufferedCommandsPerPeer = 2;
    config.maximumStateHashFrames = 3;
    config.commandLeadWindowTicks = 5;
    config.stateHashLagWindowTicks = 10;
    config.stateHashPeriodTicks = 5;

    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(config).accepted
            && session.joinMember(joinRequest("alpha", "Alpha Player")).accepted
            && session.joinMember(joinRequest("beta", "Beta Player")).accepted,
        "authoritative window lobby admits its roster"
    );
    success &= installMatchSetup(
        session,
        1U,
        "authoritative window setup installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && session.setMemberReady("beta", true).accepted
            && session.start("host", 100).accepted
            && session.authoritativeTick() == 100,
        "authoritative window session starts at its deterministic barrier"
    );

    const std::uint64_t beforePoison = session.stateHash();
    RuntimeMultiplayerStateHashReport futureHash{105, "alpha", 0x1001ULL};
    success &= expect(
        !session.submitCommand(command(106, "alpha", 1, {0x01U})).accepted
            && !session.submitCommand(
                command(99, "alpha", 1, {0x01U})
            ).accepted
            && !session.submitStateHash(futureHash).accepted
            && session.commands().empty()
            && session.stateHashFrames().empty()
            && session.stateHash() == beforePoison,
        "future and expired ingress cannot consume command or hash capacity"
    );

    success &= expect(
        session.submitCommand(command(100, "alpha", 1, {0x10U})).accepted
            && session.submitCommand(
                command(105, "alpha", 2, {0x20U})
            ).accepted
            && !session.submitCommand(
                command(101, "alpha", 3, {0x30U})
            ).accepted
            && session.submitCommand(
                command(101, "host", 1, {0x40U})
            ).accepted
            && session.commands().size() == 3,
        "one peer cannot monopolize the shared command buffer"
    );

    const std::uint64_t beforeBadAdvance = session.stateHash();
    const RuntimeMultiplayerOperationResult duplicateAdvance =
        session.advanceAuthoritativeTick("host", 100);
    success &= expect(
        duplicateAdvance.accepted && duplicateAdvance.duplicate
            && !session.advanceAuthoritativeTick("alpha", 105).accepted
            && session.stateHash() == beforeBadAdvance,
        "same-tick retry is idempotent and non-host advance is atomic"
    );
    success &= expect(
        session.advanceAuthoritativeTick("host", 105).accepted
            && session.authoritativeTick() == 105
            && session.commands().size() == 1
            && session.commands().front().tick == 105,
        "host advance retires commands older than the new barrier"
    );

    const std::uint64_t beforeStaleIngress = session.stateHash();
    success &= expect(
        !session.submitCommand(command(105, "alpha", 1, {0x10U})).accepted
            && !session.submitCommand(
                command(104, "alpha", 4, {0x40U})
            ).accepted
            && !session.submitCommand(
                command(111, "alpha", 4, {0x40U})
            ).accepted
            && session.stateHash() == beforeStaleIngress,
        "retired sequences, past ticks, and excess lead are rejected atomically"
    );

    RuntimeMultiplayerStateHashReport reachedHash{105, "alpha", 0x2001ULL};
    RuntimeMultiplayerStateHashReport oldestHash{100, "alpha", 0x2000ULL};
    success &= expect(
        session.submitStateHash(reachedHash).accepted
            && session.submitStateHash(oldestHash).accepted
            && session.stateHashFrames().size() == 2,
        "reached periodic hash ticks are admitted within the lag window"
    );
    success &= expect(
        session.advanceAuthoritativeTick("host", 110).accepted,
        "host advances to the newest periodic hash tick"
    );
    RuntimeMultiplayerStateHashReport newestAlpha{110, "alpha", 0x2002ULL};
    RuntimeMultiplayerStateHashReport newestHost{110, "host", 0x2002ULL};
    success &= expect(
        session.submitStateHash(newestAlpha).accepted
            && session.stateHashFrames().size() == 3
            && session.submitStateHash(newestHost).accepted
            && session.stateHashFrames().size() == 3,
        "a full lag window cannot block reports for an existing current frame"
    );
    success &= expect(
        session.advanceAuthoritativeTick("host", 121).accepted
            && session.commands().empty()
            && session.stateHashFrames().empty(),
        "advance prunes expired commands and hash frames"
    );
    const std::uint64_t beforeOutOfOrder = session.stateHash();
    RuntimeMultiplayerStateHashReport expiredHash{110, "alpha", 0x2002ULL};
    RuntimeMultiplayerStateHashReport retainedHash{120, "alpha", 0x2003ULL};
    RuntimeMultiplayerStateHashReport futureAgain{125, "alpha", 0x2004ULL};
    success &= expect(
        !session.advanceAuthoritativeTick("host", 120).accepted
            && !session.submitStateHash(expiredHash).accepted
            && !session.submitStateHash(futureAgain).accepted
            && session.submitStateHash(retainedHash).accepted
            && session.authoritativeTick() == 121
            && session.stateHashFrames().size() == 1
            && session.stateHashFrames().front().tick == 120,
        "out-of-order advance and hashes outside the lag window stay atomic"
    );
    success &= expect(
        beforeOutOfOrder != session.stateHash(),
        "the one valid retained hash is the only state change"
    );
    return success;
}

bool testOutOfOrderSequenceRetirement() {
    bool success = true;
    RuntimeMultiplayerSessionConfig config = standardConfig();
    config.maximumMembers = 2;
    config.maximumBufferedCommands = 8;
    config.maximumBufferedCommandsPerPeer = 4;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(config).accepted
            && session.joinMember(joinRequest("alpha", "Alpha Player")).accepted,
        "sequence reorder lobby admits its roster"
    );
    success &= installMatchSetup(
        session,
        1U,
        "sequence reorder setup installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && session.start("host", 100).accepted,
        "sequence reorder session starts"
    );

    const std::uint64_t beforeJump = session.stateHash();
    success &= expect(
        !session.submitCommand(command(
            100,
            "alpha",
            runtimeMultiplayerCommandSequenceReorderWindow + 1U,
            {0x01U}
        )).accepted
            && session.stateHash() == beforeJump,
        "initial sequence jump outside the bounded reorder window is atomic"
    );
    success &= expect(
        session.submitCommand(command(100, "alpha", 8, {0x08U})).accepted
            && session.advanceAuthoritativeTick("host", 105).accepted
            && session.commands().empty()
            && session.submitCommand(
                command(105, "alpha", 1, {0x01U})
            ).accepted
            && !session.submitCommand(
                command(105, "alpha", 8, {0x88U})
            ).accepted
            && session.commands().size() == 1
            && session.commands().front().sequence == 1,
        "retiring a high sequence preserves a delayed lower future command "
        "while rejecting reuse"
    );
    return success;
}

bool testPendingSaveLoadFreezesTicksButAdmitsInFlightMessages() {
    bool success = true;
    RuntimeMultiplayerSessionConfig config = standardConfig();
    config.maximumMembers = 2;
    config.maximumBufferedCommands = 8;
    config.maximumBufferedCommandsPerPeer = 4;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(config).accepted
            && session.joinMember(joinRequest("alpha", "Alpha Player")).accepted,
        "pending barrier lobby admits its roster"
    );
    success &= installMatchSetup(
        session,
        1U,
        "pending barrier setup installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && session.start("host", 100).accepted,
        "pending barrier session starts"
    );

    const RuntimeMultiplayerCommandPacket acceptedCommand =
        command(100, "alpha", 1, {0x11U});
    RuntimeMultiplayerStateHashReport acceptedHash{
        100,
        "alpha",
        0x1111ULL
    };
    const RuntimeMultiplayerSaveLoadProposal pending = proposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        99,
        "barrier-save",
        0xabcddcbaULL
    );
    success &= expect(
        session.submitCommand(acceptedCommand).accepted
            && session.submitStateHash(acceptedHash).accepted
            && session.proposeSaveLoad(pending).accepted,
        "save/load proposal closes an established tick frontier"
    );

    const std::uint64_t heldHash = session.stateHash();
    const RuntimeMultiplayerOperationResult commandRetry =
        session.submitCommand(acceptedCommand);
    const RuntimeMultiplayerStateHashResult hashRetry =
        session.submitStateHash(acceptedHash);
    RuntimeMultiplayerStateHashReport newHash{100, "host", 0x1111ULL};
    success &= expect(
        commandRetry.accepted && commandRetry.duplicate
            && hashRetry.accepted && hashRetry.duplicate
            && session.submitCommand(
                command(100, "alpha", 2, {0x22U})
            ).accepted
            && session.submitStateHash(newHash).accepted
            && !session.advanceAuthoritativeTick("host", 101).accepted
            && session.commands().size() == 2
            && session.stateHashFrames().size() == 1
            && session.stateHashFrames().front().complete
            && session.stateHash() != heldHash,
        "pending save admits in-flight commands and hashes while freezing ticks"
    );
    success &= expect(
        session.abortSaveLoad(1, "host").accepted
            && session.submitCommand(
                command(100, "alpha", 3, {0x33U})
            ).accepted
            && session.advanceAuthoritativeTick("host", 101).accepted,
        "host abort releases tick advancement without losing queued commands"
    );
    return success;
}

bool testStrictCapacityLimits() {
    bool success = true;
    RuntimeMultiplayerSessionConfig config = standardConfig();
    config.maximumMembers = 2;
    config.maximumCommandPayloadBytes = 2;
    config.maximumBufferedCommands = 2;
    config.maximumBufferedCommandsPerPeer = 1;
    config.maximumStateHashFrames = 1;
    config.maximumSaveLoadHandshakes = 1;
    config.commandLeadWindowTicks = 1;
    config.stateHashLagWindowTicks = 0;
    config.stateHashPeriodTicks = 1;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(config).accepted
            && session.joinMember(joinRequest("client", "Client")).accepted
            && !session.joinMember(joinRequest("extra", "Extra")).accepted,
        "small configured member limit is enforced"
    );
    success &= installMatchSetup(
        session,
        1U,
        "strict-capacity setup installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("client", true).accepted
            && session.start("host", 0).accepted,
        "small configured member limit is enforced"
    );

    success &= expect(
        session.submitCommand(command(0, "host", 1, {1U, 2U})).accepted
            && session.submitCommand(
                command(0, "client", 1, {3U})
            ).accepted
            && !session.submitCommand(
                command(0, "host", 2, {4U})
            ).accepted
            && !session.submitCommand(command(
                runtimeMultiplayerMaximumTick + 1,
                "host",
                2,
                {1U}
            )).accepted,
        "per-peer command count and maximum tick limits are enforced"
    );

    RuntimeMultiplayerStateHashReport hostHash{0, "host", 0x10U};
    RuntimeMultiplayerStateHashReport clientHash{0, "client", 0x10U};
    RuntimeMultiplayerStateHashReport nextHash{1, "host", 0x10U};
    success &= expect(
        session.submitStateHash(hostHash).accepted
            && session.submitStateHash(clientHash).accepted
            && !session.submitStateHash(nextHash).accepted
            && session.advanceAuthoritativeTick("host", 1).accepted,
        "state hash frame count limit is enforced"
    );

    const RuntimeMultiplayerSaveLoadProposal first = proposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        0,
        "capacity-save",
        0x777U
    );
    success &= expect(
        session.proposeSaveLoad(first).accepted
            && session.respondToSaveLoad(1, "host", true, 0x777U).accepted
            && session.respondToSaveLoad(1, "client", true, 0x777U).accepted
            && session.issueSaveLoadCommit(1, 1, "host").accepted
            && session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{1, 1, "host", true,
                    first.descriptor.identity.authoritativeStateHash}
            ).accepted
            && session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{1, 1, "client", true,
                    first.descriptor.identity.authoritativeStateHash}
            ).accepted
            && !session.proposeSaveLoad(proposal(
                session,
                2,
                RuntimeMultiplayerSaveLoadKind::Load,
                0,
                "capacity-load",
                0x888U
            )).accepted,
        "save/load handshake count limit is enforced"
    );
    return success;
}

bool testMatchSetupAuthorityRevisionAndRosterInvalidation() {
    bool success = true;
    RuntimeMultiplayerSessionState session;
    success &= expect(
        session.beginHost(standardConfig()).accepted
            && session.joinMember(
                joinRequest("alpha", "Alpha Player")
            ).accepted
            && session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted,
        "match-setup authority fixture creates a ready lobby"
    );

    const RuntimeMultiplayerMatchSetup revisionFive = matchSetupFor(
        session,
        5U
    );
    const std::uint64_t beforeUnauthorized = session.stateHash();
    const std::vector<RuntimeMultiplayerRoomMember> readyMembers =
        session.members();
    success &= expect(
        !session.setMatchSetup("alpha", revisionFive).accepted
            && !session.matchSetup().has_value()
            && session.latestMatchSetupRevision() == 0U
            && session.members() == readyMembers
            && session.stateHash() == beforeUnauthorized,
        "non-host match setup mutation is rejected atomically"
    );

    RuntimeMultiplayerMatchSetup wrongHost = revisionFive;
    wrongHost.hostPeerId = "alpha";
    RuntimeMultiplayerMatchSetup wrongDisplay = revisionFive;
    const auto alphaDisplay = std::find_if(
        wrongDisplay.members.begin(),
        wrongDisplay.members.end(),
        [](const RuntimeMultiplayerMatchMember& value) {
            return value.peerId == "alpha";
        }
    );
    if (alphaDisplay != wrongDisplay.members.end()) {
        alphaDisplay->displayName = "Wrong Alpha";
    }
    RuntimeMultiplayerMatchSetup wrongParticipation = revisionFive;
    const auto alphaIdentity = std::find_if(
        wrongParticipation.members.begin(),
        wrongParticipation.members.end(),
        [](const RuntimeMultiplayerMatchMember& value) {
            return value.peerId == "alpha";
        }
    );
    if (alphaIdentity != wrongParticipation.members.end()) {
        alphaIdentity->identity =
            RuntimeMultiplayerMatchMemberIdentity::Spectator;
    }
    const auto alphaSlot = std::find_if(
        wrongParticipation.slots.begin(),
        wrongParticipation.slots.end(),
        [](const RuntimeMultiplayerMatchSlot& value) {
            return value.peerId == "alpha";
        }
    );
    if (alphaSlot != wrongParticipation.slots.end()) {
        alphaSlot->controller =
            RuntimeMultiplayerMatchSlotController::Computer;
        alphaSlot->peerId.clear();
    }
    success &= expect(
        !session.setMatchSetup("host", wrongHost).accepted
            && !session.setMatchSetup("host", wrongDisplay).accepted
            && !session.setMatchSetup(
                "host",
                wrongParticipation
            ).accepted
            && session.stateHash() == beforeUnauthorized,
        "setup host, identity, and participation must exactly match roster"
    );

    const RuntimeMultiplayerOperationResult installed = session.setMatchSetup(
        "host",
        revisionFive
    );
    success &= expect(
        installed.accepted && installed.changed
            && session.matchSetup().has_value()
            && session.latestMatchSetupRevision() == 5U
            && !session.allMembersReady()
            && std::none_of(
                session.members().begin(),
                session.members().end(),
                [](const RuntimeMultiplayerRoomMember& value) {
                    return value.ready;
                }
            ),
        "accepted setup becomes authoritative and resets every ready flag"
    );

    const std::uint64_t revisionFiveHash = session.stateHash();
    success &= expect(
        !session.setMatchSetup("host", revisionFive).accepted
            && !session.setMatchSetup(
                "host",
                matchSetupFor(session, 4U)
            ).accepted
            && session.latestMatchSetupRevision() == 5U
            && session.stateHash() == revisionFiveHash,
        "equal and rolled-back setup revisions are rejected atomically"
    );

    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted,
        "members ready before a new setup revision"
    );
    RuntimeMultiplayerMatchSetup revisionSix = matchSetupFor(session, 6U);
    revisionSix.rules.startingCredits = 20000;
    success &= expect(
        session.setMatchSetup("host", revisionSix).accepted
            && session.latestMatchSetupRevision() == 6U
            && !session.allMembersReady(),
        "strictly newer setup revision applies and resets ready"
    );

    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && session.joinMember(
                joinRequest("beta", "Beta Player")
            ).accepted
            && !session.matchSetup().has_value()
            && session.latestMatchSetupRevision() == 6U
            && std::none_of(
                session.members().begin(),
                session.members().end(),
                [](const RuntimeMultiplayerRoomMember& value) {
                    return value.ready;
                }
            ),
        "join invalidates setup while preserving revision floor and readiness"
    );
    success &= expect(
        !session.setMatchSetup(
            "host",
            matchSetupFor(session, 6U)
        ).accepted,
        "roster invalidation cannot reopen an old setup revision"
    );
    success &= installMatchSetup(
        session,
        7U,
        "post-join setup revision installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted
            && session.setMemberReady("beta", true).accepted
            && session.leaveMember("beta").accepted
            && !session.matchSetup().has_value()
            && session.latestMatchSetupRevision() == 7U
            && !session.allMembersReady()
            && !session.start("host", 10).accepted,
        "leave invalidates setup and blocks start until a newer setup exists"
    );

    success &= installMatchSetup(
        session,
        8U,
        "post-leave setup revision installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("alpha", true).accepted,
        "post-leave setup roster becomes ready"
    );
    RuntimeMultiplayerLobbySnapshot snapshot;
    snapshot.config = session.config();
    snapshot.members = session.members();
    snapshot.latestMatchSetupRevision =
        session.latestMatchSetupRevision();
    snapshot.matchSetup = session.matchSetup();
    RuntimeMultiplayerSessionState replica;
    const RuntimeMultiplayerOperationResult replaced =
        replica.replaceLobbySnapshot(snapshot);
    success &= expect(
        replaced.accepted && replaced.changed
            && replica.matchSetup() == session.matchSetup()
            && replica.latestMatchSetupRevision()
                == session.latestMatchSetupRevision()
            && replica.members() == session.members()
            && replica.stateHash() == session.stateHash(),
        "lobby snapshot atomically copies setup bytes and identical state hash"
    );
    return success;
}

bool testSpectatorAuthorityAndConsensus() {
    bool success = true;
    RuntimeMultiplayerSessionConfig config = standardConfig();
    RuntimeMultiplayerSessionState session;
    RuntimeMultiplayerJoinRequest combatant =
        joinRequest("fighter", "Fighter");
    RuntimeMultiplayerJoinRequest spectator =
        joinRequest("observer", "Observer");
    spectator.participation =
        RuntimeMultiplayerParticipationRole::Spectator;

    success &= expect(
        session.beginHost(config).accepted
            && session.joinMember(spectator).accepted
            && session.joinMember(combatant).accepted
            && session.members().size() == 3U
            && session.members()[2].peerId == "observer"
            && session.members()[2].participation
                == RuntimeMultiplayerParticipationRole::Spectator,
        "spectator joins as an explicit canonical lobby participant"
    );

    RuntimeMultiplayerJoinRequest roleConflict = spectator;
    roleConflict.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    const std::uint64_t beforeRoleConflict = session.stateHash();
    success &= expect(
        !session.joinMember(roleConflict).accepted
            && session.stateHash() == beforeRoleConflict,
        "a joined peer cannot change participation role by retrying admission"
    );

    success &= installMatchSetup(
        session,
        1U,
        "spectator lobby setup installs"
    );
    success &= expect(
        session.setMemberReady("host", true).accepted
            && session.setMemberReady("fighter", true).accepted
            && session.setMemberReady("observer", true).accepted
            && session.start("host", 20).accepted,
        "spectators ready with combatants and receive the running session"
    );

    const std::uint64_t beforeSpoofedCommand = session.stateHash();
    success &= expect(
        !session.submitCommand(
            command(20, "observer", 1, {0x91U})
        ).accepted
            && session.commands().empty()
            && session.stateHash() == beforeSpoofedCommand,
        "spectator gameplay commands are rejected atomically"
    );

    RuntimeMultiplayerStateHashReport hostHash{20, "host", 0xabcU};
    RuntimeMultiplayerStateHashReport fighterHash{20, "fighter", 0xabcU};
    RuntimeMultiplayerStateHashReport observerHash{20, "observer", 0xabcU};
    success &= expect(
        session.submitStateHash(hostHash).accepted
            && session.submitStateHash(fighterHash).accepted
            && session.submitStateHash(observerHash).accepted
            && session.stateHashFrames().size() == 1U
            && session.stateHashFrames().front().complete
            && !session.stateHashFrames().front().desync,
        "spectator simulation hashes participate in full-room consensus"
    );

    const RuntimeMultiplayerSaveLoadProposal save = proposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        19,
        "spectator-save",
        0xdefU
    );
    success &= expect(
        session.proposeSaveLoad(save).accepted
            && session.respondToSaveLoad(1, "host", true, 0xdefU).accepted
            && session.respondToSaveLoad(1, "fighter", true, 0xdefU).accepted
            && session.respondToSaveLoad(1, "observer", true, 0xdefU).accepted
            && session.issueSaveLoadCommit(1, 1, "host").accepted
            && session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{1, 1, "host", true,
                    save.descriptor.identity.authoritativeStateHash}
            ).accepted
            && session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{1, 1, "fighter", true,
                    save.descriptor.identity.authoritativeStateHash}
            ).accepted
            && session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{1, 1, "observer", true,
                    save.descriptor.identity.authoritativeStateHash}
            ).accepted
            && session.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::Committed,
        "spectator approval is included in save/load consensus"
    );

    RuntimeMultiplayerSessionConfig observingHostConfig = standardConfig();
    observingHostConfig.hostParticipation =
        RuntimeMultiplayerParticipationRole::Spectator;
    RuntimeMultiplayerSessionState observingHost;
    RuntimeMultiplayerJoinRequest first =
        joinRequest("fighter-a", "Fighter A");
    RuntimeMultiplayerJoinRequest second =
        joinRequest("fighter-b", "Fighter B");
    success &= expect(
        observingHost.beginHost(observingHostConfig).accepted
            && observingHost.joinMember(first).accepted
            && observingHost.setMemberReady("host", true).accepted
            && observingHost.setMemberReady("fighter-a", true).accepted
            && !observingHost.start("host", 0).accepted
            && observingHost.joinMember(second).accepted,
        "observing host cannot start without a current setup"
    );
    success &= installMatchSetup(
        observingHost,
        1U,
        "observing-host setup installs"
    );
    success &= expect(
        observingHost.setMemberReady("host", true).accepted
            && observingHost.setMemberReady("fighter-a", true).accepted
            && observingHost.setMemberReady("fighter-b", true).accepted
            && observingHost.start("host", 0).accepted,
        "an observing host retains authority and launches without a slot"
    );
    return success;
}

bool testRunningSpectatorDepartureConsensus() {
    bool success = true;
    const auto buildFixture = [&success](const std::string& label) {
        RuntimeMultiplayerSessionState session;
        RuntimeMultiplayerJoinRequest fighter =
            joinRequest("fighter", "Fighter");
        RuntimeMultiplayerJoinRequest observer =
            joinRequest("observer", "Observer");
        observer.participation =
            RuntimeMultiplayerParticipationRole::Spectator;
        success &= expect(
            session.beginHost(standardConfig()).accepted
                && session.joinMember(fighter).accepted
                && session.joinMember(observer).accepted
                && installMatchSetup(session, 1U, label + " setup")
                && session.setMemberReady("host", true).accepted
                && session.setMemberReady("fighter", true).accepted
                && session.setMemberReady("observer", true).accepted
                && session.start("host", 20).accepted,
            label + " starts with two combatants and one spectator"
        );
        return session;
    };

    RuntimeMultiplayerSessionState hashSession =
        buildFixture("spectator hash-departure fixture");
    RuntimeMultiplayerStateHashReport hostHash{20, "host", 0xabcU};
    RuntimeMultiplayerStateHashReport fighterHash{20, "fighter", 0xabcU};
    success &= expect(
        hashSession.submitStateHash(hostHash).accepted
            && hashSession.submitStateHash(fighterHash).accepted
            && !hashSession.stateHashFrames().front().complete,
        "an open hash frame initially waits for the spectator"
    );
    const RuntimeMultiplayerOperationResult left =
        hashSession.leaveMember("observer");
    const RuntimeMultiplayerOperationResult duplicateLeave =
        hashSession.leaveMember("observer");
    const std::uint64_t afterLeaveHash = hashSession.stateHash();
    success &= expect(
        left.accepted && left.changed
            && duplicateLeave.accepted && duplicateLeave.duplicate
            && !duplicateLeave.changed
            && hashSession.status() == RuntimeMultiplayerRoomStatus::Running
            && hashSession.members().size() == 2U
            && hashSession.stateHashFrames().front().complete
            && !hashSession.stateHashFrames().front().desync
            && !hashSession.leaveMember("fighter").accepted
            && !hashSession.leaveMember("unknown-observer").accepted
            && !hashSession.submitStateHash(
                RuntimeMultiplayerStateHashReport{
                    20,
                    "observer",
                    0xabcU
                }
            ).accepted
            && !hashSession.submitCommand(
                command(20, "observer", 1, {0x91U})
            ).accepted
            && hashSession.stateHash() == afterLeaveHash,
        "running spectator leave closes open hash consensus, is idempotent, "
        "and rejects removed-peer replay"
    );

    RuntimeMultiplayerSessionState preparing =
        buildFixture("spectator pre-commit departure fixture");
    const RuntimeMultiplayerSaveLoadProposal preparingSave = proposal(
        preparing,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        19,
        "spectator-preparing-save",
        0xdefU
    );
    success &= expect(
        preparing.proposeSaveLoad(preparingSave).accepted
            && preparing.respondToSaveLoad(
                1,
                "host",
                true,
                0xdefU
            ).accepted
            && preparing.respondToSaveLoad(
                1,
                "fighter",
                true,
                0xdefU
            ).accepted
            && preparing.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::Transferring
            && preparing.leaveMember("observer").accepted
            && preparing.saveLoadHandshakes().front().participants
                == std::vector<std::string>{"fighter", "host"}
            && preparing.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::Prepared
            && !preparing.respondToSaveLoad(
                1,
                "observer",
                true,
                0xdefU
            ).accepted
            && preparing.issueSaveLoadCommit(1, 1, "host").accepted,
        "pre-commit spectator departure shrinks the transaction and prepares "
        "the remaining consensus"
    );
    success &= expect(
        preparing.acknowledgeSaveLoadApplied(
            RuntimeMultiplayerSaveLoadApplied{
                1,
                1,
                "host",
                true,
                preparingSave.descriptor.identity.authoritativeStateHash
            }
        ).accepted
            && preparing.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{
                    1,
                    1,
                    "fighter",
                    true,
                    preparingSave.descriptor.identity.authoritativeStateHash
                }
            ).accepted
            && preparing.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::Committed
            && !preparing.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{
                    1,
                    1,
                    "observer",
                    true,
                    preparingSave.descriptor.identity.authoritativeStateHash
                }
            ).accepted,
        "removed spectator cannot replay a prepare or applied acknowledgement"
    );

    RuntimeMultiplayerSessionState committing =
        buildFixture("spectator post-commit departure fixture");
    const RuntimeMultiplayerSaveLoadProposal committingSave = proposal(
        committing,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        19,
        "spectator-commit-issued-save",
        0x1234U
    );
    success &= expect(
        committing.proposeSaveLoad(committingSave).accepted
            && committing.respondToSaveLoad(
                1,
                "host",
                true,
                0x1234U
            ).accepted
            && committing.respondToSaveLoad(
                1,
                "fighter",
                true,
                0x1234U
            ).accepted
            && committing.respondToSaveLoad(
                1,
                "observer",
                true,
                0x1234U
            ).accepted
            && committing.issueSaveLoadCommit(1, 1, "host").accepted
            && committing.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{
                    1,
                    1,
                    "host",
                    true,
                    committingSave.descriptor.identity.authoritativeStateHash
                }
            ).accepted
            && committing.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{
                    1,
                    1,
                    "fighter",
                    true,
                    committingSave.descriptor.identity.authoritativeStateHash
                }
            ).accepted
            && committing.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::CommitIssued
            && committing.leaveMember("observer").accepted
            && committing.saveLoadHandshakes().front().status
                == RuntimeMultiplayerSaveLoadStatus::Committed,
        "post-commit spectator departure supplies the deterministic terminal "
        "membership transition"
    );
    return success;
}

bool testTimelineEpochLoadCommitAndReplayIsolation() {
    bool success = true;
    RuntimeMultiplayerSessionState session = buildRunningSession(false, success);
    success &= expect(
        session.timelineEpoch() == 1
            && session.advanceAuthoritativeTick("host", 130, 1).accepted
            && session.submitCommand(
                command(130, "alpha", 1, {0x31U})
            ).accepted,
        "timeline fixture starts on nonzero epoch 1"
    );
    RuntimeMultiplayerStateHashReport oldHash{120, "host", 0x1111U, 1};
    success &= expect(
        session.submitStateHash(oldHash).accepted,
        "old timeline contains command/hash admission history"
    );

    RuntimeMultiplayerSaveLoadProposal load = proposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Load,
        109,
        "rollback-generation-1",
        0x9012U
    );
    RuntimeMultiplayerSaveLoadProposal setupMismatch = load;
    setupMismatch.descriptor.identity.gameplaySetupFingerprint ^= 1U;
    const std::uint64_t beforeMismatch = session.stateHash();
    success &= expect(
        !session.proposeSaveLoad(setupMismatch).accepted
            && session.stateHash() == beforeMismatch
            && session.proposeSaveLoad(load).accepted
            && session.proposeSaveLoad(load).duplicate,
        "setup mismatch is atomic and an exact proposal retry is idempotent"
    );
    RuntimeMultiplayerStateHashReport inFlightHash{
        129,
        "beta",
        0x3333U,
        1
    };
    success &= expect(
        session.submitCommand(
            command(130, "beta", 1, {0x32U})
        ).accepted
            && session.submitStateHash(inFlightHash).accepted
            && session.commands().size() == 2
            && session.stateHashFrames().size() == 2,
        "pending load admits messages already in flight on the old timeline"
    );
    RuntimeMultiplayerSaveLoadProposal conflict = load;
    conflict.descriptor.identity.snapshotToken = "rollback-generation-2";
    conflict.snapshotId = conflict.descriptor.identity.snapshotToken;
    success &= expect(
        !session.proposeSaveLoad(conflict).accepted,
        "a proposal id cannot be reused with conflicting descriptor data"
    );
    success &= submitResponses(
        session,
        1,
        load.snapshotFingerprint,
        true,
        false,
        false
    );
    const std::uint64_t beforeUnauthorizedCommit = session.stateHash();
    success &= expect(
        session.saveLoadHandshakes().back().status
                == RuntimeMultiplayerSaveLoadStatus::Prepared
            && !session.issueSaveLoadCommit(1, 1, "alpha").accepted
            && session.stateHash() == beforeUnauthorizedCommit
            && session.issueSaveLoadCommit(1, 1, "host").accepted
            && session.timelineEpoch() == 2
            && session.authoritativeTick() == 110
            && session.commands().empty()
            && session.stateHashFrames().empty()
            && !session.desyncDetected(),
        "load commit atomically rolls tick back, advances epoch, and clears admission history"
    );

    RuntimeMultiplayerCommandPacket staleCommand =
        command(110, "alpha", 1, {0x41U});
    staleCommand.timelineEpoch = 1;
    RuntimeMultiplayerStateHashReport staleHash{110, "host", 0x2222U, 1};
    RuntimeMultiplayerSaveLoadApplied staleAck{
        1,
        1,
        "host",
        true,
        load.descriptor.identity.authoritativeStateHash
    };
    const std::uint64_t beforeReplay = session.stateHash();
    success &= expect(
        !session.submitCommand(staleCommand).accepted
            && !session.submitStateHash(staleHash).accepted
            && !session.advanceAuthoritativeTick("host", 111, 1).accepted
            && !session.acknowledgeSaveLoadApplied(staleAck).accepted
            && session.stateHash() == beforeReplay,
        "old command/hash/tick/applied replay is rejected atomically"
    );
    success &= submitAppliedAcks(session, 1, false);
    success &= expect(
        session.saveLoadHandshakes().back().status
            == RuntimeMultiplayerSaveLoadStatus::Committed,
        "load becomes committed only after every peer applies the new epoch"
    );

    RuntimeMultiplayerSaveLoadProposal reused = proposal(
        session,
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        109,
        "reused-id",
        0x7771U
    );
    RuntimeMultiplayerSaveLoadProposal fatal = proposal(
        session,
        2,
        RuntimeMultiplayerSaveLoadKind::Save,
        109,
        "fatal-after-commit",
        0x7772U
    );
    success &= expect(
        !session.proposeSaveLoad(reused).accepted
            && session.proposeSaveLoad(fatal).accepted,
        "proposal ids remain monotonic across timeline epochs"
    );
    success &= submitResponses(
        session,
        2,
        fatal.snapshotFingerprint,
        false,
        false,
        false
    );
    success &= expect(
        session.issueSaveLoadCommit(2, 2, "host").accepted
            && !session.abortSaveLoad(2, 2, "host").accepted,
        "commit-issued handshakes cannot return to ordinary abort"
    );
    RuntimeMultiplayerSaveLoadApplied failed;
    failed.timelineEpoch = 2;
    failed.proposalId = 2;
    failed.peerId = "alpha";
    failed.applied = false;
    success &= expect(
        session.acknowledgeSaveLoadApplied(failed).accepted
            && session.status() == RuntimeMultiplayerRoomStatus::Aborted
            && session.abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
        "a post-commit apply failure deterministically aborts the room"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testConfigurationCompatibilityAndLobby();

    RuntimeMultiplayerSessionState forward =
        buildRunningSession(false, success);
    RuntimeMultiplayerSessionState reverse =
        buildRunningSession(true, success);
    success &= expect(
        forward.stateHash() == reverse.stateHash(),
        "room state hash is stable across join and ready arrival order"
    );
    success &= testDeterministicCommands(forward, reverse);
    success &= testSaveLoadConsensus(forward, reverse);
    success &= testInitialStateHashAdmissionAndMismatch();
    success &= testStateHashConsensus(forward, reverse);
    success &= testFirstDesyncTracksLateEarlierFrame();
    success &= testAuthoritativeTickWindowsAndCapacity();
    success &= testOutOfOrderSequenceRetirement();
    success &= testPendingSaveLoadFreezesTicksButAdmitsInFlightMessages();
    success &= testStrictCapacityLimits();
    success &= testMatchSetupAuthorityRevisionAndRosterInvalidation();
    success &= testSpectatorAuthorityAndConsensus();
    success &= testRunningSpectatorDepartureConsensus();
    success &= testTimelineEpochLoadCommitAndReplayIsolation();

    if (!success) {
        return 1;
    }
    std::cout
        << "Runtime multiplayer session smoke: compatibility=yes"
        << " lobby=yes commands=windowed desync=latched"
        << " saveLoad=commit/reject/abort limits=strict\n";
    return 0;
}
