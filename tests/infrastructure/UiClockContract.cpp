#include "ui/time/RuntimeUiClock.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error UI clock contract header must not transitively include windows.h
#endif

#include "ui/input/RuntimePlayerDefeatNotice.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<RuntimeUiTimeMs, std::uint64_t>);
static_assert(std::is_unsigned_v<RuntimeUiTimeMs>
    && std::numeric_limits<RuntimeUiTimeMs>::digits == 64);
static_assert(std::is_abstract_v<RuntimeUiClock> && std::has_virtual_destructor_v<RuntimeUiClock>);
static_assert(std::is_nothrow_destructible_v<RuntimeUiClock>);
static_assert(std::is_same_v<decltype(std::declval<const RuntimeUiClock&>().nowMs()), RuntimeUiTimeMs>);
static_assert(noexcept(std::declval<const RuntimeUiClock&>().nowMs()));
static_assert(noexcept(runtimeUiWithinDoubleSelection(0, 0)));
static_assert(noexcept(runtimeUiHoverDelayElapsed(0, 0, 0)));
static_assert(noexcept(runtimeUiBannerElapsed(0, 0, 0)));
static_assert(noexcept(runtimeUiReadyBlinkPeriod(0)));
static_assert(noexcept(runtimeUiReadyBlinkOn(0)));
static_assert(noexcept(runtimeUiScoreElapsed(0, 0, 0)));
static_assert(runtimeUiWithinDoubleSelection(500, 0));
static_assert(!runtimeUiWithinDoubleSelection(501, 0));
static_assert(runtimeUiHoverDelayElapsed(999, 1000, 3000));
static_assert(!runtimeUiBannerElapsed(999, 1000, 0));
static_assert(runtimeUiReadyBlinkPeriod(1000) == 2 && runtimeUiReadyBlinkOn(1000));
static_assert(runtimeUiScoreElapsed(0, 0, 2000) == 2000);
static_assert(runtimeUiScoreElapsed(0, 0, 3000) == 3000);

namespace {
std::size_t checks = 0;
bool expect(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "UI clock contract failed: " << message << '\n';
    return condition;
}

// Local test dependency, never an ambient/global override. Its values pass
// through the same virtual interface and real production policy helpers.
class ControlledUiClock final : public RuntimeUiClock {
public:
    explicit ControlledUiClock(RuntimeUiTimeMs value = 0) noexcept : value_(value) {}
    RuntimeUiTimeMs nowMs() const noexcept override { ++reads_; return value_; }
    void set(RuntimeUiTimeMs value) noexcept { value_ = value; }
    std::size_t reads() const noexcept { return reads_; }
private:
    RuntimeUiTimeMs value_ = 0;
    mutable std::size_t reads_ = 0;
};
constexpr auto maximum = std::numeric_limits<RuntimeUiTimeMs>::max();
struct DoubleCase { RuntimeUiTimeMs now, last; bool expected; };
struct DelayCase { RuntimeUiTimeMs now, start, delay; bool expected; };
struct ScoreCase { RuntimeUiTimeMs now, start, fallback, expected; };
struct BlinkCase { RuntimeUiTimeMs now, period; bool on; };
struct DeadlineCase { RuntimeUiTimeMs now, expected; };
struct FrameCase { RuntimeUiTimeMs now, start, fallback, expectedTick; };
} // namespace

