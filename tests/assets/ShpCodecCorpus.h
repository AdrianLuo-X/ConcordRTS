#pragma once

// Synthetic fixtures and observation serialization only; no SHP decoder lives here.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace shp_corpus {

using Bytes = std::vector<unsigned char>;

struct Frame {
    int offsetX = 0, offsetY = 0, globalWidth = 5, globalHeight = 4;
    int width = 3, height = 2, frameIndex = 0, format = 1;
    std::uint32_t dataOffset = 32;
    bool authoredPause = false;
    Bytes pixels;
    std::string sourcePath;
    bool operator==(const Frame&) const = default;
};

struct Expected {
    bool success = true;
    Frame frame;
    std::string error = "previous diagnostic";
};

struct Case {
    std::string name;
    Bytes bytes;
    int frameIndex = 0;
    Expected preview;
    Expected runtime;
};

inline void put16(Bytes& bytes, std::size_t offset, unsigned int value) {
    bytes[offset] = static_cast<unsigned char>(value & 255U);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 255U);
}
inline void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 4; ++shift) {
        bytes[offset + shift] = static_cast<unsigned char>((value >> (shift * 8U)) & 255U);
    }
}
inline unsigned int get16(const Bytes& bytes, std::size_t offset) {
    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}
inline std::uint32_t get32(const Bytes& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned int shift = 0; shift < 4; ++shift) {
        value |= static_cast<std::uint32_t>(bytes[offset + shift]) << (shift * 8U);
    }
    return value;
}

