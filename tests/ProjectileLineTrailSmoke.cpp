#include "RuntimeProjectileLineTrail.h"
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value,const char* error){if(!value)throw std::runtime_error(error);}
}
int main(){try{
    for(int hz:{15,30,60}){
        RuntimeProjectileLineTrail trail;
        for(int tick=0;tick<=hz*3;++tick){
            const double frame=tick*15.0/hz;
            runtimeLineTrailSample(trail,{frame,frame*2,frame/3,frame});
            runtimeLineTrailSample(trail,{frame,frame*2,frame/3,frame});
        }
        expect(trail.points.size()==32,"32-node trail bound");
        expect(trail.points.front().frame==14 && trail.points.back().frame==45,"15 Hz sampling");
        for(const auto& point:trail.points)expect(std::abs(point.x-point.frame)<1e-8,"sampling position");
        const auto count=trail.points.size();
        for(int paint=0;paint<100;++paint)runtimeLineTrailSample(trail,trail.head);
        expect(trail.points.size()==count,"pause/redraw consumed trail slots");
        runtimeLineTrailSample(trail,{0,0,0,0});
        expect(trail.points.size()==1,"timeline rewind did not reset history");
        runtimeLineTrailSample(trail,{10000,10000,0,10000});
        expect(trail.points.size()==32 && trail.points.front().frame==9969,"bounded long render gap");
    }
    const std::array<int,3> authored{216,216,255};
    expect(runtimeLineTrailFadedColor(authored,16,0)==authored,"head color");
    expect(runtimeLineTrailFadedColor(authored,16,1)==std::array<int,3>{200,200,239},"RGB decrement");
    expect(runtimeLineTrailFadedColor(authored,16,16)==std::array<int,3>{0,0,0},"fade completion");
    expect(runtimeLineTrailFadedColor(authored,0,500)==authored,"zero decrement");
    for(int n=0;n<32;++n){
        const double angle=n*6.283185307179586/32;
        expect(runtimeDirectionalProjectileFrame(std::sin(angle),-std::cos(angle),32)==n,"directional frame");
    }
    expect(runtimeDirectionalProjectileFrame(0,0,32)==0,"stationary frame");
    std::cout<<"Projectile line trail core passed: 15/30/60 Hz, pause, bounds, fade, 32 directions\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
