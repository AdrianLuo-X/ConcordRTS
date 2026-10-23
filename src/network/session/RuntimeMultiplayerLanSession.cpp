#include "network/session/RuntimeMultiplayerLanSession.h"
#include "network/lobby/RuntimeRoomPassword.h"
#include "network/lobby/RuntimeMultiplayerLobbyMapTransfer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t snapshotChunkRetransmitPollInterval = 32U;

class RoomPasswordStringClearGuard {
public:
    explicit RoomPasswordStringClearGuard(std::string& value)
        : value_(value) {}
    ~RoomPasswordStringClearGuard() {
        runtimeRoomPasswordSecureClear(value_);
    }

    RoomPasswordStringClearGuard(const RoomPasswordStringClearGuard&) = delete;
    RoomPasswordStringClearGuard& operator=(
        const RoomPasswordStringClearGuard&
    ) = delete;

private:
    std::string& value_;
};

std::string contentFingerprintText(std::uint64_t fingerprint) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(16U, '0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const unsigned int shift = static_cast<unsigned int>(
            (result.size() - index - 1U) * 4U
        );
        result[index] = digits[(fingerprint >> shift) & 0x0fU];
    }
    return result;
}

std::string safeReason(std::string reason) {
    if (reason.empty()) reason = "multiplayer join was rejected";
    for (char& character : reason) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumJoinRejectionReasonBytes) {
        reason.resize(runtimeMultiplayerMaximumJoinRejectionReasonBytes);
    }
    return reason;
}

std::string safeCommandValidationReason(std::string reason) {
    if (reason.empty()) {
        reason = "multiplayer command payload was rejected";
    }
    for (char& character : reason) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumJoinRejectionReasonBytes) {
        reason.resize(runtimeMultiplayerMaximumJoinRejectionReasonBytes);
    }
    return reason;
}

std::string safeSimulationReason(std::string reason) {
    if (reason.empty()) reason = "multiplayer simulation hook rejected state";
    for (char& character : reason) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumJoinRejectionReasonBytes) {
        reason.resize(runtimeMultiplayerMaximumJoinRejectionReasonBytes);
    }
    return reason;
}

std::string safeSnapshotReason(std::string reason) {
    if (reason.empty()) reason = "multiplayer snapshot hook rejected state";
    for (char& character : reason) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumJoinRejectionReasonBytes) {
        reason.resize(runtimeMultiplayerMaximumJoinRejectionReasonBytes);
    }
    return reason;
}

std::string safeLobbyResultReason(
    std::string reason,
    std::string_view fallback
) {
    if (reason.empty()) reason.assign(fallback);
    for (char& character : reason) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU) character = ' ';
    }
    if (reason.size() > runtimeMultiplayerMaximumLobbyResultReasonBytes) {
        reason.resize(runtimeMultiplayerMaximumLobbyResultReasonBytes);
    }
    return reason;
}

