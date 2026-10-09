#include "RuntimeMultiplayerSessionSystem.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kStateHashSchema = 7;

class StableHasher {
public:
    void addByte(std::uint8_t value) {
        value_ ^= value;
        value_ *= kFnvPrime;
    }

    void addBool(bool value) {
        addByte(value ? 1U : 0U);
    }

    void addUint64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            addByte(static_cast<std::uint8_t>(
                (value >> static_cast<unsigned int>(shift)) & 0xffU
            ));
        }
    }

    void addInt(int value) {
        addUint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)
        ));
    }

    void addSize(std::size_t value) {
        addUint64(static_cast<std::uint64_t>(value));
    }

    void addString(const std::string& value) {
        addSize(value.size());
        for (const unsigned char byte : value) {
            addByte(byte);
        }
    }

    void addBytes(const std::vector<std::uint8_t>& value) {
        addSize(value.size());
        for (const std::uint8_t byte : value) {
            addByte(byte);
        }
    }

    std::uint64_t value() const {
        return value_;
    }

private:
    std::uint64_t value_ = kFnvOffsetBasis;
};

RuntimeMultiplayerOperationResult rejectedOperation(std::string reason) {
    RuntimeMultiplayerOperationResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerOperationResult acceptedOperation(
    bool changed,
    bool duplicate = false
) {
    RuntimeMultiplayerOperationResult result;
    result.accepted = true;
    result.changed = changed;
    result.duplicate = duplicate;
    return result;
}

RuntimeMultiplayerStateHashResult rejectedStateHash(std::string reason) {
    RuntimeMultiplayerStateHashResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerSaveLoadResult rejectedSaveLoad(std::string reason) {
    RuntimeMultiplayerSaveLoadResult result;
    result.reason = std::move(reason);
    return result;
}

bool validIdentifier(const std::string& value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        const bool asciiAlphaNumeric =
            (character >= static_cast<unsigned char>('a')
                && character <= static_cast<unsigned char>('z'))
            || (character >= static_cast<unsigned char>('A')
                && character <= static_cast<unsigned char>('Z'))
            || (character >= static_cast<unsigned char>('0')
                && character <= static_cast<unsigned char>('9'));
        return asciiAlphaNumeric
            || character == static_cast<unsigned char>('-')
            || character == static_cast<unsigned char>('_')
            || character == static_cast<unsigned char>('.');
    });
}

bool validDisplayText(const std::string& value, std::size_t maximumBytes) {
    if (value.empty() || value.size() > maximumBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 0x20U && character != 0x7fU;
    });
}

bool validTick(int tick) {
    return tick >= 0 && tick <= runtimeMultiplayerMaximumTick;
}

bool validCompatibilityShape(
    const RuntimeMultiplayerCompatibility& compatibility
) {
    return compatibility.protocolVersion == runtimeMultiplayerProtocolVersion
        && compatibility.protocolFingerprint != 0
        && compatibility.contentFingerprint != 0;
}

bool validSaveLoadKind(RuntimeMultiplayerSaveLoadKind kind) {
    return kind == RuntimeMultiplayerSaveLoadKind::Save
        || kind == RuntimeMultiplayerSaveLoadKind::Load;
}

bool validParticipationRole(RuntimeMultiplayerParticipationRole role) {
    return role == RuntimeMultiplayerParticipationRole::Combatant
        || role == RuntimeMultiplayerParticipationRole::Spectator;
}

bool validAbortReason(RuntimeMultiplayerSessionAbortReason reason) {
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

bool memberLess(
    const RuntimeMultiplayerRoomMember& left,
    const RuntimeMultiplayerRoomMember& right
) {
    if (left.role != right.role) {
        return left.role == RuntimeMultiplayerRoomMemberRole::Host;
    }
    return left.peerId < right.peerId;
}

bool commandLess(
    const RuntimeMultiplayerCommandPacket& left,
    const RuntimeMultiplayerCommandPacket& right
) {
    return std::tie(
        left.timelineEpoch,
        left.tick,
        left.peerId,
        left.sequence
    ) < std::tie(
        right.timelineEpoch,
        right.tick,
        right.peerId,
        right.sequence
    );
}

bool reportLess(
    const RuntimeMultiplayerStateHashReport& left,
    const RuntimeMultiplayerStateHashReport& right
) {
    return std::tie(left.timelineEpoch, left.peerId)
        < std::tie(right.timelineEpoch, right.peerId);
}

bool responseLess(
    const RuntimeMultiplayerSaveLoadResponse& left,
    const RuntimeMultiplayerSaveLoadResponse& right
) {
    return left.peerId < right.peerId;
}

bool appliedLess(
    const RuntimeMultiplayerSaveLoadApplied& left,
    const RuntimeMultiplayerSaveLoadApplied& right
) {
    return left.peerId < right.peerId;
}

const RuntimeMultiplayerRoomMember* findMember(
    const std::vector<RuntimeMultiplayerRoomMember>& members,
    const std::string& peerId
) {
    const auto found = std::find_if(
        members.begin(),
        members.end(),
        [&peerId](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == peerId;
        }
    );
    return found == members.end() ? nullptr : &*found;
}

RuntimeMultiplayerRoomMember* findMember(
    std::vector<RuntimeMultiplayerRoomMember>& members,
    const std::string& peerId
) {
    const auto found = std::find_if(
        members.begin(),
        members.end(),
        [&peerId](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == peerId;
        }
    );
    return found == members.end() ? nullptr : &*found;
}

void clearLobbyReadiness(
    std::vector<RuntimeMultiplayerRoomMember>& members
) {
    for (RuntimeMultiplayerRoomMember& member : members) {
        member.ready = false;
    }
}

bool matchSetupMatchesMembers(
    const RuntimeMultiplayerMatchSetup& setup,
    const std::vector<RuntimeMultiplayerRoomMember>& members,
    std::string& reason
) {
    if (setup.members.size() != members.size()) {
        reason = "match setup member count does not match the room roster";
        return false;
    }
    for (const RuntimeMultiplayerRoomMember& roomMember : members) {
        const auto setupMember = std::find_if(
            setup.members.begin(),
            setup.members.end(),
            [&roomMember](const RuntimeMultiplayerMatchMember& candidate) {
                return candidate.peerId == roomMember.peerId;
            }
        );
        if (setupMember == setup.members.end()
            || setupMember->displayName != roomMember.displayName) {
            reason =
                "match setup member identity does not match the room roster";
            return false;
        }
        const RuntimeMultiplayerMatchMemberIdentity expectedIdentity =
            roomMember.participation
                    == RuntimeMultiplayerParticipationRole::Combatant
                ? RuntimeMultiplayerMatchMemberIdentity::Combatant
                : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        if (setupMember->identity != expectedIdentity) {
            reason =
                "match setup participation does not match the room roster";
            return false;
        }
    }
    reason.clear();
    return true;
}

void recomputeStateHashFrame(
    RuntimeMultiplayerStateHashFrame& frame,
    const std::vector<RuntimeMultiplayerRoomMember>& members,
    const std::string& hostPeerId
) {
    frame.complete = frame.reports.size() == members.size()
        && std::all_of(
            members.begin(),
            members.end(),
            [&frame](const RuntimeMultiplayerRoomMember& member) {
                return std::any_of(
                    frame.reports.begin(),
                    frame.reports.end(),
                    [&member](
                        const RuntimeMultiplayerStateHashReport& report
                    ) {
                        return report.peerId == member.peerId;
                    }
                );
            }
        );
    frame.hostStateHash = 0;
    const auto host = std::find_if(
        frame.reports.begin(),
        frame.reports.end(),
        [&hostPeerId](const RuntimeMultiplayerStateHashReport& report) {
            return report.peerId == hostPeerId;
        }
    );
    if (host != frame.reports.end()) {
        frame.hostStateHash = host->stateHash;
    }
    frame.desync = frame.complete
        && host != frame.reports.end()
        && std::any_of(
            frame.reports.begin(),
            frame.reports.end(),
            [&host](const RuntimeMultiplayerStateHashReport& report) {
                return report.stateHash != host->stateHash;
            }
        );
}

void recomputePreparedSaveLoadHandshake(
    RuntimeMultiplayerSaveLoadHandshake& handshake
) {
    if (handshake.status == RuntimeMultiplayerSaveLoadStatus::Rejected) {
        return;
    }
    const bool rejected = std::any_of(
        handshake.responses.begin(),
        handshake.responses.end(),
        [](const RuntimeMultiplayerSaveLoadResponse& response) {
            return !response.prepared;
        }
    );
    if (rejected) {
        handshake.status = RuntimeMultiplayerSaveLoadStatus::Rejected;
        return;
    }
    handshake.status = handshake.responses.size()
            == handshake.participants.size()
        ? RuntimeMultiplayerSaveLoadStatus::Prepared
        : RuntimeMultiplayerSaveLoadStatus::Transferring;
}

bool hasActiveSaveLoad(
    const std::vector<RuntimeMultiplayerSaveLoadHandshake>& handshakes
) {
    return std::any_of(
        handshakes.begin(),
        handshakes.end(),
        [](const RuntimeMultiplayerSaveLoadHandshake& handshake) {
            return handshake.status
                    == RuntimeMultiplayerSaveLoadStatus::Transferring
                || handshake.status
                    == RuntimeMultiplayerSaveLoadStatus::Prepared
                || handshake.status
                    == RuntimeMultiplayerSaveLoadStatus::CommitIssued;
        }
    );
}

void hashCompatibility(
    StableHasher& hash,
    const RuntimeMultiplayerCompatibility& compatibility
) {
    hash.addUint64(compatibility.protocolVersion);
    hash.addUint64(compatibility.protocolFingerprint);
    hash.addUint64(compatibility.contentFingerprint);
}

void hashSnapshotDescriptor(
    StableHasher& hash,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor
) {
    const RuntimeMultiplayerSnapshotIdentity& identity = descriptor.identity;
    hash.addUint64(identity.proposalId);
    hash.addUint64(static_cast<std::uint8_t>(identity.kind));
    hash.addUint64(identity.sourceTimelineEpoch);
    hash.addInt(identity.barrierTick);
    hash.addInt(identity.snapshotTick);
    hash.addString(identity.snapshotToken);
    hash.addUint64(identity.authoritativeStateHash);
    hash.addUint64(identity.gameplaySetupFingerprint);
    hash.addUint64(static_cast<std::uint8_t>(descriptor.compression));
    hash.addUint64(descriptor.byteCount);
    hash.addUint64(descriptor.chunkBytes);
    hash.addUint64(descriptor.chunkCount);
    hash.addUint64(descriptor.snapshotFingerprint);
    hash.addSize(descriptor.sha256.size());
    for (const std::uint8_t byte : descriptor.sha256) hash.addByte(byte);
}

} // namespace

