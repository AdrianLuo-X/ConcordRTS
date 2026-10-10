// Pure synthetic clock contract. It does not launch or attach any process.
// Run the UNCHANGED original 84 scenarios first, not a reconstructed substitute.
#define main runUnchangedPresentationBudgetSmokeMain
#include "RuntimePresentationWorkBudgetSmoke.cpp"
#undef main
#include "runtime/timing/RuntimePresentationBudgetRemainder.h"

#include <cstdint>
#include <iomanip>
#include <string>

namespace RemainderClock {
enum class Route { Original, Reference, Optimized };
struct Result {
    double wall=0.0, credit=0.0;
    int ticks=0, paints=0, maxTicks=0;
    int enabledBatches=0, budgetYields=0;
    double tps() const { return ticks/wall; }
    bool sameTimeline(const Result& b) const {
        return wall==b.wall && credit==b.credit && ticks==b.ticks
            && paints==b.paints && maxTicks==b.maxTicks;
    }
};

Result run(int speed,double tickMs,double paintMs,double extraMs,Route route,bool evolving=false) {
    const auto timing=runtimeGameSpeedTiming(speed,30);
    const double hz=timing.unlimited()?187.5:1.0/timing.secondsPerTick;
    RuntimePresentationRemainderReferenceScope reference(route==Route::Reference);
    RuntimePresentationBudgetRemainder remainder;
    Result out; double prior=0.0,deadline=1.0/60.0,estimate=tickMs;
    int iterations=0;
    while(out.wall<30.0) {
        require(++iterations<100000,"clock iteration bound");
        out.credit=timing.accumulate(out.credit,out.wall-prior,16);
        prior=out.wall;
        RuntimePresentationWorkBudget budget(1.0/60.0,paintMs,estimate,hz);
        remainder.begin(budget,1.0/60.0,paintMs,extraMs,estimate,hz,true,timing.unlimited(),speed);
        out.enabledBatches+=remainder.enabled()?1:0;
        const double start=out.wall; int advanced=0;
        while(timing.ready(out.credit,advanced,16)) {
            ++out.ticks; ++advanced;
            const double cost=tickMs*(evolving ? ((out.ticks/256)%3==0 ? 1.0
                : (out.ticks/256)%3==1 ? 1.2 : 0.8) : 1.0);
            out.wall+=cost/1000.0;
            estimate+=0.05*(cost-estimate);
            const double creditBefore=out.credit;
            out.credit=timing.consume(out.credit);
            require(timing.unlimited()||std::abs(out.credit
                -std::max(0.0,creditBefore-timing.secondsPerTick))<1e-12,
                "forecast changed authored credit consumption");
            const double elapsed=out.wall-start;
            const bool due=out.wall>=deadline;
            const bool yielded=route==Route::Original?budget.shouldYield(advanced,elapsed,due)
                :remainder.shouldYield(advanced,elapsed,due);
            if(yielded) {
                if(route!=Route::Original) remainder.recordActualBudgetYield(elapsed);
                ++out.budgetYields; break;
            }
        }
        out.maxTicks=std::max(out.maxTicks,advanced);
        if(out.wall>=deadline) {
            deadline+=1.0/60.0;
            if(deadline+1.0/60.0<=out.wall) deadline=out.wall+1.0/60.0;
            // Measured required service input agrees with actual service cost
            // in this contract. Movies/modal waits are never modeled as CPU.
            out.wall+=(paintMs+extraMs)/1000.0; ++out.paints;
        } else if(!timing.unlimited()) out.wall+=0.001;
        require(std::abs(remainder.carriedRemainderSeconds())<=2.0/60.0+1e-12,
            "presentation debt escaped two-frame clamp");
    }
    require(out.ticks>0&&out.paints>0&&out.maxTicks<=16,"starved clock or changed catch-up bound");
    return out;
}

void bounds() {
    RuntimePresentationRemainderReferenceScope optimized(false);
    RuntimePresentationWorkBudget old(1.0/60.0,8.3,2.6,187.5);
    RuntimePresentationBudgetRemainder q;
    const auto begin=[&](bool local=true,bool unlimited=false,int token=5,double extra=0.0) {
        q.begin(old,1.0/60.0,8.3,extra,2.6,187.5,local,unlimited,token);
    };
    begin(); require(q.enabled(),"normal finite admission");
    require(!q.shouldYield(0,10.0,true)&&!q.shouldYield(3,10.0,false),"split tick/no due paint");
    require(!q.shouldYield(3,std::numeric_limits<double>::quiet_NaN(),true),"invalid elapsed yield");
    require(q.carriedRemainderSeconds()==0.0,"readiness exit fabricated debt");
    require(q.shouldYield(3,0.0078,true),"actual 3-tick forecast not used");
    q.recordActualBudgetYield(0.0078);
    const double carried=q.carriedRemainderSeconds();
    require(carried>0.0,"underspend did not carry");
    q.recordActualBudgetYield(0.0078);
    require(q.carriedRemainderSeconds()==carried,"same batch recorded twice");
    begin(); require(q.carriedRemainderSeconds()==carried,"steady batch lost remainder");
    {RuntimePresentationRemainderReferenceScope ref(true);
        require(q.shouldYield(4,0.0104,true)==old.shouldYield(4,0.0104,true),"reference is not old budget");
        require(!q.enabled()&&q.carriedRemainderSeconds()==0.0,"reference did not reset");
        {RuntimePresentationRemainderReferenceScope nested(false);begin();require(q.enabled(),"nested optimized mask");}
        begin();require(!q.enabled(),"nested reference restore");}
    begin();require(q.enabled(),"outer optimized restore");
    require(q.shouldYield(1,10.0,true),"large overspend must yield");q.recordActualBudgetYield(10.0);
    require(q.carriedRemainderSeconds()==-2.0/60.0,"negative clamp");
    begin(true,false,4);require(q.carriedRemainderSeconds()==0.0,"mode change kept debt");
    begin(false);require(!q.enabled()&&q.carriedRemainderSeconds()==0.0,"modal/network/minimized reset");
    begin(true,true);require(!q.enabled(),"unlimited admission changed");
    q.begin(old,1.0/60.0,8.3,0.0,0.0,187.5,true,false);
    require(!q.enabled(),"unknown estimate admitted");
    q.begin(old,1.0/60.0,8.3,0.0,5.0,187.5,true,false);
    require(!q.enabled(),"share over .8 admitted");
    begin(true,false,5,1000.0);require(!q.enabled(),"unamortizable service admitted");
}
}

