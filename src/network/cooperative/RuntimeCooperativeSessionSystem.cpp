#include "network/cooperative/RuntimeCooperativeSessionSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace {

constexpr std::size_t maximumIdentityLength = 4096;
constexpr std::size_t maximumMapIdLength = 260;
constexpr std::size_t maximumOwnerLength = 256;
constexpr std::size_t maximumCountryLength = 128;
constexpr std::size_t maximumAllowedCountries = 64;
constexpr int maximumLiveTechnoCount = 100000000;

bool equalsAsciiIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const unsigned char leftCharacter =
            static_cast<unsigned char>(left[index]);
        const unsigned char rightCharacter =
            static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
            return false;
        }
    }
    return true;
}

std::string trimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size()
        && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin
        && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool validBoundedText(
    const std::string& value,
    std::size_t maximumLength,
    bool allowEmpty = false
) {
    if ((!allowEmpty && value.empty()) || value.size() > maximumLength) {
        return false;
    }
    return std::none_of(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return character == '\0';
        }
    );
}

bool validOwner(const std::string& owner) {
    return validBoundedText(owner, maximumOwnerLength)
        && trimAscii(owner) == owner;
}

std::optional<std::string> canonicalCountry(
    const std::vector<std::string>& allowed,
    std::string_view requested
) {
    const std::string trimmed = trimAscii(requested);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    const auto found = std::find_if(
        allowed.begin(),
        allowed.end(),
        [&trimmed](const std::string& candidate) {
            return equalsAsciiIgnoreCase(candidate, trimmed);
        }
    );
    if (found == allowed.end()) {
        return std::nullopt;
    }
    return *found;
}

CooperativeSlotRole expectedRole(std::size_t index) {
    if (index == 0) {
        return CooperativeSlotRole::LocalHuman;
    }
    if (index == 1) {
        return CooperativeSlotRole::AiAlly;
    }
    return CooperativeSlotRole::AiEnemy;
}

int expectedCoalition(std::size_t index) {
    return index < 2 ? 0 : 1;
}

bool sameWaypoint(
    const CooperativeScenarioWaypoint& left,
    const CooperativeScenarioWaypoint& right
) {
    return left.waypointId == right.waypointId
        && left.x == right.x
        && left.y == right.y
        && left.cell == right.cell;
}

bool validateContext(
    const CooperativeScenarioDefinition& scenario,
    const CooperativeScenarioMapValidation& mapValidation,
    std::string& reason
) {
    if (!validBoundedText(scenario.identity, maximumIdentityLength)
        || !validBoundedText(scenario.mapId, maximumMapIdLength)
        || scenario.fingerprint == 0) {
        reason = "cooperative scenario identity is invalid";
        return false;
    }
    if (!mapValidation.valid
        || mapValidation.identityFingerprint == 0
        || !validBoundedText(
            mapValidation.identity,
            maximumIdentityLength
        )) {
        reason = "cooperative map validation is invalid";
        return false;
    }
    if (scenario.playerCountries.empty()
        || scenario.enemyCountries.empty()
        || scenario.playerCountries.size() > maximumAllowedCountries
        || scenario.enemyCountries.size() > maximumAllowedCountries) {
        reason = "cooperative coalition country constraints are invalid";
        return false;
    }
    for (const std::string& country : scenario.playerCountries) {
        if (!validBoundedText(country, maximumCountryLength)) {
            reason = "player coalition country identifier is invalid";
            return false;
        }
    }
    for (const std::string& country : scenario.enemyCountries) {
        if (!validBoundedText(country, maximumCountryLength)) {
            reason = "enemy coalition country identifier is invalid";
            return false;
        }
    }

    std::set<int> startCells;
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const CooperativeScenarioSlot& slot = scenario.slots[index];
        const CooperativeScenarioWaypoint& start =
            mapValidation.waypoints[index];
        const int coalition = expectedCoalition(index);
        const std::vector<std::string>& expectedCountries = coalition == 0
            ? scenario.playerCountries
            : scenario.enemyCountries;
        const std::string expectedHouse = std::string("<Player @ ")
            + static_cast<char>('A' + index) + ">";
        if (slot.slotIndex != static_cast<int>(index)
            || slot.label != static_cast<char>('A' + index)
            || slot.authoredOwnerId != 4475 + static_cast<int>(index)
            || slot.waypointId != static_cast<int>(index)
            || slot.role != expectedRole(index)
            || slot.coalitionId != coalition
            || slot.authoredHouse != expectedHouse
            || slot.allowedCountries != expectedCountries
            || slot.defaultCountry.empty()
            || !canonicalCountry(
                expectedCountries,
                slot.defaultCountry
            ).has_value()) {
            reason = "cooperative A-D slot definition is invalid";
            return false;
        }
        if (start.waypointId != static_cast<int>(index)
            || start.x < 0
            || start.y < 0
            || start.cell < 0
            || !startCells.insert(start.cell).second) {
            reason = "cooperative A-D start waypoint is invalid";
            return false;
        }
        for (std::size_t other = 0;
             other < runtimeCooperativeSlotCount;
             ++other) {
            if (scenario.allianceMatrix[index][other]
                != (expectedCoalition(index)
                    == expectedCoalition(other))) {
                reason = "cooperative alliance matrix is invalid";
                return false;
            }
        }
    }
    return true;
}

