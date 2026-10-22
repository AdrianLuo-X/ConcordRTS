#pragma once

#include <cstdint>

#include <set>
#include <string>
#include <utility>
#include <vector>

inline constexpr std::uint64_t runtimePlayerDefeatNoticeDurationMs = 15000;

// Local presentation milliseconds; the host supplies the monotonic clock value.
inline constexpr std::uint64_t runtimePlayerDefeatNoticeDeadline(std::uint64_t nowMs) noexcept {
    constexpr std::uint64_t maximumMs = ~std::uint64_t{0};
    return nowMs > maximumMs - runtimePlayerDefeatNoticeDurationMs
        ? maximumMs : nowMs + runtimePlayerDefeatNoticeDurationMs;
}

struct RuntimePlayerDefeatNotice {
    std::string owner;
    std::uint64_t untilMs = 0;
};

// Local presentation only; never serialized or included in simulation hashes.
struct RuntimePlayerDefeatNoticeState {
    std::vector<RuntimePlayerDefeatNotice> lines;
    std::set<std::string> announcedOwners;
    bool suppressExisting = false;

    // Commit exchanges container storage directly; moving an entire set may allocate.
    friend void swap(RuntimePlayerDefeatNoticeState& live,
        RuntimePlayerDefeatNoticeState& loaded) noexcept {
        static_assert(noexcept(live.lines.swap(loaded.lines)));
        static_assert(noexcept(live.announcedOwners.swap(loaded.announcedOwners)));
        live.lines.swap(loaded.lines);
        live.announcedOwners.swap(loaded.announcedOwners);
        std::swap(live.suppressExisting, loaded.suppressExisting);
    }
};
