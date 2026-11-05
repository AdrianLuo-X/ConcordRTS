// Private W017 CLI evidence. Normal production keeps the typed observer null.
struct MainSystemFontTraceCall {
    std::wstring text;
    RuntimeInputPixelRect rect;
    std::uint32_t role=0, format=0, dark=0, middle=0;
};
bool writeMainSystemFontTrace(const fs::path& path,
    const std::vector<MainSystemFontTraceCall>& calls) {
    if(calls.empty() || calls.size()>std::numeric_limits<std::uint32_t>::max())return false;
    std::ofstream out(path,std::ios::binary);if(!out)return false;
    const char magic[8]{'W','0','1','7','M','0','0','1'};out.write(magic,8);
    const auto u32=[&](std::uint32_t value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    const auto i32=[&](std::int32_t value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    u32(kWidth);u32(kHeight);u32(static_cast<std::uint32_t>(calls.size()));
    for(const auto& call:calls){
        if(call.text.size()>std::numeric_limits<std::uint32_t>::max())return false;
        u32(call.role);u32(call.format);u32(call.dark);u32(call.middle);
        i32(call.rect.left);i32(call.rect.top);i32(call.rect.right);i32(call.rect.bottom);
        u32(static_cast<std::uint32_t>(call.text.size()));
        for(const wchar_t character:call.text){const auto unit=static_cast<std::uint16_t>(character);out.write(reinterpret_cast<const char*>(&unit),sizeof(unit));}
    }
    return bool(out);
}
