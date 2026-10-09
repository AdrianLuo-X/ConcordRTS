// Superweapon HUD rectangles are native screen pixels, independent of the
// 800x600 result-page transform used by the shared GAME.FNT loader.
int runtimeSuperWeaponTimerTextWidth(const RuntimeWindowState& state, const std::wstring& text) {
    return runtimeResultBitFont(state).measure(text);
}

void drawRuntimeSuperWeaponTimerText(HDC dc, RECT rect, const RuntimeWindowState& state,
    const std::wstring& value, COLORREF color) {
    const auto& font = runtimeResultBitFont(state);
    if (font.bytes.empty() || value.empty() || rect.right <= rect.left || rect.bottom <= rect.top) return;
    std::wstring text = value;
    const int width = static_cast<int>(rect.right - rect.left);
    if (font.measure(text) > width) {
        while (!text.empty() && font.measure(text + L"...") > width) text.pop_back();
        text += L"...";
    }
    const int left = static_cast<int>(rect.right) - font.measure(text);
    const int top = static_cast<int>(rect.top)
        + std::max(0, static_cast<int>(rect.bottom - rect.top) - static_cast<int>(font.height)) / 2;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    for (int offset : {1, 0}) {
        HBRUSH brush = CreateSolidBrush(offset ? RGB(0, 0, 0) : color);
        int cursor = left;
        for (wchar_t character : text) {
            const auto* glyph = font.glyph(character);
            if (!glyph) { cursor += 5; continue; }
            for (unsigned y = 0; y < font.rows; ++y) {
                const auto* row = glyph + 1 + y * font.stride;
                for (int x = 0; x < glyph[0];) {
                    if (!(row[x / 8] & (0x80 >> (x % 8)))) { ++x; continue; }
                    const int first = x++;
                    while (x < glyph[0] && (row[x / 8] & (0x80 >> (x % 8)))) ++x;
                    const RECT ink{cursor + first + offset, top + static_cast<int>(y) + offset,
                        cursor + x + offset, top + static_cast<int>(y) + 1 + offset};
                    FillRect(dc, &ink, brush);
                }
            }
            cursor += glyph[0] + 1;
        }
        DeleteObject(brush);
    }
    RestoreDC(dc, saved);
}
