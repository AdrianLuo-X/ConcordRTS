// Native full-canvas landmarks, reviewed in structure-research/independent-audit.
// Match full source bytes: replacement SHPs retain their authored FLH fallback.
bool runtimeNativeStructureMuzzleAnchor(const std::string& imageId,
    std::uint64_t fingerprint, int frame, int width, int height, POINT& point) {
    const auto image = toLowerAscii(imageId);
    if (width == 242 && height == 254 && frame >= 0 && frame < 52
        && ((image == "yapsyt_a" && fingerprint == 6045859813096487659ULL)
            || (image == "ygpsyt_a" && fingerprint == 3351306387071131293ULL))) {
        // The animated antenna tip, including the leaning damaged cycle.
        static constexpr std::array<POINT, 52> tips{{
            POINT{118, 7}, // frame 0
            POINT{117, 7}, // frame 1
            POINT{117, 8}, // frame 2
            POINT{117, 9}, // frame 3
            POINT{117, 9}, // frame 4
            POINT{117, 9}, // frame 5
            POINT{118, 9}, // frame 6
            POINT{118, 9}, // frame 7
            POINT{119, 9}, // frame 8
            POINT{119, 9}, // frame 9
            POINT{120, 9}, // frame 10
            POINT{121, 9}, // frame 11
            POINT{121, 9}, // frame 12
            POINT{122, 9}, // frame 13
            POINT{122, 9}, // frame 14
            POINT{122, 10}, // frame 15
            POINT{122, 10}, // frame 16
            POINT{122, 10}, // frame 17
            POINT{122, 8}, // frame 18
            POINT{122, 7}, // frame 19
            POINT{121, 7}, // frame 20
            POINT{121, 6}, // frame 21
            POINT{120, 6}, // frame 22
            POINT{120, 6}, // frame 23
            POINT{119, 6}, // frame 24
            POINT{118, 6}, // frame 25
            POINT{98, 21}, // frame 26
            POINT{102, 23}, // frame 27
            POINT{106, 25}, // frame 28
            POINT{110, 26}, // frame 29
            POINT{114, 26}, // frame 30
            POINT{118, 25}, // frame 31
            POINT{123, 23}, // frame 32
            POINT{126, 21}, // frame 33
            POINT{129, 19}, // frame 34
            POINT{131, 17}, // frame 35
            POINT{132, 14}, // frame 36
            POINT{132, 12}, // frame 37
            POINT{131, 9}, // frame 38
            POINT{128, 8}, // frame 39
            POINT{125, 6}, // frame 40
            POINT{121, 7}, // frame 41
            POINT{117, 6}, // frame 42
            POINT{113, 5}, // frame 43
            POINT{109, 6}, // frame 44
            POINT{104, 6}, // frame 45
            POINT{101, 8}, // frame 46
            POINT{98, 10}, // frame 47
            POINT{96, 12}, // frame 48
            POINT{95, 14}, // frame 49
            POINT{95, 17}, // frame 50
            POINT{96, 19}, // frame 51
        }};
        point = tips[static_cast<std::size_t>(frame)];
        return true;
    }
    if (width == 242 && height == 244 && frame >= 0 && frame < 3
        && (((image == "cdeast01" || image == "cteast01") && fingerprint == 14269728109210428427ULL)
            || ((image == "cueast01" || image == "cneast01") && fingerprint == 17714354711052135820ULL))) {
        // Intact eye socket; the surviving forward socket after damage.
        point = frame == 0 ? POINT{130, 46} : POINT{113, 45};
        return true;
    }
    return false;
}
