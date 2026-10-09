#include "RuntimeMultiplayerProtocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

class MessageWriter {
public:
    void writeU8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void writeU32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
    }

    void writeU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(
                (value >> static_cast<unsigned int>(shift)) & 0xffU
            ));
        }
    }

    void writeString(const std::string& value) {
        writeU32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void writeBytes(const std::vector<std::uint8_t>& value) {
        writeU32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class MessageReader {
public:
    explicit MessageReader(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes) {}

    bool readU8(std::uint8_t& value) {
        if (!require(1U)) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool readBool(bool& value) {
        std::uint8_t encoded = 0;
        if (!readU8(encoded)) return false;
        if (encoded > 1U) {
            return fail("multiplayer message boolean value is invalid");
        }
        value = encoded != 0;
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4U)) return false;
        value = (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3U]);
        offset_ += 4U;
        return true;
    }

    bool readU64(std::uint64_t& value) {
        if (!require(8U)) return false;
        value = 0;
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
        std::uint32_t length = 0;
        if (!readU32(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("multiplayer message ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.clear();
        if (length != 0) {
            value.assign(
                reinterpret_cast<const char*>(bytes_.data() + offset_),
                static_cast<std::size_t>(length)
            );
        }
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool readBytes(
        std::size_t maximumBytes,
        std::vector<std::uint8_t>& value,
        const char* fieldName
    ) {
        std::uint32_t length = 0;
        if (!readU32(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("multiplayer message ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.assign(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin()
                + static_cast<std::ptrdiff_t>(
                    offset_ + static_cast<std::size_t>(length)
                )
        );
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool finished() const {
        return offset_ == bytes_.size();
    }

    const std::string& error() const {
        return error_;
    }

private:
    bool require(std::size_t count) {
        if (count <= bytes_.size() - offset_) return true;
        return fail("multiplayer message is truncated");
    }

    bool fail(std::string error) {
        if (error_.empty()) error_ = std::move(error);
        return false;
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
    std::string error_;
};

bool validIdentifier(const std::string& value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        const bool alphaNumeric =
            (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
        return alphaNumeric
            || byte == static_cast<unsigned char>('-')
            || byte == static_cast<unsigned char>('_')
            || byte == static_cast<unsigned char>('.');
    });
}

bool validDisplayText(const std::string& value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte >= 0x20U && byte != 0x7fU;
    });
}

bool validTick(int tick) {
    return tick >= 0 && tick <= runtimeMultiplayerMaximumTick;
}

bool validSequence(std::uint64_t sequence) {
    return sequence != 0
        && sequence
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
            );
}

bool validTimelineEpoch(std::uint64_t timelineEpoch) {
    return timelineEpoch != 0;
}

bool validatePeerId(const std::string& peerId, std::string& errorMessage) {
    if (validIdentifier(peerId, runtimeMultiplayerMaximumPeerIdBytes)) {
        return true;
    }
    errorMessage = "multiplayer message peer id is invalid";
    return false;
}

bool validateCompatibility(
    const RuntimeMultiplayerCompatibility& compatibility,
    std::string& errorMessage
) {
    if (compatibility.protocolVersion != runtimeMultiplayerProtocolVersion) {
        errorMessage = "multiplayer message protocol version is incompatible";
        return false;
    }
    if (compatibility.protocolFingerprint == 0
        || compatibility.contentFingerprint == 0) {
        errorMessage = "multiplayer message fingerprint must be nonzero";
        return false;
    }
    return true;
}

bool validParticipationRole(RuntimeMultiplayerParticipationRole role) {
    return role == RuntimeMultiplayerParticipationRole::Combatant
        || role == RuntimeMultiplayerParticipationRole::Spectator;
}

bool validateJoinRequest(
    const RuntimeMultiplayerJoinRequest& request,
    std::string& errorMessage
) {
    if (!validatePeerId(request.peerId, errorMessage)) return false;
    if (!validDisplayText(
            request.displayName,
            runtimeMultiplayerMaximumDisplayNameBytes
        )) {
        errorMessage = "multiplayer message display name is invalid";
        return false;
    }
    if (!validParticipationRole(request.participation)) {
        errorMessage = "multiplayer message participation role is invalid";
        return false;
    }
    return validateCompatibility(request.compatibility, errorMessage);
}

bool validateStart(
    const RuntimeMultiplayerStartMessage& message,
    std::string& errorMessage
) {
    if (!validatePeerId(message.requestingPeerId, errorMessage)) return false;
    if (!validTick(message.startTick)) {
        errorMessage = "multiplayer message start tick is invalid";
        return false;
    }
    return true;
}

bool validateAdvanceTick(
    const RuntimeMultiplayerAdvanceTickMessage& message,
    std::string& errorMessage
) {
    if (!validatePeerId(message.requestingPeerId, errorMessage)) return false;
    if (!validTick(message.authoritativeTick)) {
        errorMessage = "multiplayer message authoritative tick is invalid";
        return false;
    }
    if (!validTimelineEpoch(message.timelineEpoch)) {
        errorMessage = "multiplayer message timeline epoch is invalid";
        return false;
    }
    return true;
}

bool validateCommand(
    const RuntimeMultiplayerCommandPacket& command,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(command.timelineEpoch)) {
        errorMessage = "multiplayer message command timeline epoch is invalid";
        return false;
    }
    if (!validTick(command.tick)) {
        errorMessage = "multiplayer message command tick is invalid";
        return false;
    }
    if (!validatePeerId(command.peerId, errorMessage)) return false;
    if (!validSequence(command.sequence)) {
        errorMessage = "multiplayer message command sequence is invalid";
        return false;
    }
    if (command.payload.empty()
        || command.payload.size()
            > runtimeMultiplayerMaximumCommandPayloadBytes) {
        errorMessage = "multiplayer message command payload length is invalid";
        return false;
    }
    return true;
}

bool validateStateHash(
    const RuntimeMultiplayerStateHashReport& report,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(report.timelineEpoch)) {
        errorMessage =
            "multiplayer message state hash timeline epoch is invalid";
        return false;
    }
    if (!validTick(report.tick)) {
        errorMessage = "multiplayer message state hash tick is invalid";
        return false;
    }
    if (!validatePeerId(report.peerId, errorMessage)) return false;
    if (report.stateHash == 0) {
        errorMessage = "multiplayer message state hash must be nonzero";
        return false;
    }
    return true;
}

bool validSaveLoadKind(RuntimeMultiplayerSaveLoadKind kind) {
    return kind == RuntimeMultiplayerSaveLoadKind::Save
        || kind == RuntimeMultiplayerSaveLoadKind::Load;
}

bool validateSaveLoadProposal(
    const RuntimeMultiplayerSaveLoadProposal& proposal,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(proposal.timelineEpoch)) {
        errorMessage =
            "multiplayer message save/load timeline epoch is invalid";
        return false;
    }
    if (!validSequence(proposal.proposalId)) {
        errorMessage = "multiplayer message save/load proposal id is invalid";
        return false;
    }
    if (!validSaveLoadKind(proposal.kind)) {
        errorMessage = "multiplayer message save/load kind is unknown";
        return false;
    }
    if (!validatePeerId(proposal.proposerPeerId, errorMessage)) return false;
    std::string descriptorError;
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            proposal.descriptor,
            descriptorError
        )) {
        errorMessage =
            "multiplayer message snapshot descriptor is invalid: "
                + descriptorError;
        return false;
    }
    const RuntimeMultiplayerSnapshotIdentity& identity =
        proposal.descriptor.identity;
    const RuntimeMultiplayerSnapshotKind expectedKind =
        proposal.kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    if (identity.proposalId != proposal.proposalId
        || identity.kind != expectedKind
        || identity.sourceTimelineEpoch != proposal.timelineEpoch) {
        errorMessage =
            "multiplayer message snapshot identity conflicts with proposal";
        return false;
    }
    if (proposal.tick != identity.snapshotTick
        || proposal.snapshotId != identity.snapshotToken
        || proposal.snapshotFingerprint
            != proposal.descriptor.snapshotFingerprint) {
        errorMessage =
            "multiplayer message deprecated proposal mirrors conflict";
        return false;
    }
    return true;
}

bool validateSaveLoadResponse(
    const RuntimeMultiplayerSaveLoadResponseMessage& response,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(response.timelineEpoch)) {
        errorMessage =
            "multiplayer message save/load timeline epoch is invalid";
        return false;
    }
    if (!validSequence(response.proposalId)) {
        errorMessage = "multiplayer message save/load proposal id is invalid";
        return false;
    }
    if (!validatePeerId(response.peerId, errorMessage)) return false;
    if (response.approved
        && (response.observedFingerprint == 0
            || response.observedAuthoritativeStateHash == 0)) {
        errorMessage = "multiplayer message prepare evidence is invalid";
        return false;
    }
    return true;
}

bool validateSaveLoadAbort(
    const RuntimeMultiplayerSaveLoadAbortMessage& abort,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(abort.timelineEpoch)) {
        errorMessage =
            "multiplayer message save/load timeline epoch is invalid";
        return false;
    }
    if (!validSequence(abort.proposalId)) {
        errorMessage = "multiplayer message save/load proposal id is invalid";
        return false;
    }
    return validatePeerId(abort.requestingPeerId, errorMessage);
}

bool validateSaveLoadCommit(
    const RuntimeMultiplayerSaveLoadCommitMessage& commit,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(commit.timelineEpoch)
        || !validSequence(commit.proposalId)) {
        errorMessage = "multiplayer message save/load commit identity is invalid";
        return false;
    }
    return validatePeerId(commit.requestingPeerId, errorMessage);
}

