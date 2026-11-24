#pragma once

#include "core/results/RuntimeMatchScore.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace concord::ui {

struct RuntimeMatchScoreRow {
    std::string owner;
    std::wstring name;
    RuntimeMatchScoreEntry statistics;
    std::uint32_t color = RuntimeRgbColorValue::fromChannels(220, 220, 220).packedRgb();
    bool local = false;
    bool defeated = false;
    bool winner = false;
};

struct RuntimeMatchScorePageView {
    std::string_view artPrefix;
    bool continuePressed = false;
    bool continueHovered = false;
    bool multiplayer = false;
    bool observer = false;
    bool victory = false;
};

inline constexpr RuntimeInputPixelRect matchScoreContinueBounds{638, 530, 800, 568};

inline double matchScoreScale(const RuntimeInputPixelRect& client) {
    return std::min(static_cast<double>(client.right - client.left) / 800.0,
        static_cast<double>(client.bottom - client.top) / 600.0);
}

inline RuntimeInputPixelRect scaleMatchScoreRect(const RuntimeInputPixelRect& client,
    const RuntimeInputPixelRect& base) {
    const double scale = matchScoreScale(client);
    const int left = client.left + static_cast<int>((client.right - client.left - 800 * scale) / 2);
    const int top = client.top + static_cast<int>((client.bottom - client.top - 600 * scale) / 2);
    return {left + static_cast<int>(std::lround(base.left * scale)),
        top + static_cast<int>(std::lround(base.top * scale)),
        left + static_cast<int>(std::lround(base.right * scale)),
        top + static_cast<int>(std::lround(base.bottom * scale))};
}

// The caller owns page navigation and provides this frame's resource and text drawing.
template<class Painter>
void drawMatchScorePage(const RuntimeMatchScorePageView& view, int& page, Painter& painter) {
    std::string prefix(view.artPrefix);
    if (!painter.image(prefix + "scrnl", prefix + "scrn", 0, 0)) {
        prefix = "mpy";
        painter.image(prefix + "scrnl", prefix + "scrn", 0, 0);
    }
    painter.image("lwscrnl", "shell", 0, 568);
    painter.image("sdtp", "shell", 632, 0);
    painter.image("sdwrnanm", "shell", 670, 48, 0, true);
    for (int slot = 0; slot < 8; ++slot) {
        painter.image("sdbtnbkgd", "shell", 632, 199 + slot * 42);
        painter.image("sdbtnanm", "sdbtnanm", 644, 199 + slot * 42, 0, true);
    }
    painter.image("sdbtm", "shell", 632, 535);
    const int buttonFrame = view.continuePressed ? 2 : view.continueHovered ? 1 : 0;
    if (!painter.image("mnbttn", "mainbttn", 653, 537, buttonFrame, true)) {
        painter.fallbackButton(view.continuePressed);
    }
    painter.prepareText();

    constexpr auto gold = RuntimeRgbColorValue::fromChannels(255, 216, 102).packedRgb();
    painter.text(painter.label(view.multiplayer ? "GUI:MultiplayerScore" : "GUI:SkirmishScore",
        L"遭遇战积分"), {638, 2, 798, 28}, gold, true);
    painter.text(painter.label("GUI:Continue", L"继续"), {653, 537, 779, 562}, gold, true);

    // The PCX strips carry their own palettes.
    for (int strip = 0; strip < 10; ++strip) {
        const std::string id = prefix + "scrnlbar" + (strip < 9 ? "0" : "") + std::to_string(strip + 1);
        painter.strip(id, prefix + "scrn", {94, 117 + strip * 36, 534, 153 + strip * 36});
    }
    painter.text(view.observer ? L"对局结束"
        : painter.label(view.victory ? "TXT_VICTORIOUS" : "TXT_LOST", view.victory ? L"胜利" : L"失败"),
        {99, 121, 369, 148}, gold);
    painter.text(painter.elapsedTime(), {372, 121, 530, 148}, gold, true);
    constexpr std::array<int, 6> columns{{99, 220, 303, 385, 462, 530}};
    const std::array<std::wstring, 5> headings{{painter.label("GUI:Player", L"玩家"),
        painter.label("GUI:Kills", L"摧毁数"), painter.label("GUI:Losses", L"损失数"),
        painter.label("GUI:Built", L"建造数"), painter.label("GUI:Score", L"分数")}};
    for (int column = 0; column < 5; ++column) {
        painter.text(headings[column], {columns[column], 157, columns[column + 1] - 5, 184}, gold, column != 0);
    }
    const auto rows = painter.rows();
    constexpr std::size_t pageSize = 8;
    const std::size_t pageCount = std::max<std::size_t>(1, (rows.size() + pageSize - 1) / pageSize);
    page = std::clamp(page, 0, static_cast<int>(pageCount - 1));
    const std::size_t first = static_cast<std::size_t>(page) * pageSize;
    for (std::size_t index = first; index < std::min(rows.size(), first + pageSize); ++index) {
        const auto& row = rows[index];
        const int top = 193 + static_cast<int>(index - first) * 36;
        painter.text(row.name, {columns[0], top, columns[1] - 5, top + 27}, row.color);
        const std::array<std::wstring, 4> values{{
            std::to_wstring(static_cast<long long>(row.statistics.unitsDestroyed) + row.statistics.structuresDestroyed),
            std::to_wstring(static_cast<long long>(row.statistics.unitsLost) + row.statistics.structuresLost),
            std::to_wstring(row.statistics.built), std::to_wstring(row.statistics.score)}};
        for (int column = 1; column < 5; ++column) {
            painter.text(values[column - 1], {columns[column], top, columns[column + 1] - 5, top + 27}, row.color, true);
        }
    }
    if (pageCount > 1) {
        painter.text(L"← / →  " + std::to_wstring(page + 1) + L" / " + std::to_wstring(pageCount),
            {12, 572, 620, 596}, gold);
    }
}

} // namespace concord::ui
