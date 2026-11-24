namespace h017_private_qualification {
using namespace concord::runtime::session_detail;
#include "ImpactTimelineOriginal.inl"
#include "SuperTimelineOriginal.inl"
#include "DurationQueryOriginal.inl"
#include "MidpointQueryOriginal.inl"

std::string authority(RuntimeProductionHostState& s) {
    std::ostringstream out; std::string error;
    if(!writeRuntimeStateToStream(s,out,error)) throw std::runtime_error("complete authority save: "+error);
    return out.str();
}
auto timeline(const RuntimeImpactEffect& a) {
    return std::tuple{a.animationFrameCount,a.animationFrameDelayTicks,a.animationStartFrame,a.animationEndFrame,
        a.animationLoopStartFrame,a.animationLoopEndFrame,a.animationLoopCount,a.animationSequenceSteps,
        a.frameIndex,a.totalTicks,a.remainingTicks,a.animationMidpointWidth,a.animationMidpointHeight};
}
bool shpPixelsEqual(const RuntimeShpFrameImage& a,const RuntimeShpFrameImage& b){
    return a.offsetX==b.offsetX&&a.offsetY==b.offsetY&&a.globalWidth==b.globalWidth&&a.globalHeight==b.globalHeight
        &&a.width==b.width&&a.height==b.height&&a.pixels==b.pixels&&a.sourcePath==b.sourcePath;
}
int run(const std::string& mapPath, const std::string& outputPath) {
    int checks=0,states=0,positiveDurationStates=0,negativeDurationStates=0,frameEntriesCompared=0,selectedNonemptyByteStates=0;std::string where;
    const auto require=[&](bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(where+why);};
    try {
        const std::filesystem::path output(outputPath);
        require(!std::filesystem::exists(output),"fresh explicit private output required");
        RuntimeConfig assetConfig;assetConfig.localDataRootPath=output.generic_string();
        RuntimeMapSummary assetMap;assetMap.path=mapPath;
        const std::filesystem::path looseRoot(runtimeStructureRootPath(assetConfig,assetMap));
        require(!looseRoot.empty(),"actual structure source root");
        std::filesystem::create_directories(looseRoot);
        const auto corpus=shp_corpus::cases();require(corpus.size()==72,"complete original codec corpus");
        for(std::size_t n=0;n<corpus.size();++n){
            std::ofstream file(looseRoot/("h017_c"+std::to_string(n)+".shp"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(corpus[n].bytes.data()),static_cast<std::streamsize>(corpus[n].bytes.size()));
            require(static_cast<bool>(file),"write own synthetic resource");
        }
        for(int hz:{15,30,60}) {
            auto reference=std::make_unique<IndependentRecoveryHost>(),current=std::make_unique<IndependentRecoveryHost>();
            std::string error;
            require(configureRuntimeDetailSnapshotSmokeWorld(*reference,false,error),"original real scene "+error);
            require(configureRuntimeDetailSnapshotSmokeWorld(*current,false,error),"current real scene "+error);
            for(auto* s:{reference.get(),current.get()}) {
                s->config.localDataRootPath=output.generic_string();s->config.resourceRootPath=output.generic_string();
                s->config.defaultMapPath=mapPath;s->report.map.path=mapPath;s->config.fixedTickHz=hz;s->config.multiplayerActive=false;
                s->runtimeSoundsEnabled=false;s->runtimeSoundEffectsEnabled=false;s->runtimeSuppressPlayerAudioFeedback=true;
                s->runtimePreferencePersistenceEnabled=false;s->theaterLoaded=false;
                s->particles.reset(hz);
                require(s->particles.alignEmptyStateTick(s->tick),"particle authority clock");
                s->particlesInitialized=true;
            }
            for(std::size_t n=0;n<corpus.size();++n)for(int variant=0;variant<4;++variant){
                ++states;where="hz="+std::to_string(hz)+" corpus="+corpus[n].name+" variant="+std::to_string(variant)+" ";
                std::ostringstream artText;artText<<"[H017_CASE]\nImage=h017_c"<<n<<"\nRate="<<(variant==0?0:31)
                    <<"\nShadow="<<(variant%2?"yes":"no")<<'\n';
                if(variant>=2)artText<<"Start=1\nEnd=7\nLoopStart=2\nLoopEnd=4\nLoopCount=3\n";
                for(auto* s:{reference.get(),current.get()}){
                    IniFile art;require(art.loadFromString(artText.str())&&s->art.loadFromIni(art),"actual typed ART");
                    s->artLoaded=true;s->renderCache=RuntimeMapRenderCache{};
                }
                const auto* selectedArt=current->art.findArtType("H017_CASE");
                require(selectedArt&&selectedArt->image=="h017_c"+std::to_string(n),"actual ART image override");
                const auto beforeReference=authority(*reference),beforeCurrent=authority(*current);
                require(beforeReference==beforeCurrent,"original/current complete initial save");
                const int duration=h017OriginalDuration(*reference,"H017_CASE"),midpoint=h017OriginalMidpoint(*reference,"H017_CASE");
                require(runtimeSpecialDetailAnimationDurationTicks(*current,"H017_CASE")==duration,"actual scalar duration");
                require(runtimeSpecialDetailAnimationMidpointTicks(*current,"H017_CASE")==midpoint,"actual scalar midpoint");
                const auto& byteEntries=current->renderCache.auxiliaryShpBytesByImage;
                require(byteEntries.size()==1,"one real selected resource byte key");
                const auto& bytes=byteEntries.begin()->second;
                require(bytes.loadAttempted&&bytes.bytes==corpus[n].bytes,"actual selected complete original resource bytes");
                if(!corpus[n].bytes.empty()) {
                    ++selectedNonemptyByteStates;
                    require(bytes.loaded&&bytes.sourcePath.find("h017_c"+std::to_string(n)+".shp")!=std::string::npos,
                        "actual nonempty resource loaded from intended source");
                }
                if(corpus[n].frameIndex==0) {
                    if(corpus[n].runtime.success)require(duration>0&&midpoint>0,"known valid Runtime frame0 has positive timing");
                    else require(duration==0&&midpoint==0,"known invalid Runtime frame0 keeps zero timing");
                }
                if(duration>0)++positiveDurationStates;else ++negativeDurationStates;
                require(current->renderCache.auxiliaryShpSpritesByImage.empty(),"authority never populates drawing frame cache");
                require(authority(*reference)==beforeReference&&authority(*current)==beforeCurrent,"all authoritative save bytes unchanged");
                RuntimeImpactEffect oldVisual,newVisual;oldVisual.animId=newVisual.animId="H017_CASE";
                oldVisual.totalTicks=newVisual.totalTicks=7;
                h017OriginalSuperTimeline(*reference,oldVisual);initializeRuntimeSuperWeaponAnimation(*current,newVisual);
                require(timeline(oldVisual)==timeline(newVisual),"complete visual timing and midpoint dimensions");
                const auto& oldFrames=reference->renderCache.auxiliaryShpSpritesByImage;
                const auto& newFrames=current->renderCache.auxiliaryShpSpritesByImage;
                require(oldFrames.size()==newFrames.size(),"drawing frame cache keys/count");
                frameEntriesCompared+=static_cast<int>(oldFrames.size());
                for(const auto& [key,a]:oldFrames){
                    const auto p=newFrames.find(key);require(p!=newFrames.end(),"same drawing cache key");const auto& b=p->second;
                    require(a.loadAttempted==b.loadAttempted&&a.loaded==b.loaded&&a.resolvedImageId==b.resolvedImageId
                        &&a.sourcePath==b.sourcePath&&a.sourceFingerprint==b.sourceFingerprint&&a.frameIndex==b.frameIndex
                        &&a.frameCount==b.frameCount&&a.errorMessage==b.errorMessage&&a.hasShadowFrame==b.hasShadowFrame
                        &&a.shadowFrameIndex==b.shadowFrameIndex,"complete drawing load/source/error state");
                    require(shpPixelsEqual(a.image,b.image)&&shpPixelsEqual(a.shadowImage,b.shadowImage),"full main/shadow indexed pixels and geometry");
                }
                current->renderCache.auxiliaryShpSpritesByImage.clear();
                require(runtimeSpecialDetailAnimationDurationTicks(*current,"H017_CASE")==duration
                    &&runtimeSpecialDetailAnimationMidpointTicks(*current,"H017_CASE")==midpoint
                    &&current->renderCache.auxiliaryShpSpritesByImage.empty(),"drawing frame eviction cannot alter scalar authority");
                const auto oldRemoved=releaseRuntimeAuxiliaryShpImageFromCache(reference->renderCache,"H017_CASE");
                const auto newRemoved=releaseRuntimeAuxiliaryShpImageFromCache(current->renderCache,"H017_CASE");
                require(oldRemoved>=newRemoved&&current->renderCache.auxiliaryShpBytesByImage.empty(),"original image release also evicts derived validity");
                require(runtimeSpecialDetailAnimationDurationTicks(*current,"H017_CASE")==h017OriginalDuration(*reference,"H017_CASE")
                    &&runtimeSpecialDetailAnimationMidpointTicks(*current,"H017_CASE")==h017OriginalMidpoint(*reference,"H017_CASE"),"cold reload after original release");
                for(auto* s:{reference.get(),current.get()})s->artLoaded=false;
                require(runtimeSpecialDetailAnimationDurationTicks(*current,"H017_CASE")==h017OriginalDuration(*reference,"H017_CASE")
                    &&runtimeSpecialDetailAnimationMidpointTicks(*current,"H017_CASE")==h017OriginalMidpoint(*reference,"H017_CASE"),"distinct original loaded guards");
                require(runtimeSpecialDetailAnimationDurationTicks(*current,"")==h017OriginalDuration(*reference,"")
                    &&runtimeSpecialDetailAnimationMidpointTicks(*current,"")==h017OriginalMidpoint(*reference,""),"original empty guards");
                for(auto* s:{reference.get(),current.get()})s->artLoaded=true;
                require(authority(*reference)==beforeReference&&authority(*current)==beforeCurrent,"visual/release/reload preserve complete authority");
            }
        }
        require(positiveDurationStates>0&&negativeDurationStates>0&&frameEntriesCompared>0&&selectedNonemptyByteStates>0,
            "positive and negative source/pixel qualification required");
        std::cout<<"private-animation-timing checks="<<checks<<" actual-root-states="<<states
            <<" complete-corpus=72 rates=15,30,60 positive-timing-states="<<positiveDurationStates
            <<" negative-timing-states="<<negativeDurationStates<<" compared-frame-entries="<<frameEntriesCompared
            <<" selected-nonempty-byte-states="<<selectedNonemptyByteStates
            <<" scalar-and-original-full-image=exact negative-cache-release-pixels-save=exact failures=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<where<<e.what()<<'\n';return 1;}
}
}
