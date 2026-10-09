#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <string_view>

// Private benchmark diagnostics only. Every retained byte is owned; no actor,
// World, rules or cache pointer is retained by a record or the sink.
template <std::size_t Capacity>
struct RuntimeMassMovementRejectText {
    std::array<char, Capacity> bytes{};
    std::size_t originalBytes = 0;
    void assign(std::string_view value) noexcept {
        originalBytes = value.size();
        const auto count = std::min(value.size(), Capacity - 1);
        std::copy_n(value.data(), count, bytes.data());
        bytes[count] = '\0';
    }
    bool truncated() const noexcept { return originalBytes >= Capacity; }
};

struct RuntimeMassMovementRejectRecord {
    int tick = 0;
    int objectId = -1;
    bool objectExists = false;
    int objectKind = -1;
    bool alive = false;
    int health = 0;
    int currentX = 0, currentY = 0;
    int clickX = 0, clickY = 0;
    bool hasDesired = false;
    int desiredX = 0, desiredY = 0;
    bool hasCandidate = false;
    int candidateX = 0, candidateY = 0;
    bool visionEnabled = false;
    bool ownerHasObserver = false;
    // -1 means unavailable: no observer/vision, object or corresponding point.
    int currentMapped = -1, clickMapped = -1, desiredMapped = -1, candidateMapped = -1;
    int moveToShroud = -1, teleporter = -1, chronoLocked = -1;
    RuntimeMassMovementRejectText<96> type;
    RuntimeMassMovementRejectText<96> owner;
    RuntimeMassMovementRejectText<96> reason;
    RuntimeMassMovementRejectText<256> detail;
};

class RuntimeMassMovementRejectScope {
public:
    using Callback = void (*)(void*, const RuntimeMassMovementRejectRecord&) noexcept;
    RuntimeMassMovementRejectScope(const void* state, int tick, Callback callback, void* context) noexcept
        : previous_(active_), state_(state), tick_(tick), callback_(callback), context_(context),
          enabled_(previous_ == nullptr && state != nullptr && callback != nullptr) {
        // A nested scope masks the parent, including a foreign/disabled scope.
        active_ = this;
    }
    ~RuntimeMassMovementRejectScope() { active_ = previous_; }
    RuntimeMassMovementRejectScope(const RuntimeMassMovementRejectScope&) = delete;
    RuntimeMassMovementRejectScope& operator=(const RuntimeMassMovementRejectScope&) = delete;
    static bool matches(const void* state, int tick) noexcept {
        return active_ && active_->enabled_ && active_->state_ == state && active_->tick_ == tick;
    }
    static void capture(const void* state, int tick, const RuntimeMassMovementRejectRecord& record) noexcept {
        if (matches(state, tick)) active_->callback_(active_->context_, record);
    }
private:
    inline static thread_local RuntimeMassMovementRejectScope* active_ = nullptr;
    RuntimeMassMovementRejectScope* previous_ = nullptr;
    const void* state_ = nullptr;
    int tick_ = 0;
    Callback callback_ = nullptr;
    void* context_ = nullptr;
    bool enabled_ = false;
};

struct RuntimeMassMovementRejectSink {
    static constexpr std::size_t capacity = 32;
    std::array<RuntimeMassMovementRejectRecord, capacity> records{};
    std::size_t count = 0;
    std::size_t dropped = 0;
    static void receive(void* context, const RuntimeMassMovementRejectRecord& record) noexcept {
        auto& sink = *static_cast<RuntimeMassMovementRejectSink*>(context);
        if (sink.count < capacity) sink.records[sink.count++] = record;
        else ++sink.dropped;
    }
    void print(std::ostream& output) const {
        for (std::size_t index = 0; index < count; ++index) {
            const auto& r = records[index];
            output << "Runtime mass movement rejection: tick=" << r.tick
                << " id=" << r.objectId << " type=" << std::quoted(r.type.bytes.data())
                << " owner=" << std::quoted(r.owner.bytes.data())
                << " objectExists=" << r.objectExists << " kind=" << r.objectKind
                << " alive=" << r.alive << " health=" << r.health
                << " current=" << r.currentX << ',' << r.currentY
                << " click=" << r.clickX << ',' << r.clickY
                << " hasDesired=" << r.hasDesired << " desired=" << r.desiredX << ',' << r.desiredY
                << " hasCandidate=" << r.hasCandidate << " candidate=" << r.candidateX << ',' << r.candidateY
                << " reason=" << std::quoted(r.reason.bytes.data())
                << " detail=" << std::quoted(r.detail.bytes.data())
                << " vision=" << r.visionEnabled << " observer=" << r.ownerHasObserver
                << " currentMapped=" << r.currentMapped << " clickMapped=" << r.clickMapped
                << " desiredMapped=" << r.desiredMapped << " candidateMapped=" << r.candidateMapped
                << " moveToShroud=" << r.moveToShroud << " teleporter=" << r.teleporter
                << " chronoLocked=" << r.chronoLocked
                << " textTruncated=" << (r.type.truncated() || r.owner.truncated()
                    || r.reason.truncated() || r.detail.truncated()) << '\n';
        }
    }
};
