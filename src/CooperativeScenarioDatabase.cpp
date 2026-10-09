#include "CooperativeScenarioDatabase.h"

#include "IniFile.h"
#include "MapDatabase.h"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::uint64_t fnv1aOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnv1aPrime = 1099511628211ULL;
constexpr std::string_view scenarioFingerprintVersion = "coop-scenario-v2";
constexpr std::string_view mapFingerprintVersion = "coop-map-v2";
constexpr std::string_view mapLayoutFingerprintVersion = "coop-map-layout-v1";
constexpr std::string_view mapContentFingerprintVersion = "coop-map-content-v1";
constexpr std::string_view databaseFingerprintVersion = "coop-database-v2";

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) {
            return lowerAsciiCharacter(character);
        }
    );
    return value;
}

std::string trimAscii(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool startsWithAsciiIgnoreCase(
    const std::string& value,
    const std::string_view prefix
) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (lowerAsciiCharacter(value[index])
            != lowerAsciiCharacter(prefix[index])) {
            return false;
        }
    }
    return true;
}

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    return left.size() == right.size()
        && startsWithAsciiIgnoreCase(left, right);
}

bool parsePositiveInt(const std::string& text, int& output) {
    const std::string value = trimAscii(text);
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseNonNegativeInt(const std::string& text, int& output) {
    const std::string value = trimAscii(text);
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 0) {
        return false;
    }
    output = parsed;
    return true;
}

void addDiagnostic(
    std::vector<CooperativeScenarioDiagnostic>& diagnostics,
    CooperativeDiagnosticSeverity severity,
    std::string code,
    std::string source,
    std::string message
) {
    diagnostics.push_back({
        severity,
        std::move(code),
        std::move(source),
        std::move(message)
    });
}

bool hasErrors(const std::vector<CooperativeScenarioDiagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const CooperativeScenarioDiagnostic& diagnostic) {
            return diagnostic.severity == CooperativeDiagnosticSeverity::Error;
        }
    );
}

std::string uniqueSectionValue(
    const IniFile::Section& section,
    const std::string& key,
    const std::string& source,
    bool required,
    std::vector<CooperativeScenarioDiagnostic>& diagnostics
) {
    std::string value;
    int count = 0;
    for (const auto& [candidateKey, candidateValue] : section) {
        if (!equalsAsciiIgnoreCase(candidateKey, key)) {
            continue;
        }
        ++count;
        value = trimAscii(candidateValue);
    }
    if (count == 0 && required) {
        addDiagnostic(
            diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "missing-field",
            source + ":" + key,
            "Required cooperative scenario field is missing."
        );
    } else if (count > 1) {
        addDiagnostic(
            diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "duplicate-field",
            source + ":" + key,
            "Cooperative scenario field is authored more than once."
        );
    } else if (required && value.empty()) {
        addDiagnostic(
            diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "empty-field",
            source + ":" + key,
            "Required cooperative scenario field is empty."
        );
    }
    return value;
}

bool validIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '_'
                || character == '-';
        }
    );
}

std::string normalizeMapId(std::string value) {
    value = trimAscii(value);
    const std::string lowered = lowerAscii(value);
    if (lowered.size() > 4
        && lowered.substr(lowered.size() - 4) == ".map") {
        value.resize(value.size() - 4);
    }
    return value;
}

