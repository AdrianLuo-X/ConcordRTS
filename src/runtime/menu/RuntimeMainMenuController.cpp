#include "runtime/menu/RuntimeMainMenuController.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace concord::main_menu {
void resetMainMenuForEntry(RuntimeMainMenuController& state, const RuntimeConfig& config, RuntimeMainMenuStartPage startPage) {
    state.config = config;
    state.closing = false;
    state.result = RuntimeMainMenuResult::Exit;
    state.pending = MenuAction::None;
    state.phaseTick = 0;
    state.focus = 0;
    state.hover = -1;
    state.pressed = -1;
    state.keyboardTooltip = false;
    state.mouseVisible = false;
    state.pageStatus.clear();
    state.optionWindowWidth = std::max(1, config.windowWidth);
    state.optionWindowHeight = std::max(1, config.windowHeight);
    state.optionTargetFps = std::max(1, config.targetFps);
    state.optionGameSpeed = std::clamp(config.gameSpeed, 0, 6);
    state.services.setMusicVolumePercent(std::clamp(config.musicVolume, 0, 100));
    state.page = startPage == RuntimeMainMenuStartPage::SinglePlayer
        ? MenuPage::SinglePlayer
        : MenuPage::Home;
}

std::vector<ButtonSpec> buttons(const RuntimeMainMenuController& state) {
    const auto text = [&state](const char* key, const wchar_t* fallback) {
        return state.services.text(key, fallback);
    };
    if (state.page == MenuPage::SinglePlayer) {
        return {
            {0, text("GUI:NewCampaign", L"Campaign"),
                text("STT:SingleButtonNewCampaign", L"Start a new campaign."),
                MenuAction::Campaign},
            {1, text("GUI:LoadSavedGame", L"Load Game"),
                text("STT:SingleButtonLoadSavedGame", L"Load a saved game."),
                MenuAction::LoadGame},
            {2, text("GUI:Skirmish", L"Skirmish"),
                text("STT:SingleButtonSkirmish", L"Battle the computer in a skirmish game."),
                MenuAction::Skirmish},
            {-1, text("GUI:Back", L"Back"),
                text("STT:SingleButtonBack", L"Return to the Main Menu."),
                MenuAction::OpenHome}
        };
    }
    if (state.page == MenuPage::Movies) {
        const bool moviesAvailable = !state.moviePaths.empty();
        return {
            {0, text("GUI:Previous", L"Previous"),
                L"Select the previous installed movie.",
                moviesAvailable ? MenuAction::PreviousMovie : MenuAction::None},
            {1, text("GUI:Play", L"Play Movie"),
                L"Play the selected movie. Press Esc to skip it.",
                moviesAvailable ? MenuAction::PlayMovie : MenuAction::None},
            {2, text("GUI:Next", L"Next"),
                L"Select the next installed movie.",
                moviesAvailable ? MenuAction::NextMovie : MenuAction::None},
            {3, text("GUI:ViewCredits", L"Credits"),
                L"View the game credits.", MenuAction::OpenCredits},
            {-1, text("GUI:Back", L"Back"),
                L"Return to the Main Menu.", MenuAction::OpenHome}
        };
    }
    if (state.page == MenuPage::Credits) {
        return {
            {0, text("GUI:Previous", L"Previous Page"),
                L"Show the previous credits page.",
                MenuAction::PreviousCreditsPage},
            {1, text("GUI:Next", L"Next Page"),
                L"Show the next credits page.",
                MenuAction::NextCreditsPage},
            {-1, text("GUI:Back", L"Back"),
                L"Return to Movies & Credits.", MenuAction::OpenMovies}
        };
    }
    if (state.page == MenuPage::Options) {
        return {
            {0, text("GUI:Resolution", L"Resolution"),
                L"Cycle the game window resolution.",
                MenuAction::CycleResolution},
            {1, text("GUI:TargetFPS", L"Frame Rate"),
                L"Cycle the target frame rate.",
                MenuAction::CycleTargetFps},
            {2, text("GUI:GameSpeed", L"Game Speed"),
                L"Cycle the default simulation speed.",
                MenuAction::CycleGameSpeed},
            {3, text("GUI:Apply", L"Apply"),
                L"Apply these options and return to the Main Menu.",
                MenuAction::ApplyOptions},
            {-1, text("GUI:Back", L"Cancel"),
                L"Discard these changes and return to the Main Menu.",
                MenuAction::OpenHome}
        };
    }
    return {
        {0, text("GUI:SinglePlayer", L"Single Player"),
            text("STT:MainButtonSinglePlayer", L"Play a single player game."),
            MenuAction::OpenSinglePlayer},
        {1, text("GUI:Internet", L"Internet"),
            text("STT:MainButtonWWOnline", L"Play on the Internet."),
            MenuAction::Internet},
        {2, text("GUI:Network", L"Network"),
            text("STT:MainButtonNetwork", L"Play a network game."),
            MenuAction::Network},
        {3, text("GUI:MoviesAndCredits", L"Movies & Credits"),
            text("STT:MainButtonMovies", L"View movies and credits."),
            MenuAction::OpenMovies},
        {4, text("GUI:Options", L"Options"),
            text("STT:MainButtonOptions", L"Customize game settings."),
            MenuAction::OpenOptions},
        {-1, text("GUI:ExitGame", L"Exit Game"),
            text("STT:MainButtonExitGamemd", L"Quit Yuri's Revenge."), MenuAction::Exit}
    };
}

