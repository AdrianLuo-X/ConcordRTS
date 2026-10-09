#include <cstdlib>

namespace {
struct Issue23VideoSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    unsigned char* pixels = nullptr;
    Issue23VideoSurface() {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = kWidth;
        info.bmiHeader.biHeight = -kHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* storage = nullptr;
        if (dc != nullptr) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
        pixels = static_cast<unsigned char*>(storage);
        if (bitmap != nullptr) previous = SelectObject(dc, bitmap);
    }
    ~Issue23VideoSurface() {
        if (dc && previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    bool valid() const { return dc && bitmap && pixels && previous && previous != HGDI_ERROR; }
};

std::string issue23CsvText(const std::string& value) {
    std::string result = "\"";
    for (const char ch : value) { if (ch == '"') result += '"'; result += ch; }
    return result + '"';
}

struct Issue23VideoDriver {
    int caseId = 0;
    int clipId = 0;
    int actionMode = 0; // 0: repeated then fresh Space; 1: repeat then EOF; 2: preview Space then Esc; 3: EOF.
    bool campaign = false;
    bool saveCapture = true;
    HWND host = nullptr;
    HWND child = nullptr;
    WNDPROC originalChildProc = nullptr;
    ULONGLONG started = 0;
    ULONGLONG childStarted = 0;
    int actionStage = 0;
    int captureCount = 0;
    int keyCount = 0;
    LONG greatestSequence = 0;
    bool destroyed = false;
    bool finalSkipped = false;
    bool finalFinished = false;
    bool stoppedWindowNull = false;
    bool stoppedSequenceZero = false;
    std::string finalFailure;
    std::string finalFrameSource;
    std::string problem;
    fs::path output;
    std::ofstream* actions = nullptr;
    std::ofstream* frames = nullptr;
};
thread_local Issue23VideoDriver* issue23ActiveDriver = nullptr;

LRESULT CALLBACK issue23VideoChildProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Issue23VideoDriver* driver = issue23ActiveDriver;
    if (!driver || !driver->originalChildProc) return DefWindowProcW(window, message, wParam, lParam);
    BinkVideoPageState* state = reinterpret_cast<BinkVideoPageState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_DESTROY && state) {
        driver->destroyed = true;
        driver->finalSkipped = state->skipped;
        driver->finalFinished = state->animation.finished();
        driver->finalFailure = state->animation.failure();
        driver->finalFrameSource = state->frame.source;
    }
    const LRESULT result = CallWindowProcW(driver->originalChildProc, window, message, wParam, lParam);
    if (message == WM_DESTROY && state) {
        driver->stoppedWindowNull = state->window == nullptr;
        driver->stoppedSequenceZero = state->animation.sequence() == 0;
    }
    return result;
}

bool issue23CaptureVideo(Issue23VideoDriver& driver, BinkVideoPageState& state) {
    Issue23VideoSurface surface;
    if (!surface.valid()) { driver.problem = "video DIB allocation failed"; return false; }
    std::fill(surface.pixels, surface.pixels + kWidth * kHeight * 4, 0);
    SendMessageW(driver.child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(surface.dc), PRF_CLIENT);
    GdiFlush();
    const int footerTop = driver.campaign ? kHeight - 32 : kHeight;
    const double scale = std::min(static_cast<double>(kWidth) / 140.0, static_cast<double>(footerTop) / 110.0);
    const int expectedWidth = static_cast<int>(std::lround(140.0 * scale));
    const int expectedHeight = static_cast<int>(std::lround(110.0 * scale));
    if (state.viewWidth != expectedWidth || state.viewHeight != expectedHeight
        || state.viewLeft != (kWidth - expectedWidth) / 2 || state.viewTop != (footerTop - expectedHeight) / 2
        || state.viewTop + state.viewHeight > footerTop) {
        driver.problem = "actual movie viewport/footer geometry mismatch"; return false;
    }
    std::uint64_t rgbHash = 14695981039346656037ULL;
    std::size_t footerInk = 0;
    for (int y = 0; y < kHeight; ++y) for (int x = 0; x < kWidth; ++x) {
        const unsigned char* pixel = surface.pixels + (y * kWidth + x) * 4;
        for (int channel = 0; channel < 3; ++channel) { rgbHash ^= pixel[channel]; rgbHash *= 1099511628211ULL; }
        if (y >= footerTop && (pixel[0] || pixel[1] || pixel[2])) ++footerInk;
    }
    if (driver.campaign && footerInk == 0) { driver.problem = "actual campaign footer has no rendered caption pixels"; return false; }
    std::string captureError;
    const fs::path bmp = driver.output / ("case-" + std::to_string(driver.caseId) + ".bmp");
    if (driver.saveCapture && !writeBmp(bmp.generic_string(), surface.pixels, captureError)) {
        driver.problem = captureError; return false;
    }
    *driver.frames << driver.caseId << ',' << driver.clipId << ',' << driver.campaign << ','
        << state.animation.sequence() << ',' << state.frame.width << ',' << state.frame.height << ','
        << state.viewLeft << ',' << state.viewTop << ',' << state.viewWidth << ',' << state.viewHeight << ','
        << footerTop << ',' << footerInk << ',' << rgbHash << ',' << issue23CsvText(state.frame.source) << ','
        << issue23CsvText(driver.saveCapture ? bmp.generic_string() : std::string{}) << '\n';
    ++driver.captureCount;
    return true;
}

