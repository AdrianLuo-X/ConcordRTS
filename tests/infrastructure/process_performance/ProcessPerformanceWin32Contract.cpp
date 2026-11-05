#include "runtime/performance/win32/RuntimeProcessPerformanceScopeWin32.h"
#include <iostream>
#include <type_traits>
namespace Original {
#include "diagnostics/RuntimeProcessPerformanceScope.original.h"
}
static_assert(!std::is_move_constructible_v<RuntimeWin32ProcessPerformanceScope>);
static_assert(!std::is_copy_constructible_v<RuntimeWin32ProcessPerformanceScope>);
using Policy=RuntimeProcessPowerPolicy;
using Information=BOOL(WINAPI*)(HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD);
int main(){
    unsigned checks=0,failures=0;
    const auto need=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"Native process policy failure: "<<text<<'\n';}};
    const auto kernel=GetModuleHandleW(L"kernel32.dll");
    const auto get=kernel?reinterpret_cast<Information>(GetProcAddress(kernel,"GetProcessInformation")):nullptr;
    const auto set=kernel?reinterpret_cast<Information>(GetProcAddress(kernel,"SetProcessInformation")):nullptr;
    const auto read=[&](Policy& p){PROCESS_POWER_THROTTLING_STATE s{1,0,0};
        if(!get||!get(GetCurrentProcess(),ProcessPowerThrottling,&s,sizeof(s)))return false;
        p={s.Version,s.ControlMask,s.StateMask};return true;};
    Policy before;
    if(!get||!set||!read(before)){
        const auto error=GetLastError();
        const bool unsupported=!get||!set||error==ERROR_INVALID_PARAMETER||error==ERROR_NOT_SUPPORTED;
        need(unsupported,"unexpected read failure must not be called unsupported");
        {Original::RuntimeProcessPerformanceScope old;RuntimeWin32ProcessPerformanceScope current;
            need(old.appliedMask()==0&&current.appliedMask()==0,"unavailable current-process policy is no-op");
            need(old.restore()&&current.restore(),"unavailable scopes restore no-op");}
        std::cout<<"Native process performance contract: checks="<<checks<<" failures="<<failures
            <<" skipped=0 capability="<<(unsupported?"unsupported":"failed")<<" error="<<error<<" optionalPolicy=1\n";
        return failures?1:0;
    }
    Policy oldDuring,oldRestored,oldAfter,currentDuring,currentRestored,currentAfter;std::uint32_t oldMask=0,currentMask=0;
    {
        Original::RuntimeProcessPerformanceScope original;oldMask=original.appliedMask();
        need(read(oldDuring),"query real old policy during scope");
        need(original.restore(),"old explicit restore succeeds");need(read(oldRestored)&&oldRestored==before,"old complete previous policy restored");
    }
    need(read(oldAfter)&&oldAfter==before,"old destructor preserves restoration");
    {
        RuntimeWin32ProcessPerformanceScope current;currentMask=current.appliedMask();
        need(read(currentDuring),"query real current policy during scope");
        need(current.restore(),"current explicit restore succeeds");need(read(currentRestored)&&currentRestored==before,"current complete previous policy restored");
    }
    need(read(currentAfter)&&currentAfter==before,"current destructor preserves restoration");
    need(oldMask==currentMask,"real native applied mask matches complete original");
    need(oldDuring==currentDuring,"all real native policy fields during scope equal original");
    need(oldRestored==currentRestored&&oldAfter==currentAfter,"all real native policy fields after explicit/destructor restore equal original");
    need(currentMask==0||currentMask==1||currentMask==5,"only original full/fallback/unavailable mask");
    if(currentMask){need((currentDuring.controlMask&currentMask)==currentMask,"real requested control bits enabled");
        need((currentDuring.stateMask&currentMask)==0,"real requested throttling state bits cleared");
        need((currentDuring.controlMask&~currentMask)==(before.controlMask&~currentMask)
            &&(currentDuring.stateMask&~currentMask)==(before.stateMask&~currentMask),"real unrelated native bits preserved");}
    if(currentAfter!=before){PROCESS_POWER_THROTTLING_STATE restore{before.version,before.controlMask,before.stateMask};
        const auto repaired=set(GetCurrentProcess(),ProcessPowerThrottling,&restore,sizeof(restore));
        std::cerr<<"Native fixture emergency current-process restoration="<<repaired<<"; original failure retained\n";}
    const auto emit=[](const Policy& p){std::cout<<p.version<<','<<p.controlMask<<','<<p.stateMask;};
    std::cout<<"Native policy values: before=";emit(before);std::cout<<" oldDuring=";emit(oldDuring);
    std::cout<<" currentDuring=";emit(currentDuring);std::cout<<" oldAfter=";emit(oldAfter);std::cout<<" currentAfter=";emit(currentAfter);std::cout<<'\n';
    std::cout<<"Native process performance contract: checks="<<checks<<" failures="<<failures
        <<" skipped=0 capability=available originalMask="<<oldMask<<" currentMask="<<currentMask
        <<" completeOriginalHeader=1 actualCurrentProcess=1 exactDuring="<<(oldDuring==currentDuring)
        <<" exactAfter="<<(oldAfter==before&&currentAfter==before)<<" globalFakeContext=0\n";
    return failures?1:0;
}
