#include "simulation/navigation/pathfinding/PathGoalCandidates.h"
#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/detail/PathfindingCellHelpers.h"
#include "simulation/navigation/pathfinding/detail/PathRequestQueueFingerprint.h"
#include "simulation/navigation/pathfinding/RuntimePathProfileHash.h"
#include "simulation/navigation/pathfinding/RuntimePathProfileZeroRuns.h"
#include "simulation/navigation/pathfinding/RuntimePathProfileFixedSuffix.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include "simulation/navigation/pathfinding/RuntimePathQueueWarmAdmission.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int DefaultExpandedNodesPerPump = 32768;
constexpr std::uint64_t FnvPrime = 1099511628211ull;
constexpr int NoPathFailureCooldownTicks = 64;

using concord::simulation::pathfinding::detail::FnvOffsetBasis;
using concord::simulation::pathfinding::detail::hashCell;
using concord::simulation::pathfinding::detail::hashInt;
using concord::simulation::pathfinding::detail::cellKey;
using concord::simulation::pathfinding::detail::sameCell;
using concord::simulation::pathfinding::detail::approachContextHash;
using concord::simulation::pathfinding::detail::formatHash;
using concord::simulation::pathfinding::detail::pathCellsLess;

void hashByte(std::uint64_t& hash, unsigned char value) {
    hash ^= value;
    hash *= FnvPrime;
}

void hashStartBucket(std::uint64_t& hash, PathCell start) {
    hashInt(hash, start.x / 2);
    hashInt(hash, start.y / 2);
}

std::vector<PathCell> normalizedCells(
    std::vector<PathCell> cells,
    std::size_t maxCells
) {
    std::sort(cells.begin(), cells.end(), pathCellsLess);
    cells.erase(
        std::unique(
            cells.begin(),
            cells.end(),
            [](PathCell left, PathCell right) {
                return sameCell(left, right);
            }
        ),
        cells.end()
    );
    if (cells.size() > maxCells) {
        cells.resize(maxCells);
    }
    return cells;
}

std::vector<PathCell> normalizedApproachCells(
    const PathApproachContext& context
) {
    constexpr std::size_t MaxApproachCells = 64U;
    return normalizedCells(context.approachCells, MaxApproachCells);
}

std::vector<PathCell> normalizedAlternateGoalCells(
    const PathApproachContext& context
) {
    constexpr std::size_t MaxAlternateGoalCells = 64U;
    return normalizedCells(context.alternateGoalCells, MaxAlternateGoalCells);
}

int requestedGoalSlotIndex(
    PathCell start,
    PathCell goal,
    std::uint64_t requesterStableHash,
    int goalSlotCount
) {
    if (goalSlotCount <= 1) {
        return 0;
    }

    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 10);
    if (requesterStableHash != 0) {
        hashInt(hash, static_cast<std::int64_t>(requesterStableHash));
    } else {
        hashCell(hash, start);
    }
    hashCell(hash, goal);
    return static_cast<int>(hash % static_cast<std::uint64_t>(goalSlotCount));
}

} // namespace

namespace concord::simulation::pathfinding::detail {

void hashInt(std::uint64_t& hash, std::int64_t value) {
    const std::uint64_t unsignedValue = static_cast<std::uint64_t>(value);
    for (int index = 0; index < 8; ++index) {
        hashByte(hash, static_cast<unsigned char>((unsignedValue >> (index * 8)) & 0xffu));
    }
}

void hashCell(std::uint64_t& hash, PathCell cell) {
    hashInt(hash, cell.x);
    hashInt(hash, cell.y);
}

std::string formatHash(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

std::uint64_t approachContextHash(
    const PathApproachContext& context,
    const std::vector<PathCell>& normalizedApproach,
    const std::vector<PathCell>& normalizedAlternateGoals
) {
    if (!context.mapQueryApproachAvailable
        && context.targetFootprintHash == 0
        && context.approachRingHash == 0
        && context.movementConstraintHash == 0
        && context.goalSlotCount <= 0
        && context.movementMode == PathMovementMode::Ground
        && normalizedApproach.empty()
        && normalizedAlternateGoals.empty()) {
        return 0;
    }

    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 9);
    hashInt(hash, context.mapQueryApproachAvailable ? 1 : 0);
    hashInt(hash, static_cast<std::int64_t>(context.targetFootprintHash));
    hashInt(hash, static_cast<std::int64_t>(context.approachRingHash));
    hashInt(hash, static_cast<std::int64_t>(context.movementConstraintHash));
    hashInt(hash, static_cast<int>(context.movementMode));
    hashInt(hash, std::max(0, context.goalSlotCount));
    hashInt(hash, static_cast<std::int64_t>(normalizedApproach.size()));
    for (PathCell cell : normalizedApproach) {
        hashCell(hash, cell);
    }
    hashInt(hash, static_cast<std::int64_t>(normalizedAlternateGoals.size()));
    for (PathCell cell : normalizedAlternateGoals) {
        hashCell(hash, cell);
    }
    return hash;
}

int goalSlotIndexForRequest(
    std::uint64_t collapseKey,
    std::uint64_t approachContextHash,
    std::uint64_t failureCacheKey,
    std::size_t candidateCount
) {
    if (candidateCount <= 1U) {
        return 0;
    }

    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 8);
    hashInt(hash, static_cast<std::int64_t>(collapseKey));
    hashInt(hash, static_cast<std::int64_t>(approachContextHash));
    hashInt(hash, static_cast<std::int64_t>(failureCacheKey));
    return static_cast<int>(hash % candidateCount);
}

} // namespace

