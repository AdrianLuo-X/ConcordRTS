// Qualification renders the two original production branches, without changing assets.
int runRuntimeCampaignScoreSystemFontSmoke(const RuntimeConfig& config,
 const std::filesystem::path& outputRoot) {
 if(!outputRoot.is_absolute()||std::filesystem::exists(outputRoot))return 1;
 std::filesystem::create_directories(outputRoot);
 struct FixedPresentationClock final:RuntimeUiClock {
  RuntimeUiTimeMs value=10000;
  RuntimeUiTimeMs nowMs()const noexcept override{return value;}
 } clock;
 std::vector<RuntimeCampaignScoreSystemFontTraceCall> calls;
 auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
 state.config=config;state.runtimePreferencePersistenceEnabled=false;
 state.runtimeVoiceVolume=0;state.runtimeSoundVolume=0;state.runtimeMusicEnabled=false;
 state.uiClock=&clock;state.campaignScoreSystemFontTrace=&calls;
 state.report=buildRuntimeStartupReport(config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
 state.theaterLoaded=state.report.theaterLoaded;state.artLoaded=state.report.artLoaded;state.rulesLoaded=state.report.rulesLoaded;
 if(!state.report.success){printRuntimeStartupReport(state.report);return 1;}
 initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
 state.campaignMode=true;state.skirmishMatchFinished=true;state.tick=3600;
 state.campaignScore.missionStartTick=0;state.campaignScore.finishedTick=3600;
 state.campaignScore.parTicks=4500;state.campaignScore.captured=true;state.campaignScore.finishedUnderPar=true;
 state.campaignScore.enemyUnitsDestroyed=17;state.campaignScore.enemyStructuresDestroyed=8;
 state.campaignScore.friendlyUnitsLost=4;state.campaignScore.friendlyStructuresLost=2;state.campaignScore.finalScore=7412;
 state.campaignScore.presentationStartedAtMs=10000;
 const std::array<RuntimeInputPixelRect,6> clients{{{0,0,400,300},{0,0,800,600},{7,9,807,609},{0,0,1024,768},{0,0,1280,720},{13,17,1293,817}}};
 const std::array<RuntimeUiTimeMs,6> elapsed{{0,500,600,1500,3000,6000}};
 const std::array<const char*,3> countries{{"Americans","Russians","YuriCountry"}};
 const std::array<const char*,3> campaigns{{"gdi","nod","yuri"}};
 unsigned frames=0;std::array<unsigned,2> requests{};
 for(std::size_t theme=0;theme<3;++theme){
  state.config.playerCountry=countries[theme];state.economy.owner=countries[theme];state.config.campaignId=campaigns[theme];
  state.config.campaignPlayerHouse="";state.config.campaignMissionId="font-qualification";
  for(unsigned victory=0;victory<2;++victory){state.skirmishVictory=victory!=0;
   for(unsigned mode=0;mode<2;++mode){
    for(std::size_t n=0;n<clients.size();++n){
     const auto r=clients[n];const int w=std::max(1,r.right+7),h=std::max(1,r.bottom+7);
     RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);
     const bool ready=screen&&ensureRuntimeBackBuffer(buffer,screen,w,h);if(screen)ReleaseDC(nullptr,screen);
     if(!ready||!buffer.pixels)return 1;
     buffer.beginWrite();std::fill_n(static_cast<std::uint32_t*>(buffer.pixels),static_cast<std::size_t>(w)*h,0xff1b2c3dU);
     SetBkMode(buffer.memoryDc,OPAQUE);SetTextColor(buffer.memoryDc,RGB(2,101,73));
     const auto font=GetCurrentObject(buffer.memoryDc,OBJ_FONT);const auto before=calls.size();
     const RECT nativeClient{r.left,r.top,r.right,r.bottom};clock.value=10000+elapsed[n];
     if(mode==0)drawRuntimeCampaignScoreFallbackOverlay(buffer.memoryDc,nativeClient,state);
     else drawRuntimeCampaignScoreOverlay(buffer.memoryDc,nativeClient,state);
     GdiFlush();
     if(GetBkMode(buffer.memoryDc)!=OPAQUE||GetTextColor(buffer.memoryDc)!=RGB(2,101,73)
      ||GetCurrentObject(buffer.memoryDc,OBJ_FONT)!=font||state.tick!=3600||calls.size()==before)return 1;
     for(auto at=before;at<calls.size();++at){if(calls[at].mode!=static_cast<int>(mode))return 1;++requests[mode];}
     if(!buffer.completedPixels().valid())return 1;
     const auto file=outputRoot/("score-"+std::to_string(theme)+"-"+std::to_string(victory)+"-"+std::to_string(mode)+"-"+std::to_string(n)+".bmp");
     std::string error;if(!writeRuntimeSmokeBmp(pathString(file),static_cast<const std::uint32_t*>(buffer.pixels),w,h,error))return 1;
     if(!buffer.release()||!buffer.nativeResourcesReleased())return 1;++frames;
    }
   }
  }
 }
 state.campaignScoreSystemFontTrace=nullptr;state.uiClock=&runtimeWin32UiClock();
 if(frames!=72||!requests[0]||!requests[1]||!writeRuntimeCampaignScoreSystemFontTrace(outputRoot/"actual-text.bin",calls))return 1;
 std::cout<<"Campaign score system font qualification: frames="<<frames<<" fallbackCalls="<<requests[0]<<" originalArtCalls="<<requests[1]
  <<" actualRenderers=yes stockAssetsPreserved=yes fixedBorrowedPresentationClock=yes tickFrozen=yes outerDcRestored=yes skipped=0\n";
 return 0;
}