std::vector<std::string> splitCsvStrict(
    const std::string& value,
    const std::string& source,
    std::vector<CooperativeScenarioDiagnostic>& diagnostics
) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos
            ? value.size()
            : comma;
        std::string token = trimAscii(value.substr(start, end - start));
        if (token.empty()) {
            addDiagnostic(
                diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "empty-list-entry",
                source,
                "Comma-separated cooperative scenario list contains an empty entry."
            );
        } else {
            result.push_back(std::move(token));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

std::vector<std::string> parseIdentifierSet(
    const std::string& value,
    const std::string& source,
    std::vector<CooperativeScenarioDiagnostic>& diagnostics
) {
    std::vector<std::string> identifiers = splitCsvStrict(
        value,
        source,
        diagnostics
    );
    std::set<std::string> seen;
    for (const std::string& identifier : identifiers) {
        if (!validIdentifier(identifier)) {
            addDiagnostic(
                diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "invalid-identifier",
                source,
                "Invalid cooperative country identifier: " + identifier
            );
            continue;
        }
        if (!seen.insert(lowerAscii(identifier)).second) {
            addDiagnostic(
                diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "duplicate-country",
                source,
                "Country is listed more than once: " + identifier
            );
        }
    }
    return identifiers;
}

std::vector<std::string> parseMapIds(
    const std::string& value,
    const std::string& source,
    std::vector<CooperativeScenarioDiagnostic>& diagnostics
) {
    std::vector<std::string> mapIds = splitCsvStrict(value, source, diagnostics);
    if (mapIds.size() > 3) {
        addDiagnostic(
            diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "too-many-map-variants",
            source,
            "A cooperative mission may author at most three map variants."
        );
    }
    for (std::string& mapId : mapIds) {
        mapId = normalizeMapId(mapId);
        if (!validIdentifier(mapId)) {
            addDiagnostic(
                diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "invalid-map-id",
                source,
                "Invalid cooperative map id: " + mapId
            );
        }
    }
    return mapIds;
}

bool parseIndexedKey(
    const std::string& key,
    const std::string_view prefix,
    int& index
) {
    if (!startsWithAsciiIgnoreCase(key, prefix)) {
        return false;
    }
    return parsePositiveInt(key.substr(prefix.size()), index);
}

void validateIndexedFields(
    const IniFile::Section& section,
    int missionCount,
    const std::string& source,
    std::vector<CooperativeScenarioDiagnostic>& diagnostics
) {
    for (const auto& [key, value] : section) {
        (void)value;
        for (const std::string_view prefix : {
                std::string_view("Map"),
                std::string_view("CampaignPlayer"),
                std::string_view("CampaignEnemy")
            }) {
            if (!startsWithAsciiIgnoreCase(key, prefix)) {
                continue;
            }
            int index = 0;
            if (!parseIndexedKey(key, prefix, index)) {
                addDiagnostic(
                    diagnostics,
                    CooperativeDiagnosticSeverity::Error,
                    "invalid-indexed-field",
                    source + ":" + key,
                    "Indexed cooperative field has an invalid mission number."
                );
            } else if (index > missionCount) {
                addDiagnostic(
                    diagnostics,
                    CooperativeDiagnosticSeverity::Error,
                    "out-of-range-field",
                    source + ":" + key,
                    "Indexed cooperative field exceeds NumberOfCampaignMaps."
                );
            }
            break;
        }
    }
}

std::vector<std::string> sortedCanonical(std::vector<std::string> values) {
    for (std::string& value : values) {
        value = lowerAscii(trimAscii(value));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::string join(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << values[index];
    }
    return stream.str();
}

std::uint64_t fnv1a64(const std::string_view text) {
    std::uint64_t hash = fnv1aOffset;
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnv1aPrime;
    }
    return hash;
}

std::string canonicalIniContent(const IniFile& ini) {
    std::vector<std::string> sectionNames = ini.getSectionNames();
    std::sort(
        sectionNames.begin(),
        sectionNames.end(),
        [](const std::string& left, const std::string& right) {
            return lowerAscii(trimAscii(left)) < lowerAscii(trimAscii(right));
        }
    );

    std::ostringstream stream;
    stream << mapContentFingerprintVersion;
    for (const std::string& sectionName : sectionNames) {
        const std::string canonicalSection = lowerAscii(trimAscii(sectionName));
        stream
            << "\nS" << canonicalSection.size()
            << ':' << canonicalSection;

        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) {
            continue;
        }
        struct CanonicalEntry {
            std::string key;
            std::string value;
            std::size_t duplicateOrdinal = 0;
        };
        std::vector<CanonicalEntry> entries;
        entries.reserve(section->size());
        std::map<std::string, std::size_t> duplicateOrdinals;
        for (const auto& [rawKey, rawValue] : *section) {
            const std::string key = lowerAscii(trimAscii(rawKey));
            entries.push_back({
                key,
                trimAscii(rawValue),
                duplicateOrdinals[key]++
            });
        }
        std::stable_sort(
            entries.begin(),
            entries.end(),
            [](const CanonicalEntry& left, const CanonicalEntry& right) {
                if (left.key != right.key) {
                    return left.key < right.key;
                }
                return left.duplicateOrdinal < right.duplicateOrdinal;
            }
        );
        for (const CanonicalEntry& entry : entries) {
            stream
                << "\nK" << entry.key.size()
                << ':' << entry.key
                << '#' << entry.duplicateOrdinal
                << "=V" << entry.value.size()
                << ':' << entry.value;
        }
    }
    return stream.str();
}

