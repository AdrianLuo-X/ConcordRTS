#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "network/session/RuntimeMultiplayerSessionSystem.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// A GameplayFrame is the one payload a combatant contributes for one
// RuntimeMultiplayerCommandPacket tick. Tick, peer id, and sequence stay in the
// already authenticated session envelope and are deliberately not duplicated.
inline constexpr std::uint32_t runtimeMultiplayerGameplayFrameMagic =
    0x43474c46U; // "CGLF"
inline constexpr std::uint16_t runtimeMultiplayerGameplayFrameVersion = 1;
inline constexpr std::size_t runtimeMultiplayerGameplayFrameHeaderBytes = 8;
inline constexpr std::size_t
    runtimeMultiplayerLockstepMaximumCommandsPerFrame = 64;
inline constexpr std::size_t runtimeMultiplayerLockstepMaximumFrameBytes =
    runtimeMultiplayerMaximumCommandPayloadBytes;
inline constexpr int runtimeMultiplayerLockstepMaximumInputDelayTicks =
    runtimeMultiplayerMaximumCommandLeadWindowTicks;

struct RuntimeMultiplayerGameplayFrame {
    // Command order is authored gameplay input and is therefore preserved.
    // Canonicalization applies to each command, never to this vector's order.
    std::vector<RuntimeMultiplayerGameplayCommand> commands;

    bool operator==(const RuntimeMultiplayerGameplayFrame&) const = default;
};

// Limits are deterministic protocol state and must be covered by the room's
// protocol fingerprint. Barriers begin at session.startTick(). Ticks before
// startTick + inputDelayTicks are deterministic empty-input warm-up barriers;
// every one is applied and advanced normally, so the simulation never jumps.
struct RuntimeMultiplayerLockstepConfig {
    int inputDelayTicks = 2;
    std::size_t maximumCommandsPerFrame =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame;
    std::size_t maximumFrameBytes =
        runtimeMultiplayerLockstepMaximumFrameBytes;

    bool operator==(const RuntimeMultiplayerLockstepConfig&) const = default;
};

bool canonicalizeRuntimeMultiplayerGameplayFrame(
    RuntimeMultiplayerGameplayFrame& frame,
    std::string& errorMessage,
    std::size_t maximumCommands =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame
);

// Frame integers and command byte lengths are fixed-width big-endian. Encode
// canonicalizes a copy. Decode requires canonical nested command payloads.
// Both functions leave their output argument unchanged on failure.
bool encodeRuntimeMultiplayerGameplayFrame(
    const RuntimeMultiplayerGameplayFrame& frame,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage,
    std::size_t maximumCommands =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame,
    std::size_t maximumBytes =
        runtimeMultiplayerLockstepMaximumFrameBytes
);

bool decodeRuntimeMultiplayerGameplayFrame(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerGameplayFrame& frame,
    std::string& errorMessage,
    std::size_t maximumCommands =
        runtimeMultiplayerLockstepMaximumCommandsPerFrame,
    std::size_t maximumBytes =
        runtimeMultiplayerLockstepMaximumFrameBytes
);

struct RuntimeMultiplayerLockstepApplyCommand {
    int tick = -1;
    std::string peerId;
    std::uint64_t frameSequence = 0;
    std::size_t commandIndex = 0;
    RuntimeMultiplayerGameplayCommand command;

    bool operator==(
        const RuntimeMultiplayerLockstepApplyCommand&
    ) const = default;
};

// Callbacks receive one canonical batch ordered by peerId, then by authored
// command order inside that peer's frame. The apply callback should commit the
// whole batch transactionally; a false return never completes the lockstep
// tick and permits a safe retry.
using RuntimeMultiplayerLockstepBatchCallback = std::function<bool(
    std::span<const RuntimeMultiplayerLockstepApplyCommand>,
    std::string&
)>;

class RuntimeMultiplayerLockstepDriver {
public:
    RuntimeMultiplayerLockstepDriver() = default;

    void reset();

    // Bind immediately after a room enters Running and before any gameplay
    // command envelope has been admitted to the session.
    RuntimeMultiplayerOperationResult bind(
        RuntimeMultiplayerSessionState& session,
        RuntimeMultiplayerLockstepConfig config = {}
    );

    // Read-only Session binding for RuntimeMultiplayerLanSession. It supports
    // observeAdmittedFrame and replicated-advance acknowledgement; direct
    // submitFrame/advanceHost remain available only on the mutable overload.
    RuntimeMultiplayerOperationResult bind(
        const RuntimeMultiplayerSessionState& session,
        RuntimeMultiplayerLockstepConfig config = {}
    );

