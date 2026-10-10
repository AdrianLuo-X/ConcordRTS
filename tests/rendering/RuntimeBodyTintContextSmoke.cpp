#include "render/core/RuntimeMapRenderer.h"
#include <optional>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <cstring>
#include "render/lighting/RuntimeBodyTintContext.inl"
RuntimeMapRenderContext::RuntimeMapRenderContext() = default;
RuntimeMapRenderContext::~RuntimeMapRenderContext() = default;
static std::size_t allocations=0;
void* operator new(std::size_t n) { ++allocations; if(auto* p=std::malloc(n))return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
__declspec(noinline) std::size_t consume(const RuntimeMapRenderContext& c) {
    return c.tileRootPath.size()+c.palettePath.size()+c.assetRootPath.size()
        +c.structureRootPath.size()+c.structurePalettePath.size()+c.visionObserverOwner.size()
        +static_cast<unsigned char>(c.assetRootPath.back())+c.animationTick+c.chaosBodyTint
        +c.surfaceWidth+c.airstrikeBodyColorAdd+c.visualDetail;
}
static RuntimeMapRenderContext sample(int length) {
    RuntimeMapRenderContext c;
    c.tileRootPath="X:/ExampleRTS/local_data/resources/"+std::string(length,'t');
    c.palettePath="X:/ExampleRTS/local_data/palettes/"+std::string(length,'p');
    c.assetRootPath="X:/ExampleRTS/local_data/resources/mix_exports/"+std::string(length,'a');
    c.structureRootPath="X:/ExampleRTS/local_data/structures/"+std::string(length,'s');
    c.structurePalettePath="X:/ExampleRTS/local_data/palettes/"+std::string(length,'b');
    c.visionObserverOwner="Commander"; c.animationTick=444; c.fixedTickHz=162;
    c.surfaceWidth=1440; c.surfaceHeight=900; c.stableRenderConfiguration=true;
    c.airstrikeBodyColorAdd=591; c.visualDetail=1; c.seeHiddenObjects=false;
    c.rules=reinterpret_cast<const RulesDatabase*>(0x12340);
    c.art=reinterpret_cast<const ArtDatabase*>(0x56780);
    c.surfacePixels=reinterpret_cast<std::uint32_t*>(0x9abc0);
    return c;
}
static bool equal(const RuntimeMapRenderContext& a,const RuntimeMapRenderContext& b) {
    return a.theater==b.theater
        && a.art==b.art
        && a.rules==b.rules
        && a.mapFacts==b.mapFacts
        && a.projectileActors==b.projectileActors
        && a.impactActors==b.impactActors
        && a.wrecks==b.wrecks
        && a.buildingCollapses==b.buildingCollapses
        && a.particleSnapshots==b.particleSnapshots
        && a.particleBeamSnapshots==b.particleBeamSnapshots
        && a.localLightSnapshots==b.localLightSnapshots
        && a.specialMechanismSnapshots==b.specialMechanismSnapshots
        && a.selectedObjectIds==b.selectedObjectIds
        && a.playerCheerUntilTick==b.playerCheerUntilTick
        && a.gateSnapshots==b.gateSnapshots
        && a.uiResources==b.uiResources
        && a.ownerRemapColors==b.ownerRemapColors
        && a.neutralRemapOwners==b.neutralRemapOwners
        && a.vision==b.vision
        && a.lightingTint==b.lightingTint
        && a.lightingProfile==b.lightingProfile
        && a.visionObserverOwner==b.visionObserverOwner
        && a.tileRootPath==b.tileRootPath
        && a.palettePath==b.palettePath
        && a.assetRootPath==b.assetRootPath
        && a.structureRootPath==b.structureRootPath
        && a.structurePalettePath==b.structurePalettePath
        && a.animationTick==b.animationTick
        && a.fixedTickHz==b.fixedTickHz
        && a.visualDetail==b.visualDetail
        && a.seeHiddenObjects==b.seeHiddenObjects
        && a.useTileTextures==b.useTileTextures
        && a.detailStatsEnabled==b.detailStatsEnabled
        && a.stableRenderConfiguration==b.stableRenderConfiguration
        && a.effectOnlyProbe==b.effectOnlyProbe
        && a.chaosBodyTint==b.chaosBodyTint
        && a.airstrikeBodyColorAdd==b.airstrikeBodyColorAdd
        && a.shadowsEnabled==b.shadowsEnabled
        && a.surfacePixels==b.surfacePixels
        && a.surfaceWidth==b.surfaceWidth
        && a.surfaceHeight==b.surfaceHeight;
}
int main(int argc, char**) {
    std::size_t hash=0; unsigned cases=0;
    for(int length : {0,40,120,280}) for(bool originalTint:{false,true})
        for(bool targetTint:{false,true}) for(bool force:{false,true}) {
            auto original=sample(length); original.chaosBodyTint=originalTint;
            auto expected=original; expected.chaosBodyTint=targetTint;
            const auto before=allocations;
            RuntimeBodyTintContext chosen(original,targetTint,force);
            const auto count=allocations-before;
            if(!equal(chosen.get(),expected)||original.chaosBodyTint!=originalTint)
                throw std::runtime_error("context fields differ");
            const bool borrowed=!force&&originalTint==targetTint;
            if((&chosen.get()==&original)!=borrowed || (borrowed&&count!=0))
                throw std::runtime_error("borrow/copy contract differs");
            RuntimeBodyTintContext nested(chosen.get(),!targetTint,false);
            if(nested.get().chaosBodyTint==targetTint || chosen.get().chaosBodyTint!=targetTint)
                throw std::runtime_error("nested override leaked");
            hash+=consume(chosen.get()); ++cases;
        }
    std::cout<<"context contract cases="<<cases<<" complete fields + nested + allocation/identity passed hash="<<hash<<'\n';
    if (argc <= 1) return 0;
    for(int count:{256,1024}) for(int length:{40,120,280}) for(int mixed:{0,20}) {
        auto original=sample(length);
        for(int pass=0;pass<4;++pass) {
            const bool optimized=(pass==1||pass==2);
            const auto first=allocations;
            const auto start=std::chrono::steady_clock::now();
            std::size_t sum=0;
            for(int frame=0;frame<400;++frame) for(int object=0;object<count;++object) {
                const bool tint=mixed!=0&&object%mixed==0;
                if(optimized) {RuntimeBodyTintContext selected(original,tint,false); sum+=consume(selected.get());}
                else {RuntimeMapRenderContext selected=original; selected.chaosBodyTint=tint; sum+=consume(selected);}
            }
            const auto end=std::chrono::steady_clock::now();
            const auto allocs=allocations-first;
            const double elapsed=std::chrono::duration<double,std::milli>(end-start).count();
            std::cout<<"units="<<count<<" pathSuffix="<<length<<" tintEvery="<<mixed<<" mode="<<(optimized?"borrow":"copy")
                <<" frames=400 ms="<<elapsed<<" allocations="<<allocs<<" hash="<<sum<<'\n';
        }
    }
}