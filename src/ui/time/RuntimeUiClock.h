#pragma once

#include <cstdint>

using RuntimeUiTimeMs = std::uint64_t;

// A borrowed, typed host dependency. The provider outlives every borrowing
// session and detached load state; it is never saved, hashed, or committed.
class RuntimeUiClock {
public:
    virtual ~RuntimeUiClock() = default;
    virtual RuntimeUiTimeMs nowMs() const noexcept = 0;
};

// Preserve each original consumer's arithmetic policy; these are intentionally
// different. Unsigned subtraction wraps. No clamp or monotonic filter is added.
constexpr bool runtimeUiWithinDoubleSelection(
    RuntimeUiTimeMs now, RuntimeUiTimeMs last) noexcept {
    return now - last <= 500;
}

constexpr bool runtimeUiHoverDelayElapsed(
    RuntimeUiTimeMs now, RuntimeUiTimeMs start, RuntimeUiTimeMs delay) noexcept {
    return now - start >= delay;
}

constexpr bool runtimeUiBannerElapsed(
    RuntimeUiTimeMs now, RuntimeUiTimeMs start, RuntimeUiTimeMs duration) noexcept {
    return now >= start && now - start >= duration;
}

constexpr RuntimeUiTimeMs runtimeUiReadyBlinkPeriod(RuntimeUiTimeMs now) noexcept {
    return now / 500;
}

constexpr bool runtimeUiReadyBlinkOn(RuntimeUiTimeMs now) noexcept {
    return runtimeUiReadyBlinkPeriod(now) % 2 == 0;
}

// A zero start is the original score sentinel. Callers retain their distinct
// 2000/3000ms fallback values and the original subsequent frame calculations.
constexpr RuntimeUiTimeMs runtimeUiScoreElapsed(
    RuntimeUiTimeMs now, RuntimeUiTimeMs start, RuntimeUiTimeMs fallback) noexcept {
    return start == 0 || now < start ? fallback : now - start;
}

