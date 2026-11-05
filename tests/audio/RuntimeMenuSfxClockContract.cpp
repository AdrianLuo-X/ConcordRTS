#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "runtime/host/RuntimeUiClockWin32.h"
#include "audio/events/RuntimeMenuSoundPool.h"
#include "audio/events/win32/RuntimeMenuSoundWin32.h"

namespace {
struct DeviceTrace {
    bool native = false;
    bool failOpen = false;
    bool failPlay = false;
    std::array<int,32> playing{};
    std::vector<std::wstring> calls;
};
// Device-call observation only. Every controlled clock is instance-local.
thread_local DeviceTrace* activeDeviceTrace = nullptr;
struct TraceScope {
    DeviceTrace* previous;
    explicit TraceScope(DeviceTrace& trace) : previous(activeDeviceTrace) { activeDeviceTrace=&trace; }
    ~TraceScope() { activeDeviceTrace=previous; }
};
MCIERROR WINAPI tracedMci(LPCWSTR input, LPWSTR result, UINT capacity, HWND callback) {
    auto* trace=activeDeviceTrace;
    if (!trace) return ::mciSendStringW(input,result,capacity,callback);
    const std::wstring command(input);
    MCIERROR code=0;
    if (trace->native) code=::mciSendStringW(input,result,capacity,callback);
    else {
        const std::wstring prefix=L"concord_menu_effect_";
        const auto start=command.find(prefix);
        if (start==std::wstring::npos) throw std::runtime_error("unknown production MCI alias");
        const auto channel=std::stoul(command.substr(start+prefix.size()));
        if (channel>=trace->playing.size()) throw std::runtime_error("production MCI channel outside32");
        if (command.starts_with(L"status ")) {
            if (trace->playing[channel]<0) code=MCIERR_INVALID_DEVICE_ID;
            else if (result && capacity) {
                const auto text=trace->playing[channel]?L"playing":L"stopped";
                (void)wcsncpy_s(result,capacity,text,_TRUNCATE);
            }
        } else if (command.starts_with(L"open ")) {
            if (trace->failOpen) code=MCIERR_DEVICE_OPEN;
            else trace->playing[channel]=1;
        } else if (command.starts_with(L"play ")) {
            if (trace->failPlay) code=MCIERR_DRIVER_INTERNAL;
        } else if (command.starts_with(L"close ")) trace->playing[channel]=-1;
        else throw std::runtime_error("unknown production MCI command");
    }
    trace->calls.push_back(command+L" => "+std::to_wstring(code)+L" | "+(result?result:L""));
    return code;
}
std::unique_ptr<concord::audio::MenuSoundDevice> makeRuntimeMenuSoundDevice() {
    return concord::audio::createMenuSoundDeviceWin32ForDiagnostics(
        +[](const wchar_t* text,wchar_t* result,std::uint32_t capacity)->std::uint32_t {
            return tracedMci(text,result,capacity,nullptr);
        });
}
#define mciSendStringW tracedMci
#include "audio/events/RuntimeMenuSoundMci.inl"
namespace original {
#include "menu_sound_clock_original/RuntimeMenuSoundMci.original.inl"
}
#undef mciSendStringW

struct Checks {
    std::size_t checks=0,failures=0;
    void require(bool condition,const std::string& message) {
        ++checks;
        if (!condition) { ++failures; std::cout<<"Menu SFX clock failure: "<<message<<'\n'; }
    }
};
struct Clock final : RuntimeUiClock {
    std::uint64_t value=0;
    mutable std::size_t reads=0;
    std::uint64_t nowMs() const noexcept override { ++reads; return value; }
};
template<class Playback> void bindClock(Playback& playback,const RuntimeUiClock& clock) {
    if constexpr (requires {playback.clock;}) playback.clock=&clock;
    else playback.fixtureClock=&clock;
}
template<class Playback> std::string snapshot(const Playback& playback,const Clock& clock) {
    std::ostringstream text;text<<playback.nextCollect<<'\t'<<clock.reads<<'\t'<<playback.pool.next<<'\t';
    for (bool opened:playback.pool.opened) text<<opened;
    return text.str();
}
struct Observation {
    std::vector<std::string> states;
    std::vector<std::wstring> deviceCalls;
    std::size_t clockReads=0;
};
template<class Playback> Observation scripted(Checks& check) {
    DeviceTrace trace;trace.playing.fill(-1);trace.playing[0]=1;trace.playing[1]=0;
    TraceScope active(trace);Clock clock;Observation observation;
    {
        Playback playback;bindClock(playback,clock);
        playback.pool.opened[0]=playback.pool.opened[1]=playback.pool.opened[2]=true;
        playback.pool.next=31;
        const auto max=std::numeric_limits<std::uint64_t>::max();
        for (auto now:std::array<std::uint64_t,19>{0,1,99,100,100,50,199,200,max-100,max-99,max-50,max,0,48,49,98,99,100,1000}) {
            const auto previous=playback.nextCollect;const auto calls=trace.calls.size();const auto reads=clock.reads;
            clock.value=now;playback.collect();
            const bool due=now>=previous;
            check.require(clock.reads==reads+1,"every collect queries once before even the early guard");
            check.require(playback.nextCollect==(due?now+std::uint64_t{100}:previous),"original100ms unsigned due/update/wrap policy");
            check.require(due || trace.calls.size()==calls,"early return never queries a device");
            check.require(playback.pool.opened[0]&&playback.pool.opened[2]&&!playback.pool.opened[1],"playing/unknown retained; stopped closed");
            observation.states.push_back(snapshot(playback,clock));
        }
        const auto reads=clock.reads;
        playback.play(L"clock_音效.wav");
        check.require(clock.reads==reads&&playback.pool.opened[31]&&playback.pool.next==0,"play retains unthrottled original pool collect and next slot without clock read");
        observation.states.push_back(snapshot(playback,clock));
        trace.failOpen=true;playback.play(L"clock_音效.wav");trace.failOpen=false;
        check.require(clock.reads==reads&&!playback.pool.opened[1]&&playback.pool.next==0,"both original open attempts fail then close without retiming");
        observation.states.push_back(snapshot(playback,clock));
        trace.failPlay=true;playback.play(L"clock_音效.wav");trace.failPlay=false;
        check.require(clock.reads==reads&&!playback.pool.opened[1]&&playback.pool.next==0,"original play failure closes attempted channel without retiming");
        observation.states.push_back(snapshot(playback,clock));
        playback.pool.opened.fill(true);trace.playing.fill(1);playback.play(L"clock_音效.wav");
        check.require(clock.reads==reads&&playback.pool.next==0,"full32 capacity preserves active tails and clock state");
        observation.states.push_back(snapshot(playback,clock));
    }
    check.require(clock.reads==19,"destruction never reads clock");
    observation.clockReads=clock.reads;observation.deviceCalls=std::move(trace.calls);return observation;
}
void writePcm(const std::filesystem::path& file) {
    constexpr std::uint32_t bytes=32000;
    std::vector<unsigned char> wav(44+bytes,0);
    const auto text=[&](std::size_t offset,const char* value){for(std::size_t i=0;i<4;++i) wav[offset+i]=static_cast<unsigned char>(value[i]);};
    const auto u32=[&](std::size_t offset,std::uint32_t value){for(int i=0;i<4;++i) wav[offset+static_cast<std::size_t>(i)]=static_cast<unsigned char>((value>>(i*8))&255U);};
    const auto u16=[&](std::size_t offset,std::uint16_t value){for(int i=0;i<2;++i) wav[offset+static_cast<std::size_t>(i)]=static_cast<unsigned char>((value>>(i*8))&255U);};
    text(0,"RIFF");u32(4,bytes+36);text(8,"WAVE");text(12,"fmt ");u32(16,16);u16(20,1);u16(22,1);u32(24,8000);u32(28,16000);u16(32,2);u16(34,16);text(36,"data");u32(40,bytes);
    std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(wav.data()),static_cast<std::streamsize>(wav.size()));
    if(!output) throw std::runtime_error("own silent PCM could not be written");
}
template<class Playback> Observation native(Checks& check) {
    DeviceTrace trace;trace.native=true;TraceScope active(trace);Clock clock;Observation observation;
    {
        Playback playback;bindClock(playback,clock);playback.collect();
        check.require(playback.nextCollect==100&&clock.reads==1,"native empty collect retains original due");
        playback.play(L"tone.wav");
        check.require(clock.reads==1&&playback.pool.opened[0]&&playback.pool.next==1,"real production MCI open/play without a clock query");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        std::optional<bool> playing;
        do {playing=playback.backend.isPlaying(0);if(playing&&*playing) break;std::this_thread::sleep_for(std::chrono::milliseconds(10));} while(std::chrono::steady_clock::now()<deadline);
        check.require(playing&&*playing,"real MCI reports playing for the nonempty own PCM");
        unsigned long long position=0;
        do {
            wchar_t answer[64]{};
            const auto command=L"status "+(L"concord_menu_effect_"+std::to_wstring(0))+L" position";
            const auto code=tracedMci(command.c_str(),answer,64,nullptr);
            if(code==0) position=std::wcstoull(answer,nullptr,10);
            if(position>0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while(std::chrono::steady_clock::now()<deadline);
        check.require(position>0,"real device position advances; no hearing/loopback claim");
        auto calls=trace.calls.size();clock.value=99;playback.collect();
        check.require(clock.reads==2&&trace.calls.size()==calls&&playback.pool.opened[0],"native100ms early guard does not touch playing tail");
        clock.value=100;playback.collect();
        check.require(clock.reads==3&&playback.nextCollect==200&&playback.pool.opened[0],"native due retains a playing tail");
        const auto stop=L"stop "+(L"concord_menu_effect_"+std::to_wstring(0));
        check.require(tracedMci(stop.c_str(),nullptr,0,nullptr)==0,"own actual MCI voice stopped");
        calls=trace.calls.size();clock.value=199;playback.collect();
        check.require(clock.reads==4&&trace.calls.size()==calls&&playback.pool.opened[0],"stopped device remains until original due boundary");
        clock.value=200;playback.collect();
        check.require(clock.reads==5&&playback.nextCollect==300&&!playback.pool.opened[0],"native due closes stopped alias and frees slot");
        check.require(!playback.backend.isPlaying(0).has_value(),"actual alias closed and unavailable");
        observation.states.push_back(snapshot(playback,clock));
    }
    check.require(clock.reads==5,"native destructor leaves source alone");
    observation.clockReads=clock.reads;observation.deviceCalls=std::move(trace.calls);return observation;
}
void writeObservation(const std::filesystem::path& path,const Observation& observation) {
    std::ofstream output(path,std::ios::binary);
    for(const auto& state:observation.states) output<<state<<'\n';
    output<<"clockReads="<<observation.clockReads<<'\n';
    for(const auto& command:observation.deviceCalls) {
        // Lossless UTF-16 code-unit trace; paths are never decoded as ANSI.
        for(wchar_t unit:command) output<<std::hex<<static_cast<unsigned int>(unit)<<',';
        output<<'\n';
    }
    if(!output) throw std::runtime_error("complete observation could not be written");
}
}
int wmain() {
    Checks check;
    try {
        std::size_t size=0;
        if(_wgetenv_s(&size,nullptr,0,L"CONCORD_MENU_SFX_CLOCK_OUTPUT")!=0||size<2) throw std::runtime_error("explicit isolated output environment is required");
        std::vector<wchar_t> buffer(size);
        if(_wgetenv_s(&size,buffer.data(),buffer.size(),L"CONCORD_MENU_SFX_CLOCK_OUTPUT")!=0) throw std::runtime_error("output environment read failed");
        const std::filesystem::path root(buffer.data());
        const auto projectRoot=std::filesystem::current_path();
    const auto allowedRoot=(projectRoot/L"build"/L"smoke"/L"menu-sfx-clock").lexically_normal().native()+L"\\";
    if(!std::filesystem::is_regular_file(projectRoot/L"CMakeLists.txt")||!root.is_absolute()||!root.lexically_normal().native().starts_with(allowedRoot)||std::filesystem::exists(root)) throw std::runtime_error("run from the project root and use a new build/smoke/menu-sfx-clock output directory");
        std::filesystem::create_directories(root);std::filesystem::current_path(root);writePcm(root/L"tone.wav");
        check.require(runtimeMenuSoundPlayback().clock==&runtimeWin32UiClock(),"production static borrower uses original immutable provider");
        check.require(original::runtimeMenuSoundPlayback().fixtureClock==&runtimeWin32UiClock(),"complete original static accessor remains original provider");
        const auto before=::GetTickCount64();const auto defaultSample=runtimeWin32UiClock().nowMs();const auto after=::GetTickCount64();
        check.require(defaultSample>=before&&defaultSample<=after,"default is the same immutable native provider epoch");
        const auto current=scripted<RuntimeMenuSoundPlayback>(check);const auto old=scripted<original::RuntimeMenuSoundPlayback>(check);
        check.require(current.states==old.states,"all original nextCollect/read/slot32 snapshots exactly equal");
        check.require(current.deviceCalls==old.deviceCalls,"all original complete MCI calls/results/UTF16 fields and order exactly equal");
        check.require(current.clockReads==old.clockReads&&current.clockReads==19,"both exact independent source read sequences");
        writeObservation(root/L"current-scripted.txt",current);writeObservation(root/L"original-scripted.txt",old);
        const auto nativeCurrent=native<RuntimeMenuSoundPlayback>(check);const auto nativeOld=native<original::RuntimeMenuSoundPlayback>(check);
        check.require(nativeCurrent.states==nativeOld.states&&nativeCurrent.clockReads==nativeOld.clockReads,"real original/current MCI phases finish in the same complete pool/time state");
        writeObservation(root/L"current-native.txt",nativeCurrent);writeObservation(root/L"original-native.txt",nativeOld);
        std::cout<<"Menu SFX clock contract: checks="<<check.checks<<" failures="<<check.failures
            <<" skipped=0 scriptedStates="<<current.states.size()<<" scriptedCalls="<<current.deviceCalls.size()
            <<" sourceReads="<<current.clockReads<<" originalBody=complete globalFakeClock=0 realMciPhases=2 nativeSourceReads=5,5 silentPcmFrames=16000 hearing=not-run loopback=not-run\n";
        return check.failures?1:0;
    } catch(const std::exception& error) {
        std::cerr<<"Menu SFX clock contract failed: "<<error.what()<<'\n';return 1;
    }
}