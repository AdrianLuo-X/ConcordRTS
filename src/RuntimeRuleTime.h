#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <charconv>
#include <string>
#include <string_view>

// A rule minute is 900 retail logic frames. This is an authored unit, not a
// promise that the retail process runs at 15 wall-clock frames per second.
inline constexpr int runtimeRuleFramesPerSecond = 15;

inline int runtimeRuleFramesToTicks(std::int64_t frames, int fixedTickHz) {
    const auto hz = std::clamp(fixedTickHz, 1, 1000);
    frames = std::clamp<std::int64_t>(frames, 0, std::numeric_limits<int>::max());
    return static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
        (frames * hz + runtimeRuleFramesPerSecond - 1) / runtimeRuleFramesPerSecond));
}

inline std::int64_t runtimeRuleFrameAtTick(int tick, int fixedTickHz) {
    return static_cast<std::int64_t>(std::max(0, tick)) * runtimeRuleFramesPerSecond
        / std::clamp(fixedTickHz, 1, 1000);
}

inline int runtimeRuleFramesElapsed(int tick, int fixedTickHz) {
    if (tick <= 0) return 0;
    return static_cast<int>(runtimeRuleFrameAtTick(tick, fixedTickHz)
        - runtimeRuleFrameAtTick(std::max(0, tick - 1), fixedTickHz));
}

inline bool runtimeRuleIntervalCrossed(int tick, int frames, int fixedTickHz) {
    if (tick < 0) return false;
    const auto interval = std::max(1, frames);
    return tick == 0 || runtimeRuleFrameAtTick(tick, fixedTickHz) / interval
        != runtimeRuleFrameAtTick(std::max(0, tick - 1), fixedTickHz) / interval;
}

// TechnoType::Read_INI 0x714664 and CCINI::ReadSpeed 0x47482D.
// Both units and weapon projectiles interpret Speed as a percentage of 256.
inline int runtimeRuleSpeedLeptons(int speed) {
    return std::min(255, std::clamp(speed, 0, 100) * 256 / 100);
}

// GameOptions::GetAnimSpeed 0x5FB2E0. Retail index 0 is fastest, 6 slowest.
inline int runtimeRuleNormalizedAnimationDelay(int delay, int retailGameSpeed) {
    constexpr int delayTable[4][7] = {{2,2,1,1,1,1,1}, {3,3,3,2,2,2,1},
        {5,4,4,3,3,2,2}, {7,6,5,4,4,4,3}};
    const int speed = std::clamp(retailGameSpeed, 0, 6);
    if (delay <= 0) return 0;
    if (delay < 5) return delayTable[delay-1][speed];
    return static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
        static_cast<std::int64_t>(delay) * 8 / (speed + 1)));
}

inline int runtimeRuleAnimationDelayFrames(int rate) {
    return rate > 0 ? std::max(1, 900 / rate) : 1;
}

inline int runtimeRuleSignedFacingDifference(int target, int initial) {
    const auto bits = static_cast<std::uint16_t>(target - initial);
    return bits < 0x8000 ? static_cast<int>(bits) : static_cast<int>(bits) - 0x10000;
}

// The timer retains 16-bit angular precision even though world art uses Dir256.
// Fields belong to the staged world object and are saved/hashed with it.
struct RuntimeFacingClock {
    int initial = 0;
    int target = 0;
    int rate = 0;
    int elapsedNumerator = 0;
    int lastTick = -1;
    int lastFacing = -1;

    bool active() const { return lastTick >= 0; }
    int frames() const { return rate > 0 ? std::abs(runtimeRuleSignedFacingDifference(target, initial)) / rate : 0; }
    int current16(int hz) const {
        const int count = frames();
        const int elapsed = elapsedNumerator / std::clamp(hz, 1, 1000);
        if (count <= 0 || elapsed >= count) return target;
        // Retail 0x4C9438 divides before multiplying the remaining frame count.
        return static_cast<std::uint16_t>(target
            - (runtimeRuleSignedFacingDifference(target, initial) / count) * (count - elapsed));
    }
    bool valid(int currentTick) const {
        return initial >= 0 && initial <= 65535 && target >= 0 && target <= 65535
            && rate >= 0 && rate <= 32512 && rate % 256 == 0
            && elapsedNumerator >= 0 && elapsedNumerator <= 128000
            && lastTick >= -1 && lastTick <= currentTick && lastFacing >= -1 && lastFacing <= 255
            && (active() ? rate > 0 && lastFacing >= 0 : *this == RuntimeFacingClock{});
    }
    bool operator==(const RuntimeFacingClock&) const = default;
};

inline int runtimeAdvanceFacingClock(RuntimeFacingClock& clock, int current, int desired,
    int rot, int tick, int fixedTickHz) {
    current = (current % 256 + 256) % 256;
    desired = (desired % 256 + 256) % 256;
    const int rate = std::clamp(rot, 0, 127) * 256;
    if (clock.active() && (clock.lastFacing != current || tick < clock.lastTick)) clock = {};
    if (!clock.active() && current == desired) return current;
    if (rate <= 0) { clock = {}; return desired; }
    if (clock.active() && tick > clock.lastTick) {
        // Retail FacingClass owns an independent CurrentFrame timer, including
        // frames in which the caller does not sample the direction.
        const auto frames = runtimeRuleFrameAtTick(tick, fixedTickHz)
            - runtimeRuleFrameAtTick(clock.lastTick, fixedTickHz);
        clock.elapsedNumerator = static_cast<int>(std::min<std::int64_t>(128000,
            clock.elapsedNumerator + frames * std::clamp(fixedTickHz, 1, 1000)));
        clock.lastTick = tick;
    }
    const int sampled = clock.active() ? clock.current16(fixedTickHz) : current * 256;
    if (!clock.active() || clock.target != desired * 256 || clock.rate != rate) {
        clock.initial = sampled;
        clock.target = desired * 256;
        clock.rate = rate;
        clock.elapsedNumerator = 0;
        clock.lastTick = tick;
    }
    const int result = (clock.current16(fixedTickHz) + 128) / 256 % 256;
    if (clock.frames() <= 0 || clock.elapsedNumerator / std::clamp(fixedTickHz,1,1000) >= clock.frames()) clock = {};
    else clock.lastFacing = result;
    return result;
}

inline std::string runtimeSerializeFacingClock(const RuntimeFacingClock& c) {
    return std::to_string(c.initial) + ',' + std::to_string(c.target) + ','
        + std::to_string(c.rate) + ',' + std::to_string(c.elapsedNumerator) + ','
        + std::to_string(c.lastTick) + ',' + std::to_string(c.lastFacing);
}

inline bool runtimeParseFacingClock(std::string_view text, int tick, RuntimeFacingClock& result) {
    RuntimeFacingClock candidate;
    int* fields[] = {&candidate.initial, &candidate.target, &candidate.rate,
        &candidate.elapsedNumerator, &candidate.lastTick, &candidate.lastFacing};
    for (int i = 0; i < 6; ++i) {
        const auto end = text.find(',');
        if ((i < 5) != (end != std::string_view::npos)) return false;
        const auto token = text.substr(0, end);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), *fields[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return false;
        if (i < 5) text.remove_prefix(end + 1);
    }
    if (!candidate.valid(tick)) return false;
    result = candidate;
    return true;
}
