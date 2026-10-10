// Include after RuntimePlannerCellPassabilityContract.inl. Compare the actual
// optional transition query, complete BFS, commands and full authority state.
#pragma once

#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace RuntimePlannerTerrainQueriesContractDetail {
bool sameInfo(const RuntimeTerrainInfo& a, const RuntimeTerrainInfo& b) {
    return std::tie(a.movementLandType, a.movementRampType, a.kind,
        a.authoredLandType, a.passable, a.water, a.shore, a.cliff, a.ramp, a.bridge, a.cost)
        == std::tie(b.movementLandType, b.movementRampType, b.kind,
        b.authoredLandType, b.passable, b.water, b.shore, b.cliff, b.ramp, b.bridge, b.cost);
}
const TerrainCell* scalarTerrain(const RuntimePlannerTerrainQueries::TerrainMap& map, int key) {
    const auto found = map.find(key);
    return found == map.end() ? nullptr : found->second;
}
const RuntimeTerrainInfo* scalarInfo(const RuntimeWindowState& state, int key) {
    const auto found = state.terrainInfoByCell.find(key);
    return found == state.terrainInfoByCell.end() ? nullptr : std::addressof(found->second);
}
} // namespace RuntimePlannerTerrainQueriesContractDetail

bool runRuntimePlannerTerrainQueriesPrimitiveContract(std::string& error) {
    using namespace RuntimePlannerTerrainQueriesContractDetail;
    const auto fail = [&](const std::string& why) {
        error = "planner terrain queries: " + why; return false;
    };
    RuntimeComponentTerrainQueryReferenceScope enable(false);
    auto box = std::make_unique<RuntimeWindowState>(); auto& state = *box;
    state.tick = 73;
    auto& cells = state.world.accessTerrainCells();
    cells.resize(4096 + 8); // Addresses are stable before either pointer table is built.
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        auto& cell = cells[static_cast<std::size_t>(y) * 64 + x];
        cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        state.terrainByCell.emplace(runtimeCellKey(x, y), std::addressof(cell));
        RuntimeTerrainInfo info;
        const int n = y * 64 + x;
        info.movementLandType = static_cast<RuntimeTerrainLandType>(n % 11);
        info.movementRampType = static_cast<unsigned char>(n % 256);
        info.kind = static_cast<RuntimeTerrainKind>(n % 5);
        info.authoredLandType = static_cast<RuntimeTerrainLandType>((n + 3) % 11);
        info.passable = (n & 1) != 0; info.water = (n & 2) != 0;
        info.shore = (n & 4) != 0; info.cliff = (n & 8) != 0;
        info.ramp = (n & 16) != 0; info.bridge = (n & 32) != 0;
        info.cost = n % 71 - 35;
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    // Keep actual null, missing, boundary, negative and very large map keys.
    cells[4096].x = 511; cells[4096].y = 511;
    cells[4097].x = 512; cells[4097].y = 0;
    cells[4098].x = 5; cells[4098].y = 600;
    cells[4099].x = -1; cells[4099].y = 5;
    cells[4100].x = 1001; cells[4100].y = 3;
    state.terrainByCell[runtimeCellKey(511, 511)] = &cells[4096];
    state.terrainByCell[runtimeCellKey(512, 0)] = &cells[4097];
    state.terrainByCell[runtimeCellKey(5, 600)] = &cells[4098];
    state.terrainByCell[-1] = &cells[4099];
    state.terrainByCell[std::numeric_limits<int>::min()] = &cells[4099];
    state.terrainByCell[std::numeric_limits<int>::max()] = &cells[4098];
    state.terrainByCell[runtimeCellKey(2, 2)] = nullptr;
    state.terrainByCell.erase(runtimeCellKey(3, 3));
    // The key used to find this cell is deliberately NOT its metadata key.
    state.terrainByCell[runtimeCellKey(7, 7)] = &cells[4100];
    state.terrainInfoByCell[runtimeCellKey(511, 511)] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[runtimeCellKey(512, 0)] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[runtimeCellKey(5, 600)] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[-1] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[std::numeric_limits<int>::min()] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[std::numeric_limits<int>::max()] = RuntimeTerrainInfo{};
    state.terrainInfoByCell[runtimeCellKey(1, 4)].water = true;
    state.terrainInfoByCell[runtimeCellKey(7, 7)].water = false;
    const int missingInfo = runtimeCellKey(9, 9);
    state.terrainInfoByCell.erase(missingInfo);

    std::size_t addressChecks = 0, valueChecks = 0;
    {
        RuntimePlannerTerrainQueries queries(state, state.terrainByCell);
        const auto view = queries.viewFor(state, state.terrainByCell);
        if (!view || queries.denseTerrainBytes() != 262144 * sizeof(const TerrainCell*)
            || queries.denseInfoBytes() != 262144 * sizeof(const RuntimeTerrainInfo*))
            return fail("large read-only maps must own exactly two bounded pointer arrays");
        for (const auto& [key, cell] : state.terrainByCell) {
            if (view->terrainAtKey(key) != cell || view->terrainAtKey(key) != scalarTerrain(state.terrainByCell, key))
                return fail("terrain pointer address/key/null identity");
            ++addressChecks;
            if (cell) {
                if (!sameInfo(view->infoForCell(*cell), runtimeCachedTerrainInfoForCell(state, *cell)))
                    return fail("all metadata fields use returned cell coordinates");
                ++valueChecks;
            }
        }
        for (const auto& [key, info] : state.terrainInfoByCell) {
            if (view->cachedInfoAtKey(key) != std::addressof(info)
                || view->cachedInfoAtKey(key) != scalarInfo(state, key))
                return fail("metadata map's independent exact node addresses");
            ++addressChecks;
        }
        for (int key : std::array<int, 9>{{runtimeCellKey(3, 3), missingInfo, 511999, 512000,
                -2, 1000, 3001, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}}) {
            if (view->terrainAtKey(key) != scalarTerrain(state.terrainByCell, key)
                || view->cachedInfoAtKey(key) != scalarInfo(state, key))
                return fail("missing/negative/outside/extreme original find fallback");
            addressChecks += 2;
        }
        if (runtimeCellKey(1001, 2) != runtimeCellKey(1, 3)
            || view->terrainAtKey(runtimeCellKey(1001, 2)) != view->terrainAtKey(runtimeCellKey(1, 3)))
            return fail("original legacy integer-key alias must not become raw-coordinate identity");
        if (!view->infoForCell(cells[4100]).water)
            return fail("metadata incorrectly selected terrain lookup key rather than cell's key");
        std::size_t originalCalls = 0;
        const TerrainCell& noInfo = cells[9 * 64 + 9];
        const auto original = runtimeCachedTerrainInfoForCell(state, noInfo);
        for (int n = 0; n < 4; ++n) {
            const auto observed = view->infoForCellWithFallback(noInfo, [&](const TerrainCell& cell) {
                ++originalCalls;
                return runtimeCachedTerrainInfoForCell(state, cell);
            });
            if (!sameInfo(original, observed)) return fail("real scalar missing-metadata result");
        }
        if (originalCalls != 4 || state.terrainInfoByCell.contains(missingInfo))
            return fail("metadata miss was computed early, memoized, or inserted");
        auto foreign = std::make_unique<RuntimeWindowState>();
        foreign->tick = state.tick;
        foreign->terrainInfoByCell[1001].cost = 7654;
        auto foreignMap = state.terrainByCell;
        foreignMap[1001] = &cells[4096];
        if (queries.viewFor(*foreign, state.terrainByCell) || queries.viewFor(state, foreignMap))
            return fail("foreign state/map borrowed an owned pointer table");
        // No ReadOnlyView is used after this point. End it before changing a
        // reference scope, mutating a map, or invalidating the owner.
    }
    ++state.tick; // Previous owner/view lifetime has ended before state changes.
    {
        RuntimePlannerTerrainQueries disabled(state, state.terrainByCell, false);
        if (disabled.viewFor(state, state.terrainByCell) || disabled.denseTerrainBytes()
            || disabled.denseInfoBytes()) return fail("disabled/Air/original-kernel allocated arrays");
        RuntimeComponentTerrainQueryReferenceScope reference(true);
        RuntimePlannerTerrainQueries original(state, state.terrainByCell);
        if (original.viewFor(state, state.terrainByCell) || original.denseTerrainBytes()
            || original.denseInfoBytes()) return fail("independent reference allocated/reused arrays");
        {
            RuntimeComponentTerrainQueryReferenceScope nested(false);
            RuntimePlannerTerrainQueries fresh(state, state.terrainByCell);
            if (!fresh.viewFor(state, state.terrainByCell)) return fail("nested fast scope");
        }
        if (!runtimeComponentTerrainQueryReference()) return fail("nested reference restoration");
    }
    auto sparseMap = state.terrainByCell;
    while (sparseMap.size() >= RuntimePlannerTerrainQueries::minimumTerrainEntries) sparseMap.erase(sparseMap.begin());
    RuntimePlannerTerrainQueries smallQueries(state, sparseMap);
    if (smallQueries.viewFor(state, sparseMap) || smallQueries.denseTerrainBytes()
        || smallQueries.denseInfoBytes()) return fail("fewer than 4096 queries/map entries allocated arrays");
    {
        RuntimePlannerTerrainQueries changing(state, state.terrainByCell);
        changing.invalidate();
        // Same-count map/pointer and metadata-node replacement is permitted
        // only AFTER invalidate, and must be observed by a fresh lifetime.
        state.terrainByCell[1001] = &cells[4096];
        state.terrainInfoByCell.erase(1001);
        RuntimeTerrainInfo changed; changed.cost = -678; changed.bridge = true;
        state.terrainInfoByCell.emplace(1001, changed);
        if (changing.viewFor(state, state.terrainByCell)) return fail("explicit invalidation failed");
    }
    {
        RuntimePlannerTerrainQueries fresh(state, state.terrainByCell);
        const auto view = fresh.viewFor(state, state.terrainByCell);
        if (!view || view->terrainAtKey(1001) != &cells[4096]
            || view->cachedInfoAtKey(1001) != scalarInfo(state, 1001)
            || !sameInfo(*view->cachedInfoAtKey(1001), state.terrainInfoByCell.at(1001)))
            return fail("fresh lifetime missed same-size pointer/node edit");
    }
    if (addressChecks < 8192 || valueChecks < 4096) return fail("complete pointer/value fixture did not run");
    std::cout << "Planner terrain query primitives: addressChecks=" << addressChecks
        << " allFieldCopies=" << valueChecks << " missingMetadataOriginalCalls=4"
        << " legacyAliases=yes foreignStateMap=yes bounded4MiBOnX64=yes pass\n";
    error.clear(); return true;
}

