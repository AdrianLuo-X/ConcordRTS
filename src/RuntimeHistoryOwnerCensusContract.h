#pragma once
#include "RuntimeHistoryQuery.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

inline bool runRuntimeHistoryOwnerCensusContract(std::string& error) {
    struct Object { std::string owner; bool alive; };
    const auto trim = [](std::string value) {
        const auto nonspace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
        value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
        return value;
    };
    const auto key = [](std::string value) {
        for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    };
    std::vector<Object> objects;
    const std::vector<std::string> spellings{"Americans", " AMERICANS ", "Russians", " russians\t",
        "Neutral", " CIVILIAN ", "Special", "", "\t\r\n", "<Player @ A>",
        std::string("owner\0suffix", 12), std::string("\x80Owner\xff", 7), "DeadHouse"};
    for (int n = 0; n < 10256; ++n)
        objects.push_back({spellings[static_cast<std::size_t>(n) % spellings.size()],
            n < 256 && spellings[static_cast<std::size_t>(n) % spellings.size()] != "DeadHouse"});
    if (std::any_of(objects.begin(), objects.end(), [](const auto& value) {
        return value.alive && value.owner == "DeadHouse";
    })) { error = "dead-owner fixture contains a live member"; return false; }
    for (int variation = 0; variation < 12; ++variation) {
        if (variation & 1) std::reverse(objects.begin(), objects.end());
        else std::rotate(objects.begin(), objects.begin() + 27, objects.end());
        std::map<std::string, std::string> original, reused;
        std::size_t oldCalls = 0, newCalls = 0;
        const auto collect = [&](auto& output, auto& calls) {
            return [&](const std::string& raw) {
                ++calls;
                const auto owner = trim(raw);
                const auto canonical = key(owner);
                if (!canonical.empty() && canonical != "neutral" && canonical != "civilian"
                    && canonical != "special") output.try_emplace(canonical, owner);
            };
        };
        if (variation % 3 == 0) original.emplace("americans", "EconomyPriority"),
            reused.emplace("americans", "EconomyPriority");
        {
            RuntimeHistoryQueryReferenceScope reference(true);
            runtimeForEachDistinctObjectOwner(objects, collect(original, oldCalls));
        }
        {
            RuntimeHistoryQueryReferenceScope reuse(false);
            runtimeForEachDistinctObjectOwner(objects, collect(reused, newCalls));
        }
        if (original != reused || oldCalls != objects.size() || newCalls != spellings.size()
            || !reused.contains("deadhouse")) {
            error = "history owner census differs in membership, first spelling, or dead-owner retention";
            return false;
        }
    }
    return true;
}
