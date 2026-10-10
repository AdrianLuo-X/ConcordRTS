#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

inline constexpr ULONGLONG runtimePlayerDefeatNoticeDurationMs = 15000;

struct RuntimePlayerDefeatNotice {
    std::string owner;
    ULONGLONG untilMs = 0;
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
