#include "ui/results/win32/RuntimeResultBitmapTextWin32.h"
#include "OriginalResultFontReference.h"
#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
namespace {
std::uint64_t checks=0,failures=0,frames=0,comparedBytes=0,drawCases=0;
void expect(bool ok,const char* why){++checks;if(!ok){++failures;if(failures<12)std::cerr<<why<<"\n";}}
std::uint32_t u32(std::istream& in){unsigned char b[4]{};in.read(reinterpret_cast<char*>(b),4);if(!in)throw std::runtime_error("short input");return b[0]|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
RECT rect(std::istream& in){RECT r;r.left=static_cast<LONG>(u32(in));r.top=static_cast<LONG>(u32(in));r.right=static_cast<LONG>(u32(in));r.bottom=static_cast<LONG>(u32(in));return r;}
struct Call {std::wstring text;RECT client{},base{};COLORREF color=0;bool centered=false;};
struct Dib {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;unsigned char* pixels=nullptr;std::size_t bytes=0;int width=0,height=0;
 Dib(int w,int h):width(w),height(h){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;dc=CreateCompatibleDC(nullptr);void* data=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);if(!dc||!bitmap||!data)throw std::runtime_error("actual owned DIB creation");previous=SelectObject(dc,bitmap);if(!previous||previous==HGDI_ERROR)throw std::runtime_error("actual owned DIB selection");pixels=static_cast<unsigned char*>(data);bytes=std::size_t(w)*h*4;}
 ~Dib(){
  if(dc&&previous&&previous!=HGDI_ERROR)expect(SelectObject(dc,previous)==bitmap,"actual DIB previous bitmap restored before release");
  if(bitmap)expect(DeleteObject(bitmap)!=FALSE,"actual DIB bitmap release succeeded");
  if(dc)expect(DeleteDC(dc)!=FALSE,"actual compatible DC release succeeded");
 }
 Dib(const Dib&)=delete;Dib& operator=(const Dib&)=delete;
};
std::u16string portable(const std::wstring& s){std::u16string x;for(auto c:s)x.push_back(static_cast<char16_t>(c));return x;}
struct Run {RuntimeInputPixelRect rect;bool shadow;};
struct Capture final:concord::ui::ResultBitmapTextSink {
 std::vector<Run> runs;std::vector<int> events;RuntimeInputPixelRect clip{};bool shadow=false;
 Capture(){runs.reserve(65536);events.reserve(16);}
 void beginText(const RuntimeInputPixelRect& r) noexcept override{clip=r;events.push_back(1);}
 void beginPass(bool value) noexcept override{shadow=value;events.push_back(value?2:3);}
 void fillRun(const RuntimeInputPixelRect& r) noexcept override{runs.push_back({r,shadow});}
 void endPass() noexcept override{events.push_back(4);}
 void endText() noexcept override{events.push_back(5);}
};
bool same(const RuntimeInputPixelRect& a,const RuntimeInputPixelRect& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=3)throw std::runtime_error("need actual layout fixture and original font file");
 const DWORD needed=GetEnvironmentVariableW(L"CONCORD_RESULT_BITMAP_REFERENCE_OUTPUT",nullptr,0);if(!needed)throw std::runtime_error("explicit external output env required");std::wstring parent(needed,L'\0');const auto copied=GetEnvironmentVariableW(L"CONCORD_RESULT_BITMAP_REFERENCE_OUTPUT",parent.data(),needed);if(!copied||copied>=needed)throw std::runtime_error("output env changed");parent.resize(copied);const std::filesystem::path outputParent(parent);if(!outputParent.is_absolute())throw std::runtime_error("output must be absolute");
 const auto output=outputParent/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));if(std::filesystem::exists(output))throw std::runtime_error("output exists");std::filesystem::create_directories(output);
 std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary);char magic[8]{};input.read(magic,8);expect(std::string(magic,8)=="W015R001","actual result trace magic");expect(u32(input)==0,"layout fixture excludes original font bytes");const auto count=u32(input);if(count==0||count>100000)throw std::runtime_error("invalid call count");std::vector<Call> calls;std::set<std::wstring> strings;
 for(std::uint32_t k=0;k<count;++k){Call c;const auto n=u32(input);if(n>65536)throw std::runtime_error("long fixture label");c.client=rect(input);c.base=rect(input);c.color=u32(input);c.centered=u32(input)!=0;for(std::uint32_t j=0;j<n;++j){unsigned char b[2]{};input.read(reinterpret_cast<char*>(b),2);if(!input)throw std::runtime_error("short text");c.text.push_back(static_cast<wchar_t>(b[0]|(std::uint16_t(b[1])<<8)));}strings.insert(c.text);calls.push_back(std::move(c));}expect(input.peek()==std::char_traits<char>::eof(),"all actual input consumed");
 result_original_reference::RuntimeResultBitFont old;expect(old.load(std::filesystem::path(argv[2])),"actual original native font load");const auto originalBytes=old.bytes;const auto originalStride=old.stride,originalRows=old.rows,originalHeight=old.height,originalCount=old.count,originalRecord=old.recordSize;
 expect(!old.load(output/"missing.fnt")&&old.bytes==originalBytes&&old.stride==originalStride&&old.rows==originalRows&&old.height==originalHeight&&old.count==originalCount&&old.recordSize==originalRecord,"result open failure preserves original owned bytes and metrics");
 {std::ofstream bad(output/"bad-magic.fnt",std::ios::binary);bad<<"nope";}expect(!old.load(output/"bad-magic.fnt")&&old.bytes.empty()&&old.stride==originalStride&&old.recordSize==originalRecord,"bad short header clears bytes, preserves raw metrics");expect(old.load(std::filesystem::path(argv[2])),"reload real original font");
 using namespace concord::ui;const ResultBitmapFontView font{old.bytes,{old.stride,old.rows,old.height,old.count,old.recordSize}};
 for(std::uint32_t k=0;k<65536;++k){const std::wstring w(1,static_cast<wchar_t>(k));const auto u=portable(w);const auto expected=old.measure(w);expect(measureResultBitmapText(font,std::wstring_view(w))==expected,"all BMP wide width");expect(measureResultBitmapText(font,std::u16string_view(u))==expected,"all BMP16 width");}
 const auto coldHandles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 const auto warmNative=[&]{
  // Qualify the complete actual oracle sequence, including an existing clip,
  // exact selected-state/clip snapshots and real original/current drawing.
  Dib a(1600,1000),b(1600,1000);const auto& c=calls.front();
  HRGN seed=CreateRectRgn(3,5,1593,997);expect(seed!=nullptr,"actual full warm seed region");
  SelectClipRgn(a.dc,seed);SelectClipRgn(b.dc,seed);expect(DeleteObject(seed)!=FALSE,"full warm seed released");
  SetBkMode(a.dc,OPAQUE);SetBkMode(b.dc,OPAQUE);SetTextColor(a.dc,RGB(2,3,4));SetTextColor(b.dc,RGB(2,3,4));
  const auto brush=GetCurrentObject(b.dc,OBJ_BRUSH),selectedFont=GetCurrentObject(b.dc,OBJ_FONT);
  HRGN first=CreateRectRgn(0,0,0,0),last=CreateRectRgn(0,0,0,0);expect(first&&last,"actual full warm clip snapshots");GetClipRgn(b.dc,first);
  old.draw(a.dc,c.client,c.base,c.text,c.color,c.centered);
  drawResultBitmapTextWin32(font,b.dc,c.client,c.base,c.text,c.color,c.centered,&result_original_reference::runtimeMatchScoreScaledRect);GdiFlush();
  GetClipRgn(b.dc,last);expect(EqualRgn(first,last)!=FALSE,"actual full warm clip restored");
  expect(GetCurrentObject(b.dc,OBJ_BRUSH)==brush&&GetCurrentObject(b.dc,OBJ_FONT)==selectedFont&&GetBkMode(b.dc)==OPAQUE&&GetTextColor(b.dc)==RGB(2,3,4),"actual full warm selected state restored");
  expect(DeleteObject(first)!=FALSE&&DeleteObject(last)!=FALSE,"actual full warm clip snapshots released");
 };
 warmNative();const auto warmOne=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 warmNative();const auto handlesBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 expect(warmOne==handlesBefore,"actual native resource baseline stable across two complete warm cycles");
 std::cout<<"Result GDI resource qualification: cold="<<coldHandles<<" warm1="<<warmOne<<" warm2="<<handlesBefore<<"\n";
 {
  Dib original(1600,1000),current(1600,1000);std::vector<unsigned char> initial(original.bytes);for(std::size_t k=0;k<initial.size();++k)initial[k]=static_cast<unsigned char>((k*37+k/13+71)&255);
  std::cout<<"Result GDI stage: dib-created="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";
  const auto compare=[&](const Call& c){GdiFlush();std::memcpy(original.pixels,initial.data(),initial.size());std::memcpy(current.pixels,initial.data(),initial.size());
   HRGN seed=CreateRectRgn(3,5,1593,997);expect(seed!=nullptr,"actual initial clip region");SelectClipRgn(original.dc,seed);SelectClipRgn(current.dc,seed);DeleteObject(seed);SetBkMode(original.dc,OPAQUE);SetBkMode(current.dc,OPAQUE);SetTextColor(original.dc,RGB(2,3,4));SetTextColor(current.dc,RGB(2,3,4));
   const auto beforeBrush=GetCurrentObject(current.dc,OBJ_BRUSH),beforeFont=GetCurrentObject(current.dc,OBJ_FONT);HRGN beforeClip=CreateRectRgn(0,0,0,0),afterClip=CreateRectRgn(0,0,0,0);expect(beforeClip&&afterClip,"actual clip snapshot regions");GetClipRgn(current.dc,beforeClip);
   old.draw(original.dc,c.client,c.base,c.text,c.color,c.centered);
   drawResultBitmapTextWin32(font,current.dc,c.client,c.base,c.text,c.color,c.centered,&result_original_reference::runtimeMatchScoreScaledRect);GdiFlush();
   expect(std::memcmp(original.pixels,current.pixels,original.bytes)==0,"complete original/current native GDI BGRA including clipped/empty areas");++frames;comparedBytes+=original.bytes;++drawCases;
   GetClipRgn(current.dc,afterClip);expect(EqualRgn(beforeClip,afterClip)!=FALSE,"exact preexisting clip restored");expect(GetCurrentObject(current.dc,OBJ_BRUSH)==beforeBrush&&GetCurrentObject(current.dc,OBJ_FONT)==beforeFont&&GetBkMode(current.dc)==OPAQUE&&GetTextColor(current.dc)==RGB(2,3,4),"native DC selected resources and text state restored");DeleteObject(beforeClip);DeleteObject(afterClip);
   const auto units=portable(c.text);expect(old.measure(c.text)==measureResultBitmapText(font,std::wstring_view(c.text))&&old.measure(c.text)==measureResultBitmapText(font,std::u16string_view(units)),"actual text and boundary widths unchanged");
  };
  for(const auto& c:calls){compare(c);const auto u=portable(c.text);Capture wide,units;const RuntimeInputPixelRect base{c.base.left,c.base.top,c.base.right,c.base.bottom};drawResultBitmapText(font,base,c.text,c.centered,wide);drawResultBitmapText(font,base,u,c.centered,units);bool sameRuns=wide.events==units.events&&same(wide.clip,units.clip)&&wide.runs.size()==units.runs.size();for(std::size_t k=0;k<std::min(wide.runs.size(),units.runs.size());++k)sameRuns=sameRuns&&same(wide.runs[k].rect,units.runs[k].rect)&&wide.runs[k].shadow==units.runs[k].shadow;expect(sameRuns,"both text views entire ordered runs/clip/passes exact");}
  std::cout<<"Result GDI stage: real-calls-complete="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";
  const std::array<RECT,5> clients{{{0,0,1280,720},{17,23,977,663},{0,0,1440,900},{0,0,321,237},{50,40,850,640}}};
  // Use each distinct real label once for scaling/clip/color boundaries.
  std::set<std::wstring> visited;for(const auto& source:calls)if(visited.insert(source.text).second){for(const auto& client:clients){auto c=source;c.client=client;compare(c);}for(const RECT base:std::array<RECT,6>{{{-10,-8,25,24},{0,0,1,1},{99,100,100,101},{20,30,10,20},{799,599,800,600},{10,10,10,30}}}){auto c=source;c.base=base;c.centered=!c.centered;compare(c);}for(std::uint32_t flags:std::array<std::uint32_t,3>{{0x01000000U,0x02000000U,0xff000000U}}){auto c=source;c.color|=flags;const auto beforeFlag=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);compare(c);const auto afterFlag=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);if(beforeFlag!=afterFlag)std::cout<<"Result GDI flag growth: flag="<<flags<<" before="<<beforeFlag<<" after="<<afterFlag<<"\n";}}
  std::cout<<"Result GDI stage: boundary-calls-complete="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";
  for(const auto& text:std::array<std::wstring,5>{{L"",std::wstring{wchar_t(0xd83d),wchar_t(0xde42)},std::wstring{wchar_t(0xd800),L'A',wchar_t(0xdc00)},std::wstring{0,L'A',0},std::wstring{wchar_t(0xffff)}}}){Call c;c.text=text;c.client={0,0,800,600};c.base={10,10,53,43};c.color=RGB(255,216,102);compare(c);}
 }
 GdiFlush();const auto handlesAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);expect(handlesAfter==handlesBefore,"all native brush/DIB/DC/clip resources released");
 std::cout<<"Result bitmap native reference: checks="<<checks<<" failures="<<failures<<" calls="<<count<<" strings="<<strings.size()<<" fontBytes="<<old.bytes.size()<<" cases="<<drawCases<<" fullFrames="<<frames<<" comparedBytes="<<comparedBytes<<" bmpWidths=65536 gdibefore="<<handlesBefore<<" gdiafter="<<handlesAfter<<" nativeScale=original rawColorFlags=preserved dcRestore=exact noTolerance=yes\n";return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