int main() {
    bool ok = true;
    ControlledUiClock controlled;
    const RuntimeUiClock& source = controlled;
    ok &= expect(source.nowMs() == 0, "local source defaults to zero");
    ok &= expect(controlled.reads() == 1, "virtual const read is observable without a global override");

    constexpr std::array<DoubleCase, 12> doubleCases{{
        {0, 0, true}, {499, 0, true}, {500, 0, true}, {501, 0, false},
        {1000, 500, true}, {1001, 500, false}, {999, 1000, false},
        {maximum, 0, false}, {0, maximum, true}, {499, maximum, true},
        {500, maximum, false}, {maximum, maximum, true}
    }};
    auto beforeReads = controlled.reads();
    for (const auto& c : doubleCases) {
        controlled.set(c.now);
        ok &= expect(runtimeUiWithinDoubleSelection(source.nowMs(), c.last) == c.expected,
            "500ms selection boundary/rollback/wrap");
    }
    ok &= expect(controlled.reads() - beforeReads == doubleCases.size(), "one source sample per selection fixture");

    constexpr std::array<DelayCase, 16> hoverCases{{
        {0, 0, 0, true}, {0, 0, 1000, false}, {999, 0, 1000, false},
        {1000, 0, 1000, true}, {1001, 0, 1000, true}, {2999, 0, 3000, false},
        {3000, 0, 3000, true}, {3001, 0, 3000, true}, {999, 1000, 1000, true},
        {0, maximum, 1, true}, {0, maximum, 2, false}, {maximum, 0, maximum, true},
        {maximum - 1, 0, maximum, false}, {0, 0, maximum, false},
        {1, maximum, 2, true}, {maximum, maximum, maximum, false}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : hoverCases) {
        controlled.set(c.now);
        ok &= expect(runtimeUiHoverDelayElapsed(source.nowMs(), c.start, c.delay) == c.expected,
            "1000/3000ms hover keeps unsigned rollback/wrap policy");
    }
    ok &= expect(controlled.reads() - beforeReads == hoverCases.size(), "one source sample per hover fixture");

    constexpr std::array<DelayCase, 14> bannerCases{{
        {0, 0, 0, true}, {0, 0, 1, false}, {5999, 0, 6000, false},
        {6000, 0, 6000, true}, {6001, 0, 6000, true}, {999, 1000, 0, false},
        {999, 1000, 6000, false}, {maximum, 0, maximum, true},
        {maximum - 1, 0, maximum, false}, {0, maximum, 0, false},
        {maximum, maximum, 0, true}, {maximum, maximum, 1, false},
        {6005, 5, 6000, true}, {6004, 5, 6000, false}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : bannerCases) {
        controlled.set(c.now);
        ok &= expect(runtimeUiBannerElapsed(source.nowMs(), c.start, c.delay) == c.expected,
            "banner retains ordered-time guard and exact duration");
    }
    ok &= expect(controlled.reads() - beforeReads == bannerCases.size(), "one source sample per banner fixture");

    constexpr std::array<ScoreCase, 12> scoreCases{{
        {0, 0, 2000, 2000}, {0, 0, 3000, 3000}, {maximum, 0, 3000, 3000},
        {0, 1, 2000, 2000}, {999, 1000, 3000, 3000}, {1000, 1000, 3000, 0},
        {1600, 1000, 3000, 600}, {maximum, 1, 3000, maximum - 1},
        {maximum, maximum, 3000, 0}, {1, maximum, 3000, 3000},
        {6000, 0, 2000, 2000}, {6001, 1, 3000, 6000}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : scoreCases) {
        controlled.set(c.now);
        ok &= expect(runtimeUiScoreElapsed(source.nowMs(), c.start, c.fallback) == c.expected,
            "score preserves zero sentinel and separate 2000/3000 fallbacks");
    }
    ok &= expect(controlled.reads() - beforeReads == scoreCases.size(), "one source sample per score fixture");

    constexpr std::array<BlinkCase, 11> blinkCases{{
        {0, 0, true}, {1, 0, true}, {499, 0, true}, {500, 1, false},
        {501, 1, false}, {999, 1, false}, {1000, 2, true}, {1001, 2, true},
        {1499, 2, true}, {1500, 3, false}, {maximum, 36893488147419103ULL, false}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : blinkCases) {
        controlled.set(c.now);
        const auto now = source.nowMs();
        ok &= expect(runtimeUiReadyBlinkPeriod(now) == c.period, "500ms blink period");
        ok &= expect(runtimeUiReadyBlinkOn(now) == c.on, "500ms blink phase");
    }
    ok &= expect(controlled.reads() - beforeReads == blinkCases.size(), "period and phase share a fixture sample");

    constexpr std::array<DeadlineCase, 6> deadlineCases{{
        {0, 15000}, {1, 15001}, {maximum - 15001, maximum - 1},
        {maximum - 15000, maximum}, {maximum - 14999, maximum}, {maximum, maximum}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : deadlineCases) {
        controlled.set(c.now);
        ok &= expect(runtimePlayerDefeatNoticeDeadline(source.nowMs()) == c.expected,
            "actual W001 notice helper retains 15000ms saturating deadline");
    }
    ok &= expect(controlled.reads() - beforeReads == deadlineCases.size(), "one source sample per notice deadline");

    // Frozen original expressions, independently compared against the real helper.
    // All uint64 subtraction/comparison/division inputs are defined, including wrap.
    constexpr std::array<RuntimeUiTimeMs, 15> values{
        0, 1, 499, 500, 501, 999, 1000, 1001, 2999, 3000, 3001, 6000,
        maximum - 15000, maximum - 1, maximum
    };
    constexpr std::array<RuntimeUiTimeMs, 5> delays{0, 1, 1000, 3000, maximum};
    std::size_t differentialCases = 0;
    beforeReads = controlled.reads();
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            controlled.set(values[i]);
            const auto now = source.nowMs();
            const auto start = values[j];
            const auto delay = delays[(i + j) % delays.size()];
            const RuntimeUiTimeMs fallback = i % 2 == 0 ? 2000 : 3000;
            ok &= expect(runtimeUiWithinDoubleSelection(now, start) == (now - start <= 500),
                "frozen original selection expression");
            ok &= expect(runtimeUiHoverDelayElapsed(now, start, delay) == (now - start >= delay),
                "frozen original hover expression");
            ok &= expect(runtimeUiBannerElapsed(now, start, delay) == (now >= start && now - start >= delay),
                "frozen original banner expression");
            ok &= expect(runtimeUiReadyBlinkPeriod(now) == now / 500,
                "frozen original blink-period expression");
            ok &= expect(runtimeUiReadyBlinkOn(now) == ((now / 500) % 2 == 0),
                "frozen original blink-phase expression");
            ok &= expect(runtimeUiScoreElapsed(now, start, fallback)
                == (start == 0 || now < start ? fallback : now - start),
                "frozen original score elapsed expression");
            ++differentialCases;
        }
    }
    ok &= expect(controlled.reads() - beforeReads == differentialCases, "policy comparison fixtures use one shared value");

    // Only the elapsed production helper is under test here. The unchanged caller
    // frame expression is exercised with explicit expected uint64 modulo results;
    // actual Shell painters, read order, load/scratch, and graph narrowing are
    // deliberately left to the host integration, never claimed by this leaf test.
    constexpr std::array<FrameCase, 6> frameCases{{
        {601, 1, 3000, 0}, {667, 1, 3000, 0}, {668, 1, 3000, 1},
        {0, 0, 3000, 36}, {maximum, 1, 3000, 18446744073709542ULL},
        {maximum, 600, 3000, 18446744073709533ULL}
    }};
    beforeReads = controlled.reads();
    for (const auto& c : frameCases) {
        controlled.set(c.now);
        const auto elapsed = runtimeUiScoreElapsed(source.nowMs(), c.start, c.fallback);
        ok &= expect(elapsed >= 600, "score frame fixture is in original post-transition branch");
        if (elapsed >= 600) {
            ok &= expect((elapsed - 600) * 15 / 1000 == c.expectedTick,
                "original uint64 frame multiplication order and modulo domain");
        }
    }
    ok &= expect(controlled.reads() - beforeReads == frameCases.size(), "one source sample per frame-expression fixture");
    if (!ok) return 1;
    std::cout << "UI clock contract passed: checks=" << checks
        << " differentialCases=" << differentialCases << " sourceReads=" << controlled.reads()
        << " doubleCases=" << doubleCases.size() << " hoverCases=" << hoverCases.size()
        << " bannerCases=" << bannerCases.size() << " scoreCases=" << scoreCases.size()
        << " blinkCases=" << blinkCases.size() << " noticeCases=" << deadlineCases.size()
        << " u64FrameModuloCases=" << frameCases.size()
        << " windowsHeader=absent shellIntegration=not-covered legacyGraphOverflow=not-covered\n";
    return 0;
}

