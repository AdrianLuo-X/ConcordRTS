#include "ui/input/RuntimeCameraKeyController.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI) || defined(_WINDEF_)
#error RuntimeCameraKeyController.h leaked a Windows header
#endif

#include "ui/input/RuntimeTeamKeyController.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same<decltype(RuntimeCameraKeyInput{}.rawKey), std::uint64_t>::value,
    "raw input must preserve all Win64 WPARAM bits");
static_assert(std::is_unsigned<decltype(RuntimeCameraKeyInput{}.rawKey)>::value,
    "raw input is unsigned");
static_assert(sizeof(decltype(RuntimeCameraKeyInput{}.rawKey)) == 8, "64-bit raw key");
static_assert(std::is_abstract<RuntimeCameraKeyHost>::value, "narrow synchronous host");
static_assert(!noexcept(handleRuntimeCameraKey(
    std::declval<const RuntimeCameraKeyInput&>(), std::declval<RuntimeCameraKeyHost&>())),
    "the old callback exception boundary must not acquire noexcept");

namespace {
constexpr std::int32_t VK_CLEAR = 0x0c;
constexpr std::int32_t VK_LEFT = 0x25;
constexpr std::int32_t VK_UP = 0x26;
constexpr std::int32_t VK_RIGHT = 0x27;
constexpr std::int32_t VK_DOWN = 0x28;
constexpr unsigned RDW_INVALIDATE = 0x0001;
constexpr unsigned RDW_NOERASE = 0x0020;

enum class Op {
    Enter, Rate, Binding, Follow, Sample, Vision, Applied, CellX, CellY,
    Prepare, Cell, Center, Reset, Scroll, Invalidate, Repaint, Redraw,
    DefProc, Exit, Unwind
};
struct Event {
    Op op;
    int depth;
    std::int64_t a = 0;
    std::int64_t b = 0;
    std::uint64_t raw = 0;
    std::string text;
};
bool operator==(const Event& a, const Event& b) {
    return std::tie(a.op, a.depth, a.a, a.b, a.raw, a.text)
        == std::tie(b.op, b.depth, b.a, b.b, b.raw, b.text);
}

struct Model {
    std::int32_t rate = 3;
    std::int32_t binding = VK_CLEAR;
    std::int32_t sampled = VK_CLEAR;
    bool vision = true;
    bool applied = true;
    RuntimeTeamKeyCell home{5, 7};
    bool follow = true;
    bool initialized = true;
    bool viewportValid = true;
    bool geometryEnabled = true;
    bool cacheValid = true;
    bool repaint = false;
    std::int64_t deltaX = 0;
    std::int64_t deltaY = 0;
    RuntimeTeamKeyCell lastTarget{-777, -777};
    int centerCount = 0;
    int scrollCount = 0;
    int invalidateCount = 0;
    int prepareCount = 0;
    int redrawCount = 0;
    int defCount = 0;
    int generation = 0;
    std::uint64_t authorityTick = 21;
    std::uint64_t authorityHash = 0x123456789abcdef0ULL;
    int commandCount = 3;
    int pendingCount = 0;
};
bool operator==(const Model& a, const Model& b) {
    return std::tie(a.rate, a.binding, a.sampled, a.vision, a.applied,
        a.home.x, a.home.y, a.follow, a.initialized, a.viewportValid,
        a.geometryEnabled, a.cacheValid, a.repaint, a.deltaX, a.deltaY,
        a.lastTarget.x, a.lastTarget.y, a.centerCount, a.scrollCount,
        a.invalidateCount, a.prepareCount, a.redrawCount, a.defCount,
        a.generation, a.authorityTick, a.authorityHash, a.commandCount, a.pendingCount)
        == std::tie(b.rate, b.binding, b.sampled, b.vision, b.applied,
        b.home.x, b.home.y, b.follow, b.initialized, b.viewportValid,
        b.geometryEnabled, b.cacheValid, b.repaint, b.deltaX, b.deltaY,
        b.lastTarget.x, b.lastTarget.y, b.centerCount, b.scrollCount,
        b.invalidateCount, b.prepareCount, b.redrawCount, b.defCount,
        b.generation, b.authorityTick, b.authorityHash, b.commandCount, b.pendingCount);
}

enum class Change {
    None, BindingRate, FollowSample, VisionApplied, AppliedCell,
    XChangesY, YChangesX, PrepareTarget, PrepareRoot, CenterTarget,
    ScrollRoot, NestedPrepare, NestedCenter, NestedScroll, NestedInvalidate,
    ExternalInput
};
struct ProbeException final : std::exception {
    const char* what() const noexcept override { return "scripted synchronous callback"; }
};

class Host final : public RuntimeCameraKeyHost {
public:
    explicit Host(const Model& initial, bool frozen) :
        root(std::make_unique<Model>(initial)), useFrozen(frozen) {}
    std::unique_ptr<Model> root;
    std::vector<Event> trace;
    bool useFrozen;
    Change change = Change::None;
    bool changed = false;
    bool throwEnabled = false;
    Op throwOp = Op::Rate;
    int throwsRemaining = 1;
    int depth = -1;
    RuntimeCameraKeyInput* externalInput = nullptr;

