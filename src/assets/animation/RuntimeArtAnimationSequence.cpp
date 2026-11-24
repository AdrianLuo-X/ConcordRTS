#include "assets/animation/RuntimeArtAnimationSequence.h"
#include "data/art/ArtDatabase.h"
#include <algorithm>
namespace concord::assets::animation {
RuntimeArtAnimationSequence resolveRuntimeArtAnimationSequence(const ArtType* art, int frameCount) {
    RuntimeArtAnimationSequence sequence;
    if (art == nullptr || frameCount <= 0) return sequence;
    sequence.startFrame = std::clamp(art->hasAnimationStart ? art->animationStart : 0, 0, frameCount - 1);
    sequence.endFrame = frameCount;
    if (art->hasAnimationEnd) {
        sequence.endFrame = std::clamp(sequence.startFrame + std::max(1, art->animationEnd),
            sequence.startFrame + 1, frameCount);
    }
    sequence.loopStartFrame = std::clamp(
        art->hasAnimationLoopStart ? art->animationLoopStart : sequence.startFrame,
        sequence.startFrame, sequence.endFrame - 1);
    sequence.loopEndFrame = std::clamp(
        art->hasAnimationLoopEnd ? art->animationLoopEnd : sequence.endFrame,
        sequence.loopStartFrame + 1, sequence.endFrame);
    sequence.loopCount = art->hasAnimationLoopEnd ? std::clamp(art->animationLoopCount, 1, 1000) : 1;
    sequence.introSteps = std::max(0, sequence.loopStartFrame - sequence.startFrame);
    sequence.loopSpan = std::max(1, sequence.loopEndFrame - sequence.loopStartFrame);
    sequence.outroSteps = std::max(0, sequence.endFrame - sequence.loopEndFrame);
    sequence.sequenceSteps = sequence.introSteps + sequence.loopSpan * sequence.loopCount + sequence.outroSteps;
    return sequence;
}
int runtimeSuperWeaponAnimationFrameDelayTicks(const ArtType* art, int fixedTickHz) {
    const int hz = std::max(1, fixedTickHz);
    return art && art->rate > 0 ? std::max(1, std::max(1, 900 / art->rate) * hz / 15)
        : std::max(1, hz / 15);
}
}
