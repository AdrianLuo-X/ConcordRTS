// Included immediately after the unchanged full signature implementation.
// This is an explicit immutable-input lease, not a pointer/count mutation epoch.
// Only the WM_PAINT capture entry may publish; the projection entry consumes
// that complete signature at most once, before any interaction drawing occurs.
inline thread_local int runtimePaintProjectionSignatureReferenceOverride = -1;
inline bool runtimePaintProjectionSignatureReference() {
    if (runtimePaintProjectionSignatureReferenceOverride >= 0)
        return runtimePaintProjectionSignatureReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PAINT_PROJECTION_SIGNATURE_REFERENCE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return reference;
}
struct RuntimePaintProjectionSignatureReferenceScope {
    int previous = runtimePaintProjectionSignatureReferenceOverride;
    explicit RuntimePaintProjectionSignatureReferenceScope(bool reference) noexcept {
        runtimePaintProjectionSignatureReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePaintProjectionSignatureReferenceScope() {
        runtimePaintProjectionSignatureReferenceOverride = previous;
    }
    RuntimePaintProjectionSignatureReferenceScope(const RuntimePaintProjectionSignatureReferenceScope&) = delete;
    RuntimePaintProjectionSignatureReferenceScope& operator=(const RuntimePaintProjectionSignatureReferenceScope&) = delete;
};

struct RuntimePaintProjectionSignatureObservation {
    std::uint64_t captureQueries = 0;
    std::uint64_t projectionQueries = 0;
    std::uint64_t scalarQueries = 0;
    std::uint64_t published = 0;
    std::uint64_t hits = 0;
    std::uint64_t invalidations = 0;
};
inline thread_local RuntimePaintProjectionSignatureObservation* runtimePaintProjectionSignatureObservation = nullptr;
struct RuntimePaintProjectionSignatureObservationScope {
    RuntimePaintProjectionSignatureObservation* previous = runtimePaintProjectionSignatureObservation;
    explicit RuntimePaintProjectionSignatureObservationScope(RuntimePaintProjectionSignatureObservation& observation) noexcept {
        runtimePaintProjectionSignatureObservation = &observation;
    }
    ~RuntimePaintProjectionSignatureObservationScope() { runtimePaintProjectionSignatureObservation = previous; }
    RuntimePaintProjectionSignatureObservationScope(const RuntimePaintProjectionSignatureObservationScope&) = delete;
    RuntimePaintProjectionSignatureObservationScope& operator=(const RuntimePaintProjectionSignatureObservationScope&) = delete;
};

class RuntimePaintProjectionSignatureScope {
public:
    inline static thread_local RuntimePaintProjectionSignatureScope* active = nullptr;
    // readonlyInputs=true is supplied only after all authority/snapshot refreshes
    // and for the closed, non-pumping map -> initial projection call chain.
    explicit RuntimePaintProjectionSignatureScope(const RuntimeWindowState& state,
        bool readonlyInputs = false) noexcept
        : previous_(active), state_(&state), enabled_(readonlyInputs && previous_ == nullptr
            && !runtimePaintProjectionSignatureReference()),
          objects_(state.world.getObjects().data()), objectCount_(state.world.getObjects().size()),
          terrainObjects_(state.world.getTerrainObjects().data()),
          terrainObjectCount_(state.world.getTerrainObjects().size()),
          impacts_(state.impactEffects.data()), impactCount_(state.impactEffects.size()),
          owner_(state.economy.owner.data()), ownerSize_(state.economy.owner.size()),
          tick_(state.tick), visionEnabled_(state.visionEnabled),
          specialInitialized_(state.specialMechanismsInitialized), gatesInitialized_(state.gatesInitialized),
          historyReference_(runtimeHistoryQueryReference()) {
        // A nested lease is deliberately unsupported. Revoke the outer result
        // before masking it, so it cannot survive an unproved nested source use.
        if (previous_) previous_->invalidate();
        active = this;
    }
    ~RuntimePaintProjectionSignatureScope() { active = previous_; }
    RuntimePaintProjectionSignatureScope(const RuntimePaintProjectionSignatureScope&) = delete;
    RuntimePaintProjectionSignatureScope& operator=(const RuntimePaintProjectionSignatureScope&) = delete;

