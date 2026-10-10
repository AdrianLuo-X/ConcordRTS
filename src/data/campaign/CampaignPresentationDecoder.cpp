#include "data/campaign/CampaignPresentationDecoder.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t kMaximumInputBytes = 256U * 1024U * 1024U;
constexpr std::uint32_t kMaximumDimension = 4096U;
constexpr std::uint64_t kMaximumPixels = 16U * 1024U * 1024U;
constexpr std::uint32_t kMaximumFrameRate = 120U;
constexpr std::size_t kMaximumCodebookBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumPendingBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kNoCurrentFrame =
    std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t fourCc(
    char a,
    char b,
    char c,
    char d
) noexcept {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

constexpr std::uint32_t kFormTag = fourCc('F', 'O', 'R', 'M');
constexpr std::uint32_t kWvqaTag = fourCc('W', 'V', 'Q', 'A');
constexpr std::uint32_t kVqhdTag = fourCc('V', 'Q', 'H', 'D');
constexpr std::uint32_t kFinfTag = fourCc('F', 'I', 'N', 'F');
constexpr std::uint32_t kVqfrTag = fourCc('V', 'Q', 'F', 'R');
constexpr std::uint32_t kVqflTag = fourCc('V', 'Q', 'F', 'L');
constexpr std::uint32_t kCbf0Tag = fourCc('C', 'B', 'F', '0');
constexpr std::uint32_t kCbfzTag = fourCc('C', 'B', 'F', 'Z');
constexpr std::uint32_t kCbp0Tag = fourCc('C', 'B', 'P', '0');
constexpr std::uint32_t kCbpzTag = fourCc('C', 'B', 'P', 'Z');
constexpr std::uint32_t kCpl0Tag = fourCc('C', 'P', 'L', '0');
constexpr std::uint32_t kVpt0Tag = fourCc('V', 'P', 'T', '0');
constexpr std::uint32_t kVptzTag = fourCc('V', 'P', 'T', 'Z');
constexpr std::uint32_t kVptrTag = fourCc('V', 'P', 'T', 'R');
constexpr std::uint32_t kVprzTag = fourCc('V', 'P', 'R', 'Z');

std::uint16_t readLittle16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) noexcept {
    return static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t readLittle32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) noexcept {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint32_t readBig32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
        | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::string tagName(std::uint32_t tag) {
    std::string result(4U, '?');
    for (std::size_t i = 0; i < 4U; ++i) {
        const unsigned int shift = static_cast<unsigned int>((3U - i) * 8U);
        const unsigned char value = static_cast<unsigned char>(tag >> shift);
        result[i] = value >= 32U && value <= 126U
            ? static_cast<char>(value)
            : '?';
    }
    return result;
}

bool paddedChunkEnd(
    std::size_t payloadOffset,
    std::uint32_t payloadBytes,
    std::size_t limit,
    std::size_t& endOffset
) noexcept {
    const std::size_t available = limit >= payloadOffset
        ? limit - payloadOffset
        : 0U;
    const std::size_t padding = payloadBytes & 1U;
    if (static_cast<std::size_t>(payloadBytes) > available
        || padding > available - static_cast<std::size_t>(payloadBytes)) {
        return false;
    }
    endOffset = payloadOffset + static_cast<std::size_t>(payloadBytes) + padding;
    return true;
}

bool copyLcwRange(
    std::vector<std::uint8_t>& output,
    std::size_t sourceOffset,
    std::size_t count,
    std::size_t maximumOutputBytes,
    std::string& error
) {
    if (count > maximumOutputBytes - output.size()) {
        error = "LCW copy exceeds the configured output size limit";
        return false;
    }
    if (count != 0U && sourceOffset >= output.size()) {
        error = "LCW copy points outside previously decoded output";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readOffset = sourceOffset + i;
        if (readOffset >= output.size()) {
            error = "LCW copy overruns previously decoded output";
            return false;
        }
        output.push_back(output[readOffset]);
    }
    return true;
}

bool decodeLcw(
    std::span<const std::uint8_t> compressed,
    std::size_t maximumOutputBytes,
    std::vector<std::uint8_t>& output,
    std::string& error
) {
    output.clear();
    error.clear();
    if (compressed.empty()) {
        error = "LCW stream is empty";
        return false;
    }

    std::size_t cursor = 0U;
    bool relativeReferences = false;
    if (compressed.front() == 0U) {
        relativeReferences = true;
        ++cursor;
    }

    bool sawTerminator = false;
    while (cursor < compressed.size()) {
        const std::uint8_t opcode = compressed[cursor++];
        if (opcode == 0x80U) {
            sawTerminator = true;
            break;
        }

        if (opcode == 0xFFU) {
            if (compressed.size() - cursor < 4U) {
                error = "LCW long-copy command is truncated";
                return false;
            }
            const std::size_t count = static_cast<std::size_t>(compressed[cursor])
                | (static_cast<std::size_t>(compressed[cursor + 1U]) << 8U);
            const std::size_t encodedSource =
                static_cast<std::size_t>(compressed[cursor + 2U])
                | (static_cast<std::size_t>(compressed[cursor + 3U]) << 8U);
            cursor += 4U;

            std::size_t sourceOffset = encodedSource;
            if (relativeReferences) {
                if (encodedSource > output.size()) {
                    error = "LCW relative long-copy points before decoded output";
                    return false;
                }
                sourceOffset = output.size() - encodedSource;
            }
            if (!copyLcwRange(
                    output,
                    sourceOffset,
                    count,
                    maximumOutputBytes,
                    error
                )) {
                return false;
            }
            continue;
        }

        if (opcode == 0xFEU) {
            if (compressed.size() - cursor < 3U) {
                error = "LCW fill command is truncated";
                return false;
            }
            const std::size_t count = static_cast<std::size_t>(compressed[cursor])
                | (static_cast<std::size_t>(compressed[cursor + 1U]) << 8U);
            const std::uint8_t value = compressed[cursor + 2U];
            cursor += 3U;
            if (count > maximumOutputBytes - output.size()) {
                error = "LCW fill exceeds the configured output size limit";
                return false;
            }
            output.insert(output.end(), count, value);
            continue;
        }

        if ((opcode & 0xC0U) == 0xC0U) {
            if (compressed.size() - cursor < 2U) {
                error = "LCW absolute-copy command is truncated";
                return false;
            }
            const std::size_t count = (opcode & 0x3FU) + 3U;
            const std::size_t encodedSource =
                static_cast<std::size_t>(compressed[cursor])
                | (static_cast<std::size_t>(compressed[cursor + 1U]) << 8U);
            cursor += 2U;

            std::size_t sourceOffset = encodedSource;
            if (relativeReferences) {
                if (encodedSource > output.size()) {
                    error = "LCW relative copy points before decoded output";
                    return false;
                }
                sourceOffset = output.size() - encodedSource;
            }
            if (!copyLcwRange(
                    output,
                    sourceOffset,
                    count,
                    maximumOutputBytes,
                    error
                )) {
                return false;
            }
            continue;
        }

        if (opcode > 0x80U) {
            const std::size_t count = opcode & 0x3FU;
            if (count > compressed.size() - cursor) {
                error = "LCW literal command exceeds the compressed stream";
                return false;
            }
            if (count > maximumOutputBytes - output.size()) {
                error = "LCW literal command exceeds the output size limit";
                return false;
            }
            output.insert(
                output.end(),
                compressed.begin() + static_cast<std::ptrdiff_t>(cursor),
                compressed.begin() + static_cast<std::ptrdiff_t>(cursor + count)
            );
            cursor += count;
            continue;
        }

        if (cursor >= compressed.size()) {
            error = "LCW short-copy command is truncated";
            return false;
        }
        const std::size_t count = ((opcode & 0x70U) >> 4U) + 3U;
        const std::size_t displacement =
            (static_cast<std::size_t>(opcode & 0x0FU) << 8U)
            | compressed[cursor++];
        if (displacement == 0U || displacement > output.size()) {
            error = "LCW short-copy displacement is outside decoded output";
            return false;
        }
        if (!copyLcwRange(
                output,
                output.size() - displacement,
                count,
                maximumOutputBytes,
                error
            )) {
            return false;
        }
    }

    if (!sawTerminator) {
        error = "LCW stream has no end command";
        return false;
    }
    return true;
}

struct ChunkReference {
    std::uint32_t tag = 0U;
    std::size_t dataOffset = 0U;
    std::size_t dataBytes = 0U;
};

enum class PendingCodebookKind {
    None,
    Raw,
    Compressed
};

struct DecodeState {
    std::array<std::array<std::uint8_t, 4U>, 256U> palette{};
    std::array<bool, 256U> paletteDefined{};
    std::vector<std::uint8_t> codebook;
    std::vector<std::uint8_t> pendingCodebook;
    PendingCodebookKind pendingKind = PendingCodebookKind::None;
    std::uint32_t pendingParts = 0U;
    std::vector<std::uint8_t> frameBgra;
};

struct FrameSubchunks {
    ChunkReference palette;
    ChunkReference fullCodebook;
    ChunkReference partialCodebook;
    ChunkReference vectorPointers;
};

bool hasChunk(const ChunkReference& chunk) noexcept {
    return chunk.tag != 0U;
}

} // namespace

