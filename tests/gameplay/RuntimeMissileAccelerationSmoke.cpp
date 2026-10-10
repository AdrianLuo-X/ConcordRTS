#include "simulation/combat/projectiles/RuntimeMissileAcceleration.h"
#include <cmath>
#include <iostream>

int main() {
    const auto close = [](double actual, double expected) {
        return std::abs(actual - expected) <= 1e-9;
    };
    const auto check = [&](bool good, const char* label) {
        if (!good) { std::cerr << "Missile acceleration: " << label << "\n"; return false; }
        return true;
    };
    // Independent handwritten native-frame sums: 4,7,10,...,73,76,76 (one-lepton launch velocity).
    if (!check(close(runtimeMissileAccelerationDistanceLeptons(1,76,3),4),"first frame")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(2,76,3),11),"second frame")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(15,76,3),375),"one second")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(25,76,3),1000),"capped launch sum")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(26,76,3),1076),"cruise increment")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(45,76,3),2520),"three seconds")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(0.5,76,3),2),"fractional rule frame")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(2,76,0),152),"explicit zero compatibility")
        || !check(close(runtimeMissileAccelerationDistanceLeptons(2,76,-2),152),"negative compatibility")) return 1;
    int samples=0;
    for (int hz : {15,30,60}) {
        const double top = 76.0 / 256.0 * 15.0 / hz;
        double distance=0.0, previous=-1.0;
        for (int tick=1;tick<=hz*3;++tick) {
            const double step=runtimeMissileAccelerationStepCells(top,3,tick,hz);
            if (!check(step>0.0 && step<=top+1e-12 && step+1e-12>=previous,"monotonic capped step")) return 1;
            if (tick==1 && !check(close(step*hz/15.0*256.0,4),"slow first velocity")) return 1;
            distance+=step; previous=step; ++samples;
            if (tick==hz && !check(close(distance*256.0,375),"frequency-independent one second")) return 1;
        }
        if (!check(close(distance*256.0,2520),"frequency-independent three seconds")) return 1;
        const int delay=runtimeMissileAccelerationStartupDelayTicks(top,3,hz);
        if (!check(delay==int(std::ceil((900.0/76.0)*hz/15.0)),"startup lifetime loss")) return 1;
        if (!check(runtimeMissileAccelerationStartupDelayTicks(top,0,hz)==0,"zero lifetime compatibility")) return 1;
    }
    std::cout << "Missile acceleration math: native handwritten sums, fractional frames, "
        << samples << " measured steps, 15/30/60 Hz, cruise cap and lifetime loss passed\n";
    return 0;
}
