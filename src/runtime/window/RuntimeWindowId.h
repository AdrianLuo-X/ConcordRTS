#pragma once
#include <cstdint>

// An allocated identity, never a native handle. Zero selects an unhosted window.
// A backend resolves identities only while their scoped parent lease is alive.
struct RuntimeWindowId {
    std::uint64_t value = 0;
    bool operator==(const RuntimeWindowId&) const = default;
};