    std::intptr_t invoke(const RuntimeCameraKeyInput& input, std::int32_t message);
    std::size_t observe(Op op, std::int64_t a = 0, std::int64_t b = 0,
        std::uint64_t raw = 0, const std::string& text = {}) {
        const std::size_t index = trace.size();
        trace.push_back(Event{op, depth, a, b, raw, text});
        react(op);
        if (throwEnabled && op == throwOp && throwsRemaining > 0) {
            --throwsRemaining;
            throw ProbeException{};
        }
        return index;
    }
    void react(Op op) {
        if (changed) return;
        bool trigger = false;
        switch (change) {
        case Change::BindingRate: trigger = op == Op::Binding; break;
        case Change::FollowSample: trigger = op == Op::Follow; break;
        case Change::VisionApplied: trigger = op == Op::Vision; break;
        case Change::AppliedCell: trigger = op == Op::Applied; break;
        case Change::XChangesY: trigger = op == Op::CellX; break;
        case Change::YChangesX: trigger = op == Op::CellY; break;
        case Change::PrepareTarget: case Change::PrepareRoot:
        case Change::NestedPrepare: trigger = op == Op::Prepare; break;
        case Change::CenterTarget: case Change::NestedCenter: trigger = op == Op::Center; break;
        case Change::ScrollRoot: case Change::NestedScroll: trigger = op == Op::Scroll; break;
        case Change::NestedInvalidate: trigger = op == Op::Invalidate; break;
        case Change::ExternalInput: trigger = op == Op::Binding; break;
        case Change::None: break;
        }
        if (!trigger) return;
        changed = true;
        switch (change) {
        case Change::BindingRate: root->rate = 6; break;
        case Change::FollowSample: root->sampled = root->binding; break;
        case Change::VisionApplied: root->applied = false; break;
        case Change::AppliedCell: root->home = {-3, 23}; break;
        case Change::XChangesY: root->home.y = -5; break;
        case Change::YChangesX: root->home.x = -9; break;
        case Change::PrepareTarget:
            root->home = {std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()};
            break;
        case Change::PrepareRoot: {
            auto replacement = std::make_unique<Model>(*root);
            replacement->home = {57, 79};
            ++replacement->generation;
            root.swap(replacement);
            break;
        }
        case Change::CenterTarget: root->home = {401, 409}; break;
        case Change::ScrollRoot: {
            auto replacement = std::make_unique<Model>(*root);
            replacement->cacheValid = true;
            ++replacement->generation;
            root.swap(replacement);
            break;
        }
        case Change::NestedPrepare: case Change::NestedCenter:
        case Change::NestedScroll: case Change::NestedInvalidate:
            root->sampled = 0;
            (void)invoke(RuntimeCameraKeyInput{static_cast<std::uint64_t>(VK_DOWN), true}, 0x104);
            break;
        case Change::ExternalInput:
            if (externalInput) {
                externalInput->rawKey = static_cast<std::uint64_t>(VK_RIGHT);
                externalInput->repeated = !externalInput->repeated;
            }
            break;
        case Change::None: break;
        }
    }

