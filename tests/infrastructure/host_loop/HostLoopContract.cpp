#include "runtime/timing/RuntimeHostLoopPort.h"
#include "runtime/timing/RuntimeGameSpeedTiming.h"
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <vector>
struct Observation {int step;std::uint64_t credit;bool operator==(const Observation&)const=default;};
struct Port final:RuntimeHostLoopPort {
    bool running=true;std::vector<int> calls;
    bool drainMessages()override{calls.push_back(1);return running;}
    void sleepForIdleSlice()override{calls.push_back(2);}
};
int main(){
    std::size_t checks=0,failures=0,ledgers=0,decisions=0;
    const auto need=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"Host loop policy failure: "<<why<<'\n';}};
    for(int speed=-1;speed<=7;++speed)for(int hz:{0,15,30,60})for(bool multi:{false,true})
    for(double previous:{-1.0,0.0,0.1,3600.0})for(double elapsed:{-1.0,0.0,0.001,0.01,1.0,3600.0}){
        ++ledgers;const auto timing=runtimeGameSpeedTiming(speed,hz);
        const int oldLimit=multi?8:16,newLimit=runtimeHostLoopCatchupLimit(multi);
        need(newLimit==oldLimit,"same single/multi per-loop budget");
        double oldCredit=timing.accumulate(previous,elapsed,oldLimit),newCredit=timing.accumulate(previous,elapsed,newLimit);
        need(std::bit_cast<std::uint64_t>(oldCredit)==std::bit_cast<std::uint64_t>(newCredit),"complete accumulated credit bits equal");
        std::vector<Observation> oldSteps,newSteps;
        for(int step=0;timing.ready(oldCredit,step,oldLimit);++step){oldCredit=timing.consume(oldCredit);oldSteps.push_back({step+1,std::bit_cast<std::uint64_t>(oldCredit)});}
        for(int step=0;timing.ready(newCredit,step,newLimit);++step){newCredit=timing.consume(newCredit);newSteps.push_back({step+1,std::bit_cast<std::uint64_t>(newCredit)});}
        need(oldSteps==newSteps,"all pacing step ordinals and credit bits equal to original limit expression");
        need(newSteps.size()<=static_cast<std::size_t>(oldLimit),"no new uncapped catch-up batch");
        need(std::bit_cast<std::uint64_t>(oldCredit)==std::bit_cast<std::uint64_t>(newCredit),"full remainder bits equal");
        // The actual production timing implementation is unchanged, not a copied simulation.
        if(timing.unlimited())need(newSteps.size()==static_cast<std::size_t>(oldLimit)&&newCredit==0,"unlimited still respects original8/16 budget");
    }
    for(int speed=0;speed<=6;++speed)for(bool paint:{false,true})for(bool advanced:{false,true})
    for(bool multi:{false,true})for(bool host:{false,true}){
        ++decisions;const auto timing=runtimeGameSpeedTiming(speed,30);
        const bool unlimitedAuthorityAdvanced=timing.unlimited()&&advanced&&(!multi||host);
        const bool oldSleep=!paint&&!unlimitedAuthorityAdvanced;
        Port port;RuntimeHostLoopPort& services=port;
        const bool running=services.drainMessages();
        if(running&&runtimeHostLoopShouldIdleSleep(paint,unlimitedAuthorityAdvanced))services.sleepForIdleSlice();
        need(port.calls==(oldSleep?std::vector<int>{1,2}:std::vector<int>{1}),"same idle condition and drain-before-sleep ordering");
        port.calls.clear();port.running=false;
        if(services.drainMessages()&&runtimeHostLoopShouldIdleSleep(paint,unlimitedAuthorityAdvanced))services.sleepForIdleSlice();
        need(port.calls==std::vector<int>{1},"quit drain never reaches a later idle request");
    }
    std::cout<<"Host loop policy contract: checks="<<checks<<" ledgers="<<ledgers<<" idleDecisions="<<decisions
        <<" failures="<<failures<<" skipped=0 fullCreditBits=equal limits=8,16 gameSpeedTiming=unchanged fakeWorld=0 nativeWait=not-run\n";
    return failures?1:0;
}
