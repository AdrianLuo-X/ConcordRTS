// Stock dolphin snout pixels, measured from the original full SHP canvas.
// Evidence: build/smoke/feature-set-15/03-muzzles/marine-research/.
// Caller verifies the native SHP byte fingerprint; replacement art falls back.
// The squid has melee/parasite attacks and no authored projectile muzzle.
static constexpr std::array<POINT, 104> runtimeDolphinMuzzleFramePoints{{
    {36, 68}, // frame 0
    {36, 69}, // frame 1
    {36, 70}, // frame 2
    {36, 70}, // frame 3
    {36, 70}, // frame 4
    {36, 69}, // frame 5
    {46, 72}, // frame 6
    {47, 73}, // frame 7
    {47, 74}, // frame 8
    {47, 74}, // frame 9
    {47, 74}, // frame 10
    {47, 73}, // frame 11
    {53, 81}, // frame 12
    {53, 81}, // frame 13
    {54, 82}, // frame 14
    {54, 83}, // frame 15
    {54, 82}, // frame 16
    {53, 81}, // frame 17
    {48, 88}, // frame 18
    {48, 88}, // frame 19
    {48, 89}, // frame 20
    {49, 89}, // frame 21
    {49, 89}, // frame 22
    {48, 88}, // frame 23
    {36, 90}, // frame 24
    {36, 90}, // frame 25
    {36, 91}, // frame 26
    {36, 92}, // frame 27
    {35, 91}, // frame 28
    {36, 91}, // frame 29
    {24, 88}, // frame 30
    {23, 88}, // frame 31
    {23, 89}, // frame 32
    {23, 89}, // frame 33
    {23, 89}, // frame 34
    {23, 88}, // frame 35
    {19, 81}, // frame 36
    {18, 82}, // frame 37
    {18, 82}, // frame 38
    {18, 82}, // frame 39
    {18, 82}, // frame 40
    {18, 82}, // frame 41
    {25, 73}, // frame 42
    {25, 73}, // frame 43
    {25, 73}, // frame 44
    {25, 73}, // frame 45
    {24, 75}, // frame 46
    {24, 75}, // frame 47
    {36, 68}, // frame 48
    {46, 72}, // frame 49
    {53, 81}, // frame 50
    {48, 88}, // frame 51
    {36, 90}, // frame 52
    {24, 88}, // frame 53
    {19, 81}, // frame 54
    {25, 73}, // frame 55
    {36, 68}, // frame 56
    {36, 69}, // frame 57
    {36, 69}, // frame 58
    {35, 70}, // frame 59
    {36, 70}, // frame 60
    {36, 69}, // frame 61
    {46, 72}, // frame 62
    {47, 72}, // frame 63
    {47, 71}, // frame 64
    {46, 73}, // frame 65
    {44, 74}, // frame 66
    {45, 73}, // frame 67
    {53, 81}, // frame 68
    {53, 79}, // frame 69
    {51, 76}, // frame 70
    {50, 78}, // frame 71
    {49, 80}, // frame 72
    {51, 81}, // frame 73
    {48, 88}, // frame 74
    {48, 85}, // frame 75
    {47, 82}, // frame 76
    {46, 83}, // frame 77
    {45, 85}, // frame 78
    {36, 89}, // frame 79
    {36, 90}, // frame 80
    {36, 88}, // frame 81
    {35, 84}, // frame 82
    {36, 85}, // frame 83
    {36, 87}, // frame 84
    {25, 87}, // frame 85
    {24, 88}, // frame 86
    {24, 85}, // frame 87
    {25, 82}, // frame 88
    {26, 83}, // frame 89
    {27, 85}, // frame 90
    {21, 82}, // frame 91
    {19, 81}, // frame 92
    {19, 79}, // frame 93
    {21, 76}, // frame 94
    {22, 78}, // frame 95
    {23, 80}, // frame 96
    {25, 76}, // frame 97
    {25, 73}, // frame 98
    {25, 72}, // frame 99
    {25, 71}, // frame 100
    {27, 72}, // frame 101
    {28, 73}, // frame 102
    {34, 70}, // frame 103
}};

bool runtimeFindMarineMuzzleAnchor(const std::string& imageId, int frameIndex,
    int width, int height, POINT& result) {
    if (toLowerAscii(imageId) != "dlph" || width != 74 || height != 158
        || frameIndex < 0 || frameIndex >= static_cast<int>(runtimeDolphinMuzzleFramePoints.size())) return false;
    result = runtimeDolphinMuzzleFramePoints[static_cast<std::size_t>(frameIndex)];
    return true;
}
