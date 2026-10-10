#pragma once

#include "network/protocol/RuntimeMultiplayerMatchSetup.h"
#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Version 9 adds requested, bounded lobby map transfers for missing maps.
// Version 8 added lobby snapshot bootstrap for rooms resumed from a Save.
// Version 7 added authenticated, bounded running-combatant reconnect metadata.
// Recovery is permitted only from an identical deterministic Session
// checkpoint and always completes through the existing coordinated Load
// snapshot path. Version 6 introduced the separate spectator capacity.
inline constexpr std::uint32_t runtimeMultiplayerProtocolVersion = 9;
inline constexpr std::size_t runtimeMultiplayerMaximumCombatants = 8;
inline constexpr std::size_t runtimeMultiplayerMaximumSpectators = 8;
inline constexpr std::size_t runtimeMultiplayerMaximumMembers =
    runtimeMultiplayerMaximumCombatants
        + runtimeMultiplayerMaximumSpectators;
inline constexpr std::size_t runtimeMultiplayerMaximumPeerIdBytes = 64;
inline constexpr std::size_t runtimeMultiplayerMaximumDisplayNameBytes = 96;
inline constexpr std::size_t runtimeMultiplayerMaximumRoomIdBytes = 64;
inline constexpr std::size_t runtimeMultiplayerMaximumCommandPayloadBytes =
    64U * 1024U;
inline constexpr std::size_t runtimeMultiplayerMaximumBufferedCommandBytes =
    128U * 1024U * 1024U;
inline constexpr std::size_t runtimeMultiplayerMaximumBufferedCommands =
    runtimeMultiplayerMaximumBufferedCommandBytes
        / runtimeMultiplayerMaximumCommandPayloadBytes;
inline constexpr std::size_t
    runtimeMultiplayerMaximumBufferedCommandBytesPerPeer =
        8U * 1024U * 1024U;
inline constexpr std::size_t
    runtimeMultiplayerMaximumBufferedCommandsPerPeer =
        runtimeMultiplayerMaximumBufferedCommandBytesPerPeer
            / runtimeMultiplayerMaximumCommandPayloadBytes;
inline constexpr std::size_t runtimeMultiplayerMaximumStateHashFrames = 4096;
inline constexpr std::size_t runtimeMultiplayerMaximumSaveLoadHandshakes = 256;
inline constexpr std::size_t runtimeMultiplayerMaximumSnapshotIdBytes = 256;
inline constexpr int runtimeMultiplayerMaximumTick = 2000000000;
inline constexpr int runtimeMultiplayerMaximumCommandLeadWindowTicks = 600;
inline constexpr int runtimeMultiplayerMaximumStateHashLagWindowTicks = 600;
inline constexpr std::uint64_t
    runtimeMultiplayerCommandSequenceReorderWindow = 65536;

enum class RuntimeMultiplayerRoomStatus : std::uint8_t {
    Inactive,
    Lobby,
    Running,
    Aborted
};

enum class RuntimeMultiplayerRoomMemberRole : std::uint8_t {
    Host,
    Client
};

enum class RuntimeMultiplayerParticipationRole : std::uint8_t {
    Combatant,
    Spectator
};

enum class RuntimeMultiplayerSaveLoadKind : std::uint8_t {
    Save,
    Load
};

enum class RuntimeMultiplayerSaveLoadStatus : std::uint8_t {
    Transferring,
    Prepared,
    CommitIssued,
    Committed,
    Rejected,
    Aborted,
    // Source compatibility name; canonical state uses Transferring.
    Pending = Transferring
};

struct RuntimeMultiplayerCompatibility {
    std::uint32_t protocolVersion = runtimeMultiplayerProtocolVersion;
    std::uint64_t protocolFingerprint = 0;
    std::uint64_t contentFingerprint = 0;

    bool operator==(const RuntimeMultiplayerCompatibility&) const = default;
};

