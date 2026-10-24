#include "network/snapshots/RuntimeMultiplayerSnapshotSession.h"
#include <algorithm>
#include <utility>

namespace
{
std::string safeSnapshotReason(std::string reason)
{
    if (reason.empty())
        reason = "multiplayer snapshot hook rejected state";
    for (char &character : reason)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU)
            character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumJoinRejectionReasonBytes)
    {
        reason.resize(runtimeMultiplayerMaximumJoinRejectionReasonBytes);
    }
    return reason;
}
RuntimeMultiplayerLanOperationResult failedOperation(std::string reason)
{
    RuntimeMultiplayerLanOperationResult result;
    result.reason = std::move(reason);
    return result;
}
RuntimeMultiplayerLanOperationResult appliedOperation(
    const RuntimeMultiplayerMessageApplyResult &applied, bool broadcastQueued,
    std::string transportError)
{
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = applied.accepted && broadcastQueued;
    result.applied = applied.accepted;
    result.queued = broadcastQueued;
    result.duplicate = applied.duplicate;
    result.reason = applied.accepted ? std::move(transportError) : applied.reason;
    return result;
}
RuntimeMultiplayerLanEvent event(
    RuntimeMultiplayerLanEventKind kind,
    RuntimeMultiplayerMessageType type = RuntimeMultiplayerMessageType::JoinRequest)
{
    RuntimeMultiplayerLanEvent result;
    result.kind = kind;
    result.messageType = type;
    return result;
}
} // namespace

bool RuntimeMultiplayerLanSnapshotHooks::empty() const
{
    return !stageVerifiedBytes && !commitStaged && !postSessionCommit && !discardStaged;
}
bool RuntimeMultiplayerLanSnapshotHooks::complete() const
{
    return static_cast<bool>(stageVerifiedBytes) && static_cast<bool>(commitStaged) &&
           static_cast<bool>(postSessionCommit) && static_cast<bool>(discardStaged);
}

const RuntimeMultiplayerSaveLoadHandshake *RuntimeMultiplayerSnapshotSession::activeSaveLoad() const
{
    if (!data_.snapshotTransferActive)
        return nullptr;
    const auto &handshakes = session.saveLoadHandshakes();
    const auto found = std::find_if(
        handshakes.rbegin(), handshakes.rend(),
        [this](const RuntimeMultiplayerSaveLoadHandshake &handshake) {
            return handshake.proposal.proposalId == data_.snapshotProposal.proposalId &&
                   handshake.proposal.timelineEpoch == data_.snapshotProposal.timelineEpoch;
        });
    return found == handshakes.rend() ? nullptr : &*found;
}

