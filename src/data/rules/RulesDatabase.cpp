#include "data/rules/RulesDatabase.h"
#include "data/rules/ProductionRulesLoader.h"
#include "data/rules/RuntimeDetailRulesHash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr double maximumProductionCoefficient = 100.0;
constexpr double maximumProductionRatio = 1.0;

constexpr std::array<const char*, 17> ifvModePrefixes{
    "Normal",
    "Repair",
    "MachineGun",
    "Flak",
    "Pistol",
    "Sniper",
    "Shock",
    "Explode",
    "BrainBlast",
    "RadCannon",
    "Chrono",
    "TerroristExplode",
    "Cow",
    "Initiate",
    "Virus",
    "YuriPrime",
    "Guardian"
};

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

std::string trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::string canonicalIdKey(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(whitespace);
    std::string key;
    key.reserve(last - first + 1);
    for (std::size_t index = first; index <= last; ++index) {
        key.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[index]))
        ));
    }
    return key;
}

bool rulesDatabaseTraceEnabled() {
    const char* rawValue = std::getenv("CONCORD_RULES_TRACE");
    if (rawValue == nullptr || rawValue[0] == '\0') {
        return false;
    }

    const std::string value(rawValue);
    return value != "0"
        && value != "false"
        && value != "False"
        && value != "no"
        && value != "off";
}

bool rulesDatabaseTraceIdsEnabled() {
    const char* rawValue = std::getenv("CONCORD_RULES_TRACE_IDS");
    if (rawValue == nullptr || rawValue[0] == '\0') {
        return false;
    }

    const std::string value(rawValue);
    return value != "0"
        && value != "false"
        && value != "False"
        && value != "no"
        && value != "off";
}

void traceRulesDatabaseLoad(const std::string& message) {
    if (!rulesDatabaseTraceEnabled()) {
        return;
    }
    std::cerr << "RulesDatabase::loadFromIni: " << message << "\n";
}

template <typename Object>
void traceTypeIdHealth(
    const std::vector<Object>& objects,
    const std::string& label
) {
    if (!rulesDatabaseTraceIdsEnabled()) {
        return;
    }

    std::cerr << "RulesDatabase::loadFromIni: id-health "
              << label
              << " count="
              << objects.size();
    const std::size_t sampleCount = std::min<std::size_t>(objects.size(), 4U);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const std::size_t idSize = objects[index].id.size();
        std::cerr << " item"
                  << index
                  << " size="
                  << idSize;
        if (idSize <= 64U) {
            std::cerr << " value="
                      << objects[index].id;
        } else {
            std::cerr << " value=<oversized>";
        }
    }
    std::cerr << "\n";
}

template <typename Object>
void reconstructObjectId(Object& object, const std::string& id) {
    object.id = id;
}

template <typename Object>
void repairLoadedTypeIdsFromList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<Object>& objects
) {
    const IniFile::Section* section = ini.getSection(typeListSection);
    if (section == nullptr) {
        return;
    }

    std::size_t objectIndex = 0;
    for (const auto& [indexText, objectId] : *section) {
        if (objectIndex >= objects.size()) {
            break;
        }
        if (ini.getSection(objectId) == nullptr) {
            continue;
        }

        reconstructObjectId(objects[objectIndex], objectId);
        ++objectIndex;
    }
}

void repairLoadedOverlayIds(
    const std::vector<std::string>& stableIds,
    std::vector<OverlayType>& overlays
) {
    // Overlay records retain missing-section placeholders and collapse duplicate
    // IDs. Use the same order as loadOverlayList, never the generic
    // section-only repair that would rename later records without their data.
    for (std::size_t index = 0; index < std::min(stableIds.size(), overlays.size()); ++index) {
        reconstructObjectId(overlays[index], stableIds[index]);
    }
}

std::vector<std::string> collectAuthoredTypeIdsFromList(
    const IniFile& ini,
    const std::string& typeListSection
) {
    std::vector<std::string> ids;
    const IniFile::Section* section = ini.getSection(typeListSection);
    if (section == nullptr) {
        return ids;
    }
    ids.reserve(section->size());
    for (const auto& [indexText, objectId] : *section) {
        (void)indexText;
        ids.push_back(objectId);
    }
    return ids;
}

std::vector<std::string> collectTypeIdsFromList(
    const IniFile& ini,
    const std::string& typeListSection,
    bool requireObjectSection,
    bool deduplicate
) {
    std::vector<std::string> ids;
    const IniFile::Section* section = ini.getSection(typeListSection);
    if (section == nullptr) {
        return ids;
    }

    std::unordered_set<std::string> loadedIds;
    ids.reserve(section->size());
    for (const auto& [indexText, objectId] : *section) {
        if (objectId.empty()) {
            continue;
        }
        if (requireObjectSection && ini.getSection(objectId) == nullptr) {
            continue;
        }

        const std::string key = canonicalIdKey(objectId);
        if (key.empty()) {
            continue;
        }
        if (deduplicate) {
            if (loadedIds.find(key) != loadedIds.end()) {
                continue;
            }
            loadedIds.insert(key);
        }
        ids.push_back(objectId);
    }
    return ids;
}

