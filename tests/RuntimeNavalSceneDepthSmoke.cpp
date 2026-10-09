#include "RuntimeBuildingDepth.h"
#include "RuntimeBuildingDepthOracle.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using RuntimeBuildingDepthOracle::Bytes;
using RuntimeBuildingDepthOracle::little;
constexpr const char* retailSha =
    "7b8a068535d6af06845edf95ae829b113d00c02909330e16f197426cd7db94b6";

void require(bool condition, const std::string& message, int& checks) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}

std::size_t peOffset(const Bytes& bytes, std::uint32_t address) {
    const auto pe = little(bytes, 0x3c, 4);
    if (little(bytes, pe, 4) != 0x4550 || little(bytes, pe + 24, 2) != 0x10b)
        throw std::runtime_error("retail PE32 header differs");
    const auto imageBase = little(bytes, pe + 24 + 28, 4);
    if (address < imageBase) throw std::runtime_error("address precedes retail image");
    const auto rva = address - imageBase;
    const auto sections = pe + 24 + little(bytes, pe + 20, 2);
    for (std::uint32_t index = 0; index < little(bytes, pe + 6, 2); ++index) {
        const auto section = sections + index * 40;
        const auto start = little(bytes, section + 12, 4);
        const auto size = little(bytes, section + 16, 4);
        if (rva >= start && rva - start < size)
            return little(bytes, section + 20, 4) + rva - start;
    }
    throw std::runtime_error("retail address is outside file-backed sections");
}

void requireCode(const Bytes& bytes, std::uint32_t address,
    std::initializer_list<unsigned char> expected, int& checks) {
    const auto begin = peOffset(bytes, address);
    require(begin + expected.size() <= bytes.size()
        && std::equal(expected.begin(), expected.end(), bytes.begin() + begin),
        "retail instruction bytes differ at VA " + std::to_string(address), checks);
}

std::array<int, 6> pinRetailEvidence(const Bytes& bytes, int& checks) {
    const auto pointerAt = [&](std::uint32_t address) {
        return little(bytes, peOffset(bytes, address), 4);
    };
    // UnitClass's final compositor, FootClass dispatch, and Ship locomotion.
    require(pointerAt(0x7f5c70 + 0x55c) == 0x73b140
        && pointerAt(0x7f5c70 + 0x510) == 0x4daf10
        && pointerAt(0x7f5c70 + 0x444) == 0x706640
        && pointerAt(0x7f5c70 + 0x2ec) == 0x4dafc0
        && pointerAt(0x7f5c70 + 0x2f0) == 0x4db0a0,
        "UnitClass voxel/depth dispatch differs", checks);
    require(pointerAt(0x7f2d8c + 0x38) == 0x6a3ea0
        && pointerAt(0x7f2d8c + 0x3c) == 0x6a3eb0,
        "Ship locomotion depth dispatch differs", checks);
    requireCode(bytes, 0x6a3ea0, {0x33,0xc0,0xc2,0x04,0x00}, checks);
    requireCode(bytes, 0x6a3eb0,
        {0x8b,0x44,0x24,0x04,0x50,0xe8,0xf6,0x6c,0xeb,0xff,0xc2,0x04,0x00}, checks);
    requireCode(bytes, 0x55abb0, {0xb8,0x02,0x00,0x00,0x00,0xc2,0x04,0x00}, checks);
    // Drive shares the same UnitClass final compositor on the audited flat
    // ground branch. Pin its class identity and both locomotor depth methods;
    // terrain/host eligibility is exercised separately by production scenes.
    requireCode(bytes, 0x7e9a30,
        {0x41,0x27,0x58,0x4a,0x39,0x98,0xd1,0x11,
         0xb7,0x09,0x00,0xa0,0x24,0xdd,0xaf,0xd1}, checks);
    requireCode(bytes, 0x4af5c2,
        {0xc7,0x06,0x7c,0x7f,0x7e,0x00,0xc7,0x46,0x04,0xb0,0x7e,0x7e,0x00}, checks);
    require(pointerAt(0x7e7f7c + 0x0c) == 0x4b4830
        && pointerAt(0x7e7eb0 + 0x38) == 0x4b4870
        && pointerAt(0x7e7eb0 + 0x3c) == 0x4b4880,
        "Drive locomotion class/depth dispatch differs", checks);
    requireCode(bytes, 0x4b4870, {0x33,0xc0,0xc2,0x04,0x00}, checks);
    requireCode(bytes, 0x4b4880,
        {0x8b,0x44,0x24,0x04,0x50,0xe8,0x26,0x63,0x0a,0x00,0xc2,0x04,0x00}, checks);
    // Normal drawZ returns -heightPixel-1; Unit SHP additionally subtracts 2.
    requireCode(bytes, 0x704991,
        {0x5d,0x8d,0x41,0xff,0x5b,0x83,0xc4,0x5c,0xc3}, checks);
    requireCode(bytes, 0x706430, {0x83,0xc1,0xfe}, checks);
    // Normal flags choose Convert+0x98, whose line blitter is read-only in Z.
    requireCode(bytes, 0x73b217, {0xbf,0x00,0x28,0x00,0x00}, checks);
    require(pointerAt(0x81dc24) == 0x3000
        && pointerAt(0x7e56f4) == 0x494b60,
        "normal read-only blitter selection differs", checks);
    requireCode(bytes, 0x494bb9,
        {0x8b,0x5c,0x24,0x20,0x33,0xc0,0x66,0x8b,0x02,0x83,0xc2,0x02,
         0x3b,0xd8,0x7d,0x39}, checks);
    requireCode(bytes, 0x494bf9, {0x66,0x89,0x07}, checks);
    // idiv initialization and the per-row gradient accumulator, respectively.
    requireCode(bytes, 0x437579,
        {0xf7,0xf9,0x8b,0xe8,0x8b,0xc6,0x99,0x0f,0xaf,0xe9,0xf7,0xf9,
         0x2b,0xe8,0x8b,0xc6,0x99,0xf7,0xf9,0x8b,0xc7,0x2b,0xc2,0x3b,0xc7}, checks);
    requireCode(bytes, 0x437929,
        {0x8b,0x44,0x24,0x1c,0x03,0xc1,0x8b,0x4c,0x24,0x2c,0x3b,0xc1,
         0x89,0x44,0x24,0x1c,0x7c,0x0d,0x8b,0x7c,0x24,0x78,0x03,0x6f,
         0x10,0x2b,0xc1,0x89,0x44,0x24,0x1c}, checks);
    // A later VXL part that extends the union's bottom adds one native pixel.
    requireCode(bytes, 0x7069b3, {0x8d,0x74,0x01,0x01}, checks);
    std::array<int, 6> table{};
    for (std::size_t index = 0; index < table.size(); ++index)
        table[index] = static_cast<int>(little(bytes,
            peOffset(bytes, 0x817740) + index * 4, 4));
    require(table == std::array<int, 6>{{1,3,1,3,1,0}},
        "retail gradient-2 table differs", checks);
    return table;
}

