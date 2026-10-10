#include "audio/events/RuntimePowerAudioFeedback.h"

#include <algorithm>
#include <cctype>

namespace {

std::string trimSoundId(const std::string& value) {
    const auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace)
        .base();
    return std::string(first, last);
}

} // namespace

std::string runtimePoweredUnitTransitionSound(
    const UnitType& unit,
    bool transitionStateObserved,
    bool localOwner,
    bool wasOperational,
    bool isOperational
) {
    if (!unit.poweredUnit
        || !transitionStateObserved
        || !localOwner
        || wasOperational == isOperational) {
        return {};
    }
    return trimSoundId(
        isOperational ? unit.activateSound : unit.deactivateSound
    );
}

std::string runtimeBuildingPowerTransitionSound(
    const BuildingType& building,
    bool transitionStateObserved,
    bool localOwner,
    bool wasOperational,
    bool isOperational
) {
    if (!building.powered
        || !transitionStateObserved
        || !localOwner
        || !wasOperational
        || isOperational) {
        return {};
    }
    return trimSoundId(building.notWorkingSound);
}
