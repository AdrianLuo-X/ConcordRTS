#include "IniFile.h"
#include "RuntimeLightingSystem.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
    }
    return condition;
}

bool nearlyEqual(double left, double right) {
    return std::abs(left - right) <= 1.0e-6;
}

std::int32_t encodedFloat(float value) {
    return std::bit_cast<std::int32_t>(value);
}

MapLighting fixtureLighting() {
    MapLighting lighting;
    lighting.sectionPresent = true;
    lighting.authoredFieldCount = 19;
    lighting.base = {1.0, 1.0, 1.0, 1.0, 0.0, 0.032};
    lighting.ion = {0.4, 0.70, 0.80, 0.90, 0.0, 0.020};
    lighting.dominator = {1.4, 0.81, 0.22, 0.33, 0.06, 0.004};
    lighting.dominatorAmbientChangeRate = 0.0;
    return lighting;
}

} // namespace

int main() {
    bool ok = true;
    const MapLighting lighting = fixtureLighting();

    RuntimeLightingSystemState state(
        lighting,
        RuntimeLightingConfig{10, 0.01, 0.20}
    );
    ok &= expect(
        state.mode() == RuntimeLightingMode::Base
            && nearlyEqual(state.currentProfile().ambient, 1.0),
        "base profile was not the initial lighting"
    );

    // Renderer contract: the stock defaults are an exact identity, while a
    // profile tint changes every RGB channel using one shared operation.
    const RuntimeLightingTint defaultTint = state.renderTint();
    constexpr std::uint32_t sourceColor = 0x006478C8U;
    ok &= expect(
        runtimeLightingTintIsIdentity(defaultTint)
            && runtimeApplyLightingTintToPackedDibColor(
                sourceColor,
                defaultTint
            ) == sourceColor,
        "default lighting changed renderer pixels"
    );
    const RuntimeLightingTint ionTint = runtimeLightingTintForProfile(
        lighting.ion
    );
    ok &= expect(
        runtimeApplyLightingTintToPackedDibColor(sourceColor, ionTint)
            == 0x001C2648U,
        "profile tint did not apply uniformly to packed RGB"
    );

    // Actions 71/72 decode IEEE float bits. Action 73 remains an integer
    // percentage. A nine-tick delay at 10 Hz and .015625 minutes updates on
    // tick 9 (the following fixed tick), matching the original countdown.
    RuntimeLightingTriggerActionResult action = state.applyTriggerAction({
        71,
        encodedFloat(0.20F)
    });
    ok &= expect(
        action.supported && action.accepted
            && nearlyEqual(state.config().ambientChangeStep, 0.20),
        "Action 71 did not decode AmbientChangeStep"
    );
    action = state.applyTriggerAction({72, encodedFloat(0.015625F)});
    ok &= expect(
        action.supported && action.accepted
            && nearlyEqual(state.config().ambientChangeRate, 0.015625),
        "Action 72 did not decode AmbientChangeRate"
    );
    action = state.applyTriggerAction({73, 80});
    ok &= expect(
        action.supported && action.accepted
            && state.hasAmbientTarget()
            && nearlyEqual(state.targetAmbient(), 0.8),
        "Action 73 did not set the percentage ambient target"
    );
    ok &= expect(
        state.tick(0).empty()
            && state.tick(8).empty()
            && nearlyEqual(state.currentProfile().ambient, 1.0),
        "ambient changed before its fixed-tick interval elapsed"
    );
    const std::vector<RuntimeLightingChange> intervalChanges = state.tick(9);
    ok &= expect(
        intervalChanges.size() == 2
            && intervalChanges.front().kind
                == RuntimeLightingChangeKind::AmbientChanged
            && intervalChanges.back().kind
                == RuntimeLightingChangeKind::AmbientTargetReached
            && nearlyEqual(state.currentProfile().ambient, 0.8)
            && state.tick(9).empty(),
        "fixed-tick transition or same-tick idempotence failed"
    );

    // Event 45/46 are edge events. The first observation primes the watcher;
    // only an actual crossing can fire it.
    RuntimeLightingThresholdResult below = state.evaluateThreshold({
        4501,
        45,
        75
    });
    ok &= expect(
        below.valid && !below.hadPreviousSample && !below.triggered,
        "Event 45 fired on its initial sample"
    );
    (void)state.activateIon(10);
    below = state.evaluateThreshold({4501, 45, 75});
    ok &= expect(
        below.hadPreviousSample && below.triggered
            && nearlyEqual(below.previousAmbient, 0.8)
            && nearlyEqual(below.currentAmbient, 0.4),
        "Event 45 did not fire on a downward crossing"
    );
    RuntimeLightingThresholdResult above = state.evaluateThreshold({
        4601,
        46,
        75
    });
    ok &= expect(
        above.valid && !above.triggered,
        "Event 46 fired on its initial sample"
    );
    (void)state.restoreBase(11, false);
    above = state.evaluateThreshold({4601, 46, 75});
    ok &= expect(
        above.hadPreviousSample && above.triggered
            && nearlyEqual(above.previousAmbient, 0.4)
            && nearlyEqual(above.currentAmbient, 1.0),
        "Event 46 did not fire on an upward crossing"
    );

    // Dominator switches the RGB/ground/level target immediately and uses the
    // map-authored ambient rate for its stepped ambient transition.
    state.setAmbientChangeStep(0.20);
    (void)state.tick(11);
    const RuntimeLightingChange dominator = state.activateDominator(12);
    ok &= expect(
        dominator.kind == RuntimeLightingChangeKind::ProfileActivated
            && state.mode() == RuntimeLightingMode::Dominator
            && nearlyEqual(state.currentProfile().red, 0.81)
            && nearlyEqual(state.currentProfile().ambient, 1.0)
            && state.hasAmbientTarget(),
        "Dominator profile target was not activated"
    );
    const std::vector<RuntimeLightingChange> firstDominatorStep = state.tick(12);
    ok &= expect(
        !firstDominatorStep.empty()
            && nearlyEqual(state.currentProfile().ambient, 1.2),
        "Dominator ambient did not advance at the authored rate"
    );

    // Save in the middle of a transition. Watcher history, fixed-tick phase,
    // profile target and the deterministic hash must all round-trip.
    (void)state.evaluateThreshold({4502, 45, 110});
    std::ostringstream saved;
    state.save(saved);
    IniFile saveIni;
    ok &= expect(
        saveIni.loadFromString(saved.str()),
        "lighting save could not be parsed as INI"
    );
    RuntimeLightingSystemState loaded;
    std::string loadError;
    RuntimeLightingReconcileReport report;
    ok &= expect(
        loaded.load(
            saveIni,
            lighting,
            &loadError,
            &report
        ),
        "lighting save did not load: " + loadError
    );
    ok &= expect(
        report.contextValid && report.profilesMatched && !report.stateReset
            && loaded.stateHash() == state.stateHash()
            && loaded.mode() == RuntimeLightingMode::Dominator
            && loaded.hasAmbientTarget(),
        "lighting save/hash did not preserve the transition state"
    );
    const RuntimeLightingThresholdResult restoredWatcher =
        loaded.evaluateThreshold({4502, 45, 110});
    ok &= expect(
        restoredWatcher.hadPreviousSample && !restoredWatcher.triggered,
        "restored Event 45 watcher fired without a new crossing"
    );
    const std::vector<RuntimeLightingChange> originalNext = state.tick(13);
    const std::vector<RuntimeLightingChange> loadedNext = loaded.tick(13);
    ok &= expect(
        originalNext.size() == loadedNext.size()
            && loaded.stateHash() == state.stateHash(),
        "loaded lighting state diverged on the next fixed tick"
    );

    MapLighting changedMap = lighting;
    changedMap.base.ambient = 0.65;
    RuntimeLightingReconcileReport changedReport = loaded.reconcile(changedMap);
    ok &= expect(
        changedReport.contextValid
            && !changedReport.profilesMatched
            && changedReport.stateReset
            && loaded.mode() == RuntimeLightingMode::Base
            && nearlyEqual(loaded.currentProfile().ambient, 0.65),
        "cross-map reconcile retained incompatible lighting state"
    );

    const RuntimeLightingTriggerActionResult unsupported =
        loaded.applyTriggerAction({70, 0});
    ok &= expect(
        !unsupported.supported && !unsupported.accepted,
        "unsupported trigger action was accepted"
    );

    if (!ok) {
        return 1;
    }
    std::cout << "runtime lighting system smoke passed\n";
    return 0;
}
