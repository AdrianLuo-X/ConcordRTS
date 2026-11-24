#pragma once

#include "simulation/navigation/pathfinding/PathGoalTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PathApproachContext;

namespace concord::simulation::pathfinding::detail {

inline constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ull;
void hashInt(std::uint64_t& hash, std::int64_t value);
void hashCell(std::uint64_t& hash, PathCell cell);

std::string formatHash(std::uint64_t value);
std::uint64_t approachContextHash(
    const PathApproachContext& context,
    const std::vector<PathCell>& normalizedApproach,
    const std::vector<PathCell>& normalizedAlternateGoals
);
int goalSlotIndexForRequest(
    std::uint64_t collapseKey,
    std::uint64_t approachContextHash,
    std::uint64_t failureCacheKey,
    std::size_t candidateCount
);

} // namespace concord::simulation::pathfinding::detail
