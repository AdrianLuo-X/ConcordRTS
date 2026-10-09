#include "RuntimeWorldAudioSystem.h"

#include "IniFile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "world audio system smoke failure: " << message << '\n';
    }
    return condition;
}

const RuntimeWorldAudioAction* findAction(
    const std::vector<RuntimeWorldAudioAction>& actions,
    RuntimeWorldAudioActionKind kind,
    std::uint64_t handleId
) {
    for (const RuntimeWorldAudioAction& action : actions) {
        if (action.kind == kind && action.handleId == handleId) {
            return &action;
        }
    }
    return nullptr;
}

bool replaceOnce(
    std::string& text,
    const std::string& from,
    const std::string& to
) {
    const std::size_t offset = text.find(from);
    if (offset == std::string::npos) {
        return false;
    }
    text.replace(offset, from.size(), to);
    return true;
}

RuntimeWorldAudioPlayRequest playRequest(
    const std::string& eventId,
    int waypointId,
    int x,
    int y,
    const std::string& token
) {
    RuntimeWorldAudioPlayRequest request;
    request.eventId = eventId;
    request.waypointId = waypointId;
    request.cell = {x, y, 0};
    request.requestToken = token;
    return request;
}

bool verifyEmptyAudioContext(const RuntimeWorldAudioCatalog& catalog) {
    RuntimeWorldAudioContext full;
    full.catalog = &catalog;
    full.listenerCellValid = true;
    full.listenerCell = {17, 29, 2};
    RuntimeWorldAudioContext minimal;
    minimal.catalog = &catalog;
    RuntimeWorldAudioSystemState reference, optimized;
    const auto statesMatch = [&]() {
        std::ostringstream left, right;
        reference.save(left);
        optimized.save(right);
        return reference.stateHash() == optimized.stateHash()
            && reference.lastAdvancedTick() == optimized.lastAdvancedTick()
            && left.str() == right.str();
    };
    const auto actionKey = [](const RuntimeWorldAudioAction& action) {
        return std::tie(action.kind, action.handleId, action.eventId,
            action.sampleName, action.waypointId, action.cell.x, action.cell.y,
            action.cell.level, action.eventTick, action.playbackOrdinal,
            action.gainPermille, action.frequencyPermille, action.local,
            action.rangeCells, action.backendLoopIterations, action.reason,
            action.authoredGainPermille, action.sourceObjectId, action.trackCamera,
            action.pan16384, action.spatialGain16384, action.queueAfterCurrent);
    };
    const auto actionsMatch = [&](const auto& left, const auto& right) {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(),
                [&](const auto& a, const auto& b) { return actionKey(a) == actionKey(b); });
    };
    bool ok = true;
    for (int tick : {-1, 0, 1, 1, 30, 29, 60}) {
        full.listenerCell.x += 11;
        const auto expected = reference.tick(full, tick);
        const auto actual = optimized.tick(minimal, tick);
        const auto expectedReport = reference.reconcile(full);
        const auto actualReport = optimized.reconcile(minimal);
        ok &= expect(expected.empty() && actual.empty()
                && reference.handles().empty() && optimized.handles().empty()
                && expectedReport.contextValid && actualReport.contextValid
                && expectedReport.retainedHandles == 0 && actualReport.retainedHandles == 0
                && expectedReport.removedHandleIds.empty() && actualReport.removedHandleIds.empty()
                && statesMatch(),
            "catalog-only empty tick changed serialized state, hash or tick progression");
    }
    ok &= expect(reference.lastAdvancedTick() == 60,
        "empty audio tick was skipped instead of advancing its clock");

    std::uint64_t rainHandle = 0, bellHandle = 0;
    for (const auto& request : {
            playRequest("_Amb_LocalRain", 10, 0, 0, "empty-resume/rain"),
            playRequest("GlobalBell", 11, 1000, 1000, "empty-resume/bell")}) {
        const auto expected = reference.playAt(full, request, 61);
        const auto actual = optimized.playAt(full, request, 61);
        ok &= expect(expected.accepted && actual.accepted
                && expected.handleId == actual.handleId
                && actionsMatch(expected.actions, actual.actions) && statesMatch(),
            "playing after an empty interval changed handle IDs or command state");
        if (request.eventId == "GlobalBell") bellHandle = actual.handleId;
        else rainHandle = actual.handleId;
    }
    bool sawLocal = false, sawGlobal = false, sawReplay = false;
    for (int tick = 61; tick <= 100; ++tick) {
        full.listenerCell = {tick * 3, -tick, tick % 3};
        const auto expected = reference.tick(full, tick);
        // This is the host optimization: a listener is omitted only while
        // there are no handles. Playback resumes with the full context.
        const auto actual = optimized.tick(optimized.handles().empty() ? minimal : full, tick);
        ok &= expect(actionsMatch(expected, actual) && statesMatch(),
            "resumed audio gain, sample, playback ordinal or save state diverged");
        for (const auto& action : actual) {
            if (action.handleId == rainHandle) {
                sawLocal = sawLocal || (action.local && action.gainPermille == 600
                    && action.authoredGainPermille == 600 && !action.sampleName.empty());
                sawReplay = sawReplay || (action.kind == RuntimeWorldAudioActionKind::Replay
                    && action.playbackOrdinal > 0);
            }
            if (action.handleId == bellHandle) {
                sawGlobal = !action.local && action.gainPermille == 700
                    && action.authoredGainPermille == 700 && action.playbackOrdinal == 0;
            }
        }
    }
    RuntimeWorldAudioStopRequest stop;
    stop.handleId = rainHandle;
    stop.requestToken = "empty-resume/stop";
    const auto expectedStop = reference.stopAt(full, stop, 101);
    const auto actualStop = optimized.stopAt(full, stop, 101);
    ok &= expect(expectedStop.accepted && actualStop.accepted
            && actionsMatch(expectedStop.actions, actualStop.actions)
            && reference.handles().empty() && optimized.handles().empty(),
        "resumed sound did not return both systems to the empty state");
    const auto expectedEmpty = reference.tick(full, 102);
    const auto actualEmpty = optimized.tick(minimal, 102);
    return expect(sawLocal && sawGlobal && sawReplay
            && expectedEmpty.empty() && actualEmpty.empty() && statesMatch(),
        "catalog-only empty context lost local/global gain or replay sequence") && ok;
}

} // namespace

