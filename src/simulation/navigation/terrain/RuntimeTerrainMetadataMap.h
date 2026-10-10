#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include "simulation/navigation/terrain/RuntimeTrackedAuthoredLand.h"
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

// Value-owned map with an optional index of its OWN mapped-value addresses.
// Composition prevents a mutable raw-map reference from bypassing structural
// invalidation. Iterator/at writes update values at their existing addresses;
// neither their values nor lookup results are copied into the dense index.
template<class Value>
class RuntimeTerrainMetadataMap {
    using RawMap = std::unordered_map<int, Value>;
public:
    using key_type = typename RawMap::key_type;
    using mapped_type = typename RawMap::mapped_type;
    using value_type = typename RawMap::value_type;
    using size_type = typename RawMap::size_type;
    using difference_type = typename RawMap::difference_type;
    using iterator = typename RawMap::iterator;
    using const_iterator = typename RawMap::const_iterator;
    static constexpr size_type denseSide = 512;
    static constexpr size_type denseCellCount = denseSide * denseSide;

    RuntimeTerrainMetadataMap() { initializeLandBinding(); }
    explicit RuntimeTerrainMetadataMap(size_type bucketCount) : map_(bucketCount) { initializeLandBinding(); }
    RuntimeTerrainMetadataMap(std::initializer_list<value_type> values) : map_(values) {
        initializeLandBinding(); rebindLandNodes();
    }

    // Copies own new nodes. Never copy addresses, generation or diagnostics
    // from a different container, even when its bucket shape is identical.
    RuntimeTerrainMetadataMap(const RuntimeTerrainMetadataMap& other) : map_(other.map_) {
        initializeLandBinding(); rebindLandNodes();
    }
    RuntimeTerrainMetadataMap& operator=(const RuntimeTerrainMetadataMap& other) {
        invalidateDense();
        if (this != &other) {
            try { map_ = other.map_; }
            catch (...) { rebindLandNodes(); throw; }
            rebindLandNodes();
        }
        return *this;
    }

    // MSVC's unordered_map move constructor may allocate its sentinel. Do
    // not promise noexcept here. Invalidate before that potentially throwing
    // operation, and never transfer a cached table along with the nodes.
    RuntimeTerrainMetadataMap(RuntimeTerrainMetadataMap&& other) try
        : map_(takeMapForMove(other)) {
        initializeLandBinding(); rebindLandNodes(); other.rebindLandNodes();
    } catch (...) { other.rebindLandNodes(); throw; }
    RuntimeTerrainMetadataMap& operator=(RuntimeTerrainMetadataMap&& other) {
        invalidateDense();
        if (this != &other) {
            other.invalidateDense();
            try { map_ = std::move(other.map_); }
            catch (...) { rebindLandNodes(); other.rebindLandNodes(); throw; }
            rebindLandNodes(); other.rebindLandNodes();
        }
        return *this;
    }
    RuntimeTerrainMetadataMap& operator=(std::initializer_list<value_type> values) {
        invalidateDense();
        try { map_ = values; }
        catch (...) { rebindLandNodes(); throw; }
        rebindLandNodes();
        return *this;
    }

    iterator begin() noexcept { return map_.begin(); }
    const_iterator begin() const noexcept { return map_.begin(); }
    const_iterator cbegin() const noexcept { return map_.cbegin(); }
    iterator end() noexcept { return map_.end(); }
    const_iterator end() const noexcept { return map_.end(); }
    const_iterator cend() const noexcept { return map_.cend(); }
    iterator find(const key_type& key) { return map_.find(key); }
    const_iterator find(const key_type& key) const { return map_.find(key); }
    bool contains(const key_type& key) const { return map_.contains(key); }
    mapped_type& at(const key_type& key) { return map_.at(key); }
    const mapped_type& at(const key_type& key) const { return map_.at(key); }
    size_type size() const noexcept { return map_.size(); }
    bool empty() const noexcept { return map_.empty(); }
    size_type bucket_count() const noexcept { return map_.bucket_count(); }