void RuntimeMultiplayerSessionState::reset() {
    config_ = RuntimeMultiplayerSessionConfig{};
    status_ = RuntimeMultiplayerRoomStatus::Inactive;
    startTick_ = -1;
    authoritativeTick_ = -1;
    timelineEpoch_ = 0;
    firstDesyncTick_ = -1;
    members_.clear();
    departedSpectatorPeerIds_.clear();
    latestMatchSetupRevision_ = 0;
    matchSetup_.reset();
    commands_.clear();
    commandSequenceWindows_.clear();
    stateHashFrames_.clear();
    saveLoadHandshakes_.clear();
    highestSaveLoadProposalId_ = 0;
    abortState_ = RuntimeMultiplayerSessionAbort{};
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerSessionState::beginHost(
    RuntimeMultiplayerSessionConfig config
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Inactive) {
        return rejectedOperation("multiplayer session is already active");
    }
    if (!validIdentifier(config.roomId, runtimeMultiplayerMaximumRoomIdBytes)) {
        return rejectedOperation("room id is invalid");
    }
    if (!validIdentifier(
            config.hostPeerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return rejectedOperation("host peer id is invalid");
    }
    if (!validDisplayText(
            config.hostDisplayName,
            runtimeMultiplayerMaximumDisplayNameBytes
        )) {
        return rejectedOperation("host display name is invalid");
    }
    if (!validParticipationRole(config.hostParticipation)) {
        return rejectedOperation("host participation role is invalid");
    }
    if (!validCompatibilityShape(config.compatibility)) {
        return rejectedOperation("host protocol or content fingerprint is invalid");
    }
    if (config.maximumMembers < 2
        || config.maximumMembers > runtimeMultiplayerMaximumMembers) {
        return rejectedOperation("multiplayer member limit is invalid");
    }
    if (config.maximumCommandPayloadBytes == 0
        || config.maximumCommandPayloadBytes
            > runtimeMultiplayerMaximumCommandPayloadBytes) {
        return rejectedOperation("multiplayer command payload limit is invalid");
    }
    if (config.maximumBufferedCommands == 0
        || config.maximumBufferedCommands
            > runtimeMultiplayerMaximumBufferedCommands) {
        return rejectedOperation("multiplayer command count limit is invalid");
    }
    if (config.maximumBufferedCommandsPerPeer == 0
        || config.maximumBufferedCommandsPerPeer
            > runtimeMultiplayerMaximumBufferedCommandsPerPeer
        || config.maximumBufferedCommandsPerPeer
            > config.maximumBufferedCommands / config.maximumMembers) {
        return rejectedOperation(
            "multiplayer command buffer cannot reserve every peer quota"
        );
    }
    if (config.maximumStateHashFrames == 0
        || config.maximumStateHashFrames
            > runtimeMultiplayerMaximumStateHashFrames) {
        return rejectedOperation("multiplayer state hash frame limit is invalid");
    }
    if (config.maximumSaveLoadHandshakes == 0
        || config.maximumSaveLoadHandshakes
            > runtimeMultiplayerMaximumSaveLoadHandshakes) {
        return rejectedOperation("multiplayer save/load handshake limit is invalid");
    }
    if (config.commandLeadWindowTicks < 0
        || config.commandLeadWindowTicks
            > runtimeMultiplayerMaximumCommandLeadWindowTicks) {
        return rejectedOperation("multiplayer command lead window is invalid");
    }
    if (config.stateHashLagWindowTicks < 0
        || config.stateHashLagWindowTicks
            > runtimeMultiplayerMaximumStateHashLagWindowTicks) {
        return rejectedOperation("multiplayer state hash lag window is invalid");
    }
    if (config.stateHashPeriodTicks <= 0
        || config.stateHashPeriodTicks > runtimeMultiplayerMaximumTick) {
        return rejectedOperation("multiplayer state hash period is invalid");
    }
    const std::size_t requiredHashFrameCapacity =
        static_cast<std::size_t>(
            config.stateHashLagWindowTicks / config.stateHashPeriodTicks
        ) + 1U;
    if (config.maximumStateHashFrames < requiredHashFrameCapacity) {
        return rejectedOperation(
            "multiplayer state hash frame limit cannot cover the lag window"
        );
    }

    reset();
    config_ = std::move(config);
    status_ = RuntimeMultiplayerRoomStatus::Lobby;
    timelineEpoch_ = 1;
    RuntimeMultiplayerRoomMember host;
    host.peerId = config_.hostPeerId;
    host.displayName = config_.hostDisplayName;
    host.role = RuntimeMultiplayerRoomMemberRole::Host;
    host.participation = config_.hostParticipation;
    members_.push_back(std::move(host));
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::replaceLobbySnapshot(
    RuntimeMultiplayerLobbySnapshot snapshot
) {
    if (status_ == RuntimeMultiplayerRoomStatus::Running
        || status_ == RuntimeMultiplayerRoomStatus::Aborted) {
        return rejectedOperation(
            "a lobby snapshot cannot replace a started multiplayer session"
        );
    }
    if (snapshot.timelineEpoch != 1) {
        return rejectedOperation(
            "lobby snapshots must use the initial timeline epoch"
        );
    }

    RuntimeMultiplayerSessionState candidate;
    RuntimeMultiplayerOperationResult begun =
        candidate.beginHost(snapshot.config);
    if (!begun.accepted) {
        return rejectedOperation(
            "lobby snapshot configuration is invalid: " + begun.reason
        );
    }
    if (snapshot.members.empty()
        || snapshot.members.size() > snapshot.config.maximumMembers) {
        return rejectedOperation("lobby snapshot member count is invalid");
    }

    bool foundHost = false;
    for (const RuntimeMultiplayerRoomMember& member : snapshot.members) {
        if (member.role == RuntimeMultiplayerRoomMemberRole::Host) {
            if (foundHost
                || member.peerId != snapshot.config.hostPeerId
                || member.displayName != snapshot.config.hostDisplayName
                || member.participation
                    != snapshot.config.hostParticipation) {
                return rejectedOperation(
                    "lobby snapshot host member is invalid"
                );
            }
            foundHost = true;
            continue;
        }
        if (member.role != RuntimeMultiplayerRoomMemberRole::Client) {
            return rejectedOperation("lobby snapshot member role is invalid");
        }
        RuntimeMultiplayerJoinRequest request;
        request.peerId = member.peerId;
        request.displayName = member.displayName;
        request.participation = member.participation;
        request.compatibility = snapshot.config.compatibility;
        RuntimeMultiplayerOperationResult joined =
            candidate.joinMember(std::move(request));
        if (!joined.accepted) {
            return rejectedOperation(
                "lobby snapshot member is invalid: " + joined.reason
            );
        }
    }
    if (!foundHost) {
        return rejectedOperation("lobby snapshot has no host member");
    }
    // Ready flags are applied only after roster/setup validation.
    std::vector<RuntimeMultiplayerRoomMember> unreadySnapshotMembers =
        snapshot.members;
    clearLobbyReadiness(unreadySnapshotMembers);
    if (candidate.members() != unreadySnapshotMembers) {
        return rejectedOperation(
            "lobby snapshot members are not in canonical order"
        );
    }

    if (snapshot.matchSetup.has_value()) {
        if (snapshot.latestMatchSetupRevision
            != snapshot.matchSetup->revision) {
            return rejectedOperation(
                "lobby snapshot match setup revision is inconsistent"
            );
        }
        RuntimeMultiplayerOperationResult configured = candidate.setMatchSetup(
            snapshot.config.hostPeerId,
            *snapshot.matchSetup
        );
        if (!configured.accepted) {
            return rejectedOperation(
                "lobby snapshot match setup is invalid: "
                    + configured.reason
            );
        }
    } else {
        candidate.latestMatchSetupRevision_ =
            snapshot.latestMatchSetupRevision;
    }

    for (const RuntimeMultiplayerRoomMember& member : snapshot.members) {
        if (!member.ready) continue;
        RuntimeMultiplayerOperationResult readied =
            candidate.setMemberReady(member.peerId, true);
        if (!readied.accepted) {
            return rejectedOperation(
                "lobby snapshot ready state is invalid: " + readied.reason
            );
        }
    }
    if (candidate.members() != snapshot.members) {
        return rejectedOperation(
            "lobby snapshot members are not in canonical order"
        );
    }

    if (status_ == RuntimeMultiplayerRoomStatus::Lobby
        && stateHash() == candidate.stateHash()) {
        return acceptedOperation(false, true);
    }
    *this = std::move(candidate);
    return acceptedOperation(true);
}

RuntimeMultiplayerCompatibilityResult
RuntimeMultiplayerSessionState::validateCompatibility(
    const RuntimeMultiplayerCompatibility& compatibility
) const {
    RuntimeMultiplayerCompatibilityResult result;
    if (!active()) {
        result.reason = "multiplayer session is inactive";
        return result;
    }
    if (!validCompatibilityShape(compatibility)) {
        result.reason = "peer protocol or content fingerprint is invalid";
        return result;
    }
    if (compatibility.protocolVersion
        != config_.compatibility.protocolVersion) {
        result.reason = "protocol version mismatch";
        return result;
    }
    if (compatibility.protocolFingerprint
        != config_.compatibility.protocolFingerprint) {
        result.reason = "protocol fingerprint mismatch";
        return result;
    }
    if (compatibility.contentFingerprint
        != config_.compatibility.contentFingerprint) {
        result.reason = "content fingerprint mismatch";
        return result;
    }
    result.compatible = true;
    return result;
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerSessionState::joinMember(
    RuntimeMultiplayerJoinRequest request
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Lobby) {
        return rejectedOperation("members may join only while the room is in lobby");
    }
    if (!validIdentifier(request.peerId, runtimeMultiplayerMaximumPeerIdBytes)) {
        return rejectedOperation("client peer id is invalid");
    }
    if (!validDisplayText(
            request.displayName,
            runtimeMultiplayerMaximumDisplayNameBytes
        )) {
        return rejectedOperation("client display name is invalid");
    }
    if (!validParticipationRole(request.participation)) {
        return rejectedOperation("client participation role is invalid");
    }
    const RuntimeMultiplayerCompatibilityResult compatibility =
        validateCompatibility(request.compatibility);
    if (!compatibility.compatible) {
        return rejectedOperation(compatibility.reason);
    }
    if (const RuntimeMultiplayerRoomMember* existing =
            findMember(members_, request.peerId)) {
        if (existing->role == RuntimeMultiplayerRoomMemberRole::Client
            && existing->displayName == request.displayName
            && existing->participation == request.participation) {
            return acceptedOperation(false, true);
        }
        return rejectedOperation("peer id is already bound to another room member");
    }
    if (members_.size() >= config_.maximumMembers) {
        return rejectedOperation("multiplayer room member limit reached");
    }

    RuntimeMultiplayerRoomMember member;
    member.peerId = std::move(request.peerId);
    member.displayName = std::move(request.displayName);
    member.role = RuntimeMultiplayerRoomMemberRole::Client;
    member.participation = request.participation;
    members_.push_back(std::move(member));
    std::sort(members_.begin(), members_.end(), memberLess);
    matchSetup_.reset();
    clearLobbyReadiness(members_);
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerSessionState::leaveMember(
    const std::string& peerId
) {
    if (!validIdentifier(peerId, runtimeMultiplayerMaximumPeerIdBytes)) {
        return rejectedOperation("peer id is invalid");
    }
    if (status_ != RuntimeMultiplayerRoomStatus::Lobby
        && status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedOperation(
            "members may leave only while the room is in lobby or running"
        );
    }
    const auto found = std::find_if(
        members_.begin(),
        members_.end(),
        [&peerId](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == peerId;
        }
    );
    if (found == members_.end()) {
        if (status_ == RuntimeMultiplayerRoomStatus::Running
            && std::binary_search(
                departedSpectatorPeerIds_.begin(),
                departedSpectatorPeerIds_.end(),
                peerId
            )) {
            return acceptedOperation(false, true);
        }
        return rejectedOperation("room member does not exist");
    }
    if (found->role == RuntimeMultiplayerRoomMemberRole::Host) {
        return rejectedOperation("host must close the room instead of leaving it");
    }
    if (status_ == RuntimeMultiplayerRoomStatus::Running
        && found->participation
            != RuntimeMultiplayerParticipationRole::Spectator) {
        return rejectedOperation(
            "combatants may not leave a running multiplayer session"
        );
    }

    if (status_ == RuntimeMultiplayerRoomStatus::Running) {
        departedSpectatorPeerIds_.insert(
            std::lower_bound(
                departedSpectatorPeerIds_.begin(),
                departedSpectatorPeerIds_.end(),
                peerId
            ),
            peerId
        );
    }
    members_.erase(found);
    if (status_ == RuntimeMultiplayerRoomStatus::Lobby) {
        matchSetup_.reset();
        clearLobbyReadiness(members_);
        return acceptedOperation(true);
    }

    commandSequenceWindows_.erase(
        std::remove_if(
            commandSequenceWindows_.begin(),
            commandSequenceWindows_.end(),
            [&peerId](const PeerCommandSequenceWindow& window) {
                return window.peerId == peerId;
            }
        ),
        commandSequenceWindows_.end()
    );

    // Completed frames are immutable history. For every open frame, remove
    // the departed observer's evidence and close the frame if all remaining
    // authoritative participants have already reported.
    for (RuntimeMultiplayerStateHashFrame& frame : stateHashFrames_) {
        if (frame.complete) continue;
        frame.reports.erase(
            std::remove_if(
                frame.reports.begin(),
                frame.reports.end(),
                [&peerId](const RuntimeMultiplayerStateHashReport& report) {
                    return report.peerId == peerId;
                }
            ),
            frame.reports.end()
        );
        recomputeStateHashFrame(frame, members_, config_.hostPeerId);
        if (frame.desync
            && (firstDesyncTick_ < 0 || frame.tick < firstDesyncTick_)) {
            firstDesyncTick_ = frame.tick;
        }
    }

    // Active snapshot consensus follows the same canonical participant set.
    // A pre-commit transaction may become Prepared; a post-commit transaction
    // may become Committed when the departed observer was its final missing
    // acknowledgement. Terminal history is deliberately left untouched.
    for (RuntimeMultiplayerSaveLoadHandshake& handshake :
            saveLoadHandshakes_) {
        const bool active = handshake.status
                == RuntimeMultiplayerSaveLoadStatus::Transferring
            || handshake.status
                == RuntimeMultiplayerSaveLoadStatus::Prepared
            || handshake.status
                == RuntimeMultiplayerSaveLoadStatus::CommitIssued;
        if (!active) continue;
        const auto participant = std::lower_bound(
            handshake.participants.begin(),
            handshake.participants.end(),
            peerId
        );
        if (participant == handshake.participants.end()
            || *participant != peerId) {
            continue;
        }
        handshake.participants.erase(participant);
        handshake.responses.erase(
            std::remove_if(
                handshake.responses.begin(),
                handshake.responses.end(),
                [&peerId](const RuntimeMultiplayerSaveLoadResponse& response) {
                    return response.peerId == peerId;
                }
            ),
            handshake.responses.end()
        );
        handshake.appliedAcks.erase(
            std::remove_if(
                handshake.appliedAcks.begin(),
                handshake.appliedAcks.end(),
                [&peerId](const RuntimeMultiplayerSaveLoadApplied& applied) {
                    return applied.peerId == peerId;
                }
            ),
            handshake.appliedAcks.end()
        );
        if (handshake.status
            == RuntimeMultiplayerSaveLoadStatus::CommitIssued) {
            if (handshake.appliedAcks.size()
                == handshake.participants.size()) {
                handshake.status =
                    RuntimeMultiplayerSaveLoadStatus::Committed;
            }
        } else {
            recomputePreparedSaveLoadHandshake(handshake);
        }
    }
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::setMemberReady(
    const std::string& peerId,
    bool ready
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Lobby) {
        return rejectedOperation("ready state may change only in the lobby");
    }
    if (!validIdentifier(peerId, runtimeMultiplayerMaximumPeerIdBytes)) {
        return rejectedOperation("peer id is invalid");
    }
    RuntimeMultiplayerRoomMember* member = findMember(members_, peerId);
    if (member == nullptr) {
        return rejectedOperation("room member does not exist");
    }
    if (member->ready == ready) {
        return acceptedOperation(false, true);
    }
    member->ready = ready;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::setMatchSetup(
    const std::string& requestingPeerId,
    RuntimeMultiplayerMatchSetup setup
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Lobby) {
        return rejectedOperation(
            "match setup may change only while the room is in lobby"
        );
    }
    if (requestingPeerId != config_.hostPeerId) {
        return rejectedOperation("only the host may set the match setup");
    }

    std::string setupError;
    if (!canonicalizeRuntimeMultiplayerMatchSetup(setup, setupError)) {
        return rejectedOperation("match setup is invalid: " + setupError);
    }
    if (setup.hostPeerId != config_.hostPeerId) {
        return rejectedOperation(
            "match setup host does not match the room host"
        );
    }
    if (setup.revision <= latestMatchSetupRevision_) {
        return rejectedOperation(
            "match setup revision must strictly increase"
        );
    }
    std::string rosterError;
    if (!matchSetupMatchesMembers(setup, members_, rosterError)) {
        return rejectedOperation(std::move(rosterError));
    }

    latestMatchSetupRevision_ = setup.revision;
    matchSetup_ = std::move(setup);
    clearLobbyReadiness(members_);
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerSessionState::start(
    const std::string& requestingPeerId,
    int startTick
) {
    if (status_ == RuntimeMultiplayerRoomStatus::Running) {
        if (requestingPeerId == config_.hostPeerId && startTick == startTick_) {
            return acceptedOperation(false, true);
        }
        return rejectedOperation("multiplayer session has already started");
    }
    if (status_ != RuntimeMultiplayerRoomStatus::Lobby) {
        return rejectedOperation("multiplayer room is inactive");
    }
    if (requestingPeerId != config_.hostPeerId) {
        return rejectedOperation("only the host may start the room");
    }
    if (!validTick(startTick)) {
        return rejectedOperation("multiplayer start tick is invalid");
    }
    if (!matchSetup_.has_value()) {
        return rejectedOperation(
            "multiplayer room requires a match setup before start"
        );
    }
    std::string setupError;
    if (!validateRuntimeMultiplayerMatchSetup(*matchSetup_, setupError)) {
        return rejectedOperation(
            "multiplayer room match setup is inconsistent: " + setupError
        );
    }
    if (matchSetup_->hostPeerId != config_.hostPeerId) {
        return rejectedOperation(
            "multiplayer room match setup host is inconsistent"
        );
    }
    if (!matchSetupMatchesMembers(*matchSetup_, members_, setupError)) {
        return rejectedOperation(
            "multiplayer room match setup is inconsistent: " + setupError
        );
    }
    const std::size_t combatants = static_cast<std::size_t>(std::count_if(
        matchSetup_->slots.begin(),
        matchSetup_->slots.end(),
        [](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                != RuntimeMultiplayerMatchSlotController::Closed;
        }
    ));
    if (combatants < 2U) {
        return rejectedOperation(
            "multiplayer room requires at least two combatants"
        );
    }
    if (!allMembersReady()) {
        return rejectedOperation("every room member must be ready before start");
    }
    startTick_ = startTick;
    authoritativeTick_ = startTick;
    commandSequenceWindows_.clear();
    commandSequenceWindows_.reserve(members_.size());
    for (const RuntimeMultiplayerRoomMember& member : members_) {
        PeerCommandSequenceWindow window;
        window.peerId = member.peerId;
        commandSequenceWindows_.push_back(std::move(window));
    }
    status_ = RuntimeMultiplayerRoomStatus::Running;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::abortRunningSession(
    RuntimeMultiplayerSessionAbort abort
) {
    if (status_ == RuntimeMultiplayerRoomStatus::Aborted) {
        if (abort == abortState_) return acceptedOperation(false, true);
        return rejectedOperation(
            "multiplayer session already has a different abort state"
        );
    }
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedOperation(
            "only a running multiplayer session may be aborted"
        );
    }
    if (abort.requestingPeerId != config_.hostPeerId) {
        return rejectedOperation("only the host may abort the running session");
    }
    if (!validIdentifier(
            abort.affectedPeerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )
        || findMember(members_, abort.affectedPeerId) == nullptr) {
        return rejectedOperation("multiplayer abort peer is not a room member");
    }
    if (abort.tick != authoritativeTick_ || !validTick(abort.tick)) {
        return rejectedOperation(
            "multiplayer abort tick must equal the authoritative tick"
        );
    }
    if (!validAbortReason(abort.reason)) {
        return rejectedOperation("multiplayer abort reason is invalid");
    }

    abortState_ = std::move(abort);
    status_ = RuntimeMultiplayerRoomStatus::Aborted;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::advanceAuthoritativeTick(
    const std::string& requestingPeerId,
    int authoritativeTick
) {
    return advanceAuthoritativeTick(
        requestingPeerId,
        authoritativeTick,
        timelineEpoch_
    );
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::advanceAuthoritativeTick(
    const std::string& requestingPeerId,
    int authoritativeTick,
    std::uint64_t timelineEpoch
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedOperation(
            "authoritative tick advance requires a running session"
        );
    }
    if (requestingPeerId != config_.hostPeerId) {
        return rejectedOperation("only the host may advance authoritative tick");
    }
    if (timelineEpoch == 0 || timelineEpoch != timelineEpoch_) {
        return rejectedOperation(
            "authoritative tick advance timeline epoch is stale"
        );
    }
    if (!validTick(authoritativeTick)) {
        return rejectedOperation("authoritative tick is invalid");
    }
    if (authoritativeTick < authoritativeTick_) {
        return rejectedOperation("authoritative tick may not move backwards");
    }
    if (authoritativeTick == authoritativeTick_) {
        return acceptedOperation(false, true);
    }
    if (hasActiveSaveLoad(saveLoadHandshakes_)) {
        return rejectedOperation(
            "pending save/load handshake holds the authoritative tick barrier"
        );
    }

    const auto firstRetainedCommand = std::lower_bound(
        commands_.begin(),
        commands_.end(),
        authoritativeTick,
        [](const RuntimeMultiplayerCommandPacket& command, int tick) {
            return command.tick < tick;
        }
    );
    commands_.erase(commands_.begin(), firstRetainedCommand);

    const int minimumHashTick = std::max(
        startTick_,
        authoritativeTick - config_.stateHashLagWindowTicks
    );
    const auto firstRetainedHashFrame = std::lower_bound(
        stateHashFrames_.begin(),
        stateHashFrames_.end(),
        minimumHashTick,
        [](const RuntimeMultiplayerStateHashFrame& frame, int tick) {
            return frame.tick < tick;
        }
    );
    stateHashFrames_.erase(
        stateHashFrames_.begin(),
        firstRetainedHashFrame
    );
    authoritativeTick_ = authoritativeTick;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerSessionState::submitCommand(
    RuntimeMultiplayerCommandPacket packet
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedOperation("commands require a running multiplayer session");
    }
    if (packet.timelineEpoch == 0
        || packet.timelineEpoch != timelineEpoch_) {
        return rejectedOperation("multiplayer command timeline epoch is stale");
    }
    if (!validTick(packet.tick) || packet.tick < authoritativeTick_) {
        return rejectedOperation("multiplayer command tick is invalid");
    }
    const std::int64_t maximumAcceptedTick = std::min<std::int64_t>(
        runtimeMultiplayerMaximumTick,
        static_cast<std::int64_t>(authoritativeTick_)
            + static_cast<std::int64_t>(config_.commandLeadWindowTicks)
    );
    if (static_cast<std::int64_t>(packet.tick) > maximumAcceptedTick) {
        return rejectedOperation(
            "multiplayer command is beyond the authoritative lead window"
        );
    }
    const RuntimeMultiplayerRoomMember* commandMember =
        findMember(members_, packet.peerId);
    if (commandMember == nullptr) {
        return rejectedOperation("multiplayer command peer is not a room member");
    }
    if (commandMember->participation
        != RuntimeMultiplayerParticipationRole::Combatant) {
        return rejectedOperation("spectators may not submit gameplay commands");
    }
    if (packet.sequence == 0
        || packet.sequence
            > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return rejectedOperation("multiplayer command sequence is invalid");
    }
    if (packet.payload.empty()
        || packet.payload.size() > config_.maximumCommandPayloadBytes) {
        return rejectedOperation("multiplayer command payload length is invalid");
    }

    const auto existing = std::find_if(
        commands_.begin(),
        commands_.end(),
        [&packet](const RuntimeMultiplayerCommandPacket& command) {
            return command.peerId == packet.peerId
                && command.sequence == packet.sequence;
        }
    );
    if (existing != commands_.end()) {
        if (*existing == packet) {
            return acceptedOperation(false, true);
        }
        return rejectedOperation(
            "multiplayer command sequence was reused with different data"
        );
    }
    // A save/load handshake freezes authoritative tick advancement, but peers
    // can still have commands in flight before they observe the proposal.
    // Keep those commands on the current timeline. A committed Load clears
    // them atomically with the rest of that timeline; Save and abort retain
    // them for execution after the barrier is released.
    const auto sequenceWindow = std::find_if(
        commandSequenceWindows_.begin(),
        commandSequenceWindows_.end(),
        [&packet](const PeerCommandSequenceWindow& window) {
            return window.peerId == packet.peerId;
        }
    );
    if (sequenceWindow == commandSequenceWindows_.end()) {
        return rejectedOperation(
            "multiplayer command peer sequence state is unavailable"
        );
    }
    const auto acceptedSequence = std::lower_bound(
        sequenceWindow->acceptedSequences.begin(),
        sequenceWindow->acceptedSequences.end(),
        packet.sequence
    );
    if (acceptedSequence != sequenceWindow->acceptedSequences.end()
        && *acceptedSequence == packet.sequence) {
        return rejectedOperation(
            "multiplayer command sequence was already accepted"
        );
    }
    if (sequenceWindow->highestAccepted == 0) {
        if (packet.sequence > runtimeMultiplayerCommandSequenceReorderWindow) {
            return rejectedOperation(
                "initial multiplayer command sequence exceeds the reorder window"
            );
        }
    } else if (packet.sequence > sequenceWindow->highestAccepted) {
        if (packet.sequence - sequenceWindow->highestAccepted
            > runtimeMultiplayerCommandSequenceReorderWindow) {
            return rejectedOperation(
                "multiplayer command sequence jump exceeds the reorder window"
            );
        }
    } else if (sequenceWindow->highestAccepted - packet.sequence
        >= runtimeMultiplayerCommandSequenceReorderWindow) {
        return rejectedOperation(
            "multiplayer command sequence is older than the reorder window"
        );
    }
    const std::size_t peerCommandCount = static_cast<std::size_t>(
        std::count_if(
            commands_.begin(),
            commands_.end(),
            [&packet](const RuntimeMultiplayerCommandPacket& command) {
                return command.peerId == packet.peerId;
            }
        )
    );
    if (peerCommandCount >= config_.maximumBufferedCommandsPerPeer) {
        return rejectedOperation(
            "multiplayer per-peer command buffer limit reached"
        );
    }
    if (commands_.size() >= config_.maximumBufferedCommands) {
        return rejectedOperation("multiplayer command buffer limit reached");
    }

    sequenceWindow->acceptedSequences.insert(
        acceptedSequence,
        packet.sequence
    );
    sequenceWindow->highestAccepted = std::max(
        sequenceWindow->highestAccepted,
        packet.sequence
    );
    const std::uint64_t minimumRetainedSequence =
        sequenceWindow->highestAccepted
            >= runtimeMultiplayerCommandSequenceReorderWindow
        ? sequenceWindow->highestAccepted
            - runtimeMultiplayerCommandSequenceReorderWindow + 1U
        : 1U;
    sequenceWindow->acceptedSequences.erase(
        sequenceWindow->acceptedSequences.begin(),
        std::lower_bound(
            sequenceWindow->acceptedSequences.begin(),
            sequenceWindow->acceptedSequences.end(),
            minimumRetainedSequence
        )
    );
    commands_.push_back(std::move(packet));
    std::sort(commands_.begin(), commands_.end(), commandLess);
    return acceptedOperation(true);
}

std::vector<RuntimeMultiplayerCommandPacket>
RuntimeMultiplayerSessionState::commandsAtTick(int tick) const {
    std::vector<RuntimeMultiplayerCommandPacket> result;
    if (!validTick(tick)) {
        return result;
    }
    for (const RuntimeMultiplayerCommandPacket& command : commands_) {
        if (command.tick == tick) {
            result.push_back(command);
        }
    }
    return result;
}

RuntimeMultiplayerStateHashResult
RuntimeMultiplayerSessionState::submitStateHash(
    RuntimeMultiplayerStateHashReport report
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedStateHash(
            "state hash reports require a running multiplayer session"
        );
    }
    if (report.timelineEpoch == 0
        || report.timelineEpoch != timelineEpoch_) {
        return rejectedStateHash("state hash report timeline epoch is stale");
    }
    if (!validTick(report.tick) || report.tick > authoritativeTick_) {
        return rejectedStateHash("state hash report tick is invalid");
    }
    // The completed world immediately behind the current barrier is admitted
    // even when it is not a normal periodic tick.  RuntimeBridge uses this one
    // frame before the first advance and again after a Load rebind, proving
    // that map/bootstrap materialization converged before simulation resumes.
    const bool currentBarrierSnapshot = authoritativeTick_ > 0
        && report.tick == authoritativeTick_ - 1;
    if (!currentBarrierSnapshot) {
        const int minimumHashTick = std::max(
            startTick_,
            authoritativeTick_ - config_.stateHashLagWindowTicks
        );
        if (report.tick < minimumHashTick) {
            return rejectedStateHash(
                "state hash report is outside the authoritative lag window"
            );
        }
        if ((report.tick - startTick_) % config_.stateHashPeriodTicks != 0) {
            return rejectedStateHash(
                "state hash report is not on a periodic tick"
            );
        }
    }
    if (findMember(members_, report.peerId) == nullptr) {
        return rejectedStateHash("state hash report peer is not a room member");
    }
    if (report.stateHash == 0) {
        return rejectedStateHash("reported state hash must be nonzero");
    }

    auto frame = std::lower_bound(
        stateHashFrames_.begin(),
        stateHashFrames_.end(),
        report.tick,
        [](const RuntimeMultiplayerStateHashFrame& candidate, int tick) {
            return candidate.tick < tick;
        }
    );
    if (frame != stateHashFrames_.end() && frame->tick == report.tick) {
        const auto existing = std::find_if(
            frame->reports.begin(),
            frame->reports.end(),
            [&report](const RuntimeMultiplayerStateHashReport& candidate) {
                return candidate.peerId == report.peerId;
            }
        );
        if (existing != frame->reports.end()) {
            if (*existing == report) {
                RuntimeMultiplayerStateHashResult result;
                result.accepted = true;
                result.duplicate = true;
                result.frameComplete = frame->complete;
                result.desync = frame->desync;
                return result;
            }
            return rejectedStateHash(
                "peer reported conflicting state hashes for one tick"
            );
        }
    } else {
        if (stateHashFrames_.size() >= config_.maximumStateHashFrames) {
            return rejectedStateHash("multiplayer state hash frame limit reached");
        }
        RuntimeMultiplayerStateHashFrame inserted;
        inserted.tick = report.tick;
        inserted.timelineEpoch = report.timelineEpoch;
        frame = stateHashFrames_.insert(frame, std::move(inserted));
    }

    frame->reports.push_back(std::move(report));
    std::sort(frame->reports.begin(), frame->reports.end(), reportLess);
    recomputeStateHashFrame(*frame, members_, config_.hostPeerId);
    if (frame->desync
        && (firstDesyncTick_ < 0 || frame->tick < firstDesyncTick_)) {
        firstDesyncTick_ = frame->tick;
    }

    RuntimeMultiplayerStateHashResult result;
    result.accepted = true;
    result.changed = true;
    result.frameComplete = frame->complete;
    result.desync = frame->desync;
    return result;
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::proposeSaveLoad(
    RuntimeMultiplayerSaveLoadProposal proposal
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedSaveLoad(
            "save/load proposals require a running multiplayer session"
        );
    }

    if (proposal.proposalId == 0
        || proposal.proposalId
            > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return rejectedSaveLoad("save/load proposal id is invalid");
    }
    if (proposal.timelineEpoch == 0
        || proposal.timelineEpoch != timelineEpoch_) {
        return rejectedSaveLoad("save/load proposal timeline epoch is stale");
    }
    if (!validSaveLoadKind(proposal.kind)) {
        return rejectedSaveLoad("save/load proposal kind is invalid");
    }
    if (proposal.proposerPeerId != config_.hostPeerId) {
        return rejectedSaveLoad(
            "only the host may coordinate a save/load handshake"
        );
    }
    std::string descriptorError;
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            proposal.descriptor,
            descriptorError
        )) {
        return rejectedSaveLoad(
            "save/load snapshot descriptor is invalid: " + descriptorError
        );
    }
    const RuntimeMultiplayerSnapshotIdentity& identity =
        proposal.descriptor.identity;
    const RuntimeMultiplayerSnapshotKind expectedSnapshotKind =
        proposal.kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    if (identity.proposalId != proposal.proposalId
        || identity.kind != expectedSnapshotKind
        || identity.sourceTimelineEpoch != timelineEpoch_) {
        return rejectedSaveLoad(
            "save/load descriptor identity does not match the proposal"
        );
    }
    if (authoritativeTick_ <= 0
        || identity.barrierTick != authoritativeTick_ - 1) {
        return rejectedSaveLoad(
            "save/load descriptor must use the current world barrier"
        );
    }
    if ((proposal.kind == RuntimeMultiplayerSaveLoadKind::Save
            && identity.snapshotTick != identity.barrierTick)
        || (proposal.kind == RuntimeMultiplayerSaveLoadKind::Load
            && identity.snapshotTick > identity.barrierTick)) {
        return rejectedSaveLoad(
            "save/load descriptor snapshot tick is invalid for its kind"
        );
    }
    if (!matchSetup_.has_value()) {
        return rejectedSaveLoad("save/load requires a current match setup");
    }
    std::uint64_t gameplaySetupFingerprint = 0;
    std::string setupError;
    if (!runtimeMultiplayerGameplaySetupFingerprint(
            *matchSetup_,
            gameplaySetupFingerprint,
            setupError
        )
        || gameplaySetupFingerprint
            != identity.gameplaySetupFingerprint) {
        return rejectedSaveLoad(
            "save/load descriptor gameplay setup fingerprint does not match"
        );
    }
    if ((proposal.tick != -1
            && proposal.tick != identity.snapshotTick)
        || (!proposal.snapshotId.empty()
            && proposal.snapshotId != identity.snapshotToken)
        || (proposal.snapshotFingerprint != 0
            && proposal.snapshotFingerprint
                != proposal.descriptor.snapshotFingerprint)) {
        return rejectedSaveLoad(
            "deprecated save/load proposal mirrors conflict with descriptor"
        );
    }
    proposal.tick = identity.snapshotTick;
    proposal.snapshotId = identity.snapshotToken;
    proposal.snapshotFingerprint = proposal.descriptor.snapshotFingerprint;
    const auto existing = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [&proposal](const RuntimeMultiplayerSaveLoadHandshake& handshake) {
            return handshake.proposal.proposalId == proposal.proposalId;
        }
    );
    if (existing != saveLoadHandshakes_.end()) {
        if (existing->proposal == proposal) {
            RuntimeMultiplayerSaveLoadResult result;
            result.accepted = true;
            result.duplicate = true;
            result.status = existing->status;
            return result;
        }
        return rejectedSaveLoad(
            "save/load proposal id was reused with different data"
        );
    }
    if (hasActiveSaveLoad(saveLoadHandshakes_)) {
        return rejectedSaveLoad("another save/load handshake is active");
    }
    if (saveLoadHandshakes_.size() >= config_.maximumSaveLoadHandshakes) {
        return rejectedSaveLoad("multiplayer save/load handshake limit reached");
    }
    if (proposal.proposalId <= highestSaveLoadProposalId_) {
        return rejectedSaveLoad("save/load proposal ids must increase");
    }

    RuntimeMultiplayerSaveLoadHandshake handshake;
    handshake.proposal = std::move(proposal);
    handshake.status = RuntimeMultiplayerSaveLoadStatus::Transferring;
    handshake.participants.reserve(members_.size());
    for (const RuntimeMultiplayerRoomMember& member : members_) {
        handshake.participants.push_back(member.peerId);
    }
    std::sort(handshake.participants.begin(), handshake.participants.end());
    highestSaveLoadProposalId_ = handshake.proposal.proposalId;
    saveLoadHandshakes_.push_back(std::move(handshake));

    RuntimeMultiplayerSaveLoadResult result;
    result.accepted = true;
    result.changed = true;
    return result;
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::respondToSaveLoad(
    std::uint64_t proposalId,
    std::string peerId,
    bool approve,
    std::uint64_t observedFingerprint
) {
    const auto handshake = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [proposalId](const RuntimeMultiplayerSaveLoadHandshake& candidate) {
            return candidate.proposal.proposalId == proposalId;
        }
    );
    if (handshake == saveLoadHandshakes_.end()) {
        return rejectedSaveLoad("save/load proposal does not exist");
    }
    RuntimeMultiplayerSaveLoadResponse response;
    response.timelineEpoch = timelineEpoch_;
    response.proposalId = proposalId;
    response.peerId = std::move(peerId);
    response.prepared = approve;
    response.approved = approve;
    response.observedSnapshotFingerprint = observedFingerprint;
    response.observedFingerprint = observedFingerprint;
    response.observedAuthoritativeStateHash =
        handshake->proposal.descriptor.identity.authoritativeStateHash;
    return prepareSaveLoad(std::move(response));
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::prepareSaveLoad(
    RuntimeMultiplayerSaveLoadResponse response
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedSaveLoad(
            "save/load responses require a running multiplayer session"
        );
    }
    if (response.timelineEpoch == 0
        || response.timelineEpoch != timelineEpoch_) {
        return rejectedSaveLoad("save/load prepare timeline epoch is stale");
    }
    if (response.proposalId == 0) {
        return rejectedSaveLoad("save/load proposal id is invalid");
    }
    if (!validIdentifier(
            response.peerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return rejectedSaveLoad("save/load response peer id is invalid");
    }
    const bool prepared = response.prepared || response.approved;
    const std::uint64_t observedFingerprint =
        response.observedSnapshotFingerprint != 0
        ? response.observedSnapshotFingerprint
        : response.observedFingerprint;
    if ((response.observedSnapshotFingerprint != 0
            && response.observedFingerprint != 0
            && response.observedSnapshotFingerprint
                != response.observedFingerprint)
        || (prepared && (observedFingerprint == 0
            || response.observedAuthoritativeStateHash == 0))) {
        return rejectedSaveLoad("save/load prepare evidence is invalid");
    }
    auto handshake = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [&response](const RuntimeMultiplayerSaveLoadHandshake& candidate) {
            return candidate.proposal.proposalId == response.proposalId;
        }
    );
    if (handshake == saveLoadHandshakes_.end()) {
        return rejectedSaveLoad("save/load proposal does not exist");
    }
    if (std::find(
            handshake->participants.begin(),
            handshake->participants.end(),
            response.peerId
        ) == handshake->participants.end()) {
        return rejectedSaveLoad(
            "save/load response peer is not a proposal participant"
        );
    }

    const auto existing = std::find_if(
        handshake->responses.begin(),
        handshake->responses.end(),
        [&response](const RuntimeMultiplayerSaveLoadResponse& candidate) {
            return candidate.peerId == response.peerId;
        }
    );
    response.prepared = prepared;
    response.approved = prepared;
    response.observedSnapshotFingerprint = observedFingerprint;
    response.observedFingerprint = observedFingerprint;
    response.fingerprintMatches = observedFingerprint
        == handshake->proposal.descriptor.snapshotFingerprint;
    if (existing != handshake->responses.end()) {
        if (*existing == response) {
            RuntimeMultiplayerSaveLoadResult result;
            result.accepted = true;
            result.duplicate = true;
            result.status = handshake->status;
            return result;
        }
        return rejectedSaveLoad(
            "peer attempted to change its save/load response"
        );
    }
    if (handshake->status
            != RuntimeMultiplayerSaveLoadStatus::Transferring
        && handshake->status
            != RuntimeMultiplayerSaveLoadStatus::Prepared) {
        return rejectedSaveLoad("save/load handshake is already complete");
    }

    handshake->responses.push_back(std::move(response));
    std::sort(
        handshake->responses.begin(),
        handshake->responses.end(),
        responseLess
    );
    const bool evidenceMismatch = std::any_of(
        handshake->responses.begin(),
        handshake->responses.end(),
        [&handshake](const RuntimeMultiplayerSaveLoadResponse& candidate) {
            return candidate.prepared
                && (candidate.observedSnapshotFingerprint
                        != handshake->proposal.descriptor.snapshotFingerprint
                    || candidate.observedAuthoritativeStateHash
                        != handshake->proposal.descriptor.identity
                            .authoritativeStateHash);
        }
    );
    recomputePreparedSaveLoadHandshake(*handshake);
    if (evidenceMismatch) {
        handshake->status = RuntimeMultiplayerSaveLoadStatus::Rejected;
    }

    RuntimeMultiplayerSaveLoadResult result;
    result.accepted = true;
    result.changed = true;
    result.status = handshake->status;
    if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Rejected) {
        result.reason = "save/load handshake was rejected by member consensus";
    }
    return result;
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::issueSaveLoadCommit(
    std::uint64_t timelineEpoch,
    std::uint64_t proposalId,
    const std::string& requestingPeerId
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedSaveLoad("save/load commit requires a running session");
    }
    auto handshake = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [proposalId](const RuntimeMultiplayerSaveLoadHandshake& candidate) {
            return candidate.proposal.proposalId == proposalId;
        }
    );
    if (handshake == saveLoadHandshakes_.end()) {
        return rejectedSaveLoad("save/load proposal does not exist");
    }
    if (requestingPeerId != config_.hostPeerId) {
        return rejectedSaveLoad("only the host may issue save/load commit");
    }
    const bool loadCommitAlreadyChangedEpoch =
        handshake->proposal.kind == RuntimeMultiplayerSaveLoadKind::Load
        && (handshake->status
                == RuntimeMultiplayerSaveLoadStatus::CommitIssued
            || handshake->status
                == RuntimeMultiplayerSaveLoadStatus::Committed)
        && handshake->proposal.timelineEpoch
            < timelineEpoch_;
    if (timelineEpoch == 0
        || (timelineEpoch != timelineEpoch_
            && !(loadCommitAlreadyChangedEpoch
                && timelineEpoch
                    == handshake->proposal.timelineEpoch))) {
        return rejectedSaveLoad("save/load commit timeline epoch is stale");
    }
    if (handshake->status
            == RuntimeMultiplayerSaveLoadStatus::CommitIssued
        || handshake->status
            == RuntimeMultiplayerSaveLoadStatus::Committed) {
        RuntimeMultiplayerSaveLoadResult result;
        result.accepted = true;
        result.duplicate = true;
        result.status = handshake->status;
        return result;
    }
    if (handshake->status != RuntimeMultiplayerSaveLoadStatus::Prepared) {
        return rejectedSaveLoad("save/load handshake is not prepared");
    }

    if (handshake->proposal.kind == RuntimeMultiplayerSaveLoadKind::Load) {
        if (timelineEpoch_ == std::numeric_limits<std::uint64_t>::max()
            || handshake->proposal.descriptor.identity.snapshotTick
                >= runtimeMultiplayerMaximumTick) {
            return rejectedSaveLoad("load commit timeline cannot advance");
        }
        ++timelineEpoch_;
        authoritativeTick_ =
            handshake->proposal.descriptor.identity.snapshotTick + 1;
        commands_.clear();
        commandSequenceWindows_.clear();
        commandSequenceWindows_.reserve(members_.size());
        for (const RuntimeMultiplayerRoomMember& member : members_) {
            PeerCommandSequenceWindow window;
            window.peerId = member.peerId;
            commandSequenceWindows_.push_back(std::move(window));
        }
        stateHashFrames_.clear();
        firstDesyncTick_ = -1;
    }
    handshake->status = RuntimeMultiplayerSaveLoadStatus::CommitIssued;
    RuntimeMultiplayerSaveLoadResult result;
    result.accepted = true;
    result.changed = true;
    result.status = handshake->status;
    return result;
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::acknowledgeSaveLoadApplied(
    RuntimeMultiplayerSaveLoadApplied applied
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedSaveLoad("save/load applied ack requires a running session");
    }
    if (applied.timelineEpoch == 0
        || applied.timelineEpoch != timelineEpoch_) {
        return rejectedSaveLoad("save/load applied timeline epoch is stale");
    }
    if (applied.proposalId == 0
        || !validIdentifier(
            applied.peerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return rejectedSaveLoad("save/load applied identity is invalid");
    }
    auto handshake = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [&applied](const RuntimeMultiplayerSaveLoadHandshake& candidate) {
            return candidate.proposal.proposalId == applied.proposalId;
        }
    );
    if (handshake == saveLoadHandshakes_.end()) {
        return rejectedSaveLoad("save/load proposal does not exist");
    }
    if (std::find(
            handshake->participants.begin(),
            handshake->participants.end(),
            applied.peerId
        ) == handshake->participants.end()) {
        return rejectedSaveLoad(
            "save/load applied peer is not a proposal participant"
        );
    }
    const auto existing = std::find_if(
        handshake->appliedAcks.begin(),
        handshake->appliedAcks.end(),
        [&applied](const RuntimeMultiplayerSaveLoadApplied& candidate) {
            return candidate.peerId == applied.peerId;
        }
    );
    if (existing != handshake->appliedAcks.end()) {
        if (*existing == applied) {
            RuntimeMultiplayerSaveLoadResult result;
            result.accepted = true;
            result.duplicate = true;
            result.status = handshake->status;
            return result;
        }
        return rejectedSaveLoad("peer attempted to change its applied ack");
    }
    if (handshake->status
        != RuntimeMultiplayerSaveLoadStatus::CommitIssued) {
        return rejectedSaveLoad("save/load commit has not been issued");
    }
    if (!applied.applied
        || applied.observedAuthoritativeStateHash
            != handshake->proposal.descriptor.identity
                .authoritativeStateHash) {
        abortState_.requestingPeerId = config_.hostPeerId;
        abortState_.affectedPeerId = applied.peerId;
        abortState_.tick = authoritativeTick_;
        abortState_.reason =
            RuntimeMultiplayerSessionAbortReason::ProtocolViolation;
        status_ = RuntimeMultiplayerRoomStatus::Aborted;
        RuntimeMultiplayerSaveLoadResult result;
        result.accepted = true;
        result.changed = true;
        result.status = handshake->status;
        result.reason = "post-commit apply failure aborted the room";
        return result;
    }
    handshake->appliedAcks.push_back(std::move(applied));
    std::sort(
        handshake->appliedAcks.begin(),
        handshake->appliedAcks.end(),
        appliedLess
    );
    if (handshake->appliedAcks.size()
        == handshake->participants.size()) {
        handshake->status = RuntimeMultiplayerSaveLoadStatus::Committed;
    }
    RuntimeMultiplayerSaveLoadResult result;
    result.accepted = true;
    result.changed = true;
    result.status = handshake->status;
    return result;
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::abortSaveLoad(
    std::uint64_t proposalId,
    const std::string& requestingPeerId
) {
    return abortSaveLoad(timelineEpoch_, proposalId, requestingPeerId);
}

