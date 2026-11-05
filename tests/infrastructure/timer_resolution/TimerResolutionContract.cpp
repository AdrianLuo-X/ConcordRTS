#include "runtime/timing/RuntimeTimerResolutionScope.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <vector>
struct Event {char kind;std::uint32_t period;bool operator==(const Event&)const=default;};
struct Port final:RuntimeTimerResolutionPort {
    bool beginResult=true,endResult=true;
    int balance=0;
    std::vector<Event> events;
    Port(){events.reserve(256);}
    bool begin(std::uint32_t period)noexcept override{events.push_back({'b',period});if(beginResult)++balance;return beginResult;}
    bool end(std::uint32_t period)noexcept override{events.push_back({'e',period});if(endResult)--balance;return endResult;}
};
static_assert(!std::is_copy_constructible_v<RuntimeTimerResolutionScope>);
static_assert(!std::is_move_constructible_v<RuntimeTimerResolutionScope>);
static_assert(std::is_nothrow_destructible_v<RuntimeTimerResolutionScope>);
int main(){
    unsigned checks=0,failures=0,cases=0;
    const auto need=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"Timer lifetime failure: "<<text<<'\n';}};
    for(const auto period:std::array<std::uint32_t,5>{0,1,2,16,0xffffffffu})for(const bool begin:{false,true})for(const bool end:{false,true}){
        ++cases;Port p;p.beginResult=begin;p.endResult=end;
        {RuntimeTimerResolutionScope scope(p,period);
            need(scope.beginSucceeded()==begin&&scope.endPending()==begin&&!scope.endAttempted(),"original begin result and ownership captured");
            need(p.events==std::vector<Event>{{'b',period}},"begin exactly once with exact input period, no clamp");
            need(scope.close()==(!begin||end),"first close reports actual end result or no request");
            need(!scope.endPending()&&scope.endAttempted()==begin&&scope.beginSucceeded()==begin,"begin outcome immutable, end attempt closes ownership");
            const auto before=p.events;
            for(int i=0;i<8;++i)need(scope.close()==(!begin||end)&&p.events==before,"repeated close retains real failure without a second end");
        }
        need(p.events==(begin?std::vector<Event>{{'b',period},{'e',period}}:std::vector<Event>{{'b',period}}),"destructor never ends failed begin or repeats attempted end");
        need(p.balance==(begin&&!end?1:0),"failed end remains unresolved in actual port, never claimed restored");
    }
    for(const bool begin:{false,true})for(const bool end:{false,true})for(int exit=0;exit<3;++exit){
        ++cases;Port p;p.beginResult=begin;p.endResult=end;
        const auto leave=[&](){RuntimeTimerResolutionScope scope(p);need(scope.beginSucceeded()==begin,"owned early exit begin");
            if(exit==1)return;if(exit==2)throw 17;};
        try{leave();}catch(int value){need(exit==2&&value==17,"same original callback exception reaches caller");}
        need(p.events==(begin?std::vector<Event>{{'b',1},{'e',1}}:std::vector<Event>{{'b',1}}),"destructor pairs normal/early return/exception exactly once");
        need(p.balance==(begin&&!end?1:0),"destructor failure kept as failure/no retry");
    }
    {
        ++cases;Port p;
        {RuntimeTimerResolutionScope outer(p,1);{RuntimeTimerResolutionScope inner(p,2);need(p.balance==2,"independent nested requests");need(inner.close(),"inner explicit close");}
            need(p.balance==1&&outer.endPending(),"inner close never ends outer request");}
        need(p.events==std::vector<Event>{{'b',1},{'b',2},{'e',2},{'e',1}}&&p.balance==0,"nested LIFO request periods remain paired");
    }
    for(int outcome=0;outcome<3;++outcome){
        ++cases;Port p;
        try{RuntimeTimerResolutionScope timer(p);p.events.push_back({'v',1});
            const auto callback=[&](){p.events.push_back({'f',1});if(outcome==2)throw 23;return outcome==0;};
            const bool played=callback();need(played==(outcome==0)&&timer.endPending()&&p.balance==1,"scope remains alive across callback success/cancel");
            p.events.push_back({'x',1});need(timer.close(),"close after callback returns");
        }catch(int value){need(outcome==2&&value==23,"callback fault propagated through balanced scope");}
        need(p.events.front()==Event{'b',1}&&p.events.back()==Event{'e',1}&&p.balance==0,"callback cancel/fault ends after callback, not during it");
        unsigned begins=0,ends=0;for(const auto& e:p.events){begins+=e.kind=='b';ends+=e.kind=='e';}
        need(begins==1&&ends==1,"one pair spans callback; real Bink proof is a separate native game fixture");
    }
    std::cout<<"Timer resolution lifetime contract: checks="<<checks<<" cases="<<cases<<" failures="<<failures
        <<" skipped=0 matrix=20 earlyExit=12 nested=1 callbacks=3 perInstancePorts=1 globalFakeContext=0 nativeCalls=not-run\n";
    return failures?1:0;
}