    // Dirty BEFORE any operation which can replace/invalidate nodes. This
    // remains conservative when a request changes nothing or throws, and
    // covers same-size erase/reinsert, assignment and clear/rebuild cycles.
    void reserve(size_type count) { invalidateDense(); map_.reserve(count); }
    void rehash(size_type count) { invalidateDense(); map_.rehash(count); }
    void clear() noexcept { invalidateDense(); map_.clear(); }
    size_type erase(const key_type& key) { invalidateDense(); return map_.erase(key); }
    iterator erase(iterator position) { invalidateDense(); return map_.erase(position); }
    iterator erase(const_iterator position) { invalidateDense(); return map_.erase(position); }
    iterator erase(const_iterator first, const_iterator last) {
        invalidateDense(); return map_.erase(first,last);
    }
    template<class... Args>
    std::pair<iterator,bool> emplace(Args&&... args) {
        invalidateDense();
        auto result = map_.emplace(std::forward<Args>(args)...);
        bindLandNode(result.first); return result;
    }
    template<class... Args>
    std::pair<iterator,bool> try_emplace(const key_type& key, Args&&... args) {
        invalidateDense();
        auto result = map_.try_emplace(key,std::forward<Args>(args)...);
        bindLandNode(result.first); return result;
    }
    template<class... Args>
    std::pair<iterator,bool> try_emplace(key_type&& key, Args&&... args) {
        invalidateDense();
        auto result = map_.try_emplace(std::move(key),std::forward<Args>(args)...);
        bindLandNode(result.first); return result;
    }
    template<class... Args>
    iterator try_emplace(const_iterator hint, const key_type& key, Args&&... args) {
        invalidateDense();
        auto result = map_.try_emplace(hint,key,std::forward<Args>(args)...);
        bindLandNode(result); return result;
    }
    template<class... Args>
    iterator try_emplace(const_iterator hint, key_type&& key, Args&&... args) {
        invalidateDense();
        auto result = map_.try_emplace(hint,std::move(key),std::forward<Args>(args)...);
        bindLandNode(result); return result;
    }
    mapped_type& operator[](const key_type& key) {
        invalidateDense();
        auto& value = map_[key]; bindLandValue(value,key); return value;
    }
    mapped_type& operator[](key_type&& key) {
        invalidateDense();
        auto& value = map_[std::move(key)]; bindLandValue(value,key); return value;
    }

    void swap(RuntimeTerrainMetadataMap& other)
        noexcept(noexcept(std::declval<RawMap&>().swap(std::declval<RawMap&>()))) {
        invalidateDense();
        if (this != &other) {
            other.invalidateDense();
            map_.swap(other.map_);
            rebindLandNodes(); other.rebindLandNodes();
        }
        // Generation, storage for a later dense rebuild, and build statistics
        // stay with their original container identities.
    }
    friend void swap(RuntimeTerrainMetadataMap& left, RuntimeTerrainMetadataMap& right)
        noexcept(noexcept(left.swap(right))) {
        left.swap(right);
    }

    std::uint64_t generation() const noexcept { return generation_; }
    static constexpr std::int64_t authoredLandMissing = std::numeric_limits<std::int64_t>::min();
    std::uint64_t authoredLandRevision() const noexcept { return authoredLandRevision_; }
    size_type authoredLandBuildCount() const noexcept { return authoredLandBuildCount_; }
    size_type authoredLandWriteCount() const noexcept { return authoredLandWriteCount_; }
    bool authoredLandValuesValid() const noexcept { return authoredLandValid_; }
    // Full exact enum ordinals, not permission-normalized codes. The missing
    // sentinel lies outside the tracked enum's signed/unsigned 32-bit domain.
    // Acquire only for one synchronous immutable builder. Structural edits
    // invalidate before changing nodes; retained field/Value writes update it.
    const std::vector<std::int64_t>& denseAuthoredLandValues() const {
        if constexpr (trackedLandValue) {
            if (!authoredLandValid_) {
                authoredLandValues_.assign(denseCellCount,authoredLandMissing);
                for (const auto& [key,value] : map_) {
                    size_type offset = 0;
                    if (denseOffset(key,offset)) authoredLandValues_[offset] = value.authoredLandType.ordinal();
                }
                authoredLandValid_ = true; ++authoredLandBuildCount_;
            }
        }
        return authoredLandValues_;
    }
    bool denseValid() const noexcept { return denseValid_; }
    size_type denseBuildCount() const noexcept { return denseBuildCount_; }
    size_type denseRetainedEntries() const noexcept { return denseRetainedEntries_; }