// These limits are protocol state, not local allocation hints. Every peer must
// use the same values (normally guaranteed by protocolFingerprint), and they
// are included in stateHash().
struct RuntimeMultiplayerSessionConfig {
    std::string roomId;
    std::string hostPeerId;
    std::string hostDisplayName;
    RuntimeMultiplayerParticipationRole hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    RuntimeMultiplayerCompatibility compatibility;
    std::size_t maximumMembers = runtimeMultiplayerMaximumMembers;
    std::size_t maximumCommandPayloadBytes =
        runtimeMultiplayerMaximumCommandPayloadBytes;
    std::size_t maximumBufferedCommands =
        runtimeMultiplayerMaximumBufferedCommands;
    std::size_t maximumBufferedCommandsPerPeer =
        runtimeMultiplayerMaximumBufferedCommandsPerPeer;
    std::size_t maximumStateHashFrames =
        runtimeMultiplayerMaximumStateHashFrames;
    std::size_t maximumSaveLoadHandshakes =
        runtimeMultiplayerMaximumSaveLoadHandshakes;
    int commandLeadWindowTicks = 120;
    int stateHashLagWindowTicks = 120;
    int stateHashPeriodTicks = 30;

    bool operator==(const RuntimeMultiplayerSessionConfig&) const = default;
};

struct RuntimeMultiplayerRoomMember {
    std::string peerId;
    std::string displayName;
    RuntimeMultiplayerRoomMemberRole role =
        RuntimeMultiplayerRoomMemberRole::Client;
    RuntimeMultiplayerParticipationRole participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    bool ready = false;

    bool operator==(const RuntimeMultiplayerRoomMember&) const = default;
};

// Complete authoritative lobby state used when a newly admitted LAN peer
// joins an already populated room. It is deliberately limited to Lobby: a
// running match must be recovered from a gameplay snapshot rather than by
// replacing its deterministic command history.
struct RuntimeMultiplayerLobbySnapshot {
    RuntimeMultiplayerSessionConfig config;
    std::vector<RuntimeMultiplayerRoomMember> members;
    // Preserved even while roster changes have invalidated the concrete
    // setup, so a replayed or rolled-back revision cannot become current.
    std::uint64_t latestMatchSetupRevision = 0;
    std::optional<RuntimeMultiplayerMatchSetup> matchSetup;
    // Lobby snapshots are never a running-world recovery mechanism and must
    // therefore remain on the initial epoch.
    std::uint64_t timelineEpoch = 1;

    bool operator==(const RuntimeMultiplayerLobbySnapshot&) const = default;
};

enum class RuntimeMultiplayerSessionAbortReason : std::uint8_t {
    PeerDisconnected,
    HostDisconnected,
    HostClosed,
    TransportFailure,
    ProtocolViolation
};

struct RuntimeMultiplayerSessionAbort {
    std::string requestingPeerId;
    std::string affectedPeerId;
    int tick = -1;
    RuntimeMultiplayerSessionAbortReason reason =
        RuntimeMultiplayerSessionAbortReason::PeerDisconnected;

    bool operator==(const RuntimeMultiplayerSessionAbort&) const = default;
};

struct RuntimeMultiplayerJoinRequest {
    std::string peerId;
    std::string displayName;
    RuntimeMultiplayerParticipationRole participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    RuntimeMultiplayerCompatibility compatibility;

    bool operator==(const RuntimeMultiplayerJoinRequest&) const = default;
};

struct RuntimeMultiplayerOperationResult {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    std::string reason;
};

struct RuntimeMultiplayerCompatibilityResult {
    bool compatible = false;
    std::string reason;
};

struct RuntimeMultiplayerCommandPacket {
    int tick = -1;
    std::string peerId;
    // Start at a small positive value. Delivery may reorder within the most
    // recent runtimeMultiplayerCommandSequenceReorderWindow values; larger
    // initial values or jumps are rejected so one packet cannot evict the
    // bounded anti-replay history.
    std::uint64_t sequence = 0;
    std::vector<std::uint8_t> payload;
    std::uint64_t timelineEpoch = 1;

    bool operator==(const RuntimeMultiplayerCommandPacket&) const = default;
};

struct RuntimeMultiplayerStateHashReport {
    int tick = -1;
    std::string peerId;
    std::uint64_t stateHash = 0;
    std::uint64_t timelineEpoch = 1;

    bool operator==(const RuntimeMultiplayerStateHashReport&) const = default;
};

struct RuntimeMultiplayerStateHashFrame {
    int tick = -1;
    std::uint64_t timelineEpoch = 1;
    bool complete = false;
    bool desync = false;
    std::uint64_t hostStateHash = 0;
    std::vector<RuntimeMultiplayerStateHashReport> reports;

