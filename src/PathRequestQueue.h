#pragma once

#include "GameWorld.h"
#include "RuntimePathQueueTerrainStorage.h"
#include "RuntimePathQueueLayoutStorage.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

struct PathCell {
    int x = -1;
    int y = -1;
};

struct PathRequestHandle {
    int id = -1;
};

enum class PathMovementMode {
    Ground,
    Naval,
    Amphibious,
    Air
};

struct PathApproachContext {
    bool mapQueryApproachAvailable = false;
    std::uint64_t targetFootprintHash = 0;
    std::uint64_t approachRingHash = 0;
    std::uint64_t movementConstraintHash = 0;
    std::uint64_t requesterStableHash = 0;
    int noPathCooldownTicks = 64;
    int goalSlotCount = 0;
    bool shareAcrossGoalSlots = false;
    PathMovementMode movementMode = PathMovementMode::Ground;
    std::vector<PathCell> approachCells;
    std::vector<PathCell> alternateGoalCells;
};

struct PathTerrainCell {
    PathCell cell;
    int level = 0;
    bool passable = true;
    bool water = false;
    bool shore = false;
    bool bridge = false;
    bool ramp = false;
    bool cliff = false;
    bool resource = false;
    int cost = 10;
    int hazardCost = 0;
};

enum class PathResultStatus {
    Queued,
    Pending,
    Reached,
    CooldownNoPath,
    BudgetExhausted,
    RealNoPath,
    OverflowRejected
};

struct PathResult {
    int requestId = -1;
    PathCell start;
    PathCell goal;
    PathCell resolvedGoal;
    bool completed = false;
    bool reachable = false;
    bool cacheHit = false;
    bool cacheMiss = false;
    bool goalAdjusted = false;
    bool searchBudgetExhausted = false;
    bool queueBackpressure = false;
    bool fallbackTargetCacheHit = false;
    bool fallbackRing = false;
    bool mapQueryApproachUsed = false;
    bool sharedApproachCacheHit = false;
    bool collapsedFromEquivalentRequest = false;
    PathResultStatus status = PathResultStatus::Pending;
    std::string passabilityProfileHash;
    std::string requestClass;
    int expandedNodes = 0;
    int expandedNodeBudget = 0;
    int fallbackTargetsTried = 0;
    int fallbackCandidateCount = 0;
    int goalSlotIndex = 0;
    int noPathCooldownTicksRemaining = 0;
    std::string failureReason;
    std::vector<PathCell> path;
};

