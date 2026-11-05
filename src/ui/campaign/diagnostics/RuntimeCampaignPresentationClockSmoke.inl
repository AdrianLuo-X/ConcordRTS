class CampaignFixtureClock final : public RuntimeUiClock {
public:
    std::uint64_t value=0;
    std::vector<std::uint64_t> values;
    mutable std::size_t cursor=0;
    mutable std::vector<std::uint64_t> reads;
    std::uint64_t nowMs() const noexcept override {
        const auto v=cursor<values.size()?values[cursor++]:value;reads.push_back(v);return v;
    }
};

// Reuse the existing valid single-frame input and its production byte helpers.
// Only the independent new fixture's header/FINF and number of complete frames change.
CampaignFixtureBytes makeCampaignClockEightFrames() {
    const auto one=makeCampaignFixtureVqa();
    const std::array<std::uint8_t,4> tag{{'F','I','N','F'}};
    const auto found=std::search(one.begin(),one.end(),tag.begin(),tag.end());
    if(found==one.end())throw std::runtime_error("clock fixture has no FINF");
    const auto offset=static_cast<std::size_t>(found-one.begin());
    const auto frameAt=offset+12U;
    CampaignFixtureBytes result(one.begin(),found);
    writeCampaignFixtureLittle16(result,24U,8U);
    appendCampaignFixtureChunk(result,"FINF",CampaignFixtureBytes(8U*4U,0U));
    for(std::size_t frame=0;frame<8;++frame) {
        writeCampaignFixtureLittle32(result,offset+8U+frame*4U,static_cast<std::uint32_t>(result.size()/2U));
        result.insert(result.end(),one.begin()+static_cast<std::ptrdiff_t>(frameAt),one.end());
    }
    writeCampaignFixtureBig32(result,4U,static_cast<std::uint32_t>(result.size()-8U));
    return result;
}

void configureCampaignClockState(CampaignMenuState& s,const RuntimeConfig& config) {
    s.config=config;s.suppressPresentationAudio=true;s.page=CampaignPage::Missions;
    s.database.campaigns.clear();s.database.loaded=true;
    CampaignDefinition campaign;campaign.id="clock-campaign";campaign.sourceId="clock";
    CampaignMissionDefinition mission;mission.id="clock-mission";mission.ordinal=1;mission.available=true;
    mission.incomingMapSelectionStages={"clock-stage"};campaign.missions.push_back(mission);
    CampaignMapSelectionStage stage;stage.id="clock-stage";stage.sourceSection="clock-stage";
    stage.declaredTargetCount=1;stage.targets={{4,1}};
    CampaignMapSelectionChoice choice;choice.colorIndex=1;choice.targetMissionId=mission.id;stage.choices.push_back(choice);
    campaign.mapSelectionStages.push_back(stage);s.database.campaigns.push_back(campaign);
    s.mapPresentation=CampaignMapPresentationState{};auto& v=s.mapPresentation;
    v.key="clock-campaign|clock-mission|clock-stage";v.stageSection="clock-stage";
    v.phase=CampaignMapPresentationPhase::OverlayFade;
    v.clickMap.width=8;v.clickMap.height=2;v.clickMap.pixels.assign(16U,1U);v.clickMapSource="owned clock click";
    v.vqaFrame.width=8;v.vqaFrame.height=2;v.vqaFrame.bgra.resize(64U,0U);v.vqaFrame.source="owned clock frame";
    for(std::size_t i=0;i<v.vqaFrame.bgra.size();i+=4U){v.vqaFrame.bgra[i]=31U;v.vqaFrame.bgra[i+1U]=53U;v.vqaFrame.bgra[i+2U]=79U;}
    CampaignImage overlay;overlay.width=1;overlay.height=1;overlay.bgra={5,101,211,255};overlay.source="owned clock overlay";v.overlays.push_back(overlay);
    for(int index=0;index<3;++index) {
        CampaignMapAnimationVisual a;a.assetId="owned-clock-animation-"+std::to_string(index);
        a.x=index-1;a.y=1;a.frameRate=index+1;
        if(index!=1){a.frames.push_back(overlay);overlay.bgra[0]=197U;a.frames.push_back(overlay);}
        v.animations.push_back(std::move(a));
    }
}

