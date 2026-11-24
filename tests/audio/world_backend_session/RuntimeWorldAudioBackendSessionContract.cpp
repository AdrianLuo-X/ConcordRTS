// Contract-only deterministic device model. Native playback is verified by the
// unchanged real-device consumers; this executable makes no playback claim.
#include "audio/world/RuntimeWorldAudioBackendSession.h"
#include "audio/backend/RuntimeVoiceAudio.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Voice { bool playing=false; unsigned queued=1; int gain=0,pan=8192; };
struct Device {
    std::map<std::uint64_t,Voice> voices;
    std::vector<std::string> calls;
    bool open=true,mix=true,start=true,queue=true;
    std::uint64_t throwClose=0;
};
std::map<const RuntimeVoiceAudio*,Device*> devices;
Device& device(const RuntimeVoiceAudio& audio) { return *devices.at(&audio); }
}
struct RuntimeVoiceAudio::Impl { Device state; };
RuntimeVoiceAudio::RuntimeVoiceAudio(bool):impl_(std::make_unique<Impl>()) { devices.emplace(this,&impl_->state); }
RuntimeVoiceAudio::~RuntimeVoiceAudio() { devices.erase(this); }
bool RuntimeVoiceAudio::ready() const { return true; }
std::int32_t RuntimeVoiceAudio::lastError() const { return 0; }
bool RuntimeVoiceAudio::open(std::uint64_t id,const std::string&) {
    auto& m=device(*this);m.calls.push_back("open");if(!m.open)return false;
    m.voices[id]={};return true;
}
bool RuntimeVoiceAudio::setMix(std::uint64_t id,int gain,int pan) {
    auto& m=device(*this);m.calls.push_back("mix");
    if(!m.mix||!m.voices.contains(id))return false;
    m.voices.at(id).gain=gain;m.voices.at(id).pan=pan;return true;
}
bool RuntimeVoiceAudio::playFromStart(std::uint64_t id,bool,bool) {
    auto& m=device(*this);m.calls.push_back("start");
    if(!m.start||!m.voices.contains(id))return false;
    m.voices.at(id).playing=true;return true;
}
bool RuntimeVoiceAudio::queue(std::uint64_t id,const std::string&) {
    auto& m=device(*this);m.calls.push_back("queue");
    if(!m.queue||!m.voices.contains(id))return false;
    ++m.voices.at(id).queued;return true;
}
unsigned RuntimeVoiceAudio::queuedBuffers(std::uint64_t id) const {
    const auto& m=device(*this);return m.voices.contains(id)?m.voices.at(id).queued:0;
}
std::optional<bool> RuntimeVoiceAudio::isPlaying(std::uint64_t id) const {
    auto& m=device(*this);m.calls.push_back("query");
    if(!m.voices.contains(id))return std::nullopt;return m.voices.at(id).playing;
}
bool RuntimeVoiceAudio::stop(std::uint64_t id) {
    auto& m=device(*this);m.calls.push_back("stop");
    if(!m.voices.contains(id))return false;m.voices.at(id).playing=false;return true;
}
void RuntimeVoiceAudio::close(std::uint64_t id) {
    auto& m=device(*this);m.calls.push_back("close");
    if(m.throwClose==id)throw std::runtime_error("close");m.voices.erase(id);
}
RuntimeVoiceAudioProbe RuntimeVoiceAudio::probe(std::uint64_t) const { return {}; }
std::size_t RuntimeVoiceAudio::voiceCount() const { return device(*this).voices.size(); }

