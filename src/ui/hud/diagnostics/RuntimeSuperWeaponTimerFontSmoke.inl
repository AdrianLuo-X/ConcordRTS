// Finite Win32 pixel acceptance for the original GAME.FNT superweapon HUD.
// Trigger grants create real stock sidebar snapshots; no map simulation, save,
// rule edits, font substitution or after-image golden is used by this probe.
struct RuntimeSuperWeaponTimerFontSmokeRaw {
    std::vector<unsigned char> bytes;
    unsigned stride = 0, rows = 0, height = 0, count = 0, record = 0;
    std::string source;
    static constexpr std::size_t table = 28, data = 28 + 65536U * 2U;
    unsigned u32(std::size_t at) const {
        return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8)
            | (unsigned(bytes[at + 2]) << 16) | (unsigned(bytes[at + 3]) << 24);
    }
    bool load(const RuntimeConfig& config) {
        const std::filesystem::path candidates[]{
            std::filesystem::path(config.modRootPath) / "game.fnt",
            std::filesystem::path(config.resourceRootPath) / "GameMD" / "game.fnt",
            std::filesystem::path(config.resourceRootPath) / "Game" / "game.fnt"};
        for (const auto& path : candidates) {
            std::ifstream input(path, std::ios::binary);
            if (!input) continue;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            if (bytes.size() < data || bytes[0] != 'f' || bytes[1] != 'o'
                || bytes[2] != 'n' || bytes[3] != 'T') continue;
            stride = u32(8); rows = u32(12); height = u32(16);
            count = u32(20); record = u32(24);
            if (!stride || stride > 16 || !rows || rows > 64 || height < rows
                || height > 64 || record != 1 + stride * rows || !count
                || count > (bytes.size() - data) / record) continue;
            for (unsigned code = 0; code < 65536; ++code) {
                const std::size_t at = table + code * 2U;
                if ((unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8)) > count) return false;
            }
            source = pathString(path); return true;
        }
        return false;
    }
    const unsigned char* glyph(wchar_t code) const {
        std::size_t at = table + static_cast<std::uint16_t>(code) * 2U;
        unsigned id = unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
        if (!id) {
            at = table + unsigned('?') * 2U;
            id = unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
        }
        if (!id || id > count) return nullptr;
        const auto* value = bytes.data() + data + (id - 1U) * record;
        return value[0] && value[0] <= stride * 8U ? value : nullptr;
    }
    bool ownGlyph(wchar_t code) const {
        const std::size_t at = table + static_cast<std::uint16_t>(code) * 2U;
        return (unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8)) != 0;
    }
    int width(const std::wstring& text) const {
        int result = -1;
        for (wchar_t code : text) { const auto* g = glyph(code); result += g ? int(g[0]) + 1 : 5; }
        return std::max(0, result);
    }
    std::wstring fit(const std::wstring& text, int available) const {
        if (width(text) <= available) return text;
        // Linear longest-prefix search, rather than the production pop loop.
        std::wstring result = L"...";
        for (std::size_t n = 1; n <= text.size(); ++n) {
            const auto value = text.substr(0, n) + L"...";
            if (width(value) <= available) result = value; else break;
        }
        return result;
    }
    void paint(std::vector<std::uint32_t>& pixels, int w, int h, RECT rect,
        const std::wstring& value, COLORREF color, RECT outer) const {
        if (value.empty() || rect.right <= rect.left || rect.bottom <= rect.top) return;
        const auto text = fit(value, int(rect.right - rect.left));
        const int startX = int(rect.right) - width(text);
        const int startY = int(rect.top) + std::max(0, int(rect.bottom - rect.top) - int(height)) / 2;
        for (int offset : {1, 0}) {
            int cursor = startX;
            const std::uint32_t rgb = offset ? 0U
                : (std::uint32_t(GetRValue(color)) << 16)
                    | (std::uint32_t(GetGValue(color)) << 8) | GetBValue(color);
            for (wchar_t code : text) {
                const auto* g = glyph(code);
                if (!g) { cursor += 5; continue; }
                for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < g[0]; ++x) {
                    if (!(g[1 + y * stride + x / 8] & (128U >> (x & 7)))) continue;
                    const int px = cursor + int(x) + offset, py = startY + int(y) + offset;
                    if (px >= 0 && px < w && py >= 0 && py < h && px >= rect.left
                        && px < rect.right && py >= rect.top && py < rect.bottom
                        && px >= outer.left && px < outer.right && py >= outer.top && py < outer.bottom)
                        pixels[std::size_t(py) * w + px] = rgb;
                }
                cursor += int(g[0]) + 1;
            }
        }
    }
};

bool runRuntimeSuperWeaponTimerFontSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Superweapon timer font: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    state.config = config;
    state.report = buildRuntimeStartupReport(config, &state.world, &state.theater,
        &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("original startup resources unavailable");
    // Match existing stock support probes: map campaign overrides are not stock definitions.
    IniFile rulesIni; RulesDatabase stockRules;
    if (!rulesIni.loadFromFile(state.report.rulesPath) || !stockRules.loadFromIni(rulesIni))
        return fail("original rules file unavailable");
    state.rules = std::move(stockRules); state.rulesLoaded = true;
    state.runtimeSoundsEnabled = false; state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    RuntimeSuperWeaponTimerFontSmokeRaw raw;
    if (!raw.load(config) || raw.stride != 3 || raw.rows != 16 || raw.height != 17
        || raw.count != 33910 || raw.record != 49 || raw.bytes.size() != 1792690)
        return fail("original GAME.FNT identity/header/table contract");
    const auto& shared = runtimeResultBitFont(state);
    if (shared.bytes != raw.bytes) return fail("shared decoder did not use the independently read original font");
    for (wchar_t code : std::wstring(L"0123456789:.?"))
        if (!raw.ownGlyph(code)) return fail("original numeric glyph missing");
    const std::vector<std::string> ids{"NukeSpecial", "LightningStormSpecial", "IronCurtainSpecial",
        "ChronoSphereSpecial", "PsychicDominatorSpecial", "GeneticConverterSpecial"};
    std::vector<std::wstring> names;
    for (const auto& id : ids) {
        const auto* rule = state.rules.findSuperWeaponType(id);
        if (!rule || !rule->showTimer || rule->uiName.empty()) return fail(id + " original timer rule missing");
        const auto text = runtimeMenuLabel(state, rule->uiName, L"");
        if (text.empty()) return fail(id + " original CSF name missing");
        for (wchar_t code : text) if (!raw.ownGlyph(code)) return fail(id + " original CSF glyph missing");
        names.push_back(text);
    }
    const char* outputEnv = std::getenv("CONCORD_SUPERWEAPON_TIMER_FONT_OUTPUT");
    const auto output = outputEnv && *outputEnv ? std::filesystem::path(outputEnv)
        : std::filesystem::path(config.localDataRootPath).parent_path() / "build/smoke/timer-font-output";
    std::error_code directoryError; std::filesystem::create_directories(output, directoryError);
    if (directoryError) return fail("output directory: " + directoryError.message());
    std::ofstream cases(output / "cases.csv"), source(output / "font-source.csv");
    if (!cases || !source) return fail("evidence CSV open");
    std::uint64_t rawHash = 1469598103934665603ULL;
    for (unsigned char byte : raw.bytes) { rawHash ^= byte; rawHash *= 1099511628211ULL; }
    source << "source,bytes,fnv64,stride,rows,height,glyphs,record_size,scope\n\"" << raw.source
        << "\"," << raw.bytes.size() << ',' << rawHash << ',' << raw.stride << ',' << raw.rows
        << ',' << raw.height << ',' << raw.count << ',' << raw.record << ",original-file-mask\n";
    cases << "scene,hz,width,height,visible_rows,owners,pixels_checked,font_variants,state_hash_before,state_hash_after,raw_rgb_exact,whole_hfont_exact\n";
    HFONT enormous = CreateFontW(-72, 0, 0, 0, FW_BLACK, TRUE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, L"Times New Roman");
    HFONT tiny = CreateFontW(-7, 0, 0, 0, FW_THIN, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH, L"Terminal");
    struct Fonts { HFONT a, b; ~Fonts() { if (a) DeleteObject(a); if (b) DeleteObject(b); } } fonts{enormous, tiny};
    if (!enormous || !tiny) return fail("independent extreme system fonts unavailable");
    int directCases = 0, listCases = 0, bitmapCount = 0;
    std::size_t checkedPixels = 0;
    const auto compare = [&](const std::string& label, int hz, int w, int h, int rows, int owners,
        std::uint32_t background, const std::vector<std::uint32_t>& expected,
        const std::function<void(HDC)>& draw, bool capture) {
        RuntimePlayerDefeatNoticeSmokeSurface surface(w, h);
        if (!surface.valid()) return fail(label + " real DIB surface unavailable");
        const auto before = state.superWeapons.stateHash();
        std::vector<std::uint32_t> baseline;
        for (HFONT font : {surface.font, enormous, tiny}) {
            std::fill(surface.pixels, surface.pixels + std::size_t(w) * h, background);
            const HGDIOBJ previous = SelectObject(surface.dc, font);
            draw(surface.dc); GdiFlush();
            SelectObject(surface.dc, previous);
            std::vector<std::uint32_t> pixels(surface.pixels, surface.pixels + std::size_t(w) * h);
            if (baseline.empty()) baseline = pixels;
            else if (pixels != baseline) return fail(label + " selected HFONT changed whole DIB pixels");
            for (std::size_t at = 0; at < pixels.size(); ++at)
                if ((pixels[at] & 0xffffffU) != (expected[at] & 0xffffffU))
                    return fail(label + " original glyph RGB differs at x=" + std::to_string(at % w)
                        + " y=" + std::to_string(at / w) + " actual=" + std::to_string(pixels[at] & 0xffffffU)
                        + " expected=" + std::to_string(expected[at] & 0xffffffU));
        }
        const auto after = state.superWeapons.stateHash();
        if (after != before) return fail(label + " font presentation changed superweapon authority");
        if (capture) {
            if (!writeRuntimeSmokeBmp(pathString(output / (label + ".bmp")), baseline.data(), w, h, error))
                return fail(label + " BMP: " + error);
            ++bitmapCount;
        }
        checkedPixels += expected.size();
        cases << label << ',' << hz << ',' << w << ',' << h << ',' << rows << ',' << owners << ','
            << expected.size() << ",3," << before << ',' << after << ",1,1\n";
        return true;
    };
    // Isolated wrapper: nonblack background exposes both original mask and black 1px shadow.
    std::vector<std::wstring> texts = names;
    texts.insert(texts.end(), {L"0:00", L"0:01", L"0:02", L"0:03", L"12:34", L"99:59"});
    std::wstring longName; for (int i = 0; i < 10; ++i) longName += names.front();
    texts.push_back(longName); texts.push_back(L"\u221e");
    for (std::size_t index = 0; index < texts.size(); ++index) {
        const int w = 384, h = 64; const RECT rect{17, 13, index == 12 ? 81 : 365, 31};
        const COLORREF color = index % 2 ? RGB(231, 61, 173) : RGB(53, 187, 241);
        std::vector<std::uint32_t> expected(std::size_t(w) * h, 0x203040U);
        if (runtimeSuperWeaponTimerTextWidth(state, texts[index]) != raw.width(texts[index]))
            return fail("original independent text width differs");
        raw.paint(expected, w, h, rect, texts[index], color, {0, 0, w, h});
        if (index == 12 && raw.fit(texts[index], 64) == texts[index]) return fail("long-name test did not elide");
        if (!compare("glyph-" + std::to_string(index), 30, w, h, 1, 1, 0x203040U, expected,
            [&](HDC dc) { drawRuntimeSuperWeaponTimerText(dc, rect, state, texts[index], color); }, index == 0 || index == 12)) return false;
        ++directCases;
    }
    SuperWeaponWorldContext context; context.rules = &state.rules; context.superWeaponsAllowed = true;
    context.ownerPowered = [](const std::string&) { return true; };
    const auto setup = [&](int hz, bool manyOwners) {
        state.config.fixedTickHz = hz; context.fixedTickHz = hz; context.tick = 0;
        state.superWeapons.reset(state.rules); state.ownerRemapColors.clear();
        const int total = manyOwners ? runtimeMultiplayerMatchMaximumSlots : int(ids.size());
        for (int i = 0; i < total; ++i) {
            const auto owner = runtimeSkirmishOwnerForSlot(manyOwners ? std::size_t(i) : 0);
            state.ownerRemapColors[toLowerAsciiLocal(owner)] = RGB(50 + (i * 37) % 180, 45 + (i * 71) % 190, 60 + (i * 53) % 175);
            if (!state.superWeapons.grant(owner, ids[std::size_t(i) % ids.size()], -1, &context)) return false;
        }
        context.tick = 1; state.superWeapons.tick(context); return true;
    };
    const auto list = [&](const std::string& label, int w, int h, RECT viewport, bool capture) {
        std::vector<const SuperWeaponSidebarSnapshot*> timers;
        std::set<std::string> owners;
        for (const auto& snapshot : state.superWeapons.sidebarSnapshots())
            if (snapshot.showTimer && snapshot.state != SuperWeaponRuntimeState::Unavailable) {
                timers.push_back(&snapshot); owners.insert(snapshot.owner);
            }
        std::sort(timers.begin(), timers.end(), [](const auto* a, const auto* b) {
            return std::tie(a->owner, a->name, a->superWeaponId) < std::tie(b->owner, b->name, b->superWeaponId); });
        int width = 120;
        for (const auto* snapshot : timers) {
            const auto* rule = state.rules.findSuperWeaponType(snapshot->superWeaponId);
            if (!rule || snapshot->uiName != rule->uiName) return fail(label + " snapshot lost original rule identity");
            const auto name = runtimeMenuLabel(state, rule->uiName, L"");
            if (name.empty() || runtimeSuperWeaponDisplayName(state, *snapshot) != name)
                return fail(label + " actual CSF display name differs");
            width = std::max(width, raw.width(name) + 60);
        }
        width = std::min({320, std::max(0, int(viewport.right - viewport.left) - 16), width});
        const int bottom = int(viewport.bottom) - 8, right = int(viewport.right) - 8;
        const int rows = width < 120 ? 0 : std::min(int(timers.size()), std::max(0, bottom - int(viewport.top) - 8) / 18);
        std::vector<std::uint32_t> expected(std::size_t(w) * h, 0U);
        for (int index = 0; index < rows; ++index) {
            const auto& snapshot = *timers[std::size_t(index)];
            const int top = bottom - rows * 18 + index * 18;
            const auto colorAt = state.ownerRemapColors.find(toLowerAsciiLocal(snapshot.owner));
            if (colorAt == state.ownerRemapColors.end()) return fail(label + " independent owner palette missing");
            const COLORREF color = colorAt->second;
            if (runtimeSuperWeaponTimerColor(state, snapshot.owner) != color) return fail(label + " owner colour mismatch");
            const int hz = std::max(1, state.config.fixedTickHz);
            const int remaining = std::max(0, snapshot.remainingTicks);
            const int seconds = remaining / hz + (remaining % hz != 0);
            std::wostringstream time; time << seconds / 60 << L':' << std::setfill(L'0') << std::setw(2) << seconds % 60;
            const auto name = runtimeMenuLabel(state, snapshot.uiName, L"");
            raw.paint(expected, w, h, {right - width, top, right - 60, top + 18}, name, color, viewport);
            raw.paint(expected, w, h, {right - 54, top, right, top + 18}, time.str(), color, viewport);
        }
        if (label == "narrow-elision" && std::none_of(timers.begin(), timers.end(), [&](const auto* snapshot) {
            const auto text = runtimeMenuLabel(state, snapshot->uiName, L"");
            return raw.fit(text, width - 60) != text; }))
            return fail("narrow viewport did not exercise a real stock-name elision");
        if (!compare(label, state.config.fixedTickHz, w, h, rows, int(owners.size()), 0, expected,
            [&](HDC dc) { drawRuntimeSuperWeaponTimerList(dc, viewport, state); }, capture)) return false;
        ++listCases; return true;
    };
    for (int hz : {15, 30, 60}) {
        if (!setup(hz, false) || state.superWeapons.sidebarSnapshots().size() != ids.size()) return fail("six stock trigger grants");
        if (!list("stock-six-" + std::to_string(hz), 800, 600, {0, 0, 800, 600}, hz == 30)) return false;
        // Original trigger charge API avoids tens of thousands of unrelated map ticks.
        state.superWeapons.reset(state.rules);
        if (!state.superWeapons.grant(runtimeSkirmishOwnerForSlot(0), ids.front(), -1, &context)) return fail("31 tick grant");
        ++context.tick; state.superWeapons.tick(context);
        if (!state.superWeapons.setChargePercent(runtimeSkirmishOwnerForSlot(0), ids.front(), 99, &context)) return fail("stock trigger charge percent");
        int steps = 0;
        const auto* timer = state.superWeapons.findSidebarSnapshot(runtimeSkirmishOwnerForSlot(0), ids.front());
        const int limit = timer ? timer->rechargeTicks / 100 + 3 : 0;
        do { ++context.tick; state.superWeapons.tick(context); ++steps;
            timer = state.superWeapons.findSidebarSnapshot(runtimeSkirmishOwnerForSlot(0), ids.front());
        } while (timer && timer->remainingTicks > 31 && steps <= limit);
        if (!timer || timer->remainingTicks != 31 || runtimeSuperWeaponRemainingSeconds(*timer, hz) != (hz == 15 ? 3 : hz == 30 ? 2 : 1))
            return fail("actual 31 tick ceiling sample");
        if (!list("ceiling-31-" + std::to_string(hz), 800, 600, {0, 0, 800, 600}, false)) return false;
        if (!state.superWeapons.forceReady(runtimeSkirmishOwnerForSlot(0), ids.front(), &context)) return fail("ready trigger API");
        ++context.tick; state.superWeapons.tick(context);
        timer = state.superWeapons.findSidebarSnapshot(runtimeSkirmishOwnerForSlot(0), ids.front());
        if (!timer || timer->state != SuperWeaponRuntimeState::Ready || timer->remainingTicks != 0) return fail("actual Ready timer");
        if (!list("ready-" + std::to_string(hz), 800, 600, {0, 0, 800, 600}, false)) return false;
    }
    if (!setup(30, true) || state.superWeapons.sidebarSnapshots().size() != std::size_t(runtimeMultiplayerMatchMaximumSlots))
        return fail("all edition owner grants including twelfth owner");
    state.localRadarColor = RGB(1, 2, 3); // Must not override an owner's HUD identity.
    if (!list("all-owners-800", 800, 600, {10, 20, 790, 580}, true)
        || !list("all-owners-1280", 1280, 720, {0, 0, 1280, 720}, false)
        || !list("all-owners-1600", 1600, 900, {0, 0, 1600, 900}, false)
        || !list("narrow-elision", 400, 180, {0, 0, 136, 180}, false)
        || !list("short-height", 400, 80, {5, 7, 395, 47}, false)
        || !list("short-width", 400, 80, {5, 7, 124, 77}, false)) return false;
    state.superWeapons.reset(state.rules);
    if (!list("empty", 400, 80, {0, 0, 400, 80}, false)) return false;
    const SuperWeaponRule* hidden = nullptr;
    for (const auto& rule : state.rules.getSuperWeaponTypes())
        if (!rule.showTimer && !rule.postClick) { hidden = &rule; break; }
    if (!hidden || !state.superWeapons.grant(runtimeSkirmishOwnerForSlot(0), hidden->id, -1, &context)) return fail("original hidden timer rule");
    ++context.tick; state.superWeapons.tick(context);
    if (state.superWeapons.sidebarSnapshots().empty()) return fail("hidden rule did not create a real snapshot");
    if (!list("hidden", 400, 80, {0, 0, 400, 80}, false)) return false;
    cases.flush(); source.flush();
    if (!cases || !source) return fail("evidence CSV flush");
    std::cout << "Superweapon timer font smoke passed: stockNames=6 clocks=3 directCases=" << directCases
        << " listCases=" << listCases << " rawRgbComparedPixels=" << checkedPixels << " fontVariants=3 bmp=" << bitmapCount
        << " maxOwners=" << runtimeMultiplayerMatchMaximumSlots
        << " actual31TickSamples=3 showTimerHidden=1 originalFileBytes=" << raw.bytes.size()
        << " superWeaponStatePreserved=1 PASS\n";
    return true;
}
