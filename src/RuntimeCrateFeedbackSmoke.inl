// Test-only oracle: original MIX bytes, independent SHP row decoding and
// six-bit PAL expansion. The simulation receives only real UI Move orders.
#include "RuntimeCrateFeedbackSaveSmoke.inl"
namespace RuntimeCrateFeedbackOracle {
using Bytes = std::vector<std::uint8_t>;
struct Frame { int canvasW=0,canvasH=0,x=0,y=0,w=0,h=0; Bytes indices; };
std::uint32_t little(const Bytes& b,std::size_t p,int n) {
    if(p>b.size()||static_cast<std::size_t>(n)>b.size()-p)throw std::runtime_error("original SHP truncated");
    std::uint32_t v=0;for(int i=n-1;i>=0;--i)v=(v<<8)|b[p+i];return v;
}
Bytes read(MixArchive& mix,const std::string& name,std::ofstream& sources) {
    Bytes b;std::string error;const auto* entry=mix.findEntryByName(name);
    if(!entry||!mix.readEntryData(*entry,b,error))throw std::runtime_error("original MIX "+name+": "+error);
    sources<<mix.getPath()<<','<<name<<','<<entry->id<<','<<entry->absoluteOffset<<','<<b.size()<<'\n';return b;
}
Frame decode(const Bytes& b,int number) {
    if(number<0||static_cast<unsigned>(number)>=little(b,6,2))throw std::runtime_error("original frame absent");
    const std::size_t p=8+24*static_cast<std::size_t>(number);Frame f;
    f.canvasW=little(b,2,2);f.canvasH=little(b,4,2);
    f.x=static_cast<std::int16_t>(little(b,p,2));f.y=static_cast<std::int16_t>(little(b,p+2,2));
    f.w=little(b,p+4,2);f.h=little(b,p+6,2);
    if(f.canvasW<=0||f.canvasH<=0||f.w>4096||f.h>4096)throw std::runtime_error("original frame dimensions");
    f.indices.assign(static_cast<std::size_t>(f.w)*f.h,0);if(f.indices.empty())return f;
    std::size_t cursor=little(b,p+20,4);const auto format=little(b,p+8,1);
    if(format==0||format==1){
        if(cursor>b.size()||f.indices.size()>b.size()-cursor)throw std::runtime_error("original raw frame truncated");
        std::copy_n(b.begin()+cursor,f.indices.size(),f.indices.begin());return f;
    }
    if(format!=3)throw std::runtime_error("unsupported nonempty original SHP format");
    for(int y=0;y<f.h;++y){
        const auto length=little(b,cursor,2);
        if(length<2||cursor>b.size()||length>b.size()-cursor)throw std::runtime_error("original row extent");
        const auto end=cursor+length;cursor+=2;int x=0;
        while(cursor<end){const auto color=b[cursor++];if(color){
            if(x>=f.w)throw std::runtime_error("original opaque row overflow");f.indices[y*f.w+x++]=color;
        }else{if(cursor==end||!b[cursor])throw std::runtime_error("original transparent run");x+=b[cursor++];}}
        if(x<f.w||x>f.w+1)throw std::runtime_error("original decoded row width");
    }
    return f;
}
std::array<std::uint32_t,256> palette(const Bytes& b) {
    if(b.size()!=768||*std::max_element(b.begin(),b.end())>63)throw std::runtime_error("original anim PAL");
    std::array<std::uint32_t,256> p{};for(std::size_t i=0;i<256;++i)
        p[i]=(std::uint32_t(b[3*i])<<18)|(std::uint32_t(b[3*i+1])<<10)|(std::uint32_t(b[3*i+2])<<2);return p;
}
bool bmp(const std::filesystem::path& path,const std::vector<std::uint32_t>& pixels,int w,int h) {
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;
    file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+w*h*4;
    info.biSize=sizeof(info);info.biWidth=w;info.biHeight=-h;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));
    out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(std::uint32_t));return out.good();
}
std::size_t differences(const std::vector<std::uint32_t>& a,const std::vector<std::uint32_t>& b) {
    if(a.size()!=b.size())return std::max(a.size(),b.size());std::size_t n=0;
    for(std::size_t i=0;i<a.size();++i)n+=((a[i]^b[i])&0xffffffU)!=0;return n;
}
}

bool runRuntimeCrateFeedbackSmoke(RuntimeWindowState& live,std::string& error) {
    using namespace RuntimeCrateFeedbackOracle;
    const auto fail=[&](const std::string& why){error="crate feedback: "+why;return false;};
    try {
        std::cout<<std::unitbuf;
        const char* output=std::getenv("CONCORD_CRATE_FEEDBACK_OUTPUT");
        const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-19-complete/09-crate-icons/checks";
        std::filesystem::create_directories(root);
        std::ofstream sources(root/"original-resources.csv"),records(root/"crate-feedback.csv"),summary(root/"summary.txt");
        if(!sources||!records||!summary)return fail("evidence files");
        sources<<"archive,entry,id,absolute_offset,bytes\n";
        records<<"case,hz,pickup_tick,age,expected_frame,expected_delay,expected_total,present,actual_frame,actual_total,actual_remaining,world_x,world_y,world_level,visible_pixels,pixel_mismatch,repeat_mismatch,reward_ok,result\n";
        MixArchive shapes,palettes;const auto assetRoot=std::filesystem::path(runtimeAssetRootPath(live.config));
        if(!shapes.loadFromPath(assetRoot/"ra2/conquer.mix")||!palettes.loadFromPath(assetRoot/"ra2/cache.mix"))
            return fail("original conquer/cache MIX unavailable");
        const auto colors=palette(read(palettes,"anim.pal",sources));
        struct Spec {const char* reward;const char* animation;const char* file;int nativeDelay;};
        const std::array<Spec,7> specs{{
            {"Money","MONEY","money.shp",2},{"Armor","ARMOR","armor.shp",2},
            {"Firepower","FIREPOWR","firepowr.shp",2},{"HealBase","HEALALL","healall.shp",2},
            {"Reveal","REVEAL","reveal.shp",2},{"Speed","SPEED","speed.shp",1},
            {"Veteran","VETERAN","veteran.shp",2}
        }};
        std::map<std::string,std::array<Frame,23>> frames;
        for(const auto& spec:specs){const auto bytes=read(shapes,spec.file,sources);
            if(little(bytes,6,2)!=23)return fail(std::string(spec.animation)+" stock frame count");
            for(int f=0;f<23;++f)frames[spec.animation][f]=decode(bytes,f);
        }
        int clearTile=-1;const int clearSet=live.theater.getGeneralTileSetIndex("ClearTile");
        const auto terrainContext=runtimeTerrainMetadataContext(live);
        for(int tile=0;tile<static_cast<int>(live.theater.getTotalTileSlots());++tile){TerrainCell cell;
            cell.tileIndex=tile;cell.subTileIndex=0;
            if(live.theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(cell,terrainContext,live.renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(cell,terrainContext,live.renderCache)==0){clearTile=tile;break;}}
        if(clearTile<0)return fail("stock flat Clear TMP");
        constexpr RuntimeCellPoint crateCell{20,20};constexpr int collectorId=101,homeId=102,width=320,height=240;
        const auto make=[&](int hz){
            auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;seedRuntimeLoadScratch(s,live,false);
            RuntimeSnapshotActiveStateScope active(s);s.world=live.world;s.world.accessObjects().clear();
            s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
            s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
            s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
            s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
            s.config.fixedTickHz=hz;s.config.noCooldown=false;s.config.playerSlots.clear();s.config.playerSlots.resize(2);
            for(int i=0;i<2;++i){auto& slot=s.config.playerSlots[i];slot.enabled=slot.human=true;slot.local=i==0;
                slot.startPosition=i+1;slot.country=i?"Russians":"Americans";slot.team="-";slot.color=i?"Red":"Gold";
                slot.peerId=i?"crate-remote":"crate-local";slot.name=i?"Distant opponent":"Crate collector";}
            s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=s.config.crates=s.config.superweapons=false;
            s.campaignMode=false;s.config.gameMode="Battle";s.config.playerCountry="Americans";
            s.skirmishMatchFinished=s.skirmishVictory=false;s.skirmishResult.clear();
            initializeRuntimeSkirmishPlayerIdentities(s,"Americans");s.economy.credits=10000;s.economy.skirmishStartApplied=true;
            s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
            s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
            s.skirmishCrates.clear();s.initialSkirmishCrates.clear();s.impactEffects.clear();
            for(int y=0;y<40;++y)for(int x=0;x<40;++x){TerrainCell cell;cell.x=x;cell.y=y;
                cell.cellIndex512=y*512+x;cell.tileIndex=clearTile;cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);}
            const auto add=[&](int id,const char* type,WorldObjectKind kind,int x,int y){WorldObjectInstance o;
                o.instanceId=id;o.type=type;o.kind=kind;o.mapId="crate-feedback-"+std::to_string(id);o.owner=s.economy.owner;
                o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.alive=o.poweredOperational=true;o.mission="Guard";o.currentAmmo=-1;
                o.health=runtimeMaxHealthForObject(s,o);clearRuntimeRenderPosition(o);s.world.accessObjects().push_back(o);};
            add(collectorId,"MTNK",WorldObjectKind::Unit,18,20);add(homeId,"GAPOWR",WorldObjectKind::Structure,5,5);
            add(103,"NAPOWR",WorldObjectKind::Structure,35,35);findMutableObjectById(s,103)->owner=runtimeSkirmishOwnerForSlot(1);
            findMutableObjectById(s,homeId)->health-=50;
            initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);
            s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);initializeRuntimeHumanHouseAuthorities(s);
            s.vision.reset();s.vision.update(s.world,&s.rules,{{s.economy.owner,{}}},s.tick);return storage;
        };
        const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);};
        const auto capture=[&](RuntimeWindowState& s,std::vector<std::uint32_t>& pixels){
            RuntimeSnapshotActiveStateScope active(s);return drawRuntimeStateOffscreenAtCell(s,crateCell,width,height,error,"",false,false,-1,&pixels).has_value();};
        int cases=0,checks=0,failures=0,repeatChecks=0;std::size_t mismatchedPixels=0;std::string firstFailure;
        for(int hz:{15,30,60})for(const auto& spec:specs){
            const std::string label=std::string(spec.animation)+"-"+std::to_string(hz)+"Hz";
            auto storage=make(hz);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            const auto* rule=runtimeCrateRuleForType(s,spec.reward);if(!rule)return fail(label+" original Powerups rule");
            RuntimeCrateState crate;crate.cell=crateCell;crate.authored=true;crate.imageId="CRATE";runtimeConfigureCratePowerup(crate,*rule);
            s.skirmishCrates={crate};s.initialSkirmishCrates=s.skirmishCrates;
            const int beforeCredits=s.economy.credits;const bool beforeRevealed=s.vision.isCellExploredBy(s.economy.owner,30,3);
            applySelection(s,{collectorId},false,false);
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,crateCell)||s.lastCommand.acceptedCount!=1)
                return fail(label+" actual UI Move rejected");
            int waited=0;while(s.skirmishCrates.front().active&&waited++<hz*40)step(s);
            if(s.skirmishCrates.front().active)return fail(label+" actual collector never reached crate");
            const int pickupTick=s.tick;const auto* collector=findObjectById(s,collectorId);const auto* home=findObjectById(s,homeId);
            bool rewardOk=collector&&home&&s.interactionStatus.find(std::string("collected ")+spec.reward+" crate")!=std::string::npos;
            const std::string reward=spec.reward;
            if(reward=="Money")rewardOk=rewardOk&&s.economy.credits-beforeCredits>=2000&&s.economy.credits-beforeCredits<=2900;
            if(reward=="Armor")rewardOk=rewardOk&&collector->crateArmorMultiplier==1.5;
            if(reward=="Firepower")rewardOk=rewardOk&&collector->crateFirepowerMultiplier==2.0;
            if(reward=="Speed")rewardOk=rewardOk&&std::abs(collector->crateSpeedMultiplier-1.2)<1e-9;
            if(reward=="HealBase")rewardOk=rewardOk&&home->health==runtimeMaxHealthForObject(s,*home);
            if(reward=="Veteran")rewardOk=rewardOk&&collector->veterancy>=1;
            if(reward=="Reveal")rewardOk=rewardOk&&!beforeRevealed&&s.vision.isCellExploredBy(s.economy.owner,30,3);
            const int delay=spec.nativeDelay*hz/15,total=23*delay;bool caseOk=true;
            // Native +200 vertical leptons projects to 28.70495792324 pixels
            // at 60x30. Integer engine cell coordinates already mean centers.
            // 600 = (crateCell.x + crateCell.y) * 15, before camera translation.
            const int anchorX=width/2;
            const int anchorY=static_cast<int>(std::llround(600.0-200.0*60.0*std::sqrt(3.0)/(512.0*std::sqrt(2.0))))-(600-height/2);
            RuntimeMapRenderContext resourceContext;resourceContext.rules=&s.rules;resourceContext.art=&s.art;resourceContext.theater=&s.theater;
            resourceContext.assetRootPath=runtimeAssetRootPath(s.config);resourceContext.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
            resourceContext.palettePath=runtimePalettePath(s.config,s.report.map);resourceContext.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
            for(int number=0;number<=23;++number){
                const int age=number*delay;while(s.tick-pickupTick<age)step(s);
                const auto found=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& effect){return effect.animId==spec.animation;});
                const bool present=found!=s.impactEffects.end();RuntimeImpactEffect observed;if(present)observed=*found;
                std::vector<std::uint32_t> actual,background;if(!capture(s,actual))return fail(label+" actual render: "+error);
                auto held=std::move(s.impactEffects);s.impactEffects.clear();const bool baseOk=capture(s,background);s.impactEffects=std::move(held);
                if(!baseOk)return fail(label+" background render: "+error);auto expected=background;std::size_t visible=0;
                if(number<23){const auto& frame=frames.at(spec.animation)[number];RuntimeShpFrameImage loaded;std::string resolved,source;int count=0;
                    if(!loadRuntimeAuxiliaryShpFrameImage(resourceContext,s.renderCache,spec.animation,number,loaded,resolved,source,error,false,&count)
                        ||count!=23||loaded.width!=frame.w||loaded.height!=frame.h||loaded.globalWidth!=frame.canvasW||loaded.globalHeight!=frame.canvasH
                        ||loaded.offsetX!=frame.x||loaded.offsetY!=frame.y||loaded.pixels!=frame.indices)return fail(label+" actual resource differs from original MIX frame "+std::to_string(number)+": "+error);
                    for(int y=0;y<frame.h;++y)for(int x=0;x<frame.w;++x){const auto index=frame.indices[y*frame.w+x];if(!index)continue;
                        const int px=anchorX-frame.canvasW/2+frame.x+x,py=anchorY-frame.canvasH/2+frame.y+y;
                        if(px>=0&&py>=0&&px<width&&py<height){expected[py*width+px]=colors[index];++visible;}}
                }
                const auto mismatch=differences(actual,expected);mismatchedPixels+=mismatch;std::size_t repeatMismatch=0;
                if(number==10){const int tick=s.tick;std::vector<std::uint32_t> repeat;if(!capture(s,repeat))return fail(label+" repeat render");
                    repeatMismatch=differences(actual,repeat);if(s.tick!=tick||s.impactEffects.size()!=(present?1U:0U))++repeatMismatch;
                    if(present&&(s.impactEffects.front().remainingTicks!=observed.remainingTicks||s.impactEffects.front().frameIndex!=observed.frameIndex))++repeatMismatch;
                    ++repeatChecks;
                }
                const bool timingOk=number==23?!present:present&&observed.totalTicks==total&&observed.remainingTicks==total-age
                    &&observed.animationFrameCount==23&&observed.animationFrameDelayTicks==delay&&observed.frameIndex==number;
                const double effectX=observed.hasWorldPosition?observed.worldX:observed.cell.x;
                const double effectY=observed.hasWorldPosition?observed.worldY:observed.cell.y;
                const double effectLevel=observed.hasWorldPosition?observed.worldLevel:observed.level;
                const bool anchorOk=number==23||present&&(effectX==crateCell.x&&effectY==crateCell.y
                    &&std::abs(effectLevel-200.0*std::sqrt(6.0)/256.0)<1e-8);
                const bool ok=rewardOk&&timingOk&&anchorOk&&!mismatch&&!repeatMismatch;caseOk=caseOk&&ok;++checks;
                records<<label<<','<<hz<<','<<pickupTick<<','<<age<<','<<(number==23?-1:number)<<','<<delay<<','<<total<<','<<present<<','
                    <<(present?observed.frameIndex:-1)<<','<<(present?observed.totalTicks:0)<<','<<(present?observed.remainingTicks:0)<<','
                    <<effectX<<','<<effectY<<','<<effectLevel<<','<<visible<<','<<mismatch<<','<<repeatMismatch<<','<<rewardOk<<','<<(ok?"pass":"fail")<<'\n';
                if(number==0||number==10||number==21||number==22){const auto prefix=label+"-frame"+std::to_string(number);
                    if(!bmp(root/(prefix+"-actual.bmp"),actual,width,height)||!bmp(root/(prefix+"-expected.bmp"),expected,width,height))return fail("BMP evidence");}
            }
            ++cases;if(!caseOk){++failures;if(firstFailure.empty())firstFailure=label;}
            std::cout<<"Crate feedback "<<label<<": reward="<<rewardOk<<" 23 original frames + expiry "<<(caseOk?"passed":"FAILED (evidence retained)")<<'\n';
        }
        std::ofstream controls(root/"controls.csv");if(!controls)return fail("control evidence file");
        controls<<"control,detail,witness,pass\n";int controlChecks=0;
        const auto control=[&](const std::string& name,const std::string& detail,std::size_t witness,bool ok){
            controls<<name<<','<<detail<<','<<witness<<','<<ok<<'\n';++controlChecks;
            if(!ok){++failures;if(firstFailure.empty())firstFailure=name;}return ok;
        };
        const auto addCrate=[&](RuntimeWindowState& s,const char* reward,RuntimeCellPoint cell){
            const auto* rule=runtimeCrateRuleForType(s,reward);if(!rule)throw std::runtime_error("control stock reward absent");
            RuntimeCrateState crate;crate.cell=cell;crate.authored=true;crate.imageId="CRATE";runtimeConfigureCratePowerup(crate,*rule);
            s.skirmishCrates.push_back(crate);s.initialSkirmishCrates=s.skirmishCrates;
        };
        const auto collect=[&](RuntimeWindowState& s,std::size_t index){
            RuntimeSnapshotActiveStateScope active(s);applySelection(s,{collectorId},false,false);
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,s.skirmishCrates.at(index).cell)
                ||s.lastCommand.acceptedCount!=1)return false;
            int wait=0;while(s.skirmishCrates.at(index).active&&wait++<s.config.fixedTickHz*40)step(s);
            return !s.skirmishCrates.at(index).active;
        };
        // Unit really creates a goodie unit, but its stock <none> animation
        // must not turn into a generic impact or a leftover Money symbol.
        {
            auto storage=make(30);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            addCrate(s,"Unit",crateCell);const auto beforeObjects=s.world.getObjects().size();const int credits=s.economy.credits;
            if(!collect(s,0))return fail("Unit control real UI pickup");
            bool ok=s.interactionStatus.find("collected Unit crate")!=std::string::npos
                &&s.world.getObjects().size()==beforeObjects+1&&s.economy.credits==credits&&s.impactEffects.empty();
            for(int n=0;n<96;++n){step(s);ok=ok&&s.impactEffects.empty()&&!s.skirmishCrates.front().active&&s.economy.credits==credits;}
            std::vector<std::uint32_t> pixels;if(!capture(s,pixels)||!bmp(root/"unit-none-actual.bmp",pixels,width,height))return fail("Unit control image");
            control("unit-none","actual unit granted with no feedback for 96 ticks",s.world.getObjects().size()-beforeObjects,ok);
        }
        // Three authored adjacent crates are collected through three real
        // orders. The second Armor cannot stack and must award Money. The
        // third Money then overlaps that Money sequence without resetting it.
        {
            auto storage=make(30);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            addCrate(s,"Armor",crateCell);addCrate(s,"Armor",{20,21});addCrate(s,"Money",{20,22});
            struct Award {const char* animation;RuntimeCellPoint cell;int tick;};std::vector<Award> awards;
            for(std::size_t i=0;i<3;++i){const int credits=s.economy.credits;if(!collect(s,i))return fail("repeat/fallback real UI pickup");
                const char* animation=i?"MONEY":"ARMOR";awards.push_back({animation,s.skirmishCrates[i].cell,s.tick});
                const auto* unit=findObjectById(s,collectorId);const bool bonus=unit&&unit->crateArmorMultiplier==1.5;
                const int earned=s.economy.credits-credits;
                const bool reward=bonus&&(i?earned>=2000&&earned<=2900:earned==0)
                    &&s.interactionStatus.find(i?"collected Money crate":"collected Armor crate")!=std::string::npos;
                std::vector<const Award*> activeAwards;for(const auto& a:awards)if(s.tick-a.tick<92)activeAwards.push_back(&a);
                bool timelines=s.impactEffects.size()==activeAwards.size();
                for(std::size_t n=0;timelines&&n<activeAwards.size();++n){const auto& a=*activeAwards[n];const auto& effect=s.impactEffects[n];
                    timelines=effect.animId==a.animation&&effect.cell==a.cell&&effect.totalTicks==92
                        &&effect.remainingTicks==92-(s.tick-a.tick)&&effect.frameIndex==(s.tick-a.tick)/4;
                }
                control(i==0?"repeat-first-award":i==1?"armor-fallback-money":"repeat-money-independent-clock",
                    i==0?"first Armor multiplier":i==1?"second Armor grants Money and its own symbol":"third crate preserves previous Money age",
                    activeAwards.size(),reward&&timelines&&(i!=2||std::count_if(activeAwards.begin(),activeAwards.end(),[](const Award* a){return std::string(a->animation)=="MONEY";})==2));
            }
            for(int n=0;n<4;++n)step(s);std::vector<std::uint32_t> actual,background;if(!capture(s,actual))return fail("repeat actual render");
            auto held=std::move(s.impactEffects);s.impactEffects.clear();const bool baseOk=capture(s,background);s.impactEffects=std::move(held);
            if(!baseOk)return fail("repeat background render");auto expected=background;std::size_t visible=0;
            for(const auto& a:awards){const int age=s.tick-a.tick;if(age<0||age>=92)continue;
                const auto& frame=frames.at(a.animation)[age/4];
                const int x=(a.cell.x-a.cell.y)*30+width/2;
                const int y=static_cast<int>(std::llround((a.cell.x+a.cell.y)*15.0-200.0*60.0*std::sqrt(3.0)/(512.0*std::sqrt(2.0))))-600+height/2;
                for(int fy=0;fy<frame.h;++fy)for(int fx=0;fx<frame.w;++fx){const auto index=frame.indices[fy*frame.w+fx];if(!index)continue;
                    const int px=x-frame.canvasW/2+frame.x+fx,py=y-frame.canvasH/2+frame.y+fy;
                    if(px>=0&&py>=0&&px<width&&py<height){expected[py*width+px]=colors[index];++visible;}}
            }
            const auto mismatch=differences(actual,expected);control("repeat-original-pixels","two independent Money icons at their own crate cells",visible,visible>0&&!mismatch);
            if(!bmp(root/"repeat-actual.bmp",actual,width,height)||!bmp(root/"repeat-expected.bmp",expected,width,height))return fail("repeat images");
        }
        // View the real collected effect from the existing remote house. Its
        // own units are distant, so the crate cell starts genuinely shrouded.
        // Vision APIs alter only the observer fixture; the effect is never
        // moved, replaced, made owner-visible, or advanced by rendering.
        {
            auto storage=make(30);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);addCrate(s,"Money",crateCell);
            if(!collect(s,0))return fail("vision control real UI pickup");for(int n=0;n<40;++n)step(s);
            const std::string observer=runtimeSkirmishOwnerForSlot(1);s.selectedObjectIds.clear();s.visionEnabled=true;
            s.vision.reset();s.vision.update(s.world,&s.rules,{{s.economy.owner,{}},{observer,{}}},s.tick);
            s.economy.owner=observer;s.config.seeHiddenObjects=false;const int tick=s.tick;
            std::ofstream diagnostics(root/"vision-diagnostics.csv");if(!diagnostics)return fail("vision diagnostic file");
            diagnostics<<"phase,observer,mutation_return,visible_before,visible_after,revision_before,revision_after,drawn,hidden,changed_pixels,frame,remaining\n";
            for(int phase=0;phase<3;++phase){
                bool mutation=true;
                if(phase==1)mutation=s.vision.revealArea(observer,crateCell.x,crateCell.y,8);
                if(phase==2)mutation=s.vision.resetShroud(observer);
                const auto visibilityBefore=s.vision.visibilityFor(observer,crateCell.x,crateCell.y);
                const auto revisionBefore=s.vision.revisionFor(observer);
                std::vector<std::uint32_t> actual,background;
                const auto stats=drawRuntimeStateOffscreenAtCell(s,crateCell,width,height,error,"",false,false,-1,&actual,false,nullptr,false,true);
                auto held=std::move(s.impactEffects);s.impactEffects.clear();const auto base=drawRuntimeStateOffscreenAtCell(s,crateCell,width,height,error,"",false,false,-1,&background,false,nullptr,false,true);s.impactEffects=std::move(held);
                if(!stats||!base)return fail("vision render: "+error);const auto witness=differences(actual,background);
                const auto visibilityAfter=s.vision.visibilityFor(observer,crateCell.x,crateCell.y);
                const auto revisionAfter=s.vision.revisionFor(observer);const bool visible=phase==1;
                const bool visionOk=mutation&&visibilityBefore==visibilityAfter&&revisionBefore==revisionAfter
                    &&visibilityBefore==(visible?RuntimeCellVisibility::Visible:RuntimeCellVisibility::Shrouded);
                const bool ok=visionOk&&s.tick==tick&&s.impactEffects.size()==1&&s.impactEffects.front().frameIndex==10
                    &&(visible?stats->impactActorDrawn==1&&witness>0:stats->visionHiddenImpactActors==1&&witness==0);
                diagnostics<<phase<<','<<observer<<','<<mutation<<','<<static_cast<int>(visibilityBefore)<<','<<static_cast<int>(visibilityAfter)
                    <<','<<revisionBefore<<','<<revisionAfter<<','<<stats->impactActorDrawn<<','<<stats->visionHiddenImpactActors<<','<<witness<<','
                    <<(s.impactEffects.empty()?-1:s.impactEffects.front().frameIndex)<<','<<(s.impactEffects.empty()?0:s.impactEffects.front().remainingTicks)<<'\n';
                control(phase==0?"vision-shrouded":phase==1?"vision-revealed":"vision-reshrouded","real Money frame 10 under remote observer",witness,ok);
                if(!bmp(root/("vision-"+std::to_string(phase)+"-actual.bmp"),actual,width,height))return fail("vision evidence");
            }
        }
        // The repair belongs to crate spawning. Calling the ordinary impact
        // factory with the same original image must retain its old four-rule-
        // frame contract, without the crate's +200 height or full timeline.
        for(int hz:{15,30,60}){
            auto storage=make(hz);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            spawnRuntimeImpact(s,crateCell,0,false,"MONEY");const int total=4*hz/15;
            bool ok=s.impactEffects.size()==1&&s.impactEffects.front().totalTicks==total
                &&s.impactEffects.front().remainingTicks==total&&s.impactEffects.front().animationFrameCount==0
                &&!s.impactEffects.front().hasWorldPosition&&s.impactEffects.front().level==0;
            for(int age=1;age<=total;++age){step(s);ok=ok&&(age==total?s.impactEffects.empty():
                s.impactEffects.size()==1&&s.impactEffects.front().remainingTicks==total-age);}
            control("ordinary-impact-"+std::to_string(hz)+"Hz","same image remains an ordinary four-rule-frame impact",total,ok);
        }
        summary<<"cases="<<cases<<" checks="<<checks<<" repeatChecks="<<repeatChecks<<" controlChecks="<<controlChecks<<" mismatchedPixels="<<mismatchedPixels<<" failures="<<failures<<'\n';
        std::cout<<"Crate feedback: cases="<<cases<<" checks="<<checks<<" repeatChecks="<<repeatChecks<<" mismatchedPixels="<<mismatchedPixels<<" failures="<<failures<<'\n';
        const bool savesOk=runRuntimeCrateFeedbackSaveSmoke(live,root,error);
        if(!savesOk){++failures;if(firstFailure.empty())firstFailure=error;summary<<"saveFailure="<<error<<'\n';}
        if(failures)return fail(std::to_string(failures)+" cases or controls failed; first="+firstFailure+"; full CSV and original expected/actual images retained");
        std::cout<<"Crate feedback controls: "<<controlChecks<<" passed (none/fallback/repeat/vision/ordinary impact)\n";
        error.clear();return true;
    }catch(const std::exception& ex){return fail(ex.what());}
}
