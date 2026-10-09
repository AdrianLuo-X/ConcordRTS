#include "GameWorld.h"
#include "PathRequestQueue.h"
#include "RuntimePathProfileHash.h"
#include "RuntimePathProfileZeroRuns.h"
#include "RuntimePathProfileFixedSuffix.h"
#include "RuntimePathQueueWarmAdmission.h"
#include "RuntimePathProfileSparseBytesContract.inl"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

void hashInt(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 255U;
        hash *= 1099511628211ULL;
    }
}

void hashResult(std::uint64_t& hash, const PathResult& result) {
    hashInt(hash, static_cast<int>(result.status));
    hashInt(hash, result.reachable);
    hashInt(hash, result.goalAdjusted);
    hashInt(hash, result.searchBudgetExhausted);
    hashInt(hash, result.expandedNodes);
    hashInt(hash, result.fallbackCandidateCount);
    hashInt(hash, result.goalSlotIndex);
    hashInt(hash, result.resolvedGoal.x);
    hashInt(hash, result.resolvedGoal.y);
    hashInt(hash, result.path.size());
    for (const PathCell cell : result.path) {
        hashInt(hash, cell.x);
        hashInt(hash, cell.y);
    }
    for (char byte : result.requestClass) {
        hashInt(hash, static_cast<unsigned char>(byte));
    }
}

std::vector<PathTerrainCell> terrain(int size, bool blockedTarget) {
    std::vector<PathTerrainCell> cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = !(x == size / 3 && y < size - 5);
            if (blockedTarget && x >= size - 28 && x < size - 16
                && y >= size - 28 && y < size - 16) {
                cell.passable = false;
            }
            cell.hazardCost = x == size / 2 && y % 5 != 0 ? 75 : 0;
            cells.push_back(cell);
        }
    }
    return cells;
}

std::uint64_t exercise(bool blockedTarget, int count, bool shuffled) {
    auto cells = terrain(128, blockedTarget);
    if (shuffled) {
        std::reverse(cells.begin(), cells.end());
    }
    PathRequestQueue queue(cells);
    std::uint64_t hash = 14695981039346656037ULL;
    for (int request = 0; request < count; ++request) {
        PathApproachContext context;
        context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
        context.requesterStableHash = static_cast<std::uint64_t>(request + 1);
        context.movementMode = static_cast<PathMovementMode>(request % 4);
        context.goalSlotCount = request % 7 == 0 ? 4 : 0;
        const PathCell start{request % 24, 2 + request % 31};
        const PathCell goal = blockedTarget
            ? PathCell{100 + request % 12, 100 + (request / 12) % 12}
            : PathCell{114 + request % 12, 96 + (request / 12) % 12};
        queue.submit(start, goal, context);
        constexpr int budgets[] = {1, 32, 1024, 16384};
        queue.pumpWithBudget(1, budgets[(request / 4) % 4]);
        hashResult(hash, queue.getResults().back());
    }
    return hash;
}

bool edgeCases() {
    // A radius-limited blocked component can differ between goals in the
    // same large wall. Repeated and interleaved requests must retain that.
    auto cells = terrain(64, false);
    for (auto& cell : cells) {
        cell.cell.x -= 70;
        cell.cell.y -= 70;
        cell.passable = cell.cell.y < -40 || cell.cell.y > -38;
    }
    PathRequestQueue queue(cells);
    PathApproachContext context;
    for (int index = 0; index < 260; ++index) {
        const PathCell goal{-69 + index % 62, -40 + (index / 62) % 3};
        PathRequestQueue reference(cells);
        const PathCell start{-68, -68};
        context.requesterStableHash = static_cast<std::uint64_t>(index + 1);
        queue.submit(start, goal, context);
        reference.submit(start, goal, context);
        queue.pumpWithBudget(1, 16384);
        reference.pumpWithBudget(1, 16384);
        const auto& cached = queue.getResults().back();
        const auto& fresh = reference.getResults().back();
        if (cached.reachable != fresh.reachable
            || cached.resolvedGoal.x != fresh.resolvedGoal.x
            || cached.resolvedGoal.y != fresh.resolvedGoal.y) {
            return false;
        }
    }
    return true;
}

std::uint64_t irregularTerrain() {
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint32_t random = 91723;
    for (int sample = 0; sample < 24; ++sample) {
        std::vector<PathTerrainCell> cells;
        for (int y = -12; y <= 12; ++y) {
            for (int x = -18; x <= 18; ++x) {
                random = random * 1664525U + 1013904223U;
                if (random % 13 == 0 && x != 0) {
                    continue;
                }
                PathTerrainCell cell;
                cell.cell = {x, y};
                cell.passable = random % 7 != 0 || x == 0;
                cell.cliff = random % 31 == 0 && x != 0;
                cell.water = y > 7;
                cell.shore = y == 7 || y == 8;
                cell.bridge = x % 11 == 0;
                cell.level = x > 7 ? 1 : 0;
                cell.ramp = x == 7 || x == 8;
                cell.cost = 10 + static_cast<int>(random % 4) * 10;
                cell.hazardCost = static_cast<int>(random % 9) * 12;
                cells.push_back(cell);
                if (random % 17 == 0) {
                    cell.cost += 10;
                    cells.push_back(cell);
                }
            }
        }
        if (sample % 2 == 0) {
            std::reverse(cells.begin(), cells.end());
        }
        PathRequestQueue queue(cells);
        for (int request = 0; request < 12; ++request) {
            PathApproachContext context;
            context.movementMode = static_cast<PathMovementMode>(request % 4);
            context.requesterStableHash = static_cast<std::uint64_t>(request + 1);
            context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
            context.goalSlotCount = request % 3 == 0 ? 5 : 0;
            queue.submit({0, -9 + request}, {sample - 9, 9}, context);
            queue.pumpWithBudget(1, request % 2 == 0 ? 32 : 16384);
            hashResult(hash, queue.getResults().back());
        }
    }
    return hash;
}

