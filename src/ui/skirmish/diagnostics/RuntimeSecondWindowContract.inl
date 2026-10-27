// P11: two visible OS windows, the production menu controller and production
// composition. OS-injected input enters each real backend's window procedure;
// no SDL_PushEvent, dummy driver, fake window or test controller is used.
#include <iomanip>
#include <cstring>
#include <tuple>
#include <thread>
#include "ui/skirmish/RuntimeSkirmishSetup.h"

namespace {
struct SecondWindowSample {
    std::string label,controller;
    int width=0,height=0;
    std::vector<unsigned char> pixels;
};
std::string secondWindowController(const MenuState& state) {
    const auto& s=state.selection;
    std::ostringstream out;
    out<<std::quoted(s.mapPath)<<' '<<s.accepted<<' '<<s.mapStartingPointLimit<<' '
       <<std::quoted(s.gameMode)<<' '<<std::quoted(s.gameModeRulesOverride)<<' '
       <<std::quoted(s.playerCountry)<<' '<<std::quoted(s.playerColor)<<' '
       <<std::quoted(s.loadingScreenAssetId)<<' '<<s.startingCredits<<' '<<s.unitCount<<' '<<s.gameSpeed<<' '
       <<s.shortGame<<s.mcvRepacks<<s.crates<<s.superweapons<<s.buildOffAlly<<s.computerAiEnabled<<s.noCooldown;
    for(const auto& text:{s.playerCountryDisplayName,s.playerCountryBrief,state.status}){
        out<<" text:";for(wchar_t c:text)out<<static_cast<std::uint32_t>(c)<<',';
    }
    for(const auto& slot:s.slots)out<<" slot:"<<slot.enabled<<slot.human<<' '<<std::quoted(slot.name)<<' '
        <<std::quoted(slot.country)<<' '<<std::quoted(slot.color)<<' '<<std::quoted(slot.team)<<' '
        <<std::quoted(slot.difficulty)<<' '<<slot.startPosition;
    out<<" ui:"<<static_cast<int>(state.page)<<' '<<state.selectedMode<<' '<<state.selectedMap<<' '
       <<state.mapScroll<<' '<<static_cast<int>(state.openDropdown)<<' '<<state.openDropdownSlot<<' '
       <<state.dropdownHover<<' '<<static_cast<int>(state.draggingSlider)<<' '
       <<static_cast<int>(state.pressed.kind)<<' '<<state.pressed.index<<' '
       <<static_cast<int>(state.hover.kind)<<' '<<state.hover.index<<' '
       <<state.mouseVisible<<' '<<state.mousePoint.x<<' '<<state.mousePoint.y<<' '
       <<state.viewLeft<<' '<<state.viewTop<<' '<<state.viewWidth<<' '<<state.viewHeight<<' '
       <<static_cast<int>(state.phase)<<' '<<state.phaseTick<<' '<<state.closing;
    for(const auto& hit:state.hits)out<<" hit:"<<hit.rect.left<<','<<hit.rect.top<<','<<hit.rect.right<<','
        <<hit.rect.bottom<<','<<static_cast<int>(hit.target.kind)<<','<<hit.target.index;
    RuntimeConfig mapped=state.config;
    applyRuntimeSkirmishSelection(mapped,s);
    out<<" mapped:"<<std::quoted(mapped.defaultMapPath)<<' '<<mapped.mapStartingPointLimit<<' '
       <<std::quoted(mapped.gameMode)<<' '<<std::quoted(mapped.gameModeRulesOverride)<<' '
       <<std::quoted(mapped.playerCountry)<<' '<<std::quoted(mapped.playerColor)<<' '
       <<std::quoted(mapped.loadingScreenAssetId)<<' '<<mapped.startingCredits<<' '<<mapped.startingUnits<<' '
       <<mapped.gameSpeed<<' '<<mapped.shortGame<<mapped.mcvRepacks<<mapped.crates<<mapped.superweapons
       <<mapped.buildOffAlly<<mapped.computerAiEnabled<<mapped.noCooldown<<' '
       <<mapped.fixedTickHz<<' '<<mapped.simulationRandomSeed<<' '<<mapped.multiplayerSetupHash<<' '
       <<mapped.multiplayerTimelineEpoch<<' '<<mapped.multiplayerActive<<' '
       <<std::quoted(mapped.skirmishLoadSavePath)<<' '<<std::quoted(mapped.campaignLoadSavePath)<<' '
       <<std::quoted(mapped.campaignSourceId);
    for(const auto& slot:mapped.playerSlots)out<<" mappedSlot:"<<slot.enabled<<slot.human<<' '
        <<std::quoted(slot.name)<<' '<<std::quoted(slot.country)<<' '<<std::quoted(slot.color)<<' '
        <<std::quoted(slot.team)<<' '<<std::quoted(slot.difficulty)<<' '<<slot.startPosition<<' '
        <<std::quoted(slot.peerId)<<' '<<slot.local;
    return out.str();
}

HWND secondWindowSdlHandle() {
    HWND result=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM pointer)->BOOL{
        wchar_t title[128]{};GetWindowTextW(window,title,128);
        if(std::wstring(title)==L"ConcordRTS - Skirmish (SDL3 software)"){
            *reinterpret_cast<HWND*>(pointer)=window;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}

std::vector<unsigned char> secondWindowReadback(HWND window,int width,int height) {
    // Read the actual client device only in this native diagnostic. The second
    // production window/presenter never receives or queries this handle/DC.
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
    HDC source=GetDC(window);
    if(!source)return {};
    HDC memory=CreateCompatibleDC(source);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=memory?CreateDIBSection(source,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    HGDIOBJ previous=bitmap?SelectObject(memory,bitmap):nullptr;
    const bool valid=bits&&previous&&previous!=HGDI_ERROR
        &&BitBlt(memory,0,0,width,height,source,0,0,SRCCOPY)&&GdiFlush();
    if(valid)std::memcpy(pixels.data(),bits,pixels.size());else pixels.clear();
    if(previous&&previous!=HGDI_ERROR)SelectObject(memory,previous);
    if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(window,source);
    return pixels;
}

struct SecondWindowVisit {
    explicit SecondWindowVisit(bool useSdl,int& checks,int& failures,const fs::path& output)
        :sdl(useSdl),events(state),checks(checks),failures(failures),output(output){}
    ~SecondWindowVisit(){if(software){software->close();state.softwareWindow=nullptr;}else if(handle&&IsWindow(handle))DestroyWindow(handle);}
    void check(bool value,const std::string& message){++checks;if(!value){++failures;std::cerr<<"P11 "<<(sdl?"SDL3 ":"Win32 ")<<message<<'\n';}}
    bool open(const RuntimeConfig& config) {
        events.trace=&observed;
        RuntimeConfig privateConfig=config;
        privateConfig.windowWidth=800;privateConfig.windowHeight=600;
        privateConfig.configPath=pathString(output/"preferences.ini");
        fs::create_directories(output);
        // The same input configuration, with a separate preference destination.
        if(fs::exists(fs::path(config.configPath)))fs::copy_file(fs::path(config.configPath),fs::path(privateConfig.configPath));
        check(initializeState(state,privateConfig,true,!sdl),"initialize actual resources");
        if(!state.initialized)return false;
        resetMenuForEntry(state,privateConfig);state.phase=ShellMenuPhase::Idle;state.phaseTick=0;
        if(sdl){
            std::string error;
            software=concord::window::createSdl3SoftwareWindow({},events,statistics,error);
            check(software!=nullptr,"create real video window: "+error);
            if(!software)return false;
            state.softwareWindow=software.get();handle=secondWindowSdlHandle();
            check(state.hwnd==nullptr&&state.titleFont==nullptr&&state.captionFont==nullptr
                &&state.normalFont==nullptr&&state.smallFont==nullptr&&!state.backBuffer.ready(),
                "bitmap path owns no original HWND/HDC/native-font bridge");
        }else handle=createShellSkirmishWindow(state,privateConfig,nullptr);
        check(handle&&IsWindow(handle),"actual native window exists");if(!handle)return false;
        DWORD process=0;check(GetWindowThreadProcessId(handle,&process)==GetCurrentThreadId()&&process==GetCurrentProcessId(),"operate only this process/UI thread's window");
        SetWindowPos(handle,HWND_TOPMOST,40,40,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
        SetForegroundWindow(handle);SetFocus(handle);pump();
        check(IsWindowVisible(handle)!=FALSE,"window is visible");
        return true;
    }
    void pump() {
        for(int i=0;i<4;++i){
            // SDL's production pump must preprocess its own native/raw input.
            // Manually dispatching those messages first loses raw motion data.
            if(!software){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){check(false,"unexpected process quit");continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }}
            if(software&&software->alive()){
                software->poll();
                if(software->alive())updateMenuTransition(state,software->nowMs());
                if(software->alive()&&software->drawable()&&(state.backDirty||events.paintPending)){
                    check(frame.paint(state,*software),"submit production full frame");events.paintPending=false;
                }
            }else if(handle&&IsWindow(handle)){InvalidateRect(handle,nullptr,FALSE);UpdateWindow(handle);}
        }
    }
    void settle(){
        const auto start=GetTickCount64();
        do{pump();if(state.phase==ShellMenuPhase::Idle||state.closing)break;Sleep(5);}
        while(GetTickCount64()-start<2000);
        check(state.phase==ShellMenuPhase::Idle||state.closing,"transition reaches its original boundary");
    }
    void message(UINT kind,WPARAM argument=0,LPARAM position=0){
        check(handle&&IsWindow(handle),"message target is live");
        if(!handle||!IsWindow(handle))return;
        if(kind==WM_MOUSEWHEEL){
            POINT point{320,160};ClientToScreen(handle,&point);SetCursorPos(point.x,point.y);pump();
            INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_WHEEL;
            input.mi.mouseData=static_cast<DWORD>(static_cast<int>(static_cast<short>(HIWORD(argument))));
            check(SendInput(1,&input,sizeof(INPUT))==1,"inject real wheel input");Sleep(5);
        }else SendMessageW(handle,kind,argument,position);
        pump();
        inputs<<kind<<' '<<static_cast<std::uint64_t>(argument)<<' '<<static_cast<std::int64_t>(position)<<" tick=0\n";
    }
    void pointer(UINT kind,int x,int y){
        POINT point{x,y};ClientToScreen(handle,&point);
        const auto move=[&](POINT position){
            const auto left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
            const auto width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
            INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
            input.mi.dx=static_cast<LONG>((2LL*(position.x-left)+1)*65536/(2LL*width));
            input.mi.dy=static_cast<LONG>((2LL*(position.y-top)+1)*65536/(2LL*height));
            check(SendInput(1,&input,sizeof(INPUT))==1,"inject real absolute mouse motion");
        };
        const auto waitForPoint=[&](int clientX,int clientY){
            const auto expected=concord::skirmish::shellClientToBase(state,{clientX,clientY});
            const auto started=GetTickCount64();bool received=false;
            do{Sleep(2);pump();received=state.mousePoint.x==expected.x&&state.mousePoint.y==expected.y
                &&state.mouseVisible==(expected.x>=0);}
            while((!received||GetTickCount64()-started<20)&&GetTickCount64()-started<500);
            check(received,"real pointer event reached controller before next injected action");
        };
        if(kind==WM_MOUSEMOVE){
            // A request to set an already identical position is coalesced by
            // SDL. Exercise an actual one-pixel motion in both OS windows.
            move({point.x+1,point.y});waitForPoint(x+1,y);
        }
        move(point);waitForPoint(x,y);
        if(kind!=WM_MOUSEMOVE){
            // These actions are single clicks. Do not turn the later same-pixel
            // slider gesture into an unintended OS double click in a fast test.
            if(kind==WM_LBUTTONDOWN)Sleep(GetDoubleClickTime()+5);
            INPUT input{};input.type=INPUT_MOUSE;
            input.mi.dwFlags=kind==WM_LBUTTONDOWN?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;
            check(SendInput(1,&input,sizeof(INPUT))==1,"inject real mouse button");Sleep(5);pump();
        }
        inputs<<"pointer "<<kind<<' '<<x<<' '<<y<<" menuTick=none\n";
    }
    void key(int key,bool repeat=false){
        INPUT input{};input.type=INPUT_KEYBOARD;
        input.ki.wScan=static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC));
        input.ki.dwFlags=KEYEVENTF_SCANCODE;
        if(key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END)input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
        check(SendInput(1,&input,sizeof(INPUT))==1,"inject real key down");pump();
        if(repeat){check(SendInput(1,&input,sizeof(INPUT))==1,"inject held-key repeat");pump();}
        input.ki.dwFlags|=KEYEVENTF_KEYUP;check(SendInput(1,&input,sizeof(INPUT))==1,"inject real key up");pump();
        inputs<<"key "<<key<<' '<<repeat<<" menuTick=none\n";
        settle();
    }
    POINT target(HitKind kind,int index=-1){
        pump();const auto hit=std::find_if(state.hits.begin(),state.hits.end(),[&](const HitRegion& h){return h.target.kind==kind&&(index<0||h.target.index==index);});
        check(hit!=state.hits.end(),"production hit exists "+std::to_string(static_cast<int>(kind)));
        if(hit==state.hits.end())return {-1,-1};
        return {state.viewLeft+(hit->rect.left+hit->rect.right)/2*state.viewWidth/kBaseWidth,
            state.viewTop+(hit->rect.top+hit->rect.bottom)/2*state.viewHeight/kBaseHeight};
    }
    void click(HitKind kind,int index=-1){const POINT p=target(kind,index);if(p.x<0||p.y<0)return;pointer(WM_MOUSEMOVE,p.x,p.y);pointer(WM_LBUTTONDOWN,p.x,p.y);pointer(WM_LBUTTONUP,p.x,p.y);settle();}
    void resize(int width,int height){
        if(software)check(software->resize(width,height),"SDL API resize");
        else {RECT rect{0,0,width,height};AdjustWindowRectEx(&rect,static_cast<DWORD>(GetWindowLongPtrW(handle,GWL_STYLE)),FALSE,0);
            check(SetWindowPos(handle,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER)!=FALSE,"Win32 resize");}
        pump();RECT client{};GetClientRect(handle,&client);
        check(client.right==width&&client.bottom==height,"actual pixel client size");
        pointer(WM_MOUSEMOVE,-20,-20);message(WM_MOUSELEAVE);pump();
    }
    void sample(const std::string& label){
        settle();
        // Observe a settled frame after the same explicit real mouse motion,
        // rather than comparing two OS queues at different coalescing points.
        POINT current{};GetCursorPos(&current);ScreenToClient(handle,&current);
        pointer(WM_MOUSEMOVE,current.x,current.y);pump();
        RECT client{};GetClientRect(handle,&client);
        auto pixels=secondWindowReadback(handle,client.right,client.bottom);
        check(!pixels.empty(),"read actual displayed full client: "+label);
        if(pixels.empty())return;
        if(software){
            const auto source=frame.pixels();check(source.valid()&&source.width==client.right&&source.height==client.bottom,"complete source dimensions: "+label);
            std::size_t different=0;
            if(source.valid())for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x)for(int c=0;c<3;++c)
                different+=source.bytes[static_cast<std::size_t>(y)*source.rowStride+static_cast<std::size_t>(x)*4+c]!=pixels[(static_cast<std::size_t>(y)*source.width+x)*4+c];
            check(different==0,"SDL native surface displays every source BGR byte: "+label+" difference="+std::to_string(different));
        }
        std::string error;check(writeBgraBmp(pathString(output/(label+".bmp")),pixels.data(),client.right,client.bottom,error),"save displayed frame");
        const auto controller=secondWindowController(state);
        std::ofstream text(output/(label+".txt"),std::ios::binary);text<<controller;check(text.good(),"save complete controller/setup observation");
        samples.push_back({label,controller,client.right,client.bottom,std::move(pixels)});
    }
    void script(bool accept){
        resize(800,600);sample("01-lobby");
        click(HitKind::ChooseMap);check(state.page==SkirmishPage::MapSelection,"click enters actual map selection");sample("02-maps");
        key(VK_DOWN);key(VK_DOWN,true);sample("03-key-repeat");
        const auto beforeFractional=state.mapScroll;
        message(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
        message(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
        check(state.mapScroll==beforeFractional,"fractional wheel messages retain per-message truncation");
        message(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);sample("04-wheel");
        key(VK_RETURN);check(state.page==SkirmishPage::Lobby,"Enter confirms selected map");sample("05-map-confirm");
        click(HitKind::SlotCountry,0);check(state.openDropdown==DropdownKind::Country,"real country dropdown");sample("06-dropdown");
        click(HitKind::DropdownOption,1);sample("07-seat");
        resize(1280,720);sample("08-letterbox");
        const POINT slider=target(HitKind::SpeedSlider);
        pointer(WM_LBUTTONDOWN,slider.x,slider.y);
        check(state.draggingSlider==HitKind::SpeedSlider&&GetCapture()==handle,"real slider capture");
        pointer(WM_MOUSEMOVE,slider.x+30,slider.y);pointer(WM_LBUTTONUP,-40,-40);
        check(state.draggingSlider==HitKind::None&&state.pressed.kind==HitKind::None&&GetCapture()!=handle,"outside release preserves synchronous cancellation ordering");
        sample("09-outside-release");
        const POINT second=target(HitKind::SpeedSlider);pointer(WM_LBUTTONDOWN,second.x,second.y);
        check(GetCapture()==handle,"capture before native transfer");
        HWND other=CreateWindowExW(0,L"STATIC",L"P11 owned capture peer",WS_OVERLAPPEDWINDOW,80,80,80,60,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(other!=nullptr,"create owned focus/capture peer");
        if(other){ShowWindow(other,SW_SHOW);SetCapture(other);SetForegroundWindow(other);SetFocus(other);pump();
            check(state.draggingSlider==HitKind::None&&state.pressed.kind==HitKind::None,"native focus/capture loss clears the original controller gesture");
            if(GetCapture()==other)ReleaseCapture();
            INPUT release{};release.type=INPUT_MOUSE;release.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&release,sizeof(INPUT));
            DestroyWindow(other);SetForegroundWindow(handle);SetFocus(handle);pump();}
        pointer(WM_MOUSEMOVE,-20,-20);message(WM_MOUSELEAVE);sample("10-focus-return");
        if(software)check(software->minimize(),"SDL minimize");else ShowWindow(handle,SW_MINIMIZE);
        pump();check(IsIconic(handle)!=FALSE,"actual minimized OS window");
        const auto successful=statistics.successful;
        if(software){concord::presentation::SoftwareFrameView invalid;check(software->presenter().present(invalid)==concord::presentation::FramePresentResult::InvalidFrame,"invalid full-frame rejected");check(statistics.successful==successful,"minimized/invalid is not successful presentation");}
        if(software)check(software->restore(),"SDL restore");else ShowWindow(handle,SW_RESTORE);
        pump();check(IsIconic(handle)==FALSE,"actual restored OS window");pointer(WM_MOUSEMOVE,-20,-20);message(WM_MOUSELEAVE);sample("11-restored");
        resize(640,480);sample("12-fit");
        resize(799,599);sample("12b-near-identity");
        resize(798,599);sample("12c-near-fit");resize(800,600);
        click(HitKind::ChooseMap);key(VK_ESCAPE);check(state.page==SkirmishPage::Lobby,"Escape cancels map selection");sample("13-map-cancel");
        pointer(WM_MOUSEMOVE,500,300);sample("14-software-cursor");
        const auto authority=std::tuple(state.config.fixedTickHz,state.config.simulationRandomSeed,state.config.multiplayerSetupHash,state.config.multiplayerTimelineEpoch);
        if(accept)key(VK_RETURN);else {message(WM_CLOSE);settle();}
        check(state.selection.accepted==accept&&state.closing,"real accept/close transition completes");
        check(!IsWindow(handle)&&(!software||!software->alive()),"native window actually destroyed");
        check(authority==std::tuple(state.config.fixedTickHz,state.config.simulationRandomSeed,state.config.multiplayerSetupHash,state.config.multiplayerTimelineEpoch),"no authority/tick/RNG change from menu events");
        samples.push_back({"15-closed",secondWindowController(state),0,0,{}});
        if(software){check(statistics.successful>0&&statistics.attempted>=statistics.successful,"real S38 presentation counters");check(state.softwareFailure.empty(),"capture/focus API errors are not swallowed");}
        std::ofstream trace(output/"input.txt");trace<<inputs.str();
        std::ofstream nativeEvents(output/"adapter-events.txt");
        for(const auto& event:observed)nativeEvents<<static_cast<int>(event.kind)<<' '<<event.point.x<<' '<<event.point.y<<' '
            <<static_cast<int>(event.key)<<' '<<event.repeat<<' '<<event.notches<<'\n';
    }
    bool sdl;
    MenuState state;
    SoftwareSkirmishEvents events;
    concord::presentation::FramePresentStatistics statistics;
    std::unique_ptr<concord::window::SoftwareWindow> software;
    SkirmishSoftwareFrame frame;
    HWND handle=nullptr;
    int& checks;int& failures;
    fs::path output;
    std::ostringstream inputs;
    std::vector<SecondWindowSample> samples;
    std::vector<concord::window::SoftwareWindowEvent> observed;
};

void secondWindowBackendContract(int& checks,int& failures,const fs::path& output) {
    const auto check=[&](bool valid,const std::string& message){++checks;if(!valid){++failures;std::cerr<<"P11 SDL backend "<<message<<'\n';}};
    struct Events final : concord::window::SoftwareWindowEvents {
        void windowEvent(const concord::window::SoftwareWindowEvent&) override{}
    } events;
    concord::presentation::FramePresentStatistics statistics;
    concord::window::SoftwareWindowOptions options;options.width=97;options.height=61;
    std::string error;
    auto window=concord::window::createSdl3SoftwareWindow(options,events,statistics,error);
    check(window!=nullptr,"create real surface: "+error);if(!window)return;
    HWND native=secondWindowSdlHandle();check(native&&IsWindowVisible(native),"real visible pattern window");
    if(!native)return;
    SetWindowPos(native,HWND_TOPMOST,30,30,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
    for(int i=0;i<5;++i){window->poll();Sleep(2);}
    std::int32_t width=0,height=0;check(window->size(width,height)&&width==97&&height==61,"actual pattern client size");
    const std::size_t stride=static_cast<std::size_t>(width)*4+13;
    std::vector<unsigned char> source(static_cast<std::size_t>(height-1)*stride+static_cast<std::size_t>(width)*4,0xE7);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto at=static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*4;
        source[at]=static_cast<unsigned char>(x*13);source[at+1]=static_cast<unsigned char>(y*9);
        source[at+2]=static_cast<unsigned char>((x+y)*7);source[at+3]=static_cast<unsigned char>(x*17+y*3);}
    const auto untouched=source;
    concord::presentation::SoftwareFrameView frame{{source,width,height,stride,1},1,1,0,concord::presentation::SoftwareFrameContent::Redrawn};
    using Result=concord::presentation::FramePresentResult;
    check(window->presenter().present(frame)==Result::Presented,"padded complete frame with all alpha values");
    const auto pixels=secondWindowReadback(native,width,height);
    std::size_t differences=0;
    if(pixels.size()!=static_cast<std::size_t>(width)*height*4)differences=1;
    else for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<3;++c)
        differences+=pixels[(static_cast<std::size_t>(y)*width+x)*4+c]!=source[static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*4+c];
    check(differences==0,"every actual BGR byte exact, fourth byte ignored without blending");
    check(source==untouched,"source alpha/padding/capacity unchanged");
    check(window->presenter().present(frame)==Result::Rejected&&statistics.attempted==1&&statistics.successful==1,"duplicate rejected without counting success");
    concord::presentation::SoftwareFrameView invalid;
    check(window->presenter().present(invalid)==Result::InvalidFrame&&statistics.attempted==1,"invalid frame rejected before attempt");
    frame.sequence=2;Result foreign=Result::Presented;
    std::thread other([&]{foreign=window->presenter().present(frame);});other.join();
    check(foreign==Result::Rejected&&statistics.attempted==1,"foreign thread rejected before SDL or counter mutation");
    --frame.pixels.width;
    check(window->presenter().present(frame)==Result::Failed&&statistics.attempted==2&&statistics.successful==1,"mismatched extent fails without successful submission");
    check(secondWindowReadback(native,width,height)==pixels,"failed extent leaves displayed frame intact");
    ++frame.pixels.width;frame.sequence=3;
    check(window->presenter().present(frame)==Result::Presented&&statistics.successful==2,"retry uses a fresh sequence");
    auto concurrent=concord::window::createSdl3SoftwareWindow(options,events,statistics,error);
    check(!concurrent&&!error.empty()&&window->alive(),"second video owner rejected without disturbing the live provider");
    check(window->minimize(),"real minimize request");for(int i=0;i<8;++i){window->poll();Sleep(2);}
    check(IsIconic(native)!=FALSE&&!window->drawable(),"actual OS minimized state");frame.sequence=4;
    check(window->presenter().present(frame)==Result::Failed&&statistics.successful==2,"minimized update not counted as display");
    check(window->restore(),"real restore request");for(int i=0;i<8;++i){window->poll();Sleep(2);}
    check(!IsIconic(native)&&window->drawable(),"actual restored state");frame.sequence=5;
    check(window->presenter().present(frame)==Result::Presented&&statistics.successful==3,"restored surface reacquired and presented");
    window->close();check(!IsWindow(native)&&!window->alive(),"native pattern window actually destroyed");
    window.reset();check(GetModuleHandleW(L"SDL3.dll")==nullptr,"exclusive SDL session quit and DLL lease released");
    std::string imageError;
    check(writeBgraBmp(pathString(output/"backend-pattern.bmp"),pixels.data(),width,height,imageError),"save actual backend readback");
    std::cout<<"P11 SDL padded pattern: BGR bytes="<<static_cast<std::uint64_t>(width)*height*3<<" differences="<<differences<<" sourceUnchanged="<<(source==untouched)<<'\n';
}
} // namespace

