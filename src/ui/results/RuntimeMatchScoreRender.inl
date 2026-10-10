struct RuntimeMatchScoreRow {
    std::string owner;
    std::wstring name;
    RuntimeMatchScoreEntry statistics;
    COLORREF color = RGB(220, 220, 220);
    bool local = false;
    bool defeated = false;
    bool winner = false;
};

std::vector<RuntimeMatchScoreRow> runtimeMatchScoreRows(const RuntimeWindowState& state) {
    std::vector<RuntimeMatchScoreRow> rows;
    std::set<std::string> added;
    const auto addOwner = [&](const std::string& owner, const std::wstring& name) {
        const auto key = runtimeCanonicalHumanHouseOwner(owner);
        if (runtimeOwnerIsNeutralForAutoTarget(owner) || runtimeObserverOwner(state, owner)
            || !added.insert(key).second) return;
        RuntimeMatchScoreRow row;
        row.owner = owner;
        row.name = name.empty() ? widenUtf8(owner) : name;
        const auto score = state.matchScores.find(key);
        if (score != state.matchScores.end()) row.statistics = score->second;
        row.color = runtimeRadarColorForOwner(state, owner);
        row.local = !state.multiplayerLocalSpectator
            && key == runtimeCanonicalHumanHouseOwner(state.economy.owner);
        row.defeated = state.multiplayerDefeatedOwners.contains(key)
            || runtimeSkirmishOwnerDefeated(state, owner);
        if (row.local) row.defeated = !state.skirmishVictory;
        row.winner = state.multiplayerWinningOwners.contains(key)
            || (!state.config.multiplayerActive && state.skirmishVictory
                && runtimeOwnersAreAllied(state, state.economy.owner, owner));
        if (row.winner) row.defeated = false;
        rows.push_back(std::move(row));
    };
    for (std::size_t slot = 0; slot < state.config.playerSlots.size(); ++slot) {
        const auto& player = state.config.playerSlots[slot];
        if (!player.enabled) continue;
        addOwner(runtimeSkirmishOwnerForSlot(slot), player.name.empty()
            ? runtimeMenuLabel(state, "GUI:Player" + std::to_string(slot + 1), L"玩家")
            : widenUtf8(player.name));
    }
    // Slots retain eliminated players even when no world objects remain.
    // Map-authored participants and older saves can instead identify houses.
    if (rows.empty()) {
        for (const auto& object : state.world.getObjects()) addOwner(object.owner, {});
        if (!state.multiplayerLocalSpectator) addOwner(state.economy.owner, {});
    }
    for (const auto& [owner, score] : state.matchScores) {
        (void)score;
        addOwner(owner, {});
    }
    if (state.multiplayerWinningOwners.empty()) {
        const auto survivor = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return !row.defeated; });
        if (survivor != rows.end() && std::all_of(rows.begin(), rows.end(), [&](const auto& row) {
                return row.defeated || runtimeOwnersAreAllied(state, survivor->owner, row.owner);
            })) {
            for (auto& row : rows) if (!row.defeated) row.winner = true;
        }
    }
    return rows;
}

RECT runtimeMatchScoreScaledRect(const RECT& clientRect, const RECT& base) {
    const double scale = std::min(static_cast<double>(clientRect.right - clientRect.left) / 800.0,
        static_cast<double>(clientRect.bottom - clientRect.top) / 600.0);
    const int left = clientRect.left + static_cast<int>((clientRect.right - clientRect.left - 800 * scale) / 2);
    const int top = clientRect.top + static_cast<int>((clientRect.bottom - clientRect.top - 600 * scale) / 2);
    return {left + static_cast<int>(std::lround(base.left * scale)), top + static_cast<int>(std::lround(base.top * scale)),
        left + static_cast<int>(std::lround(base.right * scale)), top + static_cast<int>(std::lround(base.bottom * scale))};
}

