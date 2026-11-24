#pragma once
#include <string>
#include <vector>
struct ArtType;
struct RuntimeAuxiliaryShpBytesCacheEntry;
namespace concord::simulation::superweapons {
class RuntimeAnimationFrame0Validator {
public:
    virtual ~RuntimeAnimationFrame0Validator() = default;
    virtual bool validate(const std::vector<unsigned char>& bytes, const std::string& sourcePath,
        std::string& error) = 0;
};
struct RuntimeSuperWeaponAnimationTiming {
    int sequenceSteps = 0;
    int frameDelayTicks = 0;
    int durationTicks = 0;
    int midpointTicks = 0;
};
RuntimeSuperWeaponAnimationTiming resolveRuntimeSuperWeaponAnimationTiming(
    const ArtType*, int fixedTickHz, RuntimeAuxiliaryShpBytesCacheEntry*, RuntimeAnimationFrame0Validator&);
}
