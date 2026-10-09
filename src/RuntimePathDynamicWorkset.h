#pragma once
#include "RuntimePathDynamicPreparation.h"
#include "RuntimePathDynamicResourceFusion.h"
#include "RuntimePathDenseAuthoredLand.h"

#include <algorithm>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

inline thread_local int runtimePathDynamicWorksetReferenceOverride = -1;
inline bool runtimePathDynamicWorksetReference() {
    if (runtimePathDynamicWorksetReferenceOverride >= 0)
        return runtimePathDynamicWorksetReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_DYNAMIC_WORKSET_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimePathDynamicWorksetReferenceScope {
    int previous_;
public:
    explicit RuntimePathDynamicWorksetReferenceScope(bool reference) noexcept
        : previous_(runtimePathDynamicWorksetReferenceOverride) {
        runtimePathDynamicWorksetReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathDynamicWorksetReferenceScope() {
        runtimePathDynamicWorksetReferenceOverride = previous_;
    }
    RuntimePathDynamicWorksetReferenceScope(const RuntimePathDynamicWorksetReferenceScope&) = delete;
    RuntimePathDynamicWorksetReferenceScope& operator=(const RuntimePathDynamicWorksetReferenceScope&) = delete;
};

enum class RuntimePathDynamicWorksetReason {
    Disabled, Reference, Nested, Size, OccupancyDensity, RepairDomain,
    Coordinates, CandidateDensity, Allocation, Ready
};
struct RuntimePathDynamicWorksetStats {
    bool admitted = false;
    RuntimePathDynamicWorksetReason reason = RuntimePathDynamicWorksetReason::Disabled;
    std::size_t entries = 0, examined = 0, candidates = 0, omitted = 0;
    std::size_t occupiedEntries = 0, repairPoints = 0, retainedBytes = 0;
    // Counts real entrances to the shared original predicate body. Incremented
    // only by that body, never inferred from the admitted range or fabricated
    // as an old Sparse/Conditional observer's skipped calls.
    std::size_t bodyVisits = 0;
};
inline bool runtimePathDynamicWorksetTraceEnabled() {
    static const bool enabled = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
    return enabled;
}
inline void runtimeRecordPathDynamicWorkset(const RuntimePathDynamicWorksetStats& value) {
    if (!runtimePathDynamicWorksetTraceEnabled()) return;
    struct Totals {
        std::uint64_t calls=0, admitted=0, rejected=0, entries=0, examined=0;
        std::uint64_t candidates=0, omitted=0, bodyVisits=0, fullBodyVisits=0, worksetBodyVisits=0;
        ~Totals() {
            std::cerr << "Runtime path workset admission diagnostic: calls=" << calls
                << " admitted=" << admitted << " rejected=" << rejected << " entries=" << entries
                << " examined=" << examined << " candidates=" << candidates << " omitted=" << omitted
                << " bodyVisits=" << bodyVisits << " fullBodyVisits=" << fullBodyVisits
                << " worksetBodyVisits=" << worksetBodyVisits << '\n';
        }
    };
    static thread_local Totals totals;
    ++totals.calls; totals.admitted += value.admitted; totals.rejected += !value.admitted;
    totals.entries += value.entries; totals.examined += value.examined;
    totals.candidates += value.candidates; totals.omitted += value.omitted;
    totals.bodyVisits += value.bodyVisits;
    if (value.admitted) totals.worksetBodyVisits += value.bodyVisits;
    else totals.fullBodyVisits += value.bodyVisits;
}
using RuntimePathDynamicWorksetObserver = void(*)(void*, const void*,
    const RuntimePathDynamicWorksetStats&);
inline thread_local RuntimePathDynamicWorksetObserver runtimePathDynamicWorksetObserver = nullptr;
inline thread_local void* runtimePathDynamicWorksetObserverContext = nullptr;
inline thread_local const void* runtimeActivePathDynamicWorkset = nullptr;
class RuntimePathDynamicWorksetObserverScope {
    RuntimePathDynamicWorksetObserver previous_;
    void* previousContext_;
public:
    RuntimePathDynamicWorksetObserverScope(RuntimePathDynamicWorksetObserver observer, void* context) noexcept
        : previous_(runtimePathDynamicWorksetObserver), previousContext_(runtimePathDynamicWorksetObserverContext) {
        runtimePathDynamicWorksetObserver = observer;
        runtimePathDynamicWorksetObserverContext = context;
    }
    ~RuntimePathDynamicWorksetObserverScope() {
        runtimePathDynamicWorksetObserver = previous_;
        runtimePathDynamicWorksetObserverContext = previousContext_;
    }
    RuntimePathDynamicWorksetObserverScope(const RuntimePathDynamicWorksetObserverScope&) = delete;
    RuntimePathDynamicWorksetObserverScope& operator=(const RuntimePathDynamicWorksetObserverScope&) = delete;
};

// ONE synchronous immutable snapshot preparation owns this proof. The output
// remains the complete owned authored vector. Addresses/count/tick are only
// defensive identity checks, NEVER a content revision. Invalidate/end before
// any mover, rules, metadata value, repair, gate, occupancy or World mutation.
// All proof/rejection/allocation happens before the original body writes output.
// Cell is PathTerrainCell at the Shell hook; the header needs no World API.
template<class Cell>
class RuntimePathDynamicWorkset {
public:
    using Occupants = std::unordered_map<int, int>;
    static constexpr std::size_t side = 512, slots = side * side;
    static constexpr std::size_t minimumEntries = 4096, maximumEntries = slots;
    static constexpr std::size_t maximumCandidates = 4096;
    static constexpr std::size_t presenceWords = slots / 64;

    class ReadOnlyView;
    class Iterator {
        RuntimePathDynamicWorkset* owner_;
        std::size_t position_;
        friend class RuntimePathDynamicWorkset;
        friend class ReadOnlyView;
        Iterator(RuntimePathDynamicWorkset* owner, std::size_t position) : owner_(owner), position_(position) {}
    public:
        Cell& operator*() const { return (*owner_->snapshot_)[owner_->indices_[position_]]; }
        Iterator& operator++() noexcept { ++position_; return *this; }
        bool operator!=(const Iterator& other) const noexcept {
            return owner_ != other.owner_ || position_ != other.position_;
        }
    };
    class ReadOnlyView {
        RuntimePathDynamicWorkset* owner_;
        friend class RuntimePathDynamicWorkset;
        explicit ReadOnlyView(RuntimePathDynamicWorkset* owner) : owner_(owner) {}
    public:
        Iterator begin() const { return Iterator(owner_, 0); }
        Iterator end() const { return Iterator(owner_, owner_->indices_.size()); }
    };

    template<class VisitRepair, class FindMetadata, class LandAllows, class CollectBounds = std::false_type, class ResourceFusion = std::false_type, class DenseLand = RuntimePathOriginalAuthoredLandPermission>
    RuntimePathDynamicWorkset(const void* state, const void* mover, const int& tick,
        std::vector<Cell>& snapshot, const Occupants& occupants,
        bool enabled, bool ground, bool naval, bool hasTiberiumLandType,
        VisitRepair&& visitRepair, FindMetadata&& findMetadata, LandAllows&& landAllows, CollectBounds = {}, ResourceFusion resourceFusion = {}, DenseLand denseLand = {})
        : state_(state), mover_(mover), tickAddress_(&tick), tick_(tick),
          snapshot_(&snapshot), data_(snapshot.data()), count_(snapshot.size()), occupants_(&occupants),
          previous_(runtimeActivePathDynamicWorkset), observer_(runtimePathDynamicWorksetObserver),
          observerContext_(runtimePathDynamicWorksetObserverContext), resourceObserver_(runtimePathDynamicResourceFusionObserver), resourceObserverContext_(runtimePathDynamicResourceFusionObserverContext) {
        // Disabled/nested scopes mask their parent; they cannot borrow its proof.
        runtimeActivePathDynamicWorkset = this;
        stats_.entries = count_; stats_.occupiedEntries = occupants.size();
        bool fuseResources = false;
        if constexpr (ResourceFusion::value) {
            fuseResources = !runtimePathDynamicResourceFusionReference()
                && std::isfinite(resourceFusion.percent) && observer_ == nullptr;
        }
        fusionStats_.requested = fuseResources;
        if (!enabled) return;
        if (runtimePathDynamicWorksetReference()) { stats_.reason = RuntimePathDynamicWorksetReason::Reference; return; }
        if (previous_) { stats_.reason = RuntimePathDynamicWorksetReason::Nested; return; }
        if (count_ < minimumEntries || count_ > maximumEntries) { stats_.reason = RuntimePathDynamicWorksetReason::Size; return; }
        const std::size_t limit = count_ / 8 < maximumCandidates ? count_ / 8 : maximumCandidates;
        if (occupants.size() > limit) { stats_.reason = RuntimePathDynamicWorksetReason::OccupancyDensity; return; }
        try {
            std::vector<std::uint64_t> presence;
            const auto mark = [&](int x, int y) {
                if (!canonical(x, y)) return false;
                if (presence.empty()) presence.assign(presenceWords, 0);
                const auto offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
                presence[offset / 64] |= std::uint64_t{1} << (offset % 64);
                return true;
            };
            for (const auto& entry : occupants) {
                // Test presence of original LEGACY integer keys, including
                // negative occupant IDs. No live-object filtering or ID test.
                const int key = entry.first;
                if (key < 0 || !mark(key % 1000, key / 1000)) {
                    stats_.reason = RuntimePathDynamicWorksetReason::Coordinates; return;
                }
            }
            bool repairValid = true;
            visitRepair([&](int x, int y) {
                if (!repairValid) return;
                ++stats_.repairPoints;
                if (stats_.repairPoints > maximumCandidates || !mark(x, y)) repairValid = false;
            });
            if (!repairValid) { stats_.reason = RuntimePathDynamicWorksetReason::RepairDomain; return; }
            std::vector<std::size_t> indices;
            std::vector<std::size_t> resources;
            indices.reserve(limit);
            RuntimePathDynamicPreparationBounds bounds;
            for (std::size_t index = 0; index < count_; ++index) {
                const Cell& cell = snapshot[index];
                ++stats_.examined;
                if (!canonical(cell.cell.x, cell.cell.y)) {
                    stats_.reason = RuntimePathDynamicWorksetReason::Coordinates; return;
                }
                // Original/reference callers compile out every per-row bound
                // reduction. Only the explicitly early owner requests bounds.
                if constexpr (CollectBounds::value) {
                    if (index == 0) {
                        bounds.minimumX = bounds.maximumX = cell.cell.x;
                        bounds.minimumY = bounds.maximumY = cell.cell.y;
                    } else {
                        bounds.minimumX = std::min(bounds.minimumX, cell.cell.x);
                        bounds.maximumX = std::max(bounds.maximumX, cell.cell.x);
                        bounds.minimumY = std::min(bounds.minimumY, cell.cell.y);
                        bounds.maximumY = std::max(bounds.maximumY, cell.cell.y);
                    }
                }
                const auto offset = static_cast<std::size_t>(cell.cell.y) * side + static_cast<std::size_t>(cell.cell.x);
                const bool present = !presence.empty() && (presence[offset / 64] & (std::uint64_t{1} << (offset % 64))) != 0;
                const bool modeBlocks = cell.passable && ((ground && cell.water && !cell.bridge && !cell.shore)
                    || (naval && !cell.water && !cell.bridge));
                bool candidate = present || modeBlocks || cell.hazardCost != 0
                    || (cell.resource && hasTiberiumLandType);
                // A resource row leaves the body ONLY after all other dynamic
                // causes have a current negative proof. Metadata missing skips
                // permission just as the original body; denied land stays in it.
                bool neutralResource = false;
                if constexpr (ResourceFusion::value) {
                    if (fuseResources && cell.passable && cell.resource && hasTiberiumLandType
                        && !present && !modeBlocks && cell.hazardCost == 0) {
                        const bool allowed = [&] {
                            if constexpr (DenseLand::exactDenseAuthoredLand)
                                return denseLand.allows(cell.cell.x,cell.cell.y);
                            else {
                                const auto* info = findMetadata(cell.cell.x + 1000 * cell.cell.y);
                                return !info || landAllows(*info);
                            }
                        }();
                        if (allowed) {
                            candidate = false; neutralResource = true;
                        }
                    }
                }
                if (!candidate && cell.passable && !modeBlocks && !neutralResource) {
                    // Read the CURRENT original metadata node and authored land
                    // permission; generation is not used as a value signature.
                    // Missing metadata skips permission, exactly as the body.
                    if constexpr (DenseLand::exactDenseAuthoredLand) {
                        if (!denseLand.allows(cell.cell.x,cell.cell.y)) candidate=true;
                    } else {
                        const auto* info = findMetadata(cell.cell.x + 1000 * cell.cell.y);
                        if (info && !landAllows(*info)) candidate = true;
                    }
                }
                if (candidate) {
                    if (indices.size() == limit) {
                        stats_.reason = RuntimePathDynamicWorksetReason::CandidateDensity; return;
                    }
                    // Every duplicate row is independently appended in authored
                    // order; coordinate presence never replaces row identity.
                    indices.push_back(index);
                } else if constexpr (ResourceFusion::value) {
                    if (neutralResource) {
                        // Grow explicitly within N, never letting push_back invoke
                        // MSVC's1.5x growth beyond the admitted authored bound.
                        if (resources.size() == resources.capacity()) {
                            const std::size_t next = resources.empty() ? std::min(count_,std::size_t{8192})
                                : std::min(count_,resources.capacity()*2);
                            resources.reserve(next);
                        }
                        resources.push_back(index);
                    }
                }
            }
            if constexpr (CollectBounds::value) { bounds_ = bounds; boundsCollected_ = true; }
            indices_ = std::move(indices);
            resources_ = std::move(resources);
            resourceFusion_ = fuseResources;
            enabled_ = true;
            stats_.admitted = true; stats_.reason = RuntimePathDynamicWorksetReason::Ready;
            stats_.candidates = indices_.size(); stats_.omitted = count_ - indices_.size();
            stats_.retainedBytes = indices_.capacity() * sizeof(std::size_t);
            // Temporary <=32KiB presence storage is already destroyed on return.
        } catch (const std::bad_alloc&) {
            indices_.clear(); enabled_ = false; stats_.reason = RuntimePathDynamicWorksetReason::Allocation;
        } catch (...) {
            // A constructor which propagates never gets a destructor. Restore
            // the lexical mask before propagating a non-allocation exception.
            runtimeActivePathDynamicWorkset = previous_;
            throw;
        }
    }
    ~RuntimePathDynamicWorkset() {
        runtimeActivePathDynamicWorkset = previous_;
        runtimeRecordPathDynamicWorkset(stats_);
        // Test-only POD diagnostic. It may not throw, mutate World/state or
        // retain borrowed addresses. Publication happens after the body/tails.
        if (observer_) observer_(observerContext_, state_, stats_);
        fusionStats_.admitted = enabled_ && resourceFusion_;
        fusionStats_.entries = stats_.entries; fusionStats_.examined = stats_.examined;
        fusionStats_.bodyCandidates = stats_.candidates; fusionStats_.resourceRows = resources_.size();
        fusionStats_.bodyVisits = stats_.bodyVisits;
        fusionStats_.retainedResourceBytes = resources_.capacity() * sizeof(std::size_t);
        runtimeRecordPathDynamicResourceFusion(fusionStats_);
        if(resourceObserver_) resourceObserver_(resourceObserverContext_,state_,fusionStats_);
    }
    RuntimePathDynamicWorkset(const RuntimePathDynamicWorkset&) = delete;
    RuntimePathDynamicWorkset& operator=(const RuntimePathDynamicWorkset&) = delete;
    RuntimePathDynamicWorkset(RuntimePathDynamicWorkset&&) = delete;
    RuntimePathDynamicWorkset& operator=(RuntimePathDynamicWorkset&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    bool collectsStats() const noexcept { return observer_ != nullptr || resourceObserver_ != nullptr || runtimePathDynamicWorksetTraceEnabled(); }
    template<bool Collect> void recordBodyVisit() noexcept {
        if constexpr (Collect) ++stats_.bodyVisits;
    }
    const RuntimePathDynamicWorksetStats& stats() const noexcept { return stats_; }
    const std::vector<std::size_t>& indicesForDiagnostics() const noexcept { return indices_; }
    bool resourceFusionEnabled() const noexcept { return resourceFusion_; }
    const std::vector<std::size_t>& resourceIndicesForDiagnostics() const noexcept { return resources_; }
    template<bool Collect> void recordResourceVisit() noexcept {
        if constexpr(Collect) ++fusionStats_.resourceVisits;
    }
    // Valid only together with a currently acquired admitted ReadOnlyView.
    const RuntimePathDynamicPreparationBounds& bounds() const noexcept { return bounds_; }
    bool boundsCollected() const noexcept { return boundsCollected_; }
    std::optional<ReadOnlyView> viewFor(const void* state, const void* mover, int tick,
        const std::vector<Cell>& snapshot, const Occupants& occupants) {
        if (!enabled_ || runtimePathDynamicWorksetReference() || (resourceFusion_ && runtimePathDynamicResourceFusionReference()) || runtimeActivePathDynamicWorkset != this
            || state != state_ || mover != mover_ || tick != tick_ || *tickAddress_ != tick_
            || &snapshot != snapshot_ || snapshot.data() != data_ || snapshot.size() != count_
            || &occupants != occupants_) return std::nullopt;
        return ReadOnlyView(this);
    }
private:
    static bool canonical(int x, int y) noexcept {
        return x >= 0 && y >= 0 && x < static_cast<int>(side) && y < static_cast<int>(side);
    }
    const void* state_; const void* mover_; const int* tickAddress_; int tick_;
    std::vector<Cell>* snapshot_; const Cell* data_; std::size_t count_;
    const Occupants* occupants_; const void* previous_;
    RuntimePathDynamicWorksetObserver observer_; void* observerContext_;
    bool enabled_ = false, resourceFusion_ = false;
    RuntimePathDynamicResourceFusionObserver resourceObserver_; void* resourceObserverContext_;
    RuntimePathDynamicResourceFusionStats fusionStats_;
    std::vector<std::size_t> resources_;
    std::vector<std::size_t> indices_;
    RuntimePathDynamicWorksetStats stats_;
    RuntimePathDynamicPreparationBounds bounds_;
    bool boundsCollected_ = false;
};
