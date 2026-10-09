#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

inline thread_local int runtimePathDynamicSparseQueriesReferenceOverride = -1;
inline bool runtimePathDynamicSparseQueriesReference() {
    if (runtimePathDynamicSparseQueriesReferenceOverride >= 0)
        return runtimePathDynamicSparseQueriesReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_DYNAMIC_SPARSE_QUERIES_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathDynamicSparseQueriesReferenceScope {
    int previous_;
public:
    explicit RuntimePathDynamicSparseQueriesReferenceScope(bool reference)
        : previous_(runtimePathDynamicSparseQueriesReferenceOverride) {
        runtimePathDynamicSparseQueriesReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathDynamicSparseQueriesReferenceScope() {
        runtimePathDynamicSparseQueriesReferenceOverride = previous_;
    }
    RuntimePathDynamicSparseQueriesReferenceScope(const RuntimePathDynamicSparseQueriesReferenceScope&) = delete;
    RuntimePathDynamicSparseQueriesReferenceScope& operator=(const RuntimePathDynamicSparseQueriesReferenceScope&) = delete;
};

struct RuntimePathDynamicSparseQueryStats {
    bool enabled = false;
    std::size_t retainedBytes = 0;
    std::size_t occupiedEntries = 0;
    std::size_t repairPoints = 0;
    std::size_t representativeBytesAvoided = 0;
    std::size_t proofs = 0;
    std::size_t proofFallbacks = 0;
    std::size_t representativeQueries = 0;
    std::size_t representativeSkipped = 0;
    std::size_t repairQueries = 0;
    std::size_t repairSkipped = 0;
};
class RuntimePathDynamicSparseQueries;
inline thread_local RuntimePathDynamicSparseQueries* runtimeActivePathDynamicSparseQueries = nullptr;
using RuntimePathDynamicSparseQueryObserver = void(*)(void*, const void*, const RuntimePathDynamicSparseQueryStats&);
inline thread_local RuntimePathDynamicSparseQueryObserver runtimePathDynamicSparseQueryObserver = nullptr;
inline thread_local void* runtimePathDynamicSparseQueryObserverContext = nullptr;
class RuntimePathDynamicSparseQueryObserverScope {
    RuntimePathDynamicSparseQueryObserver previous_;
    void* previousContext_;
public:
    RuntimePathDynamicSparseQueryObserverScope(RuntimePathDynamicSparseQueryObserver observer, void* context)
        : previous_(runtimePathDynamicSparseQueryObserver), previousContext_(runtimePathDynamicSparseQueryObserverContext) {
        runtimePathDynamicSparseQueryObserver = observer;
        runtimePathDynamicSparseQueryObserverContext = context;
    }
    ~RuntimePathDynamicSparseQueryObserverScope() {
        runtimePathDynamicSparseQueryObserver = previous_;
        runtimePathDynamicSparseQueryObserverContext = previousContext_;
    }
    RuntimePathDynamicSparseQueryObserverScope(const RuntimePathDynamicSparseQueryObserverScope&) = delete;
    RuntimePathDynamicSparseQueryObserverScope& operator=(const RuntimePathDynamicSparseQueryObserverScope&) = delete;
};

// ONE immutable snapshot builder owns this census. It copies only conservative
// presence bits, not object/terrain classification or passability results.
// Occupant keys use the ORIGINAL legacy integer-key identity. Repair points are
// exact raw coordinates supplied AFTER the original mover/phase/negative filter;
// bridge layer is deliberately overmarked and the old predicate still decides it.
// End/invalidate before ANY source, mover, relation, gate, world or rules edit.
// No pointer/count/tick is claimed to be a content revision. No view may escape.
class RuntimePathDynamicSparseQueries {
public:
    using Occupants = std::unordered_map<int, int>;
    static constexpr std::size_t side = 512;
    static constexpr std::size_t cellCount = side * side;
    static constexpr std::size_t maximumBytes = cellCount;
    static constexpr std::size_t minimumQueries = 4096;
    static constexpr unsigned char Occupied = 1;
    static constexpr unsigned char Repair = 2;
    static constexpr unsigned char Original = Occupied | Repair;

    class ReadOnlyView {
        friend class RuntimePathDynamicSparseQueries;
        RuntimePathDynamicSparseQueries* owner_;
        explicit ReadOnlyView(RuntimePathDynamicSparseQueries* owner) : owner_(owner) {}
    public:
        template<bool CollectStats = true>
        unsigned char possibleInputs(int x, int y) const noexcept {
            auto& stats = owner_->stats_;
            if constexpr (CollectStats) ++stats.proofs;
            if (!owner_->enabled_ || runtimeActivePathDynamicSparseQueries != owner_
                || *owner_->tickAddress_ != owner_->tick_
                || runtimePathDynamicSparseQueriesReference()
                || x < 0 || y < 0 || x >= static_cast<int>(side) || y >= static_cast<int>(side)) {
                if constexpr (CollectStats) ++stats.proofFallbacks;
                return Original;
            }
            // Raw canonical coordinates have exactly the same old integer key
            // as the canonical decode used when inserting occupant keys. Raw
            // aliases/noncanonical coordinates above ALWAYS use old queries.
            return owner_->flags_.empty() ? 0
                : owner_->flags_[static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x)];
        }
        template<bool CollectStats = true, class OriginalQuery>
        auto representative(unsigned char inputs, OriginalQuery&& original) const -> decltype(original()) {
            if ((inputs & Occupied) == 0) {
                if constexpr (CollectStats) ++owner_->stats_.representativeSkipped;
                return nullptr;
            }
            if constexpr (CollectStats) ++owner_->stats_.representativeQueries;
            return original();
        }
        template<bool CollectStats = true, class OriginalQuery>
        bool repairReserved(unsigned char inputs, OriginalQuery&& original) const {
            if ((inputs & Repair) == 0) {
                if constexpr (CollectStats) ++owner_->stats_.repairSkipped;
                return false;
            }
            if constexpr (CollectStats) ++owner_->stats_.repairQueries;
            return original();
        }
    };

    // visitRepairs accepts a callback(int x,int y) and MUST only iterate current
    // original filtered reservations; it must not mutate or escape any input.
    template<class VisitRepairs>
    RuntimePathDynamicSparseQueries(const void* stateIdentity, const void* moverIdentity,
        const int& tick, const Occupants& occupants, std::size_t expectedQueries,
        bool qualified, VisitRepairs&& visitRepairs,
        bool reference = runtimePathDynamicSparseQueriesReference())
        : previous_(runtimeActivePathDynamicSparseQueries), state_(stateIdentity), mover_(moverIdentity),
          occupants_(&occupants), tickAddress_(&tick), tick_(tick),
          collectStats_(runtimePathDynamicSparseQueryObserver != nullptr) {
        if (qualified && expectedQueries >= minimumQueries && !reference
            && !runtimePathDynamicSparseQueriesReference()
            && occupants.size() <= 4096 && occupants.size() <= expectedQueries / 8) {
            try {
                for (const auto& entry : occupants) {
                    ++stats_.occupiedEntries;
                    std::size_t offset = 0;
                    if (legacyOffset(entry.first, offset)) {
                        mark(offset, Occupied);
                        hasCanonicalOccupants_ = true;
                    }
                }
                visitRepairs([&](int x, int y) {
                    ++stats_.repairPoints;
                    if (x >= 0 && y >= 0 && x < static_cast<int>(side) && y < static_cast<int>(side))
                        mark(static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x), Repair);
                    // An out-of-domain raw repair point cannot equal a canonical
                    // raw query point. Such QUERY points use old predicates.
                    // Never compute y*1000+x here, avoiding new overflow paths.
                });
                enabled_ = true;
            } catch (const std::bad_alloc&) {
                std::vector<unsigned char> empty;
                flags_.swap(empty);
                enabled_ = false;
            }
        }
        stats_.enabled = enabled_;
        stats_.retainedBytes = enabled_ ? flags_.size() : 0;
        stats_.representativeBytesAvoided = enabled_ && hasCanonicalOccupants_
            ? cellCount * sizeof(const int*) : 0;
        // Disabled/reference owners also mask an outer enabled census.
        runtimeActivePathDynamicSparseQueries = this;
    }
    ~RuntimePathDynamicSparseQueries() {
        runtimeActivePathDynamicSparseQueries = previous_;
        if (runtimePathDynamicSparseQueryObserver)
            runtimePathDynamicSparseQueryObserver(runtimePathDynamicSparseQueryObserverContext, state_, stats_);
    }
    RuntimePathDynamicSparseQueries(const RuntimePathDynamicSparseQueries&) = delete;
    RuntimePathDynamicSparseQueries& operator=(const RuntimePathDynamicSparseQueries&) = delete;
    RuntimePathDynamicSparseQueries(RuntimePathDynamicSparseQueries&&) = delete;
    RuntimePathDynamicSparseQueries& operator=(RuntimePathDynamicSparseQueries&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    std::optional<ReadOnlyView> viewFor(const void* stateIdentity, const void* moverIdentity,
        const int& tick, const Occupants& occupants) {
        if (!enabled_ || runtimeActivePathDynamicSparseQueries != this
            || runtimePathDynamicSparseQueriesReference() || state_ != stateIdentity || mover_ != moverIdentity
            || tickAddress_ != &tick || tick_ != tick || occupants_ != &occupants) return std::nullopt;
        return ReadOnlyView(this);
    }
    bool enabled() const noexcept { return enabled_; }
    bool collectStats() const noexcept { return collectStats_; }
    const RuntimePathDynamicSparseQueryStats& stats() const noexcept { return stats_; }
private:
    static bool legacyOffset(int key, std::size_t& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000, y = key / 1000;
        if (x >= static_cast<int>(side) || y >= static_cast<int>(side)) return false;
        offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
        return true;
    }
    void mark(std::size_t offset, unsigned char flag) {
        if (flags_.empty()) flags_.assign(cellCount, 0);
        flags_[offset] |= flag;
    }
    RuntimePathDynamicSparseQueries* previous_;
    const void* state_;
    const void* mover_;
    const Occupants* occupants_;
    const int* tickAddress_;
    int tick_;
    bool enabled_ = false;
    bool collectStats_ = false;
    bool hasCanonicalOccupants_ = false;
    std::vector<unsigned char> flags_;
    RuntimePathDynamicSparseQueryStats stats_;
};
