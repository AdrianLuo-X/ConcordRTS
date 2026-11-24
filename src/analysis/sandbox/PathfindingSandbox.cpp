#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include "simulation/navigation/pathfinding/detail/PathfindingCellHelpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using concord::simulation::pathfinding::detail::cellKey;
using concord::simulation::pathfinding::detail::sameCell;

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

} // namespace

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
