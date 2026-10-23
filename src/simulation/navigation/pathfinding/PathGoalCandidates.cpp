#include "simulation/navigation/pathfinding/PathGoalCandidates.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
using TerrainNode = PathTerrainCell;
int octile(PathCell left, PathCell right) {
    const int dx = std::abs(left.x - right.x);
    const int dy = std::abs(left.y - right.y);
    const int diagonal = std::min(dx, dy);
    const int straight = std::max(dx, dy) - diagonal;
    return (diagonal * 14) + (straight * 10);
}

bool sameCell(PathCell left, PathCell right) {
    return left.x == right.x && left.y == right.y;
}

bool pathCellsLess(PathCell left, PathCell right) {
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.x < right.x;
}

} // namespace

PathGoalCandidateSet generatePathGoalCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain) {
    PathGoalCandidateSet generated;
    const TerrainNode* goalNode = terrain.nodeAt(request.goal);
    const auto targetUsable = [&request](const TerrainNode& node) {
        if (request.movementMode == PathMovementMode::Air) {
            return true;
        }
        if (!node.passable || node.cliff) {
            return false;
        }
        if (request.movementMode == PathMovementMode::Naval) {
            return node.water || node.bridge;
        }
        if (request.movementMode == PathMovementMode::Ground) {
            return !node.water || node.bridge || node.shore;
        }
        return true;
    };

    std::vector<PathCell> targetCandidates;
    if (request.mapQueryApproach) {
        targetCandidates.reserve(request.approachCells.size());
        for (PathCell candidate : request.approachCells) {
            const TerrainNode* candidateNode = terrain.nodeAt(candidate);
            if (candidateNode != nullptr
                && targetUsable(*candidateNode)
                && !sameCell(candidate, request.start)) {
                targetCandidates.push_back(candidate);
            }
        }
        std::sort(
            targetCandidates.begin(),
            targetCandidates.end(),
            [&request](PathCell left, PathCell right) {
                const int leftStartScore = octile(left, request.start);
                const int rightStartScore = octile(right, request.start);
                if (leftStartScore != rightStartScore) {
                    return leftStartScore < rightStartScore;
                }
                const int leftGoalScore = octile(left, request.goal);
                const int rightGoalScore = octile(right, request.goal);
                if (leftGoalScore != rightGoalScore) {
                    return leftGoalScore < rightGoalScore;
                }
                return pathCellsLess(left, right);
            }
        );
    } else if (!request.alternateGoalCells.empty()) {
        targetCandidates.reserve(request.alternateGoalCells.size());
        for (PathCell candidate : request.alternateGoalCells) {
            const TerrainNode* candidateNode = terrain.nodeAt(candidate);
            if (candidateNode != nullptr
                && targetUsable(*candidateNode)
                && !sameCell(candidate, request.start)) {
                targetCandidates.push_back(candidate);
            }
        }
        std::sort(
            targetCandidates.begin(),
            targetCandidates.end(),
            [&request](PathCell left, PathCell right) {
                const int leftGoalScore = octile(left, request.goal);
                const int rightGoalScore = octile(right, request.goal);
                if (leftGoalScore != rightGoalScore) {
                    return leftGoalScore < rightGoalScore;
                }
                const int leftStartScore = octile(left, request.start);
                const int rightStartScore = octile(right, request.start);
                if (leftStartScore != rightStartScore) {
                    return leftStartScore < rightStartScore;
                }
                return pathCellsLess(left, right);
            }
        );
    }

    if (!targetCandidates.empty()) {
        // Runtime/MapQuery supplied approach cells already encode the footprint/ring.
        if (request.mapQueryApproach) {
            generated.mapQueryApproachUsed = true;
            
        }
    } else if (goalNode != nullptr && targetUsable(*goalNode)) {
        targetCandidates.push_back(request.goal);
        if (request.goalSlotCount > 1) {
            constexpr int AlternateGoalRadius = 2;
            constexpr std::size_t MaxAlternateGoalTargets = 16U;
            for (const int index : terrain.indicesInBounds(
                    static_cast<std::int64_t>(request.goal.x) - AlternateGoalRadius,
                    static_cast<std::int64_t>(request.goal.y) - AlternateGoalRadius,
                    static_cast<std::int64_t>(request.goal.x) + AlternateGoalRadius,
                    static_cast<std::int64_t>(request.goal.y) + AlternateGoalRadius)) {
                const TerrainNode& node = terrain.nodeAtIndex(index);
                if (targetCandidates.size() >= MaxAlternateGoalTargets) {
                    break;
                }
                if (!targetUsable(node)
                    || sameCell(node.cell, request.start)
                    || sameCell(node.cell, request.goal)) {
                    continue;
                }
                const int distance = std::max(
                    std::abs(node.cell.x - request.goal.x),
                    std::abs(node.cell.y - request.goal.y)
                );
                if (distance <= 0 || distance > AlternateGoalRadius) {
                    continue;
                }
                targetCandidates.push_back(node.cell);
            }
            std::sort(
                targetCandidates.begin() + 1,
                targetCandidates.end(),
                [&request](PathCell left, PathCell right) {
                    const int leftGoalScore = octile(left, request.goal);
                    const int rightGoalScore = octile(right, request.goal);
                    if (leftGoalScore != rightGoalScore) {
                        return leftGoalScore < rightGoalScore;
                    }
                    const int leftStartScore = octile(left, request.start);
                    const int rightStartScore = octile(right, request.start);
                    if (leftStartScore != rightStartScore) {
                        return leftStartScore < rightStartScore;
                    }
                    return pathCellsLess(left, right);
                }
            );
        }
    } else {
        generated.requiresFallbackRing = true;
    }
    generated.cells = std::move(targetCandidates);
    return generated;
}

