#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <unordered_map>

// This reference switch changes only the representative-occupant lookup.
// Whole-cell blocker/subcell/foundation reference switches stay independent.
inline thread_local int runtimePathSnapshotOccupancyQueryReferenceOverride = -1;
inline bool runtimePathSnapshotOccupancyQueryReference() {
    if (runtimePathSnapshotOccupancyQueryReferenceOverride >= 0)
        return runtimePathSnapshotOccupancyQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* value=std::getenv("CONCORD_PATH_SNAPSHOT_OCCUPANCY_QUERY_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimePathSnapshotOccupancyQueryReferenceScope {
    int previous_;
public:
    explicit RuntimePathSnapshotOccupancyQueryReferenceScope(bool reference)
        : previous_(runtimePathSnapshotOccupancyQueryReferenceOverride) {
        runtimePathSnapshotOccupancyQueryReferenceOverride=reference ? 1 : 0;
    }
    ~RuntimePathSnapshotOccupancyQueryReferenceScope() {
        runtimePathSnapshotOccupancyQueryReferenceOverride=previous_;
    }
    RuntimePathSnapshotOccupancyQueryReferenceScope(const RuntimePathSnapshotOccupancyQueryReferenceScope&) = delete;
    RuntimePathSnapshotOccupancyQueryReferenceScope& operator=(const RuntimePathSnapshotOccupancyQueryReferenceScope&) = delete;
};

// The input map and its node addresses must remain unchanged for this lexical
// lifetime. In the path builder it is a const local, constructed before this
// helper and destroyed after it. Nothing is retained beyond this local lifetime.
class RuntimePathRepresentativeOccupants {
public:
    using Map = std::unordered_map<int,int>;
    static constexpr std::size_t minimumDenseQueries = 4096;
    static constexpr std::size_t denseSlots = 512U * 512U;
    static constexpr std::size_t maximumDenseBytes = denseSlots * sizeof(const int*);
    static_assert(maximumDenseBytes <= 2U * 1024U * 1024U);

    explicit RuntimePathRepresentativeOccupants(const Map& occupants,
        std::size_t expectedQueryCount,
        bool reference=runtimePathSnapshotOccupancyQueryReference())
        : occupants_(occupants), reference_(reference) {
        if (reference_ || expectedQueryCount < minimumDenseQueries || occupants_.empty()) return;
        for (const auto& [key,value]:occupants_) {
            const int offset=denseOffset(key);
            if (offset < 0) continue;
            // Allocate only if a queried canonical-key domain can benefit.
            // Value-initialization clears exactly 262144 pointer slots.
            if (!dense_) dense_=std::make_unique<const int*[]>(denseSlots);
            dense_[static_cast<std::size_t>(offset)]=&value;
        }
    }
    RuntimePathRepresentativeOccupants(const RuntimePathRepresentativeOccupants&) = delete;
    RuntimePathRepresentativeOccupants& operator=(const RuntimePathRepresentativeOccupants&) = delete;
    RuntimePathRepresentativeOccupants(RuntimePathRepresentativeOccupants&&) = delete;
    RuntimePathRepresentativeOccupants& operator=(RuntimePathRepresentativeOccupants&&) = delete;

    const int* find(int legacyKey) const {
        if (dense_) {
            const int offset=denseOffset(legacyKey);
            if (offset >= 0) return dense_[static_cast<std::size_t>(offset)];
        }
        const auto found=occupants_.find(legacyKey);
        return found==occupants_.end() ? nullptr : &found->second;
    }
    bool usingDense() const noexcept { return static_cast<bool>(dense_); }
    bool reference() const noexcept { return reference_; }
    std::size_t denseBytes() const noexcept { return dense_ ? maximumDenseBytes : 0; }

private:
    // Decode the original y*1000+x INTEGER key, not the query's raw point.
    // Thus raw aliases (1000,0)/(0,1) still share one representative. Keys
    // with no canonical x/y<512 representation keep the original map lookup.
    static int denseOffset(int key) noexcept {
        if (key < 0 || key > 511511) return -1;
        const int x=key%1000, y=key/1000;
        return x < 512 ? y*512+x : -1;
    }
    const Map& occupants_;
    bool reference_;
    std::unique_ptr<const int*[]> dense_;
};
