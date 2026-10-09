// Included after the campaign navigation actions.
void resetCampaignMenuInput(CampaignMenuState& state) {
    const bool highlighted = state.pressedControl >= 0
        || state.hoveredControl >= 0 || state.focusedControl >= 0;
    state.pressedControl = -1;
    state.hoveredControl = -1;
    state.focusedControl = -1;
    releaseCampaignMapMouseCapture(state);
    if (highlighted && state.window && IsWindow(state.window)) {
        InvalidateRect(state.window, nullptr, FALSE);
    }
}

void activateCampaignControl(CampaignMenuState& state, int index) {
    const auto controls = campaignControls(state);
    if (index < 0 || index >= static_cast<int>(controls.size())) return;
    const CampaignControl control = controls[static_cast<std::size_t>(index)];
    if (!control.enabled) return;
    resetCampaignMenuInput(state);
    using Action = CampaignControlAction;
    switch (control.action) {
    case Action::Back: goBack(state); break;
    case Action::Confirm: activateSelection(state); break;
    case Action::Edition:
    case Action::Campaign:
        state.campaignIndex = control.index;
        chooseFirstUnlockedMission(state);
        if (control.action == Action::Campaign) activateSelection(state);
        break;
    case Action::Difficulty: state.difficultyIndex = control.index; break;
    case Action::Previous: moveSelection(state, -1); break;
    case Action::Next: moveSelection(state, 1); break;
    case Action::Row:
        if (state.page == CampaignPage::Saves) state.saveIndex = control.index;
        else state.missionIndex = control.index;
        break;
    }
    if (!state.closing) {
        refreshMissionBackground(state);
        refreshCampaignMapPresentation(state);
    }
}

