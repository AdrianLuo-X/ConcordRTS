#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <iostream>
#include <thread>
#include <type_traits>
static_assert(sizeof(RuntimeInputPixelPoint)==sizeof(POINT));
static_assert(alignof(RuntimeInputPixelPoint)==alignof(POINT));
int main(){unsigned checks=0,failures=0;auto require=[&](bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"Main native parent: "<<why<<"\n";}};
 auto zero=resolveRuntimeWindowParentWin32({});require(zero.supported&&zero.window==nullptr,"zero ID standalone");
 require(!resolveRuntimeWindowParentWin32({~std::uint64_t{0}}).supported,"unknown identity rejected");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Owned Main parent contract",WS_OVERLAPPEDWINDOW,40,40,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"real same-thread parent");if(!parent)return 1;RuntimeWindowId old{};
 {RuntimeWindowParentWin32 lease(parent);old=lease.id();const auto r=resolveRuntimeWindowParentWin32(old);require(old.value!=0&&r.supported&&r.window==parent,"allocated identity resolves actual parent");
  bool supported=true;std::thread foreign([&]{supported=resolveRuntimeWindowParentWin32(old).supported;});foreign.join();require(!supported,"cross-thread borrow rejected");
  require(IsWindow(parent)!=FALSE,"lease never takes native ownership");
 }
 require(!resolveRuntimeWindowParentWin32(old).supported,"expired identity revoked");require(IsWindow(parent)!=FALSE,"release retains parent");
 {RuntimeWindowParentWin32 lease(parent);require(lease.id()!=old,"fresh lease never revives stale identity");require(DestroyWindow(parent)!=FALSE,"destroy owned actual parent");const auto dead=resolveRuntimeWindowParentWin32(lease.id());require(dead.supported&&dead.window==parent&&!IsWindow(dead.window),"existing lease resolves identity; native caller checks destroyed handle");}
 require(!IsWindow(parent),"fixture parent retired");
 std::cout<<"Main native parent contract: checks="<<checks<<" failures="<<failures<<" skipped=0 allocatedId=1 nativeOwnership=borrow\n";return failures?1:0;}
