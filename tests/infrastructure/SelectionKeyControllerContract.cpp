#include "ui/input/RuntimeSelectionKeyController.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI) \
    || defined(CALLBACK) || defined(DECLARE_HANDLE)
#error RuntimeSelectionKeyController.h must not transitively include Windows headers
#endif

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same_v<std::underlying_type_t<RuntimeKeyDownSource>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<RuntimeSelectionKeyBinding>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<RuntimeSelectionKeyAction>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<RuntimeSelectionKeyResult>, std::uint8_t>);
static_assert(std::is_same_v<decltype(RuntimeSelectionKeyInput::rawKey), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeSelectionKeyInput::repeated), bool>);
static_assert(std::is_same_v<decltype(RuntimeSelectionKeyInput::source), RuntimeKeyDownSource>);
static_assert(std::is_aggregate_v<RuntimeSelectionKeyInput>);
static_assert(std::is_standard_layout_v<RuntimeSelectionKeyInput>);
static_assert(std::is_trivially_copyable_v<RuntimeSelectionKeyInput>);
static_assert(std::has_virtual_destructor_v<RuntimeSelectionKeyHost>);
static_assert(std::is_abstract_v<RuntimeSelectionKeyHost>);
static_assert(!noexcept(handleRuntimeSelectionKey(
    std::declval<const RuntimeSelectionKeyInput&>(),
    std::declval<RuntimeSelectionKeyHost&>())));
constexpr RuntimeSelectionKeyInput emptyInput{};
static_assert(emptyInput.rawKey == 0 && !emptyInput.repeated);
static_assert(emptyInput.source == RuntimeKeyDownSource::KeyDown);

namespace {

using Binding = RuntimeSelectionKeyBinding;
using Action = RuntimeSelectionKeyAction;
using Result = RuntimeSelectionKeyResult;
using Source = RuntimeKeyDownSource;

std::size_t checks = 0;
std::size_t differentialCases = 0;
std::size_t sourceReads = 0;
std::size_t synchronousDispatches = 0;
std::size_t traceEvents = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        std::cerr << "Selection key controller contract failed: " << message << '\n';
        std::exit(1);
    }
}

// Test-only oracle frozen from the old four-key block in RuntimeShell.cpp,
// before/src/runtime/host/RuntimeShell.cpp SHA256:
// 0CFAE82E80FE1BA63975A1BB3335F1A6DCAE16D111C0CCE0C0D9898425B2168D.
// It retains the old bool commandHandled/if-else flow and its unconditional
// initial observation. It is never linked into the runtime production path.
Result frozenOldAdapter(
    const RuntimeSelectionKeyInput& input,
    RuntimeSelectionKeyHost& host
) {
    const std::int32_t stopKey = host.binding(Binding::Stop, 'S');
    const std::int32_t guardKey = host.binding(Binding::Guard, 'G');
    const std::int32_t scatterKey = host.binding(Binding::Scatter, 'X');
    const std::int32_t deployKey = host.binding(Binding::Deploy, 'D');
    const std::int32_t selectionCode = host.sampleKeyCode(input);
    if (input.repeated
        && (selectionCode == stopKey || selectionCode == guardKey
            || selectionCode == scatterKey || selectionCode == deployKey)) {
        return Result::RepeatedConsumed;
    }
    bool commandHandled = true;
    if (host.sampleKeyCode(input) == stopKey) {
        host.dispatch(Action::Stop);
    } else if (host.sampleKeyCode(input) == guardKey) {
        host.dispatch(Action::Guard);
    } else if (host.sampleKeyCode(input) == scatterKey) {
        host.dispatch(Action::Scatter);
    } else if (host.sampleKeyCode(input) == deployKey) {
        host.dispatch(Action::Deploy);
    } else {
        commandHandled = false;
    }
    return commandHandled ? Result::DispatchedConsumed : Result::Unhandled;
}

enum class TraceKind {
    Enter, LazyBindingModel, BindingQuery, Sample, SinkEnter, RootReplaced,
    SinkExit, Returned, RedrawRequestModel, Leave
};

// Each field is compared, including raw source/repeat, order, fallback,
// returned binding/sample, synchronous sink nesting and root generation.
struct TraceEvent {
    TraceKind kind;
    std::array<std::int64_t, 6> fields;
};

bool equal(const TraceEvent& a, const TraceEvent& b) {
    return a.kind == b.kind && a.fields == b.fields;
}

