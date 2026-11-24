#include "simulation/navigation/pathfinding/PathGoalCandidates.h"
#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/detail/PathfindingCellHelpers.h"
#include "simulation/navigation/pathfinding/detail/PathRequestQueueFingerprint.h"
#include "runtime/performance/RuntimePerfTrace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef CONCORD_PATH_GOAL_CANDIDATE_TEST_OBSERVER
// Compiled only by the isolated S22 differential fixture; absent from product targets.
void recordPathGoalCandidatesForTest(int, int, const std::vector<PathCell>&);
#endif

namespace {

constexpr int DefaultExpandedNodesPerRequest = 16384;

using concord::simulation::pathfinding::detail::approachContextHash;
using concord::simulation::pathfinding::detail::cellKey;
using concord::simulation::pathfinding::detail::formatHash;
using concord::simulation::pathfinding::detail::goalSlotIndexForRequest;
using concord::simulation::pathfinding::detail::pathCellsLess;
using concord::simulation::pathfinding::detail::sameCell;

int octile(PathCell left, PathCell right) {
    const int dx = std::abs(left.x - right.x);
    const int dy = std::abs(left.y - right.y);
    const int diagonal = std::min(dx, dy);
    const int straight = std::max(dx, dy) - diagonal;
    return (diagonal * 14) + (straight * 10);
}

} // namespace

std::vector<PathCell> PathRequestQueue::reconstructPath(
    int currentIndex
) const {
    std::vector<PathCell> path;
    while (currentIndex >= 0) {
        path.push_back(terrain_[currentIndex].cell);
        currentIndex = searchNodes_[currentIndex].parent;
    }

    std::reverse(path.begin(), path.end());
    return path;
}