// Test oracle transcribes the native registers, idiv quotient/remainder and
// row counter at 43754A..4375A4 / 437921..437944. It does not use the production
// bottom-relative closed form, nor call a production depth function.
// Here T/H are the final blit rectangle. Viewport clipping is tested separately
// as this engine's explicit un-clipped policy, not as a retail edge equivalence.
std::vector<int> nativeRows(const std::array<int, 6>& gradient,
    int top, int height, int heightPixel, bool sprite) {
    const int initial = 32768 - top - height + 1;
    if (initial < 0 || initial > 65535 || height <= 0)
        throw std::runtime_error("oracle fixture exceeds the audited native reference range");
    const int drawZ = -heightPixel - 1 - (sprite ? 2 : 0);
    const int divisor = gradient[3] / gradient[2];
    int ebp = std::div(static_cast<int>(static_cast<std::uint16_t>(initial))
        + drawZ, divisor).quot;
    ebp *= divisor;
    const auto heightDivision = std::div(height, divisor);
    ebp -= heightDivision.quot;
    int counter = gradient[3] - heightDivision.rem;
    if (counter == gradient[3]) {
        counter = 0;
        ebp += gradient[4];
    }
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        result.push_back(ebp);
        counter += gradient[2];
        if (counter >= gradient[3]) {
            ebp += gradient[4];
            counter -= gradient[3];
        }
    }
    return result;
}

RuntimeBuildingDepthFrame frameFor(int top, int height, int heightPixel, bool sprite) {
    RuntimeBuildingDepthFrame frame;
    frame.width = 23;
    frame.height = height;
    frame.nativeFrameTopY = top;
    frame.heightPixel = heightPixel;
    frame.layer = sprite ? RuntimeBuildingDepthLayer::NavalSprite
        : RuntimeBuildingDepthLayer::NavalVoxel;
    // Building-template, art and local-canvas offsets must not leak into naval Z.
    frame.canvasWidth = 119;
    frame.canvasHeight = 151;
    frame.offsetX = 21;
    frame.offsetY = 37;
    frame.normalZAdjust = 500;
    frame.zAdjust = -120;
    frame.yDrawOffset = 80;
    return frame;
}

