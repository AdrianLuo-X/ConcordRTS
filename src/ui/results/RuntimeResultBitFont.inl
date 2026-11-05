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
    concord::ui::ResultBitmapFontView view() const {
        return {bytes,{stride,rows,height,count,recordSize}};
    }
    int measure(const std::wstring& text) const {
        return concord::ui::measureResultBitmapText(view(),std::wstring_view(text));
    }
    void draw(HDC dc, const RECT& client, RECT base, std::wstring text, COLORREF color, bool centered,
        std::vector<RuntimeResultBitmapTraceCall>* trace = nullptr) const {
        if (bytes.empty() || text.empty()) return;
        if (trace != nullptr) trace->push_back({text, {client.left, client.top, client.right, client.bottom},
            {base.left, base.top, base.right, base.bottom}, color, centered});
        concord::ui::drawResultBitmapTextWin32(view(),dc,client,base,std::move(text),color,
            centered,&runtimeMatchScoreScaledRect);
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