bool validateSaveLoadApplied(
    const RuntimeMultiplayerSaveLoadAppliedMessage& applied,
    std::string& errorMessage
) {
    if (!validTimelineEpoch(applied.timelineEpoch)
        || !validSequence(applied.proposalId)) {
        errorMessage = "multiplayer message save/load applied identity is invalid";
        return false;
    }
    if (!validatePeerId(applied.peerId, errorMessage)) return false;
    if (applied.applied
        && applied.observedAuthoritativeStateHash == 0) {
        errorMessage =
            "multiplayer message applied state hash must be nonzero";
        return false;
    }
    return true;
}

bool validateRoomConfig(
    const RuntimeMultiplayerSessionConfig& config,
    std::string& errorMessage
) {
    if (!validIdentifier(config.roomId, runtimeMultiplayerMaximumRoomIdBytes)) {
        errorMessage = "multiplayer message room id is invalid";
        return false;
    }
    if (!validatePeerId(config.hostPeerId, errorMessage)) return false;
    if (!validDisplayText(
            config.hostDisplayName,
            runtimeMultiplayerMaximumDisplayNameBytes
        )) {
        errorMessage = "multiplayer message host display name is invalid";
        return false;
    }
    if (!validParticipationRole(config.hostParticipation)) {
        errorMessage = "multiplayer message host participation is invalid";
        return false;
    }
    if (!validateCompatibility(config.compatibility, errorMessage)) {
        return false;
    }
    if (config.maximumMembers < 2
        || config.maximumMembers > runtimeMultiplayerMaximumMembers) {
        errorMessage = "multiplayer message member limit is invalid";
        return false;
    }
    if (config.maximumCommandPayloadBytes == 0
        || config.maximumCommandPayloadBytes
            > runtimeMultiplayerMaximumCommandPayloadBytes) {
        errorMessage = "multiplayer message command payload limit is invalid";
        return false;
    }
    if (config.maximumBufferedCommands == 0
        || config.maximumBufferedCommands
            > runtimeMultiplayerMaximumBufferedCommands) {
        errorMessage = "multiplayer message command count limit is invalid";
        return false;
    }
    if (config.maximumBufferedCommandsPerPeer == 0
        || config.maximumBufferedCommandsPerPeer
            > runtimeMultiplayerMaximumBufferedCommandsPerPeer
        || config.maximumBufferedCommandsPerPeer
            > config.maximumBufferedCommands / config.maximumMembers) {
        errorMessage =
            "multiplayer message command buffer cannot reserve peer quotas";
        return false;
    }
    if (config.maximumStateHashFrames == 0
        || config.maximumStateHashFrames
            > runtimeMultiplayerMaximumStateHashFrames) {
        errorMessage = "multiplayer message state hash frame limit is invalid";
        return false;
    }
    if (config.maximumSaveLoadHandshakes == 0
        || config.maximumSaveLoadHandshakes
            > runtimeMultiplayerMaximumSaveLoadHandshakes) {
        errorMessage = "multiplayer message save/load limit is invalid";
        return false;
    }
    if (config.commandLeadWindowTicks < 0
        || config.commandLeadWindowTicks
            > runtimeMultiplayerMaximumCommandLeadWindowTicks) {
        errorMessage = "multiplayer message command lead window is invalid";
        return false;
    }
    if (config.stateHashLagWindowTicks < 0
        || config.stateHashLagWindowTicks
            > runtimeMultiplayerMaximumStateHashLagWindowTicks) {
        errorMessage = "multiplayer message state hash lag window is invalid";
        return false;
    }
    if (config.stateHashPeriodTicks <= 0
        || config.stateHashPeriodTicks > runtimeMultiplayerMaximumTick) {
        errorMessage = "multiplayer message state hash period is invalid";
        return false;
    }
    const std::size_t requiredHashFrameCapacity =
        static_cast<std::size_t>(
            config.stateHashLagWindowTicks / config.stateHashPeriodTicks
        ) + 1U;
    if (config.maximumStateHashFrames < requiredHashFrameCapacity) {
        errorMessage =
            "multiplayer message state hash capacity cannot cover lag window";
        return false;
    }
    return true;
}

bool validateRoomMember(
    const RuntimeMultiplayerRoomMember& member,
    std::string& errorMessage
) {
    if (!validatePeerId(member.peerId, errorMessage)) return false;
    if (!validDisplayText(
            member.displayName,
            runtimeMultiplayerMaximumDisplayNameBytes
        )) {
        errorMessage = "multiplayer message member display name is invalid";
        return false;
    }
    if (member.role != RuntimeMultiplayerRoomMemberRole::Host
        && member.role != RuntimeMultiplayerRoomMemberRole::Client) {
        errorMessage = "multiplayer message member role is invalid";
        return false;
    }
    if (!validParticipationRole(member.participation)) {
        errorMessage = "multiplayer message member participation is invalid";
        return false;
    }
    return true;
}

bool validateAdmissionChallenge(
    const RuntimeMultiplayerAdmissionChallengeMessage& challenge,
    std::string& errorMessage
) {
    if (challenge.roomGeneration == 0 || challenge.admissionToken == 0) {
        errorMessage =
            "multiplayer admission generation and token must be nonzero";
        return false;
    }
    if (challenge.reconnectGraceMilliseconds
        > runtimeMultiplayerMaximumReconnectGraceMilliseconds) {
        errorMessage = "multiplayer reconnect grace exceeds the hard limit";
        return false;
    }
    return validateRoomConfig(challenge.config, errorMessage);
}

bool validateJoinAdmissionRequest(
    const RuntimeMultiplayerJoinAdmissionRequestMessage& admission,
    std::string& errorMessage
) {
    if (admission.roomGeneration == 0 || admission.admissionToken == 0) {
        errorMessage =
            "multiplayer admission generation and token must be nonzero";
        return false;
    }
    if (admission.reconnect) {
        if (admission.reconnectTimelineEpoch == 0
            || admission.reconnectAuthoritativeTick < 0
            || admission.reconnectAuthoritativeTick
                > runtimeMultiplayerMaximumTick
            || admission.reconnectSessionStateHash == 0) {
            errorMessage = "multiplayer reconnect checkpoint is invalid";
            return false;
        }
    } else if (admission.reconnectTimelineEpoch != 0
        || admission.reconnectAuthoritativeTick != -1
        || admission.reconnectSessionStateHash != 0) {
        errorMessage = "ordinary admission carries reconnect state";
        return false;
    }
    return validateJoinRequest(admission.request, errorMessage);
}

bool validateJoinAccepted(
    const RuntimeMultiplayerJoinAcceptedMessage& accepted,
    std::string& errorMessage
) {
    if (accepted.roomGeneration == 0) {
        errorMessage = "multiplayer room generation must be nonzero";
        return false;
    }
    if (!validIdentifier(
            accepted.roomId,
            runtimeMultiplayerMaximumRoomIdBytes
        )) {
        errorMessage = "multiplayer message room id is invalid";
        return false;
    }
    return validatePeerId(accepted.peerId, errorMessage);
}

bool validateJoinRejected(
    const RuntimeMultiplayerJoinRejectedMessage& rejected,
    std::string& errorMessage
) {
    if (!validIdentifier(
            rejected.roomId,
            runtimeMultiplayerMaximumRoomIdBytes
        )) {
        errorMessage = "multiplayer message room id is invalid";
        return false;
    }
    if (!rejected.peerId.empty()
        && !validatePeerId(rejected.peerId, errorMessage)) {
        return false;
    }
    if (!validDisplayText(
            rejected.reason,
            runtimeMultiplayerMaximumJoinRejectionReasonBytes
        )) {
        errorMessage = "multiplayer join rejection reason is invalid";
        return false;
    }
    return true;
}

bool validateLobbySnapshot(
    const RuntimeMultiplayerLobbySnapshotMessage& message,
    std::string& errorMessage
) {
    if (message.roomGeneration == 0) {
        errorMessage = "multiplayer room generation must be nonzero";
        return false;
    }
    if (message.snapshot.timelineEpoch != 1) {
        errorMessage =
            "multiplayer lobby snapshot may only advertise initial epoch 1";
        return false;
    }
    if (!validateRoomConfig(message.snapshot.config, errorMessage)) {
        return false;
    }
    if (message.snapshot.members.empty()
        || message.snapshot.members.size()
            > message.snapshot.config.maximumMembers) {
        errorMessage = "multiplayer lobby snapshot member count is invalid";
        return false;
    }
    for (const RuntimeMultiplayerRoomMember& member :
            message.snapshot.members) {
        if (!validateRoomMember(member, errorMessage)) return false;
    }
    if (message.snapshot.matchSetup.has_value()) {
        if (message.snapshot.latestMatchSetupRevision
            != message.snapshot.matchSetup->revision) {
            errorMessage =
                "multiplayer lobby snapshot setup revision is inconsistent";
            return false;
        }
        std::string setupError;
        if (!validateRuntimeMultiplayerMatchSetup(
                *message.snapshot.matchSetup,
                setupError
            )) {
            errorMessage =
                "multiplayer lobby snapshot setup is invalid: " + setupError;
            return false;
        }
    }

    RuntimeMultiplayerSessionState candidate;
    const RuntimeMultiplayerOperationResult replaced =
        candidate.replaceLobbySnapshot(message.snapshot);
    if (!replaced.accepted) {
        errorMessage =
            "multiplayer lobby snapshot state is invalid: "
                + replaced.reason;
        return false;
    }
    return true;
}

bool validateSessionAbortReason(
    RuntimeMultiplayerSessionAbortReason reason
) {
    switch (reason) {
    case RuntimeMultiplayerSessionAbortReason::PeerDisconnected:
    case RuntimeMultiplayerSessionAbortReason::HostDisconnected:
    case RuntimeMultiplayerSessionAbortReason::HostClosed:
    case RuntimeMultiplayerSessionAbortReason::TransportFailure:
    case RuntimeMultiplayerSessionAbortReason::ProtocolViolation:
        return true;
    default:
        return false;
    }
}

