// Included immediately before RuntimeGeometryQueryFrame. No persistent index,
// world ownership, allocation, clock, or gameplay state is introduced here.
static thread_local std::optional<bool> runtimePaintSpecialObjectIdReferenceOverride;
static bool runtimePaintSpecialObjectIdReference() {
    if (runtimePaintSpecialObjectIdReferenceOverride)
        return *runtimePaintSpecialObjectIdReferenceOverride;
    const char* flag = runtimeRendererEnvironmentValue("CONCORD_PAINT_SPECIAL_OBJECT_ID_REFERENCE");
    return flag && flag[0] == '1' && flag[1] == '\0';
}
struct RuntimePaintSpecialObjectIdReferenceScope {
    std::optional<bool> previous = runtimePaintSpecialObjectIdReferenceOverride;
    explicit RuntimePaintSpecialObjectIdReferenceScope(bool enabled) {
        runtimePaintSpecialObjectIdReferenceOverride = enabled;
    }
    ~RuntimePaintSpecialObjectIdReferenceScope() {
        runtimePaintSpecialObjectIdReferenceOverride = previous;
    }
    RuntimePaintSpecialObjectIdReferenceScope(const RuntimePaintSpecialObjectIdReferenceScope&) = delete;
    RuntimePaintSpecialObjectIdReferenceScope& operator=(const RuntimePaintSpecialObjectIdReferenceScope&) = delete;
};

struct RuntimePaintSpecialObjectIdObservation {
    std::uint64_t queries = 0, negativeQueries = 0, scalarQueries = 0;
    std::uint64_t scannedObjects = 0, hits = 0, negativeHits = 0, missingHits = 0;
    std::uint64_t scopeFallbacks = 0, capacityFallbacks = 0;
};
static thread_local RuntimePaintSpecialObjectIdObservation* runtimePaintSpecialObjectIdObservation = nullptr;
struct RuntimePaintSpecialObjectIdObservationScope {
    RuntimePaintSpecialObjectIdObservation* previous = runtimePaintSpecialObjectIdObservation;
    explicit RuntimePaintSpecialObjectIdObservationScope(RuntimePaintSpecialObjectIdObservation& value) {
        runtimePaintSpecialObjectIdObservation = &value;
    }
    ~RuntimePaintSpecialObjectIdObservationScope() { runtimePaintSpecialObjectIdObservation = previous; }
    RuntimePaintSpecialObjectIdObservationScope(const RuntimePaintSpecialObjectIdObservationScope&) = delete;
    RuntimePaintSpecialObjectIdObservationScope& operator=(const RuntimePaintSpecialObjectIdObservationScope&) = delete;
};

class RuntimePaintSpecialObjectIdMemo {
public:
    static constexpr std::size_t capacity = 64;
    explicit RuntimePaintSpecialObjectIdMemo(const GameWorld& world)
        : world_(&world), data_(world.getObjects().data()), count_(world.getObjects().size()) {}
    RuntimePaintSpecialObjectIdMemo(const RuntimePaintSpecialObjectIdMemo&) = delete;
    RuntimePaintSpecialObjectIdMemo& operator=(const RuntimePaintSpecialObjectIdMemo&) = delete;
    bool matches(const GameWorld& world) {
        if (!enabled_ || &world != world_) return false;
        // These are defensive structural checks, not an in-place mutation epoch.
        // The existing paint lease forbids *all* object/ID/order mutations. A
        // caller leaving that contract must invalidate or end the scope first.
        if (world.getObjects().data() != data_ || world.getObjects().size() != count_) {
            invalidate(); return false;
        }
        return true;
    }
    bool find(int objectId, const WorldObjectInstance*& result) const {
        for (std::size_t i = 0; i < used_; ++i) if (entries_[i].objectId == objectId) {
            result = entries_[i].result; return true;
        }
        return false;
    }
    bool remember(int objectId, const WorldObjectInstance* result) {
        if (used_ == capacity) return false;
        // Only publish the completed original first-match query. nullptr is a
        // resolved miss; negative IDs and dead objects are not special-cased.
        entries_[used_++] = {objectId, result}; return true;
    }
    void invalidate() { enabled_ = false; used_ = 0; }
    std::size_t size() const { return used_; }
private:
    struct Entry { int objectId = 0; const WorldObjectInstance* result = nullptr; };
    const GameWorld* world_;
    const WorldObjectInstance* data_;
    std::size_t count_, used_ = 0;
    bool enabled_ = true;
    std::array<Entry, capacity> entries_{};
};
static_assert(sizeof(RuntimePaintSpecialObjectIdMemo) <= 1152,
    "The paint ID memo must stay a small fixed array, not a full world index.");
