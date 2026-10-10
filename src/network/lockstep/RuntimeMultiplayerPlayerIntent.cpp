#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"

#include <exception>
#include <utility>

namespace {

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

template <typename Body>
RuntimeMultiplayerGameplayCommand commandFor(Body body) {
    RuntimeMultiplayerGameplayCommand command;
    command.body = std::move(body);
    return command;
}

RuntimeMultiplayerGameplayCommand placementCommand(
    const RuntimeMultiplayerPlacementIntentState& state
) {
    return commandFor(RuntimeMultiplayerGameplayPlaceBuildingCommand{
        state.lane,
        state.productionSerial,
        state.buildingTypeId,
        state.cell
    });
}

RuntimeMultiplayerGameplayCommand superWeaponCommand(
    const RuntimeMultiplayerSuperWeaponIntentState& state
) {
    return commandFor(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
        state.superWeaponId,
        state.targets
    });
}

bool canonicalizeLocalCommand(
    RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
) {
    return canonicalizeRuntimeMultiplayerGameplayCommand(
        command,
        errorMessage
    );
}

} // namespace

RuntimeMultiplayerPlayerIntentRouter::
RuntimeMultiplayerPlayerIntentRouter(
    RuntimeMultiplayerGameplayCaptureCallback capture
) : capture_(std::move(capture)) {}

void RuntimeMultiplayerPlayerIntentRouter::setCaptureCallback(
    RuntimeMultiplayerGameplayCaptureCallback capture
) {
    capture_ = std::move(capture);
}

void RuntimeMultiplayerPlayerIntentRouter::setAuthorityState(
    RuntimeMultiplayerPlayerIntentAuthorityState state
) {
    authority_ = state;
}

const RuntimeMultiplayerPlayerIntentAuthorityState&
RuntimeMultiplayerPlayerIntentRouter::authorityState() const {
    return authority_;
}

void RuntimeMultiplayerPlayerIntentRouter::setSpectator(bool spectator) {
    authority_.spectator = spectator;
}

void RuntimeMultiplayerPlayerIntentRouter::setTerminal(bool terminal) {
    authority_.terminal = terminal;
}

void RuntimeMultiplayerPlayerIntentRouter::setLocalDefeated(bool defeated) {
    authority_.localDefeated = defeated;
}

bool RuntimeMultiplayerPlayerIntentRouter::
mayIssueAuthoritativeIntent() const {
    return static_cast<bool>(capture_)
        && !authority_.spectator
        && !authority_.terminal
        && !authority_.localDefeated;
}

