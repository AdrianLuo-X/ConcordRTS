#include "network/snapshots/RuntimeMultiplayerSnapshotSession.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
namespace
{
int checks = 0, failures = 0;
void expect(bool condition, const char *text)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cerr << "snapshot session contract: " << text << '\n';
    }
}
RuntimeMultiplayerMatchSetup matchSetupFor(const RuntimeMultiplayerSessionState &session)
{
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = session.config().hostPeerId;
    setup.map.canonicalId = "maps/official/lockstep-smoke.map";
    setup.map.contentFingerprint = session.config().compatibility.contentFingerprint;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x0123456789abcdefULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    std::uint8_t slotIndex = 0;
    for (const RuntimeMultiplayerRoomMember &roomMember : session.members())
    {
        RuntimeMultiplayerMatchMember member;
        member.peerId = roomMember.peerId;
        member.displayName = roomMember.displayName;
        member.identity = roomMember.participation == RuntimeMultiplayerParticipationRole::Combatant
                              ? RuntimeMultiplayerMatchMemberIdentity::Combatant
                              : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        setup.members.push_back(std::move(member));

        if (roomMember.participation == RuntimeMultiplayerParticipationRole::Spectator)
        {
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
bool startSession(RuntimeMultiplayerSessionState &session)
{
    RuntimeMultiplayerSessionConfig room;
    room.roomId = "lockstep-room";
    room.hostPeerId = "zulu";
    room.hostDisplayName = "Zulu";
    room.hostParticipation = RuntimeMultiplayerParticipationRole::Combatant;
    room.compatibility.protocolFingerprint = 0x1111222233334444ULL;
    room.compatibility.contentFingerprint = 0xaaaabbbbccccddddULL;

    const RuntimeMultiplayerOperationResult began = session.beginHost(room);
    if (!began.accepted)
        return false;

    RuntimeMultiplayerJoinRequest alpha;
    alpha.peerId = "alpha";
    alpha.displayName = "Alpha";
    alpha.participation = RuntimeMultiplayerParticipationRole::Combatant;
    alpha.compatibility = room.compatibility;
    const RuntimeMultiplayerOperationResult joinedAlpha = session.joinMember(alpha);
    if (!joinedAlpha.accepted)
        return false;

    RuntimeMultiplayerJoinRequest spectator;
    spectator.peerId = "mid";
    spectator.displayName = "Middle Spectator";
    spectator.participation = RuntimeMultiplayerParticipationRole::Spectator;
    spectator.compatibility = room.compatibility;
    const RuntimeMultiplayerOperationResult joinedSpectator = session.joinMember(spectator);
    if (!joinedSpectator.accepted)
        return false;

    if (!session.setMatchSetup("zulu", matchSetupFor(session)).accepted)
    {
        return false;
    }

    if (!session.setMemberReady("zulu", true).accepted ||
        !session.setMemberReady("alpha", true).accepted ||
        !session.setMemberReady("mid", true).accepted)
    {
        return false;
    }
    return session.start("zulu", 10).accepted;
}
RuntimeMultiplayerSaveLoadProposal snapshotProposal(const RuntimeMultiplayerSessionState &session,
                                                    std::uint64_t proposalId,
                                                    RuntimeMultiplayerSaveLoadKind kind,
                                                    int snapshotTick, std::string token)
{
    RuntimeMultiplayerSaveLoadProposal result;
    result.timelineEpoch = session.timelineEpoch();
    result.proposalId = proposalId;
    result.kind = kind;
    result.proposerPeerId = session.config().hostPeerId;
    RuntimeMultiplayerSnapshotIdentity &identity = result.descriptor.identity;
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
    if (session.matchSetup().has_value())
    {
        runtimeMultiplayerGameplaySetupFingerprint(*session.matchSetup(),
                                                   identity.gameplaySetupFingerprint, error);
    }
    result.descriptor.byteCount = 1;
    result.descriptor.chunkCount = 1;
    result.descriptor.snapshotFingerprint = 0xabcdef0000000000ULL ^ proposalId;
    result.descriptor.sha256[0] = 1;
    result.tick = snapshotTick;
    result.snapshotId = identity.snapshotToken;
    result.snapshotFingerprint = result.descriptor.snapshotFingerprint;
    return result;
}
struct Fixture
{
    RuntimeMultiplayerSessionState session;
    RuntimeMultiplayerLanSnapshotHooks hooks;
    std::string local = "alpha";
    RuntimeMultiplayerLanPhase phase = RuntimeMultiplayerLanPhase::Running;
    RuntimeMultiplayerSnapshotSession flow{session, hooks, local, phase};
    std::vector<std::string> trace;
    int mode = 0, stages = 0, commits = 0, posts = 0, discards = 0;
    std::vector<std::uint8_t> bytes =
        std::vector<std::uint8_t>(runtimeMultiplayerSnapshotChunkBytes + 7U, 0x53);
    RuntimeMultiplayerSaveLoadProposal proposal;
    Fixture()
    {
        hooks.stageVerifiedBytes = [this](const auto &d, auto b, auto &h, auto &e) {
            ++stages;
            trace.push_back("stage");
            expect(b.size() == bytes.size() && std::equal(b.begin(), b.end(), bytes.begin()),
                   "stage exact verified bytes");
            if (mode == 3)
                throw std::runtime_error("stage");
            h = d.identity.authoritativeStateHash + (mode == 2 ? 1U : 0U);
            if (mode == 1)
            {
                e = "bad\nstage";
                return false;
            }
            return true;
        };
        hooks.commitStaged = [this](const auto &d, auto &h, auto &e) {
            ++commits;
            trace.push_back("commit");
            if (mode == 6)
                throw std::runtime_error("commit");
            h = d.identity.authoritativeStateHash + (mode == 5 ? 1U : 0U);
            if (mode == 4)
            {
                e = "bad\ncommit";
                return false;
            }
            return true;
        };
        hooks.postSessionCommit = [this](const auto &, const auto &, auto &e) {
            ++posts;
            trace.push_back("post");
            if (mode == 8)
                throw std::runtime_error("post");
            if (mode == 7)
            {
                e = "bad\npost";
                return false;
            }
            return true;
        };
        hooks.discardStaged = [this](const auto &) {
            ++discards;
            trace.push_back("discard");
            if (mode == 9)
                throw std::runtime_error("discard");
        };
        proposal.proposalId = 1;
        proposal.timelineEpoch = 1;
        proposal.kind = RuntimeMultiplayerSaveLoadKind::Save;
        proposal.proposerPeerId = "zulu";
        proposal.tick = 9;
        proposal.snapshotId = "snapshot-contract";
        RuntimeMultiplayerSnapshotIdentity id;
        id.proposalId = 1;
        id.sourceTimelineEpoch = 1;
        id.barrierTick = 9;
        id.snapshotTick = 9;
        id.snapshotToken = proposal.snapshotId;
        id.authoritativeStateHash = 123456;
        id.gameplaySetupFingerprint = 1;
        std::string error;
        const bool valid =
            makeRuntimeMultiplayerSnapshotDescriptor(id, bytes, proposal.descriptor, error);
        expect(valid, "real descriptor");
        if (!valid)
            throw std::runtime_error(error);
        proposal.snapshotFingerprint = proposal.descriptor.snapshotFingerprint;
        activate();
    }
    void activate()
    {
        flow.resetState();
        auto &s = flow.state();
        s.snapshotProposal = proposal;
        s.snapshotTransferActive = true;
        expect(s.snapshotAssembler.begin(proposal.descriptor).accepted, "assembler begin");
    }
    void canonical(bool load = false)
    {
        expect(startSession(session), "canonical running session");
        proposal = snapshotProposal(session, 1,
                                    load ? RuntimeMultiplayerSaveLoadKind::Load
                                         : RuntimeMultiplayerSaveLoadKind::Save,
                                    9, "snapshot-contract");
        std::string e;
        auto identity = proposal.descriptor.identity;
        expect(makeRuntimeMultiplayerSnapshotDescriptor(identity, bytes, proposal.descriptor, e),
               "canonical descriptor");
        proposal.snapshotFingerprint = proposal.descriptor.snapshotFingerprint;
        expect(session.proposeSaveLoad(proposal).accepted, "canonical proposal");
        for (const std::string peer : {"zulu", "alpha", "mid"})
        {
            RuntimeMultiplayerSaveLoadResponse r;
            r.timelineEpoch = 1;
            r.proposalId = 1;
            r.peerId = peer;
            r.prepared = true;
            r.observedSnapshotFingerprint = proposal.descriptor.snapshotFingerprint;
            r.observedAuthoritativeStateHash = identity.authoritativeStateHash;
            expect(session.prepareSaveLoad(r).accepted, "canonical prepared participant");
        }
        expect(session.saveLoadHandshakes().back().status ==
                   RuntimeMultiplayerSaveLoadStatus::Prepared,
               "canonical prepared status");
        activate();
        flow.state().snapshotStaged = true;
    }
};
struct Port final : RuntimeMultiplayerSnapshotSessionPort
{
    Fixture &f;
    bool sendFail = false, broadcastFail = false;
    std::vector<RuntimeMultiplayerMessage> messages;
    std::vector<RuntimeNetworkPacket> packets;
    int protocolErrors = 0, clears = 0, applies = 0, aborts = 0;
    bool discarded = false;
    std::string lastError;
    RuntimeMultiplayerSessionAbortReason abortedReason{};
    explicit Port(Fixture &x) : f(x)
    {
    }
    bool sendToHost(const RuntimeMultiplayerMessage &m, std::string &e) override
    {
        f.trace.push_back(m.type == RuntimeMultiplayerMessageType::SnapshotChunkAck ? "ack"
                                                                                    : "prepare");
        messages.push_back(m);
        if (sendFail)
        {
            e = "send failed";
            return false;
        }
        return true;
    }
    bool sendPacketToHost(const RuntimeNetworkPacket &p, std::string &e) override
    {
        f.trace.push_back("packet");
        packets.push_back(p);
        if (sendFail)
        {
            e = "packet failed";
            return false;
        }
        return true;
    }
    bool broadcastPacket(const RuntimeNetworkPacket &p, std::string &e) override
    {
        f.trace.push_back("broadcast");
        packets.push_back(p);
        if (broadcastFail)
        {
            e = "broadcast failed";
            return false;
        }
        return true;
    }
    void protocolFailure(std::string e, std::vector<RuntimeMultiplayerLanEvent> &) override
    {
        ++protocolErrors;
        lastError = std::move(e);
        f.trace.push_back("protocol");
    }
    void appendAppliedEvent(const RuntimeMultiplayerMessage &,
                            const RuntimeMultiplayerMessageApplyResult &,
                            std::vector<RuntimeMultiplayerLanEvent> &) override
    {
        f.trace.push_back("append");
    }
    void clearSnapshotTransfer(bool discard) noexcept override
    {
        ++clears;
        discarded = discard;
        f.flow.discardStaged(discard);
        f.flow.resetState();
    }
    void abortHost(RuntimeMultiplayerSessionAbortReason reason) override
    {
        ++aborts;
        abortedReason = reason;
        f.trace.push_back("abort");
    }
    RuntimeMultiplayerLanOperationResult applyHostMessage(RuntimeMultiplayerMessage m) override
    {
        ++applies;
        f.trace.push_back("apply");
        RuntimeNetworkPacket p;
        std::string e;
        RuntimeMultiplayerLanOperationResult r;
        if (encodeRuntimeMultiplayerMessage(m, p, e))
        {
            auto a = applyRuntimeMultiplayerMessage(p, f.local, f.session);
            r.applied = a.accepted;
            r.succeeded = a.accepted;
            r.reason = a.reason;
        }
        return r;
    }
};
void chunkCases()
{
    Fixture f;
    Port port(f);
    std::vector<RuntimeMultiplayerLanEvent> events;
    RuntimeMultiplayerSnapshotChunkProducer producer;
    std::string error;
    expect(producer.begin(f.proposal.descriptor, f.bytes).accepted, "real two-chunk producer");
    RuntimeMultiplayerSnapshotChunk first, last;
    expect(producer.currentChunk(first, error), "first chunk");
    auto bad = first;
    ++bad.proposalId;
    f.flow.handleClientSnapshotChunk(bad, port, events);
    expect(port.protocolErrors == 1 && f.stages == 0 && port.messages.empty(),
           "bad proposal no stage/send");
    port.protocolErrors = 0;
    f.trace.clear();
    f.flow.handleClientSnapshotChunk(first, port, events);
    expect(f.trace == std::vector<std::string>{"ack"} && !f.flow.state().snapshotStaged,
           "first ack before candidate");
    if (port.messages.empty())
        return;
    expect(producer
               .acknowledge(std::get<RuntimeMultiplayerSnapshotChunkAck>(port.messages.back().body))
               .accepted,
           "first ack");
    expect(producer.currentChunk(last, error), "last chunk");
    f.flow.handleClientSnapshotChunk(last, port, events);
    expect(f.trace == std::vector<std::string>{"ack", "ack", "stage", "prepare"} && f.stages == 1 &&
               f.flow.state().snapshotStaged,
           "ack then stage then prepare");
    f.trace.clear();
    f.flow.handleClientSnapshotChunk(last, port, events);
    expect(f.trace == std::vector<std::string>{"ack"} && f.stages == 1, "completed retry ack only");
    bad = last;
    bad.bytes[0] ^= 1;
    f.flow.handleClientSnapshotChunk(bad, port, events);
    expect(port.protocolErrors == 1 && f.stages == 1, "conflicting completed retry rejected");
    for (int mode = 1; mode <= 3; ++mode)
    {
        Fixture x;
        Port p(x);
        x.mode = mode;
        x.flow.handleClientSnapshotChunk(first, p, events);
        x.trace.clear();
        x.flow.handleClientSnapshotChunk(last, p, events);
        expect(p.protocolErrors == 1 && x.stages == 1 &&
                   x.trace == std::vector<std::string>{"ack", "stage", "protocol"},
               "stage false/hash/exception never prepare");
        expect(x.flow.state().snapshotStaged && x.flow.state().haveCompletedSnapshotChunk,
               "old pre-hook candidate flags retained for caller cleanup");
        expect(p.lastError.find('\n') == std::string::npos, "stage reason sanitized");
    }
    {
        Fixture x;
        Port p(x);
        p.sendFail = true;
        x.flow.handleClientSnapshotChunk(first, p, events);
        expect(x.stages == 0 && p.protocolErrors == 1 && !x.flow.state().snapshotStaged,
               "ack send failure no stage");
    }
    {
        Fixture x;
        Port p(x);
        x.flow.handleClientSnapshotChunk(first, p, events);
        auto corrupt = last;
        corrupt.bytes[0] ^= 1;
        x.trace.clear();
        x.flow.handleClientSnapshotChunk(corrupt, p, events);
        expect(x.stages == 0 && p.protocolErrors == 1 && !x.flow.state().haveCompletedSnapshotChunk,
               "digest failure no candidate publish");
    }
}
void hookAndCadenceCases()
{
    Fixture f;
    std::string e;
    std::uint64_t hash = 0;
    RuntimeMultiplayerSnapshotChunkProducer producer;
    expect(producer.begin(f.proposal.descriptor, f.bytes).accepted, "cadence producer");
    RuntimeMultiplayerSnapshotChunk chunk;
    bool sent = false;
    std::uint64_t last = 0;
    f.flow.state().snapshotTransferActive = false;
    expect(!f.flow.beginHostPoll(), "inactive no poll");
    f.flow.state().snapshotTransferActive = true;
    f.phase = RuntimeMultiplayerLanPhase::Lobby;
    expect(!f.flow.beginHostPoll(), "lobby no poll");
    f.phase = RuntimeMultiplayerLanPhase::Running;
    expect(f.flow.beginHostPoll(), "running poll");
    expect(f.flow.nextHostChunk(false, producer, sent, last, chunk, e) ==
               RuntimeMultiplayerSnapshotSession::HostChunk::Skip,
           "unadmitted peer skipped");
    expect(f.flow.nextHostChunk(true, producer, sent, last, chunk, e) ==
               RuntimeMultiplayerSnapshotSession::HostChunk::Ready,
           "first chunk ready");
    f.flow.sentHostChunk(sent, last);
    expect(sent && last == 1, "only confirmed send marks poll");
    for (int i = 0; i < 31; ++i)
    {
        expect(f.flow.beginHostPoll(), "poll retained");
        expect(f.flow.nextHostChunk(true, producer, sent, last, chunk, e) ==
                   RuntimeMultiplayerSnapshotSession::HostChunk::Skip,
               "old32 poll cadence");
    }
    expect(f.flow.beginHostPoll() && f.flow.nextHostChunk(true, producer, sent, last, chunk, e) ==
                                         RuntimeMultiplayerSnapshotSession::HostChunk::Ready,
           "exact32 retransmit");
    f.flow.state().snapshotSendPollCounter = std::numeric_limits<std::uint64_t>::max();
    last = std::numeric_limits<std::uint64_t>::max() - 31;
    expect(f.flow.beginHostPoll() && f.flow.state().snapshotSendPollCounter == 0 &&
               f.flow.nextHostChunk(true, producer, true, last, chunk, e) ==
                   RuntimeMultiplayerSnapshotSession::HostChunk::Ready,
           "unsigned cadence wrap unchanged");
    f.flow.state().snapshotStaged = false;
    expect(!f.flow.commitSnapshot(hash, e) && e == "no verified staged snapshot is available",
           "commit no staged");
    f.flow.state().snapshotStaged = true;
    for (int mode = 4; mode <= 6; ++mode)
    {
        f.mode = mode;
        expect(!f.flow.commitSnapshot(hash, e) && e.find('\n') == std::string::npos,
               "commit false/hash/exception rejected");
    }
    f.mode = 0;
    expect(f.flow.commitSnapshot(hash, e) &&
               hash == f.proposal.descriptor.identity.authoritativeStateHash,
           "commit exact hash");
    expect(f.flow.postSnapshotSessionCommit(e) && f.posts == 0, "save skips load rebind");
    f.flow.state().snapshotProposal.kind = RuntimeMultiplayerSaveLoadKind::Load;
    for (int mode = 7; mode <= 8; ++mode)
    {
        f.mode = mode;
        expect(!f.flow.postSnapshotSessionCommit(e) && e.find('\n') == std::string::npos,
               "post false/exception sanitized");
    }
    f.mode = 9;
    f.flow.discardStaged(true);
    expect(f.discards == 1, "discard exception contained");
    f.flow.resetState();
    expect(!f.flow.state().snapshotTransferActive && !f.flow.state().snapshotStaged &&
               f.flow.state().snapshotSendPollCounter == 0,
           "reset leaves cadence unchanged0");
    f.hooks = {};
    expect(f.hooks.empty() && !f.hooks.complete() &&
               !f.flow.stageSnapshotBytes(f.proposal, f.bytes, hash, e),
           "missing hooks rejected");
}
void canonicalCases()
{
    std::vector<RuntimeMultiplayerLanEvent> events;
    {
        Fixture f;
        Port p(f);
        f.local = "zulu";
        f.canonical();
        f.trace.clear();
        auto r = f.flow.issueHostSnapshotCommit(p, events);
        expect(r.succeeded && r.applied &&
                   f.trace == std::vector<std::string>{"commit", "broadcast", "append", "apply"},
               "host canonical commit order");
        expect(f.flow.state().snapshotCommitCompleted && p.applies == 1 && p.aborts == 0,
               "host commit owner");
        for (const std::string peer : {"alpha", "mid"})
        {
            RuntimeMultiplayerSaveLoadApplied a;
            a.timelineEpoch = 1;
            a.proposalId = 1;
            a.peerId = peer;
            a.applied = true;
            a.observedAuthoritativeStateHash =
                f.proposal.descriptor.identity.authoritativeStateHash;
            expect(f.session.acknowledgeSaveLoadApplied(a).accepted, "remote applied");
        }
        bool reconnect = true;
        std::uint64_t proposal = 1;
        f.flow.finishSnapshotTerminalState(p, reconnect, proposal);
        expect(p.clears == 1 && !p.discarded && !reconnect && proposal == 0,
               "terminal success clears without discard and releases matching reconnect");
    }
    for (int fail : {4, 7, 0})
    {
        Fixture f;
        Port p(f);
        f.local = "zulu";
        f.canonical(fail == 7);
        f.mode = fail;
        p.broadcastFail = fail == 0;
        f.trace.clear();
        auto r = f.flow.issueHostSnapshotCommit(p, events);
        expect(!r.succeeded && r.applied && p.aborts == 1 && p.applies == 0,
               "host hook/broadcast failure terminates after canonical transition");
        expect(f.trace.front() == "commit" && f.trace.back() == "abort", "host failure order");
        expect(p.abortedReason == (fail == 0
                                       ? RuntimeMultiplayerSessionAbortReason::TransportFailure
                                       : RuntimeMultiplayerSessionAbortReason::ProtocolViolation),
               "host exact abort class");
    }
    for (int mode : {0, 4})
    {
        Fixture f;
        Port p(f);
        f.canonical(true);
        f.mode = mode;
        f.trace.clear();
        RuntimeMultiplayerMessage m;
        m.type = RuntimeMultiplayerMessageType::SaveLoadCommit;
        m.body = RuntimeMultiplayerSaveLoadCommitMessage{1, 1, "zulu"};
        RuntimeNetworkPacket packet;
        std::string e;
        expect(encodeRuntimeMultiplayerMessage(m, packet, e), "client commit canonical packet");
        f.flow.handleClientSnapshotCommit(packet, m, p, events);
        if (mode == 0)
        {
            expect(f.trace == std::vector<std::string>{"commit", "append", "post", "packet"} &&
                       f.flow.state().snapshotCommitCompleted,
                   "client commit then canonical assign/rebind/ack");
            expect(f.session.timelineEpoch() == 2 && f.commits == 1 && f.posts == 1,
                   "load new epoch only once");
            f.trace.clear();
            f.flow.handleClientSnapshotCommit(packet, m, p, events);
            expect(f.trace == std::vector<std::string>{"append", "packet"} && f.commits == 1 &&
                       f.posts == 1,
                   "duplicate client commit no hook repeat");
        }
        else
            expect(f.phase == RuntimeMultiplayerLanPhase::Aborted && p.clears == 1 && p.discarded &&
                       f.discards == 1 &&
                       f.trace == std::vector<std::string>{"commit", "append", "packet", "append",
                                                           "discard"},
                   "failed client commit ack before abort/discard");
    }
    {
        Fixture f;
        Port p(f);
        f.trace.clear();
        auto r = f.flow.issueHostSnapshotCommit(p, events);
        expect(!r.succeeded && f.trace.empty(), "unprepared host no hook");
        bool reconnect = true;
        std::uint64_t proposal = 7;
        f.flow.finishSnapshotTerminalState(p, reconnect, proposal);
        expect(p.clears == 0 && reconnect && proposal == 7,
               "missing handshake no terminal cleanup");
    }
}
} // namespace
int main()
{
    try
    {
        chunkCases();
        hookAndCadenceCases();
        canonicalCases();
        std::cout << "Snapshot session contract: checks=" << checks << " failures=" << failures
                  << " realCandidateBytes=262151 canonicalCommit=yes noSocketProvider=yes\n";
        return failures ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "snapshot session fixture setup error: " << e.what() << '\n';
        return 1;
    }
}
