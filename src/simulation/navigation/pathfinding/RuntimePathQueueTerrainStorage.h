#pragma once

#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

// Explicit opt-in. The input vector must remain alive and completely immutable
// until the queue and all borrowed moves of it have been destroyed. Queue copies
// materialize independent owned data. Ordinary constructors never borrow.
struct RuntimeReadOnlyPathTerrainTag final {};

inline thread_local int runtimePathTerrainViewReferenceOverride = -1;

inline bool runtimePathTerrainViewReference() noexcept {
    if (runtimePathTerrainViewReferenceOverride >= 0)
        return runtimePathTerrainViewReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_QUEUE_TERRAIN_VIEW_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimePathTerrainViewReferenceScope final {
public:
    explicit RuntimePathTerrainViewReferenceScope(bool reference) noexcept
        : previous_(runtimePathTerrainViewReferenceOverride) {
        runtimePathTerrainViewReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathTerrainViewReferenceScope() {
        runtimePathTerrainViewReferenceOverride = previous_;
    }
    RuntimePathTerrainViewReferenceScope(const RuntimePathTerrainViewReferenceScope&) = delete;
    RuntimePathTerrainViewReferenceScope& operator=(const RuntimePathTerrainViewReferenceScope&) = delete;
private:
    int previous_;
};

template<class Node>
class RuntimePathQueueTerrainStorage final {
public:
    RuntimePathQueueTerrainStorage() = default;
    RuntimePathQueueTerrainStorage(const RuntimePathQueueTerrainStorage& other)
        : owned_(other.nodes()) { publishReadView(); }
    RuntimePathQueueTerrainStorage& operator=(const RuntimePathQueueTerrainStorage& other) {
        if (this != &other) {
            std::vector<Node> replacement(other.nodes());
            owned_.swap(replacement);
            borrowed_ = nullptr;
            publishReadView();
        }
        return *this;
    }
    RuntimePathQueueTerrainStorage(RuntimePathQueueTerrainStorage&& other) noexcept
        : owned_(std::move(other.owned_)), borrowed_(std::exchange(other.borrowed_, nullptr)) {
        publishReadView(); other.data_ = nullptr; other.size_ = 0;
    }
    RuntimePathQueueTerrainStorage& operator=(RuntimePathQueueTerrainStorage&& other) noexcept {
        if (this != &other) {
            owned_ = std::move(other.owned_);
            borrowed_ = std::exchange(other.borrowed_, nullptr);
            publishReadView(); other.data_ = nullptr; other.size_ = 0;
        }
        return *this;
    }
    // No owned/borrowed branch on a raster/search read. Publish once after
    // construction; copy and move always rebind this typed contiguous view.
    std::size_t size() const noexcept { return size_; }
    const Node* data() const noexcept { return data_; }
    const Node& operator[](std::size_t index) const noexcept { return data_[index]; }
    bool isBorrowed() const noexcept { return borrowed_ != nullptr; }
    std::size_t ownedCapacity() const noexcept { return owned_.capacity(); }
    void borrow(const std::vector<Node>& source) {
        if (borrowed_ != nullptr || !owned_.empty() || owned_.capacity() != 0)
            throw std::logic_error("terrain storage must be fresh before borrowing");
        borrowed_ = &source;
        publishReadView();
    }
    void reserve(std::size_t capacity) { requireOwned(); owned_.reserve(capacity); publishReadView(); }
    void push_back(const Node& node) { requireOwned(); owned_.push_back(node); publishReadView(); }
    Node& mutableOwnedAt(std::size_t index) { requireOwned(); return owned_[index]; }
    std::vector<Node>& ownedConstructionNodes() { requireOwned(); return owned_; }
    void finishOwnedConstruction() { requireOwned(); publishReadView(); }
private:
    const std::vector<Node>& nodes() const noexcept { return borrowed_ ? *borrowed_ : owned_; }
    void requireOwned() const {
        if (borrowed_ != nullptr) throw std::logic_error("borrowed terrain is read-only");
    }
    void publishReadView() noexcept {
        const auto& source = nodes();
        data_ = source.data(); size_ = source.size();
    }
    std::vector<Node> owned_;
    const std::vector<Node>* borrowed_ = nullptr;
    const Node* data_ = nullptr;
    std::size_t size_ = 0;
};