    std::int32_t runtimeScrollRate() override {
        const auto i = observe(Op::Rate);
        const auto value = root->rate; trace[i].a = value; return value;
    }
    std::int32_t binding(const std::string& action, std::int32_t fallback) override {
        const auto i = observe(Op::Binding, fallback, 0, 0, action);
        const auto value = root->binding; trace[i].b = value; return value;
    }
    void clearCameraFollow() override {
        root->follow = false; (void)observe(Op::Follow, 0);
    }
    std::int32_t sampleCurrentKeyCode(const RuntimeCameraKeyInput& input) override {
        const auto i = observe(Op::Sample, 0, input.repeated ? 1 : 0, input.rawKey);
        const auto value = root->sampled; trace[i].a = value; return value;
    }
    bool visionEnabled() override {
        const auto i = observe(Op::Vision);
        const bool value = root->vision; trace[i].a = value ? 1 : 0; return value;
    }
    bool skirmishStartApplied() override {
        const auto i = observe(Op::Applied);
        const bool value = root->applied; trace[i].a = value ? 1 : 0; return value;
    }
    std::int32_t skirmishStartCellX() override {
        const auto i = observe(Op::CellX);
        const auto value = root->home.x; trace[i].a = value; return value;
    }
    std::int32_t skirmishStartCellY() override {
        const auto i = observe(Op::CellY);
        const auto value = root->home.y; trace[i].a = value; return value;
    }
    void prepareCenterClient() override {
        ++root->prepareCount;
        (void)observe(Op::Prepare, root->viewportValid ? 1 : 0);
    }
    RuntimeTeamKeyCell skirmishStartCell() override {
        const auto i = observe(Op::Cell);
        const RuntimeTeamKeyCell value = root->home;
        trace[i].a = value.x; trace[i].b = value.y; return value;
    }
    void centerCamera(RuntimeTeamKeyCell cell) override {
        ++root->centerCount; root->lastTarget = cell;
        if (root->viewportValid) root->initialized = true;
        (void)observe(Op::Center, cell.x, cell.y);
    }
    void resetCameraInitialized() override {
        root->initialized = false; (void)observe(Op::Reset, 0);
    }
    void scrollCamera(std::int32_t dx, std::int32_t dy) override {
        ++root->scrollCount;
        if (root->geometryEnabled) { root->deltaX += dx; root->deltaY += dy; }
        (void)observe(Op::Scroll, dx, dy);
    }
    void invalidateProjection() override {
        root->cacheValid = false; ++root->invalidateCount;
        (void)observe(Op::Invalidate);
    }
    void callerRepaint() {
        root->repaint = true; (void)observe(Op::Repaint, 1);
    }
    void callerRedraw(unsigned flags) {
        ++root->redrawCount; (void)observe(Op::Redraw, flags);
    }
    std::intptr_t callerDef(std::int32_t message, std::uint64_t raw, std::int64_t lParam) {
        ++root->defCount; (void)observe(Op::DefProc, message, lParam, raw);
        return 399;
    }
};

// The oracle below retains the complete original consumer including the caller
// epilogue. These field proxies instrument original individual reads/assignments;
// geometry remains a shared opaque sink, never a second camera implementation.
struct LegacyBool {
    Host& host;
    Op op;
    operator bool() const {
        return op == Op::Vision ? host.visionEnabled() : host.skirmishStartApplied();
    }
};
struct LegacyCoordinate {
    Host& host;
    bool x;
    operator std::int32_t() const {
        return x ? host.skirmishStartCellX() : host.skirmishStartCellY();
    }
};
struct LegacyCell {
    Host& host;
    LegacyCoordinate x;
    LegacyCoordinate y;
    explicit LegacyCell(Host& h) : host(h), x{h, true}, y{h, false} {}
    operator RuntimeTeamKeyCell() const { return host.skirmishStartCell(); }
};
struct LegacyWrite {
    Host& host;
    Op op;
    void operator=(bool value) {
        if (op == Op::Follow && !value) host.clearCameraFollow();
        else if (op == Op::Reset && !value) host.resetCameraInitialized();
        else if (op == Op::Repaint && value) host.callerRepaint();
        else throw ProbeException{};
    }
};
struct LegacyState {
    struct Local { LegacyWrite cameraFollow; };
    struct Economy { LegacyBool skirmishStartApplied; LegacyCell skirmishStartCell; };
    struct Camera { LegacyWrite initialized; };
    Local playerLocalCommands;
    LegacyBool visionEnabled;
    Economy economy;
    Camera camera;
    LegacyWrite repaintRequested;
    explicit LegacyState(Host& h) : playerLocalCommands{{h, Op::Follow}},
        visionEnabled{h, Op::Vision}, economy{{h, Op::Applied}, LegacyCell(h)},
        camera{{h, Op::Reset}}, repaintRequested{h, Op::Repaint} {}
};
struct LegacyRect {};

std::intptr_t frozenOldCameraBlock(Host& host, RuntimeCameraKeyInput input,
    std::int32_t message, std::int64_t lParam) {
    LegacyState storage(host);
    LegacyState* state = &storage;
    LegacyState* window = state;
    const std::uint64_t wParam = input.rawKey;
    using RECT = LegacyRect;
    const auto runtimeCameraScrollStep = [&host](const LegacyState&) {
        return 24 + (std::clamp(host.runtimeScrollRate(), 0, 6) * 8);
    };
    const auto runtimeMenuKeyboardBindingCode = [&host](
        LegacyState&, const char* action, int fallback) {
        return host.binding(action, fallback);
    };
    const auto runtimePlayerCurrentKeyCode = [&host, input](std::uint64_t raw) {
        return host.sampleCurrentKeyCode(RuntimeCameraKeyInput{raw, input.repeated});
    };
    const auto GetClientRect = [&host](LegacyState*, RECT*) { host.prepareCenterClient(); };
    const auto centerRuntimeCameraOnCell = [&host](
        LegacyState&, const RECT&, RuntimeTeamKeyCell cell) { host.centerCamera(cell); };
    const auto invalidateRuntimeObjectProjectionCache =
        [&host](LegacyState&) { host.invalidateProjection(); };
    const auto scrollRuntimeMapCameraClamped = [&host](LegacyState&, int dx, int dy) {
        host.scrollCamera(dx, dy);
        return false;
    };
    const auto RedrawWindow = [&host](LegacyState*, std::nullptr_t,
        std::nullptr_t, unsigned flags) { host.callerRedraw(flags); };
    const auto DefWindowProcW = [&host](LegacyState*, std::int32_t msg,
        std::uint64_t raw, std::int64_t flags) { return host.callerDef(msg, raw, flags); };
        const int scrollStep = runtimeCameraScrollStep(*state);
        const int centerViewKey = runtimeMenuKeyboardBindingCode(
            *state,
            "CenterView",
            VK_CLEAR
        );
        bool handled = true;
        if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)
            state->playerLocalCommands.cameraFollow = false;
        if (runtimePlayerCurrentKeyCode(wParam) == centerViewKey) {
            if (state->visionEnabled
                && state->economy.skirmishStartApplied
                && state->economy.skirmishStartCell.x >= 0
                && state->economy.skirmishStartCell.y >= 0) {
                RECT homeClientRect{};
                GetClientRect(window, &homeClientRect);
                centerRuntimeCameraOnCell(
                    *state,
                    homeClientRect,
                    state->economy.skirmishStartCell
                );
            } else {
                state->camera.initialized = false;
            }
            invalidateRuntimeObjectProjectionCache(*state);
        } else {
            switch (wParam) {
            case VK_LEFT:
                scrollRuntimeMapCameraClamped(*state, -scrollStep, 0);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_RIGHT:
                scrollRuntimeMapCameraClamped(*state, scrollStep, 0);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_UP:
                scrollRuntimeMapCameraClamped(*state, 0, -scrollStep);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_DOWN:
                scrollRuntimeMapCameraClamped(*state, 0, scrollStep);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            default:
                handled = false;
                break;
            }
        }
        if (handled) {
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
}

std::intptr_t Host::invoke(const RuntimeCameraKeyInput& input, std::int32_t message) {
    const std::uint64_t raw = input.rawKey;
    const std::int64_t flags = input.repeated ? 0x40000000LL : 0;
    ++depth;
    try {
        (void)observe(Op::Enter, message, input.repeated ? 1 : 0, raw);
        std::intptr_t answer = 0;
        if (useFrozen) {
            answer = frozenOldCameraBlock(*this, RuntimeCameraKeyInput{raw, input.repeated},
                message, flags);
        } else if (handleRuntimeCameraKey(input, *this)) {
            callerRepaint();
            callerRedraw(RDW_INVALIDATE | RDW_NOERASE);
        } else {
            answer = callerDef(message, raw, flags);
        }
        (void)observe(Op::Exit, answer, 0, raw);
        --depth;
        return answer;
    } catch (...) {
        trace.push_back(Event{Op::Unwind, depth, 0, 0, raw, {}});
        --depth;
        throw;
    }
}

struct Stats {
    std::size_t checks = 0;
    std::size_t failures = 0;
    std::size_t cases = 0;
    std::size_t bindings = 0;
    std::size_t samples = 0;
    std::size_t rows = 0;
    std::size_t callbacks = 0;
    std::size_t retail = 0;
    std::size_t collision = 0;
    std::size_t guards = 0;
    std::size_t dynamic = 0;
    std::size_t nested = 0;
    std::size_t exceptions = 0;
    std::size_t owned = 0;
};
void check(Stats& s, bool pass, const char* label) {
    ++s.checks;
    if (!pass) {
        ++s.failures;
        std::cerr << "camera-key contract failed: " << label << '\n';
    }
}
struct Spec {
    Model state;
    RuntimeCameraKeyInput input{static_cast<std::uint64_t>(VK_CLEAR), false};
    std::int32_t message = 0x100;
    Change change = Change::None;
    bool throwEnabled = false;
    Op throwOp = Op::Rate;
};
struct Result {
    Model state;
    std::vector<Event> trace;
    std::intptr_t answer;
    bool threw;
    bool changed;
    RuntimeCameraKeyInput carrier;
    int depth;
};
Result runOne(const Spec& spec, bool frozen) {
    Host host(spec.state, frozen);
    host.change = spec.change;
    host.throwEnabled = spec.throwEnabled;
    host.throwOp = spec.throwOp;
    RuntimeCameraKeyInput carrier = spec.input;
    host.externalInput = &carrier;
    bool threw = false;
    std::intptr_t answer = 0;
    try {
        answer = host.invoke(carrier, spec.message);
    } catch (const ProbeException&) {
        threw = true;
    }
    return Result{*host.root, std::move(host.trace), answer, threw, host.changed,
        carrier, host.depth};
}
std::size_t countOp(const Result& r, Op op, int depth = 0) {
    return static_cast<std::size_t>(std::count_if(r.trace.begin(), r.trace.end(),
        [op, depth](const Event& e) { return e.op == op && e.depth == depth; }));
}
Result pairCase(const Spec& spec, Stats& s) {
    const Result old = runOne(spec, true);
    Result current = runOne(spec, false);
    ++s.cases;
    s.bindings += countOp(old, Op::Binding, 0) + countOp(old, Op::Binding, 1);
    s.samples += countOp(old, Op::Sample, 0) + countOp(old, Op::Sample, 1);
    s.rows += old.trace.size();
    if (spec.change != Change::None || spec.throwEnabled) ++s.callbacks;
    check(s, old.answer == current.answer && old.threw == current.threw,
        "old/new return and propagated callback exception");
    check(s, old.trace == current.trace, "all ordered query/sink/return/reentry trace fields");
    check(s, old.state == current.state, "all local state fields");
    check(s, old.changed == current.changed && old.depth == -1 && current.depth == -1,
        "callback consumption and synchronous stack unwind");
    check(s, old.carrier.rawKey == current.carrier.rawKey
        && old.carrier.repeated == current.carrier.repeated, "external carrier effects");
    check(s, current.state.authorityTick == spec.state.authorityTick
        && current.state.authorityHash == spec.state.authorityHash
        && current.state.commandCount == spec.state.commandCount
        && current.state.pendingCount == spec.state.pendingCount,
        "the local model performs no authority command/tick/hash mutation");
    if (!spec.throwEnabled) {
        check(s, countOp(current, Op::Rate) == 1 && countOp(current, Op::Binding) == 1
            && countOp(current, Op::Sample) == 1,
            "exactly one rate, lazy binding and fresh sample per outer input");
        check(s, (current.answer == 0) == (countOp(current, Op::Repaint) == 1)
            && (current.answer == 0) == (countOp(current, Op::Redraw) == 1),
            "handled alone reaches the unchanged caller repaint/redraw epilogue");
    }
    return current;
}
bool opOrder(const Result& result, const std::vector<Op>& expected) {
    std::vector<Op> actual;
    for (const auto& event : result.trace) {
        if (event.depth == 0) actual.push_back(event.op);
    }
    return actual == expected;
}
std::int32_t signedLow32(std::uint64_t raw) {
    const std::uint32_t low = static_cast<std::uint32_t>(raw & 0xffffffffULL);
    const std::int64_t value = low >= 0x80000000U
        ? static_cast<std::int64_t>(low) - 0x100000000LL
        : static_cast<std::int64_t>(low);
    return static_cast<std::int32_t>(value);
}
void retailMatrix(Stats& s) {
    const std::array<std::uint64_t, 11> keys{{
        static_cast<std::uint64_t>(VK_CLEAR), static_cast<std::uint64_t>(VK_LEFT),
        static_cast<std::uint64_t>(VK_RIGHT), static_cast<std::uint64_t>(VK_UP),
        static_cast<std::uint64_t>(VK_DOWN), 0x51ULL, 0ULL, 0xffffffffULL,
        0x100000025ULL, 0x10000000cULL, 0xffffffffffffffffULL
    }};
    const std::array<std::int32_t, 8> rates{{
        std::numeric_limits<std::int32_t>::min(), -1, 0, 1, 3, 6, 7,
        std::numeric_limits<std::int32_t>::max()
    }};
    for (const auto raw : keys) for (std::int32_t mods = 0; mods < 8; ++mods) {
        for (const auto rate : rates) for (int repeat = 0; repeat < 2; ++repeat) {
            for (const std::int32_t message : {0x100, 0x104}) {
                Spec c;
                c.input = {raw, repeat != 0};
                c.message = message;
                c.state.rate = rate;
                c.state.sampled = signedLow32(raw) | (mods << 8);
                (void)pairCase(c, s);
                ++s.retail;
            }
        }
    }
}
void collisionMatrix(Stats& s) {
    const std::array<std::uint64_t, 5> keys{{0x25, 0x27, 0x26, 0x28, 0x51}};
    for (const auto raw : keys) for (std::int32_t mods = 0; mods < 8; ++mods) {
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (const std::int32_t message : {0x100, 0x104}) {
                Spec c;
                c.input = {raw, repeat != 0};
                c.message = message;
                c.state.sampled = signedLow32(raw) | (mods << 8);
                c.state.binding = c.state.sampled;
                const auto out = pairCase(c, s);
                check(s, out.answer == 0 && out.state.centerCount == 1
                    && out.state.scrollCount == 0 && out.state.prepareCount == 1,
                    "a rebound modified key takes CenterView before raw-arrow scroll");
                check(s, out.state.follow == (raw == 0x51),
                    "raw arrow clears follow even when rebound to CenterView");
                ++s.collision;
            }
        }
    }
    const std::array<std::int32_t, 5> extremeCodes{{
        std::numeric_limits<std::int32_t>::min(), -1, 0, 0x7ffffffe,
        std::numeric_limits<std::int32_t>::max()
    }};
    for (const auto code : extremeCodes) for (const auto raw : keys) {
        Spec c;
        c.input.rawKey = raw;
        c.state.binding = code;
        c.state.sampled = code;
        const auto out = pairCase(c, s);
        check(s, out.state.centerCount == 1, "signed full-width sampled/binding equality");
        ++s.collision;
        c.state.sampled = code == std::numeric_limits<std::int32_t>::max()
            ? std::numeric_limits<std::int32_t>::min() : code + 1;
        const auto miss = pairCase(c, s);
        check(s, miss.state.centerCount == 0
            && miss.answer == (raw == 0x51 ? 399 : 0),
            "signed full-width mismatch retains raw direction or DefWindowProc");
        ++s.collision;
    }
}
void guardMatrix(Stats& s) {
    const std::array<std::int32_t, 4> cells{{
        std::numeric_limits<std::int32_t>::min(), -1, 0,
        std::numeric_limits<std::int32_t>::max()
    }};
    for (int vision = 0; vision < 2; ++vision) for (int applied = 0; applied < 2; ++applied) {
        for (const auto x : cells) for (const auto y : cells) {
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (const std::int32_t message : {0x100, 0x104}) {
                    Spec c;
                    c.state.vision = vision != 0;
                    c.state.applied = applied != 0;
                    c.state.home = {x, y};
                    c.input.repeated = repeat != 0;
                    c.message = message;
                    const auto out = pairCase(c, s);
                    const bool good = vision != 0 && applied != 0 && x >= 0 && y >= 0;
                    check(s, out.answer == 0 && out.state.invalidateCount == 1
                        && out.state.centerCount == (good ? 1 : 0)
                        && out.state.prepareCount == (good ? 1 : 0),
                        "every center guard still consumes and invalidates; viewport query is late");
                    check(s, countOp(out, Op::Applied) == (vision != 0 ? 1U : 0U)
                        && countOp(out, Op::CellX) == (vision != 0 && applied != 0 ? 1U : 0U)
                        && countOp(out, Op::CellY)
                            == (vision != 0 && applied != 0 && x >= 0 ? 1U : 0U),
                        "vision/applied/X/Y are separate original short-circuit reads");
                    ++s.guards;
                }
            }
        }
    }
}

