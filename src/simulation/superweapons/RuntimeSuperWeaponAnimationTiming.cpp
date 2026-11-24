#include "simulation/superweapons/RuntimeSuperWeaponAnimationTiming.h"
#include "assets/sprites/RuntimeAuxiliaryShpBytes.h"
#include "assets/animation/RuntimeArtAnimationSequence.h"
#include "data/art/ArtDatabase.h"
namespace concord::simulation::superweapons {
RuntimeSuperWeaponAnimationTiming resolveRuntimeSuperWeaponAnimationTiming(
    const ArtType* art, int fixedTickHz, RuntimeAuxiliaryShpBytesCacheEntry* bytes,
    RuntimeAnimationFrame0Validator& validator) {
    RuntimeSuperWeaponAnimationTiming timing;
    if (art == nullptr || bytes == nullptr || !bytes->loaded || bytes->frameCount <= 0) return timing;
    if (!bytes->timingFrame0Attempted) {
        bytes->timingFrame0Attempted = true;
        bytes->timingFrame0Valid = false;
        std::string error;
        bytes->timingFrame0Valid = validator.validate(bytes->bytes, bytes->sourcePath, error);
        bytes->timingFrame0Error = error;
    }
    if (!bytes->timingFrame0Valid) return timing;
    const int frameCount = concord::runtime::resource_detail::runtimeVisibleMainFrameCount(
        bytes->frameCount, art->shadowSpecified && art->shadow);
    const auto sequence = concord::assets::animation::resolveRuntimeArtAnimationSequence(art, frameCount);
    timing.sequenceSteps = sequence.sequenceSteps;
    if (timing.sequenceSteps <= 0) return timing;
    timing.frameDelayTicks = concord::assets::animation::runtimeSuperWeaponAnimationFrameDelayTicks(art, fixedTickHz);
    timing.durationTicks = timing.sequenceSteps * timing.frameDelayTicks;
    timing.midpointTicks = (timing.sequenceSteps / 2 + 1) * timing.frameDelayTicks;
    return timing;
}
}
