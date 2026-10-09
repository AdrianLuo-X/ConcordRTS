// Include immediately after runtimeCellIsPassableUncached. This memo is opt-in
// through the complete-component planner; the general query retains its scalar default.
// Commit gate/subcell helpers only inspect existing state here. Invalidate/end
// this scope BEFORE any world, rules, behavior, gate, repair or reservation edit.
inline thread_local int runtimeComponentPassabilityReferenceOverride = -1;
bool runtimeComponentPassabilityReference() {
    if (runtimeComponentPassabilityReferenceOverride >= 0)
        return runtimeComponentPassabilityReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_COMPONENT_PASSABILITY_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}
class RuntimeComponentPassabilityReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentPassabilityReferenceScope(bool value)
        : previous_(runtimeComponentPassabilityReferenceOverride) {
        runtimeComponentPassabilityReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeComponentPassabilityReferenceScope() {
        runtimeComponentPassabilityReferenceOverride = previous_;
    }
    RuntimeComponentPassabilityReferenceScope(const RuntimeComponentPassabilityReferenceScope&) = delete;
    RuntimeComponentPassabilityReferenceScope& operator=(const RuntimeComponentPassabilityReferenceScope&) = delete;
};

// This switch restores the C12 unordered memo while retaining the complete
// component scope. The older switch above still selects the original scalar.
inline thread_local int runtimeComponentDenseReferenceOverride = -1;
bool runtimeComponentDenseReference() {
    if (runtimeComponentDenseReferenceOverride >= 0)
        return runtimeComponentDenseReferenceOverride != 0;
    static const bool value = [] {
        const char* raw = std::getenv("CONCORD_COMPONENT_DENSE_REFERENCE");
        return raw && raw[0] == '1' && raw[1] == '\0';
    }();
    return value;
}
class RuntimeComponentDenseReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentDenseReferenceScope(bool value)
        : previous_(runtimeComponentDenseReferenceOverride) {
        runtimeComponentDenseReferenceOverride = value ? 1 : 0;
    }
    ~RuntimeComponentDenseReferenceScope() { runtimeComponentDenseReferenceOverride = previous_; }
    RuntimeComponentDenseReferenceScope(const RuntimeComponentDenseReferenceScope&) = delete;
    RuntimeComponentDenseReferenceScope& operator=(const RuntimeComponentDenseReferenceScope&) = delete;
};

