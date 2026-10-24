#include "ui/audio/RuntimeSkirmishMusicPolicy.h"
#if defined(_WINDOWS_)
#error Music policy must not import Windows headers.
#endif
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0,probes=0;bool exists=true;
void expect(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<name<<'\n';}}
bool regular(const std::filesystem::path&){++probes;return exists;}
struct Clock final:RuntimeUiClock {
    RuntimeUiTimeMs value=0;mutable int reads=0;mutable std::size_t index=0;std::vector<RuntimeUiTimeMs> sequence;
    RuntimeUiTimeMs nowMs() const noexcept override{++reads;return index<sequence.size()?sequence[index++]:value;}
};
struct Device final:concord::audio::MenuMusicDevice {
    bool opened=false,openFails=false;int playFailures=0;std::optional<bool> playing=false;
    std::vector<std::string> events;
    bool supported()const noexcept override{return true;}
    bool open(const std::filesystem::path& p)override{events.push_back("open:"+p.string());opened=!openFails;playing=false;return opened;}
    bool playFromStart()override{events.push_back("play");if(playFailures>0){--playFailures;return false;}if(!opened)return false;playing=true;return true;}
    std::optional<bool> isPlaying()override{events.push_back("status");return playing;}
    void stop()override{events.push_back("stop");playing=false;}
    void close()override{events.push_back("close");opened=false;playing=std::nullopt;}
};
}
int main(){
    using concord::ui::SkirmishMusicPolicy;
    Clock clock;Device d;SkirmishMusicPolicy p(clock,regular);
    expect(!p.isOpen()&&p.path().empty()&&p.nextProbe()==0,"initial pure state");
    p.start("a.wav",false,d);p.poll(false,d);expect(clock.reads==0&&probes==0&&d.events.empty(),"disabled guards before services");
    p.start({},true,d);expect(clock.reads==0&&probes==0,"empty path guard");
    exists=false;p.start("a.wav",true,d);expect(clock.reads==0&&probes==1&&d.events.empty(),"missing file guard before clock/device");
    exists=true;clock.value=100;p.start("a.wav",true,d);
    expect(p.isOpen()&&p.path()=="a.wav"&&p.nextProbe()==1100&&clock.reads==1,"exact start state and source sample");
    expect(d.events==std::vector<std::string>{"open:a.wav","play"},"start native-open abstraction then play");
    const auto prior=d.events;const int probeBefore=probes;p.start("a.wav",true,d);
    expect(clock.reads==1&&probes==probeBefore+1&&d.events==prior,"same path still probes file, never samples/restarts");
    clock.value=1099;p.poll(true,d);expect(d.events==prior&&p.nextProbe()==1100&&clock.reads==2,"deadline-minus1 does not touch device");
    clock.value=1100;p.poll(true,d);expect(d.events.back()=="status"&&p.nextProbe()==2100&&clock.reads==3,"playing deadline only status and nextdue");
    d.events.clear();d.playing=false;clock.value=2100;p.poll(true,d);
    expect(d.events==std::vector<std::string>{"status","play"}&&p.nextProbe()==3100&&p.isOpen(),"EOF loops by play without reopen");
    d.events.clear();d.playing=std::nullopt;d.playFailures=1;clock.sequence={3100,3200};clock.index=0;const int reads=clock.reads;p.poll(true,d);
    expect(clock.reads==reads+2&&clock.index==2&&p.nextProbe()==4200&&p.isOpen(),"recovery uses independent poll and start samples");
    expect(d.events==std::vector<std::string>{"status","play","close","open:a.wav","play"},"status-error/replay-error/close/reopen exact order");
    clock.sequence.clear();clock.value=4300;d.events.clear();d.openFails=true;p.start("b.wav",true,d);
    expect(!p.isOpen()&&p.path()=="b.wav"&&p.nextProbe()==5300,"failed new open retains requested path/due");
    expect(d.events==std::vector<std::string>{"stop","close","open:b.wav","close"},"changed-file stop/close before open failure cleanup");
    d.openFails=false;d.events.clear();clock.sequence={5300,5500};clock.index=0;p.poll(true,d);
    expect(p.isOpen()&&p.path()=="b.wav"&&p.nextProbe()==6500&&clock.index==2,"failed-open recovery retains request and samples twice");
    expect(d.events==std::vector<std::string>{"open:b.wav","play"},"not-open retry does not query stale device status");
    clock.sequence.clear();clock.value=1;const auto due=p.nextProbe();p.poll(true,d);expect(p.nextProbe()==due,"backward source follows original unsigned comparison");
    const auto current=d.events;const int beforeDisabled=clock.reads;const int beforeProbe=probes;p.poll(false,d);p.start("c.wav",false,d);
    expect(clock.reads==beforeDisabled&&probes==beforeProbe&&d.events==current&&p.path()=="b.wav","disabled does not clear/restart current session");
    exists=false;p.start("c.wav",true,d);expect(p.path()=="b.wav"&&p.isOpen(),"missing replacement leaves playing session alone");exists=true;
    d.events.clear();const int beforeStop=clock.reads;p.stop(d);
    expect(!p.isOpen()&&p.path().empty()&&p.nextProbe()==0&&clock.reads==beforeStop&&d.events==std::vector<std::string>{"stop","close"},"stop closes/clears without clock");
    p.stop(d);expect(d.events==std::vector<std::string>{"stop","close","stop","close"},"repeat stop retains original explicit calls");
    clock.value=std::numeric_limits<RuntimeUiTimeMs>::max()-500;p.start("a.wav",true,d);expect(p.nextProbe()==499,"uint64 addition wraps as original");
    d.playing=false;clock.value=498;d.events.clear();p.poll(true,d);expect(d.events.empty()&&p.nextProbe()==499,"wrapped deadline-minus1");
    clock.value=499;p.poll(true,d);expect(p.nextProbe()==1499&&d.events==std::vector<std::string>{"status","play"},"wrapped exact deadline loop");
    p.stop(d);
    auto unsupported=concord::audio::makeUnsupportedMenuMusicDevice();
    expect(!unsupported->supported()&&!unsupported->open("a.wav")&&!unsupported->playFromStart()&&!unsupported->isPlaying(),"unsupported never reports playback success");
    SkirmishMusicPolicy silent(clock,regular);silent.start("a.wav",true,*unsupported);
    expect(!silent.isOpen()&&silent.path()=="a.wav","unsupported is failed capability, not fake playback");
    silent.stop(*unsupported);expect(silent.path().empty(),"unsupported explicit stop still clears policy");
    SkirmishMusicPolicy missingProbe(clock,nullptr);const int beforeNull=clock.reads;missingProbe.start("a.wav",true,d);expect(clock.reads==beforeNull&&!missingProbe.isOpen(),"absent typed file probe safely rejects");
    std::cout<<"Skirmish music policy: checks="<<checks<<" failures="<<failures<<" pureClockDevicePort=yes unsupported=explicit noNativeHeader=yes\n";
    return failures?1:0;
}
