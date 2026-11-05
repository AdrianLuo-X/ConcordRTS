#include "OriginalCampaignScoreSystemFontReference.h"
#include "ui/text/win32/RuntimeCampaignScoreSystemFontWin32.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>
using namespace concord::ui;namespace fs=std::filesystem;
constexpr std::uint32_t ACTUAL_SCORE_CALLS=720;
unsigned checks=0,failures=0;
void require(bool v,const char* what){++checks;if(!v){++failures;std::cerr<<what<<"\n";}}
struct Dib{
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;unsigned char* pixels=nullptr;
 Dib(){dc=CreateCompatibleDC(nullptr);BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=800;i.bmiHeader.biHeight=-600;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;i.bmiHeader.biCompression=BI_RGB;void* p=nullptr;bitmap=CreateDIBSection(dc,&i,DIB_RGB_COLORS,&p,nullptr,0);pixels=static_cast<unsigned char*>(p);if(bitmap)previous=SelectObject(dc,bitmap);require(dc&&bitmap&&pixels&&previous&&previous!=HGDI_ERROR,"actual DIB");}
 ~Dib(){if(dc&&previous)require(SelectObject(dc,previous)==bitmap,"restore bitmap");if(bitmap)require(DeleteObject(bitmap)!=FALSE,"delete bitmap");if(dc)require(DeleteDC(dc)!=FALSE,"delete DC");}
};
CampaignScoreFontRole role(int mode,int slot){
 constexpr std::array<CampaignScoreFontRole,4> fallback{CampaignScoreFontRole::Title,CampaignScoreFontRole::Heading,CampaignScoreFontRole::Body,CampaignScoreFontRole::Number};
 constexpr std::array<CampaignScoreFontRole,4> art{CampaignScoreFontRole::Title,CampaignScoreFontRole::Message,CampaignScoreFontRole::Label,CampaignScoreFontRole::Value};return mode?art.at(slot):fallback.at(slot);
}
struct Call{std::wstring text;RuntimeInputPixelRect rect;int mode=0,slot=0,clientHeight=600,height=0,weight=0;std::uint32_t color=0,flags=0;};
std::vector<Call> inputs(const fs::path&path){
 std::ifstream f(path,std::ios::binary);require(bool(f),"actual trace");std::vector<unsigned char>d((std::istreambuf_iterator<char>(f)),{});if(d.size()<12)return{};
 require(std::memcmp(d.data(),"W017C001",8)==0,"actual trace identity");std::size_t at=8;
 auto u32=[&](){require(at+4<=d.size(),"bounded scalar");std::uint32_t v=0;if(at+4<=d.size())std::memcpy(&v,d.data()+at,4);at+=4;return v;};
 const auto count=u32();require(count==ACTUAL_SCORE_CALLS,"all actual calls");std::vector<Call>out;
 for(unsigned n=0;n<count;++n){Call c;c.mode=static_cast<int>(u32());c.slot=static_cast<int>(u32());c.height=static_cast<int>(u32());c.weight=static_cast<int>(u32());c.color=u32();c.flags=u32();
  std::array<std::int32_t,4> client{};for(auto&v:client)v=static_cast<std::int32_t>(u32());c.clientHeight=client[3]-client[1];for(unsigned j=0;j<4;++j)(void)u32();
  c.rect={static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32())};const auto units=u32();require(at+units*2<=d.size(),"bounded text");
  for(unsigned j=0;j<units&&at+2<=d.size();++j){std::uint16_t ch=0;std::memcpy(&ch,d.data()+at,2);at+=2;c.text.push_back(static_cast<wchar_t>(ch));}require((c.mode==0||c.mode==1)&&c.slot>=0&&c.slot<4,"bounded actual mode/role");out.push_back(std::move(c));
 }
 require(at==d.size(),"exact trace EOF");return out;
}
struct Pair{
 std::array<HFONT,4> original{};CampaignScoreSystemFontsWin32 current;int mode=0;
 Pair(int m,int clientHeight):mode(m){const double scale=std::max(0.65,static_cast<double>(clientHeight)/600.0);original=original_campaign_score_font::create(m!=0,scale);current.initialize(m?CampaignScoreFontPage::OriginalArt:CampaignScoreFontPage::Fallback,scale);for(unsigned n=0;n<4;++n)require(original[n]&&current.nativeFont(role(mode,n)),"all four original/current fonts");}
 ~Pair(){for(auto font:original)require(DeleteObject(font)!=FALSE,"original four-font deletion order");current.reset();}
};
void state(Dib&d,std::size_t n){
 GdiFlush();std::fill_n(d.pixels,800*600*4,static_cast<unsigned char>(0x5b));SetBkMode(d.dc,n%3?TRANSPARENT:OPAQUE);SetBkColor(d.dc,RGB(71,37,19));SetTextColor(d.dc,RGB(2,101,73));SetTextAlign(d.dc,TA_LEFT|TA_TOP);SetViewportOrgEx(d.dc,7,-3,nullptr);SelectClipRgn(d.dc,nullptr);
 if(n%2){auto r=CreateRectRgn(9,11,751,581);require(r!=nullptr,"clip allocation");SelectClipRgn(d.dc,r);require(DeleteObject(r)!=FALSE,"clip release");}
}
int main(int argc,char**argv){
 require(argc==2,"explicit actual fixture");if(argc!=2)return 1;auto calls=inputs(fs::path(argv[1]));if(failures)return 1;const auto actual=calls.size();
 const std::array<std::wstring,8>texts{L"",L"A&B",L"中文 与 Mixed",L"line1\nline2",std::wstring(L"null\0tail",9),std::wstring({wchar_t(0xd83d),wchar_t(0xde00),wchar_t(0xd800),wchar_t(0xdc00)}),L"trailing   ",std::wstring(257,L'W')};
 const std::array<RuntimeInputPixelRect,5>rects{{{10,10,320,78},{-7,-9,80,25},{20,20,21,21},{90,90,40,40},{798,598,830,625}}};
 const std::array<UINT,5>flags{DT_LEFT|DT_TOP|DT_NOPREFIX,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_WORDBREAK|DT_END_ELLIPSIS,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX};
 for(int mode:{0,1})for(int slot:{0,1,2,3})for(int height:{0,600,800})for(const auto&t:texts)for(auto rect:rects)for(auto flag:flags)calls.push_back({t,rect,mode,slot,height,0,0,0x0312ab77U,flag});
 const auto boundary=calls.size()-actual;const auto total=calls.size();std::uint64_t compared=0;
 {
 std::map<std::pair<int,int>,std::unique_ptr<Pair>> fonts;
 for(const auto& c:calls){const auto key=std::make_pair(c.mode,c.clientHeight);if(!fonts.contains(key))fonts.emplace(key,std::make_unique<Pair>(c.mode,c.clientHeight));}
 Dib a,b;require(!failures,"all DIB/font creation");if(failures)return 1;
 for(const auto& [key,pair]:fonts){(void)key;require(pair->current.capability()==CampaignScoreSystemFontCapability::NativeText,"native capability");
  for(unsigned slot=0;slot<4;++slot){LOGFONTW x{},y{};const auto current=pair->current.nativeFont(role(pair->mode,slot));require(GetObjectW(pair->original[slot],sizeof(x),&x)==sizeof(x)&&GetObjectW(current,sizeof(y),&y)==sizeof(y),"actual LOGFONT");require(std::memcmp(&x,&y,sizeof(x))==0,"all original font params exact");auto ax=SelectObject(a.dc,pair->original[slot]),by=SelectObject(b.dc,current);TEXTMETRICW mx{},my{};require(GetTextMetricsW(a.dc,&mx)&&GetTextMetricsW(b.dc,&my),"actual metrics");require(std::memcmp(&mx,&my,sizeof(mx))==0,"metrics exact");SelectObject(a.dc,ax);SelectObject(b.dc,by);}
 }
 for(std::size_t n=0;n<actual;++n){const auto&c=calls[n];auto& pair=*fonts.at({c.mode,c.clientHeight});LOGFONTW f{};require(GetObjectW(pair.current.nativeFont(role(c.mode,c.slot)),sizeof(f),&f)==sizeof(f)&&f.lfHeight==c.height&&f.lfWeight==c.weight,"actual producer height/weight preserved");}
 auto run=[&](unsigned pass){
  for(std::size_t n=0;n<total;++n){const auto&c=calls[n];auto&pair=*fonts.at({c.mode,c.clientHeight});state(a,n);state(b,n);const auto fontA=GetCurrentObject(a.dc,OBJ_FONT),fontB=GetCurrentObject(b.dc,OBJ_FONT);const int modeA=GetBkMode(a.dc),modeB=GetBkMode(b.dc);
   HRGN ca=CreateRectRgn(0,0,0,0),cb=CreateRectRgn(0,0,0,0);const int ra=GetClipRgn(a.dc,ca),rb=GetClipRgn(b.dc,cb);RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};
   original_campaign_score_font::draw(a.dc,pair.original[c.slot],c.mode!=0,c.text,rect,c.color,c.flags);
   if(pass%2==0)pair.current.draw(b.dc,CampaignScoreSystemWideTextRequest::fromPacked(role(c.mode,c.slot),c.text,c.rect,c.flags,c.color));
   else{const std::u16string text(c.text.begin(),c.text.end());pair.current.draw(b.dc,CampaignScoreSystemTextRequest::fromPacked(role(c.mode,c.slot),text,c.rect,c.flags,c.color));}
   GdiFlush();require(std::equal(a.pixels,a.pixels+800*600*4,b.pixels),"whole original/current DIB exact");require(GetCurrentObject(a.dc,OBJ_FONT)==fontA&&GetCurrentObject(b.dc,OBJ_FONT)==fontB,"font selection restored");
   require(GetBkMode(a.dc)==modeA&&GetBkMode(b.dc)==modeB,"draw preserves original background mode including empty");require(GetTextColor(a.dc)==c.color&&GetTextColor(b.dc)==c.color,"retained text color including empty");require(GetBkColor(a.dc)==GetBkColor(b.dc)&&GetTextAlign(a.dc)==GetTextAlign(b.dc),"native DC state exact");
   HRGN aa=CreateRectRgn(0,0,0,0),bb=CreateRectRgn(0,0,0,0);require(GetClipRgn(a.dc,aa)==ra&&GetClipRgn(b.dc,bb)==rb&&EqualRgn(ca,aa)&&EqualRgn(cb,bb),"clip unchanged");for(auto r:{ca,cb,aa,bb})require(DeleteObject(r)!=FALSE,"release region");if(pass>=2)compared+=800ULL*600*4;
  }
 };
 run(0);auto warm1=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);run(1);auto warm2=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(warm1==warm2,"full sequence warm stable");
 auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"handles before");run(2);run(3);
 for(unsigned n=0;n<128;++n){CampaignScoreSystemFontsWin32 cycle;const auto page=n%2?CampaignScoreFontPage::OriginalArt:CampaignScoreFontPage::Fallback;cycle.initialize(page,1.0);std::array<HFONT,4> owned{};for(unsigned slot=0;slot<4;++slot){owned[slot]=cycle.nativeFont(role(n%2,slot));require(owned[slot]!=nullptr,"lifetime font created");}cycle.reset();for(unsigned slot=0;slot<4;++slot){LOGFONTW deleted{};require(cycle.nativeFont(role(n%2,slot))==nullptr&&GetObjectW(owned[slot],sizeof(deleted),&deleted)==0,"four font identities released");}cycle.reset();}
 DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"handles after");const auto gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(gdiBefore==gdiAfter&&handlesBefore==handlesAfter,"strict GDI/handle equality");
 std::cout<<"Campaign score system font native: checks="<<checks<<" actualCalls="<<actual<<" boundaryCases="<<boundary<<" fullFramePairs="<<total*2<<" comparedBytes="<<compared<<" fontSets="<<fonts.size()<<" gdiBefore="<<gdiBefore<<" gdiAfter="<<gdiAfter<<" handlesBefore="<<handlesBefore<<" handlesAfter="<<handlesAfter<<" twoOriginalDrawPolicies=yes utf16AndWide=yes exactNoTolerance=yes failures="<<failures<<" skipped=0\n";
 }
 require(failures==0,"all DIB/font destructors asserted");return failures?1:0;
}