int sidebarX(const RuntimeMainMenuController& state) {
    (void)state;
    return kSidebarLeft;
}

int buttonsTop(const RuntimeMainMenuController& state) {
    (void)state;
    return kSidebarTopHeight;
}

int slotHeight(const RuntimeMainMenuController& state) {
    (void)state;
    return kSlotHeight;
}

int slotCount(const RuntimeMainMenuController& state) {
    (void)state;
    // The original shell has eight 42-pixel background slots ending at y=535.
    // Exit/Back is a separate button inside SDBTM below those slots.
    return kSlotCount;
}

int resolvedSlot(const RuntimeMainMenuController& state, const ButtonSpec& button) {
    (void)state;
    // Negative is the dedicated SDBTM button, not the eighth normal slot.
    return button.slot;
}

int buttonTopFor(const ButtonSpec& button) {
    return button.slot < 0
        ? kBottomButtonTop
        : kMenuButtonTop + button.slot * kSlotHeight;
}

void updateViewport(RuntimeMainMenuController& state, int clientWidth, int clientHeight) {
    const double fitScale = std::min(
        static_cast<double>(std::max(1, clientWidth)) / kWidth,
        static_cast<double>(std::max(1, clientHeight)) / kHeight
    );
    // Preserve the original pixel grid whenever it fits.  1024x640, the
    // normal runtime window, therefore shows an exact centered 800x600 image
    // instead of a blurred 1.066x stretch with shifted hit targets.
    const int integerScale = static_cast<int>(std::floor(fitScale));
    const double scale = integerScale >= 1
        ? static_cast<double>(integerScale)
        : fitScale;
    state.viewWidth = std::max(1, static_cast<int>(std::lround(kWidth * scale)));
    state.viewHeight = std::max(1, static_cast<int>(std::lround(kHeight * scale)));
    state.viewLeft = (clientWidth - state.viewWidth) / 2;
    state.viewTop = (clientHeight - state.viewHeight) / 2;
}

RuntimeInputPixelPoint clientToBase(const RuntimeMainMenuController& state, RuntimeInputPixelPoint point) {
    RuntimeInputPixelPoint result{-1, -1};
    if (point.x < state.viewLeft || point.y < state.viewTop
        || point.x >= state.viewLeft + state.viewWidth
        || point.y >= state.viewTop + state.viewHeight) {
        return result;
    }
    result.x = std::clamp<int>(
        (point.x - state.viewLeft) * kWidth / std::max(1, state.viewWidth),
        0, kWidth - 1
    );
    result.y = std::clamp<int>(
        (point.y - state.viewTop) * kHeight / std::max(1, state.viewHeight),
        0, kHeight - 1
    );
    return result;
}

int hitButton(const RuntimeMainMenuController& state, RuntimeInputPixelPoint point) {
    if (point.x < 0 || state.phase != MenuPhase::Idle) return -1;
    const int side = sidebarX(state);
    if (point.x < side + 12 || point.x >= side + 158) return -1;
    const int top = buttonsTop(state);
    const int height = slotHeight(state);
    const std::vector<ButtonSpec> current = buttons(state);
    for (std::size_t i = 0; i < current.size(); ++i) {
        const int slot = resolvedSlot(state, current[i]);
        if (slot < 0) {
            if (point.y >= kBottomHitTop && point.y < kHeight) {
                return static_cast<int>(i);
            }
        } else {
            const int y = top + slot * height;
            if (point.y >= y && point.y < y + height) return static_cast<int>(i);
        }
    }
    return -1;
}