// Independent C21 ablation: preserve the C20 dense/unordered memo and kernel,
// but validate the general memo boundary on every query again.
inline thread_local int runtimeComponentQueryLeaseReferenceOverride = -1;
bool runtimeComponentQueryLeaseReference() {
    if (runtimeComponentQueryLeaseReferenceOverride >= 0)
        return runtimeComponentQueryLeaseReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_QUERY_LEASE_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeComponentQueryLeaseReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentQueryLeaseReferenceScope(bool reference)
        : previous_(runtimeComponentQueryLeaseReferenceOverride) {
        runtimeComponentQueryLeaseReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentQueryLeaseReferenceScope() {
        runtimeComponentQueryLeaseReferenceOverride = previous_;
    }
    RuntimeComponentQueryLeaseReferenceScope(const RuntimeComponentQueryLeaseReferenceScope&) = delete;
    RuntimeComponentQueryLeaseReferenceScope& operator=(const RuntimeComponentQueryLeaseReferenceScope&) = delete;
};

// The complete-component BFS still iterates the original terrain/neighbor
// order and stores original integer component keys. Only private visited
// membership changes; legacy coordinate aliases intentionally remain aliases.
class RuntimePlannerVisitedCellKeys {
    static constexpr int maximumBitKey_ = 1048575;
    int lastBitKey_ = -1;
    std::size_t bitEntries_ = 0;
    std::vector<std::uint64_t> bits_;
    std::unordered_set<int> outside_;
public:
    explicit RuntimePlannerVisitedCellKeys(const std::unordered_map<int,const TerrainCell*>& terrain) {
        if (runtimeComponentDenseReference() || runtimeComponentPassabilityReference()) {
            outside_.reserve(terrain.size()); // Original C12 visited allocation.
            return;
        }
        for (const auto& [key, cell] : terrain) {
            (void)cell;
            if (key < 0) continue;
            lastBitKey_ = std::max(lastBitKey_, std::min(key, maximumBitKey_));
            if (lastBitKey_ == maximumBitKey_) break;
        }
        if (lastBitKey_ < 0) { outside_.reserve(terrain.size()); return; }
        bits_.assign(static_cast<std::size_t>(lastBitKey_) / 64 + 1, std::uint64_t{0});
    }
    bool contains(int key) const {
        if (key >= 0 && key <= lastBitKey_)
            return (bits_[static_cast<std::size_t>(key) / 64]
                & (std::uint64_t{1} << (static_cast<unsigned>(key) % 64))) != 0;
        return outside_.find(key) != outside_.end();
    }
    bool insert(int key) {
        if (key >= 0 && key <= lastBitKey_) {
            auto& word = bits_[static_cast<std::size_t>(key) / 64];
            const auto mask = std::uint64_t{1} << (static_cast<unsigned>(key) % 64);
            if (word & mask) return false;
            word |= mask; ++bitEntries_; return true;
        }
        return outside_.insert(key).second;
    }
    std::size_t size() const { return bitEntries_ + outside_.size(); }
    std::size_t bitmapBytes() const { return bits_.size() * sizeof(std::uint64_t); }
    std::size_t unorderedEntries() const { return outside_.size(); }
};

struct RuntimePlannerPassabilityKey {
    int x = 0, y = 0, allowed = -1;
    bool crush = false, hasMode = false;
    PathMovementMode mode = PathMovementMode::Ground;
    bool operator==(const RuntimePlannerPassabilityKey&) const = default;
};
struct RuntimePlannerPassabilityKeyHash {
    std::size_t operator()(const RuntimePlannerPassabilityKey& key) const noexcept {
        std::size_t value = std::hash<int>{}(key.x);
        const auto mix = [&](std::size_t part) {
            value ^= part + std::size_t{0x9e3779b9U} + (value << 6U) + (value >> 2U);
        };
        mix(std::hash<int>{}(key.y)); mix(std::hash<int>{}(key.allowed));
        mix(key.crush); mix(key.hasMode); mix(static_cast<std::size_t>(key.mode));
        return value;
    }
};

class RuntimePlannerCellPassabilityScope;
class RuntimePlannerCellPassabilityReadOnlyLease;
inline thread_local RuntimePlannerCellPassabilityScope* runtimeActivePlannerCellPassability = nullptr;
class RuntimePlannerCellPassabilityScope {
    friend class RuntimePlannerCellPassabilityReadOnlyLease;
    RuntimePlannerCellPassabilityScope* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const std::unordered_map<int, const TerrainCell*>* terrain_;
    const std::unordered_map<int, int>* occupancy_;
    const void *objects_, *cells_, *terrainObjects_, *mapFacts_;
    std::size_t objectCount_, cellCount_, terrainObjectCount_, mapFactCount_;
    std::size_t terrainCount_, occupancyCount_, terrainBuckets_, occupancyBuckets_;
    int tick_;
    bool enabled_;
    std::size_t capacity_;
    std::unordered_map<RuntimePlannerPassabilityKey, bool, RuntimePlannerPassabilityKeyHash> values_;
    std::uint64_t hits_ = 0, misses_ = 0, fallbacks_ = 0;
    // Four exact common keys per raw coordinate: optional mode presence and
    // crush flag. Two bits hold unknown/false/true; at most 256 KiB per scope.
    static constexpr std::size_t denseCellLimit_ = 262144;
    bool densePermitted_, denseAttempted_ = false;
    int denseMinX_ = 0, denseMaxX_ = -1, denseMinY_ = 0, denseMaxY_ = -1;
    std::size_t denseWidth_ = 0, denseEntries_ = 0;
    std::uint64_t denseHits_ = 0;
    PathMovementMode denseMoverMode_ = PathMovementMode::Ground;
    std::vector<std::uint8_t> denseValues_;

    static bool canonicalDensePoint(int x, int y) {
        return x >= 0 && x < 1000 && y >= 0
            && y <= (std::numeric_limits<int>::max() - x) / 1000;
    }
    void prepareDenseDomain() {
        denseAttempted_ = true;
        std::size_t canonicalCount = 0;
        int minX = 999, maxX = -1, minY = std::numeric_limits<int>::max(), maxY = -1;
        for (const auto& [key, cell] : *terrain_) {
            (void)key;
            if (!cell || !canonicalDensePoint(cell->x, cell->y)) continue;
            ++canonicalCount;
            minX = std::min(minX, cell->x); maxX = std::max(maxX, cell->x);
            minY = std::min(minY, cell->y); maxY = std::max(maxY, cell->y);
        }
        if (!canonicalCount) return;
        const auto width = static_cast<std::uint64_t>(maxX) - minX + 1;
        const auto height = static_cast<std::uint64_t>(maxY) - minY + 1;
        const auto area = width * height;
        // Sparse/oversized domains retain the old bounded unordered memo.
        if (area > denseCellLimit_ || (area + 3) / 4 > canonicalCount) return;
        denseMinX_ = minX; denseMaxX_ = maxX; denseMinY_ = minY; denseMaxY_ = maxY;
        denseWidth_ = static_cast<std::size_t>(width);
        denseMoverMode_ = runtimeObjectMovementMode(*state_, *mover_);
        denseValues_.assign(static_cast<std::size_t>(area), std::uint8_t{0});
    }
    bool denseSlot(const RuntimePlannerPassabilityKey& key, std::size_t& offset,
        unsigned& shift) {
        if (!densePermitted_ || key.allowed != -1 || !canonicalDensePoint(key.x, key.y)) return false;
        if (!denseAttempted_) prepareDenseDomain();
        if (denseValues_.empty() || key.x < denseMinX_ || key.x > denseMaxX_
            || key.y < denseMinY_ || key.y > denseMaxY_
            || (key.hasMode ? key.mode != denseMoverMode_ : key.mode != PathMovementMode::Ground)) return false;
        offset = static_cast<std::size_t>(key.y - denseMinY_) * denseWidth_
            + static_cast<std::size_t>(key.x - denseMinX_);
        shift = ((key.hasMode ? 2U : 0U) + (key.crush ? 1U : 0U)) * 2U;
        return true;
    }
    bool explicitCapacityAvailable() const {
        // An explicitly small fixture/caller cap limits both stores together.
        // The normal 65536 unordered cap remains unchanged; dense slots have
        // their separate fixed byte bound and avoid large-map hash allocations.
        return capacity_ == 65536 || entries() < capacity_;
    }
public:
    RuntimePlannerCellPassabilityScope(const RuntimeWindowState& state,
        const WorldObjectInstance& mover,
        const std::unordered_map<int, const TerrainCell*>& terrain,
        const std::unordered_map<int, int>& occupancy,
        bool enabled = true, std::size_t capacity = 65536)
        : previous_(runtimeActivePlannerCellPassability), state_(&state), mover_(&mover),
          terrain_(&terrain), occupancy_(&occupancy),
          objects_(state.world.getObjects().data()), cells_(state.world.getTerrainCells().data()),
          terrainObjects_(state.world.getTerrainObjects().data()), mapFacts_(state.mapFacts.cells.data()),
          objectCount_(state.world.getObjects().size()), cellCount_(state.world.getTerrainCells().size()),
          terrainObjectCount_(state.world.getTerrainObjects().size()), mapFactCount_(state.mapFacts.cells.size()),
          terrainCount_(terrain.size()), occupancyCount_(occupancy.size()),
          terrainBuckets_(terrain.bucket_count()), occupancyBuckets_(occupancy.bucket_count()),
          tick_(state.tick), enabled_(enabled), capacity_(std::min<std::size_t>(capacity, 65536)),
          densePermitted_(enabled && capacity_ != 0 && !runtimeComponentDenseReference()) {
        // Even a disabled inner scope masks its outer scope.
        runtimeActivePlannerCellPassability = this;
    }
    ~RuntimePlannerCellPassabilityScope() { runtimeActivePlannerCellPassability = previous_; }
    RuntimePlannerCellPassabilityScope(const RuntimePlannerCellPassabilityScope&) = delete;
    RuntimePlannerCellPassabilityScope& operator=(const RuntimePlannerCellPassabilityScope&) = delete;
    void invalidate() { enabled_ = false; }
    bool matches(const RuntimeWindowState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int, const TerrainCell*>& terrain,
        const std::unordered_map<int, int>& occupancy) const {
        return enabled_ && state_ == &state && mover_ == &mover && terrain_ == &terrain
            && occupancy_ == &occupancy && tick_ == state.tick
            && objects_ == state.world.getObjects().data() && objectCount_ == state.world.getObjects().size()
            && cells_ == state.world.getTerrainCells().data() && cellCount_ == state.world.getTerrainCells().size()
            && terrainObjects_ == state.world.getTerrainObjects().data()
            && terrainObjectCount_ == state.world.getTerrainObjects().size()
            && mapFacts_ == state.mapFacts.cells.data() && mapFactCount_ == state.mapFacts.cells.size()
            && terrainCount_ == terrain.size() && occupancyCount_ == occupancy.size()
            && terrainBuckets_ == terrain.bucket_count() && occupancyBuckets_ == occupancy.bucket_count();
    }
    template<class Compute>
    bool query(const RuntimePlannerPassabilityKey& key, Compute&& compute, bool& hit) {
        std::size_t offset = 0; unsigned shift = 0;
        if (denseSlot(key, offset, shift)) {
            const unsigned stored = (denseValues_[offset] >> shift) & 3U;
            if (stored) { ++hits_; ++denseHits_; hit = true; return stored == 2U; }
            ++misses_;
            const bool value = compute();
            if (explicitCapacityAvailable()) {
                denseValues_[offset] |= static_cast<std::uint8_t>((value ? 2U : 1U) << shift);
                ++denseEntries_;
            } else ++fallbacks_;
            return value;
        }
        const auto found = values_.find(key);
        if (found != values_.end()) { ++hits_; hit = true; return found->second; }
        ++misses_;
        const bool value = compute();
        if (values_.size() < capacity_ && explicitCapacityAvailable()) values_.emplace(key, value);
        else ++fallbacks_;
        return value;
    }
    std::size_t entries() const { return values_.size() + denseEntries_; }
    std::size_t unorderedEntries() const { return values_.size(); }
    std::size_t denseEntries() const { return denseEntries_; }
    std::size_t denseBytes() const { return denseValues_.size(); }
    std::uint64_t denseHits() const { return denseHits_; }
    bool denseAttempted() const { return denseAttempted_; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::uint64_t fallbacks() const { return fallbacks_; }
};

// Test observer records the actual production query sequence, not a reconstructed
// neighbor loop. No observer is installed during normal gameplay/benchmarks.
using RuntimePlannerPassabilityObserver = void(*)(
    void*, const WorldObjectInstance&, const RuntimePlannerPassabilityKey&,
    const std::unordered_set<int>*, bool, bool);
inline thread_local RuntimePlannerPassabilityObserver runtimePlannerPassabilityObserver = nullptr;
inline thread_local void* runtimePlannerPassabilityObserverContext = nullptr;
class RuntimePlannerPassabilityObserverScope {
    RuntimePlannerPassabilityObserver previous_;
    void* context_;
public:
    RuntimePlannerPassabilityObserverScope(RuntimePlannerPassabilityObserver observer, void* context)
        : previous_(runtimePlannerPassabilityObserver), context_(runtimePlannerPassabilityObserverContext) {
        runtimePlannerPassabilityObserver = observer; runtimePlannerPassabilityObserverContext = context;
    }
    ~RuntimePlannerPassabilityObserverScope() {
        runtimePlannerPassabilityObserver = previous_; runtimePlannerPassabilityObserverContext = context_;
    }
    RuntimePlannerPassabilityObserverScope(const RuntimePlannerPassabilityObserverScope&) = delete;
    RuntimePlannerPassabilityObserverScope& operator=(const RuntimePlannerPassabilityObserverScope&) = delete;
};

bool runtimeCellIsPassable(
    const RuntimeWindowState& state,
    const std::unordered_map<int, const TerrainCell*>& terrainByCell,
    const std::unordered_map<int, int>& occupiedByCell,
    const RuntimeCellPoint& point, const WorldObjectInstance& mover,
    int allowedOccupiedObjectId = -1,
    const std::unordered_set<int>* softOccupiedObjectIds = nullptr,
    bool allowCrushableOccupants = false,
    std::optional<PathMovementMode> movementModeOverride = std::nullopt
) {
    auto* scope = runtimeActivePlannerCellPassability;
    if (!scope && !runtimePlannerPassabilityObserver)
        return runtimeCellIsPassableUncached(state, terrainByCell, occupiedByCell, point,
            mover, allowedOccupiedObjectId, softOccupiedObjectIds, allowCrushableOccupants,
            movementModeOverride);
    const auto scalar = [&] {
        return runtimeCellIsPassableUncached(state, terrainByCell, occupiedByCell, point,
            mover, allowedOccupiedObjectId, softOccupiedObjectIds, allowCrushableOccupants,
            movementModeOverride);
    };
    const bool reusable = scope && softOccupiedObjectIds == nullptr
        && scope->matches(state, mover, terrainByCell, occupiedByCell)
        && !runtimeComponentPassabilityReference();
    if (!reusable && !runtimePlannerPassabilityObserver) return scalar();
    const RuntimePlannerPassabilityKey key{point.x, point.y, allowedOccupiedObjectId,
        allowCrushableOccupants, movementModeOverride.has_value(),
        movementModeOverride.value_or(PathMovementMode::Ground)};
    bool hit = false;
    bool value;
    // Non-null soft sets always use the original membership query, including
    // empty sets and same-address/same-size changes; no content hash is assumed.
    if (reusable)
        value = scope->query(key, scalar, hit);
    else value = scalar();
    if (runtimePlannerPassabilityObserver)
        runtimePlannerPassabilityObserver(runtimePlannerPassabilityObserverContext,
            mover, key, softOccupiedObjectIds, value, hit);
    return value;
}

// Independent C22 ablation. true retains the C21b exact crush keys and its
// infantry-only census; false enables the owned empty-cell crush-key alias.
inline thread_local int runtimeComponentCrushAliasReferenceOverride = -1;
bool runtimeComponentCrushAliasReference() {
    if (runtimeComponentCrushAliasReferenceOverride>=0)
        return runtimeComponentCrushAliasReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_COMPONENT_CRUSH_ALIAS_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeComponentCrushAliasReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentCrushAliasReferenceScope(bool value)
        : previous_(runtimeComponentCrushAliasReferenceOverride) {
        runtimeComponentCrushAliasReferenceOverride=value?1:0;
    }
    ~RuntimeComponentCrushAliasReferenceScope() { runtimeComponentCrushAliasReferenceOverride=previous_; }
    RuntimeComponentCrushAliasReferenceScope(const RuntimeComponentCrushAliasReferenceScope&)=delete;
    RuntimeComponentCrushAliasReferenceScope& operator=(const RuntimeComponentCrushAliasReferenceScope&)=delete;
};

class RuntimePlannerConnectivityKernel;
struct RuntimePlannerQueryLeaseContractAccess;

// Constructed and owned by ONE complete-component kernel, after its explicit
// scope has passed every original identity/shape check. No view is returned or
// borrowed from an unrelated kernel. The complete BFS is synchronously read
// only: invalidate/end BEFORE any source payload, vector, map, mover, rule,
// gate, repair, behavior or reservation edit, even if addresses do not change.
//
// The ordinary wrapper above retains all per-query shape validation. This
// lease only removes repeated shape reads inside that proven lexical lifetime;
// reference, soft inputs, nested/disabled scopes and foreign arguments use the
// ordinary wrapper, including its original scalar/key/observer behavior.
class RuntimePlannerCellPassabilityReadOnlyLease {
    friend class RuntimePlannerConnectivityKernel;
    friend struct RuntimePlannerQueryLeaseContractAccess;
    RuntimePlannerCellPassabilityScope* scope_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const std::unordered_map<int,const TerrainCell*>* terrain_;
    const std::unordered_map<int,int>* occupancy_;
    int tick_;
    bool enabled_ = false;
    std::size_t fullValidations_ = 0;
    mutable std::size_t queries_ = 0, fallbacks_ = 0;
    using FoundationMap=std::unordered_map<int,std::vector<std::size_t>>;
    mutable const FoundationMap* aliasFoundations_=nullptr;
    // unknown / ineligible / eligible. Only the complete read-only lifetime
    // may reuse this answer; raw aliases/outside coordinates never enter it.
    mutable std::vector<std::uint8_t> aliasEligibility_;
    mutable std::size_t crushAliases_=0,aliasQualificationChecks_=0,aliasFoundationBorrows_=0,scalarQueries_=0;
    bool canAliasCrush(const RuntimeWindowState& state,const RuntimeCellPoint& point,int allowed) const {
        if (allowed!=-1 || runtimeComponentCrushAliasReference()
            || !runtimePlannerCensusProvesEmptyCell(state,point)) return false;
        const auto offset=static_cast<std::size_t>(point.y)*512+point.x;
        const auto index=offset/4; const unsigned shift=static_cast<unsigned>(offset%4)*2;
        if (!aliasEligibility_.empty()) {
            const auto stored=(aliasEligibility_[index]>>shift)&3U;
            if (stored) return stored==2U;
        }
        // Do not initialize an original lazy foundation cache earlier than
        // the scalar predicate would. If it is already valid, calling the
        // original helper must return this exact table (including old/stale
        // entries). Borrow it once, not once per edge. An early failing scalar
        // may leave it invalid; that query keeps its original key.
        if (!aliasFoundations_) {
            const auto& objects=state.world.getObjects();
            if (state.foundationCollisionTick!=state.tick
                || state.foundationCollisionObjects!=objects.data()
                || state.foundationCollisionObjectCount!=objects.size()) return false;
            aliasFoundations_=std::addressof(runtimeSolidFoundationsByCell(state));
            ++aliasFoundationBorrows_;
        }
        if (aliasEligibility_.empty()) aliasEligibility_.assign(512U*512U/4U,std::uint8_t{0});
        ++aliasQualificationChecks_;
        const int key=runtimeCellKey(point.x,point.y);
        const bool absent=occupancy_->find(key)==occupancy_->end()
            && aliasFoundations_->find(key)==aliasFoundations_->end();
        aliasEligibility_[index]|=static_cast<std::uint8_t>((absent?2U:1U)<<shift);
        return absent;
    }

    RuntimePlannerCellPassabilityReadOnlyLease(RuntimePlannerCellPassabilityScope* scope,
        const RuntimeWindowState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy, bool enabled)
        : scope_(scope), state_(&state), mover_(&mover), terrain_(&terrain),
          occupancy_(&occupancy), tick_(state.tick) {
        if (!enabled || !scope || runtimeComponentQueryLeaseReference()
            || runtimeComponentPassabilityReference()) return;
        ++fullValidations_;
        enabled_ = runtimeActivePlannerCellPassability == scope
            && scope->matches(state,mover,terrain,occupancy);
    }
    bool matches(const RuntimeWindowState& state, const WorldObjectInstance& mover,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,
        const std::unordered_set<int>* soft) const {
        return enabled_ && scope_ && scope_->enabled_
            && runtimeActivePlannerCellPassability == scope_
            && state_ == &state && mover_ == &mover && terrain_ == &terrain
            && occupancy_ == &occupancy && tick_ == state.tick && soft == nullptr
            && !runtimeComponentQueryLeaseReference()
            && !runtimeComponentPassabilityReference();
    }
public:
    RuntimePlannerCellPassabilityReadOnlyLease(const RuntimePlannerCellPassabilityReadOnlyLease&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease& operator=(const RuntimePlannerCellPassabilityReadOnlyLease&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease(RuntimePlannerCellPassabilityReadOnlyLease&&) = delete;
    RuntimePlannerCellPassabilityReadOnlyLease& operator=(RuntimePlannerCellPassabilityReadOnlyLease&&) = delete;
    void invalidate() noexcept {
        // The kernel owns this memo lifetime too. Do not fall back to a still
        // enabled hot general memo after the caller invalidates for mutation.
        // A reference/disabled lease may have warmed the original wrapper's
        // owner too. Disable only the captured matching owner, independently
        // of whether this lease itself ever became enabled.
        if (scope_ && scope_->state_ == state_ && scope_->mover_ == mover_
            && scope_->terrain_ == terrain_ && scope_->occupancy_ == occupancy_
            && scope_->tick_ == tick_) scope_->invalidate();
        enabled_ = false;
    }
    std::size_t fullValidations() const noexcept { return fullValidations_; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t queries() const noexcept { return queries_; }
    std::size_t fallbacks() const noexcept { return fallbacks_; }
    std::size_t crushAliases() const noexcept { return crushAliases_; }
    std::size_t aliasQualificationChecks() const noexcept { return aliasQualificationChecks_; }
    std::size_t aliasFoundationBorrows() const noexcept { return aliasFoundationBorrows_; }
    std::size_t aliasEligibilityBytes() const noexcept { return aliasEligibility_.size(); }
    std::size_t scalarQueries() const noexcept { return scalarQueries_; }
    bool query(const RuntimeWindowState& state,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy, const RuntimeCellPoint& point,
        const WorldObjectInstance& mover, int allowed = -1,
        const std::unordered_set<int>* soft = nullptr, bool crush = false,
        std::optional<PathMovementMode> mode = std::nullopt) const {
        if (!matches(state,mover,terrain,occupancy,soft)) {
            ++fallbacks_;
            return runtimeCellIsPassable(state,terrain,occupancy,point,mover,allowed,soft,crush,mode);
        }
        ++queries_;
        const RuntimePlannerPassabilityKey key{point.x,point.y,allowed,crush,
            mode.has_value(),mode.value_or(PathMovementMode::Ground)};
        auto memoKey=key;
        if (crush && canAliasCrush(state,point,allowed)) {
            memoKey.crush=false; ++crushAliases_;
        }
        bool hit = false;
        const bool value = scope_->query(memoKey,[&] {
            ++scalarQueries_;
            return runtimeCellIsPassableUncached(state,terrain,occupancy,point,
                mover,allowed,soft,crush,mode);
        },hit);
        if (runtimePlannerPassabilityObserver)
            runtimePlannerPassabilityObserver(runtimePlannerPassabilityObserverContext,
                mover,key,soft,value,hit);
        return value;
    }
};
