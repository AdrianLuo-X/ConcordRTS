#pragma once

#include <string>
#include <string_view>

inline bool runtimeArcticTheater(std::string_view theater) {
    std::string normalized;
    normalized.reserve(theater.size());
    for (char ch : theater) normalized.push_back(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
    return normalized == "snow" || normalized == ".sno";
}

inline std::string runtimeArcticArtId(const std::string& normalId, bool alternateArcticArt, bool snowy) {
    // ObjectType::LoadArt (0x5F9070) appends "%sA" only in Snow theater.
    // Derive from the immutable rule image each time: cached render contexts
    // can change theater without accumulating suffixes or changing the type.
    return alternateArcticArt && snowy && !normalId.empty() ? normalId + "A" : normalId;
}
