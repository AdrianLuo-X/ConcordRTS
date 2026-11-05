// Actual Main renderBase on its owned original DIB with bitmap font unavailable.
// Only reached by explicit CLI selector/output. No normal fallback policy change.
int runMainSystemFontFallbackSmoke(const RuntimeConfig& config,const fs::path& output) {
    if(!fs::is_directory(output))return 1;
    std::vector<MainSystemFontTraceCall> calls;
    MenuState state;
    if(!initializeMenu(state,config,true))return 1;
    state.systemFontTrace=&calls;
    state.gameFont={};
    state.mouseVisible=false;
    HDC screen=GetDC(nullptr);
    const bool ready=ensureBackBuffer(state,screen);
    if(screen!=nullptr)ReleaseDC(nullptr,screen);
    if(!ready)return 1;
    RenderSurface surface{state.backBuffer.dc(),state.composePixels.data()};
    const std::array<MenuPage,6> pages{MenuPage::Home,MenuPage::SinglePlayer,
        MenuPage::Movies,MenuPage::Credits,MenuPage::Credits,MenuPage::Options};
    unsigned scenes=0;
    for(std::size_t n=0;n<pages.size();++n){
        state.page=pages[n];state.phase=MenuPhase::Idle;state.pressed=-1;
        state.keyboardTooltip=true;state.creditsPage=n==4?creditPageCount(state)-1:0;
        state.pageStatus.clear();
        const auto current=buttons(state);
        for(int focus=-1;focus<static_cast<int>(current.size());++focus){
            state.focus=focus;state.hover=focus;state.backDirty=true;
            if(!renderBase(state,surface))return 1;
            if(!GdiFlush())return 1;
            std::string error;const auto path=output/("page-"+std::to_string(n)+"-focus-"+std::to_string(focus)+".bmp");
            if(!writeBmp(path.string(),static_cast<const unsigned char*>(state.backBuffer.pixels()),error)){std::cerr<<error<<"\n";return 1;}
            ++scenes;
        }
    }
    if(!writeMainSystemFontTrace(output/"actual-layout.bin",calls))return 1;
    std::array<unsigned,3> roles{};for(const auto& call:calls){if(call.role>=roles.size())return 1;++roles[call.role];}
    if(std::any_of(roles.begin(),roles.end(),[](unsigned count){return count==0;}))return 1;
    std::cout<<"Main system fallback actual: pages=6 scenes="<<scenes<<" calls="<<calls.size()
        <<" roleCalls="<<roles[0]<<","<<roles[1]<<","<<roles[2]
        <<" bitmapAvailable=no actualMainRenderBase=yes originalFonts=yes pixels=whole-owned-DIB skipped=0\n";
    return 0;
}