void issue23SendKey(Issue23VideoDriver& driver, WPARAM key, bool repeat) {
    const LPARAM flags = LPARAM{1} | (repeat ? (LPARAM{1} << 30) : 0);
    SendMessageW(driver.child, WM_KEYDOWN, key, flags);
    ++driver.keyCount;
    *driver.actions << driver.caseId << ',' << driver.clipId << ',' << (GetTickCount64() - driver.started)
        << ',' << key << ',' << repeat << ',' << (IsWindow(driver.child) != FALSE) << '\n';
}

void issue23VideoDriverTick(Issue23VideoDriver& driver) {
    if (GetTickCount64() - driver.started > 8000) {
        driver.problem = "short original movie watchdog expired";
        if (driver.child && IsWindow(driver.child)) SendMessageW(driver.child, WM_CLOSE, 0, 0);
        return;
    }
    if (!driver.child) {
        driver.child = FindWindowExW(driver.host, nullptr, L"ConcordRTS.RuntimeBinkVideoPage", nullptr);
        if (!driver.child) return;
        driver.childStarted = GetTickCount64();
        driver.originalChildProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            driver.child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(issue23VideoChildProc)));
        if (!driver.originalChildProc) { driver.problem = "actual movie subclass failed"; SendMessageW(driver.child, WM_CLOSE, 0, 0); return; }
    }
    if (!IsWindow(driver.child)) return;
    BinkVideoPageState* state = reinterpret_cast<BinkVideoPageState*>(GetWindowLongPtrW(driver.child, GWLP_USERDATA));
    if (!state || state->standalone || state->campaignMovie != driver.campaign || !state->frame.valid()
        || state->frame.width != 140 || state->frame.height != 110) {
        driver.problem = "actual player state/movie/scope mismatch"; SendMessageW(driver.child, WM_CLOSE, 0, 0); return;
    }
    driver.greatestSequence = std::max(driver.greatestSequence, state->animation.sequence());
    if (driver.captureCount == 0 && !issue23CaptureVideo(driver, *state)) {
        SendMessageW(driver.child, WM_CLOSE, 0, 0); return;
    }
    const ULONGLONG age = GetTickCount64() - driver.childStarted;
    if (driver.actionMode == 0) {
        if (driver.actionStage == 0 && age >= 100) {
            issue23SendKey(driver, VK_SPACE, true); driver.actionStage = 1;
            if (!IsWindow(driver.child) || state->skipped) driver.problem = "repeated campaign Space skipped current clip";
        } else if (driver.actionStage == 1 && age >= 350) {
            if (driver.greatestSequence <= 1) { driver.problem = "movie did not progress after repeated Space"; SendMessageW(driver.child, WM_CLOSE, 0, 0); return; }
            issue23SendKey(driver, VK_SPACE, false); driver.actionStage = 2;
        }
    } else if (driver.actionMode == 1 && driver.actionStage == 0 && age >= 100) {
        issue23SendKey(driver, VK_SPACE, true); driver.actionStage = 1;
        if (!IsWindow(driver.child) || state->skipped) driver.problem = "held Space skipped next clip";
    } else if (driver.actionMode == 2) {
        if (driver.actionStage == 0 && age >= 100) {
            issue23SendKey(driver, VK_SPACE, false); driver.actionStage = 1;
            if (!IsWindow(driver.child) || state->skipped) driver.problem = "preview fresh Space was consumed as skip";
        } else if (driver.actionStage == 1 && age >= 200) {
            issue23SendKey(driver, VK_SPACE, true); driver.actionStage = 2;
            if (!IsWindow(driver.child) || state->skipped) driver.problem = "preview repeated Space was consumed as skip";
        } else if (driver.actionStage == 2 && age >= 350) {
            if (driver.greatestSequence <= 1) { driver.problem = "preview did not progress after ignored Space"; SendMessageW(driver.child, WM_CLOSE, 0, 0); return; }
            issue23SendKey(driver, VK_ESCAPE, false); driver.actionStage = 3;
        }
    }
    if (!driver.problem.empty() && IsWindow(driver.child)) SendMessageW(driver.child, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK issue23VideoHostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Issue23VideoDriver* driver = reinterpret_cast<Issue23VideoDriver*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_TIMER && wParam == 231 && driver) { issue23VideoDriverTick(*driver); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool runRuntimeCampaignVideoSkipSmoke(const RuntimeConfig& sourceConfig, std::string& errorMessage) {
    errorMessage.clear();
    const char* outputEnv = std::getenv("CONCORD_CAMPAIGN_VIDEO_SKIP_OUTPUT");
    if (!outputEnv || !*outputEnv) { errorMessage = "campaign video smoke requires private output"; return false; }
    const fs::path output(outputEnv);
    std::error_code fileError;
    fs::create_directories(output, fileError);
    if (fileError) { errorMessage = "cannot create private video output"; return false; }
    for (const char* name : {"cases.csv", "playbacks.csv", "actions.csv", "frames.csv", "movie.csv", "case-1.bmp", "case-2.bmp", "case-3.bmp"})
        if (fs::exists(output / name)) { errorMessage = "video smoke output already exists"; return false; }
    RuntimeConfig config = sourceConfig;
    config.musicEnabled = false; config.soundEffectsEnabled = false; config.voiceVolume = 0;
    const fs::path movie = fs::path(config.localDataRootPath) / "resources/mix_exports/movmd03/s07_p01e.bik";
    std::ifstream movieInput(movie, std::ios::binary);
    std::vector<unsigned char> movieBytes((std::istreambuf_iterator<char>(movieInput)), std::istreambuf_iterator<char>());
    const auto read32 = [&](std::size_t offset) { return static_cast<std::uint32_t>(movieBytes[offset])
        | (static_cast<std::uint32_t>(movieBytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(movieBytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(movieBytes[offset + 3]) << 24); };
    if (!movieInput || movieBytes.size() != 246492 || movieBytes[0] != 'B' || movieBytes[1] != 'I' || movieBytes[2] != 'K'
        || read32(8) != 41 || read32(20) != 140 || read32(24) != 110 || read32(28) != 15 || read32(32) != 1) {
        errorMessage = "original finite BIK header/size differs"; return false;
    }
    std::uint64_t movieHash = 14695981039346656037ULL;
    for (const unsigned char byte : movieBytes) { movieHash ^= byte; movieHash *= 1099511628211ULL; }
    std::ofstream movieCsv(output / "movie.csv"), cases(output / "cases.csv"), playbacks(output / "playbacks.csv"),
        actions(output / "actions.csv"), frames(output / "frames.csv");
    if (!movieCsv || !cases || !playbacks || !actions || !frames) { errorMessage = "video CSV creation failed"; return false; }
    movieCsv << "path,bytes,frames,width,height,rate_numerator,rate_denominator,source_fnv64\n"
        << issue23CsvText(movie.generic_string()) << ',' << movieBytes.size() << ",41,140,110,15,1," << movieHash << '\n';
    cases << "case,status,clips,keys,captures,reason\n";
    playbacks << "case,clip,campaign,mode,elapsed_ms,max_sequence,skipped,finished,cleanup_window_null,cleanup_sequence_zero,host_alive,child_absent,result,reason,last_frame\n";
    actions << "case,clip,elapsed_ms,key,repeat,child_alive_after\n";
    frames << "case,clip,campaign,sequence,width,height,view_left,view_top,view_width,view_height,footer_top,footer_ink,rgb_fnv64,frame_source,bmp\n";
    WNDCLASSEXW hostClass{}; hostClass.cbSize = sizeof(hostClass); hostClass.lpfnWndProc = issue23VideoHostProc;
    hostClass.hInstance = GetModuleHandleW(nullptr); hostClass.lpszClassName = L"ConcordRTS.Issue23.HiddenVideoHost";
    if (!RegisterClassExW(&hostClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { errorMessage = "video host class failed"; return false; }
    HWND host = CreateWindowExW(0, hostClass.lpszClassName, L"", WS_POPUP, -32000, -32000,
        kWidth, kHeight, nullptr, nullptr, hostClass.hInstance, nullptr);
    if (!host || IsWindowVisible(host)) { if (host) DestroyWindow(host); errorMessage = "hidden video host failed"; return false; }
    int totalClips = 0, totalKeys = 0, totalCaptures = 0;
    const auto play = [&](int caseId, int clipId, int actionMode, bool campaign, bool saveCapture) {
        Issue23VideoDriver driver; driver.caseId = caseId; driver.clipId = clipId; driver.actionMode = actionMode;
        driver.campaign = campaign; driver.saveCapture = saveCapture; driver.host = host; driver.output = output;
        driver.actions = &actions; driver.frames = &frames; driver.started = GetTickCount64();
        SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&driver));
        issue23ActiveDriver = &driver;
        if (!SetTimer(host, 231, 10, nullptr)) { errorMessage = "video driver timer failed"; issue23ActiveDriver = nullptr; return false; }
        std::string playbackError;
        // The preview intentionally exercises the public default scope, not an explicit false argument.
        const bool result = campaign ? runRuntimeBinkVideo(config, host, movie.generic_string(), playbackError, 0, true)
            : runRuntimeBinkVideo(config, host, movie.generic_string(), playbackError, 0);
        KillTimer(host, 231); SetWindowLongPtrW(host, GWLP_USERDATA, 0); issue23ActiveDriver = nullptr;
        const bool expectedSkip = actionMode == 0 || actionMode == 2;
        const bool childAbsent = FindWindowExW(host, nullptr, L"ConcordRTS.RuntimeBinkVideoPage", nullptr) == nullptr;
        bool okay = result && playbackError.empty() && driver.problem.empty() && driver.captureCount == 1
            && driver.destroyed && driver.finalSkipped == expectedSkip && driver.finalFailure.empty()
            && driver.stoppedWindowNull && driver.stoppedSequenceZero && IsWindow(host) && childAbsent
            && (expectedSkip || (driver.finalFinished && driver.greatestSequence > 1));
        if (actionMode == 0) okay = okay && driver.actionStage == 2;
        if (actionMode == 1) okay = okay && driver.actionStage == 1;
        if (actionMode == 2) okay = okay && driver.actionStage == 3;
        const std::string reason = !driver.problem.empty() ? driver.problem : playbackError;
        playbacks << caseId << ',' << clipId << ',' << campaign << ',' << actionMode << ',' << (GetTickCount64() - driver.started)
            << ',' << driver.greatestSequence << ',' << driver.finalSkipped << ',' << driver.finalFinished << ','
            << driver.stoppedWindowNull << ',' << driver.stoppedSequenceZero << ',' << (IsWindow(host) != FALSE)
            << ',' << childAbsent << ',' << (okay ? "PASS" : "FAIL") << ',' << issue23CsvText(reason) << ','
            << issue23CsvText(driver.finalFrameSource) << '\n';
        ++totalClips; totalKeys += driver.keyCount; totalCaptures += driver.captureCount;
        if (!okay) errorMessage = "campaign video case " + std::to_string(caseId) + " clip " + std::to_string(clipId)
            + ": " + (reason.empty() ? "actual skip/EOF/cleanup contract differs" : reason);
        return okay;
    };
    bool allOkay = play(1, 1, 0, true, true) && play(1, 2, 1, true, false);
    cases << "1," << (allOkay ? "PASS" : "FAIL") << ',' << totalClips << ',' << totalKeys << ',' << totalCaptures << ',' << issue23CsvText(errorMessage) << '\n';
    if (allOkay) { const int oldClips = totalClips, oldKeys = totalKeys, oldCaptures = totalCaptures;
        allOkay = play(2, 1, 2, false, true); cases << "2," << (allOkay ? "PASS" : "FAIL") << ',' << totalClips - oldClips << ',' << totalKeys - oldKeys << ',' << totalCaptures - oldCaptures << ',' << issue23CsvText(errorMessage) << '\n'; }
    if (allOkay) { const int oldClips = totalClips, oldKeys = totalKeys, oldCaptures = totalCaptures;
        allOkay = play(3, 1, 3, true, true); cases << "3," << (allOkay ? "PASS" : "FAIL") << ',' << totalClips - oldClips << ',' << totalKeys - oldKeys << ',' << totalCaptures - oldCaptures << ',' << issue23CsvText(errorMessage) << '\n'; }
    DestroyWindow(host);
    if (!allOkay) return false;
    std::cout << "Campaign video skip: PASS cases=3 playbacks=" << totalClips << " keys=" << totalKeys
        << " captures=" << totalCaptures << " BMPs=3 EOFs=2 cleanup=4 caption=（按 空格 跳过）\n";
    return totalClips == 4 && totalKeys == 6 && totalCaptures == 4;
}
