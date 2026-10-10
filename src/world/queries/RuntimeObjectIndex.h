#pragma once

#include "world/model/GameWorld.h"

#include <cstddef>
#include <unordered_map>

// A lookup hint, never object ownership. World storage can be replaced by
// save/load, capture or conversion transactions without an invalidation hook.
// Validate the ID at the cached offset on every read and rebuild on a miss.
// Instance IDs are unique within a valid GameWorld; no pointers are retained.
class RuntimeObjectIndex {
public:
    template <class Objects>
    auto find(Objects& objects, int objectId) const -> decltype(objects.data()) {
        // Negative IDs are the no-target sentinel throughout the runtime.
        // Do not scan all historical objects for every idle unit/projectile.
        if (objectId < 0) return nullptr;

        // Map imports and ordinary production assign dense, one-based IDs.
        if (objectId > 0) {
            const std::size_t index = static_cast<std::size_t>(objectId - 1);
            if (index < objects.size()
                && objects[index].instanceId == objectId) {
                return &objects[index];
            }
        }
        const auto found = indices_.find(objectId);
        if (found != indices_.end() && found->second < objects.size()
            && objects[found->second].instanceId == objectId) {
            return &objects[found->second];
        }

        // Do not cache missing IDs: a same-size replacement can introduce one
        // between queries. Missing targets need no allocation or index rebuild.
        std::size_t match = 0;
        for (; match < objects.size(); ++match) {
            if (objects[match].instanceId == objectId) break;
        }
        if (match == objects.size()) return nullptr;
        indices_.clear();
        indices_.reserve(objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index) {
            indices_.try_emplace(objects[index].instanceId, index);
        }
        return &objects[match];
    }

private:
    mutable std::unordered_map<int, std::size_t> indices_;
};