bool pendingLookupChecks(bool benchmark) {
    PathTerrainCell cell;
    cell.cell = {0, 0};
    PathRequestQueue queue(std::vector<PathTerrainCell>{cell});
    std::vector<PathRequestHandle> handles;
    PathApproachContext context;
    for (int request = 0; request < 1024; ++request) {
        context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
        handles.push_back(queue.submit({0, 0}, {0, 0}, context));
        if (handles.back().id < 0) return false;
    }
    const auto start = std::chrono::steady_clock::now();
    for (int query = 0; query < 65536; ++query) {
        const auto offset = static_cast<std::size_t>((query * 73) % 1024);
        context.movementConstraintHash = offset + 1;
        if (queue.submit({0, 0}, {0, 0}, context).id != handles[offset].id) {
            return false;
        }
    }
    if (benchmark) {
        std::cout << "pending_duplicate_queries=65536 pending=1024 submit_ms="
                  << std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - start).count() << '\n';
    }
    // Copies must own independent indices. Completing a prefix and adding
    // another request must not leave old IDs in the collapse lookup.
    PathRequestQueue copy = queue;
    if (queue.pumpWithBudget(256, 32768) != 256) return false;
    for (int request = 256; request < 1024; ++request) {
        context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
        if (queue.submit({0, 0}, {0, 0}, context).id != handles[request].id) {
            return false;
        }
    }
    context.movementConstraintHash = 1;
    if (copy.submit({0, 0}, {0, 0}, context).id != handles.front().id) return false;
    const auto resubmitted = queue.submit({0, 0}, {0, 0}, context);
    if (resubmitted.id <= handles.back().id || queue.pendingCount() != 769) {
        return false;
    }
    context.movementConstraintHash = 1025;
    if (copy.submit({0, 0}, {0, 0}, context).id >= 0
        || copy.getResults().back().status != PathResultStatus::OverflowRejected) {
        return false;
    }
    if (queue.pumpWithBudget(1024, 32768) != 769 || queue.pendingCount() != 0) {
        return false;
    }
    if (queue.getResults().back().requestId != resubmitted.id) return false;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (queue.getResults()[i].requestId != handles[i].id) return false;
    }
    context.movementConstraintHash = 1;
    return queue.submit({0, 0}, {0, 0}, context).id > resubmitted.id;
}

bool largeFootprintPaths(bool benchmark) {
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t hash = 14695981039346656037ULL;
    bool workBounded = true;
    for (int shape = 0; shape < 4; ++shape) {
        std::vector<PathTerrainCell> cells;
        for (int y = -64; y < 64; ++y) {
            for (int x = -64; x < 64; ++x) {
                PathTerrainCell cell;
                cell.cell = {x, y};
                const bool inside = x >= -12 && x <= 12
                    && y >= -12 && y <= 12;
                // Rectangle, cross, courtyard, and a goal outside the map.
                const bool blocked = inside && (shape == 0
                    || (shape == 1 && (std::abs(x) <= 2 || std::abs(y) <= 2))
                    || (shape == 2 && (std::abs(x) >= 9 || std::abs(y) >= 9
                        || (x == 0 && y <= 0))));
                cell.passable = !blocked;
                cell.cliff = blocked && (x + y) % 3 == 0;
                cell.hazardCost = x == -24 && y % 5 != 0 ? 75 : 0;
                cells.push_back(cell);
            }
        }
        if (shape % 2 != 0) {
            std::reverse(cells.begin(), cells.end());
        }
        PathRequestQueue queue(cells);
        for (int request = 0; request < 96; ++request) {
            PathApproachContext context;
            context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
            context.requesterStableHash = static_cast<std::uint64_t>(request + 1);
            context.movementMode = request % 2 == 0
                ? PathMovementMode::Ground : PathMovementMode::Amphibious;
            context.goalSlotCount = request % 3 == 0 ? 8 : 0;
            const PathCell goal = shape == 3 ? PathCell{64, 0} : PathCell{0, 0};
            queue.submit({-58 + (request % 8) * 3, -58 + (request / 8) * 3},
                goal, context);
            constexpr int budgets[] = {1, 64, 1024, 16384};
            queue.pumpWithBudget(1, budgets[request % 4]);
            hashResult(hash, queue.getResults().back());
        }
        const auto stats = queue.liveStats();
        constexpr std::uint64_t componentSizes[] = {625, 225, 345, 0};
        const std::uint64_t expectedQueries = shape < 3
            ? 96 * (41 * 41 - componentSizes[shape]) : 0;
        const std::uint64_t expectedChecks = shape == 0 ? 0
            : expectedQueries * componentSizes[shape];
        // One exact distance calculation per usable candidate, including
        // candidates rejected by the radius. Rectangles do not scan cells.
        workBounded = workBounded
            && stats.fallbackDistanceQueries == expectedQueries
            && stats.fallbackComponentCellChecks == expectedChecks;
        if (benchmark) {
            std::cout << "footprint_shape=" << shape
                      << " distance_queries=" << stats.fallbackDistanceQueries
                      << " component_cell_checks="
                      << stats.fallbackComponentCellChecks
                      << " heuristic_evaluations=" << stats.heuristicEvaluations << '\n';
        }
    }
    if (benchmark) {
        std::cout << "large_footprint_requests=384 large_footprint_ms="
                  << std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - start).count() << '\n';
    }
    std::cout << "large_footprints=" << hash << '\n';
    // Captured before scoring/rectangle optimizations from the original
    // queue; checks full paths, goal slots, partial budgets and search work.
    return hash == 2971029960883841023ULL && workBounded;
}

bool heuristicWorkChecks(bool benchmark) {
    std::vector<PathTerrainCell> cells;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = x != 32;
            cell.cost = 10 + (x * 13 + y * 7) % 5 * 10;
            cells.push_back(cell);
        }
    }
    const auto start = std::chrono::steady_clock::now();
    PathRequestQueue queue(cells);
    for (int request = 0; request < 8; ++request) {
        PathApproachContext context;
        context.mapQueryApproachAvailable = true;
        context.movementConstraintHash = static_cast<std::uint64_t>(request + 1);
        for (int goal = 0; goal < 32; ++goal) {
            context.approachCells.push_back({48 + request % 3, 16 + goal});
        }
        queue.submit({2 + request, 2 + request}, {48, 32}, context);
        queue.pumpWithBudget(1, 16384);
        const auto& result = queue.getResults().back();
        if (result.status != PathResultStatus::RealNoPath
            || result.expandedNodes != 32 * 64 || result.reachable) {
            return false;
        }
    }
    if (benchmark) {
        std::cout << "heuristic_requests=8 heuristic_evaluations="
                  << queue.liveStats().heuristicEvaluations
                  << " heuristic_ms=" << std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - start).count() << '\n';
    }
    // Exhausting every reachable node with 32 goals needs one heuristic per
    // node. Heap revisits and closing a node must not rescan all 32 goals.
    return queue.liveStats().heuristicEvaluations == 8 * 32 * 64;
}