constexpr std::array<std::int32_t, 4> retailBindings{{'S', 'G', 'X', 'D'}};

struct CaseSpec {
    RuntimeSelectionKeyInput input{};
    std::array<std::int32_t, 4> bindings = retailBindings;
    std::array<std::int32_t, 5> samples{};
    bool useFallback = false;
    bool businessAccepts = true;
    bool replaceRootOnDispatch = false;
    const CaseSpec* nested = nullptr;
};

struct Expected {
    Result result;
    std::size_t samples;
    int action;
    std::size_t totalSinks;
    std::size_t accepted;
    std::size_t maxDepth = 1;
    int rootGeneration = 1;
    std::size_t destroyedRoots = 0;
};

Expected expected(Result result, std::size_t samples, int action, bool accepts) {
    const std::size_t sinks = result == Result::DispatchedConsumed ? 1u : 0u;
    return {result, samples, action, sinks, accepts ? sinks : 0u};
}

struct RootData {
    int generation;
    std::size_t& destructionCount;
    bool loaded = false;

    RootData(int value, std::size_t& count)
        : generation(value), destructionCount(count) {}
    ~RootData() { ++destructionCount; }
};

struct RootState {
    std::size_t destroyed = 0;
    std::unique_ptr<RootData> active = std::make_unique<RootData>(1, destroyed);
};

struct Frame {
    const CaseSpec& spec;
    std::size_t bindingQueries = 0;
    std::size_t samples = 0;
    std::size_t sinks = 0;
    int action = -1;
};

struct Outcome {
    Result result;
    std::size_t bindings;
    std::size_t samples;
    std::size_t sinks;
    int action;
};

class TraceHost final : public RuntimeSelectionKeyHost {
public:
    explicit TraceHost(bool useOldAdapter) : old(useOldAdapter) {}

    Outcome invoke(const CaseSpec& spec) {
        Frame frame{spec};
        frames.push_back(&frame);
        if (frames.size() > maxDepth) maxDepth = frames.size();
        add(TraceKind::Enter, spec.input.rawKey, spec.input.repeated,
            static_cast<int>(spec.input.source), depth(), root.active->generation);
        const Result result = old
            ? frozenOldAdapter(spec.input, *this)
            : handleRuntimeSelectionKey(spec.input, *this);
        add(TraceKind::Returned, static_cast<int>(result), depth(),
            static_cast<std::int64_t>(frame.samples), root.active->generation);
        // This is the old response mapping in a headless harness, not a native
        // RedrawWindow claim. The real WindowProc path has separate integration.
        if (result == Result::DispatchedConsumed) {
            ++redrawRequests;
            add(TraceKind::RedrawRequestModel, depth(), root.active->generation);
        }
        add(TraceKind::Leave, depth(), root.active->generation);
        frames.pop_back();
        return {result, frame.bindingQueries, frame.samples, frame.sinks, frame.action};
    }

    std::int32_t binding(Binding id, std::int32_t fallback) override {
        Frame& frame = *frames.back();
        const auto index = static_cast<std::size_t>(id);
        check(index < frame.spec.bindings.size(), "binding enum domain");
        check(index == frame.bindingQueries, "all four bindings in original order");
        if (!root.active->loaded) {
            root.active->loaded = true;
            add(TraceKind::LazyBindingModel, depth(), root.active->generation);
        }
        ++frame.bindingQueries;
        const std::int32_t code = frame.spec.useFallback
            ? fallback : frame.spec.bindings[index];
        add(TraceKind::BindingQuery, static_cast<std::int64_t>(index), fallback,
            code, depth(), root.active->generation);
        return code;
    }

    std::int32_t sampleKeyCode(const RuntimeSelectionKeyInput& input) override {
        Frame& frame = *frames.back();
        check(frame.bindingQueries == 4, "sampling starts after all four binding queries");
        check(frame.samples < frame.spec.samples.size(), "at most original five samples");
        check(input.rawKey == frame.spec.input.rawKey
            && input.repeated == frame.spec.input.repeated
            && input.source == frame.spec.input.source, "owned input reaches every observation");
        const auto index = frame.samples++;
        ++allSamples;
        const std::int32_t code = frame.spec.samples[index];
        add(TraceKind::Sample, static_cast<std::int64_t>(index + 1), code,
            input.rawKey, input.repeated, static_cast<int>(input.source), depth());
        return code;
    }