namespace {
using Session=concord::audio::RuntimeWorldAudioBackendSession;
struct Host final:concord::audio::RuntimeWorldAudioBackendHost {
    Session& session;
    std::unique_ptr<RuntimeVoiceAudio> audio;
    RuntimeSpatialAudioResult spatialResult{16384,8192};
    bool enabled=true,effects=true;
    int logicalTick=91;
    std::optional<std::string> path="voice.wav";
    std::vector<std::string> queries;
    std::string throwAt;
    std::uint64_t evict=0;
    explicit Host(Session& value):session(value) {}
    void mark(const char* name) { queries.emplace_back(name);if(throwAt==name)throw std::runtime_error(name); }
    RuntimeSpatialAudioPosition sourcePosition(const RuntimeWorldAudioAction& a) override {
        mark("position");return {double(a.cell.x),double(a.cell.y),double(a.cell.level)};
    }
    RuntimeSpatialAudioResult spatial(const std::string&,const RuntimeSpatialAudioPosition&) override {
        mark("spatial");return spatialResult;
    }
    bool soundsEnabled() override { mark("enabled");return enabled; }
    int effectiveGain(int authored,int gain) override { mark("gain");return effects?authored*gain/1000:0; }
    std::optional<std::string> samplePath(const RuntimeWorldAudioAction&) override { mark("sample");return path; }
    RuntimeVoiceAudio* audioIfInitialized() override { mark("existing");return audio.get(); }
    RuntimeVoiceAudio& audioFor() override {
        mark("initialize");if(!audio)audio=std::make_unique<RuntimeVoiceAudio>(true);return *audio;
    }
    void enforceVoiceLimit(const std::string&,std::uint64_t excluded) override {
        mark("limit");if(evict&&evict!=excluded){audio->stop(evict);session.evictVoice(evict,*audio);evict=0;}
    }
    int tick() override { mark("tick");return logicalTick; }
};
unsigned checks=0,failures=0;
void need(bool value,const char* why) { ++checks;if(!value){++failures;std::cerr<<why<<'\n';} }
RuntimeWorldAudioAction action(std::uint64_t id=1) {
    RuntimeWorldAudioAction a;a.handleId=id;a.eventId="event";a.sampleName="sample";
    a.gainPermille=1000;a.cell={3,4,2};a.eventTick=17;return a;
}
bool saw(const Host& h,const std::string& q) { return std::find(h.queries.begin(),h.queries.end(),q)!=h.queries.end(); }
void clear(Host& h) { h.queries.clear();if(h.audio)device(*h.audio).calls.clear(); }
void contract() {
    {
        Session s;Host h(s);auto a=action();a.kind=RuntimeWorldAudioActionKind::Stop;
        s.apply(a,h);need(!h.audio&&s.bindings().empty()&&s.history().size()==1,"stop never initializes device");
        need(h.queries==std::vector<std::string>{"existing"},"stop original lazy query");
        h.enabled=false;a.kind=RuntimeWorldAudioActionKind::Play;clear(h);s.apply(a,h);
        need(!h.audio&&s.history().back().authoredGainPermille==1000,"muted action is still observed");
        need(h.queries==std::vector<std::string>({"position","spatial","enabled"}),"muted phase order");
        for(int i=0;i<48;++i){a.eventTick=i;s.apply(a,h);}
        need(s.history().size()==32&&s.history().front().eventTick==16&&s.history().back().eventTick==47,"bounded ordered history");
        s.clearHistory();need(s.history().empty(),"explicit diagnostic clear");
    }
    {
        Session s;Host h(s);auto a=action();a.kind=RuntimeWorldAudioActionKind::SetGain;s.apply(a,h);
        need(!h.audio&&s.bindings().empty()&&s.history().size()==1,"missing gain binding does not initialize");
        need(h.queries==std::vector<std::string>({"position","spatial","enabled","gain"}),"missing gain phase order");
    }
    for(int mode=0;mode<2;++mode) {
        Session s;Host h(s);auto a=action();
        if(mode==0)h.path.reset();else h.spatialResult.gain16384=0;
        s.apply(a,h);need(h.audio&&s.bindings().empty(),"missing/inaudible one-shot still follows lazy init and close");
        need(device(*h.audio).calls==std::vector<std::string>{"close"},"missing/inaudible no open");
    }
    {
        Session s;Host h(s);auto a=action();a.backendLoopIterations=-1;h.spatialResult.gain16384=0;s.apply(a,h);
        need(s.bindings().size()==1&&!s.bindings().at(1).active,"inaudible indefinite loop retains binding");
        need(device(*h.audio).calls==std::vector<std::string>({"close","open","mix"}),"inaudible loop opens without starting");
        need(s.bindings().at(1).playbackOrder==1&&!saw(h,"limit"),"inaudible loop serial without limit");
        clear(h);h.spatialResult={8192,4096};s.mix(h);
        need(s.bindings().at(1).active&&s.bindings().at(1).action.eventTick==91&&s.bindings().at(1).playbackOrder==2,"audible loop restarts at current tick/order");
        need(device(*h.audio).calls==std::vector<std::string>({"start","mix","query"}),"loop restart/mix/reap order");
        need(s.history().back().kind==RuntimeWorldAudioActionKind::SetGain&&s.history().back().gainPermille==500,"mix observation preserves authored scaling");
        clear(h);s.mix(h);need(device(*h.audio).calls==std::vector<std::string>{"query"},"unchanged mix only reaps");
        h.spatialResult.gain16384=0;clear(h);s.mix(h);
        need(!s.bindings().at(1).active&&device(*h.audio).calls==std::vector<std::string>({"stop","mix"}),"loop leaving hearing range stops but retains PCM");
        h.spatialResult.gain16384=16384;device(*h.audio).start=false;clear(h);s.mix(h);
        need(!s.bindings().at(1).active&&s.bindings().at(1).playbackOrder==2,"failed loop restart has no serial commit");
        device(*h.audio).start=true;clear(h);s.mix(h);need(s.bindings().at(1).playbackOrder==3,"later restart retains original retry policy");
    }
    {
        Session s;Host h(s);auto a=action();s.apply(a,h);h.spatialResult.gain16384=0;clear(h);s.mix(h);
        need(s.bindings().empty()&&h.audio->voiceCount()==0,"finite shot exits permanently at hearing boundary");
        need(device(*h.audio).calls==std::vector<std::string>({"stop","close"}),"finite stop then close");
        h.spatialResult.gain16384=16384;s.mix(h);need(s.bindings().empty(),"finite shot does not resurrect");
    }
    {
        Session s;Host h(s);auto a=action();a.trackCamera=false;s.apply(a,h);clear(h);h.spatialResult.gain16384=0;s.mix(h);
        need(s.bindings().at(1).active&&!saw(h,"position")&&!saw(h,"spatial"),"fixed mix ignores camera service");
        device(*h.audio).voices.at(1).playing=false;s.reap(h);need(s.bindings().empty(),"natural end reaps active binding");
        s.apply(a,h);device(*h.audio).voices.erase(1);s.reap(h);need(s.bindings().empty(),"unknown isPlaying also reaps");
    }
    {
        Session s;Host h(s);auto a=action();a.sourceObjectId=7;a.queueAfterCurrent=true;s.apply(a,h);
        const auto first=s.bindings().at(1).playbackOrder;clear(h);s.apply(a,h);
        need(h.audio->queuedBuffers(1)==2&&s.bindings().at(1).playbackOrder==first,"queue append never republishes binding/serial");
        need(device(*h.audio).calls==std::vector<std::string>{"queue"},"queue success avoids close/open/restart");
        s.evictVoice(1,*h.audio);need(s.limitEvicted(1)&&s.bindings().empty(),"queued source eviction remembered");
        clear(h);s.apply(a,h);need(!saw(h,"sample")&&!saw(h,"initialize")&&s.bindings().empty(),"evicted sequence cannot reopen");
        auto stop=a;stop.kind=RuntimeWorldAudioActionKind::Stop;s.apply(stop,h);need(!s.limitEvicted(1),"explicit stop clears eviction");
        s.apply(a,h);need(s.bindings().at(1).playbackOrder==first+1,"stop does not reset shared voice serial");
    }
    for(int failure=0;failure<4;++failure) {
        Session s;Host h(s);auto a=action();a.queueAfterCurrent=true;s.apply(a,h);auto& m=device(*h.audio);clear(h);m.queue=false;
        if(failure==0)m.open=false;if(failure==1)m.mix=false;if(failure==2)m.start=false;
        s.apply(a,h);
        need(m.calls.front()=="queue"&&m.calls[1]=="close","queue failure follows original replace path");
        if(failure<3)need(s.bindings().empty()&&m.voices.empty(),"failed device stage leaves no published voice");
        else need(s.bindings().size()==1&&s.bindings().at(1).playbackOrder==2,"failed queue may replace successfully");
    }
    {
        Session s;Host h(s);auto a=action();a.sourceObjectId=9;s.apply(a,h);auto update=action();update.kind=RuntimeWorldAudioActionKind::SetGain;
        update.eventId.clear();update.cell={90,91,92};update.authoredGainPermille=-1;update.gainPermille=17;h.spatialResult={8192,1234};clear(h);s.apply(update,h);
        const auto& b=s.bindings().at(1);
        need(b.action.eventId=="event"&&b.action.cell.x==3&&b.action.sourceObjectId==9&&b.action.authoredGainPermille==1000,"gain inherits prior identity/source/authored value");
        need(b.gainPermille==500&&b.gain16384==8192&&b.pan16384==1234,"gain commits exact local mix");
        device(*h.audio).mix=false;h.spatialResult={4096,4321};s.apply(update,h);
        need(s.bindings().at(1).gain16384==8192&&s.history().back().spatialGain16384==4096,"failed mix preserves binding but records action");
    }
    {
        Session s;Host h(s);auto a=action();s.apply(a,h);const auto& borrowed=s.bindings().at(1).action;
        s.apply(borrowed,h);need(s.bindings().size()==1&&s.bindings().at(1).playbackOrder==2,"binding-alias action copied before replacement");
        const auto& observed=s.history().back();s.apply(observed,h);need(s.bindings().at(1).playbackOrder==3,"history-alias action copied before observation append");
        need(s.nextPlaybackOrder()==4,"SFX peer shares same monotonic order");
    }
    {
        Session s;Host h(s);auto a=action();a.queueAfterCurrent=true;a.sourceObjectId=9;s.apply(a,h);
        h.evict=1;auto second=action(2);s.apply(second,h);
        need(s.bindings().size()==1&&s.bindings().contains(2)&&s.limitEvicted(1),"voice-limit peer observes and evicts original first binding");
        const auto history=s.history().size();s.stop(h);
        need(s.bindings().empty()&&!s.limitEvicted(1)&&s.history().size()==history,"shutdown preserves observations and clears only voice state");
        s.stop(h);need(h.audio->voiceCount()==0,"repeated shutdown empty");
    }
    for(const std::string phase:{"position","spatial","enabled","gain","sample","initialize","limit"}) {
        Session s;Host h(s);h.throwAt=phase;bool threw=false;
        try{s.apply(action(),h);}catch(const std::runtime_error&){threw=true;}
        need(threw&&s.bindings().empty(),"service exception remains visible without binding publication");
        const bool beforeObservation=phase=="position"||phase=="spatial";
        need(s.history().size()==(beforeObservation?0U:1U),"exception observation boundary preserved");
    }
    {
        Session s;Host h(s);auto a=action();s.apply(a,h);device(*h.audio).throwClose=1;bool threw=false;
        try{s.apply(a,h);}catch(const std::runtime_error&){threw=true;}
        need(threw&&s.bindings().size()==1&&s.history().size()==2,"close exception precedes binding erase");
        device(*h.audio).throwClose=0;h.audio.reset();s.reap(h);need(s.bindings().size()==1,"no initialized device reap is unchanged");
        s.stop(h);need(s.bindings().empty(),"shutdown clears bindings even with no device");
    }
}
}
int main() {
    try{contract();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    need(devices.empty(),"model devices destroyed before exit");
    std::cout<<"World audio backend session contract: checks="<<checks<<" failures="<<failures<<" nativePlayback=not-claimed\n";
    return failures?1:0;
}
