using concord::ui::RuntimeMatchScoreRow;

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

RuntimeInputPixelRect runtimeMatchScorePixelRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

RECT runtimeMatchScoreNativeRect(const RuntimeInputPixelRect& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

RECT runtimeMatchScoreScaledRect(const RECT& clientRect, const RECT& base) {
    return runtimeMatchScoreNativeRect(concord::ui::scaleMatchScoreRect(
        runtimeMatchScorePixelRect(clientRect), runtimeMatchScorePixelRect(base)));
}

// GAMEMD dialog 264: the left 632x568 score artwork plus the shared
// 168-pixel shell sidebar. Its ten 440x36 strips hold outcome, headings,
// and eight players; control 1745 is the Continue button on the right.
#include "ui/results/RuntimeResultBitFont.inl"
#include "ui/results/RuntimeMatchScoreAssets.inl"

RECT runtimeMatchScoreContinueRect(const RECT& clientRect) {
    return runtimeMatchScoreNativeRect(concord::ui::scaleMatchScoreRect(
        runtimeMatchScorePixelRect(clientRect), concord::ui::matchScoreContinueBounds));
}

void drawRuntimeMatchScorePage(HDC dc, const RECT& clientRect, RuntimeWindowState& state) {
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    FillRect(dc, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const concord::ui::RuntimeMatchScorePageView view{
        runtimeCountryIsYuri(state.config.playerCountry) ? "mpy" : runtimeCountryIsSoviet(state.config.playerCountry) ? "mps" : "mpa",
        state.resultPresentation.continuePressed,
        state.resultPresentation.continueHovered,
        state.config.multiplayerActive,
        state.multiplayerLocalSpectator || runtimeLocalObserver(state.config),
        state.skirmishVictory};
    const auto drawImage = [&](const std::string& id, const std::string& palette,
        int x, int y, int frame = 0, bool transparent = false) {
        const auto* image = runtimeMatchScoreFrame(state, id, palette, frame);
        if (!image) return false;
        const int width = image->image.globalWidth > 0 ? image->image.globalWidth : image->image.width;
        const int height = image->image.globalHeight > 0 ? image->image.globalHeight : image->image.height;
        const RECT rect = runtimeMatchScoreScaledRect(clientRect, RECT{x, y, x + width, y + height});
        return transparent
            ? drawRuntimeUiShpFrameCanvasTransparent(dc, state, "match_score", id, frame, palette, rect, true)
            : drawRuntimeUiShpFrame(dc, state, "match_score", id, frame, palette, rect, true, true);
    };
    const auto drawButton = [&](bool pressed) {
        const RECT button = runtimeMatchScoreContinueRect(clientRect);
        HBRUSH brush = CreateSolidBrush(pressed ? RGB(90, 23, 16) : RGB(65, 12, 8));
        FillRect(dc, &button, brush);
        DeleteObject(brush);
        FrameRect(dc, &button, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    };
    const RuntimeResultBitFont* bitFont = nullptr;
    int systemFontHeight = 0;
    concord::ui::MatchResultSystemFontWin32 systemFont;
    const auto loadFont = [&] {
        bitFont = state.resultSystemFontQualification ? state.resultSystemFontQualification : &runtimeResultBitFont(state);
        const double scale = concord::ui::matchScoreScale(runtimeMatchScorePixelRect(clientRect));
        systemFontHeight = bitFont->bytes.empty()
            ? -std::max(9, static_cast<int>(std::lround(16 * scale))) : 0;
        if (bitFont->bytes.empty()) systemFont.initialize(systemFontHeight);
    };
    const auto drawText = [&](const std::wstring& text, const RuntimeInputPixelRect& bounds,
        std::uint32_t color, bool centered = false) {
        const RECT base = runtimeMatchScoreNativeRect(bounds);
        if (!bitFont->bytes.empty()) {
            bitFont->draw(dc, clientRect, base, text, color, centered, state.resultBitmapTrace);
            return;
        }
        const RECT rect = runtimeMatchScoreScaledRect(clientRect, base);
        const auto format = (centered ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
        if (state.resultSystemFontTrace) {
            state.resultSystemFontTrace->push_back({text, runtimeMatchScorePixelRect(clientRect), bounds,
                runtimeMatchScorePixelRect(rect), systemFontHeight, color, static_cast<std::uint32_t>(format)});
        }
        systemFont.draw(dc, concord::ui::MatchResultSystemWideTextRequest::fromPacked(
            text, runtimeMatchScorePixelRect(rect), format, color));
    };
    const auto drawStrip = [&](const std::string& id, const std::string& palette, const RuntimeInputPixelRect& bounds) {
        const RECT rect = runtimeMatchScoreScaledRect(clientRect, runtimeMatchScoreNativeRect(bounds));
        if (!runtimeMatchScoreFrame(state, id, palette)
            || !drawRuntimeUiShpFrame(dc, state, "match_score", id, 0, palette, rect, true, true)) {
            // Missing optional strips keep the statistics readable.
            HBRUSH brush = CreateSolidBrush(RGB(31, 25, 21));
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }
    };
    const auto readLabel = [&](const std::string& key, const wchar_t* fallback) {
        return runtimeMenuLabel(state, key, fallback);
    };
    const auto readTime = [&] {
        return runtimeCampaignScoreTimeText(state.tick, state.config.fixedTickHz);
    };
    const auto readRows = [&] {
        return runtimeMatchScoreRows(state);
    };
    struct {
        const decltype(drawImage)& image;
        const decltype(drawButton)& fallbackButton;
        const decltype(loadFont)& prepareText;
        const decltype(drawText)& text;
        const decltype(drawStrip)& strip;
        const decltype(readLabel)& label;
        const decltype(readTime)& elapsedTime;
        const decltype(readRows)& rows;
    } painter{drawImage, drawButton, loadFont, drawText, drawStrip, readLabel, readTime, readRows};
    concord::ui::drawMatchScorePage(view, state.resultPresentation.scorePage, painter);
    systemFont.reset();
    RestoreDC(dc, saved);
}
