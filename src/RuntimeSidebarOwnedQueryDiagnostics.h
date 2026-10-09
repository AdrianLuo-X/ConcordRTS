#pragma once

#include "RuntimePerfTrace.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>

// Private, trace-only observations. No query result or source value is cached.
// The caller activates this phase only for one actual drawRuntimeSidebar call.
namespace RuntimeSidebarOwnedQueryDiagnostics {

inline bool enabled() noexcept {
    static const bool value = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
    return value;
}

struct Totals {
    std::uint64_t phases = 0;
    std::uint64_t liveOwnedQueries = 0;
    std::uint64_t liveOwnedRowsVisited = 0;
    std::uint64_t prerequisiteQueries = 0;
    std::uint64_t prerequisiteRowsVisited = 0;
    ~Totals() noexcept {
        if (phases == 0) return;
        try {
            std::cerr << "Runtime sidebar query counts: phases=" << phases
                << " liveOwnedQueries=" << liveOwnedQueries
                << " liveOwnedRowsVisited=" << liveOwnedRowsVisited
                << " prerequisiteQueries=" << prerequisiteQueries
                << " prerequisiteRowsVisited=" << prerequisiteRowsVisited << '\n';
        } catch (...) {
            // Diagnostic output cannot change a completed query or game state.
        }
    }
};

inline Totals& totals() {
    static thread_local Totals value;
    return value;
}

class Phase {
public:
    inline static thread_local Phase* active = nullptr;
    explicit Phase(const void* state) noexcept
        : previous_(active), state_(state), admitted_(state && enabled() && !previous_) {
        active = this;
        if (admitted_) ++totals().phases;
    }
    ~Phase() { active = previous_; }
    Phase(const Phase&) = delete;
    Phase& operator=(const Phase&) = delete;
    static bool observes(const void* state) noexcept {
        return active && active->admitted_ && active->state_ == state;
    }
private:
    Phase* previous_;
    const void* state_;
    bool admitted_;
};

enum class Kind { LiveOwned, PrerequisiteBuilding };

class Query {
public:
    Query(const void* state, int tick, Kind kind) {
        if (!Phase::observes(state)) return;
        auto& counts = totals();
        if (kind == Kind::LiveOwned) {
            ++counts.liveOwnedQueries;
            rows_ = &counts.liveOwnedRowsVisited;
            trace_.emplace("sidebar.buildLimit.liveOwnedCount", tick);
        } else {
            ++counts.prerequisiteQueries;
            rows_ = &counts.prerequisiteRowsVisited;
            trace_.emplace("sidebar.prerequisite.ownedBuilding", tick);
        }
    }
    void visitRow() noexcept {
        if (rows_) ++*rows_;
    }
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;
private:
    std::uint64_t* rows_ = nullptr;
    std::optional<RuntimePerfTraceScope> trace_;
};

} // namespace RuntimeSidebarOwnedQueryDiagnostics
