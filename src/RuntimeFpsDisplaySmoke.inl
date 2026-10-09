// Included after runtimeWindowProc. This is a presentation-only contract: the
// visible sample uses the real clock and never advances a simulation tick.
namespace RuntimeFpsDisplaySmokeDetail {
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    bool create(int w, int h) {
        width = w; height = h;
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* storage = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
        if (!dc || !bitmap || !storage) return false;
        pixels = static_cast<std::uint32_t*>(storage);
        previous = SelectObject(dc, bitmap);
        return previous && previous != HGDI_ERROR;
    }
    ~Surface() {
        if (dc && previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    std::vector<std::uint32_t> read() const {
        GdiFlush();
        std::vector<std::uint32_t> result(pixels, pixels + width * height);
        for (auto& pixel : result) pixel &= 0x00ffffffU;
        return result;
    }
    void clear(std::uint32_t rgb) {
        GdiFlush();
        std::fill(pixels, pixels + width * height, rgb);
    }
};

struct DcState {
    COLORREF text = 0, background = 0, dcBrush = 0, dcPen = 0;
    int backgroundMode = 0, mapMode = 0;
    UINT textAlign = 0;
    HGDIOBJ font = nullptr, pen = nullptr, brush = nullptr;
    POINT origin{}, current{};
    RECT clip{};
    explicit DcState(HDC dc) {
        text = GetTextColor(dc); background = GetBkColor(dc);
        dcBrush = GetDCBrushColor(dc); dcPen = GetDCPenColor(dc);
        backgroundMode = GetBkMode(dc); mapMode = GetMapMode(dc);
        textAlign = GetTextAlign(dc);
        font = GetCurrentObject(dc, OBJ_FONT); pen = GetCurrentObject(dc, OBJ_PEN);
        brush = GetCurrentObject(dc, OBJ_BRUSH);
        GetViewportOrgEx(dc, &origin); GetCurrentPositionEx(dc, &current);
        GetClipBox(dc, &clip);
    }
    bool equals(const DcState& other) const {
        return text == other.text && background == other.background
            && dcBrush == other.dcBrush && dcPen == other.dcPen
            && backgroundMode == other.backgroundMode && mapMode == other.mapMode
            && textAlign == other.textAlign && font == other.font && pen == other.pen
            && brush == other.brush && origin.x == other.origin.x && origin.y == other.origin.y
            && current.x == other.current.x && current.y == other.current.y
            && EqualRect(&clip, &other.clip);
    }
};
} // namespace RuntimeFpsDisplaySmokeDetail

bool runRuntimeFpsDisplaySmoke(RuntimeWindowState& state, std::string& error) {
    using namespace RuntimeFpsDisplaySmokeDetail;
    using Clock = std::chrono::steady_clock;
    std::cout << std::unitbuf;
    std::string stage = "sampling";
    const auto fail = [&](const std::string& reason) {
        error = "FPS display " + stage + ": " + reason;
        return false;
    };
    const char* output = std::getenv("CONCORD_FPS_DISPLAY_OUTPUT");
    const auto root = output && *output ? std::filesystem::path(output)
        : std::filesystem::path(state.config.localDataRootPath).parent_path()
            / "build/smoke/feature-set-19-complete/08-fps-display/fps";
    std::error_code fsError;
    std::filesystem::create_directories(root, fsError);
    if (fsError) return fail("cannot create evidence directory: " + fsError.message());
    std::ofstream csv(root / "fps-display.csv");
    if (!csv) return fail("cannot open evidence CSV");
    csv << "case,paints,renders,fps,lower,upper,pass\n";
    int mathChecks = 0, eligibilityChecks = 0, hiddenPaints = 0, hudChecks = 0;
    const auto closeEnough = [](double actual, double expected) {
        return std::isfinite(actual) && std::abs(actual - expected) < 1.0e-8;
    };
    RuntimeFpsDisplayMeter meter;
    if (meter.hasSample() || !closeEnough(meter.framesPerSecond(), 0.0)
        || runtimeFpsDisplayText(meter) != L"FPS: --") return fail("fresh meter is not waiting");
    ++mathChecks;
    meter.recordPresentedFrame(10.0);
    if (meter.hasSample()) return fail("first frame invented a rate");
    for (int frame = 1; frame <= 60; ++frame)
        meter.recordPresentedFrame(10.0 + static_cast<double>(frame) / 60.0);
    if (!meter.hasSample() || !closeEnough(meter.framesPerSecond(), 60.0))
        return fail("60 completed frame intervals over one real second");
    ++mathChecks;
    meter.reset();
    if (meter.hasSample() || !closeEnough(meter.framesPerSecond(), 0.0)) return fail("reset retained a sample");
    ++mathChecks;
    for (double stamp : {20.0, 20.25, 20.5, 20.75, 21.25}) meter.recordPresentedFrame(stamp);
    if (!meter.hasSample() || !closeEnough(meter.framesPerSecond(), 3.2))
        return fail("oversized sample window was treated as exactly one second");
    ++mathChecks;
    meter.recordPresentedFrame(19.0);
    if (meter.hasSample() || !closeEnough(meter.framesPerSecond(), 0.0)) return fail("clock rollback retained stale FPS");
    ++mathChecks;
    for (double invalid : {std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        meter.recordPresentedFrame(invalid);
        if (meter.hasSample() || !closeEnough(meter.framesPerSecond(), 0.0)) return fail("non-finite clock published a rate");
        ++mathChecks;
    }
    meter.reset(); meter.recordPresentedFrame(30.0); meter.recordPresentedFrame(30.0);
    if (meter.hasSample() || !std::isfinite(meter.framesPerSecond())) return fail("zero duration divided by zero");
    ++mathChecks;
    meter.reset();
    for (int unused = 0; unused < 120; ++unused) {
        // No presentation event: simulation/loop frequency is not an input.
        if (meter.hasSample() || meter.framesPerSecond() != 0.0) return fail("no-presentation sample changed");
    }
    ++mathChecks;
    for (int bits = 0; bits < 32; ++bits) {
        const bool visible = (bits & 1) != 0, minimized = (bits & 2) != 0;
        const bool client = (bits & 4) != 0, dirty = (bits & 8) != 0, submitted = (bits & 16) != 0;
        const bool expected = visible && !minimized && client && dirty && submitted;
        if (runtimeFpsDisplayShouldCountFrame(visible, minimized, client, dirty, submitted) != expected)
            return fail("successful visible presentation eligibility bits=" + std::to_string(bits));
        ++eligibilityChecks;
    }
    csv << "deterministic-math," << mathChecks << ",0,0,0,0,1\n";
    csv << "eligible-presentations," << eligibilityChecks << ",0,0,0,0,1\n";

    stage = "HUD pixels";
    Surface surface;
    if (!surface.create(1280, 720)) return fail("DIB allocation");
    constexpr std::uint32_t background = 0x0031485bU;
    const RECT client800{0, 0, 800, 600};
    const auto rateMeter = [](int rate) {
        RuntimeFpsDisplayMeter result;
        result.recordPresentedFrame(100.0);
        for (int frame = 1; frame <= rate; ++frame)
            result.recordPresentedFrame(100.0 + static_cast<double>(frame) / rate);
        return result;
    };
    const auto longMeter = rateMeter(144);
    const auto shortMeter = rateMeter(3);
    if (runtimeFpsDisplayText(longMeter) != L"FPS: 144"
        || runtimeFpsDisplayText(shortMeter) != L"FPS: 3") return fail("display text differs from measured numeric value");
    SetTextColor(surface.dc, RGB(17, 28, 39)); SetBkColor(surface.dc, RGB(67, 78, 89));
    SetBkMode(surface.dc, OPAQUE); SetTextAlign(surface.dc, TA_LEFT | TA_TOP);
    SelectObject(surface.dc, GetStockObject(SYSTEM_FIXED_FONT));
    MoveToEx(surface.dc, 7, 9, nullptr);
    const DcState dcBefore(surface.dc);
    surface.clear(background);
    drawRuntimeFpsDisplay(surface.dc, client800, longMeter);
    const auto longPixels = surface.read();
    if (!dcBefore.equals(DcState(surface.dc))) return fail("HUD changed the caller's GDI state");
    drawRuntimeFpsDisplay(surface.dc, client800, shortMeter);
    const auto overwrittenShort = surface.read();
    surface.clear(background);
    drawRuntimeFpsDisplay(surface.dc, client800, shortMeter);
    const auto freshShort = surface.read();
    if (overwrittenShort != freshShort || longPixels == freshShort)
        return fail("numeric glyphs absent or shorter number retained old glyphs");
    const RECT hud = runtimeFpsDisplayRect(client800);
    if (IsRectEmpty(&hud) || hud.left < 0 || hud.top < 0
        || hud.right > runtimeMapViewport(client800).right || hud.bottom > client800.bottom)
        return fail("normal HUD bounds outside map/client");
    int changed = 0;
    for (int y = 0; y < surface.height; ++y) for (int x = 0; x < surface.width; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * surface.width + x;
        if (freshShort[index] != background) {
            ++changed;
            if (x < hud.left || x >= hud.right || y < hud.top || y >= hud.bottom)
                return fail("HUD wrote outside its fixed rectangle");
        }
    }
    if (changed == 0) return fail("HUD drew no pixels");
    ++hudChecks;
    if (!writeRuntimeSmokeBmp(pathString(root / "hud-144.bmp"), longPixels.data(), surface.width, surface.height, error)
        || !writeRuntimeSmokeBmp(pathString(root / "hud-3.bmp"), freshShort.data(), surface.width, surface.height, error)) return false;
    surface.clear(background);
    drawRuntimeFpsDisplay(surface.dc, client800, RuntimeFpsDisplayMeter{});
    const auto waitingPixels = surface.read();
    if (waitingPixels == freshShort || waitingPixels == longPixels) return fail("waiting glyphs are not distinct");
    if (!writeRuntimeSmokeBmp(pathString(root / "hud-wait.bmp"), waitingPixels.data(), surface.width, surface.height, error)) return false;
    for (const RECT tinyClient : {RECT{0, 0, 0, 0}, RECT{0, 0, 32, 24}, RECT{0, 0, 96, 22}}) {
        surface.clear(background);
        drawRuntimeFpsDisplay(surface.dc, tinyClient, shortMeter);
        const RECT smallHud = runtimeFpsDisplayRect(tinyClient);
        if (!IsRectEmpty(&smallHud)) return fail("tiny HUD should be suppressed");
        const auto pixels = surface.read();
        if (!std::all_of(pixels.begin(), pixels.end(), [&](std::uint32_t p) { return p == background; })
            || !dcBefore.equals(DcState(surface.dc))) return fail("tiny client changed pixels or DC state");
        ++hudChecks;
    }
    csv << "hud-pixels," << hudChecks << ",0,0,0,0,1\n";

    stage = "hidden WM_PAINT";
    if (!state.world.getSummary().loaded || state.skirmishMatchFinished) return fail("fresh loaded world required");
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeVoiceVolume = state.runtimeSoundVolume = 0;
    state.runtimeMusicEnabled = state.runtimeSoundsEnabled = state.runtimeSoundEffectsEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimeMenuOpen = false;
    state.playerLocalCommands.cameraFollow = false;
    state.mouseClientPointValid = false;
    const auto worldHash = state.world.deterministicSimulationStateHash();
    const int worldTick = state.tick;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordFpsDisplaySmoke";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = runtimeWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return fail("window class registration");
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"ConcordRTS FPS verification",
        WS_POPUP, workArea.left + 16, workArea.top + 16, 800, 600, nullptr, nullptr, instance, nullptr);
    if (!window) { UnregisterClassW(className, instance); return fail("window creation"); }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    struct Cleanup {
        HWND window; HINSTANCE instance; const wchar_t* name; RuntimeWindowState& state;
        ~Cleanup() {
            ShowWindow(window, SW_HIDE);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
            DestroyWindow(window); UnregisterClassW(name, instance);
            releaseRuntimeBackBuffer(state.backBuffer);
            releaseRuntimeBackBuffer(state.sidebarStaticBuffer);
            releaseRuntimeBackBuffer(state.radarTerrainBuffer);
        }
    } cleanup{window, instance, className, state};
    state.fpsDisplayMeter.reset();
    const auto bufferPixels = [&]() {
        GdiFlush();
        const auto* first = static_cast<const std::uint32_t*>(state.backBuffer.pixels);
        if (!first) return std::vector<std::uint32_t>{};
        std::vector<std::uint32_t> result(first, first + state.backBuffer.width * state.backBuffer.height);
        for (auto& pixel : result) pixel &= 0x00ffffffU;
        return result;
    };
    const auto hiddenPaint = [&](int width, int height, bool full) {
        const int beforePaint = state.paintedFramesThisSecond, beforeRender = state.renderedFramesThisSecond;
        InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0); GdiFlush();
        ++hiddenPaints;
        if (IsWindowVisible(window) || state.fpsDisplayMeter.hasSample()
            || runtimeFpsDisplayText(state.fpsDisplayMeter) != L"FPS: --") return fail("hidden draw invented visible FPS");
        if (state.paintedFramesThisSecond != beforePaint + 1
            || state.renderedFramesThisSecond != beforeRender + (full ? 1 : 0)
            || state.backBuffer.width != width || state.backBuffer.height != height
            || !state.backBuffer.pixels || !state.renderStats.rendered)
            return fail("full/cache path or DIB size mismatch");
        csv << (full ? "hidden-full" : "hidden-cache") << ",1," << (full ? 1 : 0) << ",0,0,0,1\n";
        return true;
    };
    state.repaintRequested = true;
    if (!hiddenPaint(800, 600, true)) return false;
    const auto originalCamera = state.camera;
    const auto originalPixels = bufferPixels();
    if (!hiddenPaint(800, 600, false) || bufferPixels() != originalPixels) return fail("cached draw changed clean backbuffer");
    if (!SetWindowPos(window, nullptr, 0, 0, 1280, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
        || !hiddenPaint(1280, 720, true)) return false;
    const auto largePixels = bufferPixels();
    if (!hiddenPaint(1280, 720, false) || bufferPixels() != largePixels) return fail("large cached draw changed clean backbuffer");
    state.camera = originalCamera; invalidateRuntimeObjectProjectionCache(state);
    if (!SetWindowPos(window, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
        || !hiddenPaint(800, 600, true)) return false;
    if (bufferPixels() != originalPixels) return fail("resize roundtrip changed cached scene");

    stage = "visible real-clock WM_PAINT";
    SetWindowPos(window, HWND_TOPMOST, workArea.left + 16, workArea.top + 16, 800, 600,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!IsWindowVisible(window) || IsIconic(window)) return fail("real visible presentation window unavailable");
    // Ignore startup exposure; only subsequent synchronous production paints
    // contribute to this independent clock bracket. No injected clock is used.
    UpdateWindow(window); ValidateRect(window, nullptr); state.fpsDisplayMeter.reset();
    const auto cleanBeforeVisible = bufferPixels();
    const int paintsBefore = state.paintedFramesThisSecond;
    const int rendersBefore = state.renderedFramesThisSecond;
    Clock::time_point firstBefore{}, firstAfter{}, finalBefore{}, finalAfter{};
    int presented = 0;
    const auto timeout = Clock::now() + std::chrono::seconds(5);
    while (!state.fpsDisplayMeter.hasSample() && Clock::now() < timeout) {
        InvalidateRect(window, nullptr, FALSE);
        const auto before = Clock::now();
        SendMessageW(window, WM_PAINT, 0, 0);
        const auto after = Clock::now();
        if (presented == 0) { firstBefore = before; firstAfter = after; }
        finalBefore = before; finalAfter = after; ++presented;
        if (!state.fpsDisplayMeter.hasSample()) std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    if (!state.fpsDisplayMeter.hasSample() || presented < 2) return fail("visible successful submissions never produced a sample");
    const double longest = std::chrono::duration<double>(finalAfter - firstBefore).count();
    const double shortest = std::chrono::duration<double>(finalBefore - firstAfter).count();
    if (shortest <= 0.0) return fail("clock bracket is invalid");
    const double lower = (presented - 1) / longest, upper = (presented - 1) / shortest;
    const double measured = state.fpsDisplayMeter.framesPerSecond();
    if (!std::isfinite(measured) || measured < lower - 1.0e-6 || measured > upper + 1.0e-6
        || state.paintedFramesThisSecond != paintsBefore + presented
        || state.renderedFramesThisSecond != rendersBefore
        || bufferPixels() != cleanBeforeVisible || state.tick != worldTick
        || state.world.deterministicSimulationStateHash() != worldHash)
        return fail("visible FPS not bracketed by actual submissions, or HUD changed scene/simulation");
    csv << "visible-real-clock," << presented << ",0," << std::setprecision(12) << measured
        << ',' << lower << ',' << upper << ",1\n";
    Surface capture;
    if (!capture.create(800, 600)) return fail("screenshot DIB allocation");
    HDC actualDc = GetDC(window);
    const BOOL copied = actualDc && BitBlt(capture.dc, 0, 0, 800, 600, actualDc, 0, 0, SRCCOPY);
    if (actualDc) ReleaseDC(window, actualDc);
    if (!copied) return fail("real window screenshot failed");
    const auto captured = capture.read();
    if (!writeRuntimeSmokeBmp(pathString(root / "visible-window.bmp"), captured.data(), 800, 600, error)
        || !writeRuntimeSmokeBmp(pathString(root / "clean-backbuffer.bmp"), cleanBeforeVisible.data(), 800, 600, error)) return false;
    int hudVisibleDifference = 0;
    for (int y = hud.top; y < hud.bottom; ++y) for (int x = hud.left; x < hud.right; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * 800 + x;
        if (captured[index] != cleanBeforeVisible[index]) ++hudVisibleDifference;
    }
    if (!hudVisibleDifference) return fail("production window did not display HUD over clean backbuffer");

    stage = "cursor crossing HUD boundary";
    // Choose a genuine empty map point at the panel edge, so adding the mouse
    // does not also add an unrelated object's hover selection decorations.
    bool foundCursorPoint = false;
    for (LONG inset : {6L, 10L, 2L}) for (LONG offset : {3L, 8L, 12L}) {
        if (foundCursorPoint) continue;
        const POINT point{hud.right - inset, hud.top + offset};
        if (runtimeCursorObjectAtPoint(state, runtimeMapViewport(client800), point) != nullptr) continue;
        state.mouseClientPointValid = true; state.mouseClientPoint = point;
        if (runtimeMouseCursorFrameForPoint(state, client800, point).has_value()) foundCursorPoint = true;
    }
    if (!foundCursorPoint) return fail("no empty map point crossing HUD edge");
    Surface cursorExpected;
    if (!cursorExpected.create(800, 600)) return fail("cursor expected DIB");
    // Independent composition order: the clean scene, HUD, then exactly one
    // ordinary (unclipped) cursor. The expected path never uses the new clipped
    // cursor redraw that the production final-presentation path is testing.
    std::copy(cleanBeforeVisible.begin(), cleanBeforeVisible.end(), cursorExpected.pixels);
    drawRuntimeFpsDisplay(cursorExpected.dc, client800, state.fpsDisplayMeter);
    const auto panelOnly = cursorExpected.read();
    drawRuntimeMouseCursorOverlay(cursorExpected.dc, client800, state);
    const auto expectedCursorPixels = cursorExpected.read();
    int cursorInside = 0, cursorOutside = 0;
    for (int y = 0; y < 600; ++y) for (int x = 0; x < 800; ++x) {
        const std::size_t index = static_cast<std::size_t>(y) * 800 + x;
        if (expectedCursorPixels[index] == panelOnly[index]) continue;
        if (x >= hud.left && x < hud.right && y >= hud.top && y < hud.bottom) ++cursorInside;
        else ++cursorOutside;
    }
    if (!cursorInside || !cursorOutside) return fail("cursor lacks real pixels on both sides of HUD boundary");
    state.repaintRequested = true;
    InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0); GdiFlush();
    actualDc = GetDC(window);
    const BOOL cursorCopied = actualDc && BitBlt(capture.dc, 0, 0, 800, 600, actualDc, 0, 0, SRCCOPY);
    if (actualDc) ReleaseDC(window, actualDc);
    if (!cursorCopied) return fail("cursor screenshot failed");
    const auto cursorCaptured = capture.read();
    // ClearType can round a channel differently on a 32-bit memory DIB and
    // the actual window DC. Preserve the real production image above, then
    // independently compose on the same window device: clean scene -> HUD ->
    // one ordinary, UNCLIPPED cursor. No WM_PAINT, meter update, or new clipped
    // production cursor path participates in this expected image. Readback
    // still requires exact RGB equality over the complete 800x600 picture.
    GdiFlush();
    std::copy(cleanBeforeVisible.begin(), cleanBeforeVisible.end(), cursorExpected.pixels);
    actualDc = GetDC(window);
    if (!actualDc) return fail("independent window composition DC");
    const BOOL expectedBaseCopied = BitBlt(actualDc, 0, 0, 800, 600,
        cursorExpected.dc, 0, 0, SRCCOPY);
    if (expectedBaseCopied) {
        // Use the label of the completed real paint, including any new sample
        // that the cursor paint itself published on a slower machine.
        drawRuntimeFpsDisplay(actualDc, client800, state.fpsDisplayMeter);
        drawRuntimeMouseCursorOverlay(actualDc, client800, state);
    }
    const BOOL expectedReadback = expectedBaseCopied && BitBlt(cursorExpected.dc,
        0, 0, 800, 600, actualDc, 0, 0, SRCCOPY);
    ReleaseDC(window, actualDc);
    if (!expectedReadback) return fail("independent window composition readback");
    const auto finalCursorExpected = cursorExpected.read();
    if (!writeRuntimeSmokeBmp(pathString(root / "cursor-actual.bmp"), cursorCaptured.data(), 800, 600, error)
        || !writeRuntimeSmokeBmp(pathString(root / "cursor-expected.bmp"), finalCursorExpected.data(), 800, 600, error)) return false;
    std::size_t cursorMismatches = 0;
    for (std::size_t pixel = 0; pixel < finalCursorExpected.size(); ++pixel)
        if (cursorCaptured[pixel] != finalCursorExpected[pixel]) ++cursorMismatches;
    if (cursorMismatches) return fail("HUD/cursor independent full image mismatch=" + std::to_string(cursorMismatches));
    csv << "cursor-crossing," << cursorInside << ',' << cursorOutside << ",0,0,0,1\n";

    stage = "paused menu and visibility lifecycle";
    state.mouseClientPointValid = false;
    state.runtimeMenuOpen = true;
    state.runtimeMenuPage = RuntimeMenuPage::Main;
    state.repaintRequested = true;
    const int pausePaintsBefore = state.paintedFramesThisSecond;
    const int pauseRendersBefore = state.renderedFramesThisSecond;
    InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
    const auto pausedScene = bufferPixels();
    InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
    if (!state.fpsDisplayMeter.hasSample() || state.tick != worldTick
        || state.paintedFramesThisSecond != pausePaintsBefore + 2
        || state.renderedFramesThisSecond != pauseRendersBefore + 1
        || bufferPixels() != pausedScene) return fail("paused full/cache presentation froze or changed scene");
    csv << "paused-full-cache,2,1," << state.fpsDisplayMeter.framesPerSecond() << ",0,0,1\n";
    // Real hidden and empty-dirty dispatches may reuse the scene but must not
    // create a visible sample, even when repeatedly called at the same tick.
    ShowWindow(window, SW_HIDE);
    if (state.fpsDisplayMeter.hasSample() || state.fpsDisplayMeter.framesPerSecond() != 0.0)
        return fail("WM_SHOWWINDOW hide did not clear the existing real sample");
    for (int n = 0; n < 3; ++n) {
        InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
    }
    ValidateRect(window, nullptr); SendMessageW(window, WM_PAINT, 0, 0);
    if (state.fpsDisplayMeter.hasSample() || state.tick != worldTick
        || state.world.deterministicSimulationStateHash() != worldHash)
        return fail("hidden/reset sample or world changed");
    csv << "post-visible-reset-hidden,4,0,0,0,0,1\n";
    ShowWindow(window, SW_SHOWNOACTIVATE);
    InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
    if (state.fpsDisplayMeter.hasSample() || runtimeFpsDisplayText(state.fpsDisplayMeter) != L"FPS: --")
        return fail("show reused a pre-hide sample before a new interval");
    // A second short real-time sample proves the paused cache path continues
    // measuring, and gives minimize a genuine sample to invalidate.
    const auto pauseDeadline = Clock::now() + std::chrono::seconds(5);
    int pausedPresentations = 1;
    while (!state.fpsDisplayMeter.hasSample() && Clock::now() < pauseDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
        ++pausedPresentations;
    }
    if (!state.fpsDisplayMeter.hasSample() || state.fpsDisplayMeter.framesPerSecond() <= 0.0
        || state.tick != worldTick || bufferPixels() != pausedScene)
        return fail("paused cached presentations never acquired a new real sample");
    csv << "paused-real-sample," << pausedPresentations << ",0," << state.fpsDisplayMeter.framesPerSecond() << ",0,0,1\n";
    ShowWindow(window, SW_MINIMIZE);
    if (!IsIconic(window) || state.fpsDisplayMeter.hasSample() || state.fpsDisplayMeter.framesPerSecond() != 0.0)
        return fail("minimize did not clear an actual visible sample");
    ShowWindow(window, SW_RESTORE);
    InvalidateRect(window, nullptr, FALSE); SendMessageW(window, WM_PAINT, 0, 0);
    if (IsIconic(window) || state.fpsDisplayMeter.hasSample()
        || runtimeFpsDisplayText(state.fpsDisplayMeter) != L"FPS: --")
        return fail("restored window did not begin with an unsampled interval");
    if (state.tick != worldTick || state.world.deterministicSimulationStateHash() != worldHash)
        return fail("presentation lifecycle modified simulation state");
    csv << "minimize-restore,1,0,0,0,0,1\n";
    std::cout << "FPS display smoke: math=" << mathChecks << " eligibility=" << eligibilityChecks
        << " hud=" << hudChecks << " hiddenPaints=" << hiddenPaints
        << " visiblePaints=" << presented << " actualFPS=" << measured
        << " independentRange=[" << lower << ',' << upper << "] hudPixels=" << hudVisibleDifference
        << " cursorInside=" << cursorInside << " cursorOutside=" << cursorOutside
        << " pausedPresentations=" << pausedPresentations
        << " fixedTick=" << worldTick << " worldUnchanged=yes backbufferUnchanged=yes\n";
    return true;
}
