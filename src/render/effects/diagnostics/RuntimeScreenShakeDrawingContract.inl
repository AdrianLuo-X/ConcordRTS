// Actual original Native entry versus the production standard port. Own only
// fixture buffers/capture scopes; reuse the sole offset/translation/EMF owners.
#include "render/core/win32/RuntimeScreenShakeDrawingWin32.h"
#include "ui/surface/win32/RuntimeMainBackBufferWin32.h"
#include <tuple>
#include <type_traits>
namespace RuntimeScreenShakeDrawingContractDetail {
using concord::render::MapDrawCommand;
using concord::render::MapDrawKind;
using concord::render::MapDrawList;
using concord::ui::win32::MapNativeOverlayWin32;
struct ReferenceDC {HDC dc=GetDC(nullptr);~ReferenceDC(){if(dc)ReleaseDC(nullptr,dc);}};
struct Result {
 bool value=false,finished=true;
 std::vector<MapDrawCommand> emitted,compiled;
 std::vector<std::uint32_t> resources;
 concord::ui::win32::MapNativeOverlayReport report;
};
auto reportValues(const concord::ui::win32::MapNativeOverlayReport& r){
 return std::tie(r.emfBytes,r.scratchBytes,r.lookupBytes,r.nativePixels,r.segments,
  r.lookupGroups,r.cacheHit,r.indexedDraws,r.visionEffects,r.shifts,r.failure);
}
bool sameCommands(const std::vector<MapDrawCommand>& a,const std::vector<MapDrawCommand>& b){
 static_assert(std::is_trivially_copyable_v<MapDrawCommand> && std::has_unique_object_representations_v<MapDrawCommand>);
 return a.size()==b.size()&&(a.empty()||std::memcmp(a.data(),b.data(),a.size()*sizeof(MapDrawCommand))==0);
}
std::uint64_t hashBytes(concord::ui::ConstMenuPixelSurfaceView v){
 std::uint64_t h=14695981039346656037ULL;for(unsigned char x:v.bytes)h=(h^x)*1099511628211ULL;return h;
}
}
bool runRuntimeScreenShakeDrawingContract(const RuntimeMapRenderContext& active,
 const RuntimeMapRenderContext& quiet,const std::string& output,std::string& error){
 using namespace RuntimeScreenShakeDrawingContractDetail;
 const auto fail=[&](const std::string& why){error="Screen shake drawing: "+why;return false;};
 if(MapNativeOverlayWin32::current())return fail("unexpected outer Native capture");
 if(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())!=DPI_AWARENESS_PER_MONITOR_AWARE)
  return fail("original Native capture DPI qualification unavailable");
 const auto offset=runtimeScreenShakeForRender(active),zero=runtimeScreenShakeForRender(quiet);
 if((!offset.x&&!offset.y)||zero.x||zero.y)return fail("actual active/quiet hit contexts not qualified");
 std::filesystem::path root;std::ofstream rows;
 if(!output.empty()){
  root=std::filesystem::path(output);if(!std::filesystem::create_directory(root))return fail("output must be new");
  rows.open(root/"pairs.csv");if(!rows)return fail("CSV open");
  rows<<"case,active,mode,viewport,result,explicitNoOp,explicitShift,commandsBefore,commandsAfter,nativeHash,standardHash\n";
 }
 constexpr int width=96,height=72;
 const std::array<RECT,4> views{{{0,0,width,height},{11,13,87,61},{22,17,22,17},{40,35,20,10}}};
 ReferenceDC reference;if(!reference.dc)return fail("actual reference DC");
 concord::ui::win32::MainBackBufferWin32 a,b;
 auto ca=std::make_unique<RuntimeMapRenderCache>(),cb=std::make_unique<RuntimeMapRenderCache>();
 const auto runOne=[&](bool standard,const RuntimeMapRenderContext& source,int mode,
  const RECT& view,concord::ui::win32::MainBackBufferWin32& buffer,
  RuntimeMapRenderCache& cache,Result& result)->bool {
  if(!GdiFlush())return fail("pre-write flush");buffer.beginWrite();
  auto* pixels=static_cast<std::uint32_t*>(buffer.pixels);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)
   pixels[y*width+x]=0xff000000U|static_cast<std::uint32_t>((y*width+x)*7919U);
  auto context=source;context.surfaceWidth=width;context.surfaceHeight=height;
  context.surfacePixels=(mode==2||mode==3)?nullptr:pixels;
  HDC dc=(mode==0||mode==3)?nullptr:buffer.memoryDc;
  MapDrawList list;MapNativeOverlayWin32 capture;
  if(mode>=4){
   const auto meta=capture.begin(buffer.memoryDc,width,height,list,pixels);
   if(!meta)return fail(std::string("real capture begin: ")+capture.report().failure.data());
   if(mode!=5)dc=meta;
   if(mode==6)capture.reject("screen-shake-contract-rejected");
  }
  if(standard){
   RuntimeScreenShakeDrawTargetWin32 target(dc);
   const RuntimeInputPixelRect r{view.left,view.top,view.right,view.bottom};
   result.value=::applyRuntimeScreenShake(target,r,context,cache);
  }else result.value=::applyRuntimeScreenShake(dc,view,context,cache);
  // The adapter's DC borrow has ended before capture finalization/read/release.
  if(mode>=4){
   result.emitted=list.commands();result.finished=capture.finish();
   result.compiled=list.commands();result.resources=list.resources();result.report=capture.report();
   if(result.finished!=(mode!=6))return fail(std::string("capture finish policy: ")+result.report.failure.data());
   if(MapNativeOverlayWin32::current()||capture.active())return fail("capture TLS/lease not closed");
  }
  if(!buffer.completedPixels().valid())return fail("original surface publication");return true;
 };
 // Warm actual original EMF/font environment before strict cold resource balance.
 if(!a.ensure(reference.dc,width,height)||!b.ensure(reference.dc,width,height))return fail("warm original owners");
 Result wa,wb;if(!runOne(false,quiet,4,views[0],a,*ca,wa)||!runOne(true,quiet,4,views[0],b,*cb,wb)
  ||wa.value||wb.value||!sameCommands(wa.emitted,wb.emitted)||!sameCommands(wa.compiled,wb.compiled)
  ||wa.resources!=wb.resources||reportValues(wa.report)!=reportValues(wb.report))return fail("warm exact NoOp route");
 if(!a.release()||!b.release()||!a.nativeResourcesReleased()||!b.nativeResourcesReleased())return fail("warm selection restore");
 const DWORD guiInitial=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesInitial=0;
 if(!GetProcessHandleCount(GetCurrentProcess(),&handlesInitial))return fail("initial handles");
 unsigned pairs=0,noOps=0,shifts=0,trueWithoutCommand=0,samples=0,borrowCycles=0;
 std::uint64_t rawPixels=0;
 if(!a.ensure(reference.dc,width,height)||!b.ensure(reference.dc,width,height))return fail("original DIB owners");
 for(bool shaking:{false,true})for(int mode=0;mode<7;++mode)for(std::size_t vi=0;vi<views.size();++vi){
  const auto& context=shaking?active:quiet;Result expected,actual;
  if(!runOne(false,context,mode,views[vi],a,*ca,expected)||!runOne(true,context,mode,views[vi],b,*cb,actual))return false;
  const auto av=a.completedPixels(),bv=b.completedPixels();
  if(expected.value!=actual.value||expected.finished!=actual.finished
   ||!sameCommands(expected.emitted,actual.emitted)||!sameCommands(expected.compiled,actual.compiled)
   ||expected.resources!=actual.resources||reportValues(expected.report)!=reportValues(actual.report)
   ||ca->screenShakePixels!=cb->screenShakePixels||!av.valid()||!bv.valid()
   ||av.bytes.size()!=bv.bytes.size()||!std::equal(av.bytes.begin(),av.bytes.end(),bv.bytes.begin()))
   return fail("original/std bool/raw pixels/entire affected cache/commands/resources mismatch case="+std::to_string(pairs));
  const auto count=[&](MapDrawKind kind){return std::count_if(actual.emitted.begin(),actual.emitted.end(),[&](const auto& c){return c.kind==kind;});};
  const auto n=count(MapDrawKind::NoOp),s=count(MapDrawKind::Shift);
  if(mode==4){
   const bool valid=views[vi].right>views[vi].left&&views[vi].bottom>views[vi].top;
   if(actual.value!=shaking||n!=(shaking?0:1)||s!=(shaking&&valid?1:0))return fail("NoOp/Shift/empty viewport original return policy");
   if(!shaking){
    const auto it=std::find_if(actual.emitted.begin(),actual.emitted.end(),[](const auto& c){return c.kind==MapDrawKind::NoOp;});
    if(it==actual.emitted.end()||it->left||it->top||it->right!=width||it->bottom!=height)return fail("zero offset original full segment dimensions");
   }else if(valid){
    const auto it=std::find_if(actual.emitted.begin(),actual.emitted.end(),[](const auto& c){return c.kind==MapDrawKind::Shift;});
    if(it==actual.emitted.end()||it->left!=views[vi].left||it->top!=views[vi].top
     ||it->right!=views[vi].right||it->bottom!=views[vi].bottom||it->originX!=offset.x||it->originY!=offset.y)
     return fail("original Shift payload");
   }else ++trueWithoutCommand;
  }else if(n||s)return fail("mismatched/rejected/no capture emitted a boundary command");
  if(mode==6&&actual.value)return fail("rejected capture returned true");
  noOps+=static_cast<unsigned>(n);shifts+=static_cast<unsigned>(s);rawPixels+=static_cast<std::uint64_t>(width)*height;
  if(rows.is_open())rows<<pairs<<','<<shaking<<','<<mode<<','<<vi<<','<<actual.value<<','<<n<<','<<s<<','
   <<actual.emitted.size()<<','<<actual.compiled.size()<<','<<hashBytes(av)<<','<<hashBytes(bv)<<'\n';
  if(!output.empty()&&vi==0){
   std::vector<std::uint32_t> aa(width*height),bb(width*height);std::memcpy(aa.data(),av.bytes.data(),av.bytes.size());std::memcpy(bb.data(),bv.bytes.data(),bv.bytes.size());
   const auto label="active"+std::to_string(shaking)+"-mode"+std::to_string(mode);
   if(!writeRuntimeBgraBmp(root/(label+"-native.bmp"),width,height,aa)||!writeRuntimeBgraBmp(root/(label+"-standard.bmp"),width,height,bb))return fail("BMP write");++samples;
  }++pairs;
 }
 const DWORD guiWarm=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesWarm=0;
 if(!GetProcessHandleCount(GetCurrentProcess(),&handlesWarm))return fail("warm handles");
 for(unsigned repeat=0;repeat<64;++repeat){Result na,nb;
  if(!runOne(false,active,1,views[0],a,*ca,na)||!runOne(true,active,1,views[0],b,*cb,nb)
   ||!na.value||na.value!=nb.value||ca->screenShakePixels!=cb->screenShakePixels)return fail("repeated borrow policy");
  const auto av=a.completedPixels(),bv=b.completedPixels();if(!av.valid()||!bv.valid()||!std::equal(av.bytes.begin(),av.bytes.end(),bv.bytes.begin()))return fail("repeated borrow raw pixels");++borrowCycles;
 }
 DWORD handlesAfter=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)||handlesAfter!=handlesWarm
  ||GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)!=guiWarm)return fail("borrowed target resource balance");
 if(!a.release()||!b.release()||!a.nativeResourcesReleased()||!b.nativeResourcesReleased())return fail("original owners selection restore/release");
 DWORD handlesFinal=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesFinal)||handlesFinal!=handlesInitial
  ||GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)!=guiInitial||MapNativeOverlayWin32::current())return fail("complete native resource balance");
 if(rows.is_open()){rows.flush();if(!rows)return fail("CSV final write");}
 if(pairs!=56||rawPixels!=387072||noOps!=4||shifts!=2||trueWithoutCommand!=2||borrowCycles!=64)return fail("complete case coverage");
 std::cout<<"Screen shake drawing native contract: pairs="<<pairs<<" rawPixels="<<rawPixels<<" modes=7 viewports=4"
  <<" noOp="<<noOps<<" shift="<<shifts<<" trueWithoutCommand="<<trueWithoutCommand<<" rejectedPairs=8"
  <<" borrowCycles="<<borrowCycles<<" warmupPairs=1 samples="<<samples<<" rawAlpha=exact affectedCache=entire"
  <<" gdi="<<guiInitial<<"="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" handles="<<handlesInitial<<"="<<handlesFinal
  <<" dpi=original-per-monitor noDpiMutation=yes gpuExecutionClaim=no failures=0 skipped=0\n";
 error.clear();return true;
}