bool parseIntStrict(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseFingerprint(const std::string& text, std::uint64_t& value) {
    if (text.size() != 16) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 16);
    return result.ec == std::errc{} && result.ptr == end;
}

std::string hexEncode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char character : value) {
        encoded.push_back(digits[character >> 4]);
        encoded.push_back(digits[character & 0x0f]);
    }
    return encoded;
}

int hexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool hexDecode(
    const std::string& encoded,
    std::string& decoded,
    std::size_t maximumLength
) {
    if (encoded.size() % 2 != 0
        || encoded.size() / 2 > maximumLength) {
        return false;
    }
    std::string result;
    result.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const int high = hexDigit(encoded[index]);
        const int low = hexDigit(encoded[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        result.push_back(static_cast<char>((high << 4) | low));
    }
    decoded = std::move(result);
    return true;
}

bool statusFromName(
    const std::string& name,
    RuntimeCooperativeSessionStatus& status
) {
    if (name == "inactive") {
        status = RuntimeCooperativeSessionStatus::Inactive;
    } else if (name == "running") {
        status = RuntimeCooperativeSessionStatus::Running;
    } else if (name == "player-victory") {
        status = RuntimeCooperativeSessionStatus::PlayerCoalitionVictory;
    } else if (name == "enemy-victory") {
        status = RuntimeCooperativeSessionStatus::EnemyCoalitionVictory;
    } else if (name == "draw") {
        status = RuntimeCooperativeSessionStatus::Draw;
    } else {
        return false;
    }
    return true;
}

bool roleFromName(const std::string& name, CooperativeSlotRole& role) {
    if (name == "local-human") {
        role = CooperativeSlotRole::LocalHuman;
    } else if (name == "ai-ally") {
        role = CooperativeSlotRole::AiAlly;
    } else if (name == "ai-enemy") {
        role = CooperativeSlotRole::AiEnemy;
    } else {
        return false;
    }
    return true;
}

bool snapshotMatchesStatus(
    RuntimeCooperativeSessionStatus status,
    const RuntimeCooperativeLiveTechnoSnapshot& snapshot
) {
    const long long player = static_cast<long long>(
        snapshot.liveTechnos[0]
    ) + snapshot.liveTechnos[1];
    const long long enemy = static_cast<long long>(
        snapshot.liveTechnos[2]
    ) + snapshot.liveTechnos[3];
    switch (status) {
    case RuntimeCooperativeSessionStatus::Running:
        return player > 0 && enemy > 0;
    case RuntimeCooperativeSessionStatus::PlayerCoalitionVictory:
        return player > 0 && enemy == 0;
    case RuntimeCooperativeSessionStatus::EnemyCoalitionVictory:
        return player == 0 && enemy > 0;
    case RuntimeCooperativeSessionStatus::Draw:
        return player == 0 && enemy == 0;
    case RuntimeCooperativeSessionStatus::Inactive:
        return false;
    }
    return false;
}

} // namespace

