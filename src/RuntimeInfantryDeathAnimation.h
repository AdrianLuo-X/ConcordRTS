#pragma once

#include "ArtDatabase.h"
#include "RuntimeArcticArt.h"

#include <algorithm>
#include <optional>
#include <string>

struct RuntimeInfantryDeathSequence {
    std::string artId;
    std::string name;
    int startFrame = 0;
    int frameCount = 0;
};

inline std::optional<RuntimeInfantryDeathSequence> runtimeInfantryDeathSequence(
    const ArtDatabase& database,
    const UnitType& unit,
    int infDeath,
    int facing,
    bool inWater,
    bool arcticTheater = false
) {
    const ArtType* art = database.findArtType(runtimeArcticArtId(
        unit.image.empty() ? unit.id : unit.image, unit.alternateArcticArt, arcticTheater
    ));
    if (art == nullptr) {
        art = database.findArtType(unit.id);
    }
    if (art == nullptr || art->voxel) {
        return std::nullopt;
    }
    const int variant = unit.notHuman ? std::clamp(infDeath, 1, 5)
                                     : (infDeath == 2 ? 2 : 1);
    const std::string preferred = "Die" + std::to_string(variant);
    const std::string wet = infDeath == 2 ? "WetDie2" : "WetDie1";
    for (const std::string& name : {inWater ? wet : preferred, preferred,
                                   std::string("Die1"), std::string("Die2")}) {
        for (const ArtType::SequenceFrame& sequence : art->sequenceFrames) {
            if (sequence.name != name || !sequence.present
                || sequence.frameCount <= 0 || sequence.frameCount > 4096
                || sequence.startFrame < 0 || sequence.startFrame > 65535
                || sequence.facingMultiplier < 0
                || sequence.facingMultiplier > 4096) {
                continue;
            }
            // Stock infantry uses these Ready-frame placeholders for missing
            // alternate deaths. Fall back to a real death instead of standing up.
            if ((name == "Die3" || name == "Die4" || name == "Die5")
                && sequence.startFrame == 0 && sequence.frameCount == 1
                && sequence.facingMultiplier == 1) {
                continue;
            }
            const int facings = std::clamp(art->facings > 0 ? art->facings : 8, 1, 32);
            // Infantry source strips run counterclockwise from screen north.
            const int reversed = facing < 0 ? 0 : (256 - (facing % 256)) % 256;
            const int bucket = ((reversed * facings + 128) / 256) % facings;
            const int start = sequence.startFrame + bucket * sequence.facingMultiplier;
            if (start + sequence.frameCount > 65536) {
                continue;
            }
            return RuntimeInfantryDeathSequence{
                art->id, sequence.name, start, sequence.frameCount
            };
        }
    }
    return std::nullopt;
}
