#include "CampaignPresentationDecoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Campaign presentation decoder smoke failure: "
                  << message << '\n';
    }
    return condition;
}

void appendTag(Bytes& output, std::string_view tag) {
    output.insert(output.end(), tag.begin(), tag.end());
}

void appendLittle16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendLittle32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void appendBig32(Bytes& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void writeLittle16(Bytes& output, std::size_t offset, std::uint16_t value) {
    output[offset] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void writeLittle32(Bytes& output, std::size_t offset, std::uint32_t value) {
    output[offset] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

void writeBig32(Bytes& output, std::size_t offset, std::uint32_t value) {
    output[offset] = static_cast<std::uint8_t>(value >> 24U);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 3U] = static_cast<std::uint8_t>(value);
}

void appendChunk(Bytes& output, std::string_view tag, const Bytes& payload) {
    appendTag(output, tag);
    appendBig32(output, static_cast<std::uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    if ((payload.size() & 1U) != 0U) {
        output.push_back(0U);
    }
}

Bytes chunk(std::string_view tag, const Bytes& payload) {
    Bytes result;
    appendChunk(result, tag, payload);
    return result;
}

Bytes join(const std::vector<Bytes>& pieces) {
    Bytes result;
    for (const Bytes& piece : pieces) {
        result.insert(result.end(), piece.begin(), piece.end());
    }
    return result;
}

Bytes lcwLiteral(const Bytes& decoded, bool relative = false) {
    Bytes result;
    if (relative) {
        result.push_back(0U);
    }
    std::size_t offset = 0U;
    while (offset < decoded.size()) {
        const std::size_t count = std::min<std::size_t>(
            63U,
            decoded.size() - offset
        );
        result.push_back(static_cast<std::uint8_t>(0x80U | count));
        result.insert(
            result.end(),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset + count)
        );
        offset += count;
    }
    result.push_back(0x80U);
    return result;
}

Bytes lcwFilledCodebook(std::uint8_t first, std::uint8_t second) {
    Bytes result;
    result.insert(result.end(), {0xFEU, 8U, 0U, first});
    result.insert(result.end(), {0xFEU, 8U, 0U, second});
    result.push_back(0x80U);
    return result;
}

Bytes lcwCopiedCodebook() {
    // Decode four literal palette indexes, overlap-copy them once, then copy
    // the complete first vector through an absolute reference.
    Bytes result{0x84U, 1U, 2U, 1U, 2U};
    result.insert(result.end(), {0x10U, 4U});
    result.insert(result.end(), {0xC5U, 0U, 0U});
    result.push_back(0x80U);
    return result;
}

Bytes makeVqfr(const std::vector<Bytes>& subchunks) {
    return chunk("VQFR", join(subchunks));
}

Bytes makeVqfl(const std::vector<Bytes>& subchunks) {
    return chunk("VQFL", join(subchunks));
}

struct FixtureHeader {
    std::uint16_t version = 2U;
    std::uint16_t flags = 0U;
    std::uint16_t width = 8U;
    std::uint16_t height = 2U;
    std::uint8_t blockWidth = 4U;
    std::uint8_t blockHeight = 2U;
    std::uint8_t frameRate = 15U;
    std::uint8_t codebookParts = 0U;
    std::uint16_t colors = 4U;
};

Bytes makeVqa(
    const FixtureHeader& header,
    const std::vector<Bytes>& frameTopLevelChunks
) {
    Bytes output;
    appendTag(output, "FORM");
    appendBig32(output, 0U);
    appendTag(output, "WVQA");

    Bytes vqhd;
    appendLittle16(vqhd, header.version);
    appendLittle16(vqhd, header.flags);
    appendLittle16(
        vqhd,
        static_cast<std::uint16_t>(frameTopLevelChunks.size())
    );
    appendLittle16(vqhd, header.width);
    appendLittle16(vqhd, header.height);
    vqhd.push_back(header.blockWidth);
    vqhd.push_back(header.blockHeight);
    vqhd.push_back(header.frameRate);
    vqhd.push_back(header.codebookParts);
    appendLittle16(vqhd, header.colors);
    appendLittle16(vqhd, 256U);
    appendLittle16(vqhd, 0U);
    appendLittle32(vqhd, 0U);
    appendLittle16(vqhd, 0U);
    vqhd.push_back(0U);
    vqhd.push_back(0U);
    appendLittle32(vqhd, 0U);
    appendLittle16(vqhd, 0U);
    appendLittle32(vqhd, 256U * 1024U);
    appendLittle32(vqhd, 0U);
    appendChunk(output, "VQHD", vqhd);

    Bytes finf(frameTopLevelChunks.size() * 4U, 0U);
    const std::size_t finfTagOffset = output.size();
    appendChunk(output, "FINF", finf);
    const std::size_t finfDataOffset = finfTagOffset + 8U;

    for (std::size_t i = 0; i < frameTopLevelChunks.size(); ++i) {
        if ((output.size() & 1U) != 0U) {
            output.push_back(0U);
        }
        writeLittle32(
            output,
            finfDataOffset + i * 4U,
            static_cast<std::uint32_t>(output.size() / 2U)
        );
        output.insert(
            output.end(),
            frameTopLevelChunks[i].begin(),
            frameTopLevelChunks[i].end()
        );
    }
    writeBig32(
        output,
        4U,
        static_cast<std::uint32_t>(output.size() - 8U)
    );
    return output;
}

Bytes palettedFixture() {
    const Bytes palette{
        0U, 0U, 0U,
        63U, 0U, 0U,
        0U, 63U, 0U,
        0U, 0U, 63U
    };
    const Bytes bookA{
        1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U,
        2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U
    };
    const Bytes bookB{
        3U, 3U, 3U, 3U, 3U, 3U, 3U, 3U,
        1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U
    };
    const Bytes mapForward{0U, 1U, 0U, 0U};
    const Bytes mapReverse{1U, 0U, 0U, 0U};

    const Bytes frame0 = makeVqfr({
        chunk("CPL0", palette),
        chunk("CBF0", bookA),
        chunk("CBP0", bookB),
        chunk("VPTZ", lcwLiteral(mapForward))
    });
    const Bytes frame1 = makeVqfr({
        chunk("CBPZ", lcwFilledCodebook(2U, 3U)),
        chunk("VPT0", mapReverse)
    });
    const Bytes frame2 = makeVqfr({
        chunk("VPTZ", lcwLiteral(mapForward))
    });
    const Bytes frame3 = makeVqfr({
        chunk("CBFZ", lcwCopiedCodebook()),
        chunk("VPTZ", lcwLiteral(mapForward))
    });

    FixtureHeader header;
    header.codebookParts = 1U;
    return makeVqa(header, {frame0, frame1, frame2, frame3});
}

void appendPackedColorVector(
    Bytes& codebook,
    std::uint16_t packed,
    std::size_t pixels
) {
    for (std::size_t i = 0; i < pixels; ++i) {
        appendLittle16(codebook, packed);
    }
}

Bytes highColorFixture() {
    Bytes codebook;
    appendPackedColorVector(codebook, 0x7C00U, 16U);
    appendPackedColorVector(codebook, 0x03E0U, 16U);
    appendPackedColorVector(codebook, 0x001FU, 16U);

    Bytes firstPointers;
    appendLittle16(firstPointers, 0x6000U);
    appendLittle16(firstPointers, 0x6001U);
    Bytes secondPointers;
    appendLittle16(secondPointers, 0x0001U);
    appendLittle16(secondPointers, 0x6002U);
    Bytes thirdPointers;
    appendLittle16(thirdPointers, 0x6001U);
    appendLittle16(thirdPointers, 0x0001U);

    const Bytes frame0 = join({
        makeVqfl({chunk("CBFZ", lcwLiteral(codebook, true))}),
        makeVqfr({chunk("VPTR", firstPointers)})
    });
    const Bytes frame1 = makeVqfr({chunk("VPTR", secondPointers)});
    const Bytes frame2 = makeVqfr({
        chunk("VPRZ", lcwLiteral(thirdPointers, true))
    });

    FixtureHeader header;
    header.version = 3U;
    header.flags = 0x10U;
    header.width = 8U;
    header.height = 4U;
    header.blockHeight = 4U;
    header.codebookParts = 0U;
    header.colors = 0U;
    return makeVqa(header, {frame0, frame1, frame2});
}

std::size_t findTag(const Bytes& bytes, std::string_view tag) {
    const auto found = std::search(
        bytes.begin(),
        bytes.end(),
        tag.begin(),
        tag.end()
    );
    return found == bytes.end()
        ? bytes.size()
        : static_cast<std::size_t>(found - bytes.begin());
}

bool pixelIs(
    const CampaignPresentationDecoder& decoder,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t blue,
    std::uint8_t green,
    std::uint8_t red,
    std::uint8_t alpha = 255U
) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * decoder.width() + x) * 4U;
    const auto& frame = decoder.currentFrameBgra();
    return offset + 4U <= frame.size()
        && frame[offset] == blue
        && frame[offset + 1U] == green
        && frame[offset + 2U] == red
        && frame[offset + 3U] == alpha;
}