bool RuntimeMultiplayerPlayerIntentRouter::targetOrder(
    RuntimeMultiplayerGameplayTargetOrderKind order,
    std::vector<std::int32_t> objectIds,
    RuntimeMultiplayerGameplayTarget target,
    std::string& errorMessage,
    bool append
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayTargetOrderCommand{
            order,
            std::move(objectIds),
            std::move(target),
            append
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::follow(
    std::vector<std::int32_t> objectIds, std::int32_t targetId,
    std::string& errorMessage, bool append
) {
    return targetOrder(RuntimeMultiplayerGameplayTargetOrderKind::Follow,
        std::move(objectIds), RuntimeMultiplayerGameplayObjectTarget{targetId},
        errorMessage, append);
}

bool RuntimeMultiplayerPlayerIntentRouter::setPrimaryFactory(
    std::int32_t objectId, std::string& errorMessage
) {
    return captureCommand(commandFor(
        RuntimeMultiplayerGameplaySetPrimaryFactoryCommand{objectId}), errorMessage);
}

bool RuntimeMultiplayerPlayerIntentRouter::setRallyPoint(
    std::int32_t objectId, RuntimeMultiplayerGameplayCell cell,
    std::string& errorMessage
) {
    return captureCommand(commandFor(
        RuntimeMultiplayerGameplaySetRallyPointCommand{objectId, cell}), errorMessage);
}

bool RuntimeMultiplayerPlayerIntentRouter::communicate(
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command, std::string& errorMessage
) {
    return captureCommand(commandFor(std::move(command)), errorMessage);
}

bool RuntimeMultiplayerPlayerIntentRouter::planWaypoints(
    RuntimeMultiplayerGameplayWaypointCommand command, std::string& errorMessage
) {
    return captureCommand(commandFor(command), errorMessage);
}

bool RuntimeMultiplayerPlayerIntentRouter::stop(
    std::vector<std::int32_t> objectIds,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayStopCommand{
            std::move(objectIds)
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::secondaryAction(
    RuntimeMultiplayerGameplaySecondaryActionKind action,
    std::vector<std::int32_t> objectIds,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplaySecondaryActionCommand{
            action,
            std::move(objectIds)
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::queueProduction(
    std::string typeId,
    RuntimeMultiplayerGameplayProductionInsertMode insertMode,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayQueueProductionCommand{
            std::move(typeId),
            insertMode
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::cancelProduction(
    RuntimeMultiplayerGameplayProductionLane lane,
    std::uint64_t productionSerial,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayCancelProductionCommand{
            lane,
            productionSerial
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::sell(
    std::int32_t objectId,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplaySellCommand{objectId}),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::repair(
    std::int32_t objectId,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayRepairCommand{objectId}),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::guard(
    std::vector<std::int32_t> objectIds,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayGuardCommand{
            std::move(objectIds)
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::scatter(
    std::vector<std::int32_t> objectIds,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplayScatterCommand{
            std::move(objectIds)
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::setProductionHold(
    RuntimeMultiplayerGameplayProductionLane lane,
    std::uint64_t activeProductionSerial,
    bool onHold,
    std::string& errorMessage
) {
    return captureCommand(
        commandFor(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            lane,
            activeProductionSerial,
            onHold
        }),
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::holdProduction(
    RuntimeMultiplayerGameplayProductionLane lane,
    std::uint64_t activeProductionSerial,
    std::string& errorMessage
) {
    return setProductionHold(
        lane,
        activeProductionSerial,
        true,
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::resumeProduction(
    RuntimeMultiplayerGameplayProductionLane lane,
    std::uint64_t activeProductionSerial,
    std::string& errorMessage
) {
    return setProductionHold(
        lane,
        activeProductionSerial,
        false,
        errorMessage
    );
}

bool RuntimeMultiplayerPlayerIntentRouter::beginPlacement(
    RuntimeMultiplayerGameplayProductionLane lane,
    std::uint64_t productionSerial,
    std::string buildingTypeId,
    std::string& errorMessage
) {
    RuntimeMultiplayerPlacementIntentState candidate;
    candidate.lane = lane;
    candidate.productionSerial = productionSerial;
    candidate.buildingTypeId = std::move(buildingTypeId);

    RuntimeMultiplayerGameplayCommand command = placementCommand(candidate);
    if (!canonicalizeLocalCommand(command, errorMessage)) return false;

    placement_ = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerPlayerIntentRouter::previewPlacement(
    RuntimeMultiplayerGameplayCell cell,
    bool valid,
    std::string& errorMessage
) {
    if (!placement_.has_value()) {
        return fail(errorMessage, "building placement is not active");
    }

    RuntimeMultiplayerPlacementIntentState candidate = *placement_;
    candidate.cell = cell;
    candidate.previewValid = valid;
    RuntimeMultiplayerGameplayCommand command = placementCommand(candidate);
    if (!canonicalizeLocalCommand(command, errorMessage)) return false;

    placement_ = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerPlayerIntentRouter::confirmPlacement(
    std::string& errorMessage
) {
    if (!placement_.has_value()) {
        return fail(errorMessage, "building placement is not active");
    }
    if (!placement_->previewValid) {
        return fail(errorMessage, "building placement preview is not valid");
    }

    RuntimeMultiplayerGameplayCommand command = placementCommand(*placement_);
    if (!captureCommand(std::move(command), errorMessage)) return false;

    placement_.reset();
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerPlayerIntentRouter::cancelPlacement() {
    placement_.reset();
}

const std::optional<RuntimeMultiplayerPlacementIntentState>&
RuntimeMultiplayerPlayerIntentRouter::placement() const {
    return placement_;
}

bool RuntimeMultiplayerPlayerIntentRouter::beginSuperWeapon(
    std::string superWeaponId,
    std::size_t expectedTargetCount,
    std::string& errorMessage
) {
    if (expectedTargetCount
        > runtimeMultiplayerGameplayMaximumSuperWeaponTargets) {
        return fail(
            errorMessage,
            "superweapon intent target count exceeds the protocol limit"
        );
    }

    RuntimeMultiplayerSuperWeaponIntentState candidate;
    candidate.superWeaponId = std::move(superWeaponId);
    candidate.expectedTargetCount = expectedTargetCount;
    RuntimeMultiplayerGameplayCommand command = superWeaponCommand(candidate);
    if (!canonicalizeLocalCommand(command, errorMessage)) return false;

    superWeapon_ = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerPlayerIntentRouter::targetSuperWeapon(
    RuntimeMultiplayerGameplaySuperWeaponTarget target,
    std::string& errorMessage
) {
    if (!superWeapon_.has_value()) {
        return fail(errorMessage, "superweapon targeting is not active");
    }
    if (superWeapon_->targets.size()
        >= superWeapon_->expectedTargetCount) {
        return fail(
            errorMessage,
            "superweapon target buffer is already complete"
        );
    }

    RuntimeMultiplayerSuperWeaponIntentState candidate = *superWeapon_;
    candidate.targets.push_back(std::move(target));
    RuntimeMultiplayerGameplayCommand command = superWeaponCommand(candidate);
    if (!canonicalizeLocalCommand(command, errorMessage)) return false;

    if (candidate.targets.size() < candidate.expectedTargetCount) {
        superWeapon_ = std::move(candidate);
        errorMessage.clear();
        return true;
    }

    if (captureCommand(std::move(command), errorMessage)) {
        superWeapon_.reset();
        errorMessage.clear();
        return true;
    }

    // Keep the final click so a transiently rejected capture can be retried
    // without asking the player to repeat either targeting stage.
    superWeapon_ = std::move(candidate);
    return false;
}

bool RuntimeMultiplayerPlayerIntentRouter::confirmSuperWeapon(
    std::string& errorMessage
) {
    if (!superWeapon_.has_value()) {
        return fail(errorMessage, "superweapon targeting is not active");
    }
    if (superWeapon_->targets.size()
        != superWeapon_->expectedTargetCount) {
        return fail(errorMessage, "superweapon target buffer is incomplete");
    }

    RuntimeMultiplayerGameplayCommand command =
        superWeaponCommand(*superWeapon_);
    if (!captureCommand(std::move(command), errorMessage)) return false;

    superWeapon_.reset();
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerPlayerIntentRouter::cancelSuperWeapon() {
    superWeapon_.reset();
}

const std::optional<RuntimeMultiplayerSuperWeaponIntentState>&
RuntimeMultiplayerPlayerIntentRouter::superWeapon() const {
    return superWeapon_;
}

void RuntimeMultiplayerPlayerIntentRouter::setLocalCameraCell(
    RuntimeMultiplayerGameplayCell cell
) {
    localCameraCell_ = cell;
}

const RuntimeMultiplayerGameplayCell&
RuntimeMultiplayerPlayerIntentRouter::localCameraCell() const {
    return localCameraCell_;
}

void RuntimeMultiplayerPlayerIntentRouter::setLocalMenuOpen(bool open) {
    localMenuOpen_ = open;
}

bool RuntimeMultiplayerPlayerIntentRouter::localMenuOpen() const {
    return localMenuOpen_;
}

void RuntimeMultiplayerPlayerIntentRouter::setLocalSelection(
    std::vector<std::int32_t> objectIds
) {
    localSelection_ = std::move(objectIds);
}

void RuntimeMultiplayerPlayerIntentRouter::clearLocalSelection() {
    localSelection_.clear();
}

const std::vector<std::int32_t>&
RuntimeMultiplayerPlayerIntentRouter::localSelection() const {
    return localSelection_;
}

bool RuntimeMultiplayerPlayerIntentRouter::captureCommand(
    RuntimeMultiplayerGameplayCommand command,
    std::string& errorMessage
) {
    if (!canonicalizeRuntimeMultiplayerGameplayCommand(
            command,
            errorMessage
        )) {
        return false;
    }
    if (authority_.spectator) {
        return fail(
            errorMessage,
            "spectators may not issue authoritative gameplay intents"
        );
    }
    if (authority_.terminal) {
        return fail(
            errorMessage,
            "terminal matches do not accept authoritative gameplay intents"
        );
    }
    if (authority_.localDefeated) {
        return fail(
            errorMessage,
            "defeated local players may not issue authoritative gameplay intents"
        );
    }
    if (!capture_) {
        return fail(
            errorMessage,
            "multiplayer gameplay capture callback is not installed"
        );
    }

    std::string captureError;
    try {
        if (!capture_(command, captureError)) {
            return fail(
                errorMessage,
                captureError.empty()
                    ? "multiplayer gameplay capture rejected the intent"
                    : std::move(captureError)
            );
        }
    } catch (const std::exception& exception) {
        return fail(
            errorMessage,
            std::string("multiplayer gameplay capture threw: ")
                + exception.what()
        );
    } catch (...) {
        return fail(errorMessage, "multiplayer gameplay capture threw");
    }

    errorMessage.clear();
    return true;
}
