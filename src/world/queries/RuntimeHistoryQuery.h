#pragma once

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_set>

inline thread_local int runtimeHistoryQueryReferenceOverride = -1;
inline bool runtimeHistoryQueryReference() {
    if (runtimeHistoryQueryReferenceOverride >= 0)
        return runtimeHistoryQueryReferenceOverride != 0;
    static const bool reference = [] {
        const char* flag = std::getenv("CONCORD_HISTORY_QUERY_REFERENCE");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    return reference;
}
struct RuntimeHistoryQueryReferenceScope {
    int previous = runtimeHistoryQueryReferenceOverride;
    explicit RuntimeHistoryQueryReferenceScope(bool reference) {
        runtimeHistoryQueryReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeHistoryQueryReferenceScope() { runtimeHistoryQueryReferenceOverride = previous; }
    RuntimeHistoryQueryReferenceScope(const RuntimeHistoryQueryReferenceScope&) = delete;
    RuntimeHistoryQueryReferenceScope& operator=(const RuntimeHistoryQueryReferenceScope&) = delete;
};

// Owner collectors use pure normalization plus insert-if-absent. Preserve
// original first-occurrence order and spelling, including owners that only
// have historical objects. Views live only during this immutable traversal.
template <class Objects, class Append>
void runtimeForEachDistinctObjectOwner(const Objects& objects, Append append) {
    if (runtimeHistoryQueryReference()) {
        for (const auto& object : objects) append(object.owner);
        return;
    }
    std::unordered_set<std::string_view> seen;
    for (const auto& object : objects)
        if (seen.emplace(std::string_view(object.owner)).second) append(object.owner);
}
