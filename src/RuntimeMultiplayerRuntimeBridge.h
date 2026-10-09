#pragma once

#include "RuntimeMultiplayerLanSession.h"
#include "RuntimeMultiplayerLockstep.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// One process-level handoff channel lets the outer shell show a concrete
// multiplayer failure after the runtime window has already been destroyed.
// Expected API rejections on bridges that were never handed off are omitted.
std::optional<std::string> consumeRuntimeMultiplayerVisibleFailure();
void clearRuntimeMultiplayerVisibleFailureForMainMenu();
std::string formatRuntimeMultiplayerVisibleFailureMessage(
    std::string_view reason
);

// Loading starts after the authoritative Start barrier and may legitimately
// take much longer than an ordinary transport request. It must still be
// bounded: a connected peer that keeps its lease alive but never attaches a
// world would otherwise hold a sealed room forever.
inline constexpr std::uint32_t
    runtimeMultiplayerInitialLoadTimeoutMilliseconds = 180000U;
inline constexpr std::uint32_t
    runtimeMultiplayerReconnectRecoveryTimeoutMilliseconds = 20000U;

// Runtime-facing callbacks deliberately keep RuntimeWindowState out of the
// network layer.  RuntimeShell installs one complete set after the map has
// materialized; the LAN session and lobby may already exist at that point.
// Batch application must be transactional after prevalidation.  Simulation
// advancement is the infallible fixed-tick commit that follows the batch.
struct RuntimeMultiplayerRuntimeCallbacks {
    RuntimeMultiplayerLockstepBatchCallback prevalidateCommands;
    RuntimeMultiplayerLockstepBatchCallback applyCommands;
    std::function<int()> currentCompletedTick;
    std::function<void(int completedTick)> advanceSimulation;
    std::function<std::uint64_t()> authoritativeStateHash;
    std::function<bool()> matchTerminal;

    // Host-only reconnect checkpoint capture. The bridge supplies an exact
    // Load identity at the paused authoritative barrier; the callback exports
    // the same runtime snapshot format used by ordinary multiplayer save/load.
    std::function<bool(
        const RuntimeMultiplayerSnapshotIdentity&,
        RuntimeMultiplayerSnapshotDescriptor&,
        std::vector<std::uint8_t>&,
        std::string&
    )> captureReconnectSnapshot;

    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        std::span<const std::uint8_t>,
        std::uint64_t& observedAuthoritativeStateHash,
        std::string&
    )> stageVerifiedSnapshot;
    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        std::uint64_t& observedAuthoritativeStateHash,
        std::string&
    )> commitStagedSnapshot;
    std::function<void(const RuntimeMultiplayerSnapshotDescriptor&)>
        discardStagedSnapshot;
    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState&,
        std::string&
    )> onLoadTimelineRebound;

    bool complete() const;
};

struct RuntimeMultiplayerRuntimePumpResult {
    bool succeeded = true;
    bool simulationDeferred = false;
    bool tickCommitted = false;
    int completedTick = -1;
    std::vector<RuntimeMultiplayerLanEvent> events;
    std::string error;
};

enum class RuntimeMultiplayerOrderedTransportPumpStage : std::uint8_t {
    BeforeLanPoll,
    AfterLanPoll
};

// Optional owner/pump for an ordered transport that carries the private LAN
// session (for example an authenticated Internet TCP relay). The bridge owns
// this object for the complete running match. Plain LAN bridges never install
// one and retain their existing socket path.
class RuntimeMultiplayerExternalOrderedTransport {
public:
    virtual ~RuntimeMultiplayerExternalOrderedTransport() = default;

    // Called on both sides of every LAN poll. Implementations must preserve
    // stream order and report any service, tunnel, or proxy loss as failure.
    virtual bool pump(
        RuntimeMultiplayerOrderedTransportPumpStage stage,
        int transportTimeoutMilliseconds,
        std::string& errorMessage
    ) = 0;

    virtual bool supportsReconnectRecovery() const noexcept { return false; }
    virtual std::uint32_t reconnectRecoveryTimeoutMilliseconds() const noexcept {
        return runtimeMultiplayerReconnectRecoveryTimeoutMilliseconds;
    }

