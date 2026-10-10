#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr
            << "runtime multiplayer player intent smoke failure: "
            << message << '\n';
    }
};

struct CaptureHarness {
    std::size_t attempts = 0;
    std::size_t failuresRemaining = 0;
    bool throwNext = false;
    std::vector<RuntimeMultiplayerGameplayCommand> commands;

    RuntimeMultiplayerGameplayCaptureCallback callback() {
        return [this](
            const RuntimeMultiplayerGameplayCommand& command,
            std::string& errorMessage
        ) {
            ++attempts;
            if (throwNext) {
                throwNext = false;
                throw std::runtime_error("fixture capture exception");
            }
            if (failuresRemaining != 0U) {
                --failuresRemaining;
                errorMessage = "fixture capture rejection";
                return false;
            }
            commands.push_back(command);
            errorMessage.clear();
            return true;
        };
    }
};

RuntimeMultiplayerGameplayCellTarget cellTarget(
    std::int32_t x,
    std::int32_t y
) {
    return RuntimeMultiplayerGameplayCellTarget{{x, y}};
}

RuntimeMultiplayerGameplaySuperWeaponTarget superWeaponTarget(
    std::int32_t x,
    std::int32_t y,
    std::optional<std::int32_t> objectId = std::nullopt
) {
    return RuntimeMultiplayerGameplaySuperWeaponTarget{
        {x, y},
        objectId
    };
}

bool testAllCommandBuildersAndPlacement(SmokeContext& context) {
    CaptureHarness capture;
    RuntimeMultiplayerPlayerIntentRouter router(capture.callback());
    std::string error;
    bool success = true;

    success &= router.targetOrder(
        RuntimeMultiplayerGameplayTargetOrderKind::Move,
        {9, 1, 9, 4},
        cellTarget(20, 30),
        error
    );
    success &= router.stop({8, 2, 8}, error);
    success &= router.secondaryAction(
        RuntimeMultiplayerGameplaySecondaryActionKind::Contextual,
        {17, 19, 17},
        error
    );
    success &= router.queueProduction(
        "MTNK",
        RuntimeMultiplayerGameplayProductionInsertMode::AfterActive,
        error
    );
    success &= router.cancelProduction(
        RuntimeMultiplayerGameplayProductionLane::Vehicles,
        41,
        error
    );

    const std::size_t beforePlacement = capture.attempts;
    success &= router.beginPlacement(
        RuntimeMultiplayerGameplayProductionLane::Structures,
        42,
        "GAPOWR",
        error
    );
    success &= router.previewPlacement({44, 55}, false, error);
    success &= !router.confirmPlacement(error);
    context.expect(
        capture.attempts == beforePlacement,
        "selection and an invalid placement preview must not capture"
    );
    success &= router.previewPlacement({44, 55}, true, error);
    success &= router.confirmPlacement(error);
    context.expect(
        !router.placement().has_value(),
        "accepted placement capture clears local placement state"
    );

    success &= router.sell(101, error);
    success &= router.repair(102, error);

    const std::size_t beforeSuperWeapon = capture.attempts;
    success &= router.beginSuperWeapon("PsychicRevealSpecial", 0U, error);
    context.expect(
        capture.attempts == beforeSuperWeapon,
        "zero-target superweapon begin remains local-only"
    );
    success &= router.confirmSuperWeapon(error);

    success &= router.guard({12, 3, 12, 6}, error);
    success &= router.scatter({13, 5, 13, 7}, error);
    success &= router.holdProduction(
        RuntimeMultiplayerGameplayProductionLane::Aircraft,
        43,
        error
    );
    success &= router.resumeProduction(
        RuntimeMultiplayerGameplayProductionLane::Aircraft,
        43,
        error
    );

    context.expect(success, "all strongly typed command builders succeed");
    context.expect(
        capture.commands.size() == 13U,
        "twelve command types plus Hold and Resume capture thirteen commands"
    );

    std::set<RuntimeMultiplayerGameplayCommandType> commandTypes;
    for (const RuntimeMultiplayerGameplayCommand& command : capture.commands) {
        std::string validationError;
        context.expect(
            validateRuntimeMultiplayerGameplayCommand(
                command,
                validationError
            ),
            "capture receives only valid canonical commands: "
                + validationError
        );
        commandTypes.insert(runtimeMultiplayerGameplayCommandType(command));
    }
    context.expect(
        commandTypes.size() == 12U,
        "all twelve GameplayCommand variants reach capture"
    );

    if (capture.commands.size() == 13U) {
        const auto* secondary = std::get_if<RuntimeMultiplayerGameplaySecondaryActionCommand>(
            &capture.commands[2].body);
        context.expect(secondary != nullptr
                && secondary->objectIds == std::vector<std::int32_t>({17, 19}),
            "secondary-action builder keeps the full canonical selection in one command");
        const auto* target = std::get_if<
            RuntimeMultiplayerGameplayTargetOrderCommand>(
                &capture.commands[0].body
            );
        context.expect(
            target != nullptr
                && target->objectIds
                    == std::vector<std::int32_t>({1, 4, 9}),
            "target-order builder canonicalizes selected ids"
        );

        const auto* placement = std::get_if<
            RuntimeMultiplayerGameplayPlaceBuildingCommand>(
                &capture.commands[5].body
            );
        context.expect(
            placement != nullptr
                && placement->lane
                    == RuntimeMultiplayerGameplayProductionLane::Structures
                && placement->productionSerial == 42U
                && placement->buildingTypeId == "GAPOWR"
                && placement->cell
                    == RuntimeMultiplayerGameplayCell{44, 55},
            "only final placement confirmation captures the complete body"
        );

        const auto* held = std::get_if<
            RuntimeMultiplayerGameplaySetProductionHoldCommand>(
                &capture.commands[11].body
            );
        const auto* resumed = std::get_if<
            RuntimeMultiplayerGameplaySetProductionHoldCommand>(
                &capture.commands[12].body
            );
        context.expect(
            held != nullptr && held->onHold
                && resumed != nullptr && !resumed->onHold
                && held->activeProductionSerial == 43U
                && resumed->activeProductionSerial == 43U,
            "Hold and Resume both use SetProductionHold"
        );
    }

    const std::size_t attemptsBeforeInvalid = capture.attempts;
    error.clear();
    context.expect(
        !router.stop({}, error)
            && capture.attempts == attemptsBeforeInvalid
            && !error.empty(),
        "builder validation rejects an invalid command before capture"
    );
    return success && context.failures == 0;
}

