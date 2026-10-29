#include "render/preparation/MapTilePreparation.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace concord::render;
namespace {
std::size_t checks=0;
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"invalid input accepted");}
// Original pre-S43 algorithm directly from pixel bounds: deliberately does not
// use the new snapshot transform or kernel when constructing the oracle.
std::vector<std::uint32_t> legacy(const MapDrawFrame& f) {
    const int columns=(f.width+31)/32,rows=(f.height+31)/32,total=columns*rows;
    std::vector<std::vector<std::uint32_t>> bins(static_cast<std::size_t>(total));
    for(std::size_t i=0;i<f.list.commands().size();++i){const auto& op=f.list.commands()[i];
        if(op.kind==MapDrawKind::NoOp||op.kind==MapDrawKind::Shift)continue;
        const int l=std::clamp(op.left,0,f.width),r=std::clamp(op.right,0,f.width),t=std::clamp(op.top,0,f.height),b=std::clamp(op.bottom,0,f.height);
        if(l>=r||t>=b)continue;
        for(int y=t/32;y<=(b-1)/32;++y)for(int x=l/32;x<=(r-1)/32;++x)bins[static_cast<std::size_t>(y*columns+x)].push_back(static_cast<std::uint32_t>(i));
    }
    std::vector<std::uint32_t> result(static_cast<std::size_t>(total)*2);
    for(int i=0;i<total;++i){result[static_cast<std::size_t>(i)*2]=static_cast<std::uint32_t>(result.size());result.insert(result.end(),bins[i].begin(),bins[i].end());result[static_cast<std::size_t>(i)*2+1]=static_cast<std::uint32_t>(result.size());}
    return result;
}
void suite() {
    std::uint32_t random=74193;
    for(const unsigned workers:{0U,1U,2U,4U}) {
        MapTilePreparer preparer(workers);
        for(int scene=0;scene<48;++scene) {
            MapDrawList list;const int w=scene==0?1:scene==1?8192:65+scene*19,h=scene<2?33:31+scene*13;
            for(unsigned i=0;i<512;++i){random=random*1664525+1013904223;MapDrawCommand c;
                c.left=static_cast<int>(random%static_cast<unsigned>(w+128))-64;random=random*1664525+1013904223;c.top=static_cast<int>(random%static_cast<unsigned>(h+128))-64;
                c.right=c.left+1+static_cast<int>((random>>4)%167);c.bottom=c.top+1+static_cast<int>((random>>13)%129);
                c.kind=static_cast<MapDrawKind>(i%21);list.push(c);}
            MapDrawFrame frame(list,static_cast<std::uint64_t>(scene+1),w,h,789);auto snapshot=snapshotMapTiles(frame,3);const auto stamp=snapshot.stamp;const auto expected=legacy(frame);
            check(referenceMapTiles(snapshot)==expected,"snapshot/reference differs from original pixel algorithm");
            preparer.start(std::move(snapshot));
            // Destroy producer data before any result is consumed; task bytes
            // must retain every routing value without retaining list/spans.
            list.reset();
            auto result=preparer.finish(stamp);check(result.accepted,"owned input not accepted");check(result.stamp==stamp,"stamp changed");
            check(result.indices==expected,"parallel tile order/complete list differs");
            const auto stats=preparer.stats();check(stats.inFlightJobs==0&&stats.reservedBytes==0,"unclaimed payload leak");check(stats.peakJobs<=4,"more than four jobs");
            check(result.metrics.ownedPeakBytes<256U*1024U*1024U,"CPU staging bound");
        }
        MapDrawList big;for(int i=0;i<150;++i)big.fill({0,0,4096,4096},static_cast<unsigned>(i));
        MapDrawFrame frame(big,100,4096,4096,9);const auto original=snapshotMapTiles(frame,7);
        for(unsigned field=0;field<8;++field){auto altered=original.stamp;
            switch(field){case 0:++altered.generation;break;case 1:++altered.resourceRevision;break;case 2:++altered.commandRevision;break;case 3:++altered.sequence;break;case 4:++altered.tick;break;case 5:++altered.deviceGeneration;break;case 6:--altered.width;break;default:--altered.height;break;}
            preparer.start(original);const auto discarded=preparer.finish(altered);check(!discarded.accepted&&discarded.indices.empty(),"old frame/device/source published");
            check(preparer.stats().inFlightJobs==0,"cancel did not join old input");
        }
        preparer.start(original);rejects([&]{preparer.start(original);});preparer.cancel();
        check(!preparer.finish(original.stamp).accepted,"cancelled result published");
        preparer.start(original);std::this_thread::sleep_for(std::chrono::milliseconds(8));
        check(preparer.finish(original.stamp).accepted,"late complete presentation rejected");
        {MapTilePreparer exiting(workers);exiting.start(original);} // Active exit must join.
        auto invalid=original;invalid.words=0;rejects([&]{preparer.start(invalid);});
        invalid=original;invalid.rectangles[0].left=-1;rejects([&]{preparer.start(invalid);});
        invalid=original;invalid.stamp.deviceGeneration=0;rejects([&]{preparer.start(invalid);});
        for(int n=0;n<12;++n){MapTilePreparer lifetime(workers);lifetime.start(original);if(n%2)lifetime.cancel();}
    }
    rejects([]{MapTilePreparer invalid(3);});
    MapDrawList overflowing;for(int i=0;i<257;++i)overflowing.fill({0,0,8192,8192},1);
    rejects([&]{snapshotMapTiles(MapDrawFrame(overflowing,1,8192,8192),1);});
    MapDrawList empty;auto s=snapshotMapTiles(MapDrawFrame(empty,1,97,61),1);
    for(unsigned n:{0U,1U,2U,4U}){MapTilePreparer p(n);p.start(s);auto r=p.finish(s.stamp);check(r.accepted&&r.indices==referenceMapTiles(s),"empty tiles differ");}
}
void benchmark(const char* file) {
    std::ifstream input(file,std::ios::binary);std::array<std::uint32_t,3> header{};input.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    check(input.good()&&header[0]<=8192&&header[1]<=8192&&header[2]<=32768,"invalid captured frame");MapDrawList list;
    for(std::uint32_t i=0;i<header[2];++i){MapDrawCommand c;input.read(reinterpret_cast<char*>(&c),sizeof(c));check(input.good(),"truncated capture");list.push(c);}
    MapDrawFrame frame(list,1,static_cast<int>(header[0]),static_cast<int>(header[1]),1);const auto wanted=legacy(frame);
    // Same captured production input, five repetitions of warmup+120 samples.
    // Each row includes snapshot/dispatch/wait/merge and excludes verification.
    std::cout<<"workers,round,sample,snapshotMs,dispatchMs,waitMs,mergeMs,totalMs,ownedPeakBytes,peakRunning\n";
    for(unsigned workers:{0U,1U,2U,4U})for(unsigned round=0;round<5;++round){MapTilePreparer p(workers);
        for(int i=-10;i<120;++i){const auto start=std::chrono::steady_clock::now();auto s=snapshotMapTiles(frame,1);const auto stamp=s.stamp;
            const auto snap=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();p.start(std::move(s));auto r=p.finish(stamp);
            const auto total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            check(r.accepted&&r.indices==wanted,"captured frame differs");if(i>=0)std::cout<<workers<<','<<round<<','<<i<<','<<snap<<','<<r.metrics.dispatchMs<<','<<r.metrics.waitMs<<','<<r.metrics.mergeMs<<','<<total<<','<<r.metrics.ownedPeakBytes<<','<<r.metrics.peakRunning<<'\n';
        }}
}
}
int main(int argc,char** argv){try{if(argc==2)benchmark(argv[1]);else{suite();std::cout<<"Map tile preparation passed checks="<<checks<<" modes=0,1,2,4 boundedFrames=1 boundedJobs=4\n";}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
