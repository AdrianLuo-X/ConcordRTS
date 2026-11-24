#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/detail/PathRequestQueueFingerprint.h"

#include <algorithm>
#include <utility>

namespace {
using concord::simulation::pathfinding::detail::FnvOffsetBasis;
using concord::simulation::pathfinding::detail::hashCell;
using concord::simulation::pathfinding::detail::hashInt;
} // namespace

std::uint64_t PathRequestQueue::failureCacheKeyForRequest(
    PathCell start,
    PathCell goal,
    std::uint64_t approachContextHash,
    std::uint64_t requesterStableHash
) const {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 7);
    hashInt(hash, static_cast<std::int64_t>(passabilityProfileHash_));
    hashInt(hash, static_cast<std::int64_t>(approachContextHash));
    hashInt(hash, static_cast<std::int64_t>(requesterStableHash));
    if (requesterStableHash == 0) {
        hashCell(hash, start);
    }

    const BlockedComponent& component = blockedComponentAt(goal);
    if (!component.valid) {
        hashInt(hash, 0);
        hashCell(hash, goal);
        return hash;
    }

    hashInt(hash, 1);
    hashInt(hash, component.minX);
    hashInt(hash, component.minY);
    hashInt(hash, component.maxX);
    hashInt(hash, component.maxY);
    hashInt(hash, static_cast<std::int64_t>(component.cells.size()));
    for (PathCell cell : component.cells) {
        hashCell(hash, cell);
    }
    return hash;
}

const PathRequestQueue::NoPathFailureCacheEntry*
PathRequestQueue::findNoPathFailureCacheEntry(
    std::uint64_t key,
    int currentTick
) {
    for (auto it = noPathFailureCache_.begin();
        it != noPathFailureCache_.end();
        ++it) {
        if (it->key != key) {
            continue;
        }
        if (currentTick > it->expiresAtTick) {
            noPathFailureCache_.erase(it);
            return nullptr;
        }
        return &*it;
    }
    return nullptr;
}

void PathRequestQueue::rememberNoPathFailure(
    std::uint64_t key,
    const PathResult& result,
    int currentTick,
    int cooldownTicks
) {
    if (key == 0 || result.reachable || result.searchBudgetExhausted) {
        return;
    }

    NoPathFailureCacheEntry entry;
    entry.key = key;
    entry.resolvedGoal = result.resolvedGoal;
    entry.goalAdjusted = result.goalAdjusted;
    entry.fallbackRing = result.fallbackRing;
    entry.mapQueryApproachUsed = result.mapQueryApproachUsed;
    entry.fallbackTargetsTried = result.fallbackTargetsTried;
    entry.fallbackCandidateCount = result.fallbackCandidateCount;
    entry.goalSlotIndex = result.goalSlotIndex;
    entry.expiresAtTick = currentTick + std::max(1, cooldownTicks);
    entry.failureReason = result.failureReason.empty()
        ? "No route reached the requested goal cell."
        : result.failureReason;

    for (NoPathFailureCacheEntry& cached : noPathFailureCache_) {
        if (cached.key == key) {
            cached = entry;
            return;
        }
    }

    constexpr std::size_t MaxNoPathFailureCacheEntries = 128U;
    if (noPathFailureCache_.size() >= MaxNoPathFailureCacheEntries) {
        noPathFailureCache_.erase(noPathFailureCache_.begin());
    }
    noPathFailureCache_.push_back(std::move(entry));
}