LRESULT CALLBACK campaignMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CampaignMenuState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<CampaignMenuState*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    const auto mousePoint = [&]() {
        RECT client{};
        GetClientRect(window, &client);
        return campaignShellPoint(client, static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
    };
    switch (message) {
    case WM_CREATE: SetTimer(window, 1, 50, nullptr); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: {
        if (wParam != 1 || state->page != CampaignPage::Missions) return 0;
        const bool animatedBefore = campaignShowsMap(*state);
        updateCampaignMapPresentation(*state);
        if (animatedBefore || campaignShowsMap(*state)) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SIZE:
        resetCampaignMenuInput(*state);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        drawCampaignMenu(*state, dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if ((lParam & (1LL << 30)) == 0) {
                resetCampaignMenuInput(*state);
                goBack(*state);
            }
        } else if (wParam == VK_TAB) {
            const auto controls = campaignControls(*state);
            const int direction = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            for (std::size_t tries = 0; tries < controls.size(); ++tries) {
                state->focusedControl = (state->focusedControl + direction
                    + static_cast<int>(controls.size())) % static_cast<int>(controls.size());
                const auto& control = controls[static_cast<std::size_t>(state->focusedControl)];
                if (control.enabled && !control.label.empty()) break;
            }
        } else if (wParam == VK_UP || wParam == VK_DOWN) {
            resetCampaignMenuInput(*state);
            moveSelection(*state, wParam == VK_UP ? -1 : 1);
        } else if ((wParam == VK_LEFT || wParam == VK_RIGHT)
            && (state->page == CampaignPage::Campaigns || state->page == CampaignPage::Difficulty)) {
            resetCampaignMenuInput(*state);
            state->difficultyIndex = std::clamp(state->difficultyIndex
                + (wParam == VK_LEFT ? -1 : 1), 0, static_cast<int>(difficultyNames().size()) - 1);
        } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if ((lParam & (1LL << 30)) == 0) {
                if (state->focusedControl >= 0) activateCampaignControl(*state, state->focusedControl);
                else { resetCampaignMenuInput(*state); activateSelection(*state); }
            }
        } else return DefWindowProcW(window, message, wParam, lParam);
        if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        resetCampaignMenuInput(*state);
        moveSelection(*state, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        const POINT point = mousePoint();
        const int previousControl = state->hoveredControl;
        const int previousMapColor = state->mapPresentation.hoverColor;
        state->hoveredControl = campaignControlAt(*state, point);
        if (campaignShowsMap(*state)) {
            const POINT map = campaignMapPoint(point);
            updateCampaignMapHover(*state, map.x, map.y);
        }
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(state->hoveredControl >= 0
            || (campaignShowsMap(*state) && state->mapPresentation.hoverColor != 0)
                ? 32649 : 32512)));
        if (previousControl != state->hoveredControl
            || previousMapColor != state->mapPresentation.hoverColor) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        const bool highlighted = state->hoveredControl >= 0 || state->mapPresentation.hoverColor != 0;
        state->hoveredControl = -1;
        updateCampaignMapHover(*state, -1, -1);
        if (highlighted) InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(window);
        state->focusedControl = -1;
        const POINT point = mousePoint();
        state->pressedControl = campaignControlAt(*state, point);
        const POINT map = campaignMapPoint(point);
        state->mapPresentation.pressedColor = campaignShowsMap(*state)
            ? campaignMapColorAtBasePoint(*state, map.x, map.y) : 0;
        if (state->pressedControl >= 0 || state->mapPresentation.pressedColor != 0) SetCapture(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        const POINT point = mousePoint();
        const int pressed = state->pressedControl;
        const int color = state->mapPresentation.pressedColor;
        state->pressedControl = -1;
        state->mapPresentation.pressedColor = 0;
        if (GetCapture() == window) ReleaseCapture();
        if (pressed >= 0 && campaignControlAt(*state, point) == pressed) {
            activateCampaignControl(*state, pressed);
        } else if (pressed < 0 && color != 0 && campaignShowsMap(*state)) {
            const POINT map = campaignMapPoint(point);
            if (campaignMapColorAtBasePoint(*state, map.x, map.y) == color) {
                resetCampaignMenuInput(*state);
                confirmCampaignMapChoice(*state, color);
            }
        }
        if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED: {
        const bool pressed = state->pressedControl >= 0;
        state->pressedControl = -1;
        state->mapPresentation.pressedColor = 0;
        if (pressed) InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        resetCampaignMenuInput(*state);
        return 0;
    case WM_CLOSE: closeMenu(*state, false); return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        resetCampaignMenuInput(*state);
        stopCampaignVoice();
        stopCampaignMapEffect();
        state->window = nullptr;
        return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND createCampaignMenuWindow(CampaignMenuState& state, HWND hostWindow);

bool runCampaignMenuNavigationSmoke(CampaignMenuState& state, std::string& error) {
    const int savedCampaign = state.campaignIndex;
    const int savedMission = state.missionIndex;
    const int savedDifficulty = state.difficultyIndex;
    const CampaignPage savedPage = state.page;
    for (const POINT size : std::array<POINT, 3>{{{800, 600}, {1280, 720}, {1024, 768}}}) {
        HWND host = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"STATIC", L"Campaign navigation smoke", WS_POPUP,
            -32000, -32000, size.x, size.y, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        state.closing = false;
        state.accepted = false;
        state.page = CampaignPage::Campaigns;
        HWND window = createCampaignMenuWindow(state, host);
        if (!host || !window) { error = "navigation smoke could not create hosted window"; return false; }
        const RECT client{0, 0, size.x, size.y};
        const RECT fit = campaignShellClientRect(client);
        const auto pointParam = [&](int x, int y) {
            return MAKELPARAM(fit.left + (x * (fit.right - fit.left) + 799) / 800,
                fit.top + (y * (fit.bottom - fit.top) + 599) / 600);
        };
        const auto click = [&](int x, int y) {
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, pointParam(x, y));
            SendMessageW(window, WM_LBUTTONUP, 0, pointParam(x, y));
        };
        bool valid = true;
        const auto checkRepaint = [&](bool expected, const char* step) {
            const bool pending = GetUpdateRect(window, nullptr, FALSE) != FALSE;
            if (pending != expected && error.empty()) error = std::string("repaint audit: ") + step;
            valid = valid && pending == expected;
        };
        // Windows discards invalidations for hidden windows. Make the test
        // child visible outside the desktop without activating its host.
        ShowWindow(host, SW_SHOWNOACTIVATE);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        // An idle landing page must not request another repaint on every
        // 50ms timer tick. Hovering within a button must stay idle as well.
        InvalidateRect(window, nullptr, FALSE);
        checkRepaint(true, "initial invalidation was not observable");
        ValidateRect(window, nullptr);
        for (int tick = 0; tick < 20; ++tick) SendMessageW(window, WM_TIMER, 1, 0);
        checkRepaint(false, "static timer requested repaint");
        SendMessageW(window, WM_MOUSEMOVE, 0, pointParam(700, 555));
        checkRepaint(true, "entering a control did not repaint");
        ValidateRect(window, nullptr);
        SendMessageW(window, WM_MOUSEMOVE, 0, pointParam(701, 555));
        checkRepaint(false, "moving within a control repainted");
        SendMessageW(window, WM_MOUSEMOVE, 0, pointParam(50, 300));
        checkRepaint(true, "leaving a control did not repaint");
        ValidateRect(window, nullptr);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, pointParam(700, 555));
        ValidateRect(window, nullptr);
        SendMessageW(window, WM_CANCELMODE, 0, 0);
        checkRepaint(true, "cancelled press left a stale highlight");
        valid = valid && state.pressedControl == -1 && !state.closing;
        ShowWindow(host, SW_HIDE);
        click(50, 300);
        valid = valid && state.page == CampaignPage::Campaigns && !state.closing;
        // Release-only and drag-out gestures may not activate a button.
        SendMessageW(window, WM_LBUTTONUP, 0, pointParam(700, 555));
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, pointParam(700, 555));
        SendMessageW(window, WM_LBUTTONUP, 0, pointParam(50, 300));
        valid = valid && !state.closing;
        click(340, 280);
        valid = valid && state.difficultyIndex == 2 && state.page == CampaignPage::Campaigns;
        click(300, 100);
        valid = valid && state.page == CampaignPage::Missions;
        click(50, 550);
        valid = valid && state.page == CampaignPage::Missions;
        click(700, 555);
        valid = valid && state.page == CampaignPage::Campaigns && IsWindow(host);
        for (const CampaignPage page : {CampaignPage::Briefing, CampaignPage::Difficulty, CampaignPage::Missions}) {
            state.page = page;
            refreshCampaignMapPresentation(state);
            click(700, 555);
            const CampaignPage expected = page == CampaignPage::Briefing ? CampaignPage::Difficulty
                : page == CampaignPage::Difficulty ? CampaignPage::Missions : CampaignPage::Campaigns;
            valid = valid && state.page == expected && IsWindow(window);
        }
        state.page = CampaignPage::Briefing;
        SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 1);
        SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, (1LL << 30) | 1);
        valid = valid && state.page == CampaignPage::Difficulty;
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, (1LL << 30) | 1);
        valid = valid && state.page == CampaignPage::Difficulty;
        state.page = CampaignPage::Campaigns;
        resetCampaignMenuInput(state);
        SendMessageW(window, WM_KEYDOWN, VK_TAB, 1);
        SendMessageW(window, WM_KEYDOWN, VK_RETURN, 1);
        valid = valid && state.closing && !state.accepted && !IsWindow(window) && IsWindow(host);
        if (IsWindow(window)) DestroyWindow(window);
        state.closing = false;
        state.page = CampaignPage::Saves;
        window = createCampaignMenuWindow(state, host);
        valid = valid && window != nullptr;
        click(50, 550);
        valid = valid && state.page == CampaignPage::Saves && !state.closing;
        click(700, 555);
        valid = valid && state.closing && !state.accepted && !IsWindow(window) && IsWindow(host);
        if (IsWindow(window)) DestroyWindow(window);
        DestroyWindow(host);
        if (!valid) {
            if (error.empty()) error = "hosted mouse/keyboard navigation at " + std::to_string(size.x) + "x" + std::to_string(size.y);
            return false;
        }
    }
    state.window = nullptr;
    state.closing = false;
    state.accepted = false;
    state.campaignIndex = savedCampaign;
    state.missionIndex = savedMission;
    state.difficultyIndex = savedDifficulty;
    state.page = savedPage;
    resetCampaignMenuInput(state);
    refreshCampaignMapPresentation(state);
    return true;
}