std::string campaignClockState(const CampaignMenuState& s) {
    const auto& v=s.mapPresentation;std::ostringstream o;
    o<<static_cast<int>(s.page)<<' '<<s.campaignIndex<<' '<<s.missionIndex<<' '<<s.closing<<' '<<s.accepted<<' '
     <<std::quoted(v.key)<<' '<<std::quoted(v.stageSection)<<' '<<static_cast<int>(v.phase)<<' '
     <<v.phaseStartedAt<<' '<<v.lastFrameAt<<' '<<v.paletteFallback<<' '<<v.authoredReconstruction<<' '
     <<v.voiceStarted<<' '<<v.overlaySoundPlayed<<' '<<v.targetSoundPlayed<<' '<<v.hoverColor<<' '
     <<v.pressedColor<<' '<<v.keyboardColor<<' '<<std::quoted(v.diagnostic)<<' '
     <<std::quoted(v.clickMapSource)<<' '<<std::quoted(v.paletteSource)<<' '<<std::quoted(v.voiceSource)<<' '
     <<v.vqa.isLoaded()<<' '<<v.vqa.frameCount()<<' '<<v.vqa.currentFrameIndex()<<' '<<v.vqa.frameRate()<<' '
     <<v.vqa.hasNextFrame()<<' '<<std::quoted(v.vqa.errorMessage());
    for(auto c:v.vqa.currentFrameBgra())o<<' '<<static_cast<unsigned>(c);
    o<<" click "<<v.clickMap.width<<' '<<v.clickMap.height;
    for(auto c:v.clickMap.pixels)o<<' '<<static_cast<unsigned>(c);
    auto image=[&](const CampaignImage& a){o<<" image "<<a.width<<' '<<a.height<<' '<<std::quoted(a.source);for(auto c:a.bgra)o<<' '<<static_cast<unsigned>(c);};
    image(v.vqaFrame);for(const auto& a:v.overlays)image(a);
    for(const auto& a:v.animations){o<<' '<<std::quoted(a.assetId)<<' '<<a.x<<' '<<a.y<<' '<<a.frameRate;for(const auto& f:a.frames)image(f);}
    return o.str();
}

struct CampaignClockGdiSurface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void* pixels=nullptr;
    static constexpr int width=128,height=96;
    CampaignClockGdiSurface() {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        HDC screen=GetDC(nullptr);dc=CreateCompatibleDC(screen);
        bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0U);
        if(screen)ReleaseDC(nullptr,screen);
        if(dc && bitmap)old=SelectObject(dc,bitmap);
    }
    ~CampaignClockGdiSurface(){if(dc && old && old!=HGDI_ERROR)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    bool ready()const{return dc && bitmap && pixels && old && old!=HGDI_ERROR;}
    void clear(){GdiFlush();std::fill_n(static_cast<unsigned char*>(pixels),width*height*4,0U);}
    std::vector<unsigned char> bytes(){if(!GdiFlush())throw std::runtime_error("clock GDI flush failed");const auto* p=static_cast<const unsigned char*>(pixels);return std::vector<unsigned char>(p,p+width*height*4);}
};

int runCampaignPresentationClockSmoke(const RuntimeConfig& config,const fs::path& output) {
    if(!output.is_absolute() || fs::exists(output)){std::cerr<<"Campaign clock requires a new absolute output directory\n";return 1;}
    fs::create_directories(output);std::ofstream currentTrace(output/"current.txt",std::ios::binary),originalTrace(output/"original.txt",std::ios::binary);
    int checks=0,failures=0;std::size_t states=0,cpuPairs=0,gdiPairs=0,nonemptyGdi=0,reads=0;
    auto expect=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"Campaign clock FAILED: "<<why<<'\n';}};
    const auto vqaBytes=makeCampaignClockEightFrames();expect(writeCampaignFixtureBytes(output/"eight-frame.vqa",vqaBytes),"new independent8-frame input saved");
    CampaignClockGdiSurface currentGdi,originalGdi;expect(currentGdi.ready() && originalGdi.ready(),"two owned real native GDI surfaces ready");
    if(!currentGdi.ready() || !originalGdi.ready())return 1;
    const std::array<std::uint64_t,5> bases{{0U,1U,0xffffffffULL-50U,0x100000001ULL,std::numeric_limits<std::uint64_t>::max()-100U}};
    for(std::size_t scene=0;scene<bases.size();++scene) {
        CampaignFixtureClock ca,cb;CampaignMenuState a,b;CampaignPresentationOriginal original{&cb};
        expect(a.presentationClock==&runtimeWin32UiClock(),"default borrows existing native clock");
        a.presentationClock=&ca;b.presentationClock=&cb;configureCampaignClockState(a,config);configureCampaignClockState(b,config);
        expect(selectedMapSelectionStage(a)!=nullptr && currentMapSelectionStage(a)!=nullptr,"actual authored stage lookup valid");
        auto compare=[&](const char* label) {
            ++states;const auto sa=campaignClockState(a),sb=campaignClockState(b);
            expect(sa==sb,"all mutable presentation state,decoder and full frame inputs equal");
            expect(ca.reads==cb.reads,"complete source values/read order equal");
            currentTrace<<scene<<' '<<label<<' '<<sa<<" reads";originalTrace<<scene<<' '<<label<<' '<<sb<<" reads";
            for(auto v:ca.reads)currentTrace<<' '<<v;for(auto v:cb.reads)originalTrace<<' '<<v;
            currentTrace<<'\n';originalTrace<<'\n';
        };
        auto surfaces=[&](bool retain) {
            const auto x=composeCampaignSurface(a),y=original.composeCampaignSurface(b);++cpuPairs;
            expect(x.size()==static_cast<std::size_t>(kBaseWidth*kBaseHeight*4) && x==y,"entire CPU BGRA surface exactly equal");
            currentGdi.clear();originalGdi.clear();const RECT client{0,0,CampaignClockGdiSurface::width,CampaignClockGdiSurface::height};
            drawCampaignMapTargets(a,currentGdi.dc,client);original.drawCampaignMapTargets(b,originalGdi.dc,client);
            const auto gx=currentGdi.bytes(),gy=originalGdi.bytes();++gdiPairs;
            expect(gx==gy,"entire real GDI target surface exactly equal");
            if(std::any_of(gx.begin(),gx.end(),[](unsigned char v){return v!=0;}))++nonemptyGdi;
            if(retain) {
                const auto n=std::to_string(cpuPairs);
                expect(writeCampaignFixtureBytes(output/("cpu-current-"+n+".bgra"),x),"current full CPU sample saved");
                expect(writeCampaignFixtureBytes(output/("cpu-original-"+n+".bgra"),y),"original full CPU sample saved");
                expect(writeCampaignFixtureBytes(output/("gdi-current-"+n+".bgra"),gx),"current full GDI sample saved");
                expect(writeCampaignFixtureBytes(output/("gdi-original-"+n+".bgra"),gy),"original full GDI sample saved");
            }
        };
        ca.value=cb.value=bases[scene];refreshCampaignMapPresentation(a);original.refreshCampaignMapPresentation(b);
        expect(ca.reads.empty(),"unchanged presentation key returns before clock");compare("same-key");
        a.mapPresentation.phaseStartedAt=b.mapPresentation.phaseStartedAt=bases[scene];
        for(const auto delta:std::array<std::uint64_t,6>{{0U,1U,749U,750U,751U,60000U}}) {
            a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::OverlayFade;
            ca.value=cb.value=bases[scene]+delta;
            updateCampaignMapPresentation(a);original.updateCampaignMapPresentation(b);surfaces(scene==0 && (delta==749U || delta==750U));compare("fade");
        }
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::OverlayFade;
        ca.values={bases[scene]+20U,bases[scene]+33U,bases[scene]+66U};cb.values=ca.values;ca.cursor=cb.cursor=0;
        surfaces(false);expect(ca.cursor==3U,"compose samples elapsed and each nonempty animation independently; empty animation skips");compare("three-reads");
        ca.values.clear();cb.values.clear();a.mapPresentation.paletteFallback=b.mapPresentation.paletteFallback=true;
        const auto before=ca.reads.size();surfaces(false);expect(ca.reads.size()==before+1U,"palette fallback samples only original elapsed input");compare("palette-fallback");
        a.mapPresentation.paletteFallback=b.mapPresentation.paletteFallback=false;
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::Interactive;
        const auto count=ca.reads.size();skipCampaignMapIntro(a);original.skipCampaignMapIntro(b);
        expect(ca.reads.size()==count,"interactive skip returns before clock");compare("skip-inactive");
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::VqaIntro;
        skipCampaignMapIntro(a);original.skipCampaignMapIntro(b);expect(a.mapPresentation.phase==CampaignMapPresentationPhase::Interactive,"valid skip reaches original target stage");compare("skip-valid");
        a.database.campaigns[0].mapSelectionStages[0].choices.clear();b.database.campaigns[0].mapSelectionStages[0].choices.clear();
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::VqaIntro;
        skipCampaignMapIntro(a);original.skipCampaignMapIntro(b);expect(a.mapPresentation.phase==CampaignMapPresentationPhase::FallbackList,"no usable target skip fails closed");compare("skip-no-target");
        configureCampaignClockState(a,config);configureCampaignClockState(b,config);a.mapPresentation.stageSection=b.mapPresentation.stageSection="missing-stage";
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::VqaIntro;
        skipCampaignMapIntro(a);original.skipCampaignMapIntro(b);compare("skip-missing-stage");
        configureCampaignClockState(a,config);configureCampaignClockState(b,config);
        const bool la=a.mapPresentation.vqa.loadFromMemory(vqaBytes),lb=b.mapPresentation.vqa.loadFromMemory(vqaBytes);
        expect(la && lb,"actual production decoder accepts all8 strictly indexed complete VQA frames");if(!la || !lb)return 1;
        expect(a.mapPresentation.vqa.frameCount()==8U,"eight-frame fixture count verified");
        a.mapPresentation.phase=b.mapPresentation.phase=CampaignMapPresentationPhase::VqaIntro;
        a.mapPresentation.lastFrameAt=b.mapPresentation.lastFrameAt=0U;ca.value=cb.value=1000U;
        updateCampaignMapPresentation(a);original.updateCampaignMapPresentation(b);
        expect(a.mapPresentation.vqa.currentFrameIndex()==4U && a.mapPresentation.lastFrameAt==264U,"real VQA update advances exactly4 frames on late time");compare("vqa-cap4");
        updateCampaignMapPresentation(a);original.updateCampaignMapPresentation(b);
        expect(a.mapPresentation.vqa.currentFrameIndex()==7U && a.mapPresentation.phase==CampaignMapPresentationPhase::OverlayFade,"same original second call drains remaining frames then enters fade");compare("vqa-end-fade");
        a.page=b.page=CampaignPage::Campaigns;const auto oldReads=ca.reads.size();
        refreshCampaignMapPresentation(a);original.refreshCampaignMapPresentation(b);
        expect(ca.reads.size()==oldReads && a.mapPresentation.key.empty(),"leaving missions resets presentation before clock");compare("leave");
        a.page=b.page=CampaignPage::Missions;a.campaignIndex=b.campaignIndex=-1;
        a.mapPresentation.key=b.mapPresentation.key="old-key";refreshCampaignMapPresentation(a);original.refreshCampaignMapPresentation(b);
        expect(a.mapPresentation.phase==CampaignMapPresentationPhase::FallbackList,"invalid selection uses original fallback");compare("invalid-selection");
        expect(a.presentationClock==&ca && b.presentationClock==&cb,"presentation reset retains outer borrowed clock");reads+=ca.reads.size();
    }
    expect(nonemptyGdi>0U,"native GDI comparisons included actual nonblank target pixels");
    currentTrace.close();originalTrace.close();expect(currentTrace.good() && originalTrace.good(),"complete state/read traces closed");
    std::cout<<"Campaign presentation clock contract: checks="<<checks<<" failures="<<failures<<" skipped=0 states="<<states<<" sourceReads="<<reads
        <<" cpuPairs="<<cpuPairs<<" gdiPairs="<<gdiPairs<<" nonemptyGdi="<<nonemptyGdi
        <<" originalMethods=7 vqaFrames=8 catchup=4 fade=749,750 independentComposeReads=3 audioDevice=not-run\n";
    return failures==0?0:1;
}