void dynamicCases(Stats& s) {
    const std::array<Change, 11> changes{{
        Change::BindingRate, Change::FollowSample, Change::VisionApplied,
        Change::AppliedCell, Change::XChangesY, Change::YChangesX,
        Change::PrepareTarget, Change::PrepareRoot, Change::CenterTarget,
        Change::ScrollRoot, Change::ExternalInput
    }};
    for (const auto change : changes) for (int repeat = 0; repeat < 2; ++repeat) {
        for (const std::int32_t message : {0x100, 0x104}) {
            Spec c;
            c.change = change;
            c.message = message;
            c.input.repeated = repeat != 0;
            if (change == Change::BindingRate || change == Change::ScrollRoot
                || change == Change::ExternalInput) {
                c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
                c.state.sampled = 0;
            }
            if (change == Change::BindingRate) c.state.rate = 0;
            if (change == Change::FollowSample) {
                c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
                c.state.sampled = 0;
            }
            const auto out = pairCase(c, s);
            check(s, out.changed, "the intended synchronous callback actually occurred");
            if (change == Change::BindingRate) {
                check(s, out.state.deltaX == -24 && out.state.rate == 6,
                    "scroll policy is calculated before binding can change the rate");
            } else if (change == Change::FollowSample) {
                check(s, !out.state.follow && out.state.centerCount == 1
                    && out.state.scrollCount == 0,
                    "raw follow clear precedes a fresh sampled CenterView decision");
            } else if (change == Change::VisionApplied
                || change == Change::AppliedCell || change == Change::XChangesY) {
                check(s, out.state.centerCount == 0 && out.state.prepareCount == 0
                    && !out.state.initialized, "later guards are freshly read and short-circuit");
            } else if (change == Change::YChangesX) {
                check(s, out.state.lastTarget.x == -9 && out.state.centerCount == 1,
                    "a later callback does not retroactively repeat the earlier X guard");
            } else if (change == Change::PrepareTarget) {
                check(s, out.state.lastTarget.x == std::numeric_limits<std::int32_t>::min()
                    && out.state.lastTarget.y == std::numeric_limits<std::int32_t>::max(),
                    "GetClientRect-equivalent query precedes the final fresh owned target");
            } else if (change == Change::PrepareRoot) {
                check(s, out.state.generation == 1 && out.state.lastTarget.x == 57
                    && out.state.lastTarget.y == 79 && !out.state.cacheValid,
                    "the post-viewport target and invalidation use the current root");
                ++s.owned;
            } else if (change == Change::CenterTarget) {
                check(s, out.state.lastTarget.x == 5 && out.state.lastTarget.y == 7
                    && out.state.home.x == 401 && out.state.home.y == 409,
                    "the center sink receives an owned target despite callback mutation");
                ++s.owned;
            } else if (change == Change::ScrollRoot) {
                check(s, out.state.generation == 1 && out.state.deltaX == -48
                    && !out.state.cacheValid,
                    "post-scroll invalidation reborrows after synchronous root replacement");
                ++s.owned;
            } else if (change == Change::ExternalInput) {
                check(s, out.carrier.rawKey == static_cast<std::uint64_t>(VK_RIGHT)
                    && out.state.deltaX == -48 && out.state.deltaY == 0,
                    "the current event retains its original raw value across carrier mutation");
                ++s.owned;
            }
            ++s.dynamic;
        }
    }
}