struct PathfindingSandboxReport {
    bool success = false;
    std::string errorMessage;
    int terrainCells = 0;
    int requestsSubmitted = 0;
    int requestsCompleted = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int reachablePaths = 0;
    bool cacheValidated = false;
    bool cornerCutBlocked = false;
    bool rampTransitionAllowed = false;
    bool waterShoreRulesValidated = false;
    bool navalLandBlocked = false;
    bool amphibiousTransitionAllowed = false;
    bool aircraftTerrainIgnored = false;
    bool bridgeTransitionAllowed = false;
    bool blockedGoalFallbackValidated = false;
    bool footprintFallbackValidated = false;
    bool occupiedStartEscapeValidated = false;
    bool duplicateRequestsCollapsed = false;
    bool blockedTargetCacheValidated = false;
    bool multiUnitDuplicateRequestsCollapsed = false;
    bool fallbackTargetCacheValidated = false;
    bool expandedNodeBudgetValidated = false;
    bool globalBudgetValidated = false;
    bool backpressureValidated = false;
    bool requestOverflowValidated = false;
    bool massAttackPathBurstValidated = false;
    bool multiGoalApproachValidated = false;
    bool repeatedFailureCooldownValidated = false;
    bool repeatedFailureNonLinearValidated = false;
    bool noPathCacheIsolationValidated = false;
    bool idlePumpNoSpinValidated = false;
    bool hazardAvoidanceValidated = false;
    int collapsedPendingRequests = 0;
    int moveBurstRequests = 0;
    int moveBurstCollapsedRequests = 0;
    int moveBurstSharedApproachHits = 0;
    int moveBurstGoalSlotsUsed = 0;
    int moveBurstFallbackCandidates = 0;
    int massAttackBurstRequests = 0;
    int massAttackPendingAfterSubmit = 0;
    int massAttackCollapsedRequests = 0;
    int massAttackGoalSlotsUsed = 0;
    int massAttackFallbackCandidates = 0;
    int massAttackExpandedNodes = 0;
    int sharedApproachCacheHits = 0;
    int sharedApproachCacheMisses = 0;
    int repeatedFailureSuppressed = 0;
    int noPathCooldownHits = 0;
    int cooldownNoPathResults = 0;
    int realNoPathResults = 0;
    int repeatedNoPathSubmissions = 0;
    int repeatedNoPathExpandedNodes = 0;
    int repeatedNoPathFirstExpandedNodes = 0;
    int fallbackTargetCacheHits = 0;
    int fallbackTargetCacheMisses = 0;
    int mapQueryApproachRequests = 0;
    int mapQueryApproachUsedResults = 0;
    int fallbackRingRequests = 0;
    int budgetExhaustedResults = 0;
    int equivalentStartBucketCollapses = 0;
    int lastPumpExpandedNodes = 0;
    int pendingAfterBudgetPump = 0;
    int pathBurstPendingAfterBudget = 0;
    int pathBurstBudgetExhaustedResults = 0;
    int pathBurstOverflowRejectedRequests = 0;
    int overflowRejectedResults = 0;
    int idlePumpProcessed = 0;
    int idlePumpExpandedNodes = 0;
    bool mapQueryApproachAvailable = false;
    bool blockedComponentApproachFallbackUsed = false;
    bool formationRequestsCompleted = false;
    std::vector<PathResult> results;
};

struct PathRequestQueueLiveStats {
    int pendingCount = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int budgetExhaustedResults = 0;
    bool lastPumpBudgetExhausted = false;
    int backpressureRejectedRequests = 0;
    int requestOverflowRejectedRequests = 0;
    int overflowRejectedResults = 0;
    int collapsedPendingRequests = 0;
    int massAttackCollapsedRequests = 0;
    int sharedApproachCacheHits = 0;
    int sharedApproachCacheMisses = 0;
    int repeatedFailureSuppressed = 0;
    int noPathCooldownHits = 0;
    int cooldownNoPathResults = 0;
    int realNoPathResults = 0;
    int equivalentStartBucketCollapses = 0;
    int goalSlotAssignments = 0;
    int fallbackCandidatesConsidered = 0;
    int fallbackRingRequests = 0;
    int fallbackTargetCacheHits = 0;
    int fallbackTargetCacheMisses = 0;
    int mapQueryApproachRequests = 0;
    int mapQueryApproachUsedResults = 0;
    int lastPumpExpandedNodes = 0;
    int cachedPathCount = 0;
    int fallbackTargetCacheEntries = 0;
    // Diagnostic work counters; never participate in budgets or path choices.
    std::uint64_t fallbackDistanceQueries = 0;
    std::uint64_t fallbackComponentCellChecks = 0;
    std::uint64_t heuristicEvaluations = 0;
    bool usesDenseTerrainLookup = false;
    bool usesShortHashKeys = true;
    bool mapQueryApproachAvailable = false;
    bool usesBlockedComponentApproachFallback = false;
};

