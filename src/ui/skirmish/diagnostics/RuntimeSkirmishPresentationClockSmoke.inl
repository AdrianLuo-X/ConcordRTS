namespace {
std::optional<std::string> skirmishPresentationEnvironment(const char* name) {
    char* value=nullptr;std::size_t size=0;
    if(_dupenv_s(&value,&size,name)!=0)throw std::runtime_error("skirmish clock environment read failed");
    std::unique_ptr<char,decltype(&std::free)> owned(value,&std::free);
    return value?std::optional<std::string>(value):std::nullopt;
}
class SkirmishPresentationFixtureClock final : public RuntimeUiClock {
public:
    std::uint64_t value=0;
    mutable std::vector<std::uint64_t> reads;
    std::uint64_t nowMs() const noexcept override { reads.push_back(value);return value; }
};
// Reuse the production virtual service; MenuState gets no new field/provider.
struct SkirmishPresentationFixtureState final : MenuState {
    const RuntimeUiClock* fixtureSource;
    mutable std::size_t virtualReads=0;
    explicit SkirmishPresentationFixtureState(const RuntimeUiClock& clock):fixtureSource(&clock){}
    std::uint64_t nowMs() const override { ++virtualReads;return fixtureSource->nowMs(); }
};
std::string skirmishPresentationState(const MenuState& state) {
    std::ostringstream o;o<<secondWindowController(state)<<" phaseStart:"<<state.phaseStarted
        <<" dirty:"<<state.backDirty<<" windowAlive:"<<(state.hwnd && IsWindow(state.hwnd));
    return o.str();
}
int runSkirmishPresentationClockSmoke(const RuntimeConfig& config,const fs::path& output) {
    if(!output.is_absolute() || fs::exists(output)){std::cerr<<"Skirmish clock requires a new absolute output directory\n";return 1;}
    fs::create_directories(output);std::ofstream currentTrace(output/"current.txt",std::ios::binary),originalTrace(output/"original.txt",std::ios::binary);
    int checks=0,failures=0;std::size_t states=0,sourceReads=0,currentVirtual=0,originalVirtual=0;
    auto expect=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"Skirmish clock FAILED: "<<why<<'\n';}};
    {
        MenuState native;
        expect(native.softwareWindow==nullptr,"default native owner has no software window");
        for(int i=0;i<128;++i){const auto first=GetTickCount64();const auto value=native.nowMs();const auto last=GetTickCount64();expect(first<=value && value<=last,"existing native service value bracketed by old SDK samples");}
    }
    const auto duration=static_cast<std::uint64_t>(kShellSlotCount-1+kShellAnimationFrames)*kShellAnimationFrameMs;
    const std::array<std::uint64_t,5> bases{{0U,1U,0xffffffffULL-30U,0x100000001ULL,std::numeric_limits<std::uint64_t>::max()-100U}};
    for(std::size_t scene=0;scene<bases.size();++scene) {
        SkirmishPresentationFixtureClock ca,cb;
        SkirmishPresentationFixtureState a(ca),b(cb);SkirmishOriginalTimer original{&cb};
        const bool ai=initializeState(a,config,true),bi=initializeState(b,config,true);
        expect(ai && bi,"both actual resource states initialized");if(!ai || !bi)return 1;
        a.audio.enabled=b.audio.enabled=false;
        HWND wa=createShellSkirmishWindow(a,config,nullptr),wb=createShellSkirmishWindow(b,config,nullptr);
        expect(wa && wb && a.hwnd==wa && b.hwnd==wb,"two actual owned native WndProc windows attached");if(!wa || !wb)return 1;
        expect(GetWindowThreadProcessId(wa,nullptr)==GetCurrentThreadId() && GetWindowThreadProcessId(wb,nullptr)==GetCurrentThreadId(),"both windows owned by this UI thread");
        auto compare=[&](const char* label) {
            ++states;const auto sa=skirmishPresentationState(a),sb=skirmishPresentationState(b);
            expect(sa==sb,"complete existing P11 controller/selection/setup snapshot plus phase time/window state equal");
            expect(ca.reads==cb.reads,"all current/original source values and order equal");
            currentTrace<<scene<<' '<<label<<' '<<sa<<" reads";originalTrace<<scene<<' '<<label<<' '<<sb<<" reads";
            for(auto v:ca.reads)currentTrace<<' '<<v;for(auto v:cb.reads)originalTrace<<' '<<v;
            currentTrace<<'\n';originalTrace<<'\n';
        };
        auto dispatch=[&](std::uint64_t value,WPARAM id,const char* label) {
            ca.value=cb.value=value;const auto count=ca.reads.size();const auto va=a.virtualReads,vb=b.virtualReads;
            const auto x=SendMessageW(wa,WM_TIMER,id,0),y=original.dispatch(&b,wb,id);
            expect(x==y,"actual native WndProc returns original result");
            expect(ca.reads.size()==count+(id==2?1U:0U),"original timer-id guard and one unconditional transition sample preserved");
            expect(a.virtualReads-va==(b.virtualReads-vb)+(id==2?1U:0U),"actual WndProc now routes time through existing virtual service");
            compare(label);
        };
        a.phase=b.phase=ShellMenuPhase::Idle;dispatch(bases[scene],1U,"other-timer");dispatch(bases[scene],2U,"idle-still-reads");
        a.closing=b.closing=true;dispatch(bases[scene],2U,"closing-still-reads");a.closing=b.closing=false;
        for(const auto delta:std::array<std::uint64_t,8>{{0U,41U,42U,duration-1U,duration,duration+1U,0xffffffffffffffffULL,1U}}) {
            a.phase=b.phase=ShellMenuPhase::SlideIn;a.phaseTick=b.phaseTick=0;
            a.phaseStarted=b.phaseStarted=bases[scene];a.backDirty=b.backDirty=false;
            dispatch(bases[scene]+delta,2U,"slide-in-boundary");
        }
        for(const auto delta:std::array<std::uint64_t,4>{{0U,41U,42U,duration-1U}}) {
            a.phase=b.phase=ShellMenuPhase::SlideOut;a.phaseTick=b.phaseTick=0;
            a.phaseStarted=b.phaseStarted=bases[scene];a.backDirty=b.backDirty=false;
            dispatch(bases[scene]+delta,2U,"slide-out-before");
        }
        a.phase=b.phase=ShellMenuPhase::SlideOut;a.phaseTick=b.phaseTick=0;
        a.phaseStarted=b.phaseStarted=bases[scene];a.backDirty=b.backDirty=false;
        dispatch(bases[scene]+duration,2U,"slide-out-at");
        expect(!IsWindow(wa) && !IsWindow(wb) && a.hwnd==nullptr && b.hwnd==nullptr,"both actual native transition paths close and detach owned window");
        expect(a.closing && b.closing,"original controller closing state preserved");
        sourceReads+=ca.reads.size();currentVirtual+=a.virtualReads;originalVirtual+=b.virtualReads;
    }
    currentTrace.close();originalTrace.close();expect(currentTrace.good() && originalTrace.good(),"complete current/original traces closed");
    std::cout<<"Skirmish presentation clock contract: checks="<<checks<<" failures="<<failures<<" skipped=0 states="<<states<<" sourceReads="<<sourceReads
        <<" currentVirtual="<<currentVirtual<<" originalVirtual="<<originalVirtual
        <<" sdkBrackets=128 originalTimerCase=complete productionController=unchanged productionStateFieldsAdded=0 sdl=separate-real-matrix\n";
    return failures==0?0:1;
}
} // namespace
