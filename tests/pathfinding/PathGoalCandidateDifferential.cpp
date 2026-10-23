#include "simulation/navigation/pathfinding/PathRequestQueue.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

void recordPathGoalCandidatesForTest(int id, int phase, const std::vector<PathCell>& cells) {
    std::cout << "candidates " << id << ' ' << phase << ' ' << cells.size();
    for (auto c : cells) std::cout << ' ' << c.x << ',' << c.y;
    std::cout << '\n';
}
namespace {
void result(const PathResult& r) {
    std::cout << "result";
#define F(name) std::cout << " " #name "=" << r.name
    F(requestId); F(completed); F(reachable); F(cacheHit); F(cacheMiss);
    F(goalAdjusted); F(searchBudgetExhausted); F(queueBackpressure);
    F(fallbackTargetCacheHit); F(fallbackRing); F(mapQueryApproachUsed);
    F(sharedApproachCacheHit); F(collapsedFromEquivalentRequest);
    std::cout << " status=" << static_cast<int>(r.status);
    F(passabilityProfileHash); F(requestClass); F(expandedNodes); F(expandedNodeBudget);
    F(fallbackTargetsTried); F(fallbackCandidateCount); F(goalSlotIndex);
    F(noPathCooldownTicksRemaining); F(failureReason);
#undef F
    std::cout << " start=" << r.start.x << ',' << r.start.y
        << " goal=" << r.goal.x << ',' << r.goal.y
        << " resolved=" << r.resolvedGoal.x << ',' << r.resolvedGoal.y
        << " path=" << r.path.size();
    for (auto c : r.path) std::cout << ' ' << c.x << ',' << c.y;
    std::cout << '\n';
}
void state(const PathRequestQueue& q) {
    const auto s=q.liveStats(); std::cout << "stats";
#define F(name) std::cout << " " #name "=" << s.name
    F(pendingCount); F(cacheHits); F(cacheMisses); F(budgetExhaustedResults);
    F(lastPumpBudgetExhausted); F(backpressureRejectedRequests); F(requestOverflowRejectedRequests);
    F(overflowRejectedResults); F(collapsedPendingRequests); F(massAttackCollapsedRequests);
    F(sharedApproachCacheHits); F(sharedApproachCacheMisses); F(repeatedFailureSuppressed);
    F(noPathCooldownHits); F(cooldownNoPathResults); F(realNoPathResults);
    F(equivalentStartBucketCollapses); F(goalSlotAssignments); F(fallbackCandidatesConsidered);
    F(fallbackRingRequests); F(fallbackTargetCacheHits); F(fallbackTargetCacheMisses);
    F(mapQueryApproachRequests); F(mapQueryApproachUsedResults); F(lastPumpExpandedNodes);
    F(cachedPathCount); F(fallbackTargetCacheEntries); F(fallbackDistanceQueries);
    F(fallbackComponentCellChecks); F(heuristicEvaluations); F(usesDenseTerrainLookup);
    F(usesShortHashKeys); F(mapQueryApproachAvailable); F(usesBlockedComponentApproachFallback);
#undef F
    std::cout << '\n'; for (const auto& r:q.getResults()) result(r);
}
}
int main() {
    std::cout << "abi " << sizeof(PathCell) << ' ' << sizeof(PathTerrainCell)
        << ' ' << sizeof(PathRequestQueue) << ' ' << sizeof(PathResult) << '\n';
    constexpr int budgets[]={1,32,256,16384};
    int cases=0;
    for(int scenario=0;scenario<16;++scenario) for(int mode=0;mode<4;++mode)
    for(int reverse=0;reverse<2;++reverse) for(int reference=0;reference<2;++reference)
    for(int budget:budgets) {
        std::vector<PathTerrainCell> cells;
        for(int y=0;y<20;++y) for(int x=0;x<20;++x) {
            PathTerrainCell c; c.cell={x,y};
            if(scenario==1) c.passable=!(x==15&&y==15);
            if(scenario==2) c.passable=!(x>=12&&x<=16&&y>=12&&y<=16);
            if(scenario==3) c.passable=!((x==15&&y>=10)||(y==15&&x>=10));
            if(scenario==6) c.passable=false;
            if(scenario==10||scenario==11){c.water=true;c.shore=x%7==0;c.bridge=(y==15&&x>=13);}
            if(scenario==12) c.passable=x!=9;
            if(scenario==13){c.hazardCost=x==8?100:0;c.level=x/6;c.ramp=y==5;c.cliff=(x==15&&y==15);}
            if(scenario==14&&(x+y)%5==0) continue;
            cells.push_back(c);
        }
        if(scenario==5) cells.clear();
        if(scenario==15){auto c=cells[55];c.passable=false;c.cost=-2;cells.push_back(c);cells.push_back(c);}
        if(reverse) std::reverse(cells.begin(),cells.end());
        PathRequestQueue q(cells,reference!=0);
        PathCell start{1,1},goal{15,15}; if(scenario==4) goal={21,21};
        PathApproachContext context; context.movementMode=static_cast<PathMovementMode>(mode);
        context.noPathCooldownTicks=3;context.goalSlotCount=scenario%3==0?4:0;
        context.shareAcrossGoalSlots=scenario%2==0;
        if(scenario==7||scenario==9){context.mapQueryApproachAvailable=true;context.approachCells={{17,16},{1,1},{15,15},{17,16},{19,19},{22,22}};}
        if(scenario==8||scenario==9) context.alternateGoalCells={{16,14},{15,15},{16,14},{1,1},{22,22}};
        std::cout << "case " << ++cases << ' ' << scenario << ' ' << mode << ' ' << reverse << ' ' << reference << ' ' << budget << '\n';
        for(int i=0;i<6;++i){
            context.requesterStableHash=i<3?1:2;context.movementConstraintHash=i<3?1:2;
            const auto h=q.submit(start,goal,context);std::cout << "submit " << h.id << '\n';
            if(i==0){const auto duplicate=q.submit(start,goal,context);std::cout << "duplicate " << duplicate.id << '\n';}
            if(i==3){auto copy=q;std::cout << "copy-pump " << copy.pumpWithBudget(4,budget) << '\n';state(copy);}
            std::cout << "pump " << q.pumpWithBudget(4,budget) << '\n';state(q);
        }
    }
    std::cout << "complete cases=" << cases << '\n';
    return cases==1024?0:1;
}
