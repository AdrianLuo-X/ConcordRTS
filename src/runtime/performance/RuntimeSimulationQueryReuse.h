#pragma once

#include "world/queries/RuntimeObjectIndex.h"

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <vector>

// This switch reverts only this query-reuse change. Existing movement,
// occupancy, renderer and frame-scaling reference switches remain independent.
inline thread_local int runtimeSimulationQueryReuseReferenceOverride = -1;

inline bool runtimeSimulationQueryReuseReference() {
    if (runtimeSimulationQueryReuseReferenceOverride >= 0)
        return runtimeSimulationQueryReuseReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_SIM_QUERY_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeSimulationQueryReuseReferenceScope {
public:
    explicit RuntimeSimulationQueryReuseReferenceScope(bool reference)
        : previous_(runtimeSimulationQueryReuseReferenceOverride) {
        runtimeSimulationQueryReuseReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSimulationQueryReuseReferenceScope() {
        runtimeSimulationQueryReuseReferenceOverride = previous_;
    }
    RuntimeSimulationQueryReuseReferenceScope(const RuntimeSimulationQueryReuseReferenceScope&) = delete;
    RuntimeSimulationQueryReuseReferenceScope& operator=(const RuntimeSimulationQueryReuseReferenceScope&) = delete;
private:
    int previous_;
};

inline constexpr std::size_t runtimeMissingBehaviorObjectOffset = std::numeric_limits<std::size_t>::max();

// Capture offsets once, after the existing reservations phase and before any
// behavior is advanced. IDs are unique in a valid GameWorld, as required by
// RuntimeObjectIndex. Store only alive, not positive-health, objects. Neither
// branch retains pointers or re-finds an initially absent object later in this
// tick; resurrection, append and same-size replacement keep the old semantics.
template <class Objects>
std::vector<std::size_t> buildRuntimeBehaviorObjectOffsets(
    Objects& objects, const std::vector<int>& behaviorIds,
    const RuntimeObjectIndex& objectIndex,
    bool forceReference = runtimeSimulationQueryReuseReference()) {
    std::vector<std::size_t> offsets(behaviorIds.size(), runtimeMissingBehaviorObjectOffset);
    if (forceReference) {
        // The previous implementation: first alive occurrence in world order.
        std::unordered_map<int, std::size_t> byId;
        byId.reserve(objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].alive) byId.emplace(objects[index].instanceId, index);
        for (std::size_t index = 0; index < behaviorIds.size(); ++index) {
            const auto found = byId.find(behaviorIds[index]);
            if (found != byId.end()) offsets[index] = found->second;
        }
    } else {
        for (std::size_t index = 0; index < behaviorIds.size(); ++index) {
            // The ordinary index intentionally treats negative IDs as absent.
            // Preserve the old behavior-loop semantics for malformed/sentinel
            // behavior entries without scanning history for valid positive IDs.
            if (behaviorIds[index] < 0) {
                for (std::size_t worldIndex = 0; worldIndex < objects.size(); ++worldIndex) {
                    if (objects[worldIndex].alive && objects[worldIndex].instanceId == behaviorIds[index]) {
                        offsets[index] = worldIndex;
                        break;
                    }
                }
                continue;
            }
            const auto* object = objectIndex.find(objects, behaviorIds[index]);
            if (object && object->alive)
                offsets[index] = static_cast<std::size_t>(object - objects.data());
        }
    }
    return offsets;
}

template <class Objects>
auto runtimeBehaviorObjectAtInitialOffset(Objects& objects, int objectId, std::size_t offset)
    -> decltype(objects.data()) {
    if (offset >= objects.size()) return nullptr;
    auto& object = objects[offset];
    return object.alive && object.instanceId == objectId ? &object : nullptr;
}

// The builder checks for an empty static snapshot before checking out storage.
// An old returned snapshot expires at the next builder call, as documented by
// the caller. Keep the working buffer local and invalid until its completed
// overlay and cache keys have been committed; other derived queries must not
// observe a valid cache containing a partially rewritten snapshot.
template <class Cell>
std::vector<Cell> takeRuntimePathSnapshotScratch(
    std::vector<Cell>& cachedCells, bool& dynamicValid,
    const std::vector<Cell>& staticCells,
    bool forceReference = runtimeSimulationQueryReuseReference()) {
    if (forceReference || staticCells.empty()) return staticCells;
    dynamicValid = false;
    std::vector<Cell> scratch;
    scratch.swap(cachedCells);
    scratch.assign(staticCells.begin(), staticCells.end());
    return scratch;
}

// Both contexts have just been populated in the path-batch gathering phase.
// No world/rule/movement-affecting mutation may occur before this comparison.
// Deferring the original hash expressions keeps the caller's short-circuit
// gates and makes the reference branch genuinely execute the old queries.
template <class Context, class FirstHash, class CandidateHash>
bool runtimeMoveBatchConstraintsMatch(
    const Context& first, const Context& candidate,
    FirstHash firstHash, CandidateHash candidateHash,
    bool forceReference = runtimeSimulationQueryReuseReference()) {
    return forceReference ? firstHash() == candidateHash()
        : first.movementConstraintHash == candidate.movementConstraintHash;
}
