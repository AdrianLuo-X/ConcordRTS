#pragma once

#include <array>
#include <cstddef>
#include <string>

// Menu effects may overlap, but a new click must never stop a playing effect.
// Keep ownership outside individual pages so navigation cannot truncate tails.
template <std::size_t Count>
struct RuntimeMenuSoundPool {
    std::array<bool, Count> opened{};
    std::size_t next = 0;

    template <typename Backend>
    void collect(Backend& backend) {
        for (std::size_t channel = 0; channel < Count; ++channel) {
            if (!opened[channel]) continue;
            const auto playing = backend.isPlaying(channel);
            // An unavailable status is not evidence that playback has ended.
            if (!playing.has_value() || *playing) continue;
            backend.close(channel);
            opened[channel] = false;
        }
    }

    template <typename Backend>
    bool play(const std::wstring& path, Backend& backend) {
        static_assert(Count > 0);
        collect(backend);
        for (std::size_t offset = 0; offset < Count; ++offset) {
            const std::size_t channel = (next + offset) % Count;
            if (opened[channel]) continue;
            if (!backend.open(channel, path) || !backend.playFromStart(channel)) {
                backend.close(channel);
                return false;
            }
            opened[channel] = true;
            next = (channel + 1) % Count;
            return true;
        }
        // Bounded capacity: retain every active tail instead of stealing one.
        return false;
    }

    template <typename Backend>
    void closeAll(Backend& backend) {
        for (std::size_t channel = 0; channel < Count; ++channel) {
            if (opened[channel]) backend.close(channel);
            opened[channel] = false;
        }
    }
};
