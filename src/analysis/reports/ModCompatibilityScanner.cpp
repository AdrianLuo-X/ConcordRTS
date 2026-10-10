#include "analysis/reports/ModCompatibilityScanner.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

bool isNumericKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    return std::all_of(
        key.begin(),
        key.end(),
        [](unsigned char character) {
            return std::isdigit(character) != 0;
        }
    );
}

bool hasAnyPrefix(
    const std::string& key,
    const std::vector<std::string>& prefixes
) {
    const std::string normalized = toLower(key);
    for (const std::string& prefix : prefixes) {
        if (normalized.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool isKnownExtensionLikeKey(const std::string& key) {
    return key.find('.') != std::string::npos
        || key.find(':') != std::string::npos
        || key.find('@') != std::string::npos;
}

const std::unordered_set<std::string>& originalTags() {
    static const std::unordered_set<std::string> tags = {
        "uiname",
        "name",
        "image",
        "owner",
        "prerequisite",
        "strength",
        "category",
        "armor",
        "primary",
        "secondary",
        "eliteprimary",
        "elitesecondary",
        "techlevel",
        "buildlimit",
        "sight",
        "speed",
        "speedtype",
        "movementzone",
        "locomotor",
        "crusher",
        "crushable",
        "deployedcrushable",
        "omnicrusher",
        "omnicrushresistant",
        "crushsound",
        "cost",
        "points",
        "power",
        "powered",
        "factory",
        "buildcat",
        "superweapon",
        "superweapon2",
        "deploysinto",
        "undeploysinto",
        "freeunit",
        "refinery",
        "harvester",
        "storage",
        "dock",
        "damage",
        "rof",
        "range",
        "projectile",
        "warhead",
        "burst",
        "report",
        "anim",
        "bright",
        "cellspread",
        "percentatmax",
        "verses",
        "animlist",
        "infdeath",
        "pronedamage",
        "wall",
        "wood",
        "affectsallies",
        "foundation",
        "buildup",
        "remapable",
        "cameo",
        "sequence",
        "theater",
        "normalized",
        "newtheater"
    };
    return tags;
}

const std::unordered_set<std::string>& aresExactTags() {
    static const std::unordered_set<std::string> tags = {
        "useglobalradardetect",
        "prerequisite.override",
        "prerequisite.list",
        "buildlimit.allowbypass",
        "spyable.custom",
        "superweapons",
        "revengeweapon",
        "revengeweapon.affecthouses",
        "promote.veteranexperience",
        "promote.eliteexperience",
        "academy",
        "academy.types",
        "academy.ignore",
        "attachtoobject",
        "powered.killemitters",
        "powered.killspawns",
        "convertsfrom",
        "convertsinto",
        "ispassable",
        "passable",
        "insignia",
        "insignia.showenemy"
    };
    return tags;
}

const std::vector<std::string>& aresPrefixes() {
    static const std::vector<std::string> prefixes = {
        "attach.effect.",
        "attacheffect.",
        "spyeffect.",
        "superweapon.",
        "sw.",
        "convert.",
        "promote.",
        "academy.",
        "airstrike.",
        "radarvisible.",
        "powered.",
        "insignia."
    };
    return prefixes;
}

const std::unordered_set<std::string>& phobosExactTags() {
    static const std::unordered_set<std::string> tags = {
        "initialstrength",
        "shieldapplyarmormult",
        "radialindicatorradius",
        "radialindicatorcolor",
        "radialindicatorvisibility",
        "canpassiveacquire.guard",
        "canpassiveacquire.defensive",
        "mindcontrolrangelimit",
        "mindcontrolrangelimit.enabled",
        "passengerdeletion.type",
        "passengerdeletion.rate",
        "passengerdeletion.usecost",
        "deployfireweapon",
        "deployfireonce",
        "ammo.reloadrate",
        "ammo.reloadamount",
        "bounty",
        "bounty.value",
        "bounty.display",
        "shield.type",
        "shield.strength",
        "shield.armor",
        "digitaldisplaytypes",
        "tint.color",
        "tint.intensity",
        "lasertrail.types",
        "interceptor",
        "interceptor.weapon"
    };
    return tags;
}

const std::vector<std::string>& phobosPrefixes() {
    static const std::vector<std::string> prefixes = {
        "shield.",
        "digitaldisplay.",
        "digitaldisplaytypes.",
        "lasertrail.",
        "interceptor.",
        "autodeath.",
        "bounty.",
        "passengerdeletion.",
        "ammo.",
        "tint.",
        "radialindicator",
        "selectbox.",
        "healthbar.",
        "attachedparticle."
    };
    return prefixes;
}

bool isAresTag(const std::string& key) {
    const std::string normalized = toLower(key);
    return aresExactTags().find(normalized) != aresExactTags().end()
        || hasAnyPrefix(normalized, aresPrefixes());
}

bool isPhobosTag(const std::string& key) {
    const std::string normalized = toLower(key);
    return phobosExactTags().find(normalized) != phobosExactTags().end()
        || hasAnyPrefix(normalized, phobosPrefixes());
}

void addFinding(
    ModCompatibilityReport& report,
    const std::string& severity,
    const std::string& source,
    const std::string& section,
    const std::string& key,
    const std::string& value,
    const std::string& message
) {
    report.findings.push_back({
        severity,
        source,
        section,
        key,
        value,
        message
    });
}

void addLevel(
    ModCompatibilityReport& report,
    int level,
    const std::string& name,
    bool achieved,
    const std::string& detail
) {
    report.levels.push_back({level, name, achieved, detail});
}

void scanIniTags(
    ModCompatibilityReport& report,
    const IniFile& ini,
    const std::string& source,
    const RulesDatabase& rules,
    const IniFile* artIni
) {
    constexpr int maxUnknownFindings = 40;
    int unknownFindings = 0;
    std::set<std::string> unknownKeys;

    for (const std::string& sectionName : ini.getSectionNames()) {
        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) {
            continue;
        }

        for (const auto& [key, value] : *section) {
            if (isNumericKey(key)) {
                continue;
            }

            const auto normalized = toLower(key);
            if (normalized == "superweapons" && toLower(sectionName) == "iq") continue;
            const bool shieldSection = rules.findShieldType(sectionName) != nullptr;
            const bool legacyExtra = normalized == "superweapon3" || normalized == "superweapon4";
            if (shieldSection || isAresTag(key) || isPhobosTag(key) || legacyExtra) {
                const bool isAres = !shieldSection && isAresTag(key);
                const std::string family = legacyExtra ? "Unverified" : isAres ? "Ares" : "Phobos";
                if (isAres) ++report.aresTagCount; else if (!legacyExtra) ++report.phobosTagCount;
                else ++report.unknownTagCount;
                const bool techno = rules.findTechnoModExtension(sectionName) != nullptr;
                const bool warhead = rules.findWarheadType(sectionName) != nullptr;
                const std::unordered_set<std::string> commonAttach{
                    "attacheffect.duration", "attacheffect.animation", "attacheffect.speedmultiplier",
                    "attacheffect.armormultiplier", "attacheffect.firepowermultiplier", "attacheffect.rofmultiplier",
                    "attacheffect.temporalhidesanim", "attacheffect.cloakable", "attacheffect.forcedecloak",
                    "attacheffect.discardonentry", "attacheffect.penetratesironcurtain"};
                const std::unordered_set<std::string> shieldFields{
                    "strength", "initialstrength", "armor", "inheritarmorfromtechno", "applyarmormult",
                    "powered", "absorboverdamage", "selfhealing", "selfhealing.rate", "selfhealing.restartincombat",
                    "selfhealing.restartincombatdelay", "respawn", "respawn.rate", "respawn.restartincombat",
                    "respawn.restartincombatdelay"};
                bool supported = source != "art" && (
                    (shieldSection && shieldFields.contains(normalized))
                    || ((techno || warhead) && commonAttach.contains(normalized))
                    || (techno && (normalized == "shield.type" || normalized == "attacheffect.delay" || normalized == "attacheffect.initialdelay"))
                    || (warhead && (normalized == "shield.penetrate" || normalized == "attacheffect.cumulative" || normalized == "attacheffect.animresetonreapply"))
                    || (toLower(sectionName) == "combatdamage" && normalized == "shieldapplyarmormult")
                    || (rules.findBuildingType(sectionName) && normalized == "superweapons"));
                std::string detail = supported ? "Implemented in the documented baseline; see docs/assets/mod_extension_support.md."
                    : legacyExtra ? "Not a documented Ares extra-superweapon field. Use BuildingType SuperWeapons=."
                    : "Unsupported field or section; detection does not enable gameplay.";
                if (supported && normalized == "attacheffect.animation" && artIni
                    && artIni->getValue(value, "Damage", "0") != "0") {
                    supported = false;
                    detail = "Attached animation renders, but authored animation Damage is not supported.";
                }
                report.tagHits.push_back({family, sectionName, key, value, supported, detail});
                if (!supported) addFinding(report, "warning", source, sectionName, key, value, detail);
                continue;
            }
            if (originalTags().find(normalized) != originalTags().end()) {
                continue;
            }

            if (isAresTag(key)) {
                ++report.aresTagCount;
                report.tagHits.push_back({"Ares", sectionName, key, value});
                continue;
            }

            if (isPhobosTag(key)) {
                ++report.phobosTagCount;
                report.tagHits.push_back({"Phobos", sectionName, key, value});
                continue;
            }

            if (!isKnownExtensionLikeKey(key)) {
                continue;
            }

            ++report.unknownTagCount;
            if (unknownFindings >= maxUnknownFindings) {
                continue;
            }

            const std::string unknownKey = source + "|" + sectionName + "|" + key;
            if (unknownKeys.insert(unknownKey).second) {
                ++unknownFindings;
                addFinding(
                    report,
                    "info",
                    source,
                    sectionName,
                    key,
                    value,
                    "Unknown extension-like tag; engine will preserve it for reports but does not implement behavior yet."
                );
            }
        }
    }

    if (report.unknownTagCount > maxUnknownFindings) {
        addFinding(
            report,
            "info",
            source,
            "*",
            "*",
            std::to_string(report.unknownTagCount),
            "Unknown tag list truncated in text output."
        );
    }
}

int countListEntries(const IniFile& ini, const std::string& sectionName) {
    const IniFile::Section* section = ini.getSection(sectionName);
    return section == nullptr ? 0 : static_cast<int>(section->size());
}

} // namespace

ModCompatibilityReport ModCompatibilityScanner::scan(
    const IniFile& rulesIni,
    const RulesDatabase& rules,
    const IniFile* artIni,
    const IniFile* mapIni,
    const ModCompatibilityScanOptions& options
) const {
    ModCompatibilityReport report;
    report.success = true;

    report.infantryTypes = static_cast<int>(rules.getInfantryTypes().size());
    report.vehicleTypes = static_cast<int>(rules.getVehicleTypes().size());
    report.aircraftTypes = static_cast<int>(rules.getAircraftTypes().size());
    report.buildingTypes = static_cast<int>(rules.getBuildingTypes().size());
    report.weaponTypes = static_cast<int>(rules.getWeaponTypes().size());
    report.projectileTypes = static_cast<int>(rules.getProjectileTypes().size());
    report.warheadTypes = static_cast<int>(rules.getWarheadTypes().size());
    report.customObjectCount =
        report.infantryTypes
        + report.vehicleTypes
        + report.aircraftTypes
        + report.buildingTypes
        + report.weaponTypes
        + report.projectileTypes
        + report.warheadTypes;

    if (artIni != nullptr) {
        report.artSections = static_cast<int>(artIni->getSectionNames().size());
    }
    if (mapIni != nullptr) {
        report.mapSections = static_cast<int>(mapIni->getSectionNames().size());
    }

    for (const std::string& warning : rules.getWarnings()) {
        addFinding(
            report,
            "warning",
            "rules",
            "*",
            "*",
            warning,
            "Rules parser warning."
        );
    }

    scanIniTags(report, rulesIni, "rules", rules, artIni);
    if (artIni != nullptr) {
        scanIniTags(report, *artIni, "art", rules, artIni);
    }
    if (mapIni != nullptr) {
        scanIniTags(report, *mapIni, "map", rules, artIni);
    }

    if (report.aresTagCount > 0) {
        addFinding(
            report,
            "warning",
            "rules/art/map",
            "*",
            "Ares",
            std::to_string(report.aresTagCount),
            "Ares fields have individual support results; unsupported fields remain warnings."
        );
    }

    if (report.phobosTagCount > 0) {
        addFinding(
            report,
            "warning",
            "rules/art/map",
            "*",
            "Phobos",
            std::to_string(report.phobosTagCount),
            "Phobos fields have individual support results; unsupported fields remain warnings."
        );
    }

    const bool rulesReadable =
        countListEntries(rulesIni, "InfantryTypes") > 0
        || countListEntries(rulesIni, "VehicleTypes") > 0
        || countListEntries(rulesIni, "BuildingTypes") > 0;
    const bool artReadable = options.hasArtIni && artIni != nullptr && report.artSections > 0;
    const bool objectsRecognized =
        report.customObjectCount > 0 && report.weaponTypes > 0 && report.warheadTypes > 0;
    const bool mapReadable = options.hasMapIni && mapIni != nullptr && report.mapSections > 0;

    addLevel(report, 0, "Scan MOD INI", true, "INI scanner executed.");
    addLevel(
        report,
        1,
        "Read modified rulesmd.ini",
        rulesReadable,
        "Object list sections and rules database were loaded."
    );
    addLevel(
        report,
        2,
        "Read modified artmd.ini",
        artReadable,
        artReadable
            ? "ART INI was loaded and section count is available."
            : "No ART INI supplied for this scan."
    );
    addLevel(
        report,
        3,
        "Recognize custom units/weapons/warheads",
        objectsRecognized,
        "Rules object, weapon, and warhead catalogs are available."
    );
    addLevel(
        report,
        4,
        "Load custom map",
        mapReadable,
        mapReadable
            ? "Map INI was loaded for compatibility scanning."
            : "No map INI supplied for this scan."
    );
    addLevel(
        report,
        5,
        "Scan common Ares tags",
        true,
        "Ares tag scanner ran; found " + std::to_string(report.aresTagCount) + " hits."
    );
    addLevel(
        report,
        6,
        "Scan common Phobos tags",
        true,
        "Phobos tag scanner ran; found " + std::to_string(report.phobosTagCount) + " hits."
    );
    addLevel(
        report,
        7,
        "Mainstream MOD-specific compatibility",
        false,
        "Not implemented yet; this report identifies missing feature work."
    );
    addLevel(
        report,
        8,
        "Do not promise universal MOD compatibility",
        false,
        "Explicit non-goal: the engine reports unsupported tags instead of pretending to support everything."
    );

    report.achievedLevel = 0;
    for (const ModCompatibilityLevelResult& level : report.levels) {
        if (!level.achieved) {
            break;
        }
        report.achievedLevel = level.level;
    }

    return report;
}