std::string scenarioFingerprintText(
    const CooperativeScenarioDefinition& scenario
) {
    std::vector<std::string> mapIds = scenario.authoredMapIds;
    for (std::string& mapId : mapIds) {
        mapId = lowerAscii(normalizeMapId(mapId));
    }
    std::ostringstream stream;
    stream
        << scenarioFingerprintVersion
        << "|campaign=" << lowerAscii(scenario.campaignId)
        << "|section=" << lowerAscii(scenario.campaignSection)
        << "|campaignOrdinal=" << scenario.campaignOrdinal
        << "|missionOrdinal=" << scenario.missionOrdinal
        << "|campaignName=" << lowerAscii(scenario.campaignNameKey)
        << "|campaignAi=" << lowerAscii(scenario.campaignAi)
        << "|mapIds=" << join(mapIds)
        << "|players=" << join(sortedCanonical(scenario.playerCountries))
        << "|enemies=" << join(sortedCanonical(scenario.enemyCountries));
    for (const CooperativeScenarioSlot& slot : scenario.slots) {
        stream
            << "|slot=" << slot.label
            << ':' << slot.slotIndex
            << ':' << slot.authoredOwnerId
            << ':' << slot.waypointId
            << ':' << cooperativeSlotRoleName(slot.role)
            << ':' << slot.coalitionId
            << ':' << lowerAscii(slot.authoredHouse)
            << ':' << join(sortedCanonical(slot.allowedCountries))
            << ':' << lowerAscii(slot.defaultCountry);
    }
    for (std::size_t left = 0; left < scenario.allianceMatrix.size(); ++left) {
        for (std::size_t right = 0;
             right < scenario.allianceMatrix[left].size();
             ++right) {
            stream
                << "|allied=" << left << ':' << right << ':'
                << (scenario.allianceMatrix[left][right] ? '1' : '0');
        }
    }
    return stream.str();
}

std::array<std::array<bool, 4>, 4> makeAllianceMatrix() {
    std::array<std::array<bool, 4>, 4> matrix{};
    for (int left = 0; left < 4; ++left) {
        for (int right = 0; right < 4; ++right) {
            matrix[static_cast<std::size_t>(left)]
                  [static_cast<std::size_t>(right)] =
                cooperativeSlotsAreAllied(left, right);
        }
    }
    return matrix;
}

std::array<CooperativeScenarioSlot, 4> makeSlots(
    const std::vector<std::string>& playerCountries,
    const std::vector<std::string>& enemyCountries
) {
    std::array<CooperativeScenarioSlot, 4> slots;
    for (int index = 0; index < 4; ++index) {
        CooperativeScenarioSlot& slot = slots[static_cast<std::size_t>(index)];
        slot.slotIndex = index;
        slot.label = static_cast<char>('A' + index);
        slot.authoredOwnerId = 4475 + index;
        slot.waypointId = index;
        slot.coalitionId = index < 2 ? 0 : 1;
        slot.authoredHouse = std::string("<Player @ ") + slot.label + ">";
        slot.allowedCountries = index < 2
            ? playerCountries
            : enemyCountries;
        if (!slot.allowedCountries.empty()) {
            slot.defaultCountry = slot.allowedCountries.front();
        }
        if (index == 0) {
            slot.role = CooperativeSlotRole::LocalHuman;
        } else if (index == 1) {
            slot.role = CooperativeSlotRole::AiAlly;
        } else {
            slot.role = CooperativeSlotRole::AiEnemy;
        }
    }
    return slots;
}

struct CampaignReference {
    int ordinal = 0;
    std::string id;
    std::string section;
};

} // namespace

std::string cooperativeFingerprintHex(std::uint64_t fingerprint) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << fingerprint;
    return stream.str();
}

const char* cooperativeSlotRoleName(CooperativeSlotRole role) {
    switch (role) {
    case CooperativeSlotRole::LocalHuman:
        return "local-human";
    case CooperativeSlotRole::AiAlly:
        return "ai-ally";
    case CooperativeSlotRole::AiEnemy:
        return "ai-enemy";
    }
    return "unknown";
}

std::optional<int> cooperativeSlotIndexFromAlias(std::string_view alias) {
    constexpr std::array<std::string_view, 4> numericAliases{
        "4475", "4476", "4477", "4478"
    };
    constexpr std::array<std::string_view, 4> houseAliases{
        "<Player @ A>",
        "<Player @ B>",
        "<Player @ C>",
        "<Player @ D>"
    };
    for (std::size_t index = 0; index < numericAliases.size(); ++index) {
        if (alias == numericAliases[index] || alias == houseAliases[index]) {
            return static_cast<int>(index);
        }
    }
    return std::nullopt;
}

bool cooperativeSlotsAreAllied(int leftSlotIndex, int rightSlotIndex) {
    if (leftSlotIndex < 0 || leftSlotIndex >= 4
        || rightSlotIndex < 0 || rightSlotIndex >= 4) {
        return false;
    }
    return (leftSlotIndex < 2) == (rightSlotIndex < 2);
}

bool cooperativeSlotAllowsCountry(
    const CooperativeScenarioSlot& slot,
    std::string_view country
) {
    if (country.empty()) {
        return false;
    }
    const std::string wanted = trimAscii(std::string(country));
    if (wanted.empty()) {
        return false;
    }
    return std::any_of(
        slot.allowedCountries.begin(),
        slot.allowedCountries.end(),
        [&wanted](const std::string& allowed) {
            return equalsAsciiIgnoreCase(trimAscii(allowed), wanted);
        }
    );
}

std::uint64_t cooperativeIniContentFingerprint(const IniFile& ini) {
    return fnv1a64(canonicalIniContent(ini));
}

