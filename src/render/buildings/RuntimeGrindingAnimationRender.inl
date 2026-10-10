const RuntimeImpactRenderActor* runtimeGrindingAnimationForBuilding(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
) {
    if (context.impactActors) for (const auto& actor:*context.impactActors)
        if (actor.renderIntent=="grinderCycle" && actor.targetObjectId==object.instanceId
            && actor.remainingTicks>0) return &actor;
    return nullptr;
}

int runtimeGrindingAnimationFrame(const RuntimeImpactRenderActor& cycle, const ArtType* art, int hz) {
    if (cycle.frameIndex>=0) return cycle.frameIndex;
    if (!art) return 0;
    const int start=art->hasAnimationStart ? art->animationStart
        : (art->hasAnimationLoopStart ? art->animationLoopStart : 0);
    const int end=art->hasAnimationLoopEnd ? std::max(start+1,art->animationLoopEnd) : start+1;
    const int delay=runtimeAnimationFrameDelayTicks(art,hz);
    return std::clamp(start+(cycle.totalTicks-cycle.remainingTicks)/delay,start,end-1);
}
