// Include after RuntimeWindowState/RuntimeTerrainInfo and
// runtimeCachedTerrainInfoForCell, before runtimeCellTransitionIsPassable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Independent reference restores the previous per-BFS owned metadata table.
inline thread_local int runtimeComponentMetadataBorrowReferenceOverride = -1;
inline bool runtimeComponentMetadataBorrowReference() {
    if (runtimeComponentMetadataBorrowReferenceOverride >= 0)
        return runtimeComponentMetadataBorrowReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_METADATA_BORROW_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeComponentMetadataBorrowReferenceScope {
public:
    explicit RuntimeComponentMetadataBorrowReferenceScope(bool reference)
        : previous_(runtimeComponentMetadataBorrowReferenceOverride) {
        runtimeComponentMetadataBorrowReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentMetadataBorrowReferenceScope() {
        runtimeComponentMetadataBorrowReferenceOverride = previous_;
    }
    RuntimeComponentMetadataBorrowReferenceScope(const RuntimeComponentMetadataBorrowReferenceScope&) = delete;
    RuntimeComponentMetadataBorrowReferenceScope& operator=(const RuntimeComponentMetadataBorrowReferenceScope&) = delete;
private:
    int previous_;
};

inline thread_local int runtimeComponentTerrainQueryReferenceOverride = -1;
inline bool runtimeComponentTerrainQueryReference() {
    if (runtimeComponentTerrainQueryReferenceOverride >= 0)
        return runtimeComponentTerrainQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_TERRAIN_QUERY_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeComponentTerrainQueryReferenceScope {
public:
    explicit RuntimeComponentTerrainQueryReferenceScope(bool reference)
        : previous_(runtimeComponentTerrainQueryReferenceOverride) {
        runtimeComponentTerrainQueryReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentTerrainQueryReferenceScope() {
        runtimeComponentTerrainQueryReferenceOverride = previous_;
    }
    RuntimeComponentTerrainQueryReferenceScope(const RuntimeComponentTerrainQueryReferenceScope&) = delete;
    RuntimeComponentTerrainQueryReferenceScope& operator=(const RuntimeComponentTerrainQueryReferenceScope&) = delete;
private:
    int previous_;
};

// ONE complete-component BFS/kernel owns the terrain pointer table. Metadata
// is either the former independent owned table or its typed container's exact
// address table borrowed within this owner. No terrain/info values are cached.
// Both tables use original integer map keys, NOT pointed-to raw coordinates.
// Legacy alias (1001,2)/(1,3) keeps its old key=3001 identity.
//
// End/invalidate before ANY map node/payload, terrain cell, rule, world,
// occupancy, gate or reservation edit, even if addresses/counts do not change.
// Shape checks are defensive; they are not a content-version or mutation proof.
// No lease/view escapes this owner or enters serialized authority. The existing
// metadata container may retain only its own bounded live-address table.
struct RuntimePlannerMetadataBorrowSummary {
    const void* state = nullptr;
    const void* terrain = nullptr;
    bool enabled = false, borrowed = false;
    std::size_t ownedInfoBytes = 0, availableInfoBytes = 0;
    std::size_t sourceEntriesExamined = 0, retainedInfoEntries = 0;
};
using RuntimePlannerMetadataBorrowObserver = void(*)(void*, const RuntimePlannerMetadataBorrowSummary&);
inline thread_local RuntimePlannerMetadataBorrowObserver runtimePlannerMetadataBorrowObserver = nullptr;
inline thread_local void* runtimePlannerMetadataBorrowObserverContext = nullptr;
class RuntimePlannerMetadataBorrowObserverScope {
public:
    RuntimePlannerMetadataBorrowObserverScope(RuntimePlannerMetadataBorrowObserver observer, void* context)
        : previous_(runtimePlannerMetadataBorrowObserver),
          previousContext_(runtimePlannerMetadataBorrowObserverContext) {
        runtimePlannerMetadataBorrowObserver = observer;
        runtimePlannerMetadataBorrowObserverContext = context;
    }
    ~RuntimePlannerMetadataBorrowObserverScope() {
        runtimePlannerMetadataBorrowObserver = previous_;
        runtimePlannerMetadataBorrowObserverContext = previousContext_;
    }
    RuntimePlannerMetadataBorrowObserverScope(const RuntimePlannerMetadataBorrowObserverScope&) = delete;
    RuntimePlannerMetadataBorrowObserverScope& operator=(const RuntimePlannerMetadataBorrowObserverScope&) = delete;
private:
    RuntimePlannerMetadataBorrowObserver previous_;
    void* previousContext_;
};

class RuntimePlannerTerrainQueries {
public:
    using TerrainMap = std::unordered_map<int, const TerrainCell*>;
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t minimumTerrainEntries = 4096;

    // A view is obtained explicitly for the exact current state/map. It must
    // remain inside one synchronous read-only transition call, and must not
    // outlive its owner, an invalidate, a reference-scope change, or mutation.
    // Validate ONCE immediately before the original two terrain finds; no
    // earlier movement/repair/diagonal/foundation/crush predicate is replaced.
    class ReadOnlyView {
    public:
        const TerrainCell* terrainAtKey(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) {
                if (const auto* cell = owner_->terrainPointers_[offset]) return cell;
            }
            // Missing and present-null terrain entries both take the original
            // find. No cell is generated and no failed query is remembered.
            const auto found = owner_->terrain_->find(key);
            return found == owner_->terrain_->end() ? nullptr : found->second;
        }

        // Address inspection for the primitive contract, not a replacement
        // for the original missing-metadata computation.
        const RuntimeTerrainInfo* cachedInfoAtKey(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) return owner_->infoPointerAt(offset);
            const auto found = owner_->state_->terrainInfoByCell.find(key);
            return found == owner_->state_->terrainInfoByCell.end()
                ? nullptr : std::addressof(found->second);
        }

        template<class OriginalFallback>
        RuntimeTerrainInfo infoForCellWithFallback(const TerrainCell& terrain,
            OriginalFallback&& originalFallback) const {
            // Original metadata selection is based on the returned TerrainCell
            // coordinates, which need not equal the terrain lookup map key.
            const int key = runtimeCellKey(terrain.x, terrain.y);
            std::size_t offset = 0;
            if (denseOffset(key, offset)) {
                if (const auto* info = owner_->infoPointerAt(offset)) return *info;
            }
            // A miss/out-of-domain cell calls the exact original function on
            // EVERY query. No terrain metadata is eagerly computed or inserted.
            return originalFallback(terrain);
        }

        RuntimeTerrainInfo infoForCell(const TerrainCell& terrain) const {
            return infoForCellWithFallback(terrain, [&](const TerrainCell& cell) {
                return runtimeCachedTerrainInfoForCell(*owner_->state_, cell);
            });
        }

    private:
        friend class RuntimePlannerTerrainQueries;
        explicit ReadOnlyView(const RuntimePlannerTerrainQueries* owner) : owner_(owner) {}
        const RuntimePlannerTerrainQueries* owner_;
    };

    RuntimePlannerTerrainQueries(const RuntimeWindowState& state,
        const TerrainMap& terrain, bool enabled = true,
        bool reference = runtimeComponentTerrainQueryReference())
        : state_(&state), terrain_(&terrain), terrainCount_(terrain.size()),
          terrainBuckets_(terrain.bucket_count()),
          infoCount_(state.terrainInfoByCell.size()),
          infoBuckets_(state.terrainInfoByCell.bucket_count()), tick_(state.tick),
          enabled_(enabled && !reference && !runtimeComponentTerrainQueryReference()
              && terrain.size() >= minimumTerrainEntries) {
        // Caller MUST pass enabled=false for original-cost/reference kernels,
        // Air, or planners which do not perform a complete large-map BFS.
        if (!enabled_) return;
        terrainPointers_.assign(denseCellCount, nullptr);
        // The map already owns this exact integer-key/address table. A borrowed
        // table never snapshots RuntimeTerrainInfo values or retains failed queries.
        borrowedInfo_ = !runtimeComponentMetadataBorrowReference()
            && infoCount_ >= minimumTerrainEntries && infoCount_ <= denseCellCount;
        if (borrowedInfo_) {
            infoGeneration_ = state.terrainInfoByCell.generation();
            const bool cold = !state.terrainInfoByCell.denseValid();
            if (cold) {
                RuntimePerfTraceScope metadataTrace("componentTerrainMetadataTable", state.tick);
                borrowedInfoPointers_ = std::addressof(state.terrainInfoByCell.densePointers());
                metadataSourceEntriesExamined_ = infoCount_;
            } else {
                borrowedInfoPointers_ = std::addressof(state.terrainInfoByCell.densePointers());
            }
            infoEntries_ = state.terrainInfoByCell.denseRetainedEntries();
        } else {
            infoPointers_.assign(denseCellCount, nullptr);
        }
        for (const auto& [key, cell] : terrain) {
            std::size_t offset = 0;
            if (cell && denseOffset(key, offset)) {
                terrainPointers_[offset] = cell;
                ++terrainEntries_;
            }
        }
        if (!borrowedInfo_) {
            metadataSourceEntriesExamined_ = infoCount_;
            for (const auto& [key, info] : state.terrainInfoByCell) {
                std::size_t offset = 0;
                if (denseOffset(key, offset)) {
                    infoPointers_[offset] = std::addressof(info);
                    ++infoEntries_;
                }
            }
        }
    }
    RuntimePlannerTerrainQueries(const RuntimePlannerTerrainQueries&) = delete;
    RuntimePlannerTerrainQueries& operator=(const RuntimePlannerTerrainQueries&) = delete;
    RuntimePlannerTerrainQueries(RuntimePlannerTerrainQueries&&) = delete;
    RuntimePlannerTerrainQueries& operator=(RuntimePlannerTerrainQueries&&) = delete;

    ~RuntimePlannerTerrainQueries() {
        if (runtimePlannerMetadataBorrowObserver)
            runtimePlannerMetadataBorrowObserver(runtimePlannerMetadataBorrowObserverContext,
                RuntimePlannerMetadataBorrowSummary{state_, terrain_, enabled_, borrowedInfo_,
                    ownedDenseInfoBytes(), denseInfoBytes(), metadataSourceEntriesExamined_, infoEntries_});
    }
    void invalidate() noexcept { enabled_ = false; }
    std::optional<ReadOnlyView> viewFor(const RuntimeWindowState& state,
        const TerrainMap& terrain) const {
        if (!enabled_ || runtimeComponentTerrainQueryReference()
            || state_ != &state || terrain_ != &terrain || tick_ != state.tick
            || terrainCount_ != terrain.size() || terrainBuckets_ != terrain.bucket_count()
            || infoCount_ != state.terrainInfoByCell.size()
            || infoBuckets_ != state.terrainInfoByCell.bucket_count()
            || (borrowedInfo_ && (runtimeComponentMetadataBorrowReference()
                || infoGeneration_ != state.terrainInfoByCell.generation()
                || !state.terrainInfoByCell.denseValid()))) return std::nullopt;
        return ReadOnlyView(this);
    }
    std::size_t denseTerrainBytes() const noexcept {
        return terrainPointers_.size() * sizeof(const TerrainCell*);
    }
    // Accessible table payload, not additional owned bytes. Contracts use the
    // separate ownedDenseInfoBytes() to distinguish original and borrowed routes.
    std::size_t denseInfoBytes() const noexcept {
        return (borrowedInfoPointers_ ? borrowedInfoPointers_->size() : infoPointers_.size())
            * sizeof(const RuntimeTerrainInfo*);
    }
    std::size_t denseTerrainEntries() const noexcept { return terrainEntries_; }
    std::size_t denseInfoEntries() const noexcept { return infoEntries_; }
    bool metadataBorrowed() const noexcept { return borrowedInfo_; }
    std::size_t ownedDenseInfoBytes() const noexcept {
        return infoPointers_.size() * sizeof(const RuntimeTerrainInfo*);
    }
    std::size_t metadataSourceEntriesExamined() const noexcept {
        return metadataSourceEntriesExamined_;
    }

private:
    static bool denseOffset(int key, std::size_t& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000;
        const int y = key / 1000;
        if (x >= static_cast<int>(side) || y >= static_cast<int>(side)) return false;
        offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
        return true;
    }
    const RuntimeWindowState* state_;
    const TerrainMap* terrain_;
    std::size_t terrainCount_, terrainBuckets_, infoCount_, infoBuckets_;
    int tick_;
    bool enabled_;
    std::vector<const TerrainCell*> terrainPointers_;
    std::vector<const RuntimeTerrainInfo*> infoPointers_;
    const std::vector<const RuntimeTerrainInfo*>* borrowedInfoPointers_ = nullptr;
    std::uint64_t infoGeneration_ = 0;
    bool borrowedInfo_ = false;
    std::size_t metadataSourceEntriesExamined_ = 0;
    const RuntimeTerrainInfo* infoPointerAt(std::size_t offset) const noexcept {
        return borrowedInfoPointers_ ? (*borrowedInfoPointers_)[offset] : infoPointers_[offset];
    }
    std::size_t terrainEntries_ = 0, infoEntries_ = 0;
};