RuntimeCooperativeBootstrapPolicy runtimeCooperativeBootstrapPolicy() {
    return {};
}

const char* runtimeCooperativeSessionStatusName(
    RuntimeCooperativeSessionStatus status
) {
    switch (status) {
    case RuntimeCooperativeSessionStatus::Inactive: return "inactive";
    case RuntimeCooperativeSessionStatus::Running: return "running";
    case RuntimeCooperativeSessionStatus::PlayerCoalitionVictory:
        return "player-victory";
    case RuntimeCooperativeSessionStatus::EnemyCoalitionVictory:
        return "enemy-victory";
    case RuntimeCooperativeSessionStatus::Draw: return "draw";
    }
    return "unknown";
}

std::optional<int> runtimeCooperativeScenarioSlotIndexFromAlias(
    const CooperativeScenarioDefinition& scenario,
    std::string_view alias
) {
    std::optional<int> resolved;
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const CooperativeScenarioSlot& slot = scenario.slots[index];
        const bool matches = alias == slot.authoredHouse
            || alias == std::to_string(slot.authoredOwnerId);
        if (!matches) {
            continue;
        }
        if (resolved.has_value()
            && *resolved != static_cast<int>(index)) {
            return std::nullopt;
        }
        resolved = static_cast<int>(index);
    }
    return resolved;
}

void RuntimeCooperativeSessionState::reset() {
    status_ = RuntimeCooperativeSessionStatus::Inactive;
    scenarioId_.clear();
    mapId_.clear();
    mapIdentity_.clear();
    scenarioFingerprint_ = 0;
    mapFingerprint_ = 0;
    slots_ = {};
    hasOutcomeSnapshot_ = false;
    lastOutcomeSnapshot_ = {};
    lastOutcomeSnapshot_.tick = -1;
}

RuntimeCooperativeSessionBuildResult
RuntimeCooperativeSessionState::begin(
    const CooperativeScenarioDefinition& scenario,
    const CooperativeScenarioMapValidation& mapValidation,
    RuntimeCooperativeSessionBuildOptions options
) {
    RuntimeCooperativeSessionBuildResult result;
    std::string reason;
    if (!validateContext(scenario, mapValidation, reason)) {
        result.reason = std::move(reason);
        return result;
    }

    RuntimeCooperativeSessionState built;
    built.status_ = RuntimeCooperativeSessionStatus::Running;
    built.scenarioId_ = scenario.identity;
    built.mapId_ = scenario.mapId;
    built.mapIdentity_ = mapValidation.identity;
    built.scenarioFingerprint_ = scenario.fingerprint;
    built.mapFingerprint_ = mapValidation.identityFingerprint;
    built.lastOutcomeSnapshot_.tick = -1;

    std::vector<std::string> owners;
    owners.reserve(runtimeCooperativeSlotCount);
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const CooperativeScenarioSlot& source = scenario.slots[index];
        RuntimeCooperativeSessionSlot& slot = built.slots_[index];
        slot.slotIndex = static_cast<int>(index);
        slot.label = source.label;
        slot.authoredOwnerId = source.authoredOwnerId;
        slot.authoredHouse = source.authoredHouse;
        slot.role = source.role;
        slot.coalitionId = source.coalitionId;
        slot.teamId = source.coalitionId;
        slot.waypointId = source.waypointId;
        slot.start = mapValidation.waypoints[index];
        slot.allowedCountries = source.allowedCountries;

        slot.owner = options.owners[index].empty()
            ? source.authoredHouse
            : std::move(options.owners[index]);
        if (!validOwner(slot.owner)) {
            result.reason = "cooperative runtime owner is invalid";
            return result;
        }
        if (std::any_of(
                owners.begin(),
                owners.end(),
                [&slot](const std::string& owner) {
                    return equalsAsciiIgnoreCase(owner, slot.owner);
                }
            )) {
            result.reason = "cooperative runtime owners must be unique";
            return result;
        }
        owners.push_back(slot.owner);

        const std::string requestedCountry = options.countries[index].empty()
            ? source.defaultCountry
            : options.countries[index];
        const std::optional<std::string> country = canonicalCountry(
            slot.allowedCountries,
            requestedCountry
        );
        if (!country.has_value()) {
            result.reason = "cooperative country violates coalition constraints";
            return result;
        }
        slot.country = *country;
    }

    *this = std::move(built);
    result.accepted = true;
    return result;
}

