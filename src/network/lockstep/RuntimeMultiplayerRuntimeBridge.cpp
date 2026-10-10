#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iterator>
#include <limits>
#include <mutex>
#include <utility>

namespace {

constexpr auto initialLoadAbortMinimumDrainTime =
    std::chrono::milliseconds(250);
constexpr std::uint32_t initialLoadAbortMinimumDrainPumps = 2U;

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

RuntimeMultiplayerLanOperationResult failedOperation(std::string reason) {
    RuntimeMultiplayerLanOperationResult result;
    result.reason = std::move(reason);
    return result;
}

bool operationWasQueuedOrApplied(
    const RuntimeMultiplayerLanOperationResult& result
) {
    return result.succeeded
        && (result.applied || result.queued || result.duplicate);
}

std::mutex runtimeMultiplayerVisibleFailureMutex;
std::optional<std::string> runtimeMultiplayerVisibleFailure;
bool runtimeMultiplayerVisibleFailureIsFallback = false;

void publishRuntimeMultiplayerVisibleFailure(
    std::string reason,
    bool fallback = false
) {
    if (reason.empty()) {
        reason = "multiplayer runtime bridge failed";
    }
    std::lock_guard<std::mutex> lock(
        runtimeMultiplayerVisibleFailureMutex
    );
    // Preserve the first concrete cause. A later destructor fallback must not
    // replace a transport, coordinator, attach, or runtime failure.
    if (!runtimeMultiplayerVisibleFailure.has_value()
        || (!fallback && runtimeMultiplayerVisibleFailureIsFallback)) {
        runtimeMultiplayerVisibleFailure = std::move(reason);
        runtimeMultiplayerVisibleFailureIsFallback = fallback;
    }
}

} // namespace

std::optional<std::string> consumeRuntimeMultiplayerVisibleFailure() {
    std::lock_guard<std::mutex> lock(
        runtimeMultiplayerVisibleFailureMutex
    );
    std::optional<std::string> result = std::move(
        runtimeMultiplayerVisibleFailure
    );
    runtimeMultiplayerVisibleFailure.reset();
    runtimeMultiplayerVisibleFailureIsFallback = false;
    return result;
}

void clearRuntimeMultiplayerVisibleFailureForMainMenu() {
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

std::string formatRuntimeMultiplayerVisibleFailureMessage(
    std::string_view reason
) {
    const std::string detail = reason.empty()
        ? "unknown multiplayer error"
        : std::string(reason);
    return "The multiplayer match could not continue.\n\nReason: "
        + detail
        + "\n\nWould you like to return to the main menu?";
}

bool RuntimeMultiplayerRuntimeCallbacks::complete() const {
    return static_cast<bool>(prevalidateCommands)
        && static_cast<bool>(applyCommands)
        && static_cast<bool>(currentCompletedTick)
        && static_cast<bool>(advanceSimulation)
        && static_cast<bool>(authoritativeStateHash)
        && static_cast<bool>(matchTerminal)
        && static_cast<bool>(stageVerifiedSnapshot)
        && static_cast<bool>(commitStagedSnapshot)
        && static_cast<bool>(discardStagedSnapshot)
        && static_cast<bool>(onLoadTimelineRebound);
}

RuntimeMultiplayerRuntimeBridge::RuntimeMultiplayerRuntimeBridge(
    RuntimeMultiplayerLockstepConfig config
) : config_(std::move(config)) {
    std::string errorMessage;
    hooksReady_ = installLanHooks(errorMessage);
    if (!hooksReady_) initializationError_ = std::move(errorMessage);
}

RuntimeMultiplayerRuntimeBridge::~RuntimeMultiplayerRuntimeBridge() {
    std::string maintenanceError;
    (void)stopPreAttachMaintenance(maintenanceError);
    if (preAttachMaintenanceEverStarted_) {
        publishRuntimeMultiplayerVisibleFailure(
            runtimeEverAttached_
                ? "multiplayer runtime ended unexpectedly"
                : "multiplayer loading ended before the runtime attached",
            true
        );
    }
    closeExternalOrderedTransport();
}

bool RuntimeMultiplayerRuntimeBridge::ready(
    std::string& errorMessage
) const {
    if (!hooksReady_) {
        errorMessage = initializationError_.empty()
            ? "multiplayer runtime bridge hooks are unavailable"
            : initializationError_;
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::adoptExternalOrderedTransport(
    std::unique_ptr<RuntimeMultiplayerExternalOrderedTransport> transport,
    std::string& errorMessage
) {
    if (!transport) {
        return fail(errorMessage, "external ordered transport is missing");
    }
    if (externalOrderedTransport_) {
        return fail(
            errorMessage,
            "external ordered transport is already installed"
        );
    }
    if (fatal()) {
        return fail(errorMessage, fatalReason_);
    }
    externalOrderedTransport_ = std::move(transport);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::hasExternalOrderedTransport() const {
    return externalOrderedTransport_ != nullptr;
}

RuntimeMultiplayerLanSession& RuntimeMultiplayerRuntimeBridge::lan() {
    // RuntimeShell validates the Running room through lan() immediately before
    // attachRuntime(). Make that first access the synchronization handoff so
    // its validation never races the loading maintenance worker.
    std::string ignored;
    (void)stopPreAttachMaintenance(ignored);
    return lan_;
}

const RuntimeMultiplayerLanSession& RuntimeMultiplayerRuntimeBridge::lan(
) const {
    std::string ignored;
    (void)const_cast<RuntimeMultiplayerRuntimeBridge*>(this)
        ->stopPreAttachMaintenance(ignored);
    return lan_;
}

const RuntimeMultiplayerSessionState&
RuntimeMultiplayerRuntimeBridge::orderedTransportPumpSessionView(
) const noexcept {
    return lan_.session();
}

std::uint16_t
RuntimeMultiplayerRuntimeBridge::orderedTransportPumpSessionPort(
) const noexcept {
    return lan_.sessionPort();
}

bool RuntimeMultiplayerRuntimeBridge::orderedTransportPumpReplaceReconnectEndpoint(
    std::uint16_t loopbackPort, std::vector<std::uint8_t> prelude, std::string& errorMessage
) {
    return lan_.replaceAuthenticatedReconnectEndpoint(loopbackPort, std::move(prelude), errorMessage);
}

bool RuntimeMultiplayerRuntimeBridge::orderedTransportPumpReconnectPending() const {
    return lan_.reconnectPending();
}

bool RuntimeMultiplayerRuntimeBridge::attachRuntime(
    RuntimeMultiplayerRuntimeCallbacks callbacks,
    std::string& errorMessage
) {
    std::string maintenanceError;
    if (!stopPreAttachMaintenance(maintenanceError)) {
        return failRuntimeAttach(
            errorMessage,
            std::move(maintenanceError)
        );
    }
    if (!hooksReady_) {
        return failRuntimeAttach(errorMessage, initializationError_);
    }
    if (!callbacks.complete()) {
        return failRuntimeAttach(
            errorMessage,
            "multiplayer runtime callbacks must be installed as one complete set"
        );
    }
    if (callbacksAttached_) {
        return failRuntimeAttach(
            errorMessage,
            "multiplayer runtime is already attached"
        );
    }
    if (lan_.phase() != RuntimeMultiplayerLanPhase::Running) {
        return failRuntimeAttach(
            errorMessage,
            "multiplayer runtime may attach only after the room starts"
        );
    }
    if (!lockstep_.bound()) {
        if (!bindLockstepAfterStart(lan_.session(), errorMessage)) {
            return failRuntimeAttach(errorMessage, errorMessage);
        }
    }
    if (!refreshLocalParticipation(errorMessage)) {
        return failRuntimeAttach(errorMessage, errorMessage);
    }

    int runtimeTick = -1;
    try {
        runtimeTick = callbacks.currentCompletedTick();
    } catch (const std::exception& exception) {
        return failRuntimeAttach(
            errorMessage,
            std::string("multiplayer runtime tick query threw: ")
                + exception.what()
        );
    } catch (...) {
        return failRuntimeAttach(
            errorMessage,
            "multiplayer runtime tick query threw"
        );
    }
    if (runtimeTick != lan_.session().authoritativeTick() - 1) {
        return failRuntimeAttach(
            errorMessage,
            "multiplayer runtime tick does not match the session barrier"
        );
    }

    callbacks_ = std::move(callbacks);
    callbacksAttached_ = true;
    lastCommittedTick_ = lan_.session().authoritativeTick() - 1;
    if (!queueInitialStateHash(lan_.session(), errorMessage)) {
        callbacks_ = RuntimeMultiplayerRuntimeCallbacks{};
        callbacksAttached_ = false;
        return failRuntimeAttach(errorMessage, errorMessage);
    }
    if (!refreshTerminalState(errorMessage)) {
        callbacks_ = RuntimeMultiplayerRuntimeCallbacks{};
        callbacksAttached_ = false;
        return failRuntimeAttach(errorMessage, errorMessage);
    }
    runtimeEverAttached_ = true;
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerRuntimeBridge::detachRuntime() {
    callbacks_ = RuntimeMultiplayerRuntimeCallbacks{};
    callbacksAttached_ = false;
    pendingCommands_.clear();
    pendingStateHashes_.clear();
    initialStateHashTick_ = -1;
    initialStateHashSubmitted_ = false;
    initialStateHashVerified_ = false;
    initialLoadDeadlineArmed_ = false;
    initialLoadAbortDrainReason_.reset();
    initialLoadAbortDrainPumpCount_ = 0U;
    reconnectRecoveryProposalId_ = 0;
    reconnectRecoveryStartedAt_ = {};
}

bool RuntimeMultiplayerRuntimeBridge::beginPreAttachMaintenance(
    std::string& errorMessage,
    std::uint32_t initialLoadTimeoutMilliseconds
) {
    if (!hooksReady_) return fail(errorMessage, initializationError_);
    if (fatal()) return fail(errorMessage, fatalReason_);
    if (callbacksAttached_) {
        return fail(
            errorMessage,
            "pre-attach maintenance cannot start after runtime attach"
        );
    }
    if (lan_.phase() != RuntimeMultiplayerLanPhase::Running) {
        return fail(
            errorMessage,
            "pre-attach maintenance requires a running multiplayer room"
        );
    }
    if (preAttachMaintenanceEverStarted_
        || preAttachMaintenanceThread_.joinable()) {
        return fail(
            errorMessage,
            "pre-attach multiplayer maintenance already started"
        );
    }
    if (initialLoadTimeoutMilliseconds == 0U) {
        return fail(
            errorMessage,
            "multiplayer initial loading timeout must be positive"
        );
    }

    preAttachStopRequested_.store(false, std::memory_order_release);
    preAttachMaintenanceRunning_.store(true, std::memory_order_release);
    preAttachMaintenancePumpCount_.store(0U, std::memory_order_release);
    preAttachMaintenanceError_.clear();
    initialLoadStartedAt_ = std::chrono::steady_clock::now();
    initialLoadTimeout_ = std::chrono::milliseconds(
        initialLoadTimeoutMilliseconds
    );
    initialLoadDeadlineArmed_ = true;
    try {
        preAttachMaintenanceThread_ = std::thread([this]() {
            try {
                while (!preAttachStopRequested_.load(
                    std::memory_order_acquire
                )) {
                    const RuntimeMultiplayerRuntimePumpResult result =
                        pump(false, 0);
                    if (!result.succeeded) {
                        preAttachMaintenanceError_ = result.error.empty()
                            ? "pre-attach multiplayer maintenance failed"
                            : result.error;
                        break;
                    }
                    preAttachMaintenancePumpCount_.fetch_add(
                        1U,
                        std::memory_order_release
                    );
                    if (!preAttachStopRequested_.load(
                            std::memory_order_acquire
                        )) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1)
                        );
                    }
                }
            } catch (const std::exception& exception) {
                preAttachMaintenanceError_ = std::string(
                    "pre-attach multiplayer maintenance threw: "
                ) + exception.what();
                publishRuntimeMultiplayerVisibleFailure(
                    preAttachMaintenanceError_
                );
            } catch (...) {
                preAttachMaintenanceError_ =
                    "pre-attach multiplayer maintenance threw";
                publishRuntimeMultiplayerVisibleFailure(
                    preAttachMaintenanceError_
                );
            }
            preAttachMaintenanceRunning_.store(
                false,
                std::memory_order_release
            );
        });
    } catch (const std::exception& exception) {
        preAttachMaintenanceRunning_.store(false, std::memory_order_release);
        initialLoadDeadlineArmed_ = false;
        errorMessage = std::string(
            "pre-attach multiplayer maintenance could not start: "
        ) + exception.what();
        return false;
    } catch (...) {
        preAttachMaintenanceRunning_.store(false, std::memory_order_release);
        initialLoadDeadlineArmed_ = false;
        errorMessage =
            "pre-attach multiplayer maintenance could not start";
        return false;
    }
    preAttachMaintenanceEverStarted_ = true;
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::preAttachMaintenanceRunning() const {
    return preAttachMaintenanceRunning_.load(std::memory_order_acquire);
}

std::uint64_t
RuntimeMultiplayerRuntimeBridge::preAttachMaintenancePumpCount() const {
    return preAttachMaintenancePumpCount_.load(std::memory_order_acquire);
}

bool RuntimeMultiplayerRuntimeBridge::stopPreAttachMaintenance(
    std::string& errorMessage
) {
    preAttachStopRequested_.store(true, std::memory_order_release);
    if (preAttachMaintenanceThread_.joinable()) {
        preAttachMaintenanceThread_.join();
    }
    preAttachMaintenanceRunning_.store(false, std::memory_order_release);

    // RuntimeShell's first lan() access is the ownership handoff. If that
    // arrives during the short canonical-abort drain window, finish the drain
    // synchronously before exposing the bridge or closing its Internet
    // carrier. This preserves both the abort delivery and its concrete cause.
    const auto drainDeadline = std::chrono::steady_clock::now()
        + initialLoadAbortMinimumDrainTime * 2;
    while (initialLoadAbortDrainReason_.has_value() && !fatal()
        && std::chrono::steady_clock::now() < drainDeadline) {
        const RuntimeMultiplayerRuntimePumpResult drained = pump(false, 0);
        if (!drained.succeeded) break;
        if (initialLoadAbortDrainReason_.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (initialLoadAbortDrainReason_.has_value() && !fatal()) {
        std::string reason = std::move(*initialLoadAbortDrainReason_);
        initialLoadAbortDrainReason_.reset();
        noteFatal(std::move(reason));
    }

    if (!preAttachMaintenanceError_.empty()) {
        if (!fatal()) noteFatal(preAttachMaintenanceError_);
        errorMessage = preAttachMaintenanceError_;
        preAttachMaintenanceError_.clear();
        publishRuntimeMultiplayerVisibleFailure(errorMessage);
        return false;
    }
    if (fatal()) {
        errorMessage = fatalReason_;
        if (preAttachMaintenanceEverStarted_ || runtimeEverAttached_) {
            publishRuntimeMultiplayerVisibleFailure(errorMessage);
        }
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::failRuntimeAttach(
    std::string& errorMessage,
    std::string reason
) {
    if (preAttachMaintenanceEverStarted_) {
        publishRuntimeMultiplayerVisibleFailure(reason);
    }
    return fail(errorMessage, std::move(reason));
}

bool RuntimeMultiplayerRuntimeBridge::runtimeAttached() const {
    return callbacksAttached_;
}

bool RuntimeMultiplayerRuntimeBridge::lockstepBound() const {
    return lockstep_.bound();
}

bool RuntimeMultiplayerRuntimeBridge::localPeerIsHost() const {
    return localHost_;
}

bool RuntimeMultiplayerRuntimeBridge::localPeerIsSpectator() const {
    return localSpectator_;
}

bool RuntimeMultiplayerRuntimeBridge::mayCaptureGameplay() const {
    return callbacksAttached_ && lockstep_.bound() && !localSpectator_
        && !matchTerminal_
        && lan_.phase() == RuntimeMultiplayerLanPhase::Running
        && !fatal();
}

bool RuntimeMultiplayerRuntimeBridge::captureGameplayCommand(
    RuntimeMultiplayerGameplayCommand command,
    std::string& errorMessage
) {
    if (!mayCaptureGameplay()) {
        return fail(
            errorMessage,
            localSpectator_
                ? "spectators may not issue gameplay commands"
                : "multiplayer gameplay capture is not active"
        );
    }
    if (lan_.session().hasActiveSaveLoadHandshake()) {
        return fail(
            errorMessage,
            "save/load synchronization is holding gameplay input"
        );
    }
    if (pendingCommands_.size() >= config_.maximumCommandsPerFrame) {
        return fail(errorMessage, "local multiplayer gameplay frame is full");
    }
    if (!canonicalizeRuntimeMultiplayerGameplayCommand(
            command,
            errorMessage
        )) {
        return false;
    }
    pendingCommands_.push_back(std::move(command));
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerRuntimeBridge::cancelPendingGameplayCommands() {
    pendingCommands_.clear();
}

std::size_t
RuntimeMultiplayerRuntimeBridge::pendingGameplayCommandCount() const {
    return pendingCommands_.size();
}

RuntimeMultiplayerRuntimePumpResult RuntimeMultiplayerRuntimeBridge::pump(
    bool requestHostAdvance,
    int transportTimeoutMilliseconds
) {
    RuntimeMultiplayerRuntimePumpResult result;
    if (!hooksReady_) {
        result.succeeded = false;
        result.error = initializationError_;
        return result;
    }
    if (fatal()) {
        result.succeeded = false;
        result.error = fatalReason_;
        return result;
    }

    tickCommittedDuringPump_ = false;
    std::string externalError;
    std::string operationError;
    if (externalOrderedTransport_ && !pumpExternalOrderedTransport(
            RuntimeMultiplayerOrderedTransportPumpStage::BeforeLanPoll,
            transportTimeoutMilliseconds,
            externalError
        )) {
        if (initialLoadAbortDrainReason_.has_value()) {
            std::string loadingReason = std::move(
                *initialLoadAbortDrainReason_
            );
            initialLoadAbortDrainReason_.reset();
            noteFatal(std::move(loadingReason));
        } else {
            noteFatal(
                externalError.empty()
                    ? "external ordered transport failed before LAN poll"
                    : std::move(externalError)
            );
        }
        result.succeeded = false;
        result.error = fatalReason_;
        return result;
    }
    // An external ordered carrier consumes the caller's wait budget while
    // moving relay bytes into the loopback LAN socket. Polling that socket is
    // then non-blocking, so one bridge pump never waits twice. Plain LAN keeps
    // the original timeout behavior.
    const int lanTimeoutMilliseconds = externalOrderedTransport_
        ? 0
        : transportTimeoutMilliseconds;
    RuntimeMultiplayerLanPollResult polled = lan_.poll(
        lanTimeoutMilliseconds
    );
    result.events = std::move(polled.events);
    result.simulationDeferred = polled.simulationDeferred;
    if (!polled.succeeded) {
        noteFatal(
            polled.error.empty()
                ? "multiplayer LAN poll failed"
                : std::move(polled.error)
        );
    }

    const bool sawReconnect = std::any_of(
        result.events.begin(),
        result.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind
                == RuntimeMultiplayerLanEventKind::Reconnected;
        }
    );
    if (!fatal() && sawReconnect
        && reconnectRecoveryStartedAt_
            == std::chrono::steady_clock::time_point{}) {
        reconnectRecoveryStartedAt_ = std::chrono::steady_clock::now();
    }
    if (!fatal() && localHost_ && lan_.reconnectReadyForRecovery()
        && !startReconnectRecovery(operationError)) {
        const RuntimeMultiplayerLanOperationResult aborted =
            lan_.abortRunningSession(
                lan_.session().config().hostPeerId,
                RuntimeMultiplayerSessionAbortReason::TransportFailure
            );
        if (!aborted.succeeded || !aborted.applied) {
            operationError += operationError.empty() ? "" : "; ";
            operationError += "host could not broadcast reconnect failure";
        }
        noteFatal(std::move(operationError));
    }
    if (!fatal() && !refreshReconnectRecovery(operationError)) {
        noteFatal(std::move(operationError));
    }

    bool initialHashReady = false;
    std::optional<std::string> initialLoadTimeoutReason;
    if (!fatal() && callbacksAttached_
        && !refreshInitialStateHashBarrier(
            initialHashReady,
            operationError
        )) {
        noteFatal(std::move(operationError));
    }
    if (!fatal() && initialLoadDeadlineArmed_ && localHost_
        && lan_.phase() == RuntimeMultiplayerLanPhase::Running
        && !initialHashReady
        && std::chrono::steady_clock::now() - initialLoadStartedAt_
            >= initialLoadTimeout_) {
        const std::string affectedPeerId =
            initialLoadTimeoutAffectedPeerId();
        std::string reason = "multiplayer initial loading timed out";
        if (!affectedPeerId.empty()) {
            reason += " waiting for peer '" + affectedPeerId + "'";
        }
        const auto member = std::find_if(
            lan_.session().members().begin(),
            lan_.session().members().end(),
            [&affectedPeerId](
                const RuntimeMultiplayerRoomMember& candidate
            ) {
                return candidate.peerId == affectedPeerId;
            }
        );
        const bool affectedSpectator =
            member != lan_.session().members().end()
            && member->participation
                == RuntimeMultiplayerParticipationRole::Spectator;
        if (affectedSpectator) {
            const RuntimeMultiplayerLanOperationResult dropped =
                lan_.dropRunningSpectator(
                    affectedPeerId,
                    "spectator initial loading timed out"
                );
            if (!dropped.succeeded || !dropped.applied) {
                noteFatal(
                    dropped.reason.empty()
                        ? reason + "; host could not remove the spectator"
                        : reason + "; " + dropped.reason
                );
            }
        } else {
            const RuntimeMultiplayerLanOperationResult aborted =
                lan_.abortRunningSession(
                    affectedPeerId.empty()
                        ? lan_.session().config().hostPeerId
                        : affectedPeerId,
                    RuntimeMultiplayerSessionAbortReason::TransportFailure
                );
            initialLoadDeadlineArmed_ = false;
            if (!aborted.succeeded || !aborted.applied) {
                noteFatal(
                    aborted.reason.empty()
                        ? reason
                            + "; host could not broadcast the session abort"
                        : reason + "; " + aborted.reason
                );
            } else {
                initialLoadAbortDrainReason_ = reason;
                initialLoadAbortDrainStartedAt_ =
                    std::chrono::steady_clock::now();
                initialLoadAbortDrainPumpCount_ = 0U;
            }
        }
        if (!fatal()) {
            // Host canonical messages are first queued in RuntimeTcpServer.
            // Poll once more before the external AfterLanPoll pass so an abort
            // or spectator Leave reaches the local proxy socket. SessionAbort
            // then receives additional bounded drain pumps before close.
            RuntimeMultiplayerLanPollResult drainPoll = lan_.poll(0);
            result.events.insert(
                result.events.end(),
                std::make_move_iterator(drainPoll.events.begin()),
                std::make_move_iterator(drainPoll.events.end())
            );
            result.simulationDeferred = result.simulationDeferred
                || drainPoll.simulationDeferred;
            if (!drainPoll.succeeded) {
                noteFatal(
                    reason + "; canonical loading decision could not be flushed"
                );
                initialLoadAbortDrainReason_.reset();
            }
        }
    }
    externalError.clear();
    if (!fatal() && externalOrderedTransport_
        && !pumpExternalOrderedTransport(
            RuntimeMultiplayerOrderedTransportPumpStage::AfterLanPoll,
            0,
            externalError
        )) {
        if (initialLoadAbortDrainReason_.has_value()) {
            // A peer that received SessionAbort normally closes its relay.
            // Preserve the initiating loading-timeout cause in that case.
            initialLoadTimeoutReason = std::move(
                initialLoadAbortDrainReason_
            );
            initialLoadAbortDrainReason_.reset();
        } else {
            noteFatal(
                externalError.empty()
                    ? "external ordered transport failed after LAN poll"
                    : std::move(externalError)
            );
        }
    }

    if (!fatal() && initialLoadAbortDrainReason_.has_value()) {
        ++initialLoadAbortDrainPumpCount_;
        const auto drainedFor = std::chrono::steady_clock::now()
            - initialLoadAbortDrainStartedAt_;
        if (initialLoadAbortDrainPumpCount_
                >= initialLoadAbortMinimumDrainPumps
            && drainedFor >= initialLoadAbortMinimumDrainTime) {
            initialLoadTimeoutReason = std::move(
                initialLoadAbortDrainReason_
            );
            initialLoadAbortDrainReason_.reset();
        }
    }

    const RuntimeMultiplayerLanPhase phase = lan_.phase();
    if (!fatal() && initialLoadTimeoutReason.has_value()) {
        noteFatal(std::move(*initialLoadTimeoutReason));
    }
    if (!fatal() && !initialLoadAbortDrainReason_.has_value()
        && (phase == RuntimeMultiplayerLanPhase::Aborted
            || phase == RuntimeMultiplayerLanPhase::Disconnected
            || phase == RuntimeMultiplayerLanPhase::JoinRejected)) {
        std::string terminalDetail;
        for (auto eventIterator = result.events.rbegin();
            eventIterator != result.events.rend();
            ++eventIterator) {
            if ((eventIterator->kind
                    == RuntimeMultiplayerLanEventKind::SessionAborted
                || eventIterator->kind
                    == RuntimeMultiplayerLanEventKind::Disconnected
                || eventIterator->kind
                    == RuntimeMultiplayerLanEventKind::ProtocolRejected
                || eventIterator->kind
                    == RuntimeMultiplayerLanEventKind::TransportError)
                && !eventIterator->reason.empty()) {
                terminalDetail = eventIterator->reason;
                if (!eventIterator->peerId.empty()) {
                    terminalDetail += " [peer=" + eventIterator->peerId + "]";
                }
                break;
            }
        }
        noteFatal(
            terminalDetail.empty()
                ? "multiplayer room ended before the runtime completed"
                : "multiplayer room ended before the runtime completed: "
                    + terminalDetail
        );
    }

    if (!fatal() && callbacksAttached_
        && !refreshTerminalState(operationError)) {
        noteFatal(std::move(operationError));
    }

    if (!fatal() && phase == RuntimeMultiplayerLanPhase::Running
        && !lan_.reconnectPending()
        && lockstep_.bound() && callbacksAttached_) {
        if (!refreshLocalParticipation(operationError)
            || (!matchTerminal_ && !flushLocalFrame(operationError))) {
            noteFatal(std::move(operationError));
        }
    }

    if (!fatal() && requestHostAdvance && localHost_
        && callbacksAttached_ && lockstep_.bound() && !matchTerminal_
        && !lan_.reconnectPending()
        && initialHashReady
        && !lan_.session().hasActiveSaveLoadHandshake()
        && lockstep_.frameBarrierReady(lockstep_.currentBarrierTick())
        && lockstep_.inputHorizonReady()) {
        const int completedTick = lockstep_.currentBarrierTick();
        if (completedTick < 0
            || completedTick == std::numeric_limits<int>::max()) {
            noteFatal("multiplayer authoritative tick overflowed");
        } else {
            const RuntimeMultiplayerLanOperationResult advanced =
                lan_.advanceAuthoritativeTick(completedTick + 1);
            if (!operationWasQueuedOrApplied(advanced)) {
                noteFatal(
                    advanced.reason.empty()
                        ? "host could not advance the authoritative tick"
                        : advanced.reason
                );
            }
        }
    }

    if (!fatal() && !lan_.reconnectPending()
        && !submitPendingStateHashes(operationError)) {
        noteFatal(std::move(operationError));
    }

    if (!fatal() && tickCommittedDuringPump_ && callbacksAttached_) {
        try {
            const int runtimeTick = callbacks_.currentCompletedTick();
            if (runtimeTick != lastCommittedTick_) {
                noteFatal(
                    "bridge committed tick did not match runtime state"
                );
            }
        } catch (const std::exception& exception) {
            noteFatal(
                std::string("multiplayer runtime tick query threw: ")
                    + exception.what()
            );
        } catch (...) {
            noteFatal("multiplayer runtime tick query threw");
        }
    }

    result.tickCommitted = tickCommittedDuringPump_;
    result.completedTick = tickCommittedDuringPump_
        ? lastCommittedTick_
        : -1;
    if (fatal()) {
        result.succeeded = false;
        result.error = fatalReason_;
    }
    return result;
}

bool RuntimeMultiplayerRuntimeBridge::reportRankedMatchResult(
    bool localVictory,
    bool& submitted,
    std::string& errorMessage
) {
    submitted = false;
    if (rankedMatchResultReportAttempted_) {
        errorMessage.clear();
        return true;
    }
    rankedMatchResultReportAttempted_ = true;
    if (!externalOrderedTransport_) {
        errorMessage.clear();
        return true;
    }
    try {
        return externalOrderedTransport_->reportRankedMatchResult(
            localVictory,
            submitted,
            errorMessage
        );
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("ranked match result report threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "ranked match result report threw");
    }
}

bool RuntimeMultiplayerRuntimeBridge::reportRankedMatchAbandon(bool& submitted, std::string& errorMessage) {
    submitted = false;
    if (rankedMatchResultReportAttempted_ || !externalOrderedTransport_) {
        errorMessage.clear();
        return true;
    }
    rankedMatchResultReportAttempted_ = true;
    try {
        return externalOrderedTransport_->reportRankedMatchAbandon(submitted, errorMessage);
    } catch (const std::exception& exception) {
        return fail(errorMessage, std::string("ranked departure report threw: ") + exception.what());
    } catch (...) {
        return fail(errorMessage, "ranked departure report threw");
    }
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerRuntimeBridge::proposeSaveLoad(
    RuntimeMultiplayerSaveLoadProposal proposal,
    std::vector<std::uint8_t> bytes
) {
    if (!hooksReady_ || fatal()) {
        return failedOperation(
            fatal() ? fatalReason_ : initializationError_
        );
    }
    if (!callbacksAttached_ || !localHost_
        || lan_.phase() != RuntimeMultiplayerLanPhase::Running) {
        return failedOperation(
            "only the attached running host may coordinate save/load"
        );
    }
    if (!initialStateHashVerified_) {
        return failedOperation(
            "waiting for all players to finish loading"
        );
    }
    return lan_.proposeSaveLoad(std::move(proposal), std::move(bytes));
}

bool RuntimeMultiplayerRuntimeBridge::startReconnectRecovery(
    std::string& errorMessage
) {
    if (!localHost_ || !callbacksAttached_
        || lan_.phase() != RuntimeMultiplayerLanPhase::Running
        || !lan_.reconnectPending()) {
        return fail(
            errorMessage,
            "reconnect recovery is not ready on this runtime bridge"
        );
    }
    if (externalOrderedTransport_ && !externalOrderedTransport_->supportsReconnectRecovery())
        return fail(errorMessage, "authenticated relay reconnect requires a new service route");
    if (!callbacks_.captureReconnectSnapshot) {
        return fail(
            errorMessage,
            "runtime reconnect snapshot capture is unavailable"
        );
    }
    if (lan_.session().hasActiveSaveLoadHandshake()) {
        return fail(
            errorMessage,
            "reconnect recovery conflicts with active save/load"
        );
    }

    std::uint64_t proposalId = 1;
    for (const RuntimeMultiplayerSaveLoadHandshake& handshake :
            lan_.session().saveLoadHandshakes()) {
        if (handshake.proposal.proposalId
            == std::numeric_limits<std::uint64_t>::max()) {
            return fail(
                errorMessage,
                "reconnect recovery proposal id overflowed"
            );
        }
        proposalId = std::max(
            proposalId,
            handshake.proposal.proposalId + 1U
        );
    }

    if (!lan_.session().matchSetup().has_value()) {
        return fail(errorMessage, "reconnect recovery has no match setup");
    }
    std::uint64_t setupFingerprint = 0;
    if (!runtimeMultiplayerGameplaySetupFingerprint(
            *lan_.session().matchSetup(),
            setupFingerprint,
            errorMessage
        )) {
        return false;
    }

    int runtimeTick = -1;
    std::uint64_t authoritativeHash = 0;
    try {
        runtimeTick = callbacks_.currentCompletedTick();
        authoritativeHash = callbacks_.authoritativeStateHash();
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("reconnect snapshot query threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "reconnect snapshot query threw");
    }
    if (runtimeTick < 0
        || runtimeTick != lan_.session().authoritativeTick() - 1
        || authoritativeHash == 0) {
        return fail(
            errorMessage,
            "reconnect snapshot is not at the authoritative barrier"
        );
    }

    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = proposalId;
    identity.kind = RuntimeMultiplayerSnapshotKind::Load;
    identity.sourceTimelineEpoch = lan_.session().timelineEpoch();
    identity.barrierTick = runtimeTick;
    identity.snapshotTick = runtimeTick;
    identity.snapshotToken = "reconnect-"
        + std::to_string(identity.sourceTimelineEpoch)
        + "-" + std::to_string(proposalId);
    identity.authoritativeStateHash = authoritativeHash;
    identity.gameplaySetupFingerprint = setupFingerprint;

    RuntimeMultiplayerSnapshotDescriptor descriptor;
    std::vector<std::uint8_t> bytes;
    bool captured = false;
    try {
        captured = callbacks_.captureReconnectSnapshot(
            identity,
            descriptor,
            bytes,
            errorMessage
        );
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("reconnect snapshot capture threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "reconnect snapshot capture threw");
    }
    if (!captured) {
        if (errorMessage.empty()) {
            errorMessage = "reconnect snapshot capture failed";
        }
        return false;
    }
    if (descriptor.identity != identity
        || !validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            errorMessage
        )
        || descriptor.byteCount != bytes.size()
        || runtimeMultiplayerSnapshotFingerprint(bytes)
            != descriptor.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes) != descriptor.sha256) {
        if (errorMessage.empty()) {
            errorMessage =
                "reconnect snapshot bytes do not match the descriptor";
        }
        return false;
    }
    if (!lan_.markReconnectRecoveryProposal(proposalId, errorMessage)) {
        return false;
    }

    RuntimeMultiplayerSaveLoadProposal proposal;
    proposal.timelineEpoch = identity.sourceTimelineEpoch;
    proposal.proposalId = proposalId;
    proposal.kind = RuntimeMultiplayerSaveLoadKind::Load;
    proposal.proposerPeerId = lan_.session().config().hostPeerId;
    proposal.descriptor = descriptor;
    proposal.tick = identity.snapshotTick;
    proposal.snapshotId = identity.snapshotToken;
    proposal.snapshotFingerprint = descriptor.snapshotFingerprint;
    const RuntimeMultiplayerLanOperationResult proposed =
        lan_.proposeSaveLoad(std::move(proposal), std::move(bytes));
    if (!proposed.succeeded || !proposed.applied) {
        return fail(
            errorMessage,
            proposed.reason.empty()
                ? "reconnect recovery snapshot proposal failed"
                : proposed.reason
        );
    }
    reconnectRecoveryProposalId_ = proposalId;
    reconnectRecoveryStartedAt_ = std::chrono::steady_clock::now();
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::refreshReconnectRecovery(
    std::string& errorMessage
) {
    if (!lan_.reconnectPending()) {
        reconnectRecoveryProposalId_ = 0;
        reconnectRecoveryStartedAt_ = {};
        errorMessage.clear();
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (reconnectRecoveryStartedAt_
            == std::chrono::steady_clock::time_point{}) {
        reconnectRecoveryStartedAt_ = now;
    }
    if (now - reconnectRecoveryStartedAt_
        >= std::chrono::milliseconds(
            externalOrderedTransport_ ? externalOrderedTransport_->reconnectRecoveryTimeoutMilliseconds()
                : runtimeMultiplayerReconnectRecoveryTimeoutMilliseconds
        )) {
        if (localHost_
            && lan_.phase() == RuntimeMultiplayerLanPhase::Running) {
            (void)lan_.abortRunningSession(
                lan_.session().config().hostPeerId,
                RuntimeMultiplayerSessionAbortReason::TransportFailure
            );
        }
        return fail(errorMessage, "multiplayer reconnect recovery timed out");
    }
    if (reconnectRecoveryProposalId_ == 0) {
        errorMessage.clear();
        return true;
    }

    const auto& handshakes = lan_.session().saveLoadHandshakes();
    const auto found = std::find_if(
        handshakes.rbegin(),
        handshakes.rend(),
        [this](const RuntimeMultiplayerSaveLoadHandshake& handshake) {
            return handshake.proposal.proposalId
                == reconnectRecoveryProposalId_;
        }
    );
    if (found == handshakes.rend()) {
        return fail(
            errorMessage,
            "reconnect recovery proposal disappeared from session state"
        );
    }
    if (found->status == RuntimeMultiplayerSaveLoadStatus::Rejected
        || found->status == RuntimeMultiplayerSaveLoadStatus::Aborted) {
        if (localHost_
            && lan_.phase() == RuntimeMultiplayerLanPhase::Running) {
            (void)lan_.abortRunningSession(
                lan_.session().config().hostPeerId,
                RuntimeMultiplayerSessionAbortReason::TransportFailure
            );
        }
        return fail(
            errorMessage,
            "multiplayer reconnect snapshot recovery was rejected"
        );
    }
    errorMessage.clear();
    return true;
}

const RuntimeMultiplayerLockstepDriver&
RuntimeMultiplayerRuntimeBridge::lockstep() const {
    return lockstep_;
}

int RuntimeMultiplayerRuntimeBridge::lastCommittedTick() const {
    return lastCommittedTick_;
}

bool RuntimeMultiplayerRuntimeBridge::initialStateHashVerified() const {
    return initialStateHashVerified_;
}

bool RuntimeMultiplayerRuntimeBridge::fatal() const {
    return !fatalReason_.empty();
}

const std::string& RuntimeMultiplayerRuntimeBridge::fatalReason() const {
    return fatalReason_;
}

bool RuntimeMultiplayerRuntimeBridge::installLanHooks(
    std::string& errorMessage
) {
    RuntimeMultiplayerLanCommandPayloadValidator validator =
        [this](
            const RuntimeMultiplayerCommandPacket& packet,
            std::string& validationError
        ) {
            RuntimeMultiplayerGameplayFrame frame;
            return decodeRuntimeMultiplayerGameplayFrame(
                packet.payload,
                frame,
                validationError,
                config_.maximumCommandsPerFrame,
                config_.maximumFrameBytes
            );
        };
    if (!lan_.setCommandPayloadValidator(
            std::move(validator),
            errorMessage
        )) {
        return false;
    }

    RuntimeMultiplayerLanSimulationHooks simulation;
    simulation.onStarted = [this](
        const RuntimeMultiplayerSessionState& session,
        std::string& hookError
    ) {
        return bindLockstepAfterStart(session, hookError);
    };
    simulation.onCommandAdmitted = [this](
        const RuntimeMultiplayerCommandPacket& packet,
        std::string& hookError
    ) {
        const RuntimeMultiplayerOperationResult observed =
            lockstep_.observeAdmittedFrame(packet);
        if (!observed.accepted) {
            hookError = observed.reason;
            return false;
        }
        hookError.clear();
        return true;
    };
    simulation.prepareAuthoritativeAdvance = [this](
        int completedTick,
        std::string& hookError
    ) {
        if (!callbacksAttached_) {
            hookError = "multiplayer runtime world is not attached yet";
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
        }
        bool initialHashReady = false;
        if (!refreshInitialStateHashBarrier(
                initialHashReady,
                hookError
            )) {
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
        }
        if (!initialHashReady) {
            hookError = "multiplayer initial state hash barrier is incomplete";
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
        }
        if (!lockstep_.frameBarrierReady(completedTick)
            || !lockstep_.inputHorizonReady()) {
            hookError = "multiplayer input horizon is incomplete";
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
        }
        const RuntimeMultiplayerOperationResult applied =
            lockstep_.applyReadyTick(
                completedTick,
                callbacks_.prevalidateCommands,
                [this, completedTick](
                    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
                    std::string& applyError
                ) {
                    if (!callbacks_.applyCommands(batch, applyError)) {
                        return false;
                    }
                    try {
                        callbacks_.advanceSimulation(completedTick);
                    } catch (const std::exception& exception) {
                        applyError = std::string(
                            "multiplayer simulation advance threw: "
                        ) + exception.what();
                        return false;
                    } catch (...) {
                        applyError = "multiplayer simulation advance threw";
                        return false;
                    }
                    return true;
                }
            );
        if (!applied.accepted) {
            hookError = applied.reason;
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
        }
        hookError.clear();
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
    };
    simulation.onAuthoritativeAdvance = [this](
        int completedTick,
        std::string& hookError
    ) {
        const RuntimeMultiplayerOperationResult acknowledged =
            lockstep_.acknowledgeAuthoritativeAdvance(completedTick);
        if (!acknowledged.accepted) {
            hookError = acknowledged.reason;
            return false;
        }
        lastCommittedTick_ = completedTick;
        tickCommittedDuringPump_ = true;
        const RuntimeMultiplayerSessionConfig& sessionConfig =
            lan_.session().config();
        if (sessionConfig.stateHashPeriodTicks > 0
            && (completedTick - lan_.session().startTick())
                % sessionConfig.stateHashPeriodTicks == 0) {
            std::uint64_t hash = 0;
            try {
                hash = callbacks_.authoritativeStateHash();
            } catch (const std::exception& exception) {
                hookError = std::string(
                    "multiplayer state hash threw: "
                ) + exception.what();
                return false;
            } catch (...) {
                hookError = "multiplayer state hash threw";
                return false;
            }
            if (hash == 0) {
                hookError = "multiplayer authoritative state hash is zero";
                return false;
            }
            pendingStateHashes_.push_back(
                PendingStateHash{completedTick, hash}
            );
        }
        hookError.clear();
        return true;
    };
    if (!lan_.setSimulationHooks(std::move(simulation), errorMessage)) {
        return false;
    }

    RuntimeMultiplayerLanSnapshotHooks snapshots;
    snapshots.stageVerifiedBytes = [this](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& hookError
    ) {
        if (!callbacksAttached_) {
            return fail(
                hookError,
                "multiplayer runtime world is not attached for snapshot staging"
            );
        }
        return callbacks_.stageVerifiedSnapshot(
            descriptor,
            bytes,
            observedHash,
            hookError
        );
    };
    snapshots.commitStaged = [this](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& hookError
    ) {
        if (!callbacksAttached_) {
            return fail(
                hookError,
                "multiplayer runtime world is not attached for snapshot commit"
            );
        }
        return callbacks_.commitStagedSnapshot(
            descriptor,
            observedHash,
            hookError
        );
    };
    snapshots.postSessionCommit = [this](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        const RuntimeMultiplayerSessionState& session,
        std::string& hookError
    ) {
        const RuntimeMultiplayerOperationResult rebound =
            lockstep_.rebindAfterLoadCommit(session, config_);
        if (!rebound.accepted) {
            hookError = rebound.reason;
            return false;
        }
        nextFrameSequence_ = 1;
        nextFrameTick_ = lockstep_.firstNetworkInputTick();
        pendingCommands_.clear();
        pendingStateHashes_.clear();
        lastHashSubmittedTick_ = -1;
        lastCommittedTick_ = session.authoritativeTick() - 1;
        initialStateHashTick_ = -1;
        initialStateHashSubmitted_ = false;
        initialStateHashVerified_ = false;
        matchTerminal_ = false;
        if (!refreshLocalParticipation(hookError)) return false;
        if (!callbacks_.onLoadTimelineRebound(
                descriptor,
                session,
                hookError
            )) {
            return false;
        }
        return queueInitialStateHash(session, hookError);
    };
    snapshots.discardStaged = [this](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor
    ) {
        if (callbacksAttached_) callbacks_.discardStagedSnapshot(descriptor);
    };
    return lan_.setSnapshotHooks(std::move(snapshots), errorMessage);
}

bool RuntimeMultiplayerRuntimeBridge::bindLockstepAfterStart(
    const RuntimeMultiplayerSessionState& session,
    std::string& errorMessage
) {
    if (lockstep_.bound()) {
        if (lockstep_.timelineEpoch() == session.timelineEpoch()) {
            errorMessage.clear();
            return true;
        }
        return fail(
            errorMessage,
            "multiplayer lockstep is already bound to another timeline"
        );
    }
    const RuntimeMultiplayerOperationResult bound = lockstep_.bind(
        session,
        config_
    );
    if (!bound.accepted) return fail(errorMessage, bound.reason);
    nextFrameSequence_ = 1;
    nextFrameTick_ = lockstep_.firstNetworkInputTick();
    pendingCommands_.clear();
    pendingStateHashes_.clear();
    lastHashSubmittedTick_ = -1;
    lastCommittedTick_ = session.authoritativeTick() - 1;
    return refreshLocalParticipation(errorMessage);
}

bool RuntimeMultiplayerRuntimeBridge::flushLocalFrame(
    std::string& errorMessage
) {
    if (!lockstep_.bound() || localSpectator_
        || lan_.session().hasActiveSaveLoadHandshake()) {
        errorMessage.clear();
        return true;
    }
    const int expectedFrameTick = lockstep_.currentBarrierTick()
        + config_.inputDelayTicks;
    if (nextFrameTick_ < 0) nextFrameTick_ = expectedFrameTick;
    if (nextFrameTick_ < expectedFrameTick) {
        return fail(
            errorMessage,
            "local multiplayer input frame missed its canonical horizon"
        );
    }
    if (nextFrameTick_ > expectedFrameTick) {
        errorMessage.clear();
        return true;
    }
    if (nextFrameSequence_ == 0
        || nextFrameSequence_
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()
            )) {
        return fail(errorMessage, "local multiplayer frame sequence overflowed");
    }

    RuntimeMultiplayerGameplayFrame frame;
    frame.commands = pendingCommands_;
    std::vector<std::uint8_t> payload;
    if (!encodeRuntimeMultiplayerGameplayFrame(
            frame,
            payload,
            errorMessage,
            config_.maximumCommandsPerFrame,
            config_.maximumFrameBytes
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult submitted = lan_.submitCommand(
        nextFrameTick_,
        nextFrameSequence_,
        std::move(payload)
    );
    if (!operationWasQueuedOrApplied(submitted)) {
        return fail(
            errorMessage,
            submitted.reason.empty()
                ? "local multiplayer frame could not be submitted"
                : submitted.reason
        );
    }
    pendingCommands_.clear();
    ++nextFrameSequence_;
    ++nextFrameTick_;
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::submitPendingStateHashes(
    std::string& errorMessage
) {
    if (pendingStateHashes_.empty()) {
        errorMessage.clear();
        return true;
    }
    if (!callbacksAttached_) {
        return fail(
            errorMessage,
            "multiplayer state hash is due without an attached world"
        );
    }
    while (!pendingStateHashes_.empty()) {
        const PendingStateHash pending = pendingStateHashes_.front();
        if (pending.tick == lastHashSubmittedTick_) {
            pendingStateHashes_.pop_front();
            continue;
        }
        const RuntimeMultiplayerLanOperationResult submitted =
            lan_.submitStateHash(pending.tick, pending.hash);
        if (!operationWasQueuedOrApplied(submitted)) {
            return fail(
                errorMessage,
                submitted.reason.empty()
                    ? "multiplayer state hash could not be submitted"
                    : submitted.reason
            );
        }
        lastHashSubmittedTick_ = pending.tick;
        if (pending.tick == initialStateHashTick_) {
            initialStateHashSubmitted_ = true;
        }
        pendingStateHashes_.pop_front();
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::queueInitialStateHash(
    const RuntimeMultiplayerSessionState& session,
    std::string& errorMessage
) {
    // This barrier is local orchestration state; only the resulting canonical
    // hash report enters SessionState.
    if (!callbacksAttached_) {
        return fail(
            errorMessage,
            "multiplayer initial hash requires an attached runtime"
        );
    }
    const int expectedTick = session.authoritativeTick() - 1;
    int runtimeTick = -1;
    std::uint64_t hash = 0;
    try {
        runtimeTick = callbacks_.currentCompletedTick();
        hash = callbacks_.authoritativeStateHash();
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("multiplayer initial state query threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "multiplayer initial state query threw");
    }
    if (runtimeTick != expectedTick) {
        return fail(
            errorMessage,
            "multiplayer initial world tick does not match the session barrier"
        );
    }
    if (expectedTick < 0 || hash == 0) {
        return fail(errorMessage, "multiplayer initial state hash is invalid");
    }
    initialStateHashTick_ = expectedTick;
    initialStateHashSubmitted_ = false;
    initialStateHashVerified_ = false;
    pendingStateHashes_.push_front(PendingStateHash{expectedTick, hash});
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerRuntimeBridge::refreshInitialStateHashBarrier(
    bool& ready,
    std::string& errorMessage
) {
    ready = initialStateHashVerified_;
    if (ready) {
        errorMessage.clear();
        return true;
    }
    if (!initialStateHashSubmitted_) {
        errorMessage.clear();
        return true;
    }
    const auto frame = std::find_if(
        lan_.session().stateHashFrames().begin(),
        lan_.session().stateHashFrames().end(),
        [this](const RuntimeMultiplayerStateHashFrame& candidate) {
            return candidate.tick == initialStateHashTick_
                && candidate.timelineEpoch
                    == lan_.session().timelineEpoch();
        }
    );
    if (frame == lan_.session().stateHashFrames().end()
        || !frame->complete) {
        errorMessage.clear();
        return true;
    }
    if (frame->desync) {
        return fail(
            errorMessage,
            "multiplayer initial authoritative state hash disagrees"
        );
    }
    initialStateHashVerified_ = true;
    initialLoadDeadlineArmed_ = false;
    ready = true;
    errorMessage.clear();
    return true;
}

std::string
RuntimeMultiplayerRuntimeBridge::initialLoadTimeoutAffectedPeerId() const {
    const RuntimeMultiplayerSessionState& session = lan_.session();
    const int initialTick = session.authoritativeTick() - 1;
    const auto frame = std::find_if(
        session.stateHashFrames().begin(),
        session.stateHashFrames().end(),
        [&session, initialTick](
            const RuntimeMultiplayerStateHashFrame& candidate
        ) {
            return candidate.tick == initialTick
                && candidate.timelineEpoch == session.timelineEpoch();
        }
    );
    const auto reported = [&frame, &session](const std::string& peerId) {
        if (frame == session.stateHashFrames().end()) return false;
        return std::any_of(
            frame->reports.begin(),
            frame->reports.end(),
            [&peerId](const RuntimeMultiplayerStateHashReport& report) {
                return report.peerId == peerId;
            }
        );
    };
    const auto missing = [&session, &reported](bool combatantsOnly) {
        const auto member = std::find_if(
            session.members().begin(),
            session.members().end(),
            [&reported, combatantsOnly](
                const RuntimeMultiplayerRoomMember& candidate
            ) {
                if (combatantsOnly
                    && candidate.participation
                        == RuntimeMultiplayerParticipationRole::Spectator) {
                    return false;
                }
                return !reported(candidate.peerId);
            }
        );
        return member == session.members().end()
            ? std::string{}
            : member->peerId;
    };
    std::string peerId = missing(true);
    if (peerId.empty()) peerId = missing(false);
    return peerId;
}

bool RuntimeMultiplayerRuntimeBridge::refreshTerminalState(
    std::string& errorMessage
) {
    if (!callbacksAttached_) {
        errorMessage.clear();
        return true;
    }
    try {
        matchTerminal_ = callbacks_.matchTerminal();
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("multiplayer terminal-state query threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(
            errorMessage,
            "multiplayer terminal-state query threw"
        );
    }
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerRuntimeBridge::noteFatal(std::string reason) {
    if (fatal()) return;
    initialLoadDeadlineArmed_ = false;
    initialLoadAbortDrainReason_.reset();
    fatalReason_ = reason.empty()
        ? "multiplayer runtime bridge failed"
        : std::move(reason);
    if (preAttachMaintenanceEverStarted_ || runtimeEverAttached_) {
        publishRuntimeMultiplayerVisibleFailure(fatalReason_);
    }
    if (externalOrderedTransport_) externalOrderedTransport_->close();
}

bool RuntimeMultiplayerRuntimeBridge::pumpExternalOrderedTransport(
    RuntimeMultiplayerOrderedTransportPumpStage stage,
    int transportTimeoutMilliseconds,
    std::string& errorMessage
) {
    if (!externalOrderedTransport_) {
        errorMessage.clear();
        return true;
    }
    try {
        return externalOrderedTransport_->pump(
            stage,
            transportTimeoutMilliseconds,
            errorMessage
        );
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("external ordered transport pump threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "external ordered transport pump threw");
    }
}

void RuntimeMultiplayerRuntimeBridge::closeExternalOrderedTransport()
    noexcept {
    if (!externalOrderedTransport_) return;
    try {
        externalOrderedTransport_->close();
    } catch (...) {
    }
    externalOrderedTransport_.reset();
}

bool RuntimeMultiplayerRuntimeBridge::refreshLocalParticipation(
    std::string& errorMessage
) {
    const std::string& localPeerId = lan_.localPeerId();
    if (localPeerId.empty()) {
        return fail(errorMessage, "multiplayer local peer identity is empty");
    }
    const auto found = std::find_if(
        lan_.session().members().begin(),
        lan_.session().members().end(),
        [&localPeerId](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == localPeerId;
        }
    );
    if (found == lan_.session().members().end()) {
        return fail(
            errorMessage,
            "multiplayer local peer is absent from the canonical roster"
        );
    }
    localHost_ = found->role == RuntimeMultiplayerRoomMemberRole::Host;
    localSpectator_ = found->participation
        == RuntimeMultiplayerParticipationRole::Spectator;
    errorMessage.clear();
    return true;
}
