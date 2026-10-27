#include "simulation/navigation/pathfinding/PathGoalCandidates.h"
#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"
#include "simulation/navigation/pathfinding/PathRequestQueue.h"
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

#ifdef CONCORD_PATH_GOAL_CANDIDATE_TEST_OBSERVER
// Compiled only by the isolated S22 differential fixture; absent from product targets.
void recordPathGoalCandidatesForTest(int, int, const std::vector<PathCell>&);
#endif

namespace {

constexpr int DefaultExpandedNodesPerPump = 32768;
constexpr int DefaultExpandedNodesPerRequest = 16384;
constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t FnvPrime = 1099511628211ull;
constexpr int NoPathFailureCooldownTicks = 64;

long long cellKey(int x, int y) {
    return (static_cast<long long>(y) << 32)
        ^ static_cast<unsigned int>(x);
}

long long cellKey(PathCell cell) {
    return cellKey(cell.x, cell.y);
}

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

void hashByte(std::uint64_t& hash, unsigned char value) {
    hash ^= value;
    hash *= FnvPrime;
}

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

void hashStartBucket(std::uint64_t& hash, PathCell start) {
    hashInt(hash, start.x / 2);
    hashInt(hash, start.y / 2);
}

std::string formatHash(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

const char* pathResultStatusToken(PathResultStatus status) {
    switch (status) {
    case PathResultStatus::Queued:
        return "queued";
    case PathResultStatus::Pending:
        return "pending";
    case PathResultStatus::Reached:
        return "reached";
    case PathResultStatus::CooldownNoPath:
        return "cooldown-no-path";
    case PathResultStatus::BudgetExhausted:
        return "budget-exhausted";
    case PathResultStatus::RealNoPath:
        return "real-no-path";
    case PathResultStatus::OverflowRejected:
        return "overflow-rejected";
    }
    return "unknown";
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

PathfindingSandboxReport runPathfindingSandbox(const GameWorld& world) {
    PathfindingSandboxReport report;
    const std::vector<TerrainCell>& terrainCells = world.getTerrainCells();
    report.terrainCells = static_cast<int>(terrainCells.size());
    if (terrainCells.empty()) {
        report.errorMessage = "GameWorld has no terrain cells.";
        return report;
    }

    std::unordered_map<long long, const TerrainCell*> terrainByKey;
    terrainByKey.reserve(terrainCells.size());
    for (const TerrainCell& cell : terrainCells) {
        terrainByKey[cellKey(cell.x, cell.y)] = &cell;
    }

    bool foundPair = false;
    PathCell start;
    PathCell goal;
    for (const TerrainCell& cell : terrainCells) {
        constexpr int offsets[] = {12, 10, 8, 6, 4, 2};
        for (int offset : offsets) {
            PathCell candidateGoal{cell.x + offset, cell.y};
            const auto goalIt = terrainByKey.find(cellKey(candidateGoal));
            if (goalIt == terrainByKey.end()) {
                continue;
            }
            if (goalIt->second->level != cell.level) {
                continue;
            }

            start = PathCell{cell.x, cell.y};
            goal = candidateGoal;
            foundPair = true;
            break;
        }
        if (foundPair) {
            break;
        }
    }

    if (!foundPair) {
        report.errorMessage = "No same-level terrain pair found for pathfinding.";
        return report;
    }

    PathRequestQueue queue(terrainCells);
    queue.submit(start, goal);
    ++report.requestsSubmitted;
    queue.pump(1);

    queue.submit(start, goal);
    ++report.requestsSubmitted;
    while (queue.pendingCount() > 0) {
        queue.pump(4);
    }

    report.results = queue.getResults();
    for (const PathResult& result : report.results) {
        if (result.completed) {
            ++report.requestsCompleted;
        }
        if (result.cacheHit) {
            ++report.cacheHits;
        }
        if (result.cacheMiss) {
            ++report.cacheMisses;
        }
        if (result.reachable) {
            ++report.reachablePaths;
        }
        if (result.fallbackRing) {
            ++report.fallbackRingRequests;
        }
        if (result.mapQueryApproachUsed) {
            ++report.mapQueryApproachUsedResults;
        }
        if (result.searchBudgetExhausted) {
            ++report.budgetExhaustedResults;
        }
    }

    report.cacheValidated =
        report.results.size() == 2
        && report.results[0].reachable
        && report.results[1].reachable
        && report.results[1].cacheHit;

    std::vector<PathTerrainCell> constrainedCells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.level = 0;
            cell.passable = true;
            constrainedCells.push_back(cell);
        }
    }
    for (PathTerrainCell& cell : constrainedCells) {
        if ((cell.cell.x == 1 && cell.cell.y == 0)
            || (cell.cell.x == 0 && cell.cell.y == 1)) {
            cell.passable = false;
        }
    }
    PathRequestQueue cornerQueue(constrainedCells);
    cornerQueue.submit({0, 0}, {1, 1});
    cornerQueue.pump(1);
    const bool cornerCutBlocked =
        !cornerQueue.getResults().empty()
        && !cornerQueue.getResults().front().reachable;
    report.cornerCutBlocked = cornerCutBlocked;

    std::vector<PathTerrainCell> rampCells;
    PathTerrainCell lowerRamp;
    lowerRamp.cell = {0, 0};
    lowerRamp.level = 0;
    lowerRamp.ramp = true;
    rampCells.push_back(lowerRamp);
    PathTerrainCell upperRamp;
    upperRamp.cell = {1, 0};
    upperRamp.level = 1;
    upperRamp.ramp = true;
    rampCells.push_back(upperRamp);
    PathRequestQueue rampQueue(rampCells);
    rampQueue.submit({0, 0}, {1, 0});
    rampQueue.pump(1);
    const bool rampTransitionAllowed =
        !rampQueue.getResults().empty()
        && rampQueue.getResults().front().reachable;
    report.rampTransitionAllowed = rampTransitionAllowed;

    std::vector<PathTerrainCell> waterCells;
    PathTerrainCell landCell;
    landCell.cell = {0, 0};
    landCell.passable = true;
    waterCells.push_back(landCell);
    PathTerrainCell pureWaterCell;
    pureWaterCell.cell = {1, 0};
    pureWaterCell.water = true;
    pureWaterCell.passable = true;
    waterCells.push_back(pureWaterCell);
    PathTerrainCell shoreCell;
    shoreCell.cell = {0, 1};
    shoreCell.shore = true;
    shoreCell.passable = true;
    waterCells.push_back(shoreCell);
    PathTerrainCell shoreWaterCell;
    shoreWaterCell.cell = {1, 1};
    shoreWaterCell.water = true;
    shoreWaterCell.shore = true;
    shoreWaterCell.passable = true;
    waterCells.push_back(shoreWaterCell);

    std::vector<PathTerrainCell> pureWaterBlockedCells;
    pureWaterBlockedCells.push_back(landCell);
    pureWaterBlockedCells.push_back(pureWaterCell);
    PathRequestQueue waterBlockedQueue(pureWaterBlockedCells);
    waterBlockedQueue.submit({0, 0}, {1, 0});
    waterBlockedQueue.pump(1);
    const bool pureWaterTransitionBlocked =
        !waterBlockedQueue.getResults().empty()
        && !waterBlockedQueue.getResults().front().reachable;

    PathRequestQueue shoreAllowedQueue(waterCells);
    shoreAllowedQueue.submit({0, 0}, {1, 1});
    shoreAllowedQueue.pump(1);
    const bool shoreTransitionAllowed =
        !shoreAllowedQueue.getResults().empty()
        && shoreAllowedQueue.getResults().front().reachable;
    report.waterShoreRulesValidated =
        pureWaterTransitionBlocked && shoreTransitionAllowed;

    std::vector<PathTerrainCell> domainCells;
    PathTerrainCell domainWater;
    domainWater.cell = {0, 0};
    domainWater.water = true;
    domainCells.push_back(domainWater);
    PathTerrainCell domainLand;
    domainLand.cell = {1, 0};
    domainCells.push_back(domainLand);

    PathApproachContext navalContext;
    navalContext.movementMode = PathMovementMode::Naval;
    PathRequestQueue navalQueue(domainCells);
    navalQueue.submit({0, 0}, {1, 0}, navalContext);
    navalQueue.pump(1);
    report.navalLandBlocked =
        !navalQueue.getResults().empty()
        && (!navalQueue.getResults().front().reachable
            || !sameCell(
                navalQueue.getResults().front().resolvedGoal,
                PathCell{1, 0}
            ));

    PathApproachContext amphibiousContext;
    amphibiousContext.movementMode = PathMovementMode::Amphibious;
    PathRequestQueue amphibiousQueue(domainCells);
    amphibiousQueue.submit({0, 0}, {1, 0}, amphibiousContext);
    amphibiousQueue.pump(1);
    report.amphibiousTransitionAllowed =
        !amphibiousQueue.getResults().empty()
        && amphibiousQueue.getResults().front().reachable
        && sameCell(
            amphibiousQueue.getResults().front().resolvedGoal,
            PathCell{1, 0}
        );

    std::vector<PathTerrainCell> airCells = domainCells;
    airCells[0].water = false;
    airCells[1].passable = false;
    airCells[1].cliff = true;
    airCells[1].level = 4;
    PathApproachContext airContext;
    airContext.movementMode = PathMovementMode::Air;
    PathRequestQueue airQueue(airCells);
    airQueue.submit({0, 0}, {1, 0}, airContext);
    airQueue.pump(1);
    report.aircraftTerrainIgnored =
        !airQueue.getResults().empty()
        && airQueue.getResults().front().reachable
        && sameCell(
            airQueue.getResults().front().resolvedGoal,
            PathCell{1, 0}
        );

    std::vector<PathTerrainCell> hazardCells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = true;
            if (y == 1 && x > 0 && x < 4) {
                cell.hazardCost = 180;
            }
            hazardCells.push_back(cell);
        }
    }
    PathRequestQueue hazardQueue(hazardCells);
    hazardQueue.submit({0, 1}, {4, 1});
    hazardQueue.pump(1);
    const bool hazardousRouteDetoured =
        !hazardQueue.getResults().empty()
        && hazardQueue.getResults().front().reachable
        && std::any_of(
            hazardQueue.getResults().front().path.begin(),
            hazardQueue.getResults().front().path.end(),
            [](const PathCell& cell) {
                return cell.y != 1;
            }
        );
    for (PathTerrainCell& cell : hazardCells) {
        cell.hazardCost = 0;
    }
    PathRequestQueue safeQueue(hazardCells);
    safeQueue.submit({0, 1}, {4, 1});
    safeQueue.pump(1);
    const bool safeRouteStayedDirect =
        !safeQueue.getResults().empty()
        && safeQueue.getResults().front().reachable
        && std::all_of(
            safeQueue.getResults().front().path.begin(),
            safeQueue.getResults().front().path.end(),
            [](const PathCell& cell) {
                return cell.y == 1;
            }
        );
    report.hazardAvoidanceValidated =
        hazardousRouteDetoured && safeRouteStayedDirect;
    std::vector<PathTerrainCell> directAirCells;
    for (int y = 0; y <= 5; ++y) {
        for (int x = 0; x <= 8; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = false;
            cell.water = ((x + y) % 2) == 0;
            cell.cliff = true;
            cell.level = (x * 3 + y) % 7;
            directAirCells.push_back(cell);
        }
    }
    PathRequestQueue directAirQueue(directAirCells);
    directAirQueue.submit({0, 0}, {8, 5}, airContext);
    directAirQueue.pump(1);
    const bool directAirLine =
        !directAirQueue.getResults().empty()
        && directAirQueue.getResults().front().reachable
        && directAirQueue.getResults().front().path.size() == 9U
        && sameCell(
            directAirQueue.getResults().front().path.front(),
            PathCell{0, 0}
        )
        && sameCell(
            directAirQueue.getResults().front().path.back(),
            PathCell{8, 5}
        );
    report.aircraftTerrainIgnored =
        report.aircraftTerrainIgnored && directAirLine;

    std::vector<PathTerrainCell> bridgeCells;
    PathTerrainCell lowerBridge;
    lowerBridge.cell = {0, 0};
    lowerBridge.level = 0;
    lowerBridge.bridge = true;
    bridgeCells.push_back(lowerBridge);
    PathTerrainCell upperBridge;
    upperBridge.cell = {1, 0};
    upperBridge.level = 1;
    upperBridge.bridge = true;
    bridgeCells.push_back(upperBridge);
    PathRequestQueue bridgeQueue(bridgeCells);
    bridgeQueue.submit({0, 0}, {1, 0});
    bridgeQueue.pump(1);
    report.bridgeTransitionAllowed =
        !bridgeQueue.getResults().empty()
        && bridgeQueue.getResults().front().reachable;

    std::vector<PathTerrainCell> fallbackCells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = !(x == 1 && y == 1);
            fallbackCells.push_back(cell);
        }
    }
    PathRequestQueue fallbackQueue(fallbackCells);
    fallbackQueue.submit({0, 1}, {1, 1});
    fallbackQueue.pump(1);
    report.blockedGoalFallbackValidated =
        !fallbackQueue.getResults().empty()
        && fallbackQueue.getResults().front().reachable
        && fallbackQueue.getResults().front().goalAdjusted
        && !sameCell(fallbackQueue.getResults().front().resolvedGoal, {1, 1});
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || fallbackQueue.liveStats().usesBlockedComponentApproachFallback;

    std::vector<PathTerrainCell> footprintCells;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 6; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable =
                !((x == 2 || x == 3) && (y == 1 || y == 2));
            footprintCells.push_back(cell);
        }
    }
    PathRequestQueue footprintQueue(footprintCells);
    PathApproachContext footprintApproach;
    footprintApproach.mapQueryApproachAvailable = true;
    footprintApproach.targetFootprintHash = 0x2020303010102020ull;
    footprintApproach.approachRingHash = 0x1020304050607080ull;
    footprintApproach.movementConstraintHash = 0x1122334455667788ull;
    footprintApproach.approachCells = {
        {1, 2},
        {1, 1},
        {2, 0},
        {3, 0},
        {4, 2}
    };
    footprintQueue.submit({0, 2}, {2, 2}, footprintApproach);
    footprintQueue.pump(1);
    const PathRequestQueueLiveStats footprintStats = footprintQueue.liveStats();
    report.mapQueryApproachRequests +=
        footprintQueue.mapQueryApproachRequestCount();
    report.mapQueryApproachUsedResults +=
        footprintQueue.mapQueryApproachUsedResultCount();
    report.mapQueryApproachAvailable =
        report.mapQueryApproachAvailable
        || footprintStats.mapQueryApproachAvailable;
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || footprintStats.usesBlockedComponentApproachFallback;
    report.footprintFallbackValidated =
        !footprintQueue.getResults().empty()
        && footprintQueue.getResults().front().reachable
        && footprintQueue.getResults().front().goalAdjusted
        && footprintQueue.getResults().front().mapQueryApproachUsed
        && sameCell(footprintQueue.getResults().front().resolvedGoal, {1, 2})
        && footprintQueue.getResults().front().fallbackTargetsTried == 1;

    PathRequestQueue blockedCacheQueue(footprintCells);
    blockedCacheQueue.submit({0, 2}, {2, 2});
    blockedCacheQueue.pump(1);
    blockedCacheQueue.submit({0, 2}, {3, 1});
    blockedCacheQueue.pump(1);
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || blockedCacheQueue.liveStats().usesBlockedComponentApproachFallback;
    report.blockedTargetCacheValidated =
        blockedCacheQueue.getResults().size() == 2U
        && blockedCacheQueue.getResults()[0].reachable
        && blockedCacheQueue.getResults()[1].reachable
        && blockedCacheQueue.getResults()[1].cacheHit
        && blockedCacheQueue.getResults()[1].goalAdjusted
        && sameCell(blockedCacheQueue.getResults()[1].goal, {3, 1})
        && sameCell(
            blockedCacheQueue.getResults()[0].resolvedGoal,
            blockedCacheQueue.getResults()[1].resolvedGoal
        );

    PathRequestQueue fallbackReuseQueue(footprintCells);
    fallbackReuseQueue.submit({0, 2}, {2, 2});
    fallbackReuseQueue.pump(1);
    fallbackReuseQueue.submit({1, 2}, {2, 2});
    fallbackReuseQueue.pump(1);
    report.fallbackTargetCacheHits =
        fallbackReuseQueue.fallbackTargetCacheHitCount();
    report.fallbackTargetCacheMisses =
        fallbackReuseQueue.fallbackTargetCacheMissCount();
    report.fallbackRingRequests += fallbackReuseQueue.fallbackRingRequestCount();
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || fallbackReuseQueue.liveStats().usesBlockedComponentApproachFallback;
    report.fallbackTargetCacheValidated =
        fallbackReuseQueue.getResults().size() == 2U
        && fallbackReuseQueue.getResults()[0].reachable
        && fallbackReuseQueue.getResults()[1].reachable
        && report.fallbackTargetCacheHits == 1
        && report.fallbackTargetCacheMisses == 1;

    PathRequestQueue blockedPendingQueue(footprintCells);
    const PathRequestHandle blockedPendingFirst =
        blockedPendingQueue.submit({0, 2}, {2, 2});
    const PathRequestHandle blockedPendingSecond =
        blockedPendingQueue.submit({0, 2}, {3, 1});
    const bool blockedPendingCollapsed =
        blockedPendingFirst.id == blockedPendingSecond.id
        && blockedPendingQueue.pendingCount() == 1
        && blockedPendingQueue.collapsedPendingRequestCount() == 1;
    blockedPendingQueue.pump(1);
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || blockedPendingQueue.liveStats().usesBlockedComponentApproachFallback;

    std::vector<PathTerrainCell> occupiedStartCells;
    for (int x = 0; x < 3; ++x) {
        PathTerrainCell cell;
        cell.cell = {x, 0};
        cell.passable = x != 0;
        occupiedStartCells.push_back(cell);
    }
    PathRequestQueue occupiedStartQueue(occupiedStartCells);
    occupiedStartQueue.submit({0, 0}, {2, 0});
    occupiedStartQueue.pump(1);
    report.occupiedStartEscapeValidated =
        !occupiedStartQueue.getResults().empty()
        && occupiedStartQueue.getResults().front().reachable
        && occupiedStartQueue.getResults().front().path.size() >= 2U
        && sameCell(occupiedStartQueue.getResults().front().path.front(), {0, 0})
        && sameCell(occupiedStartQueue.getResults().front().path.back(), {2, 0});

    std::vector<PathTerrainCell> formationCells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = true;
            formationCells.push_back(cell);
        }
    }
    PathRequestQueue formationQueue(formationCells);
    formationQueue.submit({0, 1}, {4, 0});
    formationQueue.submit({0, 1}, {4, 1});
    formationQueue.submit({0, 1}, {4, 2});
    while (formationQueue.pendingCount() > 0) {
        formationQueue.pump(2);
    }
    report.formationRequestsCompleted =
        formationQueue.getResults().size() == 3U
        && std::all_of(
            formationQueue.getResults().begin(),
            formationQueue.getResults().end(),
            [](const PathResult& result) {
                return result.completed && result.reachable;
            }
        );

    PathRequestQueue duplicateQueue(formationCells);
    const PathRequestHandle duplicateFirst =
        duplicateQueue.submit({0, 1}, {4, 1});
    const PathRequestHandle duplicateSecond =
        duplicateQueue.submit({0, 1}, {4, 1});
    const bool duplicatePendingCollapsed =
        duplicateFirst.id == duplicateSecond.id
        && duplicateQueue.pendingCount() == 1;
    duplicateQueue.pump(4);
    report.duplicateRequestsCollapsed =
        duplicatePendingCollapsed
        && duplicateQueue.pendingCount() == 0
        && duplicateQueue.getResults().size() == 1U
        && duplicateQueue.getResults().front().reachable;

    PathRequestQueue multiUnitDuplicateQueue(footprintCells);
    multiUnitDuplicateQueue.submit({0, 1}, {2, 2});
    multiUnitDuplicateQueue.submit({0, 1}, {2, 2});
    multiUnitDuplicateQueue.submit({0, 2}, {2, 2});
    multiUnitDuplicateQueue.submit({1, 2}, {2, 2});
    multiUnitDuplicateQueue.submit({0, 3}, {2, 2});
    multiUnitDuplicateQueue.submit({1, 3}, {2, 2});
    const bool multiUnitPendingCollapsed =
        multiUnitDuplicateQueue.pendingCount() == 2
        && multiUnitDuplicateQueue.collapsedPendingRequestCount() == 4;
    while (multiUnitDuplicateQueue.pendingCount() > 0) {
        multiUnitDuplicateQueue.pump(4);
    }
    report.blockedComponentApproachFallbackUsed =
        report.blockedComponentApproachFallbackUsed
        || multiUnitDuplicateQueue.liveStats().usesBlockedComponentApproachFallback;
    report.collapsedPendingRequests =
        duplicateQueue.collapsedPendingRequestCount()
        + blockedPendingQueue.collapsedPendingRequestCount()
        + multiUnitDuplicateQueue.collapsedPendingRequestCount();
    report.equivalentStartBucketCollapses =
        blockedPendingQueue.equivalentStartBucketCollapseCount()
        + multiUnitDuplicateQueue.equivalentStartBucketCollapseCount();
    report.multiUnitDuplicateRequestsCollapsed =
        multiUnitPendingCollapsed
        && multiUnitDuplicateQueue.getResults().size() == 2U
        && std::all_of(
            multiUnitDuplicateQueue.getResults().begin(),
            multiUnitDuplicateQueue.getResults().end(),
            [](const PathResult& result) {
                return result.completed
                    && result.reachable
                    && result.goalAdjusted
                    && result.fallbackTargetsTried <= 1;
            }
        );

    PathRequestQueue moveBurstQueue(formationCells);
    constexpr int MoveBurstRequests = 48;
    constexpr PathCell moveBurstStarts[] = {
        {0, 0},
        {1, 0},
        {0, 1},
        {1, 1}
    };
    for (int index = 0; index < MoveBurstRequests; ++index) {
        PathApproachContext moveBurstContext;
        moveBurstContext.goalSlotCount = 4;
        moveBurstContext.noPathCooldownTicks = 96;
        moveBurstContext.requesterStableHash =
            0x1700000000000000ull + static_cast<std::uint64_t>(index);
        moveBurstQueue.submit(
            moveBurstStarts[index % 4],
            {4, 1},
            moveBurstContext
        );
    }
    while (moveBurstQueue.pendingCount() > 0) {
        moveBurstQueue.pump(4);
    }
    PathApproachContext moveBurstFollowupA;
    moveBurstFollowupA.goalSlotCount = 4;
    moveBurstFollowupA.requesterStableHash = 0x1700000000000101ull;
    moveBurstQueue.submit({1, 0}, {4, 1}, moveBurstFollowupA);
    moveBurstQueue.pump(1);
    PathApproachContext moveBurstFollowupB = moveBurstFollowupA;
    moveBurstFollowupB.requesterStableHash = 0x1700000000000102ull;
    moveBurstQueue.submit({1, 1}, {4, 1}, moveBurstFollowupB);
    moveBurstQueue.pump(1);
    const PathRequestQueueLiveStats moveBurstStats =
        moveBurstQueue.liveStats();
    std::unordered_set<long long> moveBurstResolvedGoals;
    for (const PathResult& result : moveBurstQueue.getResults()) {
        if (result.reachable) {
            moveBurstResolvedGoals.insert(cellKey(result.resolvedGoal));
            report.moveBurstFallbackCandidates = std::max(
                report.moveBurstFallbackCandidates,
                result.fallbackCandidateCount
            );
        }
    }
    report.moveBurstRequests = MoveBurstRequests;
    report.moveBurstCollapsedRequests =
        moveBurstStats.collapsedPendingRequests;
    report.moveBurstSharedApproachHits =
        moveBurstStats.sharedApproachCacheHits;
    report.moveBurstGoalSlotsUsed =
        static_cast<int>(moveBurstResolvedGoals.size());
    report.collapsedPendingRequests +=
        report.moveBurstCollapsedRequests;
    report.sharedApproachCacheHits +=
        report.moveBurstSharedApproachHits;
    report.sharedApproachCacheMisses +=
        moveBurstStats.sharedApproachCacheMisses;
    report.equivalentStartBucketCollapses +=
        moveBurstStats.equivalentStartBucketCollapses;

    PathRequestQueue massAttackQueue(footprintCells);
    constexpr int MassAttackBurstRequests = 48;
    constexpr PathCell initialMassStarts[] = {
        {0, 1},
        {1, 1},
        {0, 2},
        {1, 2}
    };
    constexpr PathCell followupMassStarts[] = {
        {1, 1},
        {1, 2},
        {0, 1},
        {0, 2}
    };
    for (int index = 0; index < MassAttackBurstRequests; ++index) {
        PathApproachContext attackContext = footprintApproach;
        attackContext.goalSlotCount = 4;
        attackContext.noPathCooldownTicks = 96;
        attackContext.requesterStableHash =
            0x1717000000000000ull + static_cast<std::uint64_t>(index);
        massAttackQueue.submit(
            initialMassStarts[index % 4],
            {2, 2},
            attackContext
        );
    }
    report.massAttackBurstRequests = MassAttackBurstRequests;
    report.massAttackPendingAfterSubmit = massAttackQueue.pendingCount();
    while (massAttackQueue.pendingCount() > 0) {
        massAttackQueue.pump(4);
    }
    for (int index = 0; index < MassAttackBurstRequests; ++index) {
        PathApproachContext attackContext = footprintApproach;
        attackContext.goalSlotCount = 4;
        attackContext.noPathCooldownTicks = 96;
        attackContext.requesterStableHash =
            0x1717000000001000ull + static_cast<std::uint64_t>(index);
        massAttackQueue.submit(
            followupMassStarts[index % 4],
            {2, 2},
            attackContext
        );
    }
    while (massAttackQueue.pendingCount() > 0) {
        massAttackQueue.pump(4);
    }
    const PathRequestQueueLiveStats massAttackStats =
        massAttackQueue.liveStats();
    std::unordered_set<long long> massAttackResolvedGoals;
    for (const PathResult& result : massAttackQueue.getResults()) {
        report.massAttackExpandedNodes += result.expandedNodes;
        if (result.reachable && result.mapQueryApproachUsed) {
            massAttackResolvedGoals.insert(cellKey(result.resolvedGoal));
            report.massAttackFallbackCandidates = std::max(
                report.massAttackFallbackCandidates,
                result.fallbackCandidateCount
            );
        }
    }
    report.massAttackGoalSlotsUsed =
        static_cast<int>(massAttackResolvedGoals.size());
    report.massAttackCollapsedRequests =
        massAttackStats.massAttackCollapsedRequests;
    report.sharedApproachCacheHits +=
        massAttackStats.sharedApproachCacheHits;
    report.sharedApproachCacheMisses +=
        massAttackStats.sharedApproachCacheMisses;
    report.mapQueryApproachRequests +=
        massAttackStats.mapQueryApproachRequests;
    report.mapQueryApproachUsedResults +=
        massAttackStats.mapQueryApproachUsedResults;
    report.mapQueryApproachAvailable =
        report.mapQueryApproachAvailable
        || massAttackStats.mapQueryApproachAvailable;
    report.collapsedPendingRequests +=
        report.massAttackCollapsedRequests;
    report.equivalentStartBucketCollapses +=
        massAttackStats.equivalentStartBucketCollapses;
    report.massAttackPathBurstValidated =
        report.massAttackBurstRequests == MassAttackBurstRequests
        && report.massAttackPendingAfterSubmit <= 8
        && report.massAttackCollapsedRequests >= 80
        && report.moveBurstGoalSlotsUsed >= 2
        && report.moveBurstFallbackCandidates >= 4
        && report.massAttackGoalSlotsUsed >= 2
        && report.massAttackFallbackCandidates >= 4
        && report.moveBurstCollapsedRequests >= 40
        && report.moveBurstSharedApproachHits >= 2
        && massAttackStats.sharedApproachCacheHits >= 2
        && massAttackStats.sharedApproachCacheMisses >= 2
        && massAttackStats.mapQueryApproachRequests >= 4
        && massAttackStats.mapQueryApproachUsedResults >= 4
        && std::all_of(
            massAttackQueue.getResults().begin(),
            massAttackQueue.getResults().end(),
            [](const PathResult& result) {
                return result.completed
                    && result.reachable
                    && result.goalAdjusted
                    && result.mapQueryApproachUsed
                    && !result.fallbackRing
                    && !result.searchBudgetExhausted;
            }
        );

    std::vector<PathTerrainCell> multiGoalCells;
    multiGoalCells.reserve(25U * 25U);
    for (int y = 0; y < 25; ++y) {
        for (int x = 0; x < 25; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            const bool sealedNearGoalRing =
                x >= 10 && x <= 12
                && y >= 11 && y <= 13
                && !(x == 11 && y == 12);
            cell.passable = !sealedNearGoalRing;
            multiGoalCells.push_back(cell);
        }
    }
    PathApproachContext multiGoalContext;
    multiGoalContext.mapQueryApproachAvailable = true;
    multiGoalContext.targetFootprintHash = 0x18180001ull;
    multiGoalContext.approachRingHash = 0x18180002ull;
    multiGoalContext.requesterStableHash = 0x18180003ull;
    multiGoalContext.approachCells = {{11, 12}, {0, 24}};
    PathRequestQueue multiGoalQueue(multiGoalCells);
    const PathRequestHandle multiGoalHandle = multiGoalQueue.submit(
        {0, 12},
        {11, 12},
        multiGoalContext
    );
    multiGoalQueue.pumpWithBudget(1, 64);
    for (const PathResult& result : multiGoalQueue.getResults()) {
        if (result.requestId == multiGoalHandle.id) {
            report.multiGoalApproachValidated =
                result.reachable
                && result.status == PathResultStatus::Reached
                && result.mapQueryApproachUsed
                && sameCell(result.resolvedGoal, PathCell{0, 24})
                && result.expandedNodes <= 64;
            break;
        }
    }

    std::vector<PathTerrainCell> noPathCells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = x != 1;
            noPathCells.push_back(cell);
        }
    }
    PathApproachContext noPathContext;
    noPathContext.requesterStableHash = 0x1515151515151515ull;
    noPathContext.noPathCooldownTicks = 96;
    PathRequestQueue noPathQueue(noPathCells);
    constexpr int RepeatedNoPathSubmissions = 50;
    for (int index = 0; index < RepeatedNoPathSubmissions; ++index) {
        noPathQueue.submit({0, 1}, {2, 1}, noPathContext);
        noPathQueue.pump(1);
    }
    const PathRequestQueueLiveStats noPathStats = noPathQueue.liveStats();
    report.repeatedFailureSuppressed =
        noPathStats.repeatedFailureSuppressed;
    report.noPathCooldownHits =
        noPathStats.noPathCooldownHits;
    report.cooldownNoPathResults =
        noPathStats.cooldownNoPathResults;
    report.realNoPathResults =
        noPathStats.realNoPathResults;
    report.repeatedNoPathSubmissions = RepeatedNoPathSubmissions;
    for (const PathResult& result : noPathQueue.getResults()) {
        report.repeatedNoPathExpandedNodes += result.expandedNodes;
    }
    if (!noPathQueue.getResults().empty()) {
        report.repeatedNoPathFirstExpandedNodes =
            noPathQueue.getResults().front().expandedNodes;
    }
    report.repeatedFailureCooldownValidated =
        noPathQueue.getResults().size()
            == static_cast<std::size_t>(RepeatedNoPathSubmissions)
        && !noPathQueue.getResults()[0].reachable
        && !noPathQueue.getResults()[0].cacheHit
        && noPathQueue.getResults()[0].status == PathResultStatus::RealNoPath
        && std::all_of(
            noPathQueue.getResults().begin() + 1,
            noPathQueue.getResults().end(),
            [](const PathResult& result) {
                return !result.reachable
                    && result.cacheHit
                    && result.status == PathResultStatus::CooldownNoPath
                    && result.expandedNodes == 0;
            }
        )
        && report.repeatedFailureSuppressed >= RepeatedNoPathSubmissions - 1
        && report.noPathCooldownHits >= RepeatedNoPathSubmissions - 1
        && report.cooldownNoPathResults >= RepeatedNoPathSubmissions - 1
        && report.realNoPathResults == 1;
    report.repeatedFailureNonLinearValidated =
        report.repeatedFailureCooldownValidated
        && report.repeatedNoPathExpandedNodes
            <= report.repeatedNoPathFirstExpandedNodes + 2;

    PathRequestQueue isolatedNoPathQueue(noPathCells);
    const PathRequestHandle isolatedNoPathHandle = isolatedNoPathQueue.submit(
        {0, 1},
        {2, 1},
        noPathContext
    );
    isolatedNoPathQueue.pump(1);
    for (const PathResult& result : isolatedNoPathQueue.getResults()) {
        if (result.requestId == isolatedNoPathHandle.id) {
            report.noPathCacheIsolationValidated =
                !result.reachable
                && !result.cacheHit
                && result.status == PathResultStatus::RealNoPath
                && result.expandedNodes > 0;
            break;
        }
    }

    PathRequestQueue budgetQueue(formationCells);
    budgetQueue.submit({0, 1}, {4, 0});
    budgetQueue.submit({0, 1}, {4, 1});
    budgetQueue.submit({0, 1}, {4, 2});
    const int budgetProcessed = budgetQueue.pumpWithBudget(4, 0);
    report.lastPumpExpandedNodes = budgetQueue.lastPumpExpandedNodeCount();
    report.pendingAfterBudgetPump = budgetQueue.pendingCount();
    report.budgetExhaustedResults += budgetQueue.budgetExhaustedResultCount();
    report.globalBudgetValidated =
        budgetProcessed == 0
        && budgetQueue.lastPumpBudgetExhausted()
        && report.lastPumpExpandedNodes == 0
        && report.pendingAfterBudgetPump == 3;
    report.pathBurstPendingAfterBudget = report.pendingAfterBudgetPump;
    report.pathBurstBudgetExhaustedResults =
        budgetQueue.lastPumpBudgetExhausted() ? 1 : 0;
    report.backpressureValidated =
        report.globalBudgetValidated
        && budgetQueue.backpressureRejectedRequestCount() == 0;

    std::vector<PathTerrainCell> overflowCells;
    overflowCells.reserve(1089U);
    for (int y = 0; y < 33; ++y) {
        for (int x = 0; x < 33; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            cell.passable = true;
            overflowCells.push_back(cell);
        }
    }
    PathRequestQueue overflowQueue(overflowCells);
    int overflowRejectedHandles = 0;
    for (int y = 0; y < 33; ++y) {
        for (int x = 0; x < 33; ++x) {
            const PathRequestHandle handle =
                overflowQueue.submit({0, 0}, {x, y});
            if (handle.id < 0) {
                ++overflowRejectedHandles;
            }
        }
    }
    const PathRequestQueueLiveStats overflowStats = overflowQueue.liveStats();
    report.overflowRejectedResults =
        static_cast<int>(std::count_if(
            overflowQueue.getResults().begin(),
            overflowQueue.getResults().end(),
            [](const PathResult& result) {
                return result.status == PathResultStatus::OverflowRejected
                    && result.completed
                    && result.queueBackpressure
                    && !result.reachable
                    && result.expandedNodes == 0
                    && result.requestClass.rfind("rq:", 0) == 0;
            }
        ));
    report.requestOverflowValidated =
        overflowRejectedHandles > 0
        && overflowStats.requestOverflowRejectedRequests == overflowRejectedHandles
        && overflowStats.overflowRejectedResults == overflowRejectedHandles
        && report.overflowRejectedResults == overflowRejectedHandles
        && overflowQueue.requestOverflowRejectedRequestCount()
            == overflowRejectedHandles
        && overflowStats.backpressureRejectedRequests == overflowRejectedHandles
        && overflowStats.pendingCount == 1024;
    report.pathBurstOverflowRejectedRequests =
        overflowStats.requestOverflowRejectedRequests;

    PathRequestQueue idleQueue(formationCells);
    report.idlePumpProcessed = idleQueue.pumpWithBudget(8, 128);
    report.idlePumpExpandedNodes = idleQueue.lastPumpExpandedNodeCount();
    report.idlePumpNoSpinValidated =
        report.idlePumpProcessed == 0
        && report.idlePumpExpandedNodes == 0
        && !idleQueue.lastPumpBudgetExhausted()
        && idleQueue.pendingCount() == 0;

    report.expandedNodeBudgetValidated =
        blockedPendingCollapsed
        && std::all_of(
            multiUnitDuplicateQueue.getResults().begin(),
            multiUnitDuplicateQueue.getResults().end(),
            [](const PathResult& result) {
                return result.expandedNodeBudget > 0
                    && result.expandedNodes <= result.expandedNodeBudget
                    && !result.searchBudgetExhausted;
            }
        );

    report.success = report.requestsCompleted == report.requestsSubmitted
        && report.reachablePaths == report.requestsSubmitted
        && report.cacheValidated
        && report.cornerCutBlocked
        && report.rampTransitionAllowed
        && report.waterShoreRulesValidated
        && report.navalLandBlocked
        && report.amphibiousTransitionAllowed
        && report.aircraftTerrainIgnored
        && report.bridgeTransitionAllowed
        && report.blockedGoalFallbackValidated
        && report.footprintFallbackValidated
        && report.mapQueryApproachUsedResults > 0
        && report.occupiedStartEscapeValidated
        && report.duplicateRequestsCollapsed
        && report.blockedTargetCacheValidated
        && report.multiUnitDuplicateRequestsCollapsed
        && report.equivalentStartBucketCollapses > 0
        && report.fallbackTargetCacheValidated
        && report.expandedNodeBudgetValidated
        && report.globalBudgetValidated
        && report.backpressureValidated
        && report.requestOverflowValidated
        && report.massAttackPathBurstValidated
        && report.multiGoalApproachValidated
        && report.repeatedFailureCooldownValidated
        && report.repeatedFailureNonLinearValidated
        && report.noPathCacheIsolationValidated
        && report.idlePumpNoSpinValidated
        && report.hazardAvoidanceValidated
        && report.formationRequestsCompleted;
    if (!report.success) {
        report.errorMessage =
            "Path request queue did not complete/cache or enforce terrain constraints.";
    }
    return report;
}