bool terrainLookupEquivalenceChecks() {
    const auto compare = [](const std::vector<PathTerrainCell>& cells,
                            const std::vector<std::pair<PathCell, PathCell>>& requests,
                            bool expectDense) {
        PathRequestQueue dense(cells);
        PathRequestQueue hash(cells, true);
        const char* reference = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        const bool forcedReference = reference && std::string(reference) == "1";
        if (dense.liveStats().usesDenseTerrainLookup != (expectDense && !forcedReference)
            || hash.liveStats().usesDenseTerrainLookup) return false;
        for (std::size_t i = 0; i < requests.size(); ++i) {
            PathApproachContext context;
            context.movementConstraintHash = i + 1;
            context.requesterStableHash = i + 1;
            dense.submit(requests[i].first, requests[i].second, context);
            hash.submit(requests[i].first, requests[i].second, context);
            const int budget = i % 2 ? 16 : 4096;
            dense.pumpWithBudget(1, budget); hash.pumpWithBudget(1, budget);
            const auto& actual = dense.getResults().back();
            const auto& expected = hash.getResults().back();
            std::uint64_t a = 14695981039346656037ULL, b = a;
            hashResult(a, actual); hashResult(b, expected);
            if (a != b || actual.passabilityProfileHash != expected.passabilityProfileHash
                || actual.failureReason != expected.failureReason
                || actual.cacheHit != expected.cacheHit
                || actual.fallbackTargetsTried != expected.fallbackTargetsTried
                || dense.liveStats().lastPumpExpandedNodes != hash.liveStats().lastPumpExpandedNodes)
                return false;
        }
        return true;
    };
    if (!compare({}, {{{0, 0}, {1, 1}}}, false)) return false;
    auto compact = terrain(16, false);
    auto duplicate = compact[17];
    duplicate.cost = 50; duplicate.hazardCost = 75; duplicate.level = 1;
    compact.push_back(duplicate);
    duplicate = compact[34]; duplicate.passable = false;
    compact.push_back(duplicate);
    for (bool reverse : {false, true}) {
        if (reverse) std::reverse(compact.begin(), compact.end());
        if (!compare(compact, {{{0, 0}, {15, 15}}, {{1, 1}, {2, 2}},
                {{15, 15}, {0, 0}}, {{0, 15}, {-1, 15}}}, true)) return false;
    }
    // The 4:1 area threshold is inclusive and absent cells remain absent.
    std::vector<PathTerrainCell> boundary(4);
    for (int i = 0; i < 3; ++i) boundary[i].cell = {i, 0};
    boundary[3].cell = {15, 0};
    if (!compare(boundary, {{{0, 0}, {2, 0}}, {{2, 0}, {0, 0}}}, true)) return false;
    boundary[3].cell.x = 16;
    if (!compare(boundary, {{{0, 0}, {2, 0}}, {{2, 0}, {0, 0}}}, false)) return false;
    std::vector<PathTerrainCell> extremes;
    std::vector<std::pair<PathCell, PathCell>> extremeRequests;
    for (int origin : {std::numeric_limits<int>::min() + 64,
                       std::numeric_limits<int>::max() - 64}) {
        std::vector<PathTerrainCell> island;
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
            PathTerrainCell cell; cell.cell = {origin + x, origin + y};
            island.push_back(cell);
        }
        const std::vector<std::pair<PathCell, PathCell>> requests{
            {{origin, origin}, {origin + 10, origin + 10}},
            {{origin + 10, origin + 10}, {origin, origin}}};
        if (!compare(island, requests, true)) return false;
        extremes.insert(extremes.end(), island.begin(), island.end());
        extremeRequests.insert(extremeRequests.end(), requests.begin(), requests.end());
    }
    // A >32-bit bounding dimension must choose the hash map without overflow.
    if (!compare(extremes, extremeRequests, false)) return false;
    std::cout << "dense/hash terrain lookup equivalence passed\n";
    return true;
}

bool sameCompleteResult(const PathResult& a, const PathResult& b) {
    if (std::tie(a.requestId, a.start.x, a.start.y, a.goal.x, a.goal.y,
            a.resolvedGoal.x, a.resolvedGoal.y, a.completed, a.reachable,
            a.cacheHit, a.cacheMiss, a.goalAdjusted, a.searchBudgetExhausted,
            a.queueBackpressure, a.fallbackTargetCacheHit, a.fallbackRing,
            a.mapQueryApproachUsed, a.sharedApproachCacheHit,
            a.collapsedFromEquivalentRequest, a.status, a.passabilityProfileHash,
            a.requestClass, a.expandedNodes, a.expandedNodeBudget,
            a.fallbackTargetsTried, a.fallbackCandidateCount, a.goalSlotIndex,
            a.noPathCooldownTicksRemaining, a.failureReason)
        != std::tie(b.requestId, b.start.x, b.start.y, b.goal.x, b.goal.y,
            b.resolvedGoal.x, b.resolvedGoal.y, b.completed, b.reachable,
            b.cacheHit, b.cacheMiss, b.goalAdjusted, b.searchBudgetExhausted,
            b.queueBackpressure, b.fallbackTargetCacheHit, b.fallbackRing,
            b.mapQueryApproachUsed, b.sharedApproachCacheHit,
            b.collapsedFromEquivalentRequest, b.status, b.passabilityProfileHash,
            b.requestClass, b.expandedNodes, b.expandedNodeBudget,
            b.fallbackTargetsTried, b.fallbackCandidateCount, b.goalSlotIndex,
            b.noPathCooldownTicksRemaining, b.failureReason)
        || a.path.size() != b.path.size()) return false;
    for (std::size_t i = 0; i < a.path.size(); ++i)
        if (a.path[i].x != b.path[i].x || a.path[i].y != b.path[i].y) return false;
    return true;
}

bool sameCompleteStats(const PathRequestQueueLiveStats& a, const PathRequestQueueLiveStats& b) {
    return std::tie(a.pendingCount, a.cacheHits, a.cacheMisses, a.budgetExhaustedResults,
        a.lastPumpBudgetExhausted, a.backpressureRejectedRequests,
        a.requestOverflowRejectedRequests, a.overflowRejectedResults,
        a.collapsedPendingRequests, a.massAttackCollapsedRequests,
        a.sharedApproachCacheHits, a.sharedApproachCacheMisses,
        a.repeatedFailureSuppressed, a.noPathCooldownHits, a.cooldownNoPathResults,
        a.realNoPathResults, a.equivalentStartBucketCollapses, a.goalSlotAssignments,
        a.fallbackCandidatesConsidered, a.fallbackRingRequests,
        a.fallbackTargetCacheHits, a.fallbackTargetCacheMisses,
        a.mapQueryApproachRequests, a.mapQueryApproachUsedResults,
        a.lastPumpExpandedNodes, a.cachedPathCount, a.fallbackTargetCacheEntries,
        a.fallbackDistanceQueries, a.fallbackComponentCellChecks,
        a.heuristicEvaluations, a.usesDenseTerrainLookup, a.usesShortHashKeys,
        a.mapQueryApproachAvailable, a.usesBlockedComponentApproachFallback)
        == std::tie(b.pendingCount, b.cacheHits, b.cacheMisses, b.budgetExhaustedResults,
        b.lastPumpBudgetExhausted, b.backpressureRejectedRequests,
        b.requestOverflowRejectedRequests, b.overflowRejectedResults,
        b.collapsedPendingRequests, b.massAttackCollapsedRequests,
        b.sharedApproachCacheHits, b.sharedApproachCacheMisses,
        b.repeatedFailureSuppressed, b.noPathCooldownHits, b.cooldownNoPathResults,
        b.realNoPathResults, b.equivalentStartBucketCollapses, b.goalSlotAssignments,
        b.fallbackCandidatesConsidered, b.fallbackRingRequests,
        b.fallbackTargetCacheHits, b.fallbackTargetCacheMisses,
        b.mapQueryApproachRequests, b.mapQueryApproachUsedResults,
        b.lastPumpExpandedNodes, b.cachedPathCount, b.fallbackTargetCacheEntries,
        b.fallbackDistanceQueries, b.fallbackComponentCellChecks,
        b.heuristicEvaluations, b.usesDenseTerrainLookup, b.usesShortHashKeys,
        b.mapQueryApproachAvailable, b.usesBlockedComponentApproachFallback);
}