    // The caller gates this optional 2 MiB pointer table by query volume and
    // map size (4096..262144). Outside the legacy canonical key domain it must
    // keep using find(). A borrowed table is read-only and must not be used
    // across structural operations; query views also check generation.
    const std::vector<const mapped_type*>& densePointers() const {
        if (denseValid_) return densePointers_;
        // Keep invalid until allocation, zeroing and the entire node scan
        // complete, so bad_alloc cannot expose partially prepared pointers.
        denseRetainedEntries_ = 0;
        densePointers_.assign(denseCellCount,nullptr);
        for (const auto& [key,value] : map_) {
            size_type offset = 0;
            if (denseOffset(key,offset)) {
                densePointers_[offset] = std::addressof(value);
                ++denseRetainedEntries_;
            }
        }
        denseValid_ = true;
        ++denseBuildCount_;
        return densePointers_;
    }

private:
    static constexpr bool trackedLandValue = requires(Value& value) {
        typename std::remove_cvref_t<decltype(value.authoredLandType)>::TrackedAuthoredLandTag;
    };
    void initializeLandBinding() noexcept { landBinding_ = {this,&onLandWrite}; }
    static void onLandWrite(void* owner,int key,std::int64_t ordinal) noexcept {
        auto& map = *static_cast<RuntimeTerrainMetadataMap*>(owner);
        ++map.authoredLandRevision_; ++map.authoredLandWriteCount_;
        size_type offset=0;
        if (map.authoredLandValid_ && denseOffset(key,offset))
            map.authoredLandValues_[offset]=ordinal;
    }
    void bindLandValue(Value& value,int key) noexcept {
        if constexpr (trackedLandValue) value.authoredLandType.bind(&landBinding_,key);
    }
    void bindLandNode(iterator node) noexcept {
        if (node!=map_.end()) bindLandValue(node->second,node->first);
    }
    void rebindLandNodes() noexcept {
        if constexpr (trackedLandValue) for(auto& [key,value]:map_) bindLandValue(value,key);
    }
    static bool denseOffset(key_type key, size_type& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000, y = key / 1000;
        if (x >= static_cast<int>(denseSide) || y >= static_cast<int>(denseSide)) return false;
        offset = static_cast<size_type>(y) * denseSide + static_cast<size_type>(x);
        return true;
    }
    void invalidateDense() noexcept {
        ++generation_;
        ++authoredLandRevision_;
        authoredLandValid_ = false;
        authoredLandValues_.clear();
        denseValid_ = false;
        denseRetainedEntries_ = 0;
        // clear() destroys only trivial pointers and retains bounded storage.
        // No dangling pointer survives inside a now-invalid table.
        densePointers_.clear();
    }
    static RawMap&& takeMapForMove(RuntimeTerrainMetadataMap& other) noexcept {
        other.invalidateDense();
        return std::move(other.map_);
    }

    RawMap map_;
    RuntimeAuthoredLandWriteBinding landBinding_;
    std::uint64_t authoredLandRevision_ = 0;
    size_type authoredLandWriteCount_ = 0;
    mutable bool authoredLandValid_ = false;
    mutable size_type authoredLandBuildCount_ = 0;
    mutable std::vector<std::int64_t> authoredLandValues_;
    std::uint64_t generation_ = 0;
    mutable bool denseValid_ = false;
    mutable size_type denseBuildCount_ = 0;
    mutable size_type denseRetainedEntries_ = 0;
    mutable std::vector<const mapped_type*> densePointers_;
};