bool CooperativeScenarioDatabase::loadFromIni(const IniFile& ini) {
    loaded_ = false;
    scenarios_.clear();
    diagnostics_.clear();
    fingerprint_ = 0;
    identity_.clear();

    const IniFile::Section* campaignsSection = ini.getSection("Campaigns");
    if (campaignsSection == nullptr || campaignsSection->empty()) {
        addDiagnostic(
            diagnostics_,
            CooperativeDiagnosticSeverity::Error,
            "missing-campaigns",
            "[Campaigns]",
            "coopcampmd.ini is missing a non-empty [Campaigns] section."
        );
        return false;
    }

    std::vector<CampaignReference> campaigns;
    std::set<int> campaignOrdinals;
    std::set<std::string> campaignSections;
    for (const auto& [key, rawSection] : *campaignsSection) {
        int ordinal = 0;
        if (!parseIndexedKey(key, "Campaign", ordinal)) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "invalid-campaign-key",
                "[Campaigns]:" + key,
                "Campaign list keys must use Campaign followed by a positive integer."
            );
            continue;
        }
        const std::string section = trimAscii(rawSection);
        if (section.empty()) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "empty-campaign-section",
                "[Campaigns]:" + key,
                "Campaign section name is empty."
            );
            continue;
        }
        if (!campaignOrdinals.insert(ordinal).second) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "duplicate-campaign-ordinal",
                "[Campaigns]:" + key,
                "Campaign ordinal is authored more than once."
            );
        }
        if (!campaignSections.insert(lowerAscii(section)).second) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "duplicate-campaign-section",
                "[Campaigns]:" + key,
                "Campaign section is referenced more than once: " + section
            );
        }
        campaigns.push_back({ordinal, key, section});
    }
    std::sort(
        campaigns.begin(),
        campaigns.end(),
        [](const CampaignReference& left, const CampaignReference& right) {
            if (left.ordinal != right.ordinal) {
                return left.ordinal < right.ordinal;
            }
            return lowerAscii(left.id) < lowerAscii(right.id);
        }
    );
    for (std::size_t index = 0; index < campaigns.size(); ++index) {
        if (campaigns[index].ordinal != static_cast<int>(index + 1)) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "campaign-ordinal-gap",
                "[Campaigns]",
                "Campaign ordinals must be contiguous and start at 1."
            );
            break;
        }
    }

    std::map<std::string, std::string> mapOwner;
    for (const CampaignReference& campaign : campaigns) {
        const IniFile::Section* section = ini.getSection(campaign.section);
        const std::string source = "[" + campaign.section + "]";
        if (section == nullptr) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "missing-campaign-section",
                source,
                "Referenced cooperative campaign section is missing."
            );
            continue;
        }

        const std::string mapCountText = uniqueSectionValue(
            *section,
            "NumberOfCampaignMaps",
            source,
            true,
            diagnostics_
        );
        int missionCount = 0;
        if (!parsePositiveInt(mapCountText, missionCount)) {
            addDiagnostic(
                diagnostics_,
                CooperativeDiagnosticSeverity::Error,
                "invalid-map-count",
                source + ":NumberOfCampaignMaps",
                "NumberOfCampaignMaps must be a positive integer."
            );
            continue;
        }

        validateIndexedFields(*section, missionCount, source, diagnostics_);
        const std::string campaignName = uniqueSectionValue(
            *section,
            "CampaignName",
            source,
            true,
            diagnostics_
        );
        const std::string campaignAi = uniqueSectionValue(
            *section,
            "CampaignAI",
            source,
            true,
            diagnostics_
        );

        for (int missionOrdinal = 1;
             missionOrdinal <= missionCount;
             ++missionOrdinal) {
            const std::string suffix = std::to_string(missionOrdinal);
            const std::string mapKey = "Map" + suffix;
            const std::string playerKey = "CampaignPlayer" + suffix;
            const std::string enemyKey = "CampaignEnemy" + suffix;
            const std::string mapValue = uniqueSectionValue(
                *section,
                mapKey,
                source,
                true,
                diagnostics_
            );
            const std::string playerValue = uniqueSectionValue(
                *section,
                playerKey,
                source,
                true,
                diagnostics_
            );
            const std::string enemyValue = uniqueSectionValue(
                *section,
                enemyKey,
                source,
                true,
                diagnostics_
            );

            CooperativeScenarioDefinition scenario;
            scenario.campaignId = campaign.id;
            scenario.campaignSection = campaign.section;
            scenario.campaignOrdinal = campaign.ordinal;
            scenario.missionOrdinal = missionOrdinal;
            scenario.campaignNameKey = campaignName;
            scenario.campaignAi = campaignAi;
            scenario.authoredMapIds = parseMapIds(
                mapValue,
                source + ":" + mapKey,
                diagnostics_
            );
            scenario.playerCountries = parseIdentifierSet(
                playerValue,
                source + ":" + playerKey,
                diagnostics_
            );
            scenario.enemyCountries = parseIdentifierSet(
                enemyValue,
                source + ":" + enemyKey,
                diagnostics_
            );

            if (!scenario.authoredMapIds.empty()) {
                scenario.mapId = scenario.authoredMapIds.front();
            }
            std::set<std::string> playerSet;
            for (const std::string& country : scenario.playerCountries) {
                playerSet.insert(lowerAscii(country));
            }
            for (const std::string& country : scenario.enemyCountries) {
                if (playerSet.find(lowerAscii(country)) != playerSet.end()) {
                    addDiagnostic(
                        diagnostics_,
                        CooperativeDiagnosticSeverity::Error,
                        "country-coalition-overlap",
                        source + ":" + enemyKey,
                        "Country is allowed on both cooperative coalitions: "
                            + country
                    );
                }
            }

            scenario.slots = makeSlots(
                scenario.playerCountries,
                scenario.enemyCountries
            );
            scenario.allianceMatrix = makeAllianceMatrix();
            for (const CooperativeScenarioSlot& slot : scenario.slots) {
                if (slot.defaultCountry.empty()
                    || !cooperativeSlotAllowsCountry(
                        slot,
                        slot.defaultCountry
                    )) {
                    addDiagnostic(
                        diagnostics_,
                        CooperativeDiagnosticSeverity::Error,
                        "invalid-default-country",
                        source + ":" + (slot.slotIndex < 2
                            ? playerKey
                            : enemyKey),
                        "Cooperative slot " + std::string(1, slot.label)
                            + " has no valid default country."
                    );
                }
            }
            const std::string fingerprintText = scenarioFingerprintText(scenario);
            scenario.fingerprint = fnv1a64(fingerprintText);
            scenario.identity = std::string(scenarioFingerprintVersion)
                + ":" + lowerAscii(scenario.mapId)
                + ":" + cooperativeFingerprintHex(scenario.fingerprint);

            std::set<std::string> uniqueScenarioMapIds;
            for (const std::string& mapId : scenario.authoredMapIds) {
                const std::string canonical = lowerAscii(mapId);
                if (!uniqueScenarioMapIds.insert(canonical).second) {
                    continue;
                }
                const auto [iterator, inserted] = mapOwner.emplace(
                    canonical,
                    source + ":" + mapKey
                );
                if (!inserted) {
                    addDiagnostic(
                        diagnostics_,
                        CooperativeDiagnosticSeverity::Error,
                        "map-scenario-overlap",
                        source + ":" + mapKey,
                        "Map id is assigned to more than one cooperative mission; first assignment was "
                            + iterator->second + "."
                    );
                }
            }
            scenarios_.push_back(std::move(scenario));
        }
    }

    std::sort(
        scenarios_.begin(),
        scenarios_.end(),
        [](const CooperativeScenarioDefinition& left,
           const CooperativeScenarioDefinition& right) {
            if (left.campaignOrdinal != right.campaignOrdinal) {
                return left.campaignOrdinal < right.campaignOrdinal;
            }
            if (left.missionOrdinal != right.missionOrdinal) {
                return left.missionOrdinal < right.missionOrdinal;
            }
            return lowerAscii(left.mapId) < lowerAscii(right.mapId);
        }
    );

    if (scenarios_.empty()) {
        addDiagnostic(
            diagnostics_,
            CooperativeDiagnosticSeverity::Error,
            "no-scenarios",
            "[Campaigns]",
            "No cooperative scenarios were parsed."
        );
    }
    if (hasErrors(diagnostics_)) {
        return false;
    }

    std::ostringstream fingerprintText;
    fingerprintText << databaseFingerprintVersion;
    for (const CooperativeScenarioDefinition& scenario : scenarios_) {
        fingerprintText << '\n' << scenarioFingerprintText(scenario);
    }
    fingerprint_ = fnv1a64(fingerprintText.str());
    identity_ = std::string(databaseFingerprintVersion)
        + ":" + cooperativeFingerprintHex(fingerprint_);
    loaded_ = true;
    return true;
}

