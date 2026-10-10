#pragma once

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

inline thread_local int runtimePathLayoutViewReferenceOverride = -1;

inline bool runtimePathLayoutViewReference() noexcept {
    if (runtimePathLayoutViewReferenceOverride >= 0)
        return runtimePathLayoutViewReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_QUEUE_LAYOUT_VIEW_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimePathLayoutViewReferenceScope final {
public:
    explicit RuntimePathLayoutViewReferenceScope(bool reference) noexcept
        : previous_(runtimePathLayoutViewReferenceOverride) {
        runtimePathLayoutViewReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathLayoutViewReferenceScope() {
        runtimePathLayoutViewReferenceOverride = previous_;
    }
    RuntimePathLayoutViewReferenceScope(const RuntimePathLayoutViewReferenceScope&) = delete;
    RuntimePathLayoutViewReferenceScope& operator=(const RuntimePathLayoutViewReferenceScope&) = delete;
private:
    int previous_;
};

// Only coordinate-derived data belongs here. Terrain values, profile hashes,
// requests, paths and cooldowns remain queue-owned and are recomputed as before.
struct RuntimePathQueueLayoutData final {
    std::vector<std::pair<long long, int>> terrainIndex;
    std::unordered_map<long long, int> terrainLookup;
    std::vector<int> denseTerrainIndex;
    int denseTerrainMinX = 0;
    int denseTerrainMinY = 0;
    int denseTerrainWidth = 0;
    int denseTerrainHeight = 0;

    bool sameMapping(const RuntimePathQueueLayoutData& other) const {
        return terrainIndex == other.terrainIndex && terrainLookup == other.terrainLookup
            && denseTerrainIndex == other.denseTerrainIndex
            && denseTerrainMinX == other.denseTerrainMinX && denseTerrainMinY == other.denseTerrainMinY
            && denseTerrainWidth == other.denseTerrainWidth && denseTerrainHeight == other.denseTerrainHeight;
    }

    void swap(RuntimePathQueueLayoutData& other) noexcept {
        static_assert(noexcept(terrainIndex.swap(other.terrainIndex)));
        static_assert(noexcept(terrainLookup.swap(other.terrainLookup)));
        static_assert(noexcept(denseTerrainIndex.swap(other.denseTerrainIndex)));
        terrainIndex.swap(other.terrainIndex);
        terrainLookup.swap(other.terrainLookup);
        denseTerrainIndex.swap(other.denseTerrainIndex);
        using std::swap;
        swap(denseTerrainMinX, other.denseTerrainMinX);
        swap(denseTerrainMinY, other.denseTerrainMinY);
        swap(denseTerrainWidth, other.denseTerrainWidth);
        swap(denseTerrainHeight, other.denseTerrainHeight);
    }
};

// Default construction owns its original containers. Only the explicit warm
// runtime route publishes a completed layout once, moving the containers into
// an immutable owner. Clearing/swapping/rebuilding the source cache cannot
// invalidate a queue holding that owner. Copies always materialize independent
// owned containers; moves rebind the read view and retain the shared owner.
class RuntimePathQueueLayoutStorage final {
public:
    using Data = RuntimePathQueueLayoutData;
    using Owner = std::shared_ptr<const Data>;

    RuntimePathQueueLayoutStorage() : view_(&owned_) {}
    RuntimePathQueueLayoutStorage(const RuntimePathQueueLayoutStorage& other)
        : owned_(other.read()), view_(&owned_) {}
    RuntimePathQueueLayoutStorage& operator=(const RuntimePathQueueLayoutStorage& other) {
        if (this != &other) {
            Data replacement(other.read());
            owned_.swap(replacement);
            shared_.reset();
            bind();
        }
        return *this;
    }
    RuntimePathQueueLayoutStorage(RuntimePathQueueLayoutStorage&& other)
        : shared_(std::move(other.shared_)) {
        // MSVC unordered_map move construction allocates an empty sentinel /
        // bucket set. Default-construct before transferring anything, then use
        // its already-proved noexcept swap to retain exact buffers and nodes.
        owned_.swap(other.owned_);
        bind();
        other.bind();
    }
    RuntimePathQueueLayoutStorage& operator=(RuntimePathQueueLayoutStorage&& other) noexcept {
        static_assert(std::is_nothrow_move_assignable_v<Data>);
        if (this != &other) {
            owned_ = std::move(other.owned_);
            shared_ = std::move(other.shared_);
            bind();
            other.bind();
        }
        return *this;
    }
    void swap(RuntimePathQueueLayoutStorage& other) noexcept {
        owned_.swap(other.owned_);
        shared_.swap(other.shared_);
        bind();
        other.bind();
    }
    const Data& read() const noexcept { return *view_; }
    bool isShared() const noexcept { return shared_ != nullptr; }
    const void* sharedIdentityForDiagnostics() const noexcept { return shared_.get(); }
    Data& ownedForConstruction() {
        if (shared_) throw std::logic_error("shared queue layout is immutable");
        return owned_;
    }
    // Complete publication only. All allocation, including MSVC's empty hash
    // storage, finishes before the noexcept container swaps. Failure leaves
    // the original containers and view untouched.
    Owner shareReadOnly() {
        if (!shared_) {
            auto complete = std::make_shared<Data>();
            complete->swap(owned_);
            shared_ = std::move(complete);
            bind();
        }
        return shared_;
    }
    void borrow(Owner owner) {
        if (!owner || shared_ || !owned_.terrainIndex.empty()
            || owned_.terrainIndex.capacity() != 0 || !owned_.terrainLookup.empty()
            || !owned_.denseTerrainIndex.empty() || owned_.denseTerrainIndex.capacity() != 0)
            throw std::logic_error("queue layout must be fresh before sharing");
        shared_ = std::move(owner);
        bind();
    }
private:
    void bind() noexcept { view_ = shared_ ? shared_.get() : &owned_; }
    Data owned_;
    Owner shared_;
    const Data* view_;
};
