// Explicit private CLI capture of the original production Campaign frame text.
struct CampaignSystemFontTraceCall {
 std::wstring text; RuntimeInputPixelRect rect,result;
 std::uint32_t role=0,format=0,color=0,measurement=0;
};
std::vector<CampaignSystemFontTraceCall>* campaignSystemFontTrace=nullptr;
void traceCampaignSystemFont(std::uint32_t role,const std::wstring& text,RuntimeInputPixelRect rect,
 std::uint32_t color,std::uint32_t format,bool measure=false,RuntimeInputPixelRect result={}) {
 if(campaignSystemFontTrace)campaignSystemFontTrace->push_back({text,
  {rect.left,rect.top,rect.right,rect.bottom},{result.left,result.top,result.right,result.bottom},
  role,format,color,measure?1U:0U});
}
struct CampaignSystemFontTraceScope {
 std::vector<CampaignSystemFontTraceCall>* previous=campaignSystemFontTrace;
 explicit CampaignSystemFontTraceScope(std::vector<CampaignSystemFontTraceCall>* calls){campaignSystemFontTrace=calls;}
 ~CampaignSystemFontTraceScope(){campaignSystemFontTrace=previous;}
};
bool writeCampaignSystemFontTrace(const fs::path& path,const std::vector<CampaignSystemFontTraceCall>& calls) {
 if(calls.empty()||calls.size()>std::numeric_limits<std::uint32_t>::max())return false;
 std::ofstream out(path,std::ios::binary);if(!out)return false;
 out.write("W017C001",8);
 const auto u32=[&](std::uint32_t v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));};
 const auto i32=[&](std::int32_t v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));};
 u32(static_cast<std::uint32_t>(calls.size()));
 for(const auto& call:calls){
  u32(call.role);u32(call.format);u32(call.color);u32(call.measurement);
  for(const auto r:{call.rect,call.result}){i32(r.left);i32(r.top);i32(r.right);i32(r.bottom);}
  u32(static_cast<std::uint32_t>(call.text.size()));
  for(wchar_t c:call.text){const auto u=static_cast<std::uint16_t>(c);out.write(reinterpret_cast<const char*>(&u),sizeof(u));}
 }
 return bool(out);
}
