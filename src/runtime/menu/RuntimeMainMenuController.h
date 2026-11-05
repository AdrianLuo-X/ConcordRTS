#pragma once
#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeMainMenuResult.h"
#include "ui/time/RuntimeUiClock.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace concord::main_menu {
namespace fs = std::filesystem;
constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr int kLeftWidth = 632;
constexpr int kStatusTop = 568;
constexpr int kSidebarLeft = 632;
constexpr int kSidebarTopHeight = 199;
constexpr int kSlotHeight = 42;
constexpr int kSlotCount = 8;
constexpr int kSidebarBottomTop = 535;
constexpr int kMenuButtonLeft = 653;
constexpr int kMenuButtonTop = 207;
constexpr int kBottomButtonTop = 543;
constexpr int kBottomHitTop = 535;
constexpr int kWarningLeft = 670;
// SDTP's 92x53 monitor aperture starts at local (38, 48). The
// SDWRNANM canvas replaces that embedded image without covering its bezel.
constexpr int kWarningTop = 48;
constexpr int kAnimationFrames = 6;
constexpr std::uint64_t kAnimationFrameMs = 42;
constexpr std::size_t kCreditsLinesPerPage = 18;
constexpr std::array<std::pair<int, int>, 5> kOptionResolutions{{
    {800, 600},
    {1024, 640},
    {1024, 768},
    {1280, 720},
    {1600, 900}
}};
constexpr std::array<int, 4> kOptionTargetFps{{30, 60, 120, 144}};

enum class MenuPage {
    Home,
    SinglePlayer,
    Movies,
    Credits,
    Options
};
enum class MenuPhase { SlideIn, Idle, SlideOut };
enum class MenuAction {
    None,
    OpenSinglePlayer,
    OpenHome,
    OpenMovies,
    OpenCredits,
    OpenOptions,
    PreviousMovie,
    PlayMovie,
    NextMovie,
    PreviousCreditsPage,
    NextCreditsPage,
    CycleResolution,
    CycleTargetFps,
    CycleGameSpeed,
    ApplyOptions,
    Campaign,
    LoadGame,
    Skirmish,
    Network,
    Internet,
    Exit
};

struct ButtonSpec {
    int slot = 0;
    std::wstring label;
    std::wstring tooltip;
    MenuAction action = MenuAction::None;
};

enum class MainMenuKey { Other, Up, Down, Tab, Return, Space, Escape };
class RuntimeMainMenuServices {
public:
    virtual ~RuntimeMainMenuServices() = default;
    virtual std::wstring text(const char*, const wchar_t*) const = 0;
    virtual std::wstring widen(const std::string&) const = 0;
    virtual void playSound(const std::string&) = 0;
    virtual void stopVideo() = 0;
    virtual void stopMusic() = 0;
    virtual void setMusicVolumePercent(int) = 0;
    virtual void destroyWindow() = 0;
    virtual void repaintValidWindow() = 0;
    virtual void repaintPresentWindow() = 0;
    virtual void repaintEventWindow() = 0;
    virtual void prewarmSkirmish() = 0;
    virtual bool skirmishReady() const = 0;
    virtual bool savePreferences(const RuntimeConfig&, std::string&) = 0;
    virtual void resizeHost() = 0;
    virtual void startPageMusic() = 0;
    virtual bool playMovie(const std::filesystem::path&, std::string&) = 0;
    virtual void prepareMoviePlayback() = 0;
    virtual void restartMainVideo() = 0;
    virtual void focusAndRepaint() = 0;
    virtual bool pollVideo() = 0;
    virtual std::size_t warningFrames() const = 0;
    virtual void trackMouseLeave() = 0;
    virtual void capture() = 0;
    virtual void releaseCaptureIfOwned() = 0;
    virtual bool shiftDown() const = 0;
};

