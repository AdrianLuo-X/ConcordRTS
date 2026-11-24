void h017OriginalSuperTimeline(RuntimeProductionHostState& state, RuntimeImpactEffect& animation) {
    h017OriginalImpactTimeline(state, animation, true);
    if (animation.animationSequenceSteps <= 0) return;
    const auto* art = state.art.findArtType(animation.animId);
    const int hz = std::max(1, state.config.fixedTickHz);
    // artmd Rate is frames per game minute (900 original ticks), not ticks
    // in this host's 30/60 Hz simulation.
    animation.animationFrameDelayTicks = art && art->rate > 0
        ? std::max(1, std::max(1, 900 / art->rate) * hz / 15)
        : std::max(1, hz / 15); // AnimTypeClass default delay is one native frame.
    animation.totalTicks = animation.remainingTicks =
        animation.animationSequenceSteps * animation.animationFrameDelayTicks;
}