#include "RuntimeSfxChannelPoolSmoke.inl"
#include "RuntimeWorldAudioVariationSmoke.inl"

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        constexpr int count = 1024;
        std::ostringstream fixture;
        fixture << "[Defaults]\nVolume=80\n[SoundList]\n";
        for (int index = 0; index < count; ++index) {
            fixture << index << "=StressLoop" << index << '\n';
        }
        for (int index = 0; index < count; ++index) {
            fixture << "[StressLoop" << index << "]\nSounds=loop\n"
                << "Control=loop ambient\nLimit=0\nType=LOCAL\n";
        }
        IniFile ini;
        RuntimeWorldAudioCatalog catalog;
        std::string error;
        if (!ini.loadFromString(fixture.str())
            || !catalog.loadFromIni(ini, &error)) {
            std::cerr << "world audio benchmark: " << error << '\n';
            return 2;
        }
        RuntimeWorldAudioContext context;
        context.catalog = &catalog;
        context.listenerCellValid = true;
        RuntimeWorldAudioSystemState state;
        for (int index = 0; index < count; ++index) {
            const auto result = state.playAt(context, playRequest(
                "StressLoop" + std::to_string(index), index,
                index % 128, index / 128, std::to_string(index)), 0);
            if (!result.accepted) {
                return 3;
            }
        }
        std::uint64_t actions = state.tick(context, 0).size();
        const auto start = std::chrono::steady_clock::now();
        constexpr int repetitions = 120;
        for (int tick = 1; tick <= repetitions; ++tick) {
            context.listenerCell.x = tick % 64;
            actions += state.tick(context, tick).size();
        }
        const auto finished = std::chrono::steady_clock::now();
        std::cout << "world audio benchmark handles=" << state.handles().size()
            << " tick_ms=" << std::chrono::duration<double, std::milli>(
                finished - start).count() / repetitions
            << " actions=" << actions
            << " state_hash=" << state.stateHash() << '\n';
        return 0;
    }
    if (argc < 2 || argc > 3) {
        std::cerr
            << "usage: concord_world_audio_system_smoke <fixture.ini> [stock-soundmd.ini]\n";
        return 2;
    }

    IniFile ini;
    if (!ini.loadFromFile(argv[1])) {
        std::cerr << "world audio system smoke failure: fixture load failed\n";
        return 3;
    }
    RuntimeWorldAudioCatalog catalog;
    std::string error;
    std::vector<std::string> warnings;
    if (!catalog.loadFromIni(ini, &error, &warnings)) {
        std::cerr << "world audio system smoke failure: catalog load failed: "
                  << error << '\n';
        return 4;
    }

    bool ok = verifyRuntimeSfxChannelPool() && verifyRuntimeAudioVariation();
    ok &= verifyEmptyAudioContext(catalog);
    // Lookups must keep their case-insensitive semantics and refer into the
    // receiving catalog after copying, moving, successful reload or rejection.
    RuntimeWorldAudioCatalog copiedCatalog = catalog;
    const auto* copiedRain = copiedCatalog.find("_AMB_LOCALRAIN");
    ok &= expect(copiedRain != nullptr
            && copiedRain != catalog.find("_AMB_LOCALRAIN")
            && copiedRain == copiedCatalog.findBySoundListIndex(1)
            && copiedCatalog.find("_AMB_LOCALRAI") == nullptr
            && copiedCatalog.find("_AMB_LOCALRAIN_EXTRA") == nullptr
            && copiedCatalog.findBySoundListIndex(-1) == nullptr
            && copiedCatalog.findBySoundListIndex(99999) == nullptr,
        "catalog value indexes did not survive copying or exact-match lookup");
    RuntimeWorldAudioCatalog movedCatalog = std::move(copiedCatalog);
    IniFile replacementCatalogIni;
    replacementCatalogIni.loadFromString(
        "[Defaults]\nVolume=80\n[SoundList]\n91=ReplacementOnly\n"
        "[ReplacementOnly]\nSounds=replacement\n"
    );
    ok &= expect(movedCatalog.find("_amb_localrain") != nullptr
            && movedCatalog.loadFromIni(replacementCatalogIni)
            && movedCatalog.find("_amb_localrain") == nullptr
            && movedCatalog.findBySoundListIndex(1) == nullptr
            && movedCatalog.find("replacementonly")
                == movedCatalog.findBySoundListIndex(91),
        "catalog reload retained stale event or numeric indexes");
    const auto replacementHash = movedCatalog.stateHash();
    IniFile invalidCatalogIni;
    invalidCatalogIni.loadFromString("[SoundList]\n0=Invalid\n");
    ok &= expect(!movedCatalog.loadFromIni(invalidCatalogIni)
            && movedCatalog.stateHash() == replacementHash
            && movedCatalog.find("REPLACEMENTONLY") != nullptr,
        "rejected catalog load invalidated the previous indexes");
    const RuntimeWorldAudioSoundSpec* rain = catalog.find("_amb_localrain");
    const RuntimeWorldAudioSoundSpec* wind =
        catalog.findBySoundListIndex(2);
    const RuntimeWorldAudioSoundSpec* finite = catalog.find("FinitePulse");
    const RuntimeWorldAudioSoundSpec* bell = catalog.find("GlobalBell");
    const RuntimeWorldAudioSoundSpec* predelayed =
        catalog.find("PredelayedLoop");
    const RuntimeWorldAudioSoundSpec* finiteImmediate =
        catalog.find("FiniteImmediate");
    ok &= expect(
        rain != nullptr
            && rain->ambient
            && rain->loop
            && rain->randomSample
            && rain->local
            && rain->rangeCells == 4.0
            && rain->delayMinMilliseconds == 100
            && rain->delayMaxMilliseconds == 300
            && rain->simultaneousLimit == 2
            && rain->samples.size() == 3,
        "soundmd local/loop/range/delay/limit fields were not parsed"
    );
    ok &= expect(
        wind != nullptr
            && wind->loop
            && wind->ambient
            && wind->local
            && wind->delayMaxMilliseconds == 0,
        "continuous ambient sound fields were not parsed"
    );
    ok &= expect(
        finite != nullptr
            && finite->loopIterations == 3
            && finite->delayMinMilliseconds == 200,
        "finite Loop= value was not parsed"
    );
    ok &= expect(
        bell != nullptr
            && !bell->local
            && bell->volumePercent == 70
            && bell->simultaneousLimit == 0,
        "non-local/global sound fields were not carried"
    );
    ok &= expect(
        predelayed != nullptr && predelayed->predelay,
        "predelay control was not parsed"
    );
    ok &= expect(
        finiteImmediate != nullptr
            && finiteImmediate->loop
            && finiteImmediate->loopIterations == 3
            && finiteImmediate->delayMaxMilliseconds == 0,
        "zero-delay finite loop fields were not parsed"
    );

    // Also prove the narrow parser accepts the shipped soundmd and carries a
    // real Action 99 ambient entry used by the campaign maps.
    if (argc == 3) {
        IniFile stockIni;
        RuntimeWorldAudioCatalog stockCatalog;
        std::vector<std::string> stockWarnings;
        ok &= expect(
            stockIni.loadFromFile(argv[2])
                && stockCatalog.loadFromIni(
                    stockIni,
                    &error,
                    &stockWarnings
                ),
            "stock soundmd did not load"
        );
        const RuntimeWorldAudioSoundSpec* traffic =
            stockCatalog.find("_Amb_Traffic");
        ok &= expect(
            traffic != nullptr
                && traffic->ambient
                && traffic->loop
                && traffic->local
                && traffic->rangeCells > 0.0
                && traffic->delayMinMilliseconds > 0
                && !traffic->samples.empty(),
            "stock _Amb_Traffic semantics were not carried"
        );
    }

    RuntimeWorldAudioContext context;
    context.catalog = &catalog;
    context.listenerCellValid = true;
    context.listenerCell = {0, 0, 0};
    RuntimeWorldAudioSystemState state({10, 8});

    RuntimeWorldAudioCommandResult first = state.playAt(
        context,
        playRequest("_Amb_LocalRain", 10, 0, 0, "trigger-a/action-0"),
        0
    );
    ok &= expect(first.accepted && first.handleId != 0, "first Action 99 failed");
    const std::uint64_t firstHandle = first.handleId;
    const std::uint64_t afterFirstHash = state.stateHash();
    RuntimeWorldAudioCommandResult duplicate = state.playAt(
        context,
        playRequest("_Amb_LocalRain", 10, 0, 0, "trigger-a/action-0"),
        0
    );
    ok &= expect(
        !duplicate.accepted
            && duplicate.duplicate
            && state.stateHash() == afterFirstHash,
        "same-tick retried Action 99 was not idempotent"
    );

    RuntimeWorldAudioCommandResult second = state.playAt(
        context,
        playRequest("_Amb_LocalRain", 11, 4, 0, "trigger-a/action-1"),
        0
    );
    const std::uint64_t secondHandle = second.handleId;
    ok &= expect(second.accepted, "second limited ambient handle failed");
    RuntimeWorldAudioCommandResult limited = state.playAt(
        context,
        playRequest("_Amb_LocalRain", 12, 2, 0, "trigger-a/action-2"),
        0
    );
    ok &= expect(
        !limited.accepted
            && limited.reason.find("Limit") != std::string::npos,
        "soundmd simultaneous Limit was not enforced"
    );
    RuntimeWorldAudioCommandResult global = state.playAt(
        context,
        playRequest("GlobalBell", 99, 1000, 1000, "trigger-a/bell"),
        0
    );
    ok &= expect(global.accepted, "non-local world sound handle failed");

    std::vector<RuntimeWorldAudioAction> actions = state.tick(context, 0);
    const RuntimeWorldAudioAction* firstPlay = findAction(
        actions,
        RuntimeWorldAudioActionKind::Play,
        firstHandle
    );
    const RuntimeWorldAudioAction* secondPlay = findAction(
        actions,
        RuntimeWorldAudioActionKind::Play,
        secondHandle
    );
    const RuntimeWorldAudioAction* bellPlay = findAction(
        actions,
        RuntimeWorldAudioActionKind::Play,
        global.handleId
    );
    ok &= expect(
        firstPlay != nullptr
            && firstPlay->gainPermille == 600
            && !firstPlay->sampleName.empty(),
        "near local sound did not emit its authored gain/sample"
    );
    ok &= expect(
        secondPlay != nullptr && secondPlay->gainPermille == 600,
        "authoritative event gained a camera-dependent volume"
    );
    ok &= expect(
        bellPlay != nullptr && bellPlay->gainPermille == 700,
        "non-local sound was incorrectly distance attenuated"
    );
    ok &= expect(
        state.findHandle(global.handleId) == nullptr,
        "completed one-shot handle was not released"
    );
    const std::uint64_t afterTickHash = state.stateHash();
    ok &= expect(
        state.tick(context, 0).empty()
            && state.stateHash() == afterTickHash,
        "duplicate fixed tick emitted actions or mutated state"
    );

    RuntimeWorldAudioCommandResult continuous = state.playAt(
        context,
        playRequest(
            "_Amb_ContinuousWind",
            20,
            0,
            0,
            "trigger-b/wind"
        ),
        1
    );
    actions = state.tick(context, 1);
    const RuntimeWorldAudioAction* windPlay = findAction(
        actions,
        RuntimeWorldAudioActionKind::Play,
        continuous.handleId
    );
    ok &= expect(
        continuous.accepted
            && windPlay != nullptr
            && windPlay->backendLoopIterations == -1,
        "zero-delay loop did not request one indefinite backend loop"
    );

    auto sameAuthority = state;
    (void)sameAuthority.tick(context, 2);
    context.listenerCell = {100, 100, 0};
    actions = state.tick(context, 2);
    const RuntimeWorldAudioAction* windGain = findAction(
        actions,
        RuntimeWorldAudioActionKind::SetGain,
        continuous.handleId
    );
    ok &= expect(
        windGain == nullptr && state.stateHash() == sameAuthority.stateHash(),
        "listener movement affected serialized world authority"
    );

    // Action 116 targets the authored waypoint.  Only Action 99 handles in
    // this module can match; unrelated world handles and all combat SFX owned
    // by the normal audio system remain outside its stop set.
    RuntimeWorldAudioStopRequest stopWaypoint;
    stopWaypoint.waypointId = 10;
    stopWaypoint.requestToken = "trigger-c/stop-waypoint-10";
    RuntimeWorldAudioCommandResult stopped = state.stopAt(
        context,
        stopWaypoint,
        3
    );
    ok &= expect(
        stopped.accepted
            && stopped.actions.size() == 1
            && stopped.actions.front().kind
                == RuntimeWorldAudioActionKind::Stop
            && stopped.actions.front().handleId == firstHandle
            && state.findHandle(firstHandle) == nullptr
            && state.findHandle(secondHandle) != nullptr
            && state.findHandle(global.handleId) == nullptr
            && state.findHandle(continuous.handleId) != nullptr,
        "Action 116 waypoint stop was not narrowly targeted"
    );
    const std::uint64_t afterStopHash = state.stateHash();
    duplicate = state.stopAt(context, stopWaypoint, 3);
    ok &= expect(
        duplicate.duplicate
            && duplicate.actions.empty()
            && state.stateHash() == afterStopHash,
        "same-tick retried Action 116 was not idempotent"
    );

    RuntimeWorldAudioStopRequest stopCell;
    stopCell.matchCell = true;
    stopCell.cell = {4, 0, 0};
    stopCell.requestToken = "trigger-c/stop-cell-4-0";
    stopped = state.stopAt(context, stopCell, 3);
    ok &= expect(
        stopped.accepted
            && stopped.actions.size() == 1
            && stopped.actions.front().handleId == secondHandle
            && state.findHandle(continuous.handleId) != nullptr,
        "cell-targeted stop affected an unrelated handle"
    );

    // A delayed finite loop is replayed entirely by fixed ticks.  No wall
    // clock or device callback participates in the schedule.
    RuntimeWorldAudioSystemState finiteState({10, 8});
    RuntimeWorldAudioCommandResult finiteStart = finiteState.playAt(
        context,
        playRequest("FinitePulse", 30, 10, 10, "finite/action"),
        10
    );
    int playbackActions = 0;
    actions = finiteState.tick(context, 10);
    playbackActions += static_cast<int>(std::count_if(
        actions.begin(),
        actions.end(),
        [](const RuntimeWorldAudioAction& action) {
            return action.kind == RuntimeWorldAudioActionKind::Play
                || action.kind == RuntimeWorldAudioActionKind::Replay;
        }
    ));
    const RuntimeWorldAudioHandleState* finiteHandle =
        finiteState.findHandle(finiteStart.handleId);
    ok &= expect(
        finiteHandle != nullptr && finiteHandle->nextPlaybackTick == 12,
        "finite loop did not schedule its first replay from Delay"
    );
    actions = finiteState.tick(context, 12);
    playbackActions += static_cast<int>(std::count_if(
        actions.begin(), actions.end(), [](const RuntimeWorldAudioAction& action) {
            return action.kind == RuntimeWorldAudioActionKind::Replay;
        }
    ));
    finiteHandle = finiteState.findHandle(finiteStart.handleId);
    ok &= expect(
        finiteHandle != nullptr && finiteHandle->nextPlaybackTick == 14,
        "finite loop did not schedule its second replay"
    );
    actions = finiteState.tick(context, 14);
    playbackActions += static_cast<int>(std::count_if(
        actions.begin(), actions.end(), [](const RuntimeWorldAudioAction& action) {
            return action.kind == RuntimeWorldAudioActionKind::Replay;
        }
    ));
    finiteHandle = finiteState.findHandle(finiteStart.handleId);
    ok &= expect(
        playbackActions == 3
            && finiteHandle == nullptr
            && finiteState.tick(context, 16).empty(),
        "Loop=3 did not release its handle after three play intents"
    );

    // Even with no authored Delay, finite loops are emitted as one fixed-tick
    // Play followed by fixed-tick Replay actions.  The backend is never asked
    // to implement device-specific counted looping.
    RuntimeWorldAudioSystemState immediateFiniteState({10, 8});
    RuntimeWorldAudioCommandResult immediateFiniteStart =
        immediateFiniteState.playAt(
            context,
            playRequest(
                "FiniteImmediate",
                31,
                10,
                10,
                "finite-immediate/action"
            ),
            30
        );
    int immediatePlaybackActions = 0;
    for (int tick = 30; tick <= 32; ++tick) {
        actions = immediateFiniteState.tick(context, tick);
        for (const RuntimeWorldAudioAction& action : actions) {
            if (action.kind == RuntimeWorldAudioActionKind::Play
                || action.kind == RuntimeWorldAudioActionKind::Replay) {
                ++immediatePlaybackActions;
                ok &= expect(
                    action.backendLoopIterations == 0,
                    "finite loop delegated counted looping to the backend"
                );
            }
        }
    }
    ok &= expect(
        immediateFiniteStart.accepted
            && immediatePlaybackActions == 3
            && immediateFiniteState.findHandle(
                immediateFiniteStart.handleId
            ) == nullptr,
        "zero-delay finite loop was not core-scheduled and released"
    );

    RuntimeWorldAudioSystemState oneShotLimitState({10, 8});
    RuntimeWorldAudioCommandResult oneShotFirst = oneShotLimitState.playAt(
        context,
        playRequest("LimitedOneShot", 32, 0, 0, "oneshot/first"),
        40
    );
    actions = oneShotLimitState.tick(context, 40);
    RuntimeWorldAudioCommandResult oneShotSecond = oneShotLimitState.playAt(
        context,
        playRequest("LimitedOneShot", 33, 0, 0, "oneshot/second"),
        41
    );
    ok &= expect(
        oneShotFirst.accepted
            && findAction(
                actions,
                RuntimeWorldAudioActionKind::Play,
                oneShotFirst.handleId
            ) != nullptr
            && oneShotLimitState.findHandle(oneShotFirst.handleId) == nullptr
            && oneShotSecond.accepted,
        "completed one-shot did not release its simultaneous Limit slot"
    );

    RuntimeWorldAudioSystemState predelayState({10, 8});
    RuntimeWorldAudioCommandResult preStart = predelayState.playAt(
        context,
        playRequest("PredelayedLoop", 40, 0, 0, "predelay/action"),
        20
    );
    const RuntimeWorldAudioHandleState* preHandle =
        predelayState.findHandle(preStart.handleId);
    ok &= expect(
        preHandle != nullptr
            && preHandle->nextPlaybackTick >= 22
            && preHandle->nextPlaybackTick <= 24
            && predelayState.tick(context, 20).empty(),
        "predelay did not defer the first fixed-tick play"
    );
    preHandle = predelayState.findHandle(preStart.handleId);
    if (preHandle != nullptr) {
        const int dueTick = preHandle->nextPlaybackTick;
        actions = predelayState.tick(context, dueTick);
        ok &= expect(
            findAction(
                actions,
                RuntimeWorldAudioActionKind::Play,
                preStart.handleId
            ) != nullptr,
            "predelayed sound did not play on its deterministic due tick"
        );
    }

    // Active location, replay, attenuation and same-tick command state all
    // survive a stable save/load round trip.
    std::ostringstream savedStream;
    state.save(savedStream);
    const std::string saved = savedStream.str();
    IniFile savedIni;
    ok &= expect(savedIni.loadFromString(saved), "saved world audio INI did not parse");
    RuntimeWorldAudioSystemState restored;
    RuntimeWorldAudioReconcileReport report;
    error.clear();
    ok &= expect(
        restored.load(savedIni, context, &error, &report),
        "valid world audio save failed to load: " + error
    );
    ok &= expect(
        restored.stateHash() == state.stateHash()
            && report.contextValid
            && report.removedHandleIds.empty(),
        "world audio save/load changed stable state"
    );
    const std::uint64_t beforeRehydrateHash = restored.stateHash();
    const std::vector<RuntimeWorldAudioAction> rehydrated =
        restored.rehydrateActions(context, 4);
    const RuntimeWorldAudioAction* rehydratedWind = findAction(
        rehydrated,
        RuntimeWorldAudioActionKind::Play,
        continuous.handleId
    );
    ok &= expect(
        rehydrated.size() == 1U
            && rehydratedWind != nullptr
            && rehydratedWind->sampleName == "wind_a"
            && rehydratedWind->backendLoopIterations == -1
            && restored.stateHash() == beforeRehydrateHash,
        "continuous loop did not deterministically rehydrate after load"
    );
    duplicate = restored.stopAt(context, stopCell, 3);
    ok &= expect(
        duplicate.duplicate && duplicate.actions.empty(),
        "saved same-tick command history did not remain idempotent"
    );

    // Catalog reconciliation drops only handles whose authored sound no
    // longer exists and retains a valid unrelated handle.
    IniFile reducedIni;
    ok &= expect(
        reducedIni.loadFromString(
            "[Defaults]\nRange=10\nVolume=80\nLimit=5\nType=NORMAL\n"
            "[SoundList]\n2=_Amb_ContinuousWind\n"
            "[_Amb_ContinuousWind]\nSounds=wind_a\n"
            "Control=loop ambient\nVolume=45\nRange=8\nLimit=3\nType=LOCAL\n"
        ),
        "reduced catalog fixture did not parse"
    );
    RuntimeWorldAudioCatalog reducedCatalog;
    ok &= expect(
        reducedCatalog.loadFromIni(reducedIni, &error, nullptr),
        "reduced world audio catalog failed"
    );
    RuntimeWorldAudioContext reducedContext = context;
    reducedContext.catalog = &reducedCatalog;
    RuntimeWorldAudioSystemState reconcileState({10, 8});
    RuntimeWorldAudioCommandResult reconcileWind = reconcileState.playAt(
        context,
        playRequest("_Amb_ContinuousWind", 60, 0, 0, "reconcile/wind"),
        0
    );
    RuntimeWorldAudioCommandResult reconcileRain = reconcileState.playAt(
        context,
        playRequest("_Amb_LocalRain", 61, 1, 1, "reconcile/rain"),
        0
    );
    (void)reconcileState.tick(context, 0);
    report = reconcileState.reconcile(reducedContext);
    ok &= expect(
        reconcileWind.accepted
            && reconcileRain.accepted
            && report.removedMissingSounds == 1
            && reconcileState.handles().size() == 1
            && reconcileState.handles().front().handleId
                == reconcileWind.handleId,
        "catalog reconciliation removed the wrong world audio handles"
    );

    RuntimeWorldAudioSystemState capped({10, 1});
    ok &= expect(
        capped.playAt(
            context,
            playRequest("GlobalBell", 1, 0, 0, "cap/one"),
            0
        ).accepted
            && !capped.playAt(
                context,
                playRequest("_Amb_ContinuousWind", 2, 0, 0, "cap/two"),
                0
            ).accepted,
        "global world audio handle cap was not enforced"
    );

    // Long missions may issue many more sounds than the simultaneous cap.
    // Completed handles and old command tokens must not consume later slots.
    RuntimeWorldAudioSystemState oneShotSoak({30, 4});
    for (int tick = 0; tick < 12000; ++tick) {
        const auto started = oneShotSoak.playAt(context,
            playRequest("GlobalBell", 1, 0, 0, "soak/" + std::to_string(tick)),
            tick);
        const auto played = oneShotSoak.tick(context, tick);
        if (!started.accepted || played.size() != 1U
            || !oneShotSoak.handles().empty()) {
            ok &= expect(false, "one-shot audio accumulated during long mission");
            break;
        }
    }
    std::ostringstream soakedAudioSave;
    oneShotSoak.save(soakedAudioSave);
    ok &= expect(soakedAudioSave.str().size() < 1024U
            && soakedAudioSave.str().find("CommandCount=1\n") != std::string::npos,
        "world audio retained historical commands after long mission");

    // Strict save limits reject hostile counts without mutating live state.
    std::string oversized = saved;
    ok &= expect(
        replaceOnce(oversized, "HandleCount=1", "HandleCount=5000"),
        "test could not patch saved handle count"
    );
    IniFile oversizedIni;
    ok &= expect(
        oversizedIni.loadFromString(oversized),
        "oversized world audio save did not parse as INI"
    );
    const std::uint64_t beforeRejectedLoad = state.stateHash();
    error.clear();
    ok &= expect(
        !state.load(oversizedIni, context, &error, nullptr)
            && !error.empty()
            && state.stateHash() == beforeRejectedLoad,
        "oversized save was accepted or mutated destination state"
    );

    if (!ok) {
        return 1;
    }
    std::cout << "world audio system smoke passed\n";
    return 0;
}