void printPathfindingSandboxReport(
    std::ostream& output,
    const PathfindingSandboxReport& report
) {
    output << "\n========== Pathfinding Sandbox ==========\n";
    output << "Success:            " << (report.success ? "yes" : "no") << "\n";
    if (!report.success && !report.errorMessage.empty()) {
        output << "Reason:             " << report.errorMessage << "\n";
    }
    output << "Terrain cells:      " << report.terrainCells << "\n";
    output << "Requests submitted: " << report.requestsSubmitted << "\n";
    output << "Requests completed: " << report.requestsCompleted << "\n";
    output << "Reachable paths:    " << report.reachablePaths << "\n";
    output << "Cache:             hit="
           << report.cacheHits
           << " miss="
           << report.cacheMisses
           << "\n";
    output << "Collapsed pending:  " << report.collapsedPendingRequests << "\n";
    output << "Move burst:         requests="
           << report.moveBurstRequests
           << " collapsed="
           << report.moveBurstCollapsedRequests
           << " sharedApproachHits="
           << report.moveBurstSharedApproachHits
           << " goalSlots="
           << report.moveBurstGoalSlotsUsed
           << " fallbackCandidates="
           << report.moveBurstFallbackCandidates
           << "\n";
    output << "Mass attack collapse: "
           << report.massAttackCollapsedRequests
           << " burstRequests="
           << report.massAttackBurstRequests
           << " pendingAfterSubmit="
           << report.massAttackPendingAfterSubmit
           << " goalSlots="
           << report.massAttackGoalSlotsUsed
           << " fallbackCandidates="
           << report.massAttackFallbackCandidates
           << " expandedNodes="
           << report.massAttackExpandedNodes
           << "\n";
    output << "Start bucket merge: "
           << report.equivalentStartBucketCollapses
           << "\n";
    output << "Shared approach cache: hit="
           << report.sharedApproachCacheHits
           << " miss="
           << report.sharedApproachCacheMisses
           << "\n";
    output << "No-path cooldown:  suppressed="
           << report.repeatedFailureSuppressed
           << " hits="
           << report.noPathCooldownHits
           << " cooldownResults="
           << report.cooldownNoPathResults
           << " realNoPath="
           << report.realNoPathResults
           << " repeated="
           << report.repeatedNoPathSubmissions
           << " expanded="
           << report.repeatedNoPathExpandedNodes
           << " firstExpanded="
           << report.repeatedNoPathFirstExpandedNodes
           << "\n";
    output << "Fallback cache:     hit="
           << report.fallbackTargetCacheHits
           << " miss="
           << report.fallbackTargetCacheMisses
           << "\n";
    output << "Fallback ring:      requests="
           << report.fallbackRingRequests
           << "\n";
    output << "Approach source:    MapQuery="
           << (report.mapQueryApproachAvailable ? "yes" : "no")
           << " requests="
           << report.mapQueryApproachRequests
           << " used="
           << report.mapQueryApproachUsedResults
           << " blockedFallback="
           << (report.blockedComponentApproachFallbackUsed
                   ? "yes"
                   : "no")
           << "\n";
    output << "Budget pump:        nodes="
           << report.lastPumpExpandedNodes
           << " pending="
           << report.pendingAfterBudgetPump
           << " exhaustedResults="
           << report.budgetExhaustedResults
           << "\n";
    output << "Request overflow:   "
           << (report.requestOverflowValidated ? "validated" : "not validated")
           << "\n";
    output << "Path burst:         mass="
           << (report.massAttackPathBurstValidated ? "validated" : "not validated")
           << " pendingAfterBudget="
           << report.pathBurstPendingAfterBudget
           << " budgetExhausted="
           << report.pathBurstBudgetExhaustedResults
           << " overflowRejected="
           << report.pathBurstOverflowRejectedRequests
           << " overflowResults="
           << report.overflowRejectedResults
           << "\n";
    output << "Idle pump:          "
           << (report.idlePumpNoSpinValidated ? "no-spin" : "spun")
           << " processed="
           << report.idlePumpProcessed
           << " nodes="
           << report.idlePumpExpandedNodes
           << "\n";
    output << "Cache validated:    "
           << (report.cacheValidated ? "yes" : "no")
           << "\n";
    output << "Corner blocked:     "
           << (report.cornerCutBlocked ? "yes" : "no")
           << "\n";
    output << "Ramp transition:    "
           << (report.rampTransitionAllowed ? "yes" : "no")
           << "\n";
    output << "Water/shore rules:  "
           << (report.waterShoreRulesValidated ? "yes" : "no")
           << "\n";
    output << "Movement domains:   navalLand="
           << (report.navalLandBlocked ? "blocked" : "allowed")
           << " amphibious="
           << (report.amphibiousTransitionAllowed ? "allowed" : "blocked")
           << " airTerrain="
           << (report.aircraftTerrainIgnored ? "ignored" : "blocked")
           << "\n";
    output << "Bridge transition:  "
           << (report.bridgeTransitionAllowed ? "yes" : "no")
           << "\n";
    output << "Blocked goal fallback: "
           << (report.blockedGoalFallbackValidated ? "yes" : "no")
           << "\n";
    output << "Footprint fallback: "
           << (report.footprintFallbackValidated ? "yes" : "no")
           << "\n";
    output << "Occupied start escape: "
           << (report.occupiedStartEscapeValidated ? "yes" : "no")
           << "\n";
    output << "Duplicate requests: "
           << (report.duplicateRequestsCollapsed ? "collapsed" : "not collapsed")
           << "\n";
    output << "Blocked target cache: "
           << (report.blockedTargetCacheValidated ? "yes" : "no")
           << "\n";
    output << "Multi-unit duplicates: "
           << (report.multiUnitDuplicateRequestsCollapsed ? "collapsed" : "not collapsed")
           << "\n";
    output << "Fallback target cache: "
           << (report.fallbackTargetCacheValidated ? "yes" : "no")
           << "\n";
    output << "Expanded-node budget: "
           << (report.expandedNodeBudgetValidated ? "yes" : "no")
           << "\n";
    output << "Global budget: "
           << (report.globalBudgetValidated ? "yes" : "no")
           << "\n";
    output << "Backpressure: "
           << (report.backpressureValidated ? "yes" : "no")
           << "\n";
    output << "Request overflow: "
           << (report.requestOverflowValidated ? "yes" : "no")
           << "\n";
    output << "Mass attack path burst: "
           << (report.massAttackPathBurstValidated ? "yes" : "no")
           << "\n";
    output << "Multi-goal approach: "
           << (report.multiGoalApproachValidated ? "yes" : "no")
           << "\n";
    output << "Repeated no-path cooldown: "
           << (report.repeatedFailureCooldownValidated ? "yes" : "no")
           << "\n";
    output << "Repeated no-path non-linear: "
           << (report.repeatedFailureNonLinearValidated ? "yes" : "no")
           << "\n";
    output << "No-path cache isolation: "
           << (report.noPathCacheIsolationValidated ? "yes" : "no")
           << "\n";
    output << "Idle pump no-spin: "
           << (report.idlePumpNoSpinValidated ? "yes" : "no")
           << "\n";
    output << "Hazard avoidance: "
           << (report.hazardAvoidanceValidated ? "yes" : "no")
           << "\n";
    output << "Formation requests: "
           << (report.formationRequestsCompleted ? "yes" : "no")
           << "\n";

    for (const PathResult& result : report.results) {
        output << "- request " << result.requestId
               << " status=" << pathResultStatusToken(result.status)
               << " " << result.start.x << "," << result.start.y
               << " -> " << result.goal.x << "," << result.goal.y
               << " resolved=" << result.resolvedGoal.x << "," << result.resolvedGoal.y
               << " reachable=" << (result.reachable ? "yes" : "no")
               << " adjusted=" << (result.goalAdjusted ? "yes" : "no")
               << " cache=" << (result.cacheHit ? "yes" : "no")
               << "/" << (result.cacheMiss ? "miss" : "hit")
               << " fallbackCache=" << (result.fallbackTargetCacheHit ? "yes" : "no")
               << " fallbackRing=" << (result.fallbackRing ? "yes" : "no")
               << " mapQueryApproach="
               << (result.mapQueryApproachUsed ? "yes" : "no")
               << " sharedApproachCache="
               << (result.sharedApproachCacheHit ? "yes" : "no")
               << " nodes=" << result.expandedNodes
               << "/" << result.expandedNodeBudget
               << " fallbackTried=" << result.fallbackTargetsTried
               << " candidates=" << result.fallbackCandidateCount
               << " goalSlot=" << result.goalSlotIndex
               << " cooldownTicks=" << result.noPathCooldownTicksRemaining
               << " budgetExhausted=" << (result.searchBudgetExhausted ? "yes" : "no")
               << " backpressure=" << (result.queueBackpressure ? "yes" : "no")
               << " length=" << result.path.size()
               << "\n";
        if (!result.reachable && !result.failureReason.empty()) {
            output << "  reason=" << result.failureReason << "\n";
        }
    }
}
