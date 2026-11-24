#include "RuntimeStructureMuzzleGeometry.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <array>
#include <cmath>
#include <sstream>

namespace concord::render::geometry {
// Native full-canvas landmarks, reviewed in structure-research/independent-audit.
// Match full source bytes: replacement SHPs retain their authored FLH fallback.
bool findStructureMuzzleAnchor(const std::string& imageId,
    std::uint64_t fingerprint, int frame, int width, int height, RuntimeInputPixelPoint& point) {
    const auto image = runtime_save_text::toLowerAsciiLocal(imageId);
    if (width == 242 && height == 254 && frame >= 0 && frame < 52
        && ((image == "yapsyt_a" && fingerprint == 6045859813096487659ULL)
            || (image == "ygpsyt_a" && fingerprint == 3351306387071131293ULL))) {
        // The animated antenna tip, including the leaning damaged cycle.
        static constexpr std::array<RuntimeInputPixelPoint, 52> tips{{
            RuntimeInputPixelPoint{118, 7}, // frame 0
            RuntimeInputPixelPoint{117, 7}, // frame 1
            RuntimeInputPixelPoint{117, 8}, // frame 2
            RuntimeInputPixelPoint{117, 9}, // frame 3
            RuntimeInputPixelPoint{117, 9}, // frame 4
            RuntimeInputPixelPoint{117, 9}, // frame 5
            RuntimeInputPixelPoint{118, 9}, // frame 6
            RuntimeInputPixelPoint{118, 9}, // frame 7
            RuntimeInputPixelPoint{119, 9}, // frame 8
            RuntimeInputPixelPoint{119, 9}, // frame 9
            RuntimeInputPixelPoint{120, 9}, // frame 10
            RuntimeInputPixelPoint{121, 9}, // frame 11
            RuntimeInputPixelPoint{121, 9}, // frame 12
            RuntimeInputPixelPoint{122, 9}, // frame 13
            RuntimeInputPixelPoint{122, 9}, // frame 14
            RuntimeInputPixelPoint{122, 10}, // frame 15
            RuntimeInputPixelPoint{122, 10}, // frame 16
            RuntimeInputPixelPoint{122, 10}, // frame 17
            RuntimeInputPixelPoint{122, 8}, // frame 18
            RuntimeInputPixelPoint{122, 7}, // frame 19
            RuntimeInputPixelPoint{121, 7}, // frame 20
            RuntimeInputPixelPoint{121, 6}, // frame 21
            RuntimeInputPixelPoint{120, 6}, // frame 22
            RuntimeInputPixelPoint{120, 6}, // frame 23
            RuntimeInputPixelPoint{119, 6}, // frame 24
            RuntimeInputPixelPoint{118, 6}, // frame 25
            RuntimeInputPixelPoint{98, 21}, // frame 26
            RuntimeInputPixelPoint{102, 23}, // frame 27
            RuntimeInputPixelPoint{106, 25}, // frame 28
            RuntimeInputPixelPoint{110, 26}, // frame 29
            RuntimeInputPixelPoint{114, 26}, // frame 30
            RuntimeInputPixelPoint{118, 25}, // frame 31
            RuntimeInputPixelPoint{123, 23}, // frame 32
            RuntimeInputPixelPoint{126, 21}, // frame 33
            RuntimeInputPixelPoint{129, 19}, // frame 34
            RuntimeInputPixelPoint{131, 17}, // frame 35
            RuntimeInputPixelPoint{132, 14}, // frame 36
            RuntimeInputPixelPoint{132, 12}, // frame 37
            RuntimeInputPixelPoint{131, 9}, // frame 38
            RuntimeInputPixelPoint{128, 8}, // frame 39
            RuntimeInputPixelPoint{125, 6}, // frame 40
            RuntimeInputPixelPoint{121, 7}, // frame 41
            RuntimeInputPixelPoint{117, 6}, // frame 42
            RuntimeInputPixelPoint{113, 5}, // frame 43
            RuntimeInputPixelPoint{109, 6}, // frame 44
            RuntimeInputPixelPoint{104, 6}, // frame 45
            RuntimeInputPixelPoint{101, 8}, // frame 46
            RuntimeInputPixelPoint{98, 10}, // frame 47
            RuntimeInputPixelPoint{96, 12}, // frame 48
            RuntimeInputPixelPoint{95, 14}, // frame 49
            RuntimeInputPixelPoint{95, 17}, // frame 50
            RuntimeInputPixelPoint{96, 19}, // frame 51
        }};
        point = tips[static_cast<std::size_t>(frame)];
        return true;
    }
    if (width == 242 && height == 244 && frame >= 0 && frame < 3
        && (((image == "cdeast01" || image == "cteast01") && fingerprint == 14269728109210428427ULL)
            || ((image == "cueast01" || image == "cneast01") && fingerprint == 17714354711052135820ULL))) {
        // Intact eye socket; the surviving forward socket after damage.
        point = frame == 0 ? RuntimeInputPixelPoint{130, 46} : RuntimeInputPixelPoint{113, 45};
        return true;
    }
    return false;
}

bool resolveAuthoredStructureMuzzle(const std::string& text, bool dualOffset,
    const StructureMuzzleFlh& flh, int firingFacing,
    const RuntimeInputPixelPoint& centerPixels, RuntimeInputPixelPoint& result) {
    std::istringstream stream(text);
    int x = 0, y = 0;
    char comma = 0;
    if (!(stream >> x >> comma >> y) || comma != ',' || (stream >> std::ws, !stream.eof())
        || x < -32768 || x > 32767 || y < -32768 || y > 32767) return false;
    double sx = x, sy = y;
    if (dualOffset) {
        constexpr double pi = 3.14159265358979323846;
        const double angle = (firingFacing & 255) * 2.0 * pi / 256.0;
        const double fx = (std::sin(angle) - std::cos(angle)) / std::sqrt(2.0);
        const double fy = (-std::cos(angle) - std::sin(angle)) / std::sqrt(2.0);
        const double dx = (fx * flh.forwardLeptons - fy * flh.lateralLeptons) / 256.0;
        const double dy = (fy * flh.forwardLeptons + fx * flh.lateralLeptons) / 256.0;
        sx += (dx - dy) * 30.0;
        sy += (dx + dy) * 15.0 - flh.heightLeptons * std::sqrt(6.0) * 15.0 / 256.0;
    }
    result = {centerPixels.x + static_cast<std::int32_t>(std::lround(sx)),
        centerPixels.y + static_cast<std::int32_t>(std::lround(sy))};
    return true;
}

bool resolveStructureFrameMuzzle(const StructureMuzzleFrame& frame,
    const RuntimeInputPixelPoint& centerPixels, const RuntimeInputPixelPoint& frameOffset,
    RuntimeInputPixelPoint& result) {
    RuntimeInputPixelPoint point{};
    if (!findStructureMuzzleAnchor(frame.imageId, frame.sourceFingerprint,
            frame.frameIndex, frame.width, frame.height, point)) return false;
    // Structure main and active SHPs are center-authored, unlike infantry.
    result = {centerPixels.x + frameOffset.x - frame.width / 2 + point.x,
        centerPixels.y + frameOffset.y - frame.height / 2 + point.y};
    return true;
}
}
