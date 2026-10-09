// The reference keeps the original query order for equivalence checks.
std::string runtimeIdleVariantMissionForObjectReference(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object
) {
    if (runtimeObjectHasDeployedState(state, object)) {
        return "";
    }

    const ArtType* art = runtimeArtTypeForObject(state, object);
    if (art == nullptr) {
        return "";
    }

    const bool idle1Safe = runtimeIdleSequenceLooksSafe(*art, "Idle1");
    const bool idle2Safe = runtimeIdleSequenceLooksSafe(*art, "Idle2");
    if (!idle1Safe && !idle2Safe) {
        return "";
    }

    const int periodTicks =
        std::max(90, state.config.fixedTickHz * 6);
    const int activeTicks =
        std::max(18, state.config.fixedTickHz);
    const int phase =
        (state.tick + std::max(0, object.instanceId) * 37)
        % periodTicks;
    if (phase >= activeTicks) {
        return "";
    }
    if (idle1Safe && idle2Safe) {
        return ((object.instanceId + state.tick / periodTicks) % 2 == 0)
            ? "Idle1"
            : "Idle2";
    }
    return idle1Safe ? "Idle1" : "Idle2";
}

struct RuntimeIdleVariantPhase {
    int periodTicks = 90;
    int activeTicks = 18;
    int phase = 0;
};

std::optional<RuntimeIdleVariantPhase> runtimeSafeIdleVariantPhase(
    int tick, int fixedTickHz, int instanceId
) {
    // Hoisting must not evaluate an overflowing expression which the original
    // art/deployed guards could have skipped. Abnormal inputs keep that order.
    const std::int64_t periodProduct = std::int64_t{fixedTickHz} * 6;
    const std::int64_t idProduct = std::int64_t{std::max(0, instanceId)} * 37;
    const std::int64_t numerator = std::int64_t{tick} + idProduct;
    const auto fitsInt = [](std::int64_t value) {
        return value >= std::numeric_limits<int>::min()
            && value <= std::numeric_limits<int>::max();
    };
    if (!fitsInt(periodProduct) || !fitsInt(idProduct) || !fitsInt(numerator)) {
        return std::nullopt;
    }
    RuntimeIdleVariantPhase result;
    result.periodTicks = std::max(90, static_cast<int>(periodProduct));
    result.activeTicks = std::max(18, fixedTickHz);
    result.phase = static_cast<int>(numerator) % result.periodTicks;
    return result;
}

// Only the enclosing idle update owns this cache. That pass changes missions,
// never art, theater or rules; a new invocation observes all external changes.
struct RuntimeIdleVariantPass {
    std::unordered_map<const ArtType*, std::pair<bool, bool>> safetyByArt;
    std::size_t safetyEvaluations = 0;
    std::size_t cacheHits = 0;

    std::pair<bool, bool> safetyFor(const ArtType& art) {
        const auto found = safetyByArt.find(&art);
        if (found != safetyByArt.end()) {
            ++cacheHits;
            return found->second;
        }
        ++safetyEvaluations;
        const std::pair<bool, bool> result{
            runtimeIdleSequenceLooksSafe(art, "Idle1"),
            runtimeIdleSequenceLooksSafe(art, "Idle2")};
        safetyByArt.emplace(&art, result);
        return result;
    }
};

std::string runtimeIdleVariantMissionForObject(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    bool forceReference = false,
    RuntimeIdleVariantPass* pass = nullptr
) {
    if (forceReference || runtimeFrameScalingReferenceMovement()) {
        return runtimeIdleVariantMissionForObjectReference(state, object);
    }
    if (runtimeObjectHasDeployedState(state, object)) {
        return "";
    }
    const auto phase = runtimeSafeIdleVariantPhase(
        state.tick, state.config.fixedTickHz, object.instanceId);
    if (!phase) {
        return runtimeIdleVariantMissionForObjectReference(state, object);
    }
    if (phase->phase >= phase->activeTicks) {
        return "";
    }
    const ArtType* art = runtimeArtTypeForObject(state, object);
    if (art == nullptr) {
        return "";
    }
    const auto safety = pass ? pass->safetyFor(*art) : std::pair<bool, bool>{
        runtimeIdleSequenceLooksSafe(*art, "Idle1"),
        runtimeIdleSequenceLooksSafe(*art, "Idle2")};
    if (safety.first && safety.second) {
        return ((object.instanceId + state.tick / phase->periodTicks) % 2 == 0)
            ? "Idle1" : "Idle2";
    }
    return safety.first ? "Idle1" : (safety.second ? "Idle2" : "");
}
