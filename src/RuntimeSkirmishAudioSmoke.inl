bool runRuntimeSkirmishAudioSmoke(MenuState& state, std::string& error) {
    struct Backend {
        std::array<std::optional<bool>, 3> playing{false, false, false};
        std::array<int, 3> starts{};
        bool interrupted = false;
        std::optional<bool> isPlaying(std::size_t channel) const { return playing[channel]; }
        bool open(std::size_t, const std::wstring&) const { return true; }
        bool playFromStart(std::size_t channel) { playing[channel] = true; ++starts[channel]; return true; }
        void close(std::size_t channel) {
            interrupted = interrupted || playing[channel].value_or(true);
            playing[channel] = false;
        }
    } backend;
    RuntimeMenuSoundPool<3> pool;
    bool tails = pool.play(L"click.wav", backend) && pool.play(L"slide.wav", backend)
        && pool.play(L"tab.wav", backend) && !pool.play(L"rapid-click.wav", backend)
        && backend.starts == std::array<int, 3>{1, 1, 1} && !backend.interrupted;
    backend.playing[0] = false;
    backend.playing[1] = std::nullopt;
    pool.collect(backend);
    tails = tails && !pool.opened[0] && pool.opened[1] && pool.opened[2]
        && pool.play(L"next-page.wav", backend) && backend.starts[0] == 2 && !backend.interrupted;
    backend.playing.fill(false);
    pool.collect(backend);
    tails = tails && pool.opened == std::array<bool, 3>{false, false, false} && !backend.interrupted;
    if (!tails) { error = "button/slide tails were interrupted or completed voices were not released"; return false; }

    for (const auto* event : {"MenuClick", "MenuSlideIn", "MenuSlideOut"}) {
        const auto sample = state.audio.samples.find(event);
        if (sample == state.audio.samples.end()) { error = "missing menu event"; return false; }
        const auto resolved = resolveAudioSampleToWave(pathString(state.audio.assetRoot),
            pathString(state.audio.cacheRoot), sample->second);
        if (!resolved.success || !regularFile(fs::path(resolved.wavPath))) {
            error = std::string("missing complete menu wave: ") + event; return false;
        }
    }

    std::vector<std::string> events;
    state.audio.eventTrace = &events;
    const int oldScroll = state.mapScroll;
    const auto oldStatus = state.status;
    HWND window = createShellSkirmishWindow(state, state.config, nullptr);
    const auto cleanup = [&] {
        state.audio.eventTrace = nullptr;
        if (window && IsWindow(window)) DestroyWindow(window);
        state.mapScroll = oldScroll;
        state.status = oldStatus;
        state.hover = state.pressed = {};
        state.backDirty = true;
    };
    if (!window || IsWindowVisible(window)) { cleanup(); error = "audio smoke requires a hidden shell"; return false; }
    const auto click = [&](HitKind kind) {
        if (!renderShellBackBuffer(state)) return false;
        const auto hit = std::find_if(state.hits.begin(), state.hits.end(),
            [kind](const HitRegion& region) { return region.target.kind == kind; });
        if (hit == state.hits.end()) return false;
        const int x = state.viewLeft + (hit->rect.left + hit->rect.right) * state.viewWidth / (2 * kBaseWidth);
        const int y = state.viewTop + (hit->rect.top + hit->rect.bottom) * state.viewHeight / (2 * kBaseHeight);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return true;
    };
    bool routed = click(HitKind::ChooseMap) && state.page == SkirmishPage::MapSelection
        && events == std::vector<std::string>{"MenuClick", "MenuSlideIn"};
    events.clear();
    state.mapScroll = 0;
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    const int scrolled = state.mapScroll;
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    const bool silentWheel = scrolled > 0 && state.mapScroll == 0 && events.empty();
    routed = click(HitKind::CancelMap) && state.page == SkirmishPage::Lobby
        && events == std::vector<std::string>{"MenuClick", "MenuSlideOut"} && routed;
    events.clear();
    routed = click(HitKind::ChooseMap) && click(HitKind::UseMap) && state.page == SkirmishPage::Lobby
        && events == std::vector<std::string>{"MenuClick", "MenuSlideIn", "MenuClick", "MenuSlideOut"} && routed;
    cleanup();
    if (!routed || !silentWheel) { error = "navigation click/slide routing or silent wheel scrolling"; return false; }
    std::cout << "Skirmish audio: completeTails=yes rapidClicks=yes boundedVoices=yes "
                 "originalWaves=3 choose/use/cancel=click+slide wheel=silent+scrolling hiddenWindow=yes\n";
    return true;
}
