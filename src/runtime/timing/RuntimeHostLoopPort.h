#pragma once

// Two synchronous actions on the owning session/UI thread. The existing
// window owner drains the entire current-thread queue in its original order.
// Idle requests exactly the original one-millisecond sleep, not a new wait.
class RuntimeHostLoopPort {
public:
    virtual ~RuntimeHostLoopPort() = default;
    virtual bool drainMessages() = 0;
    virtual void sleepForIdleSlice() = 0;
};
constexpr int runtimeHostLoopCatchupLimit(bool multiplayer) noexcept {
    return multiplayer ? 8 : 16;
}
constexpr bool runtimeHostLoopShouldIdleSleep(bool paintQueued,
    bool unlimitedAuthorityAdvanced) noexcept {
    return !paintQueued && !unlimitedAuthorityAdvanced;
}
