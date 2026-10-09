#pragma once

#include "ArtDatabase.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline int runtimeSuperWeaponBuildingAnimationDelay(const ArtType* art, int tickHz) {
    const int delay=art && art->rate>0 ? std::clamp((900+art->rate/2)/art->rate,1,36) : 6;
    return std::max(1,int(std::lround(delay*std::max(1,tickHz)/30.0)));
}

// Local presentation only. Reconstructed from saved building/weapon clocks;
// never included in gameplay saves, lockstep state or random-number streams.
struct RuntimeSuperWeaponBuildingSound {
    std::string stage;
    std::string event;
    std::string owner;
    std::string sample;
    std::uint64_t nextSampleMilliseconds=0;
    std::uint64_t queuedUntilMilliseconds=0;
    std::vector<std::size_t> remainingLoopSamples;
    int ordinal=0;
    int gainPermille=0;
    bool loop=false;
    bool active=false;
};

struct RuntimeSuperWeaponBuildingAudioState {
    bool primed=false;
    std::unordered_map<int,RuntimeSuperWeaponBuildingSound> sources;
    // New support aircraft may start before the first post-load audio frame.
    std::unordered_set<int> newlyCreatedSources;
};
