#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#ifdef CONCORD_STARTUP_RECOVERY_PORT
#include "runtime/window/win32/RuntimeStartupRecoveryPromptWin32.h"
#endif
namespace {
struct Case {int command;bool reject;bool tryClose;};
struct Observation{Case scene;HWND own=nullptr;HWND dialog=nullptr;DWORD thread=0;unsigned creates=0,activated=0,closeIgnored=0;bool title=false,text=false,sameThread=false,taskDisabled=false,timeout=false;std::string expected;};
thread_local Observation* active=nullptr;
BOOL CALLBACK readBody(HWND child,LPARAM){char cls[64]{};GetClassNameA(child,cls,64);if(std::strcmp(cls,"Static")==0){const int n=GetWindowTextLengthA(child);if(n>0){std::string value(static_cast<std::size_t>(n)+1,'\0');GetWindowTextA(child,value.data(),n+1);value.resize(static_cast<std::size_t>(n));if(value==active->expected)active->text=true;}}return TRUE;}
void CALLBACK afterClose(HWND dialog,UINT,UINT_PTR timer,DWORD){KillTimer(dialog,timer);if(active&&IsWindow(dialog)){++active->closeIgnored;PostMessageA(dialog,WM_COMMAND,IDNO,0);}}
void CALLBACK deadline(HWND,UINT,UINT_PTR timer,DWORD){KillTimer(nullptr,timer);if(active){active->timeout=true;if(active->dialog&&IsWindow(active->dialog))EndDialog(active->dialog,0);}}
LRESULT CALLBACK hook(int code,WPARAM w,LPARAM l){if(!active)return CallNextHookEx(nullptr,code,w,l);HWND dialog=reinterpret_cast<HWND>(w);char cls[64]{};GetClassNameA(dialog,cls,64);if(std::strcmp(cls,"#32770")!=0)return CallNextHookEx(nullptr,code,w,l);
 if(code==HCBT_CREATEWND){++active->creates;if(active->scene.reject)return 1;}
 if(code==HCBT_ACTIVATE){active->dialog=dialog;++active->activated;char title[128]{};GetWindowTextA(dialog,title,128);active->title=std::strcmp(title,"ConcordRTS Multiplayer")==0;active->sameThread=GetWindowThreadProcessId(dialog,nullptr)==active->thread;active->taskDisabled=IsWindowEnabled(active->own)==FALSE;EnumChildWindows(dialog,readBody,0);
  if(active->scene.tryClose){PostMessageA(dialog,WM_SYSCOMMAND,SC_CLOSE,0);SetTimer(dialog,17,25,afterClose);}else PostMessageA(dialog,WM_COMMAND,active->scene.command,0);
 }return CallNextHookEx(nullptr,code,w,l);}
int originalChoice(const std::string& message){
        const int choice = MessageBoxA(
            nullptr,
            message.c_str(),
            "ConcordRTS Multiplayer",
            MB_YESNO | MB_ICONERROR | MB_TASKMODAL
        );
        return choice;
}
}
int main(){unsigned checks=0,failures=0;auto require=[&](bool v,const char* what){++checks;if(!v){++failures;std::cerr<<"Recovery native: "<<what<<"\n";}};const char* cls="ConcordStartupRecoveryNativeContract";WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName=cls;require(RegisterClassA(&wc)!=0,"class registration");
 constexpr std::array<Case,4> cases{{{IDYES,false,false},{IDNO,false,false},{IDNO,false,true},{0,true,false}}};unsigned dialogs=0,rejected=0;
 for(std::size_t index=0;index<cases.size();++index){Observation o;o.scene=cases[index];o.thread=GetCurrentThreadId();o.expected="The multiplayer match could not continue.\n\nReason: original recovery fixture "+std::to_string(index)+"\n\nWould you like to return to the main menu?";o.own=CreateWindowExA(0,cls,"Owned recovery fixture",WS_OVERLAPPEDWINDOW,40,40,240,96,nullptr,nullptr,wc.hInstance,nullptr);require(o.own!=nullptr,"owned thread window");if(!o.own)continue;ShowWindow(o.own,SW_SHOWNOACTIVATE);active=&o;HHOOK h=SetWindowsHookExA(WH_CBT,hook,nullptr,o.thread);require(h!=nullptr,"thread CBT observer");const UINT_PTR timer=SetTimer(nullptr,0,4000,deadline);require(timer!=0,"owned safety timer");
#ifdef CONCORD_STARTUP_RECOVERY_PORT
 const bool retry=requestRuntimeStartupRecoveryRetry(runtimeStartupRecoveryPromptWin32(),o.expected);
#else
 const int raw=originalChoice(o.expected);const bool retry=raw==IDYES;
#endif
 KillTimer(nullptr,timer);if(h)UnhookWindowsHookEx(h);active=nullptr;require(!o.timeout,"native dialog timeout");require(retry==(cases[index].command==IDYES&&!cases[index].reject),"original yes-only result");require(IsWindowEnabled(o.own)!=FALSE,"task window enabled after synchronous prompt");
 if(cases[index].reject){require(o.creates>=1&&o.activated==0&&!o.dialog,"actual API creation failure");++rejected;}else{require(o.creates==1&&o.activated==1,"one real dialog lifecycle");require(o.title&&o.text,"original ANSI title/body bytes");require(o.sameThread,"original calling thread");require(o.taskDisabled,"original task-modal disabled thread window");require(o.dialog&&!IsWindow(o.dialog),"dialog destroyed before return");require(o.closeIgnored==(cases[index].tryClose?1U:0U),"original close-disabled semantics");++dialogs;}
 require(DestroyWindow(o.own)!=FALSE,"owned window destruction");std::cout<<"Recovery native case "<<index<<": retry="<<retry<<" creates="<<o.creates<<" activates="<<o.activated<<" closeIgnored="<<o.closeIgnored<<" title="<<o.title<<" body="<<o.text<<" thread="<<o.sameThread<<" taskDisabled="<<o.taskDisabled<<" timeout="<<o.timeout<<"\n";
 }
 require(UnregisterClassA(cls,wc.hInstance)!=FALSE,"class release");std::cout<<"Startup recovery native: checks="<<checks<<" cases="<<cases.size()<<" dialogs="<<dialogs<<" rejected="<<rejected<<" failures="<<failures<<" skipped=0\n";return failures?1:0;}