// One bounded, caller-owned coordinate layout. It contains no passability,
// request, path or cooldown state. Ordinary queues copy its indices; an admitted
// tagged queue retains an immutable shared owner. Queue and cache copies own
// independent data, so clearing/swapping/rebuilding a cache cannot invalidate
// an already constructed queue. Terrain values retain their separate contract.
class PathRequestQueueTerrainLayoutCache {
public:
    // Fits the largest runtime rectangular fixture (480 squared) and native
    // 256-cell diamond. Larger library inputs keep the original constructor.
    static constexpr std::size_t maxRetainedCells = 262144;
    void clear() noexcept;
    void swap(PathRequestQueueTerrainLayoutCache& other) noexcept;
    friend void swap(PathRequestQueueTerrainLayoutCache& left,
                     PathRequestQueueTerrainLayoutCache& right) noexcept {
        left.swap(right);
    }
    bool lastConstructionReused() const noexcept { return lastConstructionReused_; }
    std::size_t retainedCellCount() const noexcept { return sourceCoordinates_.size(); }
    bool layoutIsSharedForDiagnostics() const noexcept { return layout_.isShared(); }
    const void* sharedLayoutIdentityForDiagnostics() const noexcept {
        return layout_.sharedIdentityForDiagnostics();
    }

private:
    friend class PathRequestQueue;
    bool valid_ = false;
    bool referenceLookup_ = false;
    bool lastConstructionReused_ = false;
    std::vector<PathCell> sourceCoordinates_;
    std::vector<int> sourceNodeIndices_;
    RuntimePathQueueLayoutStorage layout_;
};

class PathRequestQueue {
public:
    explicit PathRequestQueue(const std::vector<TerrainCell>& terrainCells);
    // The optional reference lookup is used only by equivalence diagnostics.
    explicit PathRequestQueue(
        const std::vector<PathTerrainCell>& terrainCells,
        bool forceReferenceLookup = false
    );
    // Identity is the complete authored coordinate sequence, including all
    // duplicates. Dynamic fields are merged and fingerprinted afresh.
    // The two reference switches intentionally have this explicit order.
    PathRequestQueue(
        const std::vector<PathTerrainCell>& terrainCells,
        PathRequestQueueTerrainLayoutCache& layoutCache,
        bool forceReferenceLayout = false,
        bool forceReferenceLookup = false
    );
    // Runtime-only lexical opt-in. A cold/changed layout, duplicates, raw cost
    // normalization or either old lookup/layout reference uses the owned path.
    // Every constructor still checks the complete authored coordinates and
    // recomputes the complete numeric profile. No world/epoch pointer cache.
    PathRequestQueue(
        RuntimeReadOnlyPathTerrainTag,
        const std::vector<PathTerrainCell>& terrainCells,
        PathRequestQueueTerrainLayoutCache& layoutCache,
        bool forceReferenceLayout = false,
        bool forceReferenceLookup = false
    );
    PathRequestQueue(RuntimeReadOnlyPathTerrainTag, std::vector<PathTerrainCell>&&,
        PathRequestQueueTerrainLayoutCache&, bool = false, bool = false) = delete;
    PathRequestQueue(RuntimeReadOnlyPathTerrainTag, const std::vector<PathTerrainCell>&&,
        PathRequestQueueTerrainLayoutCache&, bool = false, bool = false) = delete;
    bool terrainIsBorrowedForDiagnostics() const noexcept { return terrain_.isBorrowed(); }
    bool layoutEqualsForDiagnostics(const PathRequestQueue& other) const {
        return layout_.read().sameMapping(other.layout_.read());
    }
    bool layoutIsSharedForDiagnostics() const noexcept { return layout_.isShared(); }
    const void* sharedLayoutIdentityForDiagnostics() const noexcept {
        return layout_.sharedIdentityForDiagnostics();
    }
    std::size_t ownedTerrainBytesForDiagnostics() const noexcept {
        return terrain_.ownedCapacity() * sizeof(PathTerrainCell);
    }

    PathRequestHandle submit(PathCell start, PathCell goal);
    PathRequestHandle submit(
        PathCell start,
        PathCell goal,
        const PathApproachContext& approachContext
    );
    int pump(int maxRequests);
    int pumpWithBudget(int maxRequests, int expandedNodeBudget);