bool validateSessionAbort(
    const RuntimeMultiplayerSessionAbortMessage& message,
    std::string& errorMessage
) {
    if (message.roomGeneration == 0) {
        errorMessage = "multiplayer room generation must be nonzero";
        return false;
    }
    if (!validatePeerId(message.abort.requestingPeerId, errorMessage)
        || !validatePeerId(message.abort.affectedPeerId, errorMessage)) {
        return false;
    }
    if (!validTick(message.abort.tick)) {
        errorMessage = "multiplayer session abort tick is invalid";
        return false;
    }
    if (!validateSessionAbortReason(message.abort.reason)) {
        errorMessage = "multiplayer session abort reason is invalid";
        return false;
    }
    return true;
}

bool validateLobbySlotOptionsRequest(
    const RuntimeMultiplayerLobbySlotOptionsRequestMessage& request,
    std::string& errorMessage
) {
    if (!validatePeerId(request.peerId, errorMessage)) return false;
    if (!validSequence(request.requestId)
        || request.expectedSetupRevision == 0) {
        errorMessage =
            "multiplayer lobby slot request identity is invalid";
        return false;
    }
    if (request.slotIndex >= runtimeMultiplayerMatchMaximumSlots
        || request.startPosition > runtimeMultiplayerMatchMaximumSlots) {
        errorMessage = "multiplayer lobby slot position is invalid";
        return false;
    }
    const auto validOption = [](const std::string& option) {
        return validDisplayText(
            option,
            runtimeMultiplayerMatchMaximumSlotOptionBytes
        );
    };
    if (!validOption(request.countryId) || !validOption(request.colorId)
        || !validOption(request.teamId)
        || !validOption(request.difficultyId)) {
        errorMessage = "multiplayer lobby slot option is invalid";
        return false;
    }
    return true;
}

bool validateLobbySlotOptionsResult(
    const RuntimeMultiplayerLobbySlotOptionsResultMessage& result,
    std::string& errorMessage
) {
    if (!validatePeerId(result.peerId, errorMessage)
        || !validSequence(result.requestId)
        || result.currentSetupRevision == 0) {
        if (errorMessage.empty()) {
            errorMessage =
                "multiplayer lobby slot result identity is invalid";
        }
        return false;
    }
    if ((result.accepted && !result.reason.empty())
        || (!result.accepted
            && !validDisplayText(
                result.reason,
                runtimeMultiplayerMaximumLobbyResultReasonBytes
            ))) {
        errorMessage = "multiplayer lobby slot result reason is invalid";
        return false;
    }
    return true;
}

bool validateLobbyKicked(
    const RuntimeMultiplayerLobbyKickedMessage& kicked,
    std::string& errorMessage
) {
    if (kicked.roomGeneration == 0
        || !validatePeerId(kicked.peerId, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = "multiplayer kicked room generation is invalid";
        }
        return false;
    }
    if (!validDisplayText(
            kicked.reason,
            runtimeMultiplayerMaximumLobbyResultReasonBytes
        )) {
        errorMessage = "multiplayer kicked reason is invalid";
        return false;
    }
    return true;
}

void writeCompatibility(
    MessageWriter& writer,
    const RuntimeMultiplayerCompatibility& compatibility
) {
    writer.writeU32(compatibility.protocolVersion);
    writer.writeU64(compatibility.protocolFingerprint);
    writer.writeU64(compatibility.contentFingerprint);
}

bool readCompatibility(
    MessageReader& reader,
    RuntimeMultiplayerCompatibility& compatibility
) {
    return reader.readU32(compatibility.protocolVersion)
        && reader.readU64(compatibility.protocolFingerprint)
        && reader.readU64(compatibility.contentFingerprint);
}

constexpr std::size_t kMaximumSnapshotDescriptorPayloadBytes =
    runtimeMultiplayerSnapshotMaximumTokenBytes + 128U;

bool writeSnapshotDescriptor(
    MessageWriter& writer,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage
) {
    std::vector<std::uint8_t> payload;
    if (!encodeRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            payload,
            errorMessage
        )) {
        return false;
    }
    if (payload.size() > kMaximumSnapshotDescriptorPayloadBytes) {
        errorMessage = "multiplayer snapshot descriptor payload is too large";
        return false;
    }
    writer.writeBytes(payload);
    return true;
}

bool readSnapshotDescriptor(
    MessageReader& reader,
    RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage
) {
    std::vector<std::uint8_t> payload;
    if (!reader.readBytes(
            kMaximumSnapshotDescriptorPayloadBytes,
            payload,
            "snapshot descriptor"
        )) {
        errorMessage = reader.error();
        return false;
    }
    if (!decodeRuntimeMultiplayerSnapshotDescriptor(
            payload,
            descriptor,
            errorMessage
        )) {
        errorMessage =
            "multiplayer snapshot descriptor is invalid: " + errorMessage;
        return false;
    }
    return true;
}

void writeJoinRequest(
    MessageWriter& writer,
    const RuntimeMultiplayerJoinRequest& request
) {
    writer.writeString(request.peerId);
    writer.writeString(request.displayName);
    writer.writeU8(static_cast<std::uint8_t>(request.participation));
    writeCompatibility(writer, request.compatibility);
}

bool readJoinRequest(
    MessageReader& reader,
    RuntimeMultiplayerJoinRequest& request
) {
    std::uint8_t participation = 0;
    if (!reader.readString(
            runtimeMultiplayerMaximumPeerIdBytes,
            request.peerId,
            "peer id"
        )
        || !reader.readString(
            runtimeMultiplayerMaximumDisplayNameBytes,
            request.displayName,
            "display name"
        )
        || !reader.readU8(participation)
        || !readCompatibility(reader, request.compatibility)) {
        return false;
    }
    request.participation = static_cast<RuntimeMultiplayerParticipationRole>(
        participation
    );
    return true;
}

void writeSessionConfig(
    MessageWriter& writer,
    const RuntimeMultiplayerSessionConfig& config
) {
    writer.writeString(config.roomId);
    writer.writeString(config.hostPeerId);
    writer.writeString(config.hostDisplayName);
    writer.writeU8(static_cast<std::uint8_t>(config.hostParticipation));
    writeCompatibility(writer, config.compatibility);
    writer.writeU32(static_cast<std::uint32_t>(config.maximumMembers));
    writer.writeU32(static_cast<std::uint32_t>(
        config.maximumCommandPayloadBytes
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.maximumBufferedCommands
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.maximumBufferedCommandsPerPeer
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.maximumStateHashFrames
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.maximumSaveLoadHandshakes
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.commandLeadWindowTicks
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.stateHashLagWindowTicks
    ));
    writer.writeU32(static_cast<std::uint32_t>(
        config.stateHashPeriodTicks
    ));
}

bool readSessionConfig(
    MessageReader& reader,
    RuntimeMultiplayerSessionConfig& config
) {
    std::uint32_t maximumMembers = 0;
    std::uint32_t maximumCommandPayloadBytes = 0;
    std::uint32_t maximumBufferedCommands = 0;
    std::uint32_t maximumBufferedCommandsPerPeer = 0;
    std::uint32_t maximumStateHashFrames = 0;
    std::uint32_t maximumSaveLoadHandshakes = 0;
    std::uint32_t commandLeadWindowTicks = 0;
    std::uint32_t stateHashLagWindowTicks = 0;
    std::uint32_t stateHashPeriodTicks = 0;
    std::uint8_t hostParticipation = 0;
    if (!reader.readString(
            runtimeMultiplayerMaximumRoomIdBytes,
            config.roomId,
            "room id"
        )
        || !reader.readString(
            runtimeMultiplayerMaximumPeerIdBytes,
            config.hostPeerId,
            "host peer id"
        )
        || !reader.readString(
            runtimeMultiplayerMaximumDisplayNameBytes,
            config.hostDisplayName,
            "host display name"
        )
        || !reader.readU8(hostParticipation)
        || !readCompatibility(reader, config.compatibility)
        || !reader.readU32(maximumMembers)
        || !reader.readU32(maximumCommandPayloadBytes)
        || !reader.readU32(maximumBufferedCommands)
        || !reader.readU32(maximumBufferedCommandsPerPeer)
        || !reader.readU32(maximumStateHashFrames)
        || !reader.readU32(maximumSaveLoadHandshakes)
        || !reader.readU32(commandLeadWindowTicks)
        || !reader.readU32(stateHashLagWindowTicks)
        || !reader.readU32(stateHashPeriodTicks)) {
        return false;
    }
    config.hostParticipation =
        static_cast<RuntimeMultiplayerParticipationRole>(hostParticipation);
    config.maximumMembers = static_cast<std::size_t>(maximumMembers);
    config.maximumCommandPayloadBytes =
        static_cast<std::size_t>(maximumCommandPayloadBytes);
    config.maximumBufferedCommands =
        static_cast<std::size_t>(maximumBufferedCommands);
    config.maximumBufferedCommandsPerPeer =
        static_cast<std::size_t>(maximumBufferedCommandsPerPeer);
    config.maximumStateHashFrames =
        static_cast<std::size_t>(maximumStateHashFrames);
    config.maximumSaveLoadHandshakes =
        static_cast<std::size_t>(maximumSaveLoadHandshakes);
    config.commandLeadWindowTicks = -1;
    if (commandLeadWindowTicks
        <= static_cast<std::uint32_t>(runtimeMultiplayerMaximumTick)) {
        config.commandLeadWindowTicks =
            static_cast<int>(commandLeadWindowTicks);
    }
    config.stateHashLagWindowTicks = -1;
    if (stateHashLagWindowTicks
        <= static_cast<std::uint32_t>(runtimeMultiplayerMaximumTick)) {
        config.stateHashLagWindowTicks =
            static_cast<int>(stateHashLagWindowTicks);
    }
    config.stateHashPeriodTicks = -1;
    if (stateHashPeriodTicks
        <= static_cast<std::uint32_t>(runtimeMultiplayerMaximumTick)) {
        config.stateHashPeriodTicks =
            static_cast<int>(stateHashPeriodTicks);
    }
    return true;
}

