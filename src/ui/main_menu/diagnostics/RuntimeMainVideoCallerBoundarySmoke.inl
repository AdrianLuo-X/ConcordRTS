// Explicit private observations of the existing Main video caller, not a replacement player.
namespace {
struct MainVideoBoundaryLogicalClock final:RuntimeUiClock {
    RuntimeUiTimeMs time=0;
    RuntimeUiTimeMs nowMs() const noexcept override{return time;}
};
int runMainVideoCallerBoundarySmoke(const RuntimeConfig& config,const fs::path& output) {
    if(fs::exists(output))return 1;
    std::error_code error;fs::create_directories(output,error);if(error)return 1;
    std::ofstream report(output/"caller.csv");if(!report)return 1;
    int checks=0,failures=0;
    const auto check=[&](bool value,const char* name){++checks;report<<name<<','<<value<<std::endl;if(!value)++failures;};
    const auto same=[](const AssetImage& a,const AssetImage& b){return a.width==b.width&&a.height==b.height&&a.bgra==b.bgra&&a.source==b.source;};
    AssetImage sentinel;sentinel.width=2;sentinel.height=1;sentinel.bgra={1,2,3,255,4,5,6,255};sentinel.source="original static fallback sentinel";
    {
        BinkMenuAnimation player;auto image=sentinel;
        check(!player.poll(image,0)&&same(image,sentinel),"inactive wrapper keeps complete owned frame");
        player.stop();player.stop();
        check(player.sequence()==0&&!player.finished()&&player.failure().empty(),"inactive repeated stop state");
    }
    const fs::path movie=fs::path(config.localDataRootPath)/"resources/mix_exports/movmd03/s07_p01e.bik";
    check(regularFile(movie),"original finite BIK input exists");
    {
        BinkMenuAnimation player;auto frame=sentinel;
        check(player.start(config,{},movie,concord::bink_stream::PlaybackMode::Once,false,0),"actual original BIK helper starts");
        const bool ready=player.poll(frame,2000);
        check(ready&&frame.width==140&&frame.height==110&&frame.bgra.size()==140U*110U*4U&&player.sequence()>0,"actual original BIK first frame becomes owned AssetImage");
        const auto frozen=frame;player.stop();
        check(same(frame,frozen),"owned decoded frame survives original helper stop");
        check(player.sequence()==0&&!player.finished(),"real active helper stop resets sequence and finished");
    }
    {
        MainVideoBoundaryLogicalClock logical;
        MenuState state;state.staticFallback=sentinel;state.videoFrame=sentinel;
        state.presentationClock=&logical;state.videoFirstFramePending=true;state.videoStartupStarted=100;
        logical.time=100+kMainMenuFirstFrameDeadlineMs-1;
        check(!pollMainMenuVideo(state,0)&&state.videoFirstFramePending,"production caller stays pending at59999ms logical boundary");
        check(same(state.videoFrame,sentinel),"pending preview remains caller owned");
        logical.time=100+kMainMenuFirstFrameDeadlineMs;
        check(pollMainMenuVideo(state,0)&&!state.videoFirstFramePending,"production caller deadline fires at original60000ms logical boundary");
        check(same(state.videoFrame,state.staticFallback),"deadline restores exact original static fallback");
        check(state.videoAnimation.sequence()==0&&!state.videoAnimation.finished(),"deadline invokes original player stop");
        check(!pollMainMenuVideo(state,0),"completed failure is not published twice");
    }
    {
        MenuState state;state.staticFallback=sentinel;state.videoFrame=sentinel;
        const auto missing=output/"definitely-missing.bik";
        check(!state.videoAnimation.start(config,{},missing,concord::bink_stream::PlaybackMode::Loop,false,0),"real missing movie start fails");
        const auto actualError=state.videoAnimation.failure();check(!actualError.empty(),"real service error available to Main");
        state.videoFirstFramePending=true;state.videoStartupStarted=state.presentationClock->nowMs();
        check(pollMainMenuVideo(state,0)&&!state.videoFirstFramePending,"real start error uses original Main failure branch");
        check(same(state.videoFrame,state.staticFallback),"missing movie restores exact fallback");
        check(state.videoAnimation.failure()==actualError,"stop preserves original service error");
    }
    {
        const auto bad=output/"bad-header.bik";std::ofstream bytes(bad,std::ios::binary);bytes<<"This is deliberately not a BIK header.";bytes.close();
        MenuState state;state.staticFallback=sentinel;state.videoFrame=sentinel;
        check(regularFile(bad),"owned malformed BIK exists before decoder starts");
        const bool started=state.videoAnimation.start(config,{},bad,concord::bink_stream::PlaybackMode::Once,false,0);
        check(!started&&state.videoAnimation.failure()=="the Bink movie has an invalid header","original geometry precheck rejects malformed BIK before helper");
        state.videoFirstFramePending=true;state.videoStartupStarted=state.presentationClock->nowMs();
        const bool changed=pollMainMenuVideo(state,2000);
        check(changed&&!state.videoFirstFramePending&&!state.videoAnimation.failure().empty(),"actual geometry precheck failure reaches existing Main failure branch");
        check(same(state.videoFrame,state.staticFallback),"malformed BIK restores exact fallback");
        check(state.videoAnimation.sequence()==0&&!state.videoAnimation.finished(),"malformed BIK failure closes original helper");
    }
    {
        const auto truncated=output/"truncated.bik";
        std::array<char,36> prefix{};std::ifstream original(movie,std::ios::binary);original.read(prefix.data(),prefix.size());
        check(original.gcount()==static_cast<std::streamsize>(prefix.size()),"original36byte geometry prefix read");
        std::ofstream bytes(truncated,std::ios::binary);bytes.write(prefix.data(),prefix.size());bytes.close();
        MenuState state;state.staticFallback=sentinel;state.videoFrame=sentinel;
        const bool started=state.videoAnimation.start(config,{},truncated,concord::bink_stream::PlaybackMode::Once,false,0);
        check(started&&state.videoAnimation.failure().empty(),"actual helper starts for geometry-valid truncated BIK");
        state.videoFirstFramePending=true;state.videoStartupStarted=state.presentationClock->nowMs();
        check(pollMainMenuVideo(state,2000)&&!state.videoFirstFramePending&&!state.videoAnimation.failure().empty(),"real helper decoder error reaches original Main failure branch");
        check(same(state.videoFrame,state.staticFallback),"real helper decoder error preserves exact static fallback");
        check(state.videoAnimation.sequence()==0&&!state.videoAnimation.finished(),"real helper decoder error stops original session");
    }
    {
        BinkMenuAnimation player;
        check(player.start(config,{},movie,concord::bink_stream::PlaybackMode::Once,false,0),"real helper starts before early cancel");
        player.stop();player.stop();check(player.sequence()==0&&!player.finished(),"real preframe cancel and repeated stop");
    }
    report.close();std::cout<<"Main video caller boundary: checks="<<checks<<" failures="<<failures<<" logicalDeadline=60000 nativeOriginalBik=yes malformedBik=yes"<<std::endl;
    return failures?1:0;
}
} // namespace