struct CampaignPresentationDecoder::Impl {
    std::vector<std::uint8_t> source;
    std::vector<std::size_t> frameOffsets;
    std::size_t formEnd = 0U;
    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t frameRate = 0U;
    std::uint32_t frameCount = 0U;
    std::uint32_t blockWidth = 0U;
    std::uint32_t blockHeight = 0U;
    std::uint32_t codebookParts = 0U;
    std::uint32_t paletteColorCount = 0U;
    std::size_t blockCount = 0U;
    std::size_t frameBytes = 0U;
    std::size_t frameMapBytes = 0U;
    std::size_t maximumPointerBytes = 0U;
    bool highColor = false;
    bool containerReady = false;
    bool loaded = false;
    std::uint32_t currentFrame = kNoCurrentFrame;
    DecodeState state;
    std::string error;

    void clearData(bool clearError) {
        source.clear();
        frameOffsets.clear();
        formEnd = 0U;
        version = 0U;
        flags = 0U;
        width = 0U;
        height = 0U;
        frameRate = 0U;
        frameCount = 0U;
        blockWidth = 0U;
        blockHeight = 0U;
        codebookParts = 0U;
        paletteColorCount = 0U;
        blockCount = 0U;
        frameBytes = 0U;
        frameMapBytes = 0U;
        maximumPointerBytes = 0U;
        highColor = false;
        containerReady = false;
        loaded = false;
        currentFrame = kNoCurrentFrame;
        state = {};
        if (clearError) {
            error.clear();
        }
    }

