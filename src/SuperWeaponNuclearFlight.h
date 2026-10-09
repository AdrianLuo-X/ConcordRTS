#pragma once

#include <algorithm>
#include <cmath>
#include "RulesDatabase.h"

// Original vertical BulletClass movement (0x4671CA), silo initial velocity
// 10 and speed cap 255 (0x44CAC6), payload initial velocity -1 (0x46B310).
inline double superWeaponNuclearDistance(double frames, int initial, int speed, int acceleration) {
    frames = std::max(0.0, frames);
    const int n = int(frames);
    const int ramp = acceleration > 0 ? std::max(0, (speed-initial+acceleration-1)/acceleration) : 0;
    const int accelerating = std::min(n, ramp);
    const double top = acceleration > 0 ? initial + ramp * acceleration : initial;
    const double moved = double(accelerating)*initial
        + double(acceleration)*accelerating*(accelerating+1)/2
        + std::max(0,n-ramp)*top;
    const double next = acceleration > 0 ? initial + std::min(n+1,ramp)*acceleration : initial;
    return moved + (frames-n)*next;
}

inline int superWeaponNuclearTravelFrames(int height, int initial, int speed, int acceleration) {
    int low=0, high=1;
    while (high<1000000 && superWeaponNuclearDistance(high,initial,speed,acceleration)<=height) high*=2;
    while (low+1<high) {
        const int middle=(low+high)/2;
        if(superWeaponNuclearDistance(middle,initial,speed,acceleration)>height) high=middle;
        else low=middle;
    }
    return high;
}

inline int superWeaponNuclearAscentFrames(const SuperWeaponRule& rule, bool hasSource) {
    return hasSource ? superWeaponNuclearTravelFrames(rule.nuclearAltitudeLeptons,
        10,255,rule.nuclearUpAcceleration) : 0;
}

inline int superWeaponNuclearFlightFrames(const SuperWeaponRule& rule, bool hasSource) {
    return superWeaponNuclearAscentFrames(rule,hasSource)
        + superWeaponNuclearTravelFrames(rule.nuclearAltitudeLeptons,hasSource?1:100,
            rule.nuclearDownSpeed,rule.nuclearDownAcceleration);
}
