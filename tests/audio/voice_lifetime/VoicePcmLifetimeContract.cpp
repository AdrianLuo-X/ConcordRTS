#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "audio/backend/RuntimeVoiceAudio.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs=std::filesystem;
unsigned checks=0;
void require(bool value,const char* message) {
    ++checks;
    if(!value)throw std::runtime_error(message);
}
std::string utf8(const fs::path& path) {
    const auto bytes=path.u8string();
    return {bytes.begin(),bytes.end()};
}
void le16(std::ostream& out,std::uint16_t value) {
    out.put(static_cast<char>(value));out.put(static_cast<char>(value>>8));
}
void le32(std::ostream& out,std::uint32_t value) {
    le16(out,static_cast<std::uint16_t>(value));le16(out,static_cast<std::uint16_t>(value>>16));
}
// Fixture writer only. The unchanged production reader/voice owner consumes it.
void wave(const fs::path& path,unsigned channels,unsigned rate,unsigned frames) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    const std::uint32_t size=frames*channels*2;
    out.write("RIFF",4);le32(out,36+size);out.write("WAVEfmt ",8);
    le32(out,16);le16(out,1);le16(out,static_cast<std::uint16_t>(channels));
    le32(out,rate);le32(out,rate*channels*2);le16(out,static_cast<std::uint16_t>(channels*2));
    le16(out,16);out.write("data",4);le32(out,size);
    bool nonzero=false;
    for(unsigned n=0;n<frames;++n) {
        const auto sample=static_cast<std::int16_t>(400.0*std::sin(6.283185307179586*440.0*n/rate));
        nonzero|=sample!=0;
        for(unsigned c=0;c<channels;++c)le16(out,static_cast<std::uint16_t>(sample));
    }
    out.close();require(out.good()&&nonzero,"nonempty nonzero authored PCM fixture");
}
template<class Predicate> void waitFor(Predicate predicate,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    while(!predicate()) {
        require(std::chrono::steady_clock::now()<deadline,message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}
int wmain() try {
    const DWORD needed=GetEnvironmentVariableW(L"CONCORD_VOICE_PCM_OUTPUT",nullptr,0);
    require(needed>1,"explicit local cache output is required");
    std::wstring value(needed,L'\0');
    require(GetEnvironmentVariableW(L"CONCORD_VOICE_PCM_OUTPUT",value.data(),needed)==needed-1,"complete output path");
    value.resize(needed-1);const fs::path output(value);
    require(fs::create_directory(output),"unique new fixture output");
    const auto mono=output/L"mono-original-中.wav",stereo=output/L"stereo-original.wav";
    const auto compatible=output/L"compatible-original.wav",wrongRate=output/L"wrong-rate.wav",wrongChannels=output/L"wrong-channels.wav";
    const auto workingBase=output/L"working-base.wav",workingQueue=output/L"working-queue.wav";
    constexpr unsigned rate=48000,frames=48000;
    wave(mono,1,rate,frames);wave(compatible,1,rate,frames);wave(stereo,2,rate,frames/8);
    wave(wrongRate,1,44100,4410);wave(wrongChannels,2,rate,4800);
    require(fs::copy_file(mono,workingBase)&&fs::copy_file(compatible,workingQueue),"owned working input copies");
    std::uint64_t completedFrames=0;
    {
        RuntimeVoiceAudio audio(false);
        if(!audio.ready())std::cerr<<"Required native XAudio2 device unavailable HRESULT="<<audio.lastError()<<'\n';
        require(audio.ready(),"real native engine and mastering voice; no unsupported/skip pass");
        require(audio.voiceCount()==0&&!audio.probe(17).available,"fresh session and absent handle");
        require(audio.stop(17)&&!audio.setMix(17,16384,8192)&&!audio.playFromStart(17)
            &&!audio.queue(17,utf8(compatible)),"original absent-handle outcomes");
        require(audio.open(1,utf8(workingBase))&&audio.setMix(1,8192,8192),"real UTF8-path source voice and native mix");
        auto initial=audio.probe(1);
        require(initial.available&&initial.channels==1&&initial.sampleRate==rate&&initial.bitsPerSample==16&&initial.frameBytes==2,"original PCM format");
        require(initial.masterVolume==1&&initial.masteringLimiterEnabled&&initial.volume>0&&initial.matrix==std::vector<float>({1,1}),"unmuted real graph, native limiter enabled and mixer readback");
        // No borrowed file bytes: open already owns the base buffer, and queue
        // independently owns every future buffer before this file is changed.
        {std::ofstream changed(workingBase,std::ios::binary|std::ios::trunc);changed<<"changed after production open";}
        require(audio.playFromStart(1,false,true),"real continuous sequence starts");
        require(audio.queue(1,utf8(workingQueue)),"compatible PCM append");
        const auto beforeBad=audio.queuedBuffers(1);
        require(!audio.queue(1,utf8(wrongRate))&&!audio.queue(1,utf8(wrongChannels)),"incompatible formats rejected");
        require(audio.queuedBuffers(1)==beforeBad,"format rejection preserves current queued buffers");
        require(audio.queue(1,utf8(workingQueue))&&audio.queue(1,utf8(workingQueue)),"native four-buffer queue");
        require(audio.queuedBuffers(1)==4&&!audio.queue(1,utf8(workingQueue)),"original queue limit four");
        {std::ofstream changed(workingQueue,std::ios::binary|std::ios::trunc);changed<<"changed after three production queue copies";}
        waitFor([&]{return audio.probe(1).samplesPlayed>0;},"actual native processing advances");
        waitFor([&]{return !audio.isPlaying(1).value_or(true);},"actual natural drain");
        const auto ended=audio.probe(1);completedFrames=ended.samplesPlayed;
        require(ended.buffersQueued==0&&completedFrames==std::uint64_t(frames)*4,"all original owned PCM survives file changes and drains exactly");
        require(audio.stop(1)&&audio.queuedBuffers(1)==0,"native stop and flush");
        require(audio.playFromStart(1)&&audio.probe(1).available,"retained base buffer replay despite changed source file");
        require(audio.stop(1),"replay stops");audio.close(1);
        require(audio.voiceCount()==0&&!audio.probe(1).available,"DestroyVoice completes before owned PCM release");
        require(audio.open(2,utf8(stereo))&&audio.setMix(2,8192,8192),"stereo session remains supported");
        const auto stereoProbe=audio.probe(2);
        require(stereoProbe.channels==2&&stereoProbe.frameBytes==4&&stereoProbe.matrix==std::vector<float>({1,0,0,1}),"native stereo routing");
        require(audio.playFromStart(2,true),"real loop begins");
        waitFor([&]{return audio.probe(2).samplesPlayed>frames/8;},"loop crosses source length");
        require(audio.isPlaying(2).value_or(false),"loop retains real active queue");
        require(!audio.open(2,utf8(workingBase)),"original invalid replacement fails after closing existing voice");
        require(audio.voiceCount()==0&&!audio.probe(2).available&&audio.lastError()!=0,"invalid open leaves no partial voice and preserves native diagnostic");
        for(unsigned n=0;n<24;++n) {
            require(audio.open(100+n,utf8(mono))&&audio.setMix(100+n,8192,8192),"owned source reentry");
            require(audio.playFromStart(100+n,false,true)&&audio.queue(100+n,utf8(compatible)),"queued source before immediate close");
            audio.close(100+n);
            require(audio.voiceCount()==0&&!audio.probe(100+n).available,"immediate close releases only owned voices");
        }
        require(audio.open(999,utf8(mono))&&audio.setMix(999,8192,8192)&&audio.playFromStart(999,true),"live voice left to session destructor");
    }
    {
        RuntimeVoiceAudio reopened(false);
        require(reopened.ready()&&reopened.voiceCount()==0,"fresh native engine after destruction");
        require(reopened.open(1,utf8(stereo))&&reopened.setMix(1,8192,8192)&&reopened.playFromStart(1),"actual source after engine reopen");
        waitFor([&]{return reopened.probe(1).samplesPlayed>0;},"reopened engine advances nonzero PCM");
        reopened.close(1);require(reopened.voiceCount()==0,"reopened session cleanup");
    }
    std::cout<<"Voice PCM lifetime passed checks="<<checks<<" ownedFrames="<<completedFrames
        <<" queueLimit=4 sourceReentries=24 sessions=2 PCM=nonempty-nonzero master=unmuted"
        <<" physicalHearing=not-claimed loopbackCapture=not-run hardwareHotplug=not-run\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"Voice PCM lifetime FAILED: "<<error.what()<<'\n';return 1;
}
