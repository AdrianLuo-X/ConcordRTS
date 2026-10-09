// Include after RuntimePlannerTerrainQueries.inl, before map-query helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <vector>

inline thread_local int runtimeComponentColdLookupReferenceOverride = -1;
inline bool runtimeComponentColdLookupReference() {
    if (runtimeComponentColdLookupReferenceOverride >= 0)
        return runtimeComponentColdLookupReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_COLD_LOOKUP_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeComponentColdLookupReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentColdLookupReferenceScope(bool reference)
        : previous_(runtimeComponentColdLookupReferenceOverride) {
        runtimeComponentColdLookupReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentColdLookupReferenceScope() {
        runtimeComponentColdLookupReferenceOverride = previous_;
    }
    RuntimeComponentColdLookupReferenceScope(const RuntimeComponentColdLookupReferenceScope&) = delete;
    RuntimeComponentColdLookupReferenceScope& operator=(const RuntimeComponentColdLookupReferenceScope&) = delete;
};

class RuntimePlannerColdCellLookups;
inline thread_local RuntimePlannerColdCellLookups* runtimeActivePlannerColdCellLookups = nullptr;
using RuntimePlannerColdLookupObserver = void(*)(void*, std::size_t, std::size_t,
    std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t);
inline thread_local RuntimePlannerColdLookupObserver runtimePlannerColdLookupObserver = nullptr;
inline thread_local void* runtimePlannerColdLookupObserverContext = nullptr;
class RuntimePlannerColdLookupObserverScope {
    RuntimePlannerColdLookupObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerColdLookupObserverScope(RuntimePlannerColdLookupObserver observer, void* context)
        : previous_(runtimePlannerColdLookupObserver), previousContext_(runtimePlannerColdLookupObserverContext) {
        runtimePlannerColdLookupObserver = observer; runtimePlannerColdLookupObserverContext = context;
    }
    ~RuntimePlannerColdLookupObserverScope() {
        runtimePlannerColdLookupObserver = previous_; runtimePlannerColdLookupObserverContext = previousContext_;
    }
    RuntimePlannerColdLookupObserverScope(const RuntimePlannerColdLookupObserverScope&) = delete;
    RuntimePlannerColdLookupObserverScope& operator=(const RuntimePlannerColdLookupObserverScope&) = delete;
};

// Owned by ONE complete BFS. Its existing terrain-query owner supplies the
// supplied-terrain and metadata tables: do not build a second copy of them.
// Only facts, and a DIFFERENT state terrain map, need extra pointer tables.
// These pointers expose the original nodes, including stale metadata and facts
// whose authored x/y disagree with cellIndexLookup. No classification or lazy
// derived cache is called during preparation.
//
// END/INVALIDATE before any in-place mover, world, map, facts, rules, occupancy,
// reservation or gate edit. Shape/address checks are NOT content revisions.
// Disabled/nested/foreign owners mask older owners; nothing escapes this BFS.
class RuntimePlannerColdCellLookups {
public:
    using TerrainMap = RuntimePlannerTerrainQueries::TerrainMap;
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t maximumExtraBytes = 2 * denseCellCount * sizeof(void*);
    class ReadOnlyView {
        friend class RuntimePlannerColdCellLookups;
        const RuntimePlannerColdCellLookups* owner_;
        std::size_t offset_;
        int key_;
        ReadOnlyView(const RuntimePlannerColdCellLookups* owner, std::size_t offset, int key)
            : owner_(owner), offset_(offset), key_(key) {}
    public:
        const MapCellFacts* facts() const noexcept {
            ++owner_->factsQueries_;
            return owner_->facts_.empty() ? nullptr : owner_->facts_[offset_];
        }
        const TerrainCell* stateTerrain() const {
            ++owner_->stateTerrainQueries_;
            return owner_->sameTerrain_ ? owner_->terrainView_->terrainAtKey(key_)
                : owner_->stateTerrain_[offset_];
        }
        const TerrainCell* suppliedTerrain() const {
            ++owner_->suppliedTerrainQueries_;
            return owner_->terrainView_->terrainAtKey(key_);
        }
        RuntimeTerrainInfo infoForCell(const TerrainCell& cell) const {
            ++owner_->infoQueries_;
            return owner_->terrainView_->infoForCell(cell);
        }
    };
    RuntimePlannerColdCellLookups(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, const TerrainMap& terrain,
        const std::unordered_map<int, int>& occupancy,
        const RuntimePlannerTerrainQueries& terrainQueries, bool enabled = true)
        : previous_(runtimeActivePlannerColdCellLookups), state_(&state), mover_(&mover),
          terrain_(&terrain), occupancy_(&occupancy), tick_(state.tick),
          sameTerrain_(&terrain == &state.terrainByCell),
          terrainCount_(terrain.size()), terrainBuckets_(terrain.bucket_count()),
          stateTerrainCount_(state.terrainByCell.size()), stateTerrainBuckets_(state.terrainByCell.bucket_count()),
          occupancyCount_(occupancy.size()), occupancyBuckets_(occupancy.bucket_count()),
          infoGeneration_(state.terrainInfoByCell.generation()),
          factsData_(state.mapFacts.cells.data()), factsCount_(state.mapFacts.cells.size()),
          factsLookupCount_(state.mapFacts.cellIndexLookup.size()),
          factsLookupBuckets_(state.mapFacts.cellIndexLookup.bucket_count()),
          factsLoaded_(state.mapFactsLoaded),
          worldCellsData_(state.world.getTerrainCells().data()), worldCellsCount_(state.world.getTerrainCells().size()),
          objectsData_(state.world.getObjects().data()), objectsCount_(state.world.getObjects().size()) {
        // Publish even a disabled owner: it must mask an enabled outer owner.
        // All preparation is const lookup only, and allocation failure falls
        // back before publishing an enabled table.
        if (enabled && !runtimeComponentColdLookupReference()) {
            try { prepare(terrainQueries); } catch (const std::bad_alloc&) {
                terrainView_.reset(); facts_.clear(); stateTerrain_.clear(); enabled_ = false;
            }
        }
        runtimeActivePlannerColdCellLookups = this;
    }
    ~RuntimePlannerColdCellLookups() {
        runtimeActivePlannerColdCellLookups = previous_;
        if (runtimePlannerColdLookupObserver)
            runtimePlannerColdLookupObserver(runtimePlannerColdLookupObserverContext,
                enabled_ ? 1 : 0, fullValidations_, views_, fallbacks_, factsQueries_, stateTerrainQueries_,
                suppliedTerrainQueries_, infoQueries_);
    }
    RuntimePlannerColdCellLookups(const RuntimePlannerColdCellLookups&) = delete;
    RuntimePlannerColdCellLookups& operator=(const RuntimePlannerColdCellLookups&) = delete;
    RuntimePlannerColdCellLookups(RuntimePlannerColdCellLookups&&) = delete;
    RuntimePlannerColdCellLookups& operator=(RuntimePlannerColdCellLookups&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t fullValidations() const noexcept { return fullValidations_; }
    std::size_t extraBytes() const noexcept {
        return (facts_.size() + stateTerrain_.size()) * sizeof(void*);
    }
    std::optional<ReadOnlyView> viewFor(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, const TerrainMap& terrain,
        const std::unordered_map<int, int>& occupancy, const RuntimeCellPoint& point) const {
        if (!enabled_ || runtimeActivePlannerColdCellLookups != this
            || runtimeComponentColdLookupReference() || runtimeComponentTerrainQueryReference()
            || state_ != &state || mover_ != &mover || terrain_ != &terrain || occupancy_ != &occupancy
            || tick_ != state.tick || point.x < 0 || point.y < 0 || point.x >= 512 || point.y >= 512
            || infoGeneration_ != state.terrainInfoByCell.generation()) {
            ++fallbacks_; return std::nullopt;
        }
        ++views_;
        return ReadOnlyView(this, static_cast<std::size_t>(point.y) * side + point.x,
            runtimeCellKey(point.x, point.y));
    }
private:
    // Check every captured shape once after const preparation, then rely on
    // the owned kernel's lexical read-only lifetime just like its query lease.
    // The hot boundary is deliberately not a purported content-version check.
    bool sourceIdentitiesMatch() const noexcept {
        return terrainCount_ == terrain_->size() && terrainBuckets_ == terrain_->bucket_count()
            && stateTerrainCount_ == state_->terrainByCell.size() && stateTerrainBuckets_ == state_->terrainByCell.bucket_count()
            && occupancyCount_ == occupancy_->size() && occupancyBuckets_ == occupancy_->bucket_count()
            && infoGeneration_ == state_->terrainInfoByCell.generation()
            && factsLoaded_ == state_->mapFactsLoaded && factsData_ == state_->mapFacts.cells.data()
            && factsCount_ == state_->mapFacts.cells.size() && factsLookupCount_ == state_->mapFacts.cellIndexLookup.size()
            && factsLookupBuckets_ == state_->mapFacts.cellIndexLookup.bucket_count()
            && worldCellsData_ == state_->world.getTerrainCells().data() && worldCellsCount_ == state_->world.getTerrainCells().size()
            && objectsData_ == state_->world.getObjects().data() && objectsCount_ == state_->world.getObjects().size();
    }
    bool canonicalTerrain(const TerrainMap& map) const {
        for (const auto& [key, cell] : map) {
            if (!cell || cell->x < 0 || cell->y < 0 || cell->x >= 512 || cell->y >= 512
                || key != runtimeCellKey(cell->x, cell->y)
                || !terrainView_->cachedInfoAtKey(key)) return false;
        }
        return true;
    }
    void prepare(const RuntimePlannerTerrainQueries& queries) {
        RuntimePerfTraceScope trace("componentColdLookupBuild",state_->tick);
        terrainView_ = queries.viewFor(*state_, *terrain_);
        if (!terrainView_ || !canonicalTerrain(*terrain_)
            || (!sameTerrain_ && !canonicalTerrain(state_->terrainByCell))) {
            terrainView_.reset(); return;
        }
        if (!sameTerrain_) {
            stateTerrain_.assign(denseCellCount, nullptr);
            for (const auto& [key, cell] : state_->terrainByCell) {
                (void)key;
                stateTerrain_[static_cast<std::size_t>(cell->y) * side + cell->x] = cell;
            }
        }
        if (factsLoaded_ && factsCount_) {
            facts_.assign(denseCellCount, nullptr);
            for (const auto& [index, offset] : state_->mapFacts.cellIndexLookup) {
                if (index >= 0 && static_cast<std::size_t>(index) < denseCellCount && offset < factsCount_)
                    facts_[static_cast<std::size_t>(index)] = std::addressof(state_->mapFacts.cells[offset]);
            }
        }
        ++fullValidations_;
        enabled_ = sourceIdentitiesMatch();
    }
    RuntimePlannerColdCellLookups* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const TerrainMap* terrain_;
    const std::unordered_map<int, int>* occupancy_;
    int tick_;
    bool sameTerrain_, enabled_ = false;
    std::size_t terrainCount_, terrainBuckets_, stateTerrainCount_, stateTerrainBuckets_;
    std::size_t occupancyCount_, occupancyBuckets_;
    std::uint64_t infoGeneration_;
    const MapCellFacts* factsData_;
    std::size_t factsCount_, factsLookupCount_, factsLookupBuckets_;
    bool factsLoaded_;
    const TerrainCell* worldCellsData_;
    std::size_t worldCellsCount_;
    const WorldObjectInstance* objectsData_;
    std::size_t objectsCount_;
    std::optional<RuntimePlannerTerrainQueries::ReadOnlyView> terrainView_;
    std::vector<const MapCellFacts*> facts_;
    std::vector<const TerrainCell*> stateTerrain_;
    std::size_t fullValidations_ = 0;
    mutable std::size_t views_ = 0, fallbacks_ = 0, factsQueries_ = 0;
    mutable std::size_t stateTerrainQueries_ = 0, suppliedTerrainQueries_ = 0, infoQueries_ = 0;
};
