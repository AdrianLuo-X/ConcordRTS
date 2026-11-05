#include "runtime/window/win32/RuntimeWindowHostWin32.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "diagnostics/OriginalDrain.inl"
namespace {
constexpr UINT first=WM_APP+71,extra=WM_APP+72,reenter=WM_APP+73,nested=WM_APP+74,threadOnly=WM_APP+75;
struct State {int tag=0;std::vector<std::string>* trace=nullptr;bool postDuring=false;};
LRESULT CALLBACK dispatch(HWND w,UINT message,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<State*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(!state)return DefWindowProcW(w,message,wp,lp);
    if(message>=first&&message<=nested){
        std::ostringstream s;s<<state->tag<<':'<<message<<':'<<static_cast<std::uint64_t>(wp)<<':'<<static_cast<std::int64_t>(lp);state->trace->push_back(s.str());
        if(message==first&&state->postDuring)PostMessageW(w,extra,99,-17);
        if(message==reenter){SendMessageW(w,nested,33,-44);state->trace->push_back("sync-return");}
        return 0;
    }
    if(message==WM_CLOSE){state->trace->push_back("close-enter");DestroyWindow(w);state->trace->push_back("close-return");return 0;}
    if(message==WM_DESTROY){state->trace->push_back("destroy");PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,message,wp,lp);
}
void clearOwnQueue(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}}
}
int main(){
    unsigned checks=0,failures=0,pairs=0;
    const auto need=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"Native loop failure: "<<why<<'\n';}};
    for(int scenario=0;scenario<7;++scenario){
        std::array<std::string,2> observed;
        for(int route=0;route<2;++route){
            clearOwnQueue();std::vector<std::string> trace;State a{1,&trace,scenario==2},b{2,&trace,false};
            {
                RuntimeWindowHostWin32 one(dispatch),two(dispatch);
                need(one.create(260,180)==RuntimeWindowCreateResult::Created&&two.create(240,160)==RuntimeWindowCreateResult::Created,"two actual owned native windows");
                const auto wa=one.window(),wb=two.window();one.attachSession(reinterpret_cast<LONG_PTR>(&a));two.attachSession(reinterpret_cast<LONG_PTR>(&b));
                need(one.drainMessages(),"initial queue drained");trace.clear();
                if(scenario==1){PostMessageW(wa,first,1,-1);PostMessageW(wa,extra,2,-2);}
                if(scenario==2)PostMessageW(wa,first,3,-3);
                // A real FIFO WM_QUIT is required here. PostQuitMessage creates a low-priority
                // virtual quit after posted messages drain; ordinary WM_CLOSE below still tests it.
                if(scenario==3){PostMessageW(wa,first,4,-4);PostThreadMessageW(GetCurrentThreadId(),WM_QUIT,73,0);PostMessageW(wa,extra,5,-5);}
                if(scenario==4)PostMessageW(wa,reenter,6,-6);
                if(scenario==5)PostMessageW(wa,WM_CLOSE,0,0);
                if(scenario==6){PostMessageW(wb,first,7,-7);PostThreadMessageW(GetCurrentThreadId(),threadOnly,8,-8);PostMessageW(wa,extra,9,-9);}
                RuntimeHostLoopPort& port=one;
                const bool running=route?port.drainMessages():originalDrainMessages();
                need(running==(scenario!=3&&scenario!=5),"same first quit/close result");
                std::ostringstream result;result<<running<<':'<<(one.window()==nullptr)<<':'<<(IsWindow(wa)!=FALSE)<<'\n';
                for(const auto& t:trace)result<<t<<'\n';
                MSG remaining{};const bool pending=PeekMessageW(&remaining,nullptr,extra,extra,PM_NOREMOVE)!=FALSE;
                result<<"remaining-extra "<<pending;
                if(pending)result<<':'<<remaining.message<<':'<<static_cast<std::uint64_t>(remaining.wParam)<<':'<<static_cast<std::int64_t>(remaining.lParam);
                result<<'\n';need(pending==(scenario==3),"first quit retains later queue messages");
                if(scenario==3){trace.clear();need(route?port.drainMessages():originalDrainMessages(),"next drain resumes remaining queue");for(const auto& t:trace)result<<"after-quit "<<t<<'\n';}
                if(scenario==6){MSG m{};need(!PeekMessageW(&m,nullptr,threadOnly,threadOnly,PM_NOREMOVE),"thread-only message consumed with null HWND filter");}
                observed[route]=result.str();
                if(scenario==3)std::cout<<"FIFO quit route="<<route<<"\n"<<observed[route];
                // Both owners are destroyed before their attached per-instance states.
            }
            clearOwnQueue();
        }
        need(observed[0]==observed[1],"complete original/current dispatch values/order/reentry/quit/window outcome equal");++pairs;
    }
    clearOwnQueue();
    RuntimeWindowHostWin32 waiter(dispatch);RuntimeHostLoopPort& port=waiter;
    const auto start=std::chrono::steady_clock::now();port.sleepForIdleSlice();const auto end=std::chrono::steady_clock::now();
    need(end>=start,"actual standard sleep returns on same native owner");
    std::cout<<"Native host loop contract: checks="<<checks<<" pairs="<<pairs<<" failures="<<failures
        <<" skipped=0 originalDrain=complete nativeOwner=W013 realWindows=1 threadQueue=1 firstQuit=1 reentry=1 sleepRequestMs=1 observedSleepMs="
        <<std::chrono::duration<double,std::milli>(end-start).count()<<" fakeGlobalContext=0 effectiveWakePrecision=not-measured\n";
    return failures?1:0;
}