void finishMenu(RuntimeMainMenuController& state, RuntimeMainMenuResult result) {
    if (state.closing) return;
    state.closing = true;
    state.result = result;
    state.services.stopVideo();
    state.services.stopMusic();
    state.services.destroyWindow();
}

void publishMenuConfig(const RuntimeMainMenuController& state, RuntimeConfig& output) {
    output = state.config;
}

MenuAction escapeActionForPage(MenuPage page) {
    switch (page) {
    case MenuPage::Home:
        return MenuAction::Exit;
    case MenuPage::Credits:
        return MenuAction::OpenMovies;
    case MenuPage::SinglePlayer:
    case MenuPage::Movies:
    case MenuPage::Options:
        return MenuAction::OpenHome;
    }
    return MenuAction::OpenHome;
}

void cycleResolution(RuntimeMainMenuController& state) {
    std::size_t next = 0;
    for (std::size_t index = 0; index < kOptionResolutions.size(); ++index) {
        if (kOptionResolutions[index].first == state.optionWindowWidth
            && kOptionResolutions[index].second == state.optionWindowHeight) {
            next = (index + 1) % kOptionResolutions.size();
            break;
        }
    }
    state.optionWindowWidth = kOptionResolutions[next].first;
    state.optionWindowHeight = kOptionResolutions[next].second;
}

void cycleTargetFps(RuntimeMainMenuController& state) {
    std::size_t next = 0;
    for (std::size_t index = 0; index < kOptionTargetFps.size(); ++index) {
        if (kOptionTargetFps[index] == state.optionTargetFps) {
            next = (index + 1) % kOptionTargetFps.size();
            break;
        }
    }
    state.optionTargetFps = kOptionTargetFps[next];
}

std::size_t creditPageCount(const RuntimeMainMenuController& state) {
    return std::max<std::size_t>(
        1,
        (state.creditLines.size() + kCreditsLinesPerPage - 1)
            / kCreditsLinesPerPage
    );
}

std::wstring moviePlaybackStatus(const RuntimeMainMenuController& state, bool played, const std::string& error) {
    if (played) return L"Movie playback finished.";
    const std::wstring detail = error.empty()
        ? L"unknown decoder error"
        : state.services.widen(error);
    return L"Playback unavailable: " + detail;
}

void playSelectedMovie(RuntimeMainMenuController& state) {
    state.pageStatus.clear();
    if (state.moviePaths.empty()) {
        state.pageStatus = L"No installed movie is available.";
        state.backDirty = true;
        return;
    }
    state.services.prepareMoviePlayback();
    std::string error;
    const fs::path& movie = state.moviePaths[
        std::min(state.movieIndex, state.moviePaths.size() - 1)
    ];
    const bool played = state.services.playMovie(movie, error);
    state.pageStatus = moviePlaybackStatus(state, played, error);
    state.services.restartMainVideo();
    state.services.startPageMusic();
    state.services.focusAndRepaint();
    state.backDirty = true;
}

bool performImmediateAction(RuntimeMainMenuController& state, MenuAction action) {
    if (action == MenuAction::PreviousMovie) {
        if (!state.moviePaths.empty()) {
            state.movieIndex = (state.movieIndex + state.moviePaths.size() - 1)
                % state.moviePaths.size();
        }
    } else if (action == MenuAction::PlayMovie) {
        playSelectedMovie(state);
        return true;
    } else if (action == MenuAction::NextMovie) {
        if (!state.moviePaths.empty()) {
            state.movieIndex = (state.movieIndex + 1) % state.moviePaths.size();
        }
    } else if (action == MenuAction::PreviousCreditsPage) {
        const std::size_t pages = creditPageCount(state);
        state.creditsPage = (state.creditsPage + pages - 1) % pages;
    } else if (action == MenuAction::NextCreditsPage) {
        const std::size_t pages = creditPageCount(state);
        state.creditsPage = (state.creditsPage + 1) % pages;
    } else if (action == MenuAction::CycleResolution) {
        cycleResolution(state);
    } else if (action == MenuAction::CycleTargetFps) {
        cycleTargetFps(state);
    } else if (action == MenuAction::CycleGameSpeed) {
        state.optionGameSpeed = (state.optionGameSpeed + 1) % 7;
    } else {
        return false;
    }
    state.pageStatus.clear();
    state.backDirty = true;
    state.services.repaintValidWindow();
    return true;
}