void compareRow(const RuntimeBuildingDepthFrame& frame, int row,
    int expected, int& checks) {
    const std::vector<unsigned char> emptyTemplate;
    for (const int x : {0, frame.width / 2, frame.width - 1}) {
        int depth = -987654;
        require(runtimeBuildingDepthAtPixel(frame, emptyTemplate, 0, 0, x, row, depth)
            && depth == expected,
            "naval row depth differs: top=" + std::to_string(frame.nativeFrameTopY)
                + " height=" + std::to_string(frame.height)
                + " row=" + std::to_string(row)
                + " expected=" + std::to_string(expected)
                + " actual=" + std::to_string(depth), checks);
        depth = -987654;
        require(runtimeBuildingRenderDepthAtPixel(frame, emptyTemplate, 0, 0, x, row, depth)
            && depth == expected, "render depth diverges from audited naval row", checks);
    }
}

void verifyRows(const std::array<int, 6>& gradient, int& checks) {
    require(nativeRows(gradient, 600, 8, 0, false)
        == std::vector<int>({32158,32158,32159,32159,32159,32160,32160,32160}),
        "VXL literal row trace differs", checks);
    require(nativeRows(gradient, 600, 8, 0, true)
        == std::vector<int>({32155,32155,32156,32156,32156,32157,32157,32157}),
        "SHP literal row trace differs", checks);
    bool phases[2][3][3]{};
    for (const bool sprite : {false, true}) {
        for (const int top : {-83,0,1,2,598,599,600,32147}) {
            for (const int height : {1,2,3,4,5,8,17,31,62,63,64,97}) {
                for (const int heightPixel : {0,15,60,180,33000}) {
                    const auto expected = nativeRows(gradient, top, height, heightPixel, sprite);
                    const auto frame = frameFor(top, height, heightPixel, sprite);
                    const int base = 32768 - top - height - heightPixel - (sprite ? 2 : 0);
                    phases[sprite ? 1 : 0][(base % 3 + 3) % 3][height % 3] = true;
                    for (int row = 0; row < height; ++row)
                        compareRow(frame, row, expected[static_cast<std::size_t>(row)], checks);
                    int depth = 91;
                    for (const auto point : std::array<std::array<int, 2>, 4>{{
                        {{-1,0}}, {{frame.width,0}}, {{0,-1}}, {{0,height}}}}) {
                        require(!runtimeBuildingDepthAtPixel(frame, {}, 0, 0,
                            point[0], point[1], depth) && depth == 91,
                            "out-of-frame naval sample was accepted", checks);
                    }
                }
            }
        }
    }
    for (const auto& layer : phases)
        for (const auto& basePhase : layer)
            for (const bool covered : basePhase)
                require(covered, "missing base-phase/height-phase combination", checks);
}

void verifySharedPartBottom(const std::array<int, 6>& gradient, int& checks) {
    // Native 706963..7069B3: moving the top keeps the previous bottom; a
    // strictly larger incoming bottom extends it by one; equality does not.
    // These are complete VXL parts (hull/turret/barrel), not model sections.
    struct Part { int top; int height; };
    constexpr std::array<Part, 4> parts{{{600,40}, {575,70}, {588,58}, {570,9}}};
    int unionTop = parts[0].top;
    int unionBottom = unionTop + parts[0].height;
    for (std::size_t index = 1; index < parts.size(); ++index) {
        unionTop = std::min(unionTop, parts[index].top);
        const int incomingBottom = parts[index].top + parts[index].height;
        if (unionBottom < incomingBottom) unionBottom = incomingBottom + 1;
    }
    require(unionTop == 570 && unionBottom == 646,
        "literal native part-union trace differs", checks);
    for (const int shift : {-603,0,1,2,300}) {
        for (const int heightPixel : {0,15,60}) {
            const auto expected = nativeRows(gradient, unionTop + shift,
                unionBottom - unionTop, heightPixel, false);
            for (const auto part : parts) {
                auto frame = frameFor(part.top + shift, part.height, heightPixel, false);
                frame.navalBottom = unionBottom - part.top;
                for (int row = 0; row < part.height; ++row) {
                    const auto nativeRow = static_cast<std::size_t>(part.top - unionTop + row);
                    compareRow(frame, row, expected[nativeRow], checks);
                    // Transparent image padding may change a part canvas, but
                    // must not change any visible row's union gradient phase.
                    auto padded = frame;
                    padded.height += 13;
                    compareRow(padded, row, expected[nativeRow], checks);
                }
            }
        }
    }
}

