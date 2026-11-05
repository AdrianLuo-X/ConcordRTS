// Complete original native WM_TIMER case body, unchanged; diagnostic-only source.
struct SkirmishOriginalTimer {
    const RuntimeUiClock* source;
    std::uint64_t GetTickCount64() const noexcept { return source->nowMs(); }
    LRESULT dispatch(MenuState* state,HWND window,WPARAM wParam) {
        if (wParam == 2) {
            if (pollRandomMap(*state)) InvalidateRect(window, nullptr, FALSE);
            updateRandomMapNameWindow(*state);
            state->audio.ensureMusicPlaying();
            if (updateMenuTransition(*state, GetTickCount64())
                && IsWindow(window)) {
                state->backDirty = true;
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    }
};
