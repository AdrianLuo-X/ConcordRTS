#include "audio/menu/win32/RuntimeSkirmishMusicWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
namespace fs=std::filesystem;
namespace {
int checks=0,failures=0;
void expect(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<text<<'\n';}}
std::wstring encode(const fs::path& p){return p.wstring();}
bool wave(const fs::path& path){
    std::ofstream out(path,std::ios::binary);
    const auto u16=[&](std::uint16_t v){for(int i=0;i<2;++i)out.put(static_cast<char>(v>>(i*8)));};
    const auto u32=[&](std::uint32_t v){for(int i=0;i<4;++i)out.put(static_cast<char>(v>>(i*8)));};
    out.write("RIFF",4);u32(32036);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(8000);u32(16000);u16(2);u16(16);
    out.write("data",4);u32(32000);const std::array<char,32000> data{};out.write(data.data(),static_cast<std::streamsize>(data.size()));return bool(out);
}
bool closed(){return mciGetDeviceIDW(L"concord_skirmish_intro")==0;}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;const fs::path root=argv[1];fs::create_directories(root);
    const auto pcm=root/"silence.wav",unicode=root/fs::path(L"静音音乐.wav"),bad=root/"invalid.wav";
    expect(wave(pcm)&&wave(unicode),"owned nonempty2sec silent PCM and Unicode fixture");
    {std::ofstream f(bad,std::ios::binary);f<<"invalid wave";expect(bool(f),"owned invalid file");}
    DWORD coldHandles=0;GetProcessHandleCount(GetCurrentProcess(),&coldHandles);
    {
        auto d=concord::audio::createSkirmishMusicDeviceWin32(encode);
        expect(d->supported()&&closed(),"supported provider construction performs no MCI open");
        expect(d->open(pcm)&&d->playFromStart(),"actual WaveAudio open/play");
        expect(d->isPlaying()==std::optional<bool>(true),"real MCI playing status");
        Sleep(60);wchar_t position[32]{};const auto e=mciSendStringW(L"status concord_skirmish_intro position",position,32,nullptr);std::uint64_t played=0;std::wistringstream stream(position);stream>>played;
        expect(e==0&&played>0,"actual silent PCM playback position advances");
        d->stop();expect(d->isPlaying()==std::optional<bool>(false),"real stop is not playing");
        expect(d->playFromStart(),"real restart from0");
        expect(mciSendStringW(L"close concord_skirmish_intro",nullptr,0,nullptr)==0,"external own-alias loss fixture");
        expect(!d->isPlaying().has_value()&&!d->playFromStart(),"lost device status/play failure preserved");
        d->close();expect(closed(),"close after external loss clears ownership");
        expect(d->open(unicode)&&d->playFromStart()&&d->isPlaying()==std::optional<bool>(true),"Unicode owned wave actually plays");
        d->close();expect(closed(),"explicit close clears native alias");
        d->close();d->stop();expect(closed(),"repeat close/stop does not reopen");
        expect(!d->open(root/"missing.wav")&&closed(),"actual missing-file error has no native alias");
        d->close();expect(!d->open(bad)&&closed(),"actual invalid-wave failure has no leaked alias");d->close();
        expect(d->open(pcm)&&d->playFromStart(),"recover after open failure");
    }
    expect(closed(),"abandoned active owner destructor stops and closes real session");
    {
        auto old=concord::audio::createSkirmishMusicDeviceWin32(encode);expect(old->open(pcm)&&old->playFromStart(),"old lease actual playback");
        auto next=concord::audio::createSkirmishMusicDeviceWin32(encode);expect(next->open(unicode)&&next->playFromStart(),"explicit replacement resets original process slot");
        old->stop();old->close();expect(next->isPlaying()==std::optional<bool>(true),"stale lease explicit cleanup cannot close current owner");
        old.reset();expect(next->isPlaying()==std::optional<bool>(true),"superseded owner destruction cannot close replacement");
        auto unused=concord::audio::createSkirmishMusicDeviceWin32(encode);unused.reset();
        expect(next->isPlaying()==std::optional<bool>(true),"never-opened owner destruction cannot close active music");
    }
    expect(closed(),"newest active owner closes on destruction");
    {
        auto unsupported=concord::audio::createSkirmishMusicDeviceWin32(nullptr);
        expect(!unsupported->supported()&&!unsupported->open(pcm)&&!unsupported->playFromStart()&&!unsupported->isPlaying(),"absent native encoder selects honest unsupported");
    }
    DWORD warmHandles=0;GetProcessHandleCount(GetCurrentProcess(),&warmHandles);
    for(int visit=0;visit<40;++visit){
        {
            auto d=concord::audio::createSkirmishMusicDeviceWin32(encode);
            expect(closed(),"reentry starts with no owned alias");
            expect(d->open(pcm)&&d->playFromStart(),"reentry actual open/play");
            if(visit%2==0){d->stop();d->close();expect(closed(),"explicit alternating close");}
        }
        expect(closed(),"each reentry destructor leaves native alias count0");
    }
    DWORD afterHandles=0;GetProcessHandleCount(GetCurrentProcess(),&afterHandles);
    std::cout<<"Skirmish native music: checks="<<checks<<" failures="<<failures<<" reentries=40 realPcmPosition=yes unicode=yes missingBadFile=closed leaseHandoff=protected finalAlias=closed"
        <<" processHandlesCold="<<coldHandles<<" warm="<<warmHandles<<" after="<<afterHandles
        <<" handleCounts=diagnosticOsAudioCaches noAudibilityClaim=yes\n";
    return failures?1:0;
}