inline Bytes file(const Bytes& payload, unsigned int format = 1, unsigned int width = 3,
    unsigned int height = 2, unsigned int globalWidth = 5, unsigned int globalHeight = 4) {
    Bytes result(32, 0);
    put16(result, 2, globalWidth); put16(result, 4, globalHeight); put16(result, 6, 1);
    put16(result, 12, width); put16(result, 14, height); put16(result, 16, format);
    put32(result, 28, 32);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

inline Frame sentinel() {
    Frame result;
    result.offsetX = 71; result.offsetY = 72;
    result.globalWidth = 73; result.globalHeight = 74;
    result.width = 2; result.height = 1; result.frameIndex = 75; result.format = 76;
    result.dataOffset = 77; result.authoredPause = true;
    result.pixels = {42, 0}; result.sourcePath = "sentinel-source";
    return result;
}
inline Expected rejected(const std::string& error) { return {false, sentinel(), error}; }
inline Frame rawExpected(const Bytes& pixels) { Frame result; result.pixels = pixels; return result; }

inline std::vector<Case> cases() {
    std::vector<Case> result;
    const auto accepted = [&](const std::string& name, Bytes bytes, Frame preview, Frame runtime, int index = 0) {
        preview.sourcePath = name; runtime.sourcePath = name;
        result.push_back({name, std::move(bytes), index, {true, std::move(preview), "previous diagnostic"},
            {true, std::move(runtime), "previous diagnostic"}});
    };
    const auto failure = [&](const std::string& name, Bytes bytes, const std::string& preview,
        const std::string& runtime, int index = 0) {
        result.push_back({name, std::move(bytes), index, rejected(preview + name), rejected(runtime + name)});
    };
    const Bytes raw{0, 1, 255, 2, 0, 3};
    for (const unsigned int format : {0U, 1U}) {
        auto expected = rawExpected(raw); expected.format = static_cast<int>(format);
        accepted("raw-" + std::to_string(format), file(raw, format), expected, expected);
    }
    auto bytes = file(raw);
    bytes[0] = 255; bytes[1] = 255;
    accepted("unvalidated-file-prefix", bytes, rawExpected(raw), rawExpected(raw));
    bytes.push_back(99);
    accepted("raw-extra-tail", bytes, rawExpected(raw), rawExpected(raw));

    bytes = file(raw); put16(bytes, 8, 65535); put16(bytes, 10, 32768);
    auto preview = rawExpected(raw); auto runtime = preview;
    preview.offsetX = 65535; preview.offsetY = 32768; runtime.offsetX = -1; runtime.offsetY = -32768;
    accepted("signed-offset-policy", bytes, preview, runtime);
    for (const unsigned int global : {0U, 32768U, 65535U}) {
        bytes = file(raw, 1, 3, 2, global, 4);
        preview = rawExpected(raw); preview.globalWidth = static_cast<int>(global);
        const std::string name = "global-width-" + std::to_string(global);
        preview.sourcePath = name;
        result.push_back({name, bytes, 0, {true, preview, "previous diagnostic"},
            rejected("SHP frame dimensions are unsupported: " + name)});
    }
    for (const unsigned int local : {0U, 4097U, 32768U, 65535U}) {
        const std::string name = "bad-width-" + std::to_string(local);
        failure(name, file({1}, 1, local, 1), "SHP first frame dimensions are unsupported: ",
            "SHP frame dimensions are unsupported: ");
    }
    for (const unsigned int local : {0U, 4097U, 32768U, 65535U}) {
        failure("bad-height-" + std::to_string(local), file({1}, 1, 1, local),
            "SHP first frame dimensions are unsupported: ", "SHP frame dimensions are unsupported: ");
    }
    bytes = file(raw, 1, 3, 2, 5, 0);
    preview = rawExpected(raw); preview.globalHeight = 0; preview.sourcePath = "zero-global-height";
    result.push_back({"zero-global-height", bytes, 0, {true, preview, "previous diagnostic"},
        rejected("SHP frame dimensions are unsupported: zero-global-height")});
    auto smallest = rawExpected({255}); smallest.width = 1; smallest.height = 1;
    accepted("one-by-one", file({255}, 1, 1, 1), smallest, smallest);
    Bytes emptyRows;
    for (int row = 0; row < 4096; ++row) { emptyRows.push_back(2); emptyRows.push_back(0); }
    auto largest = rawExpected(Bytes(4096U * 4096U, 0)); largest.width = 4096; largest.height = 4096; largest.format = 3;
    accepted("maximum-area-format3-zero-rows", file(emptyRows, 3, 4096, 4096), largest, largest);
    for (const bool wide : {false, true}) {
        const unsigned int width = wide ? 4096U : 1U, height = wide ? 1U : 4096U;
        Bytes pixels(4096, 23); pixels[0] = 0; pixels.back() = 255;
        auto expected = rawExpected(pixels); expected.width = static_cast<int>(width); expected.height = static_cast<int>(height);
        accepted(wide ? "width-4096" : "height-4096", file(pixels, 1, width, height), expected, expected);
    }
    for (const unsigned int format : {0U, 1U, 3U, 4U}) {
        bytes = file({}, format, 0, 0); put32(bytes, 28, 0); put16(bytes, 8, 17); put16(bytes, 10, 18);
        const std::string name = "authored-pause-" + std::to_string(format);
        auto expected = rawExpected({}); expected.width = 0; expected.height = 0;
        expected.format = static_cast<int>(format); expected.dataOffset = 0; expected.authoredPause = true;
        expected.sourcePath = name;
        result.push_back({name, bytes, 0, rejected("SHP first frame dimensions are unsupported: " + name),
            {true, expected, "previous diagnostic"}});
    }
    bytes = file({}, 2, 0, 0); put32(bytes, 28, 0);
    failure("empty-format2-rejected", bytes, "SHP first frame dimensions are unsupported: ", "SHP frame dimensions are unsupported: ");
    bytes = file({}, 4, 0, 0, 0, 4); put32(bytes, 28, 0);
    failure("pause-zero-global-rejected", bytes, "SHP first frame dimensions are unsupported: ", "SHP frame dimensions are unsupported: ");
    bytes = file({0}, 4, 0, 0);
    failure("pause-nonzero-data-rejected", bytes, "SHP first frame dimensions are unsupported: ", "SHP frame dimensions are unsupported: ");
    bytes = file(raw, 257);
    runtime = rawExpected(raw); runtime.sourcePath = "format-high-byte-runtime-raw";
    result.push_back({"format-high-byte-runtime-raw", bytes, 0,
        rejected("Unsupported SHP first frame format 257: format-high-byte-runtime-raw"),
        {true, runtime, "previous diagnostic"}});

    bytes = file({6, 0, 9, 0, 1, 7, 5, 0, 0, 2, 6}, 259);
    runtime = rawExpected({9, 0, 7, 0, 0, 6}); runtime.format = 3; runtime.sourcePath = "format-high-byte-runtime-rle";
    result.push_back({"format-high-byte-runtime-rle", bytes, 0,
        rejected("Unsupported SHP first frame format 259: format-high-byte-runtime-rle"),
        {true, runtime, "previous diagnostic"}});
    bytes.assign(56, 0); put16(bytes, 2, 5); put16(bytes, 4, 4); put16(bytes, 6, 2); put16(bytes, 40, 3);
    runtime = rawExpected({}); runtime.width = 0; runtime.height = 0; runtime.frameIndex = 1;
    runtime.format = 3; runtime.dataOffset = 0; runtime.authoredPause = true; runtime.sourcePath = "second-frame-authored-pause";
    result.push_back({"second-frame-authored-pause", bytes, 1,
        rejected("SHP first frame dimensions are unsupported: second-frame-authored-pause"),
        {true, runtime, "previous diagnostic"}});
    const auto format2 = [&](const std::string& name, const Bytes& payload, const Bytes& pixels) {
        auto expected = rawExpected(pixels); expected.format = 2; expected.sourcePath = name;
        result.push_back({name, file(payload, 2), 0,
            rejected("Unsupported SHP first frame format 2: " + name), {true, expected, "previous diagnostic"}});
    };
    format2("format2-exact", {5, 0, 0, 7, 255, 5, 0, 1, 0, 2}, {0, 7, 255, 1, 0, 2});
    format2("format2-short-empty", {3, 0, 9, 2, 0}, {9, 0, 0, 0, 0, 0});
    format2("format2-long-row", {7, 0, 1, 2, 3, 4, 5, 6, 0, 6, 7, 8, 9}, {1, 2, 3, 6, 7, 8});
    format2("format2-extra-tail", {5, 0, 1, 2, 3, 5, 0, 4, 5, 6, 222}, {1, 2, 3, 4, 5, 6});
    const auto bad2 = [&](const std::string& name, const Bytes& payload, const std::string& error) {
        failure(name, file(payload, 2), "Unsupported SHP first frame format 2: ", error + " Path: ");
    };
    bad2("format2-short-header", {5}, "SHP format 2 row header is truncated");
    bad2("format2-zero-length", {0, 0}, "SHP format 2 row length is invalid");
    bad2("format2-one-length", {1, 0}, "SHP format 2 row length is invalid");
    bad2("format2-short-payload", {5, 0, 1, 2}, "SHP format 2 row data is truncated");
    bad2("format2-missing-second-row", {5, 0, 1, 2, 3}, "SHP format 2 row header is truncated");

    const auto format3 = [&](const std::string& name, const Bytes& payload, const Bytes& pixels) {
        auto expected = rawExpected(pixels); expected.format = 3;
        accepted(name, file(payload, 3), expected, expected);
    };
    format3("format3-mixed", {6, 0, 9, 0, 1, 7, 5, 0, 0, 2, 6}, {9, 0, 7, 0, 0, 6});
    format3("format3-zero-run", {6, 0, 0, 0, 9, 8, 2, 0}, {9, 8, 0, 0, 0, 0});
    format3("format3-overwide-run", {5, 0, 0, 255, 9, 5, 0, 1, 2, 3}, {0, 0, 0, 1, 2, 3});
    format3("format3-short-long-rows", {3, 0, 9, 7, 0, 1, 2, 3, 4, 5}, {9, 0, 0, 1, 2, 3});
    format3("format3-extra-tail", {5, 0, 1, 2, 3, 5, 0, 4, 5, 6, 222}, {1, 2, 3, 4, 5, 6});
    const auto bad3 = [&](const std::string& name, const Bytes& payload, const std::string& error) {
        failure(name, file(payload, 3), error + " Path: ", error + " Path: ");
    };
    bad3("format3-short-header", {5}, "SHP format 3 row table ended early.");
    bad3("format3-zero-length", {0, 0}, "SHP format 3 row has an invalid length.");
    bad3("format3-one-length", {1, 0}, "SHP format 3 row has an invalid length.");
    bad3("format3-short-payload", {5, 0, 1, 2}, "SHP format 3 row extends past the end of the file.");
    bad3("format3-truncated-run", {3, 0, 0}, "SHP format 3 transparent run is truncated.");
    bad3("format3-missing-second-row", {5, 0, 1, 2, 3}, "SHP format 3 row table ended early.");

    for (const std::size_t size : {std::size_t{0}, std::size_t{8}, std::size_t{31}}) {
        failure("short-file-" + std::to_string(size), Bytes(size, 0),
            "SHP file is too small for a frame table: ", "SHP file is too small for a frame table: ");
    }
    bytes = file(raw); put16(bytes, 6, 0);
    failure("zero-frame-count", bytes, "SHP file has no frames: ", "SHP file has no frames: ");
    for (const int index : {-1, 1}) {
        const std::string text = "SHP frame " + std::to_string(index) + " is outside the frame table (1 frames): ";
        failure("frame-index-" + std::to_string(index), file(raw), text, text, index);
    }
    bytes = file(raw); put16(bytes, 6, 2);
    failure("selected-table-truncated", bytes, "SHP frame table is truncated: ", "SHP frame table is truncated: ", 1);
    bytes = file(raw); put16(bytes, 6, 65535);
    accepted("unselected-table-not-validated", bytes, rawExpected(raw), rawExpected(raw));
    failure("large-selected-table-truncated", bytes, "SHP frame table is truncated: ", "SHP frame table is truncated: ", 65534);
    for (const std::uint32_t offset : {std::uint32_t{38}, std::uint32_t{0xffffffffU}}) {
        bytes = file(raw); put32(bytes, 28, offset);
        failure("outside-offset-" + std::to_string(offset), bytes, "SHP first frame data offset is outside the file: ",
            "SHP frame data offset is outside the file: ");
    }
    bytes = file({1, 2, 3, 4, 5});
    failure("raw-truncated", bytes, "SHP raw first frame is shorter than expected: ", "SHP raw frame is shorter than expected: ");
    bytes = file(raw); put32(bytes, 28, 0);
    accepted("offset-before-table-accepted", bytes, rawExpected({0, 0, 5, 0, 4, 0}), rawExpected({0, 0, 5, 0, 4, 0}));
    result.back().preview.frame.dataOffset = 0; result.back().runtime.frame.dataOffset = 0;
    failure("unsupported-format4-nonempty", file(raw, 4), "Unsupported SHP first frame format 4: ", "Unsupported SHP frame format 4: ");

    bytes.assign(62, 0);
    put16(bytes, 2, 5); put16(bytes, 4, 4); put16(bytes, 6, 2);
    put16(bytes, 12, 3); put16(bytes, 14, 1); put16(bytes, 16, 1); put32(bytes, 28, 56);
    put16(bytes, 32, 2); put16(bytes, 34, 3); put16(bytes, 36, 3); put16(bytes, 38, 1);
    put16(bytes, 40, 1); put32(bytes, 52, 59);
    bytes[56] = 1; bytes[57] = 2; bytes[58] = 3; bytes[59] = 4; bytes[60] = 0; bytes[61] = 6;
    auto expected = rawExpected({4, 0, 6}); expected.height = 1; expected.frameIndex = 1;
    expected.offsetX = 2; expected.offsetY = 3; expected.dataOffset = 59;
    accepted("second-frame-selected", bytes, expected, expected, 1);
    return result;
}

inline std::uint64_t pixelHash(const Bytes& bytes) {
    std::uint64_t result = 14695981039346656037ull;
    for (const unsigned char value : bytes) { result ^= value; result *= 1099511628211ull; }
    return result;
}
inline std::string hex(const std::string& text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char value : text) {
        result += digits[value >> 4]; result += digits[value & 15];
    }
    return result;
}
inline std::string bytesHex(const Bytes& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(bytes.size() * 2U);
    for (const unsigned char value : bytes) { result += digits[value >> 4]; result += digits[value & 15]; }
    return result;
}
inline void writeObservation(std::ostream& output, const std::string& kind, const std::string& label,
    const char* policy, int requested, bool success, const Frame& frame, const std::string& error, bool includePixels = false) {
    const auto zeros = static_cast<std::size_t>(std::count(frame.pixels.begin(), frame.pixels.end(), 0));
    output << kind << '\t' << hex(label) << '\t' << policy << '\t' << requested << '\t' << success
        << '\t' << frame.offsetX << '\t' << frame.offsetY << '\t' << frame.globalWidth << '\t' << frame.globalHeight
        << '\t' << frame.width << '\t' << frame.height << '\t' << frame.frameIndex << '\t' << frame.format
        << '\t' << frame.dataOffset << '\t' << frame.authoredPause << '\t' << frame.pixels.size()
        << '\t' << zeros << '\t' << frame.pixels.size() - zeros << '\t' << std::hex << std::setw(16)
        << std::setfill('0') << pixelHash(frame.pixels) << std::dec << '\t' << hex(frame.sourcePath)
        << '\t' << hex(error) << '\t' << (includePixels ? bytesHex(frame.pixels) : std::string{"-"}) << '\n';
}