bool testSuperWeaponTargeting(SmokeContext& context) {
    CaptureHarness capture;
    RuntimeMultiplayerPlayerIntentRouter router(capture.callback());
    std::string error;

    context.expect(
        router.beginSuperWeapon("ChronoSphereSpecial", 2U, error),
        "two-stage superweapon targeting begins: " + error
    );
    context.expect(
        capture.attempts == 0U && capture.commands.empty(),
        "two-stage begin does not capture"
    );
    context.expect(
        router.targetSuperWeapon(superWeaponTarget(10, 20), error),
        "first superweapon target is accepted locally: " + error
    );
    context.expect(
        capture.attempts == 0U
            && router.superWeapon().has_value()
            && router.superWeapon()->targets.size() == 1U,
        "the first two-stage target never captures"
    );
    context.expect(
        router.targetSuperWeapon(
            superWeaponTarget(30, 40, 77),
            error
        ),
        "second superweapon target captures: " + error
    );
    context.expect(
        capture.attempts == 1U
            && capture.commands.size() == 1U
            && !router.superWeapon().has_value(),
        "two completed stages produce exactly one command"
    );
    if (capture.commands.size() == 1U) {
        const auto* fire = std::get_if<
            RuntimeMultiplayerGameplayFireSuperWeaponCommand>(
                &capture.commands.front().body
            );
        context.expect(
            fire != nullptr && fire->targets.size() == 2U
                && fire->targets[0] == superWeaponTarget(10, 20)
                && fire->targets[1] == superWeaponTarget(30, 40, 77),
            "two-stage target order is preserved"
        );
    }

    const std::size_t beforeOneStage = capture.attempts;
    context.expect(
        router.beginSuperWeapon("IronCurtainSpecial", 1U, error)
            && router.targetSuperWeapon(
                superWeaponTarget(50, 60),
                error
            )
            && capture.attempts == beforeOneStage + 1U,
        "one-stage targeting captures on its only target"
    );

    const std::size_t beforeZeroStage = capture.attempts;
    context.expect(
        router.beginSuperWeapon("ForceShieldSpecial", 0U, error)
            && capture.attempts == beforeZeroStage
            && router.confirmSuperWeapon(error)
            && capture.attempts == beforeZeroStage + 1U,
        "zero-stage targeting captures only on explicit confirmation"
    );

    const std::size_t beforeCancel = capture.attempts;
    context.expect(
        router.beginSuperWeapon("ChronoSphereSpecial", 2U, error)
            && router.targetSuperWeapon(
                superWeaponTarget(70, 80),
                error
            ),
        "cancel fixture stages its first target"
    );
    router.cancelSuperWeapon();
    context.expect(
        !router.superWeapon().has_value()
            && capture.attempts == beforeCancel,
        "superweapon cancellation is local-only"
    );
    return context.failures == 0;
}

