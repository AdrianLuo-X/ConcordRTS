// Private assembly of the existing controller/resources and a software window.
// The chosen bitmap path never creates a Win32 font, DC or hidden HWND.
class SoftwareSkirmishEvents final : public concord::window::SoftwareWindowEvents {
public:
    explicit SoftwareSkirmishEvents(MenuState& state):state_(state){}
    void windowEvent(const concord::window::SoftwareWindowEvent& event) override {
        if(trace)trace->push_back(event);
        using Kind=concord::window::SoftwareWindowEventKind;
        switch(event.kind){
        case Kind::Resize:computeShellViewport(state_,event.width,event.height);state_.backDirty=true;paintPending=true;break;
        case Kind::Expose:case Kind::Restore:paintPending=true;break;
        case Kind::Minimize:break;
        case Kind::Move:handleSkirmishMouseMove(state_,event.point);break;
        case Kind::LeftDown:handleSkirmishLeftDown(state_,event.point);break;
        case Kind::LeftUp:handleSkirmishLeftUp(state_,event.point);break;
        case Kind::DoubleClick:handleSkirmishDoubleClick(state_,event.point);break;
        case Kind::RightUp:handleSkirmishRightUp(state_,event.point);break;
        case Kind::Leave:handleSkirmishMouseLeave(state_);break;
        case Kind::CaptureLost:handleSkirmishCaptureLost(state_);break;
        case Kind::FocusLost:break; // CaptureLost already ran, if this window owned capture.
        case Kind::Wheel:handleSkirmishWheel(state_,event.notches);break;
        case Kind::KeyDown:{
            using Key=concord::window::SoftwareWindowKey;
            RuntimeSkirmishKey key=RuntimeSkirmishKey::Other;
            switch(event.key){
            case Key::Escape:key=RuntimeSkirmishKey::Escape;break;
            case Key::Return:key=RuntimeSkirmishKey::Return;break;
            case Key::Space:key=RuntimeSkirmishKey::Space;break;
            case Key::Up:key=RuntimeSkirmishKey::Up;break;
            case Key::Down:key=RuntimeSkirmishKey::Down;break;
            case Key::Home:key=RuntimeSkirmishKey::Home;break;
            case Key::End:key=RuntimeSkirmishKey::End;break;
            default:break;
            }
            handleSkirmishKey(state_,key);break;
        }
        case Kind::Close:completeMenu(state_,false);break;
        }
    }
    bool paintPending=true;
    std::vector<concord::window::SoftwareWindowEvent>* trace=nullptr;
private:
    MenuState& state_;
};

// Preserve the reference client's COLORONCOLOR sample positions. This is the
// fixed 800x600 shell's final client scaling, not the existing preview painter.
// The Win32 near-identity (799x599) shortcut crops one edge pixel. A separate
// native index-grid probe records that case and the center-sampled ratios.
void scaleSkirmishClientFrame(concord::ui::MenuPixelSurfaceView destination,
    concord::ui::ConstMenuPixelSurfaceView source,const RuntimeInputPixelRect& viewport) {
    const std::int64_t width=viewport.right-viewport.left,height=viewport.bottom-viewport.top;
    if(!destination.valid()||!source.valid()||width<=0||height<=0)return;
    const bool nearIdentity=width==source.width-1&&height==source.height-1;
    for(std::int64_t y=0;y<height;++y){
        const auto dy=viewport.top+y;if(dy<0||dy>=destination.height)continue;
        const auto sy=nearIdentity?y:(2*y+1)*source.height/(2*height);
        for(std::int64_t x=0;x<width;++x){
            const auto dx=viewport.left+x;if(dx<0||dx>=destination.width)continue;
            const auto sx=nearIdentity?x:(2*x+1)*source.width/(2*width);
            const auto from=static_cast<std::size_t>(sy)*source.rowStride+static_cast<std::size_t>(sx)*4;
            const auto to=static_cast<std::size_t>(dy)*destination.rowStride+static_cast<std::size_t>(dx)*4;
            std::copy_n(source.bytes.data()+from,4,destination.bytes.data()+to);
            destination.bytes[to+3]=255;
        }
    }
}
class SkirmishSoftwareFrame {
public:
    bool paint(MenuState& state,concord::window::SoftwareWindow& window) {
        std::int32_t width=0,height=0;
        if(!window.size(width,height)||width<=0||height<=0)return false;
        computeShellViewport(state,width,height);
        if(!state.composeFrame.view().valid()&&!state.composeFrame.reset(kBaseWidth,kBaseHeight))return false;
        const bool redrawn=state.backDirty||version_==0;
        if(redrawn){
            if(!composeShellPixels(state,state.composeFrame.view()))return false;
            state.backDirty=false;++version_;
        }
        if((!client_.view().valid()||client_.view().width!=width||client_.view().height!=height)
            &&!client_.reset(width,height))return false;
        auto destination=client_.view();
        concord::ui::clearMenuPixels(destination);
        scaleSkirmishClientFrame(destination,std::as_const(state.composeFrame).view(),
            {state.viewLeft,state.viewTop,state.viewLeft+state.viewWidth,state.viewTop+state.viewHeight});
        concord::presentation::SoftwareFrameView frame{std::as_const(client_).view(),++sequence_,version_,0,
            redrawn?concord::presentation::SoftwareFrameContent::Redrawn:concord::presentation::SoftwareFrameContent::ReusedScene};
        return window.presenter().present(frame)==concord::presentation::FramePresentResult::Presented;
    }
    concord::ui::ConstMenuPixelSurfaceView pixels() const noexcept{return client_.view();}
private:
    concord::ui::MenuPixelBuffer client_;
    std::uint64_t sequence_=0,version_=0;
};

RuntimeSkirmishSelection runSdlSkirmishMenu(const RuntimeConfig& config,std::string& error) {
    MenuState state;
    // Resource decoders/cache/font bytes are the original production path.
    // Skip the unrelated native-font fallback owners for this bitmap scene.
    if(!initializeState(state,config,false,false)){error="Skirmish bitmap resources are unavailable";return {};}
    SoftwareSkirmishEvents events(state);
    resetMenuForEntry(state,config);
    concord::presentation::FramePresentStatistics statistics;
    concord::window::SoftwareWindowOptions options;
    options.width=std::max(kBaseWidth,config.windowWidth);
    options.height=std::max(kBaseHeight,config.windowHeight);
    auto window=concord::window::createSdl3SoftwareWindow(options,events,statistics,error);
    if(!window)return {};
    state.softwareWindow=window.get();
    state.phase=ShellMenuPhase::SlideIn;state.phaseStarted=window->nowMs();state.phaseTick=0;
    state.backDirty=true;
    if(!window->focus())std::cerr<<"Skirmish window activation: "<<window->error()<<'\n';
    state.audio.startMusic(state.exportRoot/"thememd"/"drok.wav");state.audio.play("MenuSlideIn");
    SkirmishSoftwareFrame frame;
    while(window->alive()){
        window->poll();
        if(!window->alive())break;
        updateMenuTransition(state,window->nowMs());
        if(!window->alive())break;
        if(window->drawable()&&(state.backDirty||events.paintPending)){
            if(!frame.paint(state,*window)){error="The skirmish software frame could not be displayed";break;}
            events.paintPending=false;
        }
        if(!state.softwareFailure.empty()){error=state.softwareFailure;break;}
        window->wait(16);
    }
    state.audio.stopMusic();window->close();state.softwareWindow=nullptr;
    syncSelection(state);
    return error.empty()?state.selection:RuntimeSkirmishSelection{};
}