void writeRoomMember(
    MessageWriter& writer,
    const RuntimeMultiplayerRoomMember& member
) {
    writer.writeString(member.peerId);
    writer.writeString(member.displayName);
    writer.writeU8(static_cast<std::uint8_t>(member.role));
    writer.writeU8(static_cast<std::uint8_t>(member.participation));
    writer.writeU8(member.ready ? 1U : 0U);
}

bool readRoomMember(
    MessageReader& reader,
    RuntimeMultiplayerRoomMember& member
) {
    std::uint8_t role = 0;
    std::uint8_t participation = 0;
    if (!reader.readString(
            runtimeMultiplayerMaximumPeerIdBytes,
            member.peerId,
            "member peer id"
        )
        || !reader.readString(
            runtimeMultiplayerMaximumDisplayNameBytes,
            member.displayName,
            "member display name"
        )
        || !reader.readU8(role)
        || !reader.readU8(participation)
        || !reader.readBool(member.ready)) {
        return false;
    }
    if (role == static_cast<std::uint8_t>(
            RuntimeMultiplayerRoomMemberRole::Host
        )
        || role == static_cast<std::uint8_t>(
            RuntimeMultiplayerRoomMemberRole::Client
        )) {
        member.role = static_cast<RuntimeMultiplayerRoomMemberRole>(role);
        member.participation =
            static_cast<RuntimeMultiplayerParticipationRole>(participation);
        return true;
    }
    return false;
}

bool decodeMessageType(
    std::uint16_t encoded,
    RuntimeMultiplayerMessageType& type,
    std::string& errorMessage
) {
    const std::uint8_t version = static_cast<std::uint8_t>(encoded >> 8U);
    if (version != runtimeMultiplayerMessageVersion) {
        errorMessage = "multiplayer message version is incompatible";
        return false;
    }
    switch (encoded) {
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::JoinRequest
    ):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::Leave):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::Ready):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::Start):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::Command):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::StateHash):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SaveLoadProposal
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SaveLoadResponse
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::RoomConfig
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SaveLoadAbort
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SaveLoadCommit
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SaveLoadApplied
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SnapshotChunk
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SnapshotChunkAck
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::AdmissionChallenge
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::JoinAdmissionRequest
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::JoinAccepted
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::JoinRejected
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::LobbySnapshot
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::SessionAbort
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::LobbySlotOptionsRequest
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::LobbySlotOptionsResult
    ):
    case static_cast<std::uint16_t>(
        RuntimeMultiplayerMessageType::LobbyKicked
    ):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyResumeOffer):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyResumeChunk):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyResumeAck):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyMapRequest):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyMapOffer):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyMapChunk):
    case static_cast<std::uint16_t>(RuntimeMultiplayerMessageType::LobbyMapAck):
        type = static_cast<RuntimeMultiplayerMessageType>(encoded);
        return true;
    default:
        errorMessage = "multiplayer message type is unknown";
        return false;
    }
}

template <typename Body>
const Body* requireBody(
    const RuntimeMultiplayerMessage& message,
    std::string& errorMessage
) {
    const Body* body = std::get_if<Body>(&message.body);
    if (body == nullptr) {
        errorMessage = "multiplayer message type and body do not match";
    }
    return body;
}

void copyOperationResult(
    const RuntimeMultiplayerOperationResult& source,
    RuntimeMultiplayerMessageApplyResult& destination
) {
    destination.accepted = source.accepted;
    destination.changed = source.changed;
    destination.duplicate = source.duplicate;
    destination.reason = source.reason;
}

void copyStateHashResult(
    const RuntimeMultiplayerStateHashResult& source,
    RuntimeMultiplayerMessageApplyResult& destination
) {
    destination.accepted = source.accepted;
    destination.changed = source.changed;
    destination.duplicate = source.duplicate;
    destination.stateHashFrameComplete = source.frameComplete;
    destination.desync = source.desync;
    destination.reason = source.reason;
}

void copySaveLoadResult(
    const RuntimeMultiplayerSaveLoadResult& source,
    RuntimeMultiplayerMessageApplyResult& destination
) {
    destination.accepted = source.accepted;
    destination.changed = source.changed;
    destination.duplicate = source.duplicate;
    destination.saveLoadStatus = source.status;
    destination.reason = source.reason;
}

} // namespace

