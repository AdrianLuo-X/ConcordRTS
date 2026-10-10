bool smokeNoCooldownProduction() {
    RuntimeProductionAdvanceHooks hooks;
    hooks.completeImmediately = [](auto, const auto&) { return true; };
    for (auto lane : runtimeProductionLanes) for (int cost : {0, 150, 1000000000}) {
        RuntimeProductionLanesState state;
        RuntimeProductionLaneOrder order;
        order.itemKey = "TEST"; order.cost = cost; order.totalTicks = 1000000;
        order.requiresPlacement = lane == RuntimeProductionLane::Structures || lane == RuntimeProductionLane::Armory;
        if (!expect(runtimeQueueProductionOrder(state,lane,order,29,8).decision
                == RuntimeProductionQueueDecision::Accepted,"instant queue")) return false;
        int credits = cost;
        const auto events = runtimeAdvanceProductionLanes(state,credits,hooks);
        auto& slot = state.lanes[runtimeProductionLaneIndex(lane)];
        const auto& finished = order.requiresPlacement ? slot.ready : slot.active;
        if (!expect(finished && finished->remainingTicks == 0 && finished->creditsSpent == cost
                && credits == 0 && totalDebited(events) == cost,"instant exact completion/payment")) return false;
        const auto hash = runtimeProductionLanesStateHash(state);
        runtimeAdvanceProductionLanes(state,credits,hooks);
        if (!expect(runtimeProductionLanesStateHash(state) == hash && credits == 0,
                "instant deferred delivery changed or paid twice")) return false;
        RuntimeProductionLanesState restored;
        if (!expect(runtimeDeserializeProductionLanes(runtimeSerializeProductionLanes(state),restored)
                && runtimeProductionLanesStateHash(restored) == hash,"instant saved completion")) return false;
    }
    RuntimeProductionLanesState state;
    RuntimeProductionLaneOrder order;
    order.itemKey="TEST";order.cost=150;order.totalTicks=3000;
    const auto lane=RuntimeProductionLane::Infantry;
    runtimeQueueProductionOrder(state,lane,order,29,0);
    int credits=40;
    hooks.canStart=[](auto,const auto&){return false;};
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(credits==40 && !state.lanes[2].active,"instant start prerequisite")) return false;
    hooks.canStart={};
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(credits==0 && state.lanes[2].active->creditsSpent==40
        && state.lanes[2].active->remainingTicks==2200,"instant partial funding")) return false;
    const auto hash=runtimeProductionLanesStateHash(state);
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(hash==runtimeProductionLanesStateHash(state),"instant empty wallet")) return false;
    credits=110;
    runtimeSetProductionHold(state,lane,true);
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(credits==110 && state.lanes[2].active->remainingTicks==2200,"instant hold")) return false;
    runtimeSetProductionHold(state,lane,false);
    hooks.canContinue=[](auto,const auto&){return false;};
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(credits==110,"instant continuation prerequisite")) return false;
    hooks.canContinue={};
    RuntimeProductionLanesState restored;
    if(!expect(runtimeDeserializeProductionLanes(runtimeSerializeProductionLanes(state),restored),
        "partial funding save")) return false;
    int restoredCredits=credits;
    runtimeAdvanceProductionLanes(restored,restoredCredits,hooks);
    runtimeAdvanceProductionLanes(state,credits,hooks);
    if(!expect(credits==0 && restoredCredits==0 && state.lanes[2].active->remainingTicks==0
        && runtimeProductionLanesStateHash(state)==runtimeProductionLanesStateHash(restored),
        "instant resume completion")) return false;
    const auto cancelled=runtimeCancelProductionSerial(state,lane,state.lanes[2].active->serial);
    if(!expect(cancelled.removed && cancelled.refund==150,"instant exact cancellation refund")) return false;
    runtimeQueueProductionOrder(state,lane,order,29,0);credits=150;
    runtimeAdvanceProductionLanes(state,credits);
    if(!expect(state.lanes[2].active && state.lanes[2].active->remainingTicks>0,
        "disabled option lost normal wait")) return false;
    std::cout<<"No cooldown production: six lanes, free/long/high-cost, gates, funds, hold, delivery, save/refund passed\n";
    return true;
}
