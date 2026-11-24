#pragma once
#include "data/maps/MapLightingValues.h"
#include "core/io/IniFile.h"
#include <string>
#include <vector>
namespace concord::map_lighting {
// Numeric reads borrow the exact current Map parser and warnings owner.
// The callback is used synchronously and never retained.
template<class ReadDouble>
void readLightingProfile(
    const IniFile& ini,
    const std::string& prefix,
    MapLightingProfile& profile,
    std::vector<std::string>& warnings,
    const ReadDouble& readDoubleField
) {
    profile.ambient = readDoubleField(
        ini,
        "Lighting",
        prefix + "Ambient",
        profile.ambient,
        warnings
    );
    profile.red = readDoubleField(
        ini,
        "Lighting",
        prefix + "Red",
        profile.red,
        warnings
    );
    profile.green = readDoubleField(
        ini,
        "Lighting",
        prefix + "Green",
        profile.green,
        warnings
    );
    profile.blue = readDoubleField(
        ini,
        "Lighting",
        prefix + "Blue",
        profile.blue,
        warnings
    );
    profile.ground = readDoubleField(
        ini,
        "Lighting",
        prefix + "Ground",
        profile.ground,
        warnings
    );
    profile.level = readDoubleField(
        ini,
        "Lighting",
        prefix + "Level",
        profile.level,
        warnings
    );
}

template<class ReadDouble>
MapLighting parseLightingSection(
    const IniFile& ini,
    std::vector<std::string>& warnings,
    const ReadDouble& readDoubleField
) {
    MapLighting lighting;
    const IniFile::Section* section = ini.getSection("Lighting");
    lighting.sectionPresent = section != nullptr;
    lighting.authoredFieldCount = section == nullptr ? 0 : section->size();

    readLightingProfile(ini, "", lighting.base, warnings, readDoubleField);
    readLightingProfile(ini, "Ion", lighting.ion, warnings, readDoubleField);
    readLightingProfile(ini, "Dominator", lighting.dominator, warnings, readDoubleField);
    lighting.dominatorAmbientChangeRate = readDoubleField(
        ini,
        "Lighting",
        "DominatorAmbientChangeRate",
        lighting.dominatorAmbientChangeRate,
        warnings
    );
    return lighting;
}
} // namespace concord::map_lighting