PathRequestQueue::PathRequestQueue(const std::vector<TerrainCell>& terrainCells) {
    std::vector<PathTerrainCell> convertedCells;
    convertedCells.reserve(terrainCells.size());
    for (const TerrainCell& source : terrainCells) {
        PathTerrainCell cell;
        cell.cell.x = source.x;
        cell.cell.y = source.y;
        cell.level = source.level;
        cell.passable = true;
        convertedCells.push_back(cell);
    }

    *this = PathRequestQueue(convertedCells);
}

PathRequestQueue::PathRequestQueue(
    const std::vector<PathTerrainCell>& terrainCells,
    bool forceReferenceLookup
) {
    initializeTerrain(terrainCells, forceReferenceLookup, nullptr);
}

void PathRequestQueueTerrainLayoutCache::clear() noexcept {
    PathRequestQueueTerrainLayoutCache empty;
    swap(empty);
}

void PathRequestQueueTerrainLayoutCache::swap(
    PathRequestQueueTerrainLayoutCache& other
) noexcept {
    static_assert(noexcept(sourceCoordinates_.swap(other.sourceCoordinates_)));
    static_assert(noexcept(sourceNodeIndices_.swap(other.sourceNodeIndices_)));
    static_assert(noexcept(layout_.swap(other.layout_)));
    sourceCoordinates_.swap(other.sourceCoordinates_);
    sourceNodeIndices_.swap(other.sourceNodeIndices_);
    layout_.swap(other.layout_);
    using std::swap;
    swap(valid_, other.valid_);
    swap(referenceLookup_, other.referenceLookup_);
    swap(lastConstructionReused_, other.lastConstructionReused_);

}

