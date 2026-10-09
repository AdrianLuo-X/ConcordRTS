#pragma once

#include "GameWorld.h"
#include "RuntimeHistoryQuery.h"

#include <cstddef>
#include <functional>
#include <string_view>
#include <unordered_map>

// A memo belongs to one phase while rules and object type/discriminators stay
// unchanged. It stores views into those objects, never outlives their storage,
// and is discarded before actions, world append/reorder or rules replacement.
// Preserve exact spellings and both aircraft discriminators: runtime rule
// namespaces do not all select aircraft using the same discriminator.
template<class Result>
class RuntimeHistoryStageRuleMemo {
    struct Key {
        WorldObjectKind kind;
        std::string_view type;
        bool authoredAircraft;
        bool airborne;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const {
            const auto flags = static_cast<std::size_t>(key.kind) * 4U
                + (key.authoredAircraft ? 2U : 0U) + (key.airborne ? 1U : 0U);
            return std::hash<std::string_view>{}(key.type) ^ (flags * 0x9e3779b9U);
        }
    };
public:
    explicit RuntimeHistoryStageRuleMemo(bool reference = runtimeHistoryQueryReference())
        : reference_(reference) {}

    template<class Resolve>
    Result query(const WorldObjectInstance& object, Resolve&& resolve) {
        if (reference_) return resolve(object);
        const Key key{object.kind, std::string_view(object.type.data(), object.type.size()),
            object.authoredAircraft, object.airborne};
        if (hasLast_ && lastKey_ == key) return lastResult_;
        const auto found = results_.find(key);
        if (found != results_.end()) {
            lastKey_ = key; lastResult_ = found->second; hasLast_ = true;
            return lastResult_;
        }
        const Result result = resolve(object);
        // Unresolved imported/modded type spellings can be arbitrarily many.
        // Bound per-phase allocation; overflow retains scalar resolution plus
        // the same last-key fast path without changing any result.
        if (results_.size() < maximumMemoizedTypes) results_.emplace(key, result);
        lastKey_ = key; lastResult_ = result; hasLast_ = true;
        return result;
    }
private:
    static constexpr std::size_t maximumMemoizedTypes = 256;
    bool reference_;
    bool hasLast_ = false;
    Key lastKey_{};
    Result lastResult_{};
    std::unordered_map<Key, Result, KeyHash> results_;
};