bool testLocalOnlyAndAuthorityGates(SmokeContext& context) {
    CaptureHarness capture;
    RuntimeMultiplayerPlayerIntentRouter router(capture.callback());
    std::string error;

    router.setLocalCameraCell({91, 92});
    router.setLocalMenuOpen(true);
    router.setLocalSelection({8, 3, 8});
    context.expect(
        router.beginPlacement(
            RuntimeMultiplayerGameplayProductionLane::Structures,
            91,
            "GACNST",
            error
        )
            && router.previewPlacement({12, 13}, true, error),
        "local placement state can be prepared"
    );
    router.cancelPlacement();
    context.expect(
        router.beginSuperWeapon("ChronoSphereSpecial", 2U, error)
            && router.targetSuperWeapon(
                superWeaponTarget(14, 15),
                error
            ),
        "local superweapon stage can be prepared"
    );
    router.cancelSuperWeapon();
    context.expect(
        capture.attempts == 0U
            && router.localCameraCell()
                == RuntimeMultiplayerGameplayCell{91, 92}
            && router.localMenuOpen()
            && router.localSelection()
                == std::vector<std::int32_t>({8, 3, 8}),
        "camera, menu, selection, preview, and cancellation never capture"
    );

    router.setSpectator(true);
    context.expect(
        !router.stop({1}, error)
            && capture.attempts == 0U
            && !router.mayIssueAuthoritativeIntent(),
        "spectator rejects authoritative intent before capture"
    );
    router.setSpectator(false);
    router.setTerminal(true);
    context.expect(
        !router.sell(1, error) && capture.attempts == 0U,
        "terminal match rejects authoritative intent before capture"
    );
    router.setTerminal(false);
    router.setLocalDefeated(true);
    context.expect(
        !router.repair(1, error) && capture.attempts == 0U,
        "defeated local player rejects authoritative intent before capture"
    );
    router.setLocalDefeated(false);
    context.expect(
        router.guard({4, 2}, error)
            && capture.attempts == 1U
            && router.mayIssueAuthoritativeIntent(),
        "combatant can capture again when all gates are clear"
    );

    router.setSpectator(true);
    context.expect(
        router.beginPlacement(
            RuntimeMultiplayerGameplayProductionLane::Structures,
            92,
            "GAPOWR",
            error
        )
            && router.previewPlacement({16, 17}, true, error),
        "placement preparation remains local for a spectator"
    );
    const auto blockedPlacement = router.placement();
    context.expect(
        !router.confirmPlacement(error)
            && router.placement() == blockedPlacement
            && capture.attempts == 1U,
        "blocked placement confirmation preserves retryable local state"
    );
    router.setSpectator(false);
    context.expect(
        router.confirmPlacement(error)
            && !router.placement().has_value()
            && capture.attempts == 2U,
        "preserved placement confirms after the gate clears"
    );
    return context.failures == 0;
}

bool testCaptureFailureRetryAtomicity(SmokeContext& context) {
    CaptureHarness capture;
    RuntimeMultiplayerPlayerIntentRouter router(capture.callback());
    std::string error;

    router.setLocalCameraCell({201, 202});
    router.setLocalMenuOpen(true);
    router.setLocalSelection({19, 11, 19});
    capture.failuresRemaining = 1U;
    context.expect(
        !router.stop({9, 2, 9}, error)
            && capture.commands.empty()
            && router.localCameraCell()
                == RuntimeMultiplayerGameplayCell{201, 202}
            && router.localMenuOpen()
            && router.localSelection()
                == std::vector<std::int32_t>({19, 11, 19}),
        "ordinary capture rejection does not disturb local facade state"
    );
    context.expect(
        router.stop({9, 2, 9}, error)
            && capture.commands.size() == 1U,
        "ordinary command can be retried after capture rejection"
    );

    context.expect(
        router.beginPlacement(
            RuntimeMultiplayerGameplayProductionLane::Structures,
            300,
            "GAREFN",
            error
        )
            && router.previewPlacement({21, 22}, true, error),
        "placement retry fixture is prepared"
    );
    const auto placementBeforeFailure = router.placement();
    capture.failuresRemaining = 1U;
    context.expect(
        !router.confirmPlacement(error)
            && router.placement() == placementBeforeFailure
            && capture.commands.size() == 1U,
        "failed placement capture preserves the exact final preview"
    );
    context.expect(
        router.confirmPlacement(error)
            && !router.placement().has_value()
            && capture.commands.size() == 2U,
        "failed placement capture retries without rebuilding local state"
    );

    context.expect(
        router.beginSuperWeapon("ChronoSphereSpecial", 2U, error)
            && router.targetSuperWeapon(
                superWeaponTarget(31, 32),
                error
            ),
        "superweapon retry fixture stages its first target"
    );
    capture.failuresRemaining = 1U;
    context.expect(
        !router.targetSuperWeapon(
            superWeaponTarget(33, 34, 88),
            error
        )
            && router.superWeapon().has_value()
            && router.superWeapon()->targets.size() == 2U
            && router.superWeapon()->targets[1]
                == superWeaponTarget(33, 34, 88)
            && capture.commands.size() == 2U,
        "failed final-stage capture retains both retryable targets"
    );
    const auto completeSuperWeapon = router.superWeapon();
    context.expect(
        router.confirmSuperWeapon(error)
            && !router.superWeapon().has_value()
            && capture.commands.size() == 3U,
        "complete superweapon buffer retries as one command"
    );
    if (capture.commands.size() == 3U
        && completeSuperWeapon.has_value()) {
        const auto* fire = std::get_if<
            RuntimeMultiplayerGameplayFireSuperWeaponCommand>(
                &capture.commands.back().body
            );
        context.expect(
            fire != nullptr
                && fire->superWeaponId
                    == completeSuperWeapon->superWeaponId
                && fire->targets == completeSuperWeapon->targets,
            "superweapon retry submits the preserved final buffer verbatim"
        );
    }

    context.expect(
        router.beginPlacement(
            RuntimeMultiplayerGameplayProductionLane::Structures,
            301,
            "GAPOWR",
            error
        )
            && router.previewPlacement({41, 42}, true, error),
        "throwing callback fixture is prepared"
    );
    const auto placementBeforeThrow = router.placement();
    capture.throwNext = true;
    context.expect(
        !router.confirmPlacement(error)
            && router.placement() == placementBeforeThrow
            && error.find("threw") != std::string::npos,
        "capture exceptions are rejected without losing placement state"
    );
    context.expect(
        router.confirmPlacement(error),
        "placement retries after a capture exception"
    );
    return context.failures == 0;
}

