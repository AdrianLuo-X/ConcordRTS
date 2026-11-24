int h017OriginalMidpoint(RuntimeProductionHostState& state,
    const std::string& animationId) {
    RuntimeImpactEffect animation; animation.animId=animationId;
    h017OriginalSuperTimeline(state,animation);
    return animation.animationSequenceSteps > 0
        ? (animation.animationSequenceSteps/2+1)*animation.animationFrameDelayTicks : 0;
}