    void dispatch(Action action) override {
        Frame& frame = *frames.back();
        const CaseSpec& spec = frame.spec;
        ++frame.sinks;
        frame.action = static_cast<int>(action);
        ++allSinks;
        if (spec.businessAccepts) ++acceptedCommands;
        add(TraceKind::SinkEnter, static_cast<int>(action), spec.businessAccepts,
            depth(), root.active->generation);
        if (spec.replaceRootOnDispatch) {
            const int next = root.active->generation + 1;
            root.active = std::make_unique<RootData>(next, root.destroyed);
            add(TraceKind::RootReplaced, depth(), root.active->generation,
                static_cast<std::int64_t>(root.destroyed));
        }
        // Re-enter synchronously using the same host/root. There is no effect
        // queue or saved child-state reference across this callback.
        if (spec.nested != nullptr) {
            const Outcome nested = invoke(*spec.nested);
            check(nested.result == Result::DispatchedConsumed, "nested sink completed synchronously");
        }
        add(TraceKind::SinkExit, static_cast<int>(action), depth(), root.active->generation);
    }

    bool old;
    RootState root;
    std::vector<TraceEvent> trace;
    std::size_t allSamples = 0;
    std::size_t allSinks = 0;
    std::size_t acceptedCommands = 0;
    std::size_t redrawRequests = 0;
    std::size_t maxDepth = 0;

private:
    std::vector<Frame*> frames;

    std::int64_t depth() const {
        return static_cast<std::int64_t>(frames.size());
    }

    void add(
        TraceKind kind,
        std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0,
        std::int64_t d = 0, std::int64_t e = 0, std::int64_t f = 0
    ) {
        trace.push_back({kind, {{a, b, c, d, e, f}}});
    }
};

void compareTrace(
    const std::vector<TraceEvent>& old,
    const std::vector<TraceEvent>& current,
    const std::string& label
) {
    check(old.size() == current.size(), label + ": complete trace length");
    for (std::size_t i = 0; i < old.size(); ++i) {
        check(equal(old[i], current[i]), label + ": complete trace event " + std::to_string(i));
    }
}

void runCase(const CaseSpec& spec, const Expected& want, const std::string& label) {
    TraceHost old(true);
    TraceHost current(false);
    const Outcome baseline = old.invoke(spec);
    const Outcome actual = current.invoke(spec);
    check(actual.result == baseline.result, label + ": old return");
    compareTrace(old.trace, current.trace, label);
    check(actual.result == want.result, label + ": independent expected return");
    check(actual.bindings == 4, label + ": exactly four bindings even for repeats");
    check(actual.samples == want.samples, label + ": exact original sample count");
    check(actual.action == want.action, label + ": independent action/priority");
    check(actual.sinks == (want.action >= 0 ? 1u : 0u), label + ": immediate outer sink count");
    check(current.allSinks == want.totalSinks, label + ": all synchronous sinks");
    check(current.acceptedCommands == want.accepted, label + ": rejected business does not change consume");
    check(current.redrawRequests == want.totalSinks, label + ": dispatched response requests redraw");
    check(current.maxDepth == want.maxDepth, label + ": synchronous reentry depth");
    check(current.root.active->generation == want.rootGeneration
        && current.root.destroyed == want.destroyedRoots, label + ": root swap model lifetime");
    std::size_t bindingIndex = 0;
    for (const auto& event : current.trace) {
        if (event.kind == TraceKind::BindingQuery && event.fields[3] == 1) {
            check(event.fields[0] == static_cast<std::int64_t>(bindingIndex)
                && event.fields[1] == retailBindings[bindingIndex],
                label + ": independent original fallback and order");
            ++bindingIndex;
        }
    }
    check(bindingIndex == 4, label + ": all original fallback values observed");
    ++differentialCases;
    sourceReads += current.allSamples;
    synchronousDispatches += current.allSinks;
    traceEvents += current.trace.size();
}

CaseSpec stable(
    Source source, std::int32_t raw, std::int32_t code,
    bool repeated, bool accepts
) {
    CaseSpec spec;
    spec.input = {raw, repeated, source};
    spec.samples.fill(code);
    spec.businessAccepts = accepts;
    return spec;
}

