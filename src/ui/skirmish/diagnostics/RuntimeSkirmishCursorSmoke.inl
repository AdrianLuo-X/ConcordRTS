// Exercise the real shell window messages without moving the desktop pointer.
bool runRuntimeSkirmishCursorSmoke(
    MenuState& state,
    const RuntimeConfig& config,
    const fs::path& bmpPath,
    std::string& error
) {
    struct CursorProbeGuard {
        MenuState& state;
        RuntimeConfig config;
        HWND host = nullptr;
        HWND child = nullptr;
        HCURSOR originalCursor = GetCursor();
        ~CursorProbeGuard() {
            if (child != nullptr && IsWindow(child)) DestroyWindow(child);
            if (host != nullptr && IsWindow(host)) DestroyWindow(host);
            SetCursor(originalCursor);
            resetMenuForEntry(state, config);
        }
    } guard{state, config};
    const auto fail = [&](const std::string& reason) {
        error = "skirmish cursor: " + reason;
        return false;
    };
    if (!state.mouse.valid() || state.mouse.source.empty())
        return fail("original mouse.shp frame was not loaded");
    std::size_t transparentPixels = 0;
    std::size_t opaquePixels = 0;
    for (std::size_t at = 0; at + 3 < state.mouse.bgra.size(); at += 4) {
        if (state.mouse.bgra[at + 3] == 0) ++transparentPixels;
        else ++opaquePixels;
    }
    if (transparentPixels == 0 || opaquePixels < 3)
        return fail("mouse frame lost transparent or visible pixels");

    resetMenuForEntry(state, config);
    guard.host = CreateWindowExW(0, L"STATIC", L"Skirmish cursor smoke",
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, kBaseWidth, kBaseHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (guard.host == nullptr) return fail("hidden host creation failed");
    guard.child = createShellSkirmishWindow(state, config, guard.host);
    if (guard.child == nullptr) return fail("hidden shell creation failed");
    const auto render = [&]() {
        RECT client{};
        GetClientRect(guard.child, &client);
        computeShellViewport(state, client.right, client.bottom);
        HDC dc = GetDC(nullptr);
        if (dc == nullptr) return false;
        const bool ok = ensureShellBackBuffer(state, dc)
            && (!state.backDirty || renderShellBackBuffer(state));
        ReleaseDC(nullptr, dc);
        return ok;
    };
    const auto move = [&](POINT base, WPARAM buttons = 0) {
        const int x = state.viewLeft + base.x * state.viewWidth / kBaseWidth;
        const int y = state.viewTop + base.y * state.viewHeight / kBaseHeight;
        SendMessageW(guard.child, WM_MOUSEMOVE, buttons, MAKELPARAM(x, y));
    };
    const auto cursorMatches = [&]() {
        if (!state.mouseVisible || state.mousePoint.x < 0 || state.mousePoint.y < 0)
            return false;
        int checked = 0;
        for (int y = 0; y < state.mouse.height; ++y) {
            for (int x = 0; x < state.mouse.width; ++x) {
                const std::size_t source = static_cast<std::size_t>(
                    (y * state.mouse.width + x) * 4);
                if (state.mouse.bgra[source + 3] != 255) continue;
                const int dx = state.mousePoint.x + x;
                const int dy = state.mousePoint.y + y;
                if (dx < 0 || dy < 0 || dx >= kBaseWidth || dy >= kBaseHeight)
                    continue;
                const std::size_t dest = static_cast<std::size_t>(
                    (dy * kBaseWidth + dx) * 4);
                for (int channel = 0; channel < 3; ++channel)
                    if (state.composeFrame.bytes()[dest + channel]
                        != state.mouse.bgra[source + channel]) return false;
                ++checked;
            }
        }
        return checked >= 3;
    };
    SendMessageW(guard.child, WM_MOUSELEAVE, 0, 0);
    if (!render()) return fail("initial hidden shell render failed");
    const std::vector<unsigned char> noCursor = state.composeFrame.bytes();
    move(POINT{575, 260});
    if (!state.backDirty || !render() || state.mousePoint.x != 575
        || state.mousePoint.y != 260 || !cursorMatches())
        return fail("mouse movement did not repaint the original arrow");
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    if (SendMessageW(guard.child, WM_SETCURSOR,
            reinterpret_cast<WPARAM>(guard.child), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE))
            != TRUE || GetCursor() != nullptr)
        return fail("system pointer remains visible over the shell");

    move(POINT{590, 425});
    if (!state.backDirty || !render() || !cursorMatches())
        return fail("second mouse position was not repainted");
    // Both chosen positions avoid lobby controls, so the old arrow footprint
    // must return exactly to the untouched background after a move and leave.
    for (int y = 0; y < state.mouse.height && y + 260 < kBaseHeight; ++y)
        for (int x = 0; x < state.mouse.width && x + 575 < kBaseWidth; ++x) {
            const std::size_t at = static_cast<std::size_t>(
                ((y + 260) * kBaseWidth + x + 575) * 4);
            for (int channel = 0; channel < 3; ++channel)
                if (state.composeFrame.bytes()[at + channel] != noCursor[at + channel])
                    return fail("moving the pointer left pixels at its old position");
        }
    SendMessageW(guard.child, WM_MOUSELEAVE, 0, 0);
    if (state.mouseVisible || !state.backDirty || !render()
        || state.composeFrame.bytes() != noCursor)
        return fail("mouse leave did not clear the painted arrow");

    const auto slider = std::find_if(state.hits.begin(), state.hits.end(),
        [](const HitRegion& hit) { return hit.target.kind == HitKind::CreditsSlider; });
    if (slider == state.hits.end()) return fail("credits slider was not found");
    const POINT sliderPoint{(slider->rect.left + slider->rect.right) / 2,
        (slider->rect.top + slider->rect.bottom) / 2};
    const int sliderX = state.viewLeft + sliderPoint.x * state.viewWidth / kBaseWidth;
    const int sliderY = state.viewTop + sliderPoint.y * state.viewHeight / kBaseHeight;
    SendMessageW(guard.child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sliderX, sliderY));
    move(POINT{sliderPoint.x + 20, sliderPoint.y}, MK_LBUTTON);
    if (state.draggingSlider != HitKind::CreditsSlider || !state.backDirty
        || !render() || state.mousePoint.x != sliderPoint.x + 20 || !cursorMatches())
        return fail("slider drag bypassed pointer movement or repaint");
    SendMessageW(guard.child, WM_LBUTTONUP, 0,
        MAKELPARAM(sliderX + 20, sliderY));
    if (state.draggingSlider != HitKind::None)
        return fail("slider drag did not release");

    const std::array<SkirmishPage, 5> pages{{SkirmishPage::Lobby,
        SkirmishPage::MapSelection, SkirmishPage::RandomMap,
        SkirmishPage::RandomMapPreview, SkirmishPage::RandomMapLoad}};
    for (const SkirmishPage page : pages) {
        state.page = page;
        state.backDirty = true;
        move(POINT{575, 260});
        if (!render() || !cursorMatches())
            return fail("a shell subpage did not paint the original arrow");
    }
    state.page = SkirmishPage::RandomMap;
    state.phase = ShellMenuPhase::Idle;
    updateRandomMapNameWindow(state);
    if (state.randomNameWindow == nullptr)
        return fail("random-map name edit was not created");
    SetCursor(nullptr);
    SendMessageW(state.randomNameWindow, WM_SETCURSOR,
        reinterpret_cast<WPARAM>(state.randomNameWindow),
        MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    if (GetCursor() == nullptr)
        return fail("native name edit lost its pointer");

    state.page = SkirmishPage::Lobby;
    updateRandomMapNameWindow(state);
    if (!MoveWindow(guard.child, 0, 0, 1000, 700, FALSE) || !render())
        return fail("letterboxed shell resize failed");
    if (state.viewLeft <= 0 || state.viewTop <= 0)
        return fail("letterbox fixture did not create margins");
    SendMessageW(guard.child, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    if (state.mouseVisible || !render())
        return fail("pointer remained painted outside the menu viewport");
    move(POINT{575, 260});
    if (!render() || state.mousePoint.x != 575 || state.mousePoint.y != 260
        || !cursorMatches()) return fail("letterbox offset changed the arrow hotspot");

    fs::path screenshot = bmpPath;
    if (screenshot.empty()) screenshot = fs::path(config.localDataRootPath)
        / "runtime_skirmish_ui_smoke.bmp";
    screenshot.replace_filename(screenshot.stem().string() + "_cursor.bmp");
    if (!screenshot.parent_path().empty()) fs::create_directories(screenshot.parent_path());
    if (!writeBgraBmp(pathString(screenshot), state.composeFrame.bytes().data(),
            kBaseWidth, kBaseHeight, error)) return fail(error);

    // Cancel/reentry reuses decoded resources but must discard the old pointer.
    resetMenuForEntry(state, config);
    if (state.mouseVisible || state.mousePoint.x != 0 || state.mousePoint.y != 0)
        return fail("reentry retained the previous pointer location");
    move(POINT{575, 260});
    if (!render() || !cursorMatches())
        return fail("reentered shell could not repaint the arrow");
    std::cout << "Skirmish cursor: original=yes opaquePixels=" << opaquePixels
        << " move=yes leave=yes drag=yes pages=5 letterbox=yes reentry=yes"
        << " nativeEdit=yes\n";
    return true;
}
