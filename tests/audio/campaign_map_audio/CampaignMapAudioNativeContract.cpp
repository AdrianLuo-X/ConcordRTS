#include "audio/menu/win32/RuntimeCampaignMapAudioWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace concord::audio;
namespace fs=std::filesystem;
namespace {
unsigned checks=0,failures=0,references=0,reentries=0,naturalEnds=0,handoffs=0;
unsigned injectedOpen=0,injectedVolume=0,injectedPlay=0,injectedClose=0;
enum class Failure {None,Open,Volume,Play,CloseOnce};
Failure failure=Failure::None;
struct Call {std::wstring command;std::uint32_t capacity,result;bool operator==(const Call&)const=default;};
std::vector<Call> calls;
void expect(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<message<<'\n';}}
std::uint32_t observed(const wchar_t* command,wchar_t* output,std::uint32_t capacity,std::nullptr_t){
 const std::wstring text(command);std::uint32_t result=0;
 if(failure==Failure::Open&&text.starts_with(L"open ")){++injectedOpen;result=MCIERR_DEVICE_NOT_READY;}
 else if(failure==Failure::Volume&&text.starts_with(L"setaudio ")){++injectedVolume;result=MCIERR_UNSUPPORTED_FUNCTION;}
 else if(failure==Failure::Play&&text.starts_with(L"play ")){++injectedPlay;result=MCIERR_DEVICE_NOT_READY;}
 else if(failure==Failure::CloseOnce&&text.starts_with(L"close ")){failure=Failure::None;++injectedClose;result=MCIERR_DEVICE_LOCKED;}
 else result=mciSendStringW(command,output,static_cast<UINT>(capacity),nullptr);
 calls.push_back({text,capacity,result});return result;
}
std::uint32_t currentSender(const wchar_t* command,wchar_t* output,std::uint32_t capacity){return observed(command,output,capacity,nullptr);}
#define mciSendStringW observed
namespace original {
#include "OriginalCampaignNative.inl"
}
#undef mciSendStringW
bool wave(const fs::path& path,unsigned bits){
 const auto bytes=bits==8?16000U:32000U;std::ofstream out(path,std::ios::binary);const auto u16=[&](std::uint16_t x){for(int i=0;i<2;++i)out.put(static_cast<char>(x>>(i*8)));};const auto u32=[&](std::uint32_t x){for(int i=0;i<4;++i)out.put(static_cast<char>(x>>(i*8)));};
 out.write("RIFF",4);u32(36+bytes);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(bits==8?8000:16000);u16(bits==8?1:2);u16(static_cast<std::uint16_t>(bits));out.write("data",4);u32(bytes);for(unsigned i=0;i<bytes;++i)out.put(bits==8?static_cast<char>(128):0);return bool(out);
}
std::wstring alias(CampaignMapAudioChannel c){return c==CampaignMapAudioChannel::Voice?L"concord_campaign_map_voice":L"concord_campaign_map_effect";}
bool closed(CampaignMapAudioChannel c){return mciGetDeviceIDW(alias(c).c_str())==0;}
std::wstring mode(CampaignMapAudioChannel c){wchar_t value[32]{};const auto command=L"status "+alias(c)+L" mode";return mciSendStringW(command.c_str(),value,32,nullptr)==0?std::wstring(value):L"<error>";}
void oldStop(CampaignMapAudioChannel c){if(c==CampaignMapAudioChannel::Voice)original::stopCampaignVoice();else original::stopCampaignMapEffect();}
bool oldPlay(CampaignMapAudioChannel c,const fs::path& path,int volume){return c==CampaignMapAudioChannel::Voice?original::playCampaignVoiceWave(path,volume):original::playCampaignEffectWave(path,volume);}
auto make(CampaignMapAudioChannel c){return createCampaignMapAudioDeviceWin32ForDiagnostics(c,currentSender);}
void compareOriginal(CampaignMapAudioChannel c,const fs::path& path,int volume,Failure injection,bool shouldPlay){
 failure=Failure::None;oldStop(c);failure=injection;calls.clear();const bool before=oldPlay(c,path,volume);const auto originalCalls=calls;expect(before==shouldPlay,"frozen original result qualified");if(before)expect(mode(c)==L"playing","frozen original real playing state");failure=Failure::None;oldStop(c);expect(closed(c),"frozen original explicit cleanup closes native device");
 auto d=make(c);failure=injection;calls.clear();const bool current=d->play(path,volume);const auto currentCalls=calls;expect(current==before&&currentCalls==originalCalls,"full original native command/capacity/error stream and result exact");expect(std::none_of(currentCalls.begin(),currentCalls.end(),[](const Call& x){return x.command.find(L" shareable")!=std::wstring::npos||x.command.find(L" from 0")!=std::wstring::npos;}),"original no-shareable and no-from0 preserved");if(current)expect(mode(c)==L"playing","current same native PCM really plays");failure=Failure::None;d->stopAndClose();expect(closed(c),"current cleanup native alias0");++references;
}
}
int main(){
 const DWORD count=GetEnvironmentVariableW(L"CONCORD_CAMPAIGN_MAP_AUDIO_OUTPUT",nullptr,0);if(count<2){std::cerr<<"Explicit owned native output required\n";return 2;}std::vector<wchar_t> text(count);if(GetEnvironmentVariableW(L"CONCORD_CAMPAIGN_MAP_AUDIO_OUTPUT",text.data(),count)!=count-1)return 2;
 const fs::path root(text.data());fs::create_directories(root);const auto pcm=root/"p.wav",unicode=root/fs::path(L"原样8.wav"),bad=root/"bad.wav";expect(wave(pcm,16)&&wave(unicode,8),"real owned original 8/16bit mono two-second PCM");{std::ofstream f(bad,std::ios::binary);f<<"invalid";expect(bool(f),"owned invalid file");}
 for(auto c:{CampaignMapAudioChannel::Voice,CampaignMapAudioChannel::Effect}){
  compareOriginal(c,pcm,-40,Failure::None,true);compareOriginal(c,pcm,170,Failure::None,true);compareOriginal(c,unicode,100,Failure::None,true);compareOriginal(c,root/"missing.wav",0,Failure::None,false);compareOriginal(c,bad,0,Failure::None,false);compareOriginal(c,pcm,0,Failure::Open,false);compareOriginal(c,pcm,0,Failure::Volume,true);compareOriginal(c,pcm,0,Failure::Play,false);
  {
   auto d=make(c);expect(d->supported()&&closed(c),"construction does not open device");expect(d->play(pcm,0),"actual independent channel start");Sleep(80);wchar_t position[32]{};const auto query=L"status "+alias(c)+L" position";const auto code=mciSendStringW(query.c_str(),position,32,nullptr);std::uint64_t value=0;std::wistringstream(position)>>value;expect(code==0&&value>0,"actual native playback position progresses");const auto deadline=GetTickCount64()+5000;while(mode(c)==L"playing"&&GetTickCount64()<deadline)Sleep(20);expect(mode(c)==L"stopped","actual native natural EOF");++naturalEnds;d->stopAndClose();expect(closed(c),"EOF cleanup native alias0");d->stopAndClose();expect(closed(c),"repeat cleanup no reopen");expect(d->play(unicode,0),"same channel actual 8bit Unicode reopen");const auto close=L"close "+alias(c);expect(mciSendStringW(close.c_str(),nullptr,0,nullptr)==0&&mode(c)==L"<error>","actual own device-loss fixture");d->stopAndClose();expect(d->play(pcm,0)&&mode(c)==L"playing","actual recovery after alias loss");failure=Failure::CloseOnce;d->stopAndClose();expect(!closed(c),"specified failed close retains actual stopped alias for cleanup");
  }
  expect(closed(c),"destructor retries actual failed close");
  {
   auto old=make(c);expect(old->play(pcm,0),"old owner actual play");auto next=make(c);expect(next->play(unicode,0),"new owner replaces same alias");old->stopAndClose();expect(mode(c)==L"playing","stale explicit cleanup preserves new owner");old.reset();expect(mode(c)==L"playing","stale destructor preserves new owner");auto unused=make(c);unused.reset();expect(mode(c)==L"playing","unused destructor preserves new owner");++handoffs;
  }
  expect(closed(c),"replacement owner cleanup closes alias");for(unsigned visit=0;visit<24;++visit){{auto d=make(c);expect(d->play(pcm,0),"actual native reentry");if(visit%2==0)d->stopAndClose();}expect(closed(c),"each native reentry ends alias0");++reentries;}
 }
 expect(original::playCampaignVoiceWave(pcm,0)&&original::playCampaignEffectWave(unicode,0),"original simultaneous voice/effect qualified");expect(mode(CampaignMapAudioChannel::Voice)==L"playing"&&mode(CampaignMapAudioChannel::Effect)==L"playing","original two real channels overlap");original::stopCampaignVoice();original::stopCampaignMapEffect();
 {
  auto v=make(CampaignMapAudioChannel::Voice),e=make(CampaignMapAudioChannel::Effect);expect(v->play(pcm,0)&&e->play(unicode,0),"same production two independent channels overlap");expect(mode(CampaignMapAudioChannel::Voice)==L"playing"&&mode(CampaignMapAudioChannel::Effect)==L"playing","both actual native devices playing");v->stopAndClose();expect(closed(CampaignMapAudioChannel::Voice)&&mode(CampaignMapAudioChannel::Effect)==L"playing","voice stop does not stop effect");
 }
 expect(closed(CampaignMapAudioChannel::Voice)&&closed(CampaignMapAudioChannel::Effect),"all owned native aliases finally0");auto invalid=createCampaignMapAudioDeviceWin32ForDiagnostics(CampaignMapAudioChannel::Voice,nullptr);expect(!invalid->supported()&&!invalid->play(pcm,0),"missing native sender honest unsupported");auto invalidChannel=createCampaignMapAudioDeviceWin32(static_cast<CampaignMapAudioChannel>(-1));expect(!invalidChannel->supported()&&!invalidChannel->play(pcm,0),"invalid channel does not address another slot");
 expect(references==16&&reentries==48&&naturalEnds==2&&handoffs==2&&injectedOpen==4&&injectedVolume==4&&injectedPlay==4&&injectedClose==2,"all specified failure injections/actual lifecycle cases accounted");std::cout<<"Campaign map audio native: checks="<<checks<<" failures="<<failures<<" skipped=0 originalReferences="<<references<<" slots=2 progressed=2 naturalEOF="<<naturalEnds<<" reentries="<<reentries<<" protectedHandoffs="<<handoffs<<" oldNewCommandsAndErrors=exact PCM8and16=yes simultaneousOldAndNew=yes noShareable=yes noFrom0=yes finalAliases=0 physicalHearingHotplug=not-claimed\n";return failures?1:0;
}