// Real production transition and ordered production BFS. Requires the proposed
// optional LAST parameter const RuntimePlannerTerrainQueries* on transition.
bool runRuntimePlannerTerrainQueriesDirectedContract(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    const auto fail = [&](const std::string& why) {
        error = "planner terrain transition: " + why; return false;
    };
    RuntimeComponentKernelReferenceScope newKernel(false);
    RuntimeComponentTerrainQueryReferenceScope newQueries(false);
    auto box = std::make_unique<RuntimeWindowState>(); auto& state = *box;
    RuntimeSnapshotActiveStateScope active(state);
    if (!make(state, config, 0, 0, error)) return false;
    auto& cells = state.world.accessTerrainCells();
    cells.clear(); cells.resize(4096);
    state.terrainByCell.clear(); state.terrainInfoByCell.clear();
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        auto& cell = cells[y * 64 + x]; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y); cell.level = x >= 48 ? 1 : 0;
        state.terrainByCell.emplace(runtimeCellKey(x, y), &cell);
        RuntimeTerrainInfo info;
        info.authoredLandType = info.movementLandType = x >= 50
            ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
        info.water = x >= 50; info.shore = x == 49;
        info.ramp = x == 47 || x == 48; info.bridge = x >= 49 && y == 5;
        if (x == 8 && y == 6) { info.cliff = true; info.passable = false; }
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    invalidateRuntimeTickOccupancyCache(state);
    const auto occupied = buildRuntimeOccupancyLookup(state);
    Authority before, after;
    if (!capture(state, before, error)) return false;
    std::size_t edgeChecks = 0, componentChecks = 0;
    for (int id : {1, 2, 3, 4, 5, 6}) {
        const auto* mover = findObjectById(state, id);
        if (!mover) return fail("missing real authored movement fixture actor");
        const auto mode = runtimeObjectMovementMode(state, *mover);
        RuntimePlannerTerrainQueries queries(state, state.terrainByCell, mode != PathMovementMode::Air);
        if ((mode == PathMovementMode::Air) ? (queries.denseTerrainBytes() != 0)
            : !queries.viewFor(state, state.terrainByCell)) return fail("actual Air/large-map allocation eligibility");
        for (int y = -1; y <= 64; ++y) for (int x = -1; x <= 64; ++x) {
            const RuntimeCellPoint current{x, y};
            for (const auto& next : runtimeNeighborsOf(current)) {
                const bool original = runtimeCellTransitionIsPassable(state, state.terrainByCell,
                    occupied, current, next, *mover, -1, nullptr, true, mode, nullptr);
                const bool observed = runtimeCellTransitionIsPassable(state, state.terrainByCell,
                    occupied, current, next, *mover, -1, nullptr, true, mode, &queries);
                if (original != observed) return fail("actual directed transition id=" + std::to_string(id)
                    + " from=" + std::to_string(x) + "," + std::to_string(y));
                ++edgeChecks;
            }
            for (const auto next : {current, RuntimeCellPoint{x + 2, y}, RuntimeCellPoint{x, y - 2}}) {
                if (runtimeCellTransitionIsPassable(state, state.terrainByCell, occupied,
                        current, next, *mover, -1, nullptr, true, mode, nullptr)
                    != runtimeCellTransitionIsPassable(state, state.terrainByCell, occupied,
                        current, next, *mover, -1, nullptr, true, mode, &queries))
                    return fail("same-cell/nonadjacent original early return order");
                ++edgeChecks;
            }
        }
        Observations oldCalls, newCalls;
        std::unordered_map<int, int> oldComponent, newComponent;
        {
            RuntimeComponentTerrainQueryReferenceScope reference(true);
            RuntimePlannerPassabilityObserverScope record(&Observations::append, &oldCalls);
            oldComponent = buildRuntimeMoveComponentByCell(state, *mover, state.terrainByCell, occupied);
        }
        {
            RuntimeComponentTerrainQueryReferenceScope optimized(false);
            RuntimePlannerPassabilityObserverScope record(&Observations::append, &newCalls);
            newComponent = buildRuntimeMoveComponentByCell(state, *mover, state.terrainByCell, occupied);
        }
        if (oldComponent != newComponent || oldCalls.values != newCalls.values || oldCalls.values.empty())
            return fail("complete production component IDs/ordered original BFS queries id=" + std::to_string(id));
        ++componentChecks;
    }
    if (!capture(state, after, error) || !(before == after))
        return fail("directed transitions/components changed full world/system authority or serialized state");
    if (edgeChecks != 287496 || componentChecks != 6) return fail("complete directed/component matrix count");
    std::cout << "Planner terrain transition: exactDirectedEdges=" << edgeChecks
        << " completeOrderedComponents=" << componentChecks
        << " allAuthoritiesAndFullSave=yes pass\n";
    error.clear(); return true;
}

// Opt-in strong integration entry point. This intentionally reuses the EXISTING
// four-way real planner/command/full-save/per-tick-authority contract under each
// independent terrain-reference setting. Do not replace it with hash-only or a
// reconstructed BFS. This is more expensive than the primitive helper checks;
// root chooses when to run it after source integration, never during C16 build.
bool runRuntimePlannerTerrainQueriesFullContract(const RuntimeConfig& config, std::string& error) {
    if (!runRuntimePlannerTerrainQueriesPrimitiveContract(error)
        || !runRuntimePlannerTerrainQueriesDirectedContract(config, error)) return false;
    for (bool reference : {true, false}) {
        RuntimeComponentTerrainQueryReferenceScope terrainBranch(reference);
        if (!runRuntimePlannerCellPassabilityContract(config, error)) return false;
    }
    error.clear(); return true;
}