    bool fail(std::string message) {
        error = std::move(message);
        return false;
    }

    std::span<const std::uint8_t> chunkBytes(
        const ChunkReference& chunk
    ) const noexcept {
        return std::span<const std::uint8_t>(
            source.data() + chunk.dataOffset,
            chunk.dataBytes
        );
    }

    bool validateCodebook(
        const std::vector<std::uint8_t>& candidate,
        const std::string& sourceName
    ) {
        if (candidate.empty()) {
            return fail(sourceName + " decoded to an empty codebook");
        }
        const std::size_t bytesPerVector = highColor
            ? static_cast<std::size_t>(blockWidth)
                * static_cast<std::size_t>(blockHeight) * 2U
            : static_cast<std::size_t>(blockWidth)
                * static_cast<std::size_t>(blockHeight);
        if (candidate.size() > kMaximumCodebookBytes) {
            return fail(sourceName + " exceeds the codebook size limit");
        }
        if (bytesPerVector == 0U || candidate.size() % bytesPerVector != 0U) {
            return fail(sourceName + " is not an integral number of vectors");
        }
        return true;
    }

    bool decodeCodebookChunk(
        const ChunkReference& chunk,
        std::vector<std::uint8_t>& decoded
    ) {
        if (chunk.tag == kCbf0Tag) {
            decoded.assign(chunkBytes(chunk).begin(), chunkBytes(chunk).end());
        } else if (chunk.tag == kCbfzTag) {
            std::string lcwError;
            if (!decodeLcw(
                    chunkBytes(chunk),
                    kMaximumCodebookBytes,
                    decoded,
                    lcwError
                )) {
                return fail("CBFZ " + lcwError);
            }
        } else {
            return fail("internal error: invalid full-codebook chunk");
        }
        return validateCodebook(decoded, tagName(chunk.tag));
    }

    bool applyReadyPartialCodebook(DecodeState& decodeState) {
        if (decodeState.pendingParts == 0U) {
            return true;
        }
        if (codebookParts == 0U || decodeState.pendingParts > codebookParts) {
            return fail("partial codebook part count exceeds the VQHD declaration");
        }
        if (decodeState.pendingParts != codebookParts) {
            return true;
        }

        std::vector<std::uint8_t> decoded;
        if (decodeState.pendingKind == PendingCodebookKind::Raw) {
            decoded = decodeState.pendingCodebook;
        } else if (decodeState.pendingKind == PendingCodebookKind::Compressed) {
            std::string lcwError;
            if (!decodeLcw(
                    decodeState.pendingCodebook,
                    kMaximumCodebookBytes,
                    decoded,
                    lcwError
                )) {
                return fail("CBPZ " + lcwError);
            }
        } else {
            return fail("partial codebook data has no encoding kind");
        }

        if (!validateCodebook(decoded, "partial codebook")) {
            return false;
        }
        decodeState.codebook = std::move(decoded);
        decodeState.pendingCodebook.clear();
        decodeState.pendingKind = PendingCodebookKind::None;
        decodeState.pendingParts = 0U;
        return true;
    }

    bool parseSubchunks(
        std::size_t begin,
        std::size_t end,
        bool codebookOnly,
        FrameSubchunks& chunks
    ) {
        std::size_t cursor = begin;
        while (cursor < end) {
            // A few encoders leave the 16-bit alignment byte before the next
            // tag instead of accounting for it in the preceding subchunk.
            if (source[cursor] == 0U) {
                ++cursor;
                if (cursor == end) {
                    break;
                }
            }
            if (end - cursor < 8U) {
                return fail("VQA frame ends in a truncated subchunk header");
            }

            const std::uint32_t tag = readBig32(source, cursor);
            const std::uint32_t size = readBig32(source, cursor + 4U);
            const std::size_t dataOffset = cursor + 8U;
            std::size_t next = 0U;
            if (!paddedChunkEnd(dataOffset, size, end, next)) {
                return fail(tagName(tag) + " subchunk exceeds its VQ frame");
            }
            const ChunkReference reference{
                tag,
                dataOffset,
                static_cast<std::size_t>(size)
            };

            if (tag == kCbf0Tag || tag == kCbfzTag) {
                if (hasChunk(chunks.fullCodebook)) {
                    return fail("VQA frame contains multiple full codebooks");
                }
                chunks.fullCodebook = reference;
            } else if (!codebookOnly && tag == kCpl0Tag) {
                if (hasChunk(chunks.palette)) {
                    return fail("VQA frame contains multiple palettes");
                }
                chunks.palette = reference;
            } else if (!codebookOnly && (tag == kCbp0Tag || tag == kCbpzTag)) {
                if (hasChunk(chunks.partialCodebook)) {
                    return fail("VQA frame contains multiple partial codebooks");
                }
                chunks.partialCodebook = reference;
            } else if (!codebookOnly
                && (tag == kVpt0Tag || tag == kVptzTag
                    || tag == kVptrTag || tag == kVprzTag)) {
                if (hasChunk(chunks.vectorPointers)) {
                    return fail("VQA frame contains multiple vector-pointer chunks");
                }
                chunks.vectorPointers = reference;
            } else {
                return fail(
                    codebookOnly
                        ? "VQFL contains unsupported subchunk " + tagName(tag)
                        : "VQFR contains unsupported subchunk " + tagName(tag)
                );
            }
            cursor = next;
        }
        return cursor == end || fail("VQA subchunk alignment exceeds its parent");
    }