    // Optional, non-deterministic service-side completion for ranked Internet
    // matches. Plain LAN and ordinary Internet rooms leave submitted false.
    // The runtime bridge invokes this at most once after the canonical world
    // has reached its terminal result.
    virtual bool reportRankedMatchResult(
        bool localVictory,
        bool& submitted,
        std::string& errorMessage
    ) {
        (void)localVictory;
        submitted = false;
        errorMessage.clear();
        return true;
    }
    virtual bool reportRankedMatchAbandon(bool& submitted, std::string& errorMessage) {
        submitted = false;
        errorMessage.clear();
        return true;
    }
    virtual void close() noexcept = 0;
};

// Owns the production LAN session so its proxy hooks can be installed before
// hosting/joining.  It is created by the lobby, moved into RuntimeShell, and
// then attached to one live authoritative world.  Socket/session state never
// becomes part of the world hash or snapshot.
class RuntimeMultiplayerRuntimeBridge {
public:
    explicit RuntimeMultiplayerRuntimeBridge(
        RuntimeMultiplayerLockstepConfig config = {}
    );
    ~RuntimeMultiplayerRuntimeBridge();

    RuntimeMultiplayerRuntimeBridge(
        RuntimeMultiplayerRuntimeBridge&&
    ) = delete;
    RuntimeMultiplayerRuntimeBridge& operator=(
        RuntimeMultiplayerRuntimeBridge&&
    ) = delete;
    RuntimeMultiplayerRuntimeBridge(
        const RuntimeMultiplayerRuntimeBridge&
    ) = delete;
    RuntimeMultiplayerRuntimeBridge& operator=(
        const RuntimeMultiplayerRuntimeBridge&
    ) = delete;

    // Must succeed before startHost/startBrowsing/join is used.  Constructor
    // performs this installation and records any failure for ready().
    bool ready(std::string& errorMessage) const;

    // Transfers the lifetime of a carrier for the already-owned LAN session.
    // Installation is one-shot and does not alter deterministic session state.
    bool adoptExternalOrderedTransport(
        std::unique_ptr<RuntimeMultiplayerExternalOrderedTransport> transport,
        std::string& errorMessage
    );
    bool hasExternalOrderedTransport() const;

    RuntimeMultiplayerLanSession& lan();
    const RuntimeMultiplayerLanSession& lan() const;

    // Side-effect-free read views for the currently executing external
    // ordered-transport pump only. Unlike lan(), these never stop/join the
    // pre-attach worker; callers must already own the bridge pump callback.
    const RuntimeMultiplayerSessionState&
        orderedTransportPumpSessionView() const noexcept;
    std::uint16_t orderedTransportPumpSessionPort() const noexcept;
    bool orderedTransportPumpReplaceReconnectEndpoint(
        std::uint16_t loopbackPort, std::vector<std::uint8_t> prelude, std::string& errorMessage);
    bool orderedTransportPumpReconnectPending() const;

    // Attaching is transactional and only allowed once the session is Running.
    // A spectator attaches the same full deterministic world but cannot capture
    // input frames.  Detach is used only while tearing the match down.
    bool attachRuntime(
        RuntimeMultiplayerRuntimeCallbacks callbacks,
        std::string& errorMessage
    );
    void detachRuntime();

    // Once a Running bridge leaves the lobby, keep its coordinator lease and
    // transport alive while the runtime synchronously loads resources. The
    // worker never advances simulation and is joined before runtime attach.
    bool beginPreAttachMaintenance(
        std::string& errorMessage,
        std::uint32_t initialLoadTimeoutMilliseconds =
            runtimeMultiplayerInitialLoadTimeoutMilliseconds
    );
    bool preAttachMaintenanceRunning() const;
    std::uint64_t preAttachMaintenancePumpCount() const;

    bool runtimeAttached() const;
    bool lockstepBound() const;
    bool localPeerIsHost() const;
    bool localPeerIsSpectator() const;
    bool mayCaptureGameplay() const;

    // Captures one local authored action without mutating authoritative state.
    // The action is canonicalized immediately and is assigned to the next
    // unsent local frame when pump() runs.
    bool captureGameplayCommand(
        RuntimeMultiplayerGameplayCommand command,
        std::string& errorMessage
    );
    void cancelPendingGameplayCommands();
    std::size_t pendingGameplayCommandCount() const;

    // Drains transport and keeps exactly one empty-or-authored frame supplied
    // inputDelayTicks ahead.  Only the host honors requestHostAdvance; clients
    // and spectators advance exclusively from the canonical host message.
    RuntimeMultiplayerRuntimePumpResult pump(
        bool requestHostAdvance,
        int transportTimeoutMilliseconds = 0
    );

