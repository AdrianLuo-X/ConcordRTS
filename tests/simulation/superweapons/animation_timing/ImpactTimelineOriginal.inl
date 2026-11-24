void h017OriginalImpactTimeline(
    RuntimeProductionHostState& state,
    RuntimeImpactEffect& impact,
    bool force) {
    const ArtType* art = state.art.findArtType(impact.animId);
    if (art == nullptr || (!force && !art->crater && !art->scorch)) {
        return;
    }

    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(
        state.config,
        state.report.map
    );
    context.structurePalettePath = runtimeStructurePalettePath(
        state.config,
        state.report.map
    );
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;

    RuntimeShpFrameImage firstFrame;
    std::string resolvedImageId;
    std::string sourcePath;
    std::string loadError;
    int frameCount = 0;
    const bool mainFramesOnly = art->shadowSpecified && art->shadow;
    if (!loadRuntimeAuxiliaryShpFrameImage(
            context,
            state.renderCache,
            impact.animId,
            0,
            firstFrame,
            resolvedImageId,
            sourcePath,
            loadError,
            mainFramesOnly,
            &frameCount
        )
        || frameCount <= 0) {
        return;
    }

    impact.animationFrameCount = frameCount;
    impact.animationFrameDelayTicks = runtimeImpactAnimationFrameDelayTicks(art, state.config.fixedTickHz);
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
    impact.frameIndex = impact.animationStartFrame;
    impact.totalTicks = std::max(
        impact.totalTicks,
        impact.animationSequenceSteps * impact.animationFrameDelayTicks
    );
    impact.remainingTicks = impact.totalTicks;

    const int firstMidpointStep = introSteps + (loopSpan / 2);
    const int midpointFrame = runtimeImpactAnimationFrameForStep(
        impact,
        firstMidpointStep
    );
    RuntimeShpFrameImage midpointImage;
    int ignoredFrameCount = 0;
    if (loadRuntimeAuxiliaryShpFrameImage(
            context,
            state.renderCache,
            impact.animId,
            midpointFrame,
            midpointImage,
            resolvedImageId,
            sourcePath,
            loadError,
            mainFramesOnly,
            &ignoredFrameCount
        )) {
        impact.animationMidpointWidth = midpointImage.globalWidth > 0
            ? midpointImage.globalWidth
            : midpointImage.width;
        impact.animationMidpointHeight = midpointImage.globalHeight > 0
            ? midpointImage.globalHeight
            : midpointImage.height;
    }
}
