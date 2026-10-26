#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
struct RuntimeMapRenderContext;
struct RuntimeMapRenderCache;

// Construct, prepare, collect and install only on the creating frame owner.
// No context, rules, art, theater, resource or live-cache pointer is retained.
class RuntimeGrandCannonPrewarmJob {
public:
    struct Metrics {
        std::size_t workers = 0, submitted = 0, installed = 0, synchronousFallbacks = 0;
        std::size_t peakJobs = 0, peakRunning = 0, peakExecutorBytes = 0;
        std::size_t largestJobBytes = 0, cacheBytes = 0, sourceBytes = 0;
        double prepareMs = 0, ownerWaitMs = 0, elapsedMs = 0;
    };
    RuntimeGrandCannonPrewarmJob(int requestedWorkers, std::uint64_t generation);
    ~RuntimeGrandCannonPrewarmJob();
    RuntimeGrandCannonPrewarmJob(const RuntimeGrandCannonPrewarmJob&) = delete;
    RuntimeGrandCannonPrewarmJob& operator=(const RuntimeGrandCannonPrewarmJob&) = delete;
    bool prepare(const RuntimeMapRenderContext&, const RuntimeMapRenderCache&, std::string&);
    void merge(RuntimeMapRenderCache&, std::uint64_t generation);
    void finish(RuntimeMapRenderCache&, std::uint64_t generation);
    void stop() noexcept;
    bool done() const noexcept;
    const std::string& error() const noexcept;
    Metrics metrics() const noexcept;
    static constexpr std::size_t ExecutorBytes = 64U * 1024U * 1024U;
    static constexpr std::size_t CacheBytes = 128U * 1024U * 1024U;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
