#pragma once
#include <algorithm>
#include <cstdint>

// Retail's BOMBCURS has 13 frames. The Ares BombClass::GetCurrentFrame hook
// retains the native lifetime stepping and global-frame flicker when no
// per-weapon overrides are present. Keep both divisions in authored frames.
inline int runtimeIvanBombIconFrame(int progress, int duration, int simulationTick,
    int fixedTickHz, int flickerRateFrames, int frameCount) {
    if (frameCount <= 0 || duration <= 0 || progress >= duration) return -1;
    if (frameCount == 1) return 0;
    const std::int64_t hz = std::clamp(fixedTickHz, 1, 1000);
    const std::int64_t lifetime = static_cast<std::int64_t>(std::max(0,progress)) * 15 / hz;
    const std::int64_t delay = std::max<std::int64_t>(1,static_cast<std::int64_t>(duration) * 15 / hz);
    const std::int64_t period = 2LL * std::max(1,flickerRateFrames);
    const std::int64_t gameFrame = static_cast<std::int64_t>(std::max(0,simulationTick)) * 15 / hz;
    std::int64_t frame = lifetime / std::max<std::int64_t>(1,delay / (frameCount - 1));
    if (gameFrame % period >= std::max(1,flickerRateFrames)) ++frame;
    if (frame >= frameCount) frame = frameCount - 1;
    else if (frame == frameCount - 1) --frame;
    return static_cast<int>(std::max<std::int64_t>(0,frame));
}
