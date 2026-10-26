#include "core/tasks/BoundedTaskExecutor.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

namespace concord::tasks {

OwnedBytes::OwnedBytes(std::size_t size)
    : data_(size == 0 ? nullptr : std::make_unique<std::byte[]>(size)), size_(size) {}
OwnedBytes::OwnedBytes(OwnedBytes&& other) noexcept
    : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)) {}
OwnedBytes& OwnedBytes::operator=(OwnedBytes&& other) noexcept {
    if (this != &other) {
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}
std::span<std::byte> OwnedBytes::bytes() noexcept { return {data_.get(), size_}; }
std::span<const std::byte> OwnedBytes::bytes() const noexcept { return {data_.get(), size_}; }
JobContext::JobContext(JobKey jobKey, std::span<const std::byte> jobInput,
    std::span<std::byte> jobOutput, std::span<std::byte> jobScratch,
    const std::atomic<bool>& cancelled) noexcept
    : key(jobKey), input(jobInput), output(jobOutput), scratch(jobScratch), cancelled_(cancelled) {}
bool JobContext::stopRequested() const noexcept { return cancelled_.load(std::memory_order_relaxed); }

struct BoundedTaskExecutor::Impl {
    enum class State { Free, Queued, Running, Complete };
    struct Slot {
        State state = State::Free;
        JobKey key{};
        OwnedBytes input, output, scratch;
        JobKernel kernel = nullptr;
        std::atomic<bool> cancelled{false};
        std::size_t reserved = 0;
        std::size_t used = 0;
        JobStatus status = JobStatus::Cancelled;
        std::array<char, 160> error{};
    };
    static thread_local const Impl* executing;
    const ExecutorConfig config;
    std::unique_ptr<Slot[]> slots;
    std::unique_ptr<std::size_t[]> queue;
    std::unique_ptr<std::thread[]> threads;
    mutable std::mutex mutex;
    std::mutex joinMutex;
    std::condition_variable changed;
    ExecutorStats counters{};
    std::size_t queueHead = 0;
    std::uint64_t lastId = 0;

    explicit Impl(ExecutorConfig options) : config(options) {
        if (config.workers > MaxWorkers || config.maxInFlightJobs == 0 ||
            config.maxInFlightJobs > MaxJobs || config.queueCapacity == 0 ||
            config.queueCapacity > config.maxInFlightJobs || config.maxInFlightBytes == 0 ||
            config.maxInFlightBytes > MaxPayloadBytes || config.initialGeneration == 0)
            throw std::invalid_argument("Invalid bounded task executor limits");
        slots = std::make_unique<Slot[]>(config.maxInFlightJobs);
        queue = std::make_unique<std::size_t[]>(config.queueCapacity);
        if (config.workers != 0) threads = std::make_unique<std::thread[]>(config.workers);
        counters.generation = config.initialGeneration;
        counters.fixedStorageBytes = sizeof(Impl) + sizeof(Slot) * config.maxInFlightJobs +
            sizeof(std::size_t) * config.queueCapacity + sizeof(std::thread) * config.workers;
        try {
            for (std::size_t i = 0; i < config.workers; ++i)
                threads[i] = std::thread([this] { worker(); });
        } catch (...) {
            requestStop();
            for (std::size_t i = 0; i < config.workers; ++i)
                if (threads[i].joinable()) threads[i].join();
            throw;
        }
    }

    Slot* find(JobKey key) {
        for (std::size_t i = 0; i < config.maxInFlightJobs; ++i)
            if (slots[i].state != State::Free && slots[i].key == key) return &slots[i];
        return nullptr;
    }
    void release(Slot& slot) {
        counters.reservedBytes -= slot.reserved;
        --counters.inFlightJobs;
        slot.input = OwnedBytes{};
        slot.output = OwnedBytes{};
        slot.scratch = OwnedBytes{};
        slot.kernel = nullptr;
        slot.reserved = 0;
        slot.state = State::Free;
    }
    void removeQueued(std::size_t index) {
        std::size_t retained = 0;
        for (std::size_t i = 0; i < counters.queued; ++i) {
            const auto item = queue[(queueHead + i) % config.queueCapacity];
            if (item != index) queue[(queueHead + retained++) % config.queueCapacity] = item;
        }
        counters.queued = retained;
    }
    void markRunning(Slot& slot) {
        slot.state = State::Running;
        ++counters.running;
        counters.peakRunning = std::max(counters.peakRunning, counters.running);
    }
    void run(std::size_t index) {
        auto& slot = slots[index]; // Running slots cannot be taken or reclaimed.
        const auto* previous = executing;
        executing = this;
        std::size_t used = 0;
        JobStatus status = JobStatus::Succeeded;
        std::array<char, 160> error{};
        try {
            const JobContext context(slot.key, slot.input.bytes(), slot.output.bytes(),
                slot.scratch.bytes(), slot.cancelled);
            used = slot.kernel(context);
            if (used > slot.output.size()) {
                status = JobStatus::Failed;
                constexpr char message[] = "Kernel output length exceeds reserved capacity";
                std::copy_n(message, sizeof(message), error.data());
            }
        } catch (const std::exception& exception) {
            status = JobStatus::Failed;
            const char* message = exception.what();
            if (message != nullptr)
                for (std::size_t i = 0; i + 1 < error.size() && message[i] != '\0'; ++i)
                    error[i] = message[i];
        } catch (...) {
            status = JobStatus::Failed;
            constexpr char message[] = "Unknown kernel exception";
            std::copy_n(message, sizeof(message), error.data());
        }
        executing = previous;
        {
            std::lock_guard lock(mutex);
            --counters.running;
            if (slot.key.generation != counters.generation) {
                release(slot);
            } else {
                slot.status = slot.cancelled.load(std::memory_order_relaxed) ? JobStatus::Cancelled : status;
                slot.used = slot.status == JobStatus::Succeeded ? used : 0;
                slot.error = error;
                slot.state = State::Complete;
            }
        }
        changed.notify_all();
    }
    void worker() {
        {
            std::lock_guard lock(mutex);
            ++counters.workerThreads;
        }
        changed.notify_all();
        for (;;) {
            std::size_t index = 0;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [this] { return counters.stopped || counters.queued != 0; });
                if (counters.stopped) break;
                index = queue[queueHead];
                queueHead = (queueHead + 1) % config.queueCapacity;
                --counters.queued;
                markRunning(slots[index]);
            }
            run(index);
        }
        {
            std::lock_guard lock(mutex);
            --counters.workerThreads;
        }
        changed.notify_all();
    }
    Submission submit(Task& task) {
        if (executing == this) return {SubmitCode::Reentrant};
        std::unique_lock lock(mutex);
        if (counters.stopped) return {SubmitCode::Stopped};
        if (task.generation != counters.generation) return {SubmitCode::StaleGeneration};
        if (task.kernel == nullptr) return {SubmitCode::InvalidTask};
        if (lastId == std::numeric_limits<std::uint64_t>::max()) return {SubmitCode::IdExhausted};
        if (counters.inFlightJobs == config.maxInFlightJobs) return {SubmitCode::InFlightFull};
        if (config.workers != 0 && counters.queued == config.queueCapacity) return {SubmitCode::QueueFull};
        std::size_t reservation = 0;
        for (const auto bytes : {task.input.size(), task.outputBytes, task.scratchBytes}) {
            if (bytes > config.maxInFlightBytes - reservation) return {SubmitCode::MemoryFull};
            reservation += bytes;
        }
        if (reservation > config.maxInFlightBytes - counters.reservedBytes) return {SubmitCode::MemoryFull};
        OwnedBytes output, scratch;
        try {
            output = OwnedBytes(task.outputBytes);
            scratch = OwnedBytes(task.scratchBytes);
        } catch (const std::bad_alloc&) {
            return {SubmitCode::AllocationFailed};
        }
        std::size_t index = 0;
        while (slots[index].state != State::Free) ++index; // Proven free by job count.
        auto& slot = slots[index];
        slot.input = std::move(task.input);
        slot.output = std::move(output);
        slot.scratch = std::move(scratch);
        slot.kernel = task.kernel;
        slot.key = {++lastId, counters.generation};
        const auto key = slot.key;
        slot.cancelled.store(false, std::memory_order_relaxed);
        slot.reserved = reservation;
        slot.used = 0;
        slot.error = {};
        ++counters.inFlightJobs;
        counters.reservedBytes += reservation;
        counters.peakJobs = std::max(counters.peakJobs, counters.inFlightJobs);
        counters.peakBytes = std::max(counters.peakBytes, counters.reservedBytes);
        if (config.workers == 0) {
            markRunning(slot);
            lock.unlock();
            run(index); // A real call on this submitting thread, with no worker.
        } else {
            slot.state = State::Queued;
            queue[(queueHead + counters.queued++) % config.queueCapacity] = index;
            lock.unlock();
            changed.notify_all();
        }
        return {SubmitCode::Accepted, key};
    }
    TakenResult take(JobKey key, bool wait) {
        if (wait && executing == this) return {TakeCode::WouldDeadlock};
        std::unique_lock lock(mutex);
        for (;;) {
            if (key.generation != counters.generation) return {TakeCode::StaleGeneration};
            auto* slot = find(key);
            if (slot == nullptr) return {TakeCode::UnknownJob};
            if (slot->state == State::Complete) {
                TaskResult result;
                result.key = slot->key;
                result.status = slot->status;
                result.outputSize = slot->used;
                result.error = slot->error;
                if (slot->status == JobStatus::Succeeded) result.output = std::move(slot->output);
                release(*slot);
                return {TakeCode::Taken, std::move(result)};
            }
            if (!wait) return {TakeCode::NotReady};
            changed.wait(lock);
        }
    }
    bool cancel(JobKey key) {
        std::lock_guard lock(mutex);
        if (key.generation != counters.generation) return false;
        auto* slot = find(key);
        if (slot == nullptr) return false;
        slot->cancelled.store(true, std::memory_order_relaxed);
        if (slot->state == State::Queued) {
            removeQueued(static_cast<std::size_t>(slot - slots.get()));
            slot->state = State::Complete;
        }
        if (slot->state == State::Complete) {
            slot->status = JobStatus::Cancelled;
            slot->used = 0;
        }
        changed.notify_all();
        return true;
    }
    bool advanceGeneration(std::uint64_t generation) {
        std::lock_guard lock(mutex);
        if (counters.stopped || generation <= counters.generation) return false;
        counters.generation = generation;
        counters.queued = 0;
        queueHead = 0;
        for (std::size_t i = 0; i < config.maxInFlightJobs; ++i) {
            auto& slot = slots[i];
            if (slot.state == State::Running) slot.cancelled.store(true, std::memory_order_relaxed);
            else if (slot.state != State::Free) release(slot);
        }
        changed.notify_all();
        return true;
    }
    void requestStop() {
        std::lock_guard lock(mutex);
        counters.stopped = true;
        counters.queued = 0;
        queueHead = 0;
        for (std::size_t i = 0; i < config.maxInFlightJobs; ++i) {
            auto& slot = slots[i];
            if (slot.state != State::Free) {
                slot.cancelled.store(true, std::memory_order_relaxed);
                if (slot.state != State::Running) {
                    slot.state = State::Complete;
                    slot.status = JobStatus::Cancelled;
                    slot.used = 0;
                }
            }
        }
        changed.notify_all();
    }
    bool stopAndJoin() {
        if (executing == this) return false;
        requestStop();
        std::lock_guard joining(joinMutex);
        for (std::size_t i = 0; i < config.workers; ++i)
            if (threads[i].joinable()) threads[i].join();
        std::unique_lock lock(mutex);
        changed.wait(lock, [this] { return counters.running == 0; }); // Includes inline callers.
        return true;
    }
};

thread_local const BoundedTaskExecutor::Impl* BoundedTaskExecutor::Impl::executing = nullptr;
BoundedTaskExecutor::BoundedTaskExecutor(ExecutorConfig config) : impl_(std::make_unique<Impl>(config)) {}
BoundedTaskExecutor::~BoundedTaskExecutor() { if (!impl_->stopAndJoin()) std::terminate(); }
Submission BoundedTaskExecutor::submit(Task& task) { return impl_->submit(task); }
TakenResult BoundedTaskExecutor::tryTake(JobKey key) { return impl_->take(key, false); }
TakenResult BoundedTaskExecutor::waitTake(JobKey key) { return impl_->take(key, true); }
bool BoundedTaskExecutor::cancel(JobKey key) { return impl_->cancel(key); }
bool BoundedTaskExecutor::advanceGeneration(std::uint64_t generation) { return impl_->advanceGeneration(generation); }
void BoundedTaskExecutor::requestStop() { impl_->requestStop(); }
bool BoundedTaskExecutor::stopAndJoin() { return impl_->stopAndJoin(); }
ExecutorStats BoundedTaskExecutor::stats() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->counters;
}

} // namespace concord::tasks