bool testPlayerPlanningAndFactoryBuilders(SmokeContext& context) {
    CaptureHarness capture;
    RuntimeMultiplayerPlayerIntentRouter router(capture.callback());
    std::string error;
    context.expect(router.follow({9, 3, 9}, 12, error, true), "follow captures queued intent");
    context.expect(router.setPrimaryFactory(15, error), "primary factory captures intent");
    context.expect(router.setRallyPoint(15, {23, 24}, error), "rally point captures intent");
    context.expect(capture.commands.size() == 3, "new actions each emit one authoritative command");
    if (capture.commands.size() == 3) {
        const auto* follow = std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&capture.commands[0].body);
        context.expect(follow && follow->append
            && follow->order == RuntimeMultiplayerGameplayTargetOrderKind::Follow
            && follow->objectIds == std::vector<std::int32_t>({3, 9}),
            "follow preserves append and canonicalizes selected actors");
    }
    router.setSpectator(true);
    context.expect(!router.setPrimaryFactory(15, error)
        && !router.setRallyPoint(15, {1, 2}, error)
        && !router.follow({3}, 12, error) && capture.commands.size() == 3,
        "spectators cannot mutate factory or follower authority");
    router.setSpectator(false);
    context.expect(!router.setRallyPoint(15, {-1, 24}, error)
        && !router.setPrimaryFactory(0, error) && capture.commands.size() == 3,
        "invalid factory intents fail before capture");
    return context.failures == 0;
}

void testCommunicationAuthority(SmokeContext& context) {
    int captures=0; bool accept=false;
    RuntimeMultiplayerPlayerIntentRouter router([&](const auto&,std::string& error) {
        ++captures; error=accept ? "" : "queue full"; return accept;
    });
    std::string error;
    RuntimeMultiplayerGameplayPlayerCommunicationCommand message;
    message.text="hello";
    context.expect(!router.communicate(message,error) && captures==1,"chat capture failure is reported");
    accept=true;
    context.expect(router.communicate(message,error) && captures==2,"chat capture can retry");
    router.setSpectator(true);
    context.expect(!router.communicate(message,error) && captures==2,"spectator cannot bypass authority using chat");
    router.setSpectator(false); router.setLocalDefeated(true);
    context.expect(!router.communicate(message,error) && captures==2,"defeated participant cannot issue communication frames");
    router.setLocalDefeated(false); router.setTerminal(true);
    context.expect(!router.communicate(message,error) && captures==2,"terminal input is rejected");
}

} // namespace

int main() {
    SmokeContext context;
    testAllCommandBuildersAndPlacement(context);
    testSuperWeaponTargeting(context);
    testLocalOnlyAndAuthorityGates(context);
    testCaptureFailureRetryAtomicity(context);
    testPlayerPlanningAndFactoryBuilders(context);
    testCommunicationAuthority(context);
    if (context.failures != 0) return 1;

    std::cout
        << "Runtime multiplayer player intent smoke: commands=15"
        << " placement=local-until-confirm"
        << " superweapon=0/1/2-stage"
        << " authority-gates=yes retry-atomic=yes\n";
    return 0;
}
