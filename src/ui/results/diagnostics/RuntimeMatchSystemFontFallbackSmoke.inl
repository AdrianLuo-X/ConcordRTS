// Explicit qualification calls the same production WM_PAINT/match score path.
// The local missing-font view never changes the installed static bitmap cache.
int runRuntimeMatchSystemFontFallbackSmoke(const RuntimeConfig& config,
 const std::filesystem::path& outputRoot) {
 if(!outputRoot.is_absolute()||std::filesystem::exists(outputRoot))return 1;
 std::filesystem::create_directories(outputRoot);
 RuntimeResultBitFont unavailable;
 std::vector<RuntimeMatchSystemFontTraceCall> calls;
 auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
 state.config=config;state.runtimePreferencePersistenceEnabled=false;
 state.runtimeVoiceVolume=0;state.runtimeSoundVolume=0;state.runtimeMusicEnabled=false;
 state.report=buildRuntimeStartupReport(config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
 state.theaterLoaded=state.report.theaterLoaded;state.artLoaded=state.report.artLoaded;state.rulesLoaded=state.report.rulesLoaded;
 if(!state.report.success){printRuntimeStartupReport(state.report);return 1;}
 initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
 state.economy.owner="Americans";state.config.playerCountry="YuriCountry";state.config.campaignId="gdi";
 state.tick=300;state.skirmishMatchFinished=true;
 const auto& installed=runtimeResultBitFont(state);const auto installedBytes=installed.bytes;
 if(installedBytes.empty()||!unavailable.bytes.empty())return 1;
 state.resultSystemFontQualification=&unavailable;state.resultSystemFontTrace=&calls;
 const RECT client{0,0,800,600};ensureRuntimeCameraForViewport(state,runtimeMapViewport(client));
 const HINSTANCE instance=GetModuleHandleW(nullptr);const wchar_t* name=L"ConcordMatchSystemFontQualification";
 WNDCLASSW wc{};wc.lpfnWndProc=runtimeWindowProc;wc.hInstance=instance;wc.lpszClassName=name;
 if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
 HWND window=CreateWindowExW(0,name,L"Match system font qualification",WS_POPUP,0,0,800,600,nullptr,nullptr,instance,nullptr);
 if(!window){UnregisterClassW(name,instance);return 1;}
 SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
 const auto cleanup=[&]{if(IsWindow(window))DestroyWindow(window);UnregisterClassW(name,instance);};
 const auto paint=[&]{
  InvalidateRect(window,nullptr,FALSE);SendMessageW(window,WM_PAINT,0,0);GdiFlush();
  auto* pixels=static_cast<const std::uint32_t*>(state.backBuffer.pixels);
  return pixels?std::vector<std::uint32_t>(pixels,pixels+800*600):std::vector<std::uint32_t>{};
 };
 unsigned captured=0;
 const auto save=[&](const char* file,const std::vector<std::uint32_t>& pixels){
  std::string error;const bool ok=pixels.size()==800U*600U&&writeRuntimeSmokeBmp(
   pathString(outputRoot/file),pixels.data(),800,600,error);if(ok)++captured;return ok;
 };
 std::string error;
 const bool original=runRuntimeMatchScorePageSmoke(state,window,paint,save,error);
 cleanup();if(!original){std::cerr<<"Match system qualification original page: "<<error<<"\n";return 1;}
 if(captured!=6||calls.empty())return 1;
 const int frozenTick=state.tick;
 const std::array<RuntimeInputPixelRect,6> clients{{{0,0,400,300},{0,0,800,600},{7,9,807,609},{0,0,1024,768},{0,0,1280,720},{13,17,1293,817}}};
 for(std::size_t n=0;n<clients.size();++n){
  const auto r=clients[n];const int w=std::max(1,r.right+7),h=std::max(1,r.bottom+7);
  RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);
  const bool ready=screen&&ensureRuntimeBackBuffer(buffer,screen,w,h);
  if(screen)ReleaseDC(nullptr,screen);
  if(!ready||!buffer.pixels)return 1;
  buffer.beginWrite();std::fill_n(static_cast<std::uint32_t*>(buffer.pixels),static_cast<std::size_t>(w)*h,0xff1b2c3dU);
  SetBkMode(buffer.memoryDc,OPAQUE);SetTextColor(buffer.memoryDc,RGB(2,101,73));
  const auto font=GetCurrentObject(buffer.memoryDc,OBJ_FONT);
  const RECT nativeClient{r.left,r.top,r.right,r.bottom};
  drawRuntimeMatchScorePage(buffer.memoryDc,nativeClient,state);
  GdiFlush();
  if(GetBkMode(buffer.memoryDc)!=OPAQUE||GetTextColor(buffer.memoryDc)!=RGB(2,101,73)
   ||GetCurrentObject(buffer.memoryDc,OBJ_FONT)!=font||state.tick!=frozenTick)return 1;
  auto view=buffer.completedPixels();if(!view.valid())return 1;
  std::string imageError;const auto file=outputRoot/("scaled-"+std::to_string(n)+".bmp");
  if(!writeRuntimeSmokeBmp(pathString(file),static_cast<const std::uint32_t*>(buffer.pixels),w,h,imageError))return 1;
  if(!buffer.release()||!buffer.nativeResourcesReleased())return 1;
  ++captured;
 }
 state.resultSystemFontTrace=nullptr;state.resultSystemFontQualification=nullptr;
 if(installed.bytes!=installedBytes||runtimeResultBitFont(state).bytes!=installedBytes||!unavailable.bytes.empty()
  ||!writeRuntimeMatchSystemFontTrace(outputRoot/"actual-text.bin",calls))return 1;
 std::cout<<"Match system font original fallback: originalPages=6 scaledPages=6 frames="<<captured
  <<" calls="<<calls.size()<<" installedBitmapPreserved=yes scopedMissingView=yes actualRenderer=yes originalPageAssertions=yes tickFrozen=yes skipped=0\n";
 return 0;
}
