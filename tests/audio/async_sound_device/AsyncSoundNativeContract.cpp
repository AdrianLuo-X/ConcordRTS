#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include "audio/backend/win32/RuntimeAsyncSoundDeviceWin32.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
using namespace concord::audio;
int checks=0,failures=0;
void check(bool value,const char* why){++checks;if(!value){++failures;std::cout<<"FAIL "<<why<<std::endl;}}
struct Call {bool nullFile;std::wstring file;std::uint32_t flags;bool result;bool operator==(const Call&)const=default;};
struct Trace {int failure=0;std::vector<Call> calls;};
thread_local Trace* active=nullptr;
bool send(const wchar_t* file,std::uint32_t flags){bool result;if(active&&((active->failure==1&&file)||(active->failure==2&&!file)))result=false;else result=PlaySoundW(file,nullptr,flags)!=FALSE;if(active)active->calls.push_back({file==nullptr,file?file:L"",flags,result});return result;}
struct Original {
 bool playFile(const std::wstring& path){return send(path.c_str(),SND_FILENAME|SND_ASYNC|SND_NODEFAULT);}
 bool stopGlobal(){return send(nullptr,0);}
};
struct Current {
 std::unique_ptr<RuntimeAsyncSoundDevice> d=createAsyncSoundDeviceWin32ForDiagnostics(send);
 bool playFile(const std::wstring& path){return d->playFile(path);}
 bool stopGlobal(){return d->stopGlobal();}
};
void wave(const std::filesystem::path& path,int bits){const std::uint32_t frames=8000,bytes=frames*static_cast<std::uint32_t>(bits/8);std::vector<unsigned char>b(44+bytes,bits==8?128:0);auto text=[&](int n,const char*t){for(int i=0;i<4;++i)b[static_cast<std::size_t>(n+i)]=static_cast<unsigned char>(t[i]);};auto u32=[&](int n,std::uint32_t x){for(int i=0;i<4;++i)b[static_cast<std::size_t>(n+i)]=static_cast<unsigned char>(x>>(8*i));};auto u16=[&](int n,unsigned x){b[static_cast<std::size_t>(n)]=static_cast<unsigned char>(x);b[static_cast<std::size_t>(n+1)]=static_cast<unsigned char>(x>>8);};text(0,"RIFF");u32(4,bytes+36);text(8,"WAVE");text(12,"fmt ");u32(16,16);u16(20,1);u16(22,1);u32(24,8000);u32(28,8000*static_cast<std::uint32_t>(bits/8));u16(32,static_cast<unsigned>(bits/8));u16(34,static_cast<unsigned>(bits));text(36,"data");u32(40,bytes);std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("PCM write failed");}
template<class D> std::vector<Call> exercise(int mode){Trace trace;trace.failure=mode;active=&trace;{D a,b;check(a.playFile(L"音效16.wav")== (mode!=1),"actual original/new16bit PCM result");check(b.playFile(L"effect8.wav")== (mode!=1),"actual global replacement8bit PCM result");check(a.playFile(L"not-present.wav")== (mode!=1),"original asynchronous acceptance includes missing-file requests; no physical-output claim");check(a.stopGlobal()==(mode!=2),"explicit old caller stops sameglobal sound");check(b.stopGlobal()==(mode!=2),"repeated global stop result");}active=nullptr;(void)PlaySoundW(nullptr,nullptr,0);return trace.calls;}
void write(const std::string& name,const std::vector<Call>& calls){std::ofstream f(name,std::ios::binary);for(const auto& c:calls){f<<c.nullFile<<','<<c.flags<<','<<c.result<<',';for(wchar_t unit:c.file)f<<std::hex<<static_cast<unsigned>(unit)<<';';f<<std::dec<<std::endl;}}
void lifecycle(){Trace trace;active=&trace;auto old=createAsyncSoundDeviceWin32ForDiagnostics(send);auto current=createAsyncSoundDeviceWin32ForDiagnostics(send);check(old->playFile(L"音效16.wav"),"old actual caller succeeds");check(current->playFile(L"effect8.wav"),"second actual caller succeeds");auto n=trace.calls.size();old.reset();check(trace.calls.size()==n,"old destruction never sends global stop");check(current->stopGlobal(),"new explicit global stop succeeds");n=trace.calls.size();current.reset();check(trace.calls.size()==n,"unused/native wrapper destruction sends no implicit global stop");for(int r=0;r<64;++r){auto d=createAsyncSoundDeviceWin32ForDiagnostics(send);check(d->playFile(r%2?L"音效16.wav":L"effect8.wav"),"64actual async reentries");n=trace.calls.size();d.reset();check(trace.calls.size()==n,"reentry destruction preserves original global policy");}check(PlaySoundW(nullptr,nullptr,0)!=FALSE,"actual final own-process global stop");active=nullptr;}
}
int wmain(){try{std::size_t n=0;if(_wgetenv_s(&n,nullptr,0,L"CONCORD_ASYNC_SOUND_OUTPUT")!=0||n<2)throw std::runtime_error("explicit output required");std::vector<wchar_t>b(n);if(_wgetenv_s(&n,b.data(),b.size(),L"CONCORD_ASYNC_SOUND_OUTPUT")!=0)throw std::runtime_error("output read failed");const std::filesystem::path dir(b.data());if(!dir.is_absolute()||std::filesystem::exists(dir))throw std::runtime_error("new absolute output required");std::filesystem::create_directories(dir);std::filesystem::current_path(dir);wave(L"音效16.wav",16);wave(L"effect8.wav",8);for(int mode=0;mode<3;++mode){const auto a=exercise<Original>(mode),c=exercise<Current>(mode);check(a==c,"whole original/new filename-nullness/UTF16/flags/returns exact");write(std::to_string(mode)+"-original.txt",a);write(std::to_string(mode)+"-current.txt",c);}lifecycle();std::cout<<"AsyncSound native: checks="<<checks<<" failures="<<failures<<" skipped=0 originalStreams=3 actualPCM8and16=1 globalReplacementStop=1 reentries=64 implicitDestructorStops=0 physicalOutput=not-captured"<<std::endl;return failures?1:0;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