template <typename Object>
void rebuildTypeIndexWithStableIds(
    const std::vector<Object>& objects,
    const std::vector<std::string>& stableIds,
    std::unordered_map<std::string, std::size_t>& index,
    std::vector<std::string>& warnings,
    const std::string& label
) {
    if (rulesDatabaseTraceEnabled()) {
        std::cerr << "rebuildTypeIndex: begin " << label
                  << " count=" << objects.size()
                  << " source=stable-list\n";
    }
    index.clear();
    index.reserve(std::min(objects.size(), stableIds.size()));
    if (stableIds.size() != objects.size()) {
        warnings.push_back(
            "Stable " + label
            + " id list size mismatch: ids="
            + std::to_string(stableIds.size())
            + " objects="
            + std::to_string(objects.size())
        );
    }
    const std::size_t count = std::min(objects.size(), stableIds.size());
    for (std::size_t objectIndex = 0; objectIndex < count; ++objectIndex) {
        const std::string key = canonicalIdKey(stableIds[objectIndex]);
        if (key.empty()) {
            continue;
        }

        const auto [it, inserted] = index.emplace(key, objectIndex);
        if (!inserted) {
            warnings.push_back(
                "Duplicate " + label
                + " id ignored by capability index: ["
                + stableIds[objectIndex] + "]"
            );
        }
    }
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;

    for (const char character : value) {
        if (character == ',' || character == ';') {
            const std::string token = trim(current);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    const std::string token = trim(current);
    if (!token.empty()) {
        tokens.push_back(token);
    }

    return tokens;
}

std::vector<std::string> mergeUniqueIniLists(
    const IniFile& ini,
    const std::string& section,
    std::initializer_list<const char*> keys
) {
    std::vector<std::string> merged;
    std::unordered_set<std::string> seen;
    for (const char* key : keys) {
        for (const std::string& id : splitList(
                 ini.getValue(section, key, "")
             )) {
            const std::string canonical = canonicalIdKey(id);
            if (!canonical.empty() && seen.insert(canonical).second) {
                merged.push_back(id);
            }
        }
    }
    return merged;
}

ParticleBehavior parseParticleBehavior(const std::string& value) {
    const std::string normalized = toLower(trim(value));
    if (normalized.empty() || normalized == "none") {
        return ParticleBehavior::None;
    }
    if (normalized == "gas") {
        return ParticleBehavior::Gas;
    }
    if (normalized == "smoke") {
        return ParticleBehavior::Smoke;
    }
    if (normalized == "fire") {
        return ParticleBehavior::Fire;
    }
    if (normalized == "spark") {
        return ParticleBehavior::Spark;
    }
    if (normalized == "railgun") {
        return ParticleBehavior::Railgun;
    }
    if (normalized == "web") {
        return ParticleBehavior::Web;
    }
    if (normalized == "weakgas") {
        return ParticleBehavior::WeakGas;
    }
    return ParticleBehavior::Unknown;
}

bool parseFiniteDouble(const std::string& value, double& result) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(trim(value), &consumed);
        if (consumed != trim(value).size() || !std::isfinite(parsed)) {
            return false;
        }
        result = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseParticleVector3(
    const std::string& value,
    ParticleVector3& result
) {
    const std::vector<std::string> values = splitList(value);
    if (values.size() != 3U) {
        return false;
    }
    return parseFiniteDouble(values[0], result.x)
        && parseFiniteDouble(values[1], result.y)
        && parseFiniteDouble(values[2], result.z);
}

bool parseParticleColor(
    const std::string& value,
    ParticleColor& result
) {
    std::string normalized = trim(value);
    if (normalized.size() >= 2U
        && normalized.front() == '('
        && normalized.back() == ')') {
        normalized = normalized.substr(1, normalized.size() - 2U);
    }
    const std::vector<std::string> values = splitList(normalized);
    if (values.size() != 3U) {
        return false;
    }
    try {
        result.red = std::clamp(std::stoi(values[0]), 0, 255);
        result.green = std::clamp(std::stoi(values[1]), 0, 255);
        result.blue = std::clamp(std::stoi(values[2]), 0, 255);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<ParticleColor> parseParticleColorList(
    const std::string& value,
    bool& valid
) {
    valid = true;
    std::vector<ParticleColor> colors;
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const std::size_t open = value.find('(', cursor);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = value.find(')', open + 1U);
        if (close == std::string::npos) {
            valid = false;
            break;
        }
        ParticleColor color;
        if (!parseParticleColor(value.substr(open, close - open + 1U), color)) {
            valid = false;
        } else {
            colors.push_back(color);
        }
        cursor = close + 1U;
    }
    if (colors.empty() && !trim(value).empty()) {
        ParticleColor color;
        if (parseParticleColor(value, color)) {
            colors.push_back(color);
        } else {
            valid = false;
        }
    }
    return colors;
}

std::optional<VeteranAbility> parseVeteranAbility(
    const std::string& value
) {
    const std::string normalized = toLower(trim(value));
    if (normalized == "faster") {
        return VeteranAbility::Faster;
    }
    if (normalized == "stronger") {
        return VeteranAbility::Stronger;
    }
    if (normalized == "firepower") {
        return VeteranAbility::Firepower;
    }
    if (normalized == "scatter") {
        return VeteranAbility::Scatter;
    }
    if (normalized == "rof") {
        return VeteranAbility::Rof;
    }
    if (normalized == "sight") {
        return VeteranAbility::Sight;
    }
    if (normalized == "cloak") {
        return VeteranAbility::Cloak;
    }
    if (normalized == "tiberium_proof") {
        return VeteranAbility::TiberiumProof;
    }
    if (normalized == "vein_proof") {
        return VeteranAbility::VeinProof;
    }
    if (normalized == "self_heal") {
        return VeteranAbility::SelfHeal;
    }
    if (normalized == "explodes") {
        return VeteranAbility::Explodes;
    }
    if (normalized == "radar_invisible") {
        return VeteranAbility::RadarInvisible;
    }
    if (normalized == "sensors") {
        return VeteranAbility::Sensors;
    }
    if (normalized == "fearless") {
        return VeteranAbility::Fearless;
    }
    if (normalized == "c4") {
        return VeteranAbility::C4;
    }
    if (normalized == "tiberium_heal") {
        return VeteranAbility::TiberiumHeal;
    }
    if (normalized == "guard_area") {
        return VeteranAbility::GuardArea;
    }
    if (normalized == "crusher") {
        return VeteranAbility::Crusher;
    }
    return std::nullopt;
}

std::string firstNonEmptyRuleValue(
    const IniFile& ini,
    const std::string& section,
    std::initializer_list<const char*> keys
) {
    for (const char* key : keys) {
        const std::string value = trim(ini.getValue(section, key, ""));
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

std::vector<int> parseIntegerList(const std::string& value) {
    std::vector<int> numbers;
    for (const std::string& token : splitList(value)) {
        try {
            std::size_t parsed = 0;
            const int number = std::stoi(token, &parsed);
            if (parsed > 0) {
                numbers.push_back(number);
            }
        } catch (const std::exception&) {
            break;
        }
    }
    return numbers;
}

RefinerySmokeProfile loadRefinerySmokeProfile(
    const IniFile& ini,
    const std::string& buildingId
) {
    RefinerySmokeProfile profile;
    const std::vector<std::pair<std::string, std::string>> offsetKeys{
        {"One", "RefinerySmokeOffsetOne"},
        {"Two", "RefinerySmokeOffsetTwo"},
        {"Three", "RefinerySmokeOffsetThree"},
        {"Four", "RefinerySmokeOffsetFour"}
    };

    for (const auto& [name, key] : offsetKeys) {
        const std::string value = ini.getValue(buildingId, key, "");
        if (trim(value).empty()) {
            continue;
        }

        const std::vector<int> values = parseIntegerList(value);
        if (values.size() < 3) {
            continue;
        }

        RefinerySmokeOffset offset;
        offset.name = name;
        offset.x = values[0];
        offset.y = values[1];
        offset.z = values[2];
        profile.offsets.push_back(offset);
    }

    profile.frames = std::max(
        0,
        [&ini, &buildingId]() {
            const std::string value =
                ini.getValue(buildingId, "RefinerySmokeFrames", "");
            if (trim(value).empty()) {
                return 0;
            }
            try {
                return std::stoi(value);
            } catch (const std::exception&) {
                return 0;
            }
        }()
    );
    profile.particleSystem =
        trim(ini.getValue(buildingId, "RefinerySmokeParticleSystem", ""));
    if (!profile.particleSystem.empty()) {
        profile.particle =
            trim(ini.getValue(profile.particleSystem, "HoldsWhat", ""));
        if (!profile.particle.empty()) {
            profile.image = trim(ini.getValue(profile.particle, "Image", ""));
        }
    }
    return profile;
}

bool containsValue(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool containsValueIgnoreCase(
    const std::unordered_set<std::string>& values,
    const std::string& value
) {
    return values.find(canonicalIdKey(value)) != values.end();
}

template <typename Object>
void rebuildTypeIndex(
    const std::vector<Object>& objects,
    std::unordered_map<std::string, std::size_t>& index,
    std::vector<std::string>& warnings,
    const std::string& label
) {
    if (rulesDatabaseTraceEnabled()) {
        std::cerr << "rebuildTypeIndex: begin " << label
                  << " count=" << objects.size() << "\n";
    }
    index.clear();
    index.reserve(objects.size());

    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        if (rulesDatabaseTraceEnabled()) {
            std::cerr << "rebuildTypeIndex: " << label
                      << " item=" << objectIndex;
            if (rulesDatabaseTraceIdsEnabled()) {
                std::cerr << " id=" << objects[objectIndex].id;
            }
            std::cerr << "\n";
        }
        const std::size_t idSize = objects[objectIndex].id.size();
        if (idSize > 256U) {
            warnings.push_back(
                "Corrupt " + label
                + " id ignored by capability index at item "
                + std::to_string(objectIndex)
            );
            continue;
        }

        const std::string key = canonicalIdKey(objects[objectIndex].id);
        if (key.empty()) {
            continue;
        }

        const auto [it, inserted] = index.emplace(key, objectIndex);
        if (!inserted) {
            warnings.push_back(
                "Duplicate " + label
                + " id ignored by capability index: ["
                + objects[objectIndex].id + "]"
            );
        }
    }
    if (rulesDatabaseTraceEnabled()) {
        std::cerr << "rebuildTypeIndex: done " << label
                  << " index=" << index.size() << "\n";
    }
}

template <typename Object>
const Object* findByCanonicalId(
    const std::vector<Object>& objects,
    const std::unordered_map<std::string, std::size_t>& index,
    const std::string& id
) {
    const std::string key = canonicalIdKey(id);
    if (key.empty()) {
        return nullptr;
    }

    const auto it = index.find(key);
    if (it == index.end() || it->second >= objects.size()) {
        return nullptr;
    }

    return &objects[it->second];
}

struct RulesLifecycleCache {
    std::vector<WeaponLifecycleProfile> weaponProfiles;
    std::vector<WarheadLifecycleProfile> warheadProfiles;
    std::unordered_map<std::string, std::size_t> weaponIndexById;
    std::unordered_map<std::string, std::size_t> warheadIndexById;
};

std::unordered_map<const RulesDatabase*, RulesLifecycleCache>& lifecycleCaches() {
    static std::unordered_map<const RulesDatabase*, RulesLifecycleCache> caches;
    return caches;
}

void resetLifecycleCache(const RulesDatabase* owner) {
    if (owner == nullptr) {
        return;
    }

    lifecycleCaches()[owner] = RulesLifecycleCache{};
}

void cacheWeaponLifecycleProfile(
    const RulesDatabase* owner,
    WeaponLifecycleProfile profile
) {
    if (owner == nullptr) {
        return;
    }

    const std::string key = canonicalIdKey(profile.id);
    if (key.empty()) {
        return;
    }

    RulesLifecycleCache& cache = lifecycleCaches()[owner];
    const auto existing = cache.weaponIndexById.find(key);
    if (existing != cache.weaponIndexById.end()) {
        if (existing->second < cache.weaponProfiles.size()) {
            cache.weaponProfiles[existing->second] = std::move(profile);
            return;
        }
        cache.weaponIndexById.erase(existing);
    }

    cache.weaponIndexById.emplace(key, cache.weaponProfiles.size());
    cache.weaponProfiles.push_back(std::move(profile));
}

void cacheWarheadLifecycleProfile(
    const RulesDatabase* owner,
    WarheadLifecycleProfile profile
) {
    if (owner == nullptr) {
        return;
    }

    const std::string key = canonicalIdKey(profile.id);
    if (key.empty()) {
        return;
    }

    RulesLifecycleCache& cache = lifecycleCaches()[owner];
    const auto existing = cache.warheadIndexById.find(key);
    if (existing != cache.warheadIndexById.end()) {
        if (existing->second < cache.warheadProfiles.size()) {
            cache.warheadProfiles[existing->second] = std::move(profile);
            return;
        }
        cache.warheadIndexById.erase(existing);
    }

    cache.warheadIndexById.emplace(key, cache.warheadProfiles.size());
    cache.warheadProfiles.push_back(std::move(profile));
}

bool isNoneValue(const std::string& value) {
    const std::string normalized = toLower(trim(value));
    return normalized == "none"
        || normalized == "<none>";
}

bool isIntegerToken(const std::string& value) {
    const std::string normalized = trim(value);
    if (normalized.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (normalized[index] == '-' || normalized[index] == '+') {
        ++index;
    }

    if (index >= normalized.size()) {
        return false;
    }

    for (; index < normalized.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(normalized[index]))) {
            return false;
        }
    }

    return true;
}

bool startsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }

    return toLower(value.substr(0, prefix.size())) == toLower(prefix);
}

bool containsIgnoreCase(const std::string& value, const std::string& needle) {
    return toLower(value).find(toLower(needle)) != std::string::npos;
}

std::string deployedWeaponForObject(
    bool deployFire,
    const std::string& secondaryWeapon
) {
    if (!deployFire || isNoneValue(secondaryWeapon)) {
        return "";
    }

    return secondaryWeapon;
}

void assignDeployWeapons(
    const IniFile& ini,
    const std::string& id,
    bool deployFire,
    const std::string& secondaryWeapon,
    const std::string& eliteSecondaryWeapon,
    std::string& deployWeapon,
    std::string& eliteDeployWeapon,
    std::string& deployWeaponSource,
    std::string& eliteDeployWeaponSource
) {
    const std::string explicitDeployWeapon =
        ini.getValue(id, "DeployWeapon", "");
    const std::string explicitEliteDeployWeapon =
        ini.getValue(id, "EliteDeployWeapon", "");

    if (!explicitDeployWeapon.empty() && !isNoneValue(explicitDeployWeapon)) {
        deployWeapon = explicitDeployWeapon;
        deployWeaponSource = "DeployWeapon";
    } else {
        deployWeapon = deployedWeaponForObject(deployFire, secondaryWeapon);
        deployWeaponSource = deployWeapon.empty() ? "" : "Secondary";
    }

    if (!explicitEliteDeployWeapon.empty()
        && !isNoneValue(explicitEliteDeployWeapon)) {
        eliteDeployWeapon = explicitEliteDeployWeapon;
        eliteDeployWeaponSource = "EliteDeployWeapon";
    } else {
        eliteDeployWeapon =
            deployedWeaponForObject(deployFire, eliteSecondaryWeapon);
        eliteDeployWeaponSource =
            eliteDeployWeapon.empty() ? "" : "EliteSecondary";
    }
}

bool normalizedEquals(const std::string& value, const std::string& expected) {
    return toLower(trim(value)) == toLower(expected);
}

bool hasMeaningfulValue(const std::string& value) {
    return !trim(value).empty() && !isNoneValue(value);
}

double parsePercentToken(const std::string& value, bool& valid) {
    std::string normalized = trim(value);
    if (!normalized.empty() && normalized.back() == '%') {
        normalized.pop_back();
    }

    if (normalized.empty()) {
        valid = false;
        return 0.0;
    }

    try {
        std::size_t parsed = 0;
        const double number = std::stod(normalized, &parsed);
        valid = parsed == normalized.size();
        return valid ? number : 0.0;
    } catch (const std::exception&) {
        valid = false;
        return 0.0;
    }
}

std::vector<double> parseVersesPercentages(
    const std::string& verses,
    bool& valid
) {
    std::vector<double> values;
    valid = false;
    if (!hasMeaningfulValue(verses)) {
        return values;
    }

    bool allTokensValid = true;
    for (const std::string& token : splitList(verses)) {
        bool tokenValid = false;
        const double percentage = parsePercentToken(token, tokenValid);
        if (!tokenValid) {
            allTokensValid = false;
            continue;
        }
        values.push_back(percentage);
    }

    valid = allTokensValid && !values.empty();
    return values;
}

bool anyPositiveInRange(
    const std::vector<double>& values,
    std::size_t begin,
    std::size_t end
) {
    const std::size_t clampedEnd = std::min(end, values.size());
    for (std::size_t index = begin; index < clampedEnd; ++index) {
        if (values[index] > 0.0) {
            return true;
        }
    }
    return false;
}

std::string sourceIfPresent(
    const std::string& value,
    const std::string& source
) {
    return hasMeaningfulValue(value) ? source : "";
}

WeaponSet weaponSetForObject(
    const std::string& primary,
    const std::string& secondary,
    const std::string& elitePrimary,
    const std::string& eliteSecondary,
    const std::string& deployWeapon,
    const std::string& eliteDeployWeapon,
    const std::string& deployWeaponSource,
    const std::string& eliteDeployWeaponSource,
    const std::string& occupyWeapon,
    const std::string& eliteOccupyWeapon,
    int openTransportWeapon
) {
    WeaponSet set;
    set.primary = primary;
    set.primarySource = sourceIfPresent(primary, "Primary");
    set.secondary = secondary;
    set.secondarySource = sourceIfPresent(secondary, "Secondary");
    set.elitePrimary = elitePrimary;
    set.elitePrimarySource = sourceIfPresent(elitePrimary, "ElitePrimary");
    set.eliteSecondary = eliteSecondary;
    set.eliteSecondarySource =
        sourceIfPresent(eliteSecondary, "EliteSecondary");
    set.deploy = deployWeapon;
    set.eliteDeploy = eliteDeployWeapon;
    set.deploySource = deployWeaponSource;
    set.eliteDeploySource = eliteDeployWeaponSource;
    set.occupy = occupyWeapon;
    set.occupySource = sourceIfPresent(occupyWeapon, "OccupyWeapon");
    set.eliteOccupy = eliteOccupyWeapon;
    set.eliteOccupySource =
        sourceIfPresent(eliteOccupyWeapon, "EliteOccupyWeapon");
    set.openTransportWeapon = openTransportWeapon;
    set.openTransportWeaponSource =
        openTransportWeapon >= 0 ? "OpenTransportWeapon" : "";
    return set;
}

DeployProfile deployProfileForObject(
    const std::string& deploysInto,
    const std::string& undeploysInto,
    const std::string& deployWeapon,
    const std::string& eliteDeployWeapon,
    const std::string& deployWeaponSource,
    const std::string& eliteDeployWeaponSource,
    bool deployer,
    bool deployFire,
    bool deployToFire,
    bool isSimpleDeployer,
    const std::string& unloadingClass,
    double deployTime,
    int undeployDelay,
    bool deployedCrushable,
    bool deployToLand,
    int deployFacing,
    const std::string& deployingAnim,
    bool noMovingFire,
    const std::string& voiceDeploy,
    const std::string& deploySound,
    const std::string& undeploySound
) {
    DeployProfile profile;
    profile.deployer = deployer;
    profile.deployFire = deployFire;
    profile.deployToFire = deployToFire;
    profile.isSimpleDeployer = isSimpleDeployer;
    profile.noMovingFire = noMovingFire;
    profile.deployToLand = deployToLand;
    profile.deployedCrushable = deployedCrushable;
    profile.deployTime = deployTime;
    profile.undeployDelay = undeployDelay;
    profile.deployFacing = deployFacing;
    profile.deploysInto = deploysInto;
    profile.undeploysInto = undeploysInto;
    profile.unloadingClass = unloadingClass;
    profile.deployWeapon = deployWeapon;
    profile.eliteDeployWeapon = eliteDeployWeapon;
    profile.deployWeaponSource = deployWeaponSource;
    profile.eliteDeployWeaponSource = eliteDeployWeaponSource;
    profile.deployingAnim = deployingAnim;
    profile.voiceDeploy = voiceDeploy;
    profile.deploySound = deploySound;
    profile.undeploySound = undeploySound;
    profile.canDeploy = deployer
        || deployFire
        || deployToFire
        || isSimpleDeployer
        || hasMeaningfulValue(deploysInto)
        || hasMeaningfulValue(undeploysInto)
        || hasMeaningfulValue(deployWeapon)
        || hasMeaningfulValue(eliteDeployWeapon)
        || hasMeaningfulValue(deployingAnim)
        || hasMeaningfulValue(deploySound)
        || hasMeaningfulValue(undeploySound)
        || hasMeaningfulValue(voiceDeploy);

    if (profile.canDeploy) {
        profile.deploySequence = "Deploy";
        profile.deployIdleSequence = "DeployIdle";
        profile.deployFireSequence = "DeployFire";
        profile.deployedSequence = "Deployed";
        profile.deployedIdleSequence = "DeployedIdle";
        profile.deployedFireSequence = "DeployedFire";
        profile.undeploySequence = "Undeploy";
    }

    return profile;
}

OccupyProfile occupyProfileForObject(
    bool occupier,
    bool canBeOccupied,
    bool canOccupyFire,
    bool distributedFire,
    int maxNumberOccupants,
    const std::string& occupyWeapon,
    const std::string& eliteOccupyWeapon,
    const std::string& occupyPip
) {
    OccupyProfile profile;
    profile.occupier = occupier;
    profile.canBeOccupied = canBeOccupied;
    profile.canOccupyFire = canOccupyFire;
    profile.distributedFire = distributedFire;
    profile.maxNumberOccupants = maxNumberOccupants;
    profile.occupyWeapon = occupyWeapon;
    profile.eliteOccupyWeapon = eliteOccupyWeapon;
    profile.occupyPip = occupyPip;
    return profile;
}

TransportProfile transportProfileForObject(
    int passengers,
    int size,
    int openTransportWeapon,
    const std::string& unloadingClass,
    bool airportBound,
    const std::string& dock
) {
    TransportProfile profile;
    profile.passengers = passengers;
    profile.size = size;
    profile.openTransportWeapon = openTransportWeapon;
    profile.unloadingClass = unloadingClass;
    profile.airportBound = airportBound;
    profile.dock = dock;
    profile.canTransport = passengers > 0 || hasMeaningfulValue(unloadingClass);
    return profile;
}

EngineerProfile engineerProfileForObject(
    bool engineer,
    bool bridgeRepair,
    bool repairable,
    bool clickRepairable,
    bool unsellable,
    bool capturable,
    bool needsEngineer,
    bool bridgeRepairHut
) {
    EngineerProfile profile;
    profile.engineer = engineer;
    profile.bridgeRepair = bridgeRepair;
    profile.repairable = repairable;
    profile.clickRepairable = clickRepairable;
    profile.unsellable = unsellable;
    profile.capturable = capturable;
    profile.needsEngineer = needsEngineer;
    profile.bridgeRepairHut = bridgeRepairHut;
    return profile;
}

ObjectCapability unitCapabilityForObject(
    const UnitType& unit,
    const std::string& objectKind
) {
    ObjectCapability capability;
    capability.movable = unit.speed > 0;
    capability.infantry = objectKind == "infantry";
    capability.vehicle = objectKind == "vehicle";
    capability.aircraft = objectKind == "aircraft";
    capability.naval = unit.naval;
    capability.waterBound = unit.waterBound;
    capability.turret = unit.turret;
    capability.crusher = unit.crusher;
    capability.crushable = unit.crushable;
    capability.omniCrusher = unit.omniCrusher;
    capability.omniCrushResistant = unit.omniCrushResistant;
    capability.repairable = unit.repairable;
    capability.canPassiveAquire = unit.canPassiveAquire;
    capability.canRetaliate = unit.canRetaliate;
    capability.speed = unit.speed;
    capability.speedType = unit.speedType;
    capability.movementZone = unit.movementZone;
    capability.locomotor = unit.locomotor;
    capability.category = unit.category;
    return capability;
}

ObjectCapability buildingObjectCapability(const BuildingType& building) {
    ObjectCapability capability;
    capability.building = true;
    capability.naval = building.naval;
    capability.waterBound = building.waterBound;
    capability.turret = building.turret || hasMeaningfulValue(building.turretAnim);
    capability.crusher = building.crusher;
    capability.crushable = building.crushable;
    capability.omniCrusher = building.omniCrusher;
    capability.omniCrushResistant = building.omniCrushResistant;
    capability.repairable = building.repairable;
    capability.canPassiveAquire = building.canPassiveAquire;
    capability.canRetaliate = building.canRetaliate;
    capability.category = building.buildCat;
    return capability;
}

BuildingCapability buildingCapabilityForObject(const BuildingType& building) {
    BuildingCapability capability;
    capability.power = building.power;
    capability.powered = building.powered;
    capability.factory = hasMeaningfulValue(building.factory);
    capability.constructionYard = building.constructionYard;
    capability.refinery = building.refinery;
    capability.barracksFactory = normalizedEquals(building.factory, "InfantryType");
    capability.vehicleFactory = normalizedEquals(building.factory, "UnitType");
    capability.aircraftFactory = normalizedEquals(building.factory, "AircraftType");
    capability.navalFactory = normalizedEquals(building.factory, "VesselType");
    capability.weaponsFactory = building.weaponsFactory;
    capability.factoryPlant = building.factoryPlant;
    capability.infantryCostBonus = building.infantryCostBonus;
    capability.unitsCostBonus = building.unitsCostBonus;
    capability.aircraftCostBonus = building.aircraftCostBonus;
    capability.buildingsCostBonus = building.buildingsCostBonus;
    capability.defensesCostBonus = building.defensesCostBonus;
    capability.unitRepair = building.unitRepair;
    capability.hospital = building.hospital;
    capability.armory = building.armory;
    capability.ammo = building.ammo;
    capability.cloning = building.cloning;
    capability.infantryGainSelfHeal = building.infantryGainSelfHeal;
    capability.unitsGainSelfHeal = building.unitsGainSelfHeal;
    capability.gapGenerator = building.gapGenerator;
    capability.gapRadiusInCells = building.gapRadiusInCells;
    capability.superGapRadiusInCells = building.superGapRadiusInCells;
    capability.secretLab = building.secretLab;
    capability.secretInfantry = building.secretInfantry;
    capability.secretUnits = building.secretUnits;
    capability.secretBuildings = building.secretBuildings;
    capability.produceCashStartup = building.produceCashStartup;
    capability.produceCashAmount = building.produceCashAmount;
    capability.produceCashDelay = building.produceCashDelay;
    capability.grinding = building.grinding;
    capability.bunker = building.bunker;
    capability.infantryAbsorb = building.infantryAbsorb;
    capability.passengers = building.passengers;
    capability.sizeLimit = building.sizeLimit;
    capability.extraPower = building.extraPower;
    capability.orePurifier = building.orePurifier;
    capability.spySat = building.spySat;
    capability.radar = building.radar;
    capability.buildCat = building.buildCat;
    capability.factoryType = building.factory;
    capability.freeUnit = building.freeUnit;
    capability.prerequisite = building.prerequisite;
    capability.prerequisiteOverride = building.prerequisiteOverride;
    return capability;
}

UiProfile uiProfileForObject(
    const std::string& pip,
    const std::string& pipScale,
    const std::string& occupyPip,
    bool pipsDrawForAll,
    int strength,
    int power,
    bool powered
) {
    UiProfile profile;
    profile.pip = pip;
    profile.pipScale = pipScale;
    profile.occupyPip = occupyPip;
    profile.pipsDrawForAll = pipsDrawForAll;
    profile.strength = strength;
    profile.power = power;
    profile.powered = powered;
    profile.hasRulesSidebarResource = false;
    profile.hasRulesPowerResource = false;
    profile.hasRulesHealthResource = false;
    profile.missingResourceNote =
        "rules/art do not provide sidebar power/health resource names; asset layer must locate them";
    return profile;
}

WeaponProfile weaponProfileForObject(const WeaponType& weapon) {
    WeaponProfile profile;
    profile.id = weapon.id;
    profile.damage = weapon.damage;
    profile.rof = weapon.rof;
    profile.range = weapon.range;
    profile.minimumRange = weapon.minimumRange;
    profile.projectile = weapon.projectile;
    profile.speed = weapon.speed;
    profile.warhead = weapon.warhead;
    profile.burst = weapon.burst;
    profile.report = weapon.report;
    profile.anim = weapon.anim;
    profile.charges = weapon.charges;
    profile.bright = weapon.bright;
    profile.decloakToFire = weapon.decloakToFire;
    profile.cellRangefinding = weapon.cellRangefinding;
    profile.neverUse = weapon.neverUse;
    profile.laser = weapon.laser;
    profile.houseColorLaser = weapon.houseColorLaser;
    profile.diskLaser = weapon.diskLaser;
    profile.drainWeapon = weapon.drainWeapon;
    profile.laserInnerColor = weapon.laserInnerColor;
    profile.laserOuterColor = weapon.laserOuterColor;
    profile.laserOuterSpread = weapon.laserOuterSpread;
    profile.laserDuration = weapon.laserDuration;
    profile.laserThickness = weapon.laserThickness;
    profile.magBeam = weapon.magBeam;
    profile.limboLaunch = weapon.limboLaunch;
    profile.disguiseFireOnly = weapon.disguiseFireOnly;
    profile.fireOnce = weapon.fireOnce;
    profile.areaFire = weapon.areaFire;
    return profile;
}

ProjectileProfile projectileProfileForObject(const ProjectileType& projectile) {
    ProjectileProfile profile;
    profile.id = projectile.id;
    profile.image = projectile.image;
    profile.effectiveImage =
        hasMeaningfulValue(projectile.image) ? projectile.image : projectile.id;
    profile.palette = projectile.palette;
    profile.color = projectile.color;
    profile.drawProjectile = !projectile.imageIsNone && !projectile.inviso;
    profile.imageIsNone = projectile.imageIsNone;
    profile.inviso = projectile.inviso;
    profile.paletteIsSet = projectile.paletteIsSet;
    profile.firersPalette = projectile.firersPalette;
    profile.animPalette = projectile.animPalette;
    profile.arcing = projectile.arcing;
    profile.vertical = projectile.vertical;
    profile.ranged = projectile.ranged;
    profile.proximity = projectile.proximity;
    profile.airburst = projectile.airburst;
    profile.aa = projectile.aa;
    profile.ag = projectile.ag;
    profile.an = projectile.an;
    profile.asw = projectile.asw;
    profile.shadow = projectile.shadow;
    profile.subjectToCliffs = projectile.subjectToCliffs;
    profile.subjectToElevation = projectile.subjectToElevation;
    profile.subjectToWalls = projectile.subjectToWalls;
    profile.dropping = projectile.dropping;
    profile.level = projectile.level;
    profile.veryHigh = projectile.veryHigh;
    profile.courseLockDuration = projectile.courseLockDuration;
    profile.rot = projectile.rot;
    profile.frames = projectile.frames;
    profile.translucency = projectile.translucency;
    profile.cluster = projectile.cluster;
    profile.acceleration = projectile.acceleration;
    profile.trailer = projectile.trailer;
    profile.trailerSeperation = projectile.trailerSeperation;
    profile.airburstWeapon = projectile.airburstWeapon;
    profile.shrapnelWeapon = projectile.shrapnelWeapon;
    profile.shrapnelCount = projectile.shrapnelCount;
    return profile;
}

WarheadProfile warheadProfileForObject(const WarheadType& warhead) {
    WarheadProfile profile;
    profile.id = warhead.id;
    profile.cellSpread = warhead.cellSpread;
    profile.percentAtMax = warhead.percentAtMax;
    profile.verses = warhead.verses;
    profile.animList = warhead.animList;
    profile.anim = warhead.animList;
    bool versesValid = false;
    const std::vector<double> verses =
        parseVersesPercentages(warhead.verses, versesValid);
    profile.versesValid = versesValid;
    profile.versesCount = static_cast<int>(verses.size());
    profile.damagesInfantry = anyPositiveInRange(verses, 0, 3);
    profile.damagesVehicles = anyPositiveInRange(verses, 3, 6);
    profile.damagesBuildings = anyPositiveInRange(verses, 6, 9);
    profile.damagesWalls = warhead.wall || anyPositiveInRange(verses, 7, 9);
    profile.damagesWood = warhead.wood || (verses.size() > 6 && verses[6] > 0.0);
    profile.explosion = warhead.explosion;
    profile.particle = warhead.particle;
    profile.particleSystem = warhead.particleSystem;
    profile.infDeath = warhead.infDeath;
    profile.proneDamage = warhead.proneDamage;
    profile.proneDamageMultiplier = warhead.proneDamageMultiplier;
    profile.combatLightSize = warhead.combatLightSize;
    profile.clDisableRed = warhead.clDisableRed;
    profile.clDisableGreen = warhead.clDisableGreen;
    profile.clDisableBlue = warhead.clDisableBlue;
    profile.wall = warhead.wall;
    profile.penetratesBunker = warhead.penetratesBunker;
    profile.wood = warhead.wood;
    profile.affectsAllies = warhead.affectsAllies;
    profile.affectsEnemies = warhead.affectsEnemies;
    profile.bright = warhead.bright;
    profile.conventional = warhead.conventional;
    profile.sparky = warhead.sparky;
    profile.fire = warhead.fire;
    profile.tiberium = warhead.tiberium;
    profile.mindControl = warhead.mindControl;
    profile.temporal = warhead.temporal;
    profile.radiation = warhead.radiation;
    profile.electricAssault = warhead.electricAssault;
    profile.psychicDamage = warhead.psychicDamage;
    profile.makesDisguise = warhead.makesDisguise;
    profile.parasite = warhead.parasite;
    profile.culling = warhead.culling;
    profile.paralyzes = warhead.paralyzes;
    profile.psychedelic = warhead.psychedelic;
    profile.ivanBomb = warhead.ivanBomb;
    profile.bombDisarm = warhead.bombDisarm;
    profile.airstrike = warhead.airstrike;
    profile.isLocomotor = warhead.isLocomotor;
    profile.locomotor = warhead.locomotor;
    profile.cellInset = warhead.cellInset;
    profile.poison = warhead.poison;
    return profile;
}

std::string findSectionName(
    const IniFile& ini,
    const std::string& requestedName
) {
    if (ini.getSection(requestedName) != nullptr) {
        return requestedName;
    }

    const std::string normalized = toLower(trim(requestedName));
    if (normalized.empty()) {
        return "";
    }

    for (const std::string& sectionName : ini.getSectionNames()) {
        if (toLower(sectionName) == normalized) {
            return sectionName;
        }
    }

    return "";
}

std::string readPrerequisiteOverride(
    const IniFile& ini,
    const std::string& section
) {
    const std::string direct = ini.getValue(section, "PrerequisiteOverride", "");
    if (!direct.empty()) {
        return direct;
    }

    return ini.getValue(section, "Prerequisite.Override", "");
}

} // namespace

void RulesDatabase::loadSuperWeaponTypes(const IniFile& ini) {
    superWeaponTypes_.clear();
    const IniFile::Section* list = ini.getSection("SuperWeaponTypes");
    if (list == nullptr) {
        return;
    }

    std::unordered_set<std::string> loaded;
    for (const auto& [indexText, sectionId] : *list) {
        (void)indexText;
        const std::string id = trim(sectionId);
        const std::string key = canonicalIdKey(id);
        if (id.empty() || key.empty() || loaded.find(key) != loaded.end()) {
            continue;
        }
        loaded.insert(key);
        if (ini.getSection(id) == nullptr) {
            warnings_.push_back(
                "SuperWeaponTypes entry [" + id + "] has no definition section"
            );
            continue;
        }

        SuperWeaponRule rule;
        rule.id = id;
        rule.uiName = trim(ini.getValue(id, "UIName", ""));
        rule.name = trim(ini.getValue(id, "Name", id));
        rule.type = trim(ini.getValue(id, "Type", ""));
        rule.action = trim(ini.getValue(id, "Action", ""));
        rule.sidebarImage = trim(ini.getValue(id, "SidebarImage", ""));
        rule.preDependent = trim(ini.getValue(id, "PreDependent", ""));
        rule.rechargeVoice = trim(ini.getValue(id, "RechargeVoice", ""));
        rule.chargingVoice = trim(ini.getValue(id, "ChargingVoice", ""));
        rule.impatientVoice = trim(ini.getValue(id, "ImpatientVoice", ""));
        rule.suspendVoice = trim(ini.getValue(id, "SuspendVoice", ""));
        rule.specialSound = trim(ini.getValue(id, "SpecialSound", ""));
        rule.startSound = trim(ini.getValue(id, "StartSound", ""));
        rule.rechargeTimeMinutes = std::max(
            0.0,
            readDouble(ini, id, "RechargeTime", 5.0)
        );
        rule.range = std::max(0.0, readDouble(ini, id, "Range", 0.0));
        rule.lineMultiplier = std::max(
            0,
            readInt(ini, id, "LineMultiplier", 0)
        );
        rule.flashSidebarTabFrames = std::max(
            -1,
            readInt(ini, id, "FlashSidebarTabFrames", -1)
        );
        rule.isPowered = readBool(ini, id, "IsPowered", true);
        rule.preClick = readBool(ini, id, "PreClick", false);
        rule.postClick = readBool(ini, id, "PostClick", false);
        rule.showTimer = readBool(ini, id, "ShowTimer", false);
        rule.disableableFromShell = readBool(
            ini,
            id,
            "DisableableFromShell",
            false
        );
        rule.aiDefendAgainst = readBool(
            ini,
            id,
            "AIDefendAgainst",
            false
        );
        rule.targetMode = trim(ini.getValue(id, "TargetMode", ""));
        rule.weaponType = trim(
            firstNonEmptyRuleValue(ini, id, {"WeaponType", "Weapon"})
        );
        rule.projectile = trim(
            firstNonEmptyRuleValue(ini, id, {"Projectile", "LaunchProjectile"})
        );
        rule.warhead = trim(ini.getValue(id, "Warhead", ""));
        rule.payloadWeapon = trim(ini.getValue(id, "PayloadWeapon", ""));
        rule.payloadProjectile = trim(
            ini.getValue(id, "PayloadProjectile", "")
        );
        rule.payloadWarhead = trim(
            ini.getValue(id, "PayloadWarhead", "")
        );
        rule.launchAnim = trim(ini.getValue(id, "LaunchAnim", ""));
        rule.impactAnim = trim(ini.getValue(id, "ImpactAnim", ""));
        rule.activeAnim = trim(ini.getValue(id, "ActiveAnim", ""));
        rule.invokeAnim = trim(ini.getValue(id, "InvokeAnim", ""));
        rule.firstAnim = trim(ini.getValue(id, "FirstAnim", ""));
        rule.secondAnim = trim(ini.getValue(id, "SecondAnim", ""));
        rule.boltAnim = trim(ini.getValue(id, "BoltAnim", ""));
        rule.cloudAnim = trim(ini.getValue(id, "CloudAnim", ""));
        rule.planeType = trim(
            firstNonEmptyRuleValue(
                ini,
                "General",
                {"ParaDropPlane", "ParadropPlane", "PDropPlane"}
            )
        );
        rule.planeType = trim(ini.getValue(id, "PlaneType", rule.planeType));
        rule.supportFlightLevelLeptons = std::max(1, readInt(ini, "General", "FlightLevel", 1500));
        rule.paradropRadiusLeptons = std::max(0, readInt(ini, "General", "ParadropRadius", 1024));
        rule.parachuteFallRate = std::max(1, std::abs(readInt(ini, "General", "ParachuteMaxFallRate", -3)));
        rule.spyCameraFrames = std::max(1, readInt(ini, "AudioVisual", "SpyPlaneCameraFrames", 16));
        rule.parachuteAnim = trim(
            ini.getValue("General", "Parachute", ini.getValue("AudioVisual", "Parachute", "")));
        // Stock GeneticConverter uses InfantryMutate/GENDEATH, whose
        // MakeInfantry=0 result is the hard-coded BRUTE.  SmallVisceroid is
        // unrelated (it is used by visceroid spawning logic).
        rule.mutationType.clear();
        rule.damage = std::max(0, readInt(ini, id, "Damage", 0));
        rule.radius = std::max(0, readInt(ini, id, "Radius", 0));
        rule.durationFrames = std::max(
            0,
            readInt(ini, id, "Duration", 0)
        );
        rule.defermentFrames = std::max(
            0,
            readInt(ini, id, "Deferment", 0)
        );
        rule.hitDelayFrames = std::max(
            0,
            readInt(ini, id, "HitDelay", 0)
        );
        rule.scatterDelayFrames = std::max(
            0,
            readInt(ini, id, "ScatterDelay", 0)
        );
        rule.cellSpread = std::max(
            0,
            readInt(ini, id, "CellSpread", 0)
        );
        rule.separation = std::max(
            0,
            readInt(ini, id, "Separation", 0)
        );
        rule.captureRange = std::max(
            0,
            readInt(ini, id, "CaptureRange", 0)
        );
        rule.blackoutDurationFrames = std::max(
            0,
            readInt(ini, id, "BlackoutDuration", 0)
        );
        rule.powerPenalty = std::max(
            0,
            readInt(ini, id, "PowerPenalty", 0)
        );

        const std::string typeKey = toLower(rule.type);
        const std::string actionKey = toLower(rule.action);
        const bool isNuke = typeKey.find("multimissile") != std::string::npos
            || actionKey.find("nuke") != std::string::npos;
        const bool isStorm = typeKey.find("lightning") != std::string::npos
            || actionKey.find("lightning") != std::string::npos;
        const bool isIron = typeKey.find("ironcurtain") != std::string::npos
            || actionKey.find("ironcurtain") != std::string::npos;
        const bool isForce = typeKey.find("forceshield") != std::string::npos
            || actionKey.find("forceshield") != std::string::npos;
        const bool isChrono = typeKey.find("chrono") != std::string::npos
            || actionKey.find("chrono") != std::string::npos;
        const bool isDominator = typeKey.find("psychicdominator")
                != std::string::npos
            || actionKey.find("dominator") != std::string::npos;
        const bool isMutator = typeKey.find("genetic") != std::string::npos
            || actionKey.find("genetic") != std::string::npos;
        const bool isPara = typeKey.find("para") != std::string::npos
            || actionKey.find("para") != std::string::npos;
        const bool isSpy = typeKey.find("spy") != std::string::npos
            || actionKey.find("spy") != std::string::npos;
        const bool isReveal = typeKey.find("reveal") != std::string::npos
            || actionKey.find("reveal") != std::string::npos;

        // These families have opposite target polarity from the default
        // hostile area weapon.  Keeping it in the parsed rule is important:
        // the UI preview and the committed target must reject the same cells
        // and objects.
        if (isIron || isForce || isChrono) {
            rule.targetEnemy = false;
            rule.targetFriendly = true;
        }
        if (isIron || isDominator) {
            rule.targetEnemy = true;
            rule.targetFriendly = true;
        }
        if (isForce) {
            rule.targetInfantry = false;
            rule.targetUnits = false;
            rule.targetBuildings = true;
        }
        if (isChrono || isDominator) {
            rule.targetBuildings = false;
        }
        if (isMutator) {
            rule.targetEnemy = true;
            rule.targetFriendly = true;
            rule.targetInfantry = true;
            rule.targetUnits = false;
            rule.targetBuildings = false;
            rule.mutationType = "BRUTE";
        }

        if (isNuke) {
            // YR's carrier/payload weapons point at GiantNukeUp/Down. The
            // obsolete General.NukeDown entry is not a weapon definition.
            const std::string carrier = rule.weaponType.empty() ? "NukeCarrier" : rule.weaponType;
            rule.projectile = ini.getValue(carrier, "Projectile", rule.projectile);
            if (rule.payloadWeapon.empty() && ini.getSection("NukePayload")) rule.payloadWeapon = "NukePayload";
            rule.payloadProjectile = ini.getValue(rule.payloadWeapon, "Projectile", rule.payloadProjectile);
            rule.payloadWarhead = ini.getValue(rule.payloadWeapon, "Warhead", rule.payloadWarhead);
            rule.impactAnim = ini.getValue(rule.payloadWarhead, "AnimList", rule.impactAnim);
            rule.launchAnim = ini.getValue("General", "NukeTakeOff", rule.launchAnim);
            rule.startSound = ini.getValue("AudioVisual", "DigSound", rule.startSound);
            if (ini.getSection(rule.projectile) && ini.getSection(rule.payloadProjectile)) {
                rule.nuclearAltitudeLeptons = std::max(1, readInt(ini, rule.projectile, "DetonationAltitude", 20000));
                rule.nuclearUpAcceleration = std::max(0, readInt(ini, rule.projectile, "Acceleration", 1));
                rule.nuclearDownAcceleration = std::max(0, readInt(ini, rule.payloadProjectile, "Acceleration", 1));
                // Retail WeaponType::CalculateSpeed (0x7729F0): ROT=0 uses
                // the ballistic range/gravity speed, including vertical nukes.
                rule.nuclearDownSpeed = readInt(ini, rule.payloadProjectile, "ROT", 0) == 0
                    ? std::max(1, int(std::sqrt(std::max(0.0,
                        readDouble(ini, rule.payloadWeapon, "Range", 30.0) * 256.0
                        * readDouble(ini, "General", "Gravity", 6.0) * 1.2))))
                    : std::max(1, readInt(ini, rule.payloadWeapon, "Speed", 10));
                rule.preImpactAnim = "NUKEBALL";
            }
        }
        if (isChrono) rule.placementAnim = ini.getValue("General", "ChronoPlacement", "CHRONOAR");
        if (isStorm) {
            rule.boltExplosionAnim = ini.getValue("General", "WeatherConBoltExplosion", "");
            rule.damage = std::max(
                0,
                readInt(ini, "General", "LightningDamage", rule.damage)
            );
            rule.defermentFrames = std::max(
                0,
                readInt(ini, "General", "LightningDeferment", rule.defermentFrames)
            );
            rule.durationFrames = std::max(
                0,
                readInt(ini, "General", "LightningStormDuration", rule.durationFrames)
            );
            rule.hitDelayFrames = std::max(
                0,
                readInt(ini, "General", "LightningHitDelay", rule.hitDelayFrames)
            );
            rule.scatterDelayFrames = std::max(
                0,
                readInt(ini, "General", "LightningScatterDelay", rule.scatterDelayFrames)
            );
            rule.cellSpread = std::max(
                0,
                readInt(ini, "General", "LightningCellSpread", rule.cellSpread)
            );
            rule.separation = std::max(
                0,
                readInt(ini, "General", "LightningSeparation", rule.separation)
            );
            rule.payloadWarhead = ini.getValue(
                "General",
                "LightningWarhead",
                rule.payloadWarhead
            );
            rule.startSound = ini.getValue(
                "AudioVisual",
                "StormSound",
                rule.startSound
            );
            rule.specialSound = ini.getValue(
                "AudioVisual",
                "LightningSounds",
                rule.specialSound
            );
            rule.cloudAnim = firstNonEmptyRuleValue(
                ini,
                "General",
                {"WeatherConClouds"}
            );
            rule.boltAnim = firstNonEmptyRuleValue(
                ini,
                "General",
                {"WeatherConBolts"}
            );
        }
        if (isIron) {
            rule.durationFrames = std::max(
                0,
                readInt(ini, "CombatDamage", "IronCurtainDuration",
                    readInt(ini, "General", "IronCurtainDuration", rule.durationFrames))
            );
            rule.invokeAnim = ini.getValue("General", "IronCurtainInvokeAnim", ini.getValue("AudioVisual", "IronCurtainInvokeAnim", rule.invokeAnim));
            rule.radius = std::max(1, static_cast<int>(std::floor(rule.range)));
        }
        if (isForce) {
            rule.radius = std::max(
                0,
                readInt(ini, "General", "ForceShieldRadius", rule.radius)
            );
            rule.durationFrames = std::max(
                0,
                readInt(ini, "General", "ForceShieldDuration", rule.durationFrames)
            );
            rule.blackoutDurationFrames = std::max(
                0,
                readInt(
                    ini,
                    "General",
                    "ForceShieldBlackoutDuration",
                    rule.blackoutDurationFrames
                )
            );
            rule.invokeAnim = ini.getValue("General", "ForceShieldInvokeAnim", ini.getValue("AudioVisual", "ForceShieldInvokeAnim", rule.invokeAnim));
            rule.fadeSoundFrames = std::max(0, readInt(ini, "General", "ForceShieldPlayFadeSoundTime", 0));
        }
        if (isChrono) {
            rule.targetEnemy = true;
            rule.radius = std::max(1, static_cast<int>(std::floor(rule.range)));
            rule.firstAnim = ini.getValue("General", "ChronoBlast", ini.getValue("AudioVisual", "ChronoBlast", rule.firstAnim));
            rule.secondAnim = ini.getValue("General", "ChronoBlastDest", ini.getValue("AudioVisual", "ChronoBlastDest", rule.secondAnim));
            rule.launchAnim = ini.getValue("General", "WarpOut", ini.getValue("AudioVisual", "WarpOut", rule.launchAnim));
            rule.impactAnim = ini.getValue("General", "WarpIn", ini.getValue("AudioVisual", "WarpIn", rule.impactAnim));
        }
        if (isDominator) {
            rule.fireAtPercentage = std::clamp(readInt(ini, id,
                "SW.FireAtPercentage", readInt(ini, "General",
                    "DominatorFireAtPercentage", 20)), 0, 100);
            rule.captureRange = std::max(
                0,
                readInt(ini, "General", "DominatorCaptureRange", rule.captureRange)
            );
            rule.damage = std::max(
                0,
                readInt(ini, "General", "DominatorDamage", rule.damage)
            );
            rule.payloadWarhead = ini.getValue(
                "General",
                "DominatorWarhead",
                rule.payloadWarhead
            );
            rule.firstAnim = ini.getValue(
                "General",
                "DominatorFirstAnim",
                rule.firstAnim
            );
            rule.secondAnim = ini.getValue(
                "General",
                "DominatorSecondAnim",
                rule.secondAnim
            );
            rule.startSound = ini.getValue(
                "AudioVisual",
                "PsychicDominatorActivateSound",
                rule.startSound
            );
        }
        if (isMutator) {
            rule.mutateExplosion = readBool(ini, "General", "MutateExplosion", false);
            rule.payloadWarhead = ini.getValue(
                "SpecialWeapons",
                "MutateWarhead",
                rule.payloadWarhead
            );
            rule.warhead = ini.getValue(
                "SpecialWeapons",
                "MutateExplosionWarhead",
                rule.warhead
            );
            rule.startSound = ini.getValue(
                "AudioVisual",
                "GeneticMutatorActivateSound",
                rule.startSound
            );
            rule.impactAnim = ini.getValue("General", "InfantryMutate", ini.getValue("AudioVisual", "InfantryMutate", rule.impactAnim));
            rule.invokeAnim = ini.getValue("General", "IonBlast", rule.invokeAnim);
            if (rule.mutateExplosion) {
                rule.radius = std::max(0, static_cast<int>(std::ceil(readDouble(ini,
                    rule.warhead, "CellSpread", 0.0))));
            } else {
                rule.radius = 1; // Retail non-explosion mode affects a 3 by 3 square.
            }
        }
        if (isPara) {
            const bool american = typeKey.find("amer") != std::string::npos
                || actionKey.find("amer") != std::string::npos;
            const std::string prefix = american
                ? "AmerParaDrop"
                : "ParaDrop";
            rule.infantryTypes = firstNonEmptyRuleValue(
                ini,
                "General",
                {prefix == "AmerParaDrop" ? "AmerParaDropInf" : "PParatrooper"}
            );
            rule.infantryCounts = firstNonEmptyRuleValue(
                ini,
                "General",
                {prefix == "AmerParaDrop" ? "AmerParaDropNum" : "PParatrooperNum"}
            );
            if (rule.planeType.empty() && ini.getSection("PDPLANE")) rule.planeType = "PDPLANE";
            const std::array<std::string, 3> sidePrefixes{"Ally", "Sov", "Yuri"};
            for (std::size_t side=0; side<sidePrefixes.size(); ++side) {
                rule.sideInfantryTypes[side] = ini.getValue("General", sidePrefixes[side]+"ParaDropInf", "");
                rule.sideInfantryCounts[side] = ini.getValue("General", sidePrefixes[side]+"ParaDropNum", "");
            }
            rule.targetFriendly = true;
            rule.parachuteAnim = ini.getValue("General", "Parachute", ini.getValue("AudioVisual", "Parachute", rule.parachuteAnim));
        }
        if (isSpy) {
            rule.planeType = trim(ini.getValue(id, "PlaneType", ini.getSection("SPYP") ? "SPYP" : ""));
            rule.radius = std::max(1, readInt(ini, "General", "CameraRange", 9));
            rule.targetFriendly = true;
            rule.startSound = ini.getValue(
                "AudioVisual",
                "SpyPlaneCamera",
                rule.startSound
            );
        }
        if (isReveal) {
            rule.targetFriendly = true;
            rule.radius = std::max(
                0,
                std::min(10, readInt(ini, "CombatDamage", "PsychicRevealRadius",
                    readInt(ini, "General", "PsychicRevealRadius", rule.radius)))
            );
            rule.startSound = ini.getValue(
                "AudioVisual",
                "PsychicRevealActivateSound",
                rule.startSound
            );
        }

        if (rule.targetMode.empty()) {
            if (rule.preClick) {
                rule.targetMode = "TwoStage";
            } else if (isPara || isSpy || isReveal) {
                rule.targetMode = "Cell";
            } else if (isNuke || isStorm || isIron || isForce || isDominator || isMutator) {
                rule.targetMode = "Area";
            } else {
                rule.targetMode = "Cell";
            }
        }
        if (rule.action.empty()) {
            rule.action = rule.type;
        }
        superWeaponTypes_.push_back(std::move(rule));
    }
}

bool RulesDatabase::loadFromIni(const IniFile& ini) {
    traceRulesDatabaseLoad("start");
    infantryTypes_.clear();
    vehicleTypes_.clear();
    aircraftTypes_.clear();
    buildingTypes_.clear();
    weaponTypes_.clear();
    projectileTypes_.clear();
    overlayTypes_.clear();
    smudgeTypes_.clear();
    tiberiumTypes_.clear();
    terrainTypes_.clear();
    landTypes_.clear();
    warheadTypes_.clear();
    particleSystemTypes_.clear();
    particleTypes_.clear();
    infantryTypeIds_.clear();
    vehicleTypeIds_.clear();
    aircraftTypeIds_.clear();
    padAircraft_.clear();
    buildingTypeIds_.clear();
    authoredBuildingTypeIds_.clear();
    authoredVehicleTypeIds_.clear();
    authoredAircraftTypeIds_.clear();
    overlayTypeIds_.clear();
    tiberiumTypeIds_.clear();
    terrainTypeIds_.clear();
    countries_.clear();
    countryDefinitions_.clear();
    countryIndexById_.clear();
    sides_.clear();
    prerequisiteAliases_.clear();
    warnings_.clear();
    audioVisualValues_.clear();
    voiceIFVRepair_.clear();
    infantryVirusAnim_.clear();
    veterancyRules_ = VeterancyRules{};
    elevationModelRules_ = ElevationModelRules{};
    terrainMovementRules_ = TerrainMovementRules{};
    productionRules_ = ProductionRules{};
    harvesterCadenceRules_ = HarvesterCadenceRules{};
    crateRules_ = CrateRules{};
    specialBuildingRules_ = SpecialBuildingRules{};
    specialMechanismRules_ = SpecialMechanismRules{};
    combatDetailRules_ = CombatDetailRules{};
    superWeaponTypes_.clear();
    purifierBonus_ = 0.0;
    computerMiningRules_ = {};
    computerBaseBuildingRules_ = {};
    computerTacticsRules_ = {};
    harvesterTooFarDistance_ = 5.0;
    chronoHarvTooFarDistance_ = 50.0;
    tiberiumNearScan_ = 5;
    tiberiumFarScan_ = 50;
    slaveMinerShortScan_ = 8;
    slaveMinerSlaveScan_ = 14;
    slaveMinerLongScan_ = 48;
    slaveMinerScanCorrection_ = 3;
    slaveMinerKickFrameDelay_ = 150;
    poseDir_ = 0;
    damageFireTypes_.clear();
    maximumQueuedObjects_ = 29;
    refundPercent_ = 0.5;
    crewSurvivorRules_ = {};
    tiberiumGrows_ = true;
    tiberiumSpreads_ = true;
    destroyableBridges_ = true;
    bridgeStrength_ = 1500;
    spawnedMissileProfiles_.clear();
    resetLifecycleCache(this);
    projectileIds_.clear();
    warheadIds_.clear();
    infantryIndexById_.clear();
    vehicleIndexById_.clear();
    aircraftIndexById_.clear();
    buildingIndexById_.clear();
    weaponIndexById_.clear();
    projectileIndexById_.clear();
    tiberiumIndexById_.clear();
    terrainIndexById_.clear();
    landTypeIndexById_.clear();
    warheadIndexById_.clear();
    particleSystemIndexById_.clear();
    particleIndexById_.clear();

    traceRulesDatabaseLoad("loadCountries");
    loadCountries(ini);
    traceRulesDatabaseLoad("loadSides");
    loadSides(ini);
    traceRulesDatabaseLoad("loadPrerequisiteAliases");
    loadPrerequisiteAliases(ini);
    traceRulesDatabaseLoad("read general resource settings");
    if (const IniFile::Section* audioVisual = ini.getSection("AudioVisual")) {
        for (const auto& [key, value] : *audioVisual) {
            const std::string normalizedKey = toLower(trim(key));
            if (!normalizedKey.empty()) {
                audioVisualValues_[normalizedKey] = trim(value);
            }
        }
    }
    // Retail places this visual resource in General, before AudioVisual.
    // Expose the resolved name with the other visual resource values.
    audioVisualValues_["behind"] = trim(ini.getValue("General", "Behind",
        ini.getValue("AudioVisual", "Behind", "BEHIND")));
    // The stock splash animation list belongs to CombatDamage. Retain the
    // existing visual-resource accessor and an AudioVisual fallback for mods.
    audioVisualValues_["splashlist"] = trim(ini.getValue("CombatDamage", "SplashList",
        ini.getValue("AudioVisual", "SplashList", "")));
    // ColorAdd is an ordered RGB565 table. Resolve the selected entry while
    // reading the rules, so the renderer also honors map/mod overrides.
    std::string laserTargetAdd = "24,0,0";
    if (const auto* colors = ini.getSection("ColorAdd")) {
        const int index = readInt(ini, "General", "LaserTargetColor", 4);
        laserTargetAdd = index >= 0 && static_cast<std::size_t>(index) < colors->size()
            ? (*colors)[static_cast<std::size_t>(index)].second : "0,0,0";
    }
    audioVisualValues_["lasertargetcoloradd"] = laserTargetAdd;
    voiceIFVRepair_ = trim(
        ini.getValue("AudioVisual", "VoiceIFVRepair", "")
    );
    infantryVirusAnim_ = trim(
        ini.getValue("General", "InfantryVirus", "")
    );
    infantryDeathAnims_ = {};
    const std::array<std::pair<const char*, const char*>, 8> deathAnimations{{
        {"InfantryExplode", "S_BANG34"},
        {"FlamingInfantry", "FLAMEGUY"},
        {"", "ELECTRO"},
        {"InfantryHeadPop", "YURIDIE"},
        {"InfantryNuked", "NUKEDIE"},
        {"InfantryVirus", "VIRUSD"},
        {"InfantryMutate", "GENDEATH"}, // InfDeath=9: genetic conversion
        {"InfantryBrute", "BRUTDIE"}    // InfDeath=10: Brute melee victim
    }};
    for (std::size_t index = 0; index < deathAnimations.size(); ++index) {
        const auto& [key, fallback] = deathAnimations[index];
        infantryDeathAnims_[index + 3] = trim(
            *key == '\0' ? std::string(fallback)
                         : ini.getValue("General", key, fallback)
        );
    }
    {
        const int authoredWindDirection = readInt(
            ini,
            "General",
            "WindDirection",
            -1
        );
        windDirection_ = ((authoredWindDirection % 8) + 8) % 8;
    }
    crateRules_.maximum = std::clamp(
        readInt(ini, "CrateRules", "CrateMaximum", 255),
        0,
        100000
    );
    crateRules_.minimum = std::clamp(
        readInt(ini, "CrateRules", "CrateMinimum", 1),
        0,
        crateRules_.maximum
    );
    crateRules_.radius = std::clamp(
        readDouble(ini, "CrateRules", "CrateRadius", 3.0),
        0.0,
        128.0
    );
    crateRules_.regenerationMinutes = std::clamp(
        readDouble(ini, "CrateRules", "CrateRegen", 3.0),
        0.0,
        1440.0
    );
    crateRules_.silverReward = trim(
        ini.getValue("CrateRules", "SilverCrate", "HealBase")
    );
    crateRules_.soloMoney = std::max(
        0,
        readInt(ini, "CrateRules", "SoloCrateMoney", 5000)
    );
    crateRules_.unitType = trim(
        ini.getValue("CrateRules", "UnitCrateType", "none")
    );
    crateRules_.woodReward = trim(
        ini.getValue("CrateRules", "WoodCrate", "Money")
    );
    crateRules_.waterReward = trim(
        ini.getValue("CrateRules", "WaterCrate", "Money")
    );
    crateRules_.healSound = trim(
        ini.getValue("CrateRules", "HealCrateSound", "HealCrate")
    );
    crateRules_.woodImage = trim(
        ini.getValue("CrateRules", "WoodCrateImg", "CRATE")
    );
    crateRules_.image = trim(
        ini.getValue("CrateRules", "CrateImg", "CRATE")
    );
    crateRules_.waterImage = trim(
        ini.getValue("CrateRules", "WaterCrateImg", "WCRATE")
    );
    crateRules_.freeMcv = readBool(
        ini,
        "CrateRules",
        "FreeMCV",
        true
    );
    if (const IniFile::Section* powerups = ini.getSection("Powerups")) {
        std::unordered_set<std::string> seenPowerups;
        std::vector<CratePowerupRule> reversedPowerups;
        reversedPowerups.reserve(powerups->size());
        for (auto it = powerups->rbegin(); it != powerups->rend(); ++it) {
            const std::string type = trim(it->first);
            const std::string typeKey = toLower(type);
            if (typeKey.empty() || !seenPowerups.insert(typeKey).second) {
                continue;
            }
            const std::vector<std::string> fields = splitList(it->second);
            if (fields.size() < 3U) {
                addWarning(
                    "Invalid [Powerups] entry: " + type + "=" + it->second
                );
                continue;
            }

            int weight = 0;
            try {
                std::size_t consumed = 0;
                weight = std::stoi(fields[0], &consumed);
                if (consumed != fields[0].size()) {
                    throw std::invalid_argument("trailing characters");
                }
            } catch (const std::exception&) {
                addWarning(
                    "Invalid [Powerups] weight: " + type + "=" + fields[0]
                );
                weight = 0;
            }

            CratePowerupRule powerup;
            powerup.type = type;
            powerup.weight = std::clamp(weight, 0, 1000000);
            powerup.animation = trim(fields[1]);
            const std::string water = toLower(trim(fields[2]));
            powerup.availableOverWater = water == "yes"
                || water == "true"
                || water == "1"
                || water == "on";
            if (fields.size() >= 4U
                && !parseFiniteDouble(fields[3], powerup.data)) {
                addWarning(
                    "Invalid [Powerups] data: " + type + "=" + fields[3]
                );
                powerup.data = 0.0;
            }
            reversedPowerups.push_back(std::move(powerup));
        }
        crateRules_.powerups.assign(
            reversedPowerups.rbegin(),
            reversedPowerups.rend()
        );
    }
    veterancyRules_.veteranRatio = std::max(
        0.0,
        readDouble(ini, "General", "VeteranRatio", 3.0)
    );
    const auto elevationKey = [&ini](
        const std::string& preferred,
        const std::string& legacy
    ) -> std::string {
        return trim(ini.getValue("ElevationModel", preferred, "")).empty()
            ? legacy
            : preferred;
    };
    const std::string elevationIncrementKey = elevationKey(
        "ElevationIncrement",
        "Increment"
    );
    const std::string elevationIncrementBonusKey = elevationKey(
        "ElevationIncrementBonus",
        "IncrementBonus"
    );
    const std::string elevationBonusCapKey = elevationKey(
        "ElevationBonusCap",
        "BonusCap"
    );
    elevationModelRules_.increment = std::max(
        1,
        readInt(ini, "ElevationModel", elevationIncrementKey, 4)
    );
    elevationModelRules_.incrementBonus = std::max(
        0.0,
        readDouble(
            ini,
            "ElevationModel",
            elevationIncrementBonusKey,
            2.0
        )
    );
    elevationModelRules_.bonusCap = std::max(
        0.0,
        readDouble(ini, "ElevationModel", elevationBonusCapKey, 2.0)
    );
    terrainMovementRules_.trackedUphill = std::max(0.0, readDouble(ini, "General", "TrackedUphill", 1.0));
    terrainMovementRules_.trackedDownhill = std::max(0.0, readDouble(ini, "General", "TrackedDownhill", 1.0));
    terrainMovementRules_.wheeledUphill = std::max(0.0, readDouble(ini, "General", "WheeledUphill", 1.0));
    terrainMovementRules_.wheeledDownhill = std::max(0.0, readDouble(ini, "General", "WheeledDownhill", 1.0));
    veterancyRules_.veteranCombat = std::max(
        0.0,
        readDouble(ini, "General", "VeteranCombat", 1.0)
    );
    veterancyRules_.veteranSpeed = std::max(
        0.0,
        readDouble(ini, "General", "VeteranSpeed", 1.0)
    );
    veterancyRules_.veteranSight = std::max(
        1.0,
        readDouble(ini, "General", "VeteranSight", 1.0)
    );
    veterancyRules_.veteranArmor = std::max(
        0.0,
        readDouble(ini, "General", "VeteranArmor", 1.0)
    );
    veterancyRules_.veteranRof = std::max(
        0.0,
        readDouble(ini, "General", "VeteranROF", 1.0)
    );
    veterancyRules_.selfHealRateMinutes = std::max(
        0.0,
        readDouble(ini, "General", "RepairRate", 0.016)
    );
    veterancyRules_.veteranCap = std::clamp(
        readInt(ini, "General", "VeteranCap", 2),
        0,
        2
    );
    veterancyRules_.initialVeteran = readBool(
        ini,
        "General",
        "InitialVeteran",
        false
    );
    specialBuildingRules_.selfHealInfantryFrames = std::max(
        0,
        readInt(ini, "General", "SelfHealInfantryFrames", 50)
    );
    specialBuildingRules_.selfHealInfantryAmount = std::max(
        0,
        readInt(ini, "General", "SelfHealInfantryAmount", 20)
    );
    specialBuildingRules_.selfHealUnitFrames = std::max(
        0,
        readInt(ini, "General", "SelfHealUnitFrames", 75)
    );
    specialBuildingRules_.selfHealUnitAmount = std::max(
        0,
        readInt(ini, "General", "SelfHealUnitAmount", 5)
    );
    specialBuildingRules_.secretInfantry = mergeUniqueIniLists(
        ini,
        "General",
        {"SecretInfantry", "SecretInfantries"}
    );
    specialBuildingRules_.secretUnits = mergeUniqueIniLists(
        ini,
        "General",
        {"SecretUnit", "SecretUnits"}
    );
    specialBuildingRules_.secretBuildings = mergeUniqueIniLists(
        ini,
        "General",
        {"SecretBuilding", "SecretBuildings"}
    );
    maximumQueuedObjects_ = std::max(
        0,
        readInt(ini, "General", "MaximumQueuedObjects", 29)
    );
    const ProductionRulesLoadResult productionLoad = loadProductionRules(ini);
    for (const std::string& warning : productionLoad.buildupParseDiagnostics) {
        addWarning(warning);
    }
    harvesterCadenceRules_.loadRate = std::clamp(
        readInt(ini, "General", "HarvesterLoadRate", 2), 1, 1000000);
    const double dumpMinutes = readDouble(ini, "General", "HarvesterDumpRate", 0.016);
    harvesterCadenceRules_.dumpRateMinutes = std::isfinite(dumpMinutes)
        && dumpMinutes >= 0.0 && dumpMinutes <= 100.0 ? dumpMinutes : 0.016;
    productionRules_ = productionLoad.values;
    for (const std::string& warning : productionLoad.diagnostics) {
        addWarning(warning);
    }
    const std::string rawRefundPercent = trim(
        ini.getValue("General", "RefundPercent", "")
    );
    constexpr std::array<const char*, 3> crewDivisorKeys{
        "AlliedSurvivorDivisor", "SovietSurvivorDivisor", "ThirdSurvivorDivisor"};
    constexpr std::array<const char*, 3> crewTypeKeys{"AlliedCrew", "SovietCrew", "ThirdCrew"};
    for (std::size_t side = 0; side < 3; ++side) {
        crewSurvivorRules_.survivorDivisors[side] = std::max(0,
            readInt(ini, "General", crewDivisorKeys[side], crewSurvivorRules_.survivorDivisors[side]));
        crewSurvivorRules_.crewTypes[side] = trim(ini.getValue("General", crewTypeKeys[side], crewSurvivorRules_.crewTypes[side]));
    }
    crewSurvivorRules_.technician = trim(ini.getValue("General", "Technician", "CTECH"));
    crewSurvivorRules_.engineer = trim(ini.getValue("General", "Engineer", "ENGINEER"));
    const double crewEscape = readRatio(ini, "General", "CrewEscape", 0.5);
    crewSurvivorRules_.escapeChance = std::isfinite(crewEscape) ? std::clamp(crewEscape, 0.0, 1.0) : 0.5;
    if (rawRefundPercent.empty()) {
        refundPercent_ = 0.5;
    } else {
        const double parsedRefundPercent = readRatio(
            ini,
            "General",
            "RefundPercent",
            0.5
        );
        if (!std::isfinite(parsedRefundPercent)
            || parsedRefundPercent < 0.0
            || parsedRefundPercent > 1.0) {
            addWarning(
                "Invalid ratio: [General] RefundPercent="
                + rawRefundPercent
            );
            refundPercent_ = 0.5;
        } else {
            refundPercent_ = parsedRefundPercent;
        }
    }
    const auto readConditionRatio = [this, &ini](
        const std::string& section,
        const std::string& key,
        double defaultValue
    ) {
        const std::string raw = trim(ini.getValue(section, key, ""));
        if (raw.empty()) {
            return defaultValue;
        }
        const double parsed = readRatio(ini, section, key, defaultValue);
        if (!std::isfinite(parsed) || parsed < 0.0 || parsed > 1.0) {
            addWarning(
                "Invalid ratio: [" + section + "] " + key + "=" + raw
            );
            return defaultValue;
        }
        return parsed;
    };
    const std::string aiSuperDefenseProbabilityText = trim(
        ini.getValue("General", "AISuperDefenseProbability", "")
    );
    if (!aiSuperDefenseProbabilityText.empty()) {
        const std::vector<int> probability = parseIntegerList(
            aiSuperDefenseProbabilityText
        );
        if (probability.size() == 3U) {
            for (std::size_t index = 0; index < 3U; ++index) {
                specialMechanismRules_.aiSuperDefense.probabilityPercent[index] =
                    std::clamp(probability[index], 0, 100);
            }
        } else {
            addWarning(
                "Invalid integer tuple: [General] AISuperDefenseProbability="
                + aiSuperDefenseProbabilityText
            );
        }
    }
    specialMechanismRules_.aiSuperDefense.alertFrames = std::max(
        0,
        readInt(ini, "General", "AISuperDefenseFrames", 50)
    );
    specialMechanismRules_.aiSuperDefense.distanceCells = std::max(
        0,
        readInt(ini, "General", "AISuperDefenseDistance", 12)
    );

    combatDetailRules_.metallicDebris = ini.getValue("General", "MetallicDebris", "");
    specialMechanismRules_.conditionRed = readConditionRatio(
        "AudioVisual",
        "ConditionRed",
        0.25
    );
    // ConditionYellow is a General rule in the runtime-facing contract.
    // Stock RA2/YR data authored it under AudioVisual, so retain that as a
    // deterministic compatibility fallback when General does not override it.
    const std::string conditionYellowSection = trim(
        ini.getValue("General", "ConditionYellow", "")
    ).empty() ? "AudioVisual" : "General";
    specialMechanismRules_.conditionYellow = readConditionRatio(
        conditionYellowSection,
        "ConditionYellow",
        0.5
    );
    const auto readAmbientChangeValue = [this, &ini](
        const std::string& key
    ) {
        const std::string raw = trim(ini.getValue("AudioVisual", key, ""));
        if (raw.empty()) {
            return 0.0;
        }
        double parsed = 0.0;
        if (!parseFiniteDouble(raw, parsed) || parsed < 0.0) {
            addWarning(
                "Invalid number: [AudioVisual] " + key + "=" + raw
            );
            return 0.0;
        }
        return parsed;
    };
    specialMechanismRules_.ambientChangeRate =
        readAmbientChangeValue("AmbientChangeRate");
    specialMechanismRules_.ambientChangeStep =
        readAmbientChangeValue("AmbientChangeStep");
    specialMechanismRules_.occupyDamageMultiplier = std::max(
        0.0,
        readDouble(
            ini,
            "CombatDamage",
            "OccupyDamageMultiplier",
            1.2
        )
    );
    specialMechanismRules_.occupyRofMultiplier = std::max(
        0.0,
        readDouble(
            ini,
            "CombatDamage",
            "OccupyROFMultiplier",
            1.2
        )
    );
    specialMechanismRules_.occupyWeaponRange = std::max(
        0,
        readInt(ini, "CombatDamage", "OccupyWeaponRange", 5)
    );
    specialMechanismRules_.directRockingCoefficient = std::clamp(
        readDouble(ini, "AudioVisual", "DirectRockingCoefficient", 1.5), 0.0, 10.0);
    // Bunker modifiers are independent of infantry garrison modifiers.
    specialMechanismRules_.bunkerDamageMultiplier = std::max(0.0,
        readDouble(ini, "CombatDamage", "BunkerDamageMultiplier", 1.3));
    specialMechanismRules_.bunkerRofMultiplier = std::max(0.0,
        readDouble(ini, "CombatDamage", "BunkerROFMultiplier", 1.3));
    specialMechanismRules_.bunkerWeaponRangeBonus =
        readDouble(ini, "CombatDamage", "BunkerWeaponRangeBonus", 2.0);
    specialMechanismRules_.prismSupport.prismType = trim(
        ini.getValue("General", "PrismType", "")
    );
    if (isNoneValue(specialMechanismRules_.prismSupport.prismType)) {
        specialMechanismRules_.prismSupport.prismType.clear();
    }
    specialMechanismRules_.prismSupport.supportModifier = std::clamp(
        readRatio(ini, "General", "PrismSupportModifier", 1.0),
        0.0,
        1000.0
    );
    specialMechanismRules_.prismSupport.supportMax = std::clamp(
        readInt(ini, "General", "PrismSupportMax", 8),
        0,
        256
    );
    specialMechanismRules_.prismSupport.supportDelayFrames = std::clamp(
        readInt(ini, "General", "PrismSupportDelay", 100),
        0,
        1000000
    );
    specialMechanismRules_.prismSupport.supportDurationFrames = std::clamp(
        readInt(ini, "General", "PrismSupportDuration", 15),
        0,
        1000000
    );
    specialMechanismRules_.prismSupport.supportHeightLeptons = std::clamp(
        readInt(ini, "General", "PrismSupportHeight", 300),
        0,
        1000000
    );
    specialMechanismRules_.cloakingStages = std::max(
        1,
        readInt(ini, "General", "CloakingStages", 9)
    );
    specialMechanismRules_.cloakDelayMinutes = std::max(
        0.0,
        readDouble(ini, "General", "CloakDelay", 0.02)
    );
    specialMechanismRules_.chronoDelay = std::max(
        0,
        readInt(ini, "General", "ChronoDelay", 60)
    );
    specialMechanismRules_.chronoReinforcementDelay = std::max(
        0,
        readInt(ini, "General", "ChronoReinfDelay", 180)
    );
    specialMechanismRules_.chronoDistanceFactor = std::max(
        1,
        readInt(ini, "General", "ChronoDistanceFactor", 48)
    );
    specialMechanismRules_.chronoTrigger = readBool(
        ini,
        "General",
        "ChronoTrigger",
        true
    );
    specialMechanismRules_.chronoMinimumDelay = std::max(
        0,
        readInt(ini, "General", "ChronoMinimumDelay", 16)
    );
    specialMechanismRules_.chronoRangeMinimum = std::max(
        0,
        readInt(ini, "General", "ChronoRangeMinimum", 0)
    );
    specialMechanismRules_.chronoPlacementAnim = trim(
        ini.getValue("AudioVisual", "ChronoPlacement", "")
    );
    specialMechanismRules_.chronoBeamAnim = trim(
        ini.getValue("AudioVisual", "ChronoBeam", "")
    );
    specialMechanismRules_.chronoBlastAnim = trim(
        ini.getValue("General", "ChronoBlast", ini.getValue("AudioVisual", "ChronoBlast", "")));
    specialMechanismRules_.chronoBlastDestinationAnim = trim(
        ini.getValue("General", "ChronoBlastDest", ini.getValue("AudioVisual", "ChronoBlastDest", "")));
    specialMechanismRules_.warpInAnim = trim(
        ini.getValue("General", "WarpIn", ini.getValue("General", "WarpIn", ini.getValue("AudioVisual", "WarpIn", ""))));
    specialMechanismRules_.warpOutAnim = trim(
        ini.getValue("General", "WarpOut", ini.getValue("General", "WarpOut", ini.getValue("AudioVisual", "WarpOut", ""))));
    specialMechanismRules_.warpAwayAnim = trim(
        ini.getValue("General", "WarpAway", ini.getValue("AudioVisual", "WarpAway", ""))
    );
    specialMechanismRules_.chronoSparkleAnim = trim(
        ini.getValue("General", "ChronoSparkle1", ini.getValue("AudioVisual", "ChronoSparkle1", ""))
    );
    specialMechanismRules_.chronoInSound = trim(
        ini.getValue("AudioVisual", "ChronoInSound", "")
    );
    specialMechanismRules_.chronoOutSound = trim(
        ini.getValue("AudioVisual", "ChronoOutSound", "")
    );
    specialMechanismRules_.diskLaserChargeUp = trim(
        ini.getValue("AudioVisual", "DiskLaserChargeUp", "")
    );
    specialMechanismRules_.drainMoneyFrameDelay = std::clamp(
        readInt(ini, "CombatDamage", "DrainMoneyFrameDelay", 30), 1, 36000);
    specialMechanismRules_.drainMoneyAmount = std::clamp(
        readInt(ini, "CombatDamage", "DrainMoneyAmount", 30), 0, 1000000000);
    specialMechanismRules_.drainAnimationType = trim(
        ini.getValue("CombatDamage", "DrainAnimationType", "DISKRAY"));
    specialMechanismRules_.mindControlSound = trim(
        ini.getValue("AudioVisual", "YuriMindControlSound", "")
    );
    specialMechanismRules_.masterMindOverloadDeathSound = trim(
        ini.getValue("AudioVisual", "MasterMindOverloadDeathSound", "")
    );
    {
        auto counts = specialMechanismRules_.overloadCount;
        auto damage = specialMechanismRules_.overloadDamage;
        auto frames = specialMechanismRules_.overloadFrames;
        const auto readOverloadList = [&](const char* key, std::vector<int>& values) {
            const auto raw = ini.getValue("CombatDamage", key, "\x01");
            if (raw == "\x01") return true;
            const auto tokens = splitList(raw);
            if (tokens.empty() || tokens.size() > 64U) return false;
            values.clear();
            for (const auto& token : tokens) {
                try {
                    std::size_t end = 0;
                    const int value = std::stoi(token, &end);
                    if (end != token.size() || value < 0) return false;
                    values.push_back(value);
                } catch (...) { return false; }
            }
            return true;
        };
        const bool valid = readOverloadList("OverloadCount", counts)
            && readOverloadList("OverloadDamage", damage)
            && readOverloadList("OverloadFrames", frames)
            && counts.size() == damage.size() && counts.size() == frames.size()
            && std::adjacent_find(counts.begin(), counts.end(), [](int a, int b) { return a >= b; }) == counts.end()
            && std::all_of(frames.begin(), frames.end(), [](int value) { return value <= 1000000; });
        if (valid) {
            specialMechanismRules_.overloadCount = std::move(counts);
            specialMechanismRules_.overloadDamage = std::move(damage);
            specialMechanismRules_.overloadFrames = std::move(frames);
        } else warnings_.push_back("Invalid Mastermind overload table; using stock defaults");
    }
    specialMechanismRules_.teslaChargeSound = trim(
        ini.getValue("AudioVisual", "TeslaCharge", "")
    );
    specialMechanismRules_.teslaZapSound = trim(
        ini.getValue("AudioVisual", "TeslaZap", "")
    );
    specialMechanismRules_.alliedDisguise = trim(
        ini.getValue("General", "AlliedDisguise", "")
    );
    specialMechanismRules_.sovietDisguise = trim(
        ini.getValue("General", "SovietDisguise", "")
    );
    specialMechanismRules_.thirdDisguise = trim(
        ini.getValue("General", "ThirdDisguise", "")
    );
    specialMechanismRules_.spyPowerBlackoutFrames = std::max(
        0,
        readInt(ini, "General", "SpyPowerBlackout", 1000)
    );
    specialMechanismRules_.spyMoneyStealPercent = std::max(
        0.0,
        readRatio(ini, "General", "SpyMoneyStealPercent", 0.5)
    );
    specialMechanismRules_.attackCursorOnDisguise = readBool(
        ini,
        "General",
        "AttackCursorOnDisguise",
        true
    );
    specialMechanismRules_.defaultMirageDisguises = splitList(
        ini.getValue(
            "General",
            "DefaultMirageDisguises",
            "TREE01,TREE02,TREE03,TREE04"
        )
    );
    specialMechanismRules_.infantryBlinkDisguiseTime = std::max(
        0,
        readInt(ini, "General", "InfantryBlinkDisguiseTime", 20)
    );
    specialMechanismRules_.mindControlAttackLineFrames = std::max(
        0,
        readInt(ini, "CombatDamage", "MindControlAttackLineFrames", 20)
    );
    specialMechanismRules_.controlledAnimationType = trim(ini.getValue(
        "CombatDamage", "ControlledAnimationType", "MINDANIM"));
    specialMechanismRules_.permaControlledAnimationType = trim(ini.getValue(
        "CombatDamage", "PermaControlledAnimationType", "MINDANIMR"));
    specialMechanismRules_.disguiseDelay = std::max(
        0,
        readInt(ini, "CombatDamage", "DisguiseDelay", 1)
    );
    specialMechanismRules_.disguiseRearmDelay = std::max(
        0,
        readInt(ini, "CombatDamage", "DisguiseRearmDelay", 1)
    );
    specialMechanismRules_.reloadRateMinutes = std::max(
        0.0,
        readDouble(ini, "General", "ReloadRate", 0.3)
    );
    specialMechanismRules_.repairPercent = std::max(
        0.0,
        readRatio(ini, "General", "RepairPercent", 0.15)
    );
    specialMechanismRules_.buildingRepairRateMinutes = std::clamp(
        readDouble(ini, "General", "RepairRate", 0.016), 0.0, 60.0);
    combatDetailRules_.ballisticScatter = std::clamp(
        readDouble(ini, "General", "BallisticScatter", 1.0), 0.0, 256.0);
    combatDetailRules_.projectileGravity = std::clamp(
        readInt(ini, "AudioVisual", "Gravity", 6), 0, 1000);
    combatDetailRules_.turboBoost = std::clamp(
        readDouble(ini, "General", "TurboBoost", 1.5), 0.01, 100.0);
    combatDetailRules_.openToppedRangeBonus = std::clamp(
        readInt(ini, "General", "OpenToppedRangeBonus", 2), -256, 256);
    combatDetailRules_.openToppedDamageMultiplier = std::clamp(
        readDouble(ini, "General", "OpenToppedDamageMultiplier", 1.2), 0.0, 100.0);
    combatDetailRules_.openToppedWarpDistance = std::clamp(
        readDouble(ini, "General", "OpenToppedWarpDistance", 7.0), 0.0, 256.0);
    specialMechanismRules_.repairStep = std::max(
        0,
        readInt(ini, "General", "RepairStep", 8)
    );
    specialMechanismRules_.unitRepairRateMinutes = std::max(
        0.0,
        readDouble(ini, "General", "URepairRate", 0.016)
    );
    specialMechanismRules_.infantryRepairRateMinutes = std::max(
        0.0,
        readDouble(ini, "General", "IRepairRate", 0.001)
    );
    specialMechanismRules_.infantryRepairStep = std::max(
        0,
        readInt(ini, "General", "IRepairStep", 20)
    );
    specialMechanismRules_.repairBayTypes = splitList(
        ini.getValue("General", "RepairBay", "")
    );
    specialMechanismRules_.c4DelayMinutes = std::max(
        0.0,
        readDouble(ini, "CombatDamage", "C4Delay", 0.03)
    );
    specialMechanismRules_.c4Warhead = trim(
        ini.getValue("CombatDamage", "C4Warhead", "Super")
    );
    specialMechanismRules_.flameDamageWarhead = trim(
        ini.getValue("CombatDamage", "FlameDamage", "Fire")
    );
    specialMechanismRules_.ivanWarhead = trim(
        ini.getValue("CombatDamage", "IvanWarhead", "IvanWH")
    );
    specialMechanismRules_.ivanDamage = std::max(
        0,
        readInt(ini, "CombatDamage", "IvanDamage", 450)
    );
    specialMechanismRules_.ivanTimedDelayFrames = std::max(
        0,
        readInt(ini, "CombatDamage", "IvanTimedDelay", 450)
    );
    specialMechanismRules_.canDetonateTimeBomb = readBool(
        ini,
        "CombatDamage",
        "CanDetonateTimeBomb",
        false
    );
    specialMechanismRules_.canDetonateDeathBomb = readBool(
        ini,
        "CombatDamage",
        "CanDetonateDeathBomb",
        false
    );
    specialMechanismRules_.ivanIconFlickerRateFrames = std::max(
        0,
        readInt(ini, "CombatDamage", "IvanIconFlickerRate", 8)
    );
    specialMechanismRules_.defaultDeathWeapon = trim(
        ini.getValue(
            "CombatDamage",
            "DeathWeapon",
            "DefaultDeathWeapon"
        )
    );
    specialMechanismRules_.fallingDamageMultiplier = std::max(
        0.0,
        readDouble(ini, "CombatDamage", "FallingDamageMultiplier", 1.0)
    );
    specialMechanismRules_.currentStrengthDamage = readBool(
        ini,
        "CombatDamage",
        "CurrentStrengthDamage",
        true
    );
    specialMechanismRules_.radiationDurationMultiple = std::max(
        1,
        readInt(ini, "Radiation", "RadDurationMultiple", 1)
    );
    specialMechanismRules_.radiationApplicationDelay = std::max(
        1,
        readInt(ini, "Radiation", "RadApplicationDelay", 16)
    );
    specialMechanismRules_.radiationLevelMax = std::max(
        1,
        readInt(ini, "Radiation", "RadLevelMax", 500)
    );
    specialMechanismRules_.radiationLevelDelay = std::max(
        1,
        readInt(ini, "Radiation", "RadLevelDelay", 90)
    );
    specialMechanismRules_.radiationLightDelay = std::max(
        1,
        readInt(ini, "Radiation", "RadLightDelay", 90)
    );
    specialMechanismRules_.radiationLevelFactor = std::max(
        0.0,
        readDouble(ini, "Radiation", "RadLevelFactor", 0.2)
    );
    specialMechanismRules_.radiationLightFactor = std::max(
        0.0,
        readDouble(ini, "Radiation", "RadLightFactor", 0.1)
    );
    specialMechanismRules_.radiationTintFactor = std::max(
        0.0,
        readDouble(ini, "Radiation", "RadTintFactor", 1.0)
    );
    specialMechanismRules_.radiationColor = trim(
        ini.getValue("Radiation", "RadColor", "0,255,0")
    );
    specialMechanismRules_.radiationSiteWarhead = trim(
        ini.getValue("Radiation", "RadSiteWarhead", "RadSite")
    );
    purifierBonus_ = std::max(
        0.0,
        readDouble(ini, "General", "PurifierBonus", 0.0)
    );
    const auto readDifficultyCounts = [&](const char* key, std::array<int, 3>& values) {
        const auto authored = parseIntegerList(ini.getValue("General", key, ""));
        for (std::size_t index = 0; index < std::min(values.size(), authored.size()); ++index)
            values[index] = std::max(0, authored[index]);
    };
    readDifficultyCounts("AIVirtualPurifiers", computerMiningRules_.virtualPurifiers);
    readDifficultyCounts("HarvestersPerRefinery", computerMiningRules_.harvestersPerRefinery);
    readDifficultyCounts("AISlaveMinerNumber", computerMiningRules_.slaveMinerNumber);
    readDifficultyCounts("AlliedBaseDefenseCounts", computerBaseBuildingRules_.alliedDefenseCounts);
    readDifficultyCounts("SovietBaseDefenseCounts", computerBaseBuildingRules_.sovietDefenseCounts);
    readDifficultyCounts("ThirdBaseDefenseCounts", computerBaseBuildingRules_.thirdDefenseCounts);
    readDifficultyCounts("AIExtraRefineries", computerBaseBuildingRules_.extraRefineries);
    computerBaseBuildingRules_.refineryTypes = splitList(
        ini.getValue("AI", "BuildRefinery", "NAREFN,GAREFN,YAREFN"));
    computerBaseBuildingRules_.harvesterTypes = splitList(
        ini.getValue("General", "HarvesterUnit", "HARV,CMIN"));
    readDifficultyCounts("TeamDelays", computerTacticsRules_.teamDelays);
    readDifficultyCounts("MinimumAIDefensiveTeams", computerTacticsRules_.minimumDefensiveTeams);
    readDifficultyCounts("MaximumAIDefensiveTeams", computerTacticsRules_.maximumDefensiveTeams);
    readDifficultyCounts("TotalAITeamCap", computerTacticsRules_.totalTeamCap);
    readDifficultyCounts("FillEarliestTeamProbability", computerTacticsRules_.fillEarliestTeamProbability);
    computerTacticsRules_.dissolveUnfilledTeamDelay = std::max(0,
        readInt(ini, "General", "DissolveUnfilledTeamDelay", 5000));
    computerTacticsRules_.useMinimumDefenseRule = readBool(ini, "General", "UseMinDefenseRule", true);
    computerTacticsRules_.minorSuperReadyPercent = std::clamp(
        readDouble(ini, "General", "AIMinorSuperReadyPercent", 0.7), 0.0, 1.0);
    computerTacticsRules_.safeDistance = std::max(0, readInt(ini, "General", "AISafeDistance", 20));
    computerTacticsRules_.stray = std::max(0.0, readDouble(ini, "General", "Stray", 2.0));
    computerTacticsRules_.relaxedStray = std::max(0.0, readDouble(ini, "General", "RelaxedStray", 3.0));
    computerTacticsRules_.successWeightDelta = readDouble(ini, "General", "AITriggerSuccessWeightDelta", 20.0);
    computerTacticsRules_.failureWeightDelta = readDouble(ini, "General", "AITriggerFailureWeightDelta", -50.0);
    computerTacticsRules_.trackRecordCoefficient = std::max(0.0,
        readDouble(ini, "General", "AITriggerTrackRecordCoefficient", 1.0));
    const auto readCaptureWeights = [&](const char* key, std::array<int, 4>& weights) {
        const auto authored = parseIntegerList(ini.getValue("General", key, ""));
        for (std::size_t index = 0; index < std::min(weights.size(), authored.size()); ++index)
            weights[index] = std::clamp(authored[index], 0, 100);
    };
    readCaptureWeights("AICaptureNormal", computerTacticsRules_.captureNormal);
    readCaptureWeights("AICaptureWounded", computerTacticsRules_.captureWounded);
    readCaptureWeights("AICaptureLowPower", computerTacticsRules_.captureLowPower);
    readCaptureWeights("AICaptureLowMoney", computerTacticsRules_.captureLowMoney);
    computerTacticsRules_.captureLowMoneyMark = std::max(0,
        readInt(ini, "General", "AICaptureLowMoneyMark", 2000));
    computerTacticsRules_.captureWoundedMark = std::clamp(
        readDouble(ini, "General", "AICaptureWoundedMark", 0.25), 0.0, 1.0);
    destroyableBridges_ = readBool(
        ini,
        "CombatDamage",
        "DestroyableBridges",
        true
    );
    bridgeStrength_ = std::max(
        1,
        readInt(ini, "CombatDamage", "BridgeStrength", 1500)
    );
    const auto loadSpawnedMissileProfile =
        [this, &ini](
            const std::string& prefix,
            const std::string& typeKey,
            const std::string& warheadKey,
            const std::string& eliteWarheadKey,
            const std::string& terminalWeapon
        ) {
            SpawnedMissileProfile profile;
            profile.typeId = ini.getValue("General", typeKey, "");
            if (profile.typeId.empty()) {
                return;
            }
            profile.terminalWeapon = terminalWeapon;
            profile.pitchInitial = std::clamp(readDouble(ini, "General", prefix + "PitchInitial", 0.0), 0.0, 1.0);
            profile.pitchFinal = std::clamp(readDouble(ini, "General", prefix + "PitchFinal", 0.5), 0.0, 1.0);
            profile.turnRate = std::max(0.001, readDouble(ini, "General", prefix + "TurnRate", 0.08));
            profile.raiseRate = std::max(0.0, readDouble(ini, "General", prefix + "RaiseRate", 0.0));
            profile.acceleration = std::max(0.001, readDouble(ini, "General", prefix + "Acceleration", 0.8));
            profile.bodyLength = std::max(0, readInt(ini, "General", prefix + "BodyLength", 0));
            profile.launchSound = ini.getValue(profile.typeId, "AuxSound1", "");
            profile.pauseFrames = std::max(
                0,
                readInt(ini, "General", prefix + "PauseFrames", 0)
            );
            profile.tiltFrames = std::max(
                0,
                readInt(ini, "General", prefix + "TiltFrames", 0)
            );
            profile.altitudeLeptons = std::max(
                0,
                readInt(ini, "General", prefix + "Altitude", 0)
            );
            profile.damage = std::max(
                0,
                readInt(ini, "General", prefix + "Damage", 0)
            );
            profile.eliteDamage = std::max(
                profile.damage,
                readInt(ini, "General", prefix + "EliteDamage", profile.damage)
            );
            profile.warhead =
                ini.getValue("CombatDamage", warheadKey, "");
            profile.eliteWarhead =
                ini.getValue("CombatDamage", eliteWarheadKey, profile.warhead);
            profile.lazyCurve =
                readBool(ini, "General", prefix + "LazyCurve", false);
            spawnedMissileProfiles_.push_back(std::move(profile));
        };
    loadSpawnedMissileProfile(
        "V3Rocket",
        "V3RocketType",
        "V3Warhead",
        "V3EliteWarhead",
        "V3Airburst"
    );
    loadSpawnedMissileProfile(
        "DMisl",
        "DMislType",
        "DMislWarhead",
        "DMislEliteWarhead",
        ""
    );
    loadSpawnedMissileProfile(
        "CMisl", "CMislType", "CMislWarhead", "CMislEliteWarhead", ""
    );
    harvesterTooFarDistance_ = std::max(
        0.0,
        readDouble(ini, "General", "HarvesterTooFarDistance", 5.0)
    );
    chronoHarvTooFarDistance_ = std::max(
        0.0,
        readDouble(ini, "General", "ChronoHarvTooFarDistance", 50.0)
    );
    const int legacyTiberiumNearScan =
        readInt(ini, "AI", "TiberiumNearScan", 5);
    const int legacyTiberiumFarScan =
        readInt(ini, "AI", "TiberiumFarScan", 50);
    tiberiumNearScan_ = std::max(
        1,
        readInt(
            ini,
            "General",
            "TiberiumShortScan",
            legacyTiberiumNearScan
        )
    );
    tiberiumFarScan_ = std::max(
        tiberiumNearScan_,
        readInt(
            ini,
            "General",
            "TiberiumLongScan",
            legacyTiberiumFarScan
        )
    );
    slaveMinerShortScan_ =
        std::max(1, readInt(ini, "General", "SlaveMinerShortScan", 8));
    slaveMinerSlaveScan_ =
        std::max(1, readInt(ini, "General", "SlaveMinerSlaveScan", 14));
    slaveMinerLongScan_ =
        std::max(slaveMinerSlaveScan_,
            readInt(ini, "General", "SlaveMinerLongScan", 48));
    slaveMinerScanCorrection_ =
        std::max(0, readInt(ini, "General", "SlaveMinerScanCorrection", 3));
    slaveMinerKickFrameDelay_ =
        std::max(1, readInt(ini, "General", "SlaveMinerKickFrameDelay", 150));
    poseDir_ = readInt(
        ini,
        "AudioVisual",
        "PoseDir",
        readInt(ini, "General", "PoseDir", 0)
    );
    traceRulesDatabaseLoad(
        "PoseDir raw="
        + ini.getValue(
            "AudioVisual",
            "PoseDir",
            ini.getValue("General", "PoseDir", "<missing>")
        )
        + " parsed=" + std::to_string(poseDir_)
    );
    {
        std::unordered_set<std::string> loadedDamageFireTypes;
        std::string configuredDamageFireTypes = trim(
            ini.getValue("General", "DamageFireTypes", "")
        );
        if (configuredDamageFireTypes.empty()) {
            configuredDamageFireTypes = trim(
                ini.getValue("AudioVisual", "DamageFireTypes", "")
            );
        }
        if (configuredDamageFireTypes.empty()) {
            configuredDamageFireTypes = "FIRE01,FIRE02,FIRE03";
        }
        for (const std::string& id : splitList(configuredDamageFireTypes)) {
            const std::string canonical = canonicalIdKey(id);
            if (canonical.empty()
                || !loadedDamageFireTypes.insert(canonical).second) {
                continue;
            }
            damageFireTypes_.push_back(trim(id));
        }
    }
    oreTwinkle_ = trim(ini.getValue("AudioVisual", "OreTwinkle", ""));
    if (oreTwinkle_.empty()) {
        oreTwinkle_ = trim(ini.getValue("General", "OreTwinkle", "TWNK1"));
    }
    if (oreTwinkle_.empty()) {
        oreTwinkle_ = "TWNK1";
    }
    wake_ = trim(ini.getValue("General", "Wake", ""));
    oreTwinkleChance_ =
        std::max(0, readInt(ini, "AudioVisual", "OreTwinkleChance", -1));
    if (oreTwinkleChance_ <= 0) {
        oreTwinkleChance_ =
            std::max(0, readInt(ini, "General", "OreTwinkleChance", 30));
    }
    tiberiumHealMinutes_ =
        std::max(0.0, readDouble(ini, "General", "TiberiumHeal", 0.01));
    tiberiumGrows_ = readBool(ini, "General", "TiberiumGrows", true);
    tiberiumSpreads_ = readBool(ini, "General", "TiberiumSpreads", true);
    {
        std::unordered_set<std::string> loadedPadAircraft;
        for (const std::string& id :
            splitList(ini.getValue("General", "PadAircraft", ""))) {
            const std::string canonical = canonicalIdKey(id);
            if (canonical.empty()
                || !loadedPadAircraft.insert(canonical).second) {
                continue;
            }
            padAircraft_.push_back(trim(id));
        }
    }

    voxelAnimTypes_.clear();
    for (const auto& id : collectTypeIdsFromList(ini, "VoxelAnims", false, true)) {
        VoxelAnimType type; type.id = id;
        type.image = ini.getValue(id, "Image", id);
        type.duration = std::clamp(readInt(ini, id, "Duration", 30), 0, 1000000);
        type.elasticity = std::clamp(readDouble(ini, id, "Elasticity", type.elasticity), 0.0, 1.0);
        // The retail reader converts nonzero angular values to radians before
        // the instance constructor performs its integer-modulo selection.
        const double angularMin = readDouble(ini, id, "MinAngularVelocity", -1.0);
        const double angularMax = readDouble(ini, id, "MaxAngularVelocity", -1.0);
        if (angularMin != 0.0) type.minAngularVelocity = angularMin * 0.01745277777777778;
        if (angularMax != 0.0) type.maxAngularVelocity = angularMax * 0.01745277777777778;
        type.minZVelocity = std::clamp(readDouble(ini, id, "MinZVel", type.minZVelocity), -100000.0, 100000.0);
        type.maxZVelocity = std::clamp(readDouble(ini, id, "MaxZVel", type.maxZVelocity), -100000.0, 100000.0);
        type.maxXYVelocity = std::clamp(readDouble(ini, id, "MaxXYVel", type.maxXYVelocity), 0.0, 100000.0);
        type.bounceAnim = ini.getValue(id, "BounceAnim", "");
        type.expireAnim = ini.getValue(id, "ExpireAnim", "");
        type.trailerAnim = ini.getValue(id, "TrailerAnim", "");
        type.damage = std::max(0, readInt(ini, id, "Damage", 0));
        type.damageRadius = std::max(0, readInt(ini, id, "DamageRadius", 0));
        type.warhead = ini.getValue(id, "Warhead", "");
        voxelAnimTypes_.push_back(std::move(type));
    }

    traceRulesDatabaseLoad("load InfantryTypes");
    loadUnitList(ini, "InfantryTypes", infantryTypes_);
    traceTypeIdHealth(infantryTypes_, "infantry before id repair");
    infantryTypeIds_ = collectTypeIdsFromList(ini, "InfantryTypes", true, false);
    repairLoadedTypeIdsFromList(ini, "InfantryTypes", infantryTypes_);
    traceRulesDatabaseLoad(
        "loaded InfantryTypes count=" + std::to_string(infantryTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after load");
    traceRulesDatabaseLoad("load VehicleTypes");
    loadUnitList(ini, "VehicleTypes", vehicleTypes_);
    vehicleTypeIds_ = collectTypeIdsFromList(ini, "VehicleTypes", true, false);
    authoredVehicleTypeIds_ = collectAuthoredTypeIdsFromList(ini, "VehicleTypes");
    repairLoadedTypeIdsFromList(ini, "VehicleTypes", vehicleTypes_);
    traceRulesDatabaseLoad(
        "loaded VehicleTypes count=" + std::to_string(vehicleTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after vehicles");
    traceRulesDatabaseLoad("load AircraftTypes");
    loadUnitList(ini, "AircraftTypes", aircraftTypes_);
    aircraftTypeIds_ = collectTypeIdsFromList(ini, "AircraftTypes", true, false);
    authoredAircraftTypeIds_ = collectAuthoredTypeIdsFromList(ini, "AircraftTypes");
    repairLoadedTypeIdsFromList(ini, "AircraftTypes", aircraftTypes_);
    traceRulesDatabaseLoad(
        "loaded AircraftTypes count=" + std::to_string(aircraftTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after aircraft");
    traceRulesDatabaseLoad("load BuildingTypes");
    loadBuildingList(ini, "BuildingTypes", buildingTypes_);
    buildingTypeIds_ = collectTypeIdsFromList(ini, "BuildingTypes", true, false);
    authoredBuildingTypeIds_ = collectAuthoredTypeIdsFromList(ini, "BuildingTypes");
    repairLoadedTypeIdsFromList(ini, "BuildingTypes", buildingTypes_);
    traceRulesDatabaseLoad(
        "loaded BuildingTypes count=" + std::to_string(buildingTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after buildings");
    traceRulesDatabaseLoad("load OverlayTypes");
    loadOverlayList(ini, "OverlayTypes", overlayTypes_);
    overlayTypeIds_ = collectTypeIdsFromList(ini, "OverlayTypes", false, true);
    repairLoadedOverlayIds(overlayTypeIds_, overlayTypes_);
    traceRulesDatabaseLoad(
        "loaded OverlayTypes count=" + std::to_string(overlayTypes_.size())
    );
    traceRulesDatabaseLoad("load SmudgeTypes");
    loadSmudgeList(ini, "SmudgeTypes", smudgeTypes_);
    traceRulesDatabaseLoad(
        "loaded SmudgeTypes count=" + std::to_string(smudgeTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after overlays");
    traceRulesDatabaseLoad("load Tiberiums");
    loadTiberiumList(ini, "Tiberiums", tiberiumTypes_);
    tiberiumTypeIds_ = collectTypeIdsFromList(ini, "Tiberiums", true, true);
    repairLoadedTypeIdsFromList(ini, "Tiberiums", tiberiumTypes_);
    traceRulesDatabaseLoad(
        "loaded Tiberiums count=" + std::to_string(tiberiumTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after tiberiums");
    traceRulesDatabaseLoad("load TerrainTypes");
    loadTerrainList(ini, "TerrainTypes", terrainTypes_);
    terrainTypeIds_ = collectTypeIdsFromList(ini, "TerrainTypes", true, true);
    repairLoadedTypeIdsFromList(ini, "TerrainTypes", terrainTypes_);
    traceRulesDatabaseLoad(
        "loaded TerrainTypes count=" + std::to_string(terrainTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after terrain");
    traceRulesDatabaseLoad("load LandTypes");
    loadLandTypes(ini);
    traceRulesDatabaseLoad(
        "loaded LandTypes count=" + std::to_string(landTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after land types");
    traceRulesDatabaseLoad("load ParticleSystems");
    loadParticleSystemList(ini);
    traceRulesDatabaseLoad("load Particles");
    loadParticleList(ini);

    traceRulesDatabaseLoad("discoverWeaponsFromObjects #1");
    discoverWeaponsFromObjects(ini);
    traceRulesDatabaseLoad(
        "discovered weapons count=" + std::to_string(weaponTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after discover weapons");
    traceRulesDatabaseLoad("discoverProjectilesFromWeapons #1");
    discoverProjectilesFromWeapons(ini);
    traceRulesDatabaseLoad(
        "discovered projectiles count=" + std::to_string(projectileTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after discover projectiles");
    traceRulesDatabaseLoad("discoverWeaponsFromProjectiles");
    discoverWeaponsFromProjectiles(ini);
    traceRulesDatabaseLoad(
        "post projectile weapons count=" + std::to_string(weaponTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after projectile weapons");
    traceRulesDatabaseLoad("discoverProjectilesFromWeapons #2");
    discoverProjectilesFromWeapons(ini);
    traceRulesDatabaseLoad(
        "post second projectiles count="
        + std::to_string(projectileTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after second projectiles");

    traceRulesDatabaseLoad("load Warheads");
    loadWarheadList(ini, "Warheads", warheadTypes_);
    repairLoadedTypeIdsFromList(ini, "Warheads", warheadTypes_);
    traceRulesDatabaseLoad(
        "loaded Warheads count=" + std::to_string(warheadTypes_.size())
    );
    traceTypeIdHealth(infantryTypes_, "infantry after warhead load");
    traceRulesDatabaseLoad("discoverWarheadsFromWeapons");
    discoverWarheadsFromWeapons(ini);
    traceRulesDatabaseLoad(
        "post weapon warheads count=" + std::to_string(warheadTypes_.size())
    );
    repairLoadedTypeIdsFromList(ini, "InfantryTypes", infantryTypes_);
    repairLoadedTypeIdsFromList(ini, "VehicleTypes", vehicleTypes_);
    repairLoadedTypeIdsFromList(ini, "AircraftTypes", aircraftTypes_);
    repairLoadedTypeIdsFromList(ini, "BuildingTypes", buildingTypes_);
    repairLoadedOverlayIds(overlayTypeIds_, overlayTypes_);
    repairLoadedTypeIdsFromList(ini, "Tiberiums", tiberiumTypes_);
    repairLoadedTypeIdsFromList(ini, "TerrainTypes", terrainTypes_);
    repairLoadedTypeIdsFromList(ini, "Warheads", warheadTypes_);
    traceRulesDatabaseLoad("load SuperWeaponTypes");
    loadSuperWeaponTypes(ini);
    traceTypeIdHealth(infantryTypes_, "infantry before rebuild");
    traceRulesDatabaseLoad("rebuildLookupIndexes");
    rebuildLookupIndexes();
    loadModExtensions(ini);
    for (const ParticleSystemType& system : particleSystemTypes_) {
        if (system.holdsWhat.empty()) {
            addWarning(
                "Particle system [" + system.id + "] has no HoldsWhat particle"
            );
        } else if (findParticleType(system.holdsWhat) == nullptr) {
            addWarning(
                "Particle system [" + system.id
                + "] references missing particle [" + system.holdsWhat + "]"
            );
        }
    }
    for (const ParticleType& particle : particleTypes_) {
        if (!particle.nextParticle.empty()
            && findParticleType(particle.nextParticle) == nullptr) {
            addWarning(
                "Particle [" + particle.id
                + "] references missing NextParticle ["
                + particle.nextParticle + "]"
            );
        }
        if (!particle.warhead.empty()
            && !isNoneValue(particle.warhead)
            && findWarheadType(particle.warhead) == nullptr) {
            addWarning(
                "Particle [" + particle.id
                + "] references missing Warhead [" + particle.warhead + "]"
            );
        }
    }
    for (const WeaponType& weapon : weaponTypes_) {
        const bool requestsAttachedParticles = weapon.isRailgun
            || weapon.useSparkParticles
            || weapon.useFireParticles;
        if (requestsAttachedParticles
            && weapon.attachedParticleSystem.empty()) {
            addWarning(
                "Weapon [" + weapon.id
                + "] enables attached particles without "
                  "AttachedParticleSystem"
            );
        } else if (!weapon.attachedParticleSystem.empty()
            && findParticleSystemType(weapon.attachedParticleSystem)
                == nullptr) {
            addWarning(
                "Weapon [" + weapon.id
                + "] references missing AttachedParticleSystem ["
                + weapon.attachedParticleSystem + "]"
            );
        }
    }
    for (const WarheadType& warhead : warheadTypes_) {
        if (!warhead.particle.empty()
            && findParticleSystemType(warhead.particle) == nullptr
            && findParticleType(warhead.particle) == nullptr) {
            addWarning(
                "Warhead [" + warhead.id
                + "] references missing Particle system/type ["
                + warhead.particle + "]"
            );
        }
        if (!warhead.particleSystem.empty()
            && findParticleSystemType(warhead.particleSystem) == nullptr) {
            addWarning(
                "Warhead [" + warhead.id
                + "] references missing ParticleSystem ["
                + warhead.particleSystem + "]"
            );
        }
    }
    for (const std::string& repairBayType :
        specialMechanismRules_.repairBayTypes) {
        if (findBuildingType(repairBayType) == nullptr) {
            addWarning(
                "[General] RepairBay references missing building type ["
                + repairBayType + "]"
            );
        }
    }
    for (const std::string& padAircraft : padAircraft_) {
        if (findUnitType("aircraft", padAircraft) == nullptr
            && findUnitType("vehicle", padAircraft) == nullptr) {
            addWarning(
                "[General] PadAircraft references missing aircraft/vehicle type ["
                + padAircraft + "]"
            );
        }
    }
    const auto validateAirstrikeType = [this](
        const UnitType& unit,
        const std::string& key,
        const std::string& aircraftType
    ) {
        if (!aircraftType.empty()
            && findUnitType("aircraft", aircraftType) == nullptr) {
            addWarning(
                "[" + unit.id + "] " + key
                + " references missing aircraft type [" + aircraftType + "]"
            );
        }
    };
    for (const UnitType& unit : infantryTypes_) {
        validateAirstrikeType(
            unit,
            "AirstrikeTeamType",
            unit.airstrikeTeamType
        );
        validateAirstrikeType(
            unit,
            "EliteAirstrikeTeamType",
            unit.eliteAirstrikeTeamType
        );
    }
    for (const UnitType& unit : vehicleTypes_) {
        validateAirstrikeType(
            unit,
            "AirstrikeTeamType",
            unit.airstrikeTeamType
        );
        validateAirstrikeType(
            unit,
            "EliteAirstrikeTeamType",
            unit.eliteAirstrikeTeamType
        );
    }
    for (const UnitType& unit : aircraftTypes_) {
        validateAirstrikeType(
            unit,
            "AirstrikeTeamType",
            unit.airstrikeTeamType
        );
        validateAirstrikeType(
            unit,
            "EliteAirstrikeTeamType",
            unit.eliteAirstrikeTeamType
        );
    }
    traceRulesDatabaseLoad(
        "complete PoseDir=" + std::to_string(poseDir_)
    );
    detailRulesHash_ = runtimeDetailRulesHash(*this);

    return true;
}

const std::vector<UnitType>& RulesDatabase::getInfantryTypes() const {
    return infantryTypes_;
}

const std::vector<UnitType>& RulesDatabase::getVehicleTypes() const {
    return vehicleTypes_;
}

const std::vector<UnitType>& RulesDatabase::getAircraftTypes() const {
    return aircraftTypes_;
}

const std::vector<std::string>& RulesDatabase::getPadAircraft() const {
    return padAircraft_;
}

bool RulesDatabase::isPadAircraft(const std::string& id) const {
    const std::string wanted = canonicalIdKey(id);
    if (wanted.empty()) {
        return false;
    }
    return std::any_of(
        padAircraft_.begin(),
        padAircraft_.end(),
        [&wanted](const std::string& candidate) {
            return canonicalIdKey(candidate) == wanted;
        }
    );
}

const std::vector<BuildingType>& RulesDatabase::getBuildingTypes() const {
    return buildingTypes_;
}

const std::vector<WeaponType>& RulesDatabase::getWeaponTypes() const {
    return weaponTypes_;
}

const std::vector<ProjectileType>& RulesDatabase::getProjectileTypes() const {
    return projectileTypes_;
}

const std::vector<OverlayType>& RulesDatabase::getOverlayTypes() const {
    return overlayTypes_;
}

const std::vector<SmudgeType>& RulesDatabase::getSmudgeTypes() const {
    return smudgeTypes_;
}

const std::vector<TiberiumType>& RulesDatabase::getTiberiumTypes() const {
    return tiberiumTypes_;
}

const std::vector<TerrainType>& RulesDatabase::getTerrainTypes() const {
    return terrainTypes_;
}

const std::vector<LandType>& RulesDatabase::getLandTypes() const {
    return landTypes_;
}

const std::vector<WarheadType>& RulesDatabase::getWarheadTypes() const {
    return warheadTypes_;
}

const std::vector<ParticleSystemType>&
RulesDatabase::getParticleSystemTypes() const {
    return particleSystemTypes_;
}

const std::vector<ParticleType>& RulesDatabase::getParticleTypes() const {
    return particleTypes_;
}

const std::vector<std::string>& RulesDatabase::getCountries() const {
    return countries_;
}

const std::vector<CountryDefinition>&
RulesDatabase::getCountryDefinitions() const {
    return countryDefinitions_;
}

const CountryDefinition* RulesDatabase::findCountryDefinition(
    const std::string& id
) const {
    const auto found = countryIndexById_.find(canonicalIdKey(id));
    if (found == countryIndexById_.end()
        || found->second >= countryDefinitions_.size()) {
        return nullptr;
    }
    return &countryDefinitions_[found->second];
}

const std::vector<SideDefinition>& RulesDatabase::getSides() const {
    return sides_;
}

const std::vector<PrerequisiteAlias>& RulesDatabase::getPrerequisiteAliases() const {
    return prerequisiteAliases_;
}

const std::vector<std::string>& RulesDatabase::getWarnings() const {
    return warnings_;
}

std::string RulesDatabase::getAudioVisualValue(const std::string& key) const {
    const auto found = audioVisualValues_.find(toLower(trim(key)));
    return found == audioVisualValues_.end() ? "" : found->second;
}

const std::string& RulesDatabase::getVoiceIFVRepair() const {
    return voiceIFVRepair_;
}

const std::string& RulesDatabase::getInfantryVirusAnim() const {
    return infantryVirusAnim_;
}

const std::string& RulesDatabase::getInfantryDeathAnim(int infDeath) const {
    return infantryDeathAnims_[static_cast<std::size_t>(
        infDeath >= 0 && infDeath < static_cast<int>(infantryDeathAnims_.size())
            ? infDeath : 0
    )];
}

const VeterancyRules& RulesDatabase::getVeterancyRules() const {
    return veterancyRules_;
}

const TerrainMovementRules& RulesDatabase::getTerrainMovementRules() const {
    return terrainMovementRules_;
}

const ElevationModelRules& RulesDatabase::getElevationModelRules() const {
    return elevationModelRules_;
}

const ProductionRules& RulesDatabase::getProductionRules() const {
    return productionRules_;
}

const HarvesterCadenceRules& RulesDatabase::getHarvesterCadenceRules() const {
    return harvesterCadenceRules_;
}

const CrateRules& RulesDatabase::getCrateRules() const {
    return crateRules_;
}

const SpecialBuildingRules& RulesDatabase::getSpecialBuildingRules() const {
    return specialBuildingRules_;
}

const SpecialMechanismRules& RulesDatabase::getSpecialMechanismRules() const {
    return specialMechanismRules_;
}

const CombatDetailRules& RulesDatabase::getCombatDetailRules() const {
    return combatDetailRules_;
}

const std::vector<SuperWeaponRule>& RulesDatabase::getSuperWeaponTypes() const {
    return superWeaponTypes_;
}

const SuperWeaponRule* RulesDatabase::findSuperWeaponType(
    const std::string& id
) const {
    const std::string key = canonicalIdKey(id);
    for (const SuperWeaponRule& rule : superWeaponTypes_) {
        if (canonicalIdKey(rule.id) == key) {
            return &rule;
        }
    }
    return nullptr;
}

double RulesDatabase::getPurifierBonus() const {
    return purifierBonus_;
}

const ComputerMiningRules& RulesDatabase::getComputerMiningRules() const {
    return computerMiningRules_;
}

const ComputerBaseBuildingRules& RulesDatabase::getComputerBaseBuildingRules() const {
    return computerBaseBuildingRules_;
}

const ComputerTacticsRules& RulesDatabase::getComputerTacticsRules() const {
    return computerTacticsRules_;
}

double RulesDatabase::getHarvesterTooFarDistance() const {
    return harvesterTooFarDistance_;
}

double RulesDatabase::getChronoHarvTooFarDistance() const {
    return chronoHarvTooFarDistance_;
}

int RulesDatabase::getTiberiumNearScan() const {
    return tiberiumNearScan_;
}

int RulesDatabase::getTiberiumFarScan() const {
    return tiberiumFarScan_;
}

int RulesDatabase::getSlaveMinerShortScan() const {
    return slaveMinerShortScan_;
}

int RulesDatabase::getSlaveMinerSlaveScan() const {
    return slaveMinerSlaveScan_;
}

int RulesDatabase::getSlaveMinerLongScan() const {
    return slaveMinerLongScan_;
}

int RulesDatabase::getSlaveMinerScanCorrection() const {
    return slaveMinerScanCorrection_;
}

int RulesDatabase::getSlaveMinerKickFrameDelay() const {
    return slaveMinerKickFrameDelay_;
}

int RulesDatabase::getPoseDir() const {
    return poseDir_;
}

int RulesDatabase::getWindDirection() const {
    return windDirection_;
}

const std::vector<std::string>& RulesDatabase::getDamageFireTypes() const {
    return damageFireTypes_;
}

const std::string& RulesDatabase::getOreTwinkle() const {
    return oreTwinkle_;
}

const std::string& RulesDatabase::getWake() const {
    return wake_;
}

int RulesDatabase::getOreTwinkleChance() const {
    return oreTwinkleChance_;
}

int RulesDatabase::getMaximumQueuedObjects() const {
    return maximumQueuedObjects_;
}

double RulesDatabase::getRefundPercent() const {
    return refundPercent_;
}

const CrewSurvivorRules& RulesDatabase::getCrewSurvivorRules() const {
    return crewSurvivorRules_;
}

double RulesDatabase::getTiberiumHealMinutes() const {
    return tiberiumHealMinutes_;
}

bool RulesDatabase::getTiberiumGrows() const {
    return tiberiumGrows_;
}

bool RulesDatabase::getTiberiumSpreads() const {
    return tiberiumSpreads_;
}

bool RulesDatabase::getDestroyableBridges() const {
    return destroyableBridges_;
}

int RulesDatabase::getBridgeStrength() const {
    return bridgeStrength_;
}

const SpawnedMissileProfile* RulesDatabase::findSpawnedMissileProfile(
    const std::string& typeId
) const {
    const std::string wanted = canonicalIdKey(typeId);
    for (const SpawnedMissileProfile& profile : spawnedMissileProfiles_) {
        if (canonicalIdKey(profile.typeId) == wanted) {
            return &profile;
        }
    }
    return nullptr;
}

const UnitType* RulesDatabase::findUnitType(
    const std::string& kind,
    const std::string& id
) const {
    static const bool referenceDispatch = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    if (!referenceDispatch) {
        // These exact spellings dominate runtime queries. Check their capital
        // letters with the same current C-locale mapping as canonicalIdKey;
        // notably, Turkish 'I' must retain the scalar fallback's behavior.
        if (kind == "UnitType"
            && std::tolower('U') == 'u' && std::tolower('T') == 't') {
            return findByCanonicalId(vehicleTypes_, vehicleIndexById_, id);
        }
        if (kind == "InfantryType"
            && std::tolower('I') == 'i' && std::tolower('T') == 't') {
            return findByCanonicalId(infantryTypes_, infantryIndexById_, id);
        }
        if (kind == "AircraftType"
            && std::tolower('A') == 'a' && std::tolower('T') == 't') {
            return findByCanonicalId(aircraftTypes_, aircraftIndexById_, id);
        }
    }
    const std::string normalizedKind = canonicalIdKey(kind);
    if (normalizedKind == "infantry" || normalizedKind == "infantrytype") {
        return findByCanonicalId(infantryTypes_, infantryIndexById_, id);
    }
    if (normalizedKind == "vehicle"
        || normalizedKind == "unit"
        || normalizedKind == "unittype") {
        return findByCanonicalId(vehicleTypes_, vehicleIndexById_, id);
    }
    if (normalizedKind == "aircraft" || normalizedKind == "aircrafttype") {
        return findByCanonicalId(aircraftTypes_, aircraftIndexById_, id);
    }
    return nullptr;
}

const VoxelAnimType* RulesDatabase::findVoxelAnimType(const std::string& id) const {
    const auto key = canonicalIdKey(id);
    for (const auto& type : voxelAnimTypes_) if (canonicalIdKey(type.id) == key) return &type;
    return nullptr;
}

const BuildingType* RulesDatabase::findBuildingType(const std::string& id) const {
    return findByCanonicalId(buildingTypes_, buildingIndexById_, id);
}

const WeaponType* RulesDatabase::findWeaponType(const std::string& id) const {
    return findByCanonicalId(weaponTypes_, weaponIndexById_, id);
}

const ProjectileType* RulesDatabase::findProjectileType(const std::string& id) const {
    return findByCanonicalId(projectileTypes_, projectileIndexById_, id);
}

const TiberiumType* RulesDatabase::findTiberiumType(const std::string& id) const {
    return findByCanonicalId(tiberiumTypes_, tiberiumIndexById_, id);
}

const TerrainType* RulesDatabase::findTerrainType(const std::string& id) const {
    return findByCanonicalId(terrainTypes_, terrainIndexById_, id);
}

const LandType* RulesDatabase::findLandType(const std::string& id) const {
    return findByCanonicalId(landTypes_, landTypeIndexById_, id);
}

const WarheadType* RulesDatabase::findWarheadType(const std::string& id) const {
    return findByCanonicalId(warheadTypes_, warheadIndexById_, id);
}

const ParticleSystemType* RulesDatabase::findParticleSystemType(
    const std::string& id
) const {
    return findByCanonicalId(
        particleSystemTypes_,
        particleSystemIndexById_,
        id
    );
}

const ParticleType* RulesDatabase::findParticleType(
    const std::string& id
) const {
    return findByCanonicalId(particleTypes_, particleIndexById_, id);
}

const WeaponLifecycleProfile* RulesDatabase::findWeaponLifecycleProfile(
    const std::string& id
) const {
    const auto* weapon = findWeaponType(id);
    return weapon ? &weapon->lifecycle : nullptr;
}

const WarheadLifecycleProfile* RulesDatabase::findWarheadLifecycleProfile(
    const std::string& id
) const {
    const auto* warhead = findWarheadType(id);
    return warhead ? &warhead->lifecycle : nullptr;
}

RulesCapabilityCacheStats RulesDatabase::getCapabilityCacheStats() const {
    const std::size_t objectCount =
        infantryTypes_.size()
        + vehicleTypes_.size()
        + aircraftTypes_.size()
        + buildingTypes_.size();

    RulesCapabilityCacheStats stats;
    stats.objectProfileCount = objectCount;
    stats.objectCapabilityCount = objectCount;
    stats.weaponSetCount = objectCount;
    stats.deployProfileCount = objectCount;
    stats.uiProfileCount = objectCount;
    stats.weaponProfileCount = weaponTypes_.size();
    stats.projectileProfileCount = projectileTypes_.size();
    stats.warheadProfileCount = warheadTypes_.size();
    stats.indexedObjectCount =
        infantryIndexById_.size()
        + vehicleIndexById_.size()
        + aircraftIndexById_.size()
        + buildingIndexById_.size();
    stats.indexedWeaponCount = weaponIndexById_.size();
    stats.indexedProjectileCount = projectileIndexById_.size();
    stats.indexedWarheadCount = warheadIndexById_.size();
    return stats;
}

void RulesDatabase::addWarning(const std::string& message) {
    warnings_.push_back(message);
}

int RulesDatabase::readInt(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    int defaultValue
) {
    const std::string value = ini.getValue(section, key, "");

    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        addWarning("Invalid integer: [" + section + "] " + key + "=" + value);
        return defaultValue;
    }
}

double RulesDatabase::readDouble(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue
) {
    const std::string value = trim(ini.getValue(section, key, ""));

    if (value.empty()) {
        return defaultValue;
    }

    double parsed = 0.0;
    if (parseFiniteDouble(value, parsed)) {
        return parsed;
    }
    addWarning("Invalid number: [" + section + "] " + key + "=" + value);
    return defaultValue;
}

double RulesDatabase::readProductionDouble(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue,
    double maximumValue
) {
    return readProductionRuleCoefficient(
        ini, section, key, defaultValue, maximumValue, warnings_);
}

double RulesDatabase::readPercent(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue
) {
    const std::string value = ini.getValue(section, key, "");
    if (trim(value).empty()) {
        return defaultValue;
    }

    bool valid = false;
    const double parsed = parsePercentToken(value, valid);
    if (valid) {
        return parsed;
    }

    addWarning("Invalid percent: [" + section + "] " + key + "=" + value);
    return defaultValue;
}

double RulesDatabase::readRatio(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue
) {
    std::string value = trim(ini.getValue(section, key, ""));
    if (value.empty()) {
        return defaultValue;
    }

    const bool percentage = value.back() == '%';
    if (percentage) {
        value.pop_back();
        value = trim(value);
    }
    try {
        std::size_t parsed = 0;
        const double number = std::stod(value, &parsed);
        if (parsed == value.size() && std::isfinite(number)) {
            return percentage ? number / 100.0 : number;
        }
    } catch (const std::exception&) {
    }

    addWarning(
        "Invalid ratio: [" + section + "] " + key + "="
        + ini.getValue(section, key, "")
    );
    return defaultValue;
}

bool RulesDatabase::readBool(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    bool defaultValue
) {
    const std::string value = ini.getValue(section, key, "");

    if (value.empty()) {
        return defaultValue;
    }

    const std::string normalized = toLower(value);
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        return true;
    }

    if (normalized == "no" || normalized == "false" || normalized == "0") {
        return false;
    }

    addWarning("Invalid boolean: [" + section + "] " + key + "=" + value);
    return defaultValue;
}

std::vector<VeteranAbility> RulesDatabase::readVeteranAbilities(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    std::vector<VeteranAbility> abilities;
    for (const std::string& token : splitList(ini.getValue(section, key, ""))) {
        const std::optional<VeteranAbility> ability =
            parseVeteranAbility(token);
        if (!ability.has_value()) {
            addWarning(
                "Unknown veteran ability: [" + section + "] "
                + key + "=" + token
            );
            continue;
        }
        if (std::find(abilities.begin(), abilities.end(), *ability)
            == abilities.end()) {
            abilities.push_back(*ability);
        }
    }
    return abilities;
}

UnitType RulesDatabase::loadUnitType(
    const IniFile& ini,
    const std::string& id,
    const std::string& objectKind,
    bool defaultCrushable
) {
    UnitType unit;
    unit.pixelSelectionBracketDelta = readInt(ini, id, "PixelSelectionBracketDelta", 0);
    unit.minDebris = std::max(0, readInt(ini, id, "MinDebris", 0));
    unit.maxDebris = std::max(unit.minDebris, readInt(ini, id, "MaxDebris", 0));
    unit.debrisTypes = ini.getValue(id, "DebrisTypes", "");
    unit.debrisMaximums = ini.getValue(id, "DebrisMaximums", "");
    unit.debrisAnims = ini.getValue(id, "DebrisAnims", "");
    unit.bunkerable = readBool(ini, id, "Bunkerable", objectKind == "vehicle");

    unit.id = id;
    unit.uiName = ini.getValue(id, "UIName", "");
    unit.name = ini.getValue(id, "Name", id);
    unit.image = ini.getValue(id, "Image", "");
    unit.ambientSound = trim(ini.getValue(id, "AmbientSound", ""));
    unit.turretRotateSound = trim(ini.getValue(id, "TurretRotateSound", ""));
    unit.auxSound1 = trim(ini.getValue(id, "AuxSound1", ""));
    unit.auxSound2 = trim(ini.getValue(id, "AuxSound2", ""));
    unit.alternateArcticArt = readBool(ini, id, "AlternateArcticArt", false);
    unit.damageParticleSystems = splitList(ini.getValue(id, "DamageParticleSystems", ""));
    const auto damageSmokeOffset = trim(ini.getValue(id, "DamageSmokeOffset", ""));
    if (!damageSmokeOffset.empty()) {
        ParticleVector3 offset;
        if (parseParticleVector3(damageSmokeOffset, offset)
            && std::abs(offset.x) <= 1000000.0 && std::abs(offset.y) <= 1000000.0
            && std::abs(offset.z) <= 1000000.0
            && std::trunc(offset.x) == offset.x && std::trunc(offset.y) == offset.y
            && std::trunc(offset.z) == offset.z) {
            unit.damageSmokeOffset = {static_cast<int>(offset.x), static_cast<int>(offset.y), static_cast<int>(offset.z)};
        } else addWarning("Invalid DamageSmokeOffset: [" + id + "] " + damageSmokeOffset);
    }
    unit.owner = ini.getValue(id, "Owner", "");
    unit.requiredHouses = ini.getValue(id, "RequiredHouses", "");
    unit.forbiddenHouses = ini.getValue(id, "ForbiddenHouses", "");
    unit.techLevel = readInt(ini, id, "TechLevel", 0);
    if (!trim(ini.getValue(id, "BuildLimit", "")).empty()) {
        unit.buildLimit = readInt(ini, id, "BuildLimit", 0);
    }
    unit.buildTimeMultiplier = readProductionDouble(
        ini,
        id,
        "BuildTimeMultiplier",
        1.0,
        maximumProductionCoefficient
    );
    unit.veterancy.trainable = readBool(ini, id, "Trainable", true);
    unit.veterancy.veteranAbilities =
        readVeteranAbilities(ini, id, "VeteranAbilities");
    unit.veterancy.eliteAbilities = unit.veterancy.veteranAbilities;
    for (const VeteranAbility ability :
        readVeteranAbilities(ini, id, "EliteAbilities")) {
        if (std::find(
                unit.veterancy.eliteAbilities.begin(),
                unit.veterancy.eliteAbilities.end(),
                ability
            ) == unit.veterancy.eliteAbilities.end()) {
            unit.veterancy.eliteAbilities.push_back(ability);
        }
    }
    unit.prerequisite = ini.getValue(id, "Prerequisite", "");
    unit.prerequisiteOverride = readPrerequisiteOverride(ini, id);
    unit.dontScore = readBool(ini, id, "DontScore", false);
    unit.insignificant = readBool(ini, id, "Insignificant", false);
    unit.strength = readInt(ini, id, "Strength", 0);
    unit.immune = readBool(ini, id, "Immune", false);
    unit.typeImmune = readBool(ini, id, "TypeImmune", false);
    unit.armor = ini.getValue(id, "Armor", "");
    unit.explodes = readBool(ini, id, "Explodes", false);
    unit.deathWeapon = trim(ini.getValue(id, "DeathWeapon", ""));
    unit.deathWeaponDamageModifier = std::max(
        0.0,
        readDouble(ini, id, "DeathWeaponDamageModifier", 1.0)
    );
    unit.primaryWeapon = ini.getValue(id, "Primary", "");
    unit.secondaryWeapon = ini.getValue(id, "Secondary", "");
    unit.elitePrimaryWeapon = ini.getValue(id, "ElitePrimary", "");
    unit.eliteSecondaryWeapon = ini.getValue(id, "EliteSecondary", "");
    unit.turretCount = std::max(0, readInt(ini, id, "TurretCount", 0));
    unit.weaponCount = std::clamp(
        readInt(ini, id, "WeaponCount", 0),
        0,
        64
    );
    unit.indexedWeapons.reserve(static_cast<std::size_t>(unit.weaponCount));
    unit.eliteIndexedWeapons.reserve(
        static_cast<std::size_t>(unit.weaponCount)
    );
    for (int weaponIndex = 1;
        weaponIndex <= unit.weaponCount;
        ++weaponIndex) {
        const std::string suffix = std::to_string(weaponIndex);
        unit.indexedWeapons.push_back(
            ini.getValue(id, "Weapon" + suffix, "")
        );
        unit.eliteIndexedWeapons.push_back(
            ini.getValue(id, "EliteWeapon" + suffix, "")
        );
    }
    unit.chargeTurret = readBool(ini, id, "IsChargeTurret", false);
    unit.gattling = readBool(ini, id, "IsGattling", false);
    unit.weaponStages = std::clamp(
        readInt(ini, id, "WeaponStages", 0),
        0,
        16
    );
    unit.weaponStageThresholds.reserve(
        static_cast<std::size_t>(unit.weaponStages)
    );
    unit.eliteWeaponStageThresholds.reserve(
        static_cast<std::size_t>(unit.weaponStages)
    );
    for (int stageIndex = 1;
        stageIndex <= unit.weaponStages;
        ++stageIndex) {
        const std::string suffix = std::to_string(stageIndex);
        unit.weaponStageThresholds.push_back(
            std::max(0, readInt(ini, id, "Stage" + suffix, 0))
        );
        unit.eliteWeaponStageThresholds.push_back(
            std::max(0, readInt(ini, id, "EliteStage" + suffix, 0))
        );
    }
    unit.weaponRateUp = std::max(0, readInt(ini, id, "RateUp", 0));
    unit.weaponRateDown = std::max(0, readInt(ini, id, "RateDown", 0));
    unit.speed = readInt(ini, id, "Speed", 0);
    unit.accelerates = readBool(ini, id, "Accelerates", true);
    unit.accelerationFactor = std::clamp(readRatio(ini, id, "AccelerationFactor", 0.03), 0.0, 10000.0);
    unit.hoverAcceleration = std::clamp(readRatio(ini, "General", "HoverAcceleration", 0.02), 0.0, 10000.0);
    unit.threatPosed = std::max(0, readInt(ini, id, "ThreatPosed", 0));
    unit.threatAvoidanceCoefficient = std::max(0.0, readDouble(ini, id, "ThreatAvoidanceCoefficient", 0.0));
    unit.walkRate = std::max(1, readInt(ini, id, "WalkRate", 1));
    unit.idleRate = std::max(0, readInt(ini, id, "IdleRate", 0));
    unit.weight = std::max(0.0, readDouble(ini, id, "Weight", 1.0));
    unit.speedType = ini.getValue(id, "SpeedType", "");
    unit.movementZone = ini.getValue(id, "MovementZone", "");
    unit.locomotor = ini.getValue(id, "Locomotor", "");
    unit.isTilter = readBool(ini, id, "IsTilter", true);
    unit.jumpJet = readBool(ini, id, "JumpJet", false);
    const auto jumpjetNumber = [&](const char* key, const char* global, double fallback) {
        const double value = readDouble(ini, id, key,
            readDouble(ini, "JumpjetControls", global, fallback));
        return std::isfinite(value) ? std::clamp(value, 0.0, 10000.0) : fallback;
    };
    unit.jumpjetSpeed = jumpjetNumber("JumpjetSpeed", "Speed", 14.0);
    unit.jumpjetClimb = jumpjetNumber("JumpjetClimb", "Climb", 5.0);
    unit.jumpjetAccel = jumpjetNumber("JumpjetAccel", "Acceleration", 2.0);
    unit.jumpjetTurnRate = std::clamp(readInt(ini, id, "JumpjetTurnRate",
        readInt(ini, "JumpjetControls", "TurnRate", 4)), 0, 128);
    unit.jumpjetWobbles = jumpjetNumber("JumpjetWobbles", "WobblesPerSecond", 0.15);
    unit.jumpjetDeviation = std::clamp(readInt(ini, id, "JumpjetDeviation",
        readInt(ini, "JumpjetControls", "WobbleDeviation", 40)), 0, 4096);
    unit.jumpjetNoWobbles = readBool(ini, id, "JumpjetNoWobbles", false);
    unit.crashable = readBool(ini, id, "Crashable", false);
    unit.tiltCrashJumpjet = readBool(ini, id, "TiltCrashJumpjet", false);
    unit.jumpjetCrash = std::max(0.01, readDouble(ini, id, "JumpjetCrash", readDouble(ini, "JumpjetControls", "Crash", 5.0)));
    unit.crashGravity = std::max(0.01, readDouble(ini, "AudioVisual", "Gravity", 6.0));
    unit.shipSinkingWeight = std::max(0.0, readDouble(ini, "General", "ShipSinkingWeight", 3.0));
    unit.crashingSound = ini.getValue(id, "CrashingSound", "");
    unit.sinkingSound = ini.getValue(id, "SinkingSound", ini.getValue("AudioVisual", "SinkingSound", ""));
    unit.impactLandSound = ini.getValue(id, "ImpactLandSound", ini.getValue("AudioVisual", "ImpactLandSound", ""));
    unit.impactWaterSound = ini.getValue(id, "ImpactWaterSound", ini.getValue("AudioVisual", "ImpactWaterSound", ""));
    unit.consideredAircraft = readBool(ini, id, "ConsideredAircraft", false);
    unit.balloonHover = readBool(ini, id, "BalloonHover", false);
    const bool jumpjetLocomotor = unit.jumpJet
        || toLower(trim(unit.locomotor)) == "{92612c46-f71f-11d1-ac9f-006008055bb5}";
    unit.jumpJetHeight = std::clamp(readInt(ini, id, "JumpjetHeight", jumpjetLocomotor
        ? readInt(ini, "JumpjetControls", "CruiseHeight", 500) : 0), 0, 32768);
    unit.sight = readInt(ini, id, "Sight", 0);
    unit.cloakable = readBool(ini, id, "Cloakable", false);
    unit.cloakingSpeed = std::clamp(
        readInt(ini, id, "CloakingSpeed", 7),
        1,
        10
    );
    unit.sensors = readBool(ini, id, "Sensors", false);
    unit.sensorArray = readBool(ini, id, "SensorArray", false);
    unit.sensorsSight = std::max(0, readInt(ini, id, "SensorsSight", 0));
    unit.category = ini.getValue(id, "Category", "");
    unit.navalTargeting = readInt(ini, id, "NavalTargeting", 0);
    unit.landTargeting = readInt(ini, id, "LandTargeting", 0);
    unit.guardRange = std::max(0, readInt(ini, id, "GuardRange", 0));
    unit.airRangeBonus = readDouble(ini, id, "AirRangeBonus", 0.0);
    unit.spawns = ini.getValue(id, "Spawns", "");
    unit.spawnsNumber = std::max(0, readInt(ini, id, "SpawnsNumber", 0));
    unit.spawnRegenRate = std::max(0, readInt(ini, id, "SpawnRegenRate", 0));
    unit.spawnReloadRate = std::max(0, readInt(ini, id, "SpawnReloadRate", 0));
    unit.spawned = readBool(ini, id, "Spawned", false);
    unit.missileSpawn = readBool(ini, id, "MissileSpawn", false);
    unit.noSpawnAlt = readBool(ini, id, "NoSpawnAlt", false);
    unit.noShadow = readBool(ini, id, "NoShadow", false);
    unit.underwater = readBool(ini, id, "Underwater", false);
    // TechnoTypeClass treats InfantryTypes as organic unless explicitly
    // overridden.  Vehicles and aircraft retain the stock false default.
    unit.organic = readBool(ini, id, "Organic", objectKind == "infantry");
    unit.fearless = readBool(ini, id, "Fearless", false);
    unit.fraidycat = readBool(ini, id, "Fraidycat", false);
    unit.notHuman = readBool(ini, id, "NotHuman", false);
    unit.crusher = readBool(ini, id, "Crusher", false);
    unit.crushable = readBool(ini, id, "Crushable", defaultCrushable);
    unit.omniCrusher = readBool(ini, id, "OmniCrusher", false);
    unit.omniCrushResistant =
        readBool(ini, id, "OmniCrushResistant", false);
    unit.repairable = readBool(ini, id, "Repairable", true);
    unit.selfHealing = readBool(ini, id, "SelfHealing", false);
    unit.tiberiumProof = readBool(ini, id, "TiberiumProof", false);
    unit.tiberiumHeal = readBool(ini, id, "TiberiumHeal", false);
    unit.crateGoodie = readBool(ini, id, "CrateGoodie", false);
    unit.engineer = readBool(ini, id, "Engineer", false);
    unit.bridgeRepair = readBool(ini, id, "BridgeRepair", false);
    unit.turret = readBool(ini, id, "Turret", false);
    unit.turretSpins = readBool(ini, id, "TurretSpins", false);
    unit.passengers = readInt(ini, id, "Passengers", 0);
    unit.size = readInt(ini, id, "Size", 0);
    unit.sizeLimit = readInt(ini, id, "SizeLimit", 0);
    unit.deploysInto = ini.getValue(id, "DeploysInto", "");
    unit.undeploysInto = ini.getValue(id, "UndeploysInto", "");
    unit.enslaves = ini.getValue(id, "Enslaves", "");
    unit.deployer = readBool(ini, id, "Deployer", false);
    unit.deployFire = readBool(ini, id, "DeployFire", false);
    unit.deployToFire = readBool(ini, id, "DeployToFire", false);
    unit.isSimpleDeployer = readBool(ini, id, "IsSimpleDeployer", false);
    unit.unloadingClass = ini.getValue(id, "UnloadingClass", "");
    unit.deployTime = readDouble(ini, id, "DeployTime", 0.0);
    unit.undeployDelay = readInt(ini, id, "UndeployDelay", -1);
    unit.deployedCrushable =
        readBool(ini, id, "DeployedCrushable", unit.crushable);
    unit.deployToLand = readBool(ini, id, "DeployToLand", false);
    unit.deployFacing = readInt(ini, id, "DeployFacing", -1);
    unit.deployingAnim = ini.getValue(id, "DeployingAnim", "");
    unit.noMovingFire = readBool(ini, id, "NoMovingFire", false);
    unit.occupier = readBool(ini, id, "Occupier", false);
    unit.occupyWeapon = ini.getValue(id, "OccupyWeapon", "");
    unit.eliteOccupyWeapon = ini.getValue(id, "EliteOccupyWeapon", "");
    unit.openTransportWeapon = readInt(ini, id, "OpenTransportWeapon", -1);
    unit.ifvMode = readInt(ini, id, "IFVMode", -1);
    for (std::size_t modeIndex = 0;
        modeIndex < ifvModePrefixes.size();
        ++modeIndex) {
        const std::string prefix = ifvModePrefixes[modeIndex];
        unit.ifvTurretIndices[modeIndex] =
            readInt(ini, id, prefix + "TurretIndex", -1);
        unit.ifvWeaponIndices[modeIndex] =
            readInt(ini, id, prefix + "TurretWeapon", -1);
    }
    unit.gunner = readBool(ini, id, "Gunner", false);
    unit.openTopped = readBool(ini, id, "OpenTopped", false);
    unit.opportunityFire = readBool(ini, id, "OpportunityFire", false);
    unit.preventAttackMove = readBool(ini, id, "PreventAttackMove", false);
    unit.distributedFire = readBool(ini, id, "DistributedFire", false);
    unit.radialFireSegments = std::clamp(readInt(ini, id, "RadialFireSegments", 0), 0, 256);
    unit.pip = ini.getValue(id, "Pip", "");
    unit.pipScale = ini.getValue(id, "PipScale", "");
    unit.occupyPip = ini.getValue(id, "OccupyPip", "");
    unit.canPassiveAquire = readBool(ini, id, "CanPassiveAquire", true);
    if (ini.getValue(id, "CanPassiveAquire", "").empty()) {
        unit.canPassiveAquire =
            readBool(ini, id, "CanPassiveAcquire", unit.canPassiveAquire);
    }
    unit.canRetaliate = readBool(ini, id, "CanRetaliate", true);
    unit.airportBound = readBool(ini, id, "AirportBound", false);
    unit.poweredUnit = readBool(ini, id, "PoweredUnit", false);
    unit.dock = ini.getValue(id, "Dock", "");
    unit.waterBound = readBool(ini, id, "WaterBound", false);
    unit.naval = readBool(ini, id, "Naval", false);
    unit.harvester = readBool(ini, id, "Harvester", false);
    unit.teleporter = readBool(ini, id, "Teleporter", false);
    unit.moveToShroud = readBool(ini, id, "MoveToShroud", objectKind != "aircraft");
    unit.agent = readBool(ini, id, "Agent", false);
    unit.infiltrate = readBool(ini, id, "Infiltrate", false);
    unit.requiresStolenAlliedTech = readBool(ini, id, "RequiresStolenAlliedTech", false);
    unit.requiresStolenSovietTech = readBool(ini, id, "RequiresStolenSovietTech", false);
    unit.requiresStolenThirdTech = readBool(ini, id, "RequiresStolenThirdTech", false);
    unit.c4 = readBool(ini, id, "C4", false);
    unit.ivan = readBool(ini, id, "Ivan", false);
    unit.bombable = readBool(ini, id, "Bombable", true);
    unit.bombSight = std::max(0, readInt(ini, id, "BombSight", 0));
    unit.parasiteable = readBool(ini, id, "Parasiteable", true);
    unit.suppressionThreshold = std::max(
        0,
        readInt(ini, id, "SuppressionThreshold", 0)
    );
    unit.reselectIfLimboed = readBool(
        ini,
        id,
        "ReselectIfLimboed",
        false
    );
    unit.rejoinTeamIfLimboed = readBool(
        ini,
        id,
        "RejoinTeamIfLimboed",
        false
    );
    unit.berserkFriendly = readBool(ini, id, "BerserkFriendly", false);
    unit.disguiseWhenStill = readBool(ini, id, "DisguiseWhenStill", false);
    unit.canDisguise = readBool(ini, id, "CanDisguise", false);
    unit.permaDisguise = readBool(ini, id, "PermaDisguise", false);
    unit.detectDisguise = readBool(ini, id, "DetectDisguise", false);
    unit.detectDisguiseRange = std::max(
        0,
        readInt(ini, id, "DetectDisguiseRange", 0)
    );
    unit.airstrikeTeam = std::max(0, readInt(ini, id, "AirstrikeTeam", 0));
    unit.eliteAirstrikeTeam = std::max(
        0,
        readInt(ini, id, "EliteAirstrikeTeam", 0)
    );
    unit.airstrikeTeamType = trim(
        ini.getValue(id, "AirstrikeTeamType", "")
    );
    unit.eliteAirstrikeTeamType = trim(
        ini.getValue(id, "EliteAirstrikeTeamType", "")
    );
    unit.airstrikeRechargeTime = std::max(
        0,
        readInt(ini, id, "AirstrikeRechargeTime", 0)
    );
    unit.eliteAirstrikeRechargeTime = std::max(
        0,
        readInt(ini, id, "EliteAirstrikeRechargeTime", 0)
    );
    unit.immuneToPsionics = readBool(ini, id, "ImmuneToPsionics", false);
    unit.mindControlRingOffset = readInt(ini, id, "MindControlRingOffset", 140);
    unit.immuneToRadiation = readBool(ini, id, "ImmuneToRadiation", false);
    unit.immuneToPoison = readBool(ini, id, "ImmuneToPoison", false);
    unit.immuneToTemporal = readBool(
        ini,
        id,
        "ImmuneToTemporal",
        readBool(ini, id, "TemporalImmune", false)
    );
    unit.immuneToElectric = readBool(
        ini,
        id,
        "ImmuneToElectric",
        readBool(ini, id, "ElectricImmune", false)
    );
    unit.resourceGatherer = readBool(ini, id, "ResourceGatherer", false);
    unit.resourceDestination = readBool(ini, id, "ResourceDestination", false);
    unit.slaved = readBool(ini, id, "Slaved", false);
    unit.allowedToStartInMultiplayer =
        readBool(ini, id, "AllowedToStartInMultiplayer", true);
    unit.storage = readInt(ini, id, "Storage", 0);
    unit.slavesNumber = readInt(ini, id, "SlavesNumber", 0);
    unit.slaveRegenRate = readInt(ini, id, "SlaveRegenRate", 0);
    unit.slaveReloadRate = readInt(ini, id, "SlaveReloadRate", 0);
    unit.harvestRate = readInt(ini, id, "HarvestRate", 0);
    unit.ammo = std::max(-1, readInt(ini, id, "Ammo", -1));
    unit.initialAmmo = std::max(
        -1,
        readInt(ini, id, "InitialAmmo", -1)
    );
    unit.manualReload = readBool(
        ini,
        id,
        "ManualReload",
        objectKind == "aircraft"
    );
    unit.rot = readInt(ini, id, "ROT", 0);
    unit.soylent = readInt(ini, id, "Soylent", 0);
    unit.cost = readInt(ini, id, "Cost", 0);
    unit.crewed = readBool(ini, id, "Crewed", false);
    unit.carriesCrate = readBool(ini, id, "CarriesCrate", false);
    unit.isTrain = readBool(ini, id, "IsTrain", false);
    unit.isSelectableCombatant = readBool(ini, id, "IsSelectableCombatant", false);
    unit.voiceSelect = ini.getValue(id, "VoiceSelect", "");
    unit.voiceMove = ini.getValue(id, "VoiceMove", "");
    unit.voiceAttack = ini.getValue(id, "VoiceAttack", "");
    unit.voiceFeedback = ini.getValue(id, "VoiceFeedback", "");
    unit.voiceSpecialAttack = ini.getValue(id, "VoiceSpecialAttack", "");
    unit.voiceSecondaryWeaponAttack =
        ini.getValue(id, "VoiceSecondaryWeaponAttack", "");
    unit.voiceCrashing = ini.getValue(id, "VoiceCrashing", "");
    unit.voiceSelectDeactivated =
        ini.getValue(id, "VoiceSelectDeactivated", "");
    unit.voiceSelectEnslaved =
        ini.getValue(id, "VoiceSelectEnslaved", "");
    unit.voiceEnter = ini.getValue(id, "VoiceEnter", "");
    unit.voiceCapture = ini.getValue(id, "VoiceCapture", "");
    unit.voiceHarvest = ini.getValue(id, "VoiceHarvest", "");
    unit.moveSound = ini.getValue(id, "MoveSound", "");
    unit.createSound = ini.getValue(id, "CreateSound", "");
    unit.dieSound = ini.getValue(id, "DieSound", "");
    unit.crushSound = ini.getValue(id, "CrushSound", "");
    unit.enterTransportSound = ini.getValue(id, "EnterTransportSound", "");
    unit.leaveTransportSound = ini.getValue(id, "LeaveTransportSound", "");
    unit.enterWaterSound = ini.getValue(id, "EnterWaterSound", "");
    unit.leaveWaterSound = ini.getValue(id, "LeaveWaterSound", "");
    unit.chronoInSound = ini.getValue(id, "ChronoInSound", "");
    unit.chronoOutSound = ini.getValue(id, "ChronoOutSound", "");
    unit.activateSound = ini.getValue(id, "ActivateSound", "");
    unit.deactivateSound = ini.getValue(id, "DeactivateSound", "");
    unit.voiceDeploy = ini.getValue(id, "VoiceDeploy", "");
    unit.deploySound = ini.getValue(id, "DeploySound", "");
    unit.undeploySound = ini.getValue(id, "UndeploySound", "");
    unit.explosion = ini.getValue(id, "Explosion", "");
    assignDeployWeapons(
        ini,
        id,
        unit.deployFire,
        unit.secondaryWeapon,
        unit.eliteSecondaryWeapon,
        unit.deployWeapon,
        unit.eliteDeployWeapon,
        unit.deployWeaponSource,
        unit.eliteDeployWeaponSource
    );
    unit.capability = unitCapabilityForObject(unit, objectKind);
    unit.weaponSet = weaponSetForObject(
        unit.primaryWeapon,
        unit.secondaryWeapon,
        unit.elitePrimaryWeapon,
        unit.eliteSecondaryWeapon,
        unit.deployWeapon,
        unit.eliteDeployWeapon,
        unit.deployWeaponSource,
        unit.eliteDeployWeaponSource,
        unit.occupyWeapon,
        unit.eliteOccupyWeapon,
        unit.openTransportWeapon
    );
    unit.weaponSet.indexedWeaponCount = unit.weaponCount;
    unit.weaponSet.indexedWeapons = unit.indexedWeapons;
    unit.weaponSet.eliteIndexedWeapons = unit.eliteIndexedWeapons;
    unit.deployProfile = deployProfileForObject(
        unit.deploysInto,
        unit.undeploysInto,
        unit.deployWeapon,
        unit.eliteDeployWeapon,
        unit.deployWeaponSource,
        unit.eliteDeployWeaponSource,
        unit.deployer,
        unit.deployFire,
        unit.deployToFire,
        unit.isSimpleDeployer,
        unit.unloadingClass,
        unit.deployTime,
        unit.undeployDelay,
        unit.deployedCrushable,
        unit.deployToLand,
        unit.deployFacing,
        unit.deployingAnim,
        unit.noMovingFire,
        unit.voiceDeploy,
        unit.deploySound,
        unit.undeploySound
    );
    unit.occupyProfile = occupyProfileForObject(
        unit.occupier,
        false,
        false,
        false,
        0,
        unit.occupyWeapon,
        unit.eliteOccupyWeapon,
        unit.occupyPip
    );
    unit.engineerProfile = engineerProfileForObject(
        unit.engineer,
        unit.bridgeRepair,
        unit.repairable,
        unit.repairable,
        false,
        true,
        false,
        false
    );
    unit.transportProfile = transportProfileForObject(
        unit.passengers,
        unit.size,
        unit.openTransportWeapon,
        unit.unloadingClass,
        unit.airportBound,
        unit.dock
    );
    unit.uiProfile = uiProfileForObject(
        unit.pip,
        unit.pipScale,
        unit.occupyPip,
        false,
        unit.strength,
        0,
        false
    );

    return unit;
}

BuildingType RulesDatabase::loadBuildingType(const IniFile& ini, const std::string& id) {
    BuildingType building;

    building.id = id;
    building.uiName = ini.getValue(id, "UIName", "");
    building.name = ini.getValue(id, "Name", id);
    building.image = ini.getValue(id, "Image", "");
    building.ambientSound = trim(ini.getValue(id, "AmbientSound", ""));
    building.turretRotateSound = trim(ini.getValue(id, "TurretRotateSound", ""));
    building.alternateArcticArt = readBool(ini, id, "AlternateArcticArt", false);
    building.owner = ini.getValue(id, "Owner", "");
    building.requiredHouses = ini.getValue(id, "RequiredHouses", "");
    building.forbiddenHouses = ini.getValue(id, "ForbiddenHouses", "");
    building.techLevel = readInt(ini, id, "TechLevel", 0);
    if (!trim(ini.getValue(id, "BuildLimit", "")).empty()) {
        building.buildLimit = readInt(ini, id, "BuildLimit", 0);
    }
    building.buildTimeMultiplier = readProductionDouble(
        ini,
        id,
        "BuildTimeMultiplier",
        1.0,
        maximumProductionCoefficient
    );
    building.veterancy.trainable = readBool(ini, id, "Trainable", false);
    building.veterancy.veteranAbilities =
        readVeteranAbilities(ini, id, "VeteranAbilities");
    building.veterancy.eliteAbilities = building.veterancy.veteranAbilities;
    for (const VeteranAbility ability :
        readVeteranAbilities(ini, id, "EliteAbilities")) {
        if (std::find(
                building.veterancy.eliteAbilities.begin(),
                building.veterancy.eliteAbilities.end(),
                ability
            ) == building.veterancy.eliteAbilities.end()) {
            building.veterancy.eliteAbilities.push_back(ability);
        }
    }
    building.prerequisite = ini.getValue(id, "Prerequisite", "");
    building.prerequisiteOverride = readPrerequisiteOverride(ini, id);
    building.dontScore = readBool(ini, id, "DontScore", false);
    building.insignificant = readBool(ini, id, "Insignificant", false);
    building.strength = readInt(ini, id, "Strength", 0);
    building.threatPosed = std::max(0, readInt(ini, id, "ThreatPosed", 0));
    building.noShadow = readBool(ini, id, "NoShadow", false);
    building.immune = readBool(ini, id, "Immune", false);
    building.typeImmune = readBool(ini, id, "TypeImmune", false);
    building.immuneToPsionics = readBool(
        ini,
        id,
        "ImmuneToPsionics",
        true
    );
    building.immuneToRadiation = readBool(
        ini,
        id,
        "ImmuneToRadiation",
        false
    );
    building.immuneToPoison = readBool(ini, id, "ImmuneToPoison", false);
    building.immuneToTemporal = readBool(
        ini,
        id,
        "ImmuneToTemporal",
        readBool(ini, id, "TemporalImmune", false)
    );
    building.immuneToElectric = readBool(
        ini,
        id,
        "ImmuneToElectric",
        readBool(ini, id, "ElectricImmune", false)
    );
    building.armor = ini.getValue(id, "Armor", "");
    building.explodes = readBool(ini, id, "Explodes", false);
    building.deathWeapon = trim(ini.getValue(id, "DeathWeapon", ""));
    building.deathWeaponDamageModifier = std::max(
        0.0,
        readDouble(ini, id, "DeathWeaponDamageModifier", 1.0)
    );
    building.primaryWeapon = ini.getValue(id, "Primary", "");
    building.secondaryWeapon = ini.getValue(id, "Secondary", "");
    building.elitePrimaryWeapon = ini.getValue(id, "ElitePrimary", "");
    building.eliteSecondaryWeapon = ini.getValue(id, "EliteSecondary", "");
    building.turretCount = std::max(0, readInt(ini, id, "TurretCount", 0));
    building.weaponCount = std::clamp(
        readInt(ini, id, "WeaponCount", 0),
        0,
        64
    );
    building.indexedWeapons.reserve(
        static_cast<std::size_t>(building.weaponCount)
    );
    building.eliteIndexedWeapons.reserve(
        static_cast<std::size_t>(building.weaponCount)
    );
    for (int weaponIndex = 1;
        weaponIndex <= building.weaponCount;
        ++weaponIndex) {
        const std::string suffix = std::to_string(weaponIndex);
        building.indexedWeapons.push_back(
            ini.getValue(id, "Weapon" + suffix, "")
        );
        building.eliteIndexedWeapons.push_back(
            ini.getValue(id, "EliteWeapon" + suffix, "")
        );
    }
    building.chargeTurret = readBool(ini, id, "IsChargeTurret", false);
    building.gattling = readBool(ini, id, "IsGattling", false);
    building.weaponStages = std::clamp(
        readInt(ini, id, "WeaponStages", 0),
        0,
        16
    );
    building.weaponStageThresholds.reserve(
        static_cast<std::size_t>(building.weaponStages)
    );
    building.eliteWeaponStageThresholds.reserve(
        static_cast<std::size_t>(building.weaponStages)
    );
    for (int stageIndex = 1;
        stageIndex <= building.weaponStages;
        ++stageIndex) {
        const std::string suffix = std::to_string(stageIndex);
        building.weaponStageThresholds.push_back(
            std::max(0, readInt(ini, id, "Stage" + suffix, 0))
        );
        building.eliteWeaponStageThresholds.push_back(
            std::max(0, readInt(ini, id, "EliteStage" + suffix, 0))
        );
    }
    building.weaponRateUp =
        std::max(0, readInt(ini, id, "RateUp", 0));
    building.weaponRateDown =
        std::max(0, readInt(ini, id, "RateDown", 0));
    building.rot = std::max(0, readInt(ini, id, "ROT", 0));
    building.power = readInt(ini, id, "Power", 0);
    building.powered = readBool(ini, id, "Powered", false);
    building.drainable = readBool(ini, id, "Drainable", false);
    building.poweredSpecial = readBool(ini, id, "PoweredSpecial", false);
    building.overpowerable = readBool(ini, id, "Overpowerable", false);
    building.navalTargeting = readInt(ini, id, "NavalTargeting", 0);
    building.landTargeting = readInt(ini, id, "LandTargeting", 0);
    building.guardRange =
        std::max(0, readInt(ini, id, "GuardRange", 0));
    building.airRangeBonus = readDouble(ini, id, "AirRangeBonus", 0.0);
    building.sight = std::max(0, readInt(ini, id, "Sight", 0));
    building.organic = readBool(ini, id, "Organic", false);
    building.cloakable = readBool(ini, id, "Cloakable", false);
    building.cloakingSpeed = std::clamp(
        readInt(ini, id, "CloakingSpeed", 7),
        1,
        10
    );
    building.sensors = readBool(ini, id, "Sensors", false);
    building.sensorArray = readBool(ini, id, "SensorArray", false);
    building.sensorsSight = std::max(
        0,
        readInt(ini, id, "SensorsSight", 0)
    );
    building.buildCat = ini.getValue(id, "BuildCat", "");
    building.adjacent = std::max(0, readInt(ini, id, "Adjacent", 1));
    building.baseNormal = readBool(ini, id, "BaseNormal", true);
    // Westwood's INI key intentionally contains the extra 'i'.
    building.eligibleForAllyBuilding =
        readBool(ini, id, "EligibileForAllyBuilding", false);
    building.isBaseDefense = readBool(ini, id, "IsBaseDefense", false);
    building.aiBasePlanningSide =
        readInt(ini, id, "AIBasePlanningSide", -1);
    building.powersUnit = ini.getValue(id, "PowersUnit", "");
    building.factory = ini.getValue(id, "Factory", "");
    building.freeUnit = ini.getValue(id, "FreeUnit", "");
    building.deploysInto = ini.getValue(id, "DeploysInto", "");
    building.undeploysInto = ini.getValue(id, "UndeploysInto", "");
    building.enslaves = ini.getValue(id, "Enslaves", "");
    building.deployer = readBool(ini, id, "Deployer", false);
    building.deployFire = readBool(ini, id, "DeployFire", false);
    building.deployToFire = readBool(ini, id, "DeployToFire", false);
    building.isSimpleDeployer =
        readBool(ini, id, "IsSimpleDeployer", false);
    building.unloadingClass = ini.getValue(id, "UnloadingClass", "");
    building.deployTime = readDouble(ini, id, "DeployTime", 0.0);
    building.deployFacing = readInt(ini, id, "DeployFacing", -1);
    building.noMovingFire = readBool(ini, id, "NoMovingFire", false);
    building.refinery = readBool(ini, id, "Refinery", false);
    building.dockUnload = readBool(ini, id, "DockUnload", false);
    building.numberOfDocks = std::max(
        0,
        readInt(ini, id, "NumberOfDocks", 0)
    );
    if (building.numberOfDocks > 4096) {
        warnings_.push_back(
            "[" + id + "] NumberOfDocks exceeds the supported limit; "
            "clamped to 4096"
        );
        building.numberOfDocks = 4096;
    }
    building.resourceGatherer =
        readBool(ini, id, "ResourceGatherer", false);
    building.resourceDestination =
        readBool(ini, id, "ResourceDestination", false);
    building.wall = readBool(ini, id, "Wall", false);
    building.gate = readBool(ini, id, "Gate", false);
    building.gateCloseDelay = std::max(
        0.0,
        readDouble(ini, id, "GateCloseDelay", 0.0)
    );
    building.crusher = readBool(ini, id, "Crusher", false);
    building.crushable = readBool(ini, id, "Crushable", false);
    building.omniCrusher = readBool(ini, id, "OmniCrusher", false);
    building.omniCrushResistant =
        readBool(ini, id, "OmniCrushResistant", false);
    building.repairable = readBool(ini, id, "Repairable", true);
    building.selfHealing = readBool(ini, id, "SelfHealing", false);
    building.clickRepairable =
        readBool(ini, id, "ClickRepairable", building.repairable);
    building.psychicDetectionRadius = std::clamp(
        readInt(ini, id, "PsychicDetectionRadius", 0), 0, 256);
    building.unsellable = readBool(ini, id, "Unsellable", false);
    building.capturable = readBool(ini, id, "Capturable", true);
    building.spyable = readBool(ini, id, "Spyable", false);
    building.canC4 = readBool(ini, id, "CanC4", true);
    building.bombable = readBool(ini, id, "Bombable", true);
    building.bombSight = std::max(0, readInt(ini, id, "BombSight", 0));
    building.detectDisguise = readBool(
        ini,
        id,
        "DetectDisguise",
        false
    );
    building.detectDisguiseRange = std::max(
        0,
        readInt(ini, id, "DetectDisguiseRange", 0)
    );
    building.waterBound = readBool(ini, id, "WaterBound", false);
    building.naval = readBool(ini, id, "Naval", false);
    building.needsEngineer = readBool(ini, id, "NeedsEngineer", false);
    building.captureEvaEvent = trim(ini.getValue(id, "CaptureEvaEvent", ""));
    building.bridgeRepairHut =
        readBool(ini, id, "BridgeRepairHut", false)
        || readBool(ini, id, "BridgeRepair", false);
    building.canBeOccupied = readBool(ini, id, "CanBeOccupied", false);
    building.canOccupyFire = readBool(ini, id, "CanOccupyFire", false);
    building.distributedFire = readBool(ini, id, "DistributedFire", false);
    building.maxNumberOccupants = readInt(ini, id, "MaxNumberOccupants", 0);
    building.pip = ini.getValue(id, "Pip", "");
    building.pipScale = ini.getValue(id, "PipScale", "");
    building.pipsDrawForAll = readBool(ini, id, "PipsDrawForAll", false);
    building.explosion = ini.getValue(id, "Explosion", "");
    building.destroyAnim = ini.getValue(id, "DestroyAnim", "");
    building.leaveRubble = readBool(ini, id, "LeaveRubble", false);
    building.invisibleInGame = readBool(ini, id, "InvisibleInGame", false);
    if (building.invisibleInGame) building.immune = true;
    building.lightVisibility = std::max(0, readInt(ini, id, "LightVisibility", 5000));
    const auto lightLevel = [&](const char* key, double fallback) {
        const std::string text = ini.getValue(id, key, "");
        char* end = nullptr;
        double value = text.empty() ? fallback : std::strtod(text.c_str(), &end);
        if ((!text.empty() && end == text.c_str()) || !std::isfinite(value)) value = fallback;
        // Retail reads the numeric prefix (stock BLUELAMP contains "0,01"),
        // multiplies by 1000, adds 0.1 and truncates toward zero.
        return static_cast<int>(std::clamp(value * 1000.0 + 0.1, -1000000.0, 1000000.0));
    };
    building.lightIntensity = lightLevel("LightIntensity", 0.0);
    building.lightRedTint = lightLevel("LightRedTint", 1.0);
    building.lightGreenTint = lightLevel("LightGreenTint", 1.0);
    building.lightBlueTint = lightLevel("LightBlueTint", 1.0);
    building.minDebris = std::max(0, readInt(ini, id, "MinDebris", 0));
    // Retail 0x7125A1..0x7125D9: clamp MinDebris to zero, raise MaxDebris to MinDebris.
    building.maxDebris = std::max(building.minDebris, readInt(ini, id, "MaxDebris", 0));
    building.debrisAnims = ini.getValue(id, "DebrisAnims", "");
    building.debrisTypes = ini.getValue(id, "DebrisTypes", "");
    building.debrisMaximums = ini.getValue(id, "DebrisMaximums", "");
    building.constructionYard = readBool(ini, id, "ConstructionYard", false);
    building.weaponsFactory = readBool(ini, id, "WeaponsFactory", false);
    building.factoryPlant = readBool(ini, id, "FactoryPlant", false);
    building.infantryCostBonus = readDouble(
        ini,
        id,
        "InfantryCostBonus",
        1.0
    );
    building.unitsCostBonus = readDouble(ini, id, "UnitsCostBonus", 1.0);
    building.aircraftCostBonus = readDouble(
        ini,
        id,
        "AircraftCostBonus",
        1.0
    );
    building.buildingsCostBonus = readDouble(
        ini,
        id,
        "BuildingsCostBonus",
        1.0
    );
    building.defensesCostBonus = readDouble(
        ini,
        id,
        "DefensesCostBonus",
        1.0
    );
    building.unitRepair = readBool(ini, id, "UnitRepair", false);
    building.unitReload = readBool(ini, id, "UnitReload", false);
    building.unitSell = readBool(ini, id, "UnitSell", false);
    building.numberImpassableRows = std::max(
        0,
        readInt(ini, id, "NumberImpassableRows", 0)
    );
    if (building.unitRepair && building.numberOfDocks == 0) {
        building.numberOfDocks = 1;
    }
    building.hospital = readBool(ini, id, "Hospital", false);
    building.armory = readBool(ini, id, "Armory", false);
    building.ammo = readInt(ini, id, "Ammo", -1);
    building.cloning = readBool(ini, id, "Cloning", false);
    building.infantryGainSelfHeal = std::max(
        0,
        readInt(ini, id, "InfantryGainSelfHeal", 0)
    );
    building.unitsGainSelfHeal = std::max(
        0,
        readInt(ini, id, "UnitsGainSelfHeal", 0)
    );
    building.gapGenerator = readBool(ini, id, "GapGenerator", false);
    building.gapRadiusInCells = std::clamp(
        readInt(ini, id, "GapRadiusInCells", 0),
        0,
        512
    );
    building.superGapRadiusInCells = std::clamp(
        readInt(ini, id, "SuperGapRadiusInCells", 0),
        0,
        512
    );
    building.secretLab = readBool(ini, id, "SecretLab", false);
    building.secretInfantry = mergeUniqueIniLists(
        ini,
        id,
        {"SecretInfantry", "SecretInfantries"}
    );
    building.secretUnits = mergeUniqueIniLists(
        ini,
        id,
        {"SecretUnit", "SecretUnits"}
    );
    building.secretBuildings = mergeUniqueIniLists(
        ini,
        id,
        {"SecretBuilding", "SecretBuildings"}
    );
    building.produceCashStartup = std::max(
        0,
        readInt(ini, id, "ProduceCashStartup", 0)
    );
    building.produceCashAmount = std::max(
        0,
        readInt(ini, id, "ProduceCashAmount", 0)
    );
    building.produceCashDelay = std::max(
        0,
        readInt(ini, id, "ProduceCashDelay", 0)
    );
    building.grinding = readBool(ini, id, "Grinding", false);
    building.bunker = readBool(ini, id, "Bunker", false);
    building.infantryAbsorb = readBool(ini, id, "InfantryAbsorb", false);
    building.passengers = std::max(0, readInt(ini, id, "Passengers", 0));
    building.sizeLimit = std::max(0, readInt(ini, id, "SizeLimit", 0));
    building.extraPower = readInt(ini, id, "ExtraPower", 0);
    building.orePurifier = readBool(ini, id, "OrePurifier", false);
    building.spySat = readBool(ini, id, "SpySat", false);
    building.radar = readBool(ini, id, "Radar", false);
    building.turret = readBool(ini, id, "Turret", false);
    building.turretAnim = ini.getValue(id, "TurretAnim", "");
    building.turretAnimIsVoxel =
        readBool(ini, id, "TurretAnimIsVoxel", false);
    building.turretAnimX = readInt(ini, id, "TurretAnimX", 0);
    building.turretAnimY = readInt(ini, id, "TurretAnimY", 0);
    building.turretAnimZAdjust = readInt(ini, id, "TurretAnimZAdjust", 0);
    building.turretRecoil = readBool(ini, id, "TurretRecoil", false);
    building.turretTravel = std::max(
        0, readInt(ini, id, "TurretTravel", 0)
    );
    building.barrelTravel = std::max(
        0, readInt(ini, id, "BarrelTravel", 0)
    );
    building.barrelCompressFrames = std::max(
        0, readInt(ini, id, "BarrelCompressFrames", 0)
    );
    building.barrelHoldFrames = std::max(
        0, readInt(ini, id, "BarrelHoldFrames", 0)
    );
    building.barrelRecoverFrames = std::max(
        0, readInt(ini, id, "BarrelRecoverFrames", 0)
    );
    building.chargedAnimTime = std::max(0.0, readDouble(ini, id, "ChargedAnimTime", 0.0));
    building.superWeapon = ini.getValue(id, "SuperWeapon", "");
    building.superWeapon2 = ini.getValue(id, "SuperWeapon2", "");
    building.extraSuperWeapons = splitList(ini.getValue(id, "SuperWeapons", ""));
    building.canPassiveAquire = readBool(ini, id, "CanPassiveAquire", true);
    if (ini.getValue(id, "CanPassiveAquire", "").empty()) {
        building.canPassiveAquire =
            readBool(ini, id, "CanPassiveAcquire", building.canPassiveAquire);
    }
    building.canRetaliate = readBool(ini, id, "CanRetaliate", true);
    building.storage = readInt(ini, id, "Storage", 0);
    building.slavesNumber = readInt(ini, id, "SlavesNumber", 0);
    building.slaveRegenRate = readInt(ini, id, "SlaveRegenRate", 0);
    building.slaveReloadRate = readInt(ini, id, "SlaveReloadRate", 0);
    building.harvestRate = readInt(ini, id, "HarvestRate", 0);
    building.soylent = readInt(ini, id, "Soylent", 0);
    building.cost = readInt(ini, id, "Cost", 0);
    building.crewed = readBool(ini, id, "Crewed", false);
    building.crateBeneath = readBool(ini, id, "CrateBeneath", false);
    building.crateBeneathIsMoney = readBool(ini, id, "CrateBeneathIsMoney", false);
    building.revealToAll = readBool(ini, id, "RevealToAll", false);
    building.createUnitSound = ini.getValue(id, "CreateUnitSound", "");
    building.damageSound = ini.getValue(id, "DamageSound", "");
    building.dieSound = ini.getValue(id, "DieSound", "");
    building.crushSound = ini.getValue(id, "CrushSound", "");
    building.workingSound = ini.getValue(id, "WorkingSound", "");
    building.notWorkingSound = ini.getValue(id, "NotWorkingSound", "");
    building.refinerySmoke = loadRefinerySmokeProfile(ini, id);
    assignDeployWeapons(
        ini,
        id,
        building.deployFire,
        building.secondaryWeapon,
        building.eliteSecondaryWeapon,
        building.deployWeapon,
        building.eliteDeployWeapon,
        building.deployWeaponSource,
        building.eliteDeployWeaponSource
    );
    building.capability = buildingObjectCapability(building);
    building.weaponSet = weaponSetForObject(
        building.primaryWeapon,
        building.secondaryWeapon,
        building.elitePrimaryWeapon,
        building.eliteSecondaryWeapon,
        building.deployWeapon,
        building.eliteDeployWeapon,
        building.deployWeaponSource,
        building.eliteDeployWeaponSource,
        "",
        "",
        -1
    );
    building.weaponSet.indexedWeaponCount = building.weaponCount;
    building.weaponSet.indexedWeapons = building.indexedWeapons;
    building.weaponSet.eliteIndexedWeapons =
        building.eliteIndexedWeapons;
    building.deployProfile = deployProfileForObject(
        building.deploysInto,
        building.undeploysInto,
        building.deployWeapon,
        building.eliteDeployWeapon,
        building.deployWeaponSource,
        building.eliteDeployWeaponSource,
        building.deployer,
        building.deployFire,
        building.deployToFire,
        building.isSimpleDeployer,
        building.unloadingClass,
        building.deployTime,
        -1,
        building.crushable,
        false,
        building.deployFacing,
        "",
        building.noMovingFire,
        "",
        "",
        ""
    );
    building.occupyProfile = occupyProfileForObject(
        false,
        building.canBeOccupied,
        building.canOccupyFire,
        building.distributedFire,
        building.maxNumberOccupants,
        "",
        "",
        ""
    );
    building.engineerProfile = engineerProfileForObject(
        false,
        false,
        building.repairable,
        building.clickRepairable,
        building.unsellable,
        building.capturable,
        building.needsEngineer,
        building.bridgeRepairHut
    );
    building.transportProfile = transportProfileForObject(
        building.infantryAbsorb ? building.passengers : 0,
        0,
        -1,
        building.unloadingClass,
        false,
        ""
    );
    building.buildingCapability = buildingCapabilityForObject(building);
    building.uiProfile = uiProfileForObject(
        building.pip,
        building.pipScale,
        "",
        building.pipsDrawForAll,
        building.strength,
        building.power,
        building.powered
    );

    return building;
}

WeaponType RulesDatabase::loadWeaponType(const IniFile& ini, const std::string& id) {
    WeaponType weapon;

    weapon.id = id;
    weapon.damage = readInt(ini, id, "Damage", 0);
    weapon.rof = readInt(ini, id, "ROF", 0);
    weapon.range = readDouble(ini, id, "Range", 0.0);
    weapon.minimumRange = readDouble(ini, id, "MinimumRange", 0.0);
    weapon.projectile = ini.getValue(id, "Projectile", "");
    weapon.speed = readInt(ini, id, "Speed", 0);
    weapon.warhead = ini.getValue(id, "Warhead", "");
    weapon.burst = readInt(ini, id, "Burst", 0);
    weapon.burstDelay = std::clamp(readInt(ini, id, "BurstDelay", 3), 1, 36000);
    weapon.burstDelaySpecified = !ini.getValue(id, "BurstDelay", "").empty();
    weapon.burstDelays = parseIntegerList(ini.getValue(id, "BurstDelays", ""));
    if (weapon.burstDelays.size() > 256) weapon.burstDelays.resize(256);
    for (int& delay : weapon.burstDelays) delay = std::clamp(delay, 1, 36000);
    weapon.turboBoost = readBool(ini, id, "TurboBoost", false);
    weapon.report = ini.getValue(id, "Report", "");
    weapon.anim = ini.getValue(id, "Anim", "");
    weapon.occupantAnim = ini.getValue(id, "OccupantAnim", "");
    weapon.openToppedAnim = ini.getValue(id, "OpenToppedAnim", "");
    weapon.charges = readBool(ini, id, "Charges", false);
    weapon.bright = readBool(ini, id, "Bright", false);
    weapon.decloakToFire = readBool(ini, id, "DecloakToFire", true);
    weapon.revealOnFire = readBool(ini, id, "RevealOnFire", true);
    weapon.cellRangefinding = readBool(ini, id, "CellRangefinding", false);
    weapon.migAttackCursor = readBool(ini, id, "MigAttackCursor", false);
    weapon.sabotageCursor = readBool(ini, id, "SabotageCursor", false);
    weapon.neverUse = readBool(ini, id, "NeverUse", false);
    weapon.suicide = readBool(ini, id, "Suicide", false);
    weapon.fireInTransport = readBool(ini, id, "FireInTransport", true);
    weapon.laser = readBool(ini, id, "IsLaser", false);
    weapon.houseColorLaser = readBool(ini, id, "IsHouseColor", false);
    weapon.diskLaser = readBool(ini, id, "DiskLaser", false);
    weapon.drainWeapon = readBool(ini, id, "DrainWeapon", false);
    weapon.laserInnerColor = ini.getValue(id, "LaserInnerColor", "");
    weapon.laserOuterColor = ini.getValue(id, "LaserOuterColor", "");
    weapon.laserOuterSpread = ini.getValue(id, "LaserOuterSpread", "");
    weapon.laserDuration =
        std::max(0, readInt(ini, id, "LaserDuration", 0));
    weapon.laserThickness = std::clamp(
        readInt(ini, id, "LaserThickness", 1),
        1,
        32
    );
    weapon.sonic = readBool(ini, id, "IsSonic", false);
    weapon.ambientDamage = readInt(ini, id, "AmbientDamage", 0);
    weapon.electricBolt = readBool(ini, id, "IsElectricBolt", false);
    weapon.alternateElectricColor =
        readBool(ini, id, "IsAlternateColor", false);
    weapon.radBeam = readBool(ini, id, "IsRadBeam", false);
    weapon.magBeam = readBool(ini, id, "IsMagBeam", false);
    weapon.isRailgun = readBool(ini, id, "IsRailgun", false);
    weapon.useSparkParticles = readBool(
        ini,
        id,
        "UseSparkParticles",
        false
    );
    weapon.useFireParticles = readBool(
        ini,
        id,
        "UseFireParticles",
        false
    );
    weapon.attachedParticleSystem = trim(
        ini.getValue(id, "AttachedParticleSystem", "")
    );
    weapon.spawner = readBool(ini, id, "Spawner", false);
    weapon.limboLaunch = readBool(ini, id, "LimboLaunch", false);
    weapon.infiniteMindControl = readBool(
        ini,
        id,
        "InfiniteMindControl",
        false
    );
    weapon.fireOnce = readBool(ini, id, "FireOnce", false);
    weapon.permanentMindControl = readBool(
        ini,
        id,
        "PermanentMindControl",
        readBool(ini, id, "PermaControl", false)
    );
    weapon.disguiseFakeBlinkTime = std::max(
        0,
        readInt(ini, id, "DisguiseFakeBlinkTime", 0)
    );
    weapon.disguiseFireOnly = readBool(
        ini,
        id,
        "DisguiseFireOnly",
        false
    );
    weapon.areaFire = readBool(ini, id, "AreaFire", false);
    weapon.isRadEruption = readBool(ini, id, "IsRadEruption", false);
    weapon.radLevel = std::max(0, readInt(ini, id, "RadLevel", 0));
    WeaponLifecycleProfile lifecycle;
    lifecycle.id = id;
    lifecycle.omniFire = readBool(ini, id, "OmniFire", false);
    lifecycle.fireOnce = weapon.fireOnce;
    lifecycle.distributedFire = readBool(ini, id, "DistributedFire", false);
    weapon.lifecycle = lifecycle;
    cacheWeaponLifecycleProfile(this, std::move(lifecycle));
    weapon.profile = weaponProfileForObject(weapon);

    return weapon;
}

ProjectileType RulesDatabase::loadProjectileType(const IniFile& ini, const std::string& id) {
    ProjectileType projectile;

    projectile.id = id;
    projectile.image = ini.getValue(id, "Image", "");
    projectile.palette = ini.getValue(id, "Palette", "");
    projectile.color = ini.getValue(id, "Color", "");
    projectile.paletteIsSet = !projectile.palette.empty();
    projectile.imageIsNone = isNoneValue(projectile.image);
    projectile.arcing = readBool(ini, id, "Arcing", false);
    projectile.floater = readBool(ini, id, "Floater", false);
    projectile.rot = readInt(ini, id, "ROT", 0);
    projectile.aa = readBool(ini, id, "AA", false);
    projectile.agExplicit = !ini.getValue(id, "AG", "").empty();
    projectile.ag = readBool(ini, id, "AG", true);
    projectile.an = readBool(ini, id, "AN", false);
    projectile.asw = readBool(ini, id, "ASW", false);
    projectile.inviso = readBool(ini, id, "Inviso", false);
    projectile.animates = readBool(ini, id, "Animates", false);
    projectile.inaccurate = readBool(ini, id, "Inaccurate", false);
    projectile.flakScatter = readBool(ini, id, "FlakScatter", false);
    projectile.subjectToCliffs = readBool(ini, id, "SubjectToCliffs", false);
    projectile.subjectToElevation =
        readBool(ini, id, "SubjectToElevation", false);
    projectile.subjectToWalls = readBool(ini, id, "SubjectToWalls", false);
    projectile.high = readBool(ini, id, "High", false);
    projectile.veryHigh = readBool(ini, id, "VeryHigh", false);
    projectile.shadow = readBool(
        ini,
        id,
        "Shadow",
        true
    );
    projectile.vertical = readBool(ini, id, "Vertical", false);
    projectile.airburst = readBool(ini, id, "Airburst", false);
    projectile.proximity = readBool(ini, id, "Proximity", false);
    projectile.ranged = readBool(ini, id, "Ranged", false);
    projectile.dropping = readBool(ini, id, "Dropping", false);
    projectile.level = readBool(ini, id, "Level", false);
    projectile.courseLockDuration = std::max(0, readInt(ini, id, "CourseLockDuration", 0));
    projectile.firersPalette = readBool(ini, id, "FirersPalette", false);
    projectile.animPalette = readBool(ini, id, "AnimPalette", false);
    projectile.frames = readInt(ini, id, "Frames", 0);
    projectile.translucency = readInt(ini, id, "Translucency", 0);
    projectile.cluster = readInt(ini, id, "Cluster", 0);
    projectile.acceleration = readInt(ini, id, "Acceleration", 3);
    projectile.trailer = ini.getValue(id, "Trailer", "");
    projectile.trailerSeperation = readInt(ini, id, "TrailerSeperation", 0);
    projectile.airburstWeapon = ini.getValue(id, "AirburstWeapon", "");
    projectile.shrapnelWeapon = ini.getValue(id, "ShrapnelWeapon", "");
    projectile.shrapnelCount =
        readInt(ini, id, "ShrapnelCount", 0);
    projectile.profile = projectileProfileForObject(projectile);

    return projectile;
}

OverlayType RulesDatabase::loadOverlayType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    OverlayType overlay;

    overlay.id = id;
    overlay.listIndex = listIndex;
    overlay.name = ini.getValue(id, "Name", id);
    overlay.image = ini.getValue(id, "Image", id);
    overlay.theater = readBool(ini, id, "Theater", false);
    overlay.newTheater = readBool(ini, id, "NewTheater", false)
        || overlay.theater;
    overlay.palette = ini.getValue(id, "Palette", "");
    overlay.altPalette = ini.getValue(id, "AltPalette", "");
    overlay.toOverlay = ini.getValue(id, "ToOverlay", "");
    overlay.foundation = ini.getValue(id, "Foundation", "");
    overlay.tiberium = readBool(ini, id, "Tiberium", false);
    overlay.gem = startsWithIgnoreCase(id, "GEM")
        || containsIgnoreCase(overlay.name, "gem");
    overlay.ore = overlay.tiberium && !overlay.gem;
    overlay.crate = readBool(ini, id, "Crate", false);
    overlay.crateTrigger = readBool(ini, id, "CrateTrigger", false);
    overlay.legalTarget = readBool(ini, id, "LegalTarget", false);
    overlay.radarInvisible = readBool(ini, id, "RadarInvisible", false);
    overlay.radarColor = ini.getValue(id, "RadarColor", "");
    overlay.noUseTileLandType =
        readBool(ini, id, "NoUseTileLandType", false);
    overlay.drawFlat = readBool(ini, id, "DrawFlat", false);
    overlay.wall = readBool(ini, id, "Wall", false);
    overlay.isARock = readBool(ini, id, "IsARock", false);
    overlay.zAdjust = readInt(ini, id, "ZAdjust", 0);
    overlay.shadowIndex = readInt(ini, id, "ShadowIndex", -1);

    return overlay;
}

SmudgeType RulesDatabase::loadSmudgeType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    SmudgeType smudge;
    smudge.id = id;
    smudge.listIndex = listIndex;
    smudge.crater = readBool(ini, id, "Crater", false);
    smudge.burn = readBool(ini, id, "Burn", false);
    smudge.width = std::max(1, readInt(ini, id, "Width", 1));
    smudge.height = std::max(1, readInt(ini, id, "Height", 1));
    return smudge;
}

TiberiumType RulesDatabase::loadTiberiumType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    TiberiumType tiberium;

    tiberium.id = id;
    tiberium.listIndex = listIndex;
    tiberium.name = ini.getValue(id, "Name", id);
    tiberium.image = readInt(ini, id, "Image", 0);
    tiberium.value = readInt(ini, id, "Value", 0);
    tiberium.growth = readInt(ini, id, "Growth", 0);
    tiberium.growthPercentage =
        readDouble(ini, id, "GrowthPercentage", 0.0);
    tiberium.spread = readInt(ini, id, "Spread", 0);
    tiberium.spreadPercentage =
        readDouble(ini, id, "SpreadPercentage", 0.0);
    tiberium.power = readInt(ini, id, "Power", 0);
    tiberium.color = ini.getValue(id, "Color", "");

    return tiberium;
}

TerrainType RulesDatabase::loadTerrainType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    TerrainType terrain;

    terrain.id = id;
    terrain.listIndex = listIndex;
    terrain.name = ini.getValue(id, "Name", id);
    terrain.ambientSound = trim(ini.getValue(id, "AmbientSound", ""));
    terrain.spawnsTiberium = readBool(ini, id, "SpawnsTiberium", false);
    terrain.isAnimated = readBool(ini, id, "IsAnimated", false);
    terrain.animationRate = readInt(ini, id, "AnimationRate", 0);
    terrain.animationProbability =
        readDouble(ini, id, "AnimationProbability", 0.0);
    terrain.strength = readInt(ini, id, "Strength", 0);
    terrain.immune = readBool(ini, id, "Immune", false);
    terrain.radarColor = ini.getValue(id, "RadarColor", "");

    return terrain;
}

LandType RulesDatabase::loadLandType(
    const IniFile& ini,
    const std::string& id
) {
    LandType land;
    land.id = id;
    land.foot = readPercent(ini, id, "Foot", land.foot);
    land.track = readPercent(ini, id, "Track", land.track);
    land.wheel = readPercent(ini, id, "Wheel", land.wheel);
    land.floatSpeed = readPercent(ini, id, "Float", land.floatSpeed);
    land.floatBeach = readPercent(ini, id, "FloatBeach", land.floatBeach);
    land.hover = readPercent(ini, id, "Hover", land.hover);
    land.amphibious = readPercent(ini, id, "Amphibious", land.amphibious);
    land.buildable = readBool(ini, id, "Buildable", land.buildable);
    return land;
}

ParticleSystemType RulesDatabase::loadParticleSystemType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    ParticleSystemType system;
    system.id = id;
    system.listIndex = listIndex;
    system.holdsWhat = trim(ini.getValue(id, "HoldsWhat", ""));
    system.behaviorName = trim(ini.getValue(id, "BehavesLike", ""));
    system.behavior = parseParticleBehavior(system.behaviorName);
    if (system.behavior == ParticleBehavior::Unknown) {
        addWarning(
            "Particle system [" + id + "] has unknown BehavesLike value ["
            + system.behaviorName + "]"
        );
    }
    system.image = trim(ini.getValue(id, "Image", ""));
    system.spawns = readBool(ini, id, "Spawns", false);
    system.spawnFrames = std::max(1, readInt(ini, id, "SpawnFrames", 1));
    system.slowdown = std::max(0.0, readDouble(ini, id, "Slowdown", 0.0));
    system.particleCap = std::max(0, readInt(ini, id, "ParticleCap", 500));
    system.spawnRadius = std::max(0, readInt(ini, id, "SpawnRadius", 0));
    system.spawnCutoff = std::max(
        0.0,
        readDouble(ini, id, "SpawnCutoff", 0.0)
    );
    system.spawnTranslucencyCutoff = std::max(
        0.0,
        readDouble(ini, id, "SpawnTranslucencyCutoff", 0.0)
    );
    system.lifetime = readInt(ini, id, "Lifetime", -1);
    if (system.lifetime < -1) {
        system.lifetime = -1;
    }
    const std::string spawnDirection = trim(
        ini.getValue(id, "SpawnDirection", "")
    );
    if (!spawnDirection.empty()
        && !parseParticleVector3(spawnDirection, system.spawnDirection)) {
        addWarning(
            "Particle system [" + id
            + "] has invalid SpawnDirection [" + spawnDirection + "]"
        );
    }
    system.particlesPerCoord = std::max(
        0.0,
        readDouble(ini, id, "ParticlesPerCoord", 0.1)
    );
    system.spiralDeltaPerCoord = readDouble(
        ini,
        id,
        "SpiralDeltaPerCoord",
        0.025
    );
    system.spiralRadius = std::max(
        0.0,
        readDouble(ini, id, "SpiralRadius", 2.890625)
    );
    system.positionPerturbationCoefficient = std::max(
        0.0,
        readDouble(ini, id, "PositionPerturbationCoefficient", 0.0)
    );
    system.movementPerturbationCoefficient = std::max(
        0.0,
        readDouble(ini, id, "MovementPerturbationCoefficient", 0.0)
    );
    system.velocityPerturbationCoefficient = std::max(
        0.0,
        readDouble(ini, id, "VelocityPerturbationCoefficient", 0.0)
    );
    system.spawnSparkPercentage = std::clamp(
        readDouble(ini, id, "SpawnSparkPercentage", 0.0),
        0.0,
        1.0
    );
    system.sparkSpawnFrames = std::max(
        0,
        readInt(ini, id, "SparkSpawnFrames", 0)
    );
    system.lightSize = std::max(0, readInt(ini, id, "LightSize", 0));
    const std::string laserColor = trim(ini.getValue(id, "LaserColor", ""));
    if (!laserColor.empty()) {
        system.hasLaserColor = parseParticleColor(
            laserColor,
            system.laserColor
        );
        if (!system.hasLaserColor) {
            addWarning(
                "Particle system [" + id
                + "] has invalid LaserColor [" + laserColor + "]"
            );
        }
    }
    system.laser = readBool(ini, id, "Laser", false);
    system.oneFrameLight = readBool(ini, id, "OneFrameLight", false);
    return system;
}

ParticleType RulesDatabase::loadParticleType(
    const IniFile& ini,
    const std::string& id,
    int listIndex
) {
    ParticleType particle;
    particle.id = id;
    particle.listIndex = listIndex;
    particle.image = trim(ini.getValue(id, "Image", ""));
    particle.behaviorName = trim(ini.getValue(id, "BehavesLike", ""));
    particle.behavior = parseParticleBehavior(particle.behaviorName);
    if (particle.behavior == ParticleBehavior::Unknown) {
        addWarning(
            "Particle [" + id + "] has unknown BehavesLike value ["
            + particle.behaviorName + "]"
        );
    }
    const std::string nextOffset = trim(
        ini.getValue(id, "NextParticleOffset", "")
    );
    if (!nextOffset.empty()
        && !parseParticleVector3(nextOffset, particle.nextParticleOffset)) {
        addWarning(
            "Particle [" + id + "] has invalid NextParticleOffset ["
            + nextOffset + "]"
        );
    }
    particle.xVelocity = readInt(ini, id, "XVelocity", 0);
    particle.yVelocity = readInt(ini, id, "YVelocity", 0);
    particle.minZVelocity = readInt(ini, id, "MinZVelocity", 0);
    particle.zVelocityRange = std::max(
        0,
        readInt(ini, id, "ZVelocityRange", 0)
    );
    particle.colorSpeed = std::clamp(
        readDouble(ini, id, "ColorSpeed", 0.0),
        0.0,
        1.0
    );
    const std::string colorList = trim(ini.getValue(id, "ColorList", ""));
    if (!colorList.empty()) {
        bool colorsValid = true;
        particle.colorList = parseParticleColorList(colorList, colorsValid);
        if (!colorsValid) {
            addWarning(
                "Particle [" + id + "] has invalid ColorList ["
                + colorList + "]"
            );
        }
    }
    const std::string startColor1 = trim(
        ini.getValue(id, "StartColor1", "")
    );
    if (!startColor1.empty()) {
        particle.hasStartColor1 = parseParticleColor(
            startColor1,
            particle.startColor1
        );
        if (!particle.hasStartColor1) {
            addWarning(
                "Particle [" + id + "] has invalid StartColor1 ["
                + startColor1 + "]"
            );
        }
    }
    const std::string startColor2 = trim(
        ini.getValue(id, "StartColor2", "")
    );
    if (!startColor2.empty()) {
        particle.hasStartColor2 = parseParticleColor(
            startColor2,
            particle.startColor2
        );
        if (!particle.hasStartColor2) {
            addWarning(
                "Particle [" + id + "] has invalid StartColor2 ["
                + startColor2 + "]"
            );
        }
    }
    particle.maxDamageDelay = std::max(0, readInt(ini, id, "MaxDC", 0));
    particle.maxLifetime = std::max(0, readInt(ini, id, "MaxEC", 0));
    particle.warhead = trim(ini.getValue(id, "Warhead", ""));
    particle.damage = readInt(ini, id, "Damage", 0);
    particle.startFrame = std::max(0, readInt(ini, id, "StartFrame", 0));
    particle.numLoopFramesSpecified = !trim(ini.getValue(id, "NumLoopFrames", "")).empty();
    particle.numLoopFrames = std::max(
        1,
        readInt(ini, id, "NumLoopFrames", 1)
    );
    particle.translucency = std::clamp(
        readInt(ini, id, "Translucency", 0),
        0,
        100
    );
    particle.windEffect = std::clamp(
        readInt(ini, id, "WindEffect", 0),
        0,
        10
    );
    particle.velocity = readDouble(ini, id, "Velocity", 0.0);
    particle.deacceleration = std::max(
        0.0,
        readDouble(ini, id, "Deacc", 0.0)
    );
    particle.radius = std::max(0, readInt(ini, id, "Radius", 0));
    particle.deleteOnStateLimit = readBool(
        ini,
        id,
        "DeleteOnStateLimit",
        false
    );
    particle.endStateAI = std::clamp(
        readInt(ini, id, "EndStateAI", 0),
        0,
        255
    );
    particle.startStateAI = std::clamp(
        readInt(ini, id, "StartStateAI", 0),
        0,
        255
    );
    particle.stateAIAdvance = std::clamp(
        readInt(ini, id, "StateAIAdvance", 0),
        0,
        255
    );
    particle.finalDamageState = std::clamp(
        readInt(ini, id, "FinalDamageState", 0),
        0,
        255
    );
    particle.translucent25State = std::clamp(
        readInt(ini, id, "Translucent25State", 0),
        0,
        255
    );
    particle.translucent50State = std::clamp(
        readInt(ini, id, "Translucent50State", 0),
        0,
        255
    );
    particle.normalized = readBool(ini, id, "Normalized", false);
    particle.nextParticle = trim(ini.getValue(id, "NextParticle", ""));
    particle.report = trim(ini.getValue(id, "Report", ""));
    return particle;
}

WarheadType RulesDatabase::loadWarheadType(const IniFile& ini, const std::string& id) {
    WarheadType warhead;

    warhead.id = id;
    warhead.attachEffect = loadAttachEffect(ini, id, true);
    warhead.shieldPenetrate = readBool(ini, id, "Shield.Penetrate", false);
    warhead.cellSpread = readDouble(ini, id, "CellSpread", 0.0);
    warhead.percentAtMax = readDouble(ini, id, "PercentAtMax", 0.0);
    warhead.verses = ini.getValue(id, "Verses", "");
    warhead.animList = ini.getValue(id, "AnimList", "");
    warhead.explosion = ini.getValue(id, "Explosion", "");
    warhead.particle = ini.getValue(id, "Particle", "");
    warhead.particleSystem = ini.getValue(id, "ParticleSystem", "");
    warhead.infDeath = readInt(ini, id, "InfDeath", 0);
    warhead.proneDamage = ini.getValue(id, "ProneDamage", "");
    warhead.proneDamageMultiplier = std::max(
        0.0,
        readRatio(ini, id, "ProneDamage", 1.0)
    );
    warhead.combatLightSize = readRatio(
        ini,
        id,
        "CombatLightSize",
        0.0
    );
    warhead.clDisableRed = readBool(ini, id, "CLDisableRed", false);
    warhead.clDisableGreen = readBool(ini, id, "CLDisableGreen", false);
    warhead.clDisableBlue = readBool(ini, id, "CLDisableBlue", false);
    warhead.wall = readBool(ini, id, "Wall", false);
    warhead.penetratesBunker = readBool(ini, id, "PenetratesBunker", false);
    warhead.wood = readBool(ini, id, "Wood", false);
    warhead.psychedelic = readBool(ini, id, "Psychedelic", false);
    warhead.poison = readBool(ini, id, "Poison", false);
    // Retail defaults AffectsAllies to yes. Restore that default for authored
    // suicide blasts and poison while retaining the ordinary-weapon policy.
    const std::string warheadKey = canonicalIdKey(id);
    const bool suicideWarhead = std::any_of(
        weaponTypes_.begin(), weaponTypes_.end(), [&warheadKey](const WeaponType& weapon) {
            return weapon.suicide && canonicalIdKey(weapon.warhead) == warheadKey;
        }
    );
    warhead.affectsAllies = readBool(
        ini, id, "AffectsAllies", warhead.psychedelic || suicideWarhead || warhead.poison
    );
    warhead.affectsEnemies = readBool(ini, id, "AffectsEnemies", true);
    warhead.bright = readBool(ini, id, "Bright", false);
    warhead.conventional = readBool(ini, id, "Conventional", false);
    warhead.sparky = readBool(ini, id, "Sparky", true);
    warhead.fire = readBool(ini, id, "Fire", false);
    warhead.tiberium = readBool(ini, id, "Tiberium", false);
    warhead.mindControl = readBool(ini, id, "MindControl", false);
    warhead.temporal = readBool(ini, id, "Temporal", false);
    warhead.radiation = readBool(ini, id, "Radiation", false);
    warhead.electricAssault = readBool(
        ini,
        id,
        "ElectricAssault",
        false
    );
    warhead.psychicDamage = readBool(ini, id, "PsychicDamage", false);
    warhead.makesDisguise = readBool(ini, id, "MakesDisguise", false);
    warhead.rocker = readBool(ini, id, "Rocker", false);
    warhead.parasite = readBool(ini, id, "Parasite", false);
    warhead.culling = readBool(ini, id, "Culling", false);
    warhead.paralyzes = std::max(0, readInt(ini, id, "Paralyzes", 0));
    warhead.ivanBomb = readBool(ini, id, "IvanBomb", false);
    warhead.bombDisarm = readBool(ini, id, "BombDisarm", false);
    warhead.airstrike = readBool(ini, id, "Airstrike", false);
    warhead.isLocomotor = readBool(ini, id, "IsLocomotor", false);
    warhead.locomotor = trim(ini.getValue(id, "Locomotor", ""));
    warhead.cellInset = std::max(0, readInt(ini, id, "CellInset", 0));
    WarheadLifecycleProfile lifecycle;
    lifecycle.id = id;
    lifecycle.shakeXlo = readInt(ini, id, "ShakeXlo", 0);
    lifecycle.shakeXhi = readInt(ini, id, "ShakeXhi", 0);
    lifecycle.shakeYlo = readInt(ini, id, "ShakeYlo", 0);
    lifecycle.shakeYhi = readInt(ini, id, "ShakeYhi", 0);
    warhead.lifecycle = lifecycle;
    cacheWarheadLifecycleProfile(this, std::move(lifecycle));
    warhead.profile = warheadProfileForObject(warhead);

    return warhead;
}

void RulesDatabase::loadUnitList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<UnitType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        addWarning("Missing type list section: [" + typeListSection + "]");
        return;
    }

    output.reserve(output.size() + section->size());
    for (const auto& [index, unitId] : *section) {
        if (ini.getSection(unitId) == nullptr) {
            addWarning(typeListSection + " -> missing unit section [" + unitId + "]");
            continue;
        }

        std::string objectKind = "vehicle";
        if (typeListSection == "InfantryTypes") {
            objectKind = "infantry";
        } else if (typeListSection == "AircraftTypes") {
            objectKind = "aircraft";
        }

        output.push_back(loadUnitType(
            ini,
            unitId,
            objectKind,
            typeListSection == "InfantryTypes"
        ));
    }
}

void RulesDatabase::loadBuildingList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<BuildingType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        addWarning("Missing type list section: [" + typeListSection + "]");
        return;
    }

    for (const auto& [index, buildingId] : *section) {
        if (ini.getSection(buildingId) == nullptr) {
            addWarning(typeListSection + " -> missing building section [" + buildingId + "]");
            continue;
        }

        output.push_back(loadBuildingType(ini, buildingId));
    }
}

void RulesDatabase::loadWarheadList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<WarheadType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        addWarning("Missing type list section: [" + typeListSection + "]");
        return;
    }

    for (const auto& [index, warheadId] : *section) {
        if (ini.getSection(warheadId) == nullptr) {
            addWarning(typeListSection + " -> missing warhead section [" + warheadId + "]");
            continue;
        }

        const std::string warheadKey = canonicalIdKey(warheadId);
        if (warheadIds_.find(warheadKey) != warheadIds_.end()) {
            continue;
        }

        output.push_back(loadWarheadType(ini, warheadId));
        warheadIds_.insert(warheadKey);
    }
}

void RulesDatabase::loadParticleSystemList(const IniFile& ini) {
    const IniFile::Section* section = ini.getSection("ParticleSystems");
    if (section == nullptr) {
        return;
    }
    std::unordered_set<std::string> loadedIds;
    particleSystemTypes_.reserve(section->size());
    for (const auto& [indexText, authoredId] : *section) {
        const std::string id = trim(authoredId);
        if (id.empty()) {
            continue;
        }
        const std::string key = canonicalIdKey(id);
        if (!loadedIds.insert(key).second) {
            addWarning("Duplicate particle system id ignored: [" + id + "]");
            continue;
        }
        const std::string sectionName = findSectionName(ini, id);
        if (sectionName.empty()) {
            addWarning(
                "ParticleSystems -> missing particle system section ["
                + id + "]"
            );
            continue;
        }
        int listIndex = -1;
        try {
            listIndex = std::stoi(trim(indexText));
        } catch (const std::exception&) {
            addWarning(
                "ParticleSystems -> invalid particle system index ["
                + indexText + "]"
            );
        }
        particleSystemTypes_.push_back(
            loadParticleSystemType(ini, sectionName, listIndex)
        );
    }
}

void RulesDatabase::loadParticleList(const IniFile& ini) {
    const IniFile::Section* section = ini.getSection("Particles");
    if (section == nullptr) {
        return;
    }
    std::unordered_set<std::string> loadedIds;
    particleTypes_.reserve(section->size());
    for (const auto& [indexText, authoredId] : *section) {
        const std::string id = trim(authoredId);
        if (id.empty()) {
            continue;
        }
        const std::string key = canonicalIdKey(id);
        if (!loadedIds.insert(key).second) {
            addWarning("Duplicate particle id ignored: [" + id + "]");
            continue;
        }
        const std::string sectionName = findSectionName(ini, id);
        if (sectionName.empty()) {
            addWarning(
                "Particles -> missing particle section [" + id + "]"
            );
            continue;
        }
        int listIndex = -1;
        try {
            listIndex = std::stoi(trim(indexText));
        } catch (const std::exception&) {
            addWarning(
                "Particles -> invalid particle index [" + indexText + "]"
            );
        }
        particleTypes_.push_back(
            loadParticleType(ini, sectionName, listIndex)
        );
    }
}

void RulesDatabase::loadOverlayList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<OverlayType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        return;
    }

    std::unordered_set<std::string> loadedOverlayIds;
    for (const auto& [indexText, overlayId] : *section) {
        if (overlayId.empty()) {
            continue;
        }

        const std::string overlayKey = canonicalIdKey(overlayId);
        if (loadedOverlayIds.find(overlayKey) != loadedOverlayIds.end()) {
            continue;
        }

        int listIndex = -1;
        try {
            listIndex = std::stoi(indexText);
        } catch (const std::exception&) {
            addWarning(typeListSection + " -> invalid overlay index [" + indexText + "]");
        }

        const std::string sectionName = findSectionName(ini, overlayId);
        if (sectionName.empty()) {
            OverlayType overlay;
            overlay.id = overlayId;
            overlay.listIndex = listIndex;
            overlay.name = overlayId;
            overlay.image = overlayId;
            overlay.gem = startsWithIgnoreCase(overlayId, "GEM");
            overlay.ore = startsWithIgnoreCase(overlayId, "TIB");
            output.push_back(std::move(overlay));
            loadedOverlayIds.insert(overlayKey);
            continue;
        }

        OverlayType overlay = loadOverlayType(ini, sectionName, listIndex);
        overlay.id = overlayId;
        output.push_back(std::move(overlay));
        loadedOverlayIds.insert(overlayKey);
    }
}

void RulesDatabase::loadSmudgeList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<SmudgeType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);
    if (section == nullptr) {
        return;
    }

    std::unordered_set<std::string> loadedSmudgeIds;
    for (const auto& [indexText, smudgeId] : *section) {
        if (smudgeId.empty()) {
            continue;
        }

        const std::string smudgeKey = canonicalIdKey(smudgeId);
        if (!loadedSmudgeIds.insert(smudgeKey).second) {
            continue;
        }

        int listIndex = -1;
        try {
            listIndex = std::stoi(trim(indexText));
        } catch (const std::exception&) {
            addWarning(
                typeListSection + " -> invalid smudge index ["
                + indexText + "]"
            );
        }

        const std::string sectionName = findSectionName(ini, smudgeId);
        if (sectionName.empty()) {
            SmudgeType smudge;
            smudge.id = smudgeId;
            smudge.listIndex = listIndex;
            output.push_back(std::move(smudge));
            addWarning(
                typeListSection + " -> missing smudge section ["
                + smudgeId + "]"
            );
            continue;
        }

        SmudgeType smudge = loadSmudgeType(ini, sectionName, listIndex);
        smudge.id = smudgeId;
        output.push_back(std::move(smudge));
    }
}

void RulesDatabase::loadTiberiumList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<TiberiumType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        return;
    }

    std::unordered_set<std::string> loadedTiberiumIds;
    for (const auto& [indexText, tiberiumId] : *section) {
        if (tiberiumId.empty()) {
            continue;
        }

        const std::string tiberiumKey = canonicalIdKey(tiberiumId);
        if (loadedTiberiumIds.find(tiberiumKey) != loadedTiberiumIds.end()) {
            continue;
        }

        int listIndex = -1;
        try {
            listIndex = std::stoi(indexText);
        } catch (const std::exception&) {
            addWarning(typeListSection + " -> invalid tiberium index [" + indexText + "]");
        }

        const std::string sectionName = findSectionName(ini, tiberiumId);
        if (sectionName.empty()) {
            addWarning(typeListSection + " -> missing tiberium section [" + tiberiumId + "]");
            continue;
        }

        output.push_back(loadTiberiumType(ini, sectionName, listIndex));
        output.back().id = tiberiumId;
        loadedTiberiumIds.insert(tiberiumKey);
    }
}

void RulesDatabase::loadTerrainList(
    const IniFile& ini,
    const std::string& typeListSection,
    std::vector<TerrainType>& output
) {
    const IniFile::Section* section = ini.getSection(typeListSection);

    if (section == nullptr) {
        return;
    }

    std::unordered_set<std::string> loadedTerrainIds;
    for (const auto& [indexText, terrainId] : *section) {
        if (terrainId.empty()) {
            continue;
        }

        const std::string terrainKey = canonicalIdKey(terrainId);
        if (loadedTerrainIds.find(terrainKey) != loadedTerrainIds.end()) {
            continue;
        }

        int listIndex = -1;
        try {
            listIndex = std::stoi(indexText);
        } catch (const std::exception&) {
            addWarning(typeListSection + " -> invalid terrain index [" + indexText + "]");
        }

        const std::string sectionName = findSectionName(ini, terrainId);
        if (sectionName.empty()) {
            addWarning(typeListSection + " -> missing terrain section [" + terrainId + "]");
            continue;
        }

        output.push_back(loadTerrainType(ini, sectionName, listIndex));
        output.back().id = terrainId;
        loadedTerrainIds.insert(terrainKey);
    }
}

void RulesDatabase::loadLandTypes(const IniFile& ini) {
    const std::vector<std::string> defaultLandTypes{
        "Clear",
        "Rough",
        "Road",
        "Water",
        "Rock",
        "Wall",
        "Tiberium",
        "Weeds",
        "Beach",
        "Ice",
        "Railroad",
        "Tunnel"
    };

    std::unordered_set<std::string> loadedLandTypes;
    loadedLandTypes.reserve(defaultLandTypes.size());
    for (const std::string& landTypeId : defaultLandTypes) {
        const std::string sectionName = findSectionName(ini, landTypeId);
        if (sectionName.empty()) {
            continue;
        }

        const std::string landTypeKey = canonicalIdKey(sectionName);
        if (loadedLandTypes.find(landTypeKey) != loadedLandTypes.end()) {
            continue;
        }

        landTypes_.push_back(loadLandType(ini, sectionName));
        loadedLandTypes.insert(landTypeKey);
    }
}

void RulesDatabase::loadCountries(const IniFile& ini) {
    const IniFile::Section* section = ini.getSection("Countries");
    if (section == nullptr) {
        return;
    }

    std::unordered_set<std::string> loadedCountryIds;
    loadedCountryIds.reserve(section->size());
    const auto readUniqueIds = [&ini](
        const std::string& countrySection,
        const std::string& key
    ) {
        std::vector<std::string> ids;
        std::unordered_set<std::string> loadedIds;
        for (const std::string& id :
                splitList(ini.getValue(countrySection, key, ""))) {
            const std::string canonical = canonicalIdKey(id);
            if (canonical.empty() || !loadedIds.insert(canonical).second) {
                continue;
            }
            ids.push_back(trim(id));
        }
        return ids;
    };

    for (const auto& [index, rawCountryId] : *section) {
        (void)index;
        const std::string countryId = trim(rawCountryId);
        const std::string countryKey = canonicalIdKey(countryId);
        if (countryKey.empty()
            || !loadedCountryIds.insert(countryKey).second) {
            continue;
        }

        countries_.push_back(countryId);

        CountryDefinition definition;
        definition.id = countryId;
        const std::string countrySection = findSectionName(ini, countryId);
        if (!countrySection.empty()) {
            definition.veteranAircraft = readUniqueIds(
                countrySection,
                "VeteranAircraft"
            );
            definition.veteranInfantry = readUniqueIds(
                countrySection,
                "VeteranInfantry"
            );
            definition.veteranUnits = readUniqueIds(
                countrySection,
                "VeteranUnits"
            );
        }

        countryIndexById_.emplace(
            countryKey,
            countryDefinitions_.size()
        );
        countryDefinitions_.push_back(std::move(definition));
    }
}

void RulesDatabase::loadSides(const IniFile& ini) {
    const IniFile::Section* section = ini.getSection("Sides");
    if (section == nullptr) {
        return;
    }

    for (const auto& [sideName, members] : *section) {
        if (sideName.empty()) {
            continue;
        }

        SideDefinition side;
        side.name = sideName;
        for (const std::string& member : splitList(members)) {
            if (!containsValue(side.members, member)) {
                side.members.push_back(member);
            }
        }

        sides_.push_back(std::move(side));
    }
}

void RulesDatabase::loadPrerequisiteAliases(const IniFile& ini) {
    const auto addAlias = [this, &ini](
        const std::string& name,
        const std::string& key
    ) {
        const std::string value = ini.getValue("General", key, "");
        if (value.empty()) {
            return;
        }

        prerequisiteAliases_.push_back({name, value});
    };

    addAlias("Power", "PrerequisitePower");
    addAlias("Factory", "PrerequisiteFactory");
    addAlias("Barracks", "PrerequisiteBarracks");
    addAlias("Radar", "PrerequisiteRadar");
    addAlias("Tech", "PrerequisiteTech");
    addAlias("Proc", "PrerequisiteProc");
    addAlias("ProcAlternate", "PrerequisiteProcAlternate");
}

void RulesDatabase::rebuildLookupIndexes() {
    rebuildTypeIndex(
        infantryTypes_,
        infantryIndexById_,
        warnings_,
        "infantry"
    );
    rebuildTypeIndex(
        vehicleTypes_,
        vehicleIndexById_,
        warnings_,
        "vehicle"
    );
    rebuildTypeIndex(
        aircraftTypes_,
        aircraftIndexById_,
        warnings_,
        "aircraft"
    );
    rebuildTypeIndex(
        buildingTypes_,
        buildingIndexById_,
        warnings_,
        "building"
    );
    rebuildTypeIndex(
        weaponTypes_,
        weaponIndexById_,
        warnings_,
        "weapon"
    );
    rebuildTypeIndex(
        projectileTypes_,
        projectileIndexById_,
        warnings_,
        "projectile"
    );
    rebuildTypeIndex(
        tiberiumTypes_,
        tiberiumIndexById_,
        warnings_,
        "tiberium"
    );
    rebuildTypeIndex(
        terrainTypes_,
        terrainIndexById_,
        warnings_,
        "terrain"
    );
    rebuildTypeIndex(
        landTypes_,
        landTypeIndexById_,
        warnings_,
        "land type"
    );
    rebuildTypeIndex(
        warheadTypes_,
        warheadIndexById_,
        warnings_,
        "warhead"
    );
    rebuildTypeIndex(
        particleSystemTypes_,
        particleSystemIndexById_,
        warnings_,
        "particle system"
    );
    rebuildTypeIndex(
        particleTypes_,
        particleIndexById_,
        warnings_,
        "particle"
    );
}

bool RulesDatabase::isEmptyWeaponId(const std::string& weaponId) {
    return trim(weaponId).empty()
        || isNoneValue(weaponId);
}

bool RulesDatabase::isEmptyProjectileId(const std::string& projectileId) {
    return trim(projectileId).empty()
        || isNoneValue(projectileId);
}

bool RulesDatabase::isEmptyWarheadId(const std::string& warheadId) {
    return trim(warheadId).empty()
        || isNoneValue(warheadId);
}

void RulesDatabase::addWeaponCandidate(
    const IniFile& ini,
    const std::string& weaponId,
    const std::string& source,
    std::unordered_set<std::string>& loadedWeaponIds
) {
    if (isEmptyWeaponId(weaponId)) {
        return;
    }

    const std::string weaponKey = canonicalIdKey(weaponId);
    if (loadedWeaponIds.find(weaponKey) != loadedWeaponIds.end()) {
        return;
    }

    const std::string sectionName = findSectionName(ini, weaponId);
    if (sectionName.empty()) {
        addWarning("Weapon referenced by " + source + " but missing section [" + weaponId + "]");
        return;
    }

    const std::string sectionKey = canonicalIdKey(sectionName);
    if (loadedWeaponIds.find(sectionKey) != loadedWeaponIds.end()) {
        return;
    }

    weaponTypes_.push_back(loadWeaponType(ini, sectionName));
    loadedWeaponIds.insert(sectionKey);
}

void RulesDatabase::discoverWeaponsFromObjects(const IniFile& ini) {
    std::unordered_set<std::string> loadedWeaponIds;

    // The V3 terminal weapon is invoked by spawned-missile code and therefore
    // has no Primary/Secondary owner in stock rules. Seed virtual terminal
    // weapons here so their projectile and Airburst child weapon enter the
    // same recursive dependency discovery as ordinary authored weapons.
    for (const SpawnedMissileProfile& profile : spawnedMissileProfiles_) {
        addWeaponCandidate(
            ini,
            profile.terminalWeapon,
            "spawned missile [" + profile.typeId + "] terminal weapon",
            loadedWeaponIds
        );
    }

    if (ini.getSection("CombatDamage") != nullptr
        || !findSectionName(
            ini,
            specialMechanismRules_.defaultDeathWeapon
        ).empty()) {
        addWeaponCandidate(
            ini,
            specialMechanismRules_.defaultDeathWeapon,
            "[CombatDamage] DeathWeapon",
            loadedWeaponIds
        );
    }

    for (const UnitType& unit : infantryTypes_) {
        const std::string source = "unit [" + unit.id + "]";

        addWeaponCandidate(ini, unit.primaryWeapon, source + " Primary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.secondaryWeapon, source + " Secondary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.elitePrimaryWeapon, source + " ElitePrimary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteSecondaryWeapon, source + " EliteSecondary", loadedWeaponIds);
        addWeaponCandidate(
            ini,
            unit.deathWeapon,
            source + " DeathWeapon",
            loadedWeaponIds
        );
        for (std::size_t weaponIndex = 0;
            weaponIndex < unit.indexedWeapons.size();
            ++weaponIndex) {
            const std::string suffix = std::to_string(weaponIndex + 1U);
            addWeaponCandidate(
                ini,
                unit.indexedWeapons[weaponIndex],
                source + " Weapon" + suffix,
                loadedWeaponIds
            );
            if (weaponIndex < unit.eliteIndexedWeapons.size()) {
                addWeaponCandidate(
                    ini,
                    unit.eliteIndexedWeapons[weaponIndex],
                    source + " EliteWeapon" + suffix,
                    loadedWeaponIds
                );
            }
        }
        addWeaponCandidate(ini, unit.deployWeapon, source + " "
            + (unit.deployWeaponSource.empty()
                ? "DeployWeapon"
                : unit.deployWeaponSource), loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteDeployWeapon, source + " "
            + (unit.eliteDeployWeaponSource.empty()
                ? "EliteDeployWeapon"
                : unit.eliteDeployWeaponSource), loadedWeaponIds);
        if (!isIntegerToken(unit.occupyWeapon)) {
            addWeaponCandidate(ini, unit.occupyWeapon, source + " OccupyWeapon", loadedWeaponIds);
        }
        if (!isIntegerToken(unit.eliteOccupyWeapon)) {
            addWeaponCandidate(
                ini,
                unit.eliteOccupyWeapon,
                source + " EliteOccupyWeapon",
                loadedWeaponIds
            );
        }
    }

    for (const UnitType& unit : vehicleTypes_) {
        const std::string source = "vehicle [" + unit.id + "]";

        addWeaponCandidate(ini, unit.primaryWeapon, source + " Primary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.secondaryWeapon, source + " Secondary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.elitePrimaryWeapon, source + " ElitePrimary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteSecondaryWeapon, source + " EliteSecondary", loadedWeaponIds);
        addWeaponCandidate(
            ini,
            unit.deathWeapon,
            source + " DeathWeapon",
            loadedWeaponIds
        );
        for (std::size_t weaponIndex = 0;
            weaponIndex < unit.indexedWeapons.size();
            ++weaponIndex) {
            const std::string suffix = std::to_string(weaponIndex + 1U);
            addWeaponCandidate(
                ini,
                unit.indexedWeapons[weaponIndex],
                source + " Weapon" + suffix,
                loadedWeaponIds
            );
            if (weaponIndex < unit.eliteIndexedWeapons.size()) {
                addWeaponCandidate(
                    ini,
                    unit.eliteIndexedWeapons[weaponIndex],
                    source + " EliteWeapon" + suffix,
                    loadedWeaponIds
                );
            }
        }
        addWeaponCandidate(ini, unit.deployWeapon, source + " "
            + (unit.deployWeaponSource.empty()
                ? "DeployWeapon"
                : unit.deployWeaponSource), loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteDeployWeapon, source + " "
            + (unit.eliteDeployWeaponSource.empty()
                ? "EliteDeployWeapon"
                : unit.eliteDeployWeaponSource), loadedWeaponIds);
        if (!isIntegerToken(unit.occupyWeapon)) {
            addWeaponCandidate(ini, unit.occupyWeapon, source + " OccupyWeapon", loadedWeaponIds);
        }
        if (!isIntegerToken(unit.eliteOccupyWeapon)) {
            addWeaponCandidate(
                ini,
                unit.eliteOccupyWeapon,
                source + " EliteOccupyWeapon",
                loadedWeaponIds
            );
        }
    }

    for (const UnitType& unit : aircraftTypes_) {
        const std::string source = "aircraft [" + unit.id + "]";

        addWeaponCandidate(ini, unit.primaryWeapon, source + " Primary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.secondaryWeapon, source + " Secondary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.elitePrimaryWeapon, source + " ElitePrimary", loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteSecondaryWeapon, source + " EliteSecondary", loadedWeaponIds);
        addWeaponCandidate(
            ini,
            unit.deathWeapon,
            source + " DeathWeapon",
            loadedWeaponIds
        );
        for (std::size_t weaponIndex = 0;
            weaponIndex < unit.indexedWeapons.size();
            ++weaponIndex) {
            const std::string suffix = std::to_string(weaponIndex + 1U);
            addWeaponCandidate(
                ini,
                unit.indexedWeapons[weaponIndex],
                source + " Weapon" + suffix,
                loadedWeaponIds
            );
            if (weaponIndex < unit.eliteIndexedWeapons.size()) {
                addWeaponCandidate(
                    ini,
                    unit.eliteIndexedWeapons[weaponIndex],
                    source + " EliteWeapon" + suffix,
                    loadedWeaponIds
                );
            }
        }
        addWeaponCandidate(ini, unit.deployWeapon, source + " "
            + (unit.deployWeaponSource.empty()
                ? "DeployWeapon"
                : unit.deployWeaponSource), loadedWeaponIds);
        addWeaponCandidate(ini, unit.eliteDeployWeapon, source + " "
            + (unit.eliteDeployWeaponSource.empty()
                ? "EliteDeployWeapon"
                : unit.eliteDeployWeaponSource), loadedWeaponIds);
        if (!isIntegerToken(unit.occupyWeapon)) {
            addWeaponCandidate(ini, unit.occupyWeapon, source + " OccupyWeapon", loadedWeaponIds);
        }
        if (!isIntegerToken(unit.eliteOccupyWeapon)) {
            addWeaponCandidate(
                ini,
                unit.eliteOccupyWeapon,
                source + " EliteOccupyWeapon",
                loadedWeaponIds
            );
        }
    }

    for (const BuildingType& building : buildingTypes_) {
        const std::string source = "building [" + building.id + "]";

        addWeaponCandidate(ini, building.primaryWeapon, source + " Primary", loadedWeaponIds);
        addWeaponCandidate(ini, building.secondaryWeapon, source + " Secondary", loadedWeaponIds);
        addWeaponCandidate(ini, building.elitePrimaryWeapon, source + " ElitePrimary", loadedWeaponIds);
        addWeaponCandidate(ini, building.eliteSecondaryWeapon, source + " EliteSecondary", loadedWeaponIds);
        addWeaponCandidate(
            ini,
            building.deathWeapon,
            source + " DeathWeapon",
            loadedWeaponIds
        );
        for (std::size_t weaponIndex = 0;
            weaponIndex < building.indexedWeapons.size();
            ++weaponIndex) {
            const std::string suffix = std::to_string(weaponIndex + 1U);
            addWeaponCandidate(
                ini,
                building.indexedWeapons[weaponIndex],
                source + " Weapon" + suffix,
                loadedWeaponIds
            );
            if (weaponIndex < building.eliteIndexedWeapons.size()) {
                addWeaponCandidate(
                    ini,
                    building.eliteIndexedWeapons[weaponIndex],
                    source + " EliteWeapon" + suffix,
                    loadedWeaponIds
                );
            }
        }
        addWeaponCandidate(ini, building.deployWeapon, source + " "
            + (building.deployWeaponSource.empty()
                ? "DeployWeapon"
                : building.deployWeaponSource), loadedWeaponIds);
        addWeaponCandidate(ini, building.eliteDeployWeapon, source + " "
            + (building.eliteDeployWeaponSource.empty()
                ? "EliteDeployWeapon"
                : building.eliteDeployWeaponSource), loadedWeaponIds);
    }
}

void RulesDatabase::discoverWeaponsFromProjectiles(const IniFile& ini) {
    std::unordered_set<std::string> loadedWeaponIds;
    for (const WeaponType& weapon : weaponTypes_) {
        loadedWeaponIds.insert(canonicalIdKey(weapon.id));
    }

    for (std::size_t index = 0; index < projectileTypes_.size(); ++index) {
        const std::string projectileId = projectileTypes_[index].id;
        const std::string airburstWeapon =
            projectileTypes_[index].airburstWeapon;
        const std::string shrapnelWeapon =
            projectileTypes_[index].shrapnelWeapon;
        const std::size_t weaponCountBefore = weaponTypes_.size();
        addWeaponCandidate(
            ini,
            airburstWeapon,
            "projectile [" + projectileId + "] AirburstWeapon",
            loadedWeaponIds
        );
        addWeaponCandidate(
            ini,
            shrapnelWeapon,
            "projectile [" + projectileId + "] ShrapnelWeapon",
            loadedWeaponIds
        );
        for (std::size_t weaponIndex = weaponCountBefore;
            weaponIndex < weaponTypes_.size();
            ++weaponIndex) {
            addProjectileCandidate(
                ini,
                weaponTypes_[weaponIndex].projectile,
                "weapon [" + weaponTypes_[weaponIndex].id + "]"
            );
        }
    }
}

void RulesDatabase::addProjectileCandidate(
    const IniFile& ini,
    const std::string& projectileId,
    const std::string& source
) {
    if (isEmptyProjectileId(projectileId)) {
        return;
    }

    const std::string projectileKey = canonicalIdKey(projectileId);
    if (projectileIds_.find(projectileKey) != projectileIds_.end()) {
        return;
    }

    const std::string sectionName = findSectionName(ini, projectileId);
    if (sectionName.empty()) {
        addWarning("Projectile referenced by " + source
            + " but missing section [" + projectileId + "]");
        return;
    }

    const std::string sectionKey = canonicalIdKey(sectionName);
    if (projectileIds_.find(sectionKey) != projectileIds_.end()) {
        return;
    }

    projectileTypes_.push_back(loadProjectileType(ini, sectionName));
    projectileIds_.insert(sectionKey);
}

void RulesDatabase::discoverProjectilesFromWeapons(const IniFile& ini) {
    for (std::size_t index = 0; index < weaponTypes_.size(); ++index) {
        const std::string weaponId = weaponTypes_[index].id;
        const std::string projectileId = weaponTypes_[index].projectile;
        addProjectileCandidate(
            ini,
            projectileId,
            "weapon [" + weaponId + "]"
        );
    }
}

void RulesDatabase::addWarheadCandidate(
    const IniFile& ini,
    const std::string& warheadId,
    const std::string& source
) {
    if (isEmptyWarheadId(warheadId)) {
        return;
    }

    const std::string warheadKey = canonicalIdKey(warheadId);
    if (warheadIds_.find(warheadKey) != warheadIds_.end()) {
        return;
    }

    const std::string sectionName = findSectionName(ini, warheadId);
    if (sectionName.empty()) {
        addWarning("Warhead referenced by " + source + " but missing section [" + warheadId + "]");
        return;
    }

    const std::string sectionKey = canonicalIdKey(sectionName);
    if (warheadIds_.find(sectionKey) != warheadIds_.end()) {
        return;
    }

    warheadTypes_.push_back(loadWarheadType(ini, sectionName));
    warheadIds_.insert(sectionKey);
}

void RulesDatabase::discoverWarheadsFromWeapons(const IniFile& ini) {
    const bool hasCombatDamage = ini.getSection("CombatDamage") != nullptr;
    if (hasCombatDamage
        || !findSectionName(ini, specialMechanismRules_.c4Warhead).empty()) {
        addWarheadCandidate(
            ini,
            specialMechanismRules_.c4Warhead,
            "[CombatDamage] C4Warhead"
        );
    }
    if (hasCombatDamage
        || !findSectionName(ini, specialMechanismRules_.ivanWarhead).empty()) {
        addWarheadCandidate(
            ini,
            specialMechanismRules_.ivanWarhead,
            "[CombatDamage] IvanWarhead"
        );
    }
    if (hasCombatDamage
        || !findSectionName(
            ini,
            specialMechanismRules_.flameDamageWarhead
        ).empty()) {
        addWarheadCandidate(
            ini,
            specialMechanismRules_.flameDamageWarhead,
            "[CombatDamage] FlameDamage"
        );
    }
    if (!findSectionName(ini, "Gas").empty()) {
        addWarheadCandidate(ini, "Gas", "Gas crate powerup");
    }
    for (std::size_t index = 0; index < weaponTypes_.size(); ++index) {
        const std::string weaponId = weaponTypes_[index].id;
        const std::string warheadId = weaponTypes_[index].warhead;
        addWarheadCandidate(
            ini,
            warheadId,
            "weapon [" + weaponId + "]"
        );
    }
}

WeaponWarheadValidationReport RulesDatabase::validateWeaponWarheads() const {
    WeaponWarheadValidationReport report;

    report.totalWeapons = static_cast<int>(weaponTypes_.size());

    for (const WeaponType& weapon : weaponTypes_) {
        if (isEmptyWarheadId(weapon.warhead)) {
            report.weaponsWithoutWarhead++;
            report.weaponsWithoutWarheadList.push_back(weapon.id);
            continue;
        }

        if (!containsValueIgnoreCase(warheadIds_, weapon.warhead)) {
            report.weaponsWithMissingWarheadSection++;
            report.missingWarheadList.push_back(
                weapon.id + " -> missing warhead [" + weapon.warhead + "]"
            );
            continue;
        }

        report.weaponsWithValidWarhead++;
    }

    return report;
}

#include "data/rules/RulesModExtensions.inl"