PathRequestQueue::PathRequestQueue(
    const std::vector<PathTerrainCell>& terrainCells,
    PathRequestQueueTerrainLayoutCache& layoutCache,
    bool forceReferenceLayout,
    bool forceReferenceLookup
) {
    static const bool referenceLayout = [] {
        const char* value = std::getenv("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    layoutCache.lastConstructionReused_ = false;
    const bool oversized = terrainCells.size() > PathRequestQueueTerrainLayoutCache::maxRetainedCells;
    if (oversized) layoutCache.clear();
    initializeTerrain(terrainCells, forceReferenceLookup,
        referenceLayout || forceReferenceLayout || oversized ? nullptr : &layoutCache);
}

PathRequestQueue::PathRequestQueue(
    RuntimeReadOnlyPathTerrainTag,
    const std::vector<PathTerrainCell>& terrainCells,
    PathRequestQueueTerrainLayoutCache& layoutCache,
    bool forceReferenceLayout,
    bool forceReferenceLookup
) {
    static const bool referenceLayout = [] {
        const char* value = std::getenv("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    layoutCache.lastConstructionReused_ = false;
    const bool oversized = terrainCells.size() > PathRequestQueueTerrainLayoutCache::maxRetainedCells;
    if (oversized) layoutCache.clear();
    initializeTerrain(terrainCells, forceReferenceLookup,
        referenceLayout || forceReferenceLayout || oversized ? nullptr : &layoutCache,
        !runtimePathTerrainViewReference());
}

void PathRequestQueue::initializeTerrain(
    const std::vector<PathTerrainCell>& terrainCells,
    bool forceReferenceLookup,
    PathRequestQueueTerrainLayoutCache* layoutCache,
    bool requestReadOnlyBorrow
) {
    RuntimePerfTraceScope terrainTrace("pathQueue.initializeTerrain", -1);
    std::vector<TerrainNode>* ownedTerrain = nullptr;
    if (!requestReadOnlyBorrow) {
        // Preserve the ordinary owned constructor's reserve/allocation order.
        ownedTerrain = &terrain_.ownedConstructionNodes();
        ownedTerrain->reserve(terrainCells.size());
    }
    auto* ownedLayout = &layout_.ownedForConstruction();
    if (!requestReadOnlyBorrow) ownedLayout->terrainIndex.reserve(terrainCells.size());
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    const bool referenceLookup = useReference || forceReferenceLookup;
    bool reuseLayout = layoutCache != nullptr && layoutCache->valid_
        && layoutCache->referenceLookup_ == referenceLookup
        && layoutCache->sourceCoordinates_.size() == terrainCells.size();
    // Identity still requires every authored coordinate, including aliases.
    // Borrow admission has an independent mapping/normalization gate. Check
    // both during the same pure traversal; never stop coordinates on a failed
    // borrow check. The independent reference retains the C37 two passes.
    const bool borrowEligible = reuseLayout && requestReadOnlyBorrow && !referenceLookup
        && !terrainCells.empty() && layoutCache != nullptr
        && layoutCache->sourceNodeIndices_.size() == terrainCells.size()
        && layoutCache->layout_.read().terrainIndex.size() == terrainCells.size();
    const auto admission = reuseLayout
        ? runtimeCheckPathQueueWarmAdmission(terrainCells,
            layoutCache->sourceCoordinates_, layoutCache->sourceNodeIndices_, true, borrowEligible)
        : RuntimePathQueueWarmAdmission{};
    reuseLayout = admission.reuseLayout;
    const bool borrowTerrain = admission.borrowTerrain;
    if (borrowTerrain) terrain_.borrow(terrainCells);
    else if (ownedTerrain == nullptr) {
        ownedTerrain = &terrain_.ownedConstructionNodes();
        ownedTerrain->reserve(terrainCells.size());
    }
    // Retain the completed coordinate layout, not a pointer into mutable cache
    // vectors. Publication moves them once; future queues retain that owner.
    // Every admission above still checks all raw coordinates and node mapping.
    if (borrowTerrain && !runtimePathLayoutViewReference()) {
        try {
            layout_.borrow(layoutCache->layout_.shareReadOnly());
            ownedLayout = nullptr;
        } catch (const std::bad_alloc&) {
            // Only this new publication allocation is optional. Original
            // owned-copy allocation failures retain their existing behavior.
        }
    }
    if (ownedLayout != nullptr && requestReadOnlyBorrow)
        ownedLayout->terrainIndex.reserve(terrainCells.size());
    std::vector<int> sourceNodeIndices;
    if (reuseLayout) {
        if (ownedLayout != nullptr) *ownedLayout = layoutCache->layout_.read();
        layoutCache->lastConstructionReused_ = true;
    } else if (layoutCache != nullptr) {
        sourceNodeIndices.reserve(terrainCells.size());
    }
    if (!reuseLayout && !referenceLookup && !terrainCells.empty()) {
        int minX = terrainCells.front().cell.x, maxX = minX;
        int minY = terrainCells.front().cell.y, maxY = minY;
        for (const auto& source : terrainCells) {
            minX = std::min(minX, source.cell.x); maxX = std::max(maxX, source.cell.x);
            minY = std::min(minY, source.cell.y); maxY = std::max(maxY, source.cell.y);
        }
        const std::int64_t width = static_cast<std::int64_t>(maxX) - minX + 1;
        const std::int64_t height = static_cast<std::int64_t>(maxY) - minY + 1;
        constexpr std::int64_t maxDenseCells = 1024 * 1024;
        // Check each dimension before multiplying, including INT_MIN/MAX
        // inputs. Sparse/huge authored coordinates keep the original lookup.
        if (width <= maxDenseCells && height <= maxDenseCells) {
            const std::int64_t area = width * height;
            if (area <= maxDenseCells
                && static_cast<std::size_t>((area + 3) / 4) <= terrainCells.size()) {
                ownedLayout->denseTerrainMinX = minX; ownedLayout->denseTerrainMinY = minY;
                ownedLayout->denseTerrainWidth = static_cast<int>(width);
                ownedLayout->denseTerrainHeight = static_cast<int>(height);
                ownedLayout->denseTerrainIndex.assign(static_cast<std::size_t>(area), -1);
            }
        }
    }
    if (!reuseLayout && ownedLayout->denseTerrainIndex.empty()) ownedLayout->terrainLookup.reserve(terrainCells.size());

    for (std::size_t sourceIndex = 0; !borrowTerrain && sourceIndex < terrainCells.size(); ++sourceIndex) {
        const PathTerrainCell& source = terrainCells[sourceIndex];
        TerrainNode node;
        node.cell = source.cell;
        node.level = source.level;
        node.passable = source.passable;
        node.water = source.water;
        node.shore = source.shore;
        node.bridge = source.bridge;
        node.ramp = source.ramp;
        node.cliff = source.cliff;
        node.cost = std::max(1, source.cost);
        node.hazardCost = std::max(0, source.hazardCost);

        const long long key = cellKey(node.cell);
        const int existingIndex = reuseLayout
            ? layoutCache->sourceNodeIndices_[sourceIndex] : terrainIndexForKey(key);
        if (!reuseLayout && layoutCache != nullptr) {
            sourceNodeIndices.push_back(existingIndex >= 0
                ? existingIndex : static_cast<int>(ownedTerrain->size()));
        }
        // The cached mapping uses the original first-authored unique order.
        // Its first occurrence is exactly terrain_.size(), hence still appends.
        if (existingIndex >= 0 && static_cast<std::size_t>(existingIndex) < ownedTerrain->size()) {
            TerrainNode& existing = (*ownedTerrain)[existingIndex];
            existing.passable = existing.passable && node.passable;
            existing.water = existing.water || node.water;
            existing.shore = existing.shore || node.shore;
            existing.bridge = existing.bridge || node.bridge;
            existing.ramp = existing.ramp || node.ramp;
            existing.cliff = existing.cliff || node.cliff;
            existing.cost = std::max(existing.cost, node.cost);
            existing.hazardCost = std::max(existing.hazardCost, node.hazardCost);
            existing.level = std::max(existing.level, node.level);
            continue;
        }

        if (!reuseLayout) {
            ownedLayout->terrainIndex.push_back({key, static_cast<int>(ownedTerrain->size())});
            if (ownedLayout->denseTerrainIndex.empty()) {
                ownedLayout->terrainLookup.emplace(key, static_cast<int>(ownedTerrain->size()));
            } else {
                ownedLayout->denseTerrainIndex[denseTerrainOffsetForKey(key)] = static_cast<int>(ownedTerrain->size());
            }
        }
        ownedTerrain->push_back(node);
    }
    if (!borrowTerrain) terrain_.finishOwnedConstruction();

    if (!reuseLayout) {
        std::sort(
            ownedLayout->terrainIndex.begin(),
            ownedLayout->terrainIndex.end(),
            [](const auto& left, const auto& right) {
                return left.first < right.first;
            }
        );
    }

    {
    // Generic queues have no simulation tick. The enclosing Shell scopes bind
    // these optional aggregate timings to the actual movement workload.
    RuntimePerfTraceScope profileTrace("pathQueue.profileHash", -1);
    passabilityProfileHash_ = FnvOffsetBasis;
    // Select once for this construction. Only the passability-profile loop
    // changes arithmetic; request/pending/cache key hashing stays unchanged.
    const bool referenceProfileHash = runtimePathProfileHashReference();
    const bool referenceSparseBytes = runtimePathProfileSparseBytesReference();
    const auto hashProfileFields = [&](auto mixProfile) {
        for (const auto& [key, index] : layout_.read().terrainIndex) {
            const TerrainNode& node = terrain_[index];
            mixProfile(key);
            mixProfile(node.level);
            mixProfile(node.passable ? 1 : 0);
            mixProfile(node.water ? 1 : 0);
            mixProfile(node.shore ? 1 : 0);
            mixProfile(node.bridge ? 1 : 0);
            mixProfile(node.ramp ? 1 : 0);
            mixProfile(node.cliff ? 1 : 0);
            mixProfile(node.cost);
            mixProfile(node.hazardCost);
        }
    };
    // Resolve both independent switches outside the per-field loop. The
    // original reference has priority; the new reference restores C22 only.
    if (referenceProfileHash) {
        hashProfileFields([&](std::int64_t value) {
            passabilityProfileHash_ = runtimePathProfileHashOriginal(passabilityProfileHash_, value);
        });
    } else if (referenceSparseBytes) {
        hashProfileFields([&](std::int64_t value) {
            passabilityProfileHash_ = runtimePathProfileHashC22(passabilityProfileHash_, value);
        });
    } else {
        // Both earlier reference branches retain priority. This independent
        // reference restores the complete C26 sparse-byte profile algorithm.
        const bool referenceZeroRuns = runtimePathProfileZeroRunsReference();
        if (referenceZeroRuns) {
            hashProfileFields([&](std::int64_t value) {
                passabilityProfileHash_ = runtimePathProfileHashFast(passabilityProfileHash_, value);
            });
        } else {
            const auto hashNodes = [&](auto referenceFixedSuffix) {
            const auto mixProfile = [&](std::int64_t value) {
                passabilityProfileHash_ = runtimePathProfileHashFast(passabilityProfileHash_, value);
            };
            for (const auto& [key, index] : layout_.read().terrainIndex) {
                const TerrainNode& node = terrain_[index];
                if constexpr (!decltype(referenceFixedSuffix)::value) {
                    if (runtimePathProfileHasFixedSuffix(node)) {
                        passabilityProfileHash_ = runtimePathProfileHashFixedSuffix(
                            runtimePathProfileHashFast(passabilityProfileHash_, key), node.passable);
                        continue;
                    }
                }
                if (!node.water && !node.shore && !node.bridge && !node.ramp
                    && !node.cliff && node.hazardCost == 0
                    && node.cost >= 1 && node.cost <= 255) {
                    passabilityProfileHash_ = runtimePathProfileHashFlatDry(
                        passabilityProfileHash_, key, node.level, node.passable, node.cost);
                    continue;
                }
                mixProfile(key);
                mixProfile(node.level);
                mixProfile(node.passable ? 1 : 0);
                mixProfile(node.water ? 1 : 0);
                mixProfile(node.shore ? 1 : 0);
                mixProfile(node.bridge ? 1 : 0);
                mixProfile(node.ramp ? 1 : 0);
                mixProfile(node.cliff ? 1 : 0);
                mixProfile(node.cost);
                mixProfile(node.hazardCost);
            }
            };
            if (runtimePathProfileFixedSuffixReference()) hashNodes(std::true_type{});
            else hashNodes(std::false_type{});
        }
    }
    }
    if (!reuseLayout && layoutCache != nullptr) {
        // Publish one complete replacement. No old coordinate layout is kept.
        PathRequestQueueTerrainLayoutCache replacement;
        replacement.sourceCoordinates_.reserve(terrainCells.size());
        for (const auto& source : terrainCells) replacement.sourceCoordinates_.push_back(source.cell);
        replacement.sourceNodeIndices_ = std::move(sourceNodeIndices);
        replacement.layout_.ownedForConstruction() = layout_.read();
        replacement.referenceLookup_ = referenceLookup;
        replacement.valid_ = true;
        layoutCache->swap(replacement);
    }
}

PathRequestHandle PathRequestQueue::submit(PathCell start, PathCell goal) {
    return submit(start, goal, PathApproachContext{});
}

PathRequestHandle PathRequestQueue::submit(
    PathCell start,
    PathCell goal,
    const PathApproachContext& approachContext
) {
    std::vector<PathCell> approachCells =
        normalizedApproachCells(approachContext);
    std::vector<PathCell> alternateGoalCells =
        normalizedAlternateGoalCells(approachContext);
    int goalSlotCount = std::max(0, approachContext.goalSlotCount);
    if (goalSlotCount <= 0 && !alternateGoalCells.empty()) {
        goalSlotCount =
            std::min(16, static_cast<int>(alternateGoalCells.size()));
    }
    const int goalSlotIndex =
        requestedGoalSlotIndex(
            start,
            goal,
            approachContext.requesterStableHash,
            goalSlotCount
        );
    const std::uint64_t contextHash =
        approachContextHash(
            approachContext,
            approachCells,
            alternateGoalCells
        );
    const bool hasMapQueryApproach =
        approachContext.mapQueryApproachAvailable && !approachCells.empty();
    const std::uint64_t requestKey =
        pendingKeyForRequest(start, goal, contextHash, goalSlotIndex);
    if (const auto found = pendingRequestIds_.find(requestKey);
        found != pendingRequestIds_.end()) {
        PendingRequest& pending = pending_[static_cast<std::size_t>(
            found->second - pending_.front().id
        )];
        ++collapsedPendingRequests_;
        if (contextHash != 0
            || !sameCell(pending.start, start)
            || !sameCell(pending.goal, goal)
            || blockedComponentAt(goal).valid) {
            ++massAttackCollapsedRequests_;
            pending.equivalentApproachBurst = true;
        }
        if (!sameCell(pending.start, start) || !sameCell(pending.goal, goal)) {
            ++equivalentStartBucketCollapses_;
            pending.equivalentApproachBurst = true;
        }
        return PathRequestHandle{pending.id};
    }

    constexpr std::size_t MaxPendingRequests = 1024U;
    if (pending_.size() >= MaxPendingRequests) {
        ++backpressureRejectedRequests_;
        ++requestOverflowRejectedRequests_;
        PathResult rejected;
        rejected.requestId = -1;
        rejected.start = start;
        rejected.goal = goal;
        rejected.resolvedGoal = goal;
        rejected.completed = true;
        rejected.queueBackpressure = true;
        rejected.status = PathResultStatus::OverflowRejected;
        rejected.passabilityProfileHash = formatHash(passabilityProfileHash_);
        rejected.requestClass = "rq:" + formatHash(requestKey);
        rejected.failureReason =
            "Path request queue overflow rejected the request before search.";
        results_.push_back(std::move(rejected));
        return PathRequestHandle{-1};
    }

    PendingRequest request;
    request.id = nextRequestId_++;
    request.start = start;
    request.goal = goal;
    request.collapseKey = requestKey;
    request.sharedApproachKey =
        sharedApproachKeyForRequest(
            goal,
            contextHash,
            goalSlotIndex,
            approachContext.shareAcrossGoalSlots
        );
    request.approachContextHash = contextHash;
    request.failureCacheKey = failureCacheKeyForRequest(
        start,
        goal,
        contextHash,
        approachContext.requesterStableHash
    );
    request.noPathCooldownTicks =
        approachContext.noPathCooldownTicks > 0
            ? approachContext.noPathCooldownTicks
            : NoPathFailureCooldownTicks;
    request.goalSlotIndex = goalSlotIndex;
    request.goalSlotCount = goalSlotCount;
    request.shareAcrossGoalSlots = approachContext.shareAcrossGoalSlots;
    request.mapQueryApproach = hasMapQueryApproach;
    request.movementMode = approachContext.movementMode;
    if (hasMapQueryApproach) {
        request.approachCells = std::move(approachCells);
        ++mapQueryApproachRequests_;
        mapQueryApproachAvailable_ = true;
    }
    request.alternateGoalCells = std::move(alternateGoalCells);
    const int requestId = request.id;
    pending_.push_back(std::move(request));
    pendingRequestIds_.emplace(requestKey, requestId);
    return PathRequestHandle{requestId};
}

int PathRequestQueue::pump(int maxRequests) {
    return pumpWithBudget(maxRequests, DefaultExpandedNodesPerPump);
}

int PathRequestQueue::pumpWithBudget(int maxRequests, int expandedNodeBudget) {
    return pumpWithBudgetImpl(maxRequests, expandedNodeBudget, nullptr, -1);
}

int PathRequestQueue::pumpWithCandidates(int maxRequests,
    PathGoalCandidateRunner& runner, std::int64_t simulationTick) {
    return pumpWithBudgetImpl(maxRequests, DefaultExpandedNodesPerPump, &runner, simulationTick);
}

int PathRequestQueue::pumpWithCandidates(int maxRequests, int expandedNodeBudget,
    PathGoalCandidateRunner& runner, std::int64_t simulationTick) {
    return pumpWithBudgetImpl(maxRequests, expandedNodeBudget, &runner, simulationTick);
}

int PathRequestQueue::pumpWithBudgetImpl(int maxRequests, int expandedNodeBudget,
    PathGoalCandidateRunner* candidateRunner, std::int64_t simulationTick) {
    RuntimePerfTraceScope pumpTrace("pathQueue.pump", -1);
    int processed = 0;
    lastPumpExpandedNodes_ = 0;
    lastPumpBudgetExhausted_ = false;
    if (maxRequests <= 0 || pending_.empty()) {
        return 0;
    }

    if (expandedNodeBudget <= 0) {
        lastPumpBudgetExhausted_ = !pending_.empty();
        return 0;
    }

    while (processed < maxRequests && !pending_.empty()) {
        const int remainingBudget = expandedNodeBudget - lastPumpExpandedNodes_;
        if (remainingBudget <= 0) {
            lastPumpBudgetExhausted_ = true;
            break;
        }

        PendingRequest request = std::move(pending_.front());
        pending_.pop_front();
        pendingRequestIds_.erase(request.collapseKey);

        ++logicalTick_;
        PathResult result = solve(request, remainingBudget, candidateRunner, simulationTick);
        lastPumpExpandedNodes_ += result.expandedNodes;
        if (result.searchBudgetExhausted) {
            result.queueBackpressure = true;
            result.status = PathResultStatus::BudgetExhausted;
            lastPumpBudgetExhausted_ = true;
            ++budgetExhaustedResults_;
        }
        if (result.status == PathResultStatus::CooldownNoPath) {
            ++cooldownNoPathResults_;
        } else if (result.status == PathResultStatus::RealNoPath) {
            ++realNoPathResults_;
        }
        if (result.reachable && !result.cacheHit) {
            constexpr std::size_t MaxCachedPaths = 256U;
            if (cache_.size() >= MaxCachedPaths) {
                cache_.erase(cache_.begin());
            }
            cache_.push_back({
                cacheKeyForRequest(
                    request.start,
                    request.goal,
                    request.approachContextHash,
                    request.goalSlotIndex
                ),
                result
            });
            const auto sharedIt = std::find_if(
                sharedApproachCache_.begin(),
                sharedApproachCache_.end(),
                [&request](const auto& entry) {
                    return entry.first == request.sharedApproachKey;
                }
            );
            if (sharedIt != sharedApproachCache_.end()) {
                sharedIt->second = result;
            } else {
                constexpr std::size_t MaxSharedApproachPaths = 256U;
                if (sharedApproachCache_.size() >= MaxSharedApproachPaths) {
                    sharedApproachCache_.erase(sharedApproachCache_.begin());
                }
                sharedApproachCache_.push_back({
                    request.sharedApproachKey,
                    result
                });
            }
        }
        results_.push_back(std::move(result));
        ++processed;
        if (lastPumpExpandedNodes_ >= expandedNodeBudget) {
            lastPumpBudgetExhausted_ = !pending_.empty();
            break;
        }
    }
    return processed;
}

const std::vector<PathResult>& PathRequestQueue::getResults() const {
    return results_;
}

int PathRequestQueue::pendingCount() const {
    return static_cast<int>(pending_.size());
}

int PathRequestQueue::collapsedPendingRequestCount() const {
    return collapsedPendingRequests_;
}

int PathRequestQueue::fallbackTargetCacheHitCount() const {
    return fallbackTargetCacheHits_;
}

int PathRequestQueue::fallbackTargetCacheMissCount() const {
    return fallbackTargetCacheMisses_;
}

int PathRequestQueue::mapQueryApproachRequestCount() const {
    return mapQueryApproachRequests_;
}

int PathRequestQueue::mapQueryApproachUsedResultCount() const {
    return mapQueryApproachUsedResults_;
}

int PathRequestQueue::cacheHitCount() const {
    return cacheHits_;
}

int PathRequestQueue::cacheMissCount() const {
    return cacheMisses_;
}

int PathRequestQueue::budgetExhaustedResultCount() const {
    return budgetExhaustedResults_;
}

int PathRequestQueue::fallbackRingRequestCount() const {
    return fallbackRingRequests_;
}

int PathRequestQueue::equivalentStartBucketCollapseCount() const {
    return equivalentStartBucketCollapses_;
}

int PathRequestQueue::lastPumpExpandedNodeCount() const {
    return lastPumpExpandedNodes_;
}

bool PathRequestQueue::lastPumpBudgetExhausted() const {
    return lastPumpBudgetExhausted_;
}

int PathRequestQueue::backpressureRejectedRequestCount() const {
    return backpressureRejectedRequests_;
}

int PathRequestQueue::requestOverflowRejectedRequestCount() const {
    return requestOverflowRejectedRequests_;
}

int PathRequestQueue::massAttackCollapsedRequestCount() const {
    return massAttackCollapsedRequests_;
}

int PathRequestQueue::sharedApproachCacheHitCount() const {
    return sharedApproachCacheHits_;
}

int PathRequestQueue::sharedApproachCacheMissCount() const {
    return sharedApproachCacheMisses_;
}

PathRequestQueueLiveStats PathRequestQueue::liveStats() const {
    PathRequestQueueLiveStats stats;
    stats.pendingCount = pendingCount();
    stats.cacheHits = cacheHits_;
    stats.cacheMisses = cacheMisses_;
    stats.budgetExhaustedResults = budgetExhaustedResults_;
    stats.lastPumpBudgetExhausted = lastPumpBudgetExhausted_;
    stats.backpressureRejectedRequests = backpressureRejectedRequests_;
    stats.requestOverflowRejectedRequests = requestOverflowRejectedRequests_;
    stats.overflowRejectedResults = requestOverflowRejectedRequests_;
    stats.collapsedPendingRequests = collapsedPendingRequests_;
    stats.massAttackCollapsedRequests = massAttackCollapsedRequests_;
    stats.sharedApproachCacheHits = sharedApproachCacheHits_;
    stats.sharedApproachCacheMisses = sharedApproachCacheMisses_;
    stats.repeatedFailureSuppressed = repeatedFailureSuppressed_;
    stats.noPathCooldownHits = noPathCooldownHits_;
    stats.cooldownNoPathResults = cooldownNoPathResults_;
    stats.realNoPathResults = realNoPathResults_;
    stats.equivalentStartBucketCollapses = equivalentStartBucketCollapses_;
    stats.goalSlotAssignments = goalSlotAssignments_;
    stats.fallbackCandidatesConsidered = fallbackCandidatesConsidered_;
    stats.fallbackRingRequests = fallbackRingRequests_;
    stats.fallbackTargetCacheHits = fallbackTargetCacheHits_;
    stats.fallbackTargetCacheMisses = fallbackTargetCacheMisses_;
    stats.mapQueryApproachRequests = mapQueryApproachRequests_;
    stats.mapQueryApproachUsedResults = mapQueryApproachUsedResults_;
    stats.lastPumpExpandedNodes = lastPumpExpandedNodes_;
    stats.cachedPathCount = static_cast<int>(cache_.size());
    stats.fallbackTargetCacheEntries =
        static_cast<int>(fallbackTargetCache_.size());
    stats.fallbackDistanceQueries = fallbackDistanceQueries_;
    stats.fallbackComponentCellChecks = fallbackComponentCellChecks_;
    stats.heuristicEvaluations = heuristicEvaluations_;
    stats.usesDenseTerrainLookup = !layout_.read().denseTerrainIndex.empty();
    stats.usesShortHashKeys = true;
    stats.mapQueryApproachAvailable = mapQueryApproachAvailable_;
    stats.usesBlockedComponentApproachFallback =
        blockedComponentApproachFallbackUsed_;
    return stats;
}

int PathRequestQueue::terrainIndexForKey(long long key) const {
    const auto& layout = layout_.read();
    if (!layout.denseTerrainIndex.empty()) {
        const int offset = denseTerrainOffsetForKey(key);
        return offset >= 0 ? layout.denseTerrainIndex[offset] : -1;
    }
    const auto lookup = layout.terrainLookup.find(key);
    return lookup != layout.terrainLookup.end() ? lookup->second : -1;
}

int PathRequestQueue::denseTerrainOffsetForKey(long long key) const {
    const auto bits = static_cast<std::uint64_t>(key);
    const auto x = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
    const auto y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits >> 32));
    const auto& layout = layout_.read();
    const std::int64_t localX = static_cast<std::int64_t>(x) - layout.denseTerrainMinX;
    const std::int64_t localY = static_cast<std::int64_t>(y) - layout.denseTerrainMinY;
    if (localX < 0 || localY < 0
        || localX >= layout.denseTerrainWidth || localY >= layout.denseTerrainHeight) return -1;
    return static_cast<int>(localY * layout.denseTerrainWidth + localX);
}

std::vector<int> PathRequestQueue::terrainIndicesInBounds(
    std::int64_t minX, std::int64_t minY,
    std::int64_t maxX, std::int64_t maxY
) const {
    std::vector<int> indices;
    minX = std::max(minX, static_cast<std::int64_t>(std::numeric_limits<int>::min()));
    minY = std::max(minY, static_cast<std::int64_t>(std::numeric_limits<int>::min()));
    maxX = std::min(maxX, static_cast<std::int64_t>(std::numeric_limits<int>::max()));
    maxY = std::min(maxY, static_cast<std::int64_t>(std::numeric_limits<int>::max()));
    const auto width = static_cast<long long>(maxX) - minX + 1;
    const auto height = static_cast<long long>(maxY) - minY + 1;
    if (width <= 0 || height <= 0) {
        return indices;
    }
    if (width <= static_cast<long long>(terrain_.size()) / height) {
        for (long long y = minY; y <= maxY; ++y) {
            for (long long x = minX; x <= maxX; ++x) {
                const int index = terrainIndexForKey(
                    cellKey(static_cast<int>(x), static_cast<int>(y))
                );
                if (index >= 0) {
                    indices.push_back(index);
                }
            }
        }
        // Existing alternate-goal selection takes the first matching terrain
        // entries before scoring them. Keep that order for authored maps.
        std::sort(indices.begin(), indices.end());
    } else {
        for (std::size_t index = 0; index < terrain_.size(); ++index) {
            const PathCell cell = terrain_[index].cell;
            if (cell.x >= minX && cell.x <= maxX
                && cell.y >= minY && cell.y <= maxY) {
                indices.push_back(static_cast<int>(index));
            }
        }
    }
    return indices;
}

const PathRequestQueue::TerrainNode* PathRequestQueue::terrainNodeAt(
    PathCell cell
) const {
    const int index = terrainIndexForKey(cellKey(cell));
    if (index < 0 || static_cast<std::size_t>(index) >= terrain_.size()) {
        return nullptr;
    }
    return &terrain_[index];
}

const PathRequestQueue::BlockedComponent& PathRequestQueue::blockedComponentAt(
    PathCell goal
) const {
    const TerrainNode* goalNode = terrainNodeAt(goal);
    if (goalNode == nullptr || (goalNode->passable && !goalNode->cliff)) {
        static const BlockedComponent empty;
        return empty;
    }
    const long long goalKey = cellKey(goal);
    const auto cached = blockedComponents_.find(goalKey);
    if (cached != blockedComponents_.end()) {
        return cached->second;
    }

    constexpr int MaxComponentRadius = 12;
    constexpr int ComponentDiameter = MaxComponentRadius * 2 + 1;
    constexpr PathCell directions[] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    BlockedComponent component;
    std::vector<PathCell> open;
    std::array<bool, ComponentDiameter * ComponentDiameter> visited{};
    open.push_back(goal);
    visited[MaxComponentRadius * ComponentDiameter + MaxComponentRadius] = true;

    component.valid = true;
    component.minX = goal.x;
    component.minY = goal.y;
    component.maxX = goal.x;
    component.maxY = goal.y;

    for (std::size_t index = 0; index < open.size(); ++index) {
        const PathCell current = open[index];
        component.cells.push_back(current);
        component.minX = std::min(component.minX, current.x);
        component.minY = std::min(component.minY, current.y);
        component.maxX = std::max(component.maxX, current.x);
        component.maxY = std::max(component.maxY, current.y);

        for (PathCell direction : directions) {
            PathCell next;
            next.x = current.x + direction.x;
            next.y = current.y + direction.y;
            if (std::max(
                    std::abs(next.x - goal.x),
                    std::abs(next.y - goal.y)
                ) > MaxComponentRadius) {
                continue;
            }

            const int localIndex = (next.y - goal.y + MaxComponentRadius)
                * ComponentDiameter + next.x - goal.x + MaxComponentRadius;
            if (visited[localIndex]) {
                continue;
            }
            visited[localIndex] = true;

            const TerrainNode* node = terrainNodeAt(next);
            if (node == nullptr || (node->passable && !node->cliff)) {
                continue;
            }

            open.push_back(next);
        }
    }

    std::sort(component.cells.begin(), component.cells.end(), pathCellsLess);
    component.fillsBounds = component.cells.size()
        == static_cast<std::size_t>(component.maxX - component.minX + 1)
            * static_cast<std::size_t>(component.maxY - component.minY + 1);
    constexpr std::size_t MaxCachedComponents = 128;
    if (blockedComponents_.size() >= MaxCachedComponents) {
        blockedComponents_.erase(blockedComponentOrder_.front());
        blockedComponentOrder_.pop_front();
    }
    blockedComponentOrder_.push_back(goalKey);
    return blockedComponents_.emplace(goalKey, std::move(component)).first->second;
}

std::uint64_t PathRequestQueue::cacheKeyForRequest(
    PathCell start,
    PathCell goal,
    std::uint64_t approachContextHash,
    int goalSlotIndex
) const {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 1);
    hashInt(hash, static_cast<std::int64_t>(passabilityProfileHash_));
    hashCell(hash, start);
    hashInt(hash, static_cast<std::int64_t>(approachContextHash));
    hashInt(hash, goalSlotIndex);

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

std::uint64_t PathRequestQueue::pendingKeyForRequest(
    PathCell start,
    PathCell goal,
    std::uint64_t approachContextHash,
    int goalSlotIndex
) const {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 2);
    hashInt(hash, static_cast<std::int64_t>(passabilityProfileHash_));
    hashStartBucket(hash, start);
    hashInt(hash, static_cast<std::int64_t>(approachContextHash));
    hashInt(hash, goalSlotIndex);

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

std::uint64_t PathRequestQueue::sharedApproachKeyForRequest(
    PathCell goal,
    std::uint64_t approachContextHash,
    int goalSlotIndex,
    bool shareAcrossGoalSlots
) const {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 11);
    hashInt(hash, static_cast<std::int64_t>(passabilityProfileHash_));
    hashInt(hash, static_cast<std::int64_t>(approachContextHash));
    if (approachContextHash != 0) {
        hashInt(hash, 2);
        if (!shareAcrossGoalSlots) {
            hashInt(hash, goalSlotIndex);
        }
        return hash;
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

std::uint64_t PathRequestQueue::fallbackTargetCacheKey(
    PathCell start,
    PathCell goal,
    const BlockedComponent& component
) const {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, 3);
    hashInt(hash, static_cast<std::int64_t>(passabilityProfileHash_));
    hashStartBucket(hash, start);
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

