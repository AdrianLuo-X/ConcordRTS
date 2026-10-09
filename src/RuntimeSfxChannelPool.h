#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>

struct RuntimeSfxChannelReuseState {
    std::string samplePath;
    bool opened = false;
    bool reusable = true;
    int volumePercent = -1;
    int gain16384 = -1;
    int pan16384 = -1;
    int durationTicks = 0;
    int durationTickHz = 0;
};

template <typename Backend>
void releaseRuntimeSfxChannel(RuntimeSfxChannelReuseState& cached,
    bool& active, int& busyUntilTick, std::size_t channel, Backend& backend,
    bool forceClose = false) {
    if (cached.opened || active || forceClose) {
        (void)backend.stop(channel);
        backend.close(channel);
    }
    cached = {};
    active = false;
    busyUntilTick = 0;
}

// Backend owns device calls. The existing active/busy/rotation fields remain
// authoritative; the extra metadata retains at most one open sample per lane.
template <std::size_t Count, typename Backend>
bool playRuntimeSfxPooled(
    std::array<RuntimeSfxChannelReuseState, Count>& cached,
    std::array<bool, Count>& active, std::array<int, Count>& busyUntilTick,
    std::size_t& nextChannel, int& steals, int tick, int fixedTickHz,
    const std::string& samplePath, int volumePercent, Backend& backend,
    int pan16384 = 8192, int gain16384 = -1) {
    static_assert(Count > 0);
    if (volumePercent <= 0 && gain16384 <= 0) return true;
    std::size_t channel = Count;
    for (std::size_t offset = 0; offset < Count; ++offset) {
        const std::size_t candidate = (nextChannel + offset) % Count;
        if (active[candidate]) {
            const std::optional<bool> playing = backend.isPlaying(candidate);
            cached[candidate].reusable = playing.has_value();
            if (playing.value_or(tick < busyUntilTick[candidate])) continue;
            active[candidate] = false;
        }
        channel = candidate;
        break;
    }
    if (channel == Count) {
        channel = static_cast<std::size_t>(std::distance(busyUntilTick.begin(),
            std::min_element(busyUntilTick.begin(), busyUntilTick.end())));
        ++steals;
    }
    nextChannel = (channel + 1U) % Count;
    auto& entry = cached[channel];
    if (entry.opened && (!entry.reusable || entry.samplePath != samplePath)) {
        releaseRuntimeSfxChannel(entry, active[channel], busyUntilTick[channel],
            channel, backend);
    } else if (active[channel]) {
        // A stolen lane can retain its sample, but the old play must stop.
        if (!backend.stop(channel)) {
            releaseRuntimeSfxChannel(entry, active[channel], busyUntilTick[channel],
                channel, backend, true);
        }
        active[channel] = false;
    }
    if (!entry.opened) {
        if (!backend.open(channel, samplePath)) {
            // Failed opens can leave a partially created device alias.
            releaseRuntimeSfxChannel(entry, active[channel], busyUntilTick[channel],
                channel, backend, true);
            return false;
        }
        entry.opened = true;
        entry.reusable = true;
        entry.samplePath = samplePath;
    }
    const int volume = std::clamp(volumePercent, 0, 100);
    const int gain = std::clamp(gain16384 < 0 ? volume * 16384 / 100 : gain16384, 0, 16384);
    const int pan = std::clamp(pan16384, 0, 16384);
    if (entry.gain16384 != gain || entry.pan16384 != pan) {
        if (!backend.setMix(channel, gain, pan)) {
            releaseRuntimeSfxChannel(entry, active[channel], busyUntilTick[channel], channel, backend);
            return false;
        }
        entry.volumePercent = volume; entry.gain16384 = gain; entry.pan16384 = pan;
    }
    // A rejected volume/matrix command must not fall through to full-volume sound.
    if (!backend.playFromStart(channel)) {
        releaseRuntimeSfxChannel(entry, active[channel], busyUntilTick[channel],
            channel, backend);
        return false;
    }
    if (entry.durationTicks <= 0 || entry.durationTickHz != fixedTickHz) {
        entry.durationTicks = std::max(1, backend.durationTicks(samplePath, fixedTickHz));
        entry.durationTickHz = fixedTickHz;
    }
    active[channel] = true;
    busyUntilTick[channel] = tick > std::numeric_limits<int>::max() - entry.durationTicks
        ? std::numeric_limits<int>::max() : tick + entry.durationTicks;
    return true;
}

template <std::size_t Count, typename Backend>
void stopRuntimeSfxPooled(
    std::array<RuntimeSfxChannelReuseState, Count>& cached,
    std::array<bool, Count>& active, std::array<int, Count>& busyUntilTick,
    Backend& backend) {
    for (std::size_t channel = 0; channel < Count; ++channel) {
        releaseRuntimeSfxChannel(cached[channel], active[channel],
            busyUntilTick[channel], channel, backend);
    }
}
