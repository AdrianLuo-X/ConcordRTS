#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IniFile;
class MapDatabase;

enum class CooperativeDiagnosticSeverity {
    Warning,
    Error
};

struct CooperativeScenarioDiagnostic {
    CooperativeDiagnosticSeverity severity = CooperativeDiagnosticSeverity::Error;
    std::string code;
    std::string source;
    std::string message;
};

enum class CooperativeSlotRole {
    LocalHuman,
    AiAlly,
    AiEnemy
};

// Cooperative maps use four stable authored identities.  A/B are the two
// cooperative participants and C/D are the two opposing participants.  The
// country vectors are constraints, not positional assignments: both members
// of a coalition may select from the corresponding authored list.
struct CooperativeScenarioSlot {
    int slotIndex = -1;
    char label = '?';
    // Westwood's cooperative trigger owner constants.  These are not house
    // array indexes: 4475..4478 are stable aliases for authored players A..D.
    int authoredOwnerId = -1;
    int waypointId = -1;
    CooperativeSlotRole role = CooperativeSlotRole::AiEnemy;
    int coalitionId = -1;
    std::string authoredHouse;
    std::vector<std::string> allowedCountries;
    // Deterministic fallback used when a caller has not supplied a country.
    // It is always the first country authored for this slot's coalition.
    std::string defaultCountry;
};

struct CooperativeScenarioDefinition {
    std::string campaignId;
    std::string campaignSection;
    int campaignOrdinal = 0;
    int missionOrdinal = 0;

    std::string campaignNameKey;
    std::string campaignAi;
    std::string mapId;
    // coopcampmd.ini authors a CSV map tuple.  Stock YR repeats the same id;
    // preserve all authored entries so mods do not silently lose identity
    // information.
    std::vector<std::string> authoredMapIds;
    std::vector<std::string> playerCountries;
    std::vector<std::string> enemyCountries;
    std::array<CooperativeScenarioSlot, 4> slots;
    std::array<std::array<bool, 4>, 4> allianceMatrix{};

    std::uint64_t fingerprint = 0;
    std::string identity;
};

struct CooperativeScenarioWaypoint {
    int waypointId = -1;
    int x = -1;
    int y = -1;
    int cell = -1;
};

struct CooperativeScenarioMapValidation {
    bool valid = false;
    std::array<CooperativeScenarioWaypoint, 4> waypoints;
    // Hash of every normalized INI section/key/value.  This deliberately
    // includes mission Trigger/Action/TeamType/ScriptType content and packed
    // map data, not only the four cooperative waypoints.
    std::uint64_t contentFingerprint = 0;
    std::string contentIdentity;
    std::uint64_t identityFingerprint = 0;
    std::string identity;
    std::vector<CooperativeScenarioDiagnostic> diagnostics;
};

// Data-only parser and validator for coopcampmd.ini.  It has no menu,
// networking, campaign-progress, or RuntimeShell dependencies.
class CooperativeScenarioDatabase {
public:
    bool loadFromIni(const IniFile& ini);

    bool loaded() const;
    std::size_t scenarioCount() const;
    const std::vector<CooperativeScenarioDefinition>& scenarios() const;
    const std::vector<CooperativeScenarioDiagnostic>& diagnostics() const;

    const CooperativeScenarioDefinition* findByMapId(
        const std::string& mapId
    ) const;

    CooperativeScenarioMapValidation validateMap(
        const CooperativeScenarioDefinition& scenario,
        const MapDatabase& map
    ) const;
    CooperativeScenarioMapValidation validateMap(
        const CooperativeScenarioDefinition& scenario,
        const MapDatabase& map,
        const IniFile& mapIni
    ) const;
    CooperativeScenarioMapValidation validateMap(
        const std::string& mapId,
        const MapDatabase& map
    ) const;
    CooperativeScenarioMapValidation validateMap(
        const std::string& mapId,
        const MapDatabase& map,
        const IniFile& mapIni
    ) const;

    std::uint64_t fingerprint() const;
    const std::string& identity() const;

private:
    bool loaded_ = false;
    std::vector<CooperativeScenarioDefinition> scenarios_;
    std::vector<CooperativeScenarioDiagnostic> diagnostics_;
    std::uint64_t fingerprint_ = 0;
    std::string identity_;
};

std::string cooperativeFingerprintHex(std::uint64_t fingerprint);
const char* cooperativeSlotRoleName(CooperativeSlotRole role);

// Exact, allocation-free parser for the two aliases used by authored
// cooperative maps.  Only "4475".."4478" and "<Player @ A>".."<Player @ D>"
// are accepted; malformed or unrelated action parameters are rejected.
std::optional<int> cooperativeSlotIndexFromAlias(std::string_view alias);

bool cooperativeSlotsAreAllied(int leftSlotIndex, int rightSlotIndex);
bool cooperativeSlotAllowsCountry(
    const CooperativeScenarioSlot& slot,
    std::string_view country
);

// Canonical fingerprint over all parsed INI content.  Section and key names
// are ASCII case-insensitive; values and duplicate-key order remain material.
std::uint64_t cooperativeIniContentFingerprint(const IniFile& ini);
