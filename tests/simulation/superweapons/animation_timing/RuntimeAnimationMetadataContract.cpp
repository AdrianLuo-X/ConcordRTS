#include "assets/animation/RuntimeArtAnimationSequence.h"
#include "assets/sprites/RuntimeAuxiliaryShpBytes.h"
#include "simulation/superweapons/RuntimeSuperWeaponAnimationTiming.h"
#include "assets/codecs/ShpCodec.h"
#include "data/art/ArtDatabase.h"
#include "../../../assets/ShpCodecCorpus.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
using concord::assets::animation::RuntimeArtAnimationSequence;
using concord::assets::animation::resolveRuntimeArtAnimationSequence;
using concord::simulation::superweapons::RuntimeAnimationFrame0Validator;
using concord::simulation::superweapons::RuntimeSuperWeaponAnimationTiming;
using concord::simulation::superweapons::resolveRuntimeSuperWeaponAnimationTiming;
namespace original {
struct Impact {
    int animationStartFrame=0,animationEndFrame=0,animationLoopStartFrame=0,animationLoopEndFrame=0;
    int animationLoopCount=1,animationSequenceSteps=0;
};
RuntimeArtAnimationSequence sequence(const ArtType* art,int frameCount) {
    RuntimeArtAnimationSequence result;
    if(art==nullptr||frameCount<=0)return result;
    Impact impact;
#include "ArtAnimationSequenceOriginal.inl"
    result.startFrame=impact.animationStartFrame;result.endFrame=impact.animationEndFrame;
    result.loopStartFrame=impact.animationLoopStartFrame;result.loopEndFrame=impact.animationLoopEndFrame;
    result.loopCount=impact.animationLoopCount;result.introSteps=introSteps;result.loopSpan=loopSpan;
    result.outroSteps=outroSteps;result.sequenceSteps=impact.animationSequenceSteps;
    return result;
}
int delay(const ArtType* art,int fixedTickHz) {
#include "SuperWeaponDelayOriginal.inl"
}
#include "VisibleFramesOriginal.inl"
// The original frame cache commits diagnostics only after its decoder returns.
struct FrameCache {bool loadAttempted=false;bool loaded=false;std::string errorMessage;};
RuntimeSuperWeaponAnimationTiming timing(const ArtType* art,int hz,
    RuntimeAuxiliaryShpBytesCacheEntry* resource,FrameCache& entry,RuntimeAnimationFrame0Validator& decoder) {
    RuntimeSuperWeaponAnimationTiming result;
    if(!art||!resource||!resource->loaded||resource->frameCount<=0)return result;
    if(!entry.loadAttempted) {
        entry.loadAttempted=true;
        std::string errorMessage;
        entry.loaded=decoder.validate(resource->bytes,resource->sourcePath,errorMessage);
        entry.errorMessage=errorMessage;
    }
    if(!entry.loaded)return result;
    const int count=runtimeVisibleMainFrameCount(resource->frameCount,art->shadowSpecified&&art->shadow);
    const auto resolved=sequence(art,count);result.sequenceSteps=resolved.sequenceSteps;
    if(result.sequenceSteps>0) {
        result.frameDelayTicks=delay(art,hz);
        result.durationTicks=result.sequenceSteps*result.frameDelayTicks;
        result.midpointTicks=(result.sequenceSteps/2+1)*result.frameDelayTicks;
    }
    return result;
}
}
static int checks=0,states=0,codecChecks=0,corpusCases=0,failures=0,exceptionCases=0;
static void check(bool value,const std::string& label) {++checks;if(!value){if(failures<8)std::cerr<<label<<'\n';++failures;}}
static auto values(const RuntimeArtAnimationSequence& s) {
    return std::tuple{s.startFrame,s.endFrame,s.loopStartFrame,s.loopEndFrame,s.loopCount,
        s.introSteps,s.loopSpan,s.outroSteps,s.sequenceSteps};
}
static auto values(const RuntimeSuperWeaponAnimationTiming& s) {
    return std::tuple{s.sequenceSteps,s.frameDelayTicks,s.durationTicks,s.midpointTicks};
}
static ArtType artFor(int i,int count) {
    ArtType art;
    art.hasAnimationStart=(i&1)!=0;art.hasAnimationEnd=(i&2)!=0;
    art.hasAnimationLoopStart=(i&4)!=0;art.hasAnimationLoopEnd=(i&8)!=0;
    const std::array<int,6> v{-3,0,1,3,count,count+9};
    art.animationStart=v[static_cast<std::size_t>((i/16)%6)];
    art.animationEnd=v[static_cast<std::size_t>((i/7)%6)];
    art.animationLoopStart=v[static_cast<std::size_t>((i/11)%6)];
    art.animationLoopEnd=v[static_cast<std::size_t>((i/13)%6)];
    const std::array<int,6> loops{-8,0,1,2,7,1001};art.animationLoopCount=loops[static_cast<std::size_t>((i/17)%6)];
    const std::array<int,7> rates{-7,0,1,15,31,900,1800};art.rate=rates[static_cast<std::size_t>((i/19)%7)];
    art.shadowSpecified=(i&32)!=0;art.shadow=(i&64)!=0;
    return art;
}
class AlwaysValid final : public RuntimeAnimationFrame0Validator {
public:
    int calls=0;
    bool validate(const std::vector<unsigned char>&,const std::string&,std::string&)override{++calls;return true;}
};
class Canonical final : public RuntimeAnimationFrame0Validator {
public:
    int calls=0;
    bool validate(const std::vector<unsigned char>& b,const std::string& s,std::string& error)override {
        ++calls;ShpIndexedFrame frame;return decodeShpIndexedFrame(b,s,0,ShpDecodePolicy::Runtime,frame,error);
    }
};
class Throwing final : public RuntimeAnimationFrame0Validator {
public:
    int calls=0;bool afterError=false;
    bool validate(const std::vector<unsigned char>&,const std::string&,std::string& error)override {
        ++calls;if(afterError)error="partial diagnostic";throw std::runtime_error("injected");
    }
};
static ShpIndexedFrame product(const shp_corpus::Frame& s) {
    ShpIndexedFrame d;d.offsetX=s.offsetX;d.offsetY=s.offsetY;d.globalWidth=s.globalWidth;d.globalHeight=s.globalHeight;
    d.width=s.width;d.height=s.height;d.frameIndex=s.frameIndex;d.format=s.format;d.dataOffset=s.dataOffset;
    d.authoredPause=s.authoredPause;d.pixels=s.pixels;d.sourcePath=s.sourcePath;return d;
}
int main() {
    const std::array<int,9> counts{0,1,2,3,7,16,33,128,256};
    const std::array<int,7> hzValues{-2,0,1,15,30,60,120};
    for(int i=0;i<4096;++i) {
        const int count=counts[static_cast<std::size_t>(i%9)];ArtType art=artFor(i,count);
        const auto old=original::sequence(&art,count),fresh=resolveRuntimeArtAnimationSequence(&art,count);
        check(values(old)==values(fresh),"literal sequence "+std::to_string(i));
        for(int hz:hzValues)check(original::delay(&art,hz)==concord::assets::animation::runtimeSuperWeaponAnimationFrameDelayTicks(&art,hz),"literal delay");
        RuntimeAuxiliaryShpBytesCacheEntry a,b;a.loaded=b.loaded=true;a.frameCount=b.frameCount=count;
        a.bytes=b.bytes={0,0,1,0,1,0,1,0};a.sourcePath=b.sourcePath="owned-bytes";
        original::FrameCache memo;AlwaysValid left,right;
        for(int round=0;round<3;++round) {
            const int hz=hzValues[static_cast<std::size_t>((i+round)%7)];
            const auto sequence=original::sequence(&art,original::runtimeVisibleMainFrameCount(count,art.shadowSpecified&&art.shadow));
            const auto duration=static_cast<std::int64_t>(sequence.sequenceSteps)*original::delay(&art,hz);
            if(duration>std::numeric_limits<int>::max())continue;
            const auto x=original::timing(&art,hz,&a,memo,left);
            const auto y=resolveRuntimeSuperWeaponAnimationTiming(&art,hz,&b,right);
            check(values(x)==values(y),"timing/memo case="+std::to_string(i));
            check(left.calls==right.calls,"warm validation call count");
            check(memo.loadAttempted==b.timingFrame0Attempted&&memo.loaded==b.timingFrame0Valid&&memo.errorMessage==b.timingFrame0Error,"full validation memo");
            art.rate=round==0?31:900;
        }
        ++states;
    }
    for(const auto& item:shp_corpus::cases()) {
        ++corpusCases;
        for(bool runtime:{false,true}) {
            const auto& expected=runtime?item.runtime:item.preview;auto frame=product(shp_corpus::sentinel());std::string error="previous diagnostic";
            const bool result=decodeShpIndexedFrame(item.bytes,item.name,item.frameIndex,runtime?ShpDecodePolicy::Runtime:ShpDecodePolicy::Preview,frame,error);
            check(result==expected.success&&frame==product(expected.frame)&&error==expected.error,"original complete codec corpus "+item.name);++codecChecks;
        }
        RuntimeAuxiliaryShpBytesCacheEntry a,b;a.bytes=b.bytes=item.bytes;a.loaded=b.loaded=!item.bytes.empty();
        a.frameCount=b.frameCount=item.bytes.size()>=8?static_cast<int>(shp_corpus::get16(item.bytes,6)):0;
        a.sourcePath=b.sourcePath=item.name;original::FrameCache memo;Canonical left,right;
        for(int j=0;j<16;++j) {
            ArtType art=artFor(j*7+31,a.frameCount);art.animationLoopCount=1;art.rate=900;
            const auto x=original::timing(&art,30,&a,memo,left);
            const auto y=resolveRuntimeSuperWeaponAnimationTiming(&art,30,&b,right);
            check(values(x)==values(y),"real Runtime frame0 scalar "+item.name);
            check(memo.loadAttempted==b.timingFrame0Attempted&&memo.loaded==b.timingFrame0Valid&&memo.errorMessage==b.timingFrame0Error,"canonical codec negative memo "+item.name);
            check(left.calls==right.calls&&right.calls<=1,"canonical once per source bytes "+item.name);
        }
    }
    for(bool after:{false,true}) {
        RuntimeAuxiliaryShpBytesCacheEntry a,b;a.loaded=b.loaded=true;a.frameCount=b.frameCount=4;
        original::FrameCache memo;ArtType art;Throwing left,right;left.afterError=right.afterError=after;
        bool x=false,y=false;
        try{original::timing(&art,30,&a,memo,left);}catch(const std::runtime_error&){x=true;}
        try{resolveRuntimeSuperWeaponAnimationTiming(&art,30,&b,right);}catch(const std::runtime_error&){y=true;}
        check(x&&y,"both propagate validator exception");
        check(memo.loadAttempted==b.timingFrame0Attempted&&memo.loaded==b.timingFrame0Valid&&memo.errorMessage==b.timingFrame0Error,"exact exception partial memo/error commit");
        const auto old=original::timing(&art,60,&a,memo,left),fresh=resolveRuntimeSuperWeaponAnimationTiming(&art,60,&b,right);
        check(values(old)==values(fresh)&&left.calls==1&&right.calls==1,"no retry after partial exception");++exceptionCases;
    }
    {RuntimeAuxiliaryShpBytesCacheEntry bytes;bytes.loaded=true;bytes.frameCount=5;AlwaysValid v;
        check(values(resolveRuntimeSuperWeaponAnimationTiming(nullptr,30,&bytes,v))==values(RuntimeSuperWeaponAnimationTiming{})&&v.calls==0,"missing Art no source decode");
        ArtType art;check(values(resolveRuntimeSuperWeaponAnimationTiming(&art,30,nullptr,v))==values(RuntimeSuperWeaponAnimationTiming{})&&v.calls==0,"missing bytes no decode");}
    std::cout<<"animation-metadata checks="<<checks<<" sequence-states="<<states<<" complete-codec-cases="<<corpusCases
        <<" codec-policy-checks="<<codecChecks<<" exception-cases="<<exceptionCases
        <<" literal-sequence-delay=exact runtime-frame0-and-negative-memo=exact failures="<<failures<<'\n';
    return failures?1:0;
}