    bool applyPalette(
        const ChunkReference& chunk,
        DecodeState& decodeState
    ) {
        if (highColor) {
            return fail("high-color VQA frame unexpectedly contains CPL0");
        }
        if (chunk.dataBytes == 0U || chunk.dataBytes % 3U != 0U) {
            return fail("CPL0 size is not a non-empty RGB palette");
        }
        const std::size_t colors = chunk.dataBytes / 3U;
        if (colors > paletteColorCount || colors > 256U) {
            return fail("CPL0 contains more colors than VQHD declares");
        }

        const auto data = chunkBytes(chunk);
        for (std::size_t i = 0; i < colors; ++i) {
            const std::uint8_t red = data[i * 3U];
            const std::uint8_t green = data[i * 3U + 1U];
            const std::uint8_t blue = data[i * 3U + 2U];
            if (red > 63U || green > 63U || blue > 63U) {
                return fail("CPL0 contains a channel outside the 6-bit DAC range");
            }
            decodeState.palette[i] = {
                static_cast<std::uint8_t>(blue << 2U),
                static_cast<std::uint8_t>(green << 2U),
                static_cast<std::uint8_t>(red << 2U),
                255U
            };
            decodeState.paletteDefined[i] = true;
        }
        return true;
    }

    bool appendPartialCodebook(
        const ChunkReference& chunk,
        DecodeState& decodeState
    ) {
        if (highColor) {
            return fail("high-color VQA does not support split codebooks");
        }
        if (codebookParts == 0U) {
            return fail("CBP chunk is present while VQHD declares zero parts");
        }
        if (chunk.dataBytes == 0U) {
            return fail(tagName(chunk.tag) + " is empty");
        }

        const PendingCodebookKind kind = chunk.tag == kCbp0Tag
            ? PendingCodebookKind::Raw
            : PendingCodebookKind::Compressed;
        if (decodeState.pendingKind != PendingCodebookKind::None
            && decodeState.pendingKind != kind) {
            return fail("a split codebook mixes CBP0 and CBPZ parts");
        }
        const std::size_t limit = kind == PendingCodebookKind::Raw
            ? kMaximumCodebookBytes
            : kMaximumPendingBytes;
        if (chunk.dataBytes > limit - decodeState.pendingCodebook.size()) {
            return fail("partial codebook accumulator exceeds its size limit");
        }
        if (decodeState.pendingParts >= codebookParts) {
            return fail("too many partial codebook chunks were supplied");
        }

        const auto bytes = chunkBytes(chunk);
        decodeState.pendingCodebook.insert(
            decodeState.pendingCodebook.end(),
            bytes.begin(),
            bytes.end()
        );
        decodeState.pendingKind = kind;
        ++decodeState.pendingParts;
        return true;
    }

    bool palettePixel(
        std::uint8_t paletteIndex,
        const DecodeState& decodeState,
        std::array<std::uint8_t, 4U>& pixel
    ) {
        if (paletteIndex >= paletteColorCount
            || !decodeState.paletteDefined[paletteIndex]) {
            return fail(
                "frame references undefined palette index "
                + std::to_string(paletteIndex)
            );
        }
        pixel = decodeState.palette[paletteIndex];
        return true;
    }

    bool renderPalettedFrame(
        std::span<const std::uint8_t> map,
        DecodeState& decodeState
    ) {
        if (map.size() != frameMapBytes) {
            return fail("vector-pointer table does not cover every frame block");
        }
        decodeState.frameBgra.assign(frameBytes, 0U);
        const std::size_t bytesPerVector =
            static_cast<std::size_t>(blockWidth)
            * static_cast<std::size_t>(blockHeight);
        const std::size_t blocksAcross = width / blockWidth;

        for (std::size_t block = 0; block < blockCount; ++block) {
            std::uint8_t low = 0U;
            std::uint8_t high = 0U;
            bool solid = false;
            std::uint8_t solidIndex = 0U;
            std::size_t vectorIndex = 0U;

            if (version == 1U) {
                low = map[block * 2U];
                high = map[block * 2U + 1U];
                solid = high == 0xFFU;
                solidIndex = static_cast<std::uint8_t>(255U - low);
                vectorIndex = (
                    static_cast<std::size_t>(high) << 8U | low
                ) >> 3U;
            } else {
                low = map[block];
                high = map[blockCount + block];
                if (blockHeight == 2U) {
                    if (high > 0x0FU) {
                        return fail("VQA2 4x2 vector index exceeds 12 bits");
                    }
                    solid = high == 0x0FU;
                } else {
                    solid = high == 0xFFU;
                }
                solidIndex = low;
                vectorIndex = (static_cast<std::size_t>(high) << 8U) | low;
            }

            if (!solid) {
                if (vectorIndex > std::numeric_limits<std::size_t>::max()
                        / bytesPerVector
                    || vectorIndex * bytesPerVector
                            > decodeState.codebook.size()
                    || bytesPerVector > decodeState.codebook.size()
                            - vectorIndex * bytesPerVector) {
                    return fail(
                        "frame references codebook vector "
                        + std::to_string(vectorIndex) + " outside CBF data"
                    );
                }
            }

            const std::size_t blockX = block % blocksAcross;
            const std::size_t blockY = block / blocksAcross;
            for (std::size_t y = 0; y < blockHeight; ++y) {
                for (std::size_t x = 0; x < blockWidth; ++x) {
                    const std::uint8_t paletteIndex = solid
                        ? solidIndex
                        : decodeState.codebook[
                            vectorIndex * bytesPerVector
                                + y * blockWidth + x
                        ];
                    std::array<std::uint8_t, 4U> pixel{};
                    if (!palettePixel(paletteIndex, decodeState, pixel)) {
                        return false;
                    }
                    const std::size_t pixelX = blockX * blockWidth + x;
                    const std::size_t pixelY = blockY * blockHeight + y;
                    const std::size_t output =
                        (pixelY * width + pixelX) * 4U;
                    std::copy(pixel.begin(), pixel.end(),
                        decodeState.frameBgra.begin()
                            + static_cast<std::ptrdiff_t>(output));
                }
            }
        }
        return true;
    }

