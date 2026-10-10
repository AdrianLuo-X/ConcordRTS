// Inspect real loading-window submissions, not a movie assembled from test
// images. Hidden/offscreen windows prove completed CPU frames and ordering;
// this smoke does not claim to measure desktop scanout or a wallclock FPS.
struct RuntimeLoadingStabilityEmfAudit {
    int width = 0, height = 0, copies = 0;
    bool completeFrameOnly = true;
    DWORD unexpectedRecord = 0;
};

int CALLBACK auditRuntimeLoadingStabilityEmf(HDC, HANDLETABLE*, const ENHMETARECORD* record,
        int, LPARAM data) {
    auto& audit = *reinterpret_cast<RuntimeLoadingStabilityEmfAudit*>(data);
    if (record->iType == EMR_BITBLT) {
        if (record->nSize < sizeof(EMRBITBLT)) { audit.completeFrameOnly = false; return 1; }
        const auto& copy = *reinterpret_cast<const EMRBITBLT*>(record);
        ++audit.copies;
        const auto inside = [&](DWORD offset, DWORD count) {
            return offset >= sizeof(EMRBITBLT) && count > 0 && std::uint64_t(offset) + count <= record->nSize;
        };
        bool sourceComplete = inside(copy.offBmiSrc,copy.cbBmiSrc) && inside(copy.offBitsSrc,copy.cbBitsSrc)
            && copy.cbBmiSrc >= sizeof(BITMAPINFOHEADER);
        if (sourceComplete) {
            BITMAPINFOHEADER header{};
            CopyMemory(&header,reinterpret_cast<const unsigned char*>(record)+copy.offBmiSrc,sizeof(header));
            const std::int64_t rows = std::abs(std::int64_t(header.biHeight));
            const std::uint64_t stride = header.biWidth > 0
                ? ((std::uint64_t(header.biWidth)*header.biBitCount+31)/32)*4 : 0;
            sourceComplete = header.biSize >= sizeof(header) && header.biSize <= copy.cbBmiSrc && header.biPlanes == 1
                && header.biWidth >= audit.width && rows >= audit.height && stride > 0
                && rows > 0 && stride <= std::uint64_t(copy.cbBitsSrc)/std::uint64_t(rows);
        }
        audit.completeFrameOnly = audit.completeFrameOnly && copy.dwRop == SRCCOPY
            && sourceComplete && copy.xDest == 0 && copy.yDest == 0 && copy.xSrc == 0 && copy.ySrc == 0
            && copy.cxDest == audit.width && copy.cyDest == audit.height
            && copy.xformSrc.eM11 == 1.0f && copy.xformSrc.eM22 == 1.0f
            && copy.xformSrc.eM12 == 0.0f && copy.xformSrc.eM21 == 0.0f
            && copy.xformSrc.eDx == 0.0f && copy.xformSrc.eDy == 0.0f;
    } else if (record->iType != EMR_HEADER && record->iType != EMR_EOF) {
        // Brushes, clearing, text and layer-by-layer image submissions belong
        // only in memory DCs. A final screenshot cannot detect this boundary.
        audit.completeFrameOnly = false;
        if (audit.unexpectedRecord == 0) audit.unexpectedRecord = record->iType;
    }
    return 1;
}

struct RuntimeLoadingStabilityDib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ original = nullptr;
    void* bits = nullptr;
    int width = 0, height = 0;
    RuntimeLoadingStabilityDib() = default;
    RuntimeLoadingStabilityDib(const RuntimeLoadingStabilityDib&) = delete;
    RuntimeLoadingStabilityDib& operator=(const RuntimeLoadingStabilityDib&) = delete;
    ~RuntimeLoadingStabilityDib() {
        if (dc && original && original != HGDI_ERROR) SelectObject(dc, original);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    bool create(int w, int h) {
        width = w; height = h;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) return false;
        original = SelectObject(dc, bitmap);
        return original && original != HGDI_ERROR;
    }
    std::vector<std::uint32_t> pixels() const {
        if (!bits) return {};
        const auto* first = static_cast<const std::uint32_t*>(bits);
        std::vector<std::uint32_t> result(first, first + std::size_t(width) * height);
        for (auto& pixel : result) pixel &= 0xffffffU; // GDI's unused alpha is not page content.
        return result;
    }
};