RuntimeMultiplayerSaveLoadResult
RuntimeMultiplayerSessionState::abortSaveLoad(
    std::uint64_t timelineEpoch,
    std::uint64_t proposalId,
    const std::string& requestingPeerId
) {
    if (status_ != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedSaveLoad(
            "save/load abort requires a running multiplayer session"
        );
    }
    if (!validIdentifier(
            requestingPeerId,
            runtimeMultiplayerMaximumPeerIdBytes
        )) {
        return rejectedSaveLoad("save/load abort peer id is invalid");
    }
    if (requestingPeerId != config_.hostPeerId) {
        return rejectedSaveLoad("only the host may abort save/load");
    }
    if (timelineEpoch == 0 || timelineEpoch != timelineEpoch_) {
        return rejectedSaveLoad("save/load abort timeline epoch is stale");
    }
    if (proposalId == 0
        || proposalId
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
            )) {
        return rejectedSaveLoad("save/load proposal id is invalid");
    }
    const auto handshake = std::find_if(
        saveLoadHandshakes_.begin(),
        saveLoadHandshakes_.end(),
        [proposalId](const RuntimeMultiplayerSaveLoadHandshake& candidate) {
            return candidate.proposal.proposalId == proposalId;
        }
    );
    if (handshake == saveLoadHandshakes_.end()) {
        return rejectedSaveLoad("save/load proposal does not exist");
    }
    if (handshake->status == RuntimeMultiplayerSaveLoadStatus::Aborted) {
        RuntimeMultiplayerSaveLoadResult result;
        result.accepted = true;
        result.duplicate = true;
        result.status = RuntimeMultiplayerSaveLoadStatus::Aborted;
        return result;
    }
    if (handshake->status
            != RuntimeMultiplayerSaveLoadStatus::Transferring
        && handshake->status
            != RuntimeMultiplayerSaveLoadStatus::Prepared) {
        return rejectedSaveLoad(
            "save/load can only be aborted before commit is issued"
        );
    }
    handshake->status = RuntimeMultiplayerSaveLoadStatus::Aborted;

    RuntimeMultiplayerSaveLoadResult result;
    result.accepted = true;
    result.changed = true;
    result.status = RuntimeMultiplayerSaveLoadStatus::Aborted;
    return result;
}

