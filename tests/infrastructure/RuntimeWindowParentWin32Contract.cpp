#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include <future>
#include <iostream>
namespace {int checks=0,failures=0;
void check(bool value,const char* what) {++checks;if(!value){++failures;std::cerr<<what<<'\n';}}
}
int main() {
    auto empty=resolveRuntimeWindowParentWin32({});check(empty.supported&&empty.window==nullptr,"unhosted");
    check(!resolveRuntimeWindowParentWin32({999999999}).supported,"unknown identity");
    HWND host=CreateWindowExW(0,L"STATIC",L"Owned parent contract",WS_OVERLAPPEDWINDOW,
        0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host!=nullptr,"create owned host");if(!host)return 1;
    RuntimeWindowId stale;
    {
        RuntimeWindowParentWin32 lease(host);stale=lease.id();
        check(stale.value!=0,"allocated id");
        auto resolved=resolveRuntimeWindowParentWin32(stale);check(resolved.supported&&resolved.window==host,"typed resolve");
        RuntimeWindowParentWin32 nested(host);check(nested.id()!=stale,"nested unique");
        check(resolveRuntimeWindowParentWin32(nested.id()).window==host,"nested borrow");
        const bool foreign=std::async(std::launch::async,[stale]{return resolveRuntimeWindowParentWin32(stale).supported;}).get();
        check(!foreign,"cross-thread token rejected");
        check(IsWindow(host)!=FALSE,"lease does not own OS window");
    }
    check(!resolveRuntimeWindowParentWin32(stale).supported,"revocation");
    check(IsWindow(host)!=FALSE,"lease exit preserves parent");
    {
        RuntimeWindowParentWin32 next(host);check(next.id()!=stale,"no identity reuse");
        check(!resolveRuntimeWindowParentWin32(stale).supported,"stale still rejected");
        check(DestroyWindow(host)!=FALSE,"destroy only owned test window");
        const auto gone=resolveRuntimeWindowParentWin32(next.id());
        check(gone.supported&&gone.window==host&&!IsWindow(gone.window),"private backend preserves parent-dead detection");
    }
    RuntimeWindowParentWin32 nullLease(nullptr);check(nullLease.id().value==0,"null standalone");
    std::cout<<"Window parent: checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