struct RuntimeLoadingStabilityWindowAudit {
    RuntimeLoadingWindowState* loading = nullptr;
    RuntimeWindowState* runtime = nullptr;
    WNDPROC originalLoadingProc = nullptr;
    int serial = 0, loadingPaints = 0, mainPaints = 0;
    int mainBegin = 0, mainCompleted = 0, loadingDestroyed = 0;
    bool loadingAliveDuringMain = true, completeMainFrame = true, releasedOnDestroy = false;
    static LRESULT CALLBACK loadingProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* audit = reinterpret_cast<RuntimeLoadingStabilityWindowAudit*>(
            GetPropW(window, L"ConcordLoadingStabilityAudit"));
        if (!audit) return runtimeLoadingWindowProc(window, message, wParam, lParam);
        if (message == WM_PAINT) ++audit->loadingPaints;
        if (message == WM_NCDESTROY) audit->loadingDestroyed = ++audit->serial;
        const LRESULT result = CallWindowProcW(audit->originalLoadingProc, window, message, wParam, lParam);
        if (message == WM_NCDESTROY) {
            const auto& buffer = audit->loading->backBuffer;
            audit->releasedOnDestroy = audit->loading->window == nullptr && buffer.memoryDc == nullptr
                && buffer.bitmap == nullptr && buffer.pixels == nullptr && buffer.oldBitmap == nullptr
                && buffer.width == 0 && buffer.height == 0;
        }
        return result;
    }
    static LRESULT CALLBACK gameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* audit = reinterpret_cast<RuntimeLoadingStabilityWindowAudit*>(
            GetPropW(window, L"ConcordLoadingStabilityAudit"));
        if (!audit || message != WM_PAINT) return runtimeWindowProc(window, message, wParam, lParam);
        ++audit->mainPaints;
        const int begin = ++audit->serial;
        if (audit->mainBegin == 0) audit->mainBegin = begin;
        audit->loadingAliveDuringMain = audit->loadingAliveDuringMain && audit->loading
            && audit->loading->window && IsWindow(audit->loading->window);
        const LRESULT result = runtimeWindowProc(window, message, wParam, lParam);
        RECT client{}; GetClientRect(window, &client);
        const auto& runtime = *audit->runtime; const auto& buffer = runtime.backBuffer;
        audit->completeMainFrame = audit->completeMainFrame && runtime.world.getSummary().loaded
            && buffer.memoryDc && buffer.bitmap && buffer.pixels && buffer.topDownDib
            && buffer.width == client.right && buffer.height == client.bottom
            && runtime.renderStats.rendered && runtime.renderStats.texturedTerrainCells > 0
            && buffer.pixels != audit->loading->backBuffer.pixels;
        if (audit->mainCompleted == 0) audit->mainCompleted = ++audit->serial;
        return result;
    }
};