    bool writeHighColorBlock(
        std::size_t blockX,
        std::size_t blockY,
        std::size_t vectorIndex,
        DecodeState& decodeState
    ) {
        const std::size_t pixelsPerVector =
            static_cast<std::size_t>(blockWidth)
            * static_cast<std::size_t>(blockHeight);
        const std::size_t bytesPerVector = pixelsPerVector * 2U;
        if (vectorIndex > std::numeric_limits<std::size_t>::max()
                / bytesPerVector
            || vectorIndex * bytesPerVector > decodeState.codebook.size()
            || bytesPerVector > decodeState.codebook.size()
                - vectorIndex * bytesPerVector) {
            return fail(
                "VPTR references codebook vector "
                + std::to_string(vectorIndex) + " outside CBF data"
            );
        }

        const std::size_t vectorOffset = vectorIndex * bytesPerVector;
        for (std::size_t y = 0; y < blockHeight; ++y) {
            for (std::size_t x = 0; x < blockWidth; ++x) {
                const std::size_t sourcePixel = y * blockWidth + x;
                const std::size_t sourceOffset =
                    vectorOffset + sourcePixel * 2U;
                const std::uint16_t packed =
                    static_cast<std::uint16_t>(decodeState.codebook[sourceOffset])
                    | (static_cast<std::uint16_t>(
                        decodeState.codebook[sourceOffset + 1U]
                    ) << 8U);
                const std::size_t pixelX = blockX * blockWidth + x;
                const std::size_t pixelY = blockY * blockHeight + y;
                const std::size_t output = (pixelY * width + pixelX) * 4U;
                decodeState.frameBgra[output] =
                    static_cast<std::uint8_t>((packed & 0x001FU) << 3U);
                decodeState.frameBgra[output + 1U] =
                    static_cast<std::uint8_t>((packed & 0x03E0U) >> 2U);
                decodeState.frameBgra[output + 2U] =
                    static_cast<std::uint8_t>((packed & 0x7C00U) >> 7U);
                decodeState.frameBgra[output + 3U] = 255U;
            }
        }
        return true;
    }

    bool renderHighColorFrame(
        std::span<const std::uint8_t> instructions,
        DecodeState& decodeState
    ) {
        if (instructions.empty()) {
            return fail("VPTR instruction stream is empty");
        }
        if (decodeState.frameBgra.size() != frameBytes) {
            decodeState.frameBgra.assign(frameBytes, 0U);
        }

        const std::size_t blocksAcross = width / blockWidth;
        const std::size_t blocksDown = height / blockHeight;
        std::size_t cursor = 0U;

        for (std::size_t blockY = 0; blockY < blocksDown; ++blockY) {
            std::size_t blockX = 0U;
            while (blockX < blocksAcross) {
                if (instructions.size() - cursor < 2U) {
                    return fail("VPTR ends before all blocks are described");
                }
                const std::uint16_t command =
                    static_cast<std::uint16_t>(instructions[cursor])
                    | (static_cast<std::uint16_t>(instructions[cursor + 1U])
                        << 8U);
                cursor += 2U;
                const std::uint32_t type = command >> 13U;
                const std::uint32_t parameter = command & 0x1FFFU;
                const std::size_t remaining = blocksAcross - blockX;

                if (type == 0U) {
                    if (parameter > remaining) {
                        return fail("VPTR skip crosses a block row boundary");
                    }
                    blockX += parameter;
                    continue;
                }

                std::size_t vectorIndex = parameter;
                std::size_t count = 1U;
                bool explicitByteVectors = false;
                if (type == 1U || type == 2U) {
                    vectorIndex = parameter & 0xFFU;
                    count = ((parameter >> 8U) + 1U) * 2U;
                    if (type == 2U) {
                        ++count;
                        explicitByteVectors = true;
                    }
                } else if (type == 5U || type == 6U) {
                    if (cursor >= instructions.size()) {
                        return fail("VPTR repeat command is missing its count");
                    }
                    count = instructions[cursor++];
                } else if (type == 7U) {
                    return fail("VPTR contains reserved command type 7");
                }

                if (count > remaining) {
                    return fail("VPTR vector run crosses a block row boundary");
                }
                for (std::size_t i = 0; i < count; ++i) {
                    if (explicitByteVectors && i != 0U) {
                        if (cursor >= instructions.size()) {
                            return fail("VPTR explicit-vector run is truncated");
                        }
                        vectorIndex = instructions[cursor++];
                    }
                    if (!writeHighColorBlock(
                            blockX,
                            blockY,
                            vectorIndex,
                            decodeState
                        )) {
                        return false;
                    }
                    ++blockX;
                }
            }
        }

        if (cursor != instructions.size()) {
            return fail("VPTR has trailing instructions after the frame is complete");
        }
        return true;
    }