// Owns navigation, options, phase and input policy. The native owner supplies
// synchronous effects; no window/device/resource watcher or media worker is stored.
// The service and presentation clock must outlive this controller; no call is retained.
struct RuntimeMainMenuController {
    RuntimeMainMenuController(RuntimeMainMenuServices& effects, const RuntimeUiClock& clock)
        : services(effects), presentationClock(&clock) {}
    RuntimeMainMenuController(const RuntimeMainMenuController&) = delete;
    RuntimeMainMenuController& operator=(const RuntimeMainMenuController&) = delete;
    RuntimeMainMenuServices& services;
    const RuntimeUiClock* presentationClock;
    RuntimeConfig config;
    RuntimeMainMenuResult result = RuntimeMainMenuResult::Exit;
    bool smoke = false;
    bool closing = false;
    MenuPage page = MenuPage::Home;
    std::vector<fs::path> moviePaths;
    std::size_t movieIndex = 0;
    std::vector<std::wstring> creditLines;
    std::size_t creditsPage = 0;
    int optionWindowWidth = 0;
    int optionWindowHeight = 0;
    int optionTargetFps = 0;
    int optionGameSpeed = 0;
    std::wstring pageStatus;
    MenuPhase phase = MenuPhase::SlideIn;
    MenuAction pending = MenuAction::None;
    std::uint64_t phaseStarted = 0;
    int focus = 0;
    int hover = -1;
    int pressed = -1;
    bool keyboardTooltip = false;
    bool mouseVisible = false;
    RuntimeInputPixelPoint mousePoint{};
    int viewLeft = 0;
    int viewTop = 0;
    int viewWidth = kWidth;
    int viewHeight = kHeight;
    bool backDirty = true;
    int warningFrame = 0;
    int phaseTick = 0;

};
void resetMainMenuForEntry(RuntimeMainMenuController& state, const RuntimeConfig& config, RuntimeMainMenuStartPage startPage);
std::vector<ButtonSpec> buttons(const RuntimeMainMenuController& state);
int sidebarX(const RuntimeMainMenuController& state);
int buttonsTop(const RuntimeMainMenuController& state);
int slotHeight(const RuntimeMainMenuController& state);
int slotCount(const RuntimeMainMenuController& state);
int resolvedSlot(const RuntimeMainMenuController& state, const ButtonSpec& button);
int buttonTopFor(const ButtonSpec& button);
void updateViewport(RuntimeMainMenuController& state, int clientWidth, int clientHeight);
RuntimeInputPixelPoint clientToBase(const RuntimeMainMenuController& state, RuntimeInputPixelPoint point);
int hitButton(const RuntimeMainMenuController& state, RuntimeInputPixelPoint point);
void finishMenu(RuntimeMainMenuController& state, RuntimeMainMenuResult result);
void publishMenuConfig(const RuntimeMainMenuController& state, RuntimeConfig& output);
MenuAction escapeActionForPage(MenuPage page);
void cycleResolution(RuntimeMainMenuController& state);
void cycleTargetFps(RuntimeMainMenuController& state);
std::size_t creditPageCount(const RuntimeMainMenuController& state);
std::wstring moviePlaybackStatus(const RuntimeMainMenuController& state, bool played, const std::string& error);
void playSelectedMovie(RuntimeMainMenuController& state);
bool performImmediateAction(RuntimeMainMenuController& state, MenuAction action);
void beginAction(RuntimeMainMenuController& state, MenuAction action);
void activate(RuntimeMainMenuController& state, int logicalIndex, bool playClick);
bool updateAnimation(RuntimeMainMenuController& state, std::uint64_t now);
bool updateDynamicState(RuntimeMainMenuController& state);
void handleMainMenuMouseMove(RuntimeMainMenuController& state, RuntimeInputPixelPoint client);
void handleMainMenuMouseLeave(RuntimeMainMenuController& state);
void handleMainMenuLeftDown(RuntimeMainMenuController& state, RuntimeInputPixelPoint client);
void handleMainMenuLeftUp(RuntimeMainMenuController& state, RuntimeInputPixelPoint client);
void handleMainMenuCaptureLost(RuntimeMainMenuController& state);
bool handleMainMenuKey(RuntimeMainMenuController& state, MainMenuKey key);
} // namespace concord::main_menu