bool profileHashArithmeticChecks() {
    // This oracle is the original literal eight-byte loop above. In
    // particular, expected values do not call either new production helper.
    constexpr std::array<std::uint64_t, 10> seeds{
        0, 1, 14695981039346656037ULL, 1099511628211ULL,
        std::numeric_limits<std::uint64_t>::max(),
        0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL,
        0x8000000000000000ULL, 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL};
    std::size_t scalarChecks = 0, chainChecks = 0;
    const auto check = [&](std::uint64_t hash, std::int64_t value) {
        auto expected = hash;
        hashInt(expected, static_cast<std::uint64_t>(value));
        ++scalarChecks;
        return runtimePathProfileHashOriginal(hash, value) == expected
            && runtimePathProfileHashC22(hash, value) == expected
            && runtimePathProfileHashFast(hash, value) == expected;
    };
    for (auto seed : seeds) {
        for (std::int64_t value = 0; value <= 255; ++value)
            if (!check(seed, value)) return false;
        for (auto value : {std::numeric_limits<std::int64_t>::min(),
                std::numeric_limits<std::int64_t>::max(), std::int64_t{-1},
                std::int64_t{-255}, std::int64_t{-256}, std::int64_t{-257},
                std::int64_t{256}, std::int64_t{257}, std::int64_t{65535},
                std::int64_t{65536}, std::int64_t{65537},
                std::int64_t{std::numeric_limits<int>::min()},
                std::int64_t{std::numeric_limits<int>::max()}})
            if (!check(seed, value)) return false;
        // Bit patterns cover every byte boundary, the sign bit, and its two
        // adjacent values without signed arithmetic overflow/conversion.
        for (int bit = 0; bit < 64; ++bit) {
            const std::uint64_t boundary = std::uint64_t{1} << bit;
            for (auto bits : {boundary - 1, boundary, boundary + 1,
                             ~boundary})
                if (!check(seed, std::bit_cast<std::int64_t>(bits))) return false;
        }
        std::uint64_t random = seed ^ 0x9E3779B97F4A7C15ULL;
        auto expected = seed, original = seed, c22 = seed, fast = seed;
        for (int i = 0; i < 4096; ++i) {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            const std::uint64_t bits = i % 4 == 0 ? random & 255U
                : i % 4 == 1 ? (random & 255U) + 256U
                : i % 4 == 2 ? ~std::uint64_t{random & 255U} : random;
            const auto value = std::bit_cast<std::int64_t>(bits);
            hashInt(expected, bits);
            original = runtimePathProfileHashOriginal(original, value);
            c22 = runtimePathProfileHashC22(c22, value);
            fast = runtimePathProfileHashFast(fast, value);
            ++chainChecks;
            if (original != expected || c22 != expected || fast != expected) return false;
        }
    }
    const int previousOverride = runtimePathProfileHashReferenceOverride;
    const bool previousReference = runtimePathProfileHashReference();
    {
        RuntimePathProfileHashReferenceScope outer(false);
        if (runtimePathProfileHashReference()) return false;
        {
            RuntimePathProfileHashReferenceScope inner(true);
            if (!runtimePathProfileHashReference()) return false;
            {
                RuntimePathProfileHashReferenceScope innermost(false);
                if (runtimePathProfileHashReference()) return false;
            }
            if (!runtimePathProfileHashReference()) return false;
        }
        if (runtimePathProfileHashReference()) return false;
    }
    if (runtimePathProfileHashReferenceOverride != previousOverride
        || runtimePathProfileHashReference() != previousReference) return false;
    std::cout << "profile hash arithmetic equivalence passed scalarChecks="
        << scalarChecks << " chainedSteps=" << chainChecks
        << " nestedReference=yes originalEightByteOracle=yes\n";
    return true;
}

bool profileZeroRunsArithmeticChecks() {
    // Independent original 80-byte node oracle. No production hash helper is
    // used to compute the expected value, including the key/level prefix.
    const auto oracle = [](std::uint64_t hash, std::int64_t key, int level,
            bool passable, int cost) {
        for (const auto value : std::array<std::int64_t, 10>{
                key, level, passable ? 1 : 0, 0, 0, 0, 0, 0, cost, 0})
            hashInt(hash, static_cast<std::uint64_t>(value));
        return hash;
    };
    constexpr std::array<std::uint64_t, 6> seeds{0, 1, 14695981039346656037ULL,
        std::numeric_limits<std::uint64_t>::max(), 0x0123456789ABCDEFULL,
        0x8000000000000000ULL};
    constexpr std::array<std::int64_t, 10> keys{0, 1, 255, 256, 65535, -1,
        0x000000FF000000FFLL, 0x000001DF000001DFLL,
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max()};
    constexpr std::array<int, 5> levels{std::numeric_limits<int>::min(), -1,
        0, 1, std::numeric_limits<int>::max()};
    std::size_t scalarChecks = 0, chainChecks = 0;
    for (auto seed : seeds) for (auto key : keys) for (int level : levels)
        for (bool passable : {false, true}) for (int cost = 1; cost <= 255; ++cost) {
            if (runtimePathProfileHashFlatDry(seed, key, level, passable, cost)
                != oracle(seed, key, level, passable, cost)) return false;
            ++scalarChecks;
        }
    for (auto seed : seeds) {
        auto expected = seed, actual = seed;
        std::uint64_t random = seed ^ 0x9E3779B97F4A7C15ULL;
        for (int i = 0; i < 4096; ++i) {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto key = std::bit_cast<std::int64_t>(random);
            const int level = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(random >> 32));
            const bool passable = (random & 1U) != 0;
            const int cost = 1 + static_cast<int>((random >> 8) % 255U);
            expected = oracle(expected, key, level, passable, cost);
            actual = runtimePathProfileHashFlatDry(actual, key, level, passable, cost);
            if (actual != expected) return false;
            ++chainChecks;
        }
    }
    const int oldOverride = runtimePathProfileZeroRunsReferenceOverride;
    const bool oldReference = runtimePathProfileZeroRunsReference();
    const int oldOriginalOverride = runtimePathProfileHashReferenceOverride;
    const int oldSparseOverride = runtimePathProfileSparseBytesReferenceOverride;
    {
        RuntimePathProfileZeroRunsReferenceScope outer(false);
        if (runtimePathProfileZeroRunsReference()) return false;
        {
            RuntimePathProfileZeroRunsReferenceScope inner(true);
            if (!runtimePathProfileZeroRunsReference()) return false;
            {
                RuntimePathProfileZeroRunsReferenceScope innerAgain(false);
                if (runtimePathProfileZeroRunsReference()) return false;
            }
            if (!runtimePathProfileZeroRunsReference()) return false;
        }
        if (runtimePathProfileZeroRunsReference()) return false;
    }
    if (runtimePathProfileZeroRunsReferenceOverride != oldOverride
        || runtimePathProfileZeroRunsReference() != oldReference
        || runtimePathProfileHashReferenceOverride != oldOriginalOverride
        || runtimePathProfileSparseBytesReferenceOverride != oldSparseOverride) return false;
    std::cout << "zero-run profile arithmetic passed scalarChecks=" << scalarChecks
        << " chainSteps=" << chainChecks
        << " cost1Through255=all original80ByteOracle=yes nestedIndependentFlag=yes\n";
    return scalarChecks == 153000 && chainChecks == 24576;
}

