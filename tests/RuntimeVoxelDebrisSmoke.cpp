#include "RuntimeVoxelDebris.h"
#include "RuntimeVehicleBodyDynamics.h"
#include <iostream>
#include <set>
#include <stdexcept>

int main() {
    const auto require = [](bool result, const char* text) { if (!result) throw std::runtime_error(text); };
    const auto close = [](double a, double b) { return std::abs(a-b) < 0.0002; };
    try {
        std::set<int> horizontal, vertical;
        for (std::uint64_t seedStart = 0; seedStart < 20000; ++seedStart) {
            auto seed = seedStart;
            auto motion = runtimeMakeVoxelDebris(16,16,0,150,0.8,28,32,10,
                12*runtimeDebrisRadiansPerDegree,24*runtimeDebrisRadiansPerDegree,seed);
            horizontal.insert(static_cast<int>(motion.velocity[0])); horizontal.insert(static_cast<int>(motion.velocity[1]));
            vertical.insert(static_cast<int>(motion.velocity[2]));
            require(motion.framesLeft == 150 && motion.position[2] == 10, "TIRE duration/origin");
            require(close(2*std::acos(motion.spin[3]),12*runtimeDebrisRadiansPerDegree), "angular interval is truncated after conversion");
            RuntimeVoxelDebrisState restored;
            require(parseRuntimeVoxelDebris(serializeRuntimeVoxelDebris(motion),restored), "initial record parse");
            require(serializeRuntimeVoxelDebris(motion) == serializeRuntimeVoxelDebris(restored), "initial record roundtrip");
        }
        require(horizontal.size()==20 && *horizontal.begin()==-10 && *horizontal.rbegin()==9, "XY integer interval");
        require(vertical==std::set<int>({28,29,30,31,32}), "Z inclusive interval");
        const auto flat = [](const auto&, const auto&) { return RuntimeDebrisSurface{}; };
        std::uint64_t seed = 33;
        auto trajectory = runtimeMakeVoxelDebris(16,16,0,150,0.8,28,28,10,0.2,0.4,seed);
        const auto initial = trajectory;
        double expectedZ=10, expectedVz=28;
        for (int frame=1;frame<=20;++frame) {
            ++trajectory.frameCount; expectedVz-=runtimeDebrisGravity; expectedZ+=expectedVz;
            require(runtimeStepVoxelDebris(trajectory,flat)==RuntimeDebrisContact::None,"ascending free flight");
            require(close(trajectory.position[2],expectedZ),"semi-implicit gravity position");
            require(close(trajectory.position[0],initial.position[0]+initial.velocity[0]*frame),"XY speed per game frame");
        }
        auto bounce=initial; bounce.frameCount=1; bounce.position[2]=0.5f; bounce.velocity={10,-5,-2};
        const auto spin=bounce.spin;
        require(runtimeStepVoxelDebris(bounce,flat)==RuntimeDebrisContact::Bounce,"ground bounce event");
        require(bounce.bounces==1 && bounce.position[2]==0 && close(bounce.velocity[0],8)
            && close(bounce.velocity[1],-4) && close(bounce.velocity[2],2.72),"elastic reflection on all axes");
        require(bounce.spin[0]==-spin[0] && bounce.spin[1]==-spin[1] && bounce.spin[2]==-spin[2]
            && bounce.spin[3]==spin[3],"collision reverses quaternion increment");
        auto stopped=initial; stopped.frameCount=1; stopped.position[2]=0; stopped.velocity={0,0,0}; stopped.elasticity=0;
        require(runtimeStepVoxelDebris(stopped,flat)==RuntimeDebrisContact::Stopped,"zero-elasticity stop");
        auto water=initial; water.frameCount=1; water.position[2]=0; water.velocity[2]=-10;
        require(runtimeStepVoxelDebris(water,[](const auto&, const auto&) {
            RuntimeDebrisSurface result; result.water=true; return result;
        })==RuntimeDebrisContact::Water,"water contact");
        RuntimeVoxelBodyRotation rotation; rotation.useQuaternion=true;
        rotation.quaternion={0,std::sqrt(0.5),0,std::sqrt(0.5)};
        const auto turned=rotation.rotate(1,0,0);
        require(close(turned[0],0) && close(turned[1],0) && close(turned[2],-1),"renderer quaternion axes");
        int bounces=0;
        auto continued=initial;
        for (int frame=1;frame<150;++frame) {
            ++continued.frameCount;
            const auto event=runtimeStepVoxelDebris(continued,flat);
            if(event==RuntimeDebrisContact::Bounce)++bounces;
            RuntimeVoxelDebrisState copy;
            require(parseRuntimeVoxelDebris(serializeRuntimeVoxelDebris(continued),copy),"moving record parse");
            require(serializeRuntimeVoxelDebris(continued)==serializeRuntimeVoxelDebris(copy),"moving record equality");
            auto uninterrupted=continued;
            ++uninterrupted.frameCount; ++copy.frameCount;
            runtimeStepVoxelDebris(uninterrupted,flat);runtimeStepVoxelDebris(copy,flat);
            require(serializeRuntimeVoxelDebris(uninterrupted)==serializeRuntimeVoxelDebris(copy),"save resumes identical motion");
            if(event==RuntimeDebrisContact::Stopped)break;
        }
        require(bounces>=2,"TIRE must rebound repeatedly");
        RuntimeVoxelDebrisState invalid;
        require(!parseRuntimeVoxelDebris("1 150 0 0 0 nan",invalid),"reject nonfinite record");
        require(!parseRuntimeVoxelDebris(serializeRuntimeVoxelDebris(initial)+" junk",invalid),"reject trailing fields");
        std::cout << "VoxelDebris physics passed: 20000 launch samples; exact intervals, gravity, elastic bounce, spin, stop, water, quaternion rendering and record continuation.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