RuntimeMultiplayerLanOperationResult failedOperation(std::string reason) {
    RuntimeMultiplayerLanOperationResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerLanOperationResult queuedOperation() {
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = true;
    result.queued = true;
    return result;
}

RuntimeMultiplayerLanOperationResult deferredOperation(std::string reason) {
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = true;
    result.deferred = true;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerLanOperationResult appliedOperation(
    const RuntimeMultiplayerMessageApplyResult& applied,
    bool broadcastQueued,
    std::string transportError
) {
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = applied.accepted && broadcastQueued;
    result.applied = applied.accepted;
    result.queued = broadcastQueued;
    result.duplicate = applied.duplicate;
    result.reason = applied.accepted
        ? std::move(transportError)
        : applied.reason;
    return result;
}

bool messageUsesLobbySnapshot(RuntimeMultiplayerMessageType type) {
    return type == RuntimeMultiplayerMessageType::JoinRequest
        || type == RuntimeMultiplayerMessageType::Leave
        || type == RuntimeMultiplayerMessageType::Ready;
}

std::size_t deferredCanonicalPacketBytes(
    const RuntimeNetworkPacket& packet
) {
    return sizeof(packet.type) + packet.payload.size();
}

} // namespace

bool RuntimeMultiplayerLanSimulationHooks::empty() const {
    return !onStarted
        && !onCommandAdmitted
        && !prepareAuthoritativeAdvance
        && !onAuthoritativeAdvance;
}

bool RuntimeMultiplayerLanSimulationHooks::complete() const {
    return static_cast<bool>(onStarted)
        && static_cast<bool>(onCommandAdmitted)
        && static_cast<bool>(prepareAuthoritativeAdvance)
        && static_cast<bool>(onAuthoritativeAdvance);
}

bool RuntimeMultiplayerLanSnapshotHooks::empty() const {
    return !stageVerifiedBytes
        && !commitStaged
        && !postSessionCommit
        && !discardStaged;
}

bool RuntimeMultiplayerLanSnapshotHooks::complete() const {
    return static_cast<bool>(stageVerifiedBytes)
        && static_cast<bool>(commitStaged)
        && static_cast<bool>(postSessionCommit)
        && static_cast<bool>(discardStaged);
}

struct RuntimeMultiplayerLanSession::Impl {
    struct HostPeer {
        std::string address;
        std::string logicalPeerId;
        std::uint64_t admissionToken = 0;
        bool admitted = false;
        bool kicked = false;
        int closeAfterPolls = -1;
        std::uint64_t lastLobbySlotRequestId = 0;
        std::optional<RuntimeMultiplayerLobbySlotOptionsRequestMessage>
            pendingLobbySlotRequest;
        RuntimeMultiplayerSnapshotChunkProducer snapshotProducer;
        bool snapshotChunkSent = false;
        std::uint64_t snapshotChunkLastSentPoll = 0;
        RuntimeMultiplayerSnapshotChunkProducer lobbyResumeProducer;
        bool lobbyResumeOfferSent = false;
        bool lobbyResumeChunkSent = false;
        std::chrono::steady_clock::time_point lobbyResumeChunkSentAt{};
        std::chrono::steady_clock::time_point lobbyResumeStartedAt{};
        RuntimeMultiplayerLobbyMapTransfer::HostPeer mapTransfer;
    };

    struct SuspendedPeer {
        std::string logicalPeerId;
        std::string displayName;
        RuntimeMultiplayerParticipationRole participation =
            RuntimeMultiplayerParticipationRole::Combatant;
        std::uint64_t timelineEpoch = 0;
        int authoritativeTick = -1;
        std::uint64_t sessionStateHash = 0;
        std::chrono::steady_clock::time_point deadline{};
        std::string disconnectReason;
    };

    struct DeferredHostPacket {
        std::uint64_t transportPeerId = 0;
        RuntimeNetworkPacket packet;
    };

    struct ReconnectCanonicalCheckpoint {
        std::uint64_t timelineEpoch = 0;
        int authoritativeTick = -1;
        std::uint64_t sessionStateHash = 0;
        RuntimeNetworkPacket packet;
    };

    RuntimeMultiplayerLanRole role = RuntimeMultiplayerLanRole::None;
    RuntimeMultiplayerLanPhase phase =
        RuntimeMultiplayerLanPhase::Inactive;
    RuntimeLanDiscoveryHost discoveryHost;
    RuntimeLanDiscoveryBrowser browser;
    RuntimeTcpServer server;
    RuntimeTcpClient client;
    RuntimeMultiplayerSessionState session;
    RuntimeMultiplayerLanCommandPayloadValidator commandPayloadValidator;
    RuntimeMultiplayerLanSimulationHooks simulationHooks;
    RuntimeMultiplayerLanSnapshotHooks snapshotHooks;
    std::size_t maximumCombatants = runtimeMultiplayerMaximumCombatants;
    std::size_t maximumSpectators = runtimeMultiplayerMaximumSpectators;
    std::deque<RuntimeNetworkPacket> pendingClientCanonicalPackets;
    std::size_t pendingClientCanonicalBytes = 0;
    RuntimeLanRoomAdvertisement advertisement;
    RuntimeMultiplayerLanJoinOptions joinOptions;
    std::map<std::uint64_t, HostPeer> hostPeers;
    std::map<std::string, SuspendedPeer> suspendedPeers;
    std::deque<DeferredHostPacket> reconnectDeferredHostPackets;
    std::size_t reconnectDeferredHostBytes = 0;
    std::deque<ReconnectCanonicalCheckpoint> reconnectCanonicalHistory;
    std::size_t reconnectCanonicalHistoryBytes = 0;
    std::vector<RuntimeMultiplayerLanEvent> pendingEvents;
    std::string localPeerId;
    RuntimeRoomPasswordVerifier hostPasswordVerifier{};
    RuntimeRoomPasswordVerifier joinPasswordVerifier{};
    std::uint64_t roomGeneration = 0;
    std::uint64_t admissionToken = 0;
    std::uint64_t nonceCounter = 0;
    std::uint64_t nextLobbySlotRequestId = 1;
    std::uint64_t lastLobbySlotRequestIdSent = 0;
    std::uint16_t boundDiscoveryPort = 0;
    std::uint16_t boundSessionPort = 0;
    std::uint32_t reconnectGraceMilliseconds = 0;
    std::uint32_t peerReconnectGraceMilliseconds = 0;
    bool challengeReceived = false;
    bool joinAccepted = false;
    bool hostPasswordProtected = false;
    bool joinPasswordProofRequired = false;
    bool clientCanonicalPacketDeferred = false;
    bool snapshotTransferActive = false;
    bool snapshotStaged = false;
    RuntimeMultiplayerSaveLoadProposal snapshotProposal;
    RuntimeMultiplayerSnapshotChunkAssembler snapshotAssembler;
    bool haveCompletedSnapshotChunk = false;
    RuntimeMultiplayerSnapshotChunk completedSnapshotChunk;
    RuntimeMultiplayerSnapshotChunkAck completedSnapshotAck;
    std::uint64_t stagedAuthoritativeStateHash = 0;
    bool snapshotCommitCompleted = false;
    std::uint64_t committedAuthoritativeStateHash = 0;
    std::uint64_t snapshotSendPollCounter = 0;
    bool reconnectRecoveryPending = false;
    std::uint64_t reconnectRecoveryProposalId = 0;
    std::chrono::steady_clock::time_point reconnectDeadline{};
    std::chrono::steady_clock::time_point nextReconnectAttempt{};
    std::uint32_t reconnectAttemptCount = 0;

    const RuntimeMultiplayerSaveLoadHandshake* activeSaveLoad() const {
        if (!snapshotTransferActive) return nullptr;
        const auto& handshakes = session.saveLoadHandshakes();
        const auto found = std::find_if(
            handshakes.rbegin(),
            handshakes.rend(),
            [this](const RuntimeMultiplayerSaveLoadHandshake& handshake) {
                return handshake.proposal.proposalId
                        == snapshotProposal.proposalId
                    && handshake.proposal.timelineEpoch
                        == snapshotProposal.timelineEpoch;
            }
        );
        return found == handshakes.rend() ? nullptr : &*found;
    }

    void clearSnapshotTransfer(bool discard) noexcept {
        if (discard && snapshotStaged && snapshotHooks.discardStaged) {
            try {
                snapshotHooks.discardStaged(snapshotProposal.descriptor);
            } catch (...) {
                // Cleanup is best-effort during an already terminal path.
            }
        }
        for (auto& entry : hostPeers) {
            entry.second.snapshotProducer.reset();
            entry.second.snapshotChunkSent = false;
            entry.second.snapshotChunkLastSentPoll = 0;
        }
        snapshotAssembler.reset();
        haveCompletedSnapshotChunk = false;
        completedSnapshotChunk = RuntimeMultiplayerSnapshotChunk{};
        completedSnapshotAck = RuntimeMultiplayerSnapshotChunkAck{};
        snapshotTransferActive = false;
        snapshotStaged = false;
        snapshotProposal = RuntimeMultiplayerSaveLoadProposal{};
        stagedAuthoritativeStateHash = 0;
        snapshotCommitCompleted = false;
        committedAuthoritativeStateHash = 0;
    }

    bool stageSnapshotBytes(
        const RuntimeMultiplayerSaveLoadProposal& proposal,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) const {
        if (!snapshotHooks.complete()) {
            errorMessage = "complete snapshot hooks are required";
            return false;
        }
        bool accepted = false;
        try {
            accepted = snapshotHooks.stageVerifiedBytes(
                proposal.descriptor,
                bytes,
                observedHash,
                errorMessage
            );
        } catch (...) {
            errorMessage = "multiplayer snapshot stage hook failed";
        }
        if (!accepted) {
            errorMessage = safeSnapshotReason(std::move(errorMessage));
            return false;
        }
        if (observedHash
            != proposal.descriptor.identity.authoritativeStateHash) {
            errorMessage =
                "staged snapshot authoritative hash does not match";
            return false;
        }
        errorMessage.clear();
        return true;
    }

    bool commitSnapshot(
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) const {
        if (!snapshotTransferActive || !snapshotStaged
            || !snapshotHooks.complete()) {
            errorMessage = "no verified staged snapshot is available";
            return false;
        }
        bool accepted = false;
        try {
            accepted = snapshotHooks.commitStaged(
                snapshotProposal.descriptor,
                observedHash,
                errorMessage
            );
        } catch (...) {
            errorMessage = "multiplayer snapshot commit hook failed";
        }
        if (!accepted) {
            errorMessage = safeSnapshotReason(std::move(errorMessage));
            return false;
        }
        if (observedHash
            != snapshotProposal.descriptor.identity
                .authoritativeStateHash) {
            errorMessage =
                "committed snapshot authoritative hash does not match";
            return false;
        }
        errorMessage.clear();
        return true;
    }

    bool postSnapshotSessionCommit(std::string& errorMessage) const {
        if (snapshotProposal.kind != RuntimeMultiplayerSaveLoadKind::Load) {
            errorMessage.clear();
            return true;
        }
        bool accepted = false;
        try {
            accepted = snapshotHooks.postSessionCommit(
                snapshotProposal.descriptor,
                session,
                errorMessage
            );
        } catch (...) {
            errorMessage =
                "multiplayer snapshot post-commit hook failed";
        }
        if (!accepted) {
            errorMessage = safeSnapshotReason(std::move(errorMessage));
            return false;
        }
        errorMessage.clear();
        return true;
    }

    void clearPendingClientCanonicalPackets() {
        pendingClientCanonicalPackets.clear();
        pendingClientCanonicalBytes = 0;
    }

    bool enqueuePendingClientCanonicalPacket(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) {
        const std::size_t packetBytes = deferredCanonicalPacketBytes(packet);
        if (pendingClientCanonicalPackets.size()
                >= runtimeMultiplayerLanMaximumDeferredCanonicalPackets
            || packetBytes
                > runtimeMultiplayerLanMaximumDeferredCanonicalBytes
            || pendingClientCanonicalBytes
                > runtimeMultiplayerLanMaximumDeferredCanonicalBytes
                    - packetBytes) {
            errorMessage =
                "host exceeded deferred canonical packet queue limits";
            return false;
        }
        pendingClientCanonicalPackets.push_back(packet);
        pendingClientCanonicalBytes += packetBytes;
        errorMessage.clear();
        return true;
    }

    void popPendingClientCanonicalPacket() {
        if (pendingClientCanonicalPackets.empty()) return;
        pendingClientCanonicalBytes -= deferredCanonicalPacketBytes(
            pendingClientCanonicalPackets.front()
        );
        pendingClientCanonicalPackets.pop_front();
    }

    bool validateCommandPayload(
        const RuntimeMultiplayerCommandPacket& command,
        std::string& errorMessage
    ) const {
        if (!commandPayloadValidator) {
            errorMessage.clear();
            return true;
        }
        const std::size_t maximumPayloadBytes = session.active()
            ? session.config().maximumCommandPayloadBytes
            : runtimeMultiplayerMaximumCommandPayloadBytes;
        if (command.payload.empty()
            || command.payload.size() > maximumPayloadBytes
            || command.payload.size()
                > runtimeMultiplayerMaximumCommandPayloadBytes) {
            errorMessage =
                "multiplayer command payload length is invalid";
            return false;
        }

        std::string validatorError;
        bool valid = false;
        try {
            valid = commandPayloadValidator(command, validatorError);
        } catch (...) {
            validatorError =
                "multiplayer command payload validator failed";
        }
        if (!valid) {
            errorMessage = safeCommandValidationReason(
                std::move(validatorError)
            );
            return false;
        }
        errorMessage.clear();
        return true;
    }

    bool notifySimulationStarted(std::string& errorMessage) const {
        if (!simulationHooks.onStarted) {
            errorMessage.clear();
            return true;
        }
        bool accepted = false;
        try {
            accepted = simulationHooks.onStarted(session, errorMessage);
        } catch (...) {
            errorMessage = "multiplayer simulation start hook failed";
        }
        if (!accepted) {
            errorMessage = safeSimulationReason(std::move(errorMessage));
            return false;
        }
        errorMessage.clear();
        return true;
    }

    bool notifyCommandAdmitted(
        const RuntimeMultiplayerCommandPacket& command,
        std::string& errorMessage
    ) const {
        if (!simulationHooks.onCommandAdmitted) {
            errorMessage.clear();
            return true;
        }
        bool accepted = false;
        try {
            accepted = simulationHooks.onCommandAdmitted(
                command,
                errorMessage
            );
        } catch (...) {
            errorMessage = "multiplayer command-admitted hook failed";
        }
        if (!accepted) {
            errorMessage = safeSimulationReason(std::move(errorMessage));
            return false;
        }
        errorMessage.clear();
        return true;
    }

    RuntimeMultiplayerLanSimulationAdvanceDecision prepareSimulationAdvance(
        int completedTick,
        std::string& errorMessage
    ) const {
        if (!simulationHooks.prepareAuthoritativeAdvance) {
            errorMessage.clear();
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
        }
        RuntimeMultiplayerLanSimulationAdvanceDecision decision =
            RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
        try {
            decision = simulationHooks.prepareAuthoritativeAdvance(
                completedTick,
                errorMessage
            );
        } catch (...) {
            errorMessage = "multiplayer simulation advance hook failed";
        }
        switch (decision) {
        case RuntimeMultiplayerLanSimulationAdvanceDecision::Ready:
            errorMessage.clear();
            return decision;
        case RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred:
            if (!errorMessage.empty()) {
                errorMessage = safeSimulationReason(
                    std::move(errorMessage)
                );
            }
            return decision;
        case RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected:
            errorMessage = safeSimulationReason(std::move(errorMessage));
            return decision;
        }
        errorMessage = "multiplayer simulation advance decision is invalid";
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
    }

    bool notifyAuthoritativeAdvance(
        int completedTick,
        std::string& errorMessage
    ) const {
        if (!simulationHooks.onAuthoritativeAdvance) {
            errorMessage.clear();
            return true;
        }
        bool accepted = false;
        try {
            accepted = simulationHooks.onAuthoritativeAdvance(
                completedTick,
                errorMessage
            );
        } catch (...) {
            errorMessage = "multiplayer advance-acknowledgement hook failed";
        }
        if (!accepted) {
            errorMessage = safeSimulationReason(std::move(errorMessage));
            return false;
        }
        errorMessage.clear();
        return true;
    }

    RuntimeMultiplayerLanSimulationAdvanceDecision
    prepareSimulationMessage(
        const RuntimeMultiplayerMessage& message,
        std::string& errorMessage
    ) const {
        if (message.type
            != RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick
            || !simulationHooks.prepareAuthoritativeAdvance) {
            errorMessage.clear();
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
        }
        const RuntimeMultiplayerAdvanceTickMessage& advance =
            std::get<RuntimeMultiplayerAdvanceTickMessage>(message.body);
        if (advance.authoritativeTick == session.authoritativeTick()) {
            errorMessage.clear();
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
        }
        const std::int64_t expected =
            static_cast<std::int64_t>(session.authoritativeTick()) + 1LL;
        if (expected > runtimeMultiplayerMaximumTick
            || static_cast<std::int64_t>(advance.authoritativeTick)
                != expected) {
            errorMessage =
                "hooked authoritative advance must move exactly one tick";
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
        }
        return prepareSimulationAdvance(
            advance.authoritativeTick - 1,
            errorMessage
        );
    }

    bool notifySimulationMessageApplied(
        const RuntimeMultiplayerMessage& message,
        std::string& errorMessage
    ) const {
        if (message.type == RuntimeMultiplayerMessageType::Start) {
            return notifySimulationStarted(errorMessage);
        }
        if (message.type == RuntimeMultiplayerMessageType::Command) {
            return notifyCommandAdmitted(
                std::get<RuntimeMultiplayerCommandPacket>(message.body),
                errorMessage
            );
        }
        if (message.type
            == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick) {
            const RuntimeMultiplayerAdvanceTickMessage& advance =
                std::get<RuntimeMultiplayerAdvanceTickMessage>(message.body);
            return notifyAuthoritativeAdvance(
                advance.authoritativeTick - 1,
                errorMessage
            );
        }
        errorMessage.clear();
        return true;
    }

    bool observesSimulationMessage(RuntimeMultiplayerMessageType type) const {
        return (type == RuntimeMultiplayerMessageType::Start
                && static_cast<bool>(simulationHooks.onStarted))
            || (type == RuntimeMultiplayerMessageType::Command
                && static_cast<bool>(simulationHooks.onCommandAdmitted))
            || (type
                    == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick
                && static_cast<bool>(
                    simulationHooks.onAuthoritativeAdvance
                ));
    }

    std::uint64_t makeNonce() {
        std::uint64_t entropy = 0;
        try {
            std::random_device random;
            entropy = static_cast<std::uint64_t>(random());
            entropy = (entropy << 32U)
                ^ static_cast<std::uint64_t>(random());
        } catch (...) {
            entropy = 0;
        }
        const auto now = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        std::uint64_t value = entropy
            ^ static_cast<std::uint64_t>(now)
            ^ (++nonceCounter * 0x9e3779b97f4a7c15ULL);
        value ^= value >> 30U;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27U;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31U;
        return value == 0 ? nonceCounter : value;
    }

    std::uint64_t makeUniqueAdmissionToken() {
        for (;;) {
            const std::uint64_t candidate = makeNonce();
            const bool used = std::any_of(
                hostPeers.begin(),
                hostPeers.end(),
                [candidate](const auto& entry) {
                    return entry.second.admissionToken == candidate;
                }
            );
            if (!used && candidate != roomGeneration) return candidate;
        }
    }

    void reset() {
        clearSnapshotTransfer(true);
        resetLobbyMapTransfer();
        lobbyResumeImage.reset();
        lobbyResumeBytes.clear();
        lobbyResumeAssembler.reset();
        lobbyResumeLastChunk.reset();
        lobbyResumeStartedAt = {};
        discoveryHost.close();
        browser.close();
        server.close();
        client.close();
        session.reset();
        advertisement = RuntimeLanRoomAdvertisement{};
        joinOptions = RuntimeMultiplayerLanJoinOptions{};
        maximumCombatants = runtimeMultiplayerMaximumCombatants;
        maximumSpectators = runtimeMultiplayerMaximumSpectators;
        hostPeers.clear();
        suspendedPeers.clear();
        reconnectDeferredHostPackets.clear();
        reconnectDeferredHostBytes = 0;
        reconnectCanonicalHistory.clear();
        reconnectCanonicalHistoryBytes = 0;
        pendingEvents.clear();
        clearPendingClientCanonicalPackets();
        localPeerId.clear();
        runtimeRoomPasswordSecureClear(hostPasswordVerifier);
        runtimeRoomPasswordSecureClear(joinPasswordVerifier);
        role = RuntimeMultiplayerLanRole::None;
        phase = RuntimeMultiplayerLanPhase::Inactive;
        roomGeneration = 0;
        admissionToken = 0;
        nextLobbySlotRequestId = 1;
        lastLobbySlotRequestIdSent = 0;
        boundDiscoveryPort = 0;
        boundSessionPort = 0;
        reconnectGraceMilliseconds = 0;
        peerReconnectGraceMilliseconds = 0;
        challengeReceived = false;
        joinAccepted = false;
        hostPasswordProtected = false;
        joinPasswordProofRequired = false;
        clientCanonicalPacketDeferred = false;
        snapshotSendPollCounter = 0;
        reconnectRecoveryPending = false;
        reconnectRecoveryProposalId = 0;
        reconnectDeadline = {};
        nextReconnectAttempt = {};
        reconnectAttemptCount = 0;
        // commandPayloadValidator is application configuration and persists.
        // simulationHooks are application configuration and persist.
        // snapshotHooks are application configuration and persist.
    }

    RuntimeMultiplayerLanEvent event(
        RuntimeMultiplayerLanEventKind kind,
        RuntimeMultiplayerMessageType type =
            RuntimeMultiplayerMessageType::JoinRequest
    ) const {
        RuntimeMultiplayerLanEvent result;
        result.kind = kind;
        result.messageType = type;
        return result;
    }

    bool encode(
        const RuntimeMultiplayerMessage& message,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const {
        return encodeRuntimeMultiplayerMessage(
            message,
            packet,
            errorMessage
        );
    }

    bool sendToHost(
        const RuntimeMultiplayerMessage& message,
        std::string& errorMessage
    ) {
        RuntimeNetworkPacket packet;
        return encode(message, packet, errorMessage)
            && client.send(packet, errorMessage);
    }

    bool sendToPeer(
        std::uint64_t transportPeerId,
        const RuntimeMultiplayerMessage& message,
        std::string& errorMessage
    ) {
        RuntimeNetworkPacket packet;
        return encode(message, packet, errorMessage)
            && server.send(transportPeerId, packet, errorMessage);
    }

    void retainReconnectCanonicalCheckpoint(const RuntimeNetworkPacket& packet) {
        if (role != RuntimeMultiplayerLanRole::Host
            || session.status() != RuntimeMultiplayerRoomStatus::Running) return;
        // Snapshot chunks use a separate acknowledged transfer. A retained
        // event prefix cannot cross that transfer; begin a fresh history once
        // every participant has completed the save/load handshake.
        if (session.hasActiveSaveLoadHandshake()) {
            reconnectCanonicalHistory.clear();
            reconnectCanonicalHistoryBytes = 0;
            return;
        }
        const auto epoch = session.timelineEpoch();
        if (!reconnectCanonicalHistory.empty()
            && reconnectCanonicalHistory.back().timelineEpoch != epoch) {
            reconnectCanonicalHistory.clear();
            reconnectCanonicalHistoryBytes = 0;
        }
        const auto hash = session.stateHash();
        if (!reconnectCanonicalHistory.empty()
            && reconnectCanonicalHistory.back().sessionStateHash == hash) return;
        const auto bytes = deferredCanonicalPacketBytes(packet);
        reconnectCanonicalHistory.push_back({epoch, session.authoritativeTick(), hash, packet});
        reconnectCanonicalHistoryBytes += bytes;
        while (reconnectCanonicalHistory.size() > runtimeMultiplayerLanMaximumDeferredCanonicalPackets
            || reconnectCanonicalHistoryBytes > runtimeMultiplayerLanMaximumDeferredCanonicalBytes) {
            reconnectCanonicalHistoryBytes -= deferredCanonicalPacketBytes(reconnectCanonicalHistory.front().packet);
            reconnectCanonicalHistory.pop_front();
        }
    }

    bool collectReconnectCanonicalCatchUp(
        const RuntimeMultiplayerJoinAdmissionRequestMessage& admission,
        std::vector<RuntimeNetworkPacket>& packets
    ) const {
        const auto hash = session.stateHash();
        if (admission.reconnectTimelineEpoch != session.timelineEpoch()) return false;
        if (admission.reconnectAuthoritativeTick == session.authoritativeTick()
            && admission.reconnectSessionStateHash == hash) return true;
        if (reconnectCanonicalHistory.empty()
            || reconnectCanonicalHistory.back().sessionStateHash != hash) return false;
        const auto checkpoint = std::find_if(reconnectCanonicalHistory.rbegin(), reconnectCanonicalHistory.rend(),
            [&](const auto& entry) {
                return entry.timelineEpoch == admission.reconnectTimelineEpoch
                    && entry.authoritativeTick == admission.reconnectAuthoritativeTick
                    && entry.sessionStateHash == admission.reconnectSessionStateHash;
            });
        if (checkpoint == reconnectCanonicalHistory.rend()) return false;
        // The hash must identify an exact retained prefix. Replay only the
        // host's already accepted canonical suffix before the shared Load.
        for (auto entry = checkpoint.base(); entry != reconnectCanonicalHistory.end(); ++entry)
            packets.push_back(entry->packet);
        return true;
    }

    bool broadcastPacket(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) {
        retainReconnectCanonicalCheckpoint(packet);
        bool success = true;
        errorMessage.clear();
        for (const auto& entry : hostPeers) {
            if (!entry.second.admitted) continue;
            std::string peerError;
            if (!server.send(entry.first, packet, peerError)) {
                success = false;
                if (errorMessage.empty()) errorMessage = std::move(peerError);
            }
        }
        return success;
    }

    bool broadcastMessage(
        const RuntimeMultiplayerMessage& message,
        std::string& errorMessage
    ) {
        RuntimeNetworkPacket packet;
        return encode(message, packet, errorMessage)
            && broadcastPacket(packet, errorMessage);
    }

    bool updateAdvertisement(std::string& errorMessage) {
        if (!discoveryHost.isOpen()) return true;
        const std::size_t memberCount = static_cast<std::size_t>(
            std::count_if(
                session.members().begin(),
                session.members().end(),
                [](const RuntimeMultiplayerRoomMember& member) {
                    return member.participation
                        == RuntimeMultiplayerParticipationRole::Combatant;
                }
            )
        );
        advertisement.playerCount = static_cast<std::uint8_t>(
            std::min<std::size_t>(
                memberCount,
                std::numeric_limits<std::uint8_t>::max()
            )
        );
        return discoveryHost.updateAdvertisement(
            advertisement,
            errorMessage
        );
    }

    RuntimeMultiplayerLobbySnapshotMessage lobbySnapshot() const {
        RuntimeMultiplayerLobbySnapshotMessage snapshot;
        snapshot.roomGeneration = roomGeneration;
        snapshot.snapshot.config = session.config();
        snapshot.snapshot.members = session.members();
        snapshot.snapshot.latestMatchSetupRevision =
            session.latestMatchSetupRevision();
        snapshot.snapshot.matchSetup = session.matchSetup();
        return snapshot;
    }

    bool broadcastLobbySnapshot(std::string& errorMessage) {
        RuntimeMultiplayerMessage message;
        message.type = RuntimeMultiplayerMessageType::LobbySnapshot;
        message.body = lobbySnapshot();
        return broadcastMessage(message, errorMessage);
    }

    std::uint64_t currentLobbySetupRevision() const {
        return session.matchSetup().has_value()
            ? session.matchSetup()->revision
            : session.latestMatchSetupRevision();
    }

    bool sendLobbySlotOptionsResult(
        std::uint64_t transportPeerId,
        std::uint64_t requestId,
        bool accepted,
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    ) {
        const auto peer = hostPeers.find(transportPeerId);
        if (peer == hostPeers.end() || peer->second.logicalPeerId.empty()) {
            return false;
        }
        RuntimeMultiplayerLobbySlotOptionsResultMessage response;
        response.peerId = peer->second.logicalPeerId;
        response.requestId = requestId;
        response.accepted = accepted;
        response.currentSetupRevision = currentLobbySetupRevision();
        if (!accepted) {
            response.reason = safeLobbyResultReason(
                std::move(reason),
                "lobby slot request was rejected"
            );
        }
        RuntimeMultiplayerMessage message;
        message.type =
            RuntimeMultiplayerMessageType::LobbySlotOptionsResult;
        message.body = response;
        std::string sendError;
        const bool sent = sendToPeer(
            transportPeerId,
            message,
            sendError
        );
        RuntimeMultiplayerLanEvent result = event(
            RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult,
            message.type
        );
        result.peerId = response.peerId;
        result.reason = sent ? response.reason : std::move(sendError);
        result.sequence = requestId;
        result.lobbySlotOptionsResult = std::move(response);
        events.push_back(std::move(result));
        return sent;
    }

    void appendAppliedEvent(
        const RuntimeMultiplayerMessage& message,
        const RuntimeMultiplayerMessageApplyResult& applied,
        std::vector<RuntimeMultiplayerLanEvent>& events
    ) const;

    void scheduleClose(std::uint64_t transportPeerId) {
        const auto found = hostPeers.find(transportPeerId);
        if (found != hostPeers.end()) found->second.closeAfterPolls = 2;
    }

    void rejectUnadmittedPeer(
        std::uint64_t transportPeerId,
        const std::string& claimedPeerId,
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    void handleHostPeerGone(
        std::uint64_t transportPeerId,
        RuntimeMultiplayerSessionAbortReason abortReason,
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    void handleHostPacket(
        std::uint64_t transportPeerId,
        const RuntimeNetworkPacket& packet,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    void handleClientPacket(
        const RuntimeNetworkPacket& packet,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    bool beginClientReconnect(
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );
    void failClientReconnect(
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );
    void drainReconnectDeferredHostPackets(
        std::vector<RuntimeMultiplayerLanEvent>& events
    );
    void refreshReconnectRecoveryState();

    void clientProtocolFailure(
        std::string reason,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    RuntimeMultiplayerLanOperationResult applyHostMessage(
        RuntimeMultiplayerMessage message
    );

    RuntimeMultiplayerLanOperationResult issueHostSnapshotCommit();
    void reconcileSnapshotAfterSpectatorDeparture();
    void handleClientSnapshotChunk(
        const RuntimeMultiplayerSnapshotChunk& chunk,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );
    void handleClientSnapshotCommit(
        const RuntimeNetworkPacket& packet,
        const RuntimeMultiplayerMessage& decoded,
        std::vector<RuntimeMultiplayerLanEvent>& events
    );
    void finishSnapshotTerminalState();
    void sendHostSnapshotChunks(
        std::vector<RuntimeMultiplayerLanEvent>& events
    );

    RuntimeMultiplayerLanOperationResult hostAbort(
        const std::string& affectedPeerId,
        RuntimeMultiplayerSessionAbortReason abortReason
    );

    RuntimeMultiplayerLanPollResult pollHost(int timeoutMilliseconds);
    RuntimeMultiplayerLanPollResult pollClient(int timeoutMilliseconds);
    RuntimeMultiplayerLanPollResult pollBrowser(int timeoutMilliseconds);

#include "network/lobby/RuntimeMultiplayerLobbyResumeTransfer.inl"
#include "network/lobby/RuntimeMultiplayerLobbyMapTransfer.inl"
};

void RuntimeMultiplayerLanSession::Impl::appendAppliedEvent(
    const RuntimeMultiplayerMessage& message,
    const RuntimeMultiplayerMessageApplyResult& applied,
    std::vector<RuntimeMultiplayerLanEvent>& events
) const {
    RuntimeMultiplayerLanEvent result;
    result.messageType = message.type;
    result.reason = applied.reason;
    switch (message.type) {
    case RuntimeMultiplayerMessageType::JoinRequest:
        result.kind = RuntimeMultiplayerLanEventKind::LobbyChanged;
        result.peerId =
            std::get<RuntimeMultiplayerJoinRequest>(message.body).peerId;
        break;
    case RuntimeMultiplayerMessageType::Leave:
        result.kind = RuntimeMultiplayerLanEventKind::MemberLeft;
        result.peerId =
            std::get<RuntimeMultiplayerLeaveMessage>(message.body).peerId;
        break;
    case RuntimeMultiplayerMessageType::Ready:
        result.kind = RuntimeMultiplayerLanEventKind::ReadyChanged;
        result.peerId =
            std::get<RuntimeMultiplayerReadyMessage>(message.body).peerId;
        break;
    case RuntimeMultiplayerMessageType::Start: {
        const RuntimeMultiplayerStartMessage& start =
            std::get<RuntimeMultiplayerStartMessage>(message.body);
        result.kind = RuntimeMultiplayerLanEventKind::Started;
        result.peerId = start.requestingPeerId;
        result.tick = start.startTick;
        break;
    }
    case RuntimeMultiplayerMessageType::Command: {
        const RuntimeMultiplayerCommandPacket& command =
            std::get<RuntimeMultiplayerCommandPacket>(message.body);
        result.kind = RuntimeMultiplayerLanEventKind::CommandAccepted;
        result.peerId = command.peerId;
        result.tick = command.tick;
        result.sequence = command.sequence;
        break;
    }
    case RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick: {
        const RuntimeMultiplayerAdvanceTickMessage& advance =
            std::get<RuntimeMultiplayerAdvanceTickMessage>(message.body);
        result.kind =
            RuntimeMultiplayerLanEventKind::AuthoritativeTickAdvanced;
        result.peerId = advance.requestingPeerId;
        result.tick = advance.authoritativeTick;
        break;
    }
    case RuntimeMultiplayerMessageType::StateHash: {
        const RuntimeMultiplayerStateHashReport& report =
            std::get<RuntimeMultiplayerStateHashReport>(message.body);
        result.kind = RuntimeMultiplayerLanEventKind::StateHashAccepted;
        result.peerId = report.peerId;
        result.tick = report.tick;
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadProposal:
    case RuntimeMultiplayerMessageType::SaveLoadResponse:
    case RuntimeMultiplayerMessageType::SaveLoadAbort:
    case RuntimeMultiplayerMessageType::SaveLoadCommit:
    case RuntimeMultiplayerMessageType::SaveLoadApplied:
        result.kind = RuntimeMultiplayerLanEventKind::SaveLoadUpdated;
        break;
    case RuntimeMultiplayerMessageType::RoomConfig:
    case RuntimeMultiplayerMessageType::LobbySnapshot:
        result.kind = RuntimeMultiplayerLanEventKind::LobbyChanged;
        break;
    case RuntimeMultiplayerMessageType::SessionAbort: {
        const RuntimeMultiplayerSessionAbortMessage& abort =
            std::get<RuntimeMultiplayerSessionAbortMessage>(message.body);
        result.kind = RuntimeMultiplayerLanEventKind::SessionAborted;
        result.peerId = abort.abort.affectedPeerId;
        result.tick = abort.abort.tick;
        break;
    }
    default:
        return;
    }
    events.push_back(std::move(result));
}

void RuntimeMultiplayerLanSession::Impl::rejectUnadmittedPeer(
    std::uint64_t transportPeerId,
    const std::string& claimedPeerId,
    std::string reason,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    reason = safeReason(std::move(reason));
    RuntimeMultiplayerJoinRejectedMessage rejected;
    rejected.roomId = session.active()
        ? session.config().roomId
        : advertisement.roomId;
    rejected.peerId = claimedPeerId;
    rejected.reason = reason;
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::JoinRejected;
    message.body = std::move(rejected);
    std::string ignored;
    sendToPeer(transportPeerId, message, ignored);
    scheduleClose(transportPeerId);

    RuntimeMultiplayerLanEvent eventResult = event(
        RuntimeMultiplayerLanEventKind::JoinRejected,
        RuntimeMultiplayerMessageType::JoinRejected
    );
    eventResult.peerId = claimedPeerId;
    eventResult.reason = std::move(reason);
    events.push_back(std::move(eventResult));
}

bool RuntimeMultiplayerLanSession::Impl::beginClientReconnect(
    std::string reason,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    if (role != RuntimeMultiplayerLanRole::Client
        || phase != RuntimeMultiplayerLanPhase::Running
        || session.status() != RuntimeMultiplayerRoomStatus::Running
        || peerReconnectGraceMilliseconds == 0
        || session.hasActiveSaveLoadHandshake()) {
        return false;
    }
    const auto member = std::find_if(
        session.members().begin(),
        session.members().end(),
        [this](const RuntimeMultiplayerRoomMember& candidate) {
            return candidate.peerId == localPeerId;
        }
    );
    if (member == session.members().end()
        || member->participation
            != RuntimeMultiplayerParticipationRole::Combatant) {
        return false;
    }

    client.close();
    // These packets have not entered SessionState. The host replays them
    // from the exact checkpoint reported during admission.
    clearPendingClientCanonicalPackets();
    phase = RuntimeMultiplayerLanPhase::Reconnecting;
    challengeReceived = false;
    joinAccepted = false;
    admissionToken = 0;
    reconnectRecoveryPending = true;
    reconnectRecoveryProposalId = 0;
    reconnectAttemptCount = 0;
    const auto now = std::chrono::steady_clock::now();
    reconnectDeadline = now + std::chrono::milliseconds(
        peerReconnectGraceMilliseconds
    );
    nextReconnectAttempt = now + std::chrono::milliseconds(
        runtimeMultiplayerLanReconnectRetryMilliseconds
    );

    RuntimeMultiplayerLanEvent pending = event(
        RuntimeMultiplayerLanEventKind::ReconnectPending
    );
    pending.peerId = localPeerId;
    pending.tick = session.authoritativeTick();
    pending.reason = reason.empty()
        ? "client transport interrupted; reconnecting"
        : std::move(reason);
    events.push_back(std::move(pending));
    return true;
}

void RuntimeMultiplayerLanSession::Impl::failClientReconnect(
    std::string reason,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    reason = safeReason(std::move(reason));
    if (session.status() == RuntimeMultiplayerRoomStatus::Running) {
        RuntimeMultiplayerSessionAbort abort;
        abort.requestingPeerId = session.config().hostPeerId;
        abort.affectedPeerId = localPeerId;
        abort.tick = session.authoritativeTick();
        abort.reason = RuntimeMultiplayerSessionAbortReason::TransportFailure;
        (void)session.abortRunningSession(std::move(abort));
    }
    phase = RuntimeMultiplayerLanPhase::Aborted;
    client.close();
    clearPendingClientCanonicalPackets();
    clearSnapshotTransfer(true);
    reconnectRecoveryPending = false;
    reconnectRecoveryProposalId = 0;

    RuntimeMultiplayerLanEvent failed = event(
        RuntimeMultiplayerLanEventKind::ReconnectFailed
    );
    failed.peerId = localPeerId;
    failed.tick = session.authoritativeTick();
    failed.reason = reason;
    events.push_back(std::move(failed));

    RuntimeMultiplayerLanEvent aborted = event(
        RuntimeMultiplayerLanEventKind::SessionAborted,
        RuntimeMultiplayerMessageType::SessionAbort
    );
    aborted.peerId = localPeerId;
    aborted.tick = session.authoritativeTick();
    aborted.reason = std::move(reason);
    events.push_back(std::move(aborted));
}

void RuntimeMultiplayerLanSession::Impl::drainReconnectDeferredHostPackets(
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    std::deque<DeferredHostPacket> deferred;
    deferred.swap(reconnectDeferredHostPackets);
    reconnectDeferredHostBytes = 0;
    for (DeferredHostPacket& item : deferred) {
        if (phase != RuntimeMultiplayerLanPhase::Running
            || !suspendedPeers.empty()
            || hostPeers.find(item.transportPeerId) == hostPeers.end()) {
            break;
        }
        handleHostPacket(item.transportPeerId, item.packet, events);
    }
}

void RuntimeMultiplayerLanSession::Impl::refreshReconnectRecoveryState() {
    if (!reconnectRecoveryPending || reconnectRecoveryProposalId == 0) {
        return;
    }
    const auto& handshakes = session.saveLoadHandshakes();
    const auto found = std::find_if(
        handshakes.rbegin(),
        handshakes.rend(),
        [this](const RuntimeMultiplayerSaveLoadHandshake& handshake) {
            return handshake.proposal.proposalId
                == reconnectRecoveryProposalId;
        }
    );
    if (found != handshakes.rend()
        && found->status == RuntimeMultiplayerSaveLoadStatus::Committed) {
        reconnectRecoveryPending = false;
        reconnectRecoveryProposalId = 0;
    }
    if (session.status() == RuntimeMultiplayerRoomStatus::Aborted) {
        reconnectRecoveryPending = false;
        reconnectRecoveryProposalId = 0;
    }
}

void RuntimeMultiplayerLanSession::Impl::handleHostPeerGone(
    std::uint64_t transportPeerId,
    RuntimeMultiplayerSessionAbortReason abortReason,
    std::string reason,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    const auto found = hostPeers.find(transportPeerId);
    if (found == hostPeers.end()) return;
    HostPeer peer = found->second;
    hostPeers.erase(found);

    const auto roomMember = std::find_if(
        session.members().begin(),
        session.members().end(),
        [&peer](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == peer.logicalPeerId;
        }
    );
    const bool runningSpectator = peer.admitted
        && session.status() == RuntimeMultiplayerRoomStatus::Running
        && roomMember != session.members().end()
        && roomMember->role == RuntimeMultiplayerRoomMemberRole::Client
        && roomMember->participation
            == RuntimeMultiplayerParticipationRole::Spectator;
    const bool runningCombatant = peer.admitted
        && session.status() == RuntimeMultiplayerRoomStatus::Running
        && roomMember != session.members().end()
        && roomMember->role == RuntimeMultiplayerRoomMemberRole::Client
        && roomMember->participation
            == RuntimeMultiplayerParticipationRole::Combatant;
    const bool recoverableTransportLoss = abortReason
            == RuntimeMultiplayerSessionAbortReason::PeerDisconnected
        || abortReason
            == RuntimeMultiplayerSessionAbortReason::TransportFailure;
    if (runningCombatant && recoverableTransportLoss
        && reconnectGraceMilliseconds > 0
        && !session.hasActiveSaveLoadHandshake()
        && !reconnectRecoveryPending) {
        SuspendedPeer suspended;
        suspended.logicalPeerId = peer.logicalPeerId;
        suspended.displayName = roomMember->displayName;
        suspended.participation = roomMember->participation;
        suspended.timelineEpoch = session.timelineEpoch();
        suspended.authoritativeTick = session.authoritativeTick();
        suspended.sessionStateHash = session.stateHash();
        suspended.deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(reconnectGraceMilliseconds);
        suspended.disconnectReason = reason;
        suspendedPeers[suspended.logicalPeerId] = std::move(suspended);

        RuntimeMultiplayerLanEvent pending = event(
            RuntimeMultiplayerLanEventKind::ReconnectPending
        );
        pending.peerId = peer.logicalPeerId;
        pending.tick = session.authoritativeTick();
        pending.reason = reason.empty()
            ? "combatant transport interrupted; awaiting reconnect"
            : reason;
        events.push_back(std::move(pending));
    } else if (peer.admitted
        && (session.status() == RuntimeMultiplayerRoomStatus::Lobby
            || runningSpectator)) {
        RuntimeMultiplayerLeaveMessage leave;
        leave.peerId = peer.logicalPeerId;
        RuntimeMultiplayerMessage leaveMessage;
        leaveMessage.type = RuntimeMultiplayerMessageType::Leave;
        leaveMessage.body = std::move(leave);
        RuntimeNetworkPacket leavePacket;
        std::string ignored;
        RuntimeMultiplayerMessageApplyResult applied;
        if (encode(leaveMessage, leavePacket, ignored)) {
            applied = applyTrustedCanonicalRuntimeMultiplayerMessage(
                leavePacket,
                session
            );
        }
        if (applied.accepted) {
            broadcastPacket(leavePacket, ignored);
            appendAppliedEvent(leaveMessage, applied, events);
            if (session.status() == RuntimeMultiplayerRoomStatus::Lobby) {
                broadcastLobbySnapshot(ignored);
                updateAdvertisement(ignored);
            } else {
                reconcileSnapshotAfterSpectatorDeparture();
            }
        } else if (runningSpectator) {
            (void)hostAbort(
                peer.logicalPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            );
        }
    } else if (peer.admitted
        && session.status() == RuntimeMultiplayerRoomStatus::Running) {
        RuntimeMultiplayerSessionAbortMessage abortMessage;
        abortMessage.roomGeneration = roomGeneration;
        abortMessage.abort.requestingPeerId = session.config().hostPeerId;
        abortMessage.abort.affectedPeerId = peer.logicalPeerId;
        abortMessage.abort.tick = session.authoritativeTick();
        abortMessage.abort.reason = abortReason;
        RuntimeMultiplayerMessage message;
        message.type = RuntimeMultiplayerMessageType::SessionAbort;
        message.body = abortMessage;
        RuntimeNetworkPacket packet;
        std::string ignored;
        if (encode(message, packet, ignored)) {
            RuntimeMultiplayerMessageApplyResult applied =
                applyTrustedCanonicalRuntimeMultiplayerMessage(
                    packet,
                    session
                );
            if (applied.accepted) {
                broadcastPacket(packet, ignored);
                phase = RuntimeMultiplayerLanPhase::Aborted;
                discoveryHost.close();
                clearSnapshotTransfer(true);
                appendAppliedEvent(message, applied, events);
            }
        }
    }

    // A host kick already has a canonical LobbyKicked event.  Do not append a
    // second generic disconnect event: Internet relay users need to keep the
    // authenticated route alive long enough to drain the queued kick and
    // final lobby frames before the loopback connection closes.
    if (!peer.kicked) {
        RuntimeMultiplayerLanEvent disconnected = event(
            RuntimeMultiplayerLanEventKind::Disconnected
        );
        disconnected.peerId = peer.logicalPeerId;
        disconnected.reason = std::move(reason);
        events.push_back(std::move(disconnected));
    }
}

void RuntimeMultiplayerLanSession::Impl::handleHostPacket(
    std::uint64_t transportPeerId,
    const RuntimeNetworkPacket& packet,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    auto found = hostPeers.find(transportPeerId);
    if (found == hostPeers.end()) return;

    RuntimeMultiplayerMessage decoded;
    std::string decodeError;
    if (!decodeRuntimeMultiplayerMessage(packet, decoded, decodeError)) {
        if (!found->second.admitted) {
            rejectUnadmittedPeer(
                transportPeerId,
                {},
                decodeError,
                events
            );
        } else {
            RuntimeMultiplayerLanEvent rejected = event(
                RuntimeMultiplayerLanEventKind::ProtocolRejected
            );
            rejected.peerId = found->second.logicalPeerId;
            rejected.reason = decodeError;
            events.push_back(std::move(rejected));
            server.disconnect(transportPeerId);
            handleHostPeerGone(
                transportPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                decodeError,
                events
            );
        }
        return;
    }

    if (decoded.type
        == RuntimeMultiplayerMessageType::JoinAdmissionRequest) {
        const RuntimeMultiplayerJoinAdmissionRequestMessage& admission =
            std::get<RuntimeMultiplayerJoinAdmissionRequestMessage>(
                decoded.body
            );
        HostPeer& binding = found->second;
        const std::uint64_t expectedAdmission = hostPasswordProtected
            ? runtimeLanRoomPasswordAdmissionProof(
                hostPasswordVerifier,
                session.config().roomId,
                admission.request.peerId,
                roomGeneration,
                binding.admissionToken
            )
            : binding.admissionToken;
        if (admission.roomGeneration != roomGeneration
            || admission.admissionToken != expectedAdmission) {
            const std::string rejection = hostPasswordProtected
                ? "LAN room password or admission challenge is incorrect"
                : "multiplayer admission challenge does not match";
            if (!binding.admitted) {
                rejectUnadmittedPeer(
                    transportPeerId,
                    admission.request.peerId,
                    rejection,
                    events
                );
            } else {
                RuntimeMultiplayerLanEvent rejected = event(
                    RuntimeMultiplayerLanEventKind::ProtocolRejected,
                    decoded.type
                );
                rejected.peerId = binding.logicalPeerId;
                rejected.reason = rejection;
                events.push_back(std::move(rejected));
                server.disconnect(transportPeerId);
                handleHostPeerGone(
                    transportPeerId,
                    RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                    "admission challenge mismatch",
                    events
                );
            }
            return;
        }

        if (admission.reconnect) {
            const auto suspended = suspendedPeers.find(
                admission.request.peerId
            );
            const auto member = std::find_if(
                session.members().begin(),
                session.members().end(),
                [&admission](const RuntimeMultiplayerRoomMember& candidate) {
                    return candidate.peerId == admission.request.peerId;
                }
            );
            std::vector<RuntimeNetworkPacket> catchUpPackets;
            const bool checkpointMatches =
                phase == RuntimeMultiplayerLanPhase::Running
                && !binding.admitted
                && suspended != suspendedPeers.end()
                && member != session.members().end()
                && member->role == RuntimeMultiplayerRoomMemberRole::Client
                && member->displayName == admission.request.displayName
                && member->participation == admission.request.participation
                && admission.request.compatibility
                    == session.config().compatibility
                && suspended->second.displayName
                    == admission.request.displayName
                && suspended->second.participation
                    == admission.request.participation
                && admission.reconnectTimelineEpoch
                    == suspended->second.timelineEpoch
                && admission.reconnectTimelineEpoch
                    == session.timelineEpoch()
                && suspended->second.authoritativeTick == session.authoritativeTick()
                && suspended->second.sessionStateHash == session.stateHash()
                && collectReconnectCanonicalCatchUp(admission, catchUpPackets);
            if (!checkpointMatches) {
                rejectUnadmittedPeer(
                    transportPeerId,
                    admission.request.peerId,
                    "multiplayer reconnect checkpoint does not match the host",
                    events
                );
                if (suspended != suspendedPeers.end()) {
                    RuntimeMultiplayerLanEvent failed = event(
                        RuntimeMultiplayerLanEventKind::ReconnectFailed
                    );
                    failed.peerId = admission.request.peerId;
                    failed.tick = session.authoritativeTick();
                    failed.reason =
                        "reconnect checkpoint mismatch; ending the room";
                    events.push_back(std::move(failed));
                    (void)hostAbort(
                        admission.request.peerId,
                        RuntimeMultiplayerSessionAbortReason::TransportFailure
                    );
                }
                return;
            }

            binding.logicalPeerId = admission.request.peerId;
            binding.admitted = true;
            binding.admissionToken = 0;
            binding.closeAfterPolls = -1;
            suspendedPeers.erase(suspended);
            reconnectRecoveryPending = true;
            reconnectRecoveryProposalId = 0;

            RuntimeMultiplayerJoinAcceptedMessage accepted;
            accepted.roomGeneration = roomGeneration;
            accepted.roomId = session.config().roomId;
            accepted.peerId = binding.logicalPeerId;
            accepted.reconnected = true;
            RuntimeMultiplayerMessage acceptedMessage;
            acceptedMessage.type = RuntimeMultiplayerMessageType::JoinAccepted;
            acceptedMessage.body = std::move(accepted);
            std::string sendError;
            bool reconnectQueued = sendToPeer(
                    transportPeerId,
                    acceptedMessage,
                    sendError
                );
            for (const auto& catchUp : catchUpPackets) {
                if (!reconnectQueued) break;
                reconnectQueued = server.send(transportPeerId, catchUp, sendError);
            }
            if (!reconnectQueued) {
                server.disconnect(transportPeerId);
                handleHostPeerGone(
                    transportPeerId,
                    RuntimeMultiplayerSessionAbortReason::TransportFailure,
                    sendError,
                    events
                );
                return;
            }

            RuntimeMultiplayerLanEvent reconnected = event(
                RuntimeMultiplayerLanEventKind::Reconnected,
                RuntimeMultiplayerMessageType::JoinAccepted
            );
            reconnected.peerId = binding.logicalPeerId;
            reconnected.tick = session.authoritativeTick();
            events.push_back(std::move(reconnected));
            if (suspendedPeers.empty()) {
                drainReconnectDeferredHostPackets(events);
            }
            return;
        }

        if (phase != RuntimeMultiplayerLanPhase::Lobby) {
            rejectUnadmittedPeer(
                transportPeerId,
                admission.request.peerId,
                "running rooms accept only a suspended peer reconnect",
                events
            );
            return;
        }

        RuntimeMultiplayerMessage joinMessage;
        joinMessage.type = RuntimeMultiplayerMessageType::JoinRequest;
        joinMessage.body = admission.request;

        if (!binding.admitted) {
            const bool duplicateIdentity = std::any_of(
                session.members().begin(),
                session.members().end(),
                [&](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == admission.request.peerId;
                }
            );
            const std::size_t roleMembers = static_cast<std::size_t>(
                std::count_if(
                    session.members().begin(),
                    session.members().end(),
                    [&](const RuntimeMultiplayerRoomMember& member) {
                        return member.participation
                            == admission.request.participation;
                    }
                )
            );
            const std::size_t roleLimit = admission.request.participation
                    == RuntimeMultiplayerParticipationRole::Combatant
                ? maximumCombatants
                : maximumSpectators;
            if (duplicateIdentity || roleMembers >= roleLimit) {
                rejectUnadmittedPeer(
                    transportPeerId,
                    admission.request.peerId,
                    duplicateIdentity
                        ? "peer id is already connected to this room"
                        : (admission.request.participation
                                == RuntimeMultiplayerParticipationRole::Combatant
                            ? "no combatant lobby slot is available"
                            : "multiplayer spectator limit reached"),
                    events
                );
                return;
            }
        }

        RuntimeNetworkPacket joinPacket;
        std::string encodeError;
        if (!encode(joinMessage, joinPacket, encodeError)) {
            rejectUnadmittedPeer(
                transportPeerId,
                admission.request.peerId,
                encodeError,
                events
            );
            return;
        }
        const std::string_view authenticated = binding.admitted
            ? std::string_view(binding.logicalPeerId)
            : std::string_view{};
        RuntimeMultiplayerMessageApplyResult applied =
            applyRuntimeMultiplayerMessage(
                joinPacket,
                authenticated,
                session
            );
        if (!applied.accepted) {
            if (!binding.admitted) {
                rejectUnadmittedPeer(
                    transportPeerId,
                    admission.request.peerId,
                    applied.reason,
                    events
                );
            } else {
                RuntimeMultiplayerLanEvent rejected = event(
                    RuntimeMultiplayerLanEventKind::ProtocolRejected,
                    decoded.type
                );
                rejected.peerId = binding.logicalPeerId;
                rejected.reason = applied.reason;
                events.push_back(std::move(rejected));
                server.disconnect(transportPeerId);
                handleHostPeerGone(
                    transportPeerId,
                    RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                    applied.reason,
                    events
                );
            }
            return;
        }

        binding.logicalPeerId = admission.request.peerId;
        binding.admitted = true;
        // The challenge response is single-use. From this point onward the
        // permanent connection binding, not a replayable token, authenticates
        // all room messages.
        binding.admissionToken = 0;
        binding.closeAfterPolls = -1;
        RuntimeMultiplayerJoinAcceptedMessage accepted;
        accepted.roomGeneration = roomGeneration;
        accepted.roomId = session.config().roomId;
        accepted.peerId = binding.logicalPeerId;
        RuntimeMultiplayerMessage acceptedMessage;
        acceptedMessage.type = RuntimeMultiplayerMessageType::JoinAccepted;
        acceptedMessage.body = std::move(accepted);
        std::string sendError;
        if (!sendToPeer(transportPeerId, acceptedMessage, sendError)) {
            server.disconnect(transportPeerId);
            handleHostPeerGone(
                transportPeerId,
                RuntimeMultiplayerSessionAbortReason::TransportFailure,
                sendError,
                events
            );
            return;
        }
        broadcastLobbySnapshot(sendError);
        updateAdvertisement(sendError);

        RuntimeMultiplayerLanEvent acceptedEvent = event(
            RuntimeMultiplayerLanEventKind::JoinAccepted,
            RuntimeMultiplayerMessageType::JoinAccepted
        );
        acceptedEvent.peerId = admission.request.peerId;
        events.push_back(std::move(acceptedEvent));
        appendAppliedEvent(joinMessage, applied, events);
        return;
    }

    if (!found->second.admitted) {
        rejectUnadmittedPeer(
            transportPeerId,
            {},
            "a valid admission response is required before room messages",
            events
        );
        return;
    }

    const std::string boundPeerId = found->second.logicalPeerId;
    if (!suspendedPeers.empty()
        && (decoded.type == RuntimeMultiplayerMessageType::Command
            || decoded.type == RuntimeMultiplayerMessageType::StateHash)) {
        const std::size_t packetBytes = deferredCanonicalPacketBytes(packet);
        if (reconnectDeferredHostPackets.size()
                >= runtimeMultiplayerLanMaximumDeferredCanonicalPackets
            || packetBytes
                > runtimeMultiplayerLanMaximumDeferredCanonicalBytes
            || reconnectDeferredHostBytes
                > runtimeMultiplayerLanMaximumDeferredCanonicalBytes
                    - packetBytes) {
            RuntimeMultiplayerLanEvent failed = event(
                RuntimeMultiplayerLanEventKind::ReconnectFailed,
                decoded.type
            );
            failed.peerId = boundPeerId;
            failed.tick = session.authoritativeTick();
            failed.reason =
                "reconnect pause exceeded the deferred packet limit";
            events.push_back(std::move(failed));
            (void)hostAbort(
                suspendedPeers.begin()->first,
                RuntimeMultiplayerSessionAbortReason::TransportFailure
            );
            return;
        }
        reconnectDeferredHostBytes += packetBytes;
        reconnectDeferredHostPackets.push_back(
            DeferredHostPacket{transportPeerId, packet}
        );
        return;
    }
    if (decoded.type
        == RuntimeMultiplayerMessageType::LobbySlotOptionsRequest) {
        const auto& request = std::get<
            RuntimeMultiplayerLobbySlotOptionsRequestMessage
        >(decoded.body);
        HostPeer& peer = found->second;
        const auto rejectRequest = [&](std::string reason) {
            (void)sendLobbySlotOptionsResult(
                transportPeerId,
                request.requestId,
                false,
                std::move(reason),
                events
            );
        };
        if (request.requestId <= peer.lastLobbySlotRequestId) {
            rejectRequest("lobby slot request id was replayed or stale");
            return;
        }
        peer.lastLobbySlotRequestId = request.requestId;
        if (phase != RuntimeMultiplayerLanPhase::Lobby) {
            rejectRequest("lobby slot request arrived outside the lobby");
            return;
        }
        if (request.peerId != boundPeerId) {
            rejectRequest(
                "lobby slot request peer does not match the authenticated connection"
            );
            return;
        }
        if (peer.pendingLobbySlotRequest.has_value()) {
            rejectRequest("a previous lobby slot request is still pending");
            return;
        }
        if (!session.matchSetup().has_value()
            || request.expectedSetupRevision
                != session.matchSetup()->revision) {
            rejectRequest("lobby slot request expected revision is stale");
            return;
        }
        const RuntimeMultiplayerMatchSetup& setup = *session.matchSetup();
        if (request.slotIndex >= setup.slots.size()) {
            rejectRequest("lobby slot request index is out of range");
            return;
        }
        const RuntimeMultiplayerMatchSlot& slot =
            setup.slots[request.slotIndex];
        if (slot.controller
                != RuntimeMultiplayerMatchSlotController::Human
            || slot.peerId != boundPeerId) {
            rejectRequest(
                "client may edit only its authenticated Human lobby slot"
            );
            return;
        }
        peer.pendingLobbySlotRequest = request;
        RuntimeMultiplayerLanEvent requested = event(
            RuntimeMultiplayerLanEventKind::LobbySlotOptionsRequested,
            decoded.type
        );
        requested.peerId = boundPeerId;
        requested.sequence = request.requestId;
        requested.lobbySlotOptionsRequest = request;
        events.push_back(std::move(requested));
        return;
    }
    if (handleHostLobbyResumePacket(transportPeerId, decoded, events)) return;
    if (handleHostLobbyMapPacket(transportPeerId, decoded, events)) return;
    if (decoded.type == RuntimeMultiplayerMessageType::SnapshotChunk) {
        RuntimeMultiplayerLanEvent rejected = event(
            RuntimeMultiplayerLanEventKind::ProtocolRejected,
            decoded.type
        );
        rejected.peerId = boundPeerId;
        rejected.reason = "snapshot chunks may only be sent by the host";
        events.push_back(std::move(rejected));
        server.disconnect(transportPeerId);
        handleHostPeerGone(
            transportPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
            "client sent a host-only snapshot chunk",
            events
        );
        return;
    }
    if (decoded.type == RuntimeMultiplayerMessageType::SnapshotChunkAck) {
        HostPeer& peer = found->second;
        const RuntimeMultiplayerSnapshotChunkAck& ack =
            std::get<RuntimeMultiplayerSnapshotChunkAck>(decoded.body);
        const bool bindingMatches = snapshotTransferActive
            && peer.snapshotProducer.active()
            && ack.proposalId == snapshotProposal.proposalId
            && ack.sourceTimelineEpoch == snapshotProposal.timelineEpoch;
        const RuntimeMultiplayerSnapshotTransferResult acknowledged =
            bindingMatches
                ? peer.snapshotProducer.acknowledge(ack)
                : RuntimeMultiplayerSnapshotTransferResult{};
        if (!bindingMatches || !acknowledged.accepted) {
            RuntimeMultiplayerLanEvent rejected = event(
                RuntimeMultiplayerLanEventKind::ProtocolRejected,
                decoded.type
            );
            rejected.peerId = boundPeerId;
            rejected.reason = bindingMatches
                ? acknowledged.reason
                : "snapshot ack does not match the active transfer";
            events.push_back(std::move(rejected));
            server.disconnect(transportPeerId);
            handleHostPeerGone(
                transportPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                "invalid snapshot chunk acknowledgement",
                events
            );
        } else if (acknowledged.changed) {
            peer.snapshotChunkSent = false;
        }
        return;
    }
    if (decoded.type == RuntimeMultiplayerMessageType::Command) {
        std::string validationError;
        if (!validateCommandPayload(
                std::get<RuntimeMultiplayerCommandPacket>(decoded.body),
                validationError
            )) {
            RuntimeMultiplayerLanEvent rejected = event(
                RuntimeMultiplayerLanEventKind::ProtocolRejected,
                decoded.type
            );
            rejected.peerId = boundPeerId;
            rejected.reason = validationError;
            events.push_back(std::move(rejected));
            server.disconnect(transportPeerId);
            handleHostPeerGone(
                transportPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
                std::move(validationError),
                events
            );
            return;
        }
    }
    const bool rollbackOnHookFailure =
        observesSimulationMessage(decoded.type);
    RuntimeMultiplayerSessionState sessionBeforeHook;
    if (rollbackOnHookFailure) sessionBeforeHook = session;
    RuntimeMultiplayerMessageApplyResult applied =
        applyRuntimeMultiplayerMessage(packet, boundPeerId, session);
    if (!applied.accepted) {
        RuntimeMultiplayerLanEvent rejected = event(
            RuntimeMultiplayerLanEventKind::ProtocolRejected,
            decoded.type
        );
        rejected.peerId = boundPeerId;
        rejected.reason = applied.reason;
        events.push_back(std::move(rejected));
        server.disconnect(transportPeerId);
        handleHostPeerGone(
            transportPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
            applied.reason,
            events
        );
        return;
    }
    std::string simulationError;
    if (!notifySimulationMessageApplied(decoded, simulationError)) {
        if (rollbackOnHookFailure) {
            session = std::move(sessionBeforeHook);
        }
        RuntimeMultiplayerLanEvent rejected = event(
            RuntimeMultiplayerLanEventKind::ProtocolRejected,
            decoded.type
        );
        rejected.peerId = boundPeerId;
        rejected.reason = simulationError;
        events.push_back(std::move(rejected));
        server.disconnect(transportPeerId);
        handleHostPeerGone(
            transportPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
            std::move(simulationError),
            events
        );
        return;
    }

    std::string broadcastError;
    broadcastPacket(packet, broadcastError);
    appendAppliedEvent(decoded, applied, events);

    if (decoded.type == RuntimeMultiplayerMessageType::SaveLoadResponse) {
        const RuntimeMultiplayerSaveLoadHandshake* handshake =
            activeSaveLoad();
        if (handshake != nullptr
            && handshake->status
                == RuntimeMultiplayerSaveLoadStatus::Prepared) {
            (void)issueHostSnapshotCommit();
            events.insert(
                events.end(),
                std::make_move_iterator(pendingEvents.begin()),
                std::make_move_iterator(pendingEvents.end())
            );
            pendingEvents.clear();
        } else if (handshake != nullptr
            && handshake->status
                == RuntimeMultiplayerSaveLoadStatus::Rejected) {
            clearSnapshotTransfer(true);
        }
    } else if (decoded.type
        == RuntimeMultiplayerMessageType::SaveLoadApplied) {
        if (session.status() == RuntimeMultiplayerRoomStatus::Aborted) {
            phase = RuntimeMultiplayerLanPhase::Aborted;
            discoveryHost.close();
        }
        finishSnapshotTerminalState();
    }

    if (decoded.type == RuntimeMultiplayerMessageType::Leave) {
        auto leaving = hostPeers.find(transportPeerId);
        if (leaving != hostPeers.end()) leaving->second.admitted = false;
        if (session.status() == RuntimeMultiplayerRoomStatus::Running) {
            reconcileSnapshotAfterSpectatorDeparture();
        }
    }
    if (messageUsesLobbySnapshot(decoded.type)
        && session.status() == RuntimeMultiplayerRoomStatus::Lobby) {
        broadcastLobbySnapshot(broadcastError);
        updateAdvertisement(broadcastError);
    }
    if (decoded.type == RuntimeMultiplayerMessageType::Leave) {
        scheduleClose(transportPeerId);
    } else if (decoded.type == RuntimeMultiplayerMessageType::Start) {
        phase = RuntimeMultiplayerLanPhase::Running;
        discoveryHost.close();
    }
}

void RuntimeMultiplayerLanSession::Impl::clientProtocolFailure(
    std::string reason,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    clearSnapshotTransfer(true);
    RuntimeMultiplayerLanEvent rejected = event(
        RuntimeMultiplayerLanEventKind::ProtocolRejected
    );
    rejected.peerId = session.active()
        ? session.config().hostPeerId
        : std::string{};
    rejected.reason = reason;
    events.push_back(std::move(rejected));

    if (session.status() == RuntimeMultiplayerRoomStatus::Running) {
        RuntimeMultiplayerSessionAbort abort;
        abort.requestingPeerId = session.config().hostPeerId;
        abort.affectedPeerId = session.config().hostPeerId;
        abort.tick = session.authoritativeTick();
        abort.reason =
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation;
        const RuntimeMultiplayerOperationResult applied =
            session.abortRunningSession(abort);
        if (applied.accepted) {
            phase = RuntimeMultiplayerLanPhase::Aborted;
            RuntimeMultiplayerLanEvent aborted = event(
                RuntimeMultiplayerLanEventKind::SessionAborted,
                RuntimeMultiplayerMessageType::SessionAbort
            );
            aborted.peerId = session.config().hostPeerId;
            aborted.tick = session.authoritativeTick();
            aborted.reason = reason;
            events.push_back(std::move(aborted));
        }
    } else if (phase != RuntimeMultiplayerLanPhase::JoinRejected) {
        phase = RuntimeMultiplayerLanPhase::Disconnected;
    }
    client.close();

    RuntimeMultiplayerLanEvent disconnected = event(
        RuntimeMultiplayerLanEventKind::Disconnected
    );
    disconnected.reason = std::move(reason);
    events.push_back(std::move(disconnected));
}

void RuntimeMultiplayerLanSession::Impl::handleClientPacket(
    const RuntimeNetworkPacket& packet,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    RuntimeMultiplayerMessage decoded;
    std::string decodeError;
    if (!decodeRuntimeMultiplayerMessage(packet, decoded, decodeError)) {
        clientProtocolFailure(std::move(decodeError), events);
        return;
    }

    if (decoded.type
        == RuntimeMultiplayerMessageType::AdmissionChallenge) {
        const RuntimeMultiplayerAdmissionChallengeMessage& challenge =
            std::get<RuntimeMultiplayerAdmissionChallengeMessage>(
                decoded.body
            );
        const bool reconnecting =
            phase == RuntimeMultiplayerLanPhase::Reconnecting;
        if ((phase != RuntimeMultiplayerLanPhase::Joining && !reconnecting)
            || challenge.config.roomId != joinOptions.room.roomId
            || challenge.config.hostDisplayName
                != joinOptions.room.hostName
            || challenge.config.compatibility
                != joinOptions.compatibility
            || (reconnecting
                && (challenge.roomGeneration != roomGeneration
                    || challenge.config != session.config()
                    || challenge.reconnectGraceMilliseconds == 0))) {
            clientProtocolFailure(
                "LAN admission challenge does not match the selected room",
                events
            );
            return;
        }
        if (challengeReceived
            && (challenge.roomGeneration != roomGeneration
                || challenge.admissionToken != admissionToken)) {
            clientProtocolFailure(
                "LAN host changed its admission challenge",
                events
            );
            return;
        }
        challengeReceived = true;
        roomGeneration = challenge.roomGeneration;
        admissionToken = challenge.admissionToken;
        if (!reconnecting) {
            peerReconnectGraceMilliseconds =
                challenge.reconnectGraceMilliseconds;
        }

        RuntimeMultiplayerJoinAdmissionRequestMessage admission;
        admission.roomGeneration = roomGeneration;
        admission.admissionToken = joinPasswordProofRequired
            ? runtimeLanRoomPasswordAdmissionProof(
                joinPasswordVerifier,
                joinOptions.room.roomId,
                joinOptions.peerId,
                roomGeneration,
                admissionToken
            )
            : admissionToken;
        admission.request.peerId = joinOptions.peerId;
        admission.request.displayName = joinOptions.displayName;
        admission.request.participation = joinOptions.participation;
        admission.request.compatibility = joinOptions.compatibility;
        admission.reconnect = reconnecting;
        if (reconnecting) {
            admission.reconnectTimelineEpoch = session.timelineEpoch();
            admission.reconnectAuthoritativeTick =
                session.authoritativeTick();
            admission.reconnectSessionStateHash = session.stateHash();
        }
        RuntimeMultiplayerMessage admissionMessage;
        admissionMessage.type =
            RuntimeMultiplayerMessageType::JoinAdmissionRequest;
        admissionMessage.body = std::move(admission);
        std::string sendError;
        if (!sendToHost(admissionMessage, sendError)) {
            clientProtocolFailure(std::move(sendError), events);
        }
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::JoinAccepted) {
        const RuntimeMultiplayerJoinAcceptedMessage& accepted =
            std::get<RuntimeMultiplayerJoinAcceptedMessage>(decoded.body);
        const bool reconnecting =
            phase == RuntimeMultiplayerLanPhase::Reconnecting;
        if ((phase != RuntimeMultiplayerLanPhase::Joining && !reconnecting)
            || !challengeReceived
            || accepted.roomGeneration != roomGeneration
            || accepted.roomId != joinOptions.room.roomId
            || accepted.peerId != localPeerId
            || accepted.reconnected != reconnecting) {
            clientProtocolFailure(
                "LAN join acceptance does not match this client",
                events
            );
            return;
        }
        joinAccepted = true;
        if (reconnecting) {
            phase = RuntimeMultiplayerLanPhase::Running;
            reconnectRecoveryPending = true;
            reconnectRecoveryProposalId = 0;
            RuntimeMultiplayerLanEvent reconnected = event(
                RuntimeMultiplayerLanEventKind::Reconnected,
                RuntimeMultiplayerMessageType::JoinAccepted
            );
            reconnected.peerId = localPeerId;
            reconnected.tick = session.authoritativeTick();
            events.push_back(std::move(reconnected));
            return;
        }
        RuntimeMultiplayerLanEvent acceptedEvent = event(
            RuntimeMultiplayerLanEventKind::JoinAccepted,
            RuntimeMultiplayerMessageType::JoinAccepted
        );
        acceptedEvent.peerId = localPeerId;
        events.push_back(std::move(acceptedEvent));
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::JoinRejected) {
        const RuntimeMultiplayerJoinRejectedMessage& rejectedMessage =
            std::get<RuntimeMultiplayerJoinRejectedMessage>(decoded.body);
        const bool reconnecting =
            phase == RuntimeMultiplayerLanPhase::Reconnecting;
        if ((phase != RuntimeMultiplayerLanPhase::Joining && !reconnecting)
            || rejectedMessage.roomId != joinOptions.room.roomId
            || (!rejectedMessage.peerId.empty()
                && rejectedMessage.peerId != localPeerId)) {
            clientProtocolFailure(
                "LAN join rejection does not match this client",
                events
            );
            return;
        }
        if (reconnecting) {
            failClientReconnect(rejectedMessage.reason, events);
            return;
        }
        phase = RuntimeMultiplayerLanPhase::JoinRejected;
        RuntimeMultiplayerLanEvent rejected = event(
            RuntimeMultiplayerLanEventKind::JoinRejected,
            RuntimeMultiplayerMessageType::JoinRejected
        );
        rejected.peerId = rejectedMessage.peerId;
        rejected.reason = rejectedMessage.reason;
        events.push_back(std::move(rejected));
        client.close();
        return;
    }

    if (decoded.type
        == RuntimeMultiplayerMessageType::LobbySlotOptionsResult) {
        const auto& response = std::get<
            RuntimeMultiplayerLobbySlotOptionsResultMessage
        >(decoded.body);
        if (phase != RuntimeMultiplayerLanPhase::Lobby
            || response.peerId != localPeerId
            || response.requestId > lastLobbySlotRequestIdSent
            || (response.accepted
                && (!session.matchSetup().has_value()
                    || session.matchSetup()->revision
                        != response.currentSetupRevision))) {
            clientProtocolFailure(
                "lobby slot result does not match this client request",
                events
            );
            return;
        }
        RuntimeMultiplayerLanEvent result = event(
            RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult,
            decoded.type
        );
        result.peerId = response.peerId;
        result.reason = response.reason;
        result.sequence = response.requestId;
        result.lobbySlotOptionsResult = response;
        events.push_back(std::move(result));
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::LobbyKicked) {
        const auto& kicked =
            std::get<RuntimeMultiplayerLobbyKickedMessage>(decoded.body);
        if (phase != RuntimeMultiplayerLanPhase::Lobby
            || kicked.roomGeneration != roomGeneration
            || kicked.peerId != localPeerId) {
            clientProtocolFailure(
                "lobby kick does not match this admitted client",
                events
            );
            return;
        }
        phase = RuntimeMultiplayerLanPhase::Kicked;
        clearSnapshotTransfer(true);
        RuntimeMultiplayerLanEvent kickedEvent = event(
            RuntimeMultiplayerLanEventKind::Kicked,
            decoded.type
        );
        kickedEvent.peerId = kicked.peerId;
        kickedEvent.reason = kicked.reason;
        events.push_back(std::move(kickedEvent));
        client.close();
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::LobbySnapshot) {
        const RuntimeMultiplayerLobbySnapshotMessage& snapshot =
            std::get<RuntimeMultiplayerLobbySnapshotMessage>(decoded.body);
        if ((phase != RuntimeMultiplayerLanPhase::Joining
                && phase != RuntimeMultiplayerLanPhase::Lobby)
            || !joinAccepted
            || snapshot.roomGeneration != roomGeneration
            || snapshot.snapshot.config.roomId
                != joinOptions.room.roomId
            || snapshot.snapshot.config.hostDisplayName
                != joinOptions.room.hostName
            || snapshot.snapshot.config.compatibility
                != joinOptions.compatibility
            || (session.active()
                && snapshot.snapshot.config != session.config())) {
            clientProtocolFailure(
                "LAN lobby snapshot does not match the admitted room",
                events
            );
            return;
        }
        const auto localMember = std::find_if(
            snapshot.snapshot.members.begin(),
            snapshot.snapshot.members.end(),
            [this](const RuntimeMultiplayerRoomMember& member) {
                return member.peerId == localPeerId;
            }
        );
        if (localMember == snapshot.snapshot.members.end()
            || localMember->displayName != joinOptions.displayName
            || localMember->role
                != RuntimeMultiplayerRoomMemberRole::Client
            || localMember->participation != joinOptions.participation) {
            clientProtocolFailure(
                "LAN lobby snapshot does not contain this admitted client",
                events
            );
            return;
        }
        RuntimeMultiplayerMessageApplyResult applied =
            applyTrustedCanonicalRuntimeMultiplayerMessage(packet, session);
        if (!applied.accepted) {
            clientProtocolFailure(applied.reason, events);
            return;
        }
        phase = RuntimeMultiplayerLanPhase::Lobby;
        appendAppliedEvent(decoded, applied, events);
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::SessionAbort) {
        const RuntimeMultiplayerSessionAbortMessage& abort =
            std::get<RuntimeMultiplayerSessionAbortMessage>(decoded.body);
        if (phase != RuntimeMultiplayerLanPhase::Running
            || abort.roomGeneration != roomGeneration
            || abort.abort.requestingPeerId
                != session.config().hostPeerId) {
            clientProtocolFailure(
                "LAN session abort is not from the admitted room host",
                events
            );
            return;
        }
        RuntimeMultiplayerMessageApplyResult applied =
            applyTrustedCanonicalRuntimeMultiplayerMessage(packet, session);
        if (!applied.accepted) {
            clientProtocolFailure(applied.reason, events);
            return;
        }
        phase = RuntimeMultiplayerLanPhase::Aborted;
        clearSnapshotTransfer(true);
        appendAppliedEvent(decoded, applied, events);
        return;
    }

    if (phase != RuntimeMultiplayerLanPhase::Lobby
        && phase != RuntimeMultiplayerLanPhase::Running) {
        clientProtocolFailure(
            "canonical room event arrived before admission completed",
            events
        );
        return;
    }
    if (handleClientLobbyResumePacket(decoded, events)) return;
    if (handleClientLobbyMapPacket(decoded, events)) return;
    if (decoded.type == RuntimeMultiplayerMessageType::SnapshotChunk) {
        if (phase != RuntimeMultiplayerLanPhase::Running) {
            clientProtocolFailure(
                "snapshot chunk arrived outside a running match",
                events
            );
            return;
        }
        handleClientSnapshotChunk(
            std::get<RuntimeMultiplayerSnapshotChunk>(decoded.body),
            events
        );
        return;
    }
    if (decoded.type == RuntimeMultiplayerMessageType::SnapshotChunkAck) {
        clientProtocolFailure(
            "snapshot chunk acknowledgements may only be sent by clients",
            events
        );
        return;
    }
    switch (decoded.type) {
    case RuntimeMultiplayerMessageType::AdmissionChallenge:
    case RuntimeMultiplayerMessageType::JoinAdmissionRequest:
    case RuntimeMultiplayerMessageType::JoinAccepted:
    case RuntimeMultiplayerMessageType::JoinRejected:
    case RuntimeMultiplayerMessageType::RoomConfig:
    case RuntimeMultiplayerMessageType::LobbySlotOptionsRequest:
        clientProtocolFailure(
            "unexpected LAN coordinator control message",
            events
        );
        return;
    default:
        break;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::Command) {
        std::string validationError;
        if (!validateCommandPayload(
                std::get<RuntimeMultiplayerCommandPacket>(decoded.body),
                validationError
            )) {
            clientProtocolFailure(std::move(validationError), events);
            return;
        }
    }

    if (decoded.type == RuntimeMultiplayerMessageType::SaveLoadProposal) {
        RuntimeMultiplayerSessionState candidate = session;
        RuntimeMultiplayerMessageApplyResult applied =
            applyTrustedCanonicalRuntimeMultiplayerMessage(packet, candidate);
        if (!applied.accepted) {
            clientProtocolFailure(applied.reason, events);
            return;
        }
        const RuntimeMultiplayerSaveLoadProposal& proposal =
            std::get<RuntimeMultiplayerSaveLoadProposal>(decoded.body);
        if (reconnectRecoveryPending) {
            if (reconnectRecoveryProposalId != 0
                && reconnectRecoveryProposalId != proposal.proposalId) {
                clientProtocolFailure(
                    "reconnect recovery proposal identity changed",
                    events
                );
                return;
            }
            if (proposal.kind != RuntimeMultiplayerSaveLoadKind::Load
                || proposal.proposerPeerId != session.config().hostPeerId
                || proposal.timelineEpoch != session.timelineEpoch()
                || proposal.descriptor.identity.barrierTick
                    != session.authoritativeTick() - 1) {
                clientProtocolFailure(
                    "reconnect recovery requires a host checkpoint Load",
                    events
                );
                return;
            }
            reconnectRecoveryProposalId = proposal.proposalId;
        }
        if (!snapshotHooks.complete()) {
            clientProtocolFailure(
                "complete snapshot hooks are required for a proposal",
                events
            );
            return;
        }
        if (snapshotTransferActive) {
            if (snapshotProposal != proposal || !applied.duplicate) {
                clientProtocolFailure(
                    "snapshot proposal conflicts with the active transfer",
                    events
                );
                return;
            }
            session = std::move(candidate);
            appendAppliedEvent(decoded, applied, events);
            return;
        }
        const RuntimeMultiplayerSnapshotTransferResult begun =
            snapshotAssembler.begin(proposal.descriptor);
        if (!begun.accepted) {
            clientProtocolFailure(begun.reason, events);
            return;
        }
        snapshotTransferActive = true;
        snapshotStaged = false;
        snapshotProposal = proposal;
        stagedAuthoritativeStateHash = 0;
        session = std::move(candidate);
        appendAppliedEvent(decoded, applied, events);
        return;
    }

    if (decoded.type == RuntimeMultiplayerMessageType::SaveLoadCommit) {
        handleClientSnapshotCommit(packet, decoded, events);
        return;
    }

    std::string simulationError;
    const bool hasPrevalidatedAdvance = decoded.type
        == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick;
    RuntimeMultiplayerSessionState prevalidatedAdvanceSession;
    RuntimeMultiplayerMessageApplyResult prevalidatedAdvanceResult;
    if (hasPrevalidatedAdvance) {
        prevalidatedAdvanceSession = session;
        prevalidatedAdvanceResult =
            applyTrustedCanonicalRuntimeMultiplayerMessage(
                packet,
                prevalidatedAdvanceSession
            );
        if (!prevalidatedAdvanceResult.accepted) {
            clientProtocolFailure(
                prevalidatedAdvanceResult.reason,
                events
            );
            return;
        }
    }
    const RuntimeMultiplayerLanSimulationAdvanceDecision advanceDecision =
        prepareSimulationMessage(decoded, simulationError);
    if (advanceDecision
        == RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred) {
        clientCanonicalPacketDeferred = true;
        return;
    }
    if (advanceDecision
        == RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected) {
        clientProtocolFailure(std::move(simulationError), events);
        return;
    }

    const bool rollbackOnHookFailure =
        observesSimulationMessage(decoded.type);
    RuntimeMultiplayerSessionState sessionBeforeHook;
    if (rollbackOnHookFailure) sessionBeforeHook = session;
    RuntimeMultiplayerMessageApplyResult applied;
    if (hasPrevalidatedAdvance) {
        session = std::move(prevalidatedAdvanceSession);
        applied = std::move(prevalidatedAdvanceResult);
    } else {
        applied = applyTrustedCanonicalRuntimeMultiplayerMessage(
            packet,
            session
        );
    }
    if (!applied.accepted) {
        clientProtocolFailure(applied.reason, events);
        return;
    }
    if (!notifySimulationMessageApplied(decoded, simulationError)) {
        if (decoded.type
            == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick) {
            // The world was prepared before SessionState advanced. Moving the
            // session backward would create a recoverable-looking split brain;
            // acknowledgement failure is therefore terminal at the new tick.
            clientProtocolFailure(std::move(simulationError), events);
            return;
        }
        if (rollbackOnHookFailure) {
            session = std::move(sessionBeforeHook);
        }
        clientProtocolFailure(std::move(simulationError), events);
        return;
    }
    appendAppliedEvent(decoded, applied, events);
    if (decoded.type == RuntimeMultiplayerMessageType::SaveLoadResponse
        || decoded.type == RuntimeMultiplayerMessageType::SaveLoadAbort
        || decoded.type == RuntimeMultiplayerMessageType::SaveLoadApplied) {
        if (session.status() == RuntimeMultiplayerRoomStatus::Aborted) {
            phase = RuntimeMultiplayerLanPhase::Aborted;
        }
        finishSnapshotTerminalState();
    }
    if (decoded.type == RuntimeMultiplayerMessageType::Start) {
        phase = RuntimeMultiplayerLanPhase::Running;
    } else if (decoded.type == RuntimeMultiplayerMessageType::Leave) {
        const RuntimeMultiplayerLeaveMessage& leave =
            std::get<RuntimeMultiplayerLeaveMessage>(decoded.body);
        finishSnapshotTerminalState();
        if (leave.peerId == localPeerId) {
            phase = RuntimeMultiplayerLanPhase::Disconnected;
            clearSnapshotTransfer(true);
            client.close();
            RuntimeMultiplayerLanEvent disconnected = event(
                RuntimeMultiplayerLanEventKind::Disconnected
            );
            disconnected.peerId = localPeerId;
            disconnected.reason = "local client left the LAN room";
            events.push_back(std::move(disconnected));
        }
    }
}

void RuntimeMultiplayerLanSession::Impl::handleClientSnapshotChunk(
    const RuntimeMultiplayerSnapshotChunk& chunk,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    if (!snapshotTransferActive
        || chunk.proposalId != snapshotProposal.proposalId
        || chunk.sourceTimelineEpoch != snapshotProposal.timelineEpoch) {
        clientProtocolFailure(
            "snapshot chunk does not match the active proposal",
            events
        );
        return;
    }

    if (haveCompletedSnapshotChunk) {
        if (chunk != completedSnapshotChunk) {
            clientProtocolFailure(
                "completed snapshot chunk retry conflicts with verified data",
                events
            );
            return;
        }
        RuntimeMultiplayerMessage ackMessage;
        ackMessage.type = RuntimeMultiplayerMessageType::SnapshotChunkAck;
        ackMessage.body = completedSnapshotAck;
        std::string sendError;
        if (!sendToHost(ackMessage, sendError)) {
            clientProtocolFailure(std::move(sendError), events);
        }
        return;
    }

    const RuntimeMultiplayerSnapshotTransferResult accepted =
        snapshotAssembler.acceptChunk(chunk);
    if (!accepted.accepted) {
        clientProtocolFailure(accepted.reason, events);
        return;
    }
    RuntimeMultiplayerSnapshotChunkAck ack;
    std::string errorMessage;
    if (!snapshotAssembler.latestAcknowledgement(ack, errorMessage)) {
        clientProtocolFailure(std::move(errorMessage), events);
        return;
    }
    RuntimeMultiplayerMessage ackMessage;
    ackMessage.type = RuntimeMultiplayerMessageType::SnapshotChunkAck;
    ackMessage.body = ack;
    if (!sendToHost(ackMessage, errorMessage)) {
        clientProtocolFailure(std::move(errorMessage), events);
        return;
    }
    if (!accepted.complete) return;

    std::vector<std::uint8_t> verifiedBytes;
    completedSnapshotChunk = chunk;
    completedSnapshotAck = ack;
    haveCompletedSnapshotChunk = true;
    if (!snapshotAssembler.takeCompletedBytes(
            verifiedBytes,
            errorMessage
        )) {
        clientProtocolFailure(std::move(errorMessage), events);
        return;
    }
    std::uint64_t observedHash = 0;
    snapshotStaged = true;
    if (!stageSnapshotBytes(
            snapshotProposal,
            verifiedBytes,
            observedHash,
            errorMessage
        )) {
        clientProtocolFailure(std::move(errorMessage), events);
        return;
    }
    stagedAuthoritativeStateHash = observedHash;

    RuntimeMultiplayerSaveLoadResponseMessage prepare;
    prepare.timelineEpoch = session.timelineEpoch();
    prepare.proposalId = snapshotProposal.proposalId;
    prepare.peerId = localPeerId;
    prepare.approved = true;
    prepare.observedFingerprint =
        snapshotProposal.descriptor.snapshotFingerprint;
    prepare.observedAuthoritativeStateHash = observedHash;
    RuntimeMultiplayerMessage prepareMessage;
    prepareMessage.type = RuntimeMultiplayerMessageType::SaveLoadPrepare;
    prepareMessage.body = std::move(prepare);
    if (!sendToHost(prepareMessage, errorMessage)) {
        clientProtocolFailure(std::move(errorMessage), events);
    }
}

void RuntimeMultiplayerLanSession::Impl::handleClientSnapshotCommit(
    const RuntimeNetworkPacket& packet,
    const RuntimeMultiplayerMessage& decoded,
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    const RuntimeMultiplayerSaveLoadCommitMessage& commit =
        std::get<RuntimeMultiplayerSaveLoadCommitMessage>(decoded.body);
    if (!snapshotTransferActive || !snapshotStaged
        || commit.proposalId != snapshotProposal.proposalId
        || commit.timelineEpoch != snapshotProposal.timelineEpoch
        || commit.requestingPeerId != session.config().hostPeerId) {
        clientProtocolFailure(
            "snapshot commit does not match verified staged bytes",
            events
        );
        return;
    }

    RuntimeMultiplayerSessionState candidate = session;
    RuntimeMultiplayerMessageApplyResult applied =
        applyTrustedCanonicalRuntimeMultiplayerMessage(packet, candidate);
    if (!applied.accepted) {
        clientProtocolFailure(applied.reason, events);
        return;
    }

    std::string errorMessage;
    bool committed = snapshotCommitCompleted;
    std::uint64_t observedHash = committedAuthoritativeStateHash;
    if (!committed) {
        committed = commitSnapshot(observedHash, errorMessage);
    }
    session = std::move(candidate);
    appendAppliedEvent(decoded, applied, events);
    if (committed && !snapshotCommitCompleted) {
        committed = postSnapshotSessionCommit(errorMessage);
    }
    if (committed) {
        snapshotCommitCompleted = true;
        committedAuthoritativeStateHash = observedHash;
    }

    RuntimeMultiplayerSaveLoadAppliedMessage ack;
    ack.timelineEpoch = session.timelineEpoch();
    ack.proposalId = snapshotProposal.proposalId;
    ack.peerId = localPeerId;
    ack.applied = committed;
    ack.observedAuthoritativeStateHash = committed ? observedHash : 0U;
    const std::string commitFailureReason = committed
        ? std::string{}
        : safeSnapshotReason(errorMessage);
    RuntimeMultiplayerMessage ackMessage;
    ackMessage.type = RuntimeMultiplayerMessageType::SaveLoadApplied;
    ackMessage.body = ack;
    RuntimeNetworkPacket ackPacket;
    if (!encode(ackMessage, ackPacket, errorMessage)
        || !client.send(ackPacket, errorMessage)) {
        clientProtocolFailure(std::move(errorMessage), events);
        return;
    }
    if (committed) return;

    RuntimeMultiplayerMessageApplyResult failed =
        applyTrustedCanonicalRuntimeMultiplayerMessage(ackPacket, session);
    phase = RuntimeMultiplayerLanPhase::Aborted;
    RuntimeMultiplayerLanEvent rejected = event(
        RuntimeMultiplayerLanEventKind::ProtocolRejected,
        RuntimeMultiplayerMessageType::SaveLoadCommit
    );
    rejected.peerId = session.config().hostPeerId;
    rejected.reason = commitFailureReason;
    events.push_back(std::move(rejected));
    if (failed.accepted) appendAppliedEvent(ackMessage, failed, events);
    clearSnapshotTransfer(true);
}

void RuntimeMultiplayerLanSession::Impl::finishSnapshotTerminalState() {
    if (!snapshotTransferActive) return;
    if (session.status() == RuntimeMultiplayerRoomStatus::Aborted) {
        clearSnapshotTransfer(true);
        return;
    }
    const RuntimeMultiplayerSaveLoadHandshake* handshake = activeSaveLoad();
    if (handshake == nullptr) return;
    if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Rejected
        || handshake->status == RuntimeMultiplayerSaveLoadStatus::Aborted) {
        clearSnapshotTransfer(true);
    } else if (handshake->status
        == RuntimeMultiplayerSaveLoadStatus::Committed) {
        const bool completedReconnectRecovery = reconnectRecoveryPending
            && reconnectRecoveryProposalId == handshake->proposal.proposalId;
        clearSnapshotTransfer(false);
        if (completedReconnectRecovery) {
            reconnectRecoveryPending = false;
            reconnectRecoveryProposalId = 0;
        }
    }
}

void RuntimeMultiplayerLanSession::Impl::
reconcileSnapshotAfterSpectatorDeparture() {
    const RuntimeMultiplayerSaveLoadHandshake* handshake = activeSaveLoad();
    if (handshake == nullptr) return;
    if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Prepared) {
        const RuntimeMultiplayerLanOperationResult committed =
            issueHostSnapshotCommit();
        if (!committed.succeeded
            && phase == RuntimeMultiplayerLanPhase::Running) {
            (void)hostAbort(
                localPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            );
        }
        return;
    }
    finishSnapshotTerminalState();
}

void RuntimeMultiplayerLanSession::Impl::sendHostSnapshotChunks(
    std::vector<RuntimeMultiplayerLanEvent>& events
) {
    if (!snapshotTransferActive
        || phase != RuntimeMultiplayerLanPhase::Running) {
        return;
    }
    ++snapshotSendPollCounter;
    std::vector<std::uint64_t> failedPeers;
    std::vector<std::string> failureReasons;
    for (auto& entry : hostPeers) {
        HostPeer& peer = entry.second;
        if (!peer.admitted || !peer.snapshotProducer.active()
            || peer.snapshotProducer.complete()) {
            continue;
        }
        if (peer.snapshotChunkSent
            && snapshotSendPollCounter - peer.snapshotChunkLastSentPoll
                < snapshotChunkRetransmitPollInterval) {
            continue;
        }
        RuntimeMultiplayerSnapshotChunk chunk;
        std::string errorMessage;
        if (!peer.snapshotProducer.currentChunk(chunk, errorMessage)) {
            failedPeers.push_back(entry.first);
            failureReasons.push_back(std::move(errorMessage));
            continue;
        }
        RuntimeMultiplayerMessage message;
        message.type = RuntimeMultiplayerMessageType::SnapshotChunk;
        message.body = std::move(chunk);
        if (!sendToPeer(entry.first, message, errorMessage)) {
            failedPeers.push_back(entry.first);
            failureReasons.push_back(std::move(errorMessage));
        } else {
            peer.snapshotChunkSent = true;
            peer.snapshotChunkLastSentPoll = snapshotSendPollCounter;
        }
    }
    for (std::size_t index = 0U; index < failedPeers.size(); ++index) {
        const std::uint64_t peerId = failedPeers[index];
        server.disconnect(peerId);
        handleHostPeerGone(
            peerId,
            RuntimeMultiplayerSessionAbortReason::TransportFailure,
            std::move(failureReasons[index]),
            events
        );
        if (phase == RuntimeMultiplayerLanPhase::Aborted) break;
    }
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::Impl::issueHostSnapshotCommit() {
    const RuntimeMultiplayerSaveLoadHandshake* handshake = activeSaveLoad();
    if (handshake == nullptr
        || handshake->status
            != RuntimeMultiplayerSaveLoadStatus::Prepared) {
        return failedOperation("snapshot handshake is not prepared");
    }

    RuntimeMultiplayerMessage commitMessage;
    commitMessage.type = RuntimeMultiplayerMessageType::SaveLoadCommit;
    commitMessage.body = RuntimeMultiplayerSaveLoadCommitMessage{
        session.timelineEpoch(),
        snapshotProposal.proposalId,
        localPeerId
    };
    RuntimeNetworkPacket commitPacket;
    std::string errorMessage;
    if (!encode(commitMessage, commitPacket, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }

    RuntimeMultiplayerSessionState candidate = session;
    RuntimeMultiplayerMessageApplyResult commitApplied =
        applyRuntimeMultiplayerMessage(
            commitPacket,
            localPeerId,
            candidate
        );
    if (!commitApplied.accepted) {
        return appliedOperation(commitApplied, false, {});
    }

    std::uint64_t observedHash = 0;
    const bool committed = commitSnapshot(observedHash, errorMessage);
    session = std::move(candidate);
    if (!committed || !postSnapshotSessionCommit(errorMessage)) {
        const std::string hookFailure = errorMessage;
        std::string ignoredBroadcastError;
        (void)broadcastPacket(commitPacket, ignoredBroadcastError);
        appendAppliedEvent(commitMessage, commitApplied, pendingEvents);
        RuntimeMultiplayerLanEvent rejected = event(
            RuntimeMultiplayerLanEventKind::ProtocolRejected,
            RuntimeMultiplayerMessageType::SaveLoadCommit
        );
        rejected.peerId = localPeerId;
        rejected.tick = session.authoritativeTick();
        rejected.reason = hookFailure;
        pendingEvents.push_back(std::move(rejected));
        (void)hostAbort(
            localPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation
        );
        RuntimeMultiplayerLanOperationResult result;
        result.applied = true;
        result.reason = hookFailure;
        return result;
    }
    snapshotCommitCompleted = true;
    committedAuthoritativeStateHash = observedHash;

    const bool commitBroadcast = broadcastPacket(
        commitPacket,
        errorMessage
    );
    appendAppliedEvent(commitMessage, commitApplied, pendingEvents);
    if (!commitBroadcast) {
        (void)hostAbort(
            localPeerId,
            RuntimeMultiplayerSessionAbortReason::TransportFailure
        );
        RuntimeMultiplayerLanOperationResult result;
        result.applied = true;
        result.reason = std::move(errorMessage);
        return result;
    }

    RuntimeMultiplayerMessage appliedMessage;
    appliedMessage.type = RuntimeMultiplayerMessageType::SaveLoadApplied;
    appliedMessage.body = RuntimeMultiplayerSaveLoadAppliedMessage{
        session.timelineEpoch(),
        snapshotProposal.proposalId,
        localPeerId,
        true,
        observedHash
    };
    return applyHostMessage(std::move(appliedMessage));
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::Impl::applyHostMessage(
    RuntimeMultiplayerMessage message
) {
    RuntimeNetworkPacket packet;
    std::string errorMessage;
    if (!encode(message, packet, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    if (message.type == RuntimeMultiplayerMessageType::Command
        && !validateCommandPayload(
            std::get<RuntimeMultiplayerCommandPacket>(message.body),
            errorMessage
        )) {
        return failedOperation(std::move(errorMessage));
    }

    // A prepare hook may commit the world tick. Prove the matching Session
    // transition on a copy first (including save/load barriers and every other
    // Session invariant), so ordinary Session rejection never runs the hook.
    const bool hasPrevalidatedAdvance = message.type
        == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick;
    RuntimeMultiplayerSessionState prevalidatedAdvanceSession;
    RuntimeMultiplayerMessageApplyResult prevalidatedAdvanceResult;
    if (hasPrevalidatedAdvance) {
        prevalidatedAdvanceSession = session;
        prevalidatedAdvanceResult = applyRuntimeMultiplayerMessage(
            packet,
            localPeerId,
            prevalidatedAdvanceSession
        );
        if (!prevalidatedAdvanceResult.accepted) {
            return appliedOperation(
                prevalidatedAdvanceResult,
                false,
                {}
            );
        }
    }
    const RuntimeMultiplayerLanSimulationAdvanceDecision advanceDecision =
        prepareSimulationMessage(message, errorMessage);
    if (advanceDecision
        == RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred) {
        return deferredOperation(std::move(errorMessage));
    }
    if (advanceDecision
        == RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected) {
        return failedOperation(std::move(errorMessage));
    }

    const bool rollbackOnHookFailure =
        observesSimulationMessage(message.type);
    RuntimeMultiplayerSessionState sessionBeforeHook;
    if (rollbackOnHookFailure) sessionBeforeHook = session;
    RuntimeMultiplayerMessageApplyResult applied;
    if (hasPrevalidatedAdvance) {
        session = std::move(prevalidatedAdvanceSession);
        applied = std::move(prevalidatedAdvanceResult);
    } else {
        applied = applyRuntimeMultiplayerMessage(
            packet,
            localPeerId,
            session
        );
    }
    if (!applied.accepted) return appliedOperation(applied, false, {});
    if (!notifySimulationMessageApplied(message, errorMessage)) {
        if (message.type
            == RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick) {
            // Preparation may already have committed the world tick. Publish
            // the matching canonical Session advance, then abort at that new
            // tick; never roll SessionState back and pretend play can resume.
            std::string broadcastError;
            (void)broadcastPacket(packet, broadcastError);
            appendAppliedEvent(message, applied, pendingEvents);
            RuntimeMultiplayerLanEvent rejected = event(
                RuntimeMultiplayerLanEventKind::ProtocolRejected,
                message.type
            );
            rejected.peerId = localPeerId;
            rejected.tick = session.authoritativeTick();
            rejected.reason = errorMessage;
            pendingEvents.push_back(std::move(rejected));
            (void)hostAbort(
                localPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            );
            RuntimeMultiplayerLanOperationResult fatalResult;
            fatalResult.applied = true;
            fatalResult.reason = std::move(errorMessage);
            return fatalResult;
        }
        if (rollbackOnHookFailure) {
            session = std::move(sessionBeforeHook);
        }
        return failedOperation(std::move(errorMessage));
    }

    const bool broadcastQueued = broadcastPacket(packet, errorMessage);
    appendAppliedEvent(message, applied, pendingEvents);
    if (messageUsesLobbySnapshot(message.type)
        && session.status() == RuntimeMultiplayerRoomStatus::Lobby) {
        std::string snapshotError;
        if (!broadcastLobbySnapshot(snapshotError)) {
            if (errorMessage.empty()) errorMessage = std::move(snapshotError);
        }
        std::string advertisementError;
        if (!updateAdvertisement(advertisementError)) {
            if (errorMessage.empty()) {
                errorMessage = std::move(advertisementError);
            }
        }
    }
    if (message.type == RuntimeMultiplayerMessageType::Start) {
        phase = RuntimeMultiplayerLanPhase::Running;
        discoveryHost.close();
    }
    RuntimeMultiplayerLanOperationResult result = appliedOperation(
        applied,
        broadcastQueued && errorMessage.empty(),
        std::move(errorMessage)
    );
    if (message.type == RuntimeMultiplayerMessageType::SaveLoadResponse) {
        const RuntimeMultiplayerSaveLoadHandshake* handshake =
            activeSaveLoad();
        if (handshake != nullptr
            && handshake->status
                == RuntimeMultiplayerSaveLoadStatus::Prepared) {
            return issueHostSnapshotCommit();
        }
        if (handshake != nullptr
            && handshake->status
                == RuntimeMultiplayerSaveLoadStatus::Rejected) {
            clearSnapshotTransfer(true);
        }
    } else if (message.type
            == RuntimeMultiplayerMessageType::SaveLoadAbort
        || message.type
            == RuntimeMultiplayerMessageType::SaveLoadApplied) {
        if (session.status() == RuntimeMultiplayerRoomStatus::Aborted) {
            phase = RuntimeMultiplayerLanPhase::Aborted;
            discoveryHost.close();
        }
        finishSnapshotTerminalState();
    }
    return result;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::Impl::hostAbort(
    const std::string& affectedPeerId,
    RuntimeMultiplayerSessionAbortReason abortReason
) {
    if (role != RuntimeMultiplayerLanRole::Host
        || phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation("only a running LAN host may abort the session");
    }
    RuntimeMultiplayerSessionAbortMessage abort;
    abort.roomGeneration = roomGeneration;
    abort.abort.requestingPeerId = session.config().hostPeerId;
    abort.abort.affectedPeerId = affectedPeerId;
    abort.abort.tick = session.authoritativeTick();
    abort.abort.reason = abortReason;
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::SessionAbort;
    message.body = std::move(abort);

    RuntimeNetworkPacket packet;
    std::string errorMessage;
    if (!encode(message, packet, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    RuntimeMultiplayerMessageApplyResult applied =
        applyTrustedCanonicalRuntimeMultiplayerMessage(packet, session);
    if (!applied.accepted) return appliedOperation(applied, false, {});
    const bool broadcastQueued = broadcastPacket(packet, errorMessage);
    phase = RuntimeMultiplayerLanPhase::Aborted;
    discoveryHost.close();
    clearSnapshotTransfer(true);
    suspendedPeers.clear();
    reconnectDeferredHostPackets.clear();
    reconnectDeferredHostBytes = 0;
    reconnectRecoveryPending = false;
    reconnectRecoveryProposalId = 0;
    appendAppliedEvent(message, applied, pendingEvents);
    return appliedOperation(
        applied,
        broadcastQueued,
        std::move(errorMessage)
    );
}

RuntimeMultiplayerLanPollResult
RuntimeMultiplayerLanSession::Impl::pollHost(int timeoutMilliseconds) {
    RuntimeMultiplayerLanPollResult result;
    result.events.swap(pendingEvents);

    if (discoveryHost.isOpen()) {
        std::string discoveryError;
        discoveryHost.poll(0, discoveryError);
        if (!discoveryError.empty()) {
            result.succeeded = false;
            result.error = discoveryError;
            RuntimeMultiplayerLanEvent transportError = event(
                RuntimeMultiplayerLanEventKind::TransportError
            );
            transportError.reason = std::move(discoveryError);
            result.events.push_back(std::move(transportError));
        }
    }

    std::vector<std::uint64_t> scheduledBeforePoll;
    for (const auto& entry : hostPeers) {
        if (entry.second.closeAfterPolls >= 0) {
            scheduledBeforePoll.push_back(entry.first);
        }
    }

    std::string serverError;
    const std::vector<RuntimeTcpEvent> transportEvents =
        server.poll(timeoutMilliseconds, serverError);
    if (!serverError.empty()) {
        result.succeeded = false;
        if (result.error.empty()) result.error = serverError;
        RuntimeMultiplayerLanEvent transportError = event(
            RuntimeMultiplayerLanEventKind::TransportError
        );
        transportError.reason = serverError;
        result.events.push_back(std::move(transportError));
    }

    for (const RuntimeTcpEvent& transportEvent : transportEvents) {
        switch (transportEvent.kind) {
        case RuntimeTcpEventKind::Connected: {
            HostPeer peer;
            peer.address = transportEvent.peerAddress;
            peer.admissionToken = makeUniqueAdmissionToken();
            hostPeers[transportEvent.peerId] = peer;

            RuntimeMultiplayerLanEvent connected = event(
                RuntimeMultiplayerLanEventKind::TransportConnected
            );
            connected.reason = transportEvent.peerAddress;
            result.events.push_back(std::move(connected));

            const bool acceptsInitialJoin =
                phase == RuntimeMultiplayerLanPhase::Lobby;
            const bool acceptsReconnect =
                phase == RuntimeMultiplayerLanPhase::Running
                && !suspendedPeers.empty();
            if (!acceptsInitialJoin && !acceptsReconnect) {
                rejectUnadmittedPeer(
                    transportEvent.peerId,
                    {},
                    "LAN room is no longer accepting members",
                    result.events
                );
                break;
            }
            RuntimeMultiplayerAdmissionChallengeMessage challenge;
            challenge.roomGeneration = roomGeneration;
            challenge.admissionToken = peer.admissionToken;
            challenge.reconnectGraceMilliseconds =
                reconnectGraceMilliseconds;
            challenge.config = session.config();
            RuntimeMultiplayerMessage challengeMessage;
            challengeMessage.type =
                RuntimeMultiplayerMessageType::AdmissionChallenge;
            challengeMessage.body = std::move(challenge);
            std::string sendError;
            if (!sendToPeer(
                    transportEvent.peerId,
                    challengeMessage,
                    sendError
                )) {
                server.disconnect(transportEvent.peerId);
                handleHostPeerGone(
                    transportEvent.peerId,
                    RuntimeMultiplayerSessionAbortReason::TransportFailure,
                    sendError,
                    result.events
                );
            }
            break;
        }
        case RuntimeTcpEventKind::Packet:
            handleHostPacket(
                transportEvent.peerId,
                transportEvent.packet,
                result.events
            );
            break;
        case RuntimeTcpEventKind::Disconnected:
            handleHostPeerGone(
                transportEvent.peerId,
                RuntimeMultiplayerSessionAbortReason::PeerDisconnected,
                transportEvent.message,
                result.events
            );
            break;
        case RuntimeTcpEventKind::Error: {
            RuntimeMultiplayerLanEvent transportError = event(
                RuntimeMultiplayerLanEventKind::TransportError
            );
            const auto peer = hostPeers.find(transportEvent.peerId);
            if (peer != hostPeers.end()) {
                transportError.peerId = peer->second.logicalPeerId;
            }
            transportError.reason = transportEvent.message;
            result.events.push_back(std::move(transportError));
            break;
        }
        }
    }

    for (const std::uint64_t transportPeerId : scheduledBeforePoll) {
        const auto found = hostPeers.find(transportPeerId);
        if (found == hostPeers.end()
            || found->second.closeAfterPolls < 0) {
            continue;
        }
        --found->second.closeAfterPolls;
        if (found->second.closeAfterPolls <= 0) {
            server.disconnect(transportPeerId);
            handleHostPeerGone(
                transportPeerId,
                RuntimeMultiplayerSessionAbortReason::PeerDisconnected,
                "LAN coordinator closed the connection",
                result.events
            );
        }
    }
    if (phase == RuntimeMultiplayerLanPhase::Running
        && !suspendedPeers.empty()) {
        const auto now = std::chrono::steady_clock::now();
        const auto expired = std::find_if(
            suspendedPeers.begin(),
            suspendedPeers.end(),
            [now](const auto& entry) {
                return now >= entry.second.deadline;
            }
        );
        if (expired != suspendedPeers.end()) {
            const std::string affectedPeerId = expired->first;
            RuntimeMultiplayerLanEvent failed = event(
                RuntimeMultiplayerLanEventKind::ReconnectFailed
            );
            failed.peerId = affectedPeerId;
            failed.tick = session.authoritativeTick();
            failed.reason = "combatant reconnect grace period expired";
            result.events.push_back(std::move(failed));
            (void)hostAbort(
                affectedPeerId,
                RuntimeMultiplayerSessionAbortReason::TransportFailure
            );
            std::vector<RuntimeMultiplayerLanEvent> terminalEvents;
            terminalEvents.swap(pendingEvents);
            result.events.insert(
                result.events.end(),
                std::make_move_iterator(terminalEvents.begin()),
                std::make_move_iterator(terminalEvents.end())
            );
            reconnectDeferredHostPackets.clear();
            reconnectDeferredHostBytes = 0;
        }
    }
    refreshReconnectRecoveryState();
    sendHostSnapshotChunks(result.events);
    sendHostLobbyResumeChunks(result.events);
    sendHostLobbyMapChunks(result.events);
    return result;
}

RuntimeMultiplayerLanPollResult
RuntimeMultiplayerLanSession::Impl::pollClient(int timeoutMilliseconds) {
    RuntimeMultiplayerLanPollResult result;
    result.events.swap(pendingEvents);
    checkClientLobbyResumeDeadline(result.events);
    checkClientLobbyMapDeadline(result.events);

    if (browser.isOpen()) {
        std::string browserError;
        const int processed = browser.poll(0, browserError);
        if (processed > 0) {
            result.events.push_back(event(
                RuntimeMultiplayerLanEventKind::RoomsUpdated
            ));
        }
        if (!browserError.empty()) {
            result.succeeded = false;
            result.error = browserError;
        }
    }

    if (phase == RuntimeMultiplayerLanPhase::Reconnecting
        && !client.isConnected()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= reconnectDeadline) {
            failClientReconnect(
                "combatant reconnect grace period expired",
                result.events
            );
            return result;
        }
        if (pendingClientCanonicalPackets.empty()
            && now >= nextReconnectAttempt) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds
            >(reconnectDeadline - now).count();
            const int attemptTimeout = static_cast<int>(std::max<long long>(
                0,
                std::min<long long>({
                    remaining,
                    joinOptions.connectTimeoutMilliseconds,
                    runtimeMultiplayerLanReconnectRetryMilliseconds
                })
            ));
            std::string reconnectError;
            ++reconnectAttemptCount;
            if (client.connect(
                    joinOptions.room.hostAddress,
                    joinOptions.room.sessionPort,
                    attemptTimeout,
                    joinOptions.localProxyPrelude,
                    reconnectError
                )) {
                challengeReceived = false;
                joinAccepted = false;
                admissionToken = 0;
                RuntimeMultiplayerLanEvent connected = event(
                    RuntimeMultiplayerLanEventKind::TransportConnected
                );
                connected.peerId = localPeerId;
                connected.reason = client.peerAddress();
                result.events.push_back(std::move(connected));
            } else {
                nextReconnectAttempt = now + std::chrono::milliseconds(
                    runtimeMultiplayerLanReconnectRetryMilliseconds
                );
            }
        }
    }

    bool simulationBlocked = false;
        const auto clientMayHandleCanonical = [this]() {
        return phase != RuntimeMultiplayerLanPhase::Disconnected
            && phase != RuntimeMultiplayerLanPhase::JoinRejected
            && phase != RuntimeMultiplayerLanPhase::Kicked
            && phase != RuntimeMultiplayerLanPhase::Aborted;
    };
    const auto processCanonicalPacket = [
        this,
        &result,
        &simulationBlocked
    ](const RuntimeNetworkPacket& packet) {
        clientCanonicalPacketDeferred = false;
        handleClientPacket(packet, result.events);
        if (clientCanonicalPacketDeferred) {
            simulationBlocked = true;
            result.simulationDeferred = true;
        }
    };
    const auto retainCanonicalPacket = [this, &result](
        const RuntimeNetworkPacket& packet
    ) {
        std::string queueError;
        if (enqueuePendingClientCanonicalPacket(packet, queueError)) {
            return true;
        }
        clientProtocolFailure(std::move(queueError), result.events);
        return false;
    };

    if (!clientMayHandleCanonical()) {
        clearPendingClientCanonicalPackets();
    }
    while (clientMayHandleCanonical()
        && !pendingClientCanonicalPackets.empty()
        && !simulationBlocked) {
        processCanonicalPacket(pendingClientCanonicalPackets.front());
        if (!simulationBlocked) {
            popPendingClientCanonicalPacket();
        }
    }
    if (!clientMayHandleCanonical()) {
        clearPendingClientCanonicalPackets();
        simulationBlocked = false;
        result.simulationDeferred = false;
    }
    if (!client.isConnected()) return result;

    std::string clientError;
    const std::vector<RuntimeTcpEvent> transportEvents =
        client.poll(
            simulationBlocked ? 0 : timeoutMilliseconds,
            clientError
        );
    if (!clientError.empty()) {
        result.succeeded = false;
        if (result.error.empty()) result.error = clientError;
        RuntimeMultiplayerLanEvent transportError = event(
            RuntimeMultiplayerLanEventKind::TransportError
        );
        transportError.reason = clientError;
        result.events.push_back(std::move(transportError));
    }
    for (const RuntimeTcpEvent& transportEvent : transportEvents) {
        if (transportEvent.kind == RuntimeTcpEventKind::Packet) {
            if (!clientMayHandleCanonical()) {
                continue;
            }
            if (simulationBlocked) {
                (void)retainCanonicalPacket(transportEvent.packet);
            } else {
                processCanonicalPacket(transportEvent.packet);
                if (simulationBlocked) {
                    (void)retainCanonicalPacket(transportEvent.packet);
                }
            }
            continue;
        }
        if (transportEvent.kind == RuntimeTcpEventKind::Error) {
            RuntimeMultiplayerLanEvent transportError = event(
                RuntimeMultiplayerLanEventKind::TransportError
            );
            transportError.reason = transportEvent.message;
            result.events.push_back(std::move(transportError));
            continue;
        }
        if (transportEvent.kind != RuntimeTcpEventKind::Disconnected) {
            continue;
        }

        if (phase == RuntimeMultiplayerLanPhase::Reconnecting) {
            client.close();
            challengeReceived = false;
            joinAccepted = false;
            admissionToken = 0;
            nextReconnectAttempt = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(
                    runtimeMultiplayerLanReconnectRetryMilliseconds
                );
            continue;
        }

        const bool localPeerStillParticipates = std::any_of(
            session.members().begin(),
            session.members().end(),
            [this](const RuntimeMultiplayerRoomMember& member) {
                return member.peerId == localPeerId;
            }
        );
        if (session.status() == RuntimeMultiplayerRoomStatus::Running
            && localPeerStillParticipates) {
            if (beginClientReconnect(
                    transportEvent.message,
                    result.events
                )) {
                continue;
            }
            RuntimeMultiplayerSessionAbort abort;
            abort.requestingPeerId = session.config().hostPeerId;
            abort.affectedPeerId = session.config().hostPeerId;
            abort.tick = session.authoritativeTick();
            abort.reason =
                RuntimeMultiplayerSessionAbortReason::HostDisconnected;
            const RuntimeMultiplayerOperationResult applied =
                session.abortRunningSession(abort);
            if (applied.accepted) {
                phase = RuntimeMultiplayerLanPhase::Aborted;
                clearSnapshotTransfer(true);
                RuntimeMultiplayerLanEvent aborted = event(
                    RuntimeMultiplayerLanEventKind::SessionAborted,
                    RuntimeMultiplayerMessageType::SessionAbort
                );
                aborted.peerId = session.config().hostPeerId;
                aborted.tick = session.authoritativeTick();
                aborted.reason = transportEvent.message;
                result.events.push_back(std::move(aborted));
            }
        } else if (phase != RuntimeMultiplayerLanPhase::JoinRejected
            && phase != RuntimeMultiplayerLanPhase::Kicked
            && phase != RuntimeMultiplayerLanPhase::Aborted) {
            phase = RuntimeMultiplayerLanPhase::Disconnected;
        }
        RuntimeMultiplayerLanEvent disconnected = event(
            RuntimeMultiplayerLanEventKind::Disconnected
        );
        disconnected.peerId = session.active()
            ? session.config().hostPeerId
            : std::string{};
        disconnected.reason = transportEvent.message;
        result.events.push_back(std::move(disconnected));
    }
    if (!clientMayHandleCanonical()) {
        clearPendingClientCanonicalPackets();
        clientCanonicalPacketDeferred = false;
        result.simulationDeferred = false;
    }
    refreshReconnectRecoveryState();
    return result;
}

RuntimeMultiplayerLanPollResult
RuntimeMultiplayerLanSession::Impl::pollBrowser(int timeoutMilliseconds) {
    RuntimeMultiplayerLanPollResult result;
    result.events.swap(pendingEvents);
    std::string browserError;
    const int processed = browser.poll(timeoutMilliseconds, browserError);
    if (processed > 0) {
        result.events.push_back(event(
            RuntimeMultiplayerLanEventKind::RoomsUpdated
        ));
    }
    if (!browserError.empty()) {
        result.succeeded = false;
        result.error = std::move(browserError);
    }
    return result;
}

RuntimeMultiplayerLanSession::RuntimeMultiplayerLanSession()
    : impl_(std::make_unique<Impl>()) {}

RuntimeMultiplayerLanSession::~RuntimeMultiplayerLanSession() = default;
RuntimeMultiplayerLanSession::RuntimeMultiplayerLanSession(
    RuntimeMultiplayerLanSession&&
) noexcept = default;
RuntimeMultiplayerLanSession& RuntimeMultiplayerLanSession::operator=(
    RuntimeMultiplayerLanSession&&
) noexcept = default;

bool RuntimeMultiplayerLanSession::setCommandPayloadValidator(
    RuntimeMultiplayerLanCommandPayloadValidator validator,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->role != RuntimeMultiplayerLanRole::None
        || (impl_->phase != RuntimeMultiplayerLanPhase::Inactive
            && impl_->phase != RuntimeMultiplayerLanPhase::Browsing)) {
        errorMessage =
            "command payload validator must be set before hosting or joining";
        return false;
    }
    impl_->commandPayloadValidator = std::move(validator);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLanSession::setSimulationHooks(
    RuntimeMultiplayerLanSimulationHooks hooks,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->role != RuntimeMultiplayerLanRole::None
        || (impl_->phase != RuntimeMultiplayerLanPhase::Inactive
            && impl_->phase != RuntimeMultiplayerLanPhase::Browsing)) {
        errorMessage =
            "simulation hooks must be set before hosting or joining";
        return false;
    }
    if (!hooks.empty() && !hooks.complete()) {
        errorMessage =
            "simulation hooks must be configured as one complete set";
        return false;
    }
    impl_->simulationHooks = std::move(hooks);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLanSession::setSnapshotHooks(
    RuntimeMultiplayerLanSnapshotHooks hooks,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->role != RuntimeMultiplayerLanRole::None
        || (impl_->phase != RuntimeMultiplayerLanPhase::Inactive
            && impl_->phase != RuntimeMultiplayerLanPhase::Browsing)) {
        errorMessage =
            "snapshot hooks must be set before hosting or joining";
        return false;
    }
    if (!hooks.empty() && !hooks.complete()) {
        errorMessage =
            "snapshot hooks must be configured as one complete set";
        return false;
    }
    impl_->snapshotHooks = std::move(hooks);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLanSession::startHost(
    RuntimeMultiplayerLanHostOptions options,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();
    errorMessage.clear();
    if (options.reconnectGraceMilliseconds
        > runtimeMultiplayerMaximumReconnectGraceMilliseconds) {
        errorMessage = "LAN reconnect grace exceeds the hard limit";
        runtimeRoomPasswordSecureClear(options.password);
        return false;
    }
    impl_->reconnectGraceMilliseconds =
        options.reconnectGraceMilliseconds;
    if (options.passwordProtected && options.password.empty()) {
        errorMessage = "password-protected LAN rooms require a password";
        runtimeRoomPasswordSecureClear(options.password);
        return false;
    }
    if (!options.password.empty()) options.passwordProtected = true;
    if (options.passwordProtected
        && !runtimeRoomPasswordValid(options.password)) {
        errorMessage = "LAN room password is empty or exceeds 128 bytes";
        runtimeRoomPasswordSecureClear(options.password);
        return false;
    }
    impl_->hostPasswordProtected = options.passwordProtected;
    if (impl_->hostPasswordProtected) {
        impl_->hostPasswordVerifier = runtimeRoomPasswordVerifier(
            options.session.roomId,
            options.password
        );
    }
    runtimeRoomPasswordSecureClear(options.password);
    const std::size_t hostCombatants = options.session.hostParticipation
            == RuntimeMultiplayerParticipationRole::Combatant
        ? 1U
        : 0U;
    const std::size_t hostSpectators = options.session.hostParticipation
            == RuntimeMultiplayerParticipationRole::Spectator
        ? 1U
        : 0U;
    if (options.maximumCombatants < hostCombatants
        || options.maximumSpectators < hostSpectators
        || options.maximumCombatants > runtimeMultiplayerMaximumCombatants
        || options.maximumSpectators > runtimeMultiplayerMaximumSpectators) {
        errorMessage = "invalid role-aware LAN admission limits";
        impl_->reset();
        return false;
    }
    // Older/direct callers commonly leave the role limits at their global
    // defaults while selecting a smaller combined roster. Preserve that API:
    // the deterministic session ceiling still limits the total roster, while
    // each role policy is narrowed to a meaningful value for that room.
    impl_->maximumCombatants = std::min(
        options.maximumCombatants,
        options.session.maximumMembers
    );
    impl_->maximumSpectators = std::min(
        options.maximumSpectators,
        options.session.maximumMembers
    );
    const RuntimeMultiplayerOperationResult begun =
        impl_->session.beginHost(std::move(options.session));
    if (!begun.accepted) {
        errorMessage = begun.reason;
        impl_->reset();
        return false;
    }
    // Keep a bounded number of admission sockets beyond the canonical roster
    // ceiling so excess peers receive JoinRejected instead of being able to
    // make the host lobby fail after transport acceptance.
    const std::size_t maximumClients = runtimeLanMaximumTcpClients;
    if (!impl_->server.open(
            options.sessionPort,
            maximumClients,
            options.sessionBindScope,
            errorMessage
        )) {
        impl_->reset();
        return false;
    }
    impl_->boundSessionPort = impl_->server.port();
    impl_->roomGeneration = options.roomGeneration == 0
        ? impl_->makeNonce()
        : options.roomGeneration;
    impl_->advertisement.roomId = impl_->session.config().roomId;
    impl_->advertisement.roomName = options.roomName.empty()
        ? impl_->session.config().roomId
        : std::move(options.roomName);
    impl_->advertisement.hostName =
        impl_->session.config().hostDisplayName;
    impl_->advertisement.contentFingerprint = contentFingerprintText(
        impl_->session.config().compatibility.contentFingerprint
    );
    impl_->advertisement.protocolVersion = runtimeLanProtocolVersion;
    impl_->advertisement.sessionPort = impl_->boundSessionPort;
    impl_->advertisement.playerCount = 1;
    impl_->advertisement.maximumPlayers = static_cast<std::uint8_t>(
        impl_->maximumCombatants
    );
    impl_->advertisement.passwordProtected =
        impl_->hostPasswordProtected;
    if (options.enableDiscovery) {
        if (!impl_->discoveryHost.open(
                options.discoveryPort,
                impl_->advertisement,
                errorMessage
            )) {
            impl_->reset();
            return false;
        }
        impl_->boundDiscoveryPort = impl_->discoveryHost.port();
    } else {
        impl_->boundDiscoveryPort = 0;
    }

    impl_->role = RuntimeMultiplayerLanRole::Host;
    impl_->phase = RuntimeMultiplayerLanPhase::Lobby;
    impl_->localPeerId = impl_->session.config().hostPeerId;
    impl_->pendingEvents.push_back(impl_->event(
        RuntimeMultiplayerLanEventKind::LobbyChanged,
        RuntimeMultiplayerMessageType::RoomConfig
    ));
    return true;
}

bool RuntimeMultiplayerLanSession::startBrowsing(
    std::uint16_t discoveryPort,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();
    if (!impl_->browser.open(discoveryPort, errorMessage)) return false;
    impl_->phase = RuntimeMultiplayerLanPhase::Browsing;
    return true;
}

bool RuntimeMultiplayerLanSession::sendDiscoveryProbe(
    const std::string& targetAddress,
    std::string& errorMessage
) {
    if (!impl_) {
        errorMessage = "LAN session is inactive";
        return false;
    }
    return impl_->browser.sendProbe(targetAddress, errorMessage);
}

std::vector<RuntimeLanRoomAdvertisement>
RuntimeMultiplayerLanSession::discoveredRooms(
    std::uint64_t maximumAgeMilliseconds
) const {
    if (!impl_) return {};
    return impl_->browser.rooms(maximumAgeMilliseconds);
}

bool RuntimeMultiplayerLanSession::join(
    RuntimeMultiplayerLanJoinOptions options,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    errorMessage.clear();
    RoomPasswordStringClearGuard passwordClear(options.password);
    if (impl_->phase != RuntimeMultiplayerLanPhase::Inactive
        && impl_->phase != RuntimeMultiplayerLanPhase::Browsing) {
        errorMessage = "LAN session is already active";
        return false;
    }
    if (options.room.hostAddress.empty()
        || options.room.roomId.empty()
        || options.room.hostName.empty()
        || options.room.sessionPort == 0
        || options.room.protocolVersion != runtimeLanProtocolVersion) {
        errorMessage = "selected LAN room advertisement is invalid";
        return false;
    }
    if (options.room.contentFingerprint
        != contentFingerprintText(options.compatibility.contentFingerprint)) {
        errorMessage = "selected LAN room content fingerprint does not match";
        return false;
    }
    const bool passwordProofRequired = options.room.passwordProtected
        && options.localProxyPrelude.empty();
    RuntimeRoomPasswordVerifier passwordVerifier{};
    if (passwordProofRequired) {
        if (!runtimeRoomPasswordValid(options.password)) {
            errorMessage =
                "selected LAN room requires a password of at most 128 bytes";
            return false;
        }
        passwordVerifier = runtimeRoomPasswordVerifier(
            options.room.roomId,
            options.password
        );
    }
    runtimeRoomPasswordSecureClear(options.password);
    RuntimeMultiplayerMessage validationMessage;
    validationMessage.type = RuntimeMultiplayerMessageType::JoinRequest;
    RuntimeMultiplayerJoinRequest validationRequest;
    validationRequest.peerId = options.peerId;
    validationRequest.displayName = options.displayName;
    validationRequest.participation = options.participation;
    validationRequest.compatibility = options.compatibility;
    validationMessage.body = std::move(validationRequest);
    RuntimeNetworkPacket ignoredPacket;
    if (!encodeRuntimeMultiplayerMessage(
            validationMessage,
            ignoredPacket,
            errorMessage
        )) {
        runtimeRoomPasswordSecureClear(passwordVerifier);
        return false;
    }
    if (!impl_->client.connect(
            options.room.hostAddress,
            options.room.sessionPort,
            options.connectTimeoutMilliseconds,
            options.localProxyPrelude,
            errorMessage
        )) {
        runtimeRoomPasswordSecureClear(passwordVerifier);
        return false;
    }

    impl_->session.reset();
    runtimeRoomPasswordSecureClear(impl_->joinPasswordVerifier);
    impl_->joinPasswordVerifier = passwordVerifier;
    runtimeRoomPasswordSecureClear(passwordVerifier);
    impl_->joinPasswordProofRequired = passwordProofRequired;
    impl_->joinOptions = std::move(options);
    impl_->localPeerId = impl_->joinOptions.peerId;
    impl_->boundSessionPort = impl_->joinOptions.room.sessionPort;
    impl_->roomGeneration = 0;
    impl_->admissionToken = 0;
    impl_->challengeReceived = false;
    impl_->joinAccepted = false;
    impl_->role = RuntimeMultiplayerLanRole::Client;
    impl_->phase = RuntimeMultiplayerLanPhase::Joining;
    RuntimeMultiplayerLanEvent connected = impl_->event(
        RuntimeMultiplayerLanEventKind::TransportConnected
    );
    connected.reason = impl_->client.peerAddress();
    impl_->pendingEvents.push_back(std::move(connected));
    return true;
}

RuntimeMultiplayerLanPollResult RuntimeMultiplayerLanSession::poll(
    int timeoutMilliseconds
) {
    if (!impl_) {
        RuntimeMultiplayerLanPollResult result;
        result.succeeded = false;
        result.error = "LAN session object has been moved from";
        return result;
    }
    if (timeoutMilliseconds < 0) {
        RuntimeMultiplayerLanPollResult result;
        result.succeeded = false;
        result.error = "LAN poll timeout must not be negative";
        return result;
    }
    if (impl_->role == RuntimeMultiplayerLanRole::Host) {
        return impl_->pollHost(timeoutMilliseconds);
    }
    if (impl_->role == RuntimeMultiplayerLanRole::Client) {
        return impl_->pollClient(timeoutMilliseconds);
    }
    if (impl_->phase == RuntimeMultiplayerLanPhase::Browsing) {
        return impl_->pollBrowser(timeoutMilliseconds);
    }
    RuntimeMultiplayerLanPollResult result;
    result.events.swap(impl_->pendingEvents);
    return result;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::setMatchSetup(
    RuntimeMultiplayerMatchSetup setup
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation(
            "only the LAN lobby host may set the match setup"
        );
    }

    const RuntimeMultiplayerOperationResult applied =
        impl_->session.setMatchSetup(
            impl_->localPeerId,
            std::move(setup)
        );
    if (!applied.accepted) {
        return failedOperation(applied.reason);
    }

    std::string broadcastError;
    const bool broadcastQueued = impl_->broadcastLobbySnapshot(
        broadcastError
    );
    RuntimeMultiplayerLanEvent changed = impl_->event(
        RuntimeMultiplayerLanEventKind::LobbyChanged,
        RuntimeMultiplayerMessageType::LobbySnapshot
    );
    impl_->pendingEvents.push_back(std::move(changed));

    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = broadcastQueued;
    result.applied = true;
    result.queued = broadcastQueued;
    result.duplicate = applied.duplicate;
    result.reason = std::move(broadcastError);
    return result;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::requestLobbySlotOptions(
    std::size_t slotIndex,
    std::string countryId,
    std::string colorId,
    std::string teamId,
    std::string difficultyId,
    std::uint8_t startPosition
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby
        || !impl_->session.matchSetup().has_value()) {
        return failedOperation(
            "lobby slot options require a joined LAN lobby client"
        );
    }
    if (slotIndex > std::numeric_limits<std::uint8_t>::max()
        || impl_->nextLobbySlotRequestId == 0
        || impl_->nextLobbySlotRequestId
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
            )) {
        return failedOperation("lobby slot request identity overflow");
    }
    RuntimeMultiplayerLobbySlotOptionsRequestMessage request;
    request.peerId = impl_->localPeerId;
    request.requestId = impl_->nextLobbySlotRequestId++;
    request.expectedSetupRevision =
        impl_->session.matchSetup()->revision;
    request.slotIndex = static_cast<std::uint8_t>(slotIndex);
    request.countryId = std::move(countryId);
    request.colorId = std::move(colorId);
    request.teamId = std::move(teamId);
    request.difficultyId = std::move(difficultyId);
    request.startPosition = startPosition;
    return sendLobbySlotOptionsRequest(std::move(request));
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::sendLobbySlotOptionsRequest(
    RuntimeMultiplayerLobbySlotOptionsRequestMessage request
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation(
            "only a joined LAN lobby client may request slot options"
        );
    }
    RuntimeMultiplayerMessage message;
    message.type =
        RuntimeMultiplayerMessageType::LobbySlotOptionsRequest;
    message.body = request;
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    impl_->lastLobbySlotRequestIdSent = std::max(
        impl_->lastLobbySlotRequestIdSent,
        request.requestId
    );
    if (request.requestId >= impl_->nextLobbySlotRequestId
        && request.requestId
            < static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
            )) {
        impl_->nextLobbySlotRequestId = request.requestId + 1U;
    }
    RuntimeMultiplayerLanOperationResult result = queuedOperation();
    result.requestId = request.requestId;
    result.setupRevision = request.expectedSetupRevision;
    return result;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::resolveLobbySlotOptionsRequest(
    const std::string& peerId,
    std::uint64_t requestId,
    bool accepted,
    std::string reason
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation(
            "only the LAN lobby host may resolve slot requests"
        );
    }
    const auto peer = std::find_if(
        impl_->hostPeers.begin(),
        impl_->hostPeers.end(),
        [&](const auto& entry) {
            return entry.second.admitted
                && entry.second.logicalPeerId == peerId;
        }
    );
    if (peer == impl_->hostPeers.end()
        || !peer->second.pendingLobbySlotRequest.has_value()
        || peer->second.pendingLobbySlotRequest->requestId != requestId) {
        return failedOperation("pending lobby slot request does not exist");
    }
    const RuntimeMultiplayerLobbySlotOptionsRequestMessage request =
        *peer->second.pendingLobbySlotRequest;
    if (accepted) {
        const RuntimeMultiplayerMatchSetup* setup =
            impl_->session.matchSetup().has_value()
            ? &*impl_->session.matchSetup()
            : nullptr;
        const RuntimeMultiplayerMatchSlot* slot =
            setup != nullptr && request.slotIndex < setup->slots.size()
            ? &setup->slots[request.slotIndex]
            : nullptr;
        const bool slotOwned = slot != nullptr
            && slot->controller
                == RuntimeMultiplayerMatchSlotController::Human
            && slot->peerId == request.peerId;
        const bool optionsMatch = slotOwned
            && slot->countryId == request.countryId
            && slot->colorId == request.colorId
            && slot->teamId == request.teamId
            && slot->difficultyId == request.difficultyId
            && slot->startPosition == request.startPosition;
        if (setup == nullptr
            || setup->revision < request.expectedSetupRevision
            || !slotOwned
            || (setup->revision == request.expectedSetupRevision
                && !optionsMatch)) {
            accepted = false;
            reason = "authoritative lobby setup did not apply the request";
        }
    }
    peer->second.pendingLobbySlotRequest.reset();
    const bool sent = impl_->sendLobbySlotOptionsResult(
        peer->first,
        requestId,
        accepted,
        std::move(reason),
        impl_->pendingEvents
    );
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = sent;
    result.applied = accepted;
    result.queued = sent;
    result.requestId = requestId;
    result.setupRevision = impl_->currentLobbySetupRevision();
    if (!sent) result.reason = "lobby slot result could not be sent";
    return result;
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLanSession::setReady(
    bool ready
) {
    if (!impl_ || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation("ready state may change only in a LAN lobby");
    }
    if (ready && !lobbyResumeSnapshotReady()) return failedOperation("resume snapshot transfer is incomplete");
    if (ready && impl_->lobbyMapTransfer.clientIncomplete(impl_->session.matchSetup()
        ? &impl_->session.matchSetup()->map : nullptr)) return failedOperation("requested map transfer is incomplete");
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::Ready;
    message.body = RuntimeMultiplayerReadyMessage{
        impl_->localPeerId,
        ready
    };
    if (impl_->role == RuntimeMultiplayerLanRole::Host) {
        return impl_->applyHostMessage(std::move(message));
    }
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    return queuedOperation();
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::startMatch(int startTick) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation("only the LAN lobby host may start the match");
    }
    if (!impl_->mapTransfersComplete()) return failedOperation("waiting for requested map transfers to complete");
    if (impl_->lobbyResumeImage) {
        std::uint64_t fingerprint = 0;
        std::string error;
        if (!lobbyResumeAllPeersReady() || !impl_->session.matchSetup()
            || !runtimeMultiplayerGameplaySetupFingerprint(*impl_->session.matchSetup(), fingerprint, error)
            || fingerprint != impl_->lobbyResumeImage->identity.gameplaySetupFingerprint
            || startTick != impl_->lobbyResumeImage->identity.snapshotTick + 1)
            return failedOperation("resume start requires all saved seats, verified snapshots and the saved next tick");
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::Start;
    message.body = RuntimeMultiplayerStartMessage{
        impl_->localPeerId,
        startTick
    };
    return impl_->applyHostMessage(std::move(message));
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::submitCommand(
    int tick,
    std::uint64_t sequence,
    std::vector<std::uint8_t> payload
) {
    if (!impl_ || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation("commands require a running LAN match");
    }
    if (impl_->role == RuntimeMultiplayerLanRole::Host
        && (!impl_->suspendedPeers.empty()
            || impl_->reconnectRecoveryPending)) {
        return deferredOperation(
            "commands are paused for reconnect snapshot recovery"
        );
    }
    RuntimeMultiplayerCommandPacket command;
    command.tick = tick;
    command.peerId = impl_->localPeerId;
    command.sequence = sequence;
    command.payload = std::move(payload);
    command.timelineEpoch = impl_->session.timelineEpoch();
    if (impl_->role == RuntimeMultiplayerLanRole::Client) {
        std::string validationError;
        if (!impl_->validateCommandPayload(command, validationError)) {
            return failedOperation(std::move(validationError));
        }
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::Command;
    message.body = std::move(command);
    if (impl_->role == RuntimeMultiplayerLanRole::Host) {
        return impl_->applyHostMessage(std::move(message));
    }
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    return queuedOperation();
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::advanceAuthoritativeTick(int tick) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation("only a running LAN host may advance the tick");
    }
    if (!impl_->suspendedPeers.empty()
        || impl_->reconnectRecoveryPending) {
        return deferredOperation(
            "authoritative advancement is paused for reconnect recovery"
        );
    }
    RuntimeMultiplayerMessage message;
    message.type =
        RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick;
    message.body = RuntimeMultiplayerAdvanceTickMessage{
        impl_->localPeerId,
        tick,
        impl_->session.timelineEpoch()
    };
    return impl_->applyHostMessage(std::move(message));
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::submitStateHash(
    int tick,
    std::uint64_t stateHash
) {
    if (!impl_ || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation("state hashes require a running LAN match");
    }
    if (impl_->role == RuntimeMultiplayerLanRole::Host
        && (!impl_->suspendedPeers.empty()
            || impl_->reconnectRecoveryPending)) {
        return deferredOperation(
            "state hashes are paused for reconnect recovery"
        );
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::StateHash;
    message.body = RuntimeMultiplayerStateHashReport{
        tick,
        impl_->localPeerId,
        stateHash,
        impl_->session.timelineEpoch()
    };
    if (impl_->role == RuntimeMultiplayerLanRole::Host) {
        return impl_->applyHostMessage(std::move(message));
    }
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    return queuedOperation();
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::proposeSaveLoad(
    RuntimeMultiplayerSaveLoadProposal proposal
) {
    (void)proposal;
    return failedOperation(
        "snapshot bytes are required for a LAN save/load proposal"
    );
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::proposeSaveLoad(
    RuntimeMultiplayerSaveLoadProposal proposal,
    std::vector<std::uint8_t> bytes
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation(
            "only a running LAN host may propose save or load"
        );
    }
    if (!impl_->snapshotHooks.complete()) {
        return failedOperation(
            "complete snapshot hooks are required for save/load"
        );
    }
    if (impl_->snapshotTransferActive) {
        return failedOperation("another snapshot transfer is active");
    }
    proposal.proposerPeerId = impl_->localPeerId;
    proposal.timelineEpoch = impl_->session.timelineEpoch();
    proposal.descriptor.identity.sourceTimelineEpoch =
        impl_->session.timelineEpoch();

    std::string errorMessage;
    RuntimeMultiplayerMessage proposalMessage;
    proposalMessage.type = RuntimeMultiplayerMessageType::SaveLoadProposal;
    proposalMessage.body = proposal;
    RuntimeNetworkPacket proposalPacket;
    if (!impl_->encode(
            proposalMessage,
            proposalPacket,
            errorMessage
        )) {
        return failedOperation(std::move(errorMessage));
    }
    RuntimeMultiplayerSessionState proposedSession = impl_->session;
    RuntimeMultiplayerMessageApplyResult proposalApplied =
        applyRuntimeMultiplayerMessage(
            proposalPacket,
            impl_->localPeerId,
            proposedSession
        );
    if (!proposalApplied.accepted) {
        return appliedOperation(proposalApplied, false, {});
    }

    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            proposal.descriptor,
            errorMessage
        )
        || bytes.size()
            != static_cast<std::size_t>(proposal.descriptor.byteCount)
        || runtimeMultiplayerSnapshotFingerprint(bytes)
            != proposal.descriptor.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes)
            != proposal.descriptor.sha256) {
        if (errorMessage.empty()) {
            errorMessage =
                "snapshot bytes do not exactly match the descriptor";
        }
        return failedOperation(std::move(errorMessage));
    }

    std::vector<std::uint64_t> remotePeers;
    for (const RuntimeMultiplayerRoomMember& member :
            impl_->session.members()) {
        if (member.peerId == impl_->localPeerId) continue;
        const auto found = std::find_if(
            impl_->hostPeers.begin(),
            impl_->hostPeers.end(),
            [&member](const auto& entry) {
                return entry.second.admitted
                    && entry.second.logicalPeerId == member.peerId;
            }
        );
        if (found == impl_->hostPeers.end()) {
            return failedOperation(
                "snapshot participant is not bound to the LAN room"
            );
        }
        remotePeers.push_back(found->first);
    }
    if (!remotePeers.empty()
        && bytes.size()
            > runtimeMultiplayerSnapshotHardMaximumBytes
                / remotePeers.size()) {
        return failedOperation(
            "snapshot producer aggregate memory limit is exceeded"
        );
    }

    impl_->snapshotTransferActive = true;
    impl_->snapshotProposal = proposal;
    impl_->snapshotStaged = true;
    std::uint64_t observedHash = 0;
    if (!impl_->stageSnapshotBytes(
            proposal,
            bytes,
            observedHash,
            errorMessage
        )) {
        impl_->clearSnapshotTransfer(true);
        (void)impl_->hostAbort(
            impl_->localPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation
        );
        return failedOperation(std::move(errorMessage));
    }
    impl_->stagedAuthoritativeStateHash = observedHash;

    for (const std::uint64_t transportPeerId : remotePeers) {
        RuntimeMultiplayerSnapshotTransferResult begun =
            impl_->hostPeers[transportPeerId].snapshotProducer.begin(
                proposal.descriptor,
                bytes
            );
        if (!begun.accepted) {
            errorMessage = begun.reason;
            impl_->clearSnapshotTransfer(true);
            (void)impl_->hostAbort(
                impl_->localPeerId,
                RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            );
            return failedOperation(std::move(errorMessage));
        }
    }

    impl_->session = std::move(proposedSession);
    const bool proposalBroadcast = impl_->broadcastPacket(
        proposalPacket,
        errorMessage
    );
    impl_->appendAppliedEvent(
        proposalMessage,
        proposalApplied,
        impl_->pendingEvents
    );
    if (!proposalBroadcast) {
        impl_->clearSnapshotTransfer(true);
        (void)impl_->hostAbort(
            impl_->localPeerId,
            RuntimeMultiplayerSessionAbortReason::TransportFailure
        );
        return appliedOperation(
            proposalApplied,
            false,
            std::move(errorMessage)
        );
    }

    RuntimeMultiplayerSaveLoadResponseMessage prepare;
    prepare.timelineEpoch = impl_->session.timelineEpoch();
    prepare.proposalId = proposal.proposalId;
    prepare.peerId = impl_->localPeerId;
    prepare.approved = true;
    prepare.observedFingerprint = proposal.descriptor.snapshotFingerprint;
    prepare.observedAuthoritativeStateHash = observedHash;
    RuntimeMultiplayerMessage prepareMessage;
    prepareMessage.type = RuntimeMultiplayerMessageType::SaveLoadPrepare;
    prepareMessage.body = std::move(prepare);
    RuntimeMultiplayerLanOperationResult prepared =
        impl_->applyHostMessage(std::move(prepareMessage));
    if (!prepared.succeeded) {
        impl_->clearSnapshotTransfer(true);
        (void)impl_->hostAbort(
            impl_->localPeerId,
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation
        );
    }
    return prepared;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::respondToSaveLoad(
    std::uint64_t proposalId,
    bool approved,
    std::uint64_t observedFingerprint
) {
    if (!impl_ || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation(
            "save/load responses require a running LAN match"
        );
    }
    RuntimeMultiplayerSaveLoadResponseMessage response;
    response.proposalId = proposalId;
    response.peerId = impl_->localPeerId;
    response.timelineEpoch = impl_->session.timelineEpoch();
    response.approved = approved;
    response.observedFingerprint = observedFingerprint;
    if (approved) {
        if (!impl_->snapshotTransferActive || !impl_->snapshotStaged
            || impl_->snapshotProposal.proposalId != proposalId) {
            return failedOperation(
                "approved prepare requires verified staged snapshot bytes"
            );
        }
        response.observedAuthoritativeStateHash =
            impl_->stagedAuthoritativeStateHash;
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::SaveLoadResponse;
    message.body = std::move(response);
    if (impl_->role == RuntimeMultiplayerLanRole::Host) {
        return impl_->applyHostMessage(std::move(message));
    }
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    return queuedOperation();
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::abortSaveLoad(std::uint64_t proposalId) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation(
            "only a running LAN host may abort save or load"
        );
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::SaveLoadAbort;
    message.body = RuntimeMultiplayerSaveLoadAbortMessage{
        proposalId,
        impl_->localPeerId,
        impl_->session.timelineEpoch()
    };
    return impl_->applyHostMessage(std::move(message));
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLanSession::leave() {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client
        || (impl_->phase != RuntimeMultiplayerLanPhase::Lobby
            && impl_->phase != RuntimeMultiplayerLanPhase::Running)) {
        return failedOperation("only a joined LAN client may leave the room");
    }
    if (impl_->phase == RuntimeMultiplayerLanPhase::Running) {
        const auto member = std::find_if(
            impl_->session.members().begin(),
            impl_->session.members().end(),
            [this](const RuntimeMultiplayerRoomMember& candidate) {
                return candidate.peerId == impl_->localPeerId;
            }
        );
        if (member == impl_->session.members().end()
            || member->participation
                != RuntimeMultiplayerParticipationRole::Spectator) {
            return failedOperation(
                "only a spectator may leave a running LAN match"
            );
        }
    }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::Leave;
    message.body = RuntimeMultiplayerLeaveMessage{impl_->localPeerId};
    std::string errorMessage;
    if (!impl_->sendToHost(message, errorMessage)) {
        return failedOperation(std::move(errorMessage));
    }
    return queuedOperation();
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::dropRunningSpectator(
    const std::string& peerId,
    std::string reason
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation(
            "only a running LAN host may remove a spectator"
        );
    }
    const auto member = std::find_if(
        impl_->session.members().begin(),
        impl_->session.members().end(),
        [&peerId](const RuntimeMultiplayerRoomMember& candidate) {
            return candidate.peerId == peerId;
        }
    );
    if (member == impl_->session.members().end()
        || member->role != RuntimeMultiplayerRoomMemberRole::Client
        || member->participation
            != RuntimeMultiplayerParticipationRole::Spectator) {
        return failedOperation(
            "running spectator removal target is not a spectator client"
        );
    }
    const auto binding = std::find_if(
        impl_->hostPeers.begin(),
        impl_->hostPeers.end(),
        [&peerId](const auto& entry) {
            return entry.second.admitted
                && entry.second.logicalPeerId == peerId;
        }
    );
    if (binding == impl_->hostPeers.end()) {
        return failedOperation(
            "running spectator is not bound to a LAN transport"
        );
    }
    const std::uint64_t transportPeerId = binding->first;
    impl_->server.disconnect(transportPeerId);
    impl_->handleHostPeerGone(
        transportPeerId,
        RuntimeMultiplayerSessionAbortReason::PeerDisconnected,
        reason.empty() ? "spectator was removed by the host" : std::move(reason),
        impl_->pendingEvents
    );
    const bool removed = std::none_of(
        impl_->session.members().begin(),
        impl_->session.members().end(),
        [&peerId](const RuntimeMultiplayerRoomMember& candidate) {
            return candidate.peerId == peerId;
        }
    );
    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = removed
        && impl_->phase == RuntimeMultiplayerLanPhase::Running;
    result.applied = removed;
    if (!result.succeeded) {
        result.reason = "running spectator removal did not remain canonical";
    }
    return result;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLanSession::abortRunningSession(
    const std::string& affectedPeerId,
    RuntimeMultiplayerSessionAbortReason reason
) {
    if (!impl_) return failedOperation("LAN session is inactive");
    return impl_->hostAbort(affectedPeerId, reason);
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLanSession::kickPeer(
    const std::string& peerId,
    std::string reason
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation("only a LAN lobby host may kick a peer");
    }
    if (peerId == impl_->localPeerId) {
        return failedOperation("the lobby host may not kick itself");
    }
    const auto peer = std::find_if(
        impl_->hostPeers.begin(),
        impl_->hostPeers.end(),
        [&](const auto& entry) {
            return entry.second.admitted
                && entry.second.logicalPeerId == peerId;
        }
    );
    if (peer == impl_->hostPeers.end()) {
        return failedOperation("kick target is not an admitted lobby peer");
    }
    reason = safeLobbyResultReason(
        std::move(reason),
        "removed by the host"
    );
    RuntimeMultiplayerLobbyKickedMessage kicked;
    kicked.roomGeneration = impl_->roomGeneration;
    kicked.peerId = peerId;
    kicked.reason = reason;
    RuntimeMultiplayerMessage kickedMessage;
    kickedMessage.type = RuntimeMultiplayerMessageType::LobbyKicked;
    kickedMessage.body = kicked;
    std::string transportError;
    const bool kickedQueued = impl_->sendToPeer(
        peer->first,
        kickedMessage,
        transportError
    );

    RuntimeMultiplayerMessage leaveMessage;
    leaveMessage.type = RuntimeMultiplayerMessageType::Leave;
    leaveMessage.body = RuntimeMultiplayerLeaveMessage{peerId};
    RuntimeNetworkPacket leavePacket;
    std::string encodeError;
    RuntimeMultiplayerMessageApplyResult applied;
    if (impl_->encode(leaveMessage, leavePacket, encodeError)) {
        applied = applyTrustedCanonicalRuntimeMultiplayerMessage(
            leavePacket,
            impl_->session
        );
    }
    if (!applied.accepted) {
        return failedOperation(
            encodeError.empty() ? applied.reason : std::move(encodeError)
        );
    }
    peer->second.admitted = false;
    peer->second.kicked = true;
    peer->second.pendingLobbySlotRequest.reset();
    peer->second.closeAfterPolls = 2;
    std::string leaveError;
    const bool leaveQueued = impl_->broadcastPacket(
        leavePacket,
        leaveError
    );
    impl_->appendAppliedEvent(
        leaveMessage,
        applied,
        impl_->pendingEvents
    );
    RuntimeMultiplayerLanEvent kickedEvent = impl_->event(
        RuntimeMultiplayerLanEventKind::Kicked,
        RuntimeMultiplayerMessageType::LobbyKicked
    );
    kickedEvent.peerId = peerId;
    kickedEvent.reason = reason;
    impl_->pendingEvents.push_back(std::move(kickedEvent));
    std::string snapshotError;
    const bool snapshotQueued = impl_->broadcastLobbySnapshot(snapshotError);
    std::string advertisementError;
    const bool advertisementUpdated = impl_->updateAdvertisement(
        advertisementError
    );

    RuntimeMultiplayerLanOperationResult result;
    result.succeeded = kickedQueued && leaveQueued && snapshotQueued
        && advertisementUpdated;
    result.applied = true;
    result.queued = result.succeeded;
    if (!kickedQueued) {
        result.reason = std::move(transportError);
    } else if (!leaveQueued) {
        result.reason = std::move(leaveError);
    } else if (!snapshotQueued) {
        result.reason = std::move(snapshotError);
    } else if (!advertisementUpdated) {
        result.reason = std::move(advertisementError);
    }
    return result;
}

bool RuntimeMultiplayerLanSession::interruptClientTransportForReconnect(
    std::string& errorMessage
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client
        || impl_->phase != RuntimeMultiplayerLanPhase::Running) {
        errorMessage =
            "only a running LAN client transport may be interrupted";
        return false;
    }
    if (!impl_->beginClientReconnect(
            "client transport was deliberately interrupted",
            impl_->pendingEvents
        )) {
        errorMessage =
            "this running peer is not eligible for bounded reconnect";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLanSession::replaceAuthenticatedReconnectEndpoint(
    std::uint16_t loopbackPort, std::vector<std::uint8_t> prelude, std::string& errorMessage
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client || loopbackPort == 0
        || impl_->joinOptions.localProxyPrelude.empty() || prelude.empty()
        || prelude.size() != impl_->joinOptions.localProxyPrelude.size()
        || (impl_->phase != RuntimeMultiplayerLanPhase::Running
            && impl_->phase != RuntimeMultiplayerLanPhase::Reconnecting)) {
        errorMessage = "authenticated reconnect endpoint replacement is unavailable";
        return false;
    }
    if (impl_->phase == RuntimeMultiplayerLanPhase::Running
        && !interruptClientTransportForReconnect(errorMessage)) return false;
    impl_->joinOptions.room.hostAddress = "127.0.0.1";
    impl_->joinOptions.room.sessionPort = loopbackPort;
    impl_->joinOptions.localProxyPrelude = std::move(prelude);
    impl_->nextReconnectAttempt = std::chrono::steady_clock::now();
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLanSession::setLobbyMap(RuntimeMultiplayerMatchMap map,
    std::vector<std::uint8_t> bytes, std::string& error) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby || !impl_->currentMapMatches(map)) {
        error = "only the lobby host may publish its current map bytes"; return false;
    }
    return impl_->lobbyMapTransfer.publish(std::move(map), std::move(bytes), error);
}

bool RuntimeMultiplayerLanSession::requestLobbyMap(const RuntimeMultiplayerMatchMap& map, std::string& error) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Client
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby || !impl_->joinAccepted || !impl_->currentMapMatches(map)) {
        error = "map download requires an admitted canonical lobby"; return false;
    }
    if (impl_->lobbyMapTransfer.alreadyRequested(map)) { error.clear(); return true; }
    RuntimeMultiplayerMapRequest request;
    if (!impl_->lobbyMapTransfer.prepareRequest(map, request, error)) return false;
    const auto unready = setReady(false);
    if (!unready.succeeded) { error = unready.reason; return false; }
    RuntimeMultiplayerMessage message;
    message.type = RuntimeMultiplayerMessageType::LobbyMapRequest; message.body = request;
    if (!impl_->sendToHost(message, error)) return false;
    impl_->lobbyMapTransfer.requested(std::move(request), std::chrono::steady_clock::now()); error.clear(); return true;
}

const RuntimeMultiplayerMapOffer* RuntimeMultiplayerLanSession::lobbyMapOffer() const {
    return impl_ ? impl_->lobbyMapTransfer.offer() : nullptr;
}
const std::vector<std::uint8_t>& RuntimeMultiplayerLanSession::lobbyMapBytes() const {
    static const std::vector<std::uint8_t> empty;
    return impl_ ? impl_->lobbyMapTransfer.bytes() : empty;
}
bool RuntimeMultiplayerLanSession::lobbyMapTransfersComplete() const {
    return impl_ && impl_->role == RuntimeMultiplayerLanRole::Host && impl_->mapTransfersComplete();
}

bool RuntimeMultiplayerLanSession::setLobbyResumeSnapshot(RuntimeMultiplayerSnapshotDescriptor descriptor,
    std::vector<std::uint8_t> bytes, std::string& errorMessage) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Lobby || impl_->lobbyResumeImage
        || descriptor.identity.kind != RuntimeMultiplayerSnapshotKind::Save
        || descriptor.identity.snapshotTick >= runtimeMultiplayerSnapshotMaximumTick
        || !validateRuntimeMultiplayerSnapshotDescriptor(descriptor, errorMessage)
        || bytes.size() != descriptor.byteCount
        || runtimeMultiplayerSnapshotFingerprint(bytes) != descriptor.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes) != descriptor.sha256) {
        if (errorMessage.empty()) errorMessage = "invalid or already configured lobby resume snapshot";
        return false;
    }
    impl_->lobbyResumeImage = std::move(descriptor);
    impl_->lobbyResumeBytes = std::move(bytes);
    errorMessage.clear();return true;
}

const RuntimeMultiplayerSnapshotDescriptor* RuntimeMultiplayerLanSession::lobbyResumeDescriptor() const {
    return impl_ && impl_->lobbyResumeImage ? &*impl_->lobbyResumeImage : nullptr;
}

bool RuntimeMultiplayerLanSession::lobbyResumeSnapshotReady() const {
    return impl_ && (!impl_->lobbyResumeImage || !impl_->lobbyResumeBytes.empty());
}

bool RuntimeMultiplayerLanSession::lobbyResumeAllPeersReady() const {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host) return false;
    if (!impl_->lobbyResumeImage) return true;
    return std::all_of(impl_->hostPeers.begin(), impl_->hostPeers.end(), [](const auto& entry) {
        const auto& peer = entry.second;
        return !peer.admitted || peer.kicked || peer.lobbyResumeProducer.complete();
    });
}

const std::vector<std::uint8_t>& RuntimeMultiplayerLanSession::lobbyResumeSnapshotBytes() const {
    static const std::vector<std::uint8_t> empty;
    return impl_ ? impl_->lobbyResumeBytes : empty;
}

bool RuntimeMultiplayerLanSession::reconnectPending() const {
    return impl_ && (impl_->phase == RuntimeMultiplayerLanPhase::Reconnecting
        || !impl_->suspendedPeers.empty()
        || impl_->reconnectRecoveryPending);
}

bool RuntimeMultiplayerLanSession::reconnectReadyForRecovery() const {
    return impl_ && impl_->role == RuntimeMultiplayerLanRole::Host
        && impl_->phase == RuntimeMultiplayerLanPhase::Running
        && impl_->reconnectRecoveryPending
        && impl_->suspendedPeers.empty()
        && impl_->reconnectRecoveryProposalId == 0;
}

bool RuntimeMultiplayerLanSession::markReconnectRecoveryProposal(
    std::uint64_t proposalId,
    std::string& errorMessage
) {
    if (!impl_ || impl_->role != RuntimeMultiplayerLanRole::Host
        || impl_->phase != RuntimeMultiplayerLanPhase::Running
        || !impl_->reconnectRecoveryPending
        || !impl_->suspendedPeers.empty()
        || proposalId == 0
        || (impl_->reconnectRecoveryProposalId != 0
            && impl_->reconnectRecoveryProposalId != proposalId)) {
        errorMessage =
            "reconnect recovery proposal cannot be armed in this state";
        return false;
    }
    impl_->reconnectRecoveryProposalId = proposalId;
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerLanSession::close() {
    if (!impl_) return;
    if (impl_->role == RuntimeMultiplayerLanRole::Host
        && impl_->phase == RuntimeMultiplayerLanPhase::Running) {
        (void)impl_->hostAbort(
            impl_->localPeerId,
            RuntimeMultiplayerSessionAbortReason::HostClosed
        );
        std::string ignored;
        (void)impl_->server.poll(0, ignored);
    } else if (impl_->role == RuntimeMultiplayerLanRole::Client
        && (impl_->phase == RuntimeMultiplayerLanPhase::Lobby
            || (impl_->phase == RuntimeMultiplayerLanPhase::Running
                && std::any_of(
                    impl_->session.members().begin(),
                    impl_->session.members().end(),
                    [this](const RuntimeMultiplayerRoomMember& member) {
                        return member.peerId == impl_->localPeerId
                            && member.participation
                                == RuntimeMultiplayerParticipationRole::Spectator;
                    }
                )))
        && impl_->client.isConnected()) {
        RuntimeMultiplayerMessage leaveMessage;
        leaveMessage.type = RuntimeMultiplayerMessageType::Leave;
        leaveMessage.body = RuntimeMultiplayerLeaveMessage{
            impl_->localPeerId
        };
        std::string ignored;
        (void)impl_->sendToHost(leaveMessage, ignored);
        (void)impl_->client.poll(0, ignored);
    }
    impl_->reset();
}

RuntimeMultiplayerLanRole RuntimeMultiplayerLanSession::role() const {
    return impl_ ? impl_->role : RuntimeMultiplayerLanRole::None;
}

RuntimeMultiplayerLanPhase RuntimeMultiplayerLanSession::phase() const {
    return impl_ ? impl_->phase : RuntimeMultiplayerLanPhase::Inactive;
}

const std::string& RuntimeMultiplayerLanSession::localPeerId() const {
    static const std::string empty;
    return impl_ ? impl_->localPeerId : empty;
}

std::uint64_t RuntimeMultiplayerLanSession::roomGeneration() const {
    return impl_ ? impl_->roomGeneration : 0;
}

std::uint16_t RuntimeMultiplayerLanSession::discoveryPort() const {
    return impl_ ? impl_->boundDiscoveryPort : 0;
}

std::uint16_t RuntimeMultiplayerLanSession::sessionPort() const {
    return impl_ ? impl_->boundSessionPort : 0;
}

const RuntimeMultiplayerSessionState&
RuntimeMultiplayerLanSession::session() const {
    static const RuntimeMultiplayerSessionState empty;
    return impl_ ? impl_->session : empty;
}