void beginAction(RuntimeMainMenuController& state, MenuAction action) {
    if (state.phase != MenuPhase::Idle || state.closing) return;
    if (action == MenuAction::None) {
        return;
    }
    if (action == MenuAction::Skirmish && !state.smoke) {
        state.services.prewarmSkirmish();
    }
    state.pending = action;
    state.phase = MenuPhase::SlideOut;
    state.phaseStarted = state.presentationClock->nowMs();
    state.phaseTick = 0;
    state.pressed = -1;
    state.backDirty = true;
    state.services.playSound("MenuSlideOut");
    state.services.repaintPresentWindow();
}

void activate(RuntimeMainMenuController& state, int logicalIndex, bool playClick) {
    const std::vector<ButtonSpec> current = buttons(state);
    if (logicalIndex < 0 || logicalIndex >= static_cast<int>(current.size())) return;
    const MenuAction action = current[static_cast<std::size_t>(logicalIndex)].action;
    if (action == MenuAction::None) return;
    if (playClick) state.services.playSound("MenuClick");
    if (!performImmediateAction(state, action)) beginAction(state, action);
}

bool updateAnimation(RuntimeMainMenuController& state, std::uint64_t now) {
    if (state.phase == MenuPhase::Idle || state.closing) return false;
    bool changed = false;
    const std::uint64_t elapsed = now - state.phaseStarted;
    const int nextTick = static_cast<int>(elapsed / kAnimationFrameMs);
    if (nextTick != state.phaseTick) {
        state.phaseTick = nextTick;
        changed = true;
    }
    const std::uint64_t duration = static_cast<std::uint64_t>(
        slotCount(state) - 1 + kAnimationFrames
    ) * kAnimationFrameMs;
    if (elapsed < duration) return changed;
    if (state.phase == MenuPhase::SlideIn) {
        state.phase = MenuPhase::Idle;
        state.phaseTick = 0;
        return true;
    }

    const MenuAction action = state.pending;
    if (action == MenuAction::Skirmish && !state.smoke
        && !state.services.skirmishReady()) {
        state.pageStatus = L"正在准备遭遇战…";
        // Keep dispatching timer, paint, mouse and close messages while the
        // worker finishes. The video and cursor continue updating normally.
        return true;
    }
    state.pending = MenuAction::None;
    if (action == MenuAction::Campaign
        || action == MenuAction::LoadGame
        || action == MenuAction::Skirmish
        || action == MenuAction::Network
        || action == MenuAction::Internet) {
        RuntimeMainMenuResult result = RuntimeMainMenuResult::Skirmish;
        if (action == MenuAction::Campaign) {
            result = RuntimeMainMenuResult::Campaign;
        } else if (action == MenuAction::LoadGame) {
            result = RuntimeMainMenuResult::LoadGame;

        } else if (action == MenuAction::Network) {
            result = RuntimeMainMenuResult::Network;
        } else if (action == MenuAction::Internet) {
            result = RuntimeMainMenuResult::Internet;
        }
        finishMenu(state, result);
        return true;
    }
    if (action == MenuAction::Exit) {
        finishMenu(state, RuntimeMainMenuResult::Exit);
        return true;
    }
    if (action == MenuAction::ApplyOptions) {
        RuntimeConfig candidate = state.config;
        candidate.windowWidth = state.optionWindowWidth;
        candidate.windowHeight = state.optionWindowHeight;
        candidate.targetFps = state.optionTargetFps;
        candidate.gameSpeed = state.optionGameSpeed;
        std::string saveError;
        if (state.services.savePreferences(candidate, saveError)) {
            state.config = std::move(candidate);
            state.services.resizeHost();
            state.page = MenuPage::Home;
        } else {
            state.pageStatus = L"Could not save options: "
                + state.services.widen(saveError);
        }
    } else if (action == MenuAction::OpenSinglePlayer) {
        state.page = MenuPage::SinglePlayer;
    } else if (action == MenuAction::OpenMovies) {
        state.page = MenuPage::Movies;
    } else if (action == MenuAction::OpenCredits) {
        state.page = MenuPage::Credits;
    } else if (action == MenuAction::OpenOptions) {
        state.optionWindowWidth = std::max(1, state.config.windowWidth);
        state.optionWindowHeight = std::max(1, state.config.windowHeight);
        state.optionTargetFps = std::max(1, state.config.targetFps);
        state.optionGameSpeed = std::clamp(state.config.gameSpeed, 0, 6);
        state.page = MenuPage::Options;
    } else if (action == MenuAction::OpenHome) {
        state.page = MenuPage::Home;
    }
    if (action != MenuAction::ApplyOptions
        || state.page != MenuPage::Options) {
        state.pageStatus.clear();
    }
    state.focus = 0;
    state.hover = -1;
    state.keyboardTooltip = false;
    state.phase = MenuPhase::SlideIn;
    state.phaseStarted = now;
    state.phaseTick = 0;
    state.services.startPageMusic();
    state.services.playSound("MenuSlideIn");
    return true;
}

