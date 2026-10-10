// Exercise the actual loading window renderer without showing a game window.
int runRuntimeCountryLoadingPresentationSmoke(const RuntimeConfig& config) {
    RuntimeWindowState runtime;
    runtime.config = config;
    runtime.config.loadingCountryDisplayName.clear();
    runtime.config.loadingCountryBrief.clear();
    runtime.config.loadingScreenAssetId.clear();
    runtime.config.gameMode = "Battle";
    runtime.config.gameModeRulesOverride.clear();
    runtime.config.startingUnits = 0;
    runtime.config.crates = false;
    runtime.config.playerCountry = "Americans";
    runtime.config.playerColor = "DarkBlue";
    RuntimeConfig::PlayerSlot player;
    player.enabled = player.human = player.local = true;
    player.name = "[New Player]";
    player.country = "Americans";
    player.color = "DarkBlue";
    player.startPosition = 2;
    RuntimeConfig::PlayerSlot computer = player;
    computer.human = computer.local = false;
    computer.name = "Computer";
    computer.country = "Russians";
    computer.color = "DarkRed";
    computer.startPosition = 1;
    RuntimeConfig::PlayerSlot closed = computer;
    closed.enabled = false;
    RuntimeConfig::PlayerSlot observer = player;
    observer.local = false;
    observer.country = "Observer";
    observer.startPosition = 1;
    runtime.config.playerSlots = {player, computer, closed, observer};
    const auto fail = [](const std::string& reason) {
        std::cerr << "Country loading presentation smoke failed: " << reason << '\n'; return 1;
    };
    const auto startMatch = [](RuntimeWindowState& state, std::vector<int>& assignments) {
        state.runtimeSoundsEnabled = false;
        state.runtimePreferencePersistenceEnabled = false;
        RuntimeLoadingWindowState loading;
        loading.smokeHidden = true;
        if (!prepareRuntimeLoadingMaterial(loading, state)
            || !prepareRuntimeLoadingMapStartPlan(loading, state)
            || !loading.startupPrepared || loading.completed != 0
            || loading.window != nullptr || loading.mapStarts.size() != 2
            || loading.mapPreview.width <= 0 || loading.mapPreview.height <= 0)
            return false;
        const auto planned = loading.mapStartAssignments;
        const auto firstMarkers = loading.mapStarts;
        // Verify the actual first window frame, before any base is spawned or
        // loading stage advances. This catches the original delayed-marker bug.
        if (!showRuntimeLoadingWindow(loading)) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 800; info.bmiHeader.biHeight = -600;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        void* bits = nullptr;
        HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (!dc || !bitmap || !bits) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            DestroyWindow(loading.window);
            return false;
        }
        HGDIOBJ old = SelectObject(dc, bitmap);
        SendMessageW(loading.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
        const auto* pixels = static_cast<const std::uint32_t*>(bits);
        const double scale = std::min(216.0 / loading.mapPreview.width, 166.0 / loading.mapPreview.height);
        const int width = std::max(1, static_cast<int>(std::lround(loading.mapPreview.width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(loading.mapPreview.height * scale)));
        bool firstFrameVisible = true;
        for (const auto& marker : firstMarkers) {
            const int x = 500 + (216 - width) / 2 + static_cast<int>(std::lround(marker.x * (width - 1)));
            const int y = 380 + (166 - height) / 2 + static_cast<int>(std::lround(marker.y * (height - 1)));
            const std::uint32_t rgb = (GetRValue(marker.color) << 16)
                | (GetGValue(marker.color) << 8) | GetBValue(marker.color);
            int colored = 0;
            for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx)
                if (x + dx >= 0 && x + dx < 800 && y + dy >= 0 && y + dy < 600)
                    colored += (pixels[(y + dy) * 800 + x + dx] & 0xffffffU) == rgb;
            firstFrameVisible = firstFrameVisible && colored >= 4;
        }
        std::string captureError;
        const bool random = state.config.playerSlots[0].startPosition == 0;
        const bool captured = writeRuntimeSmokeBmp(random
            ? "build/issue21_loading_first_frame_random.bmp"
            : "build/issue21_loading_first_frame_fixed.bmp", pixels, 800, 600, captureError);
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
        DestroyWindow(loading.window); loading.window = nullptr;
        if (!firstFrameVisible || !captured
            || !initializeRuntimeSkirmishStart(state, &assignments, &planned)
            || assignments != planned) return false;
        loading.mapStartAssignments = assignments;
        refreshRuntimeLoadingMapStarts(loading, state);
        if (loading.mapStarts.size() != firstMarkers.size()) return false;
        for (std::size_t i = 0; i < firstMarkers.size(); ++i) {
            if (loading.mapStarts[i].x != firstMarkers[i].x
                || loading.mapStarts[i].y != firstMarkers[i].y
                || loading.mapStarts[i].color != firstMarkers[i].color
                || loading.mapStarts[i].slotIndex != firstMarkers[i].slotIndex) return false;
        }
        return true;
    };
    std::vector<int> assignments;
    if (!startMatch(runtime, assignments) || assignments != std::vector<int>{1, 0, -1, -1})
        return fail("actual swapped player/computer starts, closed slot or observer assignment");
    {
        RuntimeWindowState random;
        random.config = runtime.config;
        random.config.playerSlots[0].startPosition = 0;
        random.config.playerSlots[1].startPosition = 0;
        std::vector<int> randomAssignments;
        if (!startMatch(random, randomAssignments) || randomAssignments.size() != 4
            || randomAssignments[0] < 0 || randomAssignments[1] < 0
            || randomAssignments[0] == randomAssignments[1]
            || randomAssignments[2] != -1 || randomAssignments[3] != -1)
            return fail("actual random combatant starts");
        RuntimeLoadingWindowState loading;
        if (!prepareRuntimeLoadingMaterial(loading, random)) return fail("random loading material");
        loading.mapStartAssignments = randomAssignments;
        refreshRuntimeLoadingMapStarts(loading, random);
        if (loading.mapStarts.size() != 2) return fail("random player markers");
        const auto candidates = runtimeSkirmishStartCandidates(random);
        for (const auto& marker : loading.mapStarts) {
            const auto& start = candidates[static_cast<std::size_t>(randomAssignments[marker.slotIndex])];
            const auto owner = runtimeSkirmishOwnerForSlot(marker.slotIndex);
            const auto found = std::find_if(random.world.getObjects().begin(), random.world.getObjects().end(),
                [&](const WorldObjectInstance& object) {
                    return object.owner == owner && object.health > 0
                        && std::abs(object.x - start.x) <= 16 && std::abs(object.y - start.y) <= 16;
                });
            if (found == random.world.getObjects().end()) return fail("marker disagrees with spawned player");
        }
        for (auto& slot : random.config.playerSlots) slot.enabled = false;
        refreshRuntimeLoadingMapStarts(loading, random);
        if (!loading.mapStarts.empty()) return fail("closed positions remain visible");
    }
    IniFile sparse;
    sparse.loadFromString("[Header]\nStartX=0\nStartY=0\nWidth=100\nHeight=100\nWaypoint1=bad\nWaypoint2=75,25\n");
    const auto second = runtimeLoadingPreviewStartPosition(sparse, {80, 80}, 1);
    if (!second || second->first != 0.75 || second->second != 0.25
        || runtimeLoadingPreviewStartPosition(sparse, {80, 80}, 0))
        return fail("missing preview coordinate shifted another start");
    IniFile legacy;
    legacy.loadFromString("[Map]\nSize=0,0,80,80\nLocalSize=0,0,80,80\n");
    const auto center = runtimeLoadingPreviewStartPosition(legacy, {80, 80}, 0);
    if (!center || center->first != 0.5 || center->second != 0.5)
        return fail("headerless map projection");
    if (runtimeResultBitFont(runtime).bytes.empty()) return fail("original game.fnt unavailable");
    const char* countries[]{"Americans", "British", "Germans", "French", "Alliance",
        "Russians", "Africans", "Arabs", "Confederation", "YuriCountry", "Observer"};
    for (const auto* country : countries) {
        runtime.config.playerCountry = country;
        runtime.config.playerSlots[0].country = country;
        RuntimeLoadingWindowState loading;
        loading.smokeHidden = true;
        if (!prepareRuntimeLoadingMaterial(loading, runtime)) return fail(std::string(country) + " original artwork");
        if (!loading.mapStarts.empty()) return fail("material decoding fabricated start assignments");
        loading.mapStartAssignments = assignments;
        refreshRuntimeLoadingMapStarts(loading, runtime);
        if (loading.mapStarts.size() != (std::string(country) == "Observer" ? 1U : 2U))
            return fail("combatant-only marker count");
        for (const auto& marker : loading.mapStarts) {
            if (marker.color != runtime.ownerRemapColors.at(toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(marker.slotIndex))))
                return fail("marker does not use actual owner color");
        }
        if (!loading.flagLoaded || loading.flagPalette.size() != 256 || loading.palette.size() != 256)
            return fail(std::string(country) + " flag or palette");
        if (std::string(country) != "Observer"
            && (loading.countryName.empty() || loading.unitName.empty() || loading.countryBrief.empty()))
            return fail(std::string(country) + " translated labels");
        if (loading.mapPreview.width <= 0 || loading.mapPreview.height <= 0 || loading.mapPreview.pixels.empty())
            return fail("selected map has no decoded preview");
        if (!showRuntimeLoadingWindow(loading)) return fail("hidden loading window");
        if (IsWindowVisible(loading.window)) { DestroyWindow(loading.window); return fail("smoke window became visible"); }
        std::string captureError;
        const auto capture = [&](int width, int height, std::vector<std::uint32_t>& pixels) {
            SetWindowPos(loading.window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            HDC dc = CreateCompatibleDC(nullptr);
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!dc || !bitmap || !bits) {
                if (bitmap) DeleteObject(bitmap);
                if (dc) DeleteDC(dc);
                return false;
            }
            HGDIOBJ old = SelectObject(dc, bitmap);
            SendMessageW(loading.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
            GdiFlush();
            const auto* first = static_cast<const std::uint32_t*>(bits);
            pixels.assign(first, first + width * height);
            SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
            return true;
        };
        std::vector<std::uint32_t> empty, half, full;
        // Compare the production window with and without markers. Verify their
        // actual colors and ensure no pixels outside occupied starts change.
        const auto checkMarkerPixels = [&](int width, int height) {
            const auto markers = loading.mapStarts;
            std::vector<std::uint32_t> marked, unmarked;
            if (!capture(width, height, marked)) return false;
            loading.mapStarts.clear();
            const bool captured = capture(width, height, unmarked);
            loading.mapStarts = markers;
            if (!captured) return false;
            const double scale = std::min(216.0 / loading.mapPreview.width, 166.0 / loading.mapPreview.height);
            const int pw = std::max(1, static_cast<int>(std::lround(loading.mapPreview.width * scale)));
            const int ph = std::max(1, static_cast<int>(std::lround(loading.mapPreview.height * scale)));
            std::vector<RECT> boxes;
            for (const auto& marker : markers) {
                const int x = 500 + (216 - pw) / 2 + static_cast<int>(std::lround(marker.x * (pw - 1)));
                const int y = 380 + (166 - ph) / 2 + static_cast<int>(std::lround(marker.y * (ph - 1)));
                const RECT box = runtimeMatchScoreScaledRect(RECT{0, 0, width, height}, RECT{x - 4, y - 4, x + 5, y + 5});
                const std::uint32_t rgb = (GetRValue(marker.color) << 16) | (GetGValue(marker.color) << 8) | GetBValue(marker.color);
                int colored = 0;
                for (int py = box.top; py < box.bottom; ++py) for (int px = box.left; px < box.right; ++px)
                    colored += (marked[py * width + px] & 0xffffffU) == rgb;
                if (colored < 4) return false;
                boxes.push_back(box);
            }
            int changed = 0;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                if (marked[y * width + x] == unmarked[y * width + x]) continue;
                ++changed;
                if (std::none_of(boxes.begin(), boxes.end(), [&](const RECT& box) {
                        return x >= box.left && x < box.right && y >= box.top && y < box.bottom;
                    })) return false;
            }
            return changed > 0;
        };
        loading.completed = 0;
        bool okay = checkMarkerPixels(800, 600) && checkMarkerPixels(640, 480);
        okay = capture(800, 600, empty) && okay;
        loading.completed = 3;
        okay = capture(800, 600, half) && okay;
        loading.completed = loading.total;
        okay = capture(800, 600, full) && okay;
        if (okay) {
            int firstChange = 0, secondChange = 0;
            for (int y = 0; y < 600; ++y) for (int x = 0; x < 800; ++x) {
                const auto index = y * 800 + x;
                if (empty[index] != half[index] || half[index] != full[index]) {
                    if (x < 22 || x >= 105 || y < 332 || y >= 338) okay = false;
                    firstChange += empty[index] != half[index];
                    secondChange += half[index] != full[index];
                }
            }
            okay = okay && firstChange > 0 && secondChange > 0;
        }
        if (okay) {
            const auto token = runtimeLoadingCountryText(loading.imageId).imageToken;
            const auto output = std::filesystem::path("build") / (std::string("loading_") + token + ".bmp");
            okay = writeRuntimeSmokeBmp(pathString(output), half.data(), 800, 600, captureError);
            if (std::string(country) == "Africans") {
                loading.completed = 1;
                std::vector<std::uint32_t> scaled;
                okay = capture(640, 480, scaled) && okay;
                if (okay) okay = writeRuntimeSmokeBmp("build/loading_libya_640.bmp", scaled.data(), 640, 480, captureError);
            }
        }
        DestroyWindow(loading.window);
        if (!okay) return fail(std::string(country) + " rendering, progress or capture " + captureError);
        std::cout << "[country loading] " << country << ": original art, labels, flag, occupied player colors and 0/50/100% progress passed\n";
    }
    std::cout << "Country loading presentation smoke passed: first-frame fixed/random markers before spawning, unchanged actual starts, closed slots, observers, all 11 pages and player-color pixels at 800x600/640x480.\n";
    return 0;
}