bool CooperativeScenarioDatabase::loaded() const {
    return loaded_;
}

std::size_t CooperativeScenarioDatabase::scenarioCount() const {
    return scenarios_.size();
}

const std::vector<CooperativeScenarioDefinition>&
CooperativeScenarioDatabase::scenarios() const {
    return scenarios_;
}

const std::vector<CooperativeScenarioDiagnostic>&
CooperativeScenarioDatabase::diagnostics() const {
    return diagnostics_;
}

const CooperativeScenarioDefinition* CooperativeScenarioDatabase::findByMapId(
    const std::string& mapId
) const {
    if (!loaded_) {
        return nullptr;
    }
    const std::string wanted = lowerAscii(normalizeMapId(mapId));
    for (const CooperativeScenarioDefinition& scenario : scenarios_) {
        for (const std::string& authoredMapId : scenario.authoredMapIds) {
            if (lowerAscii(authoredMapId) == wanted) {
                return &scenario;
            }
        }
    }
    return nullptr;
}

CooperativeScenarioMapValidation CooperativeScenarioDatabase::validateMap(
    const CooperativeScenarioDefinition& scenario,
    const MapDatabase& map
) const {
    CooperativeScenarioMapValidation validation;
    const MapSummary& summary = map.getSummary();
    if (summary.numberStartingPoints != 4) {
        addDiagnostic(
            validation.diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "invalid-start-count",
            scenario.mapId + ":[Header]:NumberStartingPoints",
            "Cooperative maps must declare exactly four starting points."
        );
    }
    if (summary.coopHumanStartSpots != 2) {
        addDiagnostic(
            validation.diagnostics,
            CooperativeDiagnosticSeverity::Error,
            "invalid-human-start-count",
            scenario.mapId + ":[Header]:NumCoopHumanStartSpots",
            "Cooperative maps must declare exactly two human start spots."
        );
    }

    std::array<int, 4> waypointMatches{};
    std::set<int> requiredCells;
    for (const MapWaypoint& waypoint : map.getWaypoints()) {
        int waypointId = -1;
        if (!parseNonNegativeInt(waypoint.id, waypointId)
            || waypointId < 0
            || waypointId >= 4) {
            continue;
        }
        ++waypointMatches[static_cast<std::size_t>(waypointId)];
        CooperativeScenarioWaypoint& output =
            validation.waypoints[static_cast<std::size_t>(waypointId)];
        output.waypointId = waypointId;
        output.x = waypoint.position.x;
        output.y = waypoint.position.y;
        output.cell = waypoint.position.cell;
        if (!waypoint.position.valid) {
            addDiagnostic(
                validation.diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "invalid-waypoint-cell",
                scenario.mapId + ":[Waypoints]:" + waypoint.id,
                "Required cooperative waypoint has an invalid map cell."
            );
        } else if (!requiredCells.insert(waypoint.position.cell).second) {
            addDiagnostic(
                validation.diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "overlapping-waypoint",
                scenario.mapId + ":[Waypoints]:" + waypoint.id,
                "Required cooperative waypoints must occupy distinct cells."
            );
        }
    }
    for (int waypointId = 0; waypointId < 4; ++waypointId) {
        const int matches = waypointMatches[static_cast<std::size_t>(waypointId)];
        if (matches == 0) {
            addDiagnostic(
                validation.diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "missing-waypoint",
                scenario.mapId + ":[Waypoints]:" + std::to_string(waypointId),
                "Required cooperative waypoint is missing."
            );
        } else if (matches > 1) {
            addDiagnostic(
                validation.diagnostics,
                CooperativeDiagnosticSeverity::Error,
                "duplicate-waypoint",
                scenario.mapId + ":[Waypoints]:" + std::to_string(waypointId),
                "Required cooperative waypoint id is authored more than once."
            );
        }
    }

    validation.valid = !hasErrors(validation.diagnostics);
    if (!validation.valid) {
        return validation;
    }

    std::ostringstream identityText;
    identityText
        << mapLayoutFingerprintVersion
        << "|scenario=" << scenario.identity
        << "|startingPoints=" << summary.numberStartingPoints
        << "|humanStarts=" << summary.coopHumanStartSpots;
    for (const CooperativeScenarioWaypoint& waypoint : validation.waypoints) {
        identityText
            << "|waypoint=" << waypoint.waypointId
            << ':' << waypoint.cell
            << ':' << waypoint.x
            << ':' << waypoint.y;
    }
    validation.identityFingerprint = fnv1a64(identityText.str());
    validation.identity = std::string(mapLayoutFingerprintVersion)
        + ":" + lowerAscii(scenario.mapId)
        + ":" + cooperativeFingerprintHex(validation.identityFingerprint);
    return validation;
}