    bool operator==(const RuntimeMultiplayerStateHashFrame&) const = default;
};

struct RuntimeMultiplayerStateHashResult {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    bool frameComplete = false;
    bool desync = false;
    std::string reason;
};

struct RuntimeMultiplayerSaveLoadProposal {
    std::uint64_t timelineEpoch = 1;
    std::uint64_t proposalId = 0;
    RuntimeMultiplayerSaveLoadKind kind =
        RuntimeMultiplayerSaveLoadKind::Save;
    std::string proposerPeerId;
    RuntimeMultiplayerSnapshotDescriptor descriptor;

    // Deprecated source-compatibility mirrors. Canonical admission derives
    // these from descriptor and rejects conflicting non-default values.
    int tick = -1;
    // An opaque, portable identity such as a save slot plus generation. It is
    // deliberately not a filesystem path.
    std::string snapshotId;
    std::uint64_t snapshotFingerprint = 0;

    bool operator==(const RuntimeMultiplayerSaveLoadProposal&) const = default;
};

struct RuntimeMultiplayerSaveLoadResponse {
    std::uint64_t timelineEpoch = 1;
    std::uint64_t proposalId = 0;
    std::string peerId;
    bool prepared = false;
    std::uint64_t observedSnapshotFingerprint = 0;
    std::uint64_t observedAuthoritativeStateHash = 0;

    // Deprecated source-compatible views of prepared/fingerprint.
    bool approved = false;
    std::uint64_t observedFingerprint = 0;
    bool fingerprintMatches = false;

    bool operator==(const RuntimeMultiplayerSaveLoadResponse&) const = default;
};

struct RuntimeMultiplayerSaveLoadApplied {
    std::uint64_t timelineEpoch = 1;
    std::uint64_t proposalId = 0;
    std::string peerId;
    bool applied = true;
    std::uint64_t observedAuthoritativeStateHash = 0;

    bool operator==(const RuntimeMultiplayerSaveLoadApplied&) const = default;
};

struct RuntimeMultiplayerSaveLoadHandshake {
    RuntimeMultiplayerSaveLoadProposal proposal;
    RuntimeMultiplayerSaveLoadStatus status =
        RuntimeMultiplayerSaveLoadStatus::Transferring;
    std::vector<std::string> participants;
    std::vector<RuntimeMultiplayerSaveLoadResponse> responses;
    std::vector<RuntimeMultiplayerSaveLoadApplied> appliedAcks;

    bool operator==(const RuntimeMultiplayerSaveLoadHandshake&) const = default;
};

struct RuntimeMultiplayerSaveLoadResult {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    RuntimeMultiplayerSaveLoadStatus status =
        RuntimeMultiplayerSaveLoadStatus::Transferring;
    std::string reason;
};

// Value-only authoritative room/session core. It owns no sockets, threads,
// clocks, files, or platform handles. A transport serializes validated input
// events to every peer; applying the same events yields the same stateHash().
class RuntimeMultiplayerSessionState {
public:
    RuntimeMultiplayerSessionState() = default;

    void reset();

    RuntimeMultiplayerOperationResult beginHost(
        RuntimeMultiplayerSessionConfig config
    );
    RuntimeMultiplayerOperationResult replaceLobbySnapshot(
        RuntimeMultiplayerLobbySnapshot snapshot
    );

    RuntimeMultiplayerCompatibilityResult validateCompatibility(
        const RuntimeMultiplayerCompatibility& compatibility
    ) const;

    RuntimeMultiplayerOperationResult joinMember(
        RuntimeMultiplayerJoinRequest request
    );
    RuntimeMultiplayerOperationResult leaveMember(
        const std::string& peerId
    );
    RuntimeMultiplayerOperationResult setMemberReady(
        const std::string& peerId,
        bool ready
    );
    RuntimeMultiplayerOperationResult setMatchSetup(
        const std::string& requestingPeerId,
        RuntimeMultiplayerMatchSetup setup
    );
    RuntimeMultiplayerOperationResult start(
        const std::string& requestingPeerId,
        int startTick
    );
    RuntimeMultiplayerOperationResult advanceAuthoritativeTick(
        const std::string& requestingPeerId,
        int authoritativeTick
    );
    RuntimeMultiplayerOperationResult advanceAuthoritativeTick(
        const std::string& requestingPeerId,
        int authoritativeTick,
        std::uint64_t timelineEpoch
    );
    RuntimeMultiplayerOperationResult abortRunningSession(
        RuntimeMultiplayerSessionAbort abort
    );

