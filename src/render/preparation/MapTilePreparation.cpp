#include "render/preparation/MapTilePreparation.h"
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace concord::render {
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
constexpr std::size_t maxWords=MapDrawList::maxBytes/4;
struct Packet {
    MapTileStamp stamp;
    std::uint32_t columns=0,firstRow=0,endRow=0,rectangles=0,words=0;
};
static_assert(std::is_trivially_copyable_v<Packet> && std::is_trivially_copyable_v<MapTileRect>);
// Three bounded linear passes, no allocation and no borrowed producer storage.
// Output segments are private per job. Every tile receives the original command
// ordinals in the original increasing order, including Save/Restore/BEHIND.
std::size_t tileKernel(const concord::tasks::JobContext& job) {
    Packet p;std::memcpy(&p,job.input.data(),sizeof(p));
    const std::size_t tiles=static_cast<std::size_t>(p.columns)*(p.endRow-p.firstRow),headers=tiles*2;
    if(job.input.size()!=sizeof(p)+static_cast<std::size_t>(p.rectangles)*sizeof(MapTileRect)
        ||job.output.size()!=static_cast<std::size_t>(p.words)*4||job.scratch.size()!=tiles*4)
        throw std::length_error("map tile packet lengths");
    auto* output=reinterpret_cast<std::uint32_t*>(job.output.data());
    auto* counts=reinterpret_cast<std::uint32_t*>(job.scratch.data());
    std::fill_n(counts,tiles,0U);
    const auto visit=[&](const auto& apply) {
        for(std::size_t i=0;i<p.rectangles;++i) {
            if(job.stopRequested())return false;
            MapTileRect r;std::memcpy(&r,job.input.data()+sizeof(p)+i*sizeof(r),sizeof(r));
            const int top=std::max(r.top,static_cast<int>(p.firstRow)),bottom=std::min(r.bottom,static_cast<int>(p.endRow));
            for(int y=top;y<bottom;++y)for(int x=r.left;x<r.right;++x)
                apply(static_cast<std::size_t>(y-static_cast<int>(p.firstRow))*p.columns+static_cast<std::size_t>(x),r.command);
        }
        return true;
    };
    if(!visit([&](std::size_t tile,std::uint32_t){++counts[tile];}))return 0;
    std::size_t offset=headers;
    for(std::size_t t=0;t<tiles;++t) {
        output[t*2]=static_cast<std::uint32_t>(offset);offset+=counts[t];
        if(offset>p.words)throw std::length_error("map tile output bound");
        output[t*2+1]=static_cast<std::uint32_t>(offset);counts[t]=output[t*2];
    }
    if(offset!=p.words)throw std::length_error("map tile output count");
    if(!visit([&](std::size_t tile,std::uint32_t command){output[counts[tile]++]=command;}))return 0;
    return job.output.size();
}
void validateSnapshot(const MapTileSnapshot& s) {
    const auto& p=s.stamp;
    if(!p.generation||!p.resourceRevision||!p.commandRevision||!p.sequence||!p.deviceGeneration
        ||p.width<=0||p.height<=0||p.width>MapDrawList::maxExtent||p.height>MapDrawList::maxExtent
        ||s.rectangles.size()>MapDrawList::maxCommands)throw std::invalid_argument("map tile snapshot");
    const int columns=(p.width+31)/32,rows=(p.height+31)/32;
    std::size_t words=static_cast<std::size_t>(columns)*rows*2,ordinal=0;bool first=true;
    for(const auto& r:s.rectangles) {
        if(r.left<0||r.top<0||r.right>columns||r.bottom>rows||r.left>=r.right||r.top>=r.bottom
            ||r.command>=MapDrawList::maxCommands||(!first&&r.command<=ordinal))throw std::invalid_argument("map tile rectangle");
        first=false;ordinal=r.command;
        const auto n=static_cast<std::size_t>(r.right-r.left)*(r.bottom-r.top);
        if(n>maxWords-words)throw std::length_error("map tile budget");words+=n;
    }
    if(words!=s.words||words>maxWords)throw std::length_error("map tile snapshot budget");
}
}
MapTileSnapshot snapshotMapTiles(const MapDrawFrame& f,std::uint64_t deviceGeneration) {
    // The UI owner has already validated the resource packet. Snapshot only the
    // numeric routing inputs; copying the whole resource slab is unnecessary.
    MapTileSnapshot s;
    s.stamp={f.generation,f.resourceRevision,f.commandRevision,f.sequence,f.tick,deviceGeneration,f.width,f.height};
    if(f.width<=0||f.height<=0||f.width>MapDrawList::maxExtent||f.height>MapDrawList::maxExtent
        ||f.list.commands().size()>MapDrawList::maxCommands)throw std::invalid_argument("map tile extent");
    s.words=static_cast<std::size_t>((f.width+31)/32)*((f.height+31)/32)*2;
    s.rectangles.reserve(f.list.commands().size());
    for(std::size_t i=0;i<f.list.commands().size();++i) {
        const auto& op=f.list.commands()[i];
        if(op.kind==MapDrawKind::NoOp||op.kind==MapDrawKind::Shift)continue;
        const int l=std::clamp(op.left,0,f.width),r=std::clamp(op.right,0,f.width);
        const int t=std::clamp(op.top,0,f.height),b=std::clamp(op.bottom,0,f.height);
        if(l>=r||t>=b)continue;
        MapTileRect rect{l/32,t/32,(r-1)/32+1,(b-1)/32+1,static_cast<std::uint32_t>(i)};
        const auto n=static_cast<std::size_t>(rect.right-rect.left)*(rect.bottom-rect.top);
        if(n>maxWords-s.words)throw std::length_error("map tile budget");s.words+=n;s.rectangles.push_back(rect);
    }
    validateSnapshot(s);return s;
}
std::vector<std::uint32_t> referenceMapTiles(const MapTileSnapshot& s) {
    validateSnapshot(s);
    const int columns=(s.stamp.width+31)/32,rows=(s.stamp.height+31)/32;
    const auto total=static_cast<std::size_t>(columns)*rows,headers=total*2;
    std::vector<std::vector<std::uint32_t>> bins(total);
    for(const auto& r:s.rectangles)for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
        bins[static_cast<std::size_t>(y*columns+x)].push_back(r.command);
    std::vector<std::uint32_t> indices(headers);indices.reserve(s.words);
    for(std::size_t i=0;i<total;++i) {
        indices[i*2]=static_cast<std::uint32_t>(indices.size());indices.insert(indices.end(),bins[i].begin(),bins[i].end());
        indices[i*2+1]=static_cast<std::uint32_t>(indices.size());
    }
    return indices;
}
struct MapTilePreparer::Impl {
    unsigned workers;
    std::unique_ptr<concord::tasks::BoundedTaskExecutor> executor;
    MapTileSnapshot snapshot;
    std::array<concord::tasks::JobKey,4> keys{};
    std::array<Packet,4> packets{};
    std::size_t jobs=0;
    std::uint64_t epoch=1;
    bool active=false;
    Clock::time_point started;
    MapTilePreparationMetrics metrics;
    explicit Impl(unsigned n):workers(n) {
        if(n!=0&&n!=1&&n!=2&&n!=4)throw std::invalid_argument("frame workers must be 0, 1, 2 or 4");
        if(n)executor=std::make_unique<concord::tasks::BoundedTaskExecutor>(concord::tasks::ExecutorConfig{n,4,4,128U*1024U*1024U,epoch});
    }
};
MapTilePreparer::MapTilePreparer(unsigned n):impl_(std::make_unique<Impl>(n)){}
MapTilePreparer::~MapTilePreparer(){if(impl_->executor)impl_->executor->stopAndJoin();}
void MapTilePreparer::start(MapTileSnapshot snapshot) {
    auto& p=*impl_;if(p.active)throw std::logic_error("one map preparation frame in flight");
    validateSnapshot(snapshot);p.started=Clock::now();p.snapshot=std::move(snapshot);p.metrics={};p.jobs=0;p.active=true;
    p.metrics.snapshotBytes=p.snapshot.rectangles.capacity()*sizeof(MapTileRect);
    if(!p.workers)return;
    try {
        const int columns=(p.snapshot.stamp.width+31)/32,rows=(p.snapshot.stamp.height+31)/32;
        p.jobs=std::min<std::size_t>(4,static_cast<std::size_t>(rows));
        for(std::size_t i=0;i<p.jobs;++i) {
            Packet packet{p.snapshot.stamp,static_cast<std::uint32_t>(columns),static_cast<std::uint32_t>(rows*i/p.jobs),
                static_cast<std::uint32_t>(rows*(i+1)/p.jobs),static_cast<std::uint32_t>(p.snapshot.rectangles.size()),0};
            const auto tiles=static_cast<std::size_t>(columns)*(packet.endRow-packet.firstRow);
            std::size_t words=tiles*2;
            for(const auto& r:p.snapshot.rectangles){const int top=std::max(r.top,static_cast<int>(packet.firstRow)),bottom=std::min(r.bottom,static_cast<int>(packet.endRow));
                if(top<bottom)words+=static_cast<std::size_t>(r.right-r.left)*(bottom-top);}
            packet.words=static_cast<std::uint32_t>(words);p.packets[i]=packet;
            concord::tasks::Task task;task.generation=p.epoch;task.input=concord::tasks::OwnedBytes(sizeof(packet)+p.snapshot.rectangles.size()*sizeof(MapTileRect));
            std::memcpy(task.input.bytes().data(),&packet,sizeof(packet));
            if(!p.snapshot.rectangles.empty())std::memcpy(task.input.bytes().data()+sizeof(packet),p.snapshot.rectangles.data(),p.snapshot.rectangles.size()*sizeof(MapTileRect));
            task.outputBytes=words*4;task.scratchBytes=tiles*4;task.kernel=tileKernel;
            const auto submission=p.executor->submit(task);
            if(submission.code!=concord::tasks::SubmitCode::Accepted)throw std::runtime_error("map tile admission failed");
            p.keys[i]=submission.key;
        }
        p.metrics.jobs=p.jobs;p.metrics.dispatchMs=ms(p.started);
    }catch(...){cancel();throw;}
}
PreparedMapTiles MapTilePreparer::finish(const MapTileStamp& current) {
    auto& p=*impl_;PreparedMapTiles result;if(!p.active)return result;
    result.stamp=p.snapshot.stamp;
    if(current!=p.snapshot.stamp){cancel();return result;}
    const auto wait=Clock::now();
    if(!p.workers)result.indices=referenceMapTiles(p.snapshot);
    else {
        std::array<concord::tasks::TakenResult,4> taken;
        for(std::size_t i=0;i<p.jobs;++i){taken[i]=p.executor->waitTake(p.keys[i]);
            if(taken[i].code!=concord::tasks::TakeCode::Taken||taken[i].result.status!=concord::tasks::JobStatus::Succeeded
                ||taken[i].result.outputSize!=static_cast<std::size_t>(p.packets[i].words)*4){cancel();return result;}}
        p.metrics.waitMs=ms(wait);const auto merge=Clock::now();
        result.indices.resize(p.snapshot.words);
        const auto columns=static_cast<std::size_t>((p.snapshot.stamp.width+31)/32),total=columns*((p.snapshot.stamp.height+31)/32);
        std::size_t offset=total*2;
        for(std::size_t i=0;i<p.jobs;++i) {
            const auto& packet=p.packets[i];const auto tiles=columns*(packet.endRow-packet.firstRow),headers=tiles*2;
            const auto* data=reinterpret_cast<const std::uint32_t*>(taken[i].result.output.bytes().data());
            for(std::size_t t=0;t<tiles;++t){const auto dest=(static_cast<std::size_t>(packet.firstRow)*columns+t)*2;
                result.indices[dest]=static_cast<std::uint32_t>(offset+data[t*2]-headers);
                result.indices[dest+1]=static_cast<std::uint32_t>(offset+data[t*2+1]-headers);}
            std::copy_n(data+headers,packet.words-headers,result.indices.data()+offset);offset+=packet.words-headers;
        }
        if(offset!=result.indices.size())throw std::logic_error("map tile stable merge size");
        p.metrics.mergeMs=ms(merge);
    }
    const auto stats=p.executor?p.executor->stats():concord::tasks::ExecutorStats{};
    p.metrics.peakRunning=stats.peakRunning;
    // Conservative simultaneous payload: snapshot, all packets/scratch/results,
    // merged output. Fixed metadata/OS thread stacks are reported separately.
    const auto tiles=static_cast<std::size_t>((current.width+31)/32)*((current.height+31)/32);
    p.metrics.ownedPeakBytes=p.metrics.snapshotBytes+(p.workers?
        p.jobs*(sizeof(Packet)+p.snapshot.rectangles.size()*sizeof(MapTileRect))+tiles*4+p.snapshot.words*8:
        p.snapshot.words*16+tiles*sizeof(std::vector<std::uint32_t>));
    p.metrics.totalMs=ms(p.started);result.metrics=p.metrics;result.accepted=true;p.active=false;p.snapshot={};return result;
}
void MapTilePreparer::cancel() {
    auto& p=*impl_;if(p.executor){
        if(p.epoch==UINT64_MAX)throw std::overflow_error("map tile epoch exhausted");
        p.executor->advanceGeneration(++p.epoch);
        // Drain running old jobs before allowing another frame. At most four
        // bounded jobs, no timeout/detach and no producer borrow to extend.
        p.executor->stopAndJoin();
        p.executor=std::make_unique<concord::tasks::BoundedTaskExecutor>(concord::tasks::ExecutorConfig{p.workers,4,4,128U*1024U*1024U,p.epoch});
    }
    p.active=false;p.snapshot={};p.jobs=0;
}
concord::tasks::ExecutorStats MapTilePreparer::stats() const {return impl_->executor?impl_->executor->stats():concord::tasks::ExecutorStats{};}
} // namespace concord::render