std::uint64_t RuntimeMultiplayerSessionState::stateHash() const {
    StableHasher hash;
    hash.addUint64(kStateHashSchema);
    hash.addUint64(static_cast<std::uint8_t>(status_));
    hash.addInt(startTick_);
    hash.addInt(authoritativeTick_);
    hash.addUint64(timelineEpoch_);
    hash.addInt(firstDesyncTick_);

    hash.addString(config_.roomId);
    hash.addString(config_.hostPeerId);
    hash.addString(config_.hostDisplayName);
    hash.addUint64(static_cast<std::uint8_t>(config_.hostParticipation));
    hashCompatibility(hash, config_.compatibility);
    hash.addSize(config_.maximumMembers);
    hash.addSize(config_.maximumCommandPayloadBytes);
    hash.addSize(config_.maximumBufferedCommands);
    hash.addSize(config_.maximumBufferedCommandsPerPeer);
    hash.addSize(config_.maximumStateHashFrames);
    hash.addSize(config_.maximumSaveLoadHandshakes);
    hash.addInt(config_.commandLeadWindowTicks);
    hash.addInt(config_.stateHashLagWindowTicks);
    hash.addInt(config_.stateHashPeriodTicks);

    hash.addUint64(latestMatchSetupRevision_);
    hash.addBool(matchSetup_.has_value());
    if (matchSetup_.has_value()) {
        std::vector<std::uint8_t> setupBytes;
        std::uint64_t setupHash = 0;
        std::string setupError;
        const bool encoded = encodeRuntimeMultiplayerMatchSetup(
            *matchSetup_,
            setupBytes,
            setupError
        );
        const bool hashed = encoded && runtimeMultiplayerMatchSetupHash(
            *matchSetup_,
            setupHash,
            setupError
        );
        hash.addBool(encoded && hashed);
        hash.addUint64(setupHash);
        hash.addBytes(setupBytes);
    }

    hash.addSize(members_.size());
    for (const RuntimeMultiplayerRoomMember& member : members_) {
        hash.addString(member.peerId);
        hash.addString(member.displayName);
        hash.addUint64(static_cast<std::uint8_t>(member.role));
        hash.addUint64(static_cast<std::uint8_t>(member.participation));
        hash.addBool(member.ready);
    }
    hash.addSize(departedSpectatorPeerIds_.size());
    for (const std::string& peerId : departedSpectatorPeerIds_) {
        hash.addString(peerId);
    }

    hash.addSize(commands_.size());
    for (const RuntimeMultiplayerCommandPacket& command : commands_) {
        hash.addUint64(command.timelineEpoch);
        hash.addInt(command.tick);
        hash.addString(command.peerId);
        hash.addUint64(command.sequence);
        hash.addBytes(command.payload);
    }

    hash.addUint64(runtimeMultiplayerCommandSequenceReorderWindow);
    hash.addSize(commandSequenceWindows_.size());
    for (const PeerCommandSequenceWindow& window : commandSequenceWindows_) {
        hash.addString(window.peerId);
        hash.addUint64(window.highestAccepted);
        hash.addSize(window.acceptedSequences.size());
        for (const std::uint64_t sequence : window.acceptedSequences) {
            hash.addUint64(sequence);
        }
    }

    hash.addSize(stateHashFrames_.size());
    for (const RuntimeMultiplayerStateHashFrame& frame : stateHashFrames_) {
        hash.addUint64(frame.timelineEpoch);
        hash.addInt(frame.tick);
        hash.addBool(frame.complete);
        hash.addBool(frame.desync);
        hash.addUint64(frame.hostStateHash);
        hash.addSize(frame.reports.size());
        for (const RuntimeMultiplayerStateHashReport& report : frame.reports) {
            hash.addUint64(report.timelineEpoch);
            hash.addInt(report.tick);
            hash.addString(report.peerId);
            hash.addUint64(report.stateHash);
        }
    }

    hash.addSize(saveLoadHandshakes_.size());
    hash.addUint64(highestSaveLoadProposalId_);
    for (const RuntimeMultiplayerSaveLoadHandshake& handshake :
            saveLoadHandshakes_) {
        hash.addUint64(handshake.proposal.timelineEpoch);
        hash.addUint64(handshake.proposal.proposalId);
        hash.addUint64(static_cast<std::uint8_t>(handshake.proposal.kind));
        hash.addString(handshake.proposal.proposerPeerId);
        hashSnapshotDescriptor(hash, handshake.proposal.descriptor);
        hash.addInt(handshake.proposal.tick);
        hash.addString(handshake.proposal.snapshotId);
        hash.addUint64(handshake.proposal.snapshotFingerprint);
        hash.addUint64(static_cast<std::uint8_t>(handshake.status));
        hash.addSize(handshake.participants.size());
        for (const std::string& participant : handshake.participants) {
            hash.addString(participant);
        }
        hash.addSize(handshake.responses.size());
        for (const RuntimeMultiplayerSaveLoadResponse& response :
                handshake.responses) {
            hash.addUint64(response.timelineEpoch);
            hash.addUint64(response.proposalId);
            hash.addString(response.peerId);
            hash.addBool(response.prepared);
            hash.addUint64(response.observedSnapshotFingerprint);
            hash.addUint64(response.observedAuthoritativeStateHash);
            hash.addBool(response.approved);
            hash.addUint64(response.observedFingerprint);
            hash.addBool(response.fingerprintMatches);
        }
        hash.addSize(handshake.appliedAcks.size());
        for (const RuntimeMultiplayerSaveLoadApplied& applied :
                handshake.appliedAcks) {
            hash.addUint64(applied.timelineEpoch);
            hash.addUint64(applied.proposalId);
            hash.addString(applied.peerId);
            hash.addBool(applied.applied);
            hash.addUint64(applied.observedAuthoritativeStateHash);
        }
    }
    hash.addString(abortState_.requestingPeerId);
    hash.addString(abortState_.affectedPeerId);
    hash.addInt(abortState_.tick);
    hash.addUint64(static_cast<std::uint8_t>(abortState_.reason));
    return hash.value();
}

