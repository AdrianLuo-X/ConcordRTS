class MainPresentationFixtureClock final : public RuntimeUiClock {
public:
    std::uint64_t value=0;
    std::vector<std::uint64_t> values;
    mutable std::size_t cursor=0;
    mutable std::vector<std::uint64_t> reads;
    std::uint64_t nowMs() const noexcept override {
        const auto result=cursor<values.size()?values[cursor++]:value;
        reads.push_back(result);return result;
    }
};

std::string mainPresentationState(const MenuState& s) {
    std::ostringstream o;
    o<<s.initialized<<' '<<s.smoke<<' '<<s.closing<<' '<<static_cast<int>(s.result)<<' '
     <<static_cast<int>(s.page)<<' '<<static_cast<int>(s.phase)<<' '<<static_cast<int>(s.pending)<<' '
     <<s.phaseStarted<<' '<<s.phaseTick<<' '<<s.focus<<' '<<s.hover<<' '<<s.pressed<<' '
     <<s.keyboardTooltip<<' '<<s.mouseVisible<<' '<<s.backDirty<<' '<<s.warningFrame<<' '
     <<s.optionWindowWidth<<' '<<s.optionWindowHeight<<' '<<s.optionTargetFps<<' '<<s.optionGameSpeed<<' '
     <<s.movieIndex<<' '<<s.creditsPage<<' '<<s.videoFirstFramePending<<' '<<s.videoStartupStarted<<' '
     <<s.videoAnimation.sequence()<<' '<<s.videoAnimation.finished()<<' '
     <<std::quoted(s.videoAnimation.failure())<<' '<<s.audio.enabled<<' '
     <<s.audio.musicOpen<<' '<<s.audio.nextMusicProbe<<' '<<s.audio.musicVolumePercent;
    for(const auto c:s.pageStatus)o<<' '<<static_cast<unsigned>(c);
    auto image=[&](const AssetImage& a){o<<" image "<<a.width<<' '<<a.height<<' '<<std::quoted(a.source);for(auto b:a.bgra)o<<' '<<static_cast<unsigned>(b);};
    image(s.videoFrame);image(s.staticFallback);
    return o.str();
}

int runMainMenuPresentationClockSmoke(const RuntimeConfig& config,const fs::path& output) {
    if(!output.is_absolute() || fs::exists(output)) {
        std::cerr<<"Main presentation clock requires a new absolute output directory\n";return 1;
    }
    fs::create_directories(output);std::ofstream currentTrace(output/"current.txt",std::ios::binary);
    std::ofstream originalTrace(output/"original.txt",std::ios::binary);
    int checks=0,failures=0;std::size_t comparisons=0,sourceReads=0;
    auto expect=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"Main presentation FAILED: "<<why<<'\n';}};
    const std::array<std::uint64_t,5> bases{{0U,10U,0xffffffffULL-3U,0x100000000ULL+500U,std::numeric_limits<std::uint64_t>::max()-1000U}};
    for(std::size_t scene=0;scene<bases.size();++scene) {
        MainPresentationFixtureClock currentClock,originalClock;
        MenuState a,b;MainPresentationOriginal original{&originalClock};
        expect(a.presentationClock==&runtimeWin32UiClock(),"default presentation provider is existing immutable native source");
        expect(a.audio.clock==&runtimeWin32UiClock(),"audio default independently retains same native provider");
        a.presentationClock=&currentClock;a.audio.enabled=b.audio.enabled=false;a.smoke=b.smoke=true;
        a.config=b.config=config;a.phase=b.phase=MenuPhase::Idle;
        for(MenuState* state:{&a,&b}) {
            state->staticFallback.width=2;state->staticFallback.height=1;
            state->staticFallback.bgra={3,5,7,0,11,13,17,255};state->staticFallback.source="owned clock fallback";
            state->warning.resize(3);
        }
        auto compare=[&](const char* label) {
            ++comparisons;const auto ca=mainPresentationState(a),cb=mainPresentationState(b);
            expect(ca==cb,"all mutable phase/video consumer state and full frame bytes equal");
            expect(currentClock.reads==originalClock.reads,"per-call source reads/order equal");
            currentTrace<<scene<<' '<<label<<' '<<ca<<" reads";originalTrace<<scene<<' '<<label<<' '<<cb<<" reads";
            for(auto v:currentClock.reads)currentTrace<<' '<<v;
            for(auto v:originalClock.reads)originalTrace<<' '<<v;
            currentTrace<<'\n';originalTrace<<'\n';
        };
        currentClock.value=originalClock.value=bases[scene];
        beginAction(a,MenuAction::None);original.beginAction(b,MenuAction::None);
        expect(currentClock.reads.empty(),"None action does not read clock");compare("none");
        beginAction(a,MenuAction::OpenCredits);original.beginAction(b,MenuAction::OpenCredits);
        expect(currentClock.reads.size()==1U && a.phaseStarted==bases[scene],"accepted action samples once at original point");compare("begin");
        beginAction(a,MenuAction::OpenMovies);original.beginAction(b,MenuAction::OpenMovies);
        expect(currentClock.reads.size()==1U,"busy action returns before clock");compare("busy");
        const auto duration=static_cast<std::uint64_t>(slotCount(a)-1+kAnimationFrames)*kAnimationFrameMs;
        for(const auto delta:std::array<std::uint64_t,9>{{0U,1U,kAnimationFrameMs-1U,kAnimationFrameMs,duration-1U,duration,duration+1U,2U*duration,2U*duration+1U}}) {
            currentClock.value=originalClock.value=bases[scene]+delta;
            const bool ca=updateDynamicState(a),cb=original.updateDynamicState(b);
            expect(ca==cb,"dynamic update changed flag equal");compare("dynamic");
        }
        a.closing=b.closing=true;const auto before=currentClock.reads.size();
        expect(!updateDynamicState(a) && !original.updateDynamicState(b),"closing update returns false");
        beginAction(a,MenuAction::Exit);original.beginAction(b,MenuAction::Exit);
        expect(currentClock.reads.size()==before,"closing paths do not sample");compare("closing");
        a.closing=b.closing=false;a.phase=b.phase=MenuPhase::Idle;
        expect(!pollMainMenuVideo(a) && !original.pollMainMenuVideo(b),"not-pending video poll unchanged");
        expect(currentClock.reads.size()==before,"not-pending video poll does not sample");compare("no-pending");
        a.videoFirstFramePending=b.videoFirstFramePending=true;
        a.videoStartupStarted=b.videoStartupStarted=bases[scene];
        currentClock.value=originalClock.value=bases[scene]+59999U;
        expect(!pollMainMenuVideo(a) && !original.pollMainMenuVideo(b),"59999 pending state retained");
        expect(a.videoFirstFramePending,"pending flag remains before deadline");compare("deadline-before");
        // Update samples phase first and pending video separately. They must not be coalesced.
        currentClock.values={bases[scene]+1U,bases[scene]+59999U};originalClock.values=currentClock.values;
        currentClock.cursor=originalClock.cursor=0;
        expect(updateDynamicState(a)==original.updateDynamicState(b),"two-read dynamic result equal");
        expect(currentClock.cursor==2U,"phase and pending-video each sample independently");compare("two-reads");
        currentClock.values.clear();originalClock.values.clear();
        currentClock.value=originalClock.value=bases[scene]+60000U;
        expect(pollMainMenuVideo(a) && original.pollMainMenuVideo(b),"60000 pending video enters existing deadline fallback");
        expect(!a.videoFirstFramePending && a.videoFrame.bgra==a.staticFallback.bgra,"deadline copies complete existing fallback pixels");compare("deadline-at");
        currentClock.value=originalClock.value=bases[scene]-1U;
        expect(updateDynamicState(a)==original.updateDynamicState(b),"backwards unsigned phase policy preserved");compare("backwards");
        RuntimeConfig missing=config;missing.resourceRootPath=(output/"missing-resource-root").string();
        a.exportRoot=b.exportRoot=output/"missing-export";
        const auto count=currentClock.reads.size();
        expect(!startMainMenuVideo(a,missing,0) && !original.startMainMenuVideo(b,missing,0),"actual video service missing-input start fails closed");
        expect(currentClock.reads.size()==count+1U && a.videoStartupStarted==currentClock.value,"failed video start samples exactly once before start");
        expect(!a.videoAnimation.failure().empty() && !a.videoFirstFramePending,"original video failure and fallback retained");compare("start-failure");
        expect(a.audio.clock==&runtimeWin32UiClock(),"presentation injection did not replace audio clock");
        sourceReads+=currentClock.reads.size();
    }
    currentTrace.close();originalTrace.close();expect(currentTrace.good() && originalTrace.good(),"two explicit state/read traces closed");
    std::cout<<"Main presentation clock contract: checks="<<checks<<" failures="<<failures<<" skipped=0 comparisons="<<comparisons
        <<" sourceReads="<<sourceReads<<" originalMethods=4 deadline=59999,60000 independentReads=2 nativeVideoSuccess=separate-full-menu\n";
    return failures==0?0:1;
}