    // Reports the local perspective only to an adopted ranked Internet
    // carrier. This side effect is deliberately outside authoritative state,
    // is one-shot, and is a no-op for LAN and custom-room carriers.
    bool reportRankedMatchResult(
        bool localVictory,
        bool& submitted,
        std::string& errorMessage
    );
    // Called only by explicit resignation/window close, never by transport failure.
    bool reportRankedMatchAbandon(bool& submitted, std::string& errorMessage);

    RuntimeMultiplayerLanOperationResult proposeSaveLoad(
        RuntimeMultiplayerSaveLoadProposal proposal,
        std::vector<std::uint8_t> bytes
    );

    const RuntimeMultiplayerLockstepDriver& lockstep() const;
    int lastCommittedTick() const;
    bool initialStateHashVerified() const;
    bool fatal() const;
    const std::string& fatalReason() const;

private:
    struct PendingStateHash {
        int tick = -1;
        std::uint64_t hash = 0;
    };

    bool installLanHooks(std::string& errorMessage);
    bool bindLockstepAfterStart(
        const RuntimeMultiplayerSessionState& session,
        std::string& errorMessage
    );
    bool flushLocalFrame(std::string& errorMessage);
    bool submitPendingStateHashes(std::string& errorMessage);
    bool refreshTerminalState(std::string& errorMessage);
    bool queueInitialStateHash(
        const RuntimeMultiplayerSessionState& session,
        std::string& errorMessage
    );
    bool refreshInitialStateHashBarrier(
        bool& ready,
        std::string& errorMessage
    );
    bool startReconnectRecovery(std::string& errorMessage);
    bool refreshReconnectRecovery(std::string& errorMessage);
    std::string initialLoadTimeoutAffectedPeerId() const;
    void noteFatal(std::string reason);
    bool refreshLocalParticipation(std::string& errorMessage);
    bool pumpExternalOrderedTransport(
        RuntimeMultiplayerOrderedTransportPumpStage stage,
        int transportTimeoutMilliseconds,
        std::string& errorMessage
    );
    bool stopPreAttachMaintenance(std::string& errorMessage);
    bool failRuntimeAttach(
        std::string& errorMessage,
        std::string reason
    );
    void closeExternalOrderedTransport() noexcept;

    RuntimeMultiplayerLanSession lan_;
    RuntimeMultiplayerLockstepDriver lockstep_;
    RuntimeMultiplayerLockstepConfig config_;
    RuntimeMultiplayerRuntimeCallbacks callbacks_;
    bool callbacksAttached_ = false;
    bool hooksReady_ = false;
    bool localHost_ = false;
    bool localSpectator_ = false;
    std::uint64_t nextFrameSequence_ = 1;
    int nextFrameTick_ = -1;
    std::vector<RuntimeMultiplayerGameplayCommand> pendingCommands_;
    std::deque<PendingStateHash> pendingStateHashes_;
    int lastHashSubmittedTick_ = -1;
    int lastCommittedTick_ = -1;
    int initialStateHashTick_ = -1;
    bool initialStateHashSubmitted_ = false;
    bool initialStateHashVerified_ = false;
    bool matchTerminal_ = false;
    bool rankedMatchResultReportAttempted_ = false;
    bool tickCommittedDuringPump_ = false;
    std::uint64_t reconnectRecoveryProposalId_ = 0;
    std::chrono::steady_clock::time_point reconnectRecoveryStartedAt_{};
    std::atomic<bool> preAttachStopRequested_{false};
    std::atomic<bool> preAttachMaintenanceRunning_{false};
    std::atomic<std::uint64_t> preAttachMaintenancePumpCount_{0U};
    std::thread preAttachMaintenanceThread_;
    std::string preAttachMaintenanceError_;
    bool preAttachMaintenanceEverStarted_ = false;
    bool runtimeEverAttached_ = false;
    std::chrono::steady_clock::time_point initialLoadStartedAt_{};
    std::chrono::milliseconds initialLoadTimeout_{0};
    bool initialLoadDeadlineArmed_ = false;
    std::optional<std::string> initialLoadAbortDrainReason_;
    std::chrono::steady_clock::time_point initialLoadAbortDrainStartedAt_{};
    std::uint32_t initialLoadAbortDrainPumpCount_ = 0U;
    std::string fatalReason_;
    std::string initializationError_;
    std::unique_ptr<RuntimeMultiplayerExternalOrderedTransport>
        externalOrderedTransport_;
};