    bool decodeVectorPointers(
        const ChunkReference& chunk,
        DecodeState& decodeState
    ) {
        std::vector<std::uint8_t> decoded;
        if (!highColor) {
            if (chunk.tag == kVpt0Tag) {
                decoded.assign(chunkBytes(chunk).begin(), chunkBytes(chunk).end());
            } else if (chunk.tag == kVptzTag) {
                std::string lcwError;
                if (!decodeLcw(
                        chunkBytes(chunk),
                        frameMapBytes,
                        decoded,
                        lcwError
                    )) {
                    return fail("VPTZ " + lcwError);
                }
            } else {
                return fail("paletted VQA frame requires VPT0 or VPTZ");
            }
            return renderPalettedFrame(decoded, decodeState);
        }

        if (chunk.tag == kVptrTag) {
            if (chunk.dataBytes > maximumPointerBytes) {
                return fail("VPTR exceeds the instruction-stream size limit");
            }
            decoded.assign(chunkBytes(chunk).begin(), chunkBytes(chunk).end());
        } else if (chunk.tag == kVprzTag) {
            std::string lcwError;
            if (!decodeLcw(
                    chunkBytes(chunk),
                    maximumPointerBytes,
                    decoded,
                    lcwError
                )) {
                return fail("VPRZ " + lcwError);
            }
        } else {
            return fail("high-color VQA frame requires VPTR or VPRZ");
        }
        return renderHighColorFrame(decoded, decodeState);
    }

    bool decodeVqfr(
        std::size_t begin,
        std::size_t end,
        DecodeState& decodeState
    ) {
        FrameSubchunks chunks;
        if (!parseSubchunks(begin, end, false, chunks)) {
            return false;
        }
        if (!hasChunk(chunks.vectorPointers)) {
            return fail("VQFR contains no vector-pointer chunk");
        }
        if (hasChunk(chunks.palette)
            && !applyPalette(chunks.palette, decodeState)) {
            return false;
        }
        if (hasChunk(chunks.fullCodebook)) {
            std::vector<std::uint8_t> decoded;
            if (!decodeCodebookChunk(chunks.fullCodebook, decoded)) {
                return false;
            }
            decodeState.codebook = std::move(decoded);
        }
        if (!decodeVectorPointers(chunks.vectorPointers, decodeState)) {
            return false;
        }
        if (hasChunk(chunks.partialCodebook)
            && !appendPartialCodebook(chunks.partialCodebook, decodeState)) {
            return false;
        }
        return true;
    }

    bool decodeVqfl(
        std::size_t begin,
        std::size_t end,
        DecodeState& decodeState
    ) {
        FrameSubchunks chunks;
        if (!parseSubchunks(begin, end, true, chunks)) {
            return false;
        }
        if (!hasChunk(chunks.fullCodebook)) {
            return fail("VQFL contains no full codebook");
        }
        std::vector<std::uint8_t> decoded;
        if (!decodeCodebookChunk(chunks.fullCodebook, decoded)) {
            return false;
        }
        decodeState.codebook = std::move(decoded);
        return true;
    }

    bool decodeFrame(std::uint32_t frameIndex, DecodeState& decodeState) {
        if (frameIndex >= frameOffsets.size()) {
            return fail("frame index is outside FINF");
        }
        if (!applyReadyPartialCodebook(decodeState)) {
            return false;
        }

        const std::size_t begin = frameOffsets[frameIndex];
        const std::size_t end = frameIndex + 1U < frameOffsets.size()
            ? frameOffsets[frameIndex + 1U]
            : formEnd;
        std::size_t cursor = begin;
        bool sawFrame = false;

        while (cursor < end) {
            if (source[cursor] == 0U) {
                ++cursor;
                continue;
            }
            if (end - cursor < 8U) {
                return fail("frame range ends in a truncated top-level chunk");
            }
            const std::uint32_t tag = readBig32(source, cursor);
            const std::uint32_t size = readBig32(source, cursor + 4U);
            const std::size_t dataOffset = cursor + 8U;
            std::size_t next = 0U;
            if (!paddedChunkEnd(dataOffset, size, end, next)) {
                return fail(tagName(tag) + " exceeds its FINF frame range");
            }

            if (tag == kVqflTag) {
                if (sawFrame) {
                    return fail("VQFL appears after VQFR in one frame range");
                }
                if (!decodeVqfl(
                        dataOffset,
                        dataOffset + static_cast<std::size_t>(size),
                        decodeState
                    )) {
                    return false;
                }
            } else if (tag == kVqfrTag) {
                if (sawFrame) {
                    return fail("FINF frame range contains multiple VQFR chunks");
                }
                if (!decodeVqfr(
                        dataOffset,
                        dataOffset + static_cast<std::size_t>(size),
                        decodeState
                    )) {
                    return false;
                }
                sawFrame = true;
            }
            cursor = next;
        }

        if (!sawFrame) {
            return fail("FINF frame range contains no VQFR chunk");
        }
        return true;
    }

