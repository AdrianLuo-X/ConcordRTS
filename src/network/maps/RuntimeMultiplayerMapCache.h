#pragma once
#include "network/maps/RuntimeMultiplayerMapTransfer.h"
#include <filesystem>

// Only content-derived names are written below the trusted local data root.
// A remote canonical map id is never interpreted as an output path.
bool storeRuntimeMultiplayerMapCache(const std::filesystem::path& localDataRoot,
    const RuntimeMultiplayerMapOffer&, std::span<const std::uint8_t>,
    std::filesystem::path& output, std::string& error);
bool findRuntimeMultiplayerMapCache(const std::filesystem::path& localDataRoot,
    const RuntimeMultiplayerMatchMap&, std::filesystem::path& output, std::string& error);