bool runPalettedSmoke() {
    bool success = true;
    const Bytes fixture = palettedFixture();
    CampaignPresentationDecoder decoder;
    success &= expect(
        decoder.loadFromMemory(fixture),
        "paletted fixture did not load: " + decoder.errorMessage()
    );
    success &= expect(
        decoder.isLoaded() && !decoder.isHighColor()
            && decoder.width() == 8U && decoder.height() == 2U
            && decoder.frameRate() == 15U && decoder.frameCount() == 4U
            && decoder.currentFrameIndex() == 0U
            && decoder.currentFrameBgra().size() == 8U * 2U * 4U,
        "paletted metadata or initial frame state is wrong"
    );
    success &= expect(
        pixelIs(decoder, 0U, 0U, 0U, 0U, 252U)
            && pixelIs(decoder, 7U, 1U, 0U, 252U, 0U),
        "CPL0/CBF0/VPTZ did not render frame zero in BGRA order"
    );

    success &= expect(
        decoder.advance(),
        "could not advance to raw partial-codebook frame: "
            + decoder.errorMessage()
    );
    success &= expect(
        decoder.currentFrameIndex() == 1U
            && pixelIs(decoder, 0U, 0U, 0U, 0U, 252U)
            && pixelIs(decoder, 7U, 1U, 252U, 0U, 0U),
        "CBP0 delayed application or VPT0 rendering is wrong"
    );

    success &= expect(
        decoder.advance(),
        "could not advance to compressed partial-codebook frame: "
            + decoder.errorMessage()
    );
    success &= expect(
        pixelIs(decoder, 0U, 0U, 0U, 252U, 0U)
            && pixelIs(decoder, 7U, 1U, 252U, 0U, 0U),
        "CBPZ LCW fill decoding or delayed application is wrong"
    );

    success &= expect(
        decoder.advance(),
        "could not advance to CBFZ frame: " + decoder.errorMessage()
    );
    success &= expect(
        pixelIs(decoder, 0U, 0U, 0U, 0U, 252U)
            && pixelIs(decoder, 1U, 0U, 0U, 252U, 0U)
            && pixelIs(decoder, 7U, 1U, 0U, 252U, 0U),
        "CBFZ LCW copy commands did not reconstruct vector data"
    );
    success &= expect(
        !decoder.hasNextFrame() && !decoder.advance()
            && decoder.errorMessage().empty()
            && decoder.currentFrameIndex() == 3U,
        "normal end-of-stream is not distinguishable from an error"
    );

    success &= expect(
        decoder.reset(),
        "paletted reset failed: " + decoder.errorMessage()
    );
    success &= expect(
        decoder.currentFrameIndex() == 0U
            && pixelIs(decoder, 0U, 0U, 0U, 0U, 252U)
            && pixelIs(decoder, 7U, 1U, 0U, 252U, 0U),
        "reset did not restore palette/codebook/partial state"
    );
    return success;
}