    const std::vector<PathResult>& getResults() const;
    int pendingCount() const;
    int collapsedPendingRequestCount() const;
    int fallbackTargetCacheHitCount() const;
    int fallbackTargetCacheMissCount() const;
    int mapQueryApproachRequestCount() const;
    int mapQueryApproachUsedResultCount() const;
    int cacheHitCount() const;
    int cacheMissCount() const;
    int budgetExhaustedResultCount() const;
    int fallbackRingRequestCount() const;
    int equivalentStartBucketCollapseCount() const;
    int lastPumpExpandedNodeCount() const;
    bool lastPumpBudgetExhausted() const;
    int backpressureRejectedRequestCount() const;
    int requestOverflowRejectedRequestCount() const;
    int massAttackCollapsedRequestCount() const;
    int sharedApproachCacheHitCount() const;
    int sharedApproachCacheMissCount() const;
    PathRequestQueueLiveStats liveStats() const;

private:
    // One legal object type for owned and borrowed storage. resource remains
    // ignored by path behavior and by the unchanged ten-field numeric hash.
    using TerrainNode = PathTerrainCell;

    struct BlockedComponent {
        bool valid = false;
        bool fillsBounds = false;
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
        std::vector<PathCell> cells;
    };

    struct PendingRequest {
        int id = -1;
        PathCell start;
        PathCell goal;
        std::uint64_t collapseKey = 0;
        std::uint64_t sharedApproachKey = 0;
        std::uint64_t approachContextHash = 0;
        std::uint64_t failureCacheKey = 0;
        int noPathCooldownTicks = 64;
        int goalSlotIndex = 0;
        int goalSlotCount = 0;
        bool shareAcrossGoalSlots = false;
        bool mapQueryApproach = false;
        bool equivalentApproachBurst = false;
        PathMovementMode movementMode = PathMovementMode::Ground;
        std::vector<PathCell> approachCells;
        std::vector<PathCell> alternateGoalCells;
    };

    struct NoPathFailureCacheEntry {
        std::uint64_t key = 0;
        PathCell resolvedGoal;
        bool goalAdjusted = false;
        bool fallbackRing = false;
        bool mapQueryApproachUsed = false;
        int fallbackTargetsTried = 0;
        int fallbackCandidateCount = 0;
        int goalSlotIndex = 0;
        int expiresAtTick = 0;
        std::string failureReason;
    };

    struct SearchNode {
        std::uint32_t generation = 0;
        std::uint32_t closedGeneration = 0;
        std::uint32_t goalGeneration = 0;
        int g = 0;
        int h = 0;
        int parent = -1;
    };

    int nextRequestId_ = 1;
    std::deque<PendingRequest> pending_;
    // IDs are contiguous within the FIFO: rejected/collapsed submissions do
    // not consume an ID. Store IDs instead of deque pointers so queue copies
    // retain independent, valid lookups after either queue is pumped.
    std::unordered_map<std::uint64_t, int> pendingRequestIds_;
    std::vector<PathResult> results_;
    RuntimePathQueueTerrainStorage<TerrainNode> terrain_;
    // Both views retain the original sorted keys and authored node indices.
    // Reads use one stable view; ordinary and copied queues own their layout.
    RuntimePathQueueLayoutStorage layout_;
    // Terrain is immutable for this queue. Cache exact goal cells because
    // the component's radius limit makes nearby goals non-interchangeable.
    mutable std::unordered_map<long long, BlockedComponent> blockedComponents_;
    mutable std::deque<long long> blockedComponentOrder_;
    std::vector<SearchNode> searchNodes_;
    std::uint32_t searchGeneration_ = 0;
    std::vector<std::pair<std::uint64_t, PathResult>> cache_;
    std::vector<std::pair<std::uint64_t, PathResult>> sharedApproachCache_;
    std::vector<std::pair<std::uint64_t, std::vector<PathCell>>> fallbackTargetCache_;
    std::vector<NoPathFailureCacheEntry> noPathFailureCache_;
    std::uint64_t passabilityProfileHash_ = 0;
    int collapsedPendingRequests_ = 0;
    int equivalentStartBucketCollapses_ = 0;
    int cacheHits_ = 0;
    int cacheMisses_ = 0;
    int budgetExhaustedResults_ = 0;
    int fallbackRingRequests_ = 0;
    int fallbackTargetCacheHits_ = 0;
    int fallbackTargetCacheMisses_ = 0;
    int mapQueryApproachRequests_ = 0;
    int mapQueryApproachUsedResults_ = 0;
    int lastPumpExpandedNodes_ = 0;
    bool lastPumpBudgetExhausted_ = false;
    int backpressureRejectedRequests_ = 0;
    int requestOverflowRejectedRequests_ = 0;
    int massAttackCollapsedRequests_ = 0;
    int sharedApproachCacheHits_ = 0;
    int sharedApproachCacheMisses_ = 0;
    int repeatedFailureSuppressed_ = 0;
    int noPathCooldownHits_ = 0;
    int cooldownNoPathResults_ = 0;
    int realNoPathResults_ = 0;
    int goalSlotAssignments_ = 0;
    int fallbackCandidatesConsidered_ = 0;
    std::uint64_t fallbackDistanceQueries_ = 0;
    std::uint64_t fallbackComponentCellChecks_ = 0;
    std::uint64_t heuristicEvaluations_ = 0;
    int logicalTick_ = 0;
    bool mapQueryApproachAvailable_ = false;
    bool blockedComponentApproachFallbackUsed_ = false;

    void initializeTerrain(
        const std::vector<PathTerrainCell>& terrainCells,
        bool forceReferenceLookup,
        PathRequestQueueTerrainLayoutCache* layoutCache,
        bool requestReadOnlyBorrow = false
    );
    int terrainIndexForKey(long long key) const;
    int denseTerrainOffsetForKey(long long key) const;
    const TerrainNode* terrainNodeAt(PathCell cell) const;
    const BlockedComponent& blockedComponentAt(PathCell goal) const;
    std::vector<int> terrainIndicesInBounds(
        std::int64_t minX, std::int64_t minY,
        std::int64_t maxX, std::int64_t maxY
    ) const;
    std::uint64_t cacheKeyForRequest(
        PathCell start,
        PathCell goal,
        std::uint64_t approachContextHash,
        int goalSlotIndex
    ) const;
    std::uint64_t pendingKeyForRequest(
        PathCell start,
        PathCell goal,
        std::uint64_t approachContextHash,
        int goalSlotIndex
    ) const;
    std::uint64_t sharedApproachKeyForRequest(
        PathCell goal,
        std::uint64_t approachContextHash,
        int goalSlotIndex,
        bool shareAcrossGoalSlots
    ) const;
    std::uint64_t fallbackTargetCacheKey(
        PathCell start,
        PathCell goal,
        const BlockedComponent& component
    ) const;
    std::uint64_t failureCacheKeyForRequest(
        PathCell start,
        PathCell goal,
        std::uint64_t approachContextHash,
        std::uint64_t requesterStableHash
    ) const;
    const NoPathFailureCacheEntry* findNoPathFailureCacheEntry(
        std::uint64_t key,
        int currentTick
    );
    void rememberNoPathFailure(
        std::uint64_t key,
        const PathResult& result,
        int currentTick,
        int cooldownTicks
    );
    std::vector<PathCell> reconstructPath(
        int currentIndex
    ) const;
    PathResult solve(const PendingRequest& request, int expandedNodeBudget);
};

PathfindingSandboxReport runPathfindingSandbox(const GameWorld& world);
void printPathfindingSandboxReport(
    std::ostream& output,
    const PathfindingSandboxReport& report
);
