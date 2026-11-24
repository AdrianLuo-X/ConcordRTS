#pragma once
struct ArtType;
namespace concord::assets::animation {
struct RuntimeArtAnimationSequence {
    int startFrame = 0;
    int endFrame = 0;
    int loopStartFrame = 0;
    int loopEndFrame = 0;
    int loopCount = 1;
    int introSteps = 0;
    int loopSpan = 0;
    int outroSteps = 0;
    int sequenceSteps = 0;
};
RuntimeArtAnimationSequence resolveRuntimeArtAnimationSequence(const ArtType*, int frameCount);
int runtimeSuperWeaponAnimationFrameDelayTicks(const ArtType*, int fixedTickHz);
}
