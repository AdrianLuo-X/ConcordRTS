#pragma once

#include "simulation/navigation/pathfinding/PathGoalTypes.h"

#include <cstdint>
#include <span>
#include <vector>

// A synchronous, read-only view of the queue's immutable terrain/occupancy
// snapshot. Indices retain authored order (the first 16 nearby goals depend on
// it). The view and returned node pointers are never retained by the generator.
class PathGoalTerrainView {
public:
    virtual ~PathGoalTerrainView() = default;
    virtual const PathTerrainCell* nodeAt(PathCell cell) const = 0;
    virtual const PathTerrainCell& nodeAtIndex(int index) const = 0;
    virtual std::vector<int> indicesInBounds(std::int64_t minX, std::int64_t minY,
        std::int64_t maxX, std::int64_t maxY) const = 0;
};

struct PathGoalCandidateInput {
    PathCell start;
    PathCell goal;
    PathMovementMode movementMode = PathMovementMode::Ground;
    int goalSlotCount = 0;
    bool mapQueryApproach = false;
    // Normalized/deduplicated/capped by submit, exactly as before extraction.
    // These borrowed spans live only for the synchronous generation call.
    std::span<const PathCell> approachCells;
    std::span<const PathCell> alternateGoalCells;
};

struct PathBlockedGoalView {
    bool valid = false;
    bool fillsBounds = false;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    std::span<const PathCell> cells;
};

struct PathGoalCandidateSet {
    std::vector<PathCell> cells;
    bool mapQueryApproachUsed = false;
    bool requiresFallbackRing = false;
};

struct PathGoalFallbackCandidates {
    std::vector<PathCell> cells;
    std::uint64_t distanceQueries = 0;
    std::uint64_t componentCellChecks = 0;
};

// No search, cache, queue mutation, slot rotation or budget consumption occurs
// here. Results own their cells; neither function retains any input/view.
PathGoalCandidateSet generatePathGoalCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain);
PathGoalFallbackCandidates generatePathGoalFallbackCandidates(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain,
    const PathBlockedGoalView& blockedComponent);
