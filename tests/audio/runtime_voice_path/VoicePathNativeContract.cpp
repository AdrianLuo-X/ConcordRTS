#include "audio/backend/RuntimeVoicePath.h"
#include "audio/backend/RuntimeVoiceAudio.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
int main(){
 unsigned checks=0,failures=0;const auto expect=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<name<<'\n';}};
 const DWORD length=GetEnvironmentVariableW(L"CONCORD_VOICE_PATH_SAMPLE",nullptr,0);expect(length>1,"explicit original PCM path required");if(length<=1)return 1;
 std::vector<wchar_t> chars(length);expect(GetEnvironmentVariableW(L"CONCORD_VOICE_PATH_SAMPLE",chars.data(),length)==length-1,"actual wide sample path");
 const std::filesystem::path sample(chars.data());expect(std::filesystem::is_regular_file(sample),"actual previously generated production PCM exists");
 const std::string legacy=std::filesystem::weakly_canonical(sample).generic_string();const auto utf8=concord::audio::runtimeVoiceUtf8PathFromLegacyNative(legacy);expect(legacy!=utf8,"Chinese native ACP and UTF8 are actually different");
 const int wideCount=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,legacy.data(),static_cast<int>(legacy.size()),nullptr,0);expect(wideCount>0,"independent Win32 native-narrow decode");if(wideCount<=0)return 1;
 std::wstring wide(static_cast<std::size_t>(wideCount),L'\0');expect(MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,legacy.data(),static_cast<int>(legacy.size()),wide.data(),wideCount)==wideCount,"independent exact wide path");
 const int byteCount=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),wideCount,nullptr,0,nullptr,nullptr);expect(byteCount>0,"independent Win32 UTF8 size");std::string oracle(static_cast<std::size_t>(byteCount),'\0');expect(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),wideCount,oracle.data(),byteCount,nullptr,nullptr)==byteCount&&oracle==utf8,"standard helper equals independent Win32 encoding oracle");
 RuntimeVoiceAudio audio(true);expect(audio.ready(),"same real native Voice device required");if(!audio.ready())return 1;
 bool oldOpened=false,oldException=false;try{oldOpened=audio.open(71,legacy);}catch(const std::exception& error){oldException=true;std::cout<<"Original native-narrow Voice open exception: "<<error.what()<<'\n';}
 expect(!oldOpened,"original native-narrow producer string is not UTF8 and fails actual Voice");audio.close(71);expect(audio.voiceCount()==0,"original failed open no owned voice remains");
 expect(audio.open(72,utf8),"same generated PCM opened through standard native-to-UTF8 adapter");expect(audio.setMix(72,16384,8192)&&audio.playFromStart(72,false,true),"actual native graph starts, output explicitly muted");expect(audio.queue(72,utf8),"same UTF8 queued PCM accepted");const auto queued=audio.queuedBuffers(72);expect(queued>=2,"both real PCM buffers owned");
 bool oldQueued=false,queueException=false;try{oldQueued=audio.queue(72,legacy);}catch(const std::exception& error){queueException=true;std::cout<<"Original native-narrow Voice queue exception: "<<error.what()<<'\n';}
 expect(!oldQueued&&audio.queuedBuffers(72)==queued,"original invalid UTF8 queue fails without discarding current owned buffers");
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);std::uint64_t played=0;
 do{played=audio.probe(72).samplesPlayed;if(played>=1024)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<deadline);
 expect(played>=1024,"actual native voice processed real PCM frames");expect(audio.stop(72),"native stop");audio.close(72);expect(audio.voiceCount()==0,"native owner closes all PCM/device voices");
 std::cout<<"Voice path native: checks="<<checks<<" failures="<<failures<<" skipped=0 ACP="<<GetACP()<<" originalOpenFailed="<<(!oldOpened)<<" originalOpenException="<<oldException<<" originalQueueFailed="<<(!oldQueued)<<" originalQueueException="<<queueException<<" independentWin32Oracle=exact processed="<<played<<" voicesAfter="<<audio.voiceCount()<<" output=muted sameProductionPCM=yes\n";return failures?1:0;
}
