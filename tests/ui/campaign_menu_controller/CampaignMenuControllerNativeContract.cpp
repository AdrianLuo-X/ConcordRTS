#include "runtime/menu/RuntimeCampaignMenuController.h"
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include "runtime/window/win32/RuntimeMenuEventWindowWin32.h"
#include <iostream>
#include <array>
#include <cstddef>
using namespace concord::campaign_menu;
static_assert(sizeof(RuntimeInputPixelPoint)==sizeof(POINT));
static_assert(alignof(RuntimeInputPixelPoint)==alignof(POINT));
static_assert(sizeof(RuntimeInputPixelRect)==sizeof(RECT));
static_assert(offsetof(RuntimeInputPixelRect,bottom)==offsetof(RECT,bottom));
RECT originalFit(const RECT& c){const int w=std::max(1L,c.right-c.left),h=std::max(1L,c.bottom-c.top);const double scale=std::min(w/800.0,h/600.0);const int fw=std::max(1,static_cast<int>(800*scale)),fh=std::max(1,static_cast<int>(600*scale));const int x=c.left+(w-fw)/2,y=c.top+(h-fh)/2;return {x,y,x+fw,y+fh};}
int main(){unsigned checks=0,failures=0;auto require=[&](bool b,const char* why){++checks;if(!b){++failures;std::cerr<<why<<"\n";}};
 const std::array<RECT,9> clients{{{0,0,800,600},{0,0,1280,720},{0,0,1024,768},{0,0,1024,640},{11,-15,811,585},{3,5,3,5},{7,9,6,8},{0,0,1,900},{0,0,1400,1}}};
 for(const auto& c:clients){const auto n=originalFit(c);const auto v=campaignShellClientRect({c.left,c.top,c.right,c.bottom});require(v.left==n.left&&v.top==n.top&&v.right==n.right&&v.bottom==n.bottom,"original SDK fit");for(const auto p:std::array<POINT,7>{{{n.left,n.top},{n.right-1,n.bottom-1},{n.right,n.bottom},{n.left-1,n.top},{n.left,n.bottom},{n.left+(n.right-n.left)/2,n.top+(n.bottom-n.top)/2},{-32768,32767}}}){const auto point=campaignShellPoint({c.left,c.top,c.right,c.bottom},p.x,p.y);if(!PtInRect(&n,p))require(point.x==-1&&point.y==-1,"letterbox/half-open reject");else require(point.x==(p.x-n.left)*800/std::max(1L,n.right-n.left)&&point.y==(p.y-n.top)*600/std::max(1L,n.bottom-n.top),"original SDK point mapping");}}
 for(const auto r:std::array<RECT,4>{{{1,2,8,9},{0,0,0,0},{5,7,3,2},{-20,-30,10,20}}})for(int x=-21;x<=11;++x)for(int y=-31;y<=21;++y)require(campaignPointInRect({r.left,r.top,r.right,r.bottom},{x,y})==(PtInRect(&r,POINT{x,y})!=FALSE),"real SDK PtInRect oracle");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Owned Campaign contract",WS_OVERLAPPEDWINDOW,50,50,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(parent!=nullptr,"actual parent creation");if(!parent)return 1;RuntimeWindowId old{};
 {RuntimeWindowParentWin32 lease(parent);old=lease.id();const auto r=resolveRuntimeWindowParentWin32(old);require(r.supported&&r.window==parent,"original parent resolver");RuntimeMenuEventWindowWin32 slot;require(!slot.value,"callback default");{RuntimeMenuEventWindowBorrowWin32 a(slot,parent);require(slot.value==parent,"outer callback");{RuntimeMenuEventWindowBorrowWin32 b(slot,nullptr);require(!slot.value,"nested callback");}require(slot.value==parent,"nested restore");}require(!slot.value&&IsWindow(parent),"no native ownership");}
 require(!resolveRuntimeWindowParentWin32(old).supported,"revoked identity");require(resolveRuntimeWindowParentWin32({}).supported,"standalone zero");require(!resolveRuntimeWindowParentWin32({~std::uint64_t{0}}).supported,"unknown identity");require(DestroyWindow(parent)!=FALSE,"destroy own parent");std::cout<<"Campaign native ABI/layout/parent: checks="<<checks<<" failures="<<failures<<" skipped=0 SDK-oracle=PtInRect\n";return failures?1:0;}
