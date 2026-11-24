    impact.animationStartFrame = std::clamp(
        art->hasAnimationStart ? art->animationStart : 0,
        0,
        frameCount - 1
    );
    impact.animationEndFrame = frameCount;
    if (art->hasAnimationEnd) {
        impact.animationEndFrame = std::clamp(
            impact.animationStartFrame + std::max(1, art->animationEnd),
            impact.animationStartFrame + 1,
            frameCount
        );
    }
    impact.animationLoopStartFrame = std::clamp(
        art->hasAnimationLoopStart
            ? art->animationLoopStart
            : impact.animationStartFrame,
        impact.animationStartFrame,
        impact.animationEndFrame - 1
    );
    impact.animationLoopEndFrame = std::clamp(
        art->hasAnimationLoopEnd
            ? art->animationLoopEnd
            : impact.animationEndFrame,
        impact.animationLoopStartFrame + 1,
        impact.animationEndFrame
    );
    impact.animationLoopCount = art->hasAnimationLoopEnd
        ? std::clamp(art->animationLoopCount, 1, 1000)
        : 1;
    const int introSteps = std::max(
        0,
        impact.animationLoopStartFrame - impact.animationStartFrame
    );
    const int loopSpan = std::max(
        1,
        impact.animationLoopEndFrame - impact.animationLoopStartFrame
    );
    const int outroSteps = std::max(
        0,
        impact.animationEndFrame - impact.animationLoopEndFrame
    );
    impact.animationSequenceSteps = introSteps
        + (loopSpan * impact.animationLoopCount)
        + outroSteps;