bool profileZeroRunsConstructionMicrobench(bool benchmark) {
    if (!benchmark) return true;
    std::vector<PathTerrainCell> cells;
    cells.reserve(114960);
    for (int y = 1; y <= 480; ++y) for (int x = 1; x <= 480; ++x) {
        const int u = x - y + 240, v = x + y - 240;
        if (u < 0 || u >= 480 || v < 1 || v >= 480) continue;
        PathTerrainCell cell;
        cell.cell = {x, y};
        cell.passable = (x + y) % 19 != 0;
        cells.push_back(cell);
    }
    if (cells.size() != 114960) return false;
    // Two real owned constructor workloads: all positive admissions and all
    // water fallbacks. The complete small-queue matrix below checks paths and
    // LiveStats. No wall-time threshold or FPS inference is an assertion.
    for (bool water : {false, true}) {
        for (auto& cell : cells) cell.water = water;
        PathRequestQueueTerrainLayoutCache layouts[2];
        std::string expectedProfile;
        for (int route = 0; route < 2; ++route) {
            RuntimePathProfileHashReferenceScope original(false);
            RuntimePathProfileSparseBytesReferenceScope sparse(false);
            RuntimePathProfileZeroRunsReferenceScope zero(route == 0);
            PathRequestQueue warm(cells, layouts[route]);
        }
        for (int run = 0; run < 6; ++run) {
            const int route = run == 1 || run == 2 || run == 4 ? 1 : 0;
            RuntimePathProfileHashReferenceScope original(false);
            RuntimePathProfileSparseBytesReferenceScope sparse(false);
            RuntimePathProfileZeroRunsReferenceScope zero(route == 0);
            double elapsedMs = 0.0;
            for (int repeat = 0; repeat < 2; ++repeat) {
                const auto started = std::chrono::steady_clock::now();
                PathRequestQueue queue(cells, layouts[route]);
                elapsedMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                PathApproachContext context;
                context.movementMode = PathMovementMode::Air;
                queue.submit(cells.front().cell, cells.front().cell, context);
                queue.pump(1);
                if (queue.getResults().size() != 1
                    || !queue.getResults().front().completed) return false;
                const auto& profile = queue.getResults().front().passabilityProfileHash;
                if (expectedProfile.empty()) expectedProfile = profile;
                if (profile != expectedProfile) return false;
            }
            std::cout << "zero-run owned construction microbench route="
                << (route == 0 ? "C26-reference" : "zero-runs")
                << " water=" << (water ? "yes" : "no")
                << " cells=" << cells.size() << " rounds=2 ms=" << elapsedMs
                << " profile=" << expectedProfile
                << " wallOnly=yes gameFpsInferred=no\n";
        }
    }
    return true;
}

