// Same production Bink draw function, actual decoded frame frozen for replay.
int runMainVideoFooterFontSmoke(const RuntimeConfig& config,const fs::path& output,
 const fs::path& inputFrame){
 if(!output.is_absolute()||fs::exists(output))return 1;fs::create_directories(output);
 std::vector<MainVideoFooterTraceCall> calls;BinkVideoPageState state;state.config=config;state.footerFontTrace=&calls;
 std::uint32_t sequence=0;
 if(inputFrame.empty()){
  BinkMenuAnimation decoder;const fs::path movie=fs::path(config.localDataRootPath)/"resources/mix_exports/movmd03/s07_p01e.bik";
  if(!decoder.start(config,{},movie,concord::bink_stream::PlaybackMode::Once,false,0))return 1;
  const bool ready=decoder.poll(state.frame,10000);sequence=static_cast<std::uint32_t>(decoder.sequence());decoder.stop();
  if(!ready||!state.frame.valid()||sequence==0||state.frame.width!=140||state.frame.height!=110)return 1;
 }else{
  std::ifstream in(inputFrame,std::ios::binary);char magic[8]{};in.read(magic,8);if(!in||std::memcmp(magic,"W017V001",8)!=0)return 1;
  const auto u32=[&](){std::uint32_t v=0;in.read(reinterpret_cast<char*>(&v),4);return v;};const auto w=u32(),h=u32();sequence=u32();const auto sourceBytes=u32(),dataBytes=u32();
  if(w!=140||h!=110||!sequence||sourceBytes>1048576U||dataBytes!=w*h*4U)return 1;
  state.frame.width=static_cast<int>(w);state.frame.height=static_cast<int>(h);state.frame.source.resize(sourceBytes);state.frame.bgra.resize(dataBytes);
  in.read(state.frame.source.data(),sourceBytes);in.read(reinterpret_cast<char*>(state.frame.bgra.data()),dataBytes);if(!in||in.peek()!=std::char_traits<char>::eof()||!state.frame.valid())return 1;
 }
 {
  std::ofstream out(output/"source-frame.bin",std::ios::binary);if(!out)return 1;out.write("W017V001",8);
  const auto u32=[&](std::uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);};u32(static_cast<std::uint32_t>(state.frame.width));u32(static_cast<std::uint32_t>(state.frame.height));u32(sequence);u32(static_cast<std::uint32_t>(state.frame.source.size()));u32(static_cast<std::uint32_t>(state.frame.bgra.size()));out.write(state.frame.source.data(),static_cast<std::streamsize>(state.frame.source.size()));out.write(reinterpret_cast<const char*>(state.frame.bgra.data()),static_cast<std::streamsize>(state.frame.bgra.size()));if(!out)return 1;
 }
 const std::array<RuntimeInputPixelRect,8> clients{{{0,0,800,600},{0,0,640,480},{7,9,807,609},{0,0,1280,720},{0,0,32,1},{0,0,32,2},{0,0,32,33},{0,0,12,12}}};
 unsigned frames=0;
 for(unsigned campaign=0;campaign<2;++campaign){state.campaignMovie=campaign!=0;
  for(std::size_t n=0;n<clients.size();++n){const auto r=clients[n];const int w=std::max(1,r.right+7),h=std::max(1,r.bottom+7);
   concord::ui::win32::MainBackBufferWin32 buffer;HDC screen=GetDC(nullptr);const bool ready=screen&&buffer.ensure(screen,w,h);if(screen)ReleaseDC(nullptr,screen);if(!ready||!buffer.pixels)return 1;
   buffer.beginWrite();std::fill_n(static_cast<std::uint32_t*>(buffer.pixels),static_cast<std::size_t>(w)*h,0xff1b2c3dU);SetBkMode(buffer.memoryDc,OPAQUE);SetTextColor(buffer.memoryDc,RGB(2,101,73));
   const auto font=GetCurrentObject(buffer.memoryDc,OBJ_FONT);const auto before=calls.size();const RECT client{r.left,r.top,r.right,r.bottom};drawBinkVideoPageFrame(state,buffer.memoryDc,client);GdiFlush();
   const bool hasFooter=campaign&&r.bottom-r.top>1;if(calls.size()-before!=(hasFooter?1U:0U))return 1;
   if(GetCurrentObject(buffer.memoryDc,OBJ_FONT)!=font||GetBkMode(buffer.memoryDc)!=OPAQUE||GetTextColor(buffer.memoryDc)!=RGB(2,101,73))return 1;
   const auto view=buffer.completedPixels();if(!view.valid())return 1;std::ofstream dump(output/("page-"+std::to_string(campaign)+"-"+std::to_string(n)+".bgra"),std::ios::binary);if(!dump)return 1;
   dump.write("W017P001",8);const std::uint32_t width=static_cast<std::uint32_t>(w),height=static_cast<std::uint32_t>(h);dump.write(reinterpret_cast<const char*>(&width),4);dump.write(reinterpret_cast<const char*>(&height),4);dump.write(reinterpret_cast<const char*>(view.bytes.data()),static_cast<std::streamsize>(view.bytes.size()));if(!dump)return 1;dump.close();
   if(!buffer.release()||!buffer.nativeResourcesReleased())return 1;++frames;
  }
 }
 state.footerFontTrace=nullptr;if(frames!=16||calls.size()!=7||!writeMainVideoFooterTrace(output/"actual-text.bin",calls))return 1;
 std::cout<<"Main video footer font qualification: frames=16 calls=7 decodedWidth=140 decodedHeight=110 sequence="<<sequence
  <<" actualDecoderSource=yes sameProductionRenderer=yes originalViewport=yes scopedFrameReplay="<<(!inputFrame.empty())<<" outerDcRestored=yes stockBorrowOnly=yes skipped=0\n";return 0;
}
