#include "audio/backend/RuntimeVoiceAudio.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>
namespace {
void little(std::ofstream& f,std::uint32_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)f.put(static_cast<char>((v>>(8*i))&255));}
bool wave(const std::filesystem::path& p,unsigned channels){
    std::ofstream f(p,std::ios::binary);const unsigned frames=48000,bytes=frames*channels*2;
    f.write("RIFF",4);little(f,36+bytes,4);f.write("WAVEfmt ",8);little(f,16,4);little(f,1,2);little(f,channels,2);
    little(f,48000,4);little(f,48000*channels*2,4);little(f,channels*2,2);little(f,16,2);f.write("data",4);little(f,bytes,4);
    for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<channels;++c)little(f,static_cast<std::uint16_t>(static_cast<std::int16_t>((i/24)%2?2048:-2048)),2);
    f.flush();return f.good();
}
std::string utf8(const std::filesystem::path& p){const auto u=p.u8string();return {u.begin(),u.end()};}
}
int main(){
    static_assert(sizeof(long)==4 && sizeof(HRESULT)==4 && std::is_signed_v<HRESULT>);
    static_assert(std::numeric_limits<HRESULT>::digits==31);
    unsigned checks=0,failures=0;const auto need=[&](bool b,const char* s){++checks;if(!b){++failures;std::cerr<<"Native diagnostic failure: "<<s<<'\n';}};
    const std::array<HRESULT,13> codes{S_OK,S_FALSE,E_FAIL,E_INVALIDARG,E_OUTOFMEMORY,E_POINTER,E_ACCESSDENIED,E_NOTIMPL,RPC_E_CHANGED_MODE,E_ABORT,E_UNEXPECTED,std::numeric_limits<HRESULT>::min(),std::numeric_limits<HRESULT>::max()};
    for(auto old:codes){const std::int32_t fixed=old;need(static_cast<std::int64_t>(old)==fixed,"full signed diagnostic preserved");need(static_cast<std::uint32_t>(old)==static_cast<std::uint32_t>(fixed),"all32 bit pattern preserved");std::ostringstream a,b;a<<old;b<<fixed;need(a.str()==b.str(),"existing decimal output identical");}
    RuntimeVoiceAudio audio(true);
    if(!audio.ready()){std::cerr<<"Audio native availability: unsupported currentDevice=0 HRESULT="<<audio.lastError()<<" requiredCase=not-passed\n";return 2;}
    need(audio.lastError()==S_OK,"actual successful graph getter");need(audio.voiceCount()==0,"fresh private owner");
    const auto root=std::filesystem::current_path(),missing=root/"missing-diagnostic-input.wav",bad=root/"bad-diagnostic-input.wav";
    need(!std::filesystem::exists(missing),"missing input really absent");
    need(!audio.open(1,utf8(missing)),"actual missing input failure");need(audio.lastError()==E_INVALIDARG,"actual private HRESULT from failed open");need(audio.lastError()==-2147024809,"same signed public diagnostic number");need(audio.ready()&&audio.voiceCount()==0,"failed open preserves graph and no voice");
    {std::ofstream f(bad,std::ios::binary);f<<"invalid PCM fixture";}
    need(!audio.open(2,utf8(bad)),"actual malformed PCM failure");need(audio.lastError()==E_INVALIDARG&&audio.voiceCount()==0,"same error/no partial voice after bad PCM");
    const auto prior=audio.lastError();need(audio.stop(9876)&&audio.lastError()==prior,"old idempotent stop leaves diagnostic unchanged");
    need(wave(root/"mono.wav",1)&&wave(root/"stereo.wav",2),"valid inputs for complete unchanged original consumer");
    std::cout<<"Audio native diagnostic contract: checks="<<checks<<" cases=13 failures="<<failures<<" skipped=0 actualDevice=1 initialError=0 missingError="<<audio.lastError()<<" codeBytes="<<sizeof(decltype(audio.lastError()))<<" domain=Windows-HRESULT speakerOutput=muted injectedDeviceFailure=not-run\n";
    return failures?1:0;
}
