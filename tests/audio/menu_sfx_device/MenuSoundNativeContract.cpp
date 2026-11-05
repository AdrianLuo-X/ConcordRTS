#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include "audio/events/win32/RuntimeMenuSoundWin32.h"
#include "audio/events/RuntimeMenuSoundPool.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
using namespace concord::audio;
int checks=0,failures=0;
void require(bool ok,const char* why){++checks;if(!ok){++failures;std::cout<<"FAIL "<<why<<std::endl;}}
std::wstring alias(std::size_t channel){return L"concord_menu_effect_"+std::to_wstring(channel);}
struct Trace {std::vector<std::wstring> calls;int scenario=0;bool closeFailed=false;};
thread_local Trace* active=nullptr;
std::uint32_t send(const wchar_t* input,wchar_t* result,std::uint32_t capacity){
 const std::wstring cmd(input);std::uint32_t error=0;
 if(active&&active->scenario==1&&cmd.starts_with(L"open ")&&cmd.ends_with(L" shareable"))error=MCIERR_UNSUPPORTED_FUNCTION;
 else if(active&&active->scenario==2&&cmd.starts_with(L"open "))error=MCIERR_DEVICE_OPEN;
 else if(active&&active->scenario==3&&cmd.starts_with(L"play "))error=MCIERR_DRIVER_INTERNAL;
 else if(active&&active->scenario==4&&cmd.starts_with(L"status "))error=MCIERR_INVALID_DEVICE_ID;
 else if(active&&active->scenario==5&&cmd.starts_with(L"close ")&&!active->closeFailed){active->closeFailed=true;error=MCIERR_DEVICE_LOCKED;}
 else error=mciSendStringW(input,result,static_cast<UINT>(capacity),nullptr);
 if(active)active->calls.push_back(cmd+L" |cap="+std::to_wstring(capacity)+L" |error="+std::to_wstring(error)+L" |result="+(result?result:L""));return error;
}
MCIERROR WINAPI traced(LPCWSTR s,LPWSTR o,UINT n,HWND){return send(s,o,n);}
namespace original {
#define mciSendStringW traced
#include "OriginalBackend.inl"
#undef mciSendStringW
}
struct Current {
 std::unique_ptr<MenuSoundDevice> d=createMenuSoundDeviceWin32ForDiagnostics(send);
 std::optional<bool> isPlaying(std::size_t n)const{return d->isPlaying(n);}
 bool open(std::size_t n,const std::wstring& p)const{return d->open(n,p);}
 bool playFromStart(std::size_t n)const{return d->playFromStart(n);}
 void close(std::size_t n)const{d->close(n);}
};
void wave(const std::filesystem::path& path,std::uint32_t frames,int bits){
 const std::uint32_t bytes=frames*static_cast<std::uint32_t>(bits/8);std::vector<unsigned char> b(44+bytes,bits==8?128:0);auto text=[&](int n,const char* t){for(int i=0;i<4;++i)b[static_cast<std::size_t>(n+i)]=static_cast<unsigned char>(t[i]);};auto u32=[&](int n,std::uint32_t v){for(int i=0;i<4;++i)b[static_cast<std::size_t>(n+i)]=static_cast<unsigned char>(v>>(8*i));};auto u16=[&](int n,unsigned v){b[static_cast<std::size_t>(n)]=static_cast<unsigned char>(v);b[static_cast<std::size_t>(n+1)]=static_cast<unsigned char>(v>>8);};text(0,"RIFF");u32(4,bytes+36);text(8,"WAVE");text(12,"fmt ");u32(16,16);u16(20,1);u16(22,1);u32(24,8000);u32(28,8000*static_cast<std::uint32_t>(bits/8));u16(32,static_cast<unsigned>(bits/8));u16(34,static_cast<unsigned>(bits));text(36,"data");u32(40,bytes);std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("PCM write failed");
}
template<class B> std::vector<std::wstring> scenario(int mode){
 Trace trace;trace.scenario=mode;active=&trace;
 {B b;const bool opened=b.open(0,L"long.wav");require(opened==(mode!=2),"original open/shareable/fallback outcome");if(opened){const bool played=b.playFromStart(0);require(played==(mode!=3),"original play result");const auto state=b.isPlaying(0);require(mode==4?!state.has_value():state.has_value(),"original status optional behavior");}b.close(0);b.close(0);}
 active=nullptr;require(mciGetDeviceIDW(alias(0).c_str())==0,"scenario alias closed");return trace.calls;
}
std::uint64_t position(std::size_t n){wchar_t answer[64]{};const auto cmd=L"status "+alias(n)+L" position";return mciSendStringW(cmd.c_str(),answer,64,nullptr)==0?std::wcstoull(answer,nullptr,10):0;}
void waitPosition(std::size_t n){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(position(n)==0&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));require(position(n)>0,"nonempty real PCM position advances");}
void capacity(){
 Current b;RuntimeMenuSoundPool<32> pool;
 for(std::size_t n=0;n<32;++n)require(pool.play(L"long.wav",b),"actual32slot pool fills without truncation");require(std::count(pool.opened.begin(),pool.opened.end(),true)==32,"all32 original slots owned");require(!pool.play(L"long.wav",b),"full32 preserves active tails");waitPosition(0);waitPosition(31);
 const auto stop=L"stop "+alias(7);require(mciSendStringW(stop.c_str(),nullptr,0,nullptr)==0,"stop only own designated slot");pool.collect(b);require(!pool.opened[7]&&std::count(pool.opened.begin(),pool.opened.end(),true)==31,"collect frees stopped slot only");require(pool.play(L"long.wav",b)&&pool.opened[7],"pool reuses freed slot");pool.closeAll(b);for(std::size_t n=0;n<32;++n)require(mciGetDeviceIDW(alias(n).c_str())==0,"all32 close after shared owner end");
}
void eofAndReentry(){
 for(const auto* path:{L"short8.wav",L"short16.wav"}){
  auto d=createMenuSoundDeviceWin32();require(d->open(0,path)&&d->playFromStart(0),"actual8and16 PCM starts");waitPosition(0);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);auto state=d->isPlaying(0);while(state&&*state&&std::chrono::steady_clock::now()<end){std::this_thread::sleep_for(std::chrono::milliseconds(5));state=d->isPlaying(0);}require(state.has_value()&&!*state,"natural EOF observed");d->close(0);
 }
 for(int n=0;n<48;++n){auto d=createMenuSoundDeviceWin32();require(d->open(0,L"long.wav")&&d->playFromStart(0),"48actualreentries");d.reset();require(mciGetDeviceIDW(alias(0).c_str())==0,"owned alias closes on destructor");}
}
void staleAndCloseRetry(){
 auto old=createMenuSoundDeviceWin32();require(old->open(0,L"long.wav")&&old->playFromStart(0),"old session starts");const auto close=L"close "+alias(0);require(mciSendStringW(close.c_str(),nullptr,0,nullptr)==0,"own process simulates original native alias loss");auto replacement=createMenuSoundDeviceWin32();require(replacement->open(0,L"long.wav")&&replacement->playFromStart(0),"replacement session starts");old->close(0);old.reset();require(replacement->isPlaying(0).value_or(false),"stale cleanup keeps new owner playing");replacement.reset();require(mciGetDeviceIDW(alias(0).c_str())==0,"replacement closes");
 Trace trace;trace.scenario=5;active=&trace;auto retry=createMenuSoundDeviceWin32ForDiagnostics(send);require(retry->open(0,L"long.wav")&&retry->playFromStart(0),"close retry session starts");retry->close(0);require(mciGetDeviceIDW(alias(0).c_str())!=0,"specified failed close retains native alias");retry.reset();require(mciGetDeviceIDW(alias(0).c_str())==0&&trace.closeFailed,"destructor retries only failed owned close");active=nullptr;
}
}
int wmain(int argc,wchar_t** argv){try{std::size_t count=0;std::vector<wchar_t> output;if(argc!=2){if(_wgetenv_s(&count,nullptr,0,L"CONCORD_MENU_SOUND_OUTPUT")!=0||count<2)throw std::runtime_error("explicit new output required");output.resize(count);if(_wgetenv_s(&count,output.data(),output.size(),L"CONCORD_MENU_SOUND_OUTPUT")!=0)throw std::runtime_error("output environment invalid");}std::filesystem::path dir(argc==2?argv[1]:output.data());if(!dir.is_absolute()||std::filesystem::exists(dir))throw std::runtime_error("new absolute output required");std::filesystem::create_directories(dir);std::filesystem::current_path(dir);wave("long.wav",64000,16);wave("short8.wav",1600,8);wave("short16.wav",1600,16);for(int n=0;n<6;++n){const auto a=scenario<original::RuntimeMenuSoundMciBackend>(n);const auto b=scenario<Current>(n);require(a==b,"full original/current command/capacity/result/error stream exact");for(int which=0;which<2;++which){std::ofstream trace(std::to_string(n)+(which?"-current.txt":"-original.txt"),std::ios::binary);for(const auto& line:(which?b:a)){for(wchar_t unit:line)trace<<std::hex<<static_cast<unsigned>(unit)<<',';trace<<std::endl;}}}capacity();eofAndReentry();staleAndCloseRetry();std::cout<<"MenuSound native: checks="<<checks<<" failures="<<failures<<" skipped=0 referenceStreams=6 actualSlots=32 naturalEnds=2 reentries=48 staleProtection=1 closeRetry=1 hearing=not-run"<<std::endl;return failures?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
