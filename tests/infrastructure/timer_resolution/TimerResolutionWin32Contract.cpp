#include "runtime/timing/win32/RuntimeTimerResolutionScopeWin32.h"
#include <iostream>
#include <type_traits>
static_assert(!std::is_move_constructible_v<RuntimeWin32TimerResolutionScope>);
static_assert(!std::is_copy_constructible_v<RuntimeWin32TimerResolutionScope>);
int main(){
    unsigned checks=0,failures=0,cases=0;
    const auto need=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"Native timer failure: "<<text<<'\n';}};
    const auto paired=[&](const RuntimeWin32TimerResolutionPort& port){const auto& d=port.diagnostics();
        return d.periodMilliseconds==1&&d.beginCalls==1&&d.endCalls==1&&d.beginResult==TIMERR_NOERROR&&d.endResult==TIMERR_NOERROR;};
    const auto originalBegin=timeBeginPeriod(1);const bool originalActive=originalBegin==TIMERR_NOERROR;
    const auto originalEnd=originalActive?timeEndPeriod(1):0xffffffffu;
    ++cases;
    {RuntimeWin32TimerResolutionScope current;
        need(current.beginSucceeded()==originalActive,"same real original begin result");
        need(current.diagnostics().beginResult==originalBegin&&current.diagnostics().beginCalls==1&&current.diagnostics().endCalls==0,"actual private MM begin and zero premature end");
        need(current.close()==(originalEnd==TIMERR_NOERROR),"same real end result");
        const auto& d=current.diagnostics();need(d.endResult==originalEnd&&d.endCalls==(originalActive?1u:0u),"same actual paired end count and result");
        need(current.close()==(originalEnd==TIMERR_NOERROR)&&current.diagnostics().endCalls==(originalActive?1u:0u),"repeated close cannot duplicate native end");}
    for(int outcome=0;outcome<3;++outcome){
        ++cases;RuntimeWin32TimerResolutionPort port;
        const auto leave=[&](){RuntimeTimerResolutionScope scope(port);need(scope.beginSucceeded(),"actual native early exit scope begins");if(outcome==1)return;if(outcome==2)throw 31;};
        try{leave();}catch(int value){need(outcome==2&&value==31,"actual native callback exception preserved");}
        need(paired(port),"actual native destructor pairs normal/early return/exception");
    }
    ++cases;
    {RuntimeWin32TimerResolutionPort outerPort,innerPort;
        {RuntimeTimerResolutionScope outer(outerPort);{RuntimeTimerResolutionScope inner(innerPort);need(inner.close(),"actual nested explicit close");}
            need(paired(innerPort)&&outerPort.diagnostics().endCalls==0,"actual inner request never ends outer");}
        need(paired(outerPort)&&paired(innerPort),"actual nested requests independently paired");}
    need(originalActive&&originalEnd==TIMERR_NOERROR,"native successful begin/end proof required on this Windows host");
    std::cout<<"Native timer resolution contract: checks="<<checks<<" cases="<<cases<<" failures="<<failures
        <<" skipped=0 originalBegin="<<originalBegin<<" originalEnd="<<originalEnd
        <<" actualWinmm=1 globalFakeContext=0 noEffectiveResolutionMeasurement=1\n";
    return failures?1:0;
}
