#include "runtime/window/RuntimeSoftwareWindow.h"
#if defined(CONCORD_HAS_SDL3)
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateWindow
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace concord::window {
namespace {
// The DLL is optional for the reference window. Load only the adjacent verified
// package, never PATH/current-directory libraries, and never SDL's native HWND.
#define CONCORD_SDL_FUNCTIONS(X) \
 X(SetMainReady) X(GetVersion) X(SetHint) X(InitSubSystem) X(Quit) \
 X(CreateWindow) X(DestroyWindow) X(GetWindowID) X(GetWindowSize) \
 X(GetWindowSizeInPixels) X(GetWindowSurface) X(UpdateWindowSurface) \
 X(ConvertPixels) X(LockSurface) X(UnlockSurface) X(GetError) X(PollEvent) \
 X(WaitEventTimeout) X(GetTicks) X(RaiseWindow) X(CaptureMouse) \
 X(GetWindowFlags) X(HideCursor) X(ShowCursor) X(GetCurrentVideoDriver) \
 X(SetWindowSize) X(MinimizeWindow) X(RestoreWindow)
struct SdlApi {
    HMODULE module=nullptr;
#define DECLARE_SDL(name) decltype(&SDL_##name) name=nullptr;
    CONCORD_SDL_FUNCTIONS(DECLARE_SDL)
#undef DECLARE_SDL
    ~SdlApi(){if(module)FreeLibrary(module);}
    bool open(std::string& error) {
        std::vector<wchar_t> file(32768);
        const DWORD count=GetModuleFileNameW(nullptr,file.data(),static_cast<DWORD>(file.size()));
        if(!count||count>=file.size()){error="Cannot locate the executable directory for SDL3.dll";return false;}
        std::wstring name(file.data(),count);
        const auto slash=name.find_last_of(L"\\/");
        if(slash==std::wstring::npos){error="Executable directory is unavailable";return false;}
        name.resize(slash+1);name+=L"SDL3.dll";
        module=LoadLibraryExW(name.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module){error="Optional adjacent SDL3.dll is unavailable (Windows error "+std::to_string(GetLastError())+")";return false;}
#define LOAD_SDL(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(module,"SDL_" #name)); if(!name){error="SDL3.dll lacks SDL_" #name;return false;}
        CONCORD_SDL_FUNCTIONS(LOAD_SDL)
#undef LOAD_SDL
        if(GetVersion()!=SDL_VERSION){error="SDL3.dll version differs from the locked 3.4.16 headers";return false;}
        return true;
    }
};
static_assert(SDL_MAJOR_VERSION==3&&SDL_MINOR_VERSION==4&&SDL_MICRO_VERSION==16);
SoftwareWindowKey windowKey(SDL_Keycode key) noexcept {
    switch(key){
    case SDLK_ESCAPE:return SoftwareWindowKey::Escape;
    case SDLK_RETURN:return SoftwareWindowKey::Return;
    case SDLK_SPACE:return SoftwareWindowKey::Space;
    case SDLK_UP:return SoftwareWindowKey::Up;
    case SDLK_DOWN:return SoftwareWindowKey::Down;
    case SDLK_HOME:return SoftwareWindowKey::Home;
    case SDLK_END:return SoftwareWindowKey::End;
    default:return SoftwareWindowKey::Other;
    }
}
std::atomic<bool> sdlSessionInUse{false};
class SdlWindow final : public SoftwareWindow, public presentation::FramePresenter {
public:
    SdlWindow(SoftwareWindowEvents& events,presentation::FramePresentStatistics& statistics)
        :FramePresenter(statistics),events_(events),owner_(std::this_thread::get_id()){}
    ~SdlWindow() override {
        close();
        if(initialized_)api_.ShowCursor();
        if(sessionClaimed_&&api_.Quit)api_.Quit();
        if(sessionClaimed_)sdlSessionInUse.store(false);
    }
    bool initialize(const SoftwareWindowOptions& options) {
        if(sdlSessionInUse.exchange(true)){error_="The SDL3 skirmish video session already has an owner";return false;}
        sessionClaimed_=true;
        if(!api_.open(error_))return false;
        api_.SetMainReady();
        if(!api_.SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION,"0")
            ||!api_.SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE,"0"))return fail("Cannot configure SDL3 software/capture policy");
        if(!api_.InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS))return fail("SDL3 video initialization failed");
        initialized_=true;
        const char* driver=api_.GetCurrentVideoDriver();
        if(!driver||std::strcmp(driver,"windows")!=0){error_="P11 requires the real Windows SDL video driver; dummy/offscreen are not acceptance providers";return false;}
        window_=api_.CreateWindow("ConcordRTS - Skirmish (SDL3 software)",options.width,options.height,
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if(!window_)return fail("SDL3 window creation failed");
        id_=api_.GetWindowID(window_);
        if(!api_.GetWindowSurface(window_))return fail("SDL3 software surface unavailable");
        if(!api_.HideCursor())return fail("SDL3 software cursor policy unavailable");
        // Start outside until SDL supplies its queued native focus/motion. A
        // stationary pointer outside a newly created window emits no motion.
        emit(SoftwareWindowEventKind::Leave);
        return true;
    }
    presentation::FramePresenter& presenter() noexcept override{return *this;}
    bool poll() override {
        if(!onOwningThread()||!window_)return false;
        SDL_Event event{};
        // Drain before the menu's transition clock. No simulation tick is
        // advanced here, including during modal OS resize/drag operations.
        while(window_&&api_.PollEvent(&event))dispatch(event);
        if(window_&&captured_&&!(api_.GetWindowFlags(window_)&SDL_WINDOW_MOUSE_CAPTURE))loseCapture();
        return window_!=nullptr;
    }
    void wait(std::uint32_t milliseconds) override {
        if(!onOwningThread()||!window_)return;
        SDL_Event event{};
        if(api_.WaitEventTimeout(&event,static_cast<Sint32>(std::min(milliseconds,1000u))))dispatch(event);
    }
    std::uint64_t nowMs() const noexcept override{return api_.GetTicks?api_.GetTicks():0;}
    bool focus() override{return window_&&onOwningThread()&&(api_.RaiseWindow(window_)||fail("SDL3 focus request failed"));}
    bool capture() override {
        if(!window_||!onOwningThread())return false;
        if(!api_.CaptureMouse(true))return fail("SDL3 mouse capture failed");
        captured_=true;return true;
    }
    void releaseCapture() override {
        if(!captured_||!window_||!onOwningThread())return;
        if(!api_.CaptureMouse(false)){fail("SDL3 mouse capture release failed");return;}
        // Win32 ReleaseCapture synchronously calls the controller back. Preserve
        // that ordering; a later queued OS focus event must not repeat it.
        loseCapture();
    }
    bool captured() const noexcept override{return captured_;}
    bool alive() const noexcept override{return window_!=nullptr;}
    bool drawable() const noexcept override{return window_&&onOwningThread()
        &&!(api_.GetWindowFlags(window_)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN));}
    void close() override {
        if(!window_||!onOwningThread())return;
        releaseCapture();
        api_.DestroyWindow(window_);window_=nullptr;id_=0;
    }
    bool size(std::int32_t& w,std::int32_t& h) const noexcept override {
        int width=0,height=0;
        if(!window_||!onOwningThread()||!api_.GetWindowSizeInPixels(window_,&width,&height))return false;
        w=width;h=height;return true;
    }
    bool resize(std::int32_t w,std::int32_t h) override {
        return window_&&onOwningThread()&&w>0&&h>0&&(api_.SetWindowSize(window_,w,h)||fail("SDL3 resize failed"));
    }
    bool minimize() override{return window_&&onOwningThread()&&(api_.MinimizeWindow(window_)||fail("SDL3 minimize failed"));}
    bool restore() override{return window_&&onOwningThread()&&(api_.RestoreWindow(window_)||fail("SDL3 restore failed"));}
    const std::string& error() const noexcept override{return error_;}
private:
    bool onOwningThread() const noexcept override{return owner_==std::this_thread::get_id();}
    bool fail(const char* reason){error_=std::string(reason)+": "+api_.GetError();return false;}
    bool submitCompleteFrame(const presentation::SoftwareFrameView& frame) noexcept override {
        if(!window_||(api_.GetWindowFlags(window_)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN)))return false;
        // SDL invalidates the borrowed surface on every resize; never cache it.
        SDL_Surface* surface=api_.GetWindowSurface(window_);
        const auto& pixels=frame.pixels;
        if(!surface||surface->w!=pixels.width||surface->h!=pixels.height
            ||pixels.rowStride>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        if(!api_.LockSurface(surface))return false;
        // BGRX ignores the carried fourth byte (including zero), as BI_RGB did.
        // No blend/premultiply and no assumption about the native surface pitch.
        const bool converted=api_.ConvertPixels(pixels.width,pixels.height,SDL_PIXELFORMAT_XRGB8888,
            pixels.bytes.data(),static_cast<int>(pixels.rowStride),surface->format,surface->pixels,surface->pitch);
        api_.UnlockSurface(surface);
        return converted&&api_.UpdateWindowSurface(window_);
    }
    void emit(SoftwareWindowEventKind kind){SoftwareWindowEvent e;e.kind=kind;events_.windowEvent(e);}
    void loseCapture(){if(captured_){captured_=false;emit(SoftwareWindowEventKind::CaptureLost);}}
    RuntimeInputPixelPoint pixelPoint(float x,float y) const noexcept {
        int w=0,h=0,pw=0,ph=0;
        if(!api_.GetWindowSize(window_,&w,&h)||!api_.GetWindowSizeInPixels(window_,&pw,&ph)||w<=0||h<=0)return {-1,-1};
        const auto convert=[](double v){return static_cast<std::int32_t>(std::clamp(std::floor(v),
            static_cast<double>(std::numeric_limits<std::int32_t>::min()),static_cast<double>(std::numeric_limits<std::int32_t>::max())));};
        return {convert(static_cast<double>(x)*pw/w),convert(static_cast<double>(y)*ph/h)};
    }
    void dispatch(const SDL_Event& source) {
        if(source.type==SDL_EVENT_QUIT){emit(SoftwareWindowEventKind::Close);return;}
        // All handled window/input event records have their windowID here.
        if(source.window.windowID!=id_)return;
        SoftwareWindowEvent event;
        switch(source.type){
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            event.kind=SoftwareWindowEventKind::Resize;size(event.width,event.height);break;
        case SDL_EVENT_WINDOW_EXPOSED:event.kind=SoftwareWindowEventKind::Expose;break;
        case SDL_EVENT_WINDOW_MINIMIZED:event.kind=SoftwareWindowEventKind::Minimize;break;
        case SDL_EVENT_WINDOW_RESTORED:event.kind=SoftwareWindowEventKind::Restore;break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:loseCapture();event.kind=SoftwareWindowEventKind::FocusLost;break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:event.kind=SoftwareWindowEventKind::Leave;break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:event.kind=SoftwareWindowEventKind::Close;break;
        case SDL_EVENT_MOUSE_MOTION:event.kind=SoftwareWindowEventKind::Move;event.point=pixelPoint(source.motion.x,source.motion.y);break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if(source.button.button!=SDL_BUTTON_LEFT)return;
            event.kind=source.button.clicks>0&&source.button.clicks%2==0?SoftwareWindowEventKind::DoubleClick:SoftwareWindowEventKind::LeftDown;
            event.point=pixelPoint(source.button.x,source.button.y);break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if(source.button.button!=SDL_BUTTON_LEFT&&source.button.button!=SDL_BUTTON_RIGHT)return;
            event.kind=source.button.button==SDL_BUTTON_LEFT?SoftwareWindowEventKind::LeftUp:SoftwareWindowEventKind::RightUp;
            event.point=pixelPoint(source.button.x,source.button.y);break;
        case SDL_EVENT_MOUSE_WHEEL:
            event.kind=SoftwareWindowEventKind::Wheel;
            // The reference truncates each WM_MOUSEWHEEL message independently;
            // accumulating fractional messages here would change menu scrolling.
            event.notches=static_cast<std::int32_t>(source.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-source.wheel.y:source.wheel.y);
            if(!event.notches)return;break;
        case SDL_EVENT_KEY_DOWN:event.kind=SoftwareWindowEventKind::KeyDown;event.key=windowKey(source.key.key);event.repeat=source.key.repeat;break;
        default:return;
        }
        events_.windowEvent(event);
    }
    SdlApi api_;
    SoftwareWindowEvents& events_;
    std::thread::id owner_;
    SDL_Window* window_=nullptr;
    SDL_WindowID id_=0;
    bool initialized_=false,captured_=false,sessionClaimed_=false;
    std::string error_;
};
} // namespace
std::unique_ptr<SoftwareWindow> createSdl3SoftwareWindow(const SoftwareWindowOptions& options,
    SoftwareWindowEvents& events,presentation::FramePresentStatistics& statistics,std::string& error) {
    error=softwareWindowUnsupported(options);if(!error.empty())return {};
    auto window=std::make_unique<SdlWindow>(events,statistics);
    if(!window->initialize(options)){error=window->error();return {};}
    return window;
}
} // namespace concord::window
#else
namespace concord::window {
std::unique_ptr<SoftwareWindow> createSdl3SoftwareWindow(const SoftwareWindowOptions& options,
    SoftwareWindowEvents&,presentation::FramePresentStatistics&,std::string& error) {
    error=softwareWindowUnsupported(options);
    if(error.empty())error="SDL3 window support was not built; supply the locked project-local SDL3 3.4.16 source and dependency build";
    return {};
}
}
#endif