bool encodeRuntimeMultiplayerMessage(
    const RuntimeMultiplayerMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    errorMessage.clear();
    MessageWriter writer;

    switch (message.type) {
    case RuntimeMultiplayerMessageType::JoinRequest: {
        const RuntimeMultiplayerJoinRequest* request =
            requireBody<RuntimeMultiplayerJoinRequest>(message, errorMessage);
        if (request == nullptr
            || !validateJoinRequest(*request, errorMessage)) {
            return false;
        }
        writeJoinRequest(writer, *request);
        break;
    }
    case RuntimeMultiplayerMessageType::Leave: {
        const RuntimeMultiplayerLeaveMessage* leave =
            requireBody<RuntimeMultiplayerLeaveMessage>(message, errorMessage);
        if (leave == nullptr
            || !validatePeerId(leave->peerId, errorMessage)) {
            return false;
        }
        writer.writeString(leave->peerId);
        break;
    }
    case RuntimeMultiplayerMessageType::Ready: {
        const RuntimeMultiplayerReadyMessage* ready =
            requireBody<RuntimeMultiplayerReadyMessage>(message, errorMessage);
        if (ready == nullptr
            || !validatePeerId(ready->peerId, errorMessage)) {
            return false;
        }
        writer.writeString(ready->peerId);
        writer.writeU8(ready->ready ? 1U : 0U);
        break;
    }
    case RuntimeMultiplayerMessageType::Start: {
        const RuntimeMultiplayerStartMessage* start =
            requireBody<RuntimeMultiplayerStartMessage>(message, errorMessage);
        if (start == nullptr || !validateStart(*start, errorMessage)) {
            return false;
        }
        writer.writeString(start->requestingPeerId);
        writer.writeU32(static_cast<std::uint32_t>(start->startTick));
        break;
    }
    case RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick: {
        const RuntimeMultiplayerAdvanceTickMessage* advance =
            requireBody<RuntimeMultiplayerAdvanceTickMessage>(
                message,
                errorMessage
            );
        if (advance == nullptr
            || !validateAdvanceTick(*advance, errorMessage)) {
            return false;
        }
        writer.writeString(advance->requestingPeerId);
        writer.writeU32(static_cast<std::uint32_t>(
            advance->authoritativeTick
        ));
        writer.writeU64(advance->timelineEpoch);
        break;
    }
    case RuntimeMultiplayerMessageType::Command: {
        const RuntimeMultiplayerCommandPacket* command =
            requireBody<RuntimeMultiplayerCommandPacket>(
                message,
                errorMessage
            );
        if (command == nullptr || !validateCommand(*command, errorMessage)) {
            return false;
        }
        writer.writeU32(static_cast<std::uint32_t>(command->tick));
        writer.writeString(command->peerId);
        writer.writeU64(command->sequence);
        writer.writeU64(command->timelineEpoch);
        writer.writeBytes(command->payload);
        break;
    }
    case RuntimeMultiplayerMessageType::StateHash: {
        const RuntimeMultiplayerStateHashReport* report =
            requireBody<RuntimeMultiplayerStateHashReport>(
                message,
                errorMessage
            );
        if (report == nullptr || !validateStateHash(*report, errorMessage)) {
            return false;
        }
        writer.writeU32(static_cast<std::uint32_t>(report->tick));
        writer.writeString(report->peerId);
        writer.writeU64(report->stateHash);
        writer.writeU64(report->timelineEpoch);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadProposal: {
        const RuntimeMultiplayerSaveLoadProposal* proposal =
            requireBody<RuntimeMultiplayerSaveLoadProposal>(
                message,
                errorMessage
            );
        if (proposal == nullptr
            || !validateSaveLoadProposal(*proposal, errorMessage)) {
            return false;
        }
        writer.writeU64(proposal->timelineEpoch);
        writer.writeU64(proposal->proposalId);
        writer.writeU8(static_cast<std::uint8_t>(proposal->kind));
        writer.writeString(proposal->proposerPeerId);
        if (!writeSnapshotDescriptor(
                writer,
                proposal->descriptor,
                errorMessage
            )) {
            return false;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadResponse: {
        const RuntimeMultiplayerSaveLoadResponseMessage* response =
            requireBody<RuntimeMultiplayerSaveLoadResponseMessage>(
                message,
                errorMessage
            );
        if (response == nullptr
            || !validateSaveLoadResponse(*response, errorMessage)) {
            return false;
        }
        writer.writeU64(response->timelineEpoch);
        writer.writeU64(response->proposalId);
        writer.writeString(response->peerId);
        writer.writeU8(response->approved ? 1U : 0U);
        writer.writeU64(response->observedFingerprint);
        writer.writeU64(response->observedAuthoritativeStateHash);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadAbort: {
        const RuntimeMultiplayerSaveLoadAbortMessage* abort =
            requireBody<RuntimeMultiplayerSaveLoadAbortMessage>(
                message,
                errorMessage
            );
        if (abort == nullptr
            || !validateSaveLoadAbort(*abort, errorMessage)) {
            return false;
        }
        writer.writeU64(abort->timelineEpoch);
        writer.writeU64(abort->proposalId);
        writer.writeString(abort->requestingPeerId);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadCommit: {
        const auto* commit =
            requireBody<RuntimeMultiplayerSaveLoadCommitMessage>(
                message,
                errorMessage
            );
        if (commit == nullptr
            || !validateSaveLoadCommit(*commit, errorMessage)) {
            return false;
        }
        writer.writeU64(commit->timelineEpoch);
        writer.writeU64(commit->proposalId);
        writer.writeString(commit->requestingPeerId);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadApplied: {
        const auto* applied =
            requireBody<RuntimeMultiplayerSaveLoadAppliedMessage>(
                message,
                errorMessage
            );
        if (applied == nullptr
            || !validateSaveLoadApplied(*applied, errorMessage)) {
            return false;
        }
        writer.writeU64(applied->timelineEpoch);
        writer.writeU64(applied->proposalId);
        writer.writeString(applied->peerId);
        writer.writeU8(applied->applied ? 1U : 0U);
        writer.writeU64(applied->observedAuthoritativeStateHash);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapRequest:
    case RuntimeMultiplayerMessageType::LobbyMapOffer: {
        const auto* offer = message.type == RuntimeMultiplayerMessageType::LobbyMapOffer
            ? requireBody<RuntimeMultiplayerMapOffer>(message, errorMessage) : nullptr;
        if (message.type == RuntimeMultiplayerMessageType::LobbyMapOffer && !offer) return false;
        const auto* request = offer ? &offer->request : requireBody<RuntimeMultiplayerMapRequest>(message, errorMessage);
        if (!request || !validateRuntimeMultiplayerMapRequest(*request, errorMessage)
            || (offer && !validateRuntimeMultiplayerMapOffer(*offer, errorMessage))) return false;
        writer.writeU64(request->requestId);
        writer.writeString(request->map.canonicalId);
        writer.writeU64(request->map.contentFingerprint);
        if (offer) {
            std::vector<std::uint8_t> encoded;
            if (!encodeRuntimeMultiplayerSnapshotDescriptor(offer->transfer, encoded, errorMessage)) return false;
            writer.writeBytes(encoded);
        }
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyResumeOffer: {
        const auto* descriptor = requireBody<RuntimeMultiplayerSnapshotDescriptor>(message, errorMessage);
        std::vector<std::uint8_t> encoded;
        if (!descriptor || descriptor->identity.kind != RuntimeMultiplayerSnapshotKind::Save
            || descriptor->identity.snapshotTick >= runtimeMultiplayerSnapshotMaximumTick
            || !encodeRuntimeMultiplayerSnapshotDescriptor(*descriptor, encoded, errorMessage)) {
            if (errorMessage.empty()) errorMessage = "invalid lobby resume Save descriptor";
            return false;
        }
        writer.writeBytes(encoded);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapChunk:
    case RuntimeMultiplayerMessageType::LobbyResumeChunk:
    case RuntimeMultiplayerMessageType::SnapshotChunk: {
        const auto* chunk = requireBody<RuntimeMultiplayerSnapshotChunk>(
            message,
            errorMessage
        );
        std::vector<std::uint8_t> encoded;
        if (chunk == nullptr
            || !encodeRuntimeMultiplayerSnapshotChunk(
                *chunk,
                encoded,
                errorMessage
            )) {
            return false;
        }
        writer.writeBytes(encoded);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapAck:
    case RuntimeMultiplayerMessageType::LobbyResumeAck:
    case RuntimeMultiplayerMessageType::SnapshotChunkAck: {
        const auto* ack = requireBody<RuntimeMultiplayerSnapshotChunkAck>(
            message,
            errorMessage
        );
        std::vector<std::uint8_t> encoded;
        if (ack == nullptr
            || !encodeRuntimeMultiplayerSnapshotChunkAck(
                *ack,
                encoded,
                errorMessage
            )) {
            return false;
        }
        writer.writeBytes(encoded);
        break;
    }
    case RuntimeMultiplayerMessageType::RoomConfig: {
        const RuntimeMultiplayerSessionConfig* config =
            requireBody<RuntimeMultiplayerSessionConfig>(
                message,
                errorMessage
            );
        if (config == nullptr || !validateRoomConfig(*config, errorMessage)) {
            return false;
        }
        writeSessionConfig(writer, *config);
        break;
    }
    case RuntimeMultiplayerMessageType::AdmissionChallenge: {
        const auto* challenge =
            requireBody<RuntimeMultiplayerAdmissionChallengeMessage>(
                message,
                errorMessage
            );
        if (challenge == nullptr
            || !validateAdmissionChallenge(*challenge, errorMessage)) {
            return false;
        }
        writer.writeU64(challenge->roomGeneration);
        writer.writeU64(challenge->admissionToken);
        writer.writeU32(challenge->reconnectGraceMilliseconds);
        writeSessionConfig(writer, challenge->config);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinAdmissionRequest: {
        const auto* admission =
            requireBody<RuntimeMultiplayerJoinAdmissionRequestMessage>(
                message,
                errorMessage
            );
        if (admission == nullptr
            || !validateJoinAdmissionRequest(*admission, errorMessage)) {
            return false;
        }
        writer.writeU64(admission->roomGeneration);
        writer.writeU64(admission->admissionToken);
        writeJoinRequest(writer, admission->request);
        writer.writeU8(admission->reconnect ? 1U : 0U);
        writer.writeU64(admission->reconnectTimelineEpoch);
        writer.writeU32(static_cast<std::uint32_t>(
            admission->reconnectAuthoritativeTick
        ));
        writer.writeU64(admission->reconnectSessionStateHash);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinAccepted: {
        const auto* accepted =
            requireBody<RuntimeMultiplayerJoinAcceptedMessage>(
                message,
                errorMessage
            );
        if (accepted == nullptr
            || !validateJoinAccepted(*accepted, errorMessage)) {
            return false;
        }
        writer.writeU64(accepted->roomGeneration);
        writer.writeString(accepted->roomId);
        writer.writeString(accepted->peerId);
        writer.writeU8(accepted->reconnected ? 1U : 0U);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinRejected: {
        const auto* rejected =
            requireBody<RuntimeMultiplayerJoinRejectedMessage>(
                message,
                errorMessage
            );
        if (rejected == nullptr
            || !validateJoinRejected(*rejected, errorMessage)) {
            return false;
        }
        writer.writeString(rejected->roomId);
        writer.writeString(rejected->peerId);
        writer.writeString(rejected->reason);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySnapshot: {
        const auto* snapshot =
            requireBody<RuntimeMultiplayerLobbySnapshotMessage>(
                message,
                errorMessage
            );
        if (snapshot == nullptr
            || !validateLobbySnapshot(*snapshot, errorMessage)) {
            return false;
        }
        writer.writeU64(snapshot->roomGeneration);
        writer.writeU64(snapshot->snapshot.timelineEpoch);
        writeSessionConfig(writer, snapshot->snapshot.config);
        writer.writeU64(snapshot->snapshot.latestMatchSetupRevision);
        writer.writeU8(snapshot->snapshot.matchSetup.has_value() ? 1U : 0U);
        if (snapshot->snapshot.matchSetup.has_value()) {
            std::vector<std::uint8_t> setupBytes;
            std::string setupError;
            if (!encodeRuntimeMultiplayerMatchSetup(
                    *snapshot->snapshot.matchSetup,
                    setupBytes,
                    setupError
                )) {
                errorMessage =
                    "multiplayer lobby snapshot setup could not encode: "
                        + setupError;
                return false;
            }
            writer.writeBytes(setupBytes);
        }
        writer.writeU32(static_cast<std::uint32_t>(
            snapshot->snapshot.members.size()
        ));
        for (const RuntimeMultiplayerRoomMember& member :
                snapshot->snapshot.members) {
            writeRoomMember(writer, member);
        }
        break;
    }
    case RuntimeMultiplayerMessageType::SessionAbort: {
        const auto* abort =
            requireBody<RuntimeMultiplayerSessionAbortMessage>(
                message,
                errorMessage
            );
        if (abort == nullptr
            || !validateSessionAbort(*abort, errorMessage)) {
            return false;
        }
        writer.writeU64(abort->roomGeneration);
        writer.writeString(abort->abort.requestingPeerId);
        writer.writeString(abort->abort.affectedPeerId);
        writer.writeU32(static_cast<std::uint32_t>(abort->abort.tick));
        writer.writeU8(static_cast<std::uint8_t>(abort->abort.reason));
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySlotOptionsRequest: {
        const auto* request =
            requireBody<RuntimeMultiplayerLobbySlotOptionsRequestMessage>(
                message,
                errorMessage
            );
        if (request == nullptr
            || !validateLobbySlotOptionsRequest(*request, errorMessage)) {
            return false;
        }
        writer.writeString(request->peerId);
        writer.writeU64(request->requestId);
        writer.writeU64(request->expectedSetupRevision);
        writer.writeU8(request->slotIndex);
        writer.writeString(request->countryId);
        writer.writeString(request->colorId);
        writer.writeString(request->teamId);
        writer.writeString(request->difficultyId);
        writer.writeU8(request->startPosition);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySlotOptionsResult: {
        const auto* result =
            requireBody<RuntimeMultiplayerLobbySlotOptionsResultMessage>(
                message,
                errorMessage
            );
        if (result == nullptr
            || !validateLobbySlotOptionsResult(*result, errorMessage)) {
            return false;
        }
        writer.writeString(result->peerId);
        writer.writeU64(result->requestId);
        writer.writeU8(result->accepted ? 1U : 0U);
        writer.writeU64(result->currentSetupRevision);
        writer.writeString(result->reason);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyKicked: {
        const auto* kicked =
            requireBody<RuntimeMultiplayerLobbyKickedMessage>(
                message,
                errorMessage
            );
        if (kicked == nullptr
            || !validateLobbyKicked(*kicked, errorMessage)) {
            return false;
        }
        writer.writeU64(kicked->roomGeneration);
        writer.writeString(kicked->peerId);
        writer.writeString(kicked->reason);
        break;
    }
    default:
        errorMessage = "multiplayer message type is unknown";
        return false;
    }

    RuntimeNetworkPacket encoded;
    encoded.type = static_cast<std::uint16_t>(message.type);
    encoded.payload = writer.take();
    if (encoded.payload.size() > runtimeLanMaximumPacketBytes) {
        errorMessage = "multiplayer message payload exceeds the LAN packet limit";
        return false;
    }
    packet = std::move(encoded);
    return true;
}

bool decodeRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeMultiplayerMessage& message,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (packet.payload.size() > runtimeLanMaximumPacketBytes) {
        errorMessage = "multiplayer message payload exceeds the LAN packet limit";
        return false;
    }

    RuntimeMultiplayerMessage parsed;
    if (!decodeMessageType(packet.type, parsed.type, errorMessage)) {
        return false;
    }
    MessageReader reader(packet.payload);

    switch (parsed.type) {
    case RuntimeMultiplayerMessageType::JoinRequest: {
        RuntimeMultiplayerJoinRequest request;
        if (!readJoinRequest(reader, request)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateJoinRequest(request, errorMessage)) return false;
        parsed.body = std::move(request);
        break;
    }
    case RuntimeMultiplayerMessageType::Leave: {
        RuntimeMultiplayerLeaveMessage leave;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                leave.peerId,
                "peer id"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validatePeerId(leave.peerId, errorMessage)) return false;
        parsed.body = std::move(leave);
        break;
    }
    case RuntimeMultiplayerMessageType::Ready: {
        RuntimeMultiplayerReadyMessage ready;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                ready.peerId,
                "peer id"
            )
            || !reader.readBool(ready.ready)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validatePeerId(ready.peerId, errorMessage)) return false;
        parsed.body = std::move(ready);
        break;
    }
    case RuntimeMultiplayerMessageType::Start: {
        RuntimeMultiplayerStartMessage start;
        std::uint32_t tick = 0;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                start.requestingPeerId,
                "peer id"
            )
            || !reader.readU32(tick)) {
            errorMessage = reader.error();
            return false;
        }
        if (tick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            start.startTick = static_cast<int>(tick);
        }
        if (!validateStart(start, errorMessage)) return false;
        parsed.body = std::move(start);
        break;
    }
    case RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick: {
        RuntimeMultiplayerAdvanceTickMessage advance;
        std::uint32_t tick = 0;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                advance.requestingPeerId,
                "peer id"
            )
            || !reader.readU32(tick)
            || !reader.readU64(advance.timelineEpoch)) {
            errorMessage = reader.error();
            return false;
        }
        if (tick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            advance.authoritativeTick = static_cast<int>(tick);
        }
        if (!validateAdvanceTick(advance, errorMessage)) return false;
        parsed.body = std::move(advance);
        break;
    }
    case RuntimeMultiplayerMessageType::Command: {
        RuntimeMultiplayerCommandPacket command;
        std::uint32_t tick = 0;
        if (!reader.readU32(tick)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                command.peerId,
                "peer id"
            )
            || !reader.readU64(command.sequence)
            || !reader.readU64(command.timelineEpoch)
            || !reader.readBytes(
                runtimeMultiplayerMaximumCommandPayloadBytes,
                command.payload,
                "command payload"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (tick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            command.tick = static_cast<int>(tick);
        }
        if (!validateCommand(command, errorMessage)) return false;
        parsed.body = std::move(command);
        break;
    }
    case RuntimeMultiplayerMessageType::StateHash: {
        RuntimeMultiplayerStateHashReport report;
        std::uint32_t tick = 0;
        if (!reader.readU32(tick)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                report.peerId,
                "peer id"
            )
            || !reader.readU64(report.stateHash)
            || !reader.readU64(report.timelineEpoch)) {
            errorMessage = reader.error();
            return false;
        }
        if (tick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            report.tick = static_cast<int>(tick);
        }
        if (!validateStateHash(report, errorMessage)) return false;
        parsed.body = std::move(report);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadProposal: {
        RuntimeMultiplayerSaveLoadProposal proposal;
        std::uint8_t kind = 0;
        if (!reader.readU64(proposal.timelineEpoch)
            || !reader.readU64(proposal.proposalId)
            || !reader.readU8(kind)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                proposal.proposerPeerId,
                "peer id"
            )
            || !readSnapshotDescriptor(
                reader,
                proposal.descriptor,
                errorMessage
            )) {
            if (errorMessage.empty()) errorMessage = reader.error();
            return false;
        }
        if (kind == static_cast<std::uint8_t>(
                RuntimeMultiplayerSaveLoadKind::Save
            )
            || kind == static_cast<std::uint8_t>(
                RuntimeMultiplayerSaveLoadKind::Load
            )) {
            proposal.kind = static_cast<RuntimeMultiplayerSaveLoadKind>(kind);
        } else {
            errorMessage = "multiplayer message save/load kind is unknown";
            return false;
        }
        proposal.tick = proposal.descriptor.identity.snapshotTick;
        proposal.snapshotId = proposal.descriptor.identity.snapshotToken;
        proposal.snapshotFingerprint =
            proposal.descriptor.snapshotFingerprint;
        if (!validateSaveLoadProposal(proposal, errorMessage)) return false;
        parsed.body = std::move(proposal);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadResponse: {
        RuntimeMultiplayerSaveLoadResponseMessage response;
        if (!reader.readU64(response.timelineEpoch)
            || !reader.readU64(response.proposalId)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                response.peerId,
                "peer id"
            )
            || !reader.readBool(response.approved)
            || !reader.readU64(response.observedFingerprint)
            || !reader.readU64(
                response.observedAuthoritativeStateHash
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateSaveLoadResponse(response, errorMessage)) return false;
        parsed.body = std::move(response);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadAbort: {
        RuntimeMultiplayerSaveLoadAbortMessage abort;
        if (!reader.readU64(abort.timelineEpoch)
            || !reader.readU64(abort.proposalId)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                abort.requestingPeerId,
                "peer id"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateSaveLoadAbort(abort, errorMessage)) return false;
        parsed.body = std::move(abort);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadCommit: {
        RuntimeMultiplayerSaveLoadCommitMessage commit;
        if (!reader.readU64(commit.timelineEpoch)
            || !reader.readU64(commit.proposalId)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                commit.requestingPeerId,
                "peer id"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateSaveLoadCommit(commit, errorMessage)) return false;
        parsed.body = std::move(commit);
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadApplied: {
        RuntimeMultiplayerSaveLoadAppliedMessage applied;
        if (!reader.readU64(applied.timelineEpoch)
            || !reader.readU64(applied.proposalId)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                applied.peerId,
                "peer id"
            )
            || !reader.readBool(applied.applied)
            || !reader.readU64(
                applied.observedAuthoritativeStateHash
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateSaveLoadApplied(applied, errorMessage)) return false;
        parsed.body = std::move(applied);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapRequest:
    case RuntimeMultiplayerMessageType::LobbyMapOffer: {
        RuntimeMultiplayerMapOffer offer;
        if (!reader.readU64(offer.request.requestId)
            || !reader.readString(runtimeMultiplayerMatchMaximumCanonicalMapIdBytes,
                offer.request.map.canonicalId, "map resource id")
            || !reader.readU64(offer.request.map.contentFingerprint)) {
            errorMessage = reader.error(); return false;
        }
        if (!validateRuntimeMultiplayerMapRequest(offer.request, errorMessage)) return false;
        if (parsed.type == RuntimeMultiplayerMessageType::LobbyMapOffer) {
            std::vector<std::uint8_t> encoded;
            if (!reader.readBytes(4096U, encoded, "map transfer descriptor")) { errorMessage = reader.error(); return false; }
            if (!decodeRuntimeMultiplayerSnapshotDescriptor(encoded, offer.transfer, errorMessage)
                || !validateRuntimeMultiplayerMapOffer(offer, errorMessage)) return false;
            parsed.body = std::move(offer);
        } else parsed.body = std::move(offer.request);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyResumeOffer: {
        std::vector<std::uint8_t> encoded;
        RuntimeMultiplayerSnapshotDescriptor descriptor;
        if (!reader.readBytes(4096U, encoded, "lobby resume descriptor")) {
            errorMessage = reader.error();return false;
        }
        if (!decodeRuntimeMultiplayerSnapshotDescriptor(encoded, descriptor, errorMessage)
            || descriptor.identity.kind != RuntimeMultiplayerSnapshotKind::Save
            || descriptor.identity.snapshotTick >= runtimeMultiplayerSnapshotMaximumTick) {
            if (errorMessage.empty()) errorMessage = "invalid lobby resume Save descriptor";
            return false;
        }
        parsed.body = std::move(descriptor);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapChunk:
    case RuntimeMultiplayerMessageType::LobbyResumeChunk:
    case RuntimeMultiplayerMessageType::SnapshotChunk: {
        std::vector<std::uint8_t> encoded;
        RuntimeMultiplayerSnapshotChunk chunk;
        if (!reader.readBytes(
                runtimeMultiplayerSnapshotChunkBytes + 64U,
                encoded,
                "snapshot chunk"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!decodeRuntimeMultiplayerSnapshotChunk(
                encoded,
                chunk,
                errorMessage
            )) {
            return false;
        }
        parsed.body = std::move(chunk);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyMapAck:
    case RuntimeMultiplayerMessageType::LobbyResumeAck:
    case RuntimeMultiplayerMessageType::SnapshotChunkAck: {
        std::vector<std::uint8_t> encoded;
        RuntimeMultiplayerSnapshotChunkAck ack;
        if (!reader.readBytes(64U, encoded, "snapshot chunk ack")) {
            errorMessage = reader.error();
            return false;
        }
        if (!decodeRuntimeMultiplayerSnapshotChunkAck(
                encoded,
                ack,
                errorMessage
            )) {
            return false;
        }
        parsed.body = ack;
        break;
    }
    case RuntimeMultiplayerMessageType::RoomConfig: {
        RuntimeMultiplayerSessionConfig config;
        if (!readSessionConfig(reader, config)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateRoomConfig(config, errorMessage)) return false;
        parsed.body = std::move(config);
        break;
    }
    case RuntimeMultiplayerMessageType::AdmissionChallenge: {
        RuntimeMultiplayerAdmissionChallengeMessage challenge;
        if (!reader.readU64(challenge.roomGeneration)
            || !reader.readU64(challenge.admissionToken)
            || !reader.readU32(challenge.reconnectGraceMilliseconds)
            || !readSessionConfig(reader, challenge.config)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateAdmissionChallenge(challenge, errorMessage)) return false;
        parsed.body = std::move(challenge);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinAdmissionRequest: {
        RuntimeMultiplayerJoinAdmissionRequestMessage admission;
        std::uint32_t reconnectTick = 0;
        if (!reader.readU64(admission.roomGeneration)
            || !reader.readU64(admission.admissionToken)
            || !readJoinRequest(reader, admission.request)
            || !reader.readBool(admission.reconnect)
            || !reader.readU64(admission.reconnectTimelineEpoch)
            || !reader.readU32(reconnectTick)
            || !reader.readU64(admission.reconnectSessionStateHash)) {
            errorMessage = reader.error();
            return false;
        }
        if (reconnectTick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            admission.reconnectAuthoritativeTick =
                static_cast<int>(reconnectTick);
        }
        if (!validateJoinAdmissionRequest(admission, errorMessage)) {
            return false;
        }
        parsed.body = std::move(admission);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinAccepted: {
        RuntimeMultiplayerJoinAcceptedMessage accepted;
        if (!reader.readU64(accepted.roomGeneration)
            || !reader.readString(
                runtimeMultiplayerMaximumRoomIdBytes,
                accepted.roomId,
                "room id"
            )
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                accepted.peerId,
                "peer id"
            )
            || !reader.readBool(accepted.reconnected)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateJoinAccepted(accepted, errorMessage)) return false;
        parsed.body = std::move(accepted);
        break;
    }
    case RuntimeMultiplayerMessageType::JoinRejected: {
        RuntimeMultiplayerJoinRejectedMessage rejected;
        if (!reader.readString(
                runtimeMultiplayerMaximumRoomIdBytes,
                rejected.roomId,
                "room id"
            )
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                rejected.peerId,
                "peer id"
            )
            || !reader.readString(
                runtimeMultiplayerMaximumJoinRejectionReasonBytes,
                rejected.reason,
                "join rejection reason"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateJoinRejected(rejected, errorMessage)) return false;
        parsed.body = std::move(rejected);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySnapshot: {
        RuntimeMultiplayerLobbySnapshotMessage snapshot;
        std::uint32_t memberCount = 0;
        bool hasMatchSetup = false;
        if (!reader.readU64(snapshot.roomGeneration)
            || !reader.readU64(snapshot.snapshot.timelineEpoch)
            || !readSessionConfig(reader, snapshot.snapshot.config)
            || !reader.readU64(
                snapshot.snapshot.latestMatchSetupRevision
            )
            || !reader.readBool(hasMatchSetup)) {
            errorMessage = reader.error();
            return false;
        }
        if (hasMatchSetup) {
            std::vector<std::uint8_t> setupBytes;
            if (!reader.readBytes(
                    runtimeMultiplayerMatchMaximumPayloadBytes,
                    setupBytes,
                    "match setup"
                )) {
                errorMessage = reader.error();
                return false;
            }
            RuntimeMultiplayerMatchSetup setup;
            std::string setupError;
            if (!decodeRuntimeMultiplayerMatchSetup(
                    setupBytes,
                    setup,
                    setupError
                )) {
                errorMessage =
                    "multiplayer lobby snapshot setup is invalid: "
                        + setupError;
                return false;
            }
            snapshot.snapshot.matchSetup = std::move(setup);
        }
        if (!reader.readU32(memberCount)) {
            errorMessage = reader.error();
            return false;
        }
        if (memberCount == 0
            || memberCount > runtimeMultiplayerMaximumMembers) {
            errorMessage =
                "multiplayer lobby snapshot member count is invalid";
            return false;
        }
        snapshot.snapshot.members.reserve(memberCount);
        for (std::uint32_t index = 0; index < memberCount; ++index) {
            RuntimeMultiplayerRoomMember member;
            if (!readRoomMember(reader, member)) {
                errorMessage = reader.error().empty()
                    ? "multiplayer message member role is invalid"
                    : reader.error();
                return false;
            }
            snapshot.snapshot.members.push_back(std::move(member));
        }
        if (!validateLobbySnapshot(snapshot, errorMessage)) return false;
        parsed.body = std::move(snapshot);
        break;
    }
    case RuntimeMultiplayerMessageType::SessionAbort: {
        RuntimeMultiplayerSessionAbortMessage abortMessage;
        std::uint32_t tick = 0;
        std::uint8_t reason = 0;
        if (!reader.readU64(abortMessage.roomGeneration)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                abortMessage.abort.requestingPeerId,
                "requesting peer id"
            )
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                abortMessage.abort.affectedPeerId,
                "affected peer id"
            )
            || !reader.readU32(tick)
            || !reader.readU8(reason)) {
            errorMessage = reader.error();
            return false;
        }
        if (tick <= static_cast<std::uint32_t>(
                runtimeMultiplayerMaximumTick
            )) {
            abortMessage.abort.tick = static_cast<int>(tick);
        }
        abortMessage.abort.reason =
            static_cast<RuntimeMultiplayerSessionAbortReason>(reason);
        if (!validateSessionAbort(abortMessage, errorMessage)) return false;
        parsed.body = std::move(abortMessage);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySlotOptionsRequest: {
        RuntimeMultiplayerLobbySlotOptionsRequestMessage request;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                request.peerId,
                "peer id"
            )
            || !reader.readU64(request.requestId)
            || !reader.readU64(request.expectedSetupRevision)
            || !reader.readU8(request.slotIndex)
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                request.countryId,
                "country id"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                request.colorId,
                "color id"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                request.teamId,
                "team id"
            )
            || !reader.readString(
                runtimeMultiplayerMatchMaximumSlotOptionBytes,
                request.difficultyId,
                "difficulty id"
            )
            || !reader.readU8(request.startPosition)) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateLobbySlotOptionsRequest(request, errorMessage)) {
            return false;
        }
        parsed.body = std::move(request);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySlotOptionsResult: {
        RuntimeMultiplayerLobbySlotOptionsResultMessage result;
        if (!reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                result.peerId,
                "peer id"
            )
            || !reader.readU64(result.requestId)
            || !reader.readBool(result.accepted)
            || !reader.readU64(result.currentSetupRevision)
            || !reader.readString(
                runtimeMultiplayerMaximumLobbyResultReasonBytes,
                result.reason,
                "lobby result reason"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateLobbySlotOptionsResult(result, errorMessage)) {
            return false;
        }
        parsed.body = std::move(result);
        break;
    }
    case RuntimeMultiplayerMessageType::LobbyKicked: {
        RuntimeMultiplayerLobbyKickedMessage kicked;
        if (!reader.readU64(kicked.roomGeneration)
            || !reader.readString(
                runtimeMultiplayerMaximumPeerIdBytes,
                kicked.peerId,
                "peer id"
            )
            || !reader.readString(
                runtimeMultiplayerMaximumLobbyResultReasonBytes,
                kicked.reason,
                "kicked reason"
            )) {
            errorMessage = reader.error();
            return false;
        }
        if (!validateLobbyKicked(kicked, errorMessage)) return false;
        parsed.body = std::move(kicked);
        break;
    }
    default:
        errorMessage = "multiplayer message type is unknown";
        return false;
    }

    if (!reader.finished()) {
        errorMessage = "multiplayer message contains trailing bytes";
        return false;
    }
    message = std::move(parsed);
    return true;
}

RuntimeMultiplayerMessageApplyResult applyRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    std::string_view authenticatedPeerId,
    RuntimeMultiplayerSessionState& session
) {
    RuntimeMultiplayerMessageApplyResult rejected;
    RuntimeMultiplayerMessage message;
    if (!decodeRuntimeMultiplayerMessage(packet, message, rejected.reason)) {
        return rejected;
    }
    rejected.decoded = true;
    rejected.type = message.type;

    const auto sourceMatches = [&rejected, authenticatedPeerId](
        const std::string& claimedPeerId
    ) {
        if (authenticatedPeerId == claimedPeerId) return true;
        rejected.reason =
            "multiplayer message peer does not match the authenticated connection";
        return false;
    };

    switch (message.type) {
    case RuntimeMultiplayerMessageType::JoinRequest: {
        const RuntimeMultiplayerJoinRequest& request =
            std::get<RuntimeMultiplayerJoinRequest>(message.body);
        // Empty identifies a connection that has not completed its first
        // successful JoinRequest. It may claim only a new id: allowing an
        // identical duplicate here would let a second connection take over an
        // existing member. Once bound, that same connection may retry its
        // identical JoinRequest idempotently.
        if (authenticatedPeerId.empty()) {
            const bool alreadyJoined = std::any_of(
                session.members().begin(),
                session.members().end(),
                [&request](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == request.peerId;
                }
            );
            if (alreadyJoined) {
                rejected.reason =
                    "unbound connection may not claim an existing room member";
                return rejected;
            }
        } else if (!sourceMatches(request.peerId)) {
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick: {
        const RuntimeMultiplayerAdvanceTickMessage& advance =
            std::get<RuntimeMultiplayerAdvanceTickMessage>(message.body);
        if (!sourceMatches(advance.requestingPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may advance tick";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::Leave:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerLeaveMessage>(message.body).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::Ready:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerReadyMessage>(message.body).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::Start: {
        const RuntimeMultiplayerStartMessage& start =
            std::get<RuntimeMultiplayerStartMessage>(message.body);
        if (!sourceMatches(start.requestingPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may request start";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::Command:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerCommandPacket>(message.body).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::StateHash:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerStateHashReport>(message.body).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::SaveLoadProposal: {
        const RuntimeMultiplayerSaveLoadProposal& proposal =
            std::get<RuntimeMultiplayerSaveLoadProposal>(message.body);
        if (!sourceMatches(proposal.proposerPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may propose save/load";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadResponse:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerSaveLoadResponseMessage>(
                    message.body
                ).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::SaveLoadCommit: {
        const RuntimeMultiplayerSaveLoadCommitMessage& commit =
            std::get<RuntimeMultiplayerSaveLoadCommitMessage>(message.body);
        if (!sourceMatches(commit.requestingPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may commit save/load";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadApplied:
        if (!sourceMatches(
                std::get<RuntimeMultiplayerSaveLoadAppliedMessage>(
                    message.body
                ).peerId
            )) {
            return rejected;
        }
        break;
    case RuntimeMultiplayerMessageType::SaveLoadAbort: {
        const RuntimeMultiplayerSaveLoadAbortMessage& abort =
            std::get<RuntimeMultiplayerSaveLoadAbortMessage>(message.body);
        if (!sourceMatches(abort.requestingPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may abort save/load";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::SessionAbort: {
        const RuntimeMultiplayerSessionAbortMessage& abort =
            std::get<RuntimeMultiplayerSessionAbortMessage>(message.body);
        if (!sourceMatches(abort.abort.requestingPeerId)) return rejected;
        if (authenticatedPeerId != session.config().hostPeerId) {
            rejected.reason =
                "only the authenticated host connection may abort the session";
            return rejected;
        }
        break;
    }
    case RuntimeMultiplayerMessageType::RoomConfig:
    case RuntimeMultiplayerMessageType::LobbySnapshot:
        rejected.reason =
            "room state requires a trusted canonical host broadcast";
        return rejected;
    case RuntimeMultiplayerMessageType::AdmissionChallenge:
    case RuntimeMultiplayerMessageType::JoinAdmissionRequest:
    case RuntimeMultiplayerMessageType::JoinAccepted:
    case RuntimeMultiplayerMessageType::JoinRejected:
    case RuntimeMultiplayerMessageType::LobbySlotOptionsRequest:
    case RuntimeMultiplayerMessageType::LobbySlotOptionsResult:
    case RuntimeMultiplayerMessageType::LobbyKicked:
        rejected.reason =
            "LAN lobby control messages must be handled by the coordinator";
        return rejected;
    case RuntimeMultiplayerMessageType::LobbyMapRequest:
    case RuntimeMultiplayerMessageType::LobbyMapOffer:
    case RuntimeMultiplayerMessageType::LobbyMapChunk:
    case RuntimeMultiplayerMessageType::LobbyMapAck:
    case RuntimeMultiplayerMessageType::LobbyResumeOffer:
    case RuntimeMultiplayerMessageType::LobbyResumeChunk:
    case RuntimeMultiplayerMessageType::LobbyResumeAck:
    case RuntimeMultiplayerMessageType::SnapshotChunk:
    case RuntimeMultiplayerMessageType::SnapshotChunkAck:
        rejected.reason =
            "snapshot transfer messages must be handled by the coordinator";
        return rejected;
    default:
        rejected.decoded = false;
        rejected.reason = "multiplayer message type is unknown";
        return rejected;
    }

    return applyTrustedCanonicalRuntimeMultiplayerMessage(packet, session);
}

RuntimeMultiplayerMessageApplyResult
applyTrustedCanonicalRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeMultiplayerSessionState& session
) {
    RuntimeMultiplayerMessageApplyResult applied;
    RuntimeMultiplayerMessage message;
    if (!decodeRuntimeMultiplayerMessage(packet, message, applied.reason)) {
        return applied;
    }

    applied.decoded = true;
    applied.type = message.type;
    switch (message.type) {
    case RuntimeMultiplayerMessageType::JoinRequest:
        copyOperationResult(
            session.joinMember(
                std::get<RuntimeMultiplayerJoinRequest>(message.body)
            ),
            applied
        );
        break;
    case RuntimeMultiplayerMessageType::Leave:
        copyOperationResult(
            session.leaveMember(
                std::get<RuntimeMultiplayerLeaveMessage>(message.body).peerId
            ),
            applied
        );
        break;
    case RuntimeMultiplayerMessageType::Ready: {
        const RuntimeMultiplayerReadyMessage& ready =
            std::get<RuntimeMultiplayerReadyMessage>(message.body);
        copyOperationResult(
            session.setMemberReady(ready.peerId, ready.ready),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::Start: {
        const RuntimeMultiplayerStartMessage& start =
            std::get<RuntimeMultiplayerStartMessage>(message.body);
        copyOperationResult(
            session.start(start.requestingPeerId, start.startTick),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::AdvanceAuthoritativeTick: {
        const RuntimeMultiplayerAdvanceTickMessage& advance =
            std::get<RuntimeMultiplayerAdvanceTickMessage>(message.body);
        copyOperationResult(
            session.advanceAuthoritativeTick(
                advance.requestingPeerId,
                advance.authoritativeTick,
                advance.timelineEpoch
            ),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::Command:
        copyOperationResult(
            session.submitCommand(
                std::get<RuntimeMultiplayerCommandPacket>(message.body)
            ),
            applied
        );
        break;
    case RuntimeMultiplayerMessageType::StateHash:
        copyStateHashResult(
            session.submitStateHash(
                std::get<RuntimeMultiplayerStateHashReport>(message.body)
            ),
            applied
        );
        break;
    case RuntimeMultiplayerMessageType::SaveLoadProposal:
        copySaveLoadResult(
            session.proposeSaveLoad(
                std::get<RuntimeMultiplayerSaveLoadProposal>(message.body)
            ),
            applied
        );
        break;
    case RuntimeMultiplayerMessageType::SaveLoadResponse: {
        const RuntimeMultiplayerSaveLoadResponseMessage& response =
            std::get<RuntimeMultiplayerSaveLoadResponseMessage>(message.body);
        copySaveLoadResult(
            session.prepareSaveLoad(RuntimeMultiplayerSaveLoadResponse{
                response.timelineEpoch,
                response.proposalId,
                response.peerId,
                response.approved,
                response.observedFingerprint,
                response.observedAuthoritativeStateHash,
                response.approved,
                response.observedFingerprint,
                false
            }),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadCommit: {
        const RuntimeMultiplayerSaveLoadCommitMessage& commit =
            std::get<RuntimeMultiplayerSaveLoadCommitMessage>(message.body);
        copySaveLoadResult(
            session.issueSaveLoadCommit(
                commit.timelineEpoch,
                commit.proposalId,
                commit.requestingPeerId
            ),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadApplied: {
        const RuntimeMultiplayerSaveLoadAppliedMessage& ack =
            std::get<RuntimeMultiplayerSaveLoadAppliedMessage>(message.body);
        copySaveLoadResult(
            session.acknowledgeSaveLoadApplied(
                RuntimeMultiplayerSaveLoadApplied{
                    ack.timelineEpoch,
                    ack.proposalId,
                    ack.peerId,
                    ack.applied,
                    ack.observedAuthoritativeStateHash
                }
            ),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::SaveLoadAbort: {
        const RuntimeMultiplayerSaveLoadAbortMessage& abort =
            std::get<RuntimeMultiplayerSaveLoadAbortMessage>(message.body);
        copySaveLoadResult(
            session.abortSaveLoad(
                abort.timelineEpoch,
                abort.proposalId,
                abort.requestingPeerId
            ),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::RoomConfig: {
        const RuntimeMultiplayerSessionConfig& config =
            std::get<RuntimeMultiplayerSessionConfig>(message.body);
        if (!session.active()) {
            copyOperationResult(session.beginHost(config), applied);
        } else if (session.config() == config) {
            applied.accepted = true;
            applied.duplicate = true;
        } else {
            applied.reason =
                "room configuration conflicts with the active session";
        }
        break;
    }
    case RuntimeMultiplayerMessageType::LobbySnapshot: {
        const RuntimeMultiplayerLobbySnapshotMessage& snapshot =
            std::get<RuntimeMultiplayerLobbySnapshotMessage>(message.body);
        copyOperationResult(
            session.replaceLobbySnapshot(snapshot.snapshot),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::SessionAbort: {
        const RuntimeMultiplayerSessionAbortMessage& abort =
            std::get<RuntimeMultiplayerSessionAbortMessage>(message.body);
        copyOperationResult(
            session.abortRunningSession(abort.abort),
            applied
        );
        break;
    }
    case RuntimeMultiplayerMessageType::AdmissionChallenge:
    case RuntimeMultiplayerMessageType::JoinAdmissionRequest:
    case RuntimeMultiplayerMessageType::JoinAccepted:
    case RuntimeMultiplayerMessageType::JoinRejected:
    case RuntimeMultiplayerMessageType::LobbySlotOptionsRequest:
    case RuntimeMultiplayerMessageType::LobbySlotOptionsResult:
    case RuntimeMultiplayerMessageType::LobbyKicked:
        applied.reason =
            "LAN lobby control message has no direct session state transition";
        break;
    case RuntimeMultiplayerMessageType::LobbyMapRequest:
    case RuntimeMultiplayerMessageType::LobbyMapOffer:
    case RuntimeMultiplayerMessageType::LobbyMapChunk:
    case RuntimeMultiplayerMessageType::LobbyMapAck:
    case RuntimeMultiplayerMessageType::LobbyResumeOffer:
    case RuntimeMultiplayerMessageType::LobbyResumeChunk:
    case RuntimeMultiplayerMessageType::LobbyResumeAck:
    case RuntimeMultiplayerMessageType::SnapshotChunk:
    case RuntimeMultiplayerMessageType::SnapshotChunkAck:
        applied.reason =
            "snapshot transfer message has no direct session state transition";
        break;
    default:
        applied.decoded = false;
        applied.reason = "multiplayer message type is unknown";
        break;
    }
    return applied;
}