PathGoalFallbackCandidates generatePathGoalFallbackCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain,
    const PathBlockedGoalView& blockedComponent) {
    PathGoalFallbackCandidates generated;
    const auto targetUsable = [&request](const TerrainNode& node) {
        if (request.movementMode == PathMovementMode::Air) {
            return true;
        }
        if (!node.passable || node.cliff) {
            return false;
        }
        if (request.movementMode == PathMovementMode::Naval) {
            return node.water || node.bridge;
        }
        if (request.movementMode == PathMovementMode::Ground) {
            return !node.water || node.bridge || node.shore;
        }
        return true;
    };

    const auto distanceToBlockedComponent = [&generated, &blockedComponent](PathCell cell) {
        ++generated.distanceQueries;
        // Filled rectangular foundations have exactly this Chebyshev distance.
        // Keep the point-set calculation for walls, courtyards and irregular
        // footprints: their bounding rectangle can include traversable cells.
        if (blockedComponent.fillsBounds) {
            const int dx = std::max({0,
                blockedComponent.minX - cell.x, cell.x - blockedComponent.maxX});
            const int dy = std::max({0,
                blockedComponent.minY - cell.y, cell.y - blockedComponent.maxY});
            return std::max(dx, dy);
        }
        generated.componentCellChecks += blockedComponent.cells.size();
        int best = std::numeric_limits<int>::max();
        for (PathCell blocked : blockedComponent.cells) {
            const int distance = std::max(
                std::abs(cell.x - blocked.x),
                std::abs(cell.y - blocked.y)
            );
            best = std::min(best, distance);
        }
        return best;
    };

    std::vector<PathCell> targetCandidates;
            constexpr int FallbackRadius = 8;
            constexpr std::size_t MaxFallbackTargets = 32U;
            struct ScoredCandidate {
                PathCell cell;
                int componentDistance = 0;
                int goalScore = 0;
                int startScore = 0;
            };
            std::vector<ScoredCandidate> scoredCandidates;
            scoredCandidates.reserve(MaxFallbackTargets);
            const std::int64_t minX = blockedComponent.valid
                ? blockedComponent.minX : request.goal.x;
            const std::int64_t minY = blockedComponent.valid
                ? blockedComponent.minY : request.goal.y;
            const std::int64_t maxX = blockedComponent.valid
                ? blockedComponent.maxX : request.goal.x;
            const std::int64_t maxY = blockedComponent.valid
                ? blockedComponent.maxY : request.goal.y;
            for (const int index : terrain.indicesInBounds(
                    minX - FallbackRadius, minY - FallbackRadius,
                    maxX + FallbackRadius, maxY + FallbackRadius)) {
                const TerrainNode& node = terrain.nodeAtIndex(index);
                if (!targetUsable(node) || sameCell(node.cell, request.start)) {
                    continue;
                }

                int componentDistance = 0;
                if (blockedComponent.valid) {
                    componentDistance = distanceToBlockedComponent(node.cell);
                    if (componentDistance <= 0
                        || componentDistance > FallbackRadius) {
                        continue;
                    }
                } else {
                    const int dx = std::abs(node.cell.x - request.goal.x);
                    const int dy = std::abs(node.cell.y - request.goal.y);
                    if (std::max(dx, dy) > FallbackRadius) {
                        continue;
                    }
                }
                // Compute expensive footprint distances once per candidate,
                // instead of rescanning the footprint on every heap comparison.
                scoredCandidates.push_back({node.cell, componentDistance,
                    octile(node.cell, request.goal), octile(node.cell, request.start)});
            }

            std::partial_sort(
                scoredCandidates.begin(),
                scoredCandidates.begin() + std::min(
                    scoredCandidates.size(), MaxFallbackTargets),
                scoredCandidates.end(),
                [&blockedComponent](
                    const ScoredCandidate& left,
                    const ScoredCandidate& right
                ) {
                    if (blockedComponent.valid) {
                        if (left.componentDistance != right.componentDistance) {
                            return left.componentDistance < right.componentDistance;
                        }

                        if (left.startScore != right.startScore) {
                            return left.startScore < right.startScore;
                        }
                    }

                    if (left.goalScore != right.goalScore) {
                        return left.goalScore < right.goalScore;
                    }

                    if (left.startScore != right.startScore) {
                        return left.startScore < right.startScore;
                    }
                    return pathCellsLess(left.cell, right.cell);
                }
            );

            const std::size_t targetCount = std::min(
                scoredCandidates.size(), MaxFallbackTargets);
            targetCandidates.reserve(targetCount);
            for (std::size_t index = 0; index < targetCount; ++index) {
                targetCandidates.push_back(scoredCandidates[index].cell);
            }

    generated.cells = std::move(targetCandidates);
    return generated;
}