    // Rebind after a committed Load has advanced the timeline epoch and reset
    // Session command/sequence state. The first barrier is the loaded
    // Session's authoritative tick (snapshotTick + 1); its input-delay prefix
    // is deterministic empty-input warm-up just like the initial bind.
    RuntimeMultiplayerOperationResult rebindAfterLoadCommit(
        RuntimeMultiplayerSessionState& session,
        RuntimeMultiplayerLockstepConfig config = {}
    );
    RuntimeMultiplayerOperationResult rebindAfterLoadCommit(
        const RuntimeMultiplayerSessionState& session,
        RuntimeMultiplayerLockstepConfig config = {}
    );

    // Validates and decodes before forwarding the unchanged envelope to
    // RuntimeMultiplayerSessionState::submitCommand. Thus malformed/conflicting
    // frames do not consume session sequence state.
    RuntimeMultiplayerOperationResult submitFrame(
        const RuntimeMultiplayerCommandPacket& packet
    );

    // LAN canonical-command path: Session has already admitted the envelope.
    // The driver observes it only when Session still contains the exact packet;
    // validation, conflict, and sequence failures leave driver state unchanged.
    RuntimeMultiplayerOperationResult observeAdmittedFrame(
        const RuntimeMultiplayerCommandPacket& packet
    );

    bool frameBarrierReady(int tick) const;

    // Before authoritative tick T may advance, every combatant must already
    // have contributed its frame for T + inputDelayTicks. This keeps the fixed
    // input pipeline fillable after the admission window moves to T + 1.
    bool inputHorizonReady() const;

    // Transactional output: applyCommands is unchanged when the barrier is not
    // ready, the tick is not current, or the driver/session binding is stale.
    bool canonicalApplyList(
        int tick,
        std::vector<RuntimeMultiplayerLockstepApplyCommand>& applyCommands,
        std::string& errorMessage
    ) const;

    RuntimeMultiplayerOperationResult applyReadyTick(
        int tick,
        const RuntimeMultiplayerLockstepBatchCallback& prevalidate,
        const RuntimeMultiplayerLockstepBatchCallback& apply
    );

    // Advancement is separate from application so a transport may broadcast
    // completion first. It is admitted only after the current tick was applied.
    RuntimeMultiplayerOperationResult advanceHost(
        const std::string& requestingPeerId,
        int completedTick
    );

    // Client path: the transport must check inputHorizonReady() before it
    // applies canonical TickAdvance to Session, then call this after Session
    // has advanced to completedTick + 1. The acknowledgement rechecks the
    // horizon and rejects an incorrectly ordered replicated advance.
    RuntimeMultiplayerOperationResult acknowledgeAuthoritativeAdvance(
        int completedTick
    );

    bool bound() const;
    std::uint64_t timelineEpoch() const;
    int currentBarrierTick() const;
    int firstNetworkInputTick() const;
    bool tickCompleted(int tick) const;
    const RuntimeMultiplayerLockstepConfig& config() const;
    std::uint64_t stateHash() const;

private:
    struct Participant {
        std::string peerId;
        RuntimeMultiplayerParticipationRole participation =
            RuntimeMultiplayerParticipationRole::Combatant;
        std::uint64_t lastSequence = 0;
    };

    struct StoredFrame {
        RuntimeMultiplayerCommandPacket packet;
        RuntimeMultiplayerGameplayFrame frame;
    };

    Participant* findParticipant(const std::string& peerId);
    const Participant* findParticipant(const std::string& peerId) const;
    StoredFrame* findFrame(int tick, const std::string& peerId);
    const StoredFrame* findFrame(
        int tick,
        const std::string& peerId
    ) const;
    RuntimeMultiplayerOperationResult admitFrame(
        const RuntimeMultiplayerCommandPacket& packet,
        bool observeAlreadyAdmitted
    );
    RuntimeMultiplayerOperationResult bindSession(
        const RuntimeMultiplayerSessionState& session,
        RuntimeMultiplayerSessionState* mutableSession,
        RuntimeMultiplayerLockstepConfig config,
        bool afterLoadCommit
    );
    void commitAuthoritativeAdvance(int completedTick);
    bool hasCompleteInputHorizon() const;
    bool membershipBindingStillMatches() const;
    bool bindingStillMatches() const;

    const RuntimeMultiplayerSessionState* session_ = nullptr;
    RuntimeMultiplayerSessionState* mutableSession_ = nullptr;
    RuntimeMultiplayerLockstepConfig config_;
    std::vector<Participant> participants_;
    std::vector<StoredFrame> frames_;
    std::uint64_t boundTimelineEpoch_ = 0;
    int bindingStartTick_ = -1;
    int firstNetworkInputTick_ = -1;
    int currentBarrierTick_ = -1;
    int expectedAuthoritativeTick_ = -1;
    bool currentBarrierApplied_ = false;
};
