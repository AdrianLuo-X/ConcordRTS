#pragma once
#include "core/tasks/BoundedTaskExecutor.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
struct RuntimeMapRenderContext;
struct RuntimeMapRenderCache;

// Renderer-owned preparation/installation bridge. Only the owner calls these
// methods. Resource/cache types are forward declared; they never enter Task.
class RuntimeGrandCannonPreparedFacing {
public:
    struct SourceInfo { std::size_t sourceBytes = 0, fallbackCharge = 0; };
    RuntimeGrandCannonPreparedFacing();
    ~RuntimeGrandCannonPreparedFacing();
    RuntimeGrandCannonPreparedFacing(const RuntimeGrandCannonPreparedFacing&) = delete;
    RuntimeGrandCannonPreparedFacing& operator=(const RuntimeGrandCannonPreparedFacing&) = delete;
    static SourceInfo inspect(const RuntimeMapRenderContext&, const RuntimeMapRenderCache&);
    void prepare(const RuntimeMapRenderContext&, const RuntimeMapRenderCache&, int bucket, std::uint64_t generation);
    std::size_t count() const noexcept;
    std::size_t cacheCharge() const noexcept;
    concord::tasks::Task& task(std::size_t index);
    void accept(std::size_t index, std::span<const std::byte> output);
    void install(RuntimeMapRenderCache&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