    bool parseContainer() {
        if (source.size() < 62U) {
            return fail("VQA data is shorter than FORM/WVQA/VQHD");
        }
        if (readBig32(source, 0U) != kFormTag) {
            return fail("VQA data does not start with FORM");
        }
        const std::uint32_t formBytes = readBig32(source, 4U);
        if (static_cast<std::size_t>(formBytes) > source.size() - 8U) {
            return fail("FORM length exceeds the supplied memory buffer");
        }
        formEnd = 8U + static_cast<std::size_t>(formBytes);
        if (formEnd < 62U) {
            return fail("FORM is too short for a VQA header");
        }
        if (readBig32(source, 8U) != kWvqaTag) {
            return fail("FORM does not contain WVQA");
        }
        if (readBig32(source, 12U) != kVqhdTag) {
            return fail("WVQA does not begin with VQHD");
        }
        if (readBig32(source, 16U) != 42U) {
            return fail("VQHD must contain the fixed 42-byte header");
        }

        version = readLittle16(source, 20U);
        flags = readLittle16(source, 22U);
        frameCount = readLittle16(source, 24U);
        width = readLittle16(source, 26U);
        height = readLittle16(source, 28U);
        blockWidth = source[30U];
        blockHeight = source[31U];
        frameRate = source[32U];
        codebookParts = source[33U];
        paletteColorCount = readLittle16(source, 34U);
        highColor = (flags & 0x10U) != 0U || paletteColorCount == 0U;

        if (version < 1U || version > 3U) {
            return fail("VQHD declares unsupported VQA version "
                + std::to_string(version));
        }
        if (!highColor && version == 3U) {
            return fail("VQA3 cannot use a paletted frame format");
        }
        if (!highColor
            && (paletteColorCount == 0U || paletteColorCount > 256U)) {
            return fail("VQHD palette color count is outside 1..256");
        }
        if (frameCount == 0U) {
            return fail("VQHD declares zero frames");
        }
        if (width == 0U || height == 0U
            || width > kMaximumDimension || height > kMaximumDimension) {
            return fail("VQHD dimensions are outside the decoder limit");
        }
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(width) * height;
        if (pixels > kMaximumPixels) {
            return fail("VQHD pixel count exceeds the decoder limit");
        }
        if (frameRate == 0U || frameRate > kMaximumFrameRate) {
            return fail("VQHD frame rate is outside 1..120");
        }
        if (blockWidth != 4U || (blockHeight != 2U && blockHeight != 4U)) {
            return fail("VQA vectors must be 4x2 or 4x4 pixels");
        }
        if (width % blockWidth != 0U || height % blockHeight != 0U) {
            return fail("VQHD dimensions are not multiples of the vector size");
        }
        if (codebookParts > 64U) {
            return fail("VQHD split-codebook count exceeds 64 parts");
        }

        blockCount = static_cast<std::size_t>(width / blockWidth)
            * static_cast<std::size_t>(height / blockHeight);
        frameMapBytes = blockCount * 2U;
        frameBytes = static_cast<std::size_t>(pixels) * 4U;
        if (blockCount > (std::numeric_limits<std::size_t>::max() - 1024U) / 4U) {
            return fail("VQHD block count overflows instruction limits");
        }
        maximumPointerBytes = blockCount * 4U + 1024U;

        std::size_t cursor = 62U;
        bool foundFinf = false;
        std::size_t finfData = 0U;
        std::size_t afterFinf = 0U;
        while (cursor < formEnd) {
            if (formEnd - cursor < 8U) {
                return fail("VQA header area ends in a truncated chunk");
            }
            const std::uint32_t tag = readBig32(source, cursor);
            const std::uint32_t size = readBig32(source, cursor + 4U);
            const std::size_t dataOffset = cursor + 8U;
            std::size_t next = 0U;
            if (!paddedChunkEnd(dataOffset, size, formEnd, next)) {
                return fail(tagName(tag) + " header chunk exceeds FORM");
            }
            if (tag == kFinfTag) {
                const std::size_t expected =
                    static_cast<std::size_t>(frameCount) * 4U;
                if (size != expected) {
                    return fail("FINF size does not match the VQHD frame count");
                }
                foundFinf = true;
                finfData = dataOffset;
                afterFinf = next;
                break;
            }
            cursor = next;
        }
        if (!foundFinf) {
            return fail("WVQA contains no FINF chunk");
        }

        frameOffsets.clear();
        frameOffsets.reserve(frameCount);
        for (std::uint32_t i = 0U; i < frameCount; ++i) {
            const std::uint32_t encoded = readLittle32(
                source,
                finfData + static_cast<std::size_t>(i) * 4U
            ) & 0x3FFFFFFFU;
            const std::uint64_t decoded64 =
                static_cast<std::uint64_t>(encoded) * 2U;
            if (decoded64 > formEnd) {
                return fail("FINF frame offset exceeds FORM");
            }
            const std::size_t decoded = static_cast<std::size_t>(decoded64);
            if (decoded < afterFinf) {
                return fail("FINF frame offset points into the VQA header");
            }
            if (!frameOffsets.empty() && decoded <= frameOffsets.back()) {
                return fail("FINF frame offsets are not strictly increasing");
            }
            frameOffsets.push_back(decoded);
        }
        if (frameOffsets.back() >= formEnd) {
            return fail("last FINF frame offset has no chunk data");
        }
        return true;
    }