void nestedCases(Stats& s) {
    const std::array<Change, 4> changes{{
        Change::NestedPrepare, Change::NestedCenter,
        Change::NestedScroll, Change::NestedInvalidate
    }};
    for (const auto change : changes) for (int repeat = 0; repeat < 2; ++repeat) {
        for (const std::int32_t message : {0x100, 0x104}) {
            Spec c;
            c.change = change;
            c.message = message;
            c.input.repeated = repeat != 0;
            if (change == Change::NestedScroll || change == Change::NestedInvalidate) {
                c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
                c.state.sampled = 0;
            }
            const auto out = pairCase(c, s);
            check(s, out.changed && countOp(out, Op::Enter, 1) == 1
                && countOp(out, Op::Exit, 1) == 1 && out.state.deltaY == 48
                && !out.state.cacheValid,
                "a complete nested event finishes synchronously before the outer continuation");
            ++s.nested;
        }
    }
}

void exceptionCases(Stats& s) {
    const std::array<Op, 17> points{{
        Op::Rate, Op::Binding, Op::Follow, Op::Sample, Op::Vision,
        Op::Applied, Op::CellX, Op::CellY, Op::Prepare, Op::Cell,
        Op::Center, Op::Reset, Op::Scroll, Op::Invalidate, Op::Repaint,
        Op::Redraw, Op::DefProc
    }};
    for (const auto point : points) for (const std::int32_t message : {0x100, 0x104}) {
        Spec c;
        c.message = message;
        c.throwEnabled = true;
        c.throwOp = point;
        if (point == Op::Follow) {
            c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
        } else if (point == Op::Scroll || point == Op::Repaint || point == Op::Redraw) {
            c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
            c.state.sampled = 0;
        } else if (point == Op::Reset) {
            c.state.vision = false;
        } else if (point == Op::DefProc) {
            c.input.rawKey = 0x51;
            c.state.sampled = 0;
        }
        const auto out = pairCase(c, s);
        check(s, out.threw && countOp(out, Op::Unwind) == 1,
            "every requested callback exception propagates with the original partial effects");
        ++s.exceptions;
    }
}

void fixedExpectedCases(Stats& s) {
    check(s, RuntimeCameraKeyInput{}.rawKey == 0 && !RuntimeCameraKeyInput{}.repeated,
        "default input values");
    Spec c;
    c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
    c.state.sampled = 0;
    c.state.rate = std::numeric_limits<std::int32_t>::min();
    const auto left = pairCase(c, s);
    check(s, left.state.deltaX == -24 && left.state.deltaY == 0
        && !left.state.follow && left.answer == 0,
        "INT_MIN rate clamps before multiply: left exactly -24");
    c.input.rawKey = static_cast<std::uint64_t>(VK_RIGHT);
    c.state.rate = std::numeric_limits<std::int32_t>::max();
    const auto right = pairCase(c, s);
    check(s, right.state.deltaX == 72 && right.state.deltaY == 0,
        "INT_MAX rate clamps before multiply: right exactly +72");
    c.input.rawKey = static_cast<std::uint64_t>(VK_UP);
    c.state.rate = 2;
    const auto up = pairCase(c, s);
    check(s, up.state.deltaX == 0 && up.state.deltaY == -40,
        "rate2 up exactly -40");
    c.input.rawKey = static_cast<std::uint64_t>(VK_DOWN);
    c.state.rate = 6;
    const auto down = pairCase(c, s);
    check(s, down.state.deltaX == 0 && down.state.deltaY == 72,
        "rate6 down exactly +72");

    c = Spec{};
    c.state.vision = false;
    const auto blind = pairCase(c, s);
    check(s, opOrder(blind, {Op::Enter, Op::Rate, Op::Binding, Op::Sample,
        Op::Vision, Op::Reset, Op::Invalidate, Op::Repaint, Op::Redraw, Op::Exit}),
        "vision-false complete ordered trace excludes every later guard and viewport");
    c.state.vision = true;
    c.state.home = {-1, 99};
    const auto negativeX = pairCase(c, s);
    check(s, opOrder(negativeX, {Op::Enter, Op::Rate, Op::Binding, Op::Sample,
        Op::Vision, Op::Applied, Op::CellX, Op::Reset, Op::Invalidate,
        Op::Repaint, Op::Redraw, Op::Exit}),
        "negative X complete ordered trace excludes Y and viewport");
    c.state.home = {99, -1};
    const auto negativeY = pairCase(c, s);
    check(s, countOp(negativeY, Op::CellY) == 1 && countOp(negativeY, Op::Prepare) == 0,
        "negative Y stops before the only native viewport query");

    c = Spec{};
    c.input.rawKey = 0x100000025ULL;
    c.state.sampled = VK_LEFT;
    const auto highArrow = pairCase(c, s);
    check(s, highArrow.answer == 399 && highArrow.state.follow
        && highArrow.state.scrollCount == 0 && highArrow.state.invalidateCount == 0,
        "upper raw bits do not narrow an unknown WPARAM into Left");
    c.input.rawKey = 0x10000000cULL;
    c.state.sampled = VK_CLEAR;
    const auto highCenter = pairCase(c, s);
    check(s, highCenter.answer == 0 && highCenter.state.follow
        && highCenter.state.centerCount == 1,
        "high raw bits do not suppress a separately sampled CenterView match");
    c = Spec{};
    c.input.rawKey = static_cast<std::uint64_t>(VK_LEFT);
    c.state.binding = VK_LEFT;
    c.state.sampled = VK_LEFT;
    const auto reboundArrow = pairCase(c, s);
    check(s, opOrder(reboundArrow, {Op::Enter, Op::Rate, Op::Binding, Op::Follow,
        Op::Sample, Op::Vision, Op::Applied, Op::CellX, Op::CellY, Op::Prepare,
        Op::Cell, Op::Center, Op::Invalidate, Op::Repaint, Op::Redraw, Op::Exit}),
        "arrow collision full ordered trace includes early follow clear and late owned cell");

    for (int repeat = 0; repeat < 2; ++repeat) {
        c = Spec{};
        c.input.repeated = repeat != 0;
        c.state.viewportValid = false;
        c.state.initialized = false;
        const auto invalidCenter = pairCase(c, s);
        check(s, invalidCenter.answer == 0 && invalidCenter.state.centerCount == 1
            && invalidCenter.state.prepareCount == 1 && !invalidCenter.state.initialized
            && invalidCenter.state.invalidateCount == 1,
            "invalid viewport center still consumes, reaches its sink and invalidates");
        for (const std::uint64_t raw : {0x25ULL, 0x27ULL, 0x26ULL, 0x28ULL}) {
            c.input.rawKey = raw;
            c.state.sampled = 0;
            c.state.geometryEnabled = false;
            const auto stopped = pairCase(c, s);
            check(s, stopped.answer == 0 && stopped.state.scrollCount == 1
                && stopped.state.deltaX == 0 && stopped.state.deltaY == 0
                && stopped.state.invalidateCount == 1 && stopped.state.repaint,
                "an opaque clamp sink that moves nothing still consumes and invalidates");
        }
    }
}
} // namespace

int main() {
    Stats stats;
    try {
        fixedExpectedCases(stats);
        retailMatrix(stats);
        collisionMatrix(stats);
        guardMatrix(stats);
        dynamicCases(stats);
        nestedCases(stats);
        exceptionCases(stats);
    } catch (const std::exception& error) {
        std::cerr << "camera-key unexpected harness exception: " << error.what() << '\n';
        return 1;
    }
    std::cout << "camera-key contract checks=" << stats.checks
        << " differentialCases=" << stats.cases
        << " retailCases=" << stats.retail
        << " collisionCases=" << stats.collision
        << " centerGuardCases=" << stats.guards
        << " dynamicCases=" << stats.dynamic
        << " nestedCases=" << stats.nested
        << " exceptionCases=" << stats.exceptions
        << " bindingQueries=" << stats.bindings
        << " sourceReads=" << stats.samples
        << " orderedTraceRows=" << stats.rows
        << " ownedChecks=" << stats.owned
        << " modifierCombinations=8 sources=2 oldAdapterTrace=all-fields-equal"
        << " windowsHeader=absent shellIntegration=not-covered"
        << " nativeKeyAdapter=not-run authorityTick=not-covered"
        << " geometryClampHeight=not-covered originalOverflowDomain=not-covered"
        << " rootSwapModel=only failures=" << stats.failures << '\n';
    return stats.failures == 0 ? 0 : 1;
}
