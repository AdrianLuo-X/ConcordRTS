#include <cstdlib>
#include <cstdint>
#include <optional>







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





// One advanceRuntimeRenderMove invocation only. Rules, config/report, theater,
// map facts and resource identity must not change between before/after poses.
// Object pose fields intentionally change, and are reread on every query.





// Only the lexical behavior loop may loan metadata. Actors, rules queries,
// poses, MapQuery results, bridge state and sound decisions are never cached.
// In-place replacement of source metadata must invalidate before mutation;
// pointer/count checks alone cannot detect a same-address replacement.