    void invalidate() noexcept {
        if (enabled_ && runtimePaintProjectionSignatureObservation)
            ++runtimePaintProjectionSignatureObservation->invalidations;
        enabled_ = false;
        ready_ = false;
    }
    bool accepts(const RuntimeWindowState& state) noexcept {
        if (!enabled_ || state_ != &state || active != this) return false;
        // Defensive structural checks only. Same-address field writes/order
        // changes still require invalidate(), and are not inferred from these.
        if (objects_ != state.world.getObjects().data() || objectCount_ != state.world.getObjects().size()
            || terrainObjects_ != state.world.getTerrainObjects().data()
            || terrainObjectCount_ != state.world.getTerrainObjects().size()
            || impacts_ != state.impactEffects.data() || impactCount_ != state.impactEffects.size()
            || owner_ != state.economy.owner.data() || ownerSize_ != state.economy.owner.size()
            || tick_ != state.tick || visionEnabled_ != state.visionEnabled
            || specialInitialized_ != state.specialMechanismsInitialized
            || gatesInitialized_ != state.gatesInitialized
            || historyReference_ != runtimeHistoryQueryReference()) {
            invalidate();
            return false;
        }
        return true;
    }
    void publish(const RuntimeWindowState& state, unsigned long long signature) noexcept {
        if (!accepts(state)) return;
        signature_ = signature;
        ready_ = true;
        if (runtimePaintProjectionSignatureObservation)
            ++runtimePaintProjectionSignatureObservation->published;
    }
    bool consume(const RuntimeWindowState& state, unsigned long long& signature) noexcept {
        if (!accepts(state) || !ready_) return false;
        signature = signature_;
        // No later UI/query can borrow this capture's result.
        enabled_ = false;
        ready_ = false;
        if (runtimePaintProjectionSignatureObservation)
            ++runtimePaintProjectionSignatureObservation->hits;
        return true;
    }
private:
    RuntimePaintProjectionSignatureScope* previous_;
    const RuntimeWindowState* state_;
    bool enabled_;
    bool ready_ = false;
    unsigned long long signature_ = 0;
    const WorldObjectInstance* objects_;
    std::size_t objectCount_;
    const WorldTerrainObject* terrainObjects_;
    std::size_t terrainObjectCount_;
    const RuntimeImpactEffect* impacts_;
    std::size_t impactCount_;
    const char* owner_;
    std::size_t ownerSize_;
    int tick_;
    bool visionEnabled_, specialInitialized_, gatesInitialized_, historyReference_;
};
static_assert(sizeof(RuntimePaintProjectionSignatureScope) <= 192,
    "paint signature lease must remain fixed-size and allocation-free");

unsigned long long runtimeProjectedObjectsSignature(const RuntimeWindowState& state) {
    auto* observation = runtimePaintProjectionSignatureObservation;
    if (observation) ++observation->projectionQueries;
    unsigned long long result = 0;
    if (RuntimePaintProjectionSignatureScope::active
        && RuntimePaintProjectionSignatureScope::active->consume(state, result)) return result;
    if (observation) ++observation->scalarQueries;
    return runtimeProjectedObjectsSignatureUnmemoized(state);
}

unsigned long long runtimeProjectedObjectsSignatureForPaintCapture(const RuntimeWindowState& state) {
    auto* observation = runtimePaintProjectionSignatureObservation;
    if (observation) { ++observation->captureQueries; ++observation->scalarQueries; }
    // The complete original algorithm is always run for capture. Publish only
    // after successful completion; exceptions retain the original semantics.
    const auto result = runtimeProjectedObjectsSignatureUnmemoized(state);
    if (RuntimePaintProjectionSignatureScope::active)
        RuntimePaintProjectionSignatureScope::active->publish(state, result);
    return result;
}