bool profileHashQueueEquivalenceChecks() {
    const char* layoutEnvironment = std::getenv("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
    const bool forcedLayoutEnvironment = layoutEnvironment
        && std::string(layoutEnvironment) == "1";
    std::size_t constructions = 0, resultChecks = 0, combinationChecks = 0;
    const auto sameQueues = [&](const PathRequestQueue& a, const PathRequestQueue& b) {
        if (!sameCompleteStats(a.liveStats(), b.liveStats())
            || a.getResults().size() != b.getResults().size()) return false;
        for (std::size_t i = 0; i < a.getResults().size(); ++i) {
            if (!sameCompleteResult(a.getResults()[i], b.getResults()[i])) return false;
            ++resultChecks;
        }
        return true;
    };
    const auto exerciseQueues = [&](PathRequestQueue& a, PathRequestQueue& b,
            PathCell start, PathCell goal, bool simpleRequest) {
        if (!sameQueues(a, b)) return false;
        for (int i = 0; i < (simpleRequest ? 1 : 12); ++i) {
            PathApproachContext context;
            context.movementMode = simpleRequest ? PathMovementMode::Air
                : static_cast<PathMovementMode>(i % 4);
            context.requesterStableHash = i / 2 + 1;
            context.movementConstraintHash = i / 2 + 1;
            context.noPathCooldownTicks = 3;
            context.goalSlotCount = simpleRequest ? 0 : 2;
            context.shareAcrossGoalSlots = true;
            context.mapQueryApproachAvailable = !simpleRequest && i % 3 == 0;
            if (context.mapQueryApproachAvailable) context.approachCells = {goal, start};
            if (!simpleRequest) context.alternateGoalCells = {goal};
            if (a.submit(start, goal, context).id != b.submit(start, goal, context).id
                || !sameQueues(a, b)) return false;
            if (i % 2) {
                const int budget = i % 4 == 1 ? 2 : 256;
                if (a.pumpWithBudget(2, budget) != b.pumpWithBudget(2, budget)
                    || !sameQueues(a, b)) return false;
            }
        }
        if (a.pumpWithBudget(32, 4096) != b.pumpWithBudget(32, 4096)
            || a.getResults().empty() || !sameQueues(a, b)) return false;
        for (const auto& result : a.getResults())
            if (!result.completed || result.passabilityProfileHash.empty()) return false;
        return true;
    };
    // Cross the independent profile and layout reference switches. Expected
    // always reconstructs the original layout with the original hash, while
    // the actual queue exercises cold/warm layout and metadata reconstruction.
    for (bool referenceLayout : {false, true}) {
        for (bool referenceProfile : {false, true}) {
          for (bool referenceSparse : {false, true}) {
           for (bool referenceZeroRuns : {false, true}) {
            PathRequestQueueTerrainLayoutCache actualLayout, expectedLayout;
            const auto compare = [&](const std::vector<PathTerrainCell>& input,
                    bool expectReuse, bool simpleRequest = false) {
                auto cells = input;
                const PathCell start = cells.empty() ? PathCell{0, 0} : cells.front().cell;
                const PathCell goal = simpleRequest ? start
                    : cells.empty() ? PathCell{1, 1} : cells[cells.size() / 2].cell;
                PathRequestQueue actual = [&] {
                    RuntimePathProfileHashReferenceScope scope(referenceProfile);
                    RuntimePathProfileSparseBytesReferenceScope sparseScope(referenceSparse);
                    RuntimePathProfileZeroRunsReferenceScope zeroScope(referenceZeroRuns);
                    return PathRequestQueue(cells, actualLayout, referenceLayout);
                }();
                PathRequestQueue expected = [&] {
                    RuntimePathProfileHashReferenceScope scope(true);
                    RuntimePathProfileSparseBytesReferenceScope sparseScope(true);
                    RuntimePathProfileZeroRunsReferenceScope zeroScope(true);
                    return PathRequestQueue(input, expectedLayout, true);
                }();
                constructions += 2;
                if (actualLayout.lastConstructionReused()
                    != (expectReuse && !referenceLayout && !forcedLayoutEnvironment)
                    || expectedLayout.lastConstructionReused()) return false;
                // Input ownership and the constructor-only choice are checked
                // after dropping the input and reversing the ambient override.
                for (auto& cell : cells) { cell.passable = false; cell.cost = 1000000; }
                std::vector<PathTerrainCell>().swap(cells);
                RuntimePathProfileHashReferenceScope later(!referenceProfile);
                RuntimePathProfileSparseBytesReferenceScope laterSparse(!referenceSparse);
                RuntimePathProfileZeroRunsReferenceScope laterZero(!referenceZeroRuns);
                return exerciseQueues(actual, expected, start, goal, simpleRequest);
            };
            if (!compare({}, false) || !compare({}, true)) return false;
            // Force positive zero-run coverage in complete real queues, then
            // perturb each admission field independently. Expected remains
            // the original literal-byte constructor with a fresh layout.
            auto dry = terrain(16, false);
            for (std::size_t i = 0; i < dry.size(); ++i) {
                dry[i].level = static_cast<int>(i % 3) - 1;
                dry[i].cost = 1 + static_cast<int>(i % 255);
                dry[i].hazardCost = 0;
            }
            if (!compare(dry, false) || !compare(dry, true)) return false;
            auto dryDuplicate = dry[17];
            dryDuplicate.passable = false; dryDuplicate.cost = 255;
            dryDuplicate.level = 4; dry.push_back(dryDuplicate);
            if (!compare(dry, false) || !compare(dry, true)) return false;
            for (int field = 0; field < 13; ++field) {
                auto admission = dry;
                auto& changed = admission[0];
                switch (field) {
                case 0: changed.passable = false; break;
                case 1: changed.water = true; break;
                case 2: changed.shore = true; break;
                case 3: changed.bridge = true; break;
                case 4: changed.ramp = true; break;
                case 5: changed.cliff = true; break;
                case 6: changed.cost = 0; break;
                case 7: changed.cost = 255; break;
                case 8: changed.cost = 256; break;
                case 9: changed.hazardCost = -1; break;
                case 10: changed.hazardCost = 1; break;
                case 11: changed.level = std::numeric_limits<int>::min(); break;
                case 12: changed.level = std::numeric_limits<int>::max(); break;
                }
                if (!compare(admission, true, field >= 11)) return false;
            }
            auto cells = terrain(16, false);
            for (std::size_t i = 0; i < cells.size(); ++i) {
                auto& cell = cells[i];
                cell.level = static_cast<int>(i % 3) - 1;
                cell.water = cell.cell.y >= 12;
                cell.shore = cell.cell.y == 11 || cell.cell.y == 12;
                cell.bridge = cell.cell.x == 7;
                cell.ramp = i % 7 == 0;
                cell.cliff = i % 29 == 0;
                cell.cost = i % 11 == 0 ? 256 : 1 + static_cast<int>(i % 255);
                cell.hazardCost = i % 13 == 0 ? 1024 : static_cast<int>(i % 256);
                cell.resource = i % 5 == 0;
            }
            auto duplicate = cells[17];
            duplicate.level = 4; duplicate.passable = false; duplicate.water = true;
            duplicate.shore = true; duplicate.bridge = true; duplicate.ramp = true;
            duplicate.cliff = true; duplicate.cost = 512; duplicate.hazardCost = 256;
            cells.push_back(duplicate);
            cells.push_back(cells[34]);
            if (!compare(cells, false) || !compare(cells, true)) return false;
            // A warm coordinate layout must still hash every changed metadata
            // field after the exact duplicate reduction and cost clamps.
            auto changed = cells;
            auto& first = changed[17]; auto& alias = changed[256];
            first.level = -9; alias.level = -4;
            first.passable = alias.passable = true;
            first.water = alias.water = false; first.shore = alias.shore = false;
            first.bridge = alias.bridge = false; first.ramp = alias.ramp = false;
            first.cliff = alias.cliff = false; first.resource = alias.resource = true;
            first.cost = -20; alias.cost = 255;
            first.hazardCost = -15; alias.hazardCost = 255;
            if (!compare(changed, true)) return false;
            std::reverse(changed.begin(), changed.end());
            if (!compare(changed, false) || !compare(changed, true)) return false;
            auto shifted = terrain(4, false);
            for (auto& cell : shifted) { cell.cell.x -= 120; cell.cell.y -= 120; }
            if (!compare(shifted, false) || !compare(shifted, true)) return false;
            // Distinct 64-bit keys can collide under the runtime's unrelated
            // y*1000+x encoding; they must keep their original profile identity.
            std::vector<PathTerrainCell> sparse(3);
            sparse[0].cell = {0, 1}; sparse[1].cell = {1000, 0};
            sparse[2].cell = {2000000, -2000000}; sparse[1].passable = false;
            if (!compare(sparse, false, true) || !compare(sparse, true, true)) return false;
            // Exercise signed keys, levels and integer limits without asking
            // A* to subtract widely separated/extreme coordinates or costs.
            std::vector<PathTerrainCell> extremes(4);
            extremes[0].cell = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
            extremes[1].cell = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
            extremes[2].cell = {-1, 0}; extremes[3].cell = {0, -1};
            extremes[0].level = std::numeric_limits<int>::min();
            extremes[1].level = std::numeric_limits<int>::max();
            extremes[2].cost = std::numeric_limits<int>::min();
            extremes[3].cost = std::numeric_limits<int>::max();
            extremes[2].hazardCost = std::numeric_limits<int>::min();
            extremes[3].hazardCost = std::numeric_limits<int>::max();
            if (!compare(extremes, false, true) || !compare(extremes, true, true)) return false;
            ++combinationChecks;
           }
          }
        }
    }
    std::cout << "profile hash queue equivalence passed layoutHashCombinations="
        << combinationChecks << " constructions=" << constructions
        << " completeResultChecks=" << resultChecks
        << " completeStats=yes queueOwned=yes originalProfileReference=yes"
        << " zeroRunFlagBoth=yes admissionFieldPerturbations=13\n";
    return combinationChecks == 16;
}

bool terrainLayoutReuseChecks() {
    static_assert(noexcept(std::declval<PathRequestQueueTerrainLayoutCache&>().swap(
        std::declval<PathRequestQueueTerrainLayoutCache&>())));
    const char* reference = std::getenv("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
    const bool layoutReference = reference && std::string(reference) == "1";
    const char* movementReferenceValue = std::getenv("CONCORD_MOVEMENT_REFERENCE");
    const bool movementReference = movementReferenceValue && std::string(movementReferenceValue) == "1";
    std::size_t constructions = 0, resultChecks = 0;
    const auto sameQueues = [&](const PathRequestQueue& a, const PathRequestQueue& b) {
        if (!sameCompleteStats(a.liveStats(), b.liveStats())
            || a.getResults().size() != b.getResults().size()) return false;
        for (std::size_t i = 0; i < a.getResults().size(); ++i) {
            if (!sameCompleteResult(a.getResults()[i], b.getResults()[i])) return false;
            ++resultChecks;
        }
        return true;
    };
    const auto exerciseQueues = [&](PathRequestQueue& a, PathRequestQueue& b,
            PathCell start, PathCell goal) {
        if (!sameQueues(a, b)) return false;
        for (int i = 0; i < 12; ++i) {
            PathApproachContext context;
            context.movementMode = static_cast<PathMovementMode>(i % 4);
            context.requesterStableHash = i / 2 + 1;
            context.movementConstraintHash = i / 2 + 1;
            context.noPathCooldownTicks = 3;
            context.goalSlotCount = 2;
            context.shareAcrossGoalSlots = true;
            context.mapQueryApproachAvailable = i % 3 == 0;
            if (context.mapQueryApproachAvailable) context.approachCells = {goal, start};
            context.alternateGoalCells = {goal};
            if (a.submit(start, goal, context).id != b.submit(start, goal, context).id
                || !sameQueues(a, b)) return false;
            if (i % 2) {
                const int budget = i % 4 == 1 ? 2 : 256;
                if (a.pumpWithBudget(2, budget) != b.pumpWithBudget(2, budget)
                    || !sameQueues(a, b)) return false;
            }
        }
        if (a.pumpWithBudget(32, 4096) != b.pumpWithBudget(32, 4096)) return false;
        return sameQueues(a, b);
    };
    PathRequestQueueTerrainLayoutCache layout;
    const auto compare = [&](const std::vector<PathTerrainCell>& cells, bool expectReuse,
            bool forceReferenceLayout = false, bool forceReferenceLookup = false) {
        PathRequestQueue actual(cells, layout, forceReferenceLayout, forceReferenceLookup);
        PathRequestQueue expected(cells, forceReferenceLookup);
        ++constructions;
        if (layout.lastConstructionReused() != (expectReuse && !layoutReference
                && !forceReferenceLayout)) return false;
        const PathCell start = cells.empty() ? PathCell{0, 0} : cells.front().cell;
        const PathCell goal = cells.empty() ? PathCell{1, 1} : cells[cells.size() / 2].cell;
        return exerciseQueues(actual, expected, start, goal);
    };
    if (!compare({}, false) || !compare({}, true)) return false;
    auto cells = terrain(12, true);
    auto duplicate = cells[13];
    duplicate.cost = 50; duplicate.hazardCost = 75; duplicate.level = 1;
    cells.push_back(duplicate);
    cells.push_back(cells[26]);
    if (!compare(cells, false) || !compare(cells, true)) return false;
    // Every raw dynamic field changes without changing coordinate identity.
    // Clamps, duplicate reductions and the complete profile hash stay exact.
    for (int field = 0; field < 11; ++field) {
        auto changed = cells;
        auto& a = changed[13]; auto& b = changed[144];
        switch (field) {
        case 0: a.level = -2; b.level = 4; break;
        case 1: a.passable = false; b.passable = true; break;
        case 2: a.water = true; break;
        case 3: b.shore = true; break;
        case 4: a.bridge = true; break;
        case 5: b.ramp = true; break;
        case 6: a.cliff = true; break;
        case 7: b.resource = true; break;
        case 8: a.cost = -7; b.cost = 80; break;
        case 9: a.hazardCost = -3; b.hazardCost = 240; break;
        case 10: std::swap(a, b); break; // same-coordinate raw aliases
        }
        if (!compare(changed, true)) return false;
    }
    if (!compare(cells, false, true) || !compare(cells, true)) return false;
    if (!compare(cells, movementReference, false, true) || !compare(cells, true, false, true)
        || !compare(cells, movementReference) || !compare(cells, true)) return false;
    std::reverse(cells.begin(), cells.end());
    if (!compare(cells, false) || !compare(cells, true)) return false;
    cells.back().cell.x += 1;
    if (!compare(cells, false) || !compare(cells, true)) return false;
    cells.pop_back();
    if (!compare(cells, false) || !compare(cells, true)) return false;
    // A runtime y*1000+x alias is not a duplicate of the queue's 64-bit key.
    std::vector<PathTerrainCell> aliases(3);
    aliases[0].cell = {0, 1}; aliases[1].cell = {1000, 0}; aliases[2].cell = {0, 2};
    aliases[1].passable = false;
    if (!compare(aliases, false) || !compare(aliases, true)) return false;
    for (int origin : {-120, std::numeric_limits<int>::min() + 64,
                       std::numeric_limits<int>::max() - 64}) {
        auto shifted = terrain(4, false);
        for (auto& cell : shifted) { cell.cell.x += origin; cell.cell.y += origin; }
        if (!compare(shifted, false) || !compare(shifted, true)) return false;
    }
    auto sparse = terrain(4, false);
    sparse.back().cell = {2000000, 2000000};
    if (!compare(sparse, false) || !compare(sparse, true)) return false;
    auto heldCells = terrain(12, false);
    PathRequestQueue held(heldCells, layout);
    PathRequestQueue heldReference(heldCells);
    PathRequestQueue copied = held;
    PathRequestQueueTerrainLayoutCache copiedLayout = layout;
    layout.clear(); heldCells.clear();
    if (layout.retainedCellCount() != 0 || layout.lastConstructionReused()) return false;
    PathRequestQueueTerrainLayoutCache swapped;
    swapped.swap(copiedLayout);
    if (copiedLayout.retainedCellCount() != 0
        || swapped.retainedCellCount() != (layoutReference ? 0U : 144U)) return false;
    if (!compare(sparse, false)
        || !exerciseQueues(held, heldReference, {0, 0}, {11, 11})) return false;
    PathRequestQueue copiedReference(terrain(12, false));
    if (!exerciseQueues(copied, copiedReference, {0, 0}, {11, 11})) return false;
    // One cell over the hard cap clears the prior retained layout and uses
    // the original construction route. Compare construction only: no huge A*.
    std::vector<PathTerrainCell> oversized(PathRequestQueueTerrainLayoutCache::maxRetainedCells + 1);
    for (std::size_t i = 0; i < oversized.size(); ++i)
        oversized[i].cell = {static_cast<int>(i), 0};
    PathRequestQueue oversizedActual(oversized, layout);
    PathRequestQueue oversizedReference(oversized);
    constructions += 2;
    if (layout.retainedCellCount() != 0 || layout.lastConstructionReused()
        || !sameCompleteStats(oversizedActual.liveStats(), oversizedReference.liveStats())) return false;
    std::cout << "terrain layout reuse equivalence passed constructions=" << constructions
        << " completeResultChecks=" << resultChecks << " completeStats=yes queueOwned=yes\n";
    return true;
}

#include "RuntimePathQueueReadOnlyTerrainContract.inl"
#include "RuntimePathQueueSharedLayoutContract.inl"
#include "RuntimePathQueueWarmAdmissionContract.inl"

} // namespace

int runOriginalCompleteQueueGold(int argc, char** argv) {
    const bool benchmark = argc > 1 && std::string(argv[1]) == "--benchmark";
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t ordinary = exercise(false, 96, false);
    const auto ordinaryEnd = std::chrono::steady_clock::now();
    const std::uint64_t blocked = exercise(true, 96, false);
    const auto blockedEnd = std::chrono::steady_clock::now();
    const std::uint64_t reversed = exercise(true, 96, true);
    const std::uint64_t irregular = irregularTerrain();
    std::cout << "ordinary=" << ordinary << " blocked=" << blocked
              << " reversed=" << reversed << " irregular=" << irregular << '\n';
    if (benchmark) {
        std::cout << "ordinary_ms="
                  << std::chrono::duration<double, std::milli>(ordinaryEnd - start).count()
                  << " blocked_ms="
                  << std::chrono::duration<double, std::milli>(blockedEnd - ordinaryEnd).count()
                  << '\n';
    }
    // Captured from the original hash-based implementation. The fixture
    // includes all movement domains, partial-path budgets, hazards, alternate
    // goals, footprint fallback, and a different authored terrain order.
    const bool pathsUnchanged = ordinary == 12452642976107788296ULL
        && blocked == 6314123529439397177ULL
        && reversed == 1587240991571403433ULL
        && irregular == 7135364384096626644ULL;
    if (!pathsUnchanged) {
        std::cerr << "Path results differ from the original implementation.\n";
    }
    const bool componentsValid = edgeCases();
    if (!componentsValid) {
        std::cerr << "Cached footprint differs from an isolated search.\n";
    }
    const bool pendingValid = pendingLookupChecks(benchmark);
    if (!pendingValid) {
        std::cerr << "Pending collapse lookup, FIFO, copy or overflow failed.\n";
    }
    const bool largeFootprintsValid = largeFootprintPaths(benchmark);
    if (!largeFootprintsValid) {
        std::cerr << "Large footprint paths or candidate work bounds changed.\n";
    }
    const bool heuristicsValid = heuristicWorkChecks(benchmark);
    if (!heuristicsValid) {
        std::cerr << "A* heuristic work or unreachable search result changed.\n";
    }
    const bool terrainLookupValid = terrainLookupEquivalenceChecks();
    if (!terrainLookupValid) std::cerr << "Dense terrain lookup differs from hash lookup.\n";
    const bool terrainLayoutValid = terrainLayoutReuseChecks();
    if (!terrainLayoutValid) std::cerr << "Reused terrain layout differs from a fresh owned queue.\n";
    const bool profileArithmeticValid = profileHashArithmeticChecks();
    const bool sparseArithmeticValid = runtimePathProfileSparseBytesArithmeticChecks();
    if (!sparseArithmeticValid) std::cerr << "Sparse-byte profile differs from C22 or the literal eight-byte oracle.\n";
    if (!profileArithmeticValid) std::cerr << "Profile hash shortcut differs from the original eight-byte arithmetic.\n";
    const bool profileQueuesValid = profileHashQueueEquivalenceChecks();
    if (!profileQueuesValid) std::cerr << "Profile hash or layout reference changed complete queue state/results.\n";
    const bool zeroRunArithmeticValid = profileZeroRunsArithmeticChecks();
    if (!zeroRunArithmeticValid) std::cerr << "Zero-run profile differs from the original80-byte node oracle.\n";
    const bool zeroRunConstructionValid = profileZeroRunsConstructionMicrobench(benchmark);
    if (!zeroRunConstructionValid) std::cerr << "Zero-run construction profile or queue result changed.\n";
    bool readOnlyViewValid = true;
    for (bool layoutViewReference : {false, true}) {
        RuntimePathLayoutViewReferenceScope scope(layoutViewReference);
        readOnlyViewValid = readOnlyTerrainViewChecks(benchmark) && readOnlyViewValid;
    }
    const bool warmAdmissionValid = warmAdmissionFusionChecks();
    const bool sharedLayoutValid = sharedLayoutViewChecks(benchmark);
    if (!sharedLayoutValid) std::cerr << "Immutable shared layout lifetime, mapping or complete queue oracle failed.\n";
    if (!readOnlyViewValid) std::cerr << "Explicit readonly terrain view ownership, admission or complete queue oracle failed.\n";
    return pathsUnchanged && componentsValid && pendingValid
        && largeFootprintsValid && heuristicsValid && terrainLookupValid && terrainLayoutValid
        && profileArithmeticValid && sparseArithmeticValid && profileQueuesValid
        && zeroRunArithmeticValid && zeroRunConstructionValid && readOnlyViewValid && sharedLayoutValid && warmAdmissionValid ? 0 : 1;
}

int main(int argc,char** argv) {
    // Both exact algorithms run every existing fixed-gold/result/LiveStats/
    // ownership/alias/copy/move/reference matrix. No old assertion is deleted.
    for(bool reference : {true,false}) {
        RuntimePathProfileFixedSuffixReferenceScope scope(reference);
        if(runOriginalCompleteQueueGold(argc,argv)!=0)return 1;
    }
    std::cout << "Fixed suffix complete queue gold: routes=2 oldGolds=8 allLegacyContracts=yes fullResults=yes fullLiveStats=yes\n";
    return 0;
}