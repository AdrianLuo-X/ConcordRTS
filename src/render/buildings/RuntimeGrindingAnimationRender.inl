const RuntimeImpactRenderActor* runtimeGrindingAnimationForBuilding(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
) {
    if (!context.impactActors) return nullptr;
    return concord::render::selectRuntimeGrindingAnimation(
        object.instanceId, std::span<const RuntimeImpactRenderActor>{*context.impactActors});
}

int runtimeGrindingAnimationFrame(const RuntimeImpactRenderActor& cycle, const ArtType* art, int hz) {
    return concord::render::selectRuntimeGrindingAnimationFrame(cycle.frameIndex,
        cycle.totalTicks, cycle.remainingTicks, art, hz, runtimeAnimationFrameDelayTicks);
}