RuntimeCooperativeOutcomeAdvanceResult
RuntimeCooperativeSessionState::advanceOutcome(
    const RuntimeCooperativeLiveTechnoSnapshot& snapshot
) {
    RuntimeCooperativeOutcomeAdvanceResult result;
    result.before = status_;
    result.after = status_;
    if (status_ != RuntimeCooperativeSessionStatus::Running) {
        result.reason = active()
            ? "cooperative session outcome is already terminal"
            : "cooperative session is inactive";
        return result;
    }
    if (snapshot.tick < 0
        || (hasOutcomeSnapshot_
            && snapshot.tick <= lastOutcomeSnapshot_.tick)) {
        result.reason = "cooperative outcome tick is not monotonic";
        return result;
    }
    for (const int count : snapshot.liveTechnos) {
        if (count < 0 || count > maximumLiveTechnoCount) {
            result.reason = "cooperative live techno count is invalid";
            return result;
        }
    }

    result.playerCoalitionLiveTechnos = snapshot.liveTechnos[0]
        + snapshot.liveTechnos[1];
    result.enemyCoalitionLiveTechnos = snapshot.liveTechnos[2]
        + snapshot.liveTechnos[3];
    hasOutcomeSnapshot_ = true;
    lastOutcomeSnapshot_ = snapshot;
    if (result.playerCoalitionLiveTechnos == 0
        && result.enemyCoalitionLiveTechnos == 0) {
        status_ = RuntimeCooperativeSessionStatus::Draw;
    } else if (result.playerCoalitionLiveTechnos == 0) {
        status_ = RuntimeCooperativeSessionStatus::EnemyCoalitionVictory;
    } else if (result.enemyCoalitionLiveTechnos == 0) {
        status_ = RuntimeCooperativeSessionStatus::PlayerCoalitionVictory;
    }
    result.accepted = true;
    result.changed = result.before != status_;
    result.after = status_;
    return result;
}

RuntimeCooperativeSessionReconcileReport
RuntimeCooperativeSessionState::reconcile(
    const CooperativeScenarioDefinition& scenario,
    const CooperativeScenarioMapValidation& mapValidation
) {
    RuntimeCooperativeSessionReconcileReport report;
    std::string reason;
    if (!validateContext(scenario, mapValidation, reason)) {
        report.reason = std::move(reason);
        return report;
    }
    report.contextValid = true;
    if (!active()) {
        report.reason = "cooperative session is inactive";
        return report;
    }

    bool matches = scenarioId_ == scenario.identity
        && mapId_ == scenario.mapId
        && mapIdentity_ == mapValidation.identity
        && scenarioFingerprint_ == scenario.fingerprint
        && mapFingerprint_ == mapValidation.identityFingerprint;
    for (std::size_t index = 0;
         matches && index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeCooperativeSessionSlot& slot = slots_[index];
        const CooperativeScenarioSlot& expected = scenario.slots[index];
        matches = slot.slotIndex == static_cast<int>(index)
            && slot.label == expected.label
            && slot.authoredOwnerId == expected.authoredOwnerId
            && slot.authoredHouse == expected.authoredHouse
            && slot.role == expected.role
            && slot.coalitionId == expected.coalitionId
            && slot.teamId == expected.coalitionId
            && slot.waypointId == expected.waypointId
            && sameWaypoint(slot.start, mapValidation.waypoints[index])
            && slot.allowedCountries == expected.allowedCountries
            && canonicalCountry(
                expected.allowedCountries,
                slot.country
            ) == std::optional<std::string>(slot.country)
            && validOwner(slot.owner);
    }
    if (!matches) {
        reset();
        report.resetForMapChange = true;
        report.reason = "cooperative scenario or map identity changed";
        return report;
    }
    report.retained = true;
    return report;
}

