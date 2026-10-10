// Include after RuntimeWindowState/RuntimeTerrainInfo, before the dynamic
// buildRuntimePathTerrainSnapshot body. A builder borrows its owner's table.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

inline thread_local int runtimePathSnapshotMetadataQueryReferenceOverride = -1;

inline bool runtimePathSnapshotMetadataQueryReference() {
    if (runtimePathSnapshotMetadataQueryReferenceOverride >= 0)
        return runtimePathSnapshotMetadataQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_SNAPSHOT_METADATA_QUERY_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimePathSnapshotMetadataQueryReferenceScope {
public:
    explicit RuntimePathSnapshotMetadataQueryReferenceScope(bool reference)
        : previous_(runtimePathSnapshotMetadataQueryReferenceOverride) {
        runtimePathSnapshotMetadataQueryReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathSnapshotMetadataQueryReferenceScope() {
        runtimePathSnapshotMetadataQueryReferenceOverride = previous_;
    }
    RuntimePathSnapshotMetadataQueryReferenceScope(const RuntimePathSnapshotMetadataQueryReferenceScope&) = delete;
    RuntimePathSnapshotMetadataQueryReferenceScope& operator=(const RuntimePathSnapshotMetadataQueryReferenceScope&) = delete;
private:
    int previous_;
};

// One read-only dynamic snapshot loop borrows a container-owned table. Preserve
// the ORIGINAL integer-key identity, including legacy raw-coordinate aliases.
// Missing metadata means nullptr/skip, with NO runtimeTerrainInfoForCell or
// runtimeCachedTerrainInfoForCell call. An out-of-domain key uses the old find.
//
// The metadata container invalidates the table before every structural edit,
// copy, move and swap. In-place value edits retain their live node addresses.
// The structural generation also detects same-size replacements.
// A view is acquired once immediately before the loop and must not outlive that
// synchronous read-only loop, its owner or a reference-scope change.
class RuntimePathSnapshotMetadataQueries {
public:
    static constexpr std::size_t side = 512;
    static constexpr std::size_t denseCellCount = side * side;
    static constexpr std::size_t minimumQueries = 4096;
    static constexpr std::size_t minimumMetadataEntries = 4096;
    static constexpr std::size_t maximumMetadataEntries = denseCellCount;

    static const RuntimeTerrainInfo* originalFind(const RuntimeWindowState& state, int key) {
        const auto found = state.terrainInfoByCell.find(key);
        return found == state.terrainInfoByCell.end() ? nullptr : std::addressof(found->second);
    }

    class ReadOnlyView {
    public:
        const RuntimeTerrainInfo* find(int key) const {
            std::size_t offset = 0;
            if (denseOffset(key, offset)) return (*owner_->pointers_)[offset];
            return originalFind(*owner_->state_, key);
        }
    private:
        friend class RuntimePathSnapshotMetadataQueries;
        explicit ReadOnlyView(const RuntimePathSnapshotMetadataQueries* owner) : owner_(owner) {}
        const RuntimePathSnapshotMetadataQueries* owner_;
    };

    RuntimePathSnapshotMetadataQueries(const RuntimeWindowState& state,
        std::size_t expectedQueries, bool enabled = true,
        bool reference = runtimePathSnapshotMetadataQueryReference())
        : state_(&state), tick_(state.tick), count_(state.terrainInfoByCell.size()),
          buckets_(state.terrainInfoByCell.bucket_count()),
          generation_(state.terrainInfoByCell.generation()),
          enabled_(enabled && !reference && !runtimePathSnapshotMetadataQueryReference()
              && expectedQueries >= minimumQueries && count_ >= minimumMetadataEntries
              && count_ <= maximumMetadataEntries) {
        // Reference, Air/disabled, small query/map and oversized metadata maps
        // allocate nothing and do not scan metadata. Exact old lookup remains.
        if (!enabled_) return;
        if (!state.terrainInfoByCell.denseValid()) {
            RuntimePerfTraceScope buildTrace("pathSnapshotMetadataTable", state.tick);
            pointers_ = std::addressof(state.terrainInfoByCell.densePointers());
            sourceEntriesExamined_ = count_;
        } else {
            pointers_ = std::addressof(state.terrainInfoByCell.densePointers());
        }
        retainedEntries_ = state.terrainInfoByCell.denseRetainedEntries();
    }
    RuntimePathSnapshotMetadataQueries(const RuntimePathSnapshotMetadataQueries&) = delete;
    RuntimePathSnapshotMetadataQueries& operator=(const RuntimePathSnapshotMetadataQueries&) = delete;
    RuntimePathSnapshotMetadataQueries(RuntimePathSnapshotMetadataQueries&&) = delete;
    RuntimePathSnapshotMetadataQueries& operator=(RuntimePathSnapshotMetadataQueries&&) = delete;

    void invalidate() noexcept { enabled_ = false; }
    std::optional<ReadOnlyView> viewFor(const RuntimeWindowState& state) const {
        if (!enabled_ || runtimePathSnapshotMetadataQueryReference() || state_ != &state
            || tick_ != state.tick || count_ != state.terrainInfoByCell.size()
            || generation_ != state.terrainInfoByCell.generation()
            || buckets_ != state.terrainInfoByCell.bucket_count()) return std::nullopt;
        return ReadOnlyView(this);
    }
    std::size_t denseBytes() const noexcept {
        return pointers_ ? pointers_->size() * sizeof(const RuntimeTerrainInfo*) : 0;
    }
    std::size_t retainedEntries() const noexcept { return retainedEntries_; }
    std::size_t sourceEntriesExamined() const noexcept { return sourceEntriesExamined_; }

private:
    static bool denseOffset(int key, std::size_t& offset) noexcept {
        if (key < 0) return false;
        const int x = key % 1000, y = key / 1000;
        if (x >= static_cast<int>(side) || y >= static_cast<int>(side)) return false;
        offset = static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x);
        return true;
    }
    const RuntimeWindowState* state_;
    int tick_;
    std::size_t count_, buckets_;
    std::uint64_t generation_;
    bool enabled_;
    const std::vector<const RuntimeTerrainInfo*>* pointers_ = nullptr;
    std::size_t retainedEntries_ = 0, sourceEntriesExamined_ = 0;
};
