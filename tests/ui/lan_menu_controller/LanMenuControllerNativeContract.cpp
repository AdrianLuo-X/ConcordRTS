#include "runtime/menu/RuntimeLanMenuController.h"
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include "ui/network/win32/RuntimeLanMenuWidgetViewWin32.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <thread>
#include <type_traits>
using namespace concord::lan_menu;
static_assert(std::is_standard_layout_v<RuntimeLanMenuWidgetViewWin32>);
static_assert(sizeof(RuntimeLanMenuWidgetViewWin32)==36*sizeof(HWND));
static_assert(alignof(RuntimeLanMenuWidgetViewWin32)==alignof(HWND));
static_assert(offsetof(RuntimeLanMenuWidgetViewWin32,hostWindow)==0);
static_assert(offsetof(RuntimeLanMenuWidgetViewWin32,window)==sizeof(HWND));
static_assert(offsetof(RuntimeLanMenuWidgetViewWin32,networkHint)==35*sizeof(HWND));
static_assert(sizeof(RuntimeMultiplayerMenuNetwork)==1);
static_assert(PlayerNameEdit==4101&&ResumeSwapButton==4125);
int main(){unsigned checks=0,failures=0;auto require=[&](bool b,const char* why){++checks;if(!b){++failures;std::cerr<<why<<'\n';}};
 RuntimeLanMenuWidgetViewWin32 view;require(!view.hostWindow&&!view.window&&!view.roomList&&!view.passwordEdit&&!view.networkHint,"all representative borrowed widget defaults null");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Owned LAN contract",WS_OVERLAPPEDWINDOW,50,50,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(parent!=nullptr,"actual native parent creation");if(!parent)return 1;
 RuntimeWindowId retired{};
 {RuntimeWindowParentWin32 lease(parent);retired=lease.id();const auto r=resolveRuntimeWindowParentWin32(retired);require(r.supported&&r.window==parent,"same thread parent identity");
  bool crossSupported=true;std::thread other([&]{crossSupported=resolveRuntimeWindowParentWin32(retired).supported;});other.join();require(!crossSupported,"cross thread identity rejected");
  view.hostWindow=parent;view.window=CreateWindowExW(0,L"STATIC",L"LAN child",WS_CHILD,0,0,780,540,parent,nullptr,GetModuleHandleW(nullptr),nullptr);require(view.window&&GetParent(view.window)==parent,"native child parent preserved");
  HWND borrowed=view.window;{RuntimeLanMenuWidgetViewWin32 copy=view;require(copy.window==borrowed,"private view copies only borrows");}require(IsWindow(borrowed),"view destructor never owns native child");require(DestroyWindow(borrowed)!=FALSE,"destroy owned child");
 }
 require(IsWindow(parent),"lease does not destroy parent");require(!resolveRuntimeWindowParentWin32(retired).supported,"revoked parent identity rejected");require(resolveRuntimeWindowParentWin32({}).supported&&resolveRuntimeWindowParentWin32({}).window==nullptr,"zero standalone identity");require(!resolveRuntimeWindowParentWin32({~std::uint64_t{0}}).supported,"unknown identity rejected");
 require(DestroyWindow(parent)!=FALSE,"destroy own parent");std::cout<<"LAN native ABI/widget/parent: checks="<<checks<<" failures="<<failures<<" skipped=0\n";return failures?1:0;
}
