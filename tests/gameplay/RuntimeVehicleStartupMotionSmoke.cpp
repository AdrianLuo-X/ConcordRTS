#include "simulation/movement/ground/RuntimeVehicleStartupMotion.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace RuntimeVehicleStartupMotion;
    const auto require = [](bool ok, const std::string& reason) {
        if (!ok) { std::cerr << reason << '\n'; std::exit(1); }
    };
    for (double ramp : {0.1, 0.75, 2.0, 6.0}) {
        // Independent constant-acceleration solution for a long departure:
        // startup consumes twice the cruise time over its first ramp cells.
        const double length = ramp + 4.0;
        require(std::abs(length*durationScale(length,0,ramp)-length-ramp)<1e-12,
            "long departure duration is not integrated constant acceleration");
        const double duration = length*durationScale(length,0,ramp);
        const double first = length*progress(0.05/duration,length,0,ramp);
        require(std::abs(first-0.05*0.05/(4*ramp))<1e-12,
            "departure does not start from rest");
        for (double before : {0.0, ramp*0.2, ramp, ramp+2.0})
            for (double segment : {1.0, std::sqrt(2.0)})
                for (double after : {-1.0, 0.0, 0.25, 1.0, 2.0, 8.0}) {
                    const double total = segment*durationScale(segment,before,ramp,after);
                    require(progress(0,segment,before,ramp,after)==0
                        && progress(1,segment,before,ramp,after)==1,"startup endpoints are not exact");
                    double previous=0;
                    for (int i=1;i<=200;++i) {
                        const double fraction=i/200.0;
                        const double next=progress(fraction,segment,before,ramp,after);
                        require(next>=previous && next<=1,"startup reverses or overshoots");
                        if(i<200) {
                            constexpr double dt=0.00001;
                            const double distance=next*segment;
                            const double velocity=segment*(progress(fraction+dt/total,segment,before,ramp,after)
                                -progress(fraction-dt/total,segment,before,ramp,after))/(2*dt);
                            double expected=std::min(1.0,std::sqrt((before+distance)/ramp));
                            if(after>=0 && after<RuntimeVehicleArrivalMotion::brakingDistance)
                                expected=std::min(expected,std::sqrt(std::max(0.0,segment+after-distance)
                                    /RuntimeVehicleArrivalMotion::brakingDistance));
                            require(std::abs(velocity-expected)<0.0001,"duration and displayed velocity disagree");
                        }
                        previous=next;
                    }
                    if(before>=ramp) {
                        require(durationScale(segment,before,ramp,after)
                            ==RuntimeVehicleArrivalMotion::durationScale(segment,after),"cruise duration changed");
                        require(progress(0.31,segment,before,ramp,after)
                            ==RuntimeVehicleArrivalMotion::progress(0.31,segment,after),"cruise/arrival curve changed");
                    }
                }
    }
    // The complete route has one continuous acceleration, across both diagonal
    // and orthogonal cell boundaries, followed by the original final brake.
    for (const auto& route : {std::vector<double>{1,1,1,1,1,1,1,1},
        std::vector<double>{1,std::sqrt(2.0),1,std::sqrt(2.0),1,1}}) {
        constexpr double ramp=1.2,dt=0.000001;
        double remaining=0,before=0,previousSpeed=0;
        for(double length:route) remaining+=length;
        for(double length:route) {
            remaining-=length;
            const double after=std::max(0.0,remaining);
            const double total=length*durationScale(length,before,ramp,after);
            const double start=length*progress(dt/total,length,before,ramp,after)/dt;
            const double end=length*(1-progress(1-dt/total,length,before,ramp,after))/dt;
            require(std::abs(start-previousSpeed)<0.00001,"startup repeats or jumps at cell boundary");
            previousSpeed=end;before+=length;
        }
        require(previousSpeed<0.00001,"short/combined route does not stop at zero speed");
    }
    for(int hz:{15,30,60}) {
        constexpr double cruise=0.07,factor=0.03;
        const double ramp=accelerationDistance(cruise,factor);
        const double ticks=hz/15.0*8/cruise*durationScale(8,0,ramp);
        const double expected=(8/cruise+0.5/factor)/15.0;
        require(std::abs(ticks/hz-expected)<1e-12,"startup depends on simulation tick rate");
    }
    require(std::isinf(accelerationDistance(0.07,0))
        &&std::isinf(durationScale(1,0,accelerationDistance(0.07,0)))
        &&progress(0.5,1,0,accelerationDistance(0.07,0))==0,"zero acceleration silently gains motion");
    require(rampDistanceQ16(1.234567)/65536.0>=1.234567,"distance cap prevents exact cruise recovery");
    require(distanceQ16(1e100)==maximumDistanceQ16,"distance accumulator can overflow");
    std::cout<<"Vehicle startup curve: real integrated acceleration, short/diagonal braking, continuous cells, "
        "unchanged cruise, 15/30/60Hz and zero acceleration passed\n";
}