int main() {
    if(runUnchangedPresentationBudgetSmokeMain()!=0) return 1;
    try {
        RemainderClock::bounds();
        int comparisons=0,enabled=0;
        for(int speed=0;speed<=6;++speed)
            for(double tick:{0.1,2.6,4.0,20.0})
                for(double paint:{1.0,8.3,10.0,100.0})
                    for(double extra:{0.0,1.0,5.0}) {
                        const auto a=RemainderClock::run(speed,tick,paint,extra,RemainderClock::Route::Original);
                        const auto b=RemainderClock::run(speed,tick,paint,extra,RemainderClock::Route::Reference);
                        const auto c=RemainderClock::run(speed,tick,paint,extra,RemainderClock::Route::Optimized);
                        require(a.sameTimeline(b),"reference changed complete original timeline");
                        if(speed==6) require(a.sameTimeline(c),"unlimited scheduling/throughput changed");
                        else {
                            const double requested=1.0/runtimeGameSpeedTiming(speed,30).secondsPerTick;
                            const double floor=std::min(requested,a.tps());
                            const double quantization=16.0/std::min(a.wall,c.wall);
                            if(c.tps()+quantization<floor) {
                                std::cerr<<std::setprecision(12)<<"TPS regression speed="<<speed
                                    <<" tickMs="<<tick<<" paintMs="<<paint<<" overheadMs="<<extra
                                    <<" oldTPS="<<a.tps()<<" newTPS="<<c.tps()<<" targetFloor="<<floor
                                    <<" allowedQuantization="<<quantization<<'\n';
                                throw std::runtime_error("finite TPS lower than min(requested,old) beyond catch-up quantization");
                            }
                        }
                        enabled+=c.enabledBatches>0?1:0;++comparisons;
                    }
        require(enabled>0,"all clock cases silently fell back");
        for(int speed:{3,4,5}) for(double extra:{0.0,1.0,5.0}) {
            const auto a=RemainderClock::run(speed,2.6,8.3,extra,RemainderClock::Route::Original,true);
            const auto b=RemainderClock::run(speed,2.6,8.3,extra,RemainderClock::Route::Reference,true);
            const auto c=RemainderClock::run(speed,2.6,8.3,extra,RemainderClock::Route::Optimized,true);
            require(a.sameTimeline(b),"evolving reference diverged");
            const double requested=1.0/runtimeGameSpeedTiming(speed,30).secondsPerTick;
            require(c.tps()+16.0/std::min(a.wall,c.wall)>=std::min(requested,a.tps()),
                "evolving costs regressed finite TPS beyond quantization");
            ++comparisons;
        }
        std::cout<<"Presentation remainder clock: original84=unchanged comparisons="<<comparisons
            <<" positiveEnabledCases="<<enabled
            <<" finiteTPS=min(requested,old)-catchUpQuantization referenceTimeline=exact unlimitedTimeline=exact passed=yes\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