int runSecondWindowContract(const RuntimeConfig& config,const fs::path& output) {
    int checks=0,failures=0;
    const auto check=[&](bool valid,const std::string& message){++checks;if(!valid){++failures;std::cerr<<"P11 "<<message<<'\n';}};
    if(fs::exists(output)){std::cerr<<"P11 output already exists; preserve previous attempts\n";return 1;}
    fs::create_directories(output);
    // Preserve the user's pointer after exercising only this process's windows.
    struct CursorRestore{POINT point{};CursorRestore(){GetCursorPos(&point);SetCursorPos(GetSystemMetrics(SM_CXSCREEN)-2,GetSystemMetrics(SM_CYSCREEN)-2);}~CursorRestore(){SetCursorPos(point.x,point.y);}} cursor;
    for(const auto drawing:{concord::window::SoftwareWindowDrawing::NativeSystemFont,concord::window::SoftwareWindowDrawing::D3D,concord::window::SoftwareWindowDrawing::SdlRenderer}){
        concord::window::SoftwareWindowOptions options;options.drawing=drawing;
        check(!concord::window::softwareWindowUnsupported(options).empty(),"unsupported window/drawing combination has creation-time reason");
    }
    // Initialize the same process-owned USER/theme/font/DLL dependencies before
    // the strict per-visit resource balance. All warm windows are also disposed.
    secondWindowBackendContract(checks,failures,output);
    for(bool sdl:{false,true}){
        SecondWindowVisit warm(sdl,checks,failures,output/(sdl?"warm-sdl3":"warm-win32"));
        if(warm.open(config)){
            if(warm.software){warm.software->minimize();warm.pump();warm.software->restore();}
            else {ShowWindow(warm.handle,SW_MINIMIZE);ShowWindow(warm.handle,SW_RESTORE);}
            warm.pump();
        }
    }
    const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const DWORD user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(bool accepted:{false,true}){
        std::vector<SecondWindowSample> reference;
        std::string referenceInput;
        {SecondWindowVisit visit(false,checks,failures,output/(accepted?"win32-accept":"win32-cancel"));
            if(visit.open(config)){visit.script(accepted);reference=std::move(visit.samples);referenceInput=visit.inputs.str();}}
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"Win32 visit strict GDI resource balance");
        check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"Win32 visit strict USER resource balance");
        {SecondWindowVisit visit(true,checks,failures,output/(accepted?"sdl3-accept":"sdl3-cancel"));
            if(visit.open(config)){
                visit.script(accepted);check(referenceInput==visit.inputs.str(),"identical native input messages and menu command tick=0");
                check(reference.size()==visit.samples.size(),"same number of full scene observations");
                for(std::size_t i=0;i<std::min(reference.size(),visit.samples.size());++i){
                    const auto& a=reference[i];const auto& b=visit.samples[i];
                    check(a.label==b.label&&a.width==b.width&&a.height==b.height,"same actual frame extent "+a.label);
                    check(a.controller==b.controller,"all controller/selection/setup fields identical "+a.label);
                    std::size_t different=0;
                    if(a.pixels.size()!=b.pixels.size())different=1;
                    else for(std::size_t j=0;j<a.pixels.size();++j)if(j%4!=3)different+=a.pixels[j]!=b.pixels[j];
                    check(different==0,"complete displayed BGR identical "+a.label+" differences="+std::to_string(different));
                }
            }}
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"SDL visit strict GDI resource balance");
        check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"SDL visit strict USER resource balance");
        check(GetModuleHandleW(L"SDL3.dll")==nullptr,"SDL visit releases the private DLL session");
    }
    std::ofstream summary(output/"result.json");
    summary<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"realWindows\":true,\"dummy\":false,\"menuAuthorityTicks\":0,\"scope\":\"production skirmish bitmap scene\"}\n";
    std::cout<<"P11 second-window contract: checks="<<checks<<" failures="<<failures<<" actualWin32AndSdl3=yes menuAuthorityTicks=0\n";
    return failures?1:0;
}