    bool load(std::span<const std::uint8_t> bytes) {
        clearData(true);
        if (bytes.empty()) {
            return fail("VQA memory buffer is empty");
        }
        if (bytes.size() > kMaximumInputBytes) {
            return fail("VQA memory buffer exceeds the 256 MiB limit");
        }

        try {
            source.assign(bytes.begin(), bytes.end());
            if (!parseContainer()) {
                return false;
            }
            containerReady = true;
            return resetPlayback();
        } catch (const std::bad_alloc&) {
            loaded = false;
            return fail("not enough memory to load the VQA clip");
        }
    }

    bool resetPlayback() {
        error.clear();
        loaded = false;
        currentFrame = kNoCurrentFrame;
        if (!containerReady) {
            return fail("no VQA container is loaded");
        }

        try {
            DecodeState fresh;
            fresh.frameBgra.assign(frameBytes, 0U);
            if (!decodeFrame(0U, fresh)) {
                return false;
            }
            state = std::move(fresh);
            currentFrame = 0U;
            loaded = true;
            return true;
        } catch (const std::bad_alloc&) {
            return fail("not enough memory to reset VQA playback");
        }
    }

    bool advancePlayback() {
        error.clear();
        if (!loaded) {
            return fail("no decoded VQA frame is available");
        }
        if (currentFrame + 1U >= frameCount) {
            return false;
        }

        try {
            DecodeState nextState = state;
            const std::uint32_t nextFrame = currentFrame + 1U;
            if (!decodeFrame(nextFrame, nextState)) {
                error = "frame " + std::to_string(nextFrame) + ": " + error;
                return false;
            }
            state = std::move(nextState);
            currentFrame = nextFrame;
            return true;
        } catch (const std::bad_alloc&) {
            return fail("not enough memory to decode the next VQA frame");
        }
    }
};

CampaignPresentationDecoder::CampaignPresentationDecoder()
    : impl_(std::make_unique<Impl>()) {
}

CampaignPresentationDecoder::~CampaignPresentationDecoder() = default;

CampaignPresentationDecoder::CampaignPresentationDecoder(
    CampaignPresentationDecoder&&
) noexcept = default;

CampaignPresentationDecoder& CampaignPresentationDecoder::operator=(
    CampaignPresentationDecoder&&
) noexcept = default;

bool CampaignPresentationDecoder::loadFromMemory(
    std::span<const std::uint8_t> bytes
) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->load(bytes);
}

bool CampaignPresentationDecoder::loadFromMemory(
    const std::vector<std::uint8_t>& bytes
) {
    return loadFromMemory(std::span<const std::uint8_t>(bytes));
}

bool CampaignPresentationDecoder::loadFromMemory(
    const std::uint8_t* bytes,
    std::size_t byteCount
) {
    if (bytes == nullptr && byteCount != 0U) {
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
        }
        impl_->clearData(true);
        return impl_->fail("VQA memory pointer is null");
    }
    return loadFromMemory(std::span<const std::uint8_t>(bytes, byteCount));
}

void CampaignPresentationDecoder::clear() {
    if (impl_) {
        impl_->clearData(true);
    }
}

bool CampaignPresentationDecoder::isLoaded() const noexcept {
    return impl_ && impl_->loaded;
}

bool CampaignPresentationDecoder::isHighColor() const noexcept {
    return impl_ && impl_->highColor;
}

std::uint32_t CampaignPresentationDecoder::width() const noexcept {
    return impl_ ? impl_->width : 0U;
}

std::uint32_t CampaignPresentationDecoder::height() const noexcept {
    return impl_ ? impl_->height : 0U;
}

std::uint32_t CampaignPresentationDecoder::frameRate() const noexcept {
    return impl_ ? impl_->frameRate : 0U;
}

std::uint32_t CampaignPresentationDecoder::frameCount() const noexcept {
    return impl_ ? impl_->frameCount : 0U;
}

std::uint32_t CampaignPresentationDecoder::currentFrameIndex() const noexcept {
    return impl_ && impl_->loaded ? impl_->currentFrame : 0U;
}

bool CampaignPresentationDecoder::hasNextFrame() const noexcept {
    return impl_ && impl_->loaded
        && impl_->currentFrame + 1U < impl_->frameCount;
}

const std::vector<std::uint8_t>&
CampaignPresentationDecoder::currentFrameBgra() const noexcept {
    static const std::vector<std::uint8_t> empty;
    return impl_ && impl_->loaded ? impl_->state.frameBgra : empty;
}

bool CampaignPresentationDecoder::advance() {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->advancePlayback();
}

bool CampaignPresentationDecoder::reset() {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->resetPlayback();
}

const std::string& CampaignPresentationDecoder::errorMessage() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->error : empty;
}