bool RuntimeMultiplayerSnapshotSession::stageSnapshotBytes(
    const RuntimeMultiplayerSaveLoadProposal &proposal, std::span<const std::uint8_t> bytes,
    std::uint64_t &observedHash, std::string &errorMessage) const
{
    if (!snapshotHooks.complete())
    {
        errorMessage = "complete snapshot hooks are required";
        return false;
    }
    bool accepted = false;
    try
    {
        accepted = snapshotHooks.stageVerifiedBytes(proposal.descriptor, bytes, observedHash,
                                                    errorMessage);
    }
    catch (...)
    {
        errorMessage = "multiplayer snapshot stage hook failed";
    }
    if (!accepted)
    {
        errorMessage = safeSnapshotReason(std::move(errorMessage));
        return false;
    }
    if (observedHash != proposal.descriptor.identity.authoritativeStateHash)
    {
        errorMessage = "staged snapshot authoritative hash does not match";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerSnapshotSession::commitSnapshot(std::uint64_t &observedHash,
                                                       std::string &errorMessage) const
{
    if (!data_.snapshotTransferActive || !data_.snapshotStaged || !snapshotHooks.complete())
    {
        errorMessage = "no verified staged snapshot is available";
        return false;
    }
    bool accepted = false;
    try
    {
        accepted = snapshotHooks.commitStaged(data_.snapshotProposal.descriptor, observedHash,
                                              errorMessage);
    }
    catch (...)
    {
        errorMessage = "multiplayer snapshot commit hook failed";
    }
    if (!accepted)
    {
        errorMessage = safeSnapshotReason(std::move(errorMessage));
        return false;
    }
    if (observedHash != data_.snapshotProposal.descriptor.identity.authoritativeStateHash)
    {
        errorMessage = "committed snapshot authoritative hash does not match";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerSnapshotSession::postSnapshotSessionCommit(std::string &errorMessage) const
{
    if (data_.snapshotProposal.kind != RuntimeMultiplayerSaveLoadKind::Load)
    {
        errorMessage.clear();
        return true;
    }
    bool accepted = false;
    try
    {
        accepted = snapshotHooks.postSessionCommit(data_.snapshotProposal.descriptor, session,
                                                   errorMessage);
    }
    catch (...)
    {
        errorMessage = "multiplayer snapshot post-commit hook failed";
    }
    if (!accepted)
    {
        errorMessage = safeSnapshotReason(std::move(errorMessage));
        return false;
    }
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerSnapshotSession::discardStaged(bool discard) const noexcept
{
    if (discard && data_.snapshotStaged && snapshotHooks.discardStaged)
    {
        try
        {
            snapshotHooks.discardStaged(data_.snapshotProposal.descriptor);
        }
        catch (...)
        {
            // Cleanup is best-effort during an already terminal path.
        }
    }
}

void RuntimeMultiplayerSnapshotSession::resetState() noexcept
{
    data_.snapshotAssembler.reset();
    data_.haveCompletedSnapshotChunk = false;
    data_.completedSnapshotChunk = RuntimeMultiplayerSnapshotChunk{};
    data_.completedSnapshotAck = RuntimeMultiplayerSnapshotChunkAck{};
    data_.snapshotTransferActive = false;
    data_.snapshotStaged = false;
    data_.snapshotProposal = RuntimeMultiplayerSaveLoadProposal{};
    data_.stagedAuthoritativeStateHash = 0;
    data_.snapshotCommitCompleted = false;
    data_.committedAuthoritativeStateHash = 0;
}

void RuntimeMultiplayerSnapshotSession::handleClientSnapshotChunk(
    const RuntimeMultiplayerSnapshotChunk &chunk, RuntimeMultiplayerSnapshotSessionPort &port,
    std::vector<RuntimeMultiplayerLanEvent> &events)
{
    if (!data_.snapshotTransferActive || chunk.proposalId != data_.snapshotProposal.proposalId ||
        chunk.sourceTimelineEpoch != data_.snapshotProposal.timelineEpoch)
    {
        port.protocolFailure("snapshot chunk does not match the active proposal", events);
        return;
    }

    if (data_.haveCompletedSnapshotChunk)
    {
        if (chunk != data_.completedSnapshotChunk)
        {
            port.protocolFailure("completed snapshot chunk retry conflicts with verified data",
                                 events);
            return;
        }
        RuntimeMultiplayerMessage ackMessage;
        ackMessage.type = RuntimeMultiplayerMessageType::SnapshotChunkAck;
        ackMessage.body = data_.completedSnapshotAck;
        std::string sendError;
        if (!port.sendToHost(ackMessage, sendError))
        {
            port.protocolFailure(std::move(sendError), events);
        }
        return;
    }

    const RuntimeMultiplayerSnapshotTransferResult accepted =
        data_.snapshotAssembler.acceptChunk(chunk);
    if (!accepted.accepted)
    {
        port.protocolFailure(accepted.reason, events);
        return;
    }
    RuntimeMultiplayerSnapshotChunkAck ack;
    std::string errorMessage;
    if (!data_.snapshotAssembler.latestAcknowledgement(ack, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
        return;
    }
    RuntimeMultiplayerMessage ackMessage;
    ackMessage.type = RuntimeMultiplayerMessageType::SnapshotChunkAck;
    ackMessage.body = ack;
    if (!port.sendToHost(ackMessage, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
        return;
    }
    if (!accepted.complete)
        return;

    std::vector<std::uint8_t> verifiedBytes;
    data_.completedSnapshotChunk = chunk;
    data_.completedSnapshotAck = ack;
    data_.haveCompletedSnapshotChunk = true;
    if (!data_.snapshotAssembler.takeCompletedBytes(verifiedBytes, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
        return;
    }
    std::uint64_t observedHash = 0;
    data_.snapshotStaged = true;
    if (!stageSnapshotBytes(data_.snapshotProposal, verifiedBytes, observedHash, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
        return;
    }
    data_.stagedAuthoritativeStateHash = observedHash;

    RuntimeMultiplayerSaveLoadResponseMessage prepare;
    prepare.timelineEpoch = session.timelineEpoch();
    prepare.proposalId = data_.snapshotProposal.proposalId;
    prepare.peerId = localPeerId;
    prepare.approved = true;
    prepare.observedFingerprint = data_.snapshotProposal.descriptor.snapshotFingerprint;
    prepare.observedAuthoritativeStateHash = observedHash;
    RuntimeMultiplayerMessage prepareMessage;
    prepareMessage.type = RuntimeMultiplayerMessageType::SaveLoadPrepare;
    prepareMessage.body = std::move(prepare);
    if (!port.sendToHost(prepareMessage, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
    }
}

void RuntimeMultiplayerSnapshotSession::handleClientSnapshotCommit(
    const RuntimeNetworkPacket &packet, const RuntimeMultiplayerMessage &decoded,
    RuntimeMultiplayerSnapshotSessionPort &port, std::vector<RuntimeMultiplayerLanEvent> &events)
{
    const RuntimeMultiplayerSaveLoadCommitMessage &commit =
        std::get<RuntimeMultiplayerSaveLoadCommitMessage>(decoded.body);
    if (!data_.snapshotTransferActive || !data_.snapshotStaged ||
        commit.proposalId != data_.snapshotProposal.proposalId ||
        commit.timelineEpoch != data_.snapshotProposal.timelineEpoch ||
        commit.requestingPeerId != session.config().hostPeerId)
    {
        port.protocolFailure("snapshot commit does not match verified staged bytes", events);
        return;
    }

    RuntimeMultiplayerSessionState candidate = session;
    RuntimeMultiplayerMessageApplyResult applied =
        applyTrustedCanonicalRuntimeMultiplayerMessage(packet, candidate);
    if (!applied.accepted)
    {
        port.protocolFailure(applied.reason, events);
        return;
    }

    std::string errorMessage;
    bool committed = data_.snapshotCommitCompleted;
    std::uint64_t observedHash = data_.committedAuthoritativeStateHash;
    if (!committed)
    {
        committed = commitSnapshot(observedHash, errorMessage);
    }
    session = std::move(candidate);
    port.appendAppliedEvent(decoded, applied, events);
    if (committed && !data_.snapshotCommitCompleted)
    {
        committed = postSnapshotSessionCommit(errorMessage);
    }
    if (committed)
    {
        data_.snapshotCommitCompleted = true;
        data_.committedAuthoritativeStateHash = observedHash;
    }

    RuntimeMultiplayerSaveLoadAppliedMessage ack;
    ack.timelineEpoch = session.timelineEpoch();
    ack.proposalId = data_.snapshotProposal.proposalId;
    ack.peerId = localPeerId;
    ack.applied = committed;
    ack.observedAuthoritativeStateHash = committed ? observedHash : 0U;
    const std::string commitFailureReason =
        committed ? std::string{} : safeSnapshotReason(errorMessage);
    RuntimeMultiplayerMessage ackMessage;
    ackMessage.type = RuntimeMultiplayerMessageType::SaveLoadApplied;
    ackMessage.body = ack;
    RuntimeNetworkPacket ackPacket;
    if (!encodeRuntimeMultiplayerMessage(ackMessage, ackPacket, errorMessage) ||
        !port.sendPacketToHost(ackPacket, errorMessage))
    {
        port.protocolFailure(std::move(errorMessage), events);
        return;
    }
    if (committed)
        return;

    RuntimeMultiplayerMessageApplyResult failed =
        applyTrustedCanonicalRuntimeMultiplayerMessage(ackPacket, session);
    phase = RuntimeMultiplayerLanPhase::Aborted;
    RuntimeMultiplayerLanEvent rejected = event(RuntimeMultiplayerLanEventKind::ProtocolRejected,
                                                RuntimeMultiplayerMessageType::SaveLoadCommit);
    rejected.peerId = session.config().hostPeerId;
    rejected.reason = commitFailureReason;
    events.push_back(std::move(rejected));
    if (failed.accepted)
        port.appendAppliedEvent(ackMessage, failed, events);
    port.clearSnapshotTransfer(true);
}

void RuntimeMultiplayerSnapshotSession::finishSnapshotTerminalState(
    RuntimeMultiplayerSnapshotSessionPort &port, bool &reconnectRecoveryPending,
    std::uint64_t &reconnectRecoveryProposalId)
{
    if (!data_.snapshotTransferActive)
        return;
    if (session.status() == RuntimeMultiplayerRoomStatus::Aborted)
    {
        port.clearSnapshotTransfer(true);
        return;
    }
    const RuntimeMultiplayerSaveLoadHandshake *handshake = activeSaveLoad();
    if (handshake == nullptr)
        return;
    if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Rejected ||
        handshake->status == RuntimeMultiplayerSaveLoadStatus::Aborted)
    {
        port.clearSnapshotTransfer(true);
    }
    else if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Committed)
    {
        const bool completedReconnectRecovery =
            reconnectRecoveryPending &&
            reconnectRecoveryProposalId == handshake->proposal.proposalId;
        port.clearSnapshotTransfer(false);
        if (completedReconnectRecovery)
        {
            reconnectRecoveryPending = false;
            reconnectRecoveryProposalId = 0;
        }
    }
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerSnapshotSession::issueHostSnapshotCommit(
    RuntimeMultiplayerSnapshotSessionPort &port,
    std::vector<RuntimeMultiplayerLanEvent> &pendingEvents)
{
    const RuntimeMultiplayerSaveLoadHandshake *handshake = activeSaveLoad();
    if (handshake == nullptr || handshake->status != RuntimeMultiplayerSaveLoadStatus::Prepared)
    {
        return failedOperation("snapshot handshake is not prepared");
    }

    RuntimeMultiplayerMessage commitMessage;
    commitMessage.type = RuntimeMultiplayerMessageType::SaveLoadCommit;
    commitMessage.body = RuntimeMultiplayerSaveLoadCommitMessage{
        session.timelineEpoch(), data_.snapshotProposal.proposalId, localPeerId};
    RuntimeNetworkPacket commitPacket;
    std::string errorMessage;
    if (!encodeRuntimeMultiplayerMessage(commitMessage, commitPacket, errorMessage))
    {
        return failedOperation(std::move(errorMessage));
    }

    RuntimeMultiplayerSessionState candidate = session;
    RuntimeMultiplayerMessageApplyResult commitApplied =
        applyRuntimeMultiplayerMessage(commitPacket, localPeerId, candidate);
    if (!commitApplied.accepted)
    {
        return appliedOperation(commitApplied, false, {});
    }

    std::uint64_t observedHash = 0;
    const bool committed = commitSnapshot(observedHash, errorMessage);
    session = std::move(candidate);
    if (!committed || !postSnapshotSessionCommit(errorMessage))
    {
        const std::string hookFailure = errorMessage;
        std::string ignoredBroadcastError;
        (void)port.broadcastPacket(commitPacket, ignoredBroadcastError);
        port.appendAppliedEvent(commitMessage, commitApplied, pendingEvents);
        RuntimeMultiplayerLanEvent rejected =
            event(RuntimeMultiplayerLanEventKind::ProtocolRejected,
                  RuntimeMultiplayerMessageType::SaveLoadCommit);
        rejected.peerId = localPeerId;
        rejected.tick = session.authoritativeTick();
        rejected.reason = hookFailure;
        pendingEvents.push_back(std::move(rejected));
        (void)port.abortHost(RuntimeMultiplayerSessionAbortReason::ProtocolViolation);
        RuntimeMultiplayerLanOperationResult result;
        result.applied = true;
        result.reason = hookFailure;
        return result;
    }
    data_.snapshotCommitCompleted = true;
    data_.committedAuthoritativeStateHash = observedHash;

    const bool commitBroadcast = port.broadcastPacket(commitPacket, errorMessage);
    port.appendAppliedEvent(commitMessage, commitApplied, pendingEvents);
    if (!commitBroadcast)
    {
        (void)port.abortHost(RuntimeMultiplayerSessionAbortReason::TransportFailure);
        RuntimeMultiplayerLanOperationResult result;
        result.applied = true;
        result.reason = std::move(errorMessage);
        return result;
    }

    RuntimeMultiplayerMessage appliedMessage;
    appliedMessage.type = RuntimeMultiplayerMessageType::SaveLoadApplied;
    appliedMessage.body = RuntimeMultiplayerSaveLoadAppliedMessage{
        session.timelineEpoch(), data_.snapshotProposal.proposalId, localPeerId, true,
        observedHash};
    return port.applyHostMessage(std::move(appliedMessage));
}

bool RuntimeMultiplayerSnapshotSession::beginHostPoll()
{
    if (!data_.snapshotTransferActive || phase != RuntimeMultiplayerLanPhase::Running)
        return false;
    ++data_.snapshotSendPollCounter;
    return true;
}
RuntimeMultiplayerSnapshotSession::HostChunk RuntimeMultiplayerSnapshotSession::nextHostChunk(
    bool admitted, const RuntimeMultiplayerSnapshotChunkProducer &producer, bool sent,
    std::uint64_t lastSentPoll, RuntimeMultiplayerSnapshotChunk &chunk, std::string &error) const
{
    if (!admitted || !producer.active() || producer.complete())
        return HostChunk::Skip;
    if (sent && data_.snapshotSendPollCounter - lastSentPoll < 32U)
        return HostChunk::Skip;
    if (!producer.currentChunk(chunk, error))
        return HostChunk::Error;
    return HostChunk::Ready;
}
void RuntimeMultiplayerSnapshotSession::sentHostChunk(bool &sent, std::uint64_t &lastSentPoll) const
{
    sent = true;
    lastSentPoll = data_.snapshotSendPollCounter;
}
