#include "data/maps/MapLzoPayload.h"
#include "data/maps/MapDatabase.h"
#include "assets/codecs/RuntimeLzo1xCodec.h"
namespace concord::map_packed_decode {
using concord::assets::decodeLzo1xBlock;
void decodeLzoPayload(
    const std::vector<unsigned char>& decodedSectionBytes,
    MapPackedSectionSummary& summary,
    std::vector<unsigned char>* payloadBytes,
    std::vector<std::string>& warnings
) {
    if (summary.lineCount == 0 || !summary.chunkTableValid) {
        return;
    }

    summary.payloadDecodeAttempted = true;

    for (const MapPackedChunkSummary& chunk : summary.chunks) {
        std::size_t decodedBytes = 0;
        std::string errorMessage;
        if (!decodeLzo1xBlock(
                decodedSectionBytes,
                chunk.offset + 4,
                chunk.compressedBytes,
                chunk.uncompressedBytes,
                decodedBytes,
                payloadBytes,
                errorMessage
            )) {
            ++summary.payloadDecodeFailureCount;
            warnings.push_back("Packed section [" + summary.sectionName
                + "] LZO chunk at decoded offset " + std::to_string(chunk.offset)
                + " failed: " + errorMessage + ".");
            continue;
        }

        ++summary.payloadDecodedChunks;
        summary.payloadDecodedBytes += decodedBytes;
    }

    summary.payloadDecoded =
        summary.payloadDecodeFailureCount == 0
        && summary.payloadDecodedChunks == summary.chunkCount
        && summary.payloadDecodedBytes == summary.totalUncompressedBytes;
}
} // namespace concord::map_packed_decode
