// Private CLI evidence only. No global observer, no parser, no gameplay state.
struct SkirmishBitmapTextTraceCall {
    std::wstring text;
    RuntimeInputPixelRect rect;
    bool centered = false;
    bool verticallyCentered = false;
    std::uint32_t packedRgb = 0;
};
bool writeSkirmishBitmapTextTrace(const fs::path& path,
    const std::vector<unsigned char>& fontBytes,
    const std::vector<SkirmishBitmapTextTraceCall>& calls, std::string& error) {
    if (calls.empty() || fontBytes.size() > std::numeric_limits<std::uint32_t>::max()
        || calls.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "empty/oversized bitmap trace"; return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "cannot create bitmap trace: " + path.string(); return false; }
    const char magic[8]{'W','0','1','5','T','0','0','1'}; out.write(magic, 8);
    const auto u32 = [&](std::uint32_t value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    const auto i32 = [&](std::int32_t value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    u32(static_cast<std::uint32_t>(kBaseWidth)); u32(static_cast<std::uint32_t>(kBaseHeight));
    u32(static_cast<std::uint32_t>(fontBytes.size())); u32(static_cast<std::uint32_t>(calls.size()));
    out.write(reinterpret_cast<const char*>(fontBytes.data()), static_cast<std::streamsize>(fontBytes.size()));
    for (const auto& call : calls) {
        if (call.text.size() > std::numeric_limits<std::uint32_t>::max()) { error = "oversized trace label"; return false; }
        u32(static_cast<std::uint32_t>(call.text.size()));
        i32(call.rect.left); i32(call.rect.top); i32(call.rect.right); i32(call.rect.bottom);
        u32((call.centered ? 1U : 0U) | (call.verticallyCentered ? 2U : 0U)); u32(call.packedRgb);
        for (const wchar_t character : call.text) {
            const auto unit = static_cast<std::uint16_t>(character);
            out.write(reinterpret_cast<const char*>(&unit), sizeof(unit));
        }
    }
    if (!out) { error = "bitmap trace write failed"; return false; }
    std::cout << "Skirmish bitmap text trace: calls=" << calls.size()
              << " fontBytes=" << fontBytes.size() << " path=" << path.string() << "\n";
    return true;
}
