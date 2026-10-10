#include "runtime/performance/RuntimeSimulationQueryReuse.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/diagnostics/RuntimePathRepresentativeOccupantsContract.inl"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

bool runRuntimeSimulationQueryReusePrimitiveContract(std::string& error) {
    if (!runRuntimePathRepresentativeOccupantsPrimitiveContract(error)) return false;
    const auto fail = [&](const std::string& why) {
        error = "simulation query reuse primitives: " + why;
        return false;
    };
    std::size_t offsetChecks = 0, mutationChecks = 0, cellChecks = 0;
    const auto object = [](int id, bool alive, int health) {
        WorldObjectInstance value;
        value.instanceId = id; value.kind = WorldObjectKind::Unit;
        value.type = "TANK"; value.owner = id % 2 ? "Player" : "Enemy";
        value.alive = alive; value.health = health; value.mission = alive ? "Guard" : "Destroyed";
        value.x = id % 19; value.y = id % 13; value.cell = value.y * 512 + value.x;
        return value;
    };
    for (bool sparse : {false, true}) for (bool reordered : {false, true}) {
        GameWorld world;
        auto& objects = world.accessObjects();
        // Only 256 live behaviors; large dead history must not be copied into
        // a per-tick behavior lookup. Alive zero-health records remain present.
        for (int index = 0; index < 256; ++index) {
            const int id = sparse ? 100000 + index * 7 : index + 1;
            objects.push_back(object(id, index % 11 != 0, index % 7 == 0 ? 0 : 100));
        }
        for (int index = 0; index < 10000; ++index)
            objects.push_back(object(1000000 + index, false, 0));
        objects.push_back(object(0, true, 0));
        objects.push_back(object(-7, true, 100));
        if (reordered) std::reverse(objects.begin(), objects.end());
        std::vector<int> ids;
        for (int index = 0; index < 256; ++index)
            ids.push_back(sparse ? 100000 + index * 7 : index + 1);
        ids.insert(ids.end(), {1000000, 9000000, 0, -7, -1});
        std::sort(ids.begin(), ids.end());
        const auto before = world.deterministicSimulationStateHash();
        RuntimeObjectIndex hint;
        std::unordered_map<int, std::size_t> original;
        original.reserve(objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].alive) original.emplace(objects[index].instanceId, index);
        std::vector<std::size_t> expected;
        for (int id : ids) {
            const auto found = original.find(id);
            expected.push_back(found == original.end() ? runtimeMissingBehaviorObjectOffset : found->second);
        }
        const auto reference = buildRuntimeBehaviorObjectOffsets(objects, ids, hint, true);
        const auto optimized = buildRuntimeBehaviorObjectOffsets(objects, ids, hint, false);
        if (expected != reference || expected != optimized
            || before != world.deterministicSimulationStateHash()) return fail("initial offsets/history/alive-health semantics");
        offsetChecks += ids.size();
        const auto validate = [&] {
            const auto hash = world.deterministicSimulationStateHash();
            for (std::size_t index = 0; index < ids.size(); ++index) {
                WorldObjectInstance* oldObject = nullptr;
                const auto found = original.find(ids[index]);
                if (found != original.end() && found->second < objects.size()) {
                    auto& at = objects[found->second];
                    if (at.alive && at.instanceId == ids[index]) oldObject = &at;
                }
                auto* newObject = runtimeBehaviorObjectAtInitialOffset(objects, ids[index], optimized[index]);
                if (oldObject != newObject) return false;
                ++mutationChecks;
            }
            return hash == world.deterministicSimulationStateHash();
        };
        if (!validate()) return fail("initial validated lookup");
        // Initial dead/absent IDs remain absent even after becoming alive in
        // this tick. Their next-tick capture may find them, exactly as before.
        for (auto& at : objects)
            if (!at.alive && (at.instanceId == ids[3] || at.instanceId == 1000000)) at.alive = true;
        objects.push_back(object(9000000, true, 100));
        if (!validate()) return fail("same-tick resurrection/new-id refound an initially absent object");
        const auto oldData = objects.data();
        objects.reserve(objects.capacity() + 1);
        if (objects.data() == oldData || !validate()) return fail("append/reallocation retained pointers");
        // Alive capture becoming dead, replacement with a different ID, and
        // same-size reorder must use the stored initial offset plus validation.
        const auto live = std::find_if(optimized.begin(), optimized.end(), [](std::size_t offset) {
            return offset != runtimeMissingBehaviorObjectOffset;
        });
        if (live == optimized.end()) return fail("fixture missing living offset");
        objects[*live].alive = false;
        if (!validate()) return fail("death failed alive validation");
        objects[*live] = object(9000001, true, 100);
        if (!validate()) return fail("same-offset ID replacement was refound");
        std::swap(objects[0], objects[objects.size() - 2]);
        if (!validate()) return fail("same-size reorder was refound");
        objects.resize(9);
        if (!validate()) return fail("shrinking world failed bounds validation");
        const auto refreshedReference = buildRuntimeBehaviorObjectOffsets(objects, ids, hint, true);
        const auto refreshedOptimized = buildRuntimeBehaviorObjectOffsets(objects, ids, hint, false);
        if (refreshedReference != refreshedOptimized) return fail("new-tick capture did not observe replacement/revival");
    }

    const auto fields = [](const PathTerrainCell& cell) {
        return std::tuple(cell.cell.x, cell.cell.y, cell.level, cell.passable, cell.water,
            cell.shore, cell.bridge, cell.ramp, cell.cliff, cell.resource, cell.cost, cell.hazardCost);
    };
    std::vector<PathTerrainCell> cached;
    cached.reserve(2048);
    cached.resize(2048);
    bool valid = true;
    for (std::size_t count : {1024U, 256U, 1024U, 2048U, 4096U, 256U}) {
        std::vector<PathTerrainCell> staticCells(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto& cell = staticCells[index];
            cell.cell = {static_cast<int>(index % 64), static_cast<int>(index / 64)};
            cell.level = static_cast<int>(index % 5); cell.passable = index % 7 != 0;
            cell.water = index % 3 == 0; cell.shore = index % 4 == 0; cell.bridge = index % 5 == 0;
            cell.ramp = index % 6 == 0; cell.cliff = index % 8 == 0; cell.resource = index % 9 == 0;
            cell.cost = static_cast<int>(index % 31); cell.hazardCost = static_cast<int>(index % 13);
        }
        const auto* cachedData = cached.data();
        const auto cachedCapacity = cached.capacity();
        const auto oldCacheSize = cached.size();
        auto reference = takeRuntimePathSnapshotScratch(cached, valid, staticCells, true);
        if (!valid || cached.data() != cachedData || cached.size() != oldCacheSize)
            return fail("reference scratch altered cache ownership");
        auto optimized = takeRuntimePathSnapshotScratch(cached, valid, staticCells, false);
        if (valid || !cached.empty() || optimized.size() != reference.size()
            || (count <= cachedCapacity && (optimized.data() != cachedData || optimized.capacity() != cachedCapacity)))
            return fail("scratch did not retain capacity/local invalid ownership");
        for (std::size_t index = 0; index < count; ++index) {
            if (fields(reference[index]) != fields(optimized[index])) return fail("static scratch field/order changed");
            // Old/new dynamic overlays remain identical after checkout.
            reference[index].passable = optimized[index].passable = index % 11 != 0;
            reference[index].hazardCost += 7; optimized[index].hazardCost += 7;
            if (fields(reference[index]) != fields(optimized[index])) return fail("dynamic scratch overlay changed");
            ++cellChecks;
        }
        cached = std::move(optimized); valid = true;
    }
    const auto* cachedData = cached.data();
    const auto capacity = cached.capacity();
    const auto cachedSize = cached.size();
    const std::vector<PathTerrainCell> empty;
    if (!takeRuntimePathSnapshotScratch(cached, valid, empty, false).empty() || !valid
        || cached.data() != cachedData || cached.size() != cachedSize || cached.capacity() != capacity)
        return fail("empty static input altered the existing cache");

    PathApproachContext first, candidate;
    std::size_t oldHashCalls = 0;
    for (const std::uint64_t firstHash : {0ULL, 1ULL, 0xffffffffffffffffULL})
        for (const std::uint64_t nextHash : {0ULL, 1ULL, 0xffffffffffffffffULL}) {
            first.movementConstraintHash = firstHash; candidate.movementConstraintHash = nextHash;
            const auto oldFirst = [&] { ++oldHashCalls; return firstHash; };
            const auto oldCandidate = [&] { ++oldHashCalls; return nextHash; };
            const auto before = oldHashCalls;
            const bool reference = runtimeMoveBatchConstraintsMatch(first, candidate, oldFirst, oldCandidate, true);
            if (oldHashCalls != before + 2) return fail("reference did not execute both original hash queries");
            const bool optimized = runtimeMoveBatchConstraintsMatch(first, candidate, oldFirst, oldCandidate, false);
            if (oldHashCalls != before + 2 || reference != optimized)
                return fail("fresh context comparison queried or changed hash equality");
        }
    const int previousOverride = runtimeSimulationQueryReuseReferenceOverride;
    {
        RuntimeSimulationQueryReuseReferenceScope reference(true);
        if (!runtimeSimulationQueryReuseReference()) return fail("reference scope not enabled");
        {
            RuntimeSimulationQueryReuseReferenceScope optimized(false);
            if (runtimeSimulationQueryReuseReference()) return fail("nested optimized scope not enabled");
        }
        if (!runtimeSimulationQueryReuseReference()) return fail("nested scope did not restore reference");
    }
    if (runtimeSimulationQueryReuseReferenceOverride != previousOverride) return fail("scope leaked its override");
    std::cout << "Simulation query reuse primitives: initialOffsets=" << offsetChecks
        << " postMutationLookups=" << mutationChecks << " pathFields=" << cellChecks
        << " history=10000 aliveHealth0/revival/append/reallocation/reorder/shrink/capacity/hashLazy=yes\n";
    error.clear();
    return true;
}
