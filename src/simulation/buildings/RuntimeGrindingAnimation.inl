bool initializeRuntimeGrindingAnimation(RuntimeWindowState& state, int hostId,
    RuntimeImpactEffect& cycle) {
    const auto* host=findObjectById(state,hostId);
    if (!host) return false;
    const auto* building=findRuntimeBuildingType(state.rules,*host);
    const auto* art=runtimeArtTypeForObject(state,*host);
    if (!building || !building->grinding || !art) return false;
    const auto layer=std::find_if(art->specialAnimations.begin(),art->specialAnimations.end(),
        [](const auto& animation){return animation.key=="SpecialAnim";});
    if (layer==art->specialAnimations.end()) return false;
    const bool damaged=host->health*2<runtimeMaxHealthForObject(state,*host);
    cycle=RuntimeImpactEffect{};
    cycle.attackerObjectId=cycle.targetObjectId=hostId; cycle.owner=host->owner;
    cycle.cell={host->x,host->y}; cycle.level=host->level;
    cycle.animId=damaged && !layer->damagedImage.empty() ? layer->damagedImage : layer->image;
    cycle.renderIntent="grinderCycle"; cycle.lifecycleState="GrindingAnimation";
    initializeRuntimeImpactAnimationTimeline(state,cycle,true);
    if (cycle.animationFrameCount<=0) return false;
    // The SHP stores the healthy and damaged cycles together. End at the
    // authored loop boundary instead of continuing through the other half.
    cycle.animationEndFrame=cycle.animationLoopEndFrame;
    cycle.animationLoopCount=1;
    cycle.animationSequenceSteps=cycle.animationEndFrame-cycle.animationStartFrame;
    cycle.totalTicks=cycle.remainingTicks=cycle.animationSequenceSteps*cycle.animationFrameDelayTicks;
    return cycle.animationSequenceSteps>0 && cycle.totalTicks>0;
}

void startRuntimeGrindingAnimation(RuntimeWindowState& state, int hostId) {
    RuntimeImpactEffect cycle;
    if (!initializeRuntimeGrindingAnimation(state,hostId,cycle)) return;
    const bool busy=std::any_of(state.impactEffects.begin(),state.impactEffects.end(),
        [&](const auto& impact){return impact.targetObjectId==hostId && impact.remainingTicks>0
            && (impact.lifecycleState=="GrindingAnimation"
                || impact.lifecycleState=="GrindingAnimationQueued");});
    if (busy) {
        cycle.lifecycleState="GrindingAnimationQueued";
        cycle.renderIntent="grinderCycleQueued";
    }
    state.impactEffects.push_back(std::move(cycle));
}

bool promoteRuntimeGrindingAnimations(RuntimeWindowState& state) {
    std::unordered_set<int> activeHosts;
    for (const auto& cycle:state.impactEffects)
        if (cycle.lifecycleState=="GrindingAnimation" && cycle.remainingTicks>0)
            activeHosts.insert(cycle.targetObjectId);
    bool changed=false;
    for (auto& cycle:state.impactEffects) {
        if (cycle.lifecycleState!="GrindingAnimationQueued"
            || activeHosts.contains(cycle.targetObjectId)) continue;
        RuntimeImpactEffect started;
        if (initializeRuntimeGrindingAnimation(state,cycle.targetObjectId,started)) {
            cycle=std::move(started);
            activeHosts.insert(cycle.targetObjectId);
        } else {
            cycle.remainingTicks=0;
        }
        changed=true;
    }
    std::erase_if(state.impactEffects,[](const auto& cycle) {
        return cycle.lifecycleState=="GrindingAnimationQueued" && cycle.remainingTicks<=0;
    });
    return changed;
}

#include "simulation/buildings/RuntimeGrindingAnimationSave.inl"
