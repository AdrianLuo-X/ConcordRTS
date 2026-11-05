// Exact complete original methods; only class scope supplies a per-instance source.
struct MainPresentationOriginal {
    const RuntimeUiClock* source;
    std::uint64_t GetTickCount64() const noexcept { return source->nowMs(); }

bool pollMainMenuVideo(MenuState& state, DWORD waitMilliseconds = 0) {
    bool changed = state.videoAnimation.poll(
        state.videoFrame,
        waitMilliseconds
    );
    if (!state.videoFirstFramePending) return changed;
    const std::uint64_t elapsed = GetTickCount64() - state.videoStartupStarted;
    if (changed) {
        state.videoFirstFramePending = false;
        std::cout << "Main menu Bink animation ready: sequence="
                  << state.videoAnimation.sequence()
                  << " startupMs=" << elapsed << "\n";
    } else if (!state.videoAnimation.failure().empty()) {
        failMainMenuVideoStartup(state, state.videoAnimation.failure());
        changed = true;
    } else if (state.videoAnimation.finished()) {
        failMainMenuVideoStartup(state, "decoder stopped before its first frame");
        changed = true;
    } else if (elapsed >= kMainMenuFirstFrameDeadlineMs) {
        failMainMenuVideoStartup(state, "first decoded frame startup deadline expired");
        changed = true;
    }
    return changed;
}

bool startMainMenuVideo(
    MenuState& state,
    const RuntimeConfig& config,
    DWORD initialWaitMilliseconds
) {
    state.videoFirstFramePending = false;
    state.videoStartupStarted = GetTickCount64();
    if (!state.videoAnimation.start(
            config,
            state.exportRoot,
            {},
            concord::bink_stream::PlaybackMode::Loop,
            false,
            0
        )) {
        failMainMenuVideoStartup(state, state.videoAnimation.failure());
        return false;
    }
    state.videoFirstFramePending = true;
    if (!pollMainMenuVideo(state, initialWaitMilliseconds)
        && state.videoFirstFramePending) {
        // The menu stays usable while its timer polls the live decoder.
        // A preview is not a successful decoded frame or a permanent fallback.
        std::cout << "Main menu Bink animation pending: original extracted preview "
                     "shown while waiting for the first decoded frame.\n";
    }
    return state.videoFirstFramePending || state.videoAnimation.sequence() > 0;
}

void beginAction(MenuState& state, MenuAction action) {
    if (state.phase != MenuPhase::Idle || state.closing) return;
    if (action == MenuAction::None) {
        return;
    }
    if (action == MenuAction::Skirmish && !state.smoke) {
        prewarmRuntimeSkirmishMenu(state.config);
    }
    state.pending = action;
    state.phase = MenuPhase::SlideOut;
    state.phaseStarted = GetTickCount64();
    state.phaseTick = 0;
    state.pressed = -1;
    state.backDirty = true;
    state.audio.play("MenuSlideOut");
    if (state.window != nullptr) InvalidateRect(state.window, nullptr, FALSE);
}

bool updateDynamicState(MenuState& state) {
    if (state.closing) return false;
    const std::uint64_t now = GetTickCount64();
    bool changed = false;
    changed = pollMainMenuVideo(state) || changed;
    if (!state.warning.empty()) {
        const int nextWarning = static_cast<int>(
            (now / 75ULL) % state.warning.size()
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
};