std::optional<int>
RuntimeCooperativeSessionState::resolveAuthoredSlotAlias(
    std::string_view alias
) const {
    if (!active()) {
        return std::nullopt;
    }
    std::optional<int> resolved;
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeCooperativeSessionSlot& slot = slots_[index];
        if (alias != slot.authoredHouse
            && alias != std::to_string(slot.authoredOwnerId)) {
            continue;
        }
        if (resolved.has_value()
            && *resolved != static_cast<int>(index)) {
            return std::nullopt;
        }
        resolved = static_cast<int>(index);
    }
    return resolved;
}

bool RuntimeCooperativeSessionState::slotsAreAllied(
    int leftSlotIndex,
    int rightSlotIndex
) const {
    if (!active()
        || leftSlotIndex < 0
        || leftSlotIndex >= static_cast<int>(runtimeCooperativeSlotCount)
        || rightSlotIndex < 0
        || rightSlotIndex >= static_cast<int>(runtimeCooperativeSlotCount)) {
        return false;
    }
    return slots_[static_cast<std::size_t>(leftSlotIndex)].teamId
        == slots_[static_cast<std::size_t>(rightSlotIndex)].teamId;
}

RuntimeCooperativeBootstrapPlan
RuntimeCooperativeSessionState::bootstrapPlan() const {
    RuntimeCooperativeBootstrapPlan plan;
    if (!active()) {
        return plan;
    }
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeCooperativeSessionSlot& slot = slots_[index];
        RuntimeCooperativeStartBootstrapInstruction& instruction =
            plan.starts[index];
        instruction.slotIndex = slot.slotIndex;
        instruction.owner = slot.owner;
        instruction.country = slot.country;
        instruction.waypointId = slot.waypointId;
        instruction.start = slot.start;
    }
    return plan;
}

void RuntimeCooperativeSessionState::save(std::ostream& output) const {
    output << "[CooperativeSessionRuntime]\n";
    output << "Version=1\n";
    output << "Status=" << runtimeCooperativeSessionStatusName(status_)
           << "\n";
    output << "ScenarioIdHex=" << hexEncode(scenarioId_) << "\n";
    output << "MapIdHex=" << hexEncode(mapId_) << "\n";
    output << "MapIdentityHex=" << hexEncode(mapIdentity_) << "\n";
    output << "ScenarioFingerprint="
           << cooperativeFingerprintHex(scenarioFingerprint_) << "\n";
    output << "MapFingerprint="
           << cooperativeFingerprintHex(mapFingerprint_) << "\n";
    output << "SlotCount=" << (active() ? runtimeCooperativeSlotCount : 0)
           << "\n";
    output << "HasOutcomeSnapshot=" << (hasOutcomeSnapshot_ ? 1 : 0)
           << "\n";
    output << "LastOutcomeTick=" << lastOutcomeSnapshot_.tick << "\n";
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        output << "LiveTechnos" << static_cast<char>('A' + index) << '='
               << lastOutcomeSnapshot_.liveTechnos[index] << "\n";
    }
    output << '\n';

    if (!active()) {
        return;
    }
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeCooperativeSessionSlot& slot = slots_[index];
        output << "[CooperativeSessionRuntime.Slot." << index << "]\n";
        output << "SlotIndex=" << slot.slotIndex << "\n";
        output << "Label=" << slot.label << "\n";
        output << "AuthoredOwnerId=" << slot.authoredOwnerId << "\n";
        output << "AuthoredHouseHex=" << hexEncode(slot.authoredHouse)
               << "\n";
        output << "Role=" << cooperativeSlotRoleName(slot.role) << "\n";
        output << "CoalitionId=" << slot.coalitionId << "\n";
        output << "TeamId=" << slot.teamId << "\n";
        output << "WaypointId=" << slot.waypointId << "\n";
        output << "StartWaypointId=" << slot.start.waypointId << "\n";
        output << "StartX=" << slot.start.x << "\n";
        output << "StartY=" << slot.start.y << "\n";
        output << "StartCell=" << slot.start.cell << "\n";
        output << "OwnerHex=" << hexEncode(slot.owner) << "\n";
        output << "CountryHex=" << hexEncode(slot.country) << "\n";
        output << "AllowedCountryCount=" << slot.allowedCountries.size()
               << "\n";
        for (std::size_t countryIndex = 0;
             countryIndex < slot.allowedCountries.size();
             ++countryIndex) {
            output << "AllowedCountry." << countryIndex << "Hex="
                   << hexEncode(slot.allowedCountries[countryIndex]) << "\n";
        }
        output << '\n';
    }
}