struct Options { std::string manifest; std::vector<std::string> assets; };
inline bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if ((option != "--manifest" && option != "--asset") || index + 1 >= argc) {
            error = "Expected --manifest <path> and/or repeated --asset <absolute path>"; return false;
        }
        const std::string value = argv[++index];
        if (option == "--manifest") options.manifest = value;
        else {
            if (!std::filesystem::path(value).is_absolute()) { error = "Asset paths must be explicit absolute paths"; return false; }
            options.assets.push_back(value);
        }
    }
    return true;
}

template <typename Decode>
bool writeManifest(const Options& options, Decode decode, std::string& error) {
    if (options.manifest.empty()) {
        if (!options.assets.empty()) { error = "--asset requires --manifest"; return false; }
        return true;
    }
    std::ofstream output(options.manifest, std::ios::binary | std::ios::trunc);
    if (!output) { error = "Could not create requested manifest"; return false; }
    output << "SHP-COMPATIBILITY-v1\n";
    for (const auto& item : cases()) {
        for (const bool runtime : {false, true}) {
            Frame observed = sentinel(); std::string diagnostic = "previous diagnostic";
            const bool success = decode(item.bytes, item.name, item.frameIndex, runtime, observed, diagnostic);
            writeObservation(output, "synthetic", item.name, runtime ? "runtime" : "preview", item.frameIndex,
                success, observed, diagnostic);
        }
    }
    for (const auto& path : options.assets) {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Could not read explicit asset: " + path; return false; }
        const Bytes bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const int count = bytes.size() >= 8 ? static_cast<int>(get16(bytes, 6)) : 0;
        for (int frame = -1; frame <= count; ++frame) {
            for (const bool runtime : {false, true}) {
                Frame observed = sentinel(); std::string diagnostic = "previous diagnostic";
                const bool success = decode(bytes, path, frame, runtime, observed, diagnostic);
                writeObservation(output, "asset", path, runtime ? "runtime" : "preview", frame,
                    success, observed, diagnostic, true);
            }
        }
    }
    if (!output.good()) { error = "Manifest write failed"; return false; }
    return true;
}

} // namespace shp_corpus