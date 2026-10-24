// The same installed Unicode bitmap font used by the retail menu. Draw runs
// through GDI so scaled text and the UI compositor share one ordered surface.
struct RuntimeResultBitFont {

    std::vector<unsigned char> bytes;
    unsigned stride = 0, rows = 0, height = 0, count = 0, recordSize = 0;


    bool load(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        concord::assets::WestwoodFontMetrics metrics{stride, rows, height, count, recordSize};
        const bool sane = concord::assets::decodeWestwoodFontHeader(bytes, metrics);
        stride = metrics.stride; rows = metrics.bitmapRows; height = metrics.lineHeight;
        count = metrics.glyphCount; recordSize = metrics.recordSize;
        if (!sane) { bytes.clear(); return false; }
        return true;
    }
    const unsigned char* glyph(wchar_t character) const {
        if (bytes.empty()) return nullptr;
        return concord::assets::westwoodFontGlyph(bytes,
            {stride, rows, height, count, recordSize}, static_cast<std::uint16_t>(character),
            concord::assets::WestwoodGlyphFallback::MissingReferenceOnly);
    }
    int measure(const std::wstring& text) const {
        int width = 0;
        for (const auto c : text) { const auto* g = glyph(c); width += g ? g[0] + 1 : 5; }
        return std::max(0, width - 1);
    }
    void draw(HDC dc, const RECT& client, RECT base, std::wstring text, COLORREF color, bool centered) const {
        if (bytes.empty() || text.empty()) return;
        if (measure(text) > base.right - base.left) {
            while (!text.empty() && measure(text + L"...") > base.right - base.left) text.pop_back();
            text += L"...";
        }
        const int startX = base.left + (centered ? std::max(0L, base.right - base.left - measure(text)) / 2 : 0);
        const int startY = base.top + std::max(0L, base.bottom - base.top - static_cast<LONG>(height)) / 2;
        const RECT clip = runtimeMatchScoreScaledRect(client, base);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
        for (int pass : {1, 0}) {
            HBRUSH brush = CreateSolidBrush(pass ? RGB(18, 10, 0) : color);
            int cursor = startX;
            for (const auto c : text) {
                const auto* g = glyph(c);
                if (!g) { cursor += 5; continue; }
                for (unsigned y = 0; y < rows; ++y) {
                    const auto* row = g + 1 + y * stride;
                    for (int x = 0; x < g[0];) {
                        if (!(row[x / 8] & (0x80 >> (x % 8)))) { ++x; continue; }
                        const int left = x++;
                        while (x < g[0] && (row[x / 8] & (0x80 >> (x % 8)))) ++x;
                        const RECT run = runtimeMatchScoreScaledRect(client, RECT{
                            cursor + left + pass, startY + static_cast<int>(y) + pass,
                            cursor + x + pass, startY + static_cast<int>(y) + 1 + pass});
                        FillRect(dc, &run, brush);
                    }
                }
                cursor += g[0] + 1;
            }
            DeleteObject(brush);
        }
        RestoreDC(dc, saved);
    }
};

const RuntimeResultBitFont& runtimeResultBitFont(const RuntimeWindowState& state) {
    static std::map<std::string, RuntimeResultBitFont> fonts;
    const std::string key = state.config.modRootPath + "|" + state.config.resourceRootPath;
    const auto [entry, inserted] = fonts.try_emplace(key);
    if (inserted) {
        for (const auto& path : {
                std::filesystem::path(state.config.modRootPath) / "game.fnt",
                std::filesystem::path(state.config.resourceRootPath) / "GameMD" / "game.fnt",
                std::filesystem::path(state.config.resourceRootPath) / "Game" / "game.fnt"}) {
            if (entry->second.load(path)) break;
        }
    }
    return entry->second;
}