bool RuntimeCooperativeSessionState::load(
    const IniFile& ini,
    const CooperativeScenarioDefinition& scenario,
    const CooperativeScenarioMapValidation& mapValidation,
    std::string* errorMessage
) {
    const auto fail = [errorMessage](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    std::string contextError;
    if (!validateContext(scenario, mapValidation, contextError)) {
        return fail(contextError);
    }
    if (ini.getSection("CooperativeSessionRuntime") == nullptr) {
        return fail("cooperative session save section is missing");
    }
    const auto value = [&ini](
        const std::string& section,
        const std::string& key
    ) {
        return ini.getValue(section, key, std::string());
    };

    int version = 0;
    int slotCount = 0;
    int hasSnapshot = 0;
    RuntimeCooperativeSessionStatus savedStatus;
    RuntimeCooperativeSessionState loaded;
    if (!parseIntStrict(
            value("CooperativeSessionRuntime", "Version"),
            version
        )
        || version != 1
        || !statusFromName(
            value("CooperativeSessionRuntime", "Status"),
            savedStatus
        )
        || !hexDecode(
            value("CooperativeSessionRuntime", "ScenarioIdHex"),
            loaded.scenarioId_,
            maximumIdentityLength
        )
        || !hexDecode(
            value("CooperativeSessionRuntime", "MapIdHex"),
            loaded.mapId_,
            maximumMapIdLength
        )
        || !hexDecode(
            value("CooperativeSessionRuntime", "MapIdentityHex"),
            loaded.mapIdentity_,
            maximumIdentityLength
        )
        || !parseFingerprint(
            value("CooperativeSessionRuntime", "ScenarioFingerprint"),
            loaded.scenarioFingerprint_
        )
        || !parseFingerprint(
            value("CooperativeSessionRuntime", "MapFingerprint"),
            loaded.mapFingerprint_
        )
        || !parseIntStrict(
            value("CooperativeSessionRuntime", "SlotCount"),
            slotCount
        )
        || !parseIntStrict(
            value("CooperativeSessionRuntime", "HasOutcomeSnapshot"),
            hasSnapshot
        )
        || (hasSnapshot != 0 && hasSnapshot != 1)
        || !parseIntStrict(
            value("CooperativeSessionRuntime", "LastOutcomeTick"),
            loaded.lastOutcomeSnapshot_.tick
        )) {
        return fail("cooperative session save header is invalid");
    }
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        if (!parseIntStrict(
                value(
                    "CooperativeSessionRuntime",
                    std::string("LiveTechnos")
                        + static_cast<char>('A' + index)
                ),
                loaded.lastOutcomeSnapshot_.liveTechnos[index]
            )
            || loaded.lastOutcomeSnapshot_.liveTechnos[index] < 0
            || loaded.lastOutcomeSnapshot_.liveTechnos[index]
                > maximumLiveTechnoCount) {
            return fail("cooperative live techno snapshot is invalid");
        }
    }

    loaded.status_ = savedStatus;
    loaded.hasOutcomeSnapshot_ = hasSnapshot != 0;
    if (savedStatus == RuntimeCooperativeSessionStatus::Inactive) {
        const bool zeroLive = std::all_of(
            loaded.lastOutcomeSnapshot_.liveTechnos.begin(),
            loaded.lastOutcomeSnapshot_.liveTechnos.end(),
            [](int count) { return count == 0; }
        );
        if (slotCount != 0
            || loaded.hasOutcomeSnapshot_
            || loaded.lastOutcomeSnapshot_.tick != -1
            || !zeroLive
            || !loaded.scenarioId_.empty()
            || !loaded.mapId_.empty()
            || !loaded.mapIdentity_.empty()
            || loaded.scenarioFingerprint_ != 0
            || loaded.mapFingerprint_ != 0) {
            return fail("inactive cooperative session save is inconsistent");
        }
        *this = std::move(loaded);
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return true;
    }

    if (slotCount != static_cast<int>(runtimeCooperativeSlotCount)
        || !validBoundedText(loaded.scenarioId_, maximumIdentityLength)
        || !validBoundedText(loaded.mapId_, maximumMapIdLength)
        || !validBoundedText(loaded.mapIdentity_, maximumIdentityLength)
        || loaded.scenarioFingerprint_ == 0
        || loaded.mapFingerprint_ == 0
        || loaded.scenarioId_ != scenario.identity
        || loaded.mapId_ != scenario.mapId
        || loaded.mapIdentity_ != mapValidation.identity
        || loaded.scenarioFingerprint_ != scenario.fingerprint
        || loaded.mapFingerprint_ != mapValidation.identityFingerprint) {
        return fail("cooperative scenario or map fingerprint mismatch");
    }

    std::vector<std::string> owners;
    owners.reserve(runtimeCooperativeSlotCount);
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const std::string section = "CooperativeSessionRuntime.Slot."
            + std::to_string(index);
        if (ini.getSection(section) == nullptr) {
            return fail("cooperative session slot section is missing");
        }
        RuntimeCooperativeSessionSlot& slot = loaded.slots_[index];
        std::string label;
        std::string role;
        int allowedCountryCount = 0;
        if (!parseIntStrict(value(section, "SlotIndex"), slot.slotIndex)
            || !hexDecode(
                value(section, "AuthoredHouseHex"),
                slot.authoredHouse,
                maximumOwnerLength
            )
            || !parseIntStrict(
                value(section, "AuthoredOwnerId"),
                slot.authoredOwnerId
            )
            || (label = value(section, "Label"), label.size() != 1)
            || (role = value(section, "Role"),
                !roleFromName(role, slot.role))
            || !parseIntStrict(
                value(section, "CoalitionId"),
                slot.coalitionId
            )
            || !parseIntStrict(value(section, "TeamId"), slot.teamId)
            || !parseIntStrict(
                value(section, "WaypointId"),
                slot.waypointId
            )
            || !parseIntStrict(
                value(section, "StartWaypointId"),
                slot.start.waypointId
            )
            || !parseIntStrict(value(section, "StartX"), slot.start.x)
            || !parseIntStrict(value(section, "StartY"), slot.start.y)
            || !parseIntStrict(value(section, "StartCell"), slot.start.cell)
            || !hexDecode(
                value(section, "OwnerHex"),
                slot.owner,
                maximumOwnerLength
            )
            || !hexDecode(
                value(section, "CountryHex"),
                slot.country,
                maximumCountryLength
            )
            || !parseIntStrict(
                value(section, "AllowedCountryCount"),
                allowedCountryCount
            )
            || allowedCountryCount < 1
            || static_cast<std::size_t>(allowedCountryCount)
                > maximumAllowedCountries) {
            return fail("cooperative session slot record is invalid");
        }
        slot.label = label[0];
        slot.allowedCountries.reserve(
            static_cast<std::size_t>(allowedCountryCount)
        );
        for (int countryIndex = 0;
             countryIndex < allowedCountryCount;
             ++countryIndex) {
            std::string country;
            if (!hexDecode(
                    value(
                        section,
                        "AllowedCountry." + std::to_string(countryIndex)
                            + "Hex"
                    ),
                    country,
                    maximumCountryLength
                )
                || !validBoundedText(country, maximumCountryLength)) {
                return fail(
                    "cooperative allowed country record is invalid"
                );
            }
            slot.allowedCountries.push_back(std::move(country));
        }

        const CooperativeScenarioSlot& expected = scenario.slots[index];
        if (slot.slotIndex != static_cast<int>(index)
            || slot.label != expected.label
            || slot.authoredOwnerId != expected.authoredOwnerId
            || slot.authoredHouse != expected.authoredHouse
            || slot.role != expected.role
            || slot.coalitionId != expected.coalitionId
            || slot.teamId != expected.coalitionId
            || slot.waypointId != expected.waypointId
            || !sameWaypoint(slot.start, mapValidation.waypoints[index])
            || slot.allowedCountries != expected.allowedCountries
            || canonicalCountry(
                expected.allowedCountries,
                slot.country
            ) != std::optional<std::string>(slot.country)
            || !validOwner(slot.owner)
            || std::any_of(
                owners.begin(),
                owners.end(),
                [&slot](const std::string& owner) {
                    return equalsAsciiIgnoreCase(owner, slot.owner);
                }
            )) {
            return fail("cooperative saved slot violates live scenario");
        }
        owners.push_back(slot.owner);
    }

    if (loaded.hasOutcomeSnapshot_) {
        if (loaded.lastOutcomeSnapshot_.tick < 0
            || !snapshotMatchesStatus(
                loaded.status_,
                loaded.lastOutcomeSnapshot_
            )) {
            return fail("cooperative saved outcome is inconsistent");
        }
    } else {
        const bool zeroLive = std::all_of(
            loaded.lastOutcomeSnapshot_.liveTechnos.begin(),
            loaded.lastOutcomeSnapshot_.liveTechnos.end(),
            [](int count) { return count == 0; }
        );
        if (loaded.status_ != RuntimeCooperativeSessionStatus::Running
            || loaded.lastOutcomeSnapshot_.tick != -1
            || !zeroLive) {
            return fail("cooperative save is missing its outcome snapshot");
        }
    }

    *this = std::move(loaded);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::uint64_t RuntimeCooperativeSessionState::stateHash() const {
    std::ostringstream serialized;
    save(serialized);
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char character : serialized.str()) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool RuntimeCooperativeSessionState::active() const {
    return status_ != RuntimeCooperativeSessionStatus::Inactive;
}

RuntimeCooperativeSessionStatus
RuntimeCooperativeSessionState::status() const {
    return status_;
}

const std::string& RuntimeCooperativeSessionState::scenarioId() const {
    return scenarioId_;
}

const std::string& RuntimeCooperativeSessionState::mapId() const {
    return mapId_;
}

std::uint64_t
RuntimeCooperativeSessionState::scenarioFingerprint() const {
    return scenarioFingerprint_;
}

std::uint64_t RuntimeCooperativeSessionState::mapFingerprint() const {
    return mapFingerprint_;
}

const std::array<RuntimeCooperativeSessionSlot,
                 runtimeCooperativeSlotCount>&
RuntimeCooperativeSessionState::slots() const {
    return slots_;
}

bool RuntimeCooperativeSessionState::hasOutcomeSnapshot() const {
    return hasOutcomeSnapshot_;
}

const RuntimeCooperativeLiveTechnoSnapshot&
RuntimeCooperativeSessionState::lastOutcomeSnapshot() const {
    return lastOutcomeSnapshot_;
}