bool RuntimeMultiplayerSessionState::active() const {
    return status_ != RuntimeMultiplayerRoomStatus::Inactive;
}

RuntimeMultiplayerRoomStatus RuntimeMultiplayerSessionState::status() const {
    return status_;
}

int RuntimeMultiplayerSessionState::startTick() const {
    return startTick_;
}

int RuntimeMultiplayerSessionState::authoritativeTick() const {
    return authoritativeTick_;
}

std::uint64_t RuntimeMultiplayerSessionState::timelineEpoch() const {
    return timelineEpoch_;
}

const RuntimeMultiplayerSessionConfig&
RuntimeMultiplayerSessionState::config() const {
    return config_;
}

const std::vector<RuntimeMultiplayerRoomMember>&
RuntimeMultiplayerSessionState::members() const {
    return members_;
}

const std::vector<std::string>&
RuntimeMultiplayerSessionState::departedSpectatorPeerIds() const {
    return departedSpectatorPeerIds_;
}

std::uint64_t
RuntimeMultiplayerSessionState::latestMatchSetupRevision() const {
    return latestMatchSetupRevision_;
}

const std::optional<RuntimeMultiplayerMatchSetup>&
RuntimeMultiplayerSessionState::matchSetup() const {
    return matchSetup_;
}

const std::vector<RuntimeMultiplayerCommandPacket>&
RuntimeMultiplayerSessionState::commands() const {
    return commands_;
}

const std::vector<RuntimeMultiplayerStateHashFrame>&
RuntimeMultiplayerSessionState::stateHashFrames() const {
    return stateHashFrames_;
}

const std::vector<RuntimeMultiplayerSaveLoadHandshake>&
RuntimeMultiplayerSessionState::saveLoadHandshakes() const {
    return saveLoadHandshakes_;
}

const RuntimeMultiplayerSessionAbort&
RuntimeMultiplayerSessionState::abortState() const {
    return abortState_;
}

bool RuntimeMultiplayerSessionState::hasActiveSaveLoadHandshake() const {
    return hasActiveSaveLoad(saveLoadHandshakes_);
}

bool RuntimeMultiplayerSessionState::allMembersReady() const {
    return !members_.empty()
        && std::all_of(
            members_.begin(),
            members_.end(),
            [](const RuntimeMultiplayerRoomMember& member) {
                return member.ready;
            }
        );
}

bool RuntimeMultiplayerSessionState::desyncDetected() const {
    return firstDesyncTick_ >= 0;
}

int RuntimeMultiplayerSessionState::firstDesyncTick() const {
    return firstDesyncTick_;
}