void ordinaryAndRebound() {
    for (int source = 0; source < 2; ++source) {
        for (std::int32_t modifiers = 0; modifiers < 8; ++modifiers) {
            const std::int32_t mask = modifiers * 0x100;
            for (int key = 0; key < 5; ++key) {
                const std::int32_t raw = key < 4 ? retailBindings[key] : '?';
                for (int repeated = 0; repeated < 2; ++repeated) {
                    for (int accepted = 0; accepted < 2; ++accepted) {
                        CaseSpec spec = stable(static_cast<Source>(source), raw,
                            raw | mask, repeated != 0, accepted != 0);
                        spec.useFallback = true;
                        const bool match = modifiers == 0 && key < 4;
                        const Result result = !match ? Result::Unhandled
                            : repeated ? Result::RepeatedConsumed : Result::DispatchedConsumed;
                        const std::size_t samples = !match ? 5u
                            : repeated ? 1u : static_cast<std::size_t>(key + 2);
                        runCase(spec, expected(result, samples,
                            match && !repeated ? key : -1, accepted != 0), "retail/modifier");
                    }
                }
            }
            for (int key = 0; key < 4; ++key) {
                for (int repeated = 0; repeated < 2; ++repeated) {
                    for (int accepted = 0; accepted < 2; ++accepted) {
                        CaseSpec spec = stable(static_cast<Source>(source),
                            '1' + key, ('1' + key) | mask, repeated != 0, accepted != 0);
                        for (std::size_t binding = 0; binding < 4; ++binding) {
                            spec.bindings[binding] = ('1' + static_cast<std::int32_t>(binding)) | mask;
                        }
                        runCase(spec, expected(repeated ? Result::RepeatedConsumed : Result::DispatchedConsumed,
                            repeated ? 1u : static_cast<std::size_t>(key + 2),
                            repeated ? -1 : key, accepted != 0), "rebound/modifier");
                    }
                }
            }
        }
    }
}

void collisionsAndChangingObservations() {
    for (int source = 0; source < 2; ++source) {
        for (int repeated = 0; repeated < 2; ++repeated) {
            for (int accepted = 0; accepted < 2; ++accepted) {
                CaseSpec collision = stable(static_cast<Source>(source), 'K',
                    0x700 | 'K', repeated != 0, accepted != 0);
                collision.bindings.fill(0x700 | 'K');
                runCase(collision, expected(repeated ? Result::RepeatedConsumed : Result::DispatchedConsumed,
                    repeated ? 1u : 2u, repeated ? -1 : 0, accepted != 0), "four-binding collision/Stop priority");
                for (int target = 0; target < 4; ++target) {
                    CaseSpec spec = stable(static_cast<Source>(source), '?', 0,
                        repeated != 0, accepted != 0);
                    // Non-repeat initial observation deliberately matches a
                    // different binding, yet must not determine the action.
                    spec.samples[0] = repeated ? 0 : spec.bindings[(target + 1) % 4];
                    for (int comparison = 0; comparison < target; ++comparison) {
                        spec.samples[comparison + 1] = spec.bindings[(comparison + 2) % 4];
                    }
                    spec.samples[target + 1] = spec.bindings[target];
                    runCase(spec, expected(Result::DispatchedConsumed,
                        static_cast<std::size_t>(target + 2), target, accepted != 0),
                        "changing samples/repeated initial miss can dispatch");
                }
                CaseSpec unknown = stable(static_cast<Source>(source), '?', 0,
                    repeated != 0, accepted != 0);
                for (std::size_t comparison = 0; comparison < 4; ++comparison) {
                    unknown.samples[comparison + 1] = unknown.bindings[(comparison + 1) % 4];
                }
                runCase(unknown, expected(Result::Unhandled, 5, -1, accepted != 0),
                    "changing samples/all four comparisons miss");
            }
        }
    }
}

void signedStandardValueAnchors() {
    // These cover the standard interface's comparisons and raw-value transport.
    // They are not a claim that native key-binding validation produces all int32 values.
    constexpr std::array<std::int32_t, 4> anchors{{
        std::numeric_limits<std::int32_t>::min(), -1, 0,
        std::numeric_limits<std::int32_t>::max()
    }};
    for (int source = 0; source < 2; ++source) {
        for (std::int32_t raw : anchors) {
            for (int action = 0; action < 4; ++action) {
                for (int repeated = 0; repeated < 2; ++repeated) {
                    for (int accepted = 0; accepted < 2; ++accepted) {
                        CaseSpec spec = stable(static_cast<Source>(source), raw,
                            anchors[action], repeated != 0, accepted != 0);
                        spec.bindings = anchors;
                        runCase(spec, expected(repeated ? Result::RepeatedConsumed : Result::DispatchedConsumed,
                            repeated ? 1u : static_cast<std::size_t>(action + 2),
                            repeated ? -1 : action, accepted != 0), "signed standard value anchors");
                    }
                }
            }
        }
    }
}

