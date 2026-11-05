#include "runtime/config/RuntimeConfig.h"
#include "runtime/window/win32/RuntimePersistentMenuHostWin32.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
namespace {
bool rejectParent=false;unsigned rejected=0;
LRESULT CALLBACK rejectOriginalParent(int code,WPARAM value,LPARAM data) {
    if(rejectParent&&code==HCBT_CREATEWND){wchar_t name[96]{};GetClassNameW(reinterpret_cast<HWND>(value),name,96);if(std::wstring_view(name)==L"ConcordRTS.RuntimeMenuHost"){++rejected;return 1;}}
    return CallNextHookEx(nullptr,code,value,data);
}
}
int main(){unsigned checks=0,failures=0,mappedAfterOriginalClose=0;const auto require=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<why<<'\n';}};
 RuntimePersistentMenuHostWin32 empty;require(!empty.valid()&&!empty.nativeWindow()&&empty.parentId().value==0,"original empty guard");empty.close();require(!empty.valid(),"original empty close idempotent");
 for(const auto& [width,height]:std::array<std::pair<int,int>,3>{{{0,0},{1024,640},{30000,30000}}}){
  RuntimeConfig config;config.windowWidth=width;config.windowHeight=height;RuntimePersistentMenuHostWin32 host;host.create(config);require(host.valid(),"actual original native parent creation");if(!host.valid())continue;
  wchar_t text[256]{};GetWindowTextW(host.nativeWindow(),text,256);require(std::wstring_view(text)==L"Command & Conquer: Yuri's Revenge","original title");
  const auto style=static_cast<DWORD>(GetWindowLongPtrW(host.nativeWindow(),GWL_STYLE));require((style&WS_OVERLAPPEDWINDOW)==WS_OVERLAPPEDWINDOW&&(style&WS_CLIPCHILDREN),"original native style");
  RECT work{},actual{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);GetWindowRect(host.nativeWindow(),&actual);require(actual.right-actual.left<=work.right-work.left&&actual.bottom-actual.top<=work.bottom-work.top,"original work area clamp");require(actual.left==work.left+((work.right-work.left)-(actual.right-actual.left))/2&&actual.top==work.top+((work.bottom-work.top)-(actual.bottom-actual.top))/2,"original centered placement");
  HWND child=CreateWindowExW(0,L"STATIC",L"Owned child",WS_CHILD|WS_VISIBLE,1,2,3,4,host.nativeWindow(),nullptr,GetModuleHandleW(nullptr),nullptr);require(child&&GetParent(child)==host.nativeWindow(),"actual child parent");
  ShowWindow(host.nativeWindow(),SW_SHOW);UpdateWindow(host.nativeWindow());SendMessageW(host.nativeWindow(),WM_SIZE,0,MAKELPARAM(701,433));RECT bounds{};GetClientRect(child,&bounds);require(bounds.right==701&&bounds.bottom==433,"original WM_SIZE routes child fill");
  SendMessageW(host.nativeWindow(),WM_SETFOCUS,0,0);require(GetFocus()==child,"original focus routes visible child");
  RECT client{};GetClientRect(host.nativeWindow(),&client);const int w=std::max(1L,client.right),h=std::max(1L,client.bottom);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;HDC screen=GetDC(host.nativeWindow()),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(host.nativeWindow(),screen);require(memory&&bitmap&&pixels,"actual owned DIB");
  if(memory&&bitmap&&pixels){auto previous=SelectObject(memory,bitmap);std::fill_n(static_cast<std::uint32_t*>(pixels),static_cast<std::size_t>(w)*h,0x00abcdefU);const auto handled=SendMessageW(host.nativeWindow(),WM_ERASEBKGND,reinterpret_cast<WPARAM>(memory),0);GdiFlush();const auto* data=static_cast<const std::uint32_t*>(pixels);bool black=true;for(std::size_t n=0;n<static_cast<std::size_t>(w)*h;++n)black=black&&((data[n]&0x00ffffffU)==0U);require(handled==1&&black,"original entire parent background black");SelectObject(memory,previous);}
  if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);
  RuntimeWindowId retired{};{host.acquireParentBorrow();retired=host.parentId();require(resolveRuntimeWindowParentWin32(retired).window==host.nativeWindow(),"actual existing registry borrow");host.close();const auto after=resolveRuntimeWindowParentWin32(retired);mappedAfterOriginalClose+=after.supported;require(!after.supported&&host.parentId().value==0,"explicit close revokes before native destruction");require(!IsWindow(child),"original destroy retires native child");host.close();require(!host.valid()&&!host.nativeWindow(),"original repeated close safe");}
  require(!resolveRuntimeWindowParentWin32(retired).supported,"outer original lease eventually revokes");
 }
 for(bool direct:{false,true}) {RuntimePersistentMenuHostWin32 host;RuntimeConfig config;require(host.create(config),"current external native close host");host.acquireParentBorrow();const auto id=host.parentId();HWND parent=host.nativeWindow();require(resolveRuntimeWindowParentWin32(id).window==parent,"current original registry borrow");
   if(direct)DestroyWindow(parent);else SendMessageW(parent,WM_CLOSE,0,0);
   require(!host.valid()&&!host.nativeWindow()&&host.parentId().value==0&&!resolveRuntimeWindowParentWin32(id).supported,"actual direct destroy/WM_CLOSE revoke parent lease");host.close();require(!host.valid(),"external close then explicit cleanup safe");}
 rejectParent=true;HHOOK hook=SetWindowsHookExW(WH_CBT,rejectOriginalParent,nullptr,GetCurrentThreadId());require(hook!=nullptr,"actual own-thread create refusal hook");RuntimeConfig config;RuntimePersistentMenuHostWin32 rejectedHost;rejectedHost.create(config);require(!rejectedHost.valid()&&rejected==1,"actual original creation failure");if(hook)UnhookWindowsHookEx(hook);rejectParent=false;
 MSG message{};require(!PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE),"parent destroy does not add quit to caller queue");
 std::cout<<"Current persistent parent SDK: checks="<<checks<<" failures="<<failures<<" skipped=0 cases=3 currentMappedDeadIdentity="<<mappedAfterOriginalClose<<" healthyLeaseBeforeDestroy=1\n";return failures?1:0;
}