    RuntimeMultiplayerOperationResult submitCommand(
        RuntimeMultiplayerCommandPacket packet
    );
    std::vector<RuntimeMultiplayerCommandPacket> commandsAtTick(
        int tick
    ) const;

    RuntimeMultiplayerStateHashResult submitStateHash(
        RuntimeMultiplayerStateHashReport report
    );

    RuntimeMultiplayerSaveLoadResult proposeSaveLoad(
        RuntimeMultiplayerSaveLoadProposal proposal
    );
    RuntimeMultiplayerSaveLoadResult respondToSaveLoad(
        std::uint64_t proposalId,
        std::string peerId,
        bool approve,
        std::uint64_t observedFingerprint
    );
    RuntimeMultiplayerSaveLoadResult prepareSaveLoad(
        RuntimeMultiplayerSaveLoadResponse response
    );
    RuntimeMultiplayerSaveLoadResult issueSaveLoadCommit(
        std::uint64_t timelineEpoch,
        std::uint64_t proposalId,
        const std::string& requestingPeerId
    );
    RuntimeMultiplayerSaveLoadResult acknowledgeSaveLoadApplied(
        RuntimeMultiplayerSaveLoadApplied applied
    );
    RuntimeMultiplayerSaveLoadResult abortSaveLoad(
        std::uint64_t proposalId,
        const std::string& requestingPeerId
    );
    RuntimeMultiplayerSaveLoadResult abortSaveLoad(
        std::uint64_t timelineEpoch,
        std::uint64_t proposalId,
        const std::string& requestingPeerId
    );

    std::uint64_t stateHash() const;

    bool active() const;
    RuntimeMultiplayerRoomStatus status() const;
    int startTick() const;
    int authoritativeTick() const;
    std::uint64_t timelineEpoch() const;
    const RuntimeMultiplayerSessionConfig& config() const;
    const std::vector<RuntimeMultiplayerRoomMember>& members() const;
    const std::vector<std::string>& departedSpectatorPeerIds() const;
    std::uint64_t latestMatchSetupRevision() const;
    const std::optional<RuntimeMultiplayerMatchSetup>& matchSetup() const;
    const std::vector<RuntimeMultiplayerCommandPacket>& commands() const;
    const std::vector<RuntimeMultiplayerStateHashFrame>& stateHashFrames() const;
    const std::vector<RuntimeMultiplayerSaveLoadHandshake>&
        saveLoadHandshakes() const;
    const RuntimeMultiplayerSessionAbort& abortState() const;
    bool hasActiveSaveLoadHandshake() const;
    bool allMembersReady() const;
    bool desyncDetected() const;
    int firstDesyncTick() const;

private:
    struct PeerCommandSequenceWindow {
        std::string peerId;
        std::uint64_t highestAccepted = 0;
        std::vector<std::uint64_t> acceptedSequences;
    };

    RuntimeMultiplayerSessionConfig config_;
    RuntimeMultiplayerRoomStatus status_ =
        RuntimeMultiplayerRoomStatus::Inactive;
    int startTick_ = -1;
    int authoritativeTick_ = -1;
    std::uint64_t timelineEpoch_ = 0;
    int firstDesyncTick_ = -1;
    std::vector<RuntimeMultiplayerRoomMember> members_;
    // Canonical anti-replay tombstones for spectators that left after Start.
    // They make a repeated host echo idempotent without allowing an unknown
    // peer to manufacture a running-room departure.
    std::vector<std::string> departedSpectatorPeerIds_;
    std::uint64_t latestMatchSetupRevision_ = 0;
    std::optional<RuntimeMultiplayerMatchSetup> matchSetup_;
    std::vector<RuntimeMultiplayerCommandPacket> commands_;
    std::vector<PeerCommandSequenceWindow> commandSequenceWindows_;
    std::vector<RuntimeMultiplayerStateHashFrame> stateHashFrames_;
    std::vector<RuntimeMultiplayerSaveLoadHandshake> saveLoadHandshakes_;
    std::uint64_t highestSaveLoadProposalId_ = 0;
    RuntimeMultiplayerSessionAbort abortState_;
};