bool runHighColorSmoke() {
    bool success = true;
    const Bytes fixture = highColorFixture();
    CampaignPresentationDecoder decoder;
    success &= expect(
        decoder.loadFromMemory(fixture),
        "high-color fixture did not load: " + decoder.errorMessage()
    );
    success &= expect(
        decoder.isHighColor() && decoder.frameCount() == 3U
            && pixelIs(decoder, 0U, 0U, 0U, 0U, 248U)
            && pixelIs(decoder, 7U, 3U, 0U, 248U, 0U),
        "VQFL/CBFZ/VPTR high-color frame is wrong"
    );
    success &= expect(
        decoder.advance(),
        "high-color delta frame failed: " + decoder.errorMessage()
    );
    success &= expect(
        pixelIs(decoder, 0U, 0U, 0U, 0U, 248U)
            && pixelIs(decoder, 7U, 3U, 248U, 0U, 0U),
        "VPTR skip did not preserve the previous frame block"
    );
    success &= expect(
        decoder.advance(),
        "compressed high-color delta frame failed: "
            + decoder.errorMessage()
    );
    success &= expect(
        pixelIs(decoder, 0U, 0U, 0U, 248U, 0U)
            && pixelIs(decoder, 7U, 3U, 248U, 0U, 0U),
        "VPRZ or high-color delta persistence is wrong"
    );
    success &= expect(
        decoder.reset()
            && pixelIs(decoder, 0U, 0U, 0U, 0U, 248U)
            && pixelIs(decoder, 7U, 3U, 0U, 248U, 0U),
        "high-color reset did not clear delta history"
    );
    return success;
}

bool runMalformedSmoke() {
    bool success = true;
    CampaignPresentationDecoder decoder;
    success &= expect(
        !decoder.loadFromMemory(Bytes{}) && !decoder.errorMessage().empty(),
        "empty input was accepted or produced no diagnostic"
    );

    Bytes truncated = palettedFixture();
    truncated.pop_back();
    success &= expect(
        !decoder.loadFromMemory(truncated)
            && decoder.errorMessage().find("FORM") != std::string::npos,
        "truncated FORM was not rejected at its declared boundary"
    );

    Bytes oversized = palettedFixture();
    writeLittle16(oversized, 26U, 4097U);
    success &= expect(
        !decoder.loadFromMemory(oversized)
            && decoder.errorMessage().find("dimensions") != std::string::npos,
        "oversized VQHD dimensions were accepted"
    );

    Bytes badFinf = palettedFixture();
    const std::size_t finf = findTag(badFinf, "FINF");
    success &= expect(finf != badFinf.size(), "fixture FINF was not found");
    if (finf != badFinf.size()) {
        writeLittle32(badFinf, finf + 8U, 0U);
        success &= expect(
            !decoder.loadFromMemory(badFinf)
                && decoder.errorMessage().find("FINF") != std::string::npos,
            "FINF offset into the header was accepted"
        );
    }

    Bytes badLcw = palettedFixture();
    const std::size_t vptz = findTag(badLcw, "VPTZ");
    success &= expect(vptz != badLcw.size(), "fixture VPTZ was not found");
    if (vptz != badLcw.size()) {
        badLcw[vptz + 8U] = 0U;
        success &= expect(
            !decoder.loadFromMemory(badLcw)
                && decoder.errorMessage().find("LCW") != std::string::npos,
            "out-of-range LCW reference was accepted"
        );
    }

    success &= expect(
        !decoder.loadFromMemory(nullptr, 5U)
            && decoder.errorMessage().find("null") != std::string::npos,
        "null memory pointer did not fail safely"
    );
    return success;
}

} // namespace

int main() {
    const bool success = runPalettedSmoke()
        && runHighColorSmoke()
        && runMalformedSmoke();
    if (success) {
        std::cout << "Campaign presentation decoder smoke passed\n";
    }
    return success ? 0 : 1;
}