CooperativeScenarioMapValidation CooperativeScenarioDatabase::validateMap(
    const CooperativeScenarioDefinition& scenario,
    const MapDatabase& map,
    const IniFile& mapIni
) const {
    CooperativeScenarioMapValidation validation = validateMap(scenario, map);
    validation.contentFingerprint = cooperativeIniContentFingerprint(mapIni);
    validation.contentIdentity = std::string(mapContentFingerprintVersion)
        + ":" + cooperativeFingerprintHex(validation.contentFingerprint);
    if (!validation.valid) {
        return validation;
    }

    const std::string layoutIdentity = validation.identity;
    std::ostringstream identityText;
    identityText
        << mapFingerprintVersion
        << "|scenario=" << scenario.identity
        << "|layout=" << layoutIdentity
        << "|content=" << validation.contentIdentity;
    validation.identityFingerprint = fnv1a64(identityText.str());
    validation.identity = std::string(mapFingerprintVersion)
        + ":" + lowerAscii(scenario.mapId)
        + ":" + cooperativeFingerprintHex(validation.identityFingerprint);
    return validation;
}

CooperativeScenarioMapValidation CooperativeScenarioDatabase::validateMap(
    const std::string& mapId,
    const MapDatabase& map
) const {
    const CooperativeScenarioDefinition* scenario = findByMapId(mapId);
    if (scenario != nullptr) {
        return validateMap(*scenario, map);
    }
    CooperativeScenarioMapValidation validation;
    addDiagnostic(
        validation.diagnostics,
        CooperativeDiagnosticSeverity::Error,
        "unknown-map",
        normalizeMapId(mapId),
        "Map id is not present in the loaded cooperative scenario database."
    );
    return validation;
}

CooperativeScenarioMapValidation CooperativeScenarioDatabase::validateMap(
    const std::string& mapId,
    const MapDatabase& map,
    const IniFile& mapIni
) const {
    const CooperativeScenarioDefinition* scenario = findByMapId(mapId);
    if (scenario != nullptr) {
        return validateMap(*scenario, map, mapIni);
    }
    CooperativeScenarioMapValidation validation;
    validation.contentFingerprint = cooperativeIniContentFingerprint(mapIni);
    validation.contentIdentity = std::string(mapContentFingerprintVersion)
        + ":" + cooperativeFingerprintHex(validation.contentFingerprint);
    addDiagnostic(
        validation.diagnostics,
        CooperativeDiagnosticSeverity::Error,
        "unknown-map",
        normalizeMapId(mapId),
        "Map id is not present in the loaded cooperative scenario database."
    );
    return validation;
}

std::uint64_t CooperativeScenarioDatabase::fingerprint() const {
    return fingerprint_;
}

const std::string& CooperativeScenarioDatabase::identity() const {
    return identity_;
}
