#include <cstdlib>
#include <cstdint>
#include <optional>

struct RuntimeInfantryWaterPose {
    bool valid = false;
    bool swimming = false;
    double x = 0.0;
    double y = 0.0;
};

thread_local int runtimeInfantryWaterContextReferenceOverride = -1;

bool runtimeInfantryWaterContextReference() {
    if (runtimeInfantryWaterContextReferenceOverride >= 0)
        return runtimeInfantryWaterContextReferenceOverride != 0;
    static const bool environmentReference = [] {
        const char* value = std::getenv("CONCORD_INFANTRY_WATER_CONTEXT_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return environmentReference;
}

class RuntimeInfantryWaterContextReferenceScope {
public:
    explicit RuntimeInfantryWaterContextReferenceScope(bool reference)
        : previous_(runtimeInfantryWaterContextReferenceOverride) {
        runtimeInfantryWaterContextReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeInfantryWaterContextReferenceScope() {
        runtimeInfantryWaterContextReferenceOverride = previous_;
    }
    RuntimeInfantryWaterContextReferenceScope(const RuntimeInfantryWaterContextReferenceScope&) = delete;
    RuntimeInfantryWaterContextReferenceScope& operator=(const RuntimeInfantryWaterContextReferenceScope&) = delete;
private:
    int previous_;
};

// A second, independent switch preserves the C16 per-render-move query while
// bypassing only the behavior-loop metadata loan.
thread_local int runtimeInfantryWaterBatchContextReferenceOverride = -1;
bool runtimeInfantryWaterBatchContextReference() {
    if (runtimeInfantryWaterBatchContextReferenceOverride >= 0)
        return runtimeInfantryWaterBatchContextReferenceOverride != 0;
    static const bool environmentReference = [] {
        const char* value = std::getenv("CONCORD_INFANTRY_WATER_BATCH_CONTEXT_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return environmentReference;
}
class RuntimeInfantryWaterBatchContextReferenceScope {
public:
    explicit RuntimeInfantryWaterBatchContextReferenceScope(bool reference)
        : previous_(runtimeInfantryWaterBatchContextReferenceOverride) {
        runtimeInfantryWaterBatchContextReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeInfantryWaterBatchContextReferenceScope() {
        runtimeInfantryWaterBatchContextReferenceOverride = previous_;
    }
    RuntimeInfantryWaterBatchContextReferenceScope(const RuntimeInfantryWaterBatchContextReferenceScope&) = delete;
    RuntimeInfantryWaterBatchContextReferenceScope& operator=(const RuntimeInfantryWaterBatchContextReferenceScope&) = delete;
private:
    int previous_;
};

// Contract-only witness, at the actual construction sites rather than on every
// pose. No counters or observer are stored in authoritative state.
thread_local std::uint64_t* runtimeInfantryWaterContextConstructionObserver = nullptr;
class RuntimeInfantryWaterContextConstructionScope {
public:
    explicit RuntimeInfantryWaterContextConstructionScope(std::uint64_t& count)
        : previous_(runtimeInfantryWaterContextConstructionObserver) {
        runtimeInfantryWaterContextConstructionObserver = &count;
    }
    ~RuntimeInfantryWaterContextConstructionScope() {
        runtimeInfantryWaterContextConstructionObserver = previous_;
    }
    RuntimeInfantryWaterContextConstructionScope(const RuntimeInfantryWaterContextConstructionScope&) = delete;
    RuntimeInfantryWaterContextConstructionScope& operator=(const RuntimeInfantryWaterContextConstructionScope&) = delete;
private:
    std::uint64_t* previous_;
};
void runtimeObserveInfantryWaterContextConstruction() {
    if (runtimeInfantryWaterContextConstructionObserver)
        ++*runtimeInfantryWaterContextConstructionObserver;
}
const RuntimeMapRenderContext* runtimeBorrowInfantryWaterMetadataContext(RuntimeWindowState& state);

RuntimeInfantryWaterPose runtimeInfantryWaterPose(
    RuntimeWindowState& state, const WorldObjectInstance& object
) {
    RuntimeInfantryWaterPose pose;
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || !object.alive || object.health <= 0) return pose;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (!unit || (unit->enterWaterSound.empty() && unit->leaveWaterSound.empty())) return pose;
    pose.x = object.hasRenderPosition ? object.renderX : object.x;
    pose.y = object.hasRenderPosition ? object.renderY : object.y;
    if (!std::isfinite(pose.x) || !std::isfinite(pose.y)
        || pose.x < 0 || pose.x >= 511.5 || pose.y < 0 || pose.y >= 511.5
        || !MapQuery(state.mapFacts).getCell(static_cast<int>(std::floor(pose.x + 0.5)),
            static_cast<int>(std::floor(pose.y + 0.5)))) return pose;
    auto context = runtimeTerrainMetadataContext(state);
    runtimeObserveInfantryWaterContextConstruction();
    context.mapFacts = &state.mapFacts;
    pose.valid = true;
    pose.swimming = resolveRuntimeInfantrySwimming(object, context, state.renderCache);
    return pose;
}

// One advanceRuntimeRenderMove invocation only. Rules, config/report, theater,
// map facts and resource identity must not change between before/after poses.
// Object pose fields intentionally change, and are reread on every query.
class RuntimeInfantryWaterPoseQuery {
public:
    explicit RuntimeInfantryWaterPoseQuery(RuntimeWindowState& state,
        bool reference = runtimeInfantryWaterContextReference())
        : state_(state), reference_(reference) {}
    RuntimeInfantryWaterPoseQuery(const RuntimeInfantryWaterPoseQuery&) = delete;
    RuntimeInfantryWaterPoseQuery& operator=(const RuntimeInfantryWaterPoseQuery&) = delete;

    RuntimeInfantryWaterPose pose(const WorldObjectInstance& object) {
        if (reference_) return runtimeInfantryWaterPose(state_, object);
        RuntimeInfantryWaterPose result;
        if (object.kind != WorldObjectKind::Infantry || object.airborne
            || !object.alive || object.health <= 0) return result;
        const auto* unit = findRuntimeUnitType(state_.rules, object);
        if (!unit || (unit->enterWaterSound.empty() && unit->leaveWaterSound.empty())) return result;
        result.x = object.hasRenderPosition ? object.renderX : object.x;
        result.y = object.hasRenderPosition ? object.renderY : object.y;
        if (!std::isfinite(result.x) || !std::isfinite(result.y)
            || result.x < 0 || result.x >= 511.5 || result.y < 0 || result.y >= 511.5
            || !MapQuery(state_.mapFacts).getCell(static_cast<int>(std::floor(result.x + 0.5)),
                static_cast<int>(std::floor(result.y + 0.5)))) return result;
        const auto* context = runtimeBorrowInfantryWaterMetadataContext(state_);
        if (!context) context = &metadataContext();
        result.valid = true;
        result.swimming = resolveRuntimeInfantrySwimming(object, *context, state_.renderCache);
        return result;
    }

    const RuntimeMapRenderContext& metadataContext() {
        if (!context_) {
            context_.emplace(runtimeTerrainMetadataContext(state_));
            context_->mapFacts = &state_.mapFacts;
            runtimeObserveInfantryWaterContextConstruction();
        }
        return *context_;
    }

    bool matches(const RuntimeWindowState& state) const { return &state_ == &state; }
    bool hasContext() const { return context_.has_value(); }
    bool reference() const { return reference_; }
private:
    RuntimeWindowState& state_;
    bool reference_;
    std::optional<RuntimeMapRenderContext> context_;
};

class RuntimeInfantryWaterBehaviorContextScope;
thread_local RuntimeInfantryWaterBehaviorContextScope* runtimeActiveInfantryWaterBehaviorContext = nullptr;

// Only the lexical behavior loop may loan metadata. Actors, rules queries,
// poses, MapQuery results, bridge state and sound decisions are never cached.
// In-place replacement of source metadata must invalidate before mutation;
// pointer/count checks alone cannot detect a same-address replacement.
class RuntimeInfantryWaterBehaviorContextScope {
public:
    explicit RuntimeInfantryWaterBehaviorContextScope(RuntimeWindowState& state, bool enabled = true)
        : state_(state), tick_(state.tick), enabled_(enabled),
          previous_(runtimeActiveInfantryWaterBehaviorContext) {
        runtimeActiveInfantryWaterBehaviorContext = this;
    }
    ~RuntimeInfantryWaterBehaviorContextScope() {
        runtimeActiveInfantryWaterBehaviorContext = previous_;
    }
    RuntimeInfantryWaterBehaviorContextScope(const RuntimeInfantryWaterBehaviorContextScope&) = delete;
    RuntimeInfantryWaterBehaviorContextScope& operator=(const RuntimeInfantryWaterBehaviorContextScope&) = delete;

    void invalidate() { enabled_ = false; }
    bool hasContext() const { return query_.has_value() && query_->hasContext(); }
    const RuntimeMapRenderContext* borrow(RuntimeWindowState& state) {
        if (!enabled_ || &state != &state_) return nullptr;
        if (state.tick != tick_) { invalidate(); return nullptr; }
        if (!source_) source_.emplace(state);
        else if (!source_->matches(state)) { invalidate(); return nullptr; }
        if (!query_) query_.emplace(state, false);
        return &query_->metadataContext();
    }
private:
    struct Source {
        explicit Source(const RuntimeWindowState& state)
            : root(state.config.localDataRootPath), mapPath(state.report.map.path),
              theaterLoaded(state.theaterLoaded), tileSets(state.theater.getTileSets().data()),
              tileSetCount(state.theater.getTileSets().size()),
              tileSlots(state.theater.getTotalTileSlots()), extension(state.theater.getTileExtension()),
              cells(state.mapFacts.cells.data()), cellCount(state.mapFacts.cells.size()),
              mapHash(state.mapFacts.contentHash), lookupSize(state.mapFacts.cellIndexLookup.size()),
              lookupBuckets(state.mapFacts.cellIndexLookup.bucket_count()) {}
        bool matches(const RuntimeWindowState& state) const {
            return root == state.config.localDataRootPath && mapPath == state.report.map.path
                && theaterLoaded == state.theaterLoaded
                && tileSets == state.theater.getTileSets().data()
                && tileSetCount == state.theater.getTileSets().size()
                && tileSlots == state.theater.getTotalTileSlots()
                && extension == state.theater.getTileExtension()
                && cells == state.mapFacts.cells.data() && cellCount == state.mapFacts.cells.size()
                && mapHash == state.mapFacts.contentHash
                && lookupSize == state.mapFacts.cellIndexLookup.size()
                && lookupBuckets == state.mapFacts.cellIndexLookup.bucket_count();
        }
        std::string root, mapPath;
        bool theaterLoaded;
        const TheaterTileSet* tileSets;
        std::size_t tileSetCount, tileSlots;
        std::string extension;
        const MapCellFacts* cells;
        std::size_t cellCount;
        std::uint64_t mapHash;
        std::size_t lookupSize, lookupBuckets;
    };
    RuntimeWindowState& state_;
    int tick_;
    bool enabled_;
    RuntimeInfantryWaterBehaviorContextScope* previous_;
    std::optional<Source> source_;
    std::optional<RuntimeInfantryWaterPoseQuery> query_;
};

const RuntimeMapRenderContext* runtimeBorrowInfantryWaterMetadataContext(RuntimeWindowState& state) {
    // An absent or disabled inner scope masks the outer scope. The general
    // C16 query does no source comparisons without an active batch scope.
    if (!runtimeActiveInfantryWaterBehaviorContext || runtimeInfantryWaterContextReference()
        || runtimeInfantryWaterBatchContextReference()) return nullptr;
    return runtimeActiveInfantryWaterBehaviorContext->borrow(state);
}

void emitRuntimeInfantryWaterTransition(
    RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeInfantryWaterPose& before,
    RuntimeInfantryWaterPoseQuery* query = nullptr
) {
    if (!before.valid) return;
    const auto after = query && query->matches(state)
        ? query->pose(object) : runtimeInfantryWaterPose(state, object);
    // Only a real walking/swimming step can emit. Loading, spawning, bridge
    // changes in place and teleportation do not replay an entry announcement.
    if (!after.valid || before.swimming == after.swimming
        || (before.x == after.x && before.y == after.y)
        || std::abs(after.x - before.x) > 1.0 || std::abs(after.y - before.y) > 1.0
        || !runtimeObjectVisibleToOwner(state, state.economy.owner, object)) return;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    const auto sound = trimAsciiLocal(after.swimming ? unit->enterWaterSound : unit->leaveWaterSound);
    if (!sound.empty() && toLowerAsciiLocal(sound) != "none" && toLowerAsciiLocal(sound) != "<none>")
        emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(object));
}