void synchronousNestedRootSwap() {
    for (int source = 0; source < 2; ++source) {
        for (int accepted = 0; accepted < 2; ++accepted) {
            CaseSpec nested = stable(static_cast<Source>(1 - source), 'G', 'G', false, true);
            CaseSpec outer = stable(static_cast<Source>(source), 'X', 'X', false, accepted != 0);
            outer.replaceRootOnDispatch = true;
            outer.nested = &nested;
            Expected want = expected(Result::DispatchedConsumed, 4, 2, accepted != 0);
            want.totalSinks = 2;
            want.accepted = accepted ? 2u : 1u;
            want.maxDepth = 2;
            want.rootGeneration = 2;
            want.destroyedRoots = 1;
            runCase(outer, want, "same host/root synchronous nested call after root replacement");
        }
    }
}

class ThrowingHost final : public RuntimeSelectionKeyHost {
public:
    explicit ThrowingHost(int value) : phase(value) {}
    std::int32_t binding(Binding id, std::int32_t fallback) override {
        events.push_back("binding:" + std::to_string(static_cast<int>(id)));
        if (phase == 0 && id == Binding::Guard) throw std::runtime_error("binding");
        return fallback;
    }
    std::int32_t sampleKeyCode(const RuntimeSelectionKeyInput&) override {
        ++samples;
        events.push_back("sample:" + std::to_string(samples));
        if (phase == 1 && samples == 2) throw std::runtime_error("sample");
        return samples == 1 ? '?' : 'S';
    }
    void dispatch(Action action) override {
        events.push_back("sink:" + std::to_string(static_cast<int>(action)));
        throw std::runtime_error("sink");
    }
    int phase;
    int samples = 0;
    std::vector<std::string> events;
};

void callbackFailurePropagation() {
    const RuntimeSelectionKeyInput input{'S', false, Source::KeyDown};
    for (int phase = 0; phase < 3; ++phase) {
        ThrowingHost old(phase);
        ThrowingHost current(phase);
        std::string before;
        std::string after;
        try { (void)frozenOldAdapter(input, old); }
        catch (const std::runtime_error& error) { before = error.what(); }
        try { (void)handleRuntimeSelectionKey(input, current); }
        catch (const std::runtime_error& error) { after = error.what(); }
        const std::array<std::string, 3> messages{{"binding", "sample", "sink"}};
        check(before == messages[phase] && after == before, "callback exception propagates without new noexcept");
        check(old.events == current.events, "callback exception exact old observation/sink prefix");
        check(current.events.size() == (phase == 0 ? 2u : phase == 1 ? 6u : 7u),
            "callback exception no later work or queued sink");
    }
}

} // namespace

int main() {
    ordinaryAndRebound();
    collisionsAndChangingObservations();
    signedStandardValueAnchors();
    synchronousNestedRootSwap();
    callbackFailurePropagation();
    check(differentialCases == 756, "exact meaningful matrix count");
    check(sourceReads == 2568, "exact original observations including nested calls");
    check(synchronousDispatches == 252, "exact immediate sink calls including reentry");
    check(traceEvents == 9408, "complete expected binding/sample/response/reentry trace");
    std::cout << "Selection key controller contract passed: checks=" << checks
        << " differentialCases=" << differentialCases
        << " sourceReads=" << sourceReads
        << " synchronousDispatches=" << synchronousDispatches
        << " traceEvents=" << traceEvents
        << " modifierCombinations=8 sources=2 retailCases=320 reboundCases=256"
        << " collisionCases=8 changingSampleCases=40 signedValueCases=128"
        << " nestedCases=4 callbackExceptionCases=3 oldAdapterTrace=all-fields-equal"
        << " windowsHeader=absent shellIntegration=not-covered nativeKeyAdapter=not-run"
        << " authorityTick=not-covered rootSwapModel=only\n";
    return 0;
}
