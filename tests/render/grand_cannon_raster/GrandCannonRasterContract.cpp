#include "render/voxel/GrandCannonRasterTask.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
thread_local bool insideKernel = false;
std::atomic<int> kernelAllocations{0};
std::atomic<bool> entered{false}, releaseGate{false};
int checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
}
void* operator new(std::size_t size) {
    if (insideKernel) ++kernelAllocations;
    if (void* p = std::malloc(std::max(size, std::size_t{1}))) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace concord::tasks;
using namespace concord::grand_cannon;
namespace {
std::size_t observedRaster(const JobContext& context) {
    insideKernel = true;
    const auto size = raster(context);
    insideKernel = false;
    return size;
}
std::size_t gatedRaster(const JobContext& context) {
    entered = true;
    while (!releaseGate && !context.stopRequested()) std::this_thread::yield();
    return observedRaster(context);
}
Task quad(JobKernel kernel = observedRaster) {
    Input input{16,16,0,0,2,1,0.0001,0,1,2};
    Face face{};
    face.x = {2,10,10,2}; face.y = {2,2,10,10}; face.depth = {1,1,1,1};
    face.color = 23; face.material = (1U << 16) | 23U; face.normal = 8; face.vpl = 5;
    Shadow shadow{6,12,0,23};
    Task task;
    task.input = OwnedBytes(sizeof(input)+2*sizeof(face)+sizeof(shadow));
    auto* at = task.input.bytes().data();
    std::memcpy(at,&input,sizeof(input)); at += sizeof(input);
    std::memcpy(at,&face,sizeof(face)); at += sizeof(face);
    face.x={4,8,8,4};face.y={4,4,8,8};face.depth={2,2,2,2};
    face.color=31;face.material=(2U<<16)|31U;face.ownerRemap=1;
    std::memcpy(at,&face,sizeof(face)); at += sizeof(face);
    std::memcpy(at,&shadow,sizeof(shadow));
    task.outputBytes=outputBytes(256);task.scratchBytes=256*sizeof(double);task.kernel=kernel;
    return task;
}
JobKey admit(BoundedTaskExecutor& executor, Task& task) {
    auto result=executor.submit(task);
    require(result.code==SubmitCode::Accepted,"real raster admission");
    return result.key;
}
void equality() {
    std::vector<std::byte> expected;
    for (int requested : {0,1,2,4,-1}) {
        const auto workers=workerBudget(requested,12);
        BoundedTaskExecutor pool({workers,4,4,8U*1024U*1024U});
        for(int repeat=0;repeat<8;++repeat) {
            auto task=quad();auto key=admit(pool,task);
            auto result=pool.waitTake(key);
            require(result.code==TakeCode::Taken && result.result.status==JobStatus::Succeeded,"raster result succeeded");
            auto output=result.result.output.bytes();
            if(expected.empty()) expected.assign(output.begin(),output.end());
            require(std::equal(output.begin(),output.end(),expected.begin(),expected.end()),"worker raster equality");
            const auto* plane=reinterpret_cast<const unsigned char*>(output.data()+sizeof(Output)+256*4);
            require(plane[Pixels*256+5*16+5]==31,"near face wins depth test");
            require(plane[Pixels*256+3*16+3]==23,"far face remains outside near quad");
            require(plane[Remap*256+5*16+5]==1,"remap preserved");
            require(plane[Depth*256+5*16+5]==255,"near depth normalized");
            require(plane[ShadowPixels*256+12*16+6]==170,"shadow footprint preserved");
            require(pool.tryTake(key).code==TakeCode::UnknownJob,"output single claim");
        }
        require(pool.stats().reservedBytes==0,"claimed raster frees reservation");
    }
    require(kernelAllocations==0,"production raster performs no ordinary C++ heap allocation");
    require(workerBudget(-1,0)==1 && workerBudget(-1,1)==1 && workerBudget(-1,4)==2
        && workerBudget(-1,128)==4 && workerBudget(0,128)==0,"automatic and synchronous ceilings");
}
void malformed() {
    for(int variant=0;variant<6;++variant) {
        BoundedTaskExecutor pool({1,1,1,8U*1024U*1024U});
        auto task=quad();
        if(variant==0) { Input input{};std::memcpy(&input,task.input.bytes().data(),sizeof(input));
            input.width=std::numeric_limits<int>::max();std::memcpy(task.input.bytes().data(),&input,sizeof(input)); }
        if(variant==1) { Face face{};std::memcpy(&face,task.input.bytes().data()+sizeof(Input),sizeof(face));
            face.x[0]=std::numeric_limits<double>::quiet_NaN();std::memcpy(task.input.bytes().data()+sizeof(Input),&face,sizeof(face)); }
        if(variant==2) { Shadow shadow{std::numeric_limits<int>::max(),0,0,1};
            std::memcpy(task.input.bytes().data()+sizeof(Input)+2*sizeof(Face),&shadow,sizeof(shadow)); }
        if(variant==3) task.scratchBytes=1;
        if(variant==4) task.outputBytes=1;
        if(variant==5) { Input input{};std::memcpy(&input,task.input.bytes().data(),sizeof(input));
            input.faces=std::numeric_limits<std::uint32_t>::max();std::memcpy(task.input.bytes().data(),&input,sizeof(input)); }
        auto key=admit(pool,task);auto result=pool.waitTake(key);
        require(result.code==TakeCode::Taken && result.result.status==JobStatus::Failed
            && result.result.output.size()==0,"malformed raster fails without publishing pixels");
    }
}
void backpressure() {
    BoundedTaskExecutor pool({1,1,3,8U*1024U*1024U});
    auto first=quad(gatedRaster),second=quad(),third=quad();
    const auto a=admit(pool,first);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!entered && std::chrono::steady_clock::now()<until) std::this_thread::yield();
    require(entered,"gated raster entered");
    const auto b=admit(pool,second);
    require(pool.submit(third).code==SubmitCode::QueueFull && third.input.size()>0,"queue full preserves real raster input");
    require(pool.cancel(b),"queued real raster cancelled");
    releaseGate=true;
    require(pool.waitTake(a).result.status==JobStatus::Succeeded,"running raster completes");
    require(pool.waitTake(b).result.status==JobStatus::Cancelled,"queued raster never publishes");
    auto c=admit(pool,third);
    require(pool.advanceGeneration(2),"map generation advances");
    require(pool.waitTake(c).code==TakeCode::StaleGeneration,"old raster result rejected after map change");
    pool.stopAndJoin();
    require(pool.stats().reservedBytes==0,"close joins and releases real raster storage");
}
}
int main() {
    try {
        equality();malformed();backpressure();
        std::cout<<"Grand Cannon raster contract: passed checks="<<checks<<" heapInKernel="<<kernelAllocations<<"\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"Grand Cannon raster contract: "<<error.what()<<"\n";return 1; }
}