// GAMEMD dialog 264: the left 632x568 score artwork plus the shared
// 168-pixel shell sidebar. Its ten 440x36 strips hold outcome, headings,
// and eight players; control 1745 is the Continue button on the right.
#include "ui/results/RuntimeResultBitFont.inl"
#include "ui/results/RuntimeMatchScoreAssets.inl"

RECT runtimeMatchScoreContinueRect(const RECT& clientRect) {
    return runtimeMatchScoreScaledRect(clientRect, RECT{638, 530, 800, 568});
}

void drawRuntimeMatchScorePage(HDC dc, const RECT& clientRect, RuntimeWindowState& state) {
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    FillRect(dc, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    std::string prefix = runtimeCountryIsYuri(state.config.playerCountry) ? "mpy"
        : runtimeCountryIsSoviet(state.config.playerCountry) ? "mps" : "mpa";
    const auto native = [&](const std::string& id, const std::string& palette, int x, int y,
                            int frame = 0, bool transparent = false) {
        const auto* image = runtimeMatchScoreFrame(state, id, palette, frame);
        if (!image) return false;
        const int width = image->image.globalWidth > 0 ? image->image.globalWidth : image->image.width;
        const int height = image->image.globalHeight > 0 ? image->image.globalHeight : image->image.height;
        const RECT rect = runtimeMatchScoreScaledRect(clientRect, RECT{x, y, x + width, y + height});
        return transparent
            ? drawRuntimeUiShpFrameCanvasTransparent(dc, state, "match_score", id, frame, palette, rect, true)
            : drawRuntimeUiShpFrame(dc, state, "match_score", id, frame, palette, rect, true, true);
    };
    if (!native(prefix + "scrnl", prefix + "scrn", 0, 0)) {
        prefix = "mpy";
        native(prefix + "scrnl", prefix + "scrn", 0, 0);
    }
    native("lwscrnl", "shell", 0, 568);
    native("sdtp", "shell", 632, 0);
    native("sdwrnanm", "shell", 670, 48, 0, true);
    for (int slot = 0; slot < 8; ++slot) {
        native("sdbtnbkgd", "shell", 632, 199 + slot * 42);
        native("sdbtnanm", "sdbtnanm", 644, 199 + slot * 42, 0, true);
    }
    native("sdbtm", "shell", 632, 535);
    const int buttonFrame = state.resultPresentation.continuePressed ? 2
        : state.resultPresentation.continueHovered ? 1 : 0;
    const bool originalButton = native("mnbttn", "mainbttn", 653, 537, buttonFrame, true);
    if (!originalButton) {
        const RECT button = runtimeMatchScoreContinueRect(clientRect);
        HBRUSH brush = CreateSolidBrush(state.resultPresentation.continuePressed ? RGB(90, 23, 16) : RGB(65, 12, 8));
        FillRect(dc, &button, brush); DeleteObject(brush);
        FrameRect(dc, &button, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }
    const auto& bitFont = runtimeResultBitFont(state);
    const double scale = std::min(static_cast<double>(clientRect.right - clientRect.left) / 800.0,
        static_cast<double>(clientRect.bottom - clientRect.top) / 600.0);
    HFONT fallbackFont = bitFont.bytes.empty() ? CreateFontW(-std::max(9, static_cast<int>(std::lround(16 * scale))),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI") : nullptr;
    const auto draw = [&](const std::wstring& text, RECT base, COLORREF color, bool centered = false) {
        if (!bitFont.bytes.empty()) { bitFont.draw(dc, clientRect, base, text, color, centered); return; }
        RECT rect = runtimeMatchScoreScaledRect(clientRect, base);
        HGDIOBJ old = SelectObject(dc, fallbackFont);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
            (centered ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    };
    const COLORREF gold = RGB(255, 216, 102);
    draw(runtimeMenuLabel(state, state.config.multiplayerActive ? "GUI:MultiplayerScore" : "GUI:SkirmishScore",
        L"遭遇战积分"), RECT{638, 2, 798, 28}, gold, true);
    draw(runtimeMenuLabel(state, "GUI:Continue", L"继续"), RECT{653, 537, 779, 562}, gold, true);

    // PCX strips carry their own palettes; using the background's palette
    // distorts the authored translucent texture and produces coloured bands.
    for (int strip = 0; strip < 10; ++strip) {
        const std::string id = prefix + "scrnlbar" + (strip < 9 ? "0" : "") + std::to_string(strip + 1);
        const RECT rect = runtimeMatchScoreScaledRect(clientRect, RECT{94, 117 + strip * 36, 534, 153 + strip * 36});
        if (!runtimeMatchScoreFrame(state, id, prefix + "scrn")
            || !drawRuntimeUiShpFrame(dc, state, "match_score", id, 0, prefix + "scrn", rect, true, true)) {
            // Missing optional strips keep the statistics readable.
            HBRUSH brush = CreateSolidBrush(RGB(31, 25, 21));
            FillRect(dc, &rect, brush); DeleteObject(brush);
        }
    }
    draw((state.multiplayerLocalSpectator || runtimeLocalObserver(state.config)) ? L"对局结束"
        : runtimeMenuLabel(state, state.skirmishVictory ? "TXT_VICTORIOUS" : "TXT_LOST",
            state.skirmishVictory ? L"胜利" : L"失败"), RECT{99, 121, 369, 148}, gold);
    draw(runtimeCampaignScoreTimeText(state.tick, state.config.fixedTickHz), RECT{372, 121, 530, 148}, gold, true);
    constexpr std::array<int, 6> columns{{99, 220, 303, 385, 462, 530}};
    const std::array<std::wstring, 5> headings{{runtimeMenuLabel(state, "GUI:Player", L"玩家"),
        runtimeMenuLabel(state, "GUI:Kills", L"摧毁数"), runtimeMenuLabel(state, "GUI:Losses", L"损失数"),
        runtimeMenuLabel(state, "GUI:Built", L"建造数"), runtimeMenuLabel(state, "GUI:Score", L"分数")}};
    for (int column = 0; column < 5; ++column)
        draw(headings[column], RECT{columns[column], 157, columns[column + 1] - 5, 184}, gold, column != 0);
    const auto rows = runtimeMatchScoreRows(state);
    constexpr std::size_t pageSize = 8;
    const std::size_t pageCount = std::max<std::size_t>(1, (rows.size() + pageSize - 1) / pageSize);
    state.resultPresentation.scorePage = std::clamp(state.resultPresentation.scorePage, 0, static_cast<int>(pageCount - 1));
    const std::size_t first = static_cast<std::size_t>(state.resultPresentation.scorePage) * pageSize;
    for (std::size_t index = first; index < std::min(rows.size(), first + pageSize); ++index) {
        const auto& row = rows[index];
        const int top = 193 + static_cast<int>(index - first) * 36;
        draw(row.name, RECT{columns[0], top, columns[1] - 5, top + 27}, row.color);
        const std::array<std::wstring, 4> values{{
            std::to_wstring(static_cast<long long>(row.statistics.unitsDestroyed) + row.statistics.structuresDestroyed),
            std::to_wstring(static_cast<long long>(row.statistics.unitsLost) + row.statistics.structuresLost),
            std::to_wstring(row.statistics.built), std::to_wstring(row.statistics.score)}};
        for (int column = 1; column < 5; ++column)
            draw(values[column - 1], RECT{columns[column], top, columns[column + 1] - 5, top + 27}, row.color, true);
    }
    if (pageCount > 1) draw(L"← / →  " + std::to_wstring(state.resultPresentation.scorePage + 1)
        + L" / " + std::to_wstring(pageCount), RECT{12, 572, 620, 596}, gold);
    if (fallbackFont) DeleteObject(fallbackFont);
    RestoreDC(dc, saved);
}