void verifyReadOnlyOwners(int& checks) {
    // Native Z contains a uint16 word, independent of whichever later read-only
    // sprite owns the displayed color. Use distinct owners to avoid the
    // engine's separate same-building internal-layer compatibility exception.
    struct ReferencePixel { int z = 65535; int writer = -1; int visible = -1; } reference;
    RuntimeBuildingDepthPixel actual;
    actual.depth = reference.z;
    const auto submit = [&](int owner, int candidate, bool writes) {
        const bool accepted = candidate < reference.z; // 494BC5 cmp / jge
        if (accepted) {
            reference.visible = owner;
            if (writes) {
                reference.z = candidate;
                reference.writer = owner;
            }
        }
        require(runtimeBuildingDepthAccept(actual, owner, candidate, writes) == accepted,
            "strict native scene-depth comparison differs", checks);
        require(actual.depth == reference.z && actual.depthOwner == reference.writer
            && actual.visibleOwner == reference.visible,
            "read-only visible owner corrupted the scene depth writer", checks);
    };
    submit(20, 33000, false); // Empty scene: color only.
    submit(10, 32160, true);  // Building body writes scene Z.
    submit(20, 32160, false); // Equality rejects ship.
    submit(20, 32159, false); // Ship visible, building still owns Z.
    require(actual.depthOwner == 10 && actual.visibleOwner == 20,
        "ship did not separate color and depth ownership", checks);
    submit(30, 32160, false); // Equality rejects another read-only layer.
    submit(10, 32160, false); // Writer is not visible: no own-layer bypass.
    submit(10, 32170, true);  // Hidden writer cannot bypass strict comparison.
    submit(30, 32000, false);
    submit(40, 32150, false); // Further than visible ship, nearer than scene Z.
    submit(50, 32159, true);  // Another body may write through read-only color.
    submit(20, 32159, false); // Equality against the new scene writer rejects.
    submit(20, 32158, false);
    submit(60, 32159, true);
}

void verifyCameraContract(const std::array<int, 6>& gradient, int& checks) {
    // This is the core API's documented camera-independent, un-clipped bounds
    // contract. Retail VXL viewport clipping occurs before row initialization;
    // this test does NOT claim that native viewport-edge phase is identical.
    // Renderer placement, model bounds generation and zoom rasterization need
    // the separate integration/visual gates.
    for (const bool sprite : {false, true}) {
        for (const int heightPixel : {0,15,60}) {
            const int worldOrigin = (20 + 23) * 15 - heightPixel;
            constexpr int imageOffset = -61;
            constexpr int height = 83;
            const auto expected = nativeRows(gradient, worldOrigin + imageOffset,
                height, heightPixel, sprite);
            for (const int scroll : {-100000,-73,0,23,80,1234567}) {
                const int screenOrigin = worldOrigin - scroll + 47;
                const int screenTop = screenOrigin + imageOffset;
                const int recoveredNativeTop = worldOrigin + screenTop - screenOrigin;
                const auto frame = frameFor(recoveredNativeTop, height, heightPixel, sprite);
                for (const int croppedTopRows : {0,1,2,17,61}) {
                    for (int row = croppedTopRows; row < height; ++row)
                        compareRow(frame, row, expected[static_cast<std::size_t>(row)], checks);
                }
            }
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        int checks = 0;
        const auto root = argc > 1 ? std::filesystem::path(argv[1])
            : std::filesystem::current_path();
        const auto retail = RuntimeBuildingDepthOracle::readPinned(
            root / "RedAlert2/GameMD/gamemd.exe", retailSha);
        const auto gradient = pinRetailEvidence(retail, checks);
        verifyRows(gradient, checks);
        verifySharedPartBottom(gradient, checks);
        verifyReadOnlyOwners(checks);
        verifyCameraContract(gradient, checks);
        std::cout << "naval scene depth core: " << checks << " checks passed\n"
            << "retail SHA-256: " << retailSha << '\n'
            << "gradient=2 reference=32768 VXL/SHP phases and shared part bottom verified\n"
            << "strict comparison, read-only Z ownership and un-clipped camera contract verified\n"
            << "scope: normal Ship and flat-ground Drive VXL; terrain/raster/placement require integration gates\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "naval scene depth core FAILED: " << error.what() << '\n';
        return 1;
    }
}
