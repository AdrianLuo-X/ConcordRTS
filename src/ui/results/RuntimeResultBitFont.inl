// The native font keeps the original drawing entry. The inherited data and
// process cache are shared by result, diplomacy, beacon and loading views.
struct RuntimeResultBitFont : concord::ui::RuntimeResultBitmapFontResource {
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
    return concord::ui::loadRuntimeResultBitmapFontResource<RuntimeResultBitFont>(
        state.config.modRootPath, state.config.resourceRootPath);
}
