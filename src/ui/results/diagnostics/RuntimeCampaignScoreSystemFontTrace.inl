// Explicit owned CLI capture; normal production borrows remain null.
struct RuntimeCampaignScoreSystemFontTraceCall {
 std::wstring text;
 RuntimeInputPixelRect client,base,scaled;
 std::int32_t mode=0,role=0,height=0,weight=0;
 std::uint32_t color=0,format=0;
};
bool writeRuntimeCampaignScoreSystemFontTrace(const std::filesystem::path& path,
 const std::vector<RuntimeCampaignScoreSystemFontTraceCall>& calls) {
 if(calls.empty()||calls.size()>std::numeric_limits<std::uint32_t>::max())return false;
 std::ofstream out(path,std::ios::binary);if(!out)return false;out.write("W017C001",8);
 const auto u32=[&](std::uint32_t x){out.write(reinterpret_cast<const char*>(&x),sizeof(x));};
 const auto rect=[&](RuntimeInputPixelRect r){for(auto x:{r.left,r.top,r.right,r.bottom})u32(static_cast<std::uint32_t>(x));};
 u32(static_cast<std::uint32_t>(calls.size()));
 for(const auto& c:calls){
  for(auto x:{c.mode,c.role,c.height,c.weight})u32(static_cast<std::uint32_t>(x));
  u32(c.color);u32(c.format);rect(c.client);rect(c.base);rect(c.scaled);
  if(c.text.size()>std::numeric_limits<std::uint32_t>::max())return false;
  u32(static_cast<std::uint32_t>(c.text.size()));
  for(wchar_t ch:c.text){const auto unit=static_cast<std::uint16_t>(ch);out.write(reinterpret_cast<const char*>(&unit),sizeof(unit));}
 }
 return bool(out);
}
