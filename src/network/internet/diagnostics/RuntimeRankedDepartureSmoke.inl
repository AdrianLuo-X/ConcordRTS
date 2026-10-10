bool runRuntimeRankedDepartureSmoke(RuntimeWindowState& state, HWND window, std::string& error) {
    struct Recorder final : RuntimeMultiplayerExternalOrderedTransport {
        int& departures; int& results;
        Recorder(int& d, int& r) : departures(d), results(r) {}
        bool pump(RuntimeMultiplayerOrderedTransportPumpStage, int, std::string& e) override { e.clear(); return true; }
        bool reportRankedMatchAbandon(bool& sent, std::string& e) override { ++departures; sent = true; e.clear(); return true; }
        bool reportRankedMatchResult(bool, bool& sent, std::string& e) override { ++results; sent = true; e.clear(); return true; }
        void close() noexcept override {}
    };
    const auto fail = [&](const char* reason) { state.multiplayerBridge = nullptr; error = reason; return false; };
    int departures = 0, results = 0;
    {
        RuntimeMultiplayerRuntimeBridge bridge;
        if (!bridge.adoptExternalOrderedTransport(std::make_unique<Recorder>(departures, results), error)) return false;
        state.multiplayerBridge = &bridge; state.multiplayerLocalSpectator = false;
        state.skirmishMatchFinished = false; state.resultPresentation = {};
        activateRuntimeMenuAction(window, state, RuntimeMenuAction::Exit);
        activateRuntimeMenuAction(window, state, RuntimeMenuAction::CancelExit);
        if (departures || results) return fail("cancel sent a ranked departure");
        activateRuntimeMenuAction(window, state, RuntimeMenuAction::ConfirmExit);
        reportRuntimeRankedDeparture(state);
        if (departures != 1 || results != 0) return fail("explicit resignation must report departure once before local defeat");
        state.multiplayerBridge = nullptr;
    }
    for (int mode = 0; mode < 4; ++mode) {
        auto probe = std::make_unique<RuntimeWindowState>();
        RuntimeMultiplayerRuntimeBridge bridge;
        if (!bridge.adoptExternalOrderedTransport(std::make_unique<Recorder>(departures, results), error)) return false;
        probe->multiplayerBridge = &bridge; probe->multiplayerLocalSpectator = mode == 3;
        probe->skirmishMatchFinished = mode == 1;
        const int priorDepartures = departures, priorResults = results;
        HWND closing = CreateWindowExW(0, L"STATIC", L"Ranked departure probe", WS_POPUP, 0, 0, 32, 32,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!closing) return fail("ranked close fixture window");
        SetWindowLongPtrW(closing, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(probe.get()));
        SetWindowLongPtrW(closing, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(runtimeWindowProc));
        if (mode == 2) DestroyWindow(closing); // production failure path
        else SendMessageW(closing, WM_CLOSE, 0, 0);
        MSG quit{}; while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
        probe->multiplayerBridge = nullptr;
        if (departures != priorDepartures + (mode == 0 ? 1 : 0)
            || results != priorResults + (mode == 1 ? 1 : 0)) return fail("ranked close must distinguish unfinished, final, failed and spectator paths");
    }
    std::cout << "Ranked departure UI: cancel, confirm once, window close, final result, failure and spectator paths passed.\n";
    return true;
}
