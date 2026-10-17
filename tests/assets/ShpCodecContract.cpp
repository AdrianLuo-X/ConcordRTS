#include "assets/codecs/ShpCodec.h"
#include "ShpCodecCorpus.h"

#include <iostream>
#include <string>
#include <utility>

namespace {

ShpIndexedFrame product(const shp_corpus::Frame& source) {
    ShpIndexedFrame result;
    result.offsetX = source.offsetX; result.offsetY = source.offsetY;
    result.globalWidth = source.globalWidth; result.globalHeight = source.globalHeight;
    result.width = source.width; result.height = source.height;
    result.frameIndex = source.frameIndex; result.format = source.format;
    result.dataOffset = source.dataOffset; result.authoredPause = source.authoredPause;
    result.pixels = source.pixels; result.sourcePath = source.sourcePath;
    return result;
}
shp_corpus::Frame observation(const ShpIndexedFrame& source) {
    shp_corpus::Frame result;
    result.offsetX = source.offsetX; result.offsetY = source.offsetY;
    result.globalWidth = source.globalWidth; result.globalHeight = source.globalHeight;
    result.width = source.width; result.height = source.height;
    result.frameIndex = source.frameIndex; result.format = source.format;
    result.dataOffset = source.dataOffset; result.authoredPause = source.authoredPause;
    result.pixels = source.pixels; result.sourcePath = source.sourcePath;
    return result;
}

bool decode(const shp_corpus::Bytes& bytes, const std::string& source, int frameIndex,
    bool runtime, shp_corpus::Frame& frame, std::string& error) {
    auto decoded = product(frame);
    const bool success = decodeShpIndexedFrame(bytes, source, frameIndex,
        runtime ? ShpDecodePolicy::Runtime : ShpDecodePolicy::Preview, decoded, error);
    frame = observation(decoded);
    return success;
}

} // namespace

int main(int argc, char** argv) {
    shp_corpus::Options options;
    std::string error;
    if (!shp_corpus::parseOptions(argc, argv, options, error)) {
        std::cerr << error << '\n'; return 2;
    }
    bool success = true;
    const auto cases = shp_corpus::cases();
    for (const auto& item : cases) {
        for (const bool runtime : {false, true}) {
            const auto& expected = runtime ? item.runtime : item.preview;
            auto frame = shp_corpus::sentinel();
            std::string diagnostic = "previous diagnostic";
            const bool accepted = decode(item.bytes, item.name, item.frameIndex, runtime, frame, diagnostic);
            if (accepted != expected.success || frame != expected.frame || diagnostic != expected.error) {
                std::cerr << "SHP codec contract failed: " << item.name << ' '
                    << (runtime ? "runtime" : "preview") << '\n';
                shp_corpus::writeObservation(std::cerr, "actual", item.name, runtime ? "runtime" : "preview",
                    item.frameIndex, accepted, frame, diagnostic);
                shp_corpus::writeObservation(std::cerr, "expected", item.name, runtime ? "runtime" : "preview",
                    item.frameIndex, expected.success, expected.frame, expected.error);
                success = false;
            }
        }
    }
    if (!success) return 1;
    if (!shp_corpus::writeManifest(options, decode, error)) {
        std::cerr << error << '\n'; return 2;
    }
    std::cout << "SHP codec contract: cases=" << cases.size() << " policyChecks=" << cases.size() * 2
        << " fullPixels=exact metadata=exact errors=exact atomicFailure=yes legacyTolerance=yes\n";
    return 0;
}