#pragma once
#include <string>
#include <vector>
struct MapPackedSectionSummary;
namespace concord::map_packed_decode {
void decodeLzoPayload(
    const std::vector<unsigned char>& decodedSectionBytes,
    MapPackedSectionSummary& summary,
    std::vector<unsigned char>* payloadBytes,
    std::vector<std::string>& warnings);
} // namespace concord::map_packed_decode
