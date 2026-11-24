#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace concord::render {

template <class PoseUseIndex>
void touchRuntimeVehicleBodyPoseUse(
    PoseUseIndex& uses, std::unordered_map<std::string, std::string>& aliases,
    std::uint64_t& sequenceCounter, std::uint64_t& entrySequence,
    const std::string& rasterKey, const std::string& lookupKey
) {
    auto use = uses.extract(entrySequence);
    const std::uint64_t sequence = ++sequenceCounter;
    if (use.empty()) {
        uses.emplace(sequence,
            typename PoseUseIndex::mapped_type{rasterKey, lookupKey});
    } else {
        // Exact scale lookup keys can alias the same raster key. Keep only the
        // current alias so eviction also bounds the secondary lookup cache.
        auto& reference = use.mapped();
        if (!lookupKey.empty() && reference.lookupKey != lookupKey) {
            const auto old = aliases.find(reference.lookupKey);
            if (old != aliases.end()
                && old->second == reference.rasterKey)
                aliases.erase(old);
            reference.lookupKey = lookupKey;
        }
        use.key() = sequence;
        uses.insert(std::move(use));
    }
    entrySequence = sequence;
}

template <class PoseUseIndex, class EvictRaster>
void trimRuntimeVehicleBodyPoseUses(
    PoseUseIndex& uses, std::unordered_map<std::string, std::string>& aliases,
    std::uint64_t& evictions, EvictRaster evictRaster
) {
    constexpr std::size_t capacity = 768;
    while (uses.size() > capacity) {
        const auto oldest = uses.begin();
        const auto& reference = oldest->second;
        evictRaster(reference.rasterKey, oldest->first);
        const auto lookup = aliases.find(reference.lookupKey);
        if (lookup != aliases.end()
            && lookup->second == reference.rasterKey)
            aliases.erase(lookup);
        uses.erase(oldest);
        ++evictions;
    }
}

} // namespace concord::render
