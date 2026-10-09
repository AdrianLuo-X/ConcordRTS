#pragma once

#include "RuntimeMultiplayerMatchSetup.h"
#include "RuntimeMultiplayerSnapshotTransfer.h"

// Map transfers use their own messages and request sequence. The existing
// bounded, uncompressed chunk engine handles opaque bytes only; its descriptor
// below is never submitted to a world Save/Load operation.
struct RuntimeMultiplayerMapRequest {
    std::uint64_t requestId = 0;
    RuntimeMultiplayerMatchMap map;
    bool operator==(const RuntimeMultiplayerMapRequest&) const = default;
};

struct RuntimeMultiplayerMapOffer {
    RuntimeMultiplayerMapRequest request;
    RuntimeMultiplayerSnapshotDescriptor transfer;
    bool operator==(const RuntimeMultiplayerMapOffer&) const = default;
};

bool validateRuntimeMultiplayerMapRequest(const RuntimeMultiplayerMapRequest&, std::string&);
bool validateRuntimeMultiplayerMapOffer(const RuntimeMultiplayerMapOffer&, std::string&);
bool validateRuntimeMultiplayerMapBytes(const RuntimeMultiplayerMatchMap&,
    std::span<const std::uint8_t>, std::string&);
bool makeRuntimeMultiplayerMapOffer(const RuntimeMultiplayerMapRequest&,
    std::span<const std::uint8_t>, RuntimeMultiplayerMapOffer&, std::string&);
