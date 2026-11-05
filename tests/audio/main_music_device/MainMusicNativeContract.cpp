#include "audio/menu/win32/RuntimeMainMenuMusicWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace concord::audio;
namespace fs=std::filesystem;
namespace {
unsigned checks=0,failures=0,openErrors=0,volumeErrors=0,playErrors=0,closeErrors=0;
bool failOpen=false,failVolume=false,failPlay=false,failCloseOnce=false;
struct Call{std::wstring command;std::uint32_t capacity;};
std::vector<Call> calls;
void expect(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<name<<'\n';}}
std::uint32_t traced(const wchar_t* command,wchar_t* result,std::uint32_t capacity){
 const std::wstring text(command);calls.push_back({text,capacity});
 if(failOpen&&text.starts_with(L"open ")){++openErrors;return MCIERR_DEVICE_NOT_READY;}
 if(failVolume&&text.starts_with(L"setaudio ")){++volumeErrors;return MCIERR_UNSUPPORTED_FUNCTION;}
 if(failPlay&&text.starts_with(L"play ")){++playErrors;return MCIERR_DEVICE_NOT_READY;}
 if(failCloseOnce&&text.starts_with(L"close ")){failCloseOnce=false;++closeErrors;return MCIERR_DEVICE_LOCKED;}
 return mciSendStringW(command,result,static_cast<UINT>(capacity),nullptr);
}
bool wave(const fs::path& path){
 std::ofstream out(path,std::ios::binary);const auto u16=[&](std::uint16_t x){for(int i=0;i<2;++i)out.put(static_cast<char>(x>>(i*8)));};const auto u32=[&](std::uint32_t x){for(int i=0;i<4;++i)out.put(static_cast<char>(x>>(i*8)));};
 out.write("RIFF",4);u32(32036);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);out.write("data",4);u32(32000);const std::array<char,32000> pcm{};out.write(pcm.data(),pcm.size());return bool(out);
}
bool closed(){return mciGetDeviceIDW(L"concord_runtime_intro")==0;}
auto make(){return createMainMenuMusicDeviceWin32ForDiagnostics(traced);}
}
int main(){
 const DWORD count=GetEnvironmentVariableW(L"CONCORD_MAIN_MUSIC_DEVICE_OUTPUT",nullptr,0);if(count<2){std::cerr<<"Explicit owned native output required\n";return 2;}std::vector<wchar_t> text(count);if(GetEnvironmentVariableW(L"CONCORD_MAIN_MUSIC_DEVICE_OUTPUT",text.data(),count)!=count-1)return 2;
 const fs::path root(text.data());fs::create_directories(root);const auto pcm=root/"p.wav",unicode=root/fs::path(L"静音.wav"),bad=root/"bad.wav";expect(wave(pcm)&&wave(unicode),"real owned silent two-second nonempty PCM");{std::ofstream f(bad,std::ios::binary);f<<"invalid";expect(bool(f),"owned invalid wave");}
 {
  auto d=make();expect(d->supported()&&closed(),"construction opens no native alias");calls.clear();expect(d->open(pcm),"actual original open");d->setVolumePercent(-50);expect(d->playFromStart(),"actual original play");std::vector<std::wstring> actual;for(const auto& c:calls)actual.push_back(c.command);
  const std::vector<std::wstring> expected{L"close concord_runtime_intro",L"open \""+pcm.native()+L"\" type waveaudio alias concord_runtime_intro",L"setaudio concord_runtime_intro volume to 0",L"play concord_runtime_intro from 0"};expect(actual==expected,"exact original preclose/native path/open-no-shareable/ignored-volume/play sequence");expect(d->isPlaying()==std::optional<bool>(true),"actual native playing");expect(calls.back().capacity==32,"original status buffer32");
  Sleep(80);wchar_t position[32]{};const auto code=mciSendStringW(L"status concord_runtime_intro position",position,32,nullptr);std::uint64_t value=0;std::wistringstream(position)>>value;expect(code==0&&value>0,"actual native PCM progress");
  const auto deadline=GetTickCount64()+5000;while(d->isPlaying()==std::optional<bool>(true)&&GetTickCount64()<deadline)Sleep(20);expect(d->isPlaying()==std::optional<bool>(false),"real natural EOF stopped, never false empty implementation");expect(d->playFromStart()&&d->isPlaying()==std::optional<bool>(true),"same open alias restarts from zero after natural EOF");
  d->stop();expect(d->isPlaying()==std::optional<bool>(false),"stop keeps original alias available");expect(d->playFromStart(),"stop then play restarts original device");calls.clear();d->setVolumePercent(190);expect(calls.size()==1&&calls[0].command==L"setaudio concord_runtime_intro volume to 1000","original high-volume clamp1000");d->close();expect(closed(),"real close disposes alias");d->stop();d->close();expect(closed(),"repeated cleanup never reopens");
  expect(d->open(unicode)&&d->playFromStart(),"actual native Unicode filesystem path");expect(mciSendStringW(L"close concord_runtime_intro",nullptr,0,nullptr)==0,"own native alias loss fixture");expect(!d->isPlaying().has_value()&&!d->playFromStart(),"real lost device status unknown and play fails");d->close();expect(closed(),"lost-device close cleans retained owner");
  expect(!d->open(root/"missing.wav")&&closed(),"actual missing-file error");expect(!d->open(bad)&&closed(),"actual invalid-wave error");failOpen=true;expect(!d->open(pcm)&&closed(),"specified test-only open failure has no fake device");failOpen=false;
  expect(d->open(pcm),"actual recovery open after failure");failVolume=true;d->setVolumePercent(100);failVolume=false;expect(d->playFromStart()&&d->isPlaying()==std::optional<bool>(true),"original volume-error ignored, real device still plays");d->close();
  expect(d->open(pcm),"actual native open before play error");failPlay=true;expect(!d->playFromStart()&&!closed(),"play failure preserves original caller-owned open alias for cleanup");failPlay=false;d->close();expect(closed(),"caller cleanup after play error closes actual device");
  expect(d->open(pcm)&&d->playFromStart(),"real recovery before close failure");failCloseOnce=true;d->close();expect(!closed(),"one specified close failure retains actual alias for retry");
 }
 expect(closed(),"destructor retries actual failed close");
 {
  auto old=make();expect(old->open(pcm)&&old->playFromStart(),"old owner actual playback");auto next=make();expect(next->open(unicode)&&next->playFromStart(),"new owner actual replacement");old->stop();old->close();expect(next->isPlaying()==std::optional<bool>(true),"old explicit cleanup preserves new owner");old.reset();expect(next->isPlaying()==std::optional<bool>(true),"old destructor preserves new owner");auto unused=make();unused.reset();expect(next->isPlaying()==std::optional<bool>(true),"unused destruction preserves actual owner");
 }
 expect(closed(),"replacement owner's destructor closes actual alias");unsigned reentries=0;
 for(int visit=0;visit<24;++visit){{auto d=make();expect(d->open(pcm)&&d->playFromStart(),"actual native reentry");if(visit%2==0){d->stop();d->close();}}expect(closed(),"each original native reentry returns alias0");++reentries;}
 auto unsupported=createMainMenuMusicDeviceWin32ForDiagnostics(nullptr);expect(!unsupported->supported()&&!unsupported->open(pcm)&&!unsupported->playFromStart()&&!unsupported->isPlaying().has_value(),"missing native operation selects honest unsupported");expect(openErrors==1&&volumeErrors==1&&playErrors==1&&closeErrors==1,"all other successes forwarded real WinMM");
 std::cout<<"Main music native: checks="<<checks<<" failures="<<failures<<" skipped=0 progressed=1 naturalEOF=1 reentries="<<reentries<<" protectedHandoffs=1 openErrors="<<openErrors<<" ignoredVolumeErrors="<<volumeErrors<<" playErrors="<<playErrors<<" closeRetries="<<closeErrors<<" exactOriginalCommands=yes nativePath=yes noShareable=yes status32=yes finalAlias=0 silentRealPCM=yes hearingHotplug=not-claimed\n";return failures?1:0;
}
