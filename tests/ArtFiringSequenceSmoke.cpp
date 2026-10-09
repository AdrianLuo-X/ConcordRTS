#include "ArtDatabase.h"
#include "IniFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const ArtType::SequenceFrame* findFrame(const ArtType& art, const std::string& name) {
    const auto found = std::find_if(art.sequenceFrames.begin(), art.sequenceFrames.end(),
        [&](const auto& frame) { return lower(frame.name) == lower(name); });
    return found == art.sequenceFrames.end() ? nullptr : &*found;
}

bool exact(const ArtType* art, const char* name, int start, int count, int stride) {
    const auto* frame = art ? findFrame(*art, name) : nullptr;
    if (frame && frame->present && frame->startFrame == start
        && frame->frameCount == count && frame->facingMultiplier == stride) return true;
    std::cerr << "native firing sequence differs: "
        << (art ? art->id : "<missing-art>") << '/' << name << '\n';
    return false;
}

bool exactZShapePoint(const ArtType* art, const std::array<int, 2>& expected) {
    if (art && art->zShapePointMove == expected
        && art->renderProfile.zShapePointMove == expected
        && art->buildingAnimProfile.zShapePointMove == expected) return true;
    std::cerr << "building ZShapePointMove differs: "
        << (art ? art->id : "<missing-art>") << " expected="
        << expected[0] << ',' << expected[1] << '\n';
    return false;
}

bool zShapePointFixture() {
    IniFile fixture;
    if (!fixture.loadFromString(R"INI(
[BuildingTypes]
0=POSITIVE
1=NEGATIVE
2=NEGATIVE_X
3=NEGATIVE_Y
4=DEFAULT
5=ZERO
[POSITIVE]
Foundation=4x3
ZShapePointMove=30,15
NormalZAdjust=-11
[NEGATIVE]
Foundation=4x3
ZShapePointMove=-24,-48
NormalZAdjust=19
[NEGATIVE_X]
Foundation=4x3
ZShapePointMove=-30,+15
[NEGATIVE_Y]
Foundation=4x3
ZShapePointMove=+30,-15
[DEFAULT]
Foundation=2x2
NormalZAdjust=-37
[ZERO]
Foundation=2x2
ZShapePointMove=0,0
)INI")) return false;
    ArtDatabase database;
    RulesDatabase rules;
    if (!database.loadFromIni(fixture) || !rules.loadFromIni(fixture)) return false;
    struct Expected { const char* id; std::array<int, 2> point; int normalZAdjust; };
    constexpr std::array<Expected, 6> expected{{
        {"POSITIVE", {30,15}, -11}, {"NEGATIVE", {-24,-48}, 19},
        {"NEGATIVE_X", {-30,15}, 0}, {"NEGATIVE_Y", {30,-15}, 0},
        {"DEFAULT", {0,0}, -37}, {"ZERO", {0,0}, 0}
    }};
    const auto links = database.buildLinks(rules);
    for (const auto& item : expected) {
        const auto* loaded = database.findArtType(item.id);
        if (!exactZShapePoint(loaded, item.point)) return false;
        const auto link = std::find_if(links.begin(), links.end(), [&](const ArtObjectLink& candidate) {
            return candidate.objectKind == "building" && candidate.objectId == item.id;
        });
        if (loaded->normalZAdjust != item.normalZAdjust
            || loaded->renderProfile.normalZAdjust != item.normalZAdjust
            || loaded->buildingAnimProfile.normalZAdjust != item.normalZAdjust
            || link == links.end() || !link->hasArt
            || link->zShapePointMove != item.point
            || link->renderProfile.zShapePointMove != item.point
            || link->buildingAnimProfile.zShapePointMove != item.point
            || link->normalZAdjust != item.normalZAdjust
            || link->renderProfile.normalZAdjust != item.normalZAdjust
            || link->buildingAnimProfile.normalZAdjust != item.normalZAdjust) {
            std::cerr << "building ZShapePointMove link/NormalZAdjust differs: " << item.id << '\n';
            return false;
        }
    }
    std::cout << "Building ZShapePointMove: signed axes, default, zero, profiles and links passed\n";
    return true;
}

bool nativeZShapePoints(const IniFile& raw, const ArtDatabase& database) {
    // Retail constants independently catch axis reversal and missing defaults.
    if (raw.getValue("GAREFN", "ZShapePointMove") != "30,15"
        || raw.getValue("GAAIRC", "ZShapePointMove") != "60,30"
        || !raw.getValue("GAPOWR", "ZShapePointMove").empty()
        || !exactZShapePoint(database.findArtType("GAREFN"), {30,15})
        || !exactZShapePoint(database.findArtType("GAAIRC"), {60,30})
        || !exactZShapePoint(database.findArtType("GAPOWR"), {0,0})) return false;
    int authored = 0;
    for (const auto& visual : database.getArtTypes()) {
        std::string tuple = raw.getValue(visual.id, "ZShapePointMove");
        if (tuple.empty()) continue;
        std::replace(tuple.begin(), tuple.end(), ',', ' ');
        std::istringstream input(tuple);
        std::array<int, 2> expected{};
        if (!(input >> expected[0] >> expected[1]) || !exactZShapePoint(&visual, expected)) return false;
        ++authored;
    }
    if (authored < 10) {
        std::cerr << "native building ZShapePointMove inventory under-covered\n";
        return false;
    }
    std::cout << "Native building ZShapePointMove passed: authored=" << authored << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_art_firing_sequence_smoke <native-artmd.ini>\n";
        return 1;
    }
    IniFile raw;
    ArtDatabase art;
    if (!raw.loadFromFile(argv[1]) || !art.loadFromIni(raw)) {
        std::cerr << "native ART database unavailable\n"; return 1;
    }
    if (!zShapePointFixture() || !nativeZShapePoints(raw, art)) return 1;
    // Separate native assets for AKM fire, stationary building designation,
    // and prone designation must survive loading with their exact frame spans.
    const auto* boris = art.findArtType("BORIS");
    if (!exact(boris, "FireUp", 169, 6, 6)
        || !exact(boris, "SecondaryFire", 401, 1, 1)
        || !exact(boris, "SecondaryProne", 217, 6, 6)) return 1;

    const auto* fortress = art.findArtType("BFRT");
    const std::array<std::array<int, 3>, 5> ports{{
        {45,190,90}, {45,-190,90}, {-120,200,80}, {-120,-200,80}, {220,0,130}}};
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (!fortress) return 1;
        auto value = fortress->alternateFireFLHs[i];
        std::replace(value.begin(), value.end(), ',', ' ');
        std::array<int, 3> point{};
        std::istringstream input(value);
        if (!(input >> point[0] >> point[1] >> point[2]) || point != ports[i]) {
            std::cerr << "native BFRT passenger port differs: " << i << '\n'; return 1;
        }
    }

    int types = 0, authored = 0, secondaryProne = 0, directions = 0;
    for (const auto& visual : art.getArtTypes()) {
        if (visual.sequence.empty()) continue;
        ++types;
        for (const char* key : {"Fire", "FireUp", "FireProne", "SecondaryFire",
                "SecondaryProne", "FireFly", "WetAttack", "DeployFire", "DeployedFire"}) {
            std::string tuple = raw.getValue(visual.sequence, key);
            if (tuple.empty()) continue;
            std::replace(tuple.begin(), tuple.end(), ',', ' ');
            std::istringstream input(tuple);
            std::array<int, 3> expected{};
            if (!(input >> expected[0] >> expected[1] >> expected[2]) || expected[1] <= 0) continue;
            if (!exact(&visual, key, expected[0], expected[1], expected[2])) return 1;
            const auto* loaded = findFrame(visual, key);
            // The original three integers define every direction's frame
            // interval. Verify both endpoints so a dropped stride or a frame
            // count accidentally treated as a last-index cannot pass.
            for (int facing = 0; facing < 8; ++facing) {
                const int first = expected[0] + facing * expected[2];
                const int last = first + expected[1] - 1;
                if (loaded->startFrame + facing * loaded->facingMultiplier != first
                    || loaded->startFrame + facing * loaded->facingMultiplier
                        + loaded->frameCount - 1 != last) return 1;
                ++directions;
            }
            ++authored;
            secondaryProne += lower(key) == "secondaryprone" ? 1 : 0;
        }
    }
    if (types < 20 || authored < 40 || secondaryProne < 1) {
        std::cerr << "native firing sequence inventory under-covered\n"; return 1;
    }
    std::cout << "Native firing sequences passed: artTypes=" << types
        << " firingTuples=" << authored << " directionalSpans=" << directions
        << " secondaryProne=" << secondaryProne << '\n';
    return 0;
}
