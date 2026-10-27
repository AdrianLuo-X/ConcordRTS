#include "runtime/presentation/SoftwareFrame.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace concord::presentation;
namespace {
int checks=0;
void need(bool v,const char* s){++checks;if(!v)throw std::runtime_error(s);}
struct Sink final : FramePresenter {
    explicit Sink(FramePresentStatistics& s):FramePresenter(s){}
    bool owner=true,works=true;int calls=0;SoftwareFrameView seen;std::vector<unsigned char> copy;
    bool onOwningThread()const noexcept override{return owner;}
    bool submitCompleteFrame(const SoftwareFrameView& f)noexcept override {
        ++calls;seen=f;if(!works)return false;
        try{copy.assign(f.pixels.bytes.begin(),f.pixels.bytes.end());}catch(...){return false;}return true;
    }
};
}
int main()try{
    FramePresentStatistics stats;Sink sink(stats);std::vector<unsigned char> bytes(4096);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>((i*37)%256);
    std::uint64_t sequence=0;
    for(int w=1;w<=16;++w)for(int h=1;h<=16;++h)for(std::size_t padding:{std::size_t{0},std::size_t{4}}){
        const auto stride=static_cast<std::size_t>(w)*4+padding;
        SoftwareFrameView f{{{bytes.data(),stride*static_cast<std::size_t>(h)},w,h,stride,17},++sequence,9,120,SoftwareFrameContent::Redrawn};
        need(f.valid(),"complete padded BGRA view valid");need(sink.present(f)==FramePresentResult::Presented,"synchronous complete frame submitted");
        need(sink.seen.sequence==sequence&&sink.seen.sceneVersion==9&&sink.seen.simulationTick==120&&sink.seen.pixels.generation==17,"metadata exact");
        need(sink.copy==std::vector<unsigned char>(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(stride*static_cast<std::size_t>(h))),"all original bytes including alpha/padding forwarded");
        need(sink.present(f)==FramePresentResult::Rejected,"duplicate sequence not submitted");
        f.sequence=++sequence;f.content=SoftwareFrameContent::ReusedScene;need(sink.present(f)==FramePresentResult::Presented,"cached scene counts successful submission");
    }
    need(stats.attempted==1024&&stats.successful==1024&&sink.calls==1024,"successful/cached counters");
    SoftwareFrameView good{{{bytes.data(),64},4,4,16,19},++sequence,10,121,SoftwareFrameContent::Redrawn};
    const auto calls=sink.calls;for(int i=0;i<7;++i){auto f=good;switch(i){case 0:f.pixels.width=0;break;case 1:f.pixels.height=-1;break;case 2:f.pixels.rowStride=15;break;case 3:f.pixels.bytes=f.pixels.bytes.first(63);break;case 4:f.sequence=0;break;case 5:f.sceneVersion=0;break;case 6:f.content=static_cast<SoftwareFrameContent>(90);break;}need(sink.present(f)==FramePresentResult::InvalidFrame,"invalid complete frame rejected before backend");}
    need(sink.calls==calls&&stats.attempted==1024,"invalid frame changes no submission counters");
    sink.owner=false;need(sink.present(good)==FramePresentResult::Rejected&&stats.attempted==1024,"wrong owner rejected before stats/backend");sink.owner=true;sink.works=false;
    need(sink.present(good)==FramePresentResult::Failed&&stats.attempted==1025&&stats.successful==1024,"failure is not successful submission");
    need(sink.present(good)==FramePresentResult::Rejected,"failed attempt identity not duplicated");good.sequence=++sequence;sink.works=true;
    need(sink.present(good)==FramePresentResult::Presented&&stats.successful==1025,"same scene retry uses fresh identity");
    need(stats.lastSuccessfulSequence==sequence&&stats.lastAttemptSequence==sequence,"last successful identity");
    bytes[0]^=255;need(sink.copy[0]!=bytes[0],"backend finished synchronous storage use before return");
    std::cout<<"Software frame contract passed checks="<<checks<<" layouts=512 successful=1025 rawBytes=exact cache=counts failure=no-count identity=single-use noNative=yes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}