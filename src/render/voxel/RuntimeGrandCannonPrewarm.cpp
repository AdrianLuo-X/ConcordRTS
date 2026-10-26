#include "render/voxel/RuntimeGrandCannonPrewarm.h"
#include "render/voxel/RuntimeGrandCannonPreparedFacing.h"
#include "render/voxel/RuntimeGrandCannonPrewarmTestHooks.h"
#include "render/voxel/GrandCannonRasterTask.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
std::size_t grandCannonInjectedWorkerFailure(const concord::tasks::JobContext&) {
    throw "S35 injected worker failure";
}
}
struct RuntimeGrandCannonPrewarmJob::Impl {
    using Clock = std::chrono::steady_clock;
    struct Facing {
        std::unique_ptr<RuntimeGrandCannonPreparedFacing> cache;
        std::array<concord::tasks::JobKey, 2> keys{};
        std::array<bool, 2> taken{};
        int bucket = 0;
        bool installed = false;
    };
    const std::thread::id owner = std::this_thread::get_id();
    const std::uint64_t generation;
    std::array<Facing, 31> facings;
    Metrics measured;
    std::string failure;
    bool prepared = false, stopped = false;
    Clock::time_point started = Clock::now();
    // Explicitly stopped/joined before any owner storage can be destroyed.
    concord::tasks::BoundedTaskExecutor executor;

    Impl(int requested, std::uint64_t value)
        : generation(value), executor({concord::grand_cannon::workerBudget(requested,
              std::thread::hardware_concurrency()), 4, 4, ExecutorBytes, value}) {
        // stats().workerThreads counts threads after entry, which may race construction.
        measured.workers = concord::grand_cannon::workerBudget(requested, std::thread::hardware_concurrency());
        if (concord::grand_cannon::testing::failure == 5) throw std::bad_alloc{};
    }
    ~Impl() { executor.stopAndJoin(); }
    bool isOwner() const noexcept { return owner == std::this_thread::get_id(); }
    void fail(const char* reason) {
        if (failure.empty()) failure.assign(reason, std::min<std::size_t>(std::strlen(reason), 256));
        stopped = true;
        executor.stopAndJoin();
    }
    bool collect(Facing& facing, std::size_t index, bool wait) {
        using namespace concord::tasks;
        if (facing.taken[index] || facing.keys[index].id == 0) return true;
        auto before = Clock::now();
        auto taken = wait ? executor.waitTake(facing.keys[index]) : executor.tryTake(facing.keys[index]);
        if (wait) measured.ownerWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        if (taken.code == TakeCode::NotReady) return false;
        if (taken.code != TakeCode::Taken || taken.result.key.generation != generation
            || taken.result.status != JobStatus::Succeeded) {
            fail("Grand Cannon raster was cancelled, stale or failed"); return false;
        }
        if (taken.result.outputSize != facing.cache->task(index).outputBytes) {
            fail("Grand Cannon raster returned an invalid output length"); return false;
        }
        facing.cache->accept(index, taken.result.output.bytes());
        facing.taken[index] = true;
        return true;
    }
    bool drainOne() {
        for (auto& facing : facings) if (facing.cache) for (std::size_t i = 0; i < facing.cache->count(); ++i)
            if (facing.keys[i].id != 0 && !facing.taken[i]) return collect(facing, i, true);
        return false;
    }
};


RuntimeGrandCannonPrewarmJob::RuntimeGrandCannonPrewarmJob(int workers, std::uint64_t generation)
    : impl_(std::make_unique<Impl>(workers, generation)) {}
RuntimeGrandCannonPrewarmJob::~RuntimeGrandCannonPrewarmJob() = default;

bool RuntimeGrandCannonPrewarmJob::prepare(const RuntimeMapRenderContext& context,
    const RuntimeMapRenderCache& source, std::string& errorMessage) {
    auto& job = *impl_;
    if (!job.isOwner() || job.prepared || job.stopped) {
        errorMessage = "Grand Cannon prepare requires its owner and a fresh job"; return false;
    }
    if (!concord::grand_cannon::testing::verifying && std::getenv("CONCORD_GRAND_CANNON_PREWARM_VERIFY")
        && !verifyRuntimeGrandCannonPrewarm(context, errorMessage)) {
        job.fail(errorMessage.c_str()); return false;
    }
    job.prepared = true;
    try {
        const auto info = RuntimeGrandCannonPreparedFacing::inspect(context, source);
        job.measured.sourceBytes = info.sourceBytes;
        job.measured.cacheBytes = info.fallbackCharge;
        std::array<bool, 32> added{};
        added[30] = true;
        std::size_t next = 0, dispatched = 0;
        std::size_t peakStagedInputBytes = 0;
        const auto dispatchPrepared = [&] {
            std::size_t stagedBytes = 0;
            for (auto index = dispatched; index < next; ++index)
                for (std::size_t part = 0; part < job.facings[index].cache->count(); ++part)
                    stagedBytes += job.facings[index].cache->task(part).input.size();
            if (stagedBytes > 128U * 1024U * 1024U)
                throw std::length_error("Grand Cannon producer staging exceeded 128 MiB");
            peakStagedInputBytes = std::max(peakStagedInputBytes, stagedBytes);
            while (dispatched < next) {
                auto& facing = job.facings[dispatched++];
                for (std::size_t i = 0; i < facing.cache->count(); ++i) {
                    auto& task = facing.cache->task(i);
                    if (concord::grand_cannon::testing::failure == 2) task.kernel = grandCannonInjectedWorkerFailure;
                    if (concord::grand_cannon::testing::failure == 1 && job.measured.submitted == 2) throw std::bad_alloc{};
                    job.measured.largestJobBytes = std::max(job.measured.largestJobBytes, task.input.size() + task.outputBytes + task.scratchBytes);
                    for (;;) {
                        const auto submission = job.executor.submit(task);
                        using concord::tasks::SubmitCode;
                        if (submission.code == SubmitCode::Accepted) {
                            facing.keys[i] = submission.key;
                            ++job.measured.submitted;
                            break;
                        }
                        if ((submission.code != SubmitCode::QueueFull && submission.code != SubmitCode::InFlightFull
                                && submission.code != SubmitCode::MemoryFull) || !job.drainOne())
                            throw std::runtime_error("Grand Cannon bounded executor admission failed");
                    }
                }
            }
        };
        for (int distance = 1; next < job.facings.size(); ++distance) {
            for (int bucket : {(30 + distance) % 32, (30 - distance + 32) % 32}) {
                if (added[static_cast<std::size_t>(bucket)]) continue;
                added[static_cast<std::size_t>(bucket)] = true;
                auto& facing = job.facings[next++];
                facing.bucket = bucket;
                facing.cache = std::make_unique<RuntimeGrandCannonPreparedFacing>();
                facing.cache->prepare(context, source, bucket, job.generation);
                const auto charge = facing.cache->cacheCharge();
                if (charge > CacheBytes - job.measured.cacheBytes)
                    throw std::length_error("Grand Cannon atlas exceeded 128 MiB");
                job.measured.cacheBytes += charge;
                job.measured.synchronousFallbacks += 2 - facing.cache->count();
                if (next % 4 == 0 || next == job.facings.size()) dispatchPrepared();
            }
        }
        if (concord::grand_cannon::testing::failure == 3) job.executor.advanceGeneration(job.generation + 1);
        if (concord::grand_cannon::testing::failure == 4) {
            for (auto& facing : job.facings) for (std::size_t i = 0; i < facing.cache->count(); ++i)
                if (facing.keys[i].id && !facing.taken[i]) job.executor.cancel(facing.keys[i]);
        }
        if (concord::grand_cannon::testing::verifying)
            std::clog << "S35_STAGING peakProducerInputBytes=" << peakStagedInputBytes << "\n";
        job.measured.prepareMs = std::chrono::duration<double, std::milli>(Impl::Clock::now() - job.started).count();
        return true;
    } catch (const std::exception& error) {
        job.fail(error.what());
    } catch (...) {
        job.fail("Grand Cannon preparation failed");
    }
    errorMessage = job.failure;
    return false;
}

void RuntimeGrandCannonPrewarmJob::merge(RuntimeMapRenderCache& target, std::uint64_t generation) {
    auto& job = *impl_;
    if (!job.isOwner() || job.stopped || generation != job.generation) return;
    try {
        for (auto& facing : job.facings) {
            if (!facing.cache || facing.installed) continue;
            bool complete = true;
            for (std::size_t i = 0; i < facing.cache->count(); ++i)
                if (!job.collect(facing, i, false)) complete = false;
            if (job.stopped) return;
            if (!complete) continue;
            facing.cache->install(target);
            facing.installed = true;
            job.measured.installed += 2;
            facing.cache.reset();
        }
        if (done() && job.measured.elapsedMs == 0) {
            job.measured.elapsedMs = std::chrono::duration<double, std::milli>(Impl::Clock::now() - job.started).count();
            job.executor.stopAndJoin();
        }
    } catch (const std::exception& error) { job.fail(error.what()); }
}
void RuntimeGrandCannonPrewarmJob::finish(RuntimeMapRenderCache& target, std::uint64_t generation) {
    auto& job = *impl_;
    if (!job.isOwner() || job.stopped || generation != job.generation) return;
    try {
        while (job.drainOne()) {}
        merge(target, generation);
    } catch (const std::exception& error) { job.fail(error.what()); }
}
void RuntimeGrandCannonPrewarmJob::stop() noexcept {
    impl_->stopped = true;
    impl_->executor.stopAndJoin();
}
bool RuntimeGrandCannonPrewarmJob::done() const noexcept {
    if (impl_->stopped) return true;
    return impl_->prepared && impl_->measured.installed == 62;
}
const std::string& RuntimeGrandCannonPrewarmJob::error() const noexcept { return impl_->failure; }
RuntimeGrandCannonPrewarmJob::Metrics RuntimeGrandCannonPrewarmJob::metrics() const noexcept {
    auto value = impl_->measured;
    const auto stats = impl_->executor.stats();
    value.peakJobs = stats.peakJobs; value.peakRunning = stats.peakRunning;
    value.peakExecutorBytes = stats.peakBytes;
    return value;
}