bool updateDynamicState(RuntimeMainMenuController& state) {
    if (state.closing) return false;
    const std::uint64_t now = state.presentationClock->nowMs();
    bool changed = false;
    changed = state.services.pollVideo() || changed;
    if (state.services.warningFrames() != 0) {
        const int nextWarning = static_cast<int>(
            (now / 75ULL) % state.services.warningFrames()
        );
        if (nextWarning != state.warningFrame) {
            state.warningFrame = nextWarning;
            changed = true;
        }
    }
    changed = updateAnimation(state, now) || changed;
    if (changed) state.backDirty = true;
    return changed;
}

void handleMainMenuMouseMove(RuntimeMainMenuController& state, RuntimeInputPixelPoint client) {
        const RuntimeInputPixelPoint base = clientToBase(state, client);
        const RuntimeInputPixelPoint oldMouse = state.mousePoint;
        const bool oldVisible = state.mouseVisible;
        const int oldHover = state.hover;
        const bool oldKeyboardTooltip = state.keyboardTooltip;
        state.mousePoint = base;
        state.mouseVisible = base.x >= 0;
        state.hover = hitButton(state, base);
        state.keyboardTooltip = false;
        state.services.trackMouseLeave();
        if (oldMouse.x != state.mousePoint.x || oldMouse.y != state.mousePoint.y
            || oldVisible != state.mouseVisible || oldHover != state.hover
            || oldKeyboardTooltip != state.keyboardTooltip) {
            state.backDirty = true;
            state.services.repaintEventWindow();
        }
        return;
}

void handleMainMenuMouseLeave(RuntimeMainMenuController& state) {
        state.backDirty = state.mouseVisible || state.hover >= 0 || state.backDirty;
        state.mouseVisible = false;
        state.hover = -1;
        if (state.pressed < 0) state.services.repaintEventWindow();
        return;
}

void handleMainMenuLeftDown(RuntimeMainMenuController& state, RuntimeInputPixelPoint client) {
        const int hit = hitButton(state, clientToBase(state, client));
        if (hit >= 0) {
            state.services.capture();
            state.focus = hit;
            state.hover = hit;
            state.pressed = hit;
            state.backDirty = true;
            state.services.playSound("MenuClick");
            state.services.repaintEventWindow();
        }
        return;
}

void handleMainMenuLeftUp(RuntimeMainMenuController& state, RuntimeInputPixelPoint client) {
        const int hit = hitButton(state, clientToBase(state, client));
        const int pressed = state.pressed;
        state.pressed = -1;
        state.backDirty = pressed >= 0 || state.backDirty;
        state.services.releaseCaptureIfOwned();
        if (hit >= 0 && hit == pressed) activate(state, hit, false);
        state.services.repaintEventWindow();
        return;
}

void handleMainMenuCaptureLost(RuntimeMainMenuController& state) {
        state.backDirty = state.pressed >= 0 || state.backDirty;
        state.pressed = -1;
        state.services.repaintEventWindow();
        return;
}

bool handleMainMenuKey(RuntimeMainMenuController& state, MainMenuKey key) {
        const int count = static_cast<int>(buttons(state).size());
        if (state.phase != MenuPhase::Idle || count <= 0) return true;
        if (key == MainMenuKey::Up || (key == MainMenuKey::Tab && state.services.shiftDown())) {
            state.focus = (state.focus + count - 1) % count;
            state.keyboardTooltip = true;
            state.hover = -1;
            state.backDirty = true;
            state.services.repaintEventWindow();
            return true;
        }
        if (key == MainMenuKey::Down || key == MainMenuKey::Tab) {
            state.focus = (state.focus + 1) % count;
            state.keyboardTooltip = true;
            state.hover = -1;
            state.backDirty = true;
            state.services.repaintEventWindow();
            return true;
        }
        if (key == MainMenuKey::Return || key == MainMenuKey::Space) {
            activate(state, state.focus, true);
            return true;
        }
        if (key == MainMenuKey::Escape) {
            state.services.playSound("MenuClick");
            beginAction(state, escapeActionForPage(state.page));
            return true;
        }
        return false;
}
} // namespace concord::main_menu