PathResult PathRequestQueue::solve(
    const PendingRequest& request,
    int expandedNodeBudget,
    PathGoalCandidateRunner* candidateRunner,
    std::int64_t simulationTick
) {
    RuntimePerfTraceScope solveTrace("pathQueue.solve", -1);
    PathResult result;
    result.requestId = request.id;
    result.start = request.start;
    result.goal = request.goal;
    result.resolvedGoal = request.goal;
    result.completed = true;
    result.status = PathResultStatus::Queued;
    const std::string profileHash = formatHash(passabilityProfileHash_);
    const std::string requestClass =
        "rq:" + formatHash(request.collapseKey);
    result.passabilityProfileHash = profileHash;
    result.requestClass = requestClass;
    const int requestBudget = std::max(
        1,
        std::min(DefaultExpandedNodesPerRequest, expandedNodeBudget)
    );
    result.expandedNodeBudget = requestBudget;

    if (const NoPathFailureCacheEntry* cachedFailure =
            findNoPathFailureCacheEntry(request.failureCacheKey, logicalTick_)) {
        result.resolvedGoal = cachedFailure->resolvedGoal;
        result.goalAdjusted = cachedFailure->goalAdjusted;
        result.fallbackRing = cachedFailure->fallbackRing;
        result.mapQueryApproachUsed = cachedFailure->mapQueryApproachUsed;
        result.fallbackTargetsTried = cachedFailure->fallbackTargetsTried;
        result.fallbackCandidateCount = cachedFailure->fallbackCandidateCount;
        result.goalSlotIndex = cachedFailure->goalSlotIndex;
        result.cacheHit = true;
        result.cacheMiss = false;
        result.status = PathResultStatus::CooldownNoPath;
        result.noPathCooldownTicksRemaining =
            std::max(0, cachedFailure->expiresAtTick - logicalTick_);
        result.failureReason = cachedFailure->failureReason;
        ++cacheHits_;
        ++repeatedFailureSuppressed_;
        ++noPathCooldownHits_;
        return result;
    }

    const std::uint64_t key = cacheKeyForRequest(
        request.start,
        request.goal,
        request.approachContextHash,
        request.goalSlotIndex
    );
    for (const auto& [cachedKey, cachedResult] : cache_) {
        if (cachedKey == key) {
            result = cachedResult;
            result.requestId = request.id;
            result.start = request.start;
            result.goal = request.goal;
            result.passabilityProfileHash = profileHash;
            result.requestClass = requestClass;
            result.goalAdjusted = !sameCell(result.resolvedGoal, request.goal);
            result.cacheHit = true;
            result.cacheMiss = false;
            result.status =
                result.reachable ? PathResultStatus::Reached : result.status;
            ++cacheHits_;
            result.completed = true;
            result.expandedNodes = 0;
            result.fallbackTargetsTried = 0;
            result.searchBudgetExhausted = false;
            result.expandedNodeBudget = requestBudget;
            if (result.mapQueryApproachUsed) {
                ++mapQueryApproachUsedResults_;
            }
            return result;
        }
    }

    const TerrainNode* startNode = terrainNodeAt(request.start);
    const TerrainNode* goalNode = terrainNodeAt(request.goal);
    if (startNode == nullptr) {
        result.cacheMiss = true;
        ++cacheMisses_;
        result.status = PathResultStatus::RealNoPath;
        result.failureReason = "Start cell is outside the terrain graph.";
        rememberNoPathFailure(
            request.failureCacheKey,
            result,
            logicalTick_,
            request.noPathCooldownTicks
        );
        return result;
    }

    if (sameCell(request.start, request.goal)) {
        result.cacheMiss = true;
        ++cacheMisses_;
        result.reachable = true;
        result.status = PathResultStatus::Reached;
        result.path.push_back(request.start);
        return result;
    }

    // The queue owns the immutable snapshot and component cache. The adapter is
    // lexical to this solve; the generator retains neither pointers nor spans.
    class GoalTerrainView final : public PathGoalTerrainView {
    public:
        explicit GoalTerrainView(const PathRequestQueue& queue) : queue_(queue) {}
        const PathTerrainCell* nodeAt(PathCell cell) const override {
            return queue_.terrainNodeAt(cell);
        }
        const PathTerrainCell& nodeAtIndex(int index) const override {
            return queue_.terrain_[index];
        }
        std::vector<int> indicesInBounds(std::int64_t minX, std::int64_t minY,
            std::int64_t maxX, std::int64_t maxY) const override {
            return queue_.terrainIndicesInBounds(minX, minY, maxX, maxY);
        }
    private:
        const PathRequestQueue& queue_;
    } terrainView(*this);
    const BlockedComponent& blockedComponent = blockedComponentAt(request.goal);
    const PathGoalCandidateInput candidateInput{request.start, request.goal,
        request.movementMode, request.goalSlotCount, request.mapQueryApproach,
        request.approachCells, request.alternateGoalCells};
    // The queue's complete merged-terrain fingerprint is a content revision:
    // it covers map and occupancy values, not merely a pointer or cell count.
    const PathGoalTaskStamp candidateStamp{1, 0, passabilityProfileHash_,
        passabilityProfileHash_, simulationTick, logicalTick_, request.id, requestBudget};
    auto generated = candidateRunner
        ? candidateRunner->generate(candidateInput, terrainView, candidateStamp)
        : generatePathGoalCandidates(candidateInput, terrainView);
    std::vector<PathCell> targetCandidates = std::move(generated.cells);
    if (generated.mapQueryApproachUsed) {
        result.mapQueryApproachUsed = true;
        ++mapQueryApproachUsedResults_;
    }
    if (generated.requiresFallbackRing) {
        result.fallbackRing = true;
        blockedComponentApproachFallbackUsed_ = true;
        ++fallbackRingRequests_;
        const std::uint64_t fallbackKey =
            fallbackTargetCacheKey(request.start, request.goal, blockedComponent);
        for (const auto& [cachedKey, cachedTargets] : fallbackTargetCache_) {
            if (cachedKey == fallbackKey) {
                targetCandidates = cachedTargets;
                ++fallbackTargetCacheHits_;
                result.fallbackTargetCacheHit = true;
                break;
            }
        }

        if (targetCandidates.empty()) {
            auto fallback = generatePathGoalFallbackCandidates(candidateInput,
                terrainView, {blockedComponent.valid, blockedComponent.fillsBounds,
                    blockedComponent.minX, blockedComponent.minY,
                    blockedComponent.maxX, blockedComponent.maxY, blockedComponent.cells});
            targetCandidates = std::move(fallback.cells);
            fallbackDistanceQueries_ += fallback.distanceQueries;
            fallbackComponentCellChecks_ += fallback.componentCellChecks;

            constexpr std::size_t MaxCachedFallbackTargets = 128U;
            if (fallbackTargetCache_.size() >= MaxCachedFallbackTargets) {
                fallbackTargetCache_.erase(fallbackTargetCache_.begin());
            }
            fallbackTargetCache_.push_back({fallbackKey, targetCandidates});
            ++fallbackTargetCacheMisses_;
        }
    }

#ifdef CONCORD_PATH_GOAL_CANDIDATE_TEST_OBSERVER
    recordPathGoalCandidatesForTest(request.id, 0, targetCandidates);
#endif

    if (targetCandidates.empty()) {
        result.cacheMiss = true;
        ++cacheMisses_;
        result.status = PathResultStatus::RealNoPath;
        result.failureReason =
            goalNode == nullptr
                ? "Goal cell is outside the terrain graph and no fallback cell was found."
                : "Goal cell is blocked and no fallback cell was found.";
        rememberNoPathFailure(
            request.failureCacheKey,
            result,
            logicalTick_,
            request.noPathCooldownTicks
        );
        return result;
    }

    result.fallbackCandidateCount =
        static_cast<int>(targetCandidates.size());
    if (targetCandidates.size() > 1U
        && (request.goalSlotCount > 1 || request.equivalentApproachBurst)) {
        result.goalSlotIndex =
            request.goalSlotCount > 1
                ? request.goalSlotIndex
                    % static_cast<int>(targetCandidates.size())
                : goalSlotIndexForRequest(
                    request.collapseKey,
                    request.approachContextHash,
                    request.failureCacheKey,
                    targetCandidates.size()
                );
        ++goalSlotAssignments_;
        fallbackCandidatesConsidered_ +=
            static_cast<int>(targetCandidates.size());
        std::rotate(
            targetCandidates.begin(),
            targetCandidates.begin() + result.goalSlotIndex,
            targetCandidates.end()
        );
    }

#ifdef CONCORD_PATH_GOAL_CANDIDATE_TEST_OBSERVER
    recordPathGoalCandidatesForTest(request.id, 1, targetCandidates);
#endif

    struct OpenNode {
        int f = 0;
        int g = 0;
        PathCell cell;
        int terrainIndex = -1;
    };

    const auto compare = [](const OpenNode& left, const OpenNode& right) {
        if (left.f != right.f) {
            return left.f > right.f;
        }
        if (left.g != right.g) {
            return left.g < right.g;
        }
        return pathCellsLess(right.cell, left.cell);
    };

    constexpr PathCell directions[] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
        {1, 1},
        {-1, -1},
        {1, -1},
        {-1, 1}
    };

    const auto canTraverse = [&request](
        const TerrainNode& current,
        const TerrainNode& next
    ) {
        if (request.movementMode == PathMovementMode::Air) {
            return true;
        }
        if (!next.passable || next.cliff) {
            return false;
        }

        if (request.movementMode == PathMovementMode::Naval
            && !next.water
            && !next.bridge) {
            return false;
        }
        if (request.movementMode == PathMovementMode::Ground
            && next.water
            && !next.bridge
            && !next.shore) {
            return false;
        }

        if (request.movementMode != PathMovementMode::Amphibious
            && current.water != next.water
            && !(current.shore
                || next.shore
                || current.bridge
                || next.bridge)) {
            return false;
        }

        const int levelDelta = std::abs(next.level - current.level);
        if (levelDelta > 1) {
            return false;
        }
        if (levelDelta > 0
            && !(current.ramp
                || next.ramp
                || current.bridge
                || next.bridge)) {
            return false;
        }

        return true;
    };

    const auto canStepBetween = [&canTraverse, this](PathCell from, PathCell to) {
        if (sameCell(from, to)) {
            return true;
        }

        const int dx = to.x - from.x;
        const int dy = to.y - from.y;
        if (std::abs(dx) > 1 || std::abs(dy) > 1 || (dx == 0 && dy == 0)) {
            return false;
        }

        const TerrainNode* fromNode = terrainNodeAt(from);
        const TerrainNode* toNode = terrainNodeAt(to);
        if (fromNode == nullptr || toNode == nullptr
            || !canTraverse(*fromNode, *toNode)) {
            return false;
        }

        const bool diagonal = std::abs(dx) == 1 && std::abs(dy) == 1;
        if (!diagonal) {
            return true;
        }

        const TerrainNode* horizontalNode =
            terrainNodeAt(PathCell{from.x + dx, from.y});
        const TerrainNode* verticalNode =
            terrainNodeAt(PathCell{from.x, from.y + dy});
        return horizontalNode != nullptr
            && verticalNode != nullptr
            && canTraverse(*fromNode, *horizontalNode)
            && canTraverse(*fromNode, *verticalNode);
    };

    const auto buildLocalJoinPath = [&](
        PathCell joinStart,
        const std::vector<PathCell>& cachedPath
    ) {
        std::vector<PathCell> empty;
        if (cachedPath.empty()) {
            return empty;
        }

        constexpr int MaxJoinRadius = 12;
        constexpr int MaxJoinExpansions = 384;
        constexpr std::size_t MaxCachedPathJoinCells = 96U;

        std::unordered_map<long long, std::size_t> cachedIndexByKey;
        cachedIndexByKey.reserve(
            std::min<std::size_t>(cachedPath.size(), MaxCachedPathJoinCells)
        );
        const std::size_t cachedLimit =
            std::min<std::size_t>(cachedPath.size(), MaxCachedPathJoinCells);
        for (std::size_t index = 0; index < cachedLimit; ++index) {
            cachedIndexByKey.emplace(cellKey(cachedPath[index]), index);
        }

        std::vector<PathCell> frontier;
        frontier.reserve(MaxJoinExpansions);
        frontier.push_back(joinStart);
        std::unordered_map<long long, long long> cameFrom;
        cameFrom.reserve(MaxJoinExpansions);
        std::unordered_set<long long> visited;
        visited.reserve(MaxJoinExpansions);
        visited.insert(cellKey(joinStart));

        std::size_t readIndex = 0;
        while (readIndex < frontier.size()
            && frontier.size() <= static_cast<std::size_t>(MaxJoinExpansions)) {
            const PathCell current = frontier[readIndex++];
            const auto joinIt = cachedIndexByKey.find(cellKey(current));
            if (joinIt != cachedIndexByKey.end()) {
                std::vector<PathCell> prefix;
                long long currentKey = cellKey(current);
                while (true) {
                    const int terrainIndex = terrainIndexForKey(currentKey);
                    if (terrainIndex < 0
                        || static_cast<std::size_t>(terrainIndex)
                            >= terrain_.size()) {
                        prefix.clear();
                        break;
                    }
                    prefix.push_back(terrain_[terrainIndex].cell);
                    const auto cameIt = cameFrom.find(currentKey);
                    if (cameIt == cameFrom.end()) {
                        break;
                    }
                    currentKey = cameIt->second;
                }
                if (prefix.empty()) {
                    return empty;
                }
                std::reverse(prefix.begin(), prefix.end());

                std::vector<PathCell> joined;
                joined.reserve(prefix.size() + cachedPath.size() - joinIt->second);
                joined.insert(joined.end(), prefix.begin(), prefix.end());
                joined.insert(
                    joined.end(),
                    cachedPath.begin() + static_cast<std::ptrdiff_t>(joinIt->second + 1U),
                    cachedPath.end()
                );
                return joined;
            }

            for (PathCell direction : directions) {
                PathCell next{current.x + direction.x, current.y + direction.y};
                if (octile(joinStart, next) > MaxJoinRadius
                    || !canStepBetween(current, next)) {
                    continue;
                }
                const long long nextKey = cellKey(next);
                if (visited.find(nextKey) != visited.end()) {
                    continue;
                }
                visited.insert(nextKey);
                cameFrom.emplace(nextKey, cellKey(current));
                frontier.push_back(next);
                if (frontier.size() >= static_cast<std::size_t>(MaxJoinExpansions)) {
                    break;
                }
            }
        }

        return empty;
    };

    const bool sharedApproachEligible = true;
    if (sharedApproachEligible) {
        for (const auto& [sharedKey, cachedResult] : sharedApproachCache_) {
            if (sharedKey != request.sharedApproachKey) {
                continue;
            }

            if (!cachedResult.reachable || cachedResult.path.empty()) {
                continue;
            }

            std::vector<PathCell> sharedPath;
            if (sameCell(request.start, cachedResult.resolvedGoal)) {
                sharedPath.push_back(request.start);
            } else if (sameCell(request.start, cachedResult.path.front())) {
                sharedPath = cachedResult.path;
            } else if (canStepBetween(request.start, cachedResult.path.front())) {
                sharedPath.reserve(cachedResult.path.size() + 1U);
                sharedPath.push_back(request.start);
                sharedPath.insert(
                    sharedPath.end(),
                    cachedResult.path.begin(),
                    cachedResult.path.end()
                );
            } else if (cachedResult.path.size() > 1U
                && canStepBetween(request.start, cachedResult.path[1])) {
                sharedPath.reserve(cachedResult.path.size());
                sharedPath.push_back(request.start);
                sharedPath.insert(
                    sharedPath.end(),
                    cachedResult.path.begin() + 1,
                    cachedResult.path.end()
                );
            } else {
                sharedPath =
                    buildLocalJoinPath(request.start, cachedResult.path);
            }

            if (sharedPath.empty()) {
                continue;
            }

            result = cachedResult;
            result.requestId = request.id;
            result.start = request.start;
            result.goal = request.goal;
            result.goalAdjusted = !sameCell(result.resolvedGoal, request.goal);
            result.cacheHit = true;
            result.cacheMiss = false;
            result.sharedApproachCacheHit = true;
            result.status = PathResultStatus::Reached;
            result.passabilityProfileHash = profileHash;
            result.requestClass = requestClass;
            result.expandedNodes = 0;
            result.expandedNodeBudget = requestBudget;
            result.fallbackTargetsTried = 0;
            result.searchBudgetExhausted = false;
            result.queueBackpressure = false;
            result.path = std::move(sharedPath);
            ++sharedApproachCacheHits_;
            ++cacheHits_;
            if (result.mapQueryApproachUsed) {
                ++mapQueryApproachUsedResults_;
            }
            return result;
        }

        ++sharedApproachCacheMisses_;
    }

    result.cacheMiss = true;
    ++cacheMisses_;

    if (request.movementMode == PathMovementMode::Air) {
        // Aircraft do not need A* over the ground graph.  Build the shortest
        // 8-connected raster line directly, which also permits flight across
        // holes/cliffs that have no traversable ground node.
        const PathCell target = targetCandidates.front();
        std::vector<PathCell> directPath;
        directPath.reserve(static_cast<std::size_t>(
            std::max(
                std::abs(target.x - request.start.x),
                std::abs(target.y - request.start.y)
            ) + 1
        ));
        int x = request.start.x;
        int y = request.start.y;
        const int dx = std::abs(target.x - x);
        const int dy = std::abs(target.y - y);
        const int stepX = x < target.x ? 1 : -1;
        const int stepY = y < target.y ? 1 : -1;
        int error = dx - dy;
        directPath.push_back({x, y});
        while (x != target.x || y != target.y) {
            const int twiceError = error * 2;
            if (twiceError > -dy) {
                error -= dy;
                x += stepX;
            }
            if (twiceError < dx) {
                error += dx;
                y += stepY;
            }
            directPath.push_back({x, y});
        }

        result.reachable = true;
        result.status = PathResultStatus::Reached;
        result.resolvedGoal = target;
        result.goalAdjusted = !sameCell(target, request.goal);
        result.fallbackTargetsTried = 1;
        result.expandedNodes = static_cast<int>(directPath.size());
        result.path = std::move(directPath);
        return result;
    }

    const auto tryGreedyPath = [&](PathCell searchGoal) {
        std::vector<PathCell> path;
        const int directDistance = octile(request.start, searchGoal) / 10;
        if (directDistance > 10) {
            return path;
        }
        path.push_back(request.start);
        PathCell current = request.start;
        const int maxSteps = std::max(8, (octile(request.start, searchGoal) / 10) + 8);
        for (int step = 0; step < maxSteps && !sameCell(current, searchGoal); ++step) {
            PathCell best = current;
            const TerrainNode* currentNode = terrainNodeAt(current);
            int bestScore = octile(current, searchGoal)
                + (currentNode == nullptr
                    ? 0
                    : std::min(60, std::max(0, currentNode->cost - 10))
                        + std::min(240, currentNode->hazardCost));
            for (PathCell direction : directions) {
                PathCell next;
                next.x = current.x + direction.x;
                next.y = current.y + direction.y;
                if (!canStepBetween(current, next)) {
                    continue;
                }

                const TerrainNode* nextNode = terrainNodeAt(next);
                const int terrainPenalty =
                    nextNode == nullptr
                        ? 0
                        : std::min(60, std::max(0, nextNode->cost - 10))
                            + std::min(240, nextNode->hazardCost);
                const int score = octile(next, searchGoal) + terrainPenalty;
                if (score < bestScore
                    || (score == bestScore && pathCellsLess(next, best))) {
                    best = next;
                    bestScore = score;
                }
            }

            if (sameCell(best, current)) {
                path.clear();
                return path;
            }
            current = best;
            path.push_back(current);
        }

        if (!sameCell(current, searchGoal)) {
            path.clear();
        }
        return path;
    };

    const auto runAStar = [&](int remainingBudget) {
        RuntimePerfTraceScope searchTrace("pathQueue.search", -1);
        PathResult attempt;
        attempt.requestId = request.id;
        attempt.start = request.start;
        attempt.goal = request.goal;
        attempt.resolvedGoal = targetCandidates.front();
        attempt.completed = true;
        attempt.cacheMiss = true;
        attempt.status = PathResultStatus::RealNoPath;
        attempt.fallbackRing = result.fallbackRing;
        attempt.mapQueryApproachUsed = result.mapQueryApproachUsed;
        attempt.passabilityProfileHash = result.passabilityProfileHash;
        attempt.requestClass = result.requestClass;
        attempt.goalAdjusted =
            !sameCell(targetCandidates.front(), request.goal);
        attempt.fallbackCandidateCount = result.fallbackCandidateCount;
        attempt.goalSlotIndex = result.goalSlotIndex;
        attempt.expandedNodeBudget = requestBudget;

        if (searchNodes_.size() != terrain_.size()) {
            searchNodes_.resize(terrain_.size());
        }
        if (++searchGeneration_ == 0) {
            std::fill(searchNodes_.begin(), searchNodes_.end(), SearchNode{});
            searchGeneration_ = 1;
        }
        for (PathCell target : targetCandidates) {
            const int index = terrainIndexForKey(cellKey(target));
            if (index >= 0) {
                searchNodes_[index].goalGeneration = searchGeneration_;
            }
        }
        const auto nearestGoalScore = [this, &targetCandidates](PathCell cell) {
            ++heuristicEvaluations_;
            int best = std::numeric_limits<int>::max();
            for (PathCell target : targetCandidates) {
                best = std::min(best, octile(cell, target));
            }
            return best;
        };

        std::priority_queue<
            OpenNode,
            std::vector<OpenNode>,
            decltype(compare)
        > open(compare);

        const int startIndex = static_cast<int>(startNode - terrain_.data());
        int bestPartialIndex = startIndex;
        int bestPartialScore = nearestGoalScore(request.start);
        int bestPartialCost = 0;

        SearchNode& startSearch = searchNodes_[startIndex];
        startSearch.generation = searchGeneration_;
        startSearch.g = 0;
        startSearch.h = bestPartialScore;
        startSearch.parent = -1;
        open.push(OpenNode{bestPartialScore, 0, request.start, startIndex});

        while (!open.empty()) {
            OpenNode current = open.top();
            open.pop();

            SearchNode& currentSearch = searchNodes_[current.terrainIndex];
            if (currentSearch.closedGeneration == searchGeneration_) {
                continue;
            }
            currentSearch.closedGeneration = searchGeneration_;
            ++attempt.expandedNodes;

            const int currentGoalScore = currentSearch.h;
            if (current.terrainIndex != startIndex
                && (currentGoalScore < bestPartialScore
                    || (currentGoalScore == bestPartialScore
                        && current.g > bestPartialCost))) {
                bestPartialIndex = current.terrainIndex;
                bestPartialScore = currentGoalScore;
                bestPartialCost = current.g;
            }

            if (currentSearch.goalGeneration == searchGeneration_) {
                attempt.reachable = true;
                attempt.status = PathResultStatus::Reached;
                attempt.resolvedGoal = current.cell;
                attempt.goalAdjusted = !sameCell(current.cell, request.goal);
                attempt.path = reconstructPath(current.terrainIndex);
                return attempt;
            }
            if (attempt.expandedNodes >= remainingBudget) {
                attempt.searchBudgetExhausted = true;
                attempt.status = PathResultStatus::BudgetExhausted;
                attempt.failureReason =
                    "Path search expanded-node budget was exhausted.";
                if (bestPartialIndex != startIndex) {
                    std::vector<PathCell> partialPath =
                        reconstructPath(bestPartialIndex);
                    if (partialPath.size() > 1U) {
                        attempt.reachable = true;
                        attempt.goalAdjusted = false;
                        attempt.resolvedGoal = request.goal;
                        attempt.path = std::move(partialPath);
                    }
                }
                return attempt;
            }

            const TerrainNode* currentNode = &terrain_[current.terrainIndex];

            // Cardinals are visited first. Their traversability is also the
            // corner-cut constraint of both adjacent diagonals, including
            // cardinals already closed by this search. Reuse that pure terrain
            // check instead of looking up and testing each cardinal three times.
            std::array<bool, 4> cardinalTraversable{};
            for (int directionIndex = 0; directionIndex < 8; ++directionIndex) {
                const PathCell direction = directions[directionIndex];
                const bool diagonal = directionIndex >= 4;
                if (diagonal) {
                    const int horizontalIndex = direction.x > 0 ? 0 : 1;
                    const int verticalIndex = direction.y > 0 ? 2 : 3;
                    if (!cardinalTraversable[horizontalIndex]
                        || !cardinalTraversable[verticalIndex]) {
                        continue;
                    }
                }
                PathCell next;
                next.x = current.cell.x + direction.x;
                next.y = current.cell.y + direction.y;
                const long long nextKey = cellKey(next);

                const int nextIndex = terrainIndexForKey(nextKey);
                if (nextIndex < 0) {
                    continue;
                }
                SearchNode& nextSearch = searchNodes_[nextIndex];
                const TerrainNode* nextNode = &terrain_[nextIndex];
                if (!diagonal) {
                    cardinalTraversable[directionIndex] =
                        canTraverse(*currentNode, *nextNode);
                }
                if (nextSearch.closedGeneration == searchGeneration_) {
                    continue;
                }
                if (!(diagonal ? canTraverse(*currentNode, *nextNode)
                               : cardinalTraversable[directionIndex])) {
                    continue;
                }

                const int terrainPenalty =
                    std::min(60, std::max(0, nextNode->cost - 10))
                    + std::min(240, nextNode->hazardCost);
                const int stepCost = (diagonal ? 14 : 10) + terrainPenalty;
                const int tentativeG = current.g + stepCost;
                if (nextSearch.generation == searchGeneration_
                    && tentativeG >= nextSearch.g) {
                    continue;
                }

                // The goals are fixed during an A* attempt. Its generation
                // already identifies which nodes have a valid cached score.
                if (nextSearch.generation != searchGeneration_) {
                    nextSearch.h = nearestGoalScore(next);
                }
                nextSearch.generation = searchGeneration_;
                nextSearch.parent = current.terrainIndex;
                nextSearch.g = tentativeG;
                const int f = tentativeG + nextSearch.h;
                open.push(OpenNode{f, tentativeG, next, nextIndex});
            }
        }

        attempt.failureReason =
            "No route reached any requested approach cell.";
        return attempt;
    };

    for (PathCell target : targetCandidates) {
        std::vector<PathCell> greedyPath = tryGreedyPath(target);
        if (!greedyPath.empty()) {
            PathResult attempt;
            attempt.requestId = request.id;
            attempt.start = request.start;
            attempt.goal = request.goal;
            attempt.resolvedGoal = target;
            attempt.completed = true;
            attempt.reachable = true;
            attempt.cacheMiss = true;
            attempt.status = PathResultStatus::Reached;
            attempt.fallbackRing = result.fallbackRing;
            attempt.mapQueryApproachUsed = result.mapQueryApproachUsed;
            attempt.passabilityProfileHash = result.passabilityProfileHash;
            attempt.requestClass = result.requestClass;
            attempt.goalAdjusted = !sameCell(target, request.goal);
            attempt.fallbackCandidateCount = result.fallbackCandidateCount;
            attempt.goalSlotIndex = result.goalSlotIndex;
            attempt.expandedNodes = static_cast<int>(greedyPath.size());
            attempt.expandedNodeBudget = requestBudget;
            attempt.fallbackTargetsTried = 1;
            attempt.path = std::move(greedyPath);
            return attempt;
        }
    }

    result = runAStar(requestBudget);
    result.fallbackTargetsTried =
        static_cast<int>(targetCandidates.size());
    rememberNoPathFailure(
        request.failureCacheKey,
        result,
        logicalTick_,
        request.noPathCooldownTicks
    );
    return result;
}

