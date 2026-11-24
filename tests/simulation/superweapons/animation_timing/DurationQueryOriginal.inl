int h017OriginalDuration(RuntimeProductionHostState& state,
    const std::string& animationId) {
    if (animationId.empty() || !state.artLoaded) return 0;
    RuntimeImpactEffect animation;
    animation.animId = animationId;
    h017OriginalSuperTimeline(state, animation);
    return animation.animationSequenceSteps > 0
        ? animation.animationSequenceSteps * animation.animationFrameDelayTicks : 0;
}