int runRuntimeLoadingFrameStabilitySmoke(const RuntimeConfig& config) {
    std::cout << std::unitbuf;
    const char* output = std::getenv("CONCORD_LOADING_FRAME_STABILITY_OUTPUT");
    const char* requested = std::getenv("CONCORD_LOADING_FRAME_STABILITY_CASE");
    const std::string directory = output ? output : "", filter = requested ? requested : "";
    if (!directory.empty()) std::filesystem::create_directories(directory);
    std::ofstream csv;
    if (!directory.empty()) {
        csv.open(std::filesystem::path(directory) / "loading-frames.csv");
        csv << "case,event,completed,width,height,paintCount,fullCopies,pixelHash,mainBegin,mainComplete,loadingDestroyed\n";
    }
    int cases = 0, paintChecks = 0, emfChecks = 0, repeatChecks = 0, nativePixels = 0;
    int progressPixels = 0, markerChecks = 0, eraseChecks = 0, resizeChecks = 0, releaseChecks = 0;
    int shownHandoffs = 0, hiddenHandoffs = 0;
    std::vector<std::string> failures;
    const auto hash = [](const std::vector<std::uint32_t>& pixels) {
        std::uint64_t value = 1469598103934665603ULL;
        for (const auto pixel : pixels) { value ^= pixel & 0xffffffU; value *= 1099511628211ULL; }
        return value;
    };
    const auto color = [](COLORREF rgb) { return (std::uint32_t(GetRValue(rgb)) << 16)
        | (std::uint32_t(GetGValue(rgb)) << 8) | std::uint32_t(GetBValue(rgb)); };
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr const wchar_t* gameClass = L"ConcordLoadingStabilityGameWindow";
    WNDCLASSW gameClassInfo{}; gameClassInfo.lpfnWndProc = RuntimeLoadingStabilityWindowAudit::gameProc;
    gameClassInfo.hInstance = instance; gameClassInfo.lpszClassName = gameClass;
    if (RegisterClassW(&gameClassInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::cerr << "Loading frame stability: game window class unavailable\n"; return 1;
    }
    const std::array<std::string, 6> stageNames{{"original country loading material", "map, theater, rules and art",
        "navigation and economy", "skirmish start and triggers", "sound database, EVA and music cache",
        "terrain, objects, sidebar and first frame"}};
    for (const char* country : {"Americans", "Russians", "YuriCountry"}) for (const POINT size : {POINT{800,600}, POINT{640,480}}) {
        const std::string label = std::string(country) + '-' + std::to_string(size.x) + 'x' + std::to_string(size.y);
        if (!filter.empty() && filter != label) continue;
        std::string stage = "original prepared first page", failure;
        const auto fail = [&](const std::string& reason) { failure = "Loading " + label + " [" + stage + "]: " + reason; return false; };
        const auto body = [&]() {
            auto runtimeBox = std::make_unique<RuntimeWindowState>(); auto& runtime = *runtimeBox;
            RuntimeSnapshotActiveStateScope active(runtime);
            runtime.config = config; runtime.config.loadingScreenAssetId.clear();
            runtime.config.loadingCountryDisplayName.clear(); runtime.config.loadingCountryBrief.clear();
            runtime.config.showMainMenu = runtime.config.showSkirmishMenu = false;
            runtime.config.gameMode = "Battle"; runtime.config.gameModeRulesOverride.clear();
            runtime.config.skirmishLoadSavePath.clear(); runtime.config.campaignLoadSavePath.clear();
            runtime.config.multiplayerActive = runtime.config.noCooldown = runtime.config.computerAiEnabled = false;
            runtime.config.crates = false; runtime.config.startingUnits = 0;
            runtime.config.playerCountry = country; runtime.config.playerColor = "DarkBlue";
            RuntimeConfig::PlayerSlot player; player.enabled = player.human = player.local = true;
            player.name = "Loading Continuity"; player.country = country; player.color = "DarkBlue"; player.startPosition = 2;
            RuntimeConfig::PlayerSlot opponent; opponent.enabled = true; opponent.country = "Russians";
            opponent.name = "Computer"; opponent.color = "DarkRed"; opponent.startPosition = 1;
            runtime.config.playerSlots = {player, opponent};
            runtime.runtimeSoundsEnabled = false; runtime.runtimePreferencePersistenceEnabled = false;
            RuntimeLoadingWindowState loading, handoff;
            loading.smokeHidden = handoff.smokeHidden = true;
            RuntimeLoadingStabilityWindowAudit loadingAudit, handoffAudit;
            loadingAudit.loading = &loading; loadingAudit.runtime = &runtime;
            handoffAudit.loading = &handoff; handoffAudit.runtime = &runtime;
            HWND game = nullptr;
            struct Cleanup {
                RuntimeLoadingWindowState& loading; RuntimeLoadingWindowState& handoff; HWND& game;
                ~Cleanup() {
                    if (loading.window && IsWindow(loading.window)) DestroyWindow(loading.window);
                    if (handoff.window && IsWindow(handoff.window)) DestroyWindow(handoff.window);
                    if (game && IsWindow(game)) {
                        DestroyWindow(game);
                        // The actual game WndProc posts its normal quit message.
                        // Consume only this harness-owned final cleanup message.
                        MSG quit{}; PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
                    }
                    releaseRuntimeBackBuffer(loading.backBuffer); releaseRuntimeBackBuffer(handoff.backBuffer);
                    releaseRuntimeBackBuffer(loading.runtime->backBuffer);
                    releaseRuntimeBackBuffer(loading.runtime->sidebarStaticBuffer);
                    releaseRuntimeBackBuffer(loading.runtime->radarTerrainBuffer);
                }
            } cleanup{loading, handoff, game};
            const auto observe = [&](RuntimeLoadingWindowState& page, RuntimeLoadingStabilityWindowAudit& audit) {
                if (!SetPropW(page.window, L"ConcordLoadingStabilityAudit", &audit)) return false;
                audit.originalLoadingProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(page.window, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(RuntimeLoadingStabilityWindowAudit::loadingProc)));
                return audit.originalLoadingProc != nullptr;
            };
            const auto readBuffer = [&](const RuntimeLoadingWindowState& page, int w, int h) {
                const auto& buffer = page.backBuffer;
                if (!buffer.memoryDc || !buffer.bitmap || !buffer.pixels || !buffer.topDownDib
                    || buffer.width != w || buffer.height != h) return std::vector<std::uint32_t>{};
                const auto* first = static_cast<const std::uint32_t*>(buffer.pixels);
                std::vector<std::uint32_t> pixels(first, first + std::size_t(w) * h);
                for (auto& pixel : pixels) pixel &= 0xffffffU;
                return pixels;
            };
            const auto paint = [&](RuntimeLoadingWindowState& page, RuntimeLoadingStabilityWindowAudit& audit, int w, int h) {
                const int before = audit.loadingPaints;
                // UpdateWindow on a hidden window can have no visible clip.
                // Explicitly dispatch the same real WM_PAINT as the existing
                // result-presentation smoke; read its actual completed DIB.
                pumpRuntimeLoadingWindow(page); InvalidateRect(page.window, nullptr, FALSE);
                SendMessageW(page.window, WM_PAINT, 0, 0); GdiFlush();
                auto pixels = readBuffer(page,w,h);
                if (audit.loadingPaints <= before || pixels.size() != std::size_t(w)*h) {
                    fail("actual WM_PAINT did not complete a current-size owned DIB"); return std::vector<std::uint32_t>{};
                }
                ++paintChecks; return pixels;
            };
            const auto recordAndPrint = [&](RuntimeLoadingWindowState& page, int w, int h,
                    const std::vector<std::uint32_t>& expected) {
                HDC screen = GetDC(nullptr);
                if (!screen) return fail("EMF reference DC unavailable");
                HDC recording = CreateEnhMetaFileW(screen,nullptr,nullptr,nullptr);
                if (!recording) { ReleaseDC(nullptr,screen); return fail("real target EMF DC unavailable"); }
                SendMessageW(page.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(recording), PRF_CLIENT);
                HENHMETAFILE frame = CloseEnhMetaFile(recording);
                RuntimeLoadingStabilityEmfAudit audit{w,h};
                const bool recorded = frame && EnumEnhMetaFile(nullptr,frame,auditRuntimeLoadingStabilityEmf,&audit,nullptr);
                if (frame) DeleteEnhMetaFile(frame); ReleaseDC(nullptr,screen);
                if (!recorded || !audit.completeFrameOnly || audit.copies != 1)
                    return fail("target saw partial drawing: fullCopies=" + std::to_string(audit.copies)
                        + " unexpectedEMR=" + std::to_string(audit.unexpectedRecord));
                // EMF's physical .01mm frame can quantize replay coordinates.
                // Audit its real command stream, then compare unscaled actual
                // PRINT pixels to PAINT and the unchanged original renderer.
                RuntimeLoadingStabilityDib printed, originalPage;
                if (!printed.create(w,h) || !originalPage.create(w,h)) return fail("real print/reference DIB unavailable");
                SendMessageW(page.window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(printed.dc),PRF_CLIENT);
                const RECT client{0,0,w,h}; drawRuntimeCountryLoading(originalPage.dc,client,page); GdiFlush();
                if (printed.pixels() != expected || originalPage.pixels() != expected)
                    return fail("actual PRINT/PAINT pixels differ from the unchanged original page renderer");
                ++emfChecks;
                if (csv) csv << label << ",printEMF," << page.completed << ',' << w << ',' << h << ',' << loadingAudit.loadingPaints
                    << ',' << audit.copies << ',' << hash(expected) << ",0,0,0\n";
                return true;
            };
            if (!prepareRuntimeLoadingMaterial(loading,runtime) || !prepareRuntimeLoadingMapStartPlan(loading,runtime)
                || !loading.startupPrepared || loading.completed != 0 || loading.mapStarts.size() != 2
                || loading.mapPreview.width <= 0 || loading.mapPreview.height <= 0 || !loading.flagLoaded
                || loading.countryName.empty() || loading.countryBrief.empty() || loading.unitName.empty()
                || runtimeResultBitFont(runtime).bytes.empty()) return fail("original art/font/flag/preview/planned starts unavailable before first page");
            const auto starts = loading.mapStarts; const auto assignments = loading.mapStartAssignments;
            if (assignments != std::vector<int>{1,0}) return fail("real fixed starts do not match the two selected lobby positions");
            if (!showRuntimeLoadingWindow(loading) || IsWindowVisible(loading.window)
                || !observe(loading,loadingAudit)) return fail("hidden real loading window/procedure unavailable");
            SetWindowPos(loading.window,nullptr,0,0,size.x,size.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            const auto scaled = [&](int value) { return int(std::lround(value * (size.x/800.0))); };
            const auto stockPixels = [&](const std::vector<std::uint32_t>& pixels) {
                // Literal non-overlay native regions: right geography and top
                // left special-unit portrait. Decode palette indices directly,
                // independently of the new presentation/buffering helper.
                int verified = 0, colored = 0;
                for (const RECT roi : {RECT{450,10,775,285}, RECT{20,10,420,80}}) {
                    for (int y = scaled(roi.top); y < scaled(roi.bottom); y += 7)
                        for (int x = scaled(roi.left); x < scaled(roi.right); x += 7) {
                            const int sx = x*loading.image.width/int(size.x), sy = y*loading.image.height/int(size.y);
                            const auto index = loading.image.pixels[std::size_t(sy)*loading.image.width+sx];
                            RuntimePaletteColor rgb{};
                            if (index || loading.originalImagePcxFallback) rgb = loading.palette[index];
                            const std::uint32_t expected = (std::uint32_t(rgb.r)<<16)|(std::uint32_t(rgb.g)<<8)|rgb.b;
                            if (pixels[std::size_t(y)*size.x+x] != expected) return fail("native artwork moved/disappeared at " + std::to_string(x)+','+std::to_string(y));
                            ++verified; colored += expected != 0;
                        }
                }
                if (verified < 100 || colored < 64) return fail("native geography/portrait oracle has insufficient real opaque pixels");
                nativePixels += verified; return true;
            };
            const auto markers = [&](const std::vector<std::uint32_t>& pixels) {
                const double factor = std::min(216.0/loading.mapPreview.width,166.0/loading.mapPreview.height);
                const int pw = std::max(1,int(std::lround(loading.mapPreview.width*factor)));
                const int ph = std::max(1,int(std::lround(loading.mapPreview.height*factor)));
                for (const auto& marker : starts) {
                    const int x = 500+(216-pw)/2+int(std::lround(marker.x*(pw-1)));
                    const int y = 380+(166-ph)/2+int(std::lround(marker.y*(ph-1)));
                    int matching = 0;
                    for (int py = scaled(y-4); py < scaled(y+5); ++py)
                        for (int px = scaled(x-4); px < scaled(x+5); ++px)
                            if (px>=0 && px<size.x && py>=0 && py<size.y)
                                matching += pixels[std::size_t(py)*size.x+px] == color(marker.color);
                    if (matching < 4) return fail("actual planned player's first-frame marker/color is absent");
                    ++markerChecks;
                }
                return true;
            };
            std::vector<std::uint32_t> previous;
            for (int complete = 0; complete <= 6; ++complete) {
                stage = "actual paint/progress " + std::to_string(complete);
                // Exercise the real display notifier. The separate exhaustive
                // prewarm CTest verifies execution of the underlying stages.
                if (complete) updateRuntimeLoadingStage(loading,stageNames[complete-1]);
                auto pixels = paint(loading,loadingAudit,size.x,size.y);
                if (pixels.empty() || !stockPixels(pixels) || !markers(pixels)
                    || !recordAndPrint(loading,size.x,size.y,pixels)) return false;
                const int right = scaled(22+83*complete/6);
                for (int y = scaled(332); y < scaled(338); ++y) for (int x = scaled(22); x < scaled(105); ++x) {
                    if (pixels[std::size_t(y)*size.x+x] != (x<right ? color(loading.playerColor) : 0U))
                        return fail("original compact progress extent/color changed");
                    ++progressPixels;
                }
                if (!previous.empty()) for (int y = 0; y < size.y; ++y) for (int x = 0; x < size.x; ++x) {
                    if (pixels[std::size_t(y)*size.x+x] == previous[std::size_t(y)*size.x+x]) continue;
                    if (x<scaled(22) || x>=scaled(105) || y<scaled(332) || y>=scaled(338))
                        return fail("progress redraw changed original content outside the load bar");
                }
                for (int repeat = 0; repeat < 3; ++repeat) {
                    if (paint(loading,loadingAudit,size.x,size.y) != pixels) return fail("unchanged repeated real pump/WM_PAINT lost page content");
                    ++repeatChecks;
                }
                RuntimeLoadingStabilityDib erase;
                if (!erase.create(size.x,size.y) || !BitBlt(erase.dc,0,0,size.x,size.y,loading.backBuffer.memoryDc,0,0,SRCCOPY))
                    return fail("actual completed page DC unavailable for erase probe");
                GdiFlush();
                const auto before = erase.pixels();
                const LRESULT handled = SendMessageW(loading.window,WM_ERASEBKGND,reinterpret_cast<WPARAM>(erase.dc),0); GdiFlush();
                if (!handled || erase.pixels() != before || before != pixels) return fail("WM_ERASEBKGND replaced an existing completed frame");
                ++eraseChecks; previous = std::move(pixels);
            }
            if (loading.completed != loading.total || loading.completedStages != std::vector<std::string>(stageNames.begin(),stageNames.end()))
                return fail("real stage notifier lost order/count");
            stage = "actual resize and return";
            const int alternateWidth = size.x==800 ? 640 : 800, alternateHeight = size.y==600 ? 480 : 600;
            SetWindowPos(loading.window,nullptr,0,0,alternateWidth,alternateHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            const auto resized = paint(loading,loadingAudit,alternateWidth,alternateHeight);
            if (resized.empty() || !recordAndPrint(loading,alternateWidth,alternateHeight,resized)) return false;
            SetWindowPos(loading.window,nullptr,0,0,size.x,size.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            if (paint(loading,loadingAudit,size.x,size.y) != previous) return fail("resizing back retained stale/incomplete original page");
            ++resizeChecks;
            if (!directory.empty() && std::string(country)=="YuriCountry") {
                std::string captureError;
                if (!writeRuntimeSmokeBmp(pathString(std::filesystem::path(directory)/("loading_"+label+".bmp")),previous.data(),size.x,size.y,captureError))
                    return fail("actual completed frame capture: "+captureError);
            }
            game = CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,gameClass,L"Loading real first-frame handoff",WS_POPUP,-32000,-32000,size.x,size.y,
                nullptr,nullptr,instance,nullptr);
            if (!game) return fail("real offscreen game window unavailable");
            SetWindowLongPtrW(game,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&runtime));
            SetPropW(game,L"ConcordLoadingStabilityAudit",&loadingAudit);
            stage = "hidden benchmark/no-show finish";
            const HWND loadingWindow = loading.window;
            finishRuntimeLoadingWindow(loading,game,false);
            MSG quit{};
            if (IsWindowVisible(game) || loadingAudit.mainPaints != 0 || IsWindow(loadingWindow)
                || !loadingAudit.releasedOnDestroy || !loadingAudit.loadingDestroyed
                || PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE))
                return fail("hidden finish showed/painted game, leaked page resources or posted quit");
            ++hiddenHandoffs; ++releaseChecks;
            stage = "actual prepared world/live first-paint handoff";
            if (!prepareRuntimeLoadingMaterial(handoff,runtime)) return fail("handoff original page material unavailable");
            handoff.mapStartAssignments = assignments; refreshRuntimeLoadingMapStarts(handoff,runtime);
            handoff.completed = handoff.total;
            if (!showRuntimeLoadingWindow(handoff) || !observe(handoff,handoffAudit)) return fail("real handoff page unavailable");
            SetWindowPos(handoff.window,nullptr,0,0,size.x,size.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            if (paint(handoff,handoffAudit,size.x,size.y).empty()) return false;
            std::vector<int> actualStarts;
            if (!initializeRuntimeSkirmishStart(runtime,&actualStarts,&assignments) || actualStarts != assignments)
                return fail("real match start diverged from first-page assignment");
            loadRuntimeSoundDatabase(runtime); ensureRuntimeLightingInitialized(runtime); ensureRuntimeParticlesInitialized(runtime);
            ensureRuntimeSubcellsInitialized(runtime); ensureRuntimeSpecialMechanismsInitialized(runtime); ensureRuntimeRepairDepotsInitialized(runtime);
            if (!initializeRuntimeTriggers(runtime)) return fail("actual handoff trigger initialization failed");
            completeRuntimeWorldRefresh(runtime); runtime.visionEnabled = false; runtime.repaintRequested = true;
            RECT gameClient{}; GetClientRect(game,&gameClient);
            centerRuntimeCameraOnCell(runtime,gameClient,runtime.economy.skirmishStartCell);
            SetPropW(game,L"ConcordLoadingStabilityAudit",&handoffAudit);
            finishRuntimeLoadingWindow(handoff,game,true); GdiFlush();
            if (!handoffAudit.loadingAliveDuringMain || !handoffAudit.completeMainFrame || handoffAudit.mainPaints < 1
                || handoffAudit.mainBegin <= 0 || handoffAudit.mainCompleted <= handoffAudit.mainBegin
                || handoffAudit.loadingDestroyed <= handoffAudit.mainCompleted || !handoffAudit.releasedOnDestroy
                || handoff.window != nullptr || !IsWindowVisible(game)
                || PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE)) {
                return fail("handoff lost loading coverage/complete actual main frame: begin="+std::to_string(handoffAudit.mainBegin)
                    + " complete="+std::to_string(handoffAudit.mainCompleted)+" destroyed="+std::to_string(handoffAudit.loadingDestroyed)
                    + " mainPaints="+std::to_string(handoffAudit.mainPaints)+" loadAlive="+std::to_string(handoffAudit.loadingAliveDuringMain)
                    + " fullMain="+std::to_string(handoffAudit.completeMainFrame));
            }
            ++shownHandoffs; ++releaseChecks;
            if (csv) csv << label << ",actualHandoff,6," << size.x << ',' << size.y << ',' << handoffAudit.loadingPaints
                << ",0,0," << handoffAudit.mainBegin << ',' << handoffAudit.mainCompleted << ',' << handoffAudit.loadingDestroyed << '\n';
            return true;
        };
        if (body()) { ++cases; std::cout << "Loading frame stability " << label << " PASS\n"; }
        else { failures.push_back(failure); std::cout << failure << '\n'; }
    }
    if (!failures.empty()) {
        std::cerr << "Loading frame stability failed " << failures.size() << " case(s):\n";
        for (const auto& item : failures) std::cerr << item << '\n'; return 1;
    }
    if (cases == 0) { std::cerr << "Loading frame stability: requested case was not found\n"; return 1; }
    std::cout << "Loading frame stability PASS cases=" << cases << " actualPaints=" << paintChecks << " emfFullFrame=" << emfChecks
        << " repeats=" << repeatChecks << " nativePixels=" << nativePixels << " progressPixels=" << progressPixels
        << " markers=" << markerChecks << " erase=" << eraseChecks << " resize=" << resizeChecks << " released=" << releaseChecks
        << " real/hiddenHandoff=" << shownHandoffs << '/' << hiddenHandoffs << " desktopFPS=notMeasured\n";
    return 0;
}
