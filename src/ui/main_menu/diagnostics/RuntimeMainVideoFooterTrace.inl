// Explicit owned CLI borrow; normal playback leaves it null.
struct MainVideoFooterTraceCall {
 RuntimeInputPixelRect client,footer;
 std::wstring text;
 std::uint32_t color=0,format=0;
 std::int32_t count=-1;
};
bool writeMainVideoFooterTrace(const fs::path& path,const std::vector<MainVideoFooterTraceCall>& calls){
 if(calls.empty()||calls.size()>0xffffffffU)return false;std::ofstream out(path,std::ios::binary);if(!out)return false;
 out.write("W017T001",8);const auto u32=[&](std::uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);};
 const auto rect=[&](RuntimeInputPixelRect r){for(auto v:{r.left,r.top,r.right,r.bottom})u32(static_cast<std::uint32_t>(v));};
 u32(static_cast<std::uint32_t>(calls.size()));for(const auto&c:calls){rect(c.client);rect(c.footer);u32(c.color);u32(c.format);u32(static_cast<std::uint32_t>(c.count));u32(static_cast<std::uint32_t>(c.text.size()));for(wchar_t ch:c.text){const auto unit=static_cast<std::uint16_t>(ch);out.write(reinterpret_cast<const char*>(&unit),2);}}return bool(out);
}
