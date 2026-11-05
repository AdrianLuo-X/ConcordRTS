#include "simulation/vision/RuntimeVisionPhase.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "world/queries/RuntimeHistoryQuery.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <utility>

using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::toLowerAsciiLocal;
bool runtimeVisionOwnerIsNeutral(const std::string& owner) {
    const std::string normalized =
        toUpperAsciiLocal(trimAsciiLocal(owner));
    return normalized.empty()
        || normalized == "NEUTRAL"
        || normalized == "CIVILIAN"
        || normalized == "SPECIAL";
}

bool updateRuntimePublicBuildingLocations(RuntimeVisionPhaseContext& state,
    RuntimeVisionPhaseOperations& operations,
    const std::vector<RuntimeVisionObserverDefinition>& observers) {
    if (!state.rulesLoaded) return false;
    bool changed = false;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0 || object.rubble
            || object.kind != WorldObjectKind::Structure || object.buildAnimTicksRemaining > 0) continue;
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || !building->revealToAll) continue;
        const auto owner = operations.canonicalHumanOwner(object.owner);
        if (owner.empty()) continue;
        const RuntimeCellPoint cell{object.x, object.y};
        auto found = state.publicBuildingLocations.find(object.instanceId);
        if (found == state.publicBuildingLocations.end()
            || found->second.owner != owner || found->second.cell != cell) {
            RuntimePublicBuildingLocation location;
            location.owner = owner; location.cell = cell; location.noticeTick = state.tick;
            state.publicBuildingLocations[object.instanceId] = location;
            if (owner != operations.canonicalHumanOwner(state.localOwner)) {
                operations.notifyPublicBuildingLocation(cell);
            }
            changed = true;
        }
        // Retail exposes CurrentSight + 3 cells and reapplies public locations
        // after reshrouding. This is exploration, not continuing unit sight.
        const int radius = std::clamp(building->sight, 0, 509) + 3;
        for (const auto& observer : observers) {
            if (operations.canonicalHumanOwner(observer.owner) == owner) continue;
            changed = state.vision.exploreArea(observer.owner, cell.x, cell.y, radius) || changed;
        }
    }
    return changed;
}

bool updateRuntimeVisionState(RuntimeVisionPhaseContext& state, RuntimeVisionPhaseOperations& operations) {
    RuntimePerfTraceScope trace("updateRuntimeVisionState", state.tick);
    if (!state.visionEnabled) {
        return false;
    }
    struct VisionStageTimer {
        double& elapsed;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~VisionStageTimer() {
            elapsed += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    } visionStageTimer{state.lastSimVisionMs};

    std::map<std::string, std::string> ownersByCanonicalId;
    const auto addOwner = [&ownersByCanonicalId](const std::string& owner) {
        const std::string trimmed = trimAsciiLocal(owner);
        if (trimmed.empty() || runtimeVisionOwnerIsNeutral(trimmed)) {
            return;
        }
        ownersByCanonicalId.try_emplace(
            toLowerAsciiLocal(trimmed),
            trimmed
        );
    };
    addOwner(state.localOwner);
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        if (state.config.playerSlots[slotIndex].enabled) {
            addOwner(operations.ownerForSlot(slotIndex));
        }
    }
    runtimeForEachDistinctObjectOwner(state.world.getObjects(), addOwner);

    std::vector<RuntimeVisionObserverDefinition> observers;
    observers.reserve(ownersByCanonicalId.size());
    std::vector<std::string> openingRevealOwners;
    const bool landRush = operations.isLandRushMode();
    for (const auto& [ownerKey, owner] : ownersByCanonicalId) {
        (void)ownerKey;
        if (landRush && !state.vision.hasObserver(owner)) {
            openingRevealOwners.push_back(owner);
        }
        RuntimeVisionObserverDefinition observer;
        observer.owner = owner;
        for (const auto& [candidateKey, candidate] : ownersByCanonicalId) {
            (void)candidateKey;
            if (operations.ownersAreAllied(owner, candidate)) {
                observer.sharedVisionOwners.push_back(candidate);
            }
        }
        observers.push_back(std::move(observer));
    }

    const std::uint64_t previousLocalRevision =
        state.vision.revisionFor(state.localOwner);
    bool changed = state.vision.update(
        state.world,
        state.rulesLoaded ? &state.rules : nullptr,
        observers,
        state.tick
    );
    // Land Rush removes the opening shroud for every house, including remote
    // humans. Existing observers keep their saved exploration and later shroud
    // changes. Rebuild current sight after revealing so Gap Generators retain
    // their normal behavior and a reveal never grants permanent nearby sight.
    for (const std::string& owner : openingRevealOwners) {
        changed = state.vision.revealMap(owner) || changed;
    }
    if (!openingRevealOwners.empty()) {
        changed = state.vision.update(state.world,
            state.rulesLoaded ? &state.rules : nullptr, observers, state.tick) || changed;
    }
    changed = updateRuntimePublicBuildingLocations(state, operations, observers) || changed;
    const std::uint64_t localRevision =
        state.vision.revisionFor(state.localOwner);
    if (localRevision != previousLocalRevision) {
        operations.invalidateLocalProjection();
    }
    return changed;
}
