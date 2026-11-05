#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

RuntimeCampaignDataSourceOptions runtimeCampaignDataSourceOptions(
    const RuntimeConfig& config) {
    RuntimeCampaignDataSourceOptions options;
    options.resourceRootPath = config.resourceRootPath;
    options.localDataRootPath = config.localDataRootPath;
    options.modRootPath = config.modRootPath;
    options.sourceId = config.campaignSourceId;
    return options;
}

int runtimeCampaignClockSeconds(const std::string& value) {
    const std::string trimmed = trimAsciiLocal(value);
    std::stringstream stream(trimmed);
    std::array<int, 3> parts{{0, 0, 0}};
    std::string token;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (!std::getline(stream, token, ':')
            || !parseRuntimeInt(token, parts[index])
            || parts[index] < 0) {
            return -1;
        }
    }
    if (std::getline(stream, token, ':')
        || parts[1] >= 60 || parts[2] >= 60) {
        return -1;
    }
    const long long seconds = static_cast<long long>(parts[0]) * 3600LL
        + static_cast<long long>(parts[1]) * 60LL
        + static_cast<long long>(parts[2]);
    return seconds > std::numeric_limits<int>::max()
        ? -1
        : static_cast<int>(seconds);
}

void initializeRuntimeCampaignScore(RuntimeProductionHostState& state) {
    state.resultPresentation = {};
    state.campaignScore = RuntimeCampaignScoreState{};
    state.matchScores.clear();
    state.campaignScore.missionStartTick = std::max(0, state.tick);
    if (!state.campaignMode) return;

    IniFile mapIni;
    if (trimAsciiLocal(state.config.defaultMapPath).empty()
        || !mapIni.loadFromFile(state.config.defaultMapPath)) {
        return;
    }
    const std::string difficulty = toUpperAsciiLocal(trimAsciiLocal(
        state.config.campaignDifficulty
    ));
    const char* key = difficulty == "EASY"
        ? "ParTimeEasy"
        : ((difficulty == "HARD" || difficulty == "BRUTAL")
            ? "ParTimeHard"
            : "ParTimeMedium");
    std::string authoredParTime = mapIni.getValue("Ranking", key, "");
    state.campaignScore.parTimeField = std::string("Ranking.") + key;
    if (trimAsciiLocal(authoredParTime).empty()) {
        authoredParTime = mapIni.getValue("Ranking", "ParTime", "");
        state.campaignScore.parTimeField = "Ranking.ParTime";
    }
    if (trimAsciiLocal(authoredParTime).empty()) {
        authoredParTime = mapIni.getValue("Basic", key, "");
        state.campaignScore.parTimeField = std::string("Basic.") + key;
    }
    if (trimAsciiLocal(authoredParTime).empty()) {
        authoredParTime = mapIni.getValue("Basic", "ParTime", "");
        state.campaignScore.parTimeField = "Basic.ParTime";
    }
    state.campaignScore.parTimeText = trimAsciiLocal(authoredParTime);
    state.campaignScore.underParTitleKey = trimAsciiLocal(
        mapIni.getValue("Ranking", "UnderParTitle", "")
    );
    state.campaignScore.underParMessageKey = trimAsciiLocal(
        mapIni.getValue("Ranking", "UnderParMessage", "")
    );
    state.campaignScore.overParTitleKey = trimAsciiLocal(
        mapIni.getValue("Ranking", "OverParTitle", "")
    );
    state.campaignScore.overParMessageKey = trimAsciiLocal(
        mapIni.getValue("Ranking", "OverParMessage", "")
    );
    const int seconds = runtimeCampaignClockSeconds(authoredParTime);
    if (seconds < 0) {
        state.campaignScore.parTimeField.clear();
        return;
    }
    const long long ticks = static_cast<long long>(seconds)
        * std::max(1, state.config.fixedTickHz);
    state.campaignScore.parTicks = static_cast<int>(std::min<long long>(
        std::numeric_limits<int>::max(),
        ticks
    ));
}

int runtimeCampaignScoreElapsedTicks(const RuntimeCampaignScoreState& score) {
    const int endTick = score.finishedTick >= 0
        ? score.finishedTick
        : score.missionStartTick;
    return std::max(0, endTick - score.missionStartTick);
}

void finalizeRuntimeCampaignScore(RuntimeProductionHostState& state, bool victory) {
    RuntimeCampaignScoreState& score = state.campaignScore;
    if (!score.captured) {
        score.finishedTick = std::max(score.missionStartTick, state.tick);
        const int elapsedTicks = runtimeCampaignScoreElapsedTicks(score);
        // Retail selects OverPar only when elapsed is strictly greater than
        // ParTime. Equality therefore uses the authored UnderPar result.
        score.finishedUnderPar = score.parTicks > 0
            && elapsedTicks <= score.parTicks;

        // Compatibility metric retained for existing saves/diagnostics.
        // These weights are not claimed to be Westwood's campaign formula;
        // the authored under/over-Par result above is the rank authority.
        long long timeBonus = 0;
        if (score.parTicks > 0 && elapsedTicks < score.parTicks) {
            timeBonus = static_cast<long long>(score.parTicks - elapsedTicks)
                * 2000LL / score.parTicks;
        }
        const long long enemyPoints =
            static_cast<long long>(score.enemyUnitsDestroyed) * 125LL
            + static_cast<long long>(score.enemyStructuresDestroyed) * 350LL;
        const long long lossPenalty =
            static_cast<long long>(score.friendlyUnitsLost) * 60LL
            + static_cast<long long>(score.friendlyStructuresLost) * 180LL;
        const long long total = (victory ? 1000LL : 0LL)
            + enemyPoints + timeBonus - lossPenalty;
        score.finalScore = static_cast<int>(std::clamp<long long>(
            total,
            0LL,
            std::numeric_limits<int>::max()
        ));
        score.captured = true;
    }
    score.presentationStartedAtMs = runtimeUiClockNow(state);
}

bool runtimeSkirmishModeMatches(const RuntimeGameSessionState& state,
    const char* mode, const char* file) {
    if (state.campaignMode) return false;
    const auto matchesFile = [file](const std::string& path) {
        return toUpperAsciiLocal(std::filesystem::path(path).filename().string()) == file;
    };
    return toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode)) == mode
        || matchesFile(state.config.gameModeRulesOverride)
        || matchesFile(state.skirmishModeRulesPath);
}

std::string runtimeSkirmishOwnerForSlot(std::size_t slotIndex) {
    return "SkirmishPlayer" + std::to_string(slotIndex + 1);
}

std::string runtimeNormalizeSkirmishTeam(const std::string& configuredTeam) {
    const std::string team =
        toUpperAsciiLocal(trimAsciiLocal(configuredTeam));
    if (team.empty()
        || team == "-"
        || team == "0"
        || team == "NONE"
        || team == "NO TEAM") {
        return {};
    }
    return team;
}

std::string runtimeResolvedSkirmishCountry(
    const std::string& configured,
    std::size_t slotIndex) {
    const std::string country = trimAsciiLocal(configured);
    if (!country.empty() && toUpperAsciiLocal(country) != "RANDOM") {
        return country;
    }
    constexpr std::array<const char*, 10> countries = {{
        "Americans",
        "Russians",
        "Alliance",
        "Confederation",
        "French",
        "Africans",
        "Germans",
        "Arabs",
        "British",
        "YuriCountry"
    }};
    return countries[slotIndex % countries.size()];
}

void initializeRuntimeSkirmishPlayerIdentities(
    RuntimeGameSessionState& state,
    const std::string& fallbackCountry) {
    state.skirmishCountryByOwner.clear();
    state.skirmishTeamByOwner.clear();
    state.skirmishDifficultyByOwner.clear();

    std::optional<std::size_t> localSlotIndex;
    std::optional<std::size_t> firstHumanSlotIndex;
    std::optional<std::size_t> firstEnabledSlotIndex;
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled) {
            continue;
        }
        if (!firstEnabledSlotIndex.has_value()) {
            firstEnabledSlotIndex = slotIndex;
        }
        if (slot.human && !firstHumanSlotIndex.has_value()) {
            firstHumanSlotIndex = slotIndex;
        }
        if (slot.human && slot.local && !localSlotIndex.has_value()) {
            localSlotIndex = slotIndex;
        }

        const std::string owner = runtimeSkirmishOwnerForSlot(slotIndex);
        std::string configuredCountry = trimAsciiLocal(slot.country);
        if (configuredCountry.empty()) {
            configuredCountry = fallbackCountry;
        }
        const std::string ownerKey = toLowerAsciiLocal(owner);
        state.skirmishCountryByOwner[ownerKey] =
            runtimeResolvedSkirmishCountry(configuredCountry, slotIndex);
        state.skirmishTeamByOwner[ownerKey] =
            runtimeNormalizeSkirmishTeam(slot.team);
        state.skirmishDifficultyByOwner[ownerKey] =
            slot.human
                ? "Medium"
                : trimAsciiLocal(slot.difficulty).empty()
                ? "Medium"
                : trimAsciiLocal(slot.difficulty);
    }

    if (!localSlotIndex.has_value()) {
        localSlotIndex = firstHumanSlotIndex.has_value()
            ? firstHumanSlotIndex
            : firstEnabledSlotIndex;
    }
    if (localSlotIndex.has_value()) {
        state.economy.owner =
            runtimeSkirmishOwnerForSlot(*localSlotIndex);
    }
}

bool runtimeComputerAiEnabledForSession(const RuntimeGameSessionState& state) {
    // This lobby switch is intentionally offline-only. Network matches keep
    // computer decisions enabled on every peer until the option is promoted
    // into the deterministic multiplayer setup protocol.
    return state.config.multiplayerActive || state.config.computerAiEnabled;
}

bool runtimeComputerAiPausedForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    if (runtimeComputerAiEnabledForSession(state)) {
        return false;
    }
    const std::string wanted = runtimeCanonicalHumanHouseOwner(owner);
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (slot.enabled
            && !slot.human
            && runtimeCanonicalHumanHouseOwner(
                runtimeSkirmishOwnerForSlot(slotIndex)
            ) == wanted) {
            return true;
        }
    }
    return false;
}

bool runtimeIsCooperativeMode(const RuntimeConfig& config) {
    return toUpperAsciiLocal(trimAsciiLocal(config.gameMode))
        == "COOPERATIVE";
}

std::filesystem::path runtimeCooperativeScenarioPath(
    const RuntimeConfig& config) {
    const std::filesystem::path localDataRoot(config.localDataRootPath);
    const std::filesystem::path resourceRoot(config.resourceRootPath);
    const auto exports = localDataRoot / "resources" / "mix_exports";
    // Keep the same override precedence as the multiplayer room projection.
    const std::array<std::filesystem::path, 7> candidates = {{
        localDataRoot / "coopcampmd.ini",
        exports / "expandmd01" / "coopcampmd.ini",
        exports / "ra2md" / "localmd" / "coopcampmd.ini",
        exports / "ra2md" / "coopcampmd.ini",
        resourceRoot / "GameMD" / "coopcampmd.ini",
        resourceRoot / "Game" / "coopcampmd.ini",
        resourceRoot / "coopcampmd.ini"
    }};
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code errorCode;
        if (std::filesystem::is_regular_file(candidate, errorCode)) {
            return candidate;
        }
    }
    return {};
}

bool initializeRuntimeCooperativeSession(RuntimeProductionHostState& state) {
    state.cooperativeSession.reset();
    state.cooperativeSessionStatus = "inactive";
    if (!runtimeIsCooperativeMode(state.config)) {
        return true;
    }

    const auto fail = [&state](std::string reason) {
        state.cooperativeSession.reset();
        state.cooperativeSessionStatus = std::move(reason);
        return false;
    };
    if (state.config.playerSlots.size() < runtimeCooperativeSlotCount) {
        return fail("cooperative runtime requires four A-D player slots");
    }
    for (std::size_t index = 0;
         index < state.config.playerSlots.size();
         ++index) {
        const RuntimeConfig::PlayerSlot& slot = state.config.playerSlots[index];
        if (index < runtimeCooperativeSlotCount) {
            if (!slot.enabled
                || slot.human != (state.config.multiplayerActive ? index < 2U : index == 0U)
                || slot.startPosition != static_cast<int>(index) + 1) {
                return fail("cooperative runtime A-D slot binding is invalid");
            }
        } else if (slot.enabled) {
            return fail("cooperative runtime accepts only four A-D player slots");
        }
    }

    const std::filesystem::path scenarioPath =
        runtimeCooperativeScenarioPath(state.config);
    IniFile scenarioIni;
    CooperativeScenarioDatabase database;
    if (scenarioPath.empty()
        || !scenarioIni.loadFromFile(scenarioPath.generic_string())
        || !database.loadFromIni(scenarioIni)) {
        return fail("cooperative scenario database is unavailable or invalid");
    }

    IniFile mapIni;
    MapDatabase map;
    if (trimAsciiLocal(state.config.defaultMapPath).empty()
        || !mapIni.loadFromFile(state.config.defaultMapPath)
        || !map.loadFromIni(mapIni)) {
        return fail("cooperative map data is unavailable or invalid");
    }
    const std::string mapId =
        std::filesystem::path(state.config.defaultMapPath).stem().string();
    const CooperativeScenarioDefinition* scenario =
        database.findByMapId(mapId);
    if (scenario == nullptr) {
        return fail("active map is not an authored cooperative scenario");
    }
    if (state.config.multiplayerActive
        && state.config.multiplayerCooperativeScenarioFingerprint != scenario->fingerprint)
        return fail("cooperative room mission differs from the local original scenario");
    const CooperativeScenarioMapValidation validation = database.validateMap(
        *scenario,
        map,
        mapIni
    );
    if (!validation.valid) {
        return fail("active cooperative map failed runtime validation");
    }

    RuntimeCooperativeSessionBuildOptions options;
    for (std::size_t index = 0;
         index < runtimeCooperativeSlotCount;
         ++index) {
        const RuntimeConfig::PlayerSlot& slot = state.config.playerSlots[index];
        const std::string country = trimAsciiLocal(slot.country);
        if (country.empty() || toUpperAsciiLocal(country) == "RANDOM") {
            return fail("cooperative runtime country binding is unresolved");
        }
        options.owners[index] = runtimeSkirmishOwnerForSlot(index);
        options.countries[index] = country;
    }
    const RuntimeCooperativeSessionBuildResult result =
        state.cooperativeSession.begin(*scenario, validation, std::move(options));
    if (!result.accepted) {
        return fail(
            result.reason.empty()
                ? "cooperative session initialization failed"
                : result.reason
        );
    }
    state.cooperativeSessionStatus = "running " + scenario->mapId;
    return true;
}

std::optional<int> runtimeCooperativeSlotForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    if (!state.cooperativeSession.active()) {
        return std::nullopt;
    }
    if (const std::optional<int> authored =
            state.cooperativeSession.resolveAuthoredSlotAlias(owner)) {
        return authored;
    }

    const std::string ownerKey =
        toLowerAsciiLocal(trimAsciiLocal(owner));
    if (ownerKey.empty()) {
        return std::nullopt;
    }
    for (const RuntimeCooperativeSessionSlot& slot :
            state.cooperativeSession.slots()) {
        if (toLowerAsciiLocal(trimAsciiLocal(slot.owner)) == ownerKey) {
            return slot.slotIndex;
        }
    }

    // Cooperative TeamTypes may use a country identifier directly instead
    // of an A-D owner alias. The scenario validator guarantees that player
    // and enemy country constraints do not overlap, so such an owner still
    // resolves to one unambiguous coalition (not necessarily one slot).
    std::optional<int> countrySlot;
    for (const RuntimeCooperativeSessionSlot& slot :
            state.cooperativeSession.slots()) {
        const bool allowedCountry = std::any_of(
            slot.allowedCountries.begin(),
            slot.allowedCountries.end(),
            [&ownerKey](const std::string& country) {
                return toLowerAsciiLocal(trimAsciiLocal(country)) == ownerKey;
            }
        );
        if (!allowedCountry) {
            continue;
        }
        if (countrySlot.has_value()
            && !state.cooperativeSession.slotsAreAllied(
                *countrySlot,
                slot.slotIndex
            )) {
            return std::nullopt;
        }
        countrySlot = slot.slotIndex;
    }
    return countrySlot;
}

void recordRuntimeCampaignObjectDestruction(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& sourceOwner) {
    RuntimeCampaignScoreState& score = state.campaignScore;
    if (!state.campaignMode || score.captured
        || runtimeOwnerIsNeutralForAutoTarget(object.owner)) {
        return;
    }
    const bool structure = object.kind == WorldObjectKind::Structure;
    if (runtimeOwnersAreAllied(state, state.economy.owner, object.owner)) {
        if (structure) {
            ++score.friendlyStructuresLost;
        } else {
            ++score.friendlyUnitsLost;
        }
        return;
    }
    const bool hostile = runtimeOwnersAreHostileForAutoTarget(
        state,
        state.economy.owner,
        object.owner
    );
    const bool creditedSource = trimAsciiLocal(sourceOwner).empty()
        || runtimeOwnersAreAllied(state, state.economy.owner, sourceOwner);
    if (!hostile || !creditedSource) return;
    if (structure) {
        ++score.enemyStructuresDestroyed;
    } else {
        ++score.enemyUnitsDestroyed;
    }
}

bool parseRuntimeAiTriggerConditionPayload(
    const std::string& value,
    int& number,
    int& comparator) {
    const std::string payload = trimAsciiLocal(value);
    if (payload.size() != 64) {
        return false;
    }
    for (char ch : payload) {
        if (runtimeHexNibble(ch) < 0) {
            return false;
        }
    }
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const int high = runtimeHexNibble(payload[index * 2]);
        const int low = runtimeHexNibble(payload[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    const auto readLittleEndianInt = [&bytes](std::size_t offset) {
        std::uint32_t encoded = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            encoded |= static_cast<std::uint32_t>(bytes[offset + byte])
                << (byte * 8);
        }
        return static_cast<int>(static_cast<std::int32_t>(encoded));
    };
    number = readLittleEndianInt(0);
    comparator = readLittleEndianInt(4);
    return comparator >= 0 && comparator <= 5;
}

RuntimeCampaignAiTriggerDefinition parseRuntimeCampaignAiTrigger(
    const std::string& id,
    const std::string& value) {
    RuntimeCampaignAiTriggerDefinition trigger;
    trigger.id = trimAsciiLocal(id);
    const std::vector<std::string> tokens = splitRuntimeTokenList(value);
    if (tokens.size() < 18 || trigger.id.empty()) {
        return trigger;
    }
    trigger.name = trimAsciiLocal(tokens[0]);
    trigger.primaryTeamId = trimAsciiLocal(tokens[1]);
    trigger.owner = trimAsciiLocal(tokens[2]);
    trigger.techLevel = parseRuntimeIntOrDefault(tokens[3], -1);
    trigger.conditionType = parseRuntimeIntOrDefault(tokens[4], -1);
    trigger.conditionObject = trimAsciiLocal(tokens[5]);
    if (!parseRuntimeAiTriggerConditionPayload(
            tokens[6],
            trigger.conditionNumber,
            trigger.conditionComparator
        )) {
        return trigger;
    }
    trigger.currentWeight = parseRuntimeDoubleOrDefault(tokens[7], 0.0);
    trigger.minimumWeight = parseRuntimeDoubleOrDefault(tokens[8], 0.0);
    trigger.maximumWeight = parseRuntimeDoubleOrDefault(tokens[9], 0.0);
    if (trigger.minimumWeight > trigger.maximumWeight) {
        std::swap(trigger.minimumWeight, trigger.maximumWeight);
    }
    trigger.currentWeight = std::clamp(
        trigger.currentWeight,
        trigger.minimumWeight,
        trigger.maximumWeight
    );
    trigger.availableInSkirmish = parseRuntimeBool(tokens[10]);
    trigger.baseDefense = parseRuntimeBool(tokens[11]);
    trigger.side = parseRuntimeIntOrDefault(tokens[12], -1);
    trigger.scope = parseRuntimeIntOrDefault(tokens[13], 0);
    trigger.secondaryTeamId = trimAsciiLocal(tokens[14]);
    trigger.difficultyEnabled = {{
        parseRuntimeBool(tokens[15]),
        parseRuntimeBool(tokens[16]),
        parseRuntimeBool(tokens[17])
    }};
    const std::string primary = toLowerAsciiLocal(trigger.primaryTeamId);
    trigger.valid = !primary.empty()
        && primary != "<none>"
        && trigger.techLevel >= 0
        && trigger.conditionType >= -1
        && trigger.conditionType <= 7
        && std::isfinite(trigger.currentWeight)
        && std::isfinite(trigger.minimumWeight)
        && std::isfinite(trigger.maximumWeight);
    return trigger;
}

void loadRuntimeComputerTacticTriggers(RuntimeGameSessionState& state,
    const IniFile* globalAi, const IniFile& mapIni) {
    auto& catalog = state.triggers.computerAiTriggers;
    catalog.clear();
    if (state.campaignMode) return;
    const auto merge = [&](const IniFile& ini) {
        const auto* definitions = ini.getSection("AITriggerTypes");
        if (definitions == nullptr) return;
        for (const auto& [id, value] : *definitions) {
            auto definition = parseRuntimeCampaignAiTrigger(id, value);
            if (const auto* enabled = ini.getSection("AITriggerTypesEnable"))
                for (const auto& [enabledId, enabledValue] : *enabled)
                    if (toLowerAsciiLocal(enabledId) == toLowerAsciiLocal(id))
                        definition.enabled = parseRuntimeBool(enabledValue);
            // The twelfth CSV field is reserved. Base defense is field 14,
            // as written by the original AITriggerTypeClass save routine.
            const auto fields = splitRuntimeTokenList(value);
            definition.baseDefense = fields.size() >= 14 && parseRuntimeBool(fields[13]);
            const auto previous = std::find_if(catalog.begin(),
                catalog.end(), [&](const auto& entry) {
                    return toLowerAsciiLocal(entry.id) == toLowerAsciiLocal(id);
                });
            if (previous == catalog.end())
                catalog.push_back(std::move(definition));
            else *previous = std::move(definition);
        }
    };
    if (globalAi != nullptr && !parseRuntimeBool(
        mapIni.getValue("Basic", "IgnoreGlobalAITriggers", "no"))) merge(*globalAi);
    merge(mapIni);
    if (const auto* enabled = mapIni.getSection("AITriggerTypesEnable")) {
        for (auto& definition : catalog)
            for (const auto& [id, value] : *enabled)
                if (toLowerAsciiLocal(id) == toLowerAsciiLocal(definition.id))
                    definition.enabled = parseRuntimeBool(value);
    }
    std::sort(catalog.begin(), catalog.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; });
}

std::map<std::string, std::vector<RuntimeTriggerEvent>>
parseRuntimeEventsByTrigger(const IniFile& ini) {
    std::map<std::string, std::vector<RuntimeTriggerEvent>> eventsByTrigger;
    const IniFile::Section* section = ini.getSection("Events");
    if (section == nullptr) {
        return eventsByTrigger;
    }

    for (const auto& [triggerId, value] : *section) {
        const std::vector<std::string> tokens = splitRuntimeTokenList(value);
        std::vector<RuntimeTriggerEvent> events;
        if (tokens.empty()) {
            eventsByTrigger[triggerId] = events;
            continue;
        }

        const int eventCount = parseRuntimeIntOrDefault(tokens.front(), 0);
        std::size_t offset = 1;
        for (int index = 0; index < eventCount && offset < tokens.size(); ++index) {
            RuntimeTriggerEvent event;
            event.type = parseRuntimeIntOrDefault(tokens[offset], 0);
            event.authoredOrdinal = index;
            // The second encoded field is the number-of-parameters marker.
            // Stock events normally carry one parameter in a three-field
            // record; YR's two-parameter events use a four-field record.
            const int parameterCount =
                offset + 1 < tokens.size()
                    ? parseRuntimeIntOrDefault(tokens[offset + 1], 0)
                    : 0;
            const std::size_t width = parameterCount == 2 ? 4U : 3U;
            for (std::size_t field = 0;
                field < width && offset + field < tokens.size();
                ++field) {
                event.arguments.push_back(tokens[offset + field]);
            }
            events.push_back(std::move(event));
            offset += width;
        }
        eventsByTrigger[triggerId] = std::move(events);
    }
    return eventsByTrigger;
}

std::map<std::string, std::vector<RuntimeTriggerAction>>
parseRuntimeActionsByTrigger(const IniFile& ini) {
    std::map<std::string, std::vector<RuntimeTriggerAction>> actionsByTrigger;
    const IniFile::Section* section = ini.getSection("Actions");
    if (section == nullptr) {
        return actionsByTrigger;
    }

    for (const auto& [triggerId, value] : *section) {
        const std::vector<std::string> tokens = splitRuntimeTokenList(value);
        std::vector<RuntimeTriggerAction> actions;
        if (tokens.empty()) {
            actionsByTrigger[triggerId] = actions;
            continue;
        }

        const int actionCount = parseRuntimeIntOrDefault(tokens.front(), 0);
        std::size_t offset = 1;
        for (int index = 0; index < actionCount && offset < tokens.size(); ++index) {
            RuntimeTriggerAction action;
            action.type = parseRuntimeIntOrDefault(tokens[offset], 0);
            action.authoredOrdinal = index;
            if (offset + 2 < tokens.size()) {
                action.targetId = tokens[offset + 2];
            }
            for (std::size_t field = 0;
                field < 8 && offset + field < tokens.size();
                ++field) {
                action.arguments.push_back(tokens[offset + field]);
            }
            actions.push_back(action);
            offset += 8;
        }

        actionsByTrigger[triggerId] = actions;
    }

    return actionsByTrigger;
}

std::vector<RuntimeTriggerDefinition> parseRuntimeTriggers(
    const IniFile& ini,
    const std::map<std::string, std::vector<RuntimeTriggerEvent>>& eventsByTrigger,
    const std::map<std::string, std::vector<RuntimeTriggerAction>>& actionsByTrigger) {
    std::vector<RuntimeTriggerDefinition> triggers;
    const IniFile::Section* section = ini.getSection("Triggers");
    if (section == nullptr) {
        return triggers;
    }

    triggers.reserve(section->size());
    for (const auto& [id, value] : *section) {
        const std::vector<std::string> tokens = splitRuntimeTokenList(value);
        RuntimeTriggerDefinition trigger;
        trigger.id = id;
        if (!tokens.empty()) {
            trigger.house = tokens[0];
        }
        if (tokens.size() > 1) {
            trigger.linkedTriggerId = tokens[1];
        }
        if (tokens.size() > 2) {
            trigger.name = tokens[2];
        }
        // RA2/YR trigger fields are:
        //   A=disabled, B/C/D=difficulty flags, E=legacy/unused here.
        // Repetition comes from the associated [Tags] entry.
        if (tokens.size() > 4) {
            trigger.difficultyEnabled[0] =
                parseRuntimeIntOrDefault(tokens[4], 1) != 0;
        }
        if (tokens.size() > 5) {
            trigger.difficultyEnabled[1] =
                parseRuntimeIntOrDefault(tokens[5], 1) != 0;
        }
        if (tokens.size() > 6) {
            trigger.difficultyEnabled[2] =
                parseRuntimeIntOrDefault(tokens[6], 1) != 0;
        }
        if (tokens.size() > 3) {
            trigger.enabled = parseRuntimeIntOrDefault(tokens[3], 0) == 0;
        }
        if (const auto eventsIt = eventsByTrigger.find(id);
            eventsIt != eventsByTrigger.end()) {
            trigger.events = eventsIt->second;
        }
        if (const auto actionsIt = actionsByTrigger.find(id);
            actionsIt != actionsByTrigger.end()) {
            trigger.actions = actionsIt->second;
        }
        triggers.push_back(std::move(trigger));
    }

    return triggers;
}

int runtimeComputerMiningDifficultyIndex(const std::string& difficulty) {
    const auto value = toUpperAsciiLocal(trimAsciiLocal(difficulty));
    return value == "EASY" ? 2 : (value == "HARD" || value == "BRUTAL") ? 0 : 1;
}

bool runtimeComputerTacticReservesSuperWeapon(const RuntimeGameSessionState& state,
    const std::string& owner, const SuperWeaponRule* weapon) {
    if (state.campaignMode || !weapon) return false;
    const auto identity = toLowerAsciiLocal(weapon->id + " " + weapon->type);
    const bool chrono = identity.find("chrono") != std::string::npos;
    const bool iron = identity.find("ironcurtain") != std::string::npos;
    if (!chrono && !iron) return false;
    const auto house = state.triggers.computerTacticsByOwner.find(toLowerAsciiLocal(trimAsciiLocal(owner)));
    if (house == state.triggers.computerTacticsByOwner.end()) return false;
    for (const auto& [id, team] : house->second.teams) {
        if (team.missionFinished) continue;
        const auto type = state.triggers.teams.find(team.teamTypeId);
        if (type == state.triggers.teams.end()) continue;
        const auto script = state.triggers.scripts.find(type->second.scriptId);
        if (script == state.triggers.scripts.end()) continue;
        const auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
        const std::size_t start = mission == state.triggers.teamMissions.end() ? 0 : mission->second.stepIndex;
        for (std::size_t step = start; step < script->second.steps.size(); ++step) {
            const int action = script->second.steps[step].action;
            if ((iron && action == 55) || (chrono && (action == 56 || action == 57))) return true;
        }
    }
    return false;
}

std::string runtimeTriggerSuperWeaponId(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    int actionType,
    const std::string& targetId) {
    if (!targetId.empty()
        && state.rules.findSuperWeaponType(targetId) != nullptr) {
        return state.rules.findSuperWeaponType(targetId)->id;
    }
    const std::string actionTypeKey = std::to_string(actionType);
    const std::string wanted = actionType == 95
        ? "nuke"
        : actionType == 102
            ? "lightning"
            : actionType == 109 ? "iron" : "";
    for (const SuperWeaponSidebarSnapshot& snapshot :
            state.superWeapons.sidebarSnapshots()) {
        if (toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                != toLowerAsciiLocal(trimAsciiLocal(owner))) {
            continue;
        }
        if (wanted.empty()
            || containsAsciiCaseInsensitive(snapshot.superWeaponId, wanted)
            || containsAsciiCaseInsensitive(snapshot.name, wanted)) {
            return snapshot.superWeaponId;
        }
    }
    for (const SuperWeaponRule& rule : state.rules.getSuperWeaponTypes()) {
        const std::string ruleKey = toLowerAsciiLocal(
            rule.id + " " + rule.type + " " + rule.action
        );
        if (wanted.empty() || ruleKey.find(wanted) != std::string::npos) {
            return rule.id;
        }
    }
    (void)actionTypeKey;
    return {};
}

bool runtimeCampaignPreferredTargetMatchesQuarry(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    int quarry) {
    const int normalized = std::clamp(quarry, 0, 9);
    if (normalized <= 1 || normalized == 8) {
        return true;
    }
    const BuildingType* building = object.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, object.type)
        : nullptr;
    const UnitType* unit = object.kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, object)
        : nullptr;
    switch (normalized) {
    case 2: // buildings
        return object.kind == WorldObjectKind::Structure;
    case 3: // harvesters and refineries
        return (building != nullptr && building->refinery)
            || (unit != nullptr
                && (unit->harvester || unit->resourceGatherer));
    case 4: // infantry
        return object.kind == WorldObjectKind::Infantry;
    case 5: // combat vehicles
        return unit != nullptr && !unit->airportBound;
    case 6: // factories
        return building != nullptr
            && (!trimAsciiLocal(building->factory).empty()
                || building->constructionYard
                || building->weaponsFactory
                || building->refinery);
    case 7: // base defenses
        return building != nullptr && building->isBaseDefense;
    case 9: // power facilities
        return building != nullptr && building->power > 0;
    default:
        return true;
    }
}

std::optional<SuperWeaponTarget> runtimeTriggerSuperWeaponTarget(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    const auto preferred =
        state.triggers.campaignPreferredSuperWeaponTargetByOwner.find(
            toLowerAsciiLocal(trimAsciiLocal(owner))
        );
    if (preferred
        != state.triggers.campaignPreferredSuperWeaponTargetByOwner.end()) {
        return SuperWeaponTarget{
            {preferred->second.x, preferred->second.y},
            -1
        };
    }
    int preferredQuarry = 0;
    std::string ownerKey = toLowerAsciiLocal(trimAsciiLocal(
        runtimeCampaignHouseName(state, owner)
    ));
    if (ownerKey.empty()) {
        ownerKey = toLowerAsciiLocal(trimAsciiLocal(owner));
    }
    const auto production =
        state.triggers.campaignHouseProduction.find(ownerKey);
    if (production != state.triggers.campaignHouseProduction.end()) {
        preferredQuarry = production->second.preferredTargetQuarry;
    }

    const WorldObjectInstance* best = nullptr;
    int bestScore = std::numeric_limits<int>::min();
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner.empty()
            || runtimeOwnersAreAllied(state, owner, object.owner)) {
            continue;
        }
        // PreferredTarget is a preference rather than a hard filter: if the
        // authored quarry has vanished, a valid enemy remains selectable.
        const int preferredScore = preferredQuarry > 0
                && runtimeCampaignPreferredTargetMatchesQuarry(
                    state,
                    object,
                    preferredQuarry
                )
            ? 1000000
            : 0;
        const int score = preferredScore
            + (object.kind == WorldObjectKind::Structure ? 1000 : 0)
            + std::max(0, object.health);
        if (score > bestScore) {
            bestScore = score;
            best = &object;
        }
    }
    if (best != nullptr) {
        return SuperWeaponTarget{{best->x, best->y}, best->instanceId};
    }
    const int width = state.report.map.width > 0
        ? state.report.map.width
        : state.world.getSummary().width;
    const int height = state.report.map.height > 0
        ? state.report.map.height
        : state.world.getSummary().height;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return SuperWeaponTarget{{width / 2, height / 2}, -1};
}

bool fireRuntimeTriggerSuperWeapon(
    RuntimeProductionHostState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action) {
    if (!state.rulesLoaded
        || !runtimeIsSuperWeaponTriggerAction(action.type)) {
        return false;
    }
    ensureRuntimeSuperWeaponsInitialized(state);
    const std::string owner = trigger.house.empty()
        ? state.economy.owner
        : trigger.house;
    const std::string superWeaponId = runtimeTriggerSuperWeaponId(
        state,
        owner,
        action.type,
        action.targetId
    );
    if (superWeaponId.empty()) {
        return false;
    }
    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    const std::vector<std::array<int, 4>> parasiteHostPoses =
        runtimeParasiteLinkedHostPoses(state);
    SuperWeaponTarget target = runtimeTriggerSuperWeaponTarget(
        state,
        owner
    ).value_or(SuperWeaponTarget{});
    if (action.type == 95 || action.type == 102 || action.type == 109) {
        if (const std::optional<RuntimeCellPoint> authoredTarget =
                runtimeWaypointCell(
                    state,
                    runtimeCampaignWaypointId(action, 7)
                )) {
            target = {{authoredTarget->x, authoredTarget->y}, -1};
        }
    }
    const std::string actionName = std::to_string(action.type);
    bool applied = false;
    if (action.type == 33 || action.type == 34) {
        applied = state.superWeapons.applyTriggerAction(
            owner,
            superWeaponId,
            actionName,
            target,
            context
        );
    } else {
        if (state.superWeapons.stateOf(owner, superWeaponId)
            == SuperWeaponRuntimeState::Unavailable) {
            state.superWeapons.grant(owner, superWeaponId, -1, &context);
        }
        std::string reason;
        applied = state.superWeapons.applyTriggerAction(
            owner,
            superWeaponId,
            actionName,
            target,
            context,
            &reason
        );
    }
    (void)notifyRuntimeParasiteHostsRelocated(state, parasiteHostPoses);
    (void)drainRuntimeObjectExitQueue(state);
    return applied;
}

bool runtimeObjectOccupiesAirNavigation(const WorldObjectInstance& object) {
    return object.alive && object.airborne
        && (object.kind == WorldObjectKind::Infantry
            || object.kind == WorldObjectKind::Unit);
}

std::optional<RuntimeCellPoint> runtimeActiveAirSegment(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto flight = state.jumpjetFlights.find(object.instanceId);
    if (flight != state.jumpjetFlights.end() && flight->second.active)
        return RuntimeCellPoint{flight->second.fromX, flight->second.fromY};
    const auto found = state.behaviors.find(object.instanceId);
    return found != state.behaviors.end()
            && found->second.moveCooldownTicks > 0
            && found->second.moveSegmentFrom.x >= 0 && found->second.moveSegmentFrom.y >= 0
        ? std::optional<RuntimeCellPoint>(found->second.moveSegmentFrom) : std::nullopt;
}

bool runtimeAirCellIsClear(
    const RuntimeGameSessionState& state,
    int moverId,
    const RuntimeCellPoint& cell) {
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        if (runtimeCellPointForObject(other) == cell) return false;
        const auto segment = runtimeActiveAirSegment(state, other);
        if (segment && *segment == cell) return false;
    }
    return true;
}

int runtimeAirDiagonalKey(const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    return (from.y + to.y) * 1024 + from.x + to.x;
}

bool runtimeAirTransitionIsClear(
    const RuntimeGameSessionState& state,
    int moverId,
    const RuntimeCellPoint& from,
    const RuntimeCellPoint& to) {
    if (to.x < 0 || to.y < 0 || to.x >= 512 || to.y >= 512) return false;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        if (runtimeCellPointForObject(other) == to) return false;
        const auto segment = runtimeActiveAirSegment(state, other);
        if (!segment) continue;
        if (*segment == to) return false;
        // Two diagonals can intersect even when all four endpoints differ.
        if (from.x != to.x && from.y != to.y
            && segment->x != other.x
            && segment->y != other.y
            && runtimeAirDiagonalKey(from, to)
                == runtimeAirDiagonalKey(*segment, runtimeCellPointForObject(other))) return false;
    }
    return true;
}

RuntimeAirTraffic buildRuntimeAirTraffic(const RuntimeGameSessionState& state, int moverId) {
    RuntimeAirTraffic traffic;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == moverId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        traffic.occupied.insert(runtimeCellKey(other.x, other.y));
        const auto segment = runtimeActiveAirSegment(state, other);
        if (!segment) continue;
        traffic.occupied.insert(runtimeCellKey(segment->x, segment->y));
        if (segment->x != other.x
            && segment->y != other.y) {
            traffic.diagonals.insert(runtimeAirDiagonalKey(*segment, runtimeCellPointForObject(other)));
        }
    }
    return traffic;
}

std::vector<RuntimeCellPoint> findRuntimeAirPath(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& start,
    const RuntimeCellPoint& goal) {
    if (start == goal || goal.x < 0 || goal.y < 0 || goal.x >= 512 || goal.y >= 512) return {};
    const auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    if (traffic.occupied.count(runtimeCellKey(goal.x, goal.y)) != 0) return {};

    // Rasterize the whole line, rather than greedily reducing distance each
    // tick (which produces a long diagonal followed by a sharp axial turn).
    std::vector<RuntimeCellPoint> line;
    RuntimeCellPoint current = start;
    const int dx = std::abs(goal.x - start.x), dy = std::abs(goal.y - start.y);
    const int sx = start.x < goal.x ? 1 : -1, sy = start.y < goal.y ? 1 : -1;
    int error = dx - dy;
    while (current != goal) {
        RuntimeCellPoint next = current;
        const int twiceError = error * 2;
        if (twiceError > -dy) { error -= dy; next.x += sx; }
        if (twiceError < dx) { error += dx; next.y += sy; }
        if (!traffic.canStep(current, next)) break;
        line.push_back(next);
        current = next;
    }
    if (current == goal) return line;

    // Only air traffic needs a detour. A bounded search over the air plane
    // also crosses holes/water/cliffs without inheriting ground penalties.
    const auto estimate = [&](const RuntimeCellPoint& cell) {
        const int x = std::abs(goal.x - cell.x), y = std::abs(goal.y - cell.y);
        return 10 * std::max(x, y) + 4 * std::min(x, y);
    };
    struct Node { int cost; int parent; RuntimeCellPoint cell; };
    using Entry = std::tuple<int, int, int, int>; // f, line deviation, cell key, g
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
    std::unordered_map<int, Node> nodes;
    const int startKey = runtimeCellKey(start.x, start.y);
    const int goalKey = runtimeCellKey(goal.x, goal.y);
    nodes.emplace(startKey, Node{0, -1, start});
    frontier.emplace(estimate(start), 0, startKey, 0);
    int expanded = 0;
    while (!frontier.empty() && expanded < runtimePathExpandedNodeBudget) {
        const auto [score, deviation, key, cost] = frontier.top();
        (void)score; (void)deviation;
        frontier.pop();
        if (nodes.at(key).cost != cost) continue;
        if (key == goalKey) {
            std::vector<RuntimeCellPoint> path;
            for (int cursor = key; cursor != startKey; cursor = nodes.at(cursor).parent) {
                path.push_back(nodes.at(cursor).cell);
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
        ++expanded;
        const RuntimeCellPoint from = nodes.at(key).cell;
        for (const auto& next : runtimeNeighborsOf(from)) {
            if (!traffic.canStep(from, next)) continue;
            const int nextKey = runtimeCellKey(next.x, next.y);
            const int nextCost = cost + (next.x != from.x && next.y != from.y ? 14 : 10);
            const auto found = nodes.find(nextKey);
            if (found != nodes.end() && found->second.cost <= nextCost) continue;
            nodes.insert_or_assign(nextKey, Node{nextCost, key, next});
            const int lineDeviation = std::abs((goal.x - start.x) * (next.y - start.y)
                - (goal.y - start.y) * (next.x - start.x));
            frontier.emplace(nextCost + estimate(next), lineDeviation, nextKey, nextCost);
        }
    }
    return {};
}

std::optional<RuntimeCellPoint> findRuntimeAirMoveTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover,
    const RuntimeCellPoint& desired,
    const std::unordered_set<int>* reserved,
    const std::unordered_set<int>* replacingOrders,
    const RuntimeManualFormationMappedFilter* manualMappedFilter) {
    auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    // Separate commands issued before either flyer arrives must reserve
    // separate destinations too. A new group order replaces its old goals.
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId || (replacingOrders != nullptr && replacingOrders->count(id) != 0)
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        const auto* other = findObjectById(state, id);
        if (other != nullptr && runtimeObjectOccupiesAirNavigation(*other)) {
            traffic.occupied.insert(runtimeCellKey(behavior.moveTarget.x, behavior.moveTarget.y));
        }
    }
    for (int radius = 0; radius <= 24; ++radius) {
        std::optional<RuntimeCellPoint> best;
        const int maximum = std::numeric_limits<int>::max();
        std::tuple<int, int, int> bestScore{maximum, maximum, maximum};
        for (int y = desired.y - radius; y <= desired.y + radius; ++y) {
            for (int x = desired.x - radius; x <= desired.x + radius; ++x) {
                if (std::max(std::abs(x - desired.x), std::abs(y - desired.y)) != radius
                    || x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                const int key = runtimeCellKey(x, y);
                if (state.terrainByCell.count(key) == 0 || traffic.occupied.count(key) != 0
                    || (reserved != nullptr && reserved->count(key) != 0)
                    || (manualMappedFilter != nullptr && !manualMappedFilter->allows({x,y}))) continue;
                const auto score = std::make_tuple(
                    (x - desired.x) * (x - desired.x) + (y - desired.y) * (y - desired.y),
                    (x - mover.x) * (x - mover.x) + (y - mover.y) * (y - mover.y), key);
                if (!best.has_value() || score < bestScore) { best = RuntimeCellPoint{x, y}; bestScore = score; }
            }
        }
        if (best.has_value()) return best;
    }
    return std::nullopt;
}

bool findRuntimeReachableAirMoveTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& anchor, int radius,
    RuntimeCellPoint& target, std::vector<RuntimeCellPoint>& path) {
    const auto start = runtimeCellPointForObject(mover);
    const auto traffic = buildRuntimeAirTraffic(state, mover.instanceId);
    std::unordered_set<int> destinations;
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.state == RuntimeBehaviorState::Complete) continue;
        const auto* other = findObjectById(state, id);
        if (other && runtimeObjectOccupiesAirNavigation(*other))
            destinations.insert(runtimeCellKey(behavior.moveTarget.x, behavior.moveTarget.y));
    }
    radius=std::clamp(radius,1,512);
    const int minX=std::max(0,std::min(start.x,anchor.x-radius)-8);
    const int minY=std::max(0,std::min(start.y,anchor.y-radius)-8);
    const int maxX=std::min(511,std::max(start.x,anchor.x+radius)+8);
    const int maxY=std::min(511,std::max(start.y,anchor.y+radius)+8);
    const int width=maxX-minX+1, height=maxY-minY+1;
    if (width<=0 || height<=0 || start.x<minX || start.y<minY || start.x>maxX || start.y>maxY) return false;
    const auto index=[&](const RuntimeCellPoint& p){return (p.y-minY)*width+p.x-minX;};
    const auto cell=[&](int i){return RuntimeCellPoint{minX+i%width,minY+i/width};};
    std::vector<int> parent(width*height,-2),queue;
    queue.reserve(std::min(width*height,65536));
    parent[index(start)]=-1;queue.push_back(index(start));
    int best=-1;
    std::tuple<int,int,int> bestScore{INT_MAX,INT_MAX,INT_MAX};
    constexpr std::array<RuntimeCellPoint,8> offsets{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for (std::size_t head=0;head<queue.size() && head<65536;++head) {
        const int here=queue[head];const auto point=cell(here);
        const int key=runtimeCellKey(point.x,point.y);
        const int d=(point.x-anchor.x)*(point.x-anchor.x)+(point.y-anchor.y)*(point.y-anchor.y);
        if (d<=radius*radius && state.terrainByCell.count(key) && !traffic.occupied.count(key)
            && !destinations.count(key)) {
            const auto score=std::make_tuple(d,(point.x-start.x)*(point.x-start.x)+(point.y-start.y)*(point.y-start.y),key);
            if (score<bestScore) {best=here;bestScore=score;}
            if (d==0) break;
        }
        for (const auto& offset:offsets) {
            const RuntimeCellPoint next{point.x+offset.x,point.y+offset.y};
            if(next.x<minX || next.y<minY || next.x>maxX || next.y>maxY
                || parent[index(next)]!=-2 || !traffic.canStep(point,next)) continue;
            parent[index(next)]=here;queue.push_back(index(next));
        }
    }
    if(best<0) return false;
    target=cell(best);path.clear();
    for(int here=best;parent[here]!=-1;here=parent[here])path.push_back(cell(here));
    std::reverse(path.begin(),path.end());
    return true;
}

double runtimePathThreatAvoidance(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover) {
    for (const auto& [id, mission] : state.triggers.teamMissions) {
        if (mission.completed || std::find(mission.objectIds.begin(), mission.objectIds.end(),
                mover.instanceId) == mission.objectIds.end()) continue;
        const auto* team = findRuntimeTriggerTeamById(state, mission.teamTypeId);
        if (team && team->avoidThreats) return 1.0;
    }
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, squad] : tactics.teams) {
            if (std::find(squad.objectIds.begin(), squad.objectIds.end(), mover.instanceId) == squad.objectIds.end()) continue;
            const auto* team = findRuntimeTriggerTeamById(state, squad.teamTypeId);
            if (team && team->avoidThreats) return 1.0;
        }
    const auto* unit = findRuntimeUnitType(state.rules, mover);
    return unit ? unit->threatAvoidanceCoefficient : 0.0;
}

std::vector<RuntimePathThreatArea> runtimePathThreatAreas(const RuntimeGameSessionState& state,
    const WorldObjectInstance& mover, double coefficient) {
    std::vector<RuntimePathThreatArea> result;
    if (coefficient <= 0.0) return result;
    for (const auto& enemy : state.world.getObjects()) {
        if (!enemy.alive || enemy.rubble || enemy.buildAnimTicksRemaining > 0
            || !enemy.poweredOperational || !runtimeOwnersAreHostileForAutoTarget(state, mover.owner, enemy.owner)
            || !runtimeObjectVisibleToOwner(state, mover.owner, enemy)) continue;
        const auto* unit = findRuntimeUnitType(state.rules, enemy);
        const auto* building = enemy.kind == WorldObjectKind::Structure
            ? findRuntimeBuildingType(state.rules, enemy) : nullptr;
        const int posed = building ? building->threatPosed : unit ? unit->threatPosed : 0;
        if (posed <= 0) continue;
        const auto profile = runtimeWeaponProfileForObject(state, enemy, &mover);
        if (!profile.weapon || profile.weapon->damage <= 0 || !runtimeWeaponHasAttackRange(*profile.weapon)) continue;
        const double range = runtimeEffectiveWeaponRange(state, enemy, mover, *profile.weapon);
        // Unlimited carrier targeting does not make every cell impassable.
        if (!std::isfinite(range)) continue;
        const auto footprint = runtimeStructureFootprintForObject(state, enemy);
        result.push_back({enemy.instanceId, enemy.x, enemy.y, footprint.width, footprint.height,
            range, static_cast<int>(std::lround(std::clamp(posed * coefficient, 1.0, 1000.0)))});
    }
    return result;
}

int runtimePathThreatCost(const std::vector<RuntimePathThreatArea>& threats, const PathCell& cell) {
    int cost = 0;
    for (const auto& threat : threats) {
        const int dx = std::max({threat.x - cell.x, 0, cell.x - (threat.x + threat.width - 1)});
        const int dy = std::max({threat.y - cell.y, 0, cell.y - (threat.y + threat.height - 1)});
        if (static_cast<double>(dx) * dx + static_cast<double>(dy) * dy <= threat.range * threat.range)
            cost = std::min(4000, cost + threat.cost);
    }
    return cost;
}

RuntimeSkirmishStartingUnitPools runtimeSkirmishStartingUnitPoolsForCountry(
    const RuntimeGameSessionState& state,
    const std::string& country) {
    RuntimeSkirmishStartingUnitPools pools;
    for (const UnitType& unit : state.rules.getInfantryTypes()) {
        if (runtimeStartingUnitAllowedForCountry(state, unit, country)) {
            pools.infantry.push_back(&unit);
        }
    }
    for (const UnitType& unit : state.rules.getVehicleTypes()) {
        if (runtimeStartingUnitAllowedForCountry(state, unit, country)) {
            pools.vehicles.push_back(&unit);
        }
    }
    return pools;
}

std::string runtimeSkirmishDifficultyForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    const auto found = state.skirmishDifficultyByOwner.find(
        toLowerAsciiLocal(trimAsciiLocal(owner))
    );
    return found == state.skirmishDifficultyByOwner.end()
        ? "Medium"
        : found->second;
}

void loadAndApplyRuntimeSkirmishModeRules(RuntimeGameSessionState& state) {
    state.skirmishModeRules = IniFile{};
    state.skirmishModeRulesLoaded = false;
    state.skirmishModeAlliesAllowed = true;
    state.skirmishModeAllyChangeAllowed = true;
    state.skirmishModeMustAlly = false;
    state.skirmishModeRulesPath =
        resolveRuntimeModeRulesOverridePath(state.config);
    if (state.skirmishModeRulesPath.empty()
        || !state.skirmishModeRules.loadFromFile(
            state.skirmishModeRulesPath
        )) {
        return;
    }

    state.skirmishModeRulesLoaded = true;
    // Startup loads rules -> mode -> map. Keep late catalog/start/crate
    // lookups in the same order instead of restoring a mode value over a
    // scenario override. Only the authored mode sections belong here.
    IniFile mapRules;
    if (!state.config.defaultMapPath.empty()
        && mapRules.loadFromFile(state.config.defaultMapPath)) {
        std::ostringstream overlayText;
        for (const std::string& name : state.skirmishModeRules.getSectionNames()) {
            if (const auto* section = runtimeIniSectionNoCase(mapRules, name)) {
                overlayText << '[' << name << "]\n";
                for (const auto& [key, value] : *section)
                    overlayText << key << '=' << value << '\n';
            }
        }
        IniFile overlay;
        if (overlay.loadFromString(overlayText.str()))
            state.skirmishModeRules.appendFrom(overlay);
    }
    const IniFile::Section* dialogSettings = runtimeIniSectionNoCase(
        state.skirmishModeRules,
        "MultiplayerDialogSettings"
    );
    if (const std::optional<std::string> value =
            runtimeIniSectionOptionalValueNoCase(
                dialogSettings,
                "AlliesAllowed"
            )) {
        state.skirmishModeAlliesAllowed = parseRuntimeBool(*value);
    }
    if (const std::optional<std::string> value =
            runtimeIniSectionOptionalValueNoCase(
                dialogSettings,
                "AllyChangeAllowed"
            )) {
        state.skirmishModeAllyChangeAllowed = parseRuntimeBool(*value);
    }
    if (const std::optional<std::string> value =
            runtimeIniSectionOptionalValueNoCase(
                dialogSettings,
                "MustAlly"
            )) {
        state.skirmishModeMustAlly = parseRuntimeBool(*value);
    }

    addRuntimeModeOverrideBuildCatalog(state);
    for (RuntimeBuildCatalogItem& item : state.economy.catalog) {
        applyRuntimeModeRulesToBuildItem(state, item);
    }
}

bool initializeRuntimeTriggers(RuntimeProductionHostState& state) {
    state.triggers = RuntimeTriggerState{};

    IniFile mapIni;
    if (state.config.defaultMapPath.empty()
        || !mapIni.loadFromFile(state.config.defaultMapPath)) {
        state.triggers.status = "trigger map ini missing";
        return false;
    }

    state.triggers.loaded = true;
    state.triggers.truckCrate = parseRuntimeBool(mapIni.getValue("Basic", "TruckCrate", "no"));
    state.triggers.trainCrate = parseRuntimeBool(mapIni.getValue("Basic", "TrainCrate", "no"));
    const std::string authoredTheme = trimAsciiLocal(
        mapIni.getValue("Basic", "Theme", "")
    );
    if (!runtimeMusicThemeIsUnspecified(authoredTheme)) {
        std::string themeError;
        if (!selectRuntimeMusicThemePath(state, authoredTheme, &themeError)) {
            state.lastRuntimeSound = "map theme missing " + authoredTheme
                + (themeError.empty() ? "" : ": " + themeError);
        }
    }
    state.triggers.triggerCount =
        runtimeSectionEntryCount(mapIni, "Triggers");
    state.triggers.eventCount =
        runtimeSectionEntryCount(mapIni, "Events");
    state.triggers.actionCount =
        runtimeSectionEntryCount(mapIni, "Actions");
    state.triggers.tagCount =
        runtimeSectionEntryCount(mapIni, "Tags");
    state.triggers.teamTypeCount =
        runtimeSectionEntryCount(mapIni, "TeamTypes");
    state.triggers.taskForceCount =
        runtimeSectionEntryCount(mapIni, "TaskForces");
    state.triggers.scriptTypeCount =
        runtimeSectionEntryCount(mapIni, "ScriptTypes");

    int nextCountryIndex = 0;
    std::unordered_set<std::string> indexedCountryKeys;
    const auto appendCountry = [&state, &nextCountryIndex, &indexedCountryKeys](
                                   const std::string& country
                               ) {
        const std::string trimmed = trimAsciiLocal(country);
        const std::string key = toLowerAsciiLocal(trimmed);
        if (!trimmed.empty() && indexedCountryKeys.insert(key).second) {
            state.triggers.campaignCountryByIndex[nextCountryIndex++] = trimmed;
        }
    };
    for (const std::string& country : state.rules.getCountries()) {
        appendCountry(country);
    }
    // Runtime startup already folds the active map into RulesDatabase.  Keep
    // this fallback for focused callers that provide base rules directly,
    // while preventing the map-local ordinal table from being appended twice.
    for (const std::string& country : runtimeIndexedIds(mapIni, "Countries")) {
        appendCountry(country);
    }

    std::vector<std::pair<int, std::string>> campaignHouses;
    if (const IniFile::Section* houses = mapIni.getSection("Houses")) {
        for (const auto& [indexText, houseText] : *houses) {
            int index = -1;
            const std::string house = trimAsciiLocal(houseText);
            if (parseRuntimeInt(indexText, index)
                && index >= 0
                && !house.empty()) {
                state.triggers.campaignHouseByIndex[index] = house;
                campaignHouses.emplace_back(index, house);
            }
        }
    }
    std::sort(campaignHouses.begin(), campaignHouses.end());

    const auto campaignCountrySide = [&mapIni](std::string country) {
        std::unordered_set<std::string> visited;
        for (int depth = 0; depth < 8; ++depth) {
            const std::string key = toLowerAsciiLocal(trimAsciiLocal(country));
            if (key.empty() || !visited.insert(key).second) {
                break;
            }
            const std::string side = toUpperAsciiLocal(trimAsciiLocal(
                mapIni.getValue(country, "Side", "")
            ));
            if (side == "GDI" || side == "ALLIED" || side == "ALLIES") {
                return 1;
            }
            if (side == "NOD" || side == "SOVIET" || side == "SOVIETS") {
                return 2;
            }
            if (side == "THIRDSIDE" || side == "YURI") {
                return 3;
            }
            const std::string parent = trimAsciiLocal(
                mapIni.getValue(country, "ParentCountry", "")
            );
            if (parent.empty()) {
                break;
            }
            country = parent;
        }
        const int planningSide = runtimeBasePlanningSideForCountry(country);
        return planningSide < 0 ? -1 : planningSide + 1;
    };

    for (const auto& [index, house] : campaignHouses) {
        (void)index;
        const std::string country = trimAsciiLocal(
            mapIni.getValue(house, "Country", house)
        );
        const std::string houseKey = toLowerAsciiLocal(house);
        RuntimeCampaignHouseProductionState production;
        production.owner = house;
        production.country = country.empty() ? house : country;
        production.techLevel = std::max(0, parseRuntimeIntOrDefault(
            mapIni.getValue(house, "TechLevel", "10"),
            10
        ));
        production.iq = std::max(0, parseRuntimeIntOrDefault(
            mapIni.getValue(house, "IQ", "0"),
            0
        ));
        const int authoredCredits = std::max(0, parseRuntimeIntOrDefault(
            mapIni.getValue(house, "Credits", "0"),
            0
        ));
        production.credits = authoredCredits > std::numeric_limits<int>::max() / 100
            ? std::numeric_limits<int>::max()
            : authoredCredits * 100;
        production.side = campaignCountrySide(production.country);
        production.playerControlled = parseRuntimeBool(
            mapIni.getValue(house, "PlayerControl", "no")
        );
        state.triggers.campaignHouseProduction[houseKey] = production;
        state.triggers.campaignCanonicalHouseByAlias[houseKey] = house;
        state.triggers.campaignCountryByHouse[houseKey] = production.country;
        const std::string countryKey = toLowerAsciiLocal(production.country);
        if (!countryKey.empty()) {
            state.triggers.campaignCanonicalHouseByAlias[countryKey] = house;
        }
    }

    const auto canonicalCampaignHouse = [&state](const std::string& value) {
        const std::string key = toLowerAsciiLocal(trimAsciiLocal(value));
        const auto found =
            state.triggers.campaignCanonicalHouseByAlias.find(key);
        return found == state.triggers.campaignCanonicalHouseByAlias.end()
            ? trimAsciiLocal(value)
            : found->second;
    };
    for (const auto& [index, house] : campaignHouses) {
        (void)index;
        for (const std::string& ally : splitRuntimeTokenList(
                mapIni.getValue(house, "Allies", "")
            )) {
            const std::string left = toLowerAsciiLocal(
                canonicalCampaignHouse(house)
            );
            const std::string right = toLowerAsciiLocal(
                canonicalCampaignHouse(ally)
            );
            if (left.empty() || right.empty()) {
                continue;
            }
            state.triggers.campaignAlliedPairs.insert(
                left < right ? left + "|" + right : right + "|" + left
            );
        }
    }
    if (const IniFile::Section* variables =
            mapIni.getSection("VariableNames")) {
        for (const auto& [indexText, variableText] : *variables) {
            int index = -1;
            const std::vector<std::string> tokens =
                splitRuntimeTokenList(variableText);
            if (parseRuntimeInt(indexText, index) && index >= 0) {
                state.triggers.campaignLocalVariables[index] =
                    tokens.size() >= 2
                        ? parseRuntimeIntOrDefault(tokens[1], 0)
                        : 0;
            }
        }
    }

    const auto loadTeamDefinitions = [&state](const IniFile& ini) {
        for (const std::string& id : runtimeIndexedIds(ini, "ScriptTypes")) {
            state.triggers.scriptIds.push_back(id);
            state.triggers.scripts[id] = parseRuntimeScriptType(ini, id);
        }
        for (const std::string& id : runtimeIndexedIds(ini, "TaskForces")) {
            state.triggers.taskForces[id] = parseRuntimeTaskForce(ini, id);
        }
        for (const std::string& id : runtimeIndexedIds(ini, "TeamTypes")) {
            state.triggers.teamIds.push_back(id);
            auto team = parseRuntimeTeamType(ini, id);
            if (const auto slot = state.cooperativeSession.resolveAuthoredSlotAlias(team.house))
                team.house = state.cooperativeSession.slots()[static_cast<std::size_t>(*slot)].owner;
            state.triggers.teams[id] = std::move(team);
        }
    };

    // Retail indexes used by Change Script/Change Team include the selected
    // edition's global AI definitions before map-local definitions. Authored
    // scenario triggers still come exclusively from the map.
    IniFile globalAiIni;
    const bool globalAiLoaded = loadRuntimeIniSource(
        state.config,
        RuntimeCampaignIniKind::Ai,
        globalAiIni
    );
    if (globalAiLoaded) {
        loadTeamDefinitions(globalAiIni);
    }
    loadTeamDefinitions(mapIni);
    loadRuntimeComputerTacticTriggers(state, globalAiLoaded ? &globalAiIni : nullptr, mapIni);

    const bool ignoreGlobalAiTriggers = parseRuntimeBool(
        mapIni.getValue("Basic", "IgnoreGlobalAITriggers", "no")
    );
    if (state.campaignMode && globalAiLoaded && !ignoreGlobalAiTriggers) {
        loadRuntimeCampaignAiTriggers(state.triggers, globalAiIni);
    }
    if (state.campaignMode) {
        loadRuntimeCampaignAiTriggers(state.triggers, mapIni);
        if (const IniFile::Section* enabled =
                mapIni.getSection("AITriggerTypesEnable")) {
            for (RuntimeCampaignAiTriggerDefinition& aiTrigger :
                    state.triggers.campaignAiTriggers) {
                for (const auto& [enabledId, enabledValue] : *enabled) {
                    if (toLowerAsciiLocal(trimAsciiLocal(enabledId))
                        == toLowerAsciiLocal(trimAsciiLocal(aiTrigger.id))) {
                        aiTrigger.enabled = parseRuntimeBool(enabledValue);
                        break;
                    }
                }
            }
        }
        std::sort(
            state.triggers.campaignAiTriggers.begin(),
            state.triggers.campaignAiTriggers.end(),
            [](const RuntimeCampaignAiTriggerDefinition& left,
               const RuntimeCampaignAiTriggerDefinition& right) {
                return toLowerAsciiLocal(left.id)
                    < toLowerAsciiLocal(right.id);
            }
        );
    }

    const std::map<std::string, std::vector<RuntimeTriggerEvent>>
        eventsByTrigger = parseRuntimeEventsByTrigger(mapIni);
    const std::map<std::string, std::vector<RuntimeTriggerAction>>
        actionsByTrigger = parseRuntimeActionsByTrigger(mapIni);
    state.triggers.triggers = parseRuntimeTriggers(
        mapIni,
        eventsByTrigger,
        actionsByTrigger
    );
    std::vector<std::pair<std::string, RuntimeTriggerEvent*>> lightingEvents;
    for (RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
        for (RuntimeTriggerEvent& event : trigger.events) {
            if (event.type == 45 || event.type == 46) {
                lightingEvents.emplace_back(
                    toLowerAsciiLocal(trimAsciiLocal(trigger.id)) + "|"
                        + std::to_string(event.authoredOrdinal),
                    &event
                );
            }
        }
    }
    std::sort(
        lightingEvents.begin(),
        lightingEvents.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        }
    );
    for (std::size_t index = 0; index < lightingEvents.size(); ++index) {
        lightingEvents[index].second->lightingEventId =
            static_cast<int>(index) + 1;
    }

    // [Tags] owns both target binding and repeat policy.  Only the root of an
    // AttachedTrigger chain is named by the tag; every attached trigger must
    // inherit it so object/cell events do not become orphaned.
    if (const IniFile::Section* tags = mapIni.getSection("Tags")) {
        for (const auto& [tagId, value] : *tags) {
            const std::vector<std::string> tokens = splitRuntimeTokenList(value);
            if (tokens.size() < 3) {
                continue;
            }
            const int repeatType = std::clamp(
                parseRuntimeIntOrDefault(tokens[0], 0),
                0,
                2
            );
            RuntimeTriggerDefinition* current = runtimeCampaignTriggerById(
                state.triggers,
                tokens[2]
            );
            std::unordered_set<std::string> visited;
            while (current != nullptr
                && visited.insert(toLowerAsciiLocal(current->id)).second) {
                current->tagIds.insert(toLowerAsciiLocal(trimAsciiLocal(tagId)));
                current->tagRepeatType = repeatType;
                current->repeating = repeatType == 2;
                const std::string attached = trimAsciiLocal(
                    current->linkedTriggerId
                );
                if (attached.empty()
                    || toLowerAsciiLocal(attached) == "<none>") {
                    break;
                }
                current = runtimeCampaignTriggerById(state.triggers, attached);
            }
        }
    }
    if (const IniFile::Section* cellTags = mapIni.getSection("CellTags")) {
        for (const auto& [cellText, tagId] : *cellTags) {
            int cell = -1;
            if (parseRuntimeInt(cellText, cell) && cell >= 0) {
                state.triggers.campaignCellTagByCell[cell] =
                    toLowerAsciiLocal(trimAsciiLocal(tagId));
            }
        }
    }

    // Trigger movie parameters are indexes into the selected edition's
    // art.ini/artmd.ini [Movies] table. Resolve every index through the same
    // strict edition boundary as mission intro/post-score movies. MIX entries
    // remain virtual references until playback requests one specific movie.
    RuntimeCampaignMovieResolverOptions movieOptions;
    movieOptions.resourceRootPath = state.config.resourceRootPath;
    movieOptions.localDataRootPath = state.config.localDataRootPath;
    movieOptions.modRootPath = state.config.modRootPath;
    const std::string campaignSourceId =
        normalizeRuntimeCampaignSourceId(state.config.campaignSourceId);

    IniFile movieIni;
    if (loadRuntimeIniSource(
            state.config,
            RuntimeCampaignIniKind::Art,
            movieIni
        )) {
        if (const IniFile::Section* movies = movieIni.getSection("Movies")) {
            for (const auto& [indexText, movieIdText] : *movies) {
                int index = -1;
                const std::string movieId = trimAsciiLocal(movieIdText);
                if (!parseRuntimeInt(indexText, index)
                    || index < 0
                    || movieId.empty()) {
                    continue;
                }
                const RuntimeCampaignMovieResolution resolution =
                    resolveRuntimeCampaignMovie(
                        movieOptions,
                        campaignSourceId,
                        movieId
                    );
                if (resolution.found) {
                    state.triggers.campaignMoviePathByIndex[index] =
                        resolution.referencePath;
                }
            }
        }
    }

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        state.triggers.campaignPreviouslyKnownObjects.insert(object.instanceId);
        state.triggers.campaignPreviousCellByObject[object.instanceId] =
            runtimeCellKey(object.x, object.y);
        state.triggers.campaignPreviousHealthByObject[object.instanceId] =
            object.health;
        state.triggers.campaignInitialHealthByObject[object.instanceId] =
            std::max(1, object.health);
        state.triggers.campaignPreviousAliveByObject[object.instanceId] =
            object.alive;
        state.triggers.campaignInitialOwnerByObject[object.instanceId] =
            object.owner;
        state.triggers.campaignPreviousOwnerByObject[object.instanceId] =
            object.owner;
    }
    for (RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
        const bool onceAllObjectEvent = trigger.tagRepeatType == 1
            && std::any_of(
                trigger.events.begin(),
                trigger.events.end(),
                [](const RuntimeTriggerEvent& event) {
                    return event.type == 48;
                }
            );
        if (!onceAllObjectEvent) {
            continue;
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.tag.empty()
                && trigger.tagIds.find(
                    toLowerAsciiLocal(trimAsciiLocal(object.tag))
                ) != trigger.tagIds.end()) {
                trigger.remainingOnceAllObjectIds.insert(object.instanceId);
            }
        }
    }

    if (state.campaignMode || state.cooperativeSession.active()) {
        int difficultyIndex = 1;
        const std::string difficulty =
            toUpperAsciiLocal(trimAsciiLocal(state.config.campaignDifficulty));
        if (difficulty == "EASY") {
            difficultyIndex = 0;
        } else if (difficulty == "HARD" || difficulty == "BRUTAL") {
            difficultyIndex = 2;
        }
        for (RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
            trigger.enabled =
                trigger.enabled
                && trigger.difficultyEnabled[
                    static_cast<std::size_t>(difficultyIndex)
                ];
            trigger.activationTick = 0;
        }
    }

    for (const RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
        for (const RuntimeTriggerAction& action : trigger.actions) {
            if (runtimeIsCreateTeamAction(action.type)) {
                ++state.triggers.createTeamActionCount;
            }
        }
    }

    std::string padCapacityError;
    if (!validateRuntimeAircraftPadSlotCapacity(
            state,
            padCapacityError
        )) {
        state.triggers.status = "trigger pad capacity rejected: "
            + padCapacityError;
        return false;
    }

    state.triggers.status =
        "loaded triggers="
        + std::to_string(state.triggers.triggerCount)
        + " teams="
        + std::to_string(state.triggers.teamTypeCount)
        + " createTeam="
        + std::to_string(state.triggers.createTeamActionCount)
        + " aiTriggers="
        + std::to_string(state.triggers.campaignAiTriggers.size())
        + " tagged="
        + std::to_string(state.triggers.campaignCellTagByCell.size())
        + " movies="
        + std::to_string(state.triggers.campaignMoviePathByIndex.size());
    return true;
}

void writeRuntimeComputerMining(std::ostream& output, const RuntimeGameSessionState& state) {
    ::writeRuntimeComputerMining(output, state.computerMiningByOwner);
}

void writeRuntimeComputerTactics(std::ostream& output, const RuntimeGameSessionState& state) {
    writeRuntimeComputerTacticsState(output, state.triggers.computerTacticsByOwner);
}

bool readRuntimeCooperativeSession(const RuntimeGameSessionState& state, const IniFile& ini,
    RuntimeCooperativeSessionState& result, std::string& error) {
    result = state.cooperativeSession;
    if (!runtimeIsCooperativeMode(state.config)) {
        if (ini.getSection("CooperativeSessionRuntime") != nullptr) {
            error = "cooperative save cannot be loaded into another mode"; return false;
        }
        return true;
    }
    if (!state.cooperativeSession.active()) {
        error = "cooperative save has no initialized mission"; return false;
    }
    if (ini.getSection("CooperativeSessionRuntime") == nullptr) {
        if (!state.config.multiplayerActive) return true; // Earlier local saves had no session section.
        error = "cooperative network snapshot is missing mission state"; return false;
    }
    IniFile scenarioIni, mapIni;
    CooperativeScenarioDatabase database;
    MapDatabase map;
    if (!scenarioIni.loadFromFile(runtimeCooperativeScenarioPath(state.config).generic_string())
        || !database.loadFromIni(scenarioIni)
        || !mapIni.loadFromFile(state.config.defaultMapPath) || !map.loadFromIni(mapIni)) {
        error = "saved cooperative mission resources unavailable"; return false;
    }
    const auto* scenario = database.findByMapId(state.cooperativeSession.mapId());
    if (!scenario || !result.load(ini, *scenario,
        database.validateMap(*scenario, map, mapIni), &error)) return false;
    for (std::size_t i = 0; i < runtimeCooperativeSlotCount; ++i) {
        const auto& expected = state.cooperativeSession.slots()[i];
        const auto& loaded = result.slots()[i];
        if (loaded.owner != expected.owner || loaded.country != expected.country) {
            error = "saved cooperative players differ from the current mission"; return false;
        }
    }
    const int tick = parseRuntimeIntOrDefault(ini.getValue("RuntimeSave", "Tick", "-1"), -1);
    if (result.hasOutcomeSnapshot() && result.lastOutcomeSnapshot().tick > tick) {
        error = "cooperative outcome is newer than the saved world"; return false;
    }
    return true;
}

void writeRuntimeCooperativeObservations(std::ostream& output, const RuntimeGameSessionState& state,
    bool replayCampaign) {
    if (!state.cooperativeSession.active() && !replayCampaign) return;
    if (state.cooperativeSession.active()) { output << '\n'; state.cooperativeSession.save(output); }
    if (replayCampaign) output << "\n[ReplayTriggerObservations]\nSchema=1\n";
#define WRITE_COOP_OBSERVATION(field) writeRuntimeCooperativeObservationMap(output, #field, state.triggers.field);
    RUNTIME_COOPERATIVE_OBSERVATIONS(WRITE_COOP_OBSERVATION)
#undef WRITE_COOP_OBSERVATION
    const auto saveSet = [&](const char* name, const auto& set) {
        std::map<int, int> values; for (int id : set) values[id] = 1;
        writeRuntimeCooperativeObservationMap(output, name, values);
    };
    saveSet("Known", state.triggers.campaignPreviouslyKnownObjects);
    saveSet("Discovered", state.triggers.campaignPreviouslyDiscoveredObjects);
    if (replayCampaign) saveSet("Selected", state.triggers.campaignPreviousSelectedObjects);
}

bool readRuntimeCooperativeObservations(const IniFile& ini, RuntimeTriggerState& triggers,
    std::string& error, bool replayCampaign) {
    if (!replayCampaign && ini.getSection("CooperativeSessionRuntime") == nullptr) return true;
    if (replayCampaign && ini.getValue("ReplayTriggerObservations", "Schema", "") != "1") {
        error = "missing replay campaign trigger observations"; return false;
    }
#define READ_COOP_OBSERVATION(field) if (!readRuntimeCooperativeObservationMap(ini, #field, triggers.field, error)) return false;
    RUNTIME_COOPERATIVE_OBSERVATIONS(READ_COOP_OBSERVATION)
#undef READ_COOP_OBSERVATION
    const auto loadSet = [&](const char* name, auto& set) {
        std::map<int, int> values;
        if (!readRuntimeCooperativeObservationMap(ini, name, values, error)) return false;
        set.clear(); for (const auto& [id, value] : values) { (void)value; set.insert(id); }
        return true;
    };
    return loadSet("Known", triggers.campaignPreviouslyKnownObjects)
        && loadSet("Discovered", triggers.campaignPreviouslyDiscoveredObjects)
        && (!replayCampaign || loadSet("Selected", triggers.campaignPreviousSelectedObjects));
}

void writeRuntimeTriggerReinforcements(const RuntimeGameSessionState& state, std::ostream& output) {
    ::writeRuntimeTriggerReinforcements(state.triggers, output);
}

bool readRuntimeTriggerReinforcements(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    RuntimeHostTriggerPersistenceOperations operations(state);
    return ::readRuntimeTriggerReinforcements(state.triggers, ini, error, operations);
}

std::string serializeRuntimeActiveAircraftFlights(const RuntimeGameSessionState& state) {
    RuntimeAircraftFlights active;
    for(const auto& object:state.world.getObjects()) {
        if(!object.alive||!object.airborne||!runtimeFixedWingProfile(state,object))continue;
        const auto found=state.aircraftFlights.find(object.instanceId);
        if(found!=state.aircraftFlights.end()&&object.x==found->second.x/runtimeAircraftCellQ16
            &&object.y==found->second.y/runtimeAircraftCellQ16)active.emplace(found->first,found->second);
    }
    return serializeRuntimeAircraftFlights(active);
}

void writeRuntimeAircraftFlights(const RuntimeGameSessionState& state,std::ostream& output) {
    output<<"[AircraftFlights]\nSchema=1\nDataHex="
        <<iniHexEncode(serializeRuntimeActiveAircraftFlights(state))<<"\n\n";
}

bool readRuntimeAircraftFlights(RuntimeGameSessionState& state,const IniFile& ini,std::string& error) {
    RuntimeAircraftFlights staged;
    if(ini.getSection("AircraftFlights")) {
        std::string payload;
        if(ini.getValue("AircraftFlights","Schema","")!="1"
            ||!iniHexDecode(ini.getValue("AircraftFlights","DataHex",""),payload,16U*1024U*1024U)
            ||!deserializeRuntimeAircraftFlights(payload,staged)) {
            error="invalid continuous aircraft flight section";return false;
        }
        for(const auto& [id,f]:staged) {
            const auto* object=findObjectById(state,id);
            if(!object||!object->alive||!object->airborne||!runtimeFixedWingProfile(state,*object)
                ||f.lastTick>state.tick||object->x!=f.x/runtimeAircraftCellQ16
                ||object->y!=f.y/runtimeAircraftCellQ16) {
                error="continuous aircraft flight does not match saved world";return false;
            }
            if(f.z>=0&&f.dockFlightStartZ<0
                &&f.z!=std::int64_t(object->flightLevel)*runtimeAircraftCellQ16) {
                error="continuous aircraft cruise height does not match saved world";return false;
            }
            if(f.landingHostId>=0) {
                // A killed/captured host can be retained until the next
                // mission pass cancels its approach. Its concrete pad still
                // has to be the geometry named by this saved descent.
                const auto* host=findRuntimeObjectByIdIncludingInactive(state,f.landingHostId);
                const auto* building=host?findRuntimeBuildingTypeById(state.rules,host->type):nullptr;
                if(!host||host->kind!=WorldObjectKind::Structure||!building
                    ||f.landingDockIndex>=building->numberOfDocks) {
                    error="continuous aircraft landing host does not match saved world";return false;
                }
                const auto dock=runtimeAircraftDockSpawn(state,*host,f.landingDockIndex);
                if(f.dockFlightTargetZ!=std::int64_t(dock.level)*runtimeAircraftCellQ16
                    ||f.x!=std::llround(dock.renderX*runtimeAircraftCellQ16)
                    ||f.y!=std::llround(dock.renderY*runtimeAircraftCellQ16)
                    ||(host->alive&&runtimeAircraftDockAssignmentCompatible(state,*object,*host)
                        &&(object->dockedToObjectId!=host->instanceId||object->dockIndex!=f.landingDockIndex))) {
                    error="continuous aircraft landing pad does not match saved world";return false;
                }
            }
        }
    }
    state.aircraftFlights=std::move(staged);
    for(auto& object:state.world.accessObjects())applyRuntimeFixedWingPose(state,object);
    return true;
}

bool parseRuntimeSavedCampaignAlliances(
    const IniFile& ini,
    int saveVersion,
    std::set<std::string>& alliances,
    std::string& errorMessage) {
    alliances.clear();
    const int schema = parseRuntimeIntOrDefault(
        ini.getValue("CampaignAlliances", "Schema", "0"),
        0
    );
    if (saveVersion >= 13 && schema != 2) {
        errorMessage = "v13 save is missing campaign alliance schema 2";
        return false;
    }
    int allianceCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("CampaignAlliances", "Count", "0"),
            allianceCount
        )
        || allianceCount < 0 || allianceCount > 100000) {
        errorMessage = "invalid campaign alliance header";
        return false;
    }

/*
    RuntimeWorldAudioSystemState validatedWorldAudio(
        RuntimeWorldAudioConfig{
            std::max(1, state.config.fixedTickHz),
            4096U
        }
    );
    RuntimeLightingSystemState validatedLighting(
        state.mapFacts.lighting,
        runtimeLightingConfigForState(state)
    );
    RuntimeParticleSystemState validatedParticles(
        std::max(1, state.config.fixedTickHz)
    );
    std::map<std::uint64_t, RuntimeActiveLightingToken>
        validatedActiveLightingTokens;
    if (version == 14) {
        std::uint64_t savedCatalogHash = 0;
        std::uint64_t savedWorldAudioHash = 0;
        std::uint64_t savedLightingHash = 0;
        std::uint64_t savedParticleHash = 0;
        if (!parseRuntimeUint64Strict(
                ini.getValue(
                    "RuntimeWorldAudioHost", "CatalogHash", ""
                ),
                savedCatalogHash
            )
            || (savedCatalogHash != state.worldAudioCatalog.stateHash()
                && (ini.getValue("RuntimeWorldAudioHost", "CatalogVersion", "1") != "1"
                    || savedCatalogHash != state.worldAudioCatalog.stateHash(false)))
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeWorldAudioHost", "StateHash", ""),
                savedWorldAudioHash
            )
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeLightingHost", "StateHash", ""),
                savedLightingHash
            )
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeParticleHost", "StateHash", ""),
                savedParticleHash
            )) {
            errorMessage = "invalid v14 audiovisual host hashes";
            return false;
        }

        RuntimeWorldAudioReconcileReport audioReport;
        int savedWorldAudioCommandTick = -1;
        int savedWorldAudioCommandTick = -1;
        if (!validatedWorldAudio.load(
                ini,
                runtimeWorldAudioContext(state),
                &errorMessage,
                &audioReport
            )
            || !audioReport.contextValid
            || audioReport.removedMissingSounds != 0U
            || audioReport.removedCompletedHandles != 0U
            || audioReport.rescheduledLegacyFiniteHandles != 0U
            || audioReport.removedByEventLimit != 0U
            || audioReport.removedByGlobalLimit != 0U
            || validatedWorldAudio.config().fixedTickHz
                != std::max(1, state.config.fixedTickHz)
            || validatedWorldAudio.config().maximumHandles != 4096U
            || validatedWorldAudio.lastAdvancedTick()
                != savedFactoryDeliveryTick
            || !parseRuntimeInt(
                ini.getValue(
                    "WorldAudioRuntime", "LastCommandTick", ""
                ),
                savedWorldAudioCommandTick
            )
            || savedWorldAudioCommandTick < -1
            || savedWorldAudioCommandTick > savedFactoryDeliveryTick
            || validatedWorldAudio.stateHash() != savedWorldAudioHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 world audio authority";
            }
            return false;
        }

        RuntimeLightingReconcileReport lightingReport;
        if (!validatedLighting.load(
                ini,
                state.mapFacts.lighting,
                &errorMessage,
                &lightingReport
            )
            || !lightingReport.contextValid
            || !lightingReport.profilesMatched
            || lightingReport.stateReset
            || lightingReport.valuesSanitized
            || lightingReport.removedThresholdWatchers != 0U
            || validatedLighting.lastAdvancedTick()
                != savedFactoryDeliveryTick
            || validatedLighting.config().fixedTickHz
                != std::max(1, state.config.fixedTickHz)
            || validatedLighting.stateHash() != savedLightingHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 lighting authority";
            }
            return false;
        }
        std::set<int> validLightingEventIds;
        for (const RuntimeTriggerDefinition& trigger :
                state.triggers.triggers) {
            for (const RuntimeTriggerEvent& event : trigger.events) {
                if ((event.type == 45 || event.type == 46)
                    && event.lightingEventId > 0) {
                    validLightingEventIds.insert(event.lightingEventId);
                }
            }
        }
        for (int watcherId :
                validatedLighting.thresholdWatcherEventIds()) {
            if (!validLightingEventIds.contains(watcherId)) {
                errorMessage =
                    "v14 lighting watcher is not authored by this map";
                return false;
            }
        }

        const std::array<const char*, 7> particleHeaderKeys{{
            "Version", "Tick", "FixedTickHz", "NextSystemId",
            "NextParticleId", "SystemCount", "ParticleCount"
        }};
        for (const char* key : particleHeaderKeys) {
            if (trimAsciiLocal(
                    ini.getValue("ParticleRuntime", key, "")
                ).empty()) {
                errorMessage = "v14 particle authority is missing header key ";
                errorMessage += key;
                return false;
            }
        }
        RuntimeParticleReconcileReport particleReport;
        if (!validatedParticles.load(
                ini,
                state.rules,
                &errorMessage,
                &particleReport
            )
            || particleReport.removedSystems != 0U
            || particleReport.removedParticles != 0U
            || particleReport.stoppedSystems != 0U
            || validatedParticles.currentTick()
                != savedFactoryDeliveryTick
            || validatedParticles.fixedTickHz()
                != std::max(1, state.config.fixedTickHz)
            || validatedParticles.stateHash() != savedParticleHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 particle authority";
            }
            return false;
        }
        std::unordered_map<int, const WorldObjectInstance*>
            savedParticleSourceObjects;
        savedParticleSourceObjects.reserve(validationGateObjects.size());
        for (const WorldObjectInstance& object : validationGateObjects) {
            savedParticleSourceObjects.emplace(object.instanceId, &object);
        }
        const auto savedObjectForParticleSource = [
            &savedParticleSourceObjects
        ](int objectId) -> const WorldObjectInstance* {
            const auto found = savedParticleSourceObjects.find(objectId);
            return found == savedParticleSourceObjects.end()
                ? nullptr
                : found->second;
        };
        for (const RuntimeParticleSystemInstance& system :
                validatedParticles.systems()) {
            if (system.sourceObjectId < 0) {
                continue;
            }
            const WorldObjectInstance* source =
                savedObjectForParticleSource(system.sourceObjectId);
            if (source == nullptr
                || (system.emitting && (!source->alive || source->health <= 0))) {
                errorMessage =
                    "v14 particle system has invalid source authority";
                return false;
            }
        }
        for (const RuntimeParticleInstance& particle :
                validatedParticles.particles()) {
            if (particle.sourceObjectId >= 0
                && savedObjectForParticleSource(
                    particle.sourceObjectId
                ) == nullptr) {
                errorMessage =
                    "v14 particle has missing source object";
                return false;
            }
        }

        int lightingTokenCount = -1;
        if (!parseRuntimeInt(
                ini.getValue(
                    "RuntimeLightingHost", "ActiveTokenCount", ""
                ),
                lightingTokenCount
            )
            || lightingTokenCount < 0 || lightingTokenCount > 4096) {
            errorMessage = "invalid v14 lighting token header";
            return false;
        }
        for (int index = 0; index < lightingTokenCount; ++index) {
            const std::vector<std::string> values = splitRuntimeSaveFields(
                ini.getValue(
                    "RuntimeLightingHost",
                    "Token" + std::to_string(index),
                    ""
                )
            );
            std::uint64_t token = 0;
            int startedTick = -1;
            RuntimeLightingMode mode = RuntimeLightingMode::Base;
            if (values.size() != 3U
                || !parseRuntimeUint64Strict(values[0], token)
                || token == 0
                || !runtimeLightingModeFromName(values[1], mode)
                || mode == RuntimeLightingMode::Base
                || !parseRuntimeInt(values[2], startedTick)
                || startedTick < 0
                || startedTick > savedFactoryDeliveryTick
                || !validatedActiveLightingTokens.emplace(
                    token,
                    RuntimeActiveLightingToken{token, mode, startedTick}
                ).second) {
                errorMessage = "invalid v14 lighting token record";
                return false;
            }
        }
        if (validatedLighting.mode()
            != runtimeEffectiveLightingModeForTokens(
                validatedActiveLightingTokens
            )) {
            errorMessage =
                "v14 lighting profile does not match active token authority";
            return false;
        }
    } else if (version == 15) {
        std::uint64_t savedParticleHash = 0;
        const std::array<const char*, 7> particleHeaderKeys{{
            "Version", "Tick", "FixedTickHz", "NextSystemId",
            "NextParticleId", "SystemCount", "ParticleCount"
        }};
        for (const char* key : particleHeaderKeys) {
            if (trimAsciiLocal(
                    ini.getValue("ParticleRuntime", key, "")
                ).empty()) {
                errorMessage =
                    "v15 particle authority is missing header key ";
                errorMessage += key;
                return false;
            }
        }
        RuntimeParticleReconcileReport particleReport;
        if (!parseRuntimeUint64Strict(
                ini.getValue("RuntimeParticleHost", "StateHash", ""),
                savedParticleHash
            )
            || !validatedParticles.load(
                ini,
                state.rules,
                &errorMessage,
                &particleReport
            )
            || particleReport.removedSystems != 0U
            || particleReport.removedParticles != 0U
            || particleReport.stoppedSystems != 0U
            || validatedParticles.currentTick() != savedFactoryDeliveryTick
            || validatedParticles.fixedTickHz()
                != std::max(1, state.config.fixedTickHz)
            || validatedParticles.stateHash() != savedParticleHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v15 particle authority";
            }
            return false;
        }
        std::unordered_set<int> savedObjectIds;
        savedObjectIds.reserve(validationGateObjects.size());
        for (const WorldObjectInstance& object : validationGateObjects) {
            savedObjectIds.insert(object.instanceId);
        }
        for (const RuntimeParticleSystemInstance& system :
                validatedParticles.systems()) {
            if (system.sourceObjectId >= 0
                && !savedObjectIds.contains(system.sourceObjectId)) {
                errorMessage =
                    "v15 particle system has invalid source authority";
                return false;
            }
        }
        for (const RuntimeParticleInstance& particle :
                validatedParticles.particles()) {
            if (particle.sourceObjectId >= 0
                && !savedObjectIds.contains(particle.sourceObjectId)) {
                errorMessage =
                    "v15 particle has missing source object";
                return false;
            }
        }
        (void)validatedWorldAudio.tick(
            runtimeWorldAudioContext(state),
            savedFactoryDeliveryTick
        );
        (void)validatedLighting.tick(savedFactoryDeliveryTick);
    } else {
        (void)validatedWorldAudio.tick(
            runtimeWorldAudioContext(state),
            savedFactoryDeliveryTick
        );
        (void)validatedLighting.tick(savedFactoryDeliveryTick);
        if (!validatedParticles.alignEmptyStateTick(
                savedFactoryDeliveryTick
            )) {
            errorMessage = "could not migrate empty particle authority";
            return false;
        }
    }
*/
    for (int index = 0; index < allianceCount; ++index) {
        const std::string record = ini.getValue(
            "CampaignAlliances",
            std::to_string(index),
            ""
        );
        if (schema != 2) {
            const std::string legacy = toLowerAsciiLocal(
                trimAsciiLocal(record)
            );
            if (!legacy.empty()) {
                alliances.insert(legacy);
            }
            continue;
        }
        const std::vector<std::string> tokens = splitRuntimeSaveFields(record);
        if (tokens.size() != 2U) {
            errorMessage = "invalid campaign alliance schema 2 record";
            return false;
        }
        std::string left = toLowerAsciiLocal(trimAsciiLocal(tokens[0]));
        std::string right = toLowerAsciiLocal(trimAsciiLocal(tokens[1]));
        if (left.empty() || right.empty() || left == right) {
            errorMessage = "invalid campaign alliance owner pair";
            return false;
        }
        if (right < left) {
            std::swap(left, right);
        }
        if (!alliances.insert(left + "|" + right).second) {
            errorMessage = "duplicate campaign alliance owner pair";
            return false;
        }
    }
    return true;
}

void swapRuntimeLoadedTriggers(
    RuntimeTriggerState& live,
    RuntimeTriggerState& loaded) noexcept {
#define RUNTIME_TRIGGER_SWAP(field) \
    swapRuntimeLoadedField(live.field, loaded.field)

    RUNTIME_TRIGGER_SWAP(loaded);
    RUNTIME_TRIGGER_SWAP(bootstrapComplete);
    RUNTIME_TRIGGER_SWAP(truckCrate);
    RUNTIME_TRIGGER_SWAP(trainCrate);
    RUNTIME_TRIGGER_SWAP(publicBuildingLocations);
    RUNTIME_TRIGGER_SWAP(triggerCount);
    RUNTIME_TRIGGER_SWAP(eventCount);
    RUNTIME_TRIGGER_SWAP(actionCount);
    RUNTIME_TRIGGER_SWAP(tagCount);
    RUNTIME_TRIGGER_SWAP(teamTypeCount);
    RUNTIME_TRIGGER_SWAP(taskForceCount);
    RUNTIME_TRIGGER_SWAP(scriptTypeCount);
    RUNTIME_TRIGGER_SWAP(createTeamActionCount);
    RUNTIME_TRIGGER_SWAP(firedTriggers);
    RUNTIME_TRIGGER_SWAP(teamsCreated);
    RUNTIME_TRIGGER_SWAP(unitsCreated);
    RUNTIME_TRIGGER_SWAP(ordersIssued);
    RUNTIME_TRIGGER_SWAP(scriptStepsIssued);
    RUNTIME_TRIGGER_SWAP(scriptStepsCompleted);
    RUNTIME_TRIGGER_SWAP(unsupportedScriptSteps);
    RUNTIME_TRIGGER_SWAP(unsupportedActions);
    RUNTIME_TRIGGER_SWAP(unsupportedEvents);
    RUNTIME_TRIGGER_SWAP(allowLegacyBootstrapForSmokeTest);
    RUNTIME_TRIGGER_SWAP(collectSmokeTimings);
    RUNTIME_TRIGGER_SWAP(smokeCreateTeamMs);
    RUNTIME_TRIGGER_SWAP(smokeIssueOrderMs);
    RUNTIME_TRIGGER_SWAP(status);
    RUNTIME_TRIGGER_SWAP(triggers);
    RUNTIME_TRIGGER_SWAP(teams);
    RUNTIME_TRIGGER_SWAP(taskForces);
    RUNTIME_TRIGGER_SWAP(scripts);
    RUNTIME_TRIGGER_SWAP(teamIds);
    RUNTIME_TRIGGER_SWAP(scriptIds);
    RUNTIME_TRIGGER_SWAP(teamMissions);
    RUNTIME_TRIGGER_SWAP(reinforcements);
    RUNTIME_TRIGGER_SWAP(nextTeamMissionInstanceId);
    RUNTIME_TRIGGER_SWAP(campaignHouseByIndex);
    RUNTIME_TRIGGER_SWAP(campaignCountryByIndex);
    RUNTIME_TRIGGER_SWAP(campaignCanonicalHouseByAlias);
    RUNTIME_TRIGGER_SWAP(campaignCountryByHouse);
    RUNTIME_TRIGGER_SWAP(campaignHouseProduction);
    RUNTIME_TRIGGER_SWAP(campaignAiTriggers);
    RUNTIME_TRIGGER_SWAP(computerAiTriggers);
    RUNTIME_TRIGGER_SWAP(computerTacticsByOwner);
    RUNTIME_TRIGGER_SWAP(campaignTeamBuilds);
    RUNTIME_TRIGGER_SWAP(nextCampaignTeamBuildId);
    RUNTIME_TRIGGER_SWAP(campaignAiTriggerSelections);
    RUNTIME_TRIGGER_SWAP(campaignAiTriggerTeamsCompleted);
    RUNTIME_TRIGGER_SWAP(campaignAiTriggerTeamsFailed);
    RUNTIME_TRIGGER_SWAP(campaignAlliedPairs);
    RUNTIME_TRIGGER_SWAP(campaignLocalVariables);
    RUNTIME_TRIGGER_SWAP(campaignGlobalVariables);
    RUNTIME_TRIGGER_SWAP(campaignCellTagByCell);
    RUNTIME_TRIGGER_SWAP(campaignPreviousCellByObject);
    RUNTIME_TRIGGER_SWAP(campaignPreviousHealthByObject);
    RUNTIME_TRIGGER_SWAP(campaignInitialHealthByObject);
    RUNTIME_TRIGGER_SWAP(campaignPreviousAliveByObject);
    RUNTIME_TRIGGER_SWAP(campaignInitialOwnerByObject);
    RUNTIME_TRIGGER_SWAP(campaignPreviousOwnerByObject);
    RUNTIME_TRIGGER_SWAP(campaignPreviouslyKnownObjects);
    RUNTIME_TRIGGER_SWAP(campaignPreviouslyDiscoveredObjects);
    RUNTIME_TRIGGER_SWAP(campaignPreviousSelectedObjects);
    RUNTIME_TRIGGER_SWAP(campaignAttackedTickByObject);
    RUNTIME_TRIGGER_SWAP(campaignAttackerHouseByObject);
    RUNTIME_TRIGGER_SWAP(campaignDestroyedTickByObject);
    RUNTIME_TRIGGER_SWAP(campaignDestroyerHouseByObject);
    RUNTIME_TRIGGER_SWAP(campaignEnteredTickByTargetObject);
    RUNTIME_TRIGGER_SWAP(campaignEntrantHouseByTargetObject);
    RUNTIME_TRIGGER_SWAP(campaignSpiedTickByTargetObject);
    RUNTIME_TRIGGER_SWAP(campaignSpyHouseByTargetObject);
    RUNTIME_TRIGGER_SWAP(campaignBridgeDestroyedTickByCell);
    RUNTIME_TRIGGER_SWAP(campaignMoviePathByIndex);
    RUNTIME_TRIGGER_SWAP(campaignTeamObjects);
    RUNTIME_TRIGGER_SWAP(campaignMissionTimerStarted);
    RUNTIME_TRIGGER_SWAP(campaignMissionTimerRunning);
    RUNTIME_TRIGGER_SWAP(campaignMissionTimerRemainingTicks);
    RUNTIME_TRIGGER_SWAP(campaignMissionTimerLabel);
    RUNTIME_TRIGGER_SWAP(campaignInputLocked);
    RUNTIME_TRIGGER_SWAP(campaignForceDepth);
    RUNTIME_TRIGGER_SWAP(campaignRadarBlackoutUntilTick);
    RUNTIME_TRIGGER_SWAP(campaignChronoScreenEffectUntilTick);
    RUNTIME_TRIGGER_SWAP(campaignIonStormUntilTick);
    RUNTIME_TRIGGER_SWAP(campaignNextIonStormStrikeTick);
    RUNTIME_TRIGGER_SWAP(campaignRadarEvents);
    RUNTIME_TRIGGER_SWAP(campaignTechLevelByType);
    RUNTIME_TRIGGER_SWAP(campaignBaseCenterByOwner);
    RUNTIME_TRIGGER_SWAP(campaignPreferredSuperWeaponTargetByOwner);
    RUNTIME_TRIGGER_SWAP(buildableTaskForceCache);

#undef RUNTIME_TRIGGER_SWAP
}

void runtimeMultiplayerAuthoritativeMixTriggers(
    std::uint64_t& hash,
    const RuntimeTriggerState& triggers) {
    runtimeMultiplayerAuthoritativeMixBool(hash, triggers.loaded);
    runtimeMultiplayerAuthoritativeMixBool(hash, triggers.bootstrapComplete);
    runtimeStableHashMix(hash, triggers.triggerCount);
    runtimeStableHashMix(hash, triggers.eventCount);
    runtimeStableHashMix(hash, triggers.actionCount);
    runtimeStableHashMix(hash, triggers.tagCount);
    runtimeStableHashMix(hash, triggers.teamTypeCount);
    runtimeStableHashMix(hash, triggers.taskForceCount);
    runtimeStableHashMix(hash, triggers.scriptTypeCount);
    runtimeStableHashMix(hash, triggers.createTeamActionCount);
    runtimeStableHashMix(hash, triggers.firedTriggers);
    runtimeStableHashMix(hash, triggers.teamsCreated);
    runtimeStableHashMix(hash, triggers.unitsCreated);
    runtimeStableHashMix(hash, triggers.ordersIssued);
    runtimeStableHashMix(hash, triggers.scriptStepsIssued);
    runtimeStableHashMix(hash, triggers.scriptStepsCompleted);
    runtimeStableHashMix(hash, triggers.unsupportedScriptSteps);
    runtimeStableHashMix(hash, triggers.unsupportedActions);
    runtimeStableHashMix(hash, triggers.unsupportedEvents);
    runtimeStableHashMixString(hash, serializeRuntimeTriggerReinforcements(triggers.reinforcements));

    std::vector<std::pair<std::string, std::uint64_t>> definitions;
    definitions.reserve(triggers.triggers.size());
    for (const RuntimeTriggerDefinition& trigger : triggers.triggers) {
        std::uint64_t triggerHash = runtimeStableHashString(
            "runtimeMultiplayerTrigger/v1"
        );
        runtimeStableHashMixString(
            triggerHash,
            runtimeMultiplayerCanonicalIdentifier(trigger.id)
        );
        runtimeStableHashMixString(
            triggerHash,
            runtimeCanonicalHumanHouseOwner(trigger.house)
        );
        runtimeStableHashMixString(
            triggerHash,
            runtimeMultiplayerCanonicalIdentifier(trigger.linkedTriggerId)
        );
        runtimeMultiplayerAuthoritativeMixBool(triggerHash, trigger.fired);
        runtimeMultiplayerAuthoritativeMixBool(triggerHash, trigger.enabled);
        runtimeMultiplayerAuthoritativeMixBool(triggerHash, trigger.repeating);
        for (bool enabled : trigger.difficultyEnabled) {
            runtimeMultiplayerAuthoritativeMixBool(triggerHash, enabled);
        }
        runtimeStableHashMix(triggerHash, trigger.activationTick);
        runtimeStableHashMix(triggerHash, trigger.lastFiredTick);
        runtimeStableHashMix(triggerHash, trigger.nextEligibleTick);
        runtimeStableHashMix(triggerHash, trigger.tagRepeatType);
        std::vector<std::string> tags;
        tags.reserve(trigger.tagIds.size());
        for (const std::string& tag : trigger.tagIds) {
            tags.push_back(runtimeMultiplayerCanonicalIdentifier(tag));
        }
        std::sort(tags.begin(), tags.end());
        runtimeStableHashMix(triggerHash, static_cast<long long>(tags.size()));
        for (const std::string& tag : tags) {
            runtimeStableHashMixString(triggerHash, tag);
        }
        std::vector<int> remaining(
            trigger.remainingOnceAllObjectIds.begin(),
            trigger.remainingOnceAllObjectIds.end()
        );
        std::sort(remaining.begin(), remaining.end());
        runtimeStableHashMix(
            triggerHash,
            static_cast<long long>(remaining.size())
        );
        for (int objectId : remaining) {
            runtimeStableHashMix(triggerHash, objectId);
        }
        runtimeStableHashMix(
            triggerHash,
            static_cast<long long>(trigger.events.size())
        );
        for (const RuntimeTriggerEvent& event : trigger.events) {
            runtimeStableHashMix(triggerHash, event.type);
            runtimeStableHashMix(triggerHash, event.authoredOrdinal);
            runtimeStableHashMix(triggerHash, event.lightingEventId);
            runtimeStableHashMix(
                triggerHash,
                static_cast<long long>(event.arguments.size())
            );
            for (const std::string& argument : event.arguments) {
                runtimeStableHashMixString(triggerHash, argument);
            }
        }
        runtimeStableHashMix(
            triggerHash,
            static_cast<long long>(trigger.actions.size())
        );
        for (const RuntimeTriggerAction& action : trigger.actions) {
            runtimeStableHashMix(triggerHash, action.type);
            runtimeStableHashMix(triggerHash, action.authoredOrdinal);
            runtimeStableHashMixString(
                triggerHash,
                runtimeMultiplayerCanonicalIdentifier(action.targetId)
            );
            runtimeStableHashMix(
                triggerHash,
                static_cast<long long>(action.arguments.size())
            );
            for (const std::string& argument : action.arguments) {
                runtimeStableHashMixString(triggerHash, argument);
            }
        }
        definitions.emplace_back(
            runtimeMultiplayerCanonicalIdentifier(trigger.id),
            triggerHash
        );
    }
    std::sort(definitions.begin(), definitions.end());
    runtimeStableHashMix(hash, static_cast<long long>(definitions.size()));
    for (const auto& [triggerId, triggerHash] : definitions) {
        runtimeStableHashMixString(hash, triggerId);
        runtimeMultiplayerAuthoritativeMixUnsigned(hash, triggerHash);
    }

    runtimeStableHashMix(hash, static_cast<long long>(triggers.teams.size()));
    for (const auto& [teamId, team] : triggers.teams) {
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(teamId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(team.id)
        );
        runtimeStableHashMixString(
            hash,
            runtimeCanonicalHumanHouseOwner(team.house)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(team.waypointId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(team.scriptId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(team.taskForceId)
        );
        runtimeStableHashMix(hash, team.priority);
        runtimeStableHashMix(hash, team.max);
        runtimeStableHashMix(hash, team.group);
        runtimeMultiplayerAuthoritativeMixBool(hash, team.autocreate);
        runtimeMultiplayerAuthoritativeMixBool(hash, team.prebuild);
        runtimeMultiplayerAuthoritativeMixBool(hash, team.reinforce);
        runtimeMultiplayerAuthoritativeMixBool(hash, team.full);
        runtimeMultiplayerAuthoritativeMixBool(hash, team.aggressive);
        for (bool value : {team.isBaseDefense, team.recruitable, team.recruiter,
                team.looseRecruit, team.avoidThreats, team.suicide, team.guardSlower,
                team.onlyTargetHouseEnemy, team.transportsReturnOnUnload})
            runtimeMultiplayerAuthoritativeMixBool(hash, value);
        runtimeStableHashMix(hash, team.veteranLevel);
        runtimeStableHashMix(hash, team.mindControlDecision);
    }
#include "simulation/ai/RuntimeComputerTacticsHash.inl"
    runtimeStableHashMix(
        hash,
        static_cast<long long>(triggers.taskForces.size())
    );
    for (const auto& [taskForceId, taskForce] : triggers.taskForces) {
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(taskForceId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(taskForce.id)
        );
        runtimeStableHashMix(
            hash,
            static_cast<long long>(taskForce.members.size())
        );
        runtimeStableHashMix(hash, taskForce.group);
        for (const RuntimeTaskForceMember& member : taskForce.members) {
            runtimeStableHashMix(hash, member.count);
            runtimeStableHashMixString(
                hash,
                runtimeMultiplayerCanonicalIdentifier(member.unitId)
            );
        }
    }
    runtimeStableHashMix(hash, static_cast<long long>(triggers.scripts.size()));
    for (const auto& [scriptId, script] : triggers.scripts) {
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(scriptId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(script.id)
        );
        runtimeStableHashMix(
            hash,
            static_cast<long long>(script.steps.size())
        );
        for (const RuntimeScriptStep& step : script.steps) {
            runtimeStableHashMix(hash, step.action);
            runtimeStableHashMix(hash, step.argument);
        }
    }
    const auto mixIdentifierSequence = [&hash](
        const std::vector<std::string>& values
    ) {
        runtimeStableHashMix(hash, static_cast<long long>(values.size()));
        for (const std::string& value : values) {
            runtimeStableHashMixString(
                hash,
                runtimeMultiplayerCanonicalIdentifier(value)
            );
        }
    };
    mixIdentifierSequence(triggers.teamIds);
    mixIdentifierSequence(triggers.scriptIds);

    runtimeStableHashMix(
        hash,
        static_cast<long long>(triggers.teamMissions.size())
    );
    for (const auto& [missionId, mission] : triggers.teamMissions) {
        runtimeStableHashMix(hash, missionId);
        runtimeStableHashMix(hash, mission.instanceId);
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(mission.teamTypeId)
        );
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(mission.scriptId)
        );
        runtimeStableHashMix(
            hash,
            static_cast<long long>(mission.objectIds.size())
        );
        for (int objectId : mission.objectIds) {
            runtimeStableHashMix(hash, objectId);
        }
        runtimeStableHashMix(hash, static_cast<long long>(mission.stepIndex));
        runtimeStableHashMix(hash, mission.stepStartedTick);
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.stepIssued);
        runtimeStableHashMix(hash, mission.targetObjectId);
        runtimeMultiplayerAuthoritativeMixCell(hash, mission.targetCell);
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.completed);
        runtimeStableHashMixString(
            hash,
            runtimeMultiplayerCanonicalIdentifier(mission.sourceAiTriggerId)
        );
        runtimeStableHashMix(hash, mission.sourceAiBuildId);
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.aiOutcomeReported);
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.achievedGreatSuccess);
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.targetInteractionCompleted);
        runtimeStableHashMix(hash, static_cast<long long>(mission.transportReturnCells.size()));
        for (const auto& [id, cell] : mission.transportReturnCells) {
            runtimeStableHashMix(hash, id);
            runtimeStableHashMix(hash, cell.x);
            runtimeStableHashMix(hash, cell.y);
        }
        runtimeMultiplayerAuthoritativeMixBool(hash, mission.regrouping);
        runtimeStableHashMix(hash, mission.regroupCell.x);
        runtimeStableHashMix(hash, mission.regroupCell.y);
        runtimeStableHashMix(hash, mission.regroupStartedTick);
        runtimeStableHashMix(hash, static_cast<long long>(mission.regroupDestinations.size()));
        for (const auto& [id, cell] : mission.regroupDestinations) {
            runtimeStableHashMix(hash, id); runtimeStableHashMix(hash, cell.x); runtimeStableHashMix(hash, cell.y);
        }
    }
    runtimeStableHashMix(hash, triggers.nextTeamMissionInstanceId);

    const auto mixIntIntMap = [&hash](const auto& values) {
        std::vector<std::pair<int, int>> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        runtimeStableHashMix(hash, static_cast<long long>(sorted.size()));
        for (const auto& [key, value] : sorted) {
            runtimeStableHashMix(hash, key);
            runtimeStableHashMix(hash, value);
        }
    };
    const auto mixIntStringMap = [&hash](const auto& values) {
        std::vector<std::pair<int, std::string>> sorted;
        sorted.reserve(values.size());
        for (const auto& [key, value] : values) {
            sorted.emplace_back(
                key,
                runtimeMultiplayerCanonicalIdentifier(value)
            );
        }
        std::sort(sorted.begin(), sorted.end());
        runtimeStableHashMix(hash, static_cast<long long>(sorted.size()));
        for (const auto& [key, value] : sorted) {
            runtimeStableHashMix(hash, key);
            runtimeStableHashMixString(hash, value);
        }
    };
    mixIntStringMap(triggers.campaignHouseByIndex);
    mixIntStringMap(triggers.campaignCountryByIndex);
    mixIntIntMap(triggers.campaignLocalVariables);
    mixIntIntMap(triggers.campaignGlobalVariables);
    mixIntStringMap(triggers.campaignCellTagByCell);
    mixIntIntMap(triggers.campaignPreviousCellByObject);
    mixIntIntMap(triggers.campaignPreviousHealthByObject);
    mixIntIntMap(triggers.campaignInitialHealthByObject);
    {
        std::vector<std::pair<int, int>> values;
        values.reserve(triggers.campaignPreviousAliveByObject.size());
        for (const auto& [objectId, alive] :
                triggers.campaignPreviousAliveByObject) {
            values.emplace_back(objectId, alive ? 1 : 0);
        }
        std::sort(values.begin(), values.end());
        runtimeStableHashMix(hash, static_cast<long long>(values.size()));
        for (const auto& [objectId, alive] : values) {
            runtimeStableHashMix(hash, objectId);
            runtimeStableHashMix(hash, alive);
        }
    }
    mixIntStringMap(triggers.campaignInitialOwnerByObject);
    mixIntStringMap(triggers.campaignPreviousOwnerByObject);
    mixIntIntMap(triggers.campaignAttackedTickByObject);
    mixIntStringMap(triggers.campaignAttackerHouseByObject);
    mixIntIntMap(triggers.campaignDestroyedTickByObject);
    mixIntStringMap(triggers.campaignDestroyerHouseByObject);
    mixIntIntMap(triggers.campaignEnteredTickByTargetObject);
    mixIntStringMap(triggers.campaignEntrantHouseByTargetObject);
    mixIntIntMap(triggers.campaignSpiedTickByTargetObject);
    mixIntStringMap(triggers.campaignSpyHouseByTargetObject);
    mixIntIntMap(triggers.campaignBridgeDestroyedTickByCell);

    const auto mixIntSet = [&hash](const auto& values) {
        std::vector<int> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        runtimeStableHashMix(hash, static_cast<long long>(sorted.size()));
        for (int value : sorted) {
            runtimeStableHashMix(hash, value);
        }
    };
    mixIntSet(triggers.campaignPreviouslyKnownObjects);
    mixIntSet(triggers.campaignPreviouslyDiscoveredObjects);
    // campaignPreviousSelectedObjects is a process-local observation of UI
    // selection and is deliberately excluded.

    runtimeMultiplayerAuthoritativeMixBool(
        hash,
        triggers.campaignMissionTimerStarted
    );
    runtimeMultiplayerAuthoritativeMixBool(
        hash,
        triggers.campaignMissionTimerRunning
    );
    runtimeStableHashMix(hash, triggers.campaignMissionTimerRemainingTicks);
    runtimeMultiplayerAuthoritativeMixBool(hash, triggers.campaignInputLocked);
    runtimeStableHashMix(hash, triggers.campaignForceDepth);
    runtimeStableHashMix(hash, triggers.campaignRadarBlackoutUntilTick);
    runtimeStableHashMix(hash, triggers.campaignChronoScreenEffectUntilTick);
    runtimeStableHashMix(hash, triggers.campaignIonStormUntilTick);
    runtimeStableHashMix(hash, triggers.campaignNextIonStormStrikeTick);
    runtimeStableHashMix(
        hash,
        static_cast<long long>(triggers.campaignRadarEvents.size())
    );
    for (const RuntimeCampaignRadarEvent& event :
            triggers.campaignRadarEvents) {
        runtimeMultiplayerAuthoritativeMixCell(hash, event.cell);
        runtimeStableHashMix(hash, event.eventType);
        runtimeStableHashMix(hash, event.untilTick);
    }
}

void recordRuntimeCooperativeSnapshotDifference(const RuntimeProductionHostState& live,
    const RuntimeProductionHostState& restored) {
    if (!std::getenv("CONCORD_COMMAND_SMOKE_NETWORK_COOPERATIVE_ONLY")
        && !std::getenv("CONCORD_COMMAND_SMOKE_NETWORK_COOPERATIVE_LAN_ONLY")) return;
    for (const auto& entry : {std::pair{"live", &live}, std::pair{"restored", &restored}}) {
        auto path = std::filesystem::path("build/smoke/gameplay/06-world-probe")
            / (std::string(entry.first) + "-snapshot-difference.ini");
        if (const char* directory = std::getenv("CONCORD_COOPERATIVE_LAN_DIRECTORY")) {
            const char* role = std::getenv("CONCORD_COOPERATIVE_LAN_ROLE");
            path = std::filesystem::path(directory)
                / (std::string(role ? role : "peer") + '-' + entry.first + "-snapshot-difference.ini");
        }
        std::ofstream output(path);
        std::string reason;
        (void)writeRuntimeStateToStream(*entry.second, output, reason);
        (void)writeRuntimeMultiplayerSnapshotExtension(*entry.second, output, reason);
        std::vector<std::pair<std::string, std::uint64_t>> trace;
        (void)runtimeMultiplayerAuthoritativeStateHash(*entry.second, reason, &trace);
        output << "\n[DiagnosticHashTrace]\n";
        for (const auto& [key, value] : trace) output << key << '=' << value << '\n';
        output << "\n[CooperativeDiagnostic]\nReason=" << reason << '\n';
        const auto& world = entry.second->world;
        output << "\n[DiagnosticObjects]\n" << std::setprecision(17);
        for (const auto& object : world.getObjects()) {
            output << object.instanceId << '=';
            output << object.instanceId;
            output << '|';
            output << static_cast<int>(object.kind);
            output << '|';
            output << iniHexEncode(object.mapId);
            output << '|';
            output << iniHexEncode(object.owner);
            output << '|';
            output << iniHexEncode(object.type);
            output << '|';
            output << object.health;
            output << '|';
            output << object.x;
            output << '|';
            output << object.y;
            output << '|';
            output << object.cell;
            output << '|';
            output << object.level;
            output << '|';
            output << object.facing;
            output << '|';
            output << object.turretFacing;
            output << '|';
            output << object.subCell;
            output << '|';
            output << iniHexEncode(object.mission);
            output << '|';
            output << iniHexEncode(object.tag);
            output << '|';
            output << object.veterancy;
            output << '|';
            output << object.veterancyExperience;
            output << '|';
            output << object.crateArmorMultiplier;
            output << '|';
            output << object.crateFirepowerMultiplier;
            output << '|';
            output << object.crateSpeedMultiplier;
            output << '|';
            output << object.crateCloakable;
            output << '|';
            output << object.harvestAutoSearchSuspended;
            output << '|';
            output << object.hasBeenCaptured;
            output << '|';
            output << object.onBridge;
            output << '|';
            output << object.authoredAircraft;
            output << '|';
            output << object.alive;
            output << '|';
            output << object.rubble;
            output << '|';
            output << object.submerged;
            output << '|';
            output << object.cloaked;
            output << '|';
            output << object.cloakCooldownTicks;
            output << '|';
            output << object.submergeCooldownTicks;
            output << '|';
            output << object.deployed;
            output << '|';
            output << object.deployStateTicksRemaining;
            output << '|';
            output << object.prone;
            output << '|';
            output << object.proneTicksRemaining;
            output << '|';
            output << object.weaponCooldownTicksRemaining;
            output << '|';
            output << object.radiationEruptionCooldownTicksRemaining;
            output << '|';
            output << object.hasRenderPosition;
            output << '|';
            output << object.renderX;
            output << '|';
            output << object.renderY;
            output << '|';
            output << object.renderMoveActive;
            output << '|';
            output << object.renderLevel;
            output << '|';
            output << object.renderOnBridge;
            output << '|';
            output << object.movementAnimationDistance;
            output << '|';
            output << object.buildAnimTicksTotal;
            output << '|';
            output << object.buildAnimTicksRemaining;
            output << '|';
            output << object.structureRecoilStartTick;
            output << '|';
            output << object.chargeTurretStartTick;
            output << '|';
            output << object.workAnimationActive;
            output << '|';
            output << object.poweredOperational;
            output << '|';
            output << object.powerAnimationPausedAtTick;
            output << '|';
            output << object.powerAnimationPausedTicks;
            output << '|';
            output << object.factoryExitAnimationStartTick;
            output << '|';
            output << object.factoryExitAnimationEndTick;
            output << '|';
            output << object.factoryExitAirborne;
            output << '|';
            output << object.factoryExitHostId;
            output << '|';
            output << object.specialCashNextTick;
            output << '|';
            output << object.specialCloneDeliveryObjectId;
            output << '|';
            for (int id : object.transportPassengerIds) output << id << ',';
            output << '|';
            output << object.activeGunnerMode;
            output << '|';
            output << object.activeGunnerWeaponIndex;
            output << '|';
            output << object.activeGunnerTurretIndex;
            output << '|';
            output << object.tankBunkerHostId;
            output << '|';
            output << object.tankBunkerOccupantId;
            output << '|';
            output << object.tankBunkerAnimationStartTick;
            output << '|';
            output << object.tankBunkerLowering;
            output << '|';
            output << object.dockedToObjectId;
            output << '|';
            output << object.dockIndex;
            output << '|';
            output << object.airborne;
            output << '|';
            output << object.flightLevel;
            output << '|';
            output << object.wallConnectionMask;
            output << '|';
            output << object.garrisonHostObjectId;
            output << '|';
            for (int id : object.garrisonOccupantIds) output << id << ',';
            output << '|';
            output << object.garrisonFireCursor;
            output << '|';
            output << object.currentAmmo;
            output << '\n';
        }
        output << "\n[DiagnosticTerrain]\n";
        for (const auto& t : world.getTerrainCells()) {
            output << t.cellIndex512 << '=' << t.x << '|' << t.y << '|' << t.tileIndex << '|'
                << t.subTileIndex << '|' << t.level << '|' << t.iceGrowth << '|' << t.hasOverlay << '|'
                << t.overlayIndex << '|' << t.overlayFrame << '|' << t.overlayType << '|' << t.overlayImage << '|'
                << t.overlayIsOre << '|' << t.overlayIsGem << '\n';
        }
        output << "\n[DiagnosticTerrainObjects]\n";
        for (const auto& t : world.getTerrainObjects()) {
            output << t.mapId << '=' << t.type << '|' << t.tag << '|' << t.x << '|' << t.y << '|'
                << t.cell << '|' << t.level << '|' << t.strength << '|' << t.health << '|' << t.immune << '|'
                << t.alive << '|' << t.occupationBits << '|' << t.footprintWidth << '|' << t.footprintHeight << '|';
            for (int subcell : t.occupiedSubCells) output << subcell << ',';
            output << '\n';
        }
        output << "\n[DiagnosticWaypoints]\n";
        for (const auto& w : world.getWaypoints())
            output << w.id << '=' << w.x << '|' << w.y << '|' << w.cell << '\n';
    }
}

void commitRuntimeComputerMiningWork(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeEconomyState& work, int startingCredits) {
    const std::string key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    auto& ledger = state.computerMiningByOwner[key];
    ledger.storedResourceUnits = work.storedResourceUnits;
    ledger.storedResourceValue = work.storedResourceValue;
    ledger.storedResourceGemUnits = work.storedResourceGemUnits;
    ledger.resourceTrips = work.resourceTrips;
    ledger.harvesterTeleports = work.harvesterTeleports;
    ledger.slaveMinerLoads = work.slaveMinerLoads;
    const int income = std::max(0, work.credits - startingCredits);
    depositRuntimeRepairDepotCredits(state, key, income);
    const auto ai = state.computerAiByOwner.find(key);
    if (!state.campaignMode && ai != state.computerAiByOwner.end()) {
        ai->second.incomeEarned = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
            static_cast<long long>(ai->second.incomeEarned) + income));
    }
}

bool advanceRuntimeComputerMining(RuntimeProductionHostState& state) {
    if (runtimeComputerMiningDiagnosticOverride) return runtimeComputerMiningDiagnosticOverride(state);
    if (!state.rulesLoaded || state.humanHouseScopeActive
        || state.skirmishMatchFinished) return false;
    std::vector<std::string> owners;
    if (state.campaignMode) {
        for (const auto& [key, house] : state.triggers.campaignHouseProduction) {
            if (!house.playerControlled && house.side >= 0
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(house.owner);
        }
    } else {
        if (!runtimeComputerAiEnabledForSession(state)
            || !state.economy.skirmishStartApplied) return false;
        ensureRuntimeComputerAiStates(state);
        for (const auto& [key, ai] : state.computerAiByOwner) {
            if (ai.initialized && !findRuntimeHumanHouseAuthority(state, ai.owner)
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(ai.owner);
        }
    }
    std::sort(owners.begin(), owners.end());
    bool changed = false;
    for (const std::string& owner : owners) {
        RuntimeEconomyState ordinaryWork;
        ordinaryWork.owner = owner;
        ordinaryWork.credits = runtimeRepairDepotCreditBalance(state, owner);
        const auto& ledger = state.computerMiningByOwner[runtimeRepairDepotCanonicalOwnerId(state, owner)];
        ordinaryWork.storedResourceUnits = ledger.storedResourceUnits;
        ordinaryWork.storedResourceValue = ledger.storedResourceValue;
        ordinaryWork.storedResourceGemUnits = ledger.storedResourceGemUnits;
        ordinaryWork.resourceTrips = ledger.resourceTrips;
        ordinaryWork.harvesterTeleports = ledger.harvesterTeleports;
        ordinaryWork.slaveMinerLoads = ledger.slaveMinerLoads;
        ordinaryWork.skirmishStartApplied = state.economy.skirmishStartApplied;
        refreshRuntimeEconomyMetricsForOwner(state, ordinaryWork);
        const int ordinaryStartingCredits = ordinaryWork.credits;
        try {
            RuntimeComputerMiningFeedbackScope feedback(state);
            auto context = makeRuntimeHouseEconomyContext(ordinaryWork, true);
            changed = advanceRuntimeHarvesterEconomy(state, context, ordinaryWork) || changed;
            changed = advanceRuntimeMobileSlaveMinerEconomy(state, context, ordinaryWork) || changed;
            changed = advanceRuntimeSlaveMinerEconomy(state, context, ordinaryWork) || changed;
        } catch (...) {
            commitRuntimeComputerMiningWork(state, owner, ordinaryWork, ordinaryStartingCredits);
            throw;
        }
        commitRuntimeComputerMiningWork(state, owner, ordinaryWork, ordinaryStartingCredits);
    }
    return changed;
}

void triggerRuntimeWeaponVisuals(
    RuntimeGameSessionState& state,
    WorldObjectInstance& attacker) {
    const UnitType* unit = attacker.kind == WorldObjectKind::Unit
        ? findRuntimeUnitTypeById(state.rules, attacker.type)
        : nullptr;
    const BuildingType* building =
        attacker.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, attacker.type)
        : nullptr;
    if ((unit != nullptr && unit->chargeTurret && unit->turretCount > 1)
        || (building != nullptr
            && building->chargeTurret
            && building->turretCount > 1)) {
        // Trigger once per real discharge. The renderer walks the authored
        // highest-to-lowest turret voxels at five ticks per stage.
        attacker.chargeTurretStartTick = state.tick;
    }
    if (building == nullptr
        || !building->turretRecoil
        || (building->turretTravel <= 0
            && building->barrelTravel <= 0)) {
        return;
    }
    // Projectile bursts and shrapnel must not restart the authored recoil
    // sequence for every spawned actor.
    attacker.structureRecoilStartTick = state.tick;
}

bool runtimeSkirmishAllowsStructureUndeploy(
    const RuntimeGameSessionState& state,
    const BuildingType& building) {
    return !building.constructionYard || state.config.mcvRepacks;
}

std::vector<RuntimeCellPoint> runtimeSkirmishStartCandidates(
    const RuntimeGameSessionState& state) {
    struct Candidate {
        int priority = 1000;
        RuntimeCellPoint cell;
    };

    const int authorizedStartLimit = resolveRuntimeMapStartingPointLimit(state.config);
    const int startLimit = authorizedStartLimit > 0
        ? std::min(8, authorizedStartLimit) : 8;
    std::vector<Candidate> candidates;
    for (const WorldWaypoint& waypoint : state.world.getWaypoints()) {
        int id = 0;
        if (!parseRuntimeInt(waypoint.id, id)) {
            continue;
        }
        if (id >= 0 && id < startLimit) {
            candidates.push_back({id, {waypoint.x, waypoint.y}});
        }
    }

    if (candidates.empty() && authorizedStartLimit <= 0) {
        for (const WorldWaypoint& waypoint : state.world.getWaypoints()) {
            candidates.push_back({1000, {waypoint.x, waypoint.y}});
        }
    }

    if (candidates.empty()) {
        const GameWorldSummary& summary = state.world.getSummary();
        if (summary.minCellX >= 0 && summary.minCellY >= 0) {
            candidates.push_back({
                2000,
                {
                    (summary.minCellX + summary.maxCellX) / 2,
                    (summary.minCellY + summary.maxCellY) / 2
                }
            });
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.priority < right.priority;
        }
    );

    std::vector<RuntimeCellPoint> cells;
    cells.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        cells.push_back(candidate.cell);
    }
    return cells;
}

bool runtimeComputerPlacementPreservesExits(const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item, RuntimeCellPoint cell, const std::string& owner) {
    WorldObjectInstance candidate;
    candidate.kind = WorldObjectKind::Structure; candidate.type = item.typeId;
    candidate.owner = owner; candidate.x = cell.x; candidate.y = cell.y;
    const auto footprint = runtimeStructureFootprintForObject(state, candidate);
    const auto overlaps = [&](RuntimeCellPoint point) {
        return point.x >= cell.x && point.y >= cell.y
            && point.x < cell.x + footprint.width && point.y < cell.y + footprint.height;
    };
    for (const auto& factory : state.world.getObjects()) {
        if (!factory.alive || factory.kind != WorldObjectKind::Structure
            || toLowerAsciiLocal(factory.owner) != toLowerAsciiLocal(owner)) continue;
        const auto* type = findRuntimeBuildingType(state.rules, factory);
        // Shipyards also have WeaponsFactory=yes. They use the naval spawn
        // search around the foundation, not the land factory's east corridor.
        if (!type || !type->weaponsFactory || type->naval || type->waterBound
            || toUpperAsciiLocal(type->factory) != "UNITTYPE") continue;
        const auto shape = runtimeStructureFootprintForObject(state, factory);
        const RuntimeCellPoint door{factory.x + shape.width, factory.y + shape.height / 2};
        for (int outward = 0; outward < 3; ++outward)
            if (overlaps({door.x + outward, door.y})) return false;
    }
    const auto* type = findRuntimeBuildingTypeById(state.rules, item.typeId);
    if (type && type->weaponsFactory && !type->naval && !type->waterBound
        && toUpperAsciiLocal(type->factory) == "UNITTYPE") {
        const RuntimeCellPoint door{cell.x + footprint.width, cell.y + footprint.height / 2};
        for (int outward = 0; outward < 3; ++outward) {
            const RuntimeCellPoint point{door.x + outward, door.y};
            const auto* terrain = runtimeProductionTerrainCellAt(state, point);
            if (!terrain) return false;
            const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
            if (!info.passable || (info.water && !info.bridge && !info.shore)) return false;
            for (const auto& other : state.world.getObjects()) {
                if (!other.alive || other.kind != WorldObjectKind::Structure) continue;
                const auto shape = runtimeStructureFootprintForObject(state, other);
                if (point.x >= other.x && point.y >= other.y
                    && point.x < other.x + shape.width && point.y < other.y + shape.height) return false;
            }
        }
    }
    return true;
}

std::uint32_t runtimeSkirmishStartSeed(
    const RuntimeGameSessionState& state,
    std::size_t slotIndex) {
    std::uint32_t hash = 2166136261u;
    const auto append = [&hash](const std::string& value) {
        for (unsigned char byte : value) {
            hash ^= static_cast<std::uint32_t>(byte);
            hash *= 16777619u;
        }
    };
    if (state.config.multiplayerActive) {
        // Local peers may mount the same verified map at different paths.
        // Multiplayer start assignment therefore derives only from the
        // canonical setup/map identity and shared simulation seed.
        append(toLowerAsciiLocal(
            state.config.multiplayerCanonicalMapId
        ));
        append("|");
        append(std::to_string(state.config.multiplayerSetupHash));
        append("|");
        append(std::to_string(state.config.simulationRandomSeed));
    } else {
        append(toLowerAsciiLocal(state.config.defaultMapPath));
    }
    append("|");
    append(toLowerAsciiLocal(state.config.gameMode));
    hash ^= static_cast<std::uint32_t>(slotIndex + 1u);
    hash *= 16777619u;
    return hash;
}

std::vector<int> resolveRuntimeSkirmishStartAssignments(
    const RuntimeGameSessionState& state,
    int candidateCount,
    const std::vector<std::vector<bool>>& legalBySlot) {
    std::vector<int> assignments(state.config.playerSlots.size(), -1);
    if (candidateCount <= 0) {
        return assignments;
    }

    const auto legal = [&legalBySlot](
        std::size_t slotIndex,
        int candidateIndex
    ) {
        if (legalBySlot.empty()) {
            return true;
        }
        return slotIndex < legalBySlot.size()
            && candidateIndex >= 0
            && static_cast<std::size_t>(candidateIndex)
                < legalBySlot[slotIndex].size()
            && legalBySlot[slotIndex][
                static_cast<std::size_t>(candidateIndex)
            ];
    };

    std::vector<bool> claimed(static_cast<std::size_t>(candidateCount), false);

    // Reserve all valid, unique explicit choices before assigning any random
    // slot.  Thus a random slot can never steal a later player's requested
    // numbered position.  Conflicts are won deterministically by slot order.
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || runtimeObserverSlot(slot) || slot.startPosition <= 0) {
            continue;
        }
        const int candidateIndex = slot.startPosition - 1;
        if (candidateIndex < 0
            || candidateIndex >= candidateCount
            || claimed[static_cast<std::size_t>(candidateIndex)]
            || !legal(slotIndex, candidateIndex)) {
            continue;
        }
        assignments[slotIndex] = candidateIndex;
        claimed[static_cast<std::size_t>(candidateIndex)] = true;
    }

    // Random, conflicting, out-of-range, and illegal explicit choices all
    // share the same stable pseudo-random fallback.  The seed contains only
    // match configuration and slot identity, so replaying the same setup
    // yields the same unique assignment without relying on global rand().
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || runtimeObserverSlot(slot) || assignments[slotIndex] >= 0) {
            continue;
        }
        const std::vector<int> order = runtimeDeterministicStartCandidateOrder(
            state,
            slotIndex,
            candidateCount
        );
        for (int candidateIndex : order) {
            if (claimed[static_cast<std::size_t>(candidateIndex)]
                || !legal(slotIndex, candidateIndex)) {
                continue;
            }
            assignments[slotIndex] = candidateIndex;
            claimed[static_cast<std::size_t>(candidateIndex)] = true;
            break;
        }
    }

    return assignments;
}

std::vector<std::vector<bool>> runtimeSkirmishStartCandidateLegality(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& startCandidates) {
    std::vector<std::vector<bool>> legalBySlot(
        state.config.playerSlots.size(),
        std::vector<bool>(startCandidates.size(), false)
    );
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || runtimeObserverSlot(slot)) {
            continue;
        }
        const std::string owner = runtimeSkirmishOwnerForSlot(slotIndex);
        const std::string country = runtimeCountryForOwner(state, owner);
        const std::optional<RuntimeBuildCatalogItem> constructionYardItem =
            runtimeBuildItemForBuilding(
                state,
                runtimeConstructionYardForCountry(country)
            );
        if (!constructionYardItem.has_value()) {
            continue;
        }
        for (std::size_t candidateIndex = 0;
            candidateIndex < startCandidates.size();
            ++candidateIndex) {
            legalBySlot[slotIndex][candidateIndex] =
                findRuntimePlacementCellNear(
                    state,
                    *constructionYardItem,
                    startCandidates[candidateIndex],
                    16
                ).has_value();
        }
    }
    return legalBySlot;
}

std::optional<std::size_t> runtimeLocalSkirmishSlotIndex(
    const RuntimeConfig& config) {
    std::optional<std::size_t> firstHuman;
    std::optional<std::size_t> firstEnabled;
    for (std::size_t slotIndex = 0;
        slotIndex < config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot = config.playerSlots[slotIndex];
        if (!slot.enabled) {
            continue;
        }
        if (!firstEnabled.has_value()) {
            firstEnabled = slotIndex;
        }
        if (slot.human && slot.local) {
            return slotIndex;
        }
        if (slot.human && !firstHuman.has_value()) {
            firstHuman = slotIndex;
        }
    }
    return firstHuman.has_value() ? firstHuman : firstEnabled;
}

int spawnRuntimeSkirmishStartingUnits(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::string& country,
    const RuntimeCellPoint& origin,
    int requestedCount) {
    int spawned = 0;
    const int adjustedCount = runtimeDifficultyAdjustedStartingUnits(
        state,
        owner,
        requestedCount
    );
    const RuntimeSkirmishStartingUnitPools pools =
        runtimeSkirmishStartingUnitPoolsForCountry(state, country);
    const std::vector<const UnitType*> extraMcvTypes =
        runtimeUnholyAllianceAdditionalMcvTypes(state, country);
    const RuntimeSkirmishStartingConnectivity startingConnectivity(
        state, owner, origin,
        adjustedCount > 0 ? pools : RuntimeSkirmishStartingUnitPools{}, extraMcvTypes);
    // Place additional MCVs before troops so every initial base can keep its
    // complete future foundation clear, including Unholy Alliance starts.
    const int extraMcvs = spawnRuntimeUnholyAllianceMcvs(
        state, owner, origin, extraMcvTypes, startingConnectivity);
    std::unordered_set<int> reservedDeploymentCells;
    int frontDistance = 4;
    if (const auto yard = runtimeBuildItemForBuilding(state, runtimeConstructionYardForCountry(country)))
        frontDistance = std::max(frontDistance, runtimeStructureFootprintForBuildItem(state, *yard).width);
    for (const auto& object : state.world.getObjects()) {
        const auto* type = object.alive ? findRuntimeUnitType(state.rules, object) : nullptr;
        if (!type || !runtimeUnitTypeIsMcv(state, *type)) continue;
        const auto yard = runtimeBuildItemForBuilding(state, type->deploysInto);
        if (!yard) continue;
        const auto footprint = runtimeStructureFootprintForBuildItem(state, *yard);
        for (const auto& cell : runtimeStructureFootprintCells(
                runtimePlacementTopLeftForAnchor(runtimeCellPointForObject(object), footprint), footprint))
            reservedDeploymentCells.insert(runtimeCellKey(cell.x, cell.y));
    }
    std::size_t infantryCursor = 0;
    std::size_t vehicleCursor = 0;
    for (int index = 0; index < adjustedCount; ++index) {
        const bool chooseVehicle = !pools.vehicles.empty()
            && (pools.infantry.empty() || (index % 3) == 0);
        const std::vector<const UnitType*>& category = chooseVehicle
            ? pools.vehicles
            : pools.infantry;
        if (category.empty()) {
            continue;
        }
        std::size_t& cursor = chooseVehicle
            ? vehicleCursor
            : infantryCursor;
        const UnitType* unit = category[cursor % category.size()];
        ++cursor;
        if (unit == nullptr) {
            continue;
        }

        const RuntimeBuildKind kind = chooseVehicle
            ? RuntimeBuildKind::Unit
            : RuntimeBuildKind::Infantry;
        WorldObjectInstance probe;
        probe.instanceId = -1;
        probe.kind = runtimeKindForBuildItem(kind);
        probe.owner = owner;
        probe.type = unit->id;
        probe.alive = true;

        // The reference opening has vehicles around the north/right half and
        // infantry groups around the left/bottom half. Never use the center
        // slot. Preserve the authored mix and count; only the positions change.
        const std::size_t ordinal = cursor - 1;
        const int band = static_cast<int>(ordinal / (chooseVehicle ? 5U : 10U));
        const int ring = 5 + 2 * band;
        RuntimeCellPoint offset;
        if (chooseVehicle) {
            const std::array<RuntimeCellPoint,5> slots{{{-ring,-ring},{0,-ring},
                {ring,-ring},{ring,0},{ring,ring}}};
            offset = slots[ordinal % slots.size()];
        } else {
            const int slot = ordinal % 10 == 9 ? 3 : static_cast<int>((ordinal % 10) % 3);
            const std::array<RuntimeCellPoint,4> slots{{{-ring,0},{-ring,ring},
                {0,ring},{frontDistance+2*band,0}}};
            offset = slots[slot];
        }
        const RuntimeCellPoint wanted{origin.x + offset.x, origin.y + offset.y};
        const std::optional<RuntimeCellPoint> spawnCell =
            startingConnectivity.findCellNear(probe, wanted, 5, &reservedDeploymentCells);
        if (!spawnCell.has_value()) {
            continue;
        }

        WorldObjectInstance* object = addRuntimeObjectInstanceForOwner(
            state,
            owner,
            kind,
            unit->id,
            *spawnCell
        );
        if (object == nullptr) {
            continue;
        }
        object->mission = "Guard";
        applyRuntimeInitialObjectVeterancy(state, *object);
        if (object->health > 0) {
            object->health = std::max(
                1,
                static_cast<int>(std::lround(
                    static_cast<double>(object->health)
                    * runtimeDifficultyHealthScale(state, owner)
                ))
            );
        }
        ++spawned;
    }
    return spawned + extraMcvs;
}

WorldObjectInstance* spawnRuntimeSkirmishBase(RuntimeProductionHostState& state,
    const std::string& owner, const std::string& yardId, const RuntimeCellPoint& cell) {
    // Every skirmish starts mobile. Only an owner command (or the computer's
    // own decision loop) may turn this vehicle into a construction yard.
    const auto* yard = findRuntimeBuildingTypeById(state.rules, yardId);
    const auto* mcv = yard == nullptr ? nullptr
        : findRuntimeUnitTypeById(state.rules, yard->undeploysInto);
    const auto item = runtimeBuildItemForBuilding(state, yardId);
    if (mcv == nullptr || !item) return nullptr;
    // The ordinary building finder supplies a corner; mobile bases spawn at
    // the center anchor of that already validated future foundation.
    const RuntimeCellPoint center = runtimePlacementAnchorCellForFootprint(
        cell, runtimeStructureFootprintForBuildItem(state, *item));
    auto* base = addRuntimeObjectInstanceForOwner(state, owner, RuntimeBuildKind::Unit, mcv->id, center);
    if (base != nullptr) base->mission = "Guard";
    return base;
}

int spawnRuntimeSkirmishOpponents(
    RuntimeProductionHostState& state,
    const std::vector<RuntimeCellPoint>& startCandidates,
    const std::vector<int>& startAssignments) {
    int spawned = 0;
    const std::optional<std::size_t> localSlotIndex =
        runtimeLocalSkirmishSlotIndex(state.config);
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        // Every enabled non-local combatant needs authored start assets.
        // Remote humans are intentionally spawned here but remain excluded
        // from ensureRuntimeComputerAiStates(), so only their authenticated
        // network commands may control the slot.
        if (!slot.enabled || runtimeObserverSlot(slot)
            || (localSlotIndex.has_value()
                && slotIndex == *localSlotIndex)) {
            continue;
        }
        const std::string owner = runtimeSkirmishOwnerForSlot(slotIndex);
        if (owner == state.economy.owner) {
            continue;
        }
        const int startIndex = slotIndex < startAssignments.size()
            ? startAssignments[slotIndex]
            : -1;
        if (startIndex < 0
            || static_cast<std::size_t>(startIndex)
                >= startCandidates.size()) {
            continue;
        }

        const std::string country = runtimeCountryForOwner(state, owner);
        const std::string constructionYardId =
            runtimeConstructionYardForCountry(country);
        const std::optional<RuntimeBuildCatalogItem> constructionYardItem =
            runtimeBuildItemForBuilding(state, constructionYardId);
        if (!constructionYardItem.has_value()) {
            continue;
        }

        const std::optional<RuntimeCellPoint> placementCell =
            findRuntimePlacementCellNear(
                state,
                *constructionYardItem,
                startCandidates[static_cast<std::size_t>(startIndex)],
                16
            );
        if (!placementCell.has_value()) {
            continue;
        }

        WorldObjectInstance* constructionYard = spawnRuntimeSkirmishBase(
            state, owner, constructionYardItem->typeId, *placementCell);
        if (constructionYard == nullptr) {
            continue;
        }
        if (constructionYard->health > 0) {
            constructionYard->health = std::max(
                1,
                static_cast<int>(std::lround(
                    static_cast<double>(constructionYard->health)
                    * runtimeDifficultyHealthScale(state, owner)
                ))
            );
        }
        spawnRuntimeSkirmishStartingUnits(
            state,
            owner,
            country,
            runtimeCellPointForObject(*constructionYard),
            state.config.startingUnits
        );
        ++spawned;
    }
    return spawned;
}

RuntimeAirImpactBodyCenter captureRuntimeAirImpactBodyCenter(
    RuntimeProductionHostState& state, const WorldObjectInstance& target,
    const RuntimeProjectileTargetPosition& hit) {
    RuntimeAirImpactBodyCenter center;
    if (!runtimeSessionGeometryAvailable(state)) return center;
    if (!target.alive || !target.airborne || !state.artLoaded) return center;
    // Hits can arrive between paints. Refresh the real missile presentation
    // at this pre-damage pose instead of using a previous paint's pitch cache.
    const auto* unit = findRuntimeUnitType(state.rules, target);
    if (unit && unit->missileSpawn) refreshRuntimeSpecialMechanismRenderSnapshots(state);
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    center.captured = state.geometry->resolveRuntimeAirImpactBodyCenterOffset(state.world, target,
        context, state.renderCache, hit.x, hit.y, hit.level,
        center.screenX, center.screenY, center.fixedX, center.fixedY,
        center.presentation, center.spriteOffsetY);
    return center;
}

void applyRuntimeAirImpactBodyCenter(
    RuntimeImpactEffect& impact, const RuntimeAirImpactBodyCenter* center) {
    if (!center || !center->captured) return;
    impact.screenOffsetX = center->screenX;
    impact.screenOffsetY = center->screenY;
    impact.nativeFixedX = center->fixedX;
    impact.nativeFixedY = center->fixedY;
    impact.bodyCenterAnchored = true;
    impact.bodyCenterPresentation = center->presentation;
    impact.bodyCenterSpriteOffsetY = center->spriteOffsetY;
}

std::string runtimeCampaignCratePowerupType(int authoredType) {
    switch (authoredType) {
    case 0: return "Money";
    case 1: return "Unit";
    case 2: return "HealBase";
    case 3: return "Cloak";
    case 4: return "Explosion";
    case 5: return "Napalm";
    case 6: return "Money"; // Squad is Money in TS/RA2/YR.
    case 7: return "Darkness";
    case 8: return "Reveal";
    case 9: return "Armor";
    case 10: return "Speed";
    case 11: return "Firepower";
    case 12: return "ICBM";
    case 13: return "Invulnerability";
    case 14: return "Veteran";
    case 15: return "IonStorm";
    case 16: return "Gas";
    case 17: return "Tiberium";
    case 18: return "Pod";
    default:
        // Final Alert crate ids 19+ explicitly request the weighted
        // multiplayer pool. Other out-of-range values remain unsupported.
        return authoredType >= 19 ? std::string{} : "Unsupported";
    }
}

void initializeRuntimeSkirmishCrates(
    RuntimeGameSessionState& state,
    const std::vector<RuntimeCellPoint>& startCandidates) {
    state.skirmishCrates.clear();
    state.initialSkirmishCrates.clear();
    if (!state.config.crates || startCandidates.empty()) {
        return;
    }

    const CrateRules& rules = state.rules.getCrateRules();
    const int humanPlayers = std::max(
        1,
        static_cast<int>(std::count_if(
            state.config.playerSlots.begin(),
            state.config.playerSlots.end(),
            [](const RuntimeConfig::PlayerSlot& slot) {
                return slot.enabled && slot.human;
            }
        ))
    );
    const int desiredCount = std::min(
        std::max(0, rules.maximum),
        std::max(std::max(0, rules.minimum), humanPlayers)
    );
    std::vector<RuntimeCratePlacement> candidates =
        runtimeCratePlacementCandidates(state, {});
    for (int index = 0;
        index < desiredCount && !candidates.empty();
        ++index) {
        RuntimeCrateState crate;
        const std::size_t selected = static_cast<std::size_t>(
            runtimeCrateSeed(
                state,
                nullptr,
                "initialPlacement",
                index
            ) % candidates.size()
        );
        crate.cell = candidates[selected].cell;
        crate.water = candidates[selected].water;
        crate.imageId = runtimeCrateArtImageId(
            state.rules,
            crate.water ? rules.waterImage : rules.image
        );
        candidates.erase(candidates.begin() + selected);
        runtimeEnsureCratePowerup(state, crate, index);
        crate.respawnTick = runtimeCrateTickAfter(
            state,
            runtimeCrateLifetimeTicks(state, crate, index)
        );
        state.skirmishCrates.push_back(std::move(crate));
    }
    state.initialSkirmishCrates = state.skirmishCrates;
}

void runtimeConfigureCampaignCrateReward(
    RuntimeGameSessionState& state,
    RuntimeCrateState& crate,
    const std::string& reward) {
    const std::string type = trimAsciiLocal(reward);
    if (const CratePowerupRule* powerup =
            runtimeCrateRuleForType(state, type)) {
        runtimeConfigureCratePowerup(crate, *powerup);
    } else {
        crate.powerupType = type.empty() ? "Unsupported" : type;
        crate.powerupAnimation.clear();
        crate.powerupData = 0.0;
    }
    if (runtimeCrateTypeIs(crate.powerupType, "Money")) {
        crate.value = std::max(
            0,
            state.rules.getCrateRules().soloMoney
        );
    }
}

int initializeRuntimeCampaignOverlayCrates(RuntimeGameSessionState& state) {
    if (!state.campaignMode || !state.rulesLoaded) {
        return 0;
    }
    state.skirmishCrates.clear();
    const CrateRules& rules = state.rules.getCrateRules();
    for (TerrainCell& terrain : state.world.accessTerrainCells()) {
        if (!terrain.hasOverlay) {
            continue;
        }
        const OverlayType* overlay = runtimeCrateOverlayRule(state, terrain);
        if (overlay == nullptr || !overlay->crate) {
            continue;
        }

        RuntimeCrateState crate;
        crate.authored = true;
        crate.cell = {terrain.x, terrain.y};
        const bool waterImage = runtimeCrateOverlayMatches(
            terrain,
            overlay,
            rules.waterImage
        );
        const bool woodImage = !waterImage && runtimeCrateOverlayMatches(
            terrain,
            overlay,
            rules.woodImage
        );
        const bool silverImage = !waterImage
            && !woodImage
            && runtimeCrateOverlayMatches(
                terrain,
                overlay,
                rules.image
            );
        const RuntimeTerrainInfo terrainInfo =
            runtimeTerrainInfoForCell(state, terrain);
        crate.water = waterImage
            || (terrainInfo.water
                && !terrainInfo.bridge
                && !runtimeCellIsBridge(state, terrain.x, terrain.y));
        crate.imageId = !trimAsciiLocal(terrain.overlayImage).empty()
            ? trimAsciiLocal(terrain.overlayImage)
            : runtimeCrateArtImageId(
                state.rules,
                overlay != nullptr ? overlay->id : terrain.overlayType
            );
        if (waterImage) {
            runtimeConfigureCampaignCrateReward(
                state,
                crate,
                rules.waterReward
            );
        } else if (woodImage) {
            runtimeConfigureCampaignCrateReward(
                state,
                crate,
                rules.woodReward
            );
        } else if (silverImage) {
            runtimeConfigureCampaignCrateReward(
                state,
                crate,
                rules.silverReward
            );
        } else {
            runtimeConfigureCampaignCrateReward(state, crate, "Money");
        }
        crate.respawnTick = 0;
        state.skirmishCrates.push_back(std::move(crate));

        // RuntimeCrateState now owns this one-shot overlay. Removing the map
        // copy prevents double rendering and lets save/load preserve pickup.
        terrain.hasOverlay = false;
        terrain.overlayIndex = -1;
        terrain.overlayFrame = 0;
        terrain.overlayType.clear();
        terrain.overlayImage.clear();
        terrain.overlayIsOre = false;
        terrain.overlayIsGem = false;
    }
    state.initialSkirmishCrates = state.skirmishCrates;
    return static_cast<int>(state.skirmishCrates.size());
}

bool advanceRuntimeSkirmishCrates(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeSkirmishCrates", state.tick);
    if (state.skirmishCrates.empty()) {
        return false;
    }

    bool changed = false;
    const std::size_t originalCount = state.skirmishCrates.size();
    for (std::size_t crateIndex = 0;
        crateIndex < originalCount;
        ++crateIndex) {
        RuntimeCrateState& crate = state.skirmishCrates[crateIndex];
        if (!crate.authored && !state.config.crates && !state.campaignMode) continue;
        if (!crate.active) {
            if (state.campaignMode || !state.config.crates) {
                continue;
            }
            if (state.tick >= crate.respawnTick) {
                if (!runtimeRelocateCrate(
                        state,
                        crate,
                        static_cast<int>(crateIndex)
                    )) {
                    crate.respawnTick = runtimeCrateTickAfter(
                        state,
                        std::max(1, state.config.fixedTickHz)
                    );
                }
                changed = true;
            }
            continue;
        }

        if (!state.campaignMode && state.config.crates
            && crate.respawnTick > 0
            && state.tick >= crate.respawnTick) {
            if (!runtimeRelocateCrate(
                    state,
                    crate,
                    static_cast<int>(crateIndex)
                )) {
                crate.respawnTick = runtimeCrateTickAfter(
                    state,
                    std::max(1, state.config.fixedTickHz)
                );
            }
            changed = true;
            continue;
        }

        runtimeEnsureCratePowerup(
            state,
            crate,
            static_cast<int>(crateIndex)
        );
        const std::optional<RuntimeCrateCollector> collector =
            runtimeCrateCollectorAt(state, crate);
        if (!collector.has_value()) {
            continue;
        }

        // An explosive pickup can destroy a carrier and append another crate.
        // Snapshot before granting, then reacquire by index after that mutation.
        const RuntimeCrateState collectedCrate = crate;
        const RuntimeCrateAward award = runtimeGrantCratePowerup(
            state,
            collectedCrate,
            *collector
        );
        emitRuntimeCrateAwardFeedback(state, collectedCrate, award);
        auto& consumedCrate = state.skirmishCrates[crateIndex];
        consumedCrate.active = false;
        // The original generator keeps the configured crate count constant:
        // collected crates are replaced on the next simulation tick, while
        // CrateRegen controls the lifetime of an uncollected crate.
        consumedCrate.respawnTick = (state.campaignMode || !state.config.crates)
            ? std::numeric_limits<int>::max()
            : runtimeCrateTickAfter(state, 1);
        state.interactionStatus = collector->owner + " collected "
            + (award.granted ? award.type : std::string("empty"))
            + " crate";
        if (award.credits > 0) {
            state.economy.status = "cash crate +"
                + std::to_string(award.credits);
        }
        state.repaintRequested = true;
        changed = true;
    }
    return changed;
}

bool runtimeSkirmishObjectCountsForShortGame(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    // The original BaseUnit list is AMCV/SMCV/PCV: an undeployed base keeps
    // its house in the match. Resolve the construction-yard deployment rule
    // so captured and custom MCVs work independently of the owner's faction.
    if (object.kind == WorldObjectKind::Unit) {
        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        return unit != nullptr && runtimeUnitTypeIsMcv(state, *unit);
    }
    if (object.kind != WorldObjectKind::Structure) {
        return false;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building != nullptr && building->constructionYard) {
        return true;
    }
    const std::string type = toUpperAsciiLocal(trimAsciiLocal(object.type));
    return type.rfind("CA", 0) != 0
        && type.find("WALL") == std::string::npos
        && type.find("FENC") == std::string::npos
        && type.find("GATE") == std::string::npos;
}

bool runtimeSkirmishObjectSurvives(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (object.health <= 0) return false;
    if (object.alive) return true;
    // Boarding hides the passenger with alive=false while retaining its
    // health. Count it only while a living transport actually carries it.
    if ((object.kind != WorldObjectKind::Unit && object.kind != WorldObjectKind::Infantry)
            || toLowerAsciiLocal(trimAsciiLocal(object.mission)) != "insidetransport") {
        return false;
    }
    return std::any_of(state.world.getObjects().begin(), state.world.getObjects().end(),
        [&](const WorldObjectInstance& transport) {
            return transport.alive && transport.health > 0
                && runtimeTransportContainsCargo(state, transport, object.instanceId);
        });
}

bool runtimeSkirmishOwnerDefeated(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.owner != owner || !runtimeSkirmishObjectSurvives(state, object)) {
            continue;
        }
        if (!state.config.shortGame || runtimeSkirmishObjectCountsForShortGame(state, object)) {
            return false;
        }
    }
    return true;
}

void synchronizeRuntimeCooperativeOutcome(RuntimeProductionHostState& state) {
    if (!state.cooperativeSession.active()) return;
    const auto status = state.cooperativeSession.status();
    state.cooperativeSessionStatus = runtimeCooperativeSessionStatusName(status);
    state.multiplayerDefeatedOwners.clear();
    if (state.cooperativeSession.hasOutcomeSnapshot()) {
        const auto& snapshot = state.cooperativeSession.lastOutcomeSnapshot();
        for (std::size_t index = 0; index < runtimeCooperativeSlotCount; ++index)
            if (snapshot.liveTechnos[index] == 0)
                state.multiplayerDefeatedOwners.insert(runtimeCanonicalHumanHouseOwner(
                    state.cooperativeSession.slots()[index].owner));
    }
    if (status == RuntimeCooperativeSessionStatus::Running) return;
    state.multiplayerWinningOwners.clear();
    state.multiplayerWinningTeams.clear();
    const int winningCoalition = status == RuntimeCooperativeSessionStatus::PlayerCoalitionVictory ? 0
        : (status == RuntimeCooperativeSessionStatus::EnemyCoalitionVictory ? 1 : -1);
    for (const auto& slot : state.cooperativeSession.slots()) {
        if (slot.coalitionId != winningCoalition) continue;
        state.multiplayerWinningOwners.insert(runtimeCanonicalHumanHouseOwner(slot.owner));
        state.multiplayerWinningTeams.insert(std::to_string(slot.coalitionId + 1));
    }
    state.skirmishMatchFinished = true;
    state.skirmishVictory = winningCoalition == 0;
    state.skirmishResult = winningCoalition < 0 ? "DRAW"
        : (state.skirmishVictory ? "VICTORY" : "DEFEAT");
    state.economy.skirmishStatus = state.skirmishResult + " (cooperative)";
    state.economy.status = state.economy.skirmishStatus;
    state.interactionStatus = state.economy.skirmishStatus;
}

bool advanceRuntimeCooperativeOutcome(RuntimeProductionHostState& state) {
    if (!state.cooperativeSession.active() || state.skirmishMatchFinished
        || !state.economy.skirmishStartApplied || state.tick < 5 || state.tick % 10 != 0)
        return false;
    RuntimeCooperativeLiveTechnoSnapshot snapshot;
    snapshot.tick = state.tick;
    for (const auto& object : state.world.getObjects()) {
        if (!runtimeSkirmishObjectSurvives(state, object)) continue;
        const auto slot = runtimeCooperativeSlotForOwner(state, object.owner);
        if (slot) ++snapshot.liveTechnos[static_cast<std::size_t>(*slot)];
    }
    const auto result = state.cooperativeSession.advanceOutcome(snapshot);
    if (!result.accepted) return false;
    synchronizeRuntimeCooperativeOutcome(state);
    if (result.changed) {
        for (auto& object : state.world.accessObjects())
            if (object.renderMoveActive) clearRuntimeRenderPosition(object);
        state.behaviors.clear();
    }
    return result.changed;
}

bool advanceRuntimeSkirmishOutcome(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeSkirmishOutcome", state.tick);
    if (state.cooperativeSession.active()) {
        const bool changed=advanceRuntimeCooperativeOutcome(state);
        observeRuntimePlayerDefeatNotices(state);
        return changed;
    }
    if (state.config.multiplayerActive || runtimeLocalObserver(state.config)) {
        const bool changed=advanceRuntimeMultiplayerOutcome(state);
        observeRuntimePlayerDefeatNotices(state);
        return changed;
    }
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
            == "CAMPAIGNMISSION"
        || state.skirmishMatchFinished
        || state.skirmishCountryByOwner.empty()
        || !state.economy.skirmishStartApplied
        || state.tick < 5
        || (state.tick % 10) != 0) {
        return false;
    }

    observeRuntimePlayerDefeatNotices(state);
    if (runtimeSkirmishOwnerDefeated(state, state.economy.owner)) {
        state.skirmishMatchFinished = true;
        state.skirmishVictory = false;
        state.skirmishResult = "DEFEAT";
    } else {
        bool hostileSurvivor = false;
        for (const auto& [ownerKey, country] : state.skirmishCountryByOwner) {
            (void)country;
            std::string owner;
            for (std::size_t slotIndex = 0;
                slotIndex < state.config.playerSlots.size();
                ++slotIndex) {
                const std::string candidate =
                    runtimeSkirmishOwnerForSlot(slotIndex);
                if (toLowerAsciiLocal(candidate) == ownerKey) {
                    owner = candidate;
                    break;
                }
            }
            if (owner.empty()
                || runtimeOwnersAreAllied(state, state.economy.owner, owner)
                || runtimeSkirmishOwnerDefeated(state, owner)) {
                continue;
            }
            hostileSurvivor = true;
            break;
        }
        if (!hostileSurvivor
            && toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
                == "COOPERATIVE") {
            std::unordered_set<std::string> authoredOwners;
            for (const WorldObjectInstance& object : state.world.getObjects()) {
                if (!object.alive
                    || runtimeOwnerIsNeutralForAutoTarget(object.owner)
                    || object.owner == state.economy.owner
                    || state.skirmishCountryByOwner.find(
                        toLowerAsciiLocal(trimAsciiLocal(object.owner))
                    ) != state.skirmishCountryByOwner.end()) {
                    continue;
                }
                authoredOwners.insert(object.owner);
            }
            for (const std::string& owner : authoredOwners) {
                if (!runtimeOwnersAreAllied(state, state.economy.owner, owner)
                    && !runtimeSkirmishOwnerDefeated(state, owner)) {
                    hostileSurvivor = true;
                    break;
                }
            }
        }
        if (hostileSurvivor) {
            return false;
        }
        state.skirmishMatchFinished = true;
        state.skirmishVictory = true;
        state.skirmishResult = "VICTORY";
    }

    state.economy.skirmishStatus =
        state.skirmishResult
        + std::string(state.config.shortGame ? " (short game)" : "");
    state.economy.status = state.economy.skirmishStatus;
    state.interactionStatus = state.economy.skirmishStatus;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (object.renderMoveActive) {
            clearRuntimeRenderPosition(object);
        }
    }
    state.behaviors.clear();
    return true;
}

void finishRuntimeCampaignMission(
    RuntimeProductionHostState& state,
    bool victory,
    const std::string& triggerId) {
    if (state.skirmishMatchFinished) {
        return;
    }
    state.skirmishMatchFinished = true;
    state.skirmishVictory = victory;
    state.skirmishResult = victory ? "VICTORY" : "DEFEAT";
    finalizeRuntimeCampaignScore(state, victory);
    state.triggers.status =
        std::string(victory ? "campaign victory " : "campaign defeat ")
        + triggerId;
    state.economy.skirmishStatus = state.triggers.status;
    state.economy.status = state.triggers.status;
    state.interactionStatus = state.triggers.status;

    if (!state.campaignProgressSaved) {
        const std::string& progressRoot=state.runtimeCampaignProgressRootPathOverride.empty()
            ?state.config.localDataRootPath:state.runtimeCampaignProgressRootPathOverride;
        CampaignProgressData progress;
        std::string progressError;
        if (!loadCampaignProgress(
                progressRoot,
                progress,
                progressError
            )) {
            state.triggers.status += " progressLoad=" + progressError;
        }
        markCampaignMissionResult(
            progress,
            state.config.campaignId,
            state.config.campaignMissionId,
            state.config.campaignDifficulty,
            state.config.campaignNextMissionId,
            victory
        );
        if (!saveCampaignProgress(
                progressRoot,
                progress,
                progressError
            )) {
            state.triggers.status += " progressSave=" + progressError;
        } else {
            state.campaignProgressSaved = true;
        }
    }
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (object.renderMoveActive) {
            clearRuntimeRenderPosition(object);
        }
    }
    state.behaviors.clear();
}

bool runtimeSkirmishModeTeamSetupValid(
    const RuntimeGameSessionState& state,
    std::string& reason) {
    reason.clear();
    if (!state.skirmishModeMustAlly) {
        return true;
    }
    if (!state.skirmishModeAlliesAllowed) {
        reason = "mode requires teams but forbids alliances";
        return false;
    }

    std::set<std::string> teams;
    int enabledPlayers = 0;
    for (const RuntimeConfig::PlayerSlot& slot : state.config.playerSlots) {
        if (!slot.enabled || runtimeObserverSlot(slot)) {
            continue;
        }
        ++enabledPlayers;
        const std::string team = runtimeNormalizeSkirmishTeam(slot.team);
        if (team.empty()) {
            reason = "mode requires a non-zero team for every player";
            return false;
        }
        teams.insert(team);
    }
    if (enabledPlayers < 2 || teams.size() < 2U) {
        reason = "mode requires at least two opposing teams";
        return false;
    }
    return true;
}

bool initializeRuntimeSkirmishStart(
    RuntimeProductionHostState& state,
    std::vector<int>* loadingStartAssignments,
    const std::vector<int>* plannedStartAssignments) {
    // The loading page and spawn share one plan. Copy before clearing the
    // output so callers may supply the same vector for both parameters.
    const std::vector<int> planned = plannedStartAssignments
        ? *plannedStartAssignments : std::vector<int>{};
    if (loadingStartAssignments) {
        loadingStartAssignments->assign(std::max<std::size_t>(1, state.config.playerSlots.size()), -1);
    }
    const bool localObserver = !state.campaignMode && runtimeLocalObserver(state.config);
    state.economy.skirmishStartApplied = false;
    state.economy.skirmishStartObjectId = -1;
    state.economy.skirmishStatus = "skirmish start not needed";

    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
            == "CAMPAIGNMISSION") {
        state.economy.skirmishStatus = "campaign map start preserved";
        return false;
    }

    std::string teamSetupFailure;
    if (!runtimeSkirmishModeTeamSetupValid(state, teamSetupFailure)) {
        state.economy.skirmishStatus = teamSetupFailure;
        return false;
    }

    if (runtimeHasOwnedFactory(state, "BuildingType")) {
        state.economy.skirmishStatus = "existing construction yard";
        return false;
    }

    const std::string playerCountry =
        runtimeCountryForOwner(state, state.economy.owner);
    const std::string constructionYardId =
        runtimeConstructionYardForCountry(playerCountry);
    const std::optional<RuntimeBuildCatalogItem> constructionYardItem =
        runtimeBuildItemForBuilding(state, constructionYardId);
    if (!constructionYardItem.has_value()) {
        state.economy.skirmishStatus = "missing " + constructionYardId;
        return false;
    }

    const std::vector<RuntimeCellPoint> startCandidates =
        runtimeSkirmishStartCandidates(state);
    std::vector<int> startAssignments;
    std::vector<int> localCandidateOrder;
    if (state.config.playerSlots.empty()) {
        if (plannedStartAssignments != nullptr) {
            if (!planned.empty() && planned.front() >= 0
                && static_cast<std::size_t>(planned.front()) < startCandidates.size()) {
                localCandidateOrder.push_back(planned.front());
            }
        } else {
            // Preserve the original command-line/default behavior when no
            // loading plan was prepared: try numbered waypoints in map order.
            for (std::size_t candidateIndex = 0;
                candidateIndex < startCandidates.size();
                ++candidateIndex) {
                localCandidateOrder.push_back(static_cast<int>(candidateIndex));
            }
        }
    } else {
        if (plannedStartAssignments != nullptr) {
            startAssignments = planned;
        } else {
            const std::vector<std::vector<bool>> legalBySlot =
                runtimeSkirmishStartCandidateLegality(state, startCandidates);
            startAssignments = resolveRuntimeSkirmishStartAssignments(
                state,
                static_cast<int>(startCandidates.size()),
                legalBySlot
            );
        }
        const std::optional<std::size_t> localSlotIndex =
            runtimeLocalSkirmishSlotIndex(state.config);
        if (localSlotIndex.has_value()
            && *localSlotIndex < startAssignments.size()
            && startAssignments[*localSlotIndex] >= 0) {
            localCandidateOrder.push_back(startAssignments[*localSlotIndex]);
        }

        // Spawn every combat slot in canonical slot order.  The local slot is
        // process-relative in a network match, so spawning it first would
        // assign different object ids on different peers.  Keeping creation
        // order independent of the viewing player makes the initial world a
        // deterministic lockstep baseline.
        if (localSlotIndex.has_value()) {
            int enabledSlots = 0;
            int spawnedSlots = 0;
            int nonLocalSlots = 0;
            int localStartingUnits = 0;
            int localConstructionYardId = -1;
            RuntimeCellPoint localStartCell;
            int localStartCandidate = -1;
            std::string localConstructionYardType;
            std::vector<std::tuple<std::string, std::string, int>> humanStartDescriptions;

            for (std::size_t slotIndex = 0;
                slotIndex < state.config.playerSlots.size();
                ++slotIndex) {
                const RuntimeConfig::PlayerSlot& slot =
                    state.config.playerSlots[slotIndex];
                if (!slot.enabled || runtimeObserverSlot(slot)) {
                    continue;
                }
                ++enabledSlots;
                const int candidateIndex =
                    slotIndex < startAssignments.size()
                        ? startAssignments[slotIndex]
                        : -1;
                if (candidateIndex < 0
                    || static_cast<std::size_t>(candidateIndex)
                        >= startCandidates.size()) {
                    continue;
                }

                const std::string owner =
                    runtimeSkirmishOwnerForSlot(slotIndex);
                const std::string country =
                    runtimeCountryForOwner(state, owner);
                const std::string yardType =
                    runtimeConstructionYardForCountry(country);
                const std::optional<RuntimeBuildCatalogItem> yardItem =
                    runtimeBuildItemForBuilding(state, yardType);
                if (!yardItem.has_value()) {
                    continue;
                }
                const std::optional<RuntimeCellPoint> placementCell =
                    findRuntimePlacementCellNear(
                        state,
                        *yardItem,
                        startCandidates[static_cast<std::size_t>(
                            candidateIndex
                        )],
                        16
                    );
                if (!placementCell.has_value()) {
                    continue;
                }

                WorldObjectInstance* yard = spawnRuntimeSkirmishBase(
                    state, owner, yardItem->typeId, *placementCell);
                if (yard == nullptr) {
                    continue;
                }
                if (!slot.human && yard->health > 0) {
                    yard->health = std::max(
                        1,
                        static_cast<int>(std::lround(
                            static_cast<double>(yard->health)
                                * runtimeDifficultyHealthScale(state, owner)
                        ))
                    );
                }
                const int yardId = yard->instanceId;
                const std::string spawnedYardType = yard->type;
                const RuntimeCellPoint spawnedBaseCenter = runtimeCellPointForObject(*yard);
                if (loadingStartAssignments) (*loadingStartAssignments)[slotIndex] = candidateIndex;
                const int startingUnits =
                    spawnRuntimeSkirmishStartingUnits(
                        state,
                        owner,
                        country,
                        spawnedBaseCenter,
                        state.config.startingUnits
                    );
                ++spawnedSlots;
                if (RuntimeEconomyState* human = runtimeHumanHouseEconomy(state, owner)) {
                    human->skirmishStartApplied = true;
                    human->skirmishStartObjectId = yardId;
                    human->skirmishStartCell = spawnedBaseCenter;
                    humanStartDescriptions.emplace_back(
                        owner,
                        "skirmish base " + spawnedYardType
                            + " #" + std::to_string(yardId)
                            + " at " + std::to_string(spawnedBaseCenter.x)
                            + "," + std::to_string(spawnedBaseCenter.y)
                            + " units=" + std::to_string(startingUnits),
                        candidateIndex + 1
                    );
                }

                if (slotIndex == *localSlotIndex) {
                    localConstructionYardId = yardId;
                    localConstructionYardType = spawnedYardType;
                    localStartCell = spawnedBaseCenter;
                    localStartCandidate = candidateIndex;
                    localStartingUnits = startingUnits;
                } else {
                    ++nonLocalSlots;
                }
            }

            if (spawnedSlots != enabledSlots
                || (!localObserver && localConstructionYardId < 0)) {
                state.economy.skirmishStatus =
                    "skirmish slot start failed "
                    + std::to_string(spawnedSlots)
                    + "/"
                    + std::to_string(enabledSlots);
                return false;
            }

            // Publish owner-specific text only after every combat slot has
            // started, preserving the existing local failure diagnostics.
            for (const auto& [owner, description, start] : humanStartDescriptions) {
                if (RuntimeEconomyState* human = runtimeHumanHouseEconomy(state, owner)) {
                    human->skirmishStatus = description
                        + " opponents=" + std::to_string(spawnedSlots - 1)
                        + " start=" + std::to_string(start)
                        + " mode=" + state.config.gameMode;
                }
            }

            if (localObserver && !startCandidates.empty()) localStartCell = startCandidates.front();
            state.economy.skirmishStartApplied = true;
            state.economy.skirmishStartObjectId =
                localConstructionYardId;
            state.economy.skirmishStartCell = localStartCell;
            state.economy.skirmishStatus =
                "skirmish base "
                + localConstructionYardType
                + " #"
                + std::to_string(localConstructionYardId)
                + " at "
                + std::to_string(localStartCell.x)
                + ","
                + std::to_string(localStartCell.y);
            // A fresh match starts with the ordinary pointer and no selected unit.
            applySelection(state, {}, false, false);
            initializeRuntimeSkirmishCrates(state, startCandidates);
            initializeRuntimeNavigation(state);
            rebuildRuntimeEconomyPower(state);
            for (const auto& authority : state.humanHouseAuthorities) {
                RuntimeHumanHouseScope house(state, authority.owner);
                if (house) {
                    refreshRuntimeEconomyMetricsForCurrentOwner(state);
                    (void)refreshRuntimeFactoryPlantCatalog(state);
                }
            }
            state.economy.skirmishStatus +=
                " units=" + std::to_string(localStartingUnits)
                + " opponents=" + std::to_string(nonLocalSlots)
                + " start=" + std::to_string(localStartCandidate + 1)
                + " mode=" + state.config.gameMode;
            if (localObserver) state.economy.skirmishStatus = "Observing "
                + std::to_string(spawnedSlots) + " combatants";
            state.economy.status = state.economy.skirmishStatus;
            return true;
        }
    }

    for (int candidateIndex : localCandidateOrder) {
        if (candidateIndex < 0
            || static_cast<std::size_t>(candidateIndex)
                >= startCandidates.size()) {
            continue;
        }
        const RuntimeCellPoint& candidate =
            startCandidates[static_cast<std::size_t>(candidateIndex)];
        const std::optional<RuntimeCellPoint> placementCell =
            findRuntimePlacementCellNear(
                state,
                *constructionYardItem,
                candidate,
                16
            );
        if (!placementCell.has_value()) {
            continue;
        }

        WorldObjectInstance* constructionYard = spawnRuntimeSkirmishBase(
            state, state.economy.owner, constructionYardItem->typeId, *placementCell);
        if (constructionYard == nullptr) {
            continue;
        }

        const RuntimeCellPoint spawnedBaseCenter = runtimeCellPointForObject(*constructionYard);
        state.economy.skirmishStartApplied = true;
        state.economy.skirmishStartObjectId = constructionYard->instanceId;
        state.economy.skirmishStartCell = spawnedBaseCenter;
        if (loadingStartAssignments) (*loadingStartAssignments)[0] = candidateIndex;
        state.economy.skirmishStatus =
            "skirmish base "
            + constructionYard->type
            + " #"
            + std::to_string(constructionYard->instanceId)
            + " at "
            + std::to_string(spawnedBaseCenter.x)
            + ","
            + std::to_string(spawnedBaseCenter.y);

        applySelection(state, {}, false, false);
        const int startingUnits = spawnRuntimeSkirmishStartingUnits(
            state,
            state.economy.owner,
            playerCountry,
            spawnedBaseCenter,
            state.config.startingUnits
        );
        const int opponents = spawnRuntimeSkirmishOpponents(
            state,
            startCandidates,
            startAssignments
        );
        initializeRuntimeSkirmishCrates(state, startCandidates);
        initializeRuntimeNavigation(state);
        rebuildRuntimeEconomyPower(state);
        state.economy.skirmishStatus +=
            " units=" + std::to_string(startingUnits)
            + " opponents=" + std::to_string(opponents)
            + " start=" + std::to_string(candidateIndex + 1)
            + " mode=" + state.config.gameMode;
        state.economy.status = state.economy.skirmishStatus;
        return true;
    }

    state.economy.skirmishStatus = "no legal skirmish start cell";
    return false;
}

bool finishRuntimeAirstrikeSalvo(RuntimeGameSessionState& state,
    RuntimeAirstrikeRootAuthority& authority) {
    if (authority.terminalTick >= 0 || authority.roundsRemaining > 0
        || std::any_of(authority.rounds.begin(), authority.rounds.end(),
            [](const auto& round) { return round.terminalTick < 0; })) return false;
    if (state.airstrikes.acknowledgeRootProjectile(authority.strikeId,
            authority.planeToken, state.tick) != RuntimeAirstrikeProjectileAckResult::Accepted)
        return false;
    authority.terminalTick = state.tick;
    return true;
}

void reconcileRuntimeAirstrikeSalvos(RuntimeGameSessionState& state) {
    std::set<int> liveActors;
    for (const auto& projectile : state.projectiles) liveActors.insert(projectile.actorId);
    for (auto& [rootId, authority] : state.airstrikeRootAuthoritiesByActor) {
        (void)rootId;
        if (authority.terminalTick >= 0) continue;
        for (auto& round : authority.rounds)
            if (round.terminalTick < 0 && !liveActors.contains(round.actorId))
                round.terminalTick = state.tick;
        const auto* plane = findObjectById(state, authority.planeObjectId);
        const auto* target = findObjectById(state, authority.targetObjectId);
        const auto* weapon = findRuntimeWeaponType(state.rules, authority.weaponId);
        if (!plane || plane->health <= 0 || plane->owner != authority.owner
            || plane->type != authority.aircraftTypeId || !target || target->health <= 0
            || !weapon || !runtimeWeaponCanTargetObject(state, *weapon, *target)
            || !runtimeAirstrikeBindingForObject(state, authority.planeObjectId)) {
            authority.roundsRemaining = 0;
            authority.nextFireTick = -1;
        }
        finishRuntimeAirstrikeSalvo(state, authority);
    }
}

void advanceRuntimeAirstrikeSalvos(RuntimeProductionHostState& state) {
    for (auto& [rootId, authority] : state.airstrikeRootAuthoritiesByActor) {
        (void)rootId;
        if (authority.terminalTick >= 0 || authority.roundsRemaining <= 0) continue;
        const auto* plane = findObjectById(state, authority.planeObjectId);
        const auto* target = findObjectById(state, authority.targetObjectId);
        const auto* weapon = findRuntimeWeaponType(state.rules, authority.weaponId);
        const auto* strike = state.airstrikes.findStrike(authority.strikeId);
        if (!plane || !target || !weapon || (strike && strike->cancelled)
            || state.nextProjectileActorId >= INT_MAX) {
            authority.roundsRemaining = 0; authority.nextFireTick = -1;
            finishRuntimeAirstrikeSalvo(state, authority);
            continue;
        }
        if (state.tick < authority.nextFireTick) continue;
        RuntimeAirstrikeSalvoRound round;
        round.actorId = state.nextProjectileActorId++;
        round.launchTick = state.tick;
        const auto source = runtimeCellPointForObject(*plane);
        round.sourceCell = {source.x, source.y};
        round.damage = runtimeAuthoredDamageAgainst(state, *target, weapon->damage,
            weapon->warhead, 0.0, authority.veteranDamageMultiplier);
        authority.rounds.push_back(round);
        --authority.roundsRemaining;
        authority.nextFireTick = authority.roundsRemaining > 0
            ? static_cast<int>(std::min<std::int64_t>(INT_MAX, static_cast<std::int64_t>(state.tick)
                + runtimeNextBurstDelayTicks(state, *plane, *weapon,
                    static_cast<int>(authority.rounds.size())))) : -1;
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile, state,
            authority.elite ? RuntimeWeaponSlot::ElitePrimary : RuntimeWeaponSlot::Primary,
            *weapon, "airstrikeManagedBurst");
        spawnRuntimeProjectile(state, *plane, *target, *weapon, profile, round.damage,
            -1, 0, false, -1, authority.veteranDamageMultiplier, 0, 0, 0, false,
            nullptr, round.actorId, authority.strikeId, authority.planeToken, strike && strike->forceFire);
    }
}

bool runtimeComputerTacticReservesObject(const RuntimeGameSessionState& state, int objectId) {
    if (state.pendingTransportTargets.contains(objectId) || state.pendingEngineerCaptures.contains(objectId)
        || state.pendingOccupyTargets.contains(objectId)) return true;
    for (const auto& relation : state.grinding.relations())
        if (relation.sourceId == objectId) return true;
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, team] : tactics.teams)
            if (std::find(team.objectIds.begin(), team.objectIds.end(), objectId)
                != team.objectIds.end()) return true;
    return false;
}

bool finishRuntimeComputerTacticMission(RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission) {
    for (auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (auto& [id, team] : tactics.teams)
            if (team.missionInstanceId == mission.instanceId) {
                team.missionFinished = true;
                team.achievedGreatSuccess |= mission.achievedGreatSuccess;
                return true;
            }
    return false;
}

std::string runtimeComputerTacticTargetOwner(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission) {
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, team] : tactics.teams)
            if (team.missionInstanceId == mission.instanceId) {
                const auto type = state.triggers.teams.find(team.teamTypeId);
                return type != state.triggers.teams.end() && type->second.onlyTargetHouseEnemy
                    ? team.targetOwner : std::string{};
            }
    return {};
}

const RuntimeTeamType* findRuntimeTriggerTeamById(
    const RuntimeGameSessionState& state,
    const std::string& id) {
    const auto found = state.triggers.teams.find(id);
    if (found != state.triggers.teams.end()) {
        return &found->second;
    }
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(id));
    for (const auto& [teamId, team] : state.triggers.teams) {
        if (toLowerAsciiLocal(trimAsciiLocal(teamId)) == wanted) {
            return &team;
        }
    }
    return nullptr;
}

const RuntimeTeamType* chooseRuntimeTriggerTeam(
    const RuntimeGameSessionState& state) {
    const RuntimeTeamType* bestTeam = nullptr;
    int bestScore = std::numeric_limits<int>::min();

    const auto scoreTeam = [&state](const RuntimeTeamType& team) {
        int score = team.priority;
        if (!team.house.empty() && team.house != state.economy.owner) {
            score += 20;
        }
        if (team.aggressive) {
            score += 8;
        }
        if (team.autocreate) {
            score += 5;
        }
        if (team.prebuild) {
            score += 4;
        }
        if (team.reinforce) {
            score += 3;
        }
        if (!team.waypointId.empty()) {
            score += 2;
        }
        return score;
    };

    for (const auto& [teamId, team] : state.triggers.teams) {
        (void)teamId;
        const auto taskForceIt =
            state.triggers.taskForces.find(team.taskForceId);
        if (taskForceIt == state.triggers.taskForces.end()
            || !runtimeTaskForceHasBuildableUnits(state, taskForceIt->second)) {
            continue;
        }

        const int score = scoreTeam(team);
        if (score > bestScore) {
            bestScore = score;
            bestTeam = &team;
        }
    }

    return bestTeam;
}

const RuntimeTeamType* chooseRuntimeTriggerTeamForAction(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action,
    bool allowFallback) {
    if (const RuntimeTeamType* team =
            findRuntimeTriggerTeamById(state, action.targetId)) {
        const auto taskForceIt =
            state.triggers.taskForces.find(team->taskForceId);
        if (taskForceIt != state.triggers.taskForces.end()
            && runtimeTaskForceHasBuildableUnits(state, taskForceIt->second)) {
            return team;
        }
    }

    return allowFallback ? chooseRuntimeTriggerTeam(state) : nullptr;
}

std::optional<RuntimeCellPoint> runtimeDefaultTriggerSpawnCell(
    const RuntimeGameSessionState& state) {
    if (!state.world.getWaypoints().empty()) {
        const WorldWaypoint& waypoint = state.world.getWaypoints().front();
        return RuntimeCellPoint{waypoint.x, waypoint.y};
    }

    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        return RuntimeCellPoint{terrain.x, terrain.y};
    }

    return std::nullopt;
}

const WorldObjectInstance* runtimeTriggerTargetObject(
    const RuntimeGameSessionState& state,
    const std::string& teamHouse) {
    const WorldObjectInstance* fallback = nullptr;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive) {
            continue;
        }
        if (object.owner == state.economy.owner) {
            return &object;
        }
        if (fallback == nullptr
            && !object.owner.empty()
            && object.owner != teamHouse
            && object.owner != "Neutral") {
            fallback = &object;
        }
    }
    return fallback;
}

bool runtimeTriggerTeamAtCapacity(const RuntimeGameSessionState& state, const RuntimeTeamType& team) {
    if (team.max <= 0) return false;
    const auto key = toLowerAsciiLocal(team.id);
    const auto active = std::count_if(state.triggers.teamMissions.begin(), state.triggers.teamMissions.end(),
        [&](const auto& entry) {
            return !entry.second.completed && toLowerAsciiLocal(entry.second.teamTypeId) == key;
        });
    const auto pending = std::count_if(state.triggers.reinforcements.pending.begin(),
        state.triggers.reinforcements.pending.end(), [&](const auto& entry) {
            return toLowerAsciiLocal(entry.second.teamTypeId) == key;
        });
    return active + pending >= team.max;
}

void issueRuntimeTriggerTeamOrder(
    RuntimeProductionHostState& state,
    const RuntimeTeamType& team,
    const std::vector<int>& objectIds,
    const std::string& sourceAiTriggerId,
    int sourceAiBuildId) {
    const auto smokeTimingStart = runtimeSmokeNow();
    if (objectIds.empty()) {
        return;
    }

    const auto scriptIt = state.triggers.scripts.find(team.scriptId);
    if (scriptIt != state.triggers.scripts.end()
        && !scriptIt->second.steps.empty()) {
        RuntimeTeamMissionState mission;
        mission.instanceId = state.triggers.nextTeamMissionInstanceId++;
        mission.teamTypeId = team.id;
        mission.scriptId = team.scriptId;
        mission.objectIds = objectIds;
        mission.sourceAiTriggerId = sourceAiTriggerId;
        mission.sourceAiBuildId = sourceAiBuildId;
        mission.status = "queued " + scriptIt->second.name;
        state.triggers.teamMissions.emplace(mission.instanceId, mission);
        for (int objectId : objectIds) {
            if (WorldObjectInstance* object =
                    findMutableObjectById(state, objectId)) {
                object->mission = "TeamScript";
            }
        }
        advanceRuntimeTeamMissions(state);
        if (state.triggers.collectSmokeTimings) {
            state.triggers.smokeIssueOrderMs +=
                runtimeSmokeElapsedMs(smokeTimingStart);
        }
        return;
    }

    const std::vector<int> savedSelection = state.selectedObjectIds;
    const bool savedAudioSuppression =
        state.runtimeSuppressPlayerAudioFeedback;
    state.runtimeSuppressPlayerAudioFeedback = true;
    applySelection(state, objectIds, false, false);

    const WorldObjectInstance* target =
        runtimeTriggerTargetObject(state, team.house);
    if (target != nullptr) {
        issueRuntimeAttackCommand(state, *target);
        if (state.lastCommand.acceptedCount <= 0) {
            issueRuntimeMoveCommand(state, runtimeCellPointForObject(*target));
        }
    } else if (std::optional<RuntimeCellPoint> targetCell =
            runtimeDefaultTriggerSpawnCell(state)) {
        issueRuntimeMoveCommand(state, *targetCell);
    }

    state.triggers.ordersIssued += std::max(0, state.lastCommand.acceptedCount);
    state.selectedObjectIds = savedSelection;
    state.runtimeSuppressPlayerAudioFeedback = savedAudioSuppression;
    if (state.triggers.collectSmokeTimings) {
        state.triggers.smokeIssueOrderMs +=
            runtimeSmokeElapsedMs(smokeTimingStart);
    }
}

bool advanceRuntimeTriggerReinforcements(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeTriggerReinforcements", state.tick);
    bool changed = false;
    int budget = 128; // Work per advancement pass, never a team's total size.
    auto& pending = state.triggers.reinforcements.pending;
    for (auto it = pending.begin(); it != pending.end() && budget > 0;) {
        auto& job = it->second;
        if (state.tick < job.nextAttemptTick) { ++it; continue; }
        const auto* team = findRuntimeTriggerTeamById(state, job.teamTypeId);
        if (!team) { ++it; continue; }
        bool blocked = false;
        while (job.memberIndex < job.members.size() && budget-- > 0) {
            const auto& member = job.members[job.memberIndex];
            const RuntimeBuildKind kind = runtimeBuildKindForUnitTypeId(state.rules, member.typeId);
            const auto* unitType = findRuntimeUnitTypeById(state.rules, member.typeId);
            if (!unitType) { blocked = true; break; }
            WorldObjectInstance probe;
            probe.instanceId = -1; probe.kind = runtimeKindForBuildItem(kind);
            probe.owner = job.owner; probe.type = member.typeId; probe.alive = true;
            probe.health = std::max(1, unitType->strength);
            int transportId = -1;
            if (team->full && unitType->passengers <= 0) {
                for (int id : job.objectIds) {
                    const auto* transport = findObjectById(state, id);
                    if (transport && runtimeObjectCanEnterTransport(state, probe, *transport, false)) {
                        transportId = id;
                        break;
                    }
                }
            }
            std::optional<RuntimeCellPoint> cell;
            if (transportId >= 0) {
                cell = runtimeCellPointForObject(*findObjectById(state, transportId));
            } else {
                const auto offset = formationOffsetForIndex(static_cast<int>(job.objectIds.size()));
                cell = findRuntimePassableCellNear(state, probe,
                    {job.originX + offset.x, job.originY + offset.y}, 6);
                if (!cell) {
                    const int radius = std::clamp(2 + std::max(std::abs(offset.x), std::abs(offset.y)), 6, 512);
                    cell = findRuntimePassableCellNear(state, probe, {job.originX, job.originY}, radius);
                }
            }
            if (!cell) { blocked = true; break; }
            auto* created = addRuntimeObjectInstanceForOwner(state, job.owner, kind,
                member.typeId, *cell, std::nullopt, true, transportId >= 0);
            if (!created) { blocked = true; break; }
            const int createdId = created->instanceId;
            created->mission = "TriggerTeam";
            if (transportId >= 0) {
                // Initial cargo bypasses ground occupancy, then uses the same
                // manifest, gunner and service transitions as normal boarding.
                created->alive = true;
                state.pendingTransportTargets[createdId] = transportId;
                const bool savedSuppression = state.runtimeSuppressPlayerAudioFeedback;
                state.runtimeSuppressPlayerAudioFeedback = true;
                const bool entered = completeRuntimeTransportEntry(state, createdId, true);
                state.runtimeSuppressPlayerAudioFeedback = savedSuppression;
                if (!entered) {
                    // No other object is created between allocation and this
                    // transition. Roll back a rejected initial cargo entry.
                    state.pendingTransportTargets.erase(createdId);
                    state.cloakObservedHealthByObject.erase(createdId);
                    releaseRuntimeInfantrySubcellReservation(state, createdId);
                    state.world.accessObjects().pop_back();
                    invalidateRuntimeObjectProjectionCache(state);
                    blocked = true;
                    break;
                }
            }
            job.objectIds.push_back(createdId);
            if (job.actionType == 107 && created->alive) {
                ensureRuntimeSpecialMechanismsInitialized(state);
                state.specialMechanisms.beginChronoReinforcementRecovery(
                    *created, state.tick, state.config.fixedTickHz);
                runtimeSpecialEventSink(state)({"ChronoReinforcementIn", state.tick, createdId, -1, 0,
                    {created->x, created->y}, "chrono reinforcement arrived"});
            }
            if (!job.teamCounted) { job.teamCounted = true; ++state.triggers.teamsCreated; }
            ++state.triggers.unitsCreated;
            state.triggers.campaignTeamObjects[toLowerAsciiLocal(trimAsciiLocal(team->id))].insert(createdId);
            ++job.memberProduced;
            if (job.memberProduced == member.count) { ++job.memberIndex; job.memberProduced = 0; }
            changed = true;
        }
        if (job.memberIndex == job.members.size()) {
            // A fully delivered chrono team remains pending until every live
            // arrival recovers. No script command or wait timer starts early.
            const bool recovering = job.actionType == 107 && std::any_of(
                job.objectIds.begin(), job.objectIds.end(), [&](int id) {
                    return findObjectById(state, id) != nullptr
                        && state.specialMechanisms.isChronoMovementLocked(id, state.tick);
                });
            if (recovering) {
                job.nextAttemptTick = state.tick == std::numeric_limits<int>::max()
                    ? state.tick : state.tick + 1;
                state.triggers.status = "recovering chrono team " + job.teamTypeId;
                ++it;
                continue;
            }
            const auto ids = job.objectIds;
            const RuntimeTeamType completedTeam = *team;
            it = pending.erase(it);
            initializeRuntimeNavigation(state);
            issueRuntimeTriggerTeamOrder(state, completedTeam, ids);
            state.triggers.status = "delivered team " + completedTeam.id + " units=" + std::to_string(ids.size());
            changed = true;
        } else {
            const int delay = blocked ? std::max(1, state.config.fixedTickHz / 4) : 1;
            job.nextAttemptTick = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.tick) + delay));
            state.triggers.status = "pending team " + job.teamTypeId + " delivered=" + std::to_string(job.objectIds.size());
            ++it;
        }
    }
    return changed;
}

bool queueRuntimeTriggerReinforcement(
    RuntimeProductionHostState& state,
    const RuntimeTeamType& team,
    std::optional<RuntimeCellPoint> authoredOrigin,
    int actionType) {
    const auto taskForce = state.triggers.taskForces.find(team.taskForceId);
    if (taskForce == state.triggers.taskForces.end() || taskForce->second.members.empty()) return false;
    auto& queue = state.triggers.reinforcements;
    if (queue.pending.size() >= 4096 || queue.nextInstanceId == std::numeric_limits<int>::max()) {
        state.triggers.status = "reinforcement queue capacity reached";
        return false;
    }
    auto origin = authoredOrigin;
    if (!origin) origin = runtimeWaypointCell(state, team.waypointId);
    if (!origin) {
        const auto base = state.triggers.campaignBaseCenterByOwner.find(toLowerAsciiLocal(trimAsciiLocal(team.house)));
        if (base != state.triggers.campaignBaseCenterByOwner.end()) origin = base->second;
    }
    if (!origin) origin = runtimeDefaultTriggerSpawnCell(state);
    if (!origin) { state.triggers.status = "team " + team.id + " has no spawn cell"; return false; }
    RuntimeTriggerReinforcement job;
    job.instanceId = queue.nextInstanceId;
    job.teamTypeId = team.id; job.owner = team.house.empty() ? "Neutral" : team.house;
    job.originX = origin->x; job.originY = origin->y; job.actionType = actionType;
    job.nextAttemptTick = state.tick;
    std::int64_t total = 0;
    for (const auto& member : taskForce->second.members) {
        const auto* type = findRuntimeUnitTypeById(state.rules, member.unitId);
        if (!type || member.count <= 0) {
            state.triggers.status = "team " + team.id + " has unsupported member " + member.unitId;
            return false;
        }
        total += member.count;
        job.members.push_back({type->id, member.count});
    }
    if (total > 1000000 || job.members.size() > 4096) {
        state.triggers.status = "team " + team.id + " exceeds reinforcement save capacity";
        return false;
    }
    if (team.full) {
        // Stock sov01 TaskForce lists 12 E2 before its SAPC. Full=yes means
        // the transport must exist before its initial passengers are created.
        std::stable_partition(job.members.begin(), job.members.end(), [&](const auto& member) {
            return findRuntimeUnitTypeById(state.rules, member.typeId)->passengers > 0;
        });
    }
    queue.pending.emplace(job.instanceId, std::move(job));
    ++queue.nextInstanceId;
    advanceRuntimeTriggerReinforcements(state);
    return true;
}

std::string runtimeTeamMissionOwner(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission) {
    for (int objectId : mission.objectIds) {
        const WorldObjectInstance* object =
            findRuntimeObjectByIdIncludingInactive(state, objectId);
        if (object != nullptr && !object->owner.empty()) {
            return object->owner;
        }
    }
    const auto teamIt = state.triggers.teams.find(mission.teamTypeId);
    return teamIt == state.triggers.teams.end()
        ? std::string{}
        : teamIt->second.house;
}

bool runtimeTeamMemberNeedsGuard(const RuntimeGameSessionState& state, const WorldObjectInstance& member) {
    const auto* type = findRuntimeUnitType(state.rules, member);
    if (!type) return false;
    const int retailSpeed = std::min(255, std::clamp(type->speed, 0, 100) * 256 / 100);
    const auto deploy = toLowerAsciiLocal(trimAsciiLocal(type->deploysInto));
    const bool armed = !type->primaryWeapon.empty()
        || (!type->indexedWeapons.empty() && !type->indexedWeapons.front().empty());
    return !armed || type->techLevel == -1
        || (!deploy.empty() && deploy != "<none>") || retailSpeed <= 13;
}

bool runtimeTeamGuardsSlower(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission) {
    const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
    return type && type->guardSlower;
}

double runtimeTeamMemberStray(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission,
    const WorldObjectInstance& member, int action) {
    const auto& rules = state.rules.getComputerTacticsRules();
    double radius = action == 53 || action == 54 ? rules.relaxedStray : rules.stray;
    const auto& aircraft = state.rules.getAircraftTypes();
    if (std::any_of(aircraft.begin(), aircraft.end(), [&](const auto& type) {
            return toUpperAsciiLocal(type.id) == toUpperAsciiLocal(member.type); })) radius *= 3.0;
    if (runtimeTeamGuardsSlower(state, mission) && !runtimeTeamMemberNeedsGuard(state, member)) radius /= 3.0;
    return radius;
}

bool runtimeTeamGuardHasStrength(const RuntimeGameSessionState& state, const RuntimeTeamMissionState& mission) {
    const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
    if (!type || !type->guardSlower) return false;
    const auto task = state.triggers.taskForces.find(type->taskForceId);
    int expected = 0, present = 0;
    if (task != state.triggers.taskForces.end())
        for (const auto& member : task->second.members) expected += std::max(0, member.count);
    for (int id : mission.objectIds)
        if (const auto* member = findRuntimeObjectByIdIncludingInactive(state, id);
            member && (member->alive || member->mission == "InsideTransport")) ++present;
    return expected <= 2 ? present >= expected : present > expected / 3;
}

RuntimeCellPoint runtimeTeamMissionCenter(
    const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission) {
    long long x = 0;
    long long y = 0;
    int count = 0;
    for (int objectId : mission.objectIds) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr || !object->alive) {
            continue;
        }
        const int weight = runtimeTeamGuardsSlower(state, mission)
            && runtimeTeamMemberNeedsGuard(state, *object) ? 2 : 1;
        x += object->x * weight;
        y += object->y * weight;
        count += weight;
    }
    if (count <= 0) {
        return {-1, -1};
    }
    return {
        static_cast<int>(x / count),
        static_cast<int>(y / count)
    };
}

bool runtimeTeamCanLocateTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target) {
    if (runtimeObjectHiddenFromOwnerByStealth(state, actor.owner, target)
        || runtimeSpyDisguiseConcealsFromObject(state, actor, target)) return false;
    return state.computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(actor.owner)))
        || runtimeObjectVisibleToOwner(state, actor.owner, target);
}

std::optional<RuntimeCellPoint> runtimeTeamNavalAttackApproach(
    const RuntimeGameSessionState& state, const WorldObjectInstance& actor,
    const WorldObjectInstance& target, const WeaponType& weapon) {
    const auto* type = findRuntimeUnitType(state.rules, actor);
    double range = runtimeEffectiveWeaponRange(state, actor, target, weapon);
    if (!runtimeObjectVisibleToOwner(state, actor.owner, target) && type)
        range = std::min(range, static_cast<double>(std::max(1, type->sight)));
    // The destroyer's carrier aircraft uses the authored unlimited range.
    // Once the submarine is located, the parent ship can launch in place.
    if (!std::isfinite(range)) return runtimeCellPointForObject(actor);
    const int radius = std::clamp(static_cast<int>(std::ceil(range)), 1, 64);
    const auto footprint = runtimeStructureFootprintForObject(state, target);
    const auto occupied = buildRuntimeOccupancyLookup(state, actor.instanceId, nullptr, nullptr);
    std::optional<RuntimeCellPoint> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int y = std::max(0, target.y - radius); y <= target.y + footprint.height + radius; ++y)
        for (int x = std::max(0, target.x - radius); x <= target.x + footprint.width + radius; ++x) {
            const RuntimeCellPoint cell{x, y};
            if (runtimeAttackRangeDistanceFromCell(state, cell, target, weapon.cellRangefinding) > range
                || !runtimeCellIsPassable(state, state.terrainByCell, occupied, cell, actor)) continue;
            const double distance = runtimeCellDistance(runtimeCellPointForObject(actor), cell);
            if (distance >= bestDistance) continue;
            auto firing = actor; clearRuntimeRenderPosition(firing);
            firing.x = x; firing.y = y; firing.cell = runtimeCellKey(x, y);
            if (!runtimeWeaponHasLineOfSight(state, firing, target, weapon)) continue;
            best = cell; bestDistance = distance;
        }
    return best;
}

bool runtimeTeamActorCanInteractWithTarget(const RuntimeGameSessionState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target) {
    if (!actor.alive || !target.alive || actor.instanceId == target.instanceId
        || runtimeObjectDisabledByParasite(state, actor.instanceId)
        || !runtimeTeamCanLocateTarget(state, actor, target)) return false;
    if (runtimeEngineerCanCaptureStructure(state, actor, target)
        || runtimeSpyCanInfiltrateTarget(state, actor, target)) return true;
    const auto profile = runtimeWeaponProfileForObject(state, actor, &target);
    if (profile.weapon == nullptr) return false;
    if (profile.warhead != nullptr && profile.warhead->makesDisguise)
        return runtimeSpyCanApplyMakesDisguise(state, actor.instanceId,
            target.instanceId, profile.warhead->id);
    if (!runtimeWeaponHasAttackRange(*profile.weapon)
        || (profile.weapon->damage <= 0
            && (profile.warhead == nullptr || !profile.warhead->ivanBomb))) return false;
    if (runtimeObjectMovementMode(state, actor) == PathMovementMode::Naval
        && !runtimeTeamNavalAttackApproach(state, actor, target, *profile.weapon)) return false;
    return runtimePsychicCanAttackTarget(state, actor, target, true,
        runtimeOwnersAreHostileForAutoTarget(state, actor.owner, target.owner));
}

int issueRuntimeTeamAttack(RuntimeProductionHostState& state,
    const std::vector<int>& objectIds, const WorldObjectInstance& targetObject) {
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    const WorldObjectInstance target = targetObject;
    const auto savedSelection = state.selectedObjectIds;
    const bool savedAudio = state.runtimeSuppressPlayerAudioFeedback;
    state.runtimeSuppressPlayerAudioFeedback = true;
    int accepted = 0;
    for (int objectId : objectIds) {
        const auto* actor = findObjectById(state, objectId);
        if (actor == nullptr || !runtimeTeamActorCanInteractWithTarget(state, *actor, target))
            continue;
        applySelection(state, {objectId}, false, false);
        if (runtimeEngineerCanCaptureStructure(state, *actor, target)) {
            (void)queueRuntimeEngineerCaptureForObject(state, objectId, target, false);
            if (state.pendingEngineerCaptures.contains(objectId)) {
                ++accepted; ++state.triggers.ordersIssued;
            }
        } else if (runtimeSpyCanInfiltrateTarget(state, *actor, target)) {
            (void)queueRuntimeSpyInfiltration(state, target);
            const auto behavior = state.behaviors.find(objectId);
            if (behavior != state.behaviors.end()
                && behavior->second.targetObjectId == target.instanceId) {
                ++accepted; ++state.triggers.ordersIssued;
            }
        } else if (!runtimeObjectVisibleToOwner(state, actor->owner, target)) {
            const auto profile = runtimeWeaponProfileForObject(state, *actor, &target);
            const auto destination = runtimeObjectMovementMode(state, *actor) == PathMovementMode::Naval && profile.weapon
                ? runtimeTeamNavalAttackApproach(state, *actor, target, *profile.weapon)
                : findRuntimePassableCellNear(state, *actor, runtimeCellPointForObject(target), 4);
            if (destination) accepted += issueRuntimeTeamMove(state, {objectId}, *destination);
        } else {
            issueRuntimeAttackCommand(state, target);
            const int count = std::max(0, state.lastCommand.acceptedCount);
            accepted += count; state.triggers.ordersIssued += count;
            const auto behavior = state.behaviors.find(objectId);
            if (count > 0 && behavior != state.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Attack) {
                const auto profile = runtimeWeaponProfileForObject(state, *actor, &target);
                // A Spy deliberately copies an infantry identity; this is
                // not an automatically acquired hostile weapon target.
                behavior->second.autoAcquired = profile.warhead == nullptr
                    || !profile.warhead->makesDisguise;
            }
        }
    }
    state.selectedObjectIds = savedSelection;
    state.runtimeSuppressPlayerAudioFeedback = savedAudio;
    return accepted;
}

bool runtimeTeamHasActorSpecificAttackTarget(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, const WorldObjectInstance& target) {
    for (int objectId : mission.objectIds) {
        const auto* actor = findObjectById(state, objectId);
        if (actor != nullptr && runtimeTeamActorCanInteractWithTarget(state, *actor, target))
            return true;
    }
    return false;
}

bool runtimeTeamQuarryMatches(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int quarry) {
    const auto* building = object.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    switch (quarry) {
    case 0: case 1: case 8: return true;
    case 2: return building != nullptr;
    case 3: return (building != nullptr && (building->refinery || building->resourceGatherer))
        || (unit != nullptr && (unit->harvester || unit->resourceGatherer));
    case 4: return object.kind == WorldObjectKind::Infantry;
    case 5: return object.kind == WorldObjectKind::Unit && unit != nullptr
        && !unit->airportBound && !unit->harvester && !unit->resourceGatherer;
    case 6: return building != nullptr && !trimAsciiLocal(building->factory).empty();
    case 7: return building != nullptr && building->isBaseDefense;
    case 9: return building != nullptr && building->power > 0;
    case 10: return building != nullptr && building->canBeOccupied;
    case 11: return building != nullptr && building->needsEngineer;
    default: return false;
    }
}

const WorldObjectInstance* runtimeTeamEnemyTarget(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, int quarry) {
    const auto center = runtimeTeamMissionCenter(state, mission);
    const auto intendedEnemy = runtimeComputerTacticTargetOwner(state, mission);
    const bool enemyStillPresent = !intendedEnemy.empty() && std::any_of(
        state.world.getObjects().begin(), state.world.getObjects().end(), [&](const auto& object) {
            return object.alive && toLowerAsciiLocal(object.owner) == toLowerAsciiLocal(intendedEnemy);
        });
    const WorldObjectInstance* best = nullptr;
    long long bestScore = std::numeric_limits<long long>::min();
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !runtimeTeamQuarryMatches(state, object, quarry)
            || (enemyStillPresent && !runtimeOwnerIsNeutralForAutoTarget(object.owner)
                && toLowerAsciiLocal(object.owner) != toLowerAsciiLocal(intendedEnemy))
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), object.instanceId)
                != mission.objectIds.end()
            || (runtimeOwnerIsNeutralForAutoTarget(object.owner) && quarry != 10 && quarry != 11)
            || !runtimeTeamHasActorSpecificAttackTarget(state, mission, object)) continue;
        long long score = object.kind == WorldObjectKind::Structure ? 5000LL : 1000LL;
        if (const auto* building = object.kind == WorldObjectKind::Structure
                ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr)
            score += building->cost;
        score -= static_cast<long long>(std::lround(
            runtimeCellDistance(center, runtimeCellPointForObject(object)) * 10.0));
        if (best == nullptr || score > bestScore
            || (score == bestScore && object.instanceId < best->instanceId)) {
            best = &object; bestScore = score;
        }
    }
    return best;
}

void recordRuntimeTeamTargetInteraction(RuntimeGameSessionState& state, int actorId, int targetId) {
    for (auto& [id, mission] : state.triggers.teamMissions) {
        if (!mission.completed && mission.stepIssued && mission.targetObjectId == targetId
            && std::find(mission.objectIds.begin(), mission.objectIds.end(), actorId)
                != mission.objectIds.end()) mission.targetInteractionCompleted = true;
    }
}

bool runtimeTeamAircraftNeedReload(const RuntimeGameSessionState& state,
    const std::vector<int>& objectIds) {
    bool emptyAircraft = false;
    for (int id : objectIds) {
        const auto* actor = findObjectById(state, id);
        const auto* unit = actor == nullptr ? nullptr : findRuntimeUnitType(state.rules, *actor);
        if (unit == nullptr || !unit->airportBound) return false;
        emptyAircraft = emptyAircraft || !runtimeObjectHasAttackAmmo(state.rules, *actor);
    }
    return emptyAircraft;
}

bool issueRuntimeTeamLoad(RuntimeProductionHostState& state, RuntimeTeamMissionState& mission) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    std::vector<int> transports;
    for (int id : members) {
        const auto* object = findObjectById(state, id);
        if (object && runtimeTransportProfileForObject(state, *object).canTransport) {
            transports.push_back(id);
            mission.transportReturnCells.try_emplace(id, runtimeCellPointForObject(*object));
        }
    }
    if (transports.empty()) return false;
    const auto savedSelection = state.selectedObjectIds;
    const bool savedAudio = state.runtimeSuppressPlayerAudioFeedback;
    state.runtimeSuppressPlayerAudioFeedback = true;
    bool queued = false;
    for (int passengerId : members) {
        if (std::find(transports.begin(), transports.end(), passengerId) != transports.end()) continue;
        if (state.pendingTransportTargets.contains(passengerId)) { queued = true; continue; }
        for (int transportId : transports) {
            const auto* transport = findObjectById(state, transportId);
            if (transport == nullptr) continue;
            applySelection(state, {passengerId}, false, false);
            (void)queueRuntimeTransportTarget(state, *transport);
            const auto pending = state.pendingTransportTargets.find(passengerId);
            if (pending != state.pendingTransportTargets.end() && pending->second == transportId) {
                queued = true; ++state.triggers.ordersIssued; break;
            }
        }
    }
    state.selectedObjectIds = savedSelection;
    state.runtimeSuppressPlayerAudioFeedback = savedAudio;
    return queued;
}

bool unloadRuntimeTeamTransports(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, int argument) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    std::vector<int> transports;
    bool cargoRemaining = false;
    for (int id : members) {
        auto* object = findMutableObjectById(state, id);
        if (object == nullptr || !runtimeTransportProfileForObject(state, *object).canTransport) continue;
        transports.push_back(id);
        if (!object->transportPassengerIds.empty()) (void)unloadRuntimeTransport(state, *object);
        object = findMutableObjectById(state, id);
        cargoRemaining = cargoRemaining || (object && !object->transportPassengerIds.empty());
    }
    if (cargoRemaining) return false;

    const auto team = state.triggers.teams.find(mission.teamTypeId);
    const bool returnTransports = team != state.triggers.teams.end()
        && team->second.transportsReturnOnUnload;
    // Retail action8: 0 keeps both, 1 releases passengers, 2 releases
    // transports, 3 releases both. A released transport may be sent home.
    const auto allMembers = mission.objectIds;
    for (int id : allMembers) {
        const bool transport = std::find(transports.begin(), transports.end(), id) != transports.end();
        if (transport ? (returnTransports || argument == 2 || argument == 3)
                      : (argument == 1 || argument == 3)) {
            const auto returnCell = mission.transportReturnCells.find(id);
            const auto destination = returnCell == mission.transportReturnCells.end()
                ? std::optional<RuntimeCellPoint>{} : std::optional<RuntimeCellPoint>{returnCell->second};
            detachRuntimeObjectFromTeamMembership(state, id);
            if (auto* object = findMutableObjectById(state, id)) object->mission = "Guard";
            if (transport && returnTransports && destination)
                (void)issueRuntimeTeamMove(state, {id}, *destination);
        }
    }
    return true;
}

RuntimeCellPoint runtimeTeamHouseCenter(const RuntimeGameSessionState& state,
    const std::string& owner, RuntimeCellPoint fallback) {
    long long x = 0, y = 0; int count = 0;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || toLowerAsciiLocal(object.owner) != toLowerAsciiLocal(owner)) continue;
        x += object.x; y += object.y; ++count;
    }
    return count ? RuntimeCellPoint{static_cast<int>(x / count), static_cast<int>(y / count)} : fallback;
}

RuntimeTeamStepDisposition issueRuntimeTeamGather(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, bool outsideEnemyBase) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    if (members.empty()) return RuntimeTeamStepDisposition::Complete;
    const auto owner = runtimeTeamMissionOwner(state, mission);
    const auto center = runtimeTeamMissionCenter(state, mission);
    const bool hasOwnBase = std::any_of(state.world.getObjects().begin(), state.world.getObjects().end(), [&](const auto& object) {
        return object.alive && object.kind == WorldObjectKind::Structure
            && toLowerAsciiLocal(object.owner) == toLowerAsciiLocal(owner);
    });
    const auto ownBase = runtimeTeamHouseCenter(state, owner, center);
    const auto* enemy = runtimeTeamEnemyTarget(state, mission, 2);
    const auto enemyBase = enemy
        ? runtimeTeamHouseCenter(state, enemy->owner, runtimeCellPointForObject(*enemy)) : center;
    if (outsideEnemyBase && !enemy) return RuntimeTeamStepDisposition::Complete;
    const auto origin = outsideEnemyBase ? enemyBase : ownBase;
    const auto toward = outsideEnemyBase ? ownBase : enemyBase;
    double dx = toward.x - origin.x, dy = toward.y - origin.y;
    const double length = std::hypot(dx, dy);
    if (length > 0.0) { dx /= length; dy /= length; }
    else { dx = 1.0; dy = 0.0; }
    const int distance = state.rules.getComputerTacticsRules().safeDistance;
    RuntimeCellPoint destination{origin.x + static_cast<int>(std::lround(dx * distance)),
        origin.y + static_cast<int>(std::lround(dy * distance))};
    if (!outsideEnemyBase && !hasOwnBase) destination = center;
    const auto* leader = findObjectById(state, members.front());
    const auto passable = leader ? findRuntimePassableCellNear(state, *leader, destination, 10) : std::nullopt;
    if (!passable) return RuntimeTeamStepDisposition::Waiting;
    mission.targetCell = *passable;
    // Gathering closes ranks. The ordinary player's move intentionally
    // preserves a group's old offsets, which can straddle a whole building.
    std::unordered_set<int> reserved;
    int issued = 0;
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto* actor = findObjectById(state, members[index]);
        if (!actor) continue;
        const auto offset = formationOffsetForIndex(static_cast<int>(index));
        const auto cell = findRuntimePassableCellNear(state, *actor,
            {passable->x + offset.x, passable->y + offset.y}, 4, -1, &reserved);
        if (!cell) continue;
        reserved.insert(runtimeCellKey(cell->x, cell->y));
        issued += issueRuntimeTeamMove(state, {actor->instanceId}, *cell);
    }
    return issued > 0 ? RuntimeTeamStepDisposition::Waiting : RuntimeTeamStepDisposition::Complete;
}

RuntimeCellPoint runtimeTeamSuperWeaponCenter(const RuntimeGameSessionState& state,
    const RuntimeTeamMissionState& mission, const SuperWeaponRule& rule) {
    const auto average = runtimeTeamMissionCenter(state, mission);
    std::vector<RuntimeCellPoint> cells;
    for (int id : mission.objectIds)
        if (const auto* member = findObjectById(state, id); member && !member->airborne)
            cells.push_back(runtimeCellPointForObject(*member));
    const int radius = std::max(1, rule.radius > 0 ? rule.radius
        : static_cast<int>(std::ceil(std::max(1.0, rule.range))));
    const auto covered = [&](RuntimeCellPoint point) {
        return std::count_if(cells.begin(), cells.end(), [&](RuntimeCellPoint cell) {
            return std::max(std::abs(cell.x - point.x), std::abs(cell.y - point.y)) <= radius;
        });
    };
    auto best = average; auto coverage = covered(best);
    for (auto cell : cells) if (const auto count = covered(cell); count > coverage) {
        best = cell; coverage = count;
    }
    return best;
}

RuntimeTeamStepDisposition issueRuntimeTeamOwnedSuperWeapon(RuntimeProductionHostState& state,
    RuntimeTeamMissionState& mission, bool chronosphere, RuntimeCellPoint destination) {
    const auto owner = runtimeTeamMissionOwner(state, mission);
    auto center = runtimeTeamMissionCenter(state, mission);
    if (center.x < 0 || center.y < 0) return RuntimeTeamStepDisposition::Complete;
    if (!state.config.superweapons) return RuntimeTeamStepDisposition::Complete;
    ensureRuntimeSuperWeaponsInitialized(state);
    if (!state.superWeaponsInitialized) return RuntimeTeamStepDisposition::Waiting;
    std::string weaponId;
    for (const auto& rule : state.rules.getSuperWeaponTypes()) {
        const auto identity = toLowerAsciiLocal(rule.id + " " + rule.type);
        if (identity.find(chronosphere ? "chrono" : "ironcurtain") == std::string::npos) continue;
        const auto* snapshot = state.superWeapons.findSidebarSnapshot(owner, rule.id);
        if (snapshot && snapshot->state == SuperWeaponRuntimeState::Ready) { weaponId = rule.id; break; }
    }
    if (weaponId.empty()) {
        mission.status = chronosphere ? "waiting for charged Chronosphere" : "waiting for charged Iron Curtain";
        return RuntimeTeamStepDisposition::Waiting;
    }
    center = runtimeTeamSuperWeaponCenter(state, mission, *state.rules.findSuperWeaponType(weaponId));
    if (chronosphere) {
        for (int objectId : mission.objectIds) {
            const auto* actor = findObjectById(state, objectId);
            if (!actor) continue;
            const auto cell = findRuntimePassableCellNear(state, *actor, destination, 6);
            if (cell) { destination = *cell; break; }
        }
    }
    RuntimeMultiplayerGameplayFireSuperWeaponCommand command;
    command.superWeaponId = weaponId;
    RuntimeMultiplayerGameplaySuperWeaponTarget first;
    first.cell = {center.x, center.y};
    command.targets.push_back(first);
    if (chronosphere) {
        RuntimeMultiplayerGameplaySuperWeaponTarget second;
        second.cell = {destination.x, destination.y}; command.targets.push_back(second);
    }
    // Validate without side effects, using the same charged/source/power and
    // target rules as a player's launch. No grant or trigger-force-fire path.
    if (!runtimeGameplaySuperWeaponReadyForCommand(state, owner, command))
        return RuntimeTeamStepDisposition::Waiting;
    auto context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    const SuperWeaponTarget source{{center.x, center.y}, -1};
    bool fired = false;
    if (chronosphere) {
        fired = state.superWeapons.submitTwoStageTarget(owner, weaponId, source,
            SuperWeaponTarget{{destination.x, destination.y}, -1}, context);
    } else if (state.superWeapons.beginTargetSelection(owner, weaponId, context)) {
        fired = state.superWeapons.submitTarget(owner, weaponId, source, context);
    }
    (void)drainRuntimeObjectExitQueue(state);
    if (fired) mission.status = chronosphere ? "Chronosphere launched" : "Iron Curtain launched";
    return fired ? RuntimeTeamStepDisposition::Complete : RuntimeTeamStepDisposition::Waiting;
}

RuntimeCampaignAiTriggerDefinition* findRuntimeCampaignAiTriggerById(
    RuntimeTriggerState& triggers,
    const std::string& id) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(id));
    const auto found = std::find_if(
        triggers.campaignAiTriggers.begin(),
        triggers.campaignAiTriggers.end(),
        [&wanted](const RuntimeCampaignAiTriggerDefinition& candidate) {
            return toLowerAsciiLocal(trimAsciiLocal(candidate.id)) == wanted;
        }
    );
    return found == triggers.campaignAiTriggers.end() ? nullptr : &*found;
}

void reportRuntimeCampaignAiTriggerOutcome(
    RuntimeGameSessionState& state,
    const std::string& triggerId,
    bool success) {
    RuntimeCampaignAiTriggerDefinition* trigger =
        findRuntimeCampaignAiTriggerById(state.triggers, triggerId);
    if (trigger == nullptr) {
        return;
    }
    if (success) {
        ++trigger->timesCompleted;
        ++state.triggers.campaignAiTriggerTeamsCompleted;
        trigger->currentWeight = std::clamp(
            trigger->currentWeight + 20.0,
            trigger->minimumWeight,
            trigger->maximumWeight
        );
    } else {
        ++trigger->timesFailed;
        ++state.triggers.campaignAiTriggerTeamsFailed;
        trigger->currentWeight = std::clamp(
            trigger->currentWeight - 50.0,
            trigger->minimumWeight,
            trigger->maximumWeight
        );
    }
    trigger->nextEligibleTick = state.tick
        + std::max(1, state.config.fixedTickHz * 2);
}

void noteRuntimeTeamMemberDamaged(RuntimeGameSessionState& state, int memberId, int attackerId) {
    if (attackerId < 0) return;
    for (auto& [id, mission] : state.triggers.teamMissions) {
        if (mission.completed || mission.regrouping
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), memberId) == mission.objectIds.end()
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), attackerId) != mission.objectIds.end()) continue;
        const auto* type = findRuntimeTriggerTeamById(state, mission.teamTypeId);
        if (!type || type->suicide || mission.targetObjectId == attackerId) continue;
        const auto script = state.triggers.scripts.find(mission.scriptId);
        const int action = script != state.triggers.scripts.end() && mission.stepIndex < script->second.steps.size()
            ? script->second.steps[mission.stepIndex].action : -1;
        // Retail reforms while assembling or for an aggressive armed team.
        // The attacker is not promoted to the script's strategic target.
        if (mission.stepIssued && action != 53 && action != 54) {
            if (!type->aggressive) continue;
            const auto* leader = mission.objectIds.empty() ? nullptr : findObjectById(state, mission.objectIds.front());
            if (!leader || !runtimeWeaponProfileForObject(state, *leader).weapon) continue;
            const auto& aircraft = state.rules.getAircraftTypes();
            if (std::any_of(aircraft.begin(), aircraft.end(), [&](const auto& unit) {
                    return toUpperAsciiLocal(unit.id) == toUpperAsciiLocal(leader->type); })) continue;
        }
        // A committed entry or superweapon instruction must finish through
        // its own relationship before the squad can receive another move.
        if (action == 8 || action == 43 || action == 55 || action == 56 || action == 57
            || action == 60 || action == 62) continue;
        mission.regrouping = true;
        mission.regroupCell = {-1, -1};
        mission.regroupDestinations.clear();
        mission.regroupStartedTick = state.tick;
    }
}

bool advanceRuntimeTeamRegroup(RuntimeProductionHostState& state, RuntimeTeamMissionState& mission) {
    const auto live = runtimeLiveTeamObjectIds(state, mission);
    if (live.empty()) return false;
    const auto script = state.triggers.scripts.find(mission.scriptId);
    if (script == state.triggers.scripts.end() || mission.stepIndex >= script->second.steps.size()) return false;
    const int action = script->second.steps[mission.stepIndex].action;
    const bool marching = action == 0 || action == 1 || action == 3 || action == 4 || action == 16
        || action == 46 || action == 47 || action == 48 || action == 58 || action == 59;
    if (!mission.regrouping && mission.stepIssued && marching && runtimeTeamGuardHasStrength(state, mission)) {
        std::vector<const WorldObjectInstance*> supported;
        for (int id : live) if (const auto* member = findObjectById(state, id);
            member && runtimeTeamMemberNeedsGuard(state, *member)) supported.push_back(member);
        for (int id : live) {
            const auto* member = findObjectById(state, id);
            if (!member || supported.empty() || runtimeTeamMemberNeedsGuard(state, *member)) continue;
            const auto behavior = state.behaviors.find(id);
            if (behavior == state.behaviors.end() || behavior->second.path.empty()) continue;
            // World movement commits whole cells. Leave enough space for
            // occupied neighboring cells while keeping escorts with support.
            const double radius = std::max(2.0, 3.0 * runtimeTeamMemberStray(state, mission, *member, action));
            auto destination = mission.targetCell;
            if (const auto* target = findObjectById(state, mission.targetObjectId))
                destination = runtimeCellPointForObject(*target);
            if (destination.x < 0) continue;
            const auto memberCell = runtimeCellPointForObject(*member);
            const double remaining = runtimeCellDistance(memberCell, destination);
            const bool outranSupport = std::any_of(supported.begin(), supported.end(), [&](const auto* support) {
                const auto supportCell = runtimeCellPointForObject(*support);
                return runtimeCellDistance(memberCell, supportCell) > radius
                    && remaining + radius < runtimeCellDistance(supportCell, destination);
            });
            // An escort that is behind its support should catch up along
            // the current route, rather than pull the whole group backwards.
            if (outranSupport) {
                mission.regrouping = true; mission.regroupStartedTick = state.tick; break;
            }
        }
    }
    if (!mission.regrouping) return false;
    if (mission.regroupCell.x < 0) {
        mission.regroupCell = runtimeTeamMissionCenter(state, mission);
        std::unordered_set<int> reserved;
        // Place protected support first, then escorts around it. Every
        // assigned destination is a real passable cell, saved mid-regroup.
        auto members = live;
        if (runtimeTeamGuardsSlower(state, mission)) std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
            return runtimeTeamMemberNeedsGuard(state, *findObjectById(state, a))
                > runtimeTeamMemberNeedsGuard(state, *findObjectById(state, b));
        });
        for (int id : members) {
            const auto* member = findObjectById(state, id);
            if (!member || state.pendingTransportTargets.contains(id) || state.pendingEngineerCaptures.contains(id)
                || state.pendingOccupyTargets.contains(id) || member->factoryExitHostId >= 0) continue;
            const auto target = findRuntimePassableCellNear(state, *member, mission.regroupCell, 6, -1, &reserved);
            if (!target) continue;
            reserved.insert(runtimeCellKey(target->x, target->y));
            mission.regroupDestinations[id] = *target;
        }
    }
    bool arrived = true;
    for (const auto& [id, cell] : mission.regroupDestinations) {
        const auto* member = findObjectById(state, id);
        if (!member) continue;
        const bool atDestination = member->x == cell.x && member->y == cell.y;
        if (atDestination) {
            const auto moving = state.behaviors.find(id);
            if (moving != state.behaviors.end() && moving->second.kind == RuntimeBehaviorKind::Move)
                eraseRuntimeBehavior(state, id);
            continue;
        }
        arrived = false;
        const auto behavior = state.behaviors.find(id);
        if (behavior == state.behaviors.end() || behavior->second.kind != RuntimeBehaviorKind::Move
            || behavior->second.moveTarget.x != cell.x || behavior->second.moveTarget.y != cell.y)
            (void)issueRuntimeTeamMove(state, {id}, cell);
    }
    if (arrived) {
        mission.regrouping = false; mission.regroupCell = {-1, -1};
        mission.regroupDestinations.clear(); mission.regroupStartedTick = -1;
        mission.stepIssued = false;
        return false;
    }
    mission.status = "team regrouping";
    return true;
}

bool advanceRuntimeTeamMissions(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeTeamMissions", state.tick);
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    if (!state.triggers.loaded || state.triggers.teamMissions.empty()) {
        return false;
    }
    bool changed = false;
    std::vector<int> completedMissionIds;
    for (auto& [missionId, mission] : state.triggers.teamMissions) {
        if (mission.completed) {
            completedMissionIds.push_back(missionId);
            continue;
        }
        const bool hasLiveMember = std::any_of(
            mission.objectIds.begin(),
            mission.objectIds.end(),
            [&state](int objectId) {
                const WorldObjectInstance* object = findObjectById(
                    state,
                    objectId
                );
                return object != nullptr && object->alive;
            }
        );
        const bool hasPendingTeamRejoin = std::any_of(
            mission.objectIds.begin(),
            mission.objectIds.end(),
            [&state](int objectId) {
                return runtimeObjectIsPendingTeamRejoinAfterLimbo(
                    state,
                    objectId
                );
            }
        );
        // A team made solely from temporarily limboed members still exists.
        // Mixed teams keep running, and the returning member rejoins whatever
        // script step is current when it comes back.
        if (!hasLiveMember && hasPendingTeamRejoin) {
            continue;
        }
        if (!mission.sourceAiTriggerId.empty()) {
            const bool hasSurvivingMember = std::any_of(
                mission.objectIds.begin(),
                mission.objectIds.end(),
                [&state](int objectId) {
                    const WorldObjectInstance* object =
                        findRuntimeObjectByIdIncludingInactive(
                            state,
                            objectId
                        );
                    return object != nullptr
                        && (object->alive
                            || object->mission == "InsideTransport"
                            || runtimeObjectIsPendingTeamRejoinAfterLimbo(
                                state,
                                objectId
                            ));
                }
            );
            if (!hasSurvivingMember && !mission.targetInteractionCompleted) {
                mission.completed = true;
                mission.status = "AI trigger team destroyed";
                completedMissionIds.push_back(missionId);
                changed = true;
                continue;
            }
        }
        if (advanceRuntimeTeamRegroup(state, mission)) { changed = true; continue; }
        for (int immediateSteps = 0; immediateSteps < 12; ++immediateSteps) {
            const auto scriptIt = state.triggers.scripts.find(mission.scriptId);
            if (scriptIt == state.triggers.scripts.end()
                || mission.stepIndex >= scriptIt->second.steps.size()) {
                mission.completed = true;
                mission.status = scriptIt == state.triggers.scripts.end()
                    ? "missing script " + mission.scriptId
                    : "script complete";
                completedMissionIds.push_back(missionId);
                changed = true;
                break;
            }
            const RuntimeScriptStep step =
                scriptIt->second.steps[mission.stepIndex];
            if (!mission.stepIssued) {
                mission.stepIssued = true;
                ++state.triggers.scriptStepsIssued;
                const RuntimeTeamStepDisposition disposition =
                    issueRuntimeTeamScriptStep(state, mission, step);
                changed = true;
                if (disposition == RuntimeTeamStepDisposition::Waiting) {
                    break;
                }
                if (disposition == RuntimeTeamStepDisposition::Restart) {
                    ++state.triggers.scriptStepsCompleted;
                    continue;
                }
                if (disposition == RuntimeTeamStepDisposition::Unsupported) {
                    ++state.triggers.unsupportedScriptSteps;
                    mission.status = "unsupported script action "
                        + std::to_string(step.action);
                }
                ++state.triggers.scriptStepsCompleted;
                if (mission.completed) {
                    completedMissionIds.push_back(missionId);
                    break;
                }
                ++mission.stepIndex;
                mission.stepIssued = false;
                continue;
            }

            const RuntimeTeamStepCompletion completion =
                runtimeTeamScriptStepCompletion(state, mission, step);
            if (completion == RuntimeTeamStepCompletion::Waiting) {
                break;
            }
            if (completion == RuntimeTeamStepCompletion::Reissue) {
                mission.stepIssued = false;
                mission.targetObjectId = -1;
                mission.targetCell = {-1, -1};
                break;
            }
            ++state.triggers.scriptStepsCompleted;
            ++mission.stepIndex;
            mission.stepIssued = false;
            mission.targetObjectId = -1;
            mission.targetCell = {-1, -1};
            changed = true;
        }
    }
    std::sort(completedMissionIds.begin(), completedMissionIds.end());
    completedMissionIds.erase(
        std::unique(completedMissionIds.begin(), completedMissionIds.end()),
        completedMissionIds.end()
    );
    for (int missionId : completedMissionIds) {
        const auto mission = state.triggers.teamMissions.find(missionId);
        if (mission == state.triggers.teamMissions.end()) {
            continue;
        }
        const bool computerMission = finishRuntimeComputerTacticMission(state, mission->second);
        if (!computerMission && !mission->second.sourceAiTriggerId.empty()
            && !mission->second.aiOutcomeReported) {
            const bool success = mission->second.achievedGreatSuccess
                || toLowerAsciiLocal(trimAsciiLocal(mission->second.status)) == "script success";
            reportRuntimeCampaignAiTriggerOutcome(
                state,
                mission->second.sourceAiTriggerId,
                success
            );
        }
        state.triggers.teamMissions.erase(mission);
    }
    return changed;
}

bool fireRuntimeCreateTeamTrigger(
    RuntimeProductionHostState& state,
    RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action,
    bool allowFallback,
    bool markTriggerFired) {
    const RuntimeTeamType* team = chooseRuntimeTriggerTeamForAction(
        state,
        action,
        allowFallback
    );
    if (team == nullptr) {
        state.triggers.status = "no valid team for trigger " + trigger.id;
        return false;
    }
    if (runtimeTriggerTeamAtCapacity(state, *team)) {
        // C3Y03MD has linked Create Team actions for the same Max=1 team.
        // Honoring that limit is a supported no-op, not a missing action.
        if (markTriggerFired) {
            trigger.fired = true;
            ++state.triggers.firedTriggers;
        }
        state.triggers.status = "team " + team->id + " retained at Max=" + std::to_string(team->max);
        return true;
    }

    std::optional<RuntimeCellPoint> authoredSpawnOrigin;
    if (action.type == 80 || action.type == 107) {
        authoredSpawnOrigin = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
    }
    if (!queueRuntimeTriggerReinforcement(state, *team, authoredSpawnOrigin, action.type)) {
        return false;
    }
    if (markTriggerFired) {
        trigger.fired = true;
        ++state.triggers.firedTriggers;
    }
    state.triggers.status =
        "fired " + trigger.id
        + " -> team " + team->id
        + " accepted; delivered=" + std::to_string(state.triggers.unitsCreated)
        + " orders=" + std::to_string(state.triggers.ordersIssued);
    return true;
}

std::string runtimeCampaignHouseName(
    const RuntimeGameSessionState& state,
    const std::string& rawHouse) {
    const std::string trimmed = trimAsciiLocal(rawHouse);
    if (const auto slot = state.cooperativeSession.resolveAuthoredSlotAlias(trimmed))
        return state.cooperativeSession.slots()[static_cast<std::size_t>(*slot)].owner;
    if (trimmed.empty() || trimmed == "<none>" || trimmed == "None") {
        return {};
    }
    int houseIndex = -1;
    const std::size_t digitOffset =
        (trimmed[0] == '+' || trimmed[0] == '-') ? 1U : 0U;
    if (digitOffset < trimmed.size()
        && trimmed[digitOffset] >= '0'
        && trimmed[digitOffset] <= '9'
        && parseRuntimeInt(trimmed, houseIndex)) {
        const auto country =
            state.triggers.campaignCountryByIndex.find(houseIndex);
        if (country != state.triggers.campaignCountryByIndex.end()) {
            const auto alias =
                state.triggers.campaignCanonicalHouseByAlias.find(
                    toLowerAsciiLocal(trimAsciiLocal(country->second))
                );
            return alias == state.triggers.campaignCanonicalHouseByAlias.end()
                ? country->second
                : alias->second;
        }
        const auto house = state.triggers.campaignHouseByIndex.find(houseIndex);
        if (house != state.triggers.campaignHouseByIndex.end()) {
            return house->second;
        }
    }
    const auto alias = state.triggers.campaignCanonicalHouseByAlias.find(
        toLowerAsciiLocal(trimmed)
    );
    return alias == state.triggers.campaignCanonicalHouseByAlias.end()
        ? trimmed
        : alias->second;
}

bool runtimeCampaignHouseIsPlayer(
    const RuntimeGameSessionState& state,
    const std::string& rawHouse) {
    if (state.cooperativeSession.active()) {
        const auto slot = runtimeCooperativeSlotForOwner(state, runtimeCampaignHouseName(state, rawHouse));
        return slot && *slot < 2;
    }
    const std::string house = toUpperAsciiLocal(
        trimAsciiLocal(runtimeCampaignHouseName(state, rawHouse))
    );
    if (house.empty()) {
        return false;
    }

    const std::string playerHouse = toUpperAsciiLocal(
        trimAsciiLocal(state.economy.owner)
    );
    if (house == playerHouse) {
        return true;
    }
    if ((house == "PLAYER" || house == "PLAYER HOUSE")
        && (playerHouse == "PLAYER" || playerHouse == "PLAYER HOUSE")) {
        return true;
    }

    const std::string country = toUpperAsciiLocal(
        trimAsciiLocal(state.config.playerCountry)
    );
    const bool sovietCountry = country.find("RUSS") != std::string::npos
        || country.find("SOVIET") != std::string::npos;
    if (sovietCountry
        && (house == "RUSSIANS"
            || house == "SOVIETS"
            || house == "SOVIETSCOUNTRY"
            || house == "SOVIETSHOUSE")) {
        return true;
    }

    const bool alliedCountry = country.find("AMERICAN") != std::string::npos
        || country.find("ALLI") != std::string::npos
        || country == "GDI";
    return alliedCountry
        && (house == "AMERICANS"
            || house == "ALLIANCE"
            || house == "ALLIES"
            || house == "GDI");
}

bool runtimeCampaignHouseMatches(
    const RuntimeGameSessionState& state,
    const std::string& authoredHouse,
    const std::string& objectHouse) {
    const std::string left = runtimeCampaignHouseName(state, authoredHouse);
    const std::string right = runtimeCampaignHouseName(state, objectHouse);
    if (left.empty() || right.empty()) {
        return false;
    }
    if (toLowerAsciiLocal(trimAsciiLocal(left))
        == toLowerAsciiLocal(trimAsciiLocal(right))) {
        return true;
    }
    if (state.cooperativeSession.active()) {
        const auto leftSlot = runtimeCooperativeSlotForOwner(state, left);
        const auto rightSlot = runtimeCooperativeSlotForOwner(state, right);
        if (!leftSlot || !rightSlot) return false;
        const auto explicitOwner = [&](const std::string& name) {
            for (const auto& slot : state.cooperativeSession.slots())
                if (toLowerAsciiLocal(name) == toLowerAsciiLocal(slot.owner)) return true;
            return false;
        };
        // A and B are separate houses. Only an authored country name may
        // address the shared coalition when the map has no single house alias.
        return (!explicitOwner(left) || !explicitOwner(right))
            && state.cooperativeSession.slotsAreAllied(*leftSlot, *rightSlot);
    }
    return runtimeCampaignHouseIsPlayer(state, left)
        && runtimeCampaignHouseIsPlayer(state, right);
}

std::string runtimeCampaignEventHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event) {
    // Event records are encoded as type,param-count,param-1[,param-2].
    if (event.arguments.size() > 2 && !event.arguments[2].empty()) {
        const std::string resolved = runtimeCampaignHouseName(
            state,
            event.arguments[2]
        );
        if (!resolved.empty()) {
            return resolved;
        }
    }
    return runtimeCampaignHouseName(state, trigger.house);
}

std::string runtimeCampaignTriggerHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger) {
    return runtimeCampaignHouseName(state, trigger.house);
}

bool runtimeCampaignTriggerHasTag(
    const RuntimeTriggerDefinition& trigger,
    const std::string& tag) {
    return trigger.tagIds.find(toLowerAsciiLocal(trimAsciiLocal(tag)))
        != trigger.tagIds.end();
}

bool runtimeCampaignObjectAttachedToTrigger(
    const RuntimeTriggerDefinition& trigger,
    const WorldObjectInstance& object) {
    return !object.tag.empty()
        && runtimeCampaignTriggerHasTag(trigger, object.tag);
}

bool runtimeCampaignCellAttachedToTrigger(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    int cell) {
    const auto found = state.triggers.campaignCellTagByCell.find(cell);
    return found != state.triggers.campaignCellTagByCell.end()
        && runtimeCampaignTriggerHasTag(trigger, found->second);
}

bool runtimeCampaignEventHouseMatchesObject(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    const std::string& objectHouse) {
    if (event.arguments.size() > 2
        && trimAsciiLocal(event.arguments[2]) == "-1") {
        return true;
    }
    return runtimeCampaignHouseMatches(
        state,
        runtimeCampaignEventHouse(state, trigger, event),
        objectHouse
    );
}

bool runtimeCampaignAttachedObjectEntered(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event) {
    for (const auto& [targetId, enteredTick] :
            state.triggers.campaignEnteredTickByTargetObject) {
        if (enteredTick < trigger.activationTick
            || enteredTick <= trigger.lastFiredTick) {
            continue;
        }
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(state, targetId);
        if (target == nullptr
            || !runtimeCampaignObjectAttachedToTrigger(trigger, *target)) {
            continue;
        }
        const auto entrant =
            state.triggers.campaignEntrantHouseByTargetObject.find(targetId);
        if (entrant != state.triggers.campaignEntrantHouseByTargetObject.end()
            && runtimeCampaignEventHouseMatchesObject(
                state,
                trigger,
                event,
                entrant->second
            )) {
            return true;
        }
    }
    return false;
}

bool runtimeCampaignAttachedObjectSpied(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event) {
    for (const auto& [targetId, spiedTick] :
            state.triggers.campaignSpiedTickByTargetObject) {
        if (spiedTick < trigger.activationTick
            || spiedTick <= trigger.lastFiredTick) {
            continue;
        }
        const WorldObjectInstance* target =
            findRuntimeObjectByIdIncludingInactive(state, targetId);
        if (target == nullptr
            || !runtimeCampaignObjectAttachedToTrigger(trigger, *target)) {
            continue;
        }
        const auto spyHouse =
            state.triggers.campaignSpyHouseByTargetObject.find(targetId);
        if (spyHouse != state.triggers.campaignSpyHouseByTargetObject.end()
            && runtimeCampaignEventHouseMatchesObject(
                state,
                trigger,
                event,
                spyHouse->second
            )) {
            return true;
        }
    }
    return false;
}

bool runtimeCampaignCellEntered(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    bool allowAircraft) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind == WorldObjectKind::Structure
            || (!allowAircraft && runtimeObjectIsAirTarget(state, object))
            || !runtimeCampaignEventHouseMatchesObject(
                state,
                trigger,
                event,
                object.owner
            )) {
            continue;
        }
        const int currentCell = runtimeCellKey(object.x, object.y);
        const auto previous =
            state.triggers.campaignPreviousCellByObject.find(object.instanceId);
        if (previous == state.triggers.campaignPreviousCellByObject.end()
            || previous->second == currentCell
            || !runtimeCampaignCellAttachedToTrigger(
                state,
                trigger,
                currentCell
            )) {
            continue;
        }
        return true;
    }
    return false;
}

bool runtimeCampaignTaggedObjectWasAttacked(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent* houseEvent) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
            continue;
        }
        const auto attacked =
            state.triggers.campaignAttackedTickByObject.find(object.instanceId);
        if (attacked == state.triggers.campaignAttackedTickByObject.end()
            || attacked->second < trigger.activationTick
            || attacked->second <= trigger.lastFiredTick) {
            continue;
        }
        const auto attacker =
            state.triggers.campaignAttackerHouseByObject.find(object.instanceId);
        if (attacker == state.triggers.campaignAttackerHouseByObject.end()) {
            continue;
        }
        if (houseEvent != nullptr) {
            if (runtimeCampaignEventHouseMatchesObject(
                    state,
                    trigger,
                    *houseEvent,
                    attacker->second
                )) {
                return true;
            }
            continue;
        }
        if (!runtimeOwnersAreAllied(state, attacker->second, object.owner)
            && !runtimeCampaignHouseMatches(
                state,
                attacker->second,
                object.owner
            )) {
            return true;
        }
    }
    return false;
}

int runtimeCampaignPowerNetForHouse(
    const RuntimeGameSessionState& state,
    const std::string& house) {
    int produced = 0;
    int used = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeCampaignHouseMatches(state, house, object.owner)) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr) {
            continue;
        }
        const int power = runtimeBuildingPowerOutput(*building, object);
        if (power >= 0) {
            produced += power;
        } else {
            used += -power;
        }
    }
    return produced - used;
}

bool runtimeCampaignObjectMatchesType(
    const WorldObjectInstance& object,
    const std::string& rawType) {
    const std::string type = trimAsciiLocal(rawType);
    if (type.empty() || type == "0" || type == "-1") {
        return true;
    }
    int ignored = -1;
    const std::size_t digitOffset =
        (type[0] == '+' || type[0] == '-') ? 1U : 0U;
    if (digitOffset < type.size()
        && type[digitOffset] >= '0'
        && type[digitOffset] <= '9'
        && parseRuntimeInt(type, ignored)) {
        return true;
    }
    return toLowerAsciiLocal(trimAsciiLocal(object.type))
        == toLowerAsciiLocal(type);
}

std::string runtimeCampaignIndexedTypeId(
    const RuntimeGameSessionState& state,
    int eventType,
    int index) {
    if (index < 0) {
        return {};
    }
    const std::vector<std::string>* authoredTypes = nullptr;
    if (eventType == 19 || eventType == 32 || eventType == 57) {
        authoredTypes = &state.rules.getAuthoredBuildingTypeIds();
    } else if (eventType == 20) {
        authoredTypes = &state.rules.getAuthoredVehicleTypeIds();
    } else if (eventType == 22) {
        authoredTypes = &state.rules.getAuthoredAircraftTypeIds();
    }
    if (authoredTypes != nullptr) {
        return static_cast<std::size_t>(index) < authoredTypes->size()
            ? (*authoredTypes)[static_cast<std::size_t>(index)] : std::string{};
    }
    if (eventType == 21) {
        const auto& types = state.rules.getInfantryTypes();
        return static_cast<std::size_t>(index) < types.size()
            ? types[static_cast<std::size_t>(index)].id : std::string{};
    }
    return {};
}

bool runtimeCampaignCrossedTaggedLine(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event,
    bool vertical) {
    std::unordered_set<int> lineCoordinates;
    for (const auto& [cell, tag] : state.triggers.campaignCellTagByCell) {
        if (runtimeCampaignTriggerHasTag(trigger, tag)) {
            lineCoordinates.insert(vertical ? cell % 1000 : cell / 1000);
        }
    }
    if (lineCoordinates.empty()) {
        return false;
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind == WorldObjectKind::Structure
            || !runtimeCampaignEventHouseMatchesObject(
                state,
                trigger,
                event,
                object.owner
            )) {
            continue;
        }
        const auto previous =
            state.triggers.campaignPreviousCellByObject.find(object.instanceId);
        if (previous == state.triggers.campaignPreviousCellByObject.end()) {
            continue;
        }
        const int before = vertical
            ? previous->second % 1000
            : previous->second / 1000;
        const int after = vertical ? object.x : object.y;
        for (int line : lineCoordinates) {
            if ((before < line && after >= line)
                || (before > line && after <= line)) {
                return true;
            }
        }
    }
    return false;
}

int runtimeCampaignObjectCount(
    const RuntimeGameSessionState& state,
    const std::string& house,
    std::optional<WorldObjectKind> kind,
    bool navalOnly,
    bool landOnly,
    const std::string& typeFilter) {
    int count = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || !runtimeCampaignHouseMatches(state, house, object.owner)
            || (kind.has_value() && object.kind != *kind)
            || !runtimeCampaignObjectMatchesType(object, typeFilter)) {
            continue;
        }
        if (navalOnly || landOnly) {
            bool naval = false;
            if (object.kind != WorldObjectKind::Structure) {
                const UnitType* unit =
                    findRuntimeUnitTypeById(state.rules, object.type);
                naval = unit != nullptr && runtimeUnitTypeLooksNaval(*unit);
            }
            if (navalOnly != naval) {
                continue;
            }
        }
        ++count;
    }
    return count;
}

bool runtimeCampaignEventSatisfied(
    RuntimeProductionHostState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerEvent& event) {
    RuntimePerfTraceScope originalMapCampaignLeafTrace("runtimeCampaignEventSatisfied", state.tick);
    const auto argument = [&event](std::size_t index) -> std::string {
        return index < event.arguments.size()
            ? trimAsciiLocal(event.arguments[index])
            : std::string{};
    };
    const int value = parseRuntimeIntOrDefault(argument(2), 0);
    const int fixedTickHz = std::max(1, state.config.fixedTickHz);
    const int elapsedSinceActivation = std::max(
        0,
        state.tick - trigger.activationTick
    );
    const auto timeEvent = [&]() {
        return elapsedSinceActivation >= std::max(0, value) * fixedTickHz;
    };

    switch (event.type) {
    case 0:
        return false;
    case 45:
    case 46:
        return event.lightingEventId > 0
            && state.lightingCrossingsThisTick.contains(
                event.lightingEventId
            );
    case 1:
        return runtimeCampaignAttachedObjectEntered(state, trigger, event)
            || runtimeCampaignCellEntered(
                state,
                trigger,
                event,
                false
            );
    case 2:
        return runtimeCampaignAttachedObjectSpied(state, trigger, event);
    case 4:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && runtimeCampaignObjectAttachedToTrigger(trigger, object)
                && runtimeScenarioCellExplored(state, object.x, object.y)
                && state.triggers.campaignPreviouslyDiscoveredObjects.find(
                    object.instanceId
                ) == state.triggers.campaignPreviouslyDiscoveredObjects.end()) {
                return true;
            }
        }
        return false;
    case 5:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && runtimeCampaignEventHouseMatchesObject(
                    state,
                    trigger,
                    event,
                    object.owner
                )
                && runtimeScenarioCellExplored(state, object.x, object.y)
                && state.triggers.campaignPreviouslyDiscoveredObjects.find(
                    object.instanceId
                ) == state.triggers.campaignPreviouslyDiscoveredObjects.end()) {
                return true;
            }
        }
        return false;
    case 6:
        return runtimeCampaignTaggedObjectWasAttacked(
            state,
            trigger,
            nullptr
        );
    case 7:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            const auto destroyed =
                state.triggers.campaignDestroyedTickByObject.find(
                    object.instanceId
                );
            if (destroyed == state.triggers.campaignDestroyedTickByObject.end()
                || destroyed->second < trigger.activationTick
                || destroyed->second <= trigger.lastFiredTick) {
                continue;
            }
            const auto destroyer =
                state.triggers.campaignDestroyerHouseByObject.find(
                    object.instanceId
                );
            if (destroyer != state.triggers.campaignDestroyerHouseByObject.end()
                && !runtimeOwnersAreAllied(
                    state,
                    destroyer->second,
                    object.owner
                )
                && !runtimeCampaignHouseMatches(
                    state,
                    destroyer->second,
                    object.owner
                )) {
                return true;
            }
        }
        return false;
    case 8:
        return true;
    case 13:
    case 51:
        return timeEvent();
    case 47:
        return state.tick >= std::max(0, value) * fixedTickHz;
    case 14:
        return state.triggers.campaignMissionTimerStarted
            && state.triggers.campaignMissionTimerRemainingTicks <= 0;
    case 9:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            WorldObjectKind::Unit,
            false,
            false
        ) == 0
            && runtimeCampaignObjectCount(
                state,
                runtimeCampaignEventHouse(state, trigger, event),
                WorldObjectKind::Infantry,
                false,
                false
            ) == 0;
    case 10:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            WorldObjectKind::Structure,
            false,
            false
        ) == 0;
    case 11:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            std::nullopt,
            false,
            false
        ) == 0;
    case 15:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignTriggerHouse(state, trigger),
            WorldObjectKind::Structure,
            false,
            false
        ) <= std::max(0, value);
    case 16:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignTriggerHouse(state, trigger),
            std::nullopt,
            false,
            true
        ) <= std::max(0, value);
    case 17:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.kind != WorldObjectKind::Structure
                || !runtimeCampaignHouseMatches(
                    state,
                    runtimeCampaignTriggerHouse(state, trigger),
                    object.owner
                )) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building != nullptr
                && (building->weaponsFactory || building->naval)) {
                return false;
            }
        }
        return true;
    case 19:
    case 20:
    case 21:
    case 22: {
        const std::string builtType = runtimeCampaignIndexedTypeId(
            state,
            event.type,
            value
        );
        if (builtType.empty()) {
            return false;
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && state.triggers.campaignPreviouslyKnownObjects.find(
                    object.instanceId
                ) == state.triggers.campaignPreviouslyKnownObjects.end()
                && runtimeCampaignHouseMatches(
                    state,
                    runtimeCampaignTriggerHouse(state, trigger),
                    object.owner
                )
                && toLowerAsciiLocal(object.type)
                    == toLowerAsciiLocal(builtType)) {
                return true;
            }
        }
        return false;
    }
    case 25:
        return runtimeCampaignCrossedTaggedLine(
            state,
            trigger,
            event,
            false
        );
    case 26:
        return runtimeCampaignCrossedTaggedLine(
            state,
            trigger,
            event,
            true
        );
    case 27:
    case 28: {
        const auto found = state.triggers.campaignGlobalVariables.find(value);
        const bool set = found != state.triggers.campaignGlobalVariables.end()
            && found->second != 0;
        return event.type == 27 ? set : !set;
    }
    case 29:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            const auto previousAlive =
                state.triggers.campaignPreviousAliveByObject.find(
                    object.instanceId
                );
            const auto previousOwner =
                state.triggers.campaignPreviousOwnerByObject.find(
                    object.instanceId
                );
            const bool destroyed = !object.alive
                && previousAlive
                    != state.triggers.campaignPreviousAliveByObject.end()
                && previousAlive->second;
            const bool captured = previousOwner
                    != state.triggers.campaignPreviousOwnerByObject.end()
                && !runtimeCampaignHouseMatches(
                    state,
                    previousOwner->second,
                    object.owner
                );
            if (destroyed || captured) {
                return true;
            }
        }
        return false;
    case 30:
        return runtimeCampaignPowerNetForHouse(
            state,
            runtimeCampaignEventHouse(state, trigger, event)
        ) < 0;
    case 31:
        for (const auto& [cell, destroyedTick] :
                state.triggers.campaignBridgeDestroyedTickByCell) {
            if (destroyedTick >= trigger.activationTick
                && destroyedTick > trigger.lastFiredTick
                && runtimeCampaignCellAttachedToTrigger(
                    state,
                    trigger,
                    cell
                )) {
                return true;
            }
        }
        return false;
    case 32: {
        const std::string buildingType = runtimeCampaignIndexedTypeId(
            state,
            event.type,
            value
        );
        if (buildingType.empty()) {
            return false;
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && object.kind == WorldObjectKind::Structure
                && runtimeCampaignHouseMatches(
                    state,
                    runtimeCampaignTriggerHouse(state, trigger),
                    object.owner
                )
                && toLowerAsciiLocal(object.type)
                    == toLowerAsciiLocal(buildingType)) {
                return true;
            }
        }
        return false;
    }
    case 33:
        for (int objectId : state.selectedObjectIds) {
            if (state.triggers.campaignPreviousSelectedObjects.find(objectId)
                    != state.triggers.campaignPreviousSelectedObjects.end()) {
                continue;
            }
            const WorldObjectInstance* selected = findObjectById(state, objectId);
            if (selected != nullptr
                && runtimeCampaignObjectAttachedToTrigger(trigger, *selected)) {
                return true;
            }
        }
        return false;
    case 36:
    case 37: {
        const int variable = parseRuntimeIntOrDefault(argument(2), 0);
        const auto found = state.triggers.campaignLocalVariables.find(variable);
        const int current = found == state.triggers.campaignLocalVariables.end()
            ? 0
            : found->second;
        return event.type == 36 ? current != 0 : current == 0;
    }
    case 39:
    case 40:
    case 41:
    case 43:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            const auto previous =
                state.triggers.campaignPreviousHealthByObject.find(
                    object.instanceId
                );
            if (previous == state.triggers.campaignPreviousHealthByObject.end()) {
                continue;
            }
            const bool combatOnly = event.type == 39 || event.type == 40;
            if (combatOnly) {
                const auto attacked =
                    state.triggers.campaignAttackedTickByObject.find(
                        object.instanceId
                    );
                const auto attacker =
                    state.triggers.campaignAttackerHouseByObject.find(
                        object.instanceId
                    );
                // The threshold crossing and its combat hit must belong to
                // the same simulation tick.  A stale earlier attack must not
                // turn later script/environmental damage into combat damage.
                if (attacked
                        == state.triggers.campaignAttackedTickByObject.end()
                    || attacker
                        == state.triggers.campaignAttackerHouseByObject.end()
                    || attacked->second != state.tick
                    || attacked->second < trigger.activationTick
                    || attacked->second <= trigger.lastFiredTick) {
                    continue;
                }
            }
            // Authored RA2/YR map health is a 0..256 ratio, while objects
            // created during play use their rules Strength directly.  Do not
            // compare a map object's 256 against (for example) a 1000-point
            // building Strength or health events can never cross their
            // authored threshold.
            int maximum = !object.mapId.empty()
                ? 256
                : std::max(1, runtimeMaxHealthForObject(state, object));
            if (const auto initial =
                    state.triggers.campaignInitialHealthByObject.find(
                        object.instanceId
                    );
                initial
                    != state.triggers.campaignInitialHealthByObject.end()) {
                maximum = std::max(maximum, initial->second);
            }
            if (event.type == 41
                && previous->second >= maximum
                && object.health < maximum) {
                return true;
            }
            if (event.type == 39
                && previous->second > maximum / 2
                && object.health <= maximum / 2) {
                return true;
            }
            if ((event.type == 40 || event.type == 43)
                && previous->second > maximum / 4
                && object.health <= maximum / 4) {
                return true;
            }
        }
        return false;
    case 44:
        return runtimeCampaignTaggedObjectWasAttacked(
            state,
            trigger,
            &event
        );
    case 48:
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            const auto previousAlive =
                state.triggers.campaignPreviousAliveByObject.find(
                    object.instanceId
                );
            const auto previousOwner =
                state.triggers.campaignPreviousOwnerByObject.find(
                    object.instanceId
                );
            const bool destroyed = !object.alive
                && previousAlive
                    != state.triggers.campaignPreviousAliveByObject.end()
                && previousAlive->second;
            const bool captured = previousOwner
                    != state.triggers.campaignPreviousOwnerByObject.end()
                && !runtimeCampaignHouseMatches(
                    state,
                    previousOwner->second,
                    object.owner
                );
            const auto entered =
                state.triggers.campaignEnteredTickByTargetObject.find(
                    object.instanceId
                );
            const bool infiltrated = entered
                    != state.triggers.campaignEnteredTickByTargetObject.end()
                && entered->second >= trigger.activationTick
                && entered->second > trigger.lastFiredTick;
            if (destroyed || captured || infiltrated) {
                return true;
            }
        }
        return false;
    case 52:
        return runtimeCampaignHouseIsPlayer(
                state,
                runtimeCampaignTriggerHouse(state, trigger)
            )
            && state.economy.credits < std::max(0, value);
    case 55:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            std::nullopt,
            true,
            false
        ) == 0;
    case 56:
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            std::nullopt,
            false,
            true
        ) == 0;
    case 57: {
        const std::string buildingType = runtimeCampaignIndexedTypeId(
            state,
            event.type,
            value
        );
        if (buildingType.empty()) {
            return false;
        }
        return runtimeCampaignObjectCount(
            state,
            runtimeCampaignTriggerHouse(state, trigger),
            WorldObjectKind::Structure,
            false,
            false,
            buildingType
        ) == 0;
    }
    case 58:
        return runtimeCampaignPowerNetForHouse(
            state,
            runtimeCampaignEventHouse(state, trigger, event)
        ) >= 0;
    case 59:
        return runtimeCampaignAttachedObjectEntered(state, trigger, event)
            || runtimeCampaignCellEntered(
                state,
                trigger,
                event,
                true
            );
    case 60:
    case 61: {
        const std::string techType = argument(3);
        if (techType.empty()) {
            state.triggers.status =
                "campaign event " + std::to_string(event.type)
                + " missing tech type on " + trigger.id;
            return false;
        }
        const int present = runtimeCampaignObjectCount(
            state,
            runtimeCampaignEventHouse(state, trigger, event),
            std::nullopt,
            false,
            false,
            techType
        );
        return event.type == 60 ? present > 0 : present == 0;
    }
    default:
        ++state.triggers.unsupportedEvents;
        state.triggers.status =
            "unsupported campaign event "
            + std::to_string(event.type)
            + " on " + trigger.id;
        return false;
    }
}

std::string runtimeCampaignActionHouse(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action) {
    if (action.arguments.size() > 2) {
        const std::string resolved = runtimeCampaignHouseName(
            state,
            action.arguments[2]
        );
        if (!resolved.empty()) {
            return resolved;
        }
    }
    if (!action.targetId.empty()
        && !runtimeIsCreateTeamAction(action.type)
        && action.type != 53
        && action.type != 54) {
        const std::string resolved = runtimeCampaignHouseName(
            state,
            action.targetId
        );
        if (!resolved.empty()) {
            return resolved;
        }
    }
    return runtimeCampaignHouseName(state, trigger.house);
}

std::string runtimeCampaignActionArgument(
    const RuntimeTriggerAction& action,
    std::size_t index) {
    return index < action.arguments.size()
        ? trimAsciiLocal(action.arguments[index])
        : std::string{};
}

int runtimeCampaignActionInteger(
    const RuntimeTriggerAction& action,
    std::size_t index,
    int fallback) {
    return parseRuntimeIntOrDefault(
        runtimeCampaignActionArgument(action, index),
        fallback
    );
}

std::string runtimeCampaignWaypointId(
    const RuntimeTriggerAction& action,
    std::size_t index) {
    const std::string raw = toUpperAsciiLocal(
        runtimeCampaignActionArgument(action, index)
    );
    if (raw.empty()) {
        return {};
    }
    int numeric = 0;
    if (parseRuntimeInt(raw, numeric)) {
        return std::to_string(numeric);
    }
    int encoded = 0;
    for (char ch : raw) {
        if (ch < 'A' || ch > 'Z') {
            return raw;
        }
        encoded = encoded * 26 + (ch - 'A' + 1);
    }
    return std::to_string(std::max(0, encoded - 1));
}

std::string runtimeCampaignActionSuperWeaponId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action) {
    const std::string raw = runtimeCampaignActionArgument(action, 2);
    if (!raw.empty() && state.rules.findSuperWeaponType(raw) != nullptr) {
        return state.rules.findSuperWeaponType(raw)->id;
    }
    int index = -1;
    if (!parseRuntimeInt(raw, index) || index < 0) {
        return {};
    }
    const auto& superWeapons = state.rules.getSuperWeaponTypes();
    return static_cast<std::size_t>(index) < superWeapons.size()
        ? superWeapons[static_cast<std::size_t>(index)].id
        : std::string{};
}

std::string runtimeCampaignRulesListId(
    const RuntimeGameSessionState& state,
    const std::string& sectionName,
    const std::string& rawValue) {
    const std::string raw = trimAsciiLocal(rawValue);
    int index = -1;
    if (!parseRuntimeInt(raw, index)) {
        return raw;
    }
    if (index < 0) {
        return {};
    }

    IniFile rulesIni;
    if (!loadRuntimeIniSource(
            state.config,
            RuntimeCampaignIniKind::Rules,
            rulesIni
        )) {
        return {};
    }
    const IniFile::Section* section = rulesIni.getSection(sectionName);
    if (section == nullptr
        || static_cast<std::size_t>(index) >= section->size()) {
        return {};
    }
    return trimAsciiLocal((*section)[static_cast<std::size_t>(index)].second);
}

std::string runtimeCampaignActionWarheadId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action) {
    const std::string raw = runtimeCampaignActionArgument(action, 2);
    if (!raw.empty() && state.rules.findWarheadType(raw) != nullptr) {
        return state.rules.findWarheadType(raw)->id;
    }
    int index = -1;
    if (!parseRuntimeInt(raw, index) || index < 0) {
        return {};
    }
    const auto& warheads = state.rules.getWarheadTypes();
    return static_cast<std::size_t>(index) < warheads.size()
        ? warheads[static_cast<std::size_t>(index)].id
        : std::string{};
}

void spawnRuntimeCampaignFlash(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    int durationTicks) {
    RuntimeImpactEffect flash;
    flash.targetObjectId = object.instanceId;
    flash.owner = object.owner;
    flash.cell = runtimeCellPointForObject(object);
    flash.level = object.level;
    flash.totalTicks = std::max(1, durationTicks);
    flash.remainingTicks = flash.totalTicks;
    flash.radius = object.kind == WorldObjectKind::Structure ? 20 : 12;
    flash.lifecycleState = "CampaignFlash";
    flash.renderIntent = "campaignFlash";
    flash.bright = true;
    state.impactEffects.push_back(std::move(flash));
}

std::string runtimeCampaignParticleSystemId(
    const RuntimeGameSessionState& state,
    const RuntimeTriggerAction& action) {
    const std::string raw = runtimeCampaignActionArgument(action, 2);
    if (const ParticleSystemType* exact =
            state.rules.findParticleSystemType(raw)) {
        return exact->id;
    }
    int index = -1;
    if (!parseRuntimeInt(raw, index) || index < 0) {
        return {};
    }
    const auto& systems = state.rules.getParticleSystemTypes();
    return static_cast<std::size_t>(index) < systems.size()
        ? systems[static_cast<std::size_t>(index)].id
        : std::string{};
}

const SuperWeaponRule* runtimeCampaignLightningRule(
    const RuntimeGameSessionState& state) {
    for (const SuperWeaponRule& rule : state.rules.getSuperWeaponTypes()) {
        const std::string key = toLowerAsciiLocal(
            rule.type + " " + rule.action
        );
        if (key.find("lightning") != std::string::npos) {
            return &rule;
        }
    }
    return nullptr;
}

int runtimeCampaignIonStrikeDelayTicks(const RuntimeGameSessionState& state) {
    const SuperWeaponRule* lightning = runtimeCampaignLightningRule(state);
    const int authoredFrames = lightning != nullptr
            && lightning->hitDelayFrames > 0
        ? lightning->hitDelayFrames
        : 60;
    return runtimeAuthoredFramesToFixedTicks(state, authoredFrames);
}

bool fireRuntimeCampaignIonStrike(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell,
    const std::string& owner) {
    const SuperWeaponRule* lightning = runtimeCampaignLightningRule(state);
    const std::string warheadId = lightning == nullptr
        ? std::string{}
        : trimAsciiLocal(lightning->payloadWarhead);
    const WarheadType* warhead = state.rules.findWarheadType(warheadId);
    std::string animationId;
    if (lightning != nullptr) {
        const std::vector<std::string> bolts =
            splitRuntimeTokenList(lightning->boltAnim);
        if (!bolts.empty()) {
            animationId = trimAsciiLocal(bolts.front());
        }
    }
    const int level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
    if (warhead == nullptr) {
        spawnRuntimeImpact(
            state,
            cell,
            level,
            false,
            animationId,
            warheadId,
            lightning == nullptr ? 0 : std::max(0, lightning->damage),
            lightning == nullptr
                ? 0.0
                : static_cast<double>(std::max(0, lightning->cellSpread)),
            true
        );
        return true;
    }

    RuntimeProjectileEffect strike;
    strike.actorId = nextRuntimeProjectileActorId(state);
    strike.owner = owner;
    strike.fromCell = cell;
    strike.toCell = cell;
    strike.currentCell = cell;
    strike.hasSourcePosition = true;
    strike.sourceX = static_cast<double>(cell.x);
    strike.sourceY = static_cast<double>(cell.y);
    strike.sourceZ = static_cast<double>(level + 12);
    strike.hasTargetPosition = true;
    strike.targetX = static_cast<double>(cell.x);
    strike.targetY = static_cast<double>(cell.y);
    strike.targetZ = static_cast<double>(level);
    strike.fromLevel = level + 12;
    strike.toLevel = level;
    strike.currentLevel = level;
    strike.spawnTick = state.tick;
    strike.impactTick = state.tick;
    strike.damage = lightning == nullptr
        ? 0
        : std::max(0, lightning->damage);
    strike.totalTicks = 1;
    strike.remainingTicks = 1;
    strike.lifecycle = RuntimeProjectileLifecycleState::Impacting;
    strike.lifecycleState = "IonLightningStrike";
    strike.renderIntent = "campaignIonLightning";
    strike.warheadId = warhead->id;
    strike.impactAnimId = animationId;
    strike.bright = true;
    strike.electricBolt = true;
    strike.aa = true;
    strike.ag = true;
    strike.affectsAllies = true;
    strike.affectsEnemies = true;
    strike.cellSpread = lightning == nullptr
        ? std::max(0.0, warhead->cellSpread)
        : std::max(
            warhead->cellSpread,
            static_cast<double>(std::max(0, lightning->cellSpread))
        );
    strike.percentAtMax = warhead->percentAtMax;
    (void)applyRuntimeProjectileImpact(state, strike);
    state.repaintRequested = true;
    return true;
}

void destroyRuntimeCampaignObject(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const std::string& destroyerHouse,
    const std::string& reason,
    RuntimeObjectExitKind exitKind,
    bool suppressDeathWeapon) {
    if (!object.alive) {
        return;
    }
    RuntimeObjectExitRequest objectExit;
    objectExit.objectId = object.instanceId;
    objectExit.kind = exitKind;
    objectExit.tick = state.tick;
    objectExit.mission = exitKind == RuntimeObjectExitKind::Sold
        ? "Sold"
        : exitKind == RuntimeObjectExitKind::ScriptRemoved
            ? "DeletedByScript"
            : "Destroyed";
    objectExit.reason = reason;
    objectExit.sourceOwner = destroyerHouse;
    objectExit.suppressDeathWeapon = suppressDeathWeapon;
    if (queueRuntimeObjectExit(state, std::move(objectExit))
            == RuntimeObjectExitQueueResult::Queued) {
        (void)drainRuntimeObjectExitQueue(state);
    }
}

void destroyRuntimeCampaignObjects(
    RuntimeProductionHostState& state,
    const std::string& house,
    const std::string& destroyerHouse,
    std::optional<WorldObjectKind> kind,
    bool navalOnly,
    bool landOnly) {
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || !runtimeCampaignHouseMatches(state, house, object.owner)
            || (kind.has_value() && object.kind != *kind)) {
            continue;
        }
        if (navalOnly || landOnly) {
            bool naval = false;
            if (object.kind != WorldObjectKind::Structure) {
                const UnitType* unit =
                    findRuntimeUnitTypeById(state.rules, object.type);
                naval = unit != nullptr && runtimeUnitTypeLooksNaval(*unit);
            }
            if (navalOnly != naval) {
                continue;
            }
        }
        destroyRuntimeCampaignObject(
            state,
            object,
            destroyerHouse,
            "campaign house action"
        );
    }
    state.navigationRefreshPending = true;
    state.powerRefreshPending = true;
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
}

bool applyRuntimeCampaignAction(
    RuntimeProductionHostState& state,
    RuntimeTriggerDefinition& trigger,
    const RuntimeTriggerAction& action) {
    RuntimePerfTraceScope originalMapCampaignLeafTrace("applyRuntimeCampaignAction", state.tick);
    const int type = action.type;
    if (type == 97) {
        state.triggers.trainCrate = !state.triggers.trainCrate;
        state.triggers.status = "campaign train crate toggle";
        return true;
    }
    if (type == 0) {
        return true;
    }

    if (runtimeIsSuperWeaponTriggerAction(type)) {
        if (fireRuntimeTriggerSuperWeapon(state, trigger, action)) {
            state.triggers.status =
                "campaign superweapon action " + std::to_string(type)
                + " from " + trigger.id;
            return true;
        }
        ++state.triggers.unsupportedActions;
        state.triggers.status =
            "campaign superweapon action could not resolve "
            + std::to_string(type) + " on " + trigger.id;
        return false;
    }

    if (type == 1 || type == 2) {
        const std::string targetHouse = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        if (!runtimeCampaignHouseIsPlayer(state, targetHouse)) {
            state.triggers.status =
                "campaign action " + std::to_string(type)
                + " targets " + targetHouse
                + "; player result unchanged";
            return true;
        }
        finishRuntimeCampaignMission(state, type == 1, trigger.id);
        return true;
    }

    if (type == 67 || type == 68) {
        finishRuntimeCampaignMission(state, type == 67, trigger.id);
        return true;
    }

    if (type == 4 || type == 7 || type == 80 || type == 107) {
        if (fireRuntimeCreateTeamTrigger(
                state,
                trigger,
                action,
                false,
                false
            )) {
            return true;
        }
        ++state.triggers.unsupportedActions;
        state.triggers.status =
            "campaign team action " + std::to_string(type)
            + " could not resolve TeamType " + action.targetId;
        return false;
    }

    if (type == 3) {
        const std::string owner = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        const std::string ownerKey = toLowerAsciiLocal(trimAsciiLocal(owner));
        const auto house =
            state.triggers.campaignHouseProduction.find(ownerKey);
        if (house == state.triggers.campaignHouseProduction.end()) {
            state.triggers.status =
                "campaign production house unresolved: " + owner;
            return false;
        }
        house->second.productionStarted = true;
        state.triggers.status =
            "campaign production enabled for "
            + house->second.owner;
        return true;
    }

    if (type == 5) {
        const std::string teamKey = toLowerAsciiLocal(
            trimAsciiLocal(action.targetId)
        );
        const auto found = state.triggers.campaignTeamObjects.find(teamKey);
        if (found != state.triggers.campaignTeamObjects.end()) {
            const std::string destroyer =
                runtimeCampaignTriggerHouse(state, trigger);
            for (int objectId : found->second) {
                WorldObjectInstance* object = findMutableObjectById(
                    state,
                    objectId
                );
                if (object != nullptr && object->alive) {
                    destroyRuntimeCampaignObject(
                        state,
                        *object,
                        destroyer,
                        "campaign destroy team"
                    );
                }
            }
            state.triggers.campaignTeamObjects.erase(found);
        }
        std::vector<int> destroyedMissionIds;
        for (const auto& [missionId, mission] :
                state.triggers.teamMissions) {
            if (toLowerAsciiLocal(trimAsciiLocal(mission.teamTypeId))
                == teamKey) {
                destroyedMissionIds.push_back(missionId);
            }
        }
        for (int missionId : destroyedMissionIds) {
            state.triggers.teamMissions.erase(missionId);
        }
        state.navigationRefreshPending = true;
        state.powerRefreshPending = true;
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        std::erase_if(state.triggers.reinforcements.pending, [&](const auto& entry) {
            return toLowerAsciiLocal(trimAsciiLocal(entry.second.teamTypeId)) == teamKey;
        });
        state.triggers.status = "campaign destroyed team " + action.targetId;
        return true;
    }

    if (type == 6) {
        RuntimeTeamType huntTeam;
        huntTeam.house = runtimeCampaignActionHouse(state, trigger, action);
        std::vector<int> hunters;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && object.kind != WorldObjectKind::Structure
                && runtimeCampaignHouseMatches(
                    state,
                    huntTeam.house,
                    object.owner
                )) {
                hunters.push_back(object.instanceId);
            }
        }
        issueRuntimeTriggerTeamOrder(state, huntTeam, hunters);
        state.triggers.status =
            "campaign all hunt " + huntTeam.house
            + " units=" + std::to_string(hunters.size());
        return true;
    }

    if (type == 8) {
        if (const std::optional<RuntimeCellPoint> waypoint =
                runtimeWaypointCell(
                    state,
                    runtimeCampaignWaypointId(action, 7)
                )) {
            state.vision.revealArea(
                state.economy.owner,
                waypoint->x,
                waypoint->y,
                8
            );
            state.triggers.status = "campaign drop-zone reveal";
            return true;
        }
        return false;
    }

    if (type == 9) {
        const std::string house = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        destroyRuntimeCampaignObjects(
            state,
            house,
            runtimeCampaignTriggerHouse(state, trigger),
            WorldObjectKind::Structure,
            false,
            false
        );
        state.triggers.status = "campaign fire sale " + house;
        return true;
    }

    if (type == 11) {
        const std::string message = runtimeCampaignLocalizedText(
            state,
            runtimeCampaignActionArgument(action, 2)
        );
        state.interactionStatus = message.empty()
            ? "campaign objective updated"
            : message;
        state.economy.status = state.interactionStatus;
        state.triggers.status = "campaign text " + state.interactionStatus;
        return true;
    }

    if (type == 12) {
        RuntimeTriggerDefinition* target = runtimeCampaignTriggerById(
            state.triggers,
            action.targetId
        );
        if (target == nullptr) {
            return false;
        }
        target->enabled = false;
        target->fired = true;
        state.triggers.status = "campaign destroyed trigger " + target->id;
        return true;
    }

    if (type == 14) {
        const std::string newOwner = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        std::vector<RuntimeOwnerTransitionRoot> roots;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || !runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            if (object.owner != newOwner) {
                roots.push_back({
                    object.instanceId,
                    object.owner,
                    newOwner
                });
            }
        }
        const RuntimeOwnerTransitionBatchResult transition =
            applyRuntimeOwnerTransitionBatch(
                state,
                roots,
                RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,
                "campaignOwnerChanged"
            );
        if (!transition.applied) {
            state.triggers.status =
                "campaign owner transition rejected: " + transition.error;
            return false;
        }
        state.navigationRefreshPending = true;
        state.powerRefreshPending = true;
        invalidateRuntimeObjectProjectionCache(state);
        state.triggers.status =
            "campaign changed attached house to " + newOwner
            + " objects="
            + std::to_string(transition.changedObjectIds.size());
        return true;
    }

    if (type == 16) {
        if (state.cooperativeSession.active()) {
            for (const auto& slot : state.cooperativeSession.slots())
                if (slot.coalitionId == 0) state.vision.revealMap(slot.owner);
        } else state.vision.revealMap(state.economy.owner);
        state.repaintRequested = true;
        state.triggers.status = "campaign revealed map";
        return true;
    }

    if (type == 17 || type == 18) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 2)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        if (state.cooperativeSession.active()) {
            for (const auto& slot : state.cooperativeSession.slots())
                if (slot.coalitionId == 0) state.vision.revealArea(slot.owner,
                    waypoint->x, waypoint->y, type == 17 ? 10 : 18);
        } else state.vision.revealArea(state.economy.owner,
            waypoint->x, waypoint->y, type == 17 ? 10 : 18);
        state.repaintRequested = true;
        state.triggers.status =
            std::string(type == 17
                ? "campaign revealed waypoint "
                : "campaign revealed waypoint zone ")
            + runtimeCampaignWaypointId(action, 2);
        return true;
    }

    if (type == 22) {
        RuntimeTriggerDefinition* target = runtimeCampaignTriggerById(
            state.triggers,
            action.targetId
        );
        if (target == nullptr
            || target == &trigger
            || state.triggers.campaignForceDepth >= 8) {
            return false;
        }
        ++state.triggers.campaignForceDepth;
        bool applied = false;
        for (const RuntimeTriggerAction& forcedAction : target->actions) {
            applied = applyRuntimeCampaignAction(
                state,
                *target,
                forcedAction
            ) || applied;
        }
        --state.triggers.campaignForceDepth;
        state.triggers.status = "campaign forced trigger " + target->id;
        return applied;
    }

    if (type >= 23 && type <= 27) {
        const int fixedTickHz = std::max(1, state.config.fixedTickHz);
        const int deltaTicks = std::max(
            0,
            runtimeCampaignActionInteger(action, 2, 0)
        ) * fixedTickHz;
        if (type == 23) {
            state.triggers.campaignMissionTimerStarted = true;
            state.triggers.campaignMissionTimerRunning = true;
        } else if (type == 24) {
            state.triggers.campaignMissionTimerRunning = false;
        } else if (type == 25) {
            state.triggers.campaignMissionTimerRemainingTicks += deltaTicks;
        } else if (type == 26) {
            state.triggers.campaignMissionTimerRemainingTicks = std::max(
                0,
                state.triggers.campaignMissionTimerRemainingTicks - deltaTicks
            );
        } else {
            state.triggers.campaignMissionTimerStarted = true;
            state.triggers.campaignMissionTimerRemainingTicks = deltaTicks;
        }
        state.triggers.status =
            "campaign mission timer ticks="
            + std::to_string(
                state.triggers.campaignMissionTimerRemainingTicks
            )
            + (state.triggers.campaignMissionTimerRunning
                ? " running"
                : " stopped");
        return true;
    }

    if (type == 28 || type == 29) {
        const int variable = runtimeCampaignActionInteger(action, 2, 0);
        if (type == 28) {
            state.triggers.campaignGlobalVariables[variable] = 1;
        } else {
            state.triggers.campaignGlobalVariables.erase(variable);
        }
        state.triggers.status =
            std::string(type == 28 ? "set " : "clear ")
            + "campaign global variable " + std::to_string(variable);
        return true;
    }

    if (type == 32) {
        const std::string destroyer = runtimeCampaignTriggerHouse(
            state,
            trigger
        );
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (object.alive
                && runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                destroyRuntimeCampaignObject(
                    state,
                    object,
                    destroyer,
                    "campaign destroy attached"
                );
            }
        }
        state.navigationRefreshPending = true;
        state.powerRefreshPending = true;
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.triggers.status = "campaign destroyed attached objects";
        return true;
    }

    if (type == 46 || type == 47) {
        state.triggers.campaignInputLocked = type == 46;
        state.leftPointer.active = false;
        state.rightPointer.active = false;
        state.triggers.status = type == 46
            ? "campaign input locked"
            : "campaign input unlocked";
        return true;
    }

    if (type == 48 || type == 112) {
        const std::string waypointId = type == 48
            ? runtimeCampaignWaypointId(action, 7)
            : runtimeCampaignWaypointId(action, 2);
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            waypointId
        );
        if (!waypoint.has_value()) {
            return false;
        }
        RuntimeInputPixelRect clientRect = state.cameraClampViewport;
        if (clientRect.right <= clientRect.left
            || clientRect.bottom <= clientRect.top) {
            clientRect = {
                0,
                0,
                std::max(1, state.config.windowWidth),
                std::max(1, state.config.windowHeight)
            };
        }
        centerRuntimeCameraOnCellInPixels(state, clientRect, *waypoint);
        invalidateRuntimeObjectProjectionCache(state);
        state.repaintRequested = true;
        state.triggers.status = "campaign camera waypoint " + waypointId;
        return true;
    }

    if (type == 51) {
        state.vision.resetShroud(state.economy.owner);
        state.repaintRequested = true;
        state.triggers.status = "campaign map reshrouded";
        return true;
    }

    if (type == 53 || type == 54) {
        RuntimeTriggerDefinition* target = runtimeCampaignTriggerById(
            state.triggers,
            action.targetId
        );
        if (target == nullptr) {
            ++state.triggers.unsupportedActions;
            state.triggers.status =
                "campaign trigger action " + std::to_string(type)
                + " target missing " + action.targetId;
            return false;
        }
        target->enabled = type == 53;
        target->activationTick = state.tick;
        target->nextEligibleTick = state.tick;
        state.triggers.status =
            std::string(type == 53 ? "enabled " : "disabled ")
            + target->id + " from " + trigger.id;
        return true;
    }

    if (type == 56 || type == 57) {
        const int variable = action.arguments.size() > 2
            ? parseRuntimeIntOrDefault(action.arguments[2], 0)
            : 0;
        if (type == 56) {
            state.triggers.campaignLocalVariables[variable] = 1;
        } else {
            state.triggers.campaignLocalVariables.erase(variable);
        }
        state.triggers.status =
            std::string(type == 56 ? "set " : "clear ")
            + "campaign local variable " + std::to_string(variable);
        return true;
    }

    if (type == 60 || type == 61 || type == 62) {
        int affected = 0;
        const std::string destroyer = runtimeCampaignTriggerHouse(
            state,
            trigger
        );
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (!object.alive
                || object.kind != WorldObjectKind::Structure
                || !runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            if (type == 60) {
                if (const BuildingType* building =
                        findRuntimeBuildingTypeById(state.rules, object.type)) {
                    if (runtimeCampaignHouseIsPlayer(state, object.owner)) {
                        depositRuntimeCredits(state, runtimeBuildingSaleRefund(
                            building, state.rules.getRefundPercent()));
                    }
                }
                destroyRuntimeCampaignObject(
                    state,
                    object,
                    destroyer,
                    "campaign sell attached",
                    RuntimeObjectExitKind::Sold,
                    true
                );
            } else {
                object.poweredOperational = type == 62;
                if (object.poweredOperational) {
                    if (object.powerAnimationPausedAtTick >= 0) {
                        object.powerAnimationPausedTicks += std::max(
                            0,
                            state.tick - object.powerAnimationPausedAtTick
                        );
                    }
                    object.powerAnimationPausedAtTick = -1;
                } else if (object.powerAnimationPausedAtTick < 0) {
                    object.powerAnimationPausedAtTick = state.tick;
                }
            }
            ++affected;
        }
        state.powerRefreshPending = true;
        state.navigationRefreshPending = type == 60;
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.triggers.status =
            "campaign building action " + std::to_string(type)
            + " affected=" + std::to_string(affected);
        return true;
    }

    if (type == 63) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 2)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        const std::string attacker = runtimeCampaignTriggerHouse(
            state,
            trigger
        );
        int damaged = 0;
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (!object.alive || runtimeCarrierAircraftHasDeckProtection(state, object)
                || runtimeCellDistanceToObjectFootprint(
                    state,
                    *waypoint,
                    object
                ) > 0.001) {
                continue;
            }
            state.triggers.campaignAttackedTickByObject[object.instanceId] =
                state.tick;
            state.triggers.campaignAttackerHouseByObject[object.instanceId] =
                attacker;
            const int healthBeforeDamage = object.health;
            object.health = std::max(0, object.health - 100);
            if (object.health < healthBeforeDamage) {
                runtimeRevealObjectAfterDamage(
                    state,
                    object,
                    "campaignWaypointDamage"
                );
            }
            if (object.health <= 0) {
                destroyRuntimeCampaignObject(
                    state,
                    object,
                    attacker,
                    "campaign waypoint damage"
                );
            }
            ++damaged;
        }
        state.navigationRefreshPending = true;
        state.powerRefreshPending = true;
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.triggers.status =
            "campaign waypoint damage objects=" + std::to_string(damaged);
        return true;
    }

    if (type == 36 || type == 37 || type == 38) {
        const std::string left = runtimeCampaignHouseName(state, trigger.house);
        const std::string right = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        const std::string leftKey = toLowerAsciiLocal(trimAsciiLocal(left));
        const std::string rightKey = toLowerAsciiLocal(trimAsciiLocal(right));
        if (type == 36) {
            std::vector<RuntimeOwnerTransitionRoot> roots;
            for (const WorldObjectInstance& object : state.world.getObjects()) {
                if (object.alive
                    && runtimeCampaignHouseMatches(
                        state,
                        left,
                        object.owner
                    )
                    && object.owner != right) {
                    roots.push_back({
                        object.instanceId,
                        object.owner,
                        right
                    });
                }
            }
            const RuntimeOwnerTransitionBatchResult transition =
                applyRuntimeOwnerTransitionBatch(
                    state,
                    roots,
                    RuntimeOwnerTransitionRepairPolicy::ReleaseRelations,
                    "campaignHouseOwnerChanged"
                );
            if (!transition.applied) {
                state.triggers.status =
                    "campaign house transition rejected: "
                    + transition.error;
                return false;
            }
            state.powerRefreshPending = true;
            state.triggers.status =
                "campaign ownership changed " + left + " -> " + right
                + " objects="
                + std::to_string(transition.changedObjectIds.size());
        } else {
            std::string pair = leftKey < rightKey
                ? leftKey + "|" + rightKey
                : rightKey + "|" + leftKey;
            if (type == 37) {
                state.triggers.campaignAlliedPairs.insert(pair);
            } else {
                state.triggers.campaignAlliedPairs.erase(pair);
            }
            state.triggers.status =
                std::string(type == 37 ? "campaign alliance " : "campaign enemy ")
                + left + " / " + right;
        }
        return true;
    }

    if (type == 35) {
        const std::string owner = runtimeCampaignTriggerHouse(state, trigger);
        const auto house = state.triggers.campaignHouseProduction.find(
            toLowerAsciiLocal(trimAsciiLocal(owner))
        );
        if (house == state.triggers.campaignHouseProduction.end()) {
            state.triggers.status =
                "campaign preferred-target house unresolved: " + owner;
            return false;
        }
        house->second.preferredTargetQuarry = std::clamp(
            runtimeCampaignActionInteger(action, 2, 0),
            0,
            9
        );
        state.triggers.status =
            "campaign preferred-target quarry "
            + std::to_string(house->second.preferredTargetQuarry)
            + " for " + house->second.owner;
        return true;
    }

    if (type == 44 || type == 45) {
        if (type == 45) {
            state.triggers.campaignIonStormUntilTick = 0;
            state.triggers.campaignNextIonStormStrikeTick = 0;
            state.triggers.status = "campaign ion storm stopped";
        } else {
            const int durationTicks = runtimeAuthoredFramesToFixedTicks(
                state,
                std::max(1, runtimeCampaignActionInteger(action, 2, 1))
            );
            state.triggers.campaignIonStormUntilTick = std::max(
                state.triggers.campaignIonStormUntilTick,
                state.tick + durationTicks
            );
            if (state.triggers.campaignNextIonStormStrikeTick <= state.tick) {
                state.triggers.campaignNextIonStormStrikeTick = state.tick
                    + runtimeCampaignIonStrikeDelayTicks(state);
            }
            state.triggers.status =
                "campaign ion storm until tick "
                + std::to_string(
                    state.triggers.campaignIonStormUntilTick
                );
        }
        state.repaintRequested = true;
        return true;
    }

    if (type >= 76 && type <= 79) {
        const std::string owner = runtimeCampaignTriggerHouse(state, trigger);
        const auto house = state.triggers.campaignHouseProduction.find(
            toLowerAsciiLocal(trimAsciiLocal(owner))
        );
        if (house == state.triggers.campaignHouseProduction.end()) {
            state.triggers.status =
                "campaign team-ratio house unresolved: " + owner;
            return false;
        }
        const int ratio = std::clamp(
            runtimeCampaignActionInteger(action, 2, 100),
            0,
            100
        );
        if (type == 76) {
            house->second.aiTriggerTeamRatio = ratio;
        } else if (type == 77) {
            house->second.teamAircraftRatio = ratio;
        } else if (type == 78) {
            house->second.teamInfantryRatio = ratio;
        } else {
            house->second.teamUnitRatio = ratio;
        }
        state.triggers.status =
            "campaign team allocation ratio action "
            + std::to_string(type) + "=" + std::to_string(ratio)
            + " for " + house->second.owner;
        return true;
    }

    if (type == 88) {
        const std::string waypointId = runtimeCampaignWaypointId(action, 7);
        const std::optional<RuntimeCellPoint> waypoint =
            runtimeWaypointCell(state, waypointId);
        const std::string particleSystemId =
            runtimeCampaignParticleSystemId(state, action);
        if (!waypoint.has_value() || particleSystemId.empty()) {
            return false;
        }
        ensureRuntimeParticlesInitialized(state);
        RuntimeParticleSpawnContext context;
        context.owner = runtimeCampaignTriggerHouse(state, trigger);
        context.sourceTag = "campaign:particle-waypoint:" + waypointId;
        context.position = {
            static_cast<double>(waypoint->x) * 256.0,
            static_cast<double>(waypoint->y) * 256.0,
            static_cast<double>(terrainLevelAtRuntimeCell(
                state,
                waypoint->x,
                waypoint->y
            )) * 256.0
        };
        context.direction = {0.0, 0.0, 256.0};
        context.seed = runtimeStableHashString(context.sourceTag);
        runtimeStableHashMix(context.seed, state.tick);
        const std::uint64_t systemId = state.particles.spawnSystem(
            state.rules,
            particleSystemId,
            context
        );
        if (systemId == 0) {
            return false;
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign particle " + particleSystemId
            + " at " + waypointId;
        return true;
    }

    if (type == 89) {
        const std::string waypointId = runtimeCampaignWaypointId(action, 7);
        const std::optional<RuntimeCellPoint> waypoint =
            runtimeWaypointCell(state, waypointId);
        if (!waypoint.has_value()) {
            return false;
        }
        ensureRuntimeParticlesInitialized(state);
        std::vector<std::uint64_t> systemsToStop;
        for (const RuntimeParticleSystemInstance& system :
                state.particles.systems()) {
            const int systemX = static_cast<int>(std::lround(
                system.origin.x / 256.0
            ));
            const int systemY = static_cast<int>(std::lround(
                system.origin.y / 256.0
            ));
            if (systemX == waypoint->x && systemY == waypoint->y) {
                systemsToStop.push_back(system.id);
            }
        }
        for (std::uint64_t systemId : systemsToStop) {
            (void)state.particles.stopSystem(systemId);
        }
        state.repaintRequested = !systemsToStop.empty()
            || state.repaintRequested;
        state.triggers.status =
            "campaign removed particle systems="
            + std::to_string(systemsToStop.size())
            + " at " + waypointId;
        return true;
    }

    if (type == 90) {
        const std::string waypointId = runtimeCampaignWaypointId(action, 7);
        const std::optional<RuntimeCellPoint> waypoint =
            runtimeWaypointCell(state, waypointId);
        if (!waypoint.has_value()) {
            return false;
        }
        const bool applied = fireRuntimeCampaignIonStrike(
            state,
            *waypoint,
            runtimeCampaignTriggerHouse(state, trigger)
        );
        state.triggers.status =
            "campaign ion lightning at " + waypointId;
        return applied;
    }

    if (type == 40) {
        const int x = std::max(0, runtimeCampaignActionInteger(action, 3, 0));
        const int y = std::max(0, runtimeCampaignActionInteger(action, 4, 0));
        const int width = std::max(
            1,
            runtimeCampaignActionInteger(action, 5, 1)
        );
        const int height = std::max(
            1,
            runtimeCampaignActionInteger(action, 6, 1)
        );
        state.report.map.localSize =
            std::to_string(x) + "," + std::to_string(y) + ","
            + std::to_string(width) + "," + std::to_string(height);
        state.radarProjectionCacheValid = false;
        state.radarProjectionCacheKey.clear();
        state.radarTerrainKey.clear();
        if (state.cameraClampViewport.right > state.cameraClampViewport.left
            && state.cameraClampViewport.bottom
                > state.cameraClampViewport.top) {
            clampRuntimeMapCameraToPixels(state, state.cameraClampViewport);
        }
        invalidateRuntimeObjectProjectionCache(state);
        state.repaintRequested = true;
        state.triggers.status =
            "campaign view adjusted " + state.report.map.localSize;
        return true;
    }

    if (type == 41) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        const std::string animationId = runtimeCampaignRulesListId(
            state,
            "Animations",
            runtimeCampaignActionArgument(action, 2)
        );
        if (!waypoint.has_value() || animationId.empty()) {
            return false;
        }
        spawnRuntimeImpact(
            state,
            *waypoint,
            terrainLevelAtRuntimeCell(state, waypoint->x, waypoint->y),
            false,
            animationId,
            "",
            0,
            0.0,
            false
        );
        if (!state.impactEffects.empty()) {
            RuntimeImpactEffect& animation = state.impactEffects.back();
            int frames = std::max(8, state.config.fixedTickHz);
            if (const ArtType* art = state.art.findArtType(animationId)) {
                if (art->hasAnimationEnd) {
                    frames = std::max(
                        frames,
                        art->animationEnd
                            - (art->hasAnimationStart
                                ? art->animationStart
                                : 0)
                            + 1
                    );
                }
            }
            animation.totalTicks = std::clamp(frames, 8, 600);
            animation.remainingTicks = animation.totalTicks;
            animation.lifecycleState = "CampaignAnimation";
            animation.renderIntent = "campaignAnimation";
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign animation " + animationId
            + " at " + runtimeCampaignWaypointId(action, 7);
        return true;
    }

    if (type == 42) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        const std::string warheadId = runtimeCampaignActionWarheadId(
            state,
            action
        );
        const WarheadType* warhead = state.rules.findWarheadType(warheadId);
        if (!waypoint.has_value() || warhead == nullptr) {
            return false;
        }
        RuntimeProjectileEffect explosion;
        explosion.actorId = nextRuntimeProjectileActorId(state);
        explosion.owner = runtimeCampaignTriggerHouse(state, trigger);
        explosion.fromCell = *waypoint;
        explosion.toCell = *waypoint;
        explosion.currentCell = *waypoint;
        explosion.fromLevel = terrainLevelAtRuntimeCell(
            state,
            waypoint->x,
            waypoint->y
        );
        explosion.toLevel = explosion.fromLevel;
        explosion.currentLevel = explosion.fromLevel;
        explosion.spawnTick = state.tick;
        explosion.impactTick = state.tick;
        explosion.damage = 1000;
        explosion.totalTicks = 1;
        explosion.remainingTicks = 1;
        explosion.lifecycle = RuntimeProjectileLifecycleState::Impacting;
        explosion.lifecycleState = "Impacting";
        explosion.renderIntent = "campaignExplosion";
        explosion.warheadId = warhead->id;
        const std::vector<std::string> animations =
            splitRuntimeTokenList(warhead->animList);
        if (!animations.empty()) {
            explosion.impactAnimId = animations.front();
        }
        explosion.cellSpread = std::max(0.75, warhead->cellSpread);
        explosion.affectsAllies = true;
        explosion.affectsEnemies = true;
        explosion.aa = true;
        explosion.ag = true;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (object.alive
                && runtimeCellDistanceToObjectFootprint(
                    state,
                    *waypoint,
                    object
                ) <= 0.001) {
                explosion.targetObjectId = object.instanceId;
                break;
            }
        }
        const bool applied = applyRuntimeProjectileImpact(state, explosion);
        state.repaintRequested = true;
        state.triggers.status =
            "campaign explosion " + warhead->id
            + " at " + runtimeCampaignWaypointId(action, 7);
        return applied;
    }

    if (type == 55) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        RuntimeCampaignRadarEvent event;
        event.cell = *waypoint;
        event.eventType = runtimeCampaignActionInteger(action, 2, 0);
        event.untilTick = state.tick
            + std::max(1, state.config.fixedTickHz) * 8;
        state.triggers.campaignRadarEvents.push_back(event);
        state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{event.cell.x, event.cell.y};
        spawnRuntimeImpact(
            state,
            *waypoint,
            terrainLevelAtRuntimeCell(state, waypoint->x, waypoint->y),
            false,
            "",
            "",
            0,
            0.0,
            true
        );
        if (!state.impactEffects.empty()) {
            RuntimeImpactEffect& beacon = state.impactEffects.back();
            beacon.totalTicks = std::max(16, state.config.fixedTickHz * 2);
            beacon.remainingTicks = beacon.totalTicks;
            beacon.radius = 18;
            beacon.lifecycleState = "CampaignRadarEvent";
            beacon.renderIntent = "campaignRadarEvent";
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign radar event=" + std::to_string(event.eventType)
            + " at " + runtimeCampaignWaypointId(action, 7);
        return true;
    }

    if (type == 69) {
        finishRuntimeCampaignMission(
            state,
            state.skirmishVictory,
            trigger.id
        );
        return true;
    }

    if (type == 119 || type == 120 || type == 121 || type == 122) {
        const std::string targetHouse = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        const std::string destroyerHouse = runtimeCampaignTriggerHouse(
            state,
            trigger
        );
        if (type == 119) {
            destroyRuntimeCampaignObjects(
                state,
                targetHouse,
                destroyerHouse,
                std::nullopt,
                false,
                false
            );
        } else if (type == 120) {
            destroyRuntimeCampaignObjects(
                state,
                targetHouse,
                destroyerHouse,
                WorldObjectKind::Structure,
                false,
                false
            );
        } else if (type == 121) {
            destroyRuntimeCampaignObjects(
                state,
                targetHouse,
                destroyerHouse,
                std::nullopt,
                false,
                true
            );
        } else {
            destroyRuntimeCampaignObjects(
                state,
                targetHouse,
                destroyerHouse,
                std::nullopt,
                true,
                false
            );
        }
        state.triggers.status =
            "campaign destroy action " + std::to_string(type)
            + " house=" + targetHouse;
        return true;
    }

    if (type == 71 || type == 72 || type == 73) {
        ensureRuntimeLightingInitialized(state);
        const RuntimeLightingTriggerActionResult lightingResult =
            state.lighting.applyTriggerAction({
                type,
                static_cast<std::int32_t>(
                    runtimeCampaignActionInteger(action, 2, 0)
                )
            });
        if (!lightingResult.supported || !lightingResult.accepted) {
            ++state.triggers.unsupportedActions;
            state.triggers.status = "campaign lighting action rejected: "
                + lightingResult.reason;
            return false;
        }
        state.repaintRequested = true;
        state.triggers.status = "campaign lighting action "
            + std::to_string(type);
        return true;
    }

    if (type == 99) {
        if (action.authoredOrdinal < 0) {
            return false;
        }
        const std::string eventId = runtimeCampaignActionArgument(action, 2);
        const std::string waypointText = runtimeCampaignWaypointId(action, 7);
        int waypointId = -1;
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            waypointText
        );
        if (eventId.empty()
            || !parseRuntimeInt(waypointText, waypointId)
            || waypointId < 0
            || !waypoint.has_value()) {
            return false;
        }
        if (!state.worldAudioInitialized) {
            initializeRuntimeWorldAudio(state);
        }
        RuntimeWorldAudioPlayRequest request;
        request.eventId = eventId;
        request.waypointId = waypointId;
        request.cell = {
            waypoint->x,
            waypoint->y,
            terrainLevelAtRuntimeCell(state, waypoint->x, waypoint->y)
        };
        request.requestToken =
            "campaign-world-audio:"
            + toLowerAsciiLocal(trimAsciiLocal(state.config.defaultMapPath))
            + ":trigger:"
            + toLowerAsciiLocal(trimAsciiLocal(trigger.id))
            + ":action:"
            + std::to_string(action.authoredOrdinal)
            + ":type:99";
        const RuntimeWorldAudioCommandResult result = state.worldAudio.playAt(
            runtimeWorldAudioContext(state),
            request,
            state.tick
        );
        const bool handled = result.accepted
            || result.duplicate
            || containsAsciiCaseInsensitive(result.reason, "limit");
        if (!handled) {
            state.triggers.status = "campaign world audio rejected: "
                + result.reason;
            return false;
        }
        state.triggers.status = "campaign world audio " + eventId
            + " at waypoint " + waypointText;
        return true;
    }

    if (type == 20) {
        const std::string theme = action.arguments.size() > 2
            ? trimAsciiLocal(action.arguments[2])
            : action.targetId;
        std::string themeError;
        if (!playRuntimeMusicTheme(state, theme, &themeError)) {
            state.triggers.status = "campaign music theme missing " + theme
                + (themeError.empty() ? "" : ": " + themeError);
            return true;
        }
        state.triggers.status = "campaign music theme "
            + state.runtimeMusicThemeId;
        return true;
    }

    if (type == 19 || type == 21) {
        const std::string sound = action.arguments.size() > 2
            ? trimAsciiLocal(action.arguments[2])
            : action.targetId;
        emitRuntimeSound(
            state,
            std::string(type == 21 ? "speech:" : "sound:") + sound
        );
        state.triggers.status =
            "campaign audio action " + std::to_string(type)
            + " " + sound;
        return true;
    }

    if (type == 10 || type == 100 || type == 117) {
        const int movieIndex = runtimeCampaignActionInteger(action, 2, -1);
        const auto movie = state.triggers.campaignMoviePathByIndex.find(
            movieIndex
        );
        if (movie == state.triggers.campaignMoviePathByIndex.end()) {
            ++state.triggers.unsupportedActions;
            state.triggers.status =
                "campaign movie index missing "
                + std::to_string(movieIndex)
                + " for action " + std::to_string(type);
            return false;
        }
        state.pendingCampaignVideoPaths.push_back(movie->second);
        state.triggers.status =
            "campaign movie action " + std::to_string(type)
            + " queued " + std::filesystem::path(movie->second).filename().string();
        return true;
    }

    if (type == 118) {
        const std::size_t cleared = state.world.getSmudges().size();
        state.world.accessSmudges().clear();
        state.repaintRequested = true;
        state.triggers.status = "campaign smudges cleared "
            + std::to_string(cleared);
        return true;
    }

    if (type == 101) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 2)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        state.vision.resetShroudArea(
            state.economy.owner,
            waypoint->x,
            waypoint->y,
            10
        );
        state.repaintRequested = true;
        state.triggers.status = "campaign reshrouded waypoint area";
        return true;
    }

    if (type == 103) {
        state.triggers.campaignMissionTimerLabel =
            runtimeCampaignLocalizedText(
                state,
                runtimeCampaignActionArgument(action, 2)
            );
        state.triggers.status =
            "campaign timer label "
            + state.triggers.campaignMissionTimerLabel;
        return true;
    }

    if (type == 104) {
        const std::string teamKey = toLowerAsciiLocal(trimAsciiLocal(
            !action.targetId.empty()
                ? action.targetId
                : runtimeCampaignActionArgument(action, 2)
        ));
        const int duration = std::max(
            1,
            runtimeCampaignActionInteger(action, 7, 1)
        );
        int flashed = 0;
        const auto found = state.triggers.campaignTeamObjects.find(teamKey);
        if (found != state.triggers.campaignTeamObjects.end()) {
            for (int objectId : found->second) {
                const WorldObjectInstance* object = findObjectById(
                    state,
                    objectId
                );
                if (object == nullptr || !object->alive) {
                    continue;
                }
                spawnRuntimeCampaignFlash(state, *object, duration);
                ++flashed;
            }
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign flashed team " + teamKey
            + " objects=" + std::to_string(flashed);
        return true;
    }

    if (type == 106) {
        const std::string typeId = runtimeCampaignActionArgument(action, 2);
        if (typeId.empty()) {
            return false;
        }
        const int techLevel = runtimeCampaignActionInteger(action, 7, -1);
        const std::string typeKey = toLowerAsciiLocal(typeId);
        state.triggers.campaignTechLevelByType[typeKey] = techLevel;

        auto catalogItem = std::find_if(
            state.economy.catalog.begin(),
            state.economy.catalog.end(),
            [&typeKey](const RuntimeBuildCatalogItem& item) {
                return toLowerAsciiLocal(item.typeId) == typeKey;
            }
        );
        if (catalogItem == state.economy.catalog.end()) {
            const TechTreeNode* node = runtimeFindTechTreeNode(
                state.techTree,
                typeId
            );
            if (const BuildingType* building =
                    findRuntimeBuildingTypeById(state.rules, typeId)) {
                addRuntimeBuildCatalogItem(
                    state,
                    runtimeBuildKindForBuildingType(*building),
                    building->id,
                    node
                );
            } else if (const UnitType* unit =
                    findRuntimeUnitTypeById(state.rules, typeId)) {
                addRuntimeBuildCatalogItem(
                    state,
                    runtimeBuildKindForUnitTypeId(state.rules, unit->id),
                    unit->id,
                    node
                );
            }
            catalogItem = std::find_if(
                state.economy.catalog.begin(),
                state.economy.catalog.end(),
                [&typeKey](const RuntimeBuildCatalogItem& item) {
                    return toLowerAsciiLocal(item.typeId) == typeKey;
                }
            );
        }
        if (catalogItem != state.economy.catalog.end()) {
            catalogItem->techLevel = techLevel;
        }
        state.triggers.status =
            "campaign tech level " + typeId
            + "=" + std::to_string(techLevel);
        return true;
    }

    if (type == 108) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        RuntimeCrateState crate;
        crate.cell = *waypoint;
        const auto terrain = state.terrainInfoByCell.find(
            runtimeCellKey(crate.cell.x, crate.cell.y)
        );
        crate.water = terrain != state.terrainInfoByCell.end()
            && terrain->second.water
            && !terrain->second.bridge
            && !runtimeCellIsBridge(state, crate.cell.x, crate.cell.y);
        crate.imageId = runtimeCrateArtImageId(
            state.rules,
            crate.water
                ? state.rules.getCrateRules().waterImage
                : state.rules.getCrateRules().image
        );

        const int authoredType = runtimeCampaignActionInteger(action, 2, 0);
        const std::string powerupType =
            runtimeCampaignCratePowerupType(authoredType);
        if (authoredType == 0 || authoredType == 6) {
            crate.value = state.rules.getCrateRules().soloMoney;
        }
        if (!powerupType.empty()) {
            if (const CratePowerupRule* powerup =
                    runtimeCrateRuleForType(state, powerupType)) {
                runtimeConfigureCratePowerup(crate, *powerup);
            } else {
                crate.powerupType = powerupType;
            }
        }
        runtimeEnsureCratePowerup(
            state,
            crate,
            static_cast<int>(state.skirmishCrates.size())
        );
        // Script-created campaign crates are authored one-shot objects. They
        // remain until collected and do not join the multiplayer generator.
        crate.authored = true;
        crate.respawnTick = 0;
        state.skirmishCrates.push_back(crate);
        state.triggers.status = "campaign crate created "
            + crate.powerupType;
        return true;
    }

    if (type == 111) {
        int evicted = 0;
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (object.alive
                && object.kind == WorldObjectKind::Structure
                && runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                evicted += unloadRuntimeTransport(state, object);
            }
        }
        state.triggers.status =
            "campaign evicted occupants=" + std::to_string(evicted);
        return true;
    }

    if (type == 113) {
        const std::string house = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        int cheering = 0;
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (object.alive
                && object.kind == WorldObjectKind::Infantry
                && runtimeCampaignHouseMatches(state, house, object.owner)) {
                object.mission = "Cheer";
                ++cheering;
            }
        }
        emitRuntimeSound(state, "cheer");
        state.triggers.status =
            "campaign cheer infantry=" + std::to_string(cheering);
        return true;
    }

    if (type == 114) {
        const int tab = std::clamp(
            runtimeCampaignActionInteger(action, 2, 0),
            0,
            3
        );
        state.activeBuildTab = static_cast<RuntimeBuildKind>(tab);
        state.triggers.status =
            "campaign sidebar tab=" + std::to_string(tab);
        state.repaintRequested = true;
        return true;
    }

    if (type == 115) {
        const std::string typeId = runtimeCampaignActionArgument(action, 2);
        const int duration = std::max(
            1,
            runtimeCampaignActionInteger(action, 7, 1)
        );
        int flashed = 0;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || toLowerAsciiLocal(object.type)
                    != toLowerAsciiLocal(typeId)) {
                continue;
            }
            spawnRuntimeCampaignFlash(state, object, duration);
            ++flashed;
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign flashed type " + typeId
            + " objects=" + std::to_string(flashed);
        return true;
    }

    if (type == 116) {
        if (action.authoredOrdinal < 0) {
            return false;
        }
        const std::string waypointText = runtimeCampaignWaypointId(action, 7);
        int waypointId = -1;
        if (!parseRuntimeInt(waypointText, waypointId)
            || waypointId < 0
            || runtimeCampaignActionInteger(action, 2, -1) != waypointId) {
            state.triggers.status =
                "campaign world audio stop waypoint mismatch";
            return false;
        }
        if (!state.worldAudioInitialized) {
            initializeRuntimeWorldAudio(state);
        }
        RuntimeWorldAudioStopRequest request;
        request.waypointId = waypointId;
        request.requestToken =
            "campaign-world-audio:"
            + toLowerAsciiLocal(trimAsciiLocal(state.config.defaultMapPath))
            + ":trigger:"
            + toLowerAsciiLocal(trimAsciiLocal(trigger.id))
            + ":action:"
            + std::to_string(action.authoredOrdinal)
            + ":type:116";
        const RuntimeWorldAudioCommandResult result = state.worldAudio.stopAt(
            runtimeWorldAudioContext(state),
            request,
            state.tick
        );
        applyRuntimeWorldAudioBackendActions(state, result.actions);
        if (!result.accepted
            && !result.duplicate
            && result.reason != "no matching scenario world audio handle") {
            state.triggers.status = "campaign world audio stop rejected: "
                + result.reason;
            return false;
        }
        state.triggers.status = "campaign waypoint sounds stopped at "
            + waypointText;
        return true;
    }

    if (type == 125) {
        const std::string buildingId = runtimeCampaignActionArgument(
            action,
            2
        );
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            buildingId
        );
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        if (building == nullptr || !waypoint.has_value()) {
            return false;
        }
        const std::string createdOwner = runtimeCampaignTriggerHouse(
            state,
            trigger
        );
        WorldObjectInstance* created = addRuntimeObjectInstanceForOwner(
            state,
            createdOwner,
            runtimeBuildKindForBuildingType(*building),
            building->id,
            *waypoint
        );
        if (created == nullptr) {
            return false;
        }
        initializeRuntimeNavigation(state);
        state.powerRefreshPending = true;
        state.triggers.status =
            "campaign created building " + building->id
            + " at " + runtimeCampaignWaypointId(action, 7);
        return true;
    }

    if (type == 127) {
        const int duration = std::max(
            1,
            runtimeCampaignActionInteger(action, 2, 1)
        );
        state.triggers.campaignChronoScreenEffectUntilTick = std::max(
            state.triggers.campaignChronoScreenEffectUntilTick,
            state.tick + duration
        );
        state.repaintRequested = true;
        state.triggers.status =
            "campaign chronosphere screen effect ticks="
            + std::to_string(duration);
        return true;
    }

    if (type == 130) {
        const std::string targetHouse = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        std::vector<RuntimeOwnerTransitionRoot> restoreRoots;
        std::vector<RuntimeOwnerTransitionObjectPreparation>
            restorePreparations;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            const auto initialOwner =
                state.triggers.campaignInitialOwnerByObject.find(
                    object.instanceId
                );
            if (object.kind != WorldObjectKind::Structure
                || !object.alive
                || object.health <= 0
                || state.deathWeapons.hasTriggered(object.instanceId)
                || state.pendingObjectExitIds.contains(object.instanceId)
                || state.triggers.campaignDestroyedTickByObject.contains(
                    object.instanceId
                )
                || initialOwner
                    == state.triggers.campaignInitialOwnerByObject.end()
                || !runtimeCampaignHouseMatches(
                    state,
                    targetHouse,
                    initialOwner->second
                )) {
                continue;
            }
            restoreRoots.push_back({
                object.instanceId,
                object.owner,
                initialOwner->second
            });
            restorePreparations.push_back({
                object.instanceId,
                std::max(1, runtimeMaxHealthForObject(state, object)),
                "Guard",
                true,
                -1
            });
        }

        const RuntimeOwnerTransitionBatchResult restoredBatch =
            applyRuntimeOwnerTransitionBatch(
                state,
                restoreRoots,
                RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,
                "campaignRestoreInitialBuildings",
                nullptr,
                &restorePreparations
            );
        if (!restoredBatch.applied) {
            state.triggers.status =
                "campaign restore transition rejected: "
                + restoredBatch.error;
            return false;
        }

        state.triggers.status =
            "campaign restored initial buildings house=" + targetHouse
            + " restored=" + std::to_string(restorePreparations.size());
        return true;
    }

    if (type == 131) {
        const std::string buildingType = runtimeCampaignActionArgument(
            action,
            2
        );
        const std::string owner = runtimeCampaignTriggerHouse(state, trigger);
        const int duration = std::max(
            1,
            runtimeCampaignActionInteger(action, 7, 1)
        );
        int flashed = 0;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.kind != WorldObjectKind::Structure
                || toLowerAsciiLocal(object.type)
                    != toLowerAsciiLocal(buildingType)
                || !runtimeCampaignHouseMatches(state, owner, object.owner)) {
                continue;
            }
            spawnRuntimeCampaignFlash(state, object, duration);
            ++flashed;
        }
        state.repaintRequested = true;
        state.triggers.status =
            "campaign flashed buildings " + buildingType
            + " objects=" + std::to_string(flashed);
        return true;
    }

    if (type == 128) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 7)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        const std::string house = runtimeCampaignTriggerHouse(state, trigger);
        std::unordered_set<int> reserved;
        int moved = 0;
        int formationIndex = 0;
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (!object.alive
                || object.kind == WorldObjectKind::Structure
                || !runtimeCampaignHouseMatches(state, house, object.owner)) {
                continue;
            }
            const RuntimeCellPoint offset = formationOffsetForIndex(
                formationIndex++
            );
            const RuntimeCellPoint desired{
                waypoint->x + offset.x,
                waypoint->y + offset.y
            };
            const std::optional<RuntimeCellPoint> destination =
                findRuntimePassableCellNear(
                    state,
                    object,
                    desired,
                    8,
                    -1,
                    &reserved
                );
            if (!destination.has_value()) {
                continue;
            }
            if (!handleRuntimeParasiteHostEvent(
                    state,
                    object.instanceId,
                    ParasiteHostEventKind::Teleporting,
                    std::max(1, state.config.fixedTickHz)
                )) {
                continue;
            }
            object.x = destination->x;
            object.y = destination->y;
            object.cell = runtimeCellKey(object.x, object.y);
            object.level = terrainLevelAtRuntimeCell(
                state,
                object.x,
                object.y
            );
            clearRuntimeRenderPosition(object);
            eraseRuntimeBehavior(state, object.instanceId);
            reserved.insert(object.cell);
            ++moved;
        }
        initializeRuntimeNavigation(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.triggers.status =
            "campaign teleported units=" + std::to_string(moved);
        return true;
    }

    if (type == 129 || type == 132) {
        ensureRuntimeSuperWeaponsInitialized(state);
        const std::string owner = runtimeCampaignTriggerHouse(state, trigger);
        const std::string superWeaponId = runtimeCampaignActionSuperWeaponId(
            state,
            action
        );
        if (superWeaponId.empty()) {
            return false;
        }
        SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
        context.emit = runtimeSuperWeaponEventSink(state);
        const int value = std::max(
            0,
            runtimeCampaignActionInteger(action, 7, 0)
        );
        const bool changed = type == 129
            ? state.superWeapons.setChargePercent(
                owner,
                superWeaponId,
                value,
                &context
            )
            : state.superWeapons.setRechargeTicks(
                owner,
                superWeaponId,
                value,
                &context
            );
        state.triggers.status =
            "campaign superweapon setting " + std::to_string(type)
            + " " + superWeaponId + "=" + std::to_string(value);
        return changed;
    }

    if (type == 137) {
        const std::optional<RuntimeCellPoint> waypoint = runtimeWaypointCell(
            state,
            runtimeCampaignWaypointId(action, 2)
        );
        if (!waypoint.has_value()) {
            return false;
        }
        const std::string ownerKey = toLowerAsciiLocal(trimAsciiLocal(
            runtimeCampaignTriggerHouse(state, trigger)
        ));
        state.triggers.campaignBaseCenterByOwner[ownerKey] = *waypoint;
        state.triggers.status =
            "campaign base center " + ownerKey
            + "=" + std::to_string(waypoint->x)
            + "," + std::to_string(waypoint->y);
        return true;
    }

    if (type == 135 || type == 136) {
        const std::string ownerKey = toLowerAsciiLocal(
            trimAsciiLocal(runtimeCampaignTriggerHouse(state, trigger))
        );
        if (type == 136) {
            state.triggers.campaignPreferredSuperWeaponTargetByOwner.erase(
                ownerKey
            );
        } else {
            const std::optional<RuntimeCellPoint> waypoint =
                runtimeWaypointCell(
                    state,
                    runtimeCampaignWaypointId(action, 2)
                );
            if (!waypoint.has_value()) {
                return false;
            }
            state.triggers.campaignPreferredSuperWeaponTargetByOwner[
                ownerKey
            ] = *waypoint;
        }
        state.triggers.status = type == 135
            ? "campaign preferred superweapon target set"
            : "campaign preferred superweapon target cleared";
        return true;
    }

    if (type == 139) {
        state.triggers.campaignRadarBlackoutUntilTick = std::max(
            state.triggers.campaignRadarBlackoutUntilTick,
            state.tick + std::max(
                0,
                runtimeCampaignActionInteger(action, 2, 0)
            )
        );
        state.triggers.status = "campaign radar blackout";
        state.repaintRequested = true;
        return true;
    }

    if (type == 70) {
        const std::string tagId = toLowerAsciiLocal(
            trimAsciiLocal(action.targetId)
        );
        int destroyed = 0;
        for (RuntimeTriggerDefinition& candidate : state.triggers.triggers) {
            if (candidate.tagIds.find(tagId) == candidate.tagIds.end()) {
                continue;
            }
            candidate.enabled = false;
            candidate.fired = true;
            ++destroyed;
        }
        state.triggers.status =
            "campaign destroyed tag " + action.targetId
            + " triggers=" + std::to_string(destroyed);
        return destroyed > 0;
    }

    if (type == 81) {
        int awakened = 0;
        for (WorldObjectInstance& object : state.world.accessObjects()) {
            if (!object.alive
                || object.kind == WorldObjectKind::Structure
                || !runtimeCampaignObjectAttachedToTrigger(trigger, object)) {
                continue;
            }
            eraseRuntimeBehavior(state, object.instanceId);
            setRuntimeMissionIntent(
                state,
                object,
                RuntimeMissionIntent::Idle,
                "campaign wake self"
            );
            ++awakened;
        }
        state.triggers.status =
            "campaign woke attached units=" + std::to_string(awakened);
        return true;
    }

    if (type == 74 || type == 75) {
        const std::string owner = runtimeCampaignActionHouse(
            state,
            trigger,
            action
        );
        const std::string ownerKey = toLowerAsciiLocal(trimAsciiLocal(owner));
        const auto house =
            state.triggers.campaignHouseProduction.find(ownerKey);
        if (house == state.triggers.campaignHouseProduction.end()) {
            state.triggers.status =
                "campaign AI trigger house unresolved: " + owner;
            return false;
        }
        house->second.aiTriggersEnabled = type == 74;
        state.triggers.status =
            "campaign AI triggers "
            + std::string(type == 74 ? "enabled for " : "disabled for ")
            + house->second.owner;
        return true;
    }

    if (type == 15) {
        state.triggers.status =
            "campaign action " + std::to_string(type)
            + " acknowledged on " + trigger.id;
        return true;
    }

    ++state.triggers.unsupportedActions;
    state.triggers.status =
        "unsupported campaign action " + std::to_string(type)
        + " on " + trigger.id;
    return false;
}

void snapshotRuntimeCampaignTriggerObservations(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope originalMapCampaignLeafTrace("snapshotRuntimeCampaignTriggerObservations", state.tick);
    state.triggers.campaignPreviousSelectedObjects.clear();
    state.triggers.campaignPreviousSelectedObjects.insert(
        state.selectedObjectIds.begin(),
        state.selectedObjectIds.end()
    );
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        state.triggers.campaignPreviouslyKnownObjects.insert(object.instanceId);
        state.triggers.campaignInitialHealthByObject.try_emplace(
            object.instanceId,
            std::max(1, object.health)
        );
        state.triggers.campaignPreviousCellByObject[object.instanceId] =
            runtimeCellKey(object.x, object.y);
        state.triggers.campaignPreviousHealthByObject[object.instanceId] =
            object.health;
        state.triggers.campaignPreviousAliveByObject[object.instanceId] =
            object.alive;
        state.triggers.campaignPreviousOwnerByObject[object.instanceId] =
            object.owner;
        if (object.alive
            && runtimeScenarioCellExplored(state, object.x, object.y)) {
            state.triggers.campaignPreviouslyDiscoveredObjects.insert(
                object.instanceId
            );
        }
    }
}

bool advanceRuntimeTriggers(RuntimeProductionHostState& state) {
    RuntimeSessionTriggerOperations operations(state);
    RuntimeTriggerContext context = runtimeTriggerContext(state);
    return advanceRuntimeTriggers(context, operations);
}

bool prepareRuntimeAirMoveStep(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior) {
    if (!runtimeFactoryAirExitReadyToMove(state, object)) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        return false;
    }
    const RuntimeCellPoint current = runtimeCellPointForObject(object);
    behavior.pendingHasSharedGoal = false;
    RuntimeJumpjetFlight* flight = object.airborne && runtimeJumpjetProfile(state, object)
        ? &runtimeJumpjetFlightFor(state, object) : nullptr;
    if (flight && (flight->targetX != behavior.moveTarget.x || flight->targetY != behavior.moveTarget.y)) {
        bindRuntimeJumpjetMoveGoal(state, object, behavior.moveTarget, behavior.moveTarget);
    }
    const RuntimeCellPoint anchor = flight ? RuntimeCellPoint{flight->goalX, flight->goalY} : behavior.moveTarget;
    const auto accepts = [&](const RuntimeCellPoint& point) {
        return !flight || runtimeCellDistance(anchor, point) <= flight->goalRadius;
    };
    const auto assign = [&](const RuntimeCellPoint& point) {
        behavior.moveTarget = point;
        if (flight) { flight->targetX=point.x; flight->targetY=point.y; }
    };
    if (!behavior.path.empty()
        && runtimeAirTransitionIsClear(state, object.instanceId, current, behavior.path.front())) {
        return true;
    }
    if (state.tick < behavior.nextRepathTick) return false;

    // An idle flyer may have stopped in a previously assigned slot. Move to
    // another free slot nearby; a flyer merely passing through keeps its
    // right of way. Conflicting non-player orders use stable ID priority.
    bool targetTaken = false;
    for (const auto& other : state.world.getObjects()) {
        if (other.instanceId == object.instanceId || !runtimeObjectOccupiesAirNavigation(other)) continue;
        const auto found = state.behaviors.find(other.instanceId);
        const bool leaving = found != state.behaviors.end()
            && found->second.state != RuntimeBehaviorState::Complete
            && ((found->second.kind == RuntimeBehaviorKind::Move
                    && found->second.moveTarget != behavior.moveTarget)
                || (found->second.kind == RuntimeBehaviorKind::Attack
                    && !found->second.path.empty()));
        targetTaken = targetTaken
            || (runtimeCellPointForObject(other) == behavior.moveTarget && !leaving)
            || (other.instanceId < object.instanceId && found != state.behaviors.end()
                && found->second.kind == RuntimeBehaviorKind::Move
                && found->second.state != RuntimeBehaviorState::Complete
                && found->second.moveTarget == behavior.moveTarget);
    }
    if (targetTaken && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)) {
        const auto replacement = findRuntimeAirMoveTarget(state, object, anchor);
        if (replacement.has_value() && accepts(*replacement)) assign(*replacement);
    }

    const auto started = std::chrono::steady_clock::now();
    auto path = findRuntimeAirPath(state, object, current, behavior.moveTarget);
    if (path.empty() && current != behavior.moveTarget && behavior.blockedTicks > 0
        && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)) {
        // Arrived units can surround a free inner formation slot. Reserve a
        // reachable slot on the outside instead of searching that sealed
        // pocket forever or flying through its occupants.
        RuntimeCellPoint candidate;
        std::vector<RuntimeCellPoint> alternative;
        if (findRuntimeReachableAirMoveTarget(state, object, anchor,
                flight ? flight->goalRadius : 8, candidate, alternative)) {
            assign(candidate);
            path = std::move(alternative);
        }
    }
    ++state.perf.lastSimPathRequests;
    state.perf.lastSimPathMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    replaceRuntimeBehaviorPath(behavior, std::move(path));
    behavior.pathRequestPending = false;
    behavior.pendingPathGoal = behavior.moveTarget;
    if (behavior.path.empty() && current != behavior.moveTarget) {
        ++behavior.blockedTicks;
        behavior.state = RuntimeBehaviorState::Blocked;
        behavior.nextRepathTick = state.tick + runtimeRepathCooldownTicks;
        return false;
    }
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.nextRepathTick = state.tick;
    return true;
}

const UnitType* runtimeFixedWingProfile(const RuntimeGameSessionState& state,const WorldObjectInstance& object) {
    const auto* unit=findRuntimeUnitType(state.rules,object);
    return unit&&runtimeUnitUsesAirMovement(*unit)&&!runtimeUnitUsesJumpjetMovement(*unit)
        &&!unit->spawned&&!unit->missileSpawn&&!runtimeHasSupportFlightPose(object)
        &&!runtimeObjectManagedByAirstrike(state,object.instanceId)?unit:nullptr;
}

void applyRuntimeFixedWingPose(const RuntimeGameSessionState& state,WorldObjectInstance& object) {
    const auto* jumpjet = runtimeJumpjetProfile(state, object);
    if (jumpjet && runtimeUnitUsesAutomaticJumpjetLanding(*jumpjet)) return;
    if (runtimeCarrierHasDeckFlight(state, object)) {
        applyRuntimeCarrierDeckFlightPresentation(state, object); return;
    }
    object.aircraftDockFlightProgress=-1.0;
    const auto found=state.aircraftFlights.find(object.instanceId);
    if(found==state.aircraftFlights.end()||!object.alive||!object.airborne
        ||!runtimeFixedWingProfile(state,object))return;
    const auto& f=found->second;
    // External relocation (chrono, campaign actions) owns the new cell.
    if(object.x!=f.x/runtimeAircraftCellQ16||object.y!=f.y/runtimeAircraftCellQ16)return;
    object.hasRenderPosition=true;object.renderMoveActive=f.moving;
    object.renderX=double(f.x)/runtimeAircraftCellQ16;
    object.renderY=double(f.y)/runtimeAircraftCellQ16;
    object.renderLevel=f.z<0?object.flightLevel:double(f.z)/runtimeAircraftCellQ16;
    object.level=static_cast<int>(std::lround(object.renderLevel));
    if(f.dockFlightStartZ>f.dockFlightTargetZ&&f.dockFlightTargetZ>=0)
        object.aircraftDockFlightProgress=std::clamp(double(f.z-f.dockFlightTargetZ)
            /double(f.dockFlightStartZ-f.dockFlightTargetZ),0.0,1.0);
    object.onBridge=object.renderOnBridge=false;object.subCell=-1;
}

void reconcileRuntimeAircraftFlights(RuntimeGameSessionState& state) {
    for(auto it=state.aircraftFlights.begin();it!=state.aircraftFlights.end();) {
        auto* object=findMutableObjectById(state,it->first);const auto& f=it->second;
        if(!object||!object->alive||!object->airborne||!runtimeFixedWingProfile(state,*object)
            ||object->x!=f.x/runtimeAircraftCellQ16||object->y!=f.y/runtimeAircraftCellQ16) {
            it=state.aircraftFlights.erase(it);continue;
        }
        if(!state.behaviors.contains(object->instanceId))it->second.moving=false;
        applyRuntimeFixedWingPose(state,*object);++it;
    }
}

RuntimeAircraftFlight& runtimeAircraftFlightFor(RuntimeGameSessionState& state,const WorldObjectInstance& object) {
    auto [it,inserted]=state.aircraftFlights.try_emplace(object.instanceId);
    if(inserted||object.x!=it->second.x/runtimeAircraftCellQ16||object.y!=it->second.y/runtimeAircraftCellQ16) {
        it->second={};
        it->second.x=std::llround((object.hasRenderPosition?object.renderX:object.x)*runtimeAircraftCellQ16);
        it->second.y=std::llround((object.hasRenderPosition?object.renderY:object.y)*runtimeAircraftCellQ16);
    }
    if(it->second.z<0)it->second.z=std::int64_t(object.flightLevel)*runtimeAircraftCellQ16;
    return it->second;
}

std::int64_t runtimeFixedWingVerticalStep(const RuntimeGameSessionState& state,std::int64_t remaining) {
    // Retail non-dropship Fly locomotion: clamp((height-target)/20,20,50)
    // leptons per 15 Hz rule frame. Clamp the final scaled step to the pad.
    const auto leptons=std::clamp<std::int64_t>(remaining/(runtimeAircraftCellQ16/256*20),20,50);
    return std::min(remaining,std::max<std::int64_t>(1,std::llround(
        double(leptons)*runtimeAircraftCellQ16/256*15/std::max(1,state.config.fixedTickHz))));
}

void advanceRuntimeFixedWingDepartureHeight(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,RuntimeAircraftFlight& f) {
    f.landingHostId=f.landingDockIndex=-1;
    const auto cruise=std::int64_t(object.flightLevel)*runtimeAircraftCellQ16;
    if(f.z<cruise)f.z+=runtimeFixedWingVerticalStep(state,cruise-f.z);
    if(f.z>=cruise) {
        f.dockFlightStartZ=f.dockFlightTargetZ=-1;f.landingSoundPlayed=false;
    }
}

bool recoverRuntimeFixedWingHeight(RuntimeGameSessionState& state,WorldObjectInstance& object) {
    if(!runtimeFixedWingProfile(state,object))return false;
    auto& f=runtimeAircraftFlightFor(state,object);
    if(f.lastTick==state.tick)return false;
    f.lastTick=state.tick;const auto before=f.z;
    advanceRuntimeFixedWingDepartureHeight(state,object,f);f.moving=false;
    applyRuntimeFixedWingPose(state,object);return f.z!=before;
}

bool runtimeFixedWingLandingReady(RuntimeProductionHostState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,const WorldObjectInstance& host,int dockIndex) {
    auto& f=runtimeAircraftFlightFor(state,object);
    const auto dock=runtimeAircraftDockSpawn(state,host,dockIndex);
    const auto targetZ=std::int64_t(dock.level)*runtimeAircraftCellQ16;
    if(f.z<=targetZ&&f.dockFlightStartZ<0)return true;
    if(f.landingHostId!=host.instanceId||f.landingDockIndex!=dockIndex) {
        f.landingHostId=host.instanceId;f.landingDockIndex=dockIndex;
        f.dockFlightStartZ=std::max(f.z,std::int64_t(object.flightLevel)*runtimeAircraftCellQ16);
        f.dockFlightTargetZ=targetZ;f.landingSoundPlayed=false;
    }
    const auto* unit=runtimeFixedWingProfile(state,object);
    const int desired=worldObjectFacingFromMapFacing(state.rules.getPoseDir())*256;
    if(f.heading<0)f.heading=normalizeRuntimeFacing(object.facing)*256;
    int delta=desired-f.heading;
    if(delta>32768)delta-=65536;if(delta< -32768)delta+=65536;
    if(f.lastTick!=state.tick) {
        f.lastTick=state.tick;
        f.z-=runtimeFixedWingVerticalStep(state,std::max<std::int64_t>(0,f.z-targetZ));
        const int maxTurn=std::max(1,static_cast<int>(std::llround(
            std::clamp(unit?unit->rot:1,1,127)*256.0*15/std::max(1,state.config.fixedTickHz))));
        f.heading=(f.heading+std::clamp(delta,-maxTurn,maxTurn)+65536)&65535;
        object.facing=((f.heading+128)/256)%256;object.bodyFacingClock={};
    }
    f.moving=false;applyRuntimeFixedWingPose(state,object);
    // Native AuxSound2 is a once-per-landing crossing below 300 leptons,
    // rather than a repeated request or an extra sound on final attachment.
    if(!f.landingSoundPlayed&&f.z-targetZ<300*(runtimeAircraftCellQ16/256)) {
        queueRuntimeAircraftAuxSound(state,object,false,0);f.landingSoundPlayed=true;
    }
    behavior.kind=RuntimeBehaviorKind::Move;behavior.state=RuntimeBehaviorState::Moving;
    behavior.moveTarget=dock.cell;behavior.pendingAllowedOccupiedObjectId=host.instanceId;
    behavior.path.clear();behavior.pathRequestPending=false;behavior.moveCooldownTicks=0;
    setRuntimeMissionIntent(state,object,RuntimeMissionIntent::Moving,"aircraftDescendingToDock");
    return f.z==targetZ&&f.heading==desired;
}

double runtimeFixedWingCruiseInterval(const RuntimeGameSessionState& state,const WorldObjectInstance& object) {
    // Airport fighters share the retail Speed clock. The old extra factor
    // compensated for the former accelerated ordinary movement cadence.
    return runtimeMovementBaseIntervalTicks(state,object);
}

bool advanceRuntimeFixedWingToward(RuntimeProductionHostState& state,WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior,double goalX,double goalY) {
    auto& f=runtimeAircraftFlightFor(state,object);
    if(f.lastTick==state.tick){applyRuntimeFixedWingPose(state,object);return false;}
    f.lastTick=state.tick;
    advanceRuntimeFixedWingDepartureHeight(state,object,f);
    behavior.path.clear();behavior.pathRequestPending=false;behavior.moveCooldownTicks=0;
    behavior.moveSegmentFrom={-1,-1};behavior.renderMoveTicksTotal=behavior.renderMoveTicksRemaining=0;
    const auto targetX=std::llround(std::clamp(goalX,0.0,511.999)*runtimeAircraftCellQ16);
    const auto targetY=std::llround(std::clamp(goalY,0.0,511.999)*runtimeAircraftCellQ16);
    const double dx=double(targetX-f.x),dy=double(targetY-f.y),distance=std::hypot(dx,dy);
    double step=std::min(distance,runtimeAircraftCellQ16/runtimeFixedWingCruiseInterval(state,object));
    const auto oldX=f.x,oldY=f.y;const auto previous=runtimeCellPointForObject(object);
    if(distance>0.0 && step>0.0) {
        constexpr double tau=6.2831853071795864769;
        const int desired=static_cast<int>(std::llround(std::atan2(dx-dy,-dx-dy)*65536.0/tau))&65535;
        if(f.heading<0||((f.heading+128)/256)%256!=normalizeRuntimeFacing(object.facing))
            f.heading=normalizeRuntimeFacing(object.facing)*256;
        int delta=desired-f.heading;
        if(delta>32768)delta-=65536;
        if(delta< -32768)delta+=65536;
        const auto* unit=runtimeFixedWingProfile(state,object);
        const bool bounded=unit&&unit->airportBound&&unit->rot>0;
        const int maxTurn=bounded?std::max(1,static_cast<int>(std::llround(
            std::clamp(unit->rot,1,127)*256.0*15/std::max(1,state.config.fixedTickHz)))):32768;
        const bool aligned=std::abs(delta)<=maxTurn;
        f.heading=(f.heading+std::clamp(delta,-maxTurn,maxTurn)+65536)&65535;
        if(aligned) {
            f.x+=std::llround(dx*step/distance);f.y+=std::llround(dy*step/distance);
        } else {
            // Radius <= half the remaining distance also handles a nearby
            // click behind the aircraft without stopping its forward travel.
            step=std::min(step,distance*(maxTurn*tau/65536.0)*0.5);
            const double angle=f.heading*tau/65536.0;
            const double sine=std::sin(angle),cosine=std::cos(angle);
            f.x+=std::llround((sine-cosine)*0.7071067811865475244*step);
            f.y+=std::llround((-sine-cosine)*0.7071067811865475244*step);
        }
        f.x=std::clamp<std::int64_t>(f.x,0,512*runtimeAircraftCellQ16-1);
        f.y=std::clamp<std::int64_t>(f.y,0,512*runtimeAircraftCellQ16-1);
        object.facing=((f.heading+128)/256)%256;object.bodyFacingClock={};
    }
    const RuntimeCellPoint cell{static_cast<int>(f.x/runtimeAircraftCellQ16),static_cast<int>(f.y/runtimeAircraftCellQ16)};
    if(cell!=previous){setRuntimeObjectCell(state,object,cell,false);updateRuntimeTickOccupancyForMove(state,object,previous,cell);}
    f.moving=f.x!=oldX||f.y!=oldY;
    object.movementAnimationDistance=std::fmod(object.movementAnimationDistance
        +std::hypot(double(f.x-oldX),double(f.y-oldY))/runtimeAircraftCellQ16,1024.0);
    applyRuntimeFixedWingPose(state,object);
    behavior.state=behavior.kind==RuntimeBehaviorKind::Attack?RuntimeBehaviorState::Chasing:RuntimeBehaviorState::Moving;
    setRuntimeMissionIntent(state,object,RuntimeMissionIntent::Moving,"continuousAircraftFlight");
    if(f.moving)++state.actionSummary.movedThisTick;
    return f.moving;
}

std::pair<double,double> runtimeFixedWingMoveGoal(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,const RuntimeObjectBehavior& behavior) {
    const auto* host=findObjectById(state,behavior.pendingAllowedOccupiedObjectId);
    if(host&&host->instanceId==object.dockedToObjectId&&object.dockIndex>=0) {
        const auto dock=runtimeAircraftDockSpawn(state,*host,object.dockIndex);
        if(dock.cell==behavior.moveTarget)return {dock.renderX,dock.renderY};
    }
    return {double(behavior.moveTarget.x),double(behavior.moveTarget.y)};
}

RuntimeComputerAiProfile runtimeComputerAiProfile(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai) {
    const int hz = std::max(1, state.config.fixedTickHz);
    RuntimeComputerAiProfile profile;
    profile.buildRetryTicks = hz;
    profile.repairIntervalTicks = hz * 2;
    const std::string difficulty = toUpperAsciiLocal(
        trimAsciiLocal(ai.difficulty)
    );
    if (difficulty == "EASY") {
        profile.thinkIntervalTicks = std::max(8, hz);
        profile.minimumBuildTicks = hz * 4;
        profile.initialAttackDelayTicks = hz * 30;
        profile.attackIntervalTicks = hz * 45;
        profile.attackRetryTicks = hz * 7;
        profile.maximumCombatUnits = 18;
        profile.attackThreshold = 7;
        profile.attackWaveSize = 5;
        profile.maximumDefenders = 3;
    } else if (difficulty == "HARD") {
        profile.thinkIntervalTicks = std::max(6, hz / 2);
        profile.minimumBuildTicks = hz * 2;
        profile.initialAttackDelayTicks = hz * 12;
        profile.attackIntervalTicks = hz * 20;
        profile.attackRetryTicks = hz * 4;
        profile.maximumCombatUnits = 36;
        profile.attackThreshold = 4;
        profile.attackWaveSize = 9;
        profile.maximumDefenders = 7;
    } else if (difficulty == "BRUTAL") {
        profile.thinkIntervalTicks = std::max(4, hz / 3);
        profile.minimumBuildTicks = std::max(hz, hz * 3 / 2);
        profile.initialAttackDelayTicks = hz * 8;
        profile.attackIntervalTicks = hz * 15;
        profile.attackRetryTicks = hz * 3;
        profile.maximumCombatUnits = 44;
        profile.attackThreshold = 3;
        profile.attackWaveSize = 11;
        profile.maximumDefenders = 9;
    } else {
        profile.thinkIntervalTicks = std::max(8, hz * 2 / 3);
        profile.minimumBuildTicks = hz * 3;
        profile.initialAttackDelayTicks = hz * 20;
        profile.attackIntervalTicks = hz * 30;
        profile.attackRetryTicks = hz * 5;
    }
    return profile;
}

bool runtimeComputerAiOwnerMatches(
    const WorldObjectInstance& object,
    const std::string& owner) {
    return toLowerAsciiLocal(trimAsciiLocal(object.owner))
        == toLowerAsciiLocal(trimAsciiLocal(owner));
}

bool runtimeComputerAiHasBuilding(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& typeId) {
    const std::string wanted = toUpperAsciiLocal(trimAsciiLocal(typeId));
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && object.kind == WorldObjectKind::Structure
            && runtimeComputerAiOwnerMatches(object, owner)
            && toUpperAsciiLocal(trimAsciiLocal(object.type)) == wanted) {
            return true;
        }
    }
    return false;
}

bool runtimeComputerAiHasFactory(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& factoryType) {
    if (trimAsciiLocal(factoryType).empty()) {
        return true;
    }
    const std::string wanted = toUpperAsciiLocal(trimAsciiLocal(factoryType));
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeComputerAiOwnerMatches(object, owner)) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr
            && toUpperAsciiLocal(trimAsciiLocal(building->factory)) == wanted) {
            return true;
        }
    }
    return false;
}

bool runtimeComputerAiHasRefinery(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeComputerAiOwnerMatches(object, owner)) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr && runtimeBuildingCountsAsRefinery(*building)) {
            return true;
        }
    }
    return false;
}

bool runtimeComputerAiHasTechCenter(
    const RuntimeGameSessionState& state,
    const std::string& owner) {
    static const std::unordered_set<std::string> techCenters = {
        "GATECH", "NATECH", "YATECH"
    };
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && object.kind == WorldObjectKind::Structure
            && runtimeComputerAiOwnerMatches(object, owner)
            && techCenters.find(toUpperAsciiLocal(object.type))
                != techCenters.end()) {
            return true;
        }
    }
    return false;
}

bool runtimeComputerAiPrerequisiteTokenMet(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const std::string& token) {
    const std::string normalized = toUpperAsciiLocal(trimAsciiLocal(token));
    if (normalized.empty()
        || normalized == "NONE"
        || normalized == "<NONE>") {
        return true;
    }
    if (normalized == "PROC"
        && runtimeOwnerHasAlternateRefineryPrerequisite(state, owner)) {
        return true;
    }
    for (const PrerequisiteAlias& alias : state.rules.getPrerequisiteAliases()) {
        if (toUpperAsciiLocal(alias.name) != normalized) {
            continue;
        }
        for (const std::string& buildingId : splitRuntimeTokenList(alias.value)) {
            if (runtimeComputerAiHasBuilding(state, owner, buildingId)) {
                return true;
            }
        }
        return false;
    }
    if (normalized == "CONSTRUCTION YARD") {
        return runtimeOwnerHasConstructionYard(state, owner);
    }
    if (normalized == "GAAIRC" && runtimeIsUnholyAllianceMode(state)
        && runtimeComputerAiHasBuilding(state, owner, "AMRADR")) {
        return true;
    }
    if (normalized == "POWER") {
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.kind != WorldObjectKind::Structure
                || !runtimeComputerAiOwnerMatches(object, owner)) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building != nullptr && building->power > 0) {
                return true;
            }
        }
        return false;
    }
    if (normalized == "PROC") {
        return runtimeComputerAiHasRefinery(state, owner);
    }
    if (normalized == "BARRACKS") {
        return runtimeComputerAiHasFactory(state, owner, "InfantryType");
    }
    if (normalized == "FACTORY") {
        return runtimeComputerAiHasFactory(state, owner, "UnitType");
    }
    if (normalized == "RADAR") {
        return runtimeComputerAiHasRadar(state, owner);
    }
    if (normalized == "TECH") {
        return runtimeComputerAiHasTechCenter(state, owner);
    }
    return runtimeComputerAiHasBuilding(state, owner, normalized);
}

bool runtimeComputerAiPrerequisitesMet(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    bool normalMet = true;
    for (const std::string& token : splitRuntimeTokenList(item.prerequisite)) {
        if (!runtimeComputerAiPrerequisiteTokenMet(state, owner, token)) {
            normalMet = false;
            break;
        }
    }
    if (normalMet) {
        return true;
    }
    for (const std::string& token :
            splitRuntimeTokenList(item.prerequisiteOverride)) {
        if (runtimeComputerAiPrerequisiteTokenMet(state, owner, token)) {
            return true;
        }
    }
    return false;
}

void applyRuntimeComputerAiModeRules(
    const RuntimeGameSessionState& state,
    RuntimeBuildCatalogItem& item) {
    applyRuntimeModeRulesToBuildItem(state, item);
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiBuildItem(
    const RuntimeGameSessionState& state,
    const std::string& typeId,
    const std::string& owner) {
    if (const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, typeId)) {
        RuntimeBuildCatalogItem item;
        item.kind = runtimeBuildKindForBuildingType(*building);
        item.typeId = building->id;
        item.label = building->name.empty() ? building->id : building->name;
        item.owner = building->owner;
        item.requiredHouses = building->requiredHouses;
        item.forbiddenHouses = building->forbiddenHouses;
        item.prerequisite = building->prerequisite;
        item.prerequisiteOverride = building->prerequisiteOverride;
        item.requiredFactory = "BuildingType";
        item.cost = std::max(0, building->cost);
        item.powerDelta = building->power;
        item.techLevel = building->techLevel;
        item.buildLimit = building->buildLimit;
        item.waterBound = building->waterBound;
        item.naval = building->naval;
        item.wall = building->wall;
        applyRuntimeComputerAiModeRules(state, item);
        item.buildTicks = runtimeBaseBuildTimeTicks(state, item);
        item.cost = runtimeAdjustedFactoryPlantCost(
            state,
            owner,
            item,
            item.cost
        );
        return item;
    }
    if (const UnitType* unit = findRuntimeUnitTypeById(state.rules, typeId)) {
        RuntimeBuildCatalogItem item;
        item.kind = runtimeBuildKindForUnitTypeId(state.rules, unit->id);
        item.typeId = unit->id;
        item.label = unit->name.empty() ? unit->id : unit->name;
        item.owner = unit->owner;
        item.requiredHouses = unit->requiredHouses;
        item.forbiddenHouses = unit->forbiddenHouses;
        item.prerequisite = unit->prerequisite;
        item.prerequisiteOverride = unit->prerequisiteOverride;
        item.requiredFactory = item.kind == RuntimeBuildKind::Infantry
            ? "InfantryType"
            : unit->airportBound ? "AircraftType" : "UnitType";
        item.cost = std::max(0, unit->cost);
        item.techLevel = unit->techLevel;
        item.buildLimit = unit->buildLimit;
        item.waterBound = unit->waterBound;
        item.naval = unit->naval;
        applyRuntimeComputerAiModeRules(state, item);
        item.buildTicks = runtimeBaseBuildTimeTicks(state, item);
        item.cost = runtimeAdjustedFactoryPlantCost(
            state,
            owner,
            item,
            item.cost
        );
        return item;
    }
    return std::nullopt;
}

bool runtimeComputerAiItemAllowed(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item) {
    const bool secretLabUnlock = runtimeBuildItemHasSecretLabUnlock(
        state,
        ai.owner,
        item
    );
    if (!runtimeBuildItemMeetsStolenTechRequirements(state, ai.owner, item)) {
        return false;
    }
    if (item.cost <= 0
        || item.wall
        || !runtimeOwnerHasCompatibleFactory(state, ai.owner, item)) {
        return false;
    }
    if (!secretLabUnlock
        && (item.techLevel < 0
            || item.techLevel > state.economy.techLevel)) {
        return false;
    }
    if (!secretLabUnlock
        && !runtimeBuildItemVisibleForCountry(state, item, ai.country)) {
        return false;
    }
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, item.typeId);
        const int side = runtimeBasePlanningSideForCountry(ai.country);
        if (building == nullptr
            || (!secretLabUnlock
                && !runtimeIsUnholyAllianceMode(state)
                && building->aiBasePlanningSide >= 0
                && side >= 0
                && building->aiBasePlanningSide != side)
            // Radar and battle labs grant support powers too. The option
            // disables the six major superweapon structures, just as it
            // does in the player's catalog, not their technology chain.
            || (!state.config.superweapons && runtimeIsSuperweaponCatalogItem(item))) {
            return false;
        }
    }
    if (!runtimeBuildLimitBlockReason(
            state,
            ai.owner,
            item,
            true,
            false
        ).empty()) {
        return false;
    }
    return secretLabUnlock
        || runtimeComputerAiPrerequisitesMet(state, ai.owner, item);
}

bool runtimeComputerAiObjectIsMiner(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    return unit != nullptr && (unit->harvester || unit->resourceGatherer);
}

bool runtimeComputerAiObjectCanFight(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (!object.alive
        || object.kind == WorldObjectKind::Structure
        || runtimeComputerAiObjectIsMiner(state, object)
        || !runtimeObjectCanMove(state, object)) {
        return false;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    if (unit != nullptr
        && (unit->engineer || unit->slaved || unit->spawned)) {
        return false;
    }
    const RuntimeWeaponProfile weapon =
        runtimeWeaponProfileForObject(state, object);
    return weapon.weapon != nullptr
        && (weapon.weapon->damage > 0 || runtimeWeaponHasAttachEffect(state.rules, weapon.weapon))
        && runtimeWeaponHasAttackRange(*weapon.weapon);
}

bool runtimeComputerAiBuildingCountsAsDefense(const BuildingType& building) {
    return building.isBaseDefense
        || runtimeBuildKindForBuildingType(building)
            == RuntimeBuildKind::Defense;
}

RuntimeComputerAiMetrics runtimeComputerAiMetrics(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai) {
    RuntimeComputerAiMetrics metrics;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || !runtimeComputerAiOwnerMatches(object, ai.owner)) {
            continue;
        }
        if (object.kind != WorldObjectKind::Structure) {
            ++metrics.mobileUnits;
            if (runtimeComputerAiObjectIsMiner(state, object)) {
                ++metrics.harvesters;
            }
            if (const auto* unit = findRuntimeUnitType(state.rules, object);
                    unit != nullptr && runtimeUnitIsMobileSlaveMiner(*unit)) {
                ++metrics.slaveMiners;
            }
            if (runtimeComputerAiObjectCanFight(state, object)) {
                ++metrics.combatUnits;
            }
            continue;
        }
        ++metrics.structures;
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr) {
            continue;
        }
        if (building->constructionYard) {
            ++metrics.constructionYards;
        }
        const int power = runtimeBuildingPowerOutput(*building, object);
        if (power >= 0) {
            metrics.powerProduced += power;
            if (power > 0) {
                ++metrics.powerPlants;
            }
        } else {
            metrics.powerUsed += -power;
        }
        if (runtimeBuildingCountsAsRefinery(*building)) {
            ++metrics.refineries;
        }
        // Deployed slave miners still fill a harvester slot. Ignoring their
        // building form makes Yuri keep replacing a miner after it deploys.
        if (runtimeBuildingCanGatherResources(*building)) {
            ++metrics.harvesters;
            ++metrics.slaveMiners;
        } else if (runtimeBuildingCountsAsRefinery(*building)) {
            ++metrics.ordinaryRefineries;
        }
        const std::string factory = toUpperAsciiLocal(building->factory);
        if (factory == "INFANTRYTYPE") {
            ++metrics.barracks;
        } else if (factory == "UNITTYPE"
            && !building->naval
            && !building->waterBound) {
            ++metrics.vehicleFactories;
        } else if (factory == "UNITTYPE" && (building->naval || building->waterBound)) {
            ++metrics.shipyards;
        }
        if (building->radar || building->spySat || factory == "RADAR") {
            ++metrics.radars;
        }
        const std::string type = toUpperAsciiLocal(object.type);
        if (type == "GATECH" || type == "NATECH" || type == "YATECH") {
            ++metrics.techCenters;
        }
        if (runtimeComputerAiBuildingCountsAsDefense(*building)) {
            ++metrics.defenses;
        }
        if (!buildingSuperWeapons(*building).empty()) {
            ++metrics.superweaponStructures;
        }
    }
    return metrics;
}

std::vector<std::string> runtimeComputerAiStructureCandidates(
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiStructureRole role) {
    const bool yuri = runtimeCountryIsYuri(ai.country);
    const bool soviet = runtimeCountryIsSoviet(ai.country);
    switch (role) {
    case RuntimeComputerAiStructureRole::Power:
        return yuri ? std::vector<std::string>{"YAPOWR"}
            : soviet ? std::vector<std::string>{"NAPOWR", "NANRCT"}
            : std::vector<std::string>{"GAPOWR"};
    case RuntimeComputerAiStructureRole::Refinery:
        return yuri ? std::vector<std::string>{"YAREFN"}
            : soviet ? std::vector<std::string>{"NAREFN"}
            : std::vector<std::string>{"GAREFN"};
    case RuntimeComputerAiStructureRole::Barracks:
        return yuri ? std::vector<std::string>{"YABRCK"}
            : soviet ? std::vector<std::string>{"NAHAND"}
            : std::vector<std::string>{"GAPILE"};
    case RuntimeComputerAiStructureRole::VehicleFactory:
        return yuri ? std::vector<std::string>{"YAWEAP"}
            : soviet ? std::vector<std::string>{"NAWEAP"}
            : std::vector<std::string>{"GAWEAP"};
    case RuntimeComputerAiStructureRole::Shipyard:
        return yuri ? std::vector<std::string>{"YAYARD"}
            : soviet ? std::vector<std::string>{"NAYARD"}
            : std::vector<std::string>{"GAYARD"};
    case RuntimeComputerAiStructureRole::Radar:
        return yuri ? std::vector<std::string>{"NAPSIS"}
            : soviet ? std::vector<std::string>{"NARADR"}
            : std::vector<std::string>{"GAAIRC", "AMRADR"};
    case RuntimeComputerAiStructureRole::Defense:
        return yuri ? std::vector<std::string>{"YAGGUN", "YAPSYT"}
            : soviet ? std::vector<std::string>{"NALASR", "NASAM", "TESLA"}
            : std::vector<std::string>{"GAPILL", "NASAM", "ATESLA"};
    case RuntimeComputerAiStructureRole::Tech:
        return yuri ? std::vector<std::string>{"YATECH"}
            : soviet ? std::vector<std::string>{"NATECH"}
            : std::vector<std::string>{"GATECH"};
    case RuntimeComputerAiStructureRole::Superweapon:
        return yuri ? std::vector<std::string>{"YAGNTC", "YAPPET"}
            : soviet ? std::vector<std::string>{"NAIRON", "NAMISL"}
            : std::vector<std::string>{"GACSPH", "GAWEAT"};
    }
    return {};
}

bool runtimeComputerAiBuildingMatchesRole(
    const BuildingType& building,
    RuntimeComputerAiStructureRole role) {
    switch (role) {
    case RuntimeComputerAiStructureRole::Power:
        return building.power > 0;
    case RuntimeComputerAiStructureRole::Refinery:
        return runtimeBuildingCountsAsRefinery(building);
    case RuntimeComputerAiStructureRole::Barracks:
        return toUpperAsciiLocal(building.factory) == "INFANTRYTYPE";
    case RuntimeComputerAiStructureRole::VehicleFactory:
        return toUpperAsciiLocal(building.factory) == "UNITTYPE"
            && !building.naval && !building.waterBound;
    case RuntimeComputerAiStructureRole::Shipyard:
        return toUpperAsciiLocal(building.factory) == "UNITTYPE"
            && (building.naval || building.waterBound);
    case RuntimeComputerAiStructureRole::Radar:
        return building.radar
            || building.spySat
            || toUpperAsciiLocal(building.factory) == "RADAR";
    case RuntimeComputerAiStructureRole::Defense:
        return runtimeComputerAiBuildingCountsAsDefense(building);
    case RuntimeComputerAiStructureRole::Tech:
        return toUpperAsciiLocal(building.id) == "GATECH"
            || toUpperAsciiLocal(building.id) == "NATECH"
            || toUpperAsciiLocal(building.id) == "YATECH";
    case RuntimeComputerAiStructureRole::Superweapon:
        return !buildingSuperWeapons(building).empty();
    }
    return false;
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectStructureForRole(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiStructureRole role) {
    const std::string aiOwner = runtimeRepairDepotCanonicalOwnerId(
        state,
        ai.owner
    );
    for (const RuntimeSpecialBuildingUnlock& unlock :
            RuntimeSpecialBuildingSystem::secretLabUnlocks(
                makeRuntimeSpecialBuildingContext(state)
            )) {
        if (unlock.kind != RuntimeSpecialBuildingUnlockKind::Building
            || runtimeRepairDepotCanonicalOwnerId(state, unlock.owner)
                != aiOwner) {
            continue;
        }
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            unlock.typeId
        );
        if (building == nullptr
            || !runtimeComputerAiBuildingMatchesRole(*building, role)) {
            continue;
        }
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(state, unlock.typeId, ai.owner);
        if (item.has_value()
            && runtimeComputerAiItemAllowed(state, ai, *item)) {
            return item;
        }
    }
    const auto candidates = role == RuntimeComputerAiStructureRole::Refinery
        ? state.rules.getComputerBaseBuildingRules().refineryTypes
        : runtimeComputerAiStructureCandidates(ai, role);
    for (const std::string& typeId : candidates) {
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(state, typeId, ai.owner);
        if (item.has_value() && runtimeComputerAiItemAllowed(state, ai, *item)) {
            return item;
        }
    }
    if (role == RuntimeComputerAiStructureRole::Refinery) return std::nullopt;
    for (const BuildingType& building : state.rules.getBuildingTypes()) {
        if (!runtimeComputerAiBuildingMatchesRole(building, role)) {
            continue;
        }
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(state, building.id, ai.owner);
        if (item.has_value() && runtimeComputerAiItemAllowed(state, ai, *item)) {
            return item;
        }
    }
    return std::nullopt;
}

RuntimeComputerAiBaseTargets runtimeComputerAiBaseTargets(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai) {
    RuntimeComputerAiBaseTargets targets;
    const auto& rules = state.rules.getComputerBaseBuildingRules();
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    switch (runtimeBasePlanningSideForCountry(ai.country)) {
    case 0: targets.defenses = rules.alliedDefenseCounts[difficulty]; break;
    case 1: targets.defenses = rules.sovietDefenseCounts[difficulty]; break;
    case 2: targets.defenses = rules.thirdDefenseCounts[difficulty]; break;
    default: break; // Retail 0x505C95 leaves an unknown planning side at zero.
    }
    // Retail 0x5059D6 checks the Owner mask of HarvesterUnit entries. The
    // exception is not a hardcoded Yuri-country check: mods can change it.
    targets.slaveRefineries = true;
    for (const auto& typeId : rules.harvesterTypes) {
        const auto* unit = findRuntimeUnitTypeById(state.rules, typeId);
        if (unit != nullptr && runtimeOwnerListAllowsHouse(unit->owner, ai.country)) {
            targets.slaveRefineries = false;
            break;
        }
    }
    // The first refinery is already present in the retail plan. Slave miners
    // insert (AISlaveMinerNumber - 1); other houses insert AIExtraRefineries.
    targets.refineries = targets.slaveRefineries
        ? std::max(1, state.rules.getComputerMiningRules().slaveMinerNumber[difficulty])
        : 1 + rules.extraRefineries[difficulty];
    return targets;
}

std::vector<RuntimeComputerAiStructureRole> runtimeComputerAiBasePlan(
    const RuntimeComputerAiState& ai, const RuntimeComputerAiBaseTargets& targets) {
    using Role = RuntimeComputerAiStructureRole;
    // Keep the existing technology roles. Retail 0x505A8D inserts extra
    // refineries after the first, and 0x505CF7 inserts defenses after index 3.
    std::vector<Role> plan{Role::Power, Role::Refinery, Role::Barracks,
        Role::VehicleFactory, Role::Radar, Role::Tech, Role::Superweapon};
    std::uint64_t random = runtimeStableHashString("computer base plan/1");
    runtimeStableHashMixString(random, toLowerAsciiLocal(trimAsciiLocal(ai.owner)));
    runtimeStableHashMix(random, runtimeBasePlanningSideForCountry(ai.country));
    runtimeStableHashMix(random, runtimeComputerMiningDifficultyIndex(ai.difficulty));
    const auto insert = [&](Role role, std::size_t first, int serial) {
        runtimeStableHashMix(random, serial);
        const auto position = first + static_cast<std::size_t>(random % (plan.size() - first + 1));
        plan.insert(plan.begin() + static_cast<std::ptrdiff_t>(position), role);
    };
    for (int i = 1; i < targets.refineries; ++i) insert(Role::Refinery, 2, i);
    for (int i = 0; i < targets.defenses; ++i) insert(Role::Defense, 4, i);
    return plan;
}

void appendRuntimeComputerAiBasePlanGoals(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile, const RuntimeComputerAiMetrics& metrics,
    std::vector<RuntimeComputerAiStructureRole>& goals) {
    using Role = RuntimeComputerAiStructureRole;
    const auto targets = runtimeComputerAiBaseTargets(state, ai);
    // Mobile and deployed forms fill the same quota; moving a slave miner
    // must not make the construction yard order an additional one.
    std::array<int, 9> remaining{};
    remaining[static_cast<std::size_t>(Role::Power)] = metrics.powerPlants;
    remaining[static_cast<std::size_t>(Role::Refinery)] = targets.slaveRefineries
        ? metrics.slaveMiners : metrics.ordinaryRefineries;
    remaining[static_cast<std::size_t>(Role::Barracks)] = metrics.barracks;
    remaining[static_cast<std::size_t>(Role::VehicleFactory)] = metrics.vehicleFactories;
    remaining[static_cast<std::size_t>(Role::Radar)] = metrics.radars;
    remaining[static_cast<std::size_t>(Role::Defense)] = metrics.defenses;
    remaining[static_cast<std::size_t>(Role::Tech)] = metrics.techCenters;
    remaining[static_cast<std::size_t>(Role::Superweapon)] = metrics.superweaponStructures;
    for (const Role role : runtimeComputerAiBasePlan(ai, targets)) {
        auto& count = remaining[static_cast<std::size_t>(role)];
        if (count > 0) { --count; continue; }
        if (role == Role::Superweapon && (!state.config.superweapons
                || metrics.structures < 8 || metrics.combatUnits < profile.attackThreshold)) continue;
        if (std::find(goals.begin(), goals.end(), role) == goals.end()) goals.push_back(role);
    }
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectStructure(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics) {
    if (metrics.constructionYards <= 0) {
        return std::nullopt;
    }
    std::vector<RuntimeComputerAiStructureRole> goals;
    const int powerNet = metrics.powerProduced - metrics.powerUsed;
    if (metrics.powerPlants <= 0 || powerNet < 0) {
        goals.push_back(RuntimeComputerAiStructureRole::Power);
    }
    if (metrics.shipyards <= 0 && (runtimeIsNavalWarMode(state) || metrics.vehicleFactories > 0)) {
        const auto yard = runtimeComputerAiSelectStructureForRole(
            state, ai, RuntimeComputerAiStructureRole::Shipyard);
        if (yard && findRuntimePlacementCellNear(state, *yard, ai.baseCenter, 48, ai.owner))
            goals.push_back(RuntimeComputerAiStructureRole::Shipyard);
    }
    if (powerNet < 75 || (metrics.powerPlants < 2 && metrics.powerUsed > 100)) {
        goals.push_back(RuntimeComputerAiStructureRole::Power);
    }
    appendRuntimeComputerAiBasePlanGoals(state, ai, profile, metrics, goals);
    for (RuntimeComputerAiStructureRole role : goals) {
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiSelectStructureForRole(state, ai, role);
        if (item.has_value()) {
            return item;
        }
    }
    return std::nullopt;
}

std::vector<std::string> runtimeComputerAiUnitCandidates(
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiUnitRole role) {
    const bool yuri = runtimeCountryIsYuri(ai.country);
    const bool soviet = runtimeCountryIsSoviet(ai.country);
    if (role == RuntimeComputerAiUnitRole::Engineer) {
        return yuri ? std::vector<std::string>{"YENGINEER"}
            : soviet ? std::vector<std::string>{"SENGINEER"}
            : std::vector<std::string>{"ENGINEER"};
    }
    if (role == RuntimeComputerAiUnitRole::Naval) {
        return yuri ? std::vector<std::string>{"BSUB", "SQD"}
            : soviet ? std::vector<std::string>{"SUB", "HYD", "DRED", "SQD"}
            : std::vector<std::string>{"DEST", "AEGIS", "CARRIER", "DLPH"};
    }
    if (role == RuntimeComputerAiUnitRole::Harvester) {
        return yuri ? std::vector<std::string>{"SMIN"}
            : soviet ? std::vector<std::string>{"HARV"}
            : std::vector<std::string>{"CMIN"};
    }
    if (role == RuntimeComputerAiUnitRole::Infantry) {
        return yuri ? std::vector<std::string>{"INIT", "BRUTE", "VIRUS", "YURI"}
            : soviet ? std::vector<std::string>{"E2", "FLAKT", "DOG", "SHK"}
            : std::vector<std::string>{"E1", "GGI", "ADOG", "JUMPJET"};
    }
    return yuri ? std::vector<std::string>{"LTNK", "YTNK", "TELE", "MIND", "DISK"}
        : soviet ? std::vector<std::string>{"HTNK", "HTK", "V3", "APOC"}
        : std::vector<std::string>{"MTNK", "FV", "SREF", "MGTK"};
}

bool runtimeComputerAiUnitMatchesRole(
    const RulesDatabase& rules,
    const UnitType& unit,
    RuntimeComputerAiUnitRole role) {
    if (unit.airportBound || unit.spawned || unit.slaved) return false;
    if (role == RuntimeComputerAiUnitRole::Engineer) return unit.engineer;
    if (unit.engineer) return false;
    if (role == RuntimeComputerAiUnitRole::Naval)
        return unit.naval && !unit.harvester && (!unit.primaryWeapon.empty()
            || !unit.secondaryWeapon.empty() || !unit.indexedWeapons.empty());
    if (unit.naval || unit.waterBound) return false;
    if (role == RuntimeComputerAiUnitRole::Harvester) {
        return unit.harvester || unit.resourceGatherer;
    }
    if (unit.harvester || unit.resourceGatherer) {
        return false;
    }
    const bool infantry = runtimeBuildKindForUnitTypeId(rules, unit.id)
        == RuntimeBuildKind::Infantry;
    if ((role == RuntimeComputerAiUnitRole::Infantry) != infantry) {
        return false;
    }
    return !unit.primaryWeapon.empty()
        || !unit.secondaryWeapon.empty()
        || !unit.indexedWeapons.empty();
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectUnitForRole(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    RuntimeComputerAiUnitRole role) {
    const std::string aiOwner = runtimeRepairDepotCanonicalOwnerId(
        state,
        ai.owner
    );
    for (const RuntimeSpecialBuildingUnlock& unlock :
            RuntimeSpecialBuildingSystem::secretLabUnlocks(
                makeRuntimeSpecialBuildingContext(state)
            )) {
        const bool infantryUnlock = unlock.kind
            == RuntimeSpecialBuildingUnlockKind::Infantry;
        const bool unitUnlock = unlock.kind
            == RuntimeSpecialBuildingUnlockKind::Unit;
        if ((!infantryUnlock && !unitUnlock)
            || runtimeRepairDepotCanonicalOwnerId(state, unlock.owner)
                != aiOwner) {
            continue;
        }
        const UnitType* unit = findRuntimeUnitTypeById(
            state.rules,
            unlock.typeId
        );
        if (unit == nullptr
            || !runtimeComputerAiUnitMatchesRole(state.rules, *unit, role)) {
            continue;
        }
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(state, unlock.typeId, ai.owner);
        if (item.has_value()
            && runtimeComputerAiItemAllowed(state, ai, *item)) {
            return item;
        }
    }
    const std::vector<std::string> candidates =
        runtimeComputerAiUnitCandidates(ai, role);
    for (std::size_t offset = 0; offset < candidates.size(); ++offset) {
        const std::size_t index = (
            static_cast<std::size_t>(std::max(0, ai.productionSerial)) + offset
        ) % candidates.size();
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(
                state,
                candidates[index],
                ai.owner
            );
        if (item.has_value() && runtimeComputerAiItemAllowed(state, ai, *item)) {
            return item;
        }
    }
    const auto scan = [&](const std::vector<UnitType>& units)
        -> std::optional<RuntimeBuildCatalogItem> {
        for (const UnitType& unit : units) {
            if (!runtimeComputerAiUnitMatchesRole(state.rules, unit, role)) {
                continue;
            }
            const std::optional<RuntimeBuildCatalogItem> item =
                runtimeComputerAiBuildItem(state, unit.id, ai.owner);
            if (item.has_value()
                && runtimeComputerAiItemAllowed(state, ai, *item)) {
                return item;
            }
        }
        return std::nullopt;
    };
    if (role == RuntimeComputerAiUnitRole::Infantry || role == RuntimeComputerAiUnitRole::Engineer) {
        return scan(state.rules.getInfantryTypes());
    }
    return scan(state.rules.getVehicleTypes());
}

std::vector<int> runtimeComputerAiNeutralOilTargets(const RuntimeGameSessionState& state) {
    std::vector<int> targets;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || !runtimeOwnerIsNeutralForAutoTarget(object.owner)) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr && building->capturable
            && building->produceCashAmount > 0 && building->produceCashDelay > 0)
            targets.push_back(object.instanceId);
    }
    std::sort(targets.begin(), targets.end());
    return targets;
}

bool runtimeComputerAiNeedsOilEngineer(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai) {
    const auto targets = runtimeComputerAiNeutralOilTargets(state);
    if (targets.empty()) return false;
    int engineers = 0;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeComputerAiOwnerMatches(object, ai.owner)
            && runtimeObjectIsEngineer(state, object)) ++engineers;
    return engineers < std::min(2, static_cast<int>(targets.size()));
}

bool advanceRuntimeComputerAiOilCapture(RuntimeProductionHostState& state,
    const RuntimeComputerAiState& ai) {
    if (!runtimeIsMegawealthMode(state)) return false;
    auto targets = runtimeComputerAiNeutralOilTargets(state);
    if (targets.empty()) return false;
    std::set<int> reserved;
    for (const auto& [engineerId, targetId] : state.pendingEngineerCaptures) {
        const auto* engineer = findObjectById(state, engineerId);
        if (engineer != nullptr && engineer->alive
            && runtimeComputerAiOwnerMatches(*engineer, ai.owner)) reserved.insert(targetId);
    }
    std::vector<int> engineers;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeComputerAiOwnerMatches(object, ai.owner)
            && runtimeObjectIsEngineer(state, object) && runtimeObjectCanMove(state, object)
            && !state.pendingEngineerCaptures.contains(object.instanceId))
            engineers.push_back(object.instanceId);
    std::sort(engineers.begin(), engineers.end());
    bool changed = false;
    for (int id : engineers) {
        const auto* engineer = findObjectById(state, id);
        if (engineer == nullptr) continue;
        const auto origin = runtimeCellPointForObject(*engineer);
        std::sort(targets.begin(), targets.end(), [&](int a, int b) {
            const auto* left = findObjectById(state, a);
            const auto* right = findObjectById(state, b);
            const double da = runtimeCellDistance(origin, runtimeCellPointForObject(*left));
            const double db = runtimeCellDistance(origin, runtimeCellPointForObject(*right));
            return da != db ? da < db : a < b;
        });
        for (int targetId : targets) {
            if (reserved.contains(targetId)) continue;
            const auto* target = findObjectById(state, targetId);
            if (target != nullptr && runtimeEngineerCanCaptureStructure(state, *engineer, *target)
                && queueRuntimeEngineerCaptureForObject(state, id, *target, false)) {
                reserved.insert(targetId);
                changed = true;
                break;
            }
        }
    }
    return changed;
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectNeededHarvester(
    const RuntimeGameSessionState& state, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiMetrics& metrics) {
    if (!runtimeIsMegawealthMode(state)) {
        const auto harvester = runtimeComputerAiSelectUnitForRole(
            state, ai, RuntimeComputerAiUnitRole::Harvester);
        if (harvester) {
            const auto* unit = findRuntimeUnitTypeById(state.rules, harvester->typeId);
            const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
            const auto& mining = state.rules.getComputerMiningRules();
            const int desired = unit != nullptr && runtimeUnitIsMobileSlaveMiner(*unit)
                ? mining.slaveMinerNumber[difficulty]
                : metrics.refineries * mining.harvestersPerRefinery[difficulty];
            if (metrics.harvesters < desired) return harvester;
        }
    }
    return std::nullopt;
}

std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectUnit(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics) {
    if (runtimeIsMegawealthMode(state) && runtimeComputerAiNeedsOilEngineer(state, ai)) {
        const auto engineer = runtimeComputerAiSelectUnitForRole(
            state, ai, RuntimeComputerAiUnitRole::Engineer);
        if (engineer) return engineer;
    }
    if (const auto miner = runtimeComputerAiSelectNeededHarvester(state, ai, metrics)) return miner;
    if (metrics.combatUnits >= profile.maximumCombatUnits) {
        return std::nullopt;
    }
    if (runtimeIsNavalWarMode(state) && metrics.shipyards > 0) {
        const auto ship = runtimeComputerAiSelectUnitForRole(state, ai, RuntimeComputerAiUnitRole::Naval);
        if (ship) return ship;
    }
    const bool preferVehicle = ai.productionSerial % 3 == 2;
    const RuntimeComputerAiUnitRole first = preferVehicle
        ? RuntimeComputerAiUnitRole::Vehicle
        : RuntimeComputerAiUnitRole::Infantry;
    const RuntimeComputerAiUnitRole second = preferVehicle
        ? RuntimeComputerAiUnitRole::Infantry
        : RuntimeComputerAiUnitRole::Vehicle;
    const std::optional<RuntimeBuildCatalogItem> preferred =
        runtimeComputerAiSelectUnitForRole(state, ai, first);
    return preferred.has_value()
        ? preferred
        : runtimeComputerAiSelectUnitForRole(state, ai, second);
}

int runtimeComputerAiExpectedMaxHealth(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const WorldObjectInstance& object) {
    WorldObjectInstance ownerAdjusted = object;
    ownerAdjusted.owner = ai.owner;
    return runtimeMaxHealthForObject(state, ownerAdjusted);
}

void scaleRuntimeComputerAiObjectHealth(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    WorldObjectInstance& object) {
    object.health = runtimeComputerAiExpectedMaxHealth(state, ai, object);
}

std::optional<RuntimeCellPoint> runtimeComputerAiSpawnCellNearAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    int serial) {
    WorldObjectInstance probe;
    probe.instanceId = -900000 - std::max(0, serial);
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.owner = ai.owner;
    probe.type = item.typeId;
    probe.alive = true;
    if (runtimeFactoryUsesAirExit(state, anchor, probe)) {
        return runtimeFactoryAirExitSpawnCell(state, anchor);
    }
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, anchor);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const RuntimeCellPoint origin = runtimeCellPointForObject(anchor);
    const std::array<RuntimeCellPoint, 8> candidates{{
        {origin.x + width, origin.y + height / 2},
        {origin.x + width, origin.y + height},
        {origin.x + width / 2, origin.y + height},
        {origin.x - 1, origin.y + height / 2},
        {origin.x + width / 2, origin.y - 1},
        {origin.x + width + 1, origin.y + height / 2},
        {origin.x - 1, origin.y + height},
        {origin.x + width, origin.y - 1}
    }};
    for (std::size_t offset = 0; offset < candidates.size(); ++offset) {
        const std::size_t index = (
            static_cast<std::size_t>(std::max(0, serial)) + offset
        ) % candidates.size();
        const std::optional<RuntimeCellPoint> cell = findRuntimePassableCellNear(
            state,
            probe,
            candidates[index],
            3
        );
        if (cell.has_value()) {
            return cell;
        }
    }
    return std::nullopt;
}

const WorldObjectInstance* runtimeComputerAiFactoryAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item) {
    const WorldObjectInstance* best = nullptr;
    const std::string required = toUpperAsciiLocal(item.requiredFactory);
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeComputerAiOwnerMatches(object, ai.owner)) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr
            || toUpperAsciiLocal(building->factory) != required
            || !runtimeFactoryOwnerCompatibleWithBuildItem(
                state,
                object,
                item
            )
            || (required == "UNITTYPE"
                && (building->naval || building->waterBound)
                    != runtimeBuildItemLooksNaval(state, item))) {
            continue;
        }
        if (best == nullptr || object.instanceId < best->instanceId) {
            best = &object;
        }
    }
    return best;
}

bool runtimeComputerAiBuildStructure(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item) {
    static constexpr std::array<RuntimeCellPoint, 12> placementOffsets{{
        {0, 0}, {5, 0}, {-5, 0}, {0, 5}, {0, -5},
        {7, 4}, {-7, 4}, {7, -4}, {-7, -4},
        {10, 0}, {0, 10}, {-10, 0}
    }};
    const RuntimeCellPoint offset = placementOffsets[
        static_cast<std::size_t>(std::max(0, ai.productionSerial))
            % placementOffsets.size()
    ];
    const RuntimeCellPoint wanted{
        ai.baseCenter.x + offset.x,
        ai.baseCenter.y + offset.y
    };
    const std::optional<RuntimeCellPoint> placement =
        findRuntimePlacementCellNear(state, item,
            item.waterBound ? ai.baseCenter : wanted,
            item.waterBound ? 48 : 18, ai.owner, true);
    if (!placement.has_value()) {
        ++ai.stalledBuilds;
        ai.status = "placement blocked for " + item.typeId;
        return false;
    }
    WorldObjectInstance* structure = addRuntimeObjectInstanceForOwner(
        state, ai.owner, item.kind, item.typeId, *placement
    );
    if (structure == nullptr) {
        ++ai.stalledBuilds;
        ai.status = "failed to create " + item.typeId;
        return false;
    }
    structure->mission = "Construction";
    recordRuntimeCompletedBuild(state, ai.owner, item);
    scaleRuntimeComputerAiObjectHealth(state, ai, *structure);
    const int structureId = structure->instanceId;
    const WorldObjectInstance structureSnapshot = *structure;
    ++ai.structuresBuilt;

    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, item.typeId);
    if (building != nullptr
        && !building->freeUnit.empty()
        && findRuntimeUnitTypeById(state.rules, building->freeUnit) != nullptr) {
        (void)state.factoryDeliveries.deferFreeUnit({structureId,
            ai.owner, structureSnapshot.type, building->freeUnit});
        const std::optional<RuntimeBuildCatalogItem> freeUnitItem =
            runtimeComputerAiBuildItem(
                state,
                building->freeUnit,
                ai.owner
            );
        const std::optional<RuntimeCellPoint> freeCell =
            freeUnitItem.has_value()
                ? runtimeComputerAiSpawnCellNearAnchor(
                    state,
                    ai,
                    *freeUnitItem,
                    structureSnapshot,
                    ai.productionSerial
                )
                : std::nullopt;
        if (freeUnitItem.has_value() && freeCell.has_value()) {
            WorldObjectInstance* freeUnit =
                addRuntimeObjectInstanceForOwner(
                state,
                ai.owner,
                freeUnitItem->kind,
                freeUnitItem->typeId,
                *freeCell
            );
            if (freeUnit != nullptr) {
                freeUnit->mission = "Guard";
                applyRuntimeProducedObjectVeterancy(state, *freeUnit);
                recordRuntimeCompletedBuild(
                    state,
                    ai.owner,
                    *freeUnitItem
                );
                scaleRuntimeComputerAiObjectHealth(state, ai, *freeUnit);
                const int freeUnitId = freeUnit->instanceId;
                if (WorldObjectInstance* liveFreeUnit =
                        findMutableObjectById(state, freeUnitId)) {
                    if (const WorldObjectInstance* liveStructure =
                            findObjectById(state, structureId)) {
                        queueRuntimeProductionExitMove(
                            state, *liveFreeUnit, *liveStructure
                        );
                    }
                }
                state.factoryDeliveries.completeFreeUnit(structureId);
                ++ai.freeUnitsReceived;
            }
        }
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    state.powerRefreshPending = true;
    ai.lastBuildType = item.typeId;
    ai.status = "built " + item.typeId;
    state.simulationStatus = ai.owner + " AI built " + item.typeId;
    return true;
}

bool runtimeComputerAiBuildUnit(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeBuildCatalogItem& item,
    int* producedId) {
    const WorldObjectInstance* anchor =
        runtimeComputerAiFactoryAnchor(state, ai, item);
    if (anchor == nullptr) {
        ++ai.stalledBuilds;
        ai.status = "missing production anchor for " + item.typeId;
        return false;
    }
    const int anchorId = anchor->instanceId;
    if (requestRuntimeProductionDoorClearance(state, item, *anchor, ai.owner)) {
        ai.status = "clearing production exit for " + item.typeId;
        return false;
    }
    const std::optional<RuntimeCellPoint> spawn =
        runtimeComputerAiSpawnCellNearAnchor(
            state, ai, item, *anchor, ai.productionSerial
        );
    if (!spawn.has_value()) {
        ++ai.stalledBuilds;
        ai.status = "factory exit blocked for " + item.typeId;
        return false;
    }
    WorldObjectInstance* unit = addRuntimeObjectInstanceForOwner(
        state, ai.owner, item.kind, item.typeId, *spawn
    );
    if (unit == nullptr) {
        ++ai.stalledBuilds;
        return false;
    }
    unit->mission = "Guard";
    applyRuntimeProducedObjectVeterancy(state, *unit);
    recordRuntimeCompletedBuild(state, ai.owner, item);
    scaleRuntimeComputerAiObjectHealth(state, ai, *unit);
    const int unitId = unit->instanceId;
    if (producedId != nullptr) *producedId = unitId;
    if (WorldObjectInstance* liveUnit = findMutableObjectById(state, unitId)) {
        if (WorldObjectInstance* liveFactory =
                findMutableObjectById(state, anchorId)) {
            beginRuntimeFactoryExitAnimation(state, *liveFactory, *liveUnit);
            queueRuntimeProductionExitMove(state, *liveUnit, *liveFactory);
        }
    }
    if (item.kind == RuntimeBuildKind::Infantry) {
        ++ai.infantryBuilt;
        (void)cloneRuntimeProducedInfantry(
            state,
            ai.owner,
            item.typeId,
            anchorId
        );
    } else {
        ++ai.unitsBuilt;
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    ai.lastBuildType = item.typeId;
    ai.status = "produced " + item.typeId;
    state.simulationStatus = ai.owner + " AI produced " + item.typeId;
    return true;
}

int runtimeComputerTacticLane(const RuntimeBuildCatalogItem& item) {
    return item.kind == RuntimeBuildKind::Infantry ? 0
        : toUpperAsciiLocal(item.requiredFactory) == "AIRCRAFTTYPE" ? 2 : 1;
}

std::optional<RuntimeProductionSpawn> runtimeComputerTacticAircraftSpawn(
    const RuntimeGameSessionState& state, const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    RuntimeAircraftProductionOrder order;
    order.serial = 1; order.typeId = item.typeId;
    order.phase = RuntimeAircraftProductionOrderPhase::Prospective;
    const auto build = buildRuntimeAircraftProductionMatchInputForOwner(
        state, owner, order, nullptr, false);
    if (build.error) return std::nullopt;
    const auto match = runtimeMatchAircraftProductionDocks(build.input);
    if (!match.feasible) return std::nullopt;
    for (const auto& assignment : match.assignments) {
        if (assignment.orderSerial != order.serial) continue;
        const auto* host = findObjectById(state, assignment.hostId);
        const auto* rule = host ? findRuntimeBuildingTypeById(state.rules, host->type) : nullptr;
        if (host && rule && (!rule->powered || host->poweredOperational))
            return runtimeAircraftDockSpawn(state, *host, assignment.dockIndex);
    }
    return std::nullopt;
}

bool completeRuntimeComputerTacticUnit(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item, int& producedId) {
    producedId = -1;
    if (runtimeComputerTacticLane(item) != 2)
        return runtimeComputerAiBuildUnit(state, ai, item, &producedId);
    refreshRuntimeAircraftDockAssignments(state);
    const auto spawn = runtimeComputerTacticAircraftSpawn(state, ai.owner, item);
    if (!spawn || !spawn->anchor) return false;
    const int hostId = spawn->anchor->instanceId;
    const int dockIndex = spawn->dockIndex;
    auto* aircraft = addRuntimeObjectInstanceForOwner(
        state, ai.owner, item.kind, item.typeId, spawn->cell);
    if (!aircraft) return false;
    producedId = aircraft->instanceId;
    applyRuntimeProducedObjectVeterancy(state, *aircraft);
    scaleRuntimeComputerAiObjectHealth(state, ai, *aircraft);
    if (const auto* host = findObjectById(state, hostId))
        applyRuntimeAircraftDockPose(state, *aircraft, *host, dockIndex);
    recordRuntimeCompletedBuild(state, ai.owner, item);
    ++ai.unitsBuilt;
    ai.lastBuildType = item.typeId;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

bool queueRuntimeComputerTacticProduction(RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai, const RuntimeBuildCatalogItem& item,
    int teamInstanceId) {
    auto& tactics = state.triggers.computerTacticsByOwner[
        toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    auto& order = tactics.production[runtimeComputerTacticLane(item)];
    if (!order.typeId.empty() || ai.credits < item.cost
        || !runtimeComputerAiItemAllowed(state, ai, item)) return false;
    if (runtimeComputerTacticLane(item) == 2
        && !runtimeComputerTacticAircraftSpawn(state, ai.owner, item)) return false;
    order.teamInstanceId = teamInstanceId;
    order.typeId = item.typeId;
    order.remainingTicks = order.totalTicks = std::max(1, item.buildTicks);
    order.paidCost = item.cost;
    spendRuntimeComputerStoredOre(state, ai.owner, item.cost);
    ai.credits -= item.cost;
    ai.creditsSpent += item.cost;
    ++ai.productionSerial;
    ai.status = "building " + item.typeId;
    return true;
}

bool advanceRuntimeComputerTacticProduction(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics) {
    bool changed = false;
    for (auto& order : tactics.production) {
        if (order.typeId.empty()) continue;
        const auto item = runtimeComputerAiBuildItem(state, order.typeId, ai.owner);
        if (!item) continue;
        // A destroyed/captured factory or lost prerequisite pauses a paid
        // order. Completion waits for a real exit instead of losing its unit.
        if (!runtimeComputerAiItemAllowed(state, ai, *item)) continue;
        const auto* anchor = runtimeComputerAiFactoryAnchor(state, ai, *item);
        const auto* building = anchor
            ? findRuntimeBuildingTypeById(state.rules, anchor->type) : nullptr;
        if (!anchor || !building || (building->powered && !anchor->poweredOperational)) continue;
        if (runtimeComputerTacticLane(*item) == 2
            && !runtimeComputerTacticAircraftSpawn(state, ai.owner, *item)) continue;
        if (order.remainingTicks > 0) { --order.remainingTicks; changed = true; }
        if (order.remainingTicks > 0) continue;
        int objectId = -1;
        if (!completeRuntimeComputerTacticUnit(state, ai, *item, objectId)) continue;
        const auto team = tactics.teams.find(order.teamInstanceId);
        if (team != tactics.teams.end()) {
            team->second.objectIds.push_back(objectId);
            if (team->second.launched) team->second.joiningObjectIds.push_back(objectId);
            const auto missing = std::find_if(team->second.missingMembers.begin(),
                team->second.missingMembers.end(), [&](const auto& type) {
                    return toUpperAsciiLocal(type) == toUpperAsciiLocal(order.typeId);
                });
            if (missing != team->second.missingMembers.end()) team->second.missingMembers.erase(missing);
            team->second.lastProgressTick = state.tick;
        }
        order = {};
        changed = true;
    }
    return changed;
}

bool advanceRuntimeComputerAiProduction(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile,
    const RuntimeComputerAiMetrics& metrics) {
    if (state.tick < ai.nextBuildTick) {
        return false;
    }
    // Replace the mining fleet before spending the remaining money on
    // optional base expansion. Without this, a lost miner could leave an AI
    // saving forever for a tech building even though it could afford a miner.
    const bool catalogProduction = !state.triggers.computerAiTriggers.empty();
    std::optional<RuntimeBuildCatalogItem> item = catalogProduction
        ? std::nullopt : runtimeComputerAiSelectNeededHarvester(state, ai, metrics);
    if (!item) item = runtimeComputerAiSelectStructure(state, ai, profile, metrics);
    if (!item && !catalogProduction) item = runtimeComputerAiSelectUnit(state, ai, profile, metrics);
    if (!item.has_value()) {
        ai.nextBuildTick = state.tick + profile.minimumBuildTicks;
        ai.status = metrics.constructionYards <= 0
            ? "base lost"
            : "force cap reached";
        return false;
    }
    if (ai.credits < item->cost) {
        ++ai.stalledBuilds;
        ai.nextBuildTick = state.tick + profile.buildRetryTicks;
        ai.status = "saving for " + item->typeId;
        return false;
    }
    const bool structure = item->kind == RuntimeBuildKind::Building
        || item->kind == RuntimeBuildKind::Defense;
    const bool completed = structure
        ? runtimeComputerAiBuildStructure(state, ai, *item)
        : runtimeComputerAiBuildUnit(state, ai, *item);
    if (!completed) {
        ai.nextBuildTick = state.tick + profile.buildRetryTicks;
        return false;
    }
    spendRuntimeComputerStoredOre(state, ai.owner, item->cost);
    ai.credits = std::max(0, ai.credits - item->cost);
    ai.creditsSpent += item->cost;
    ++ai.productionSerial;
    ai.nextBuildTick = state.tick + std::max(
        profile.minimumBuildTicks,
        item->buildTicks
    );
    return true;
}

bool runtimeComputerAiActorCanTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    const WorldObjectInstance& target) {
    if (!attacker.alive
        || !target.alive
        || !runtimeObjectHasAttackAmmo(state.rules, attacker)
        || !runtimeObjectVisibleToOwner(state, attacker.owner, target)
        || runtimeSpyDisguiseConcealsFromObject(
            state,
            attacker,
            target
        )) {
        return false;
    }
    const RuntimeWeaponProfile profile = runtimeWeaponProfileForObject(
        state,
        attacker,
        &target
    );
    return profile.weapon != nullptr
        && (profile.weapon->damage > 0 || runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
        && runtimeWeaponHasAttackRange(*profile.weapon)
        && runtimePsychicCanAttackTarget(
            state,
            attacker,
            target,
            true,
            runtimeOwnersAreHostileForAutoTarget(
                state,
                attacker.owner,
                target.owner
            )
        );
}

const WorldObjectInstance* runtimeComputerAiStrategicTarget(
    const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai,
    const std::vector<int>* actorIds) {
    const WorldObjectInstance* best = nullptr;
    long long bestScore = std::numeric_limits<long long>::min();
    for (const WorldObjectInstance& target : state.world.getObjects()) {
        bool relationLegal = runtimeOwnersAreHostileForAutoTarget(
            state,
            ai.owner,
            target.owner
        );
        if (!relationLegal && actorIds != nullptr) {
            for (int actorId : *actorIds) {
                const WorldObjectInstance* actor = findObjectById(
                    state,
                    actorId
                );
                if (actor != nullptr
                    && runtimeComputerAiActorCanTarget(
                        state,
                        *actor,
                        target
                    )) {
                    relationLegal = true;
                    break;
                }
            }
        }
        bool spyConcealed = runtimeSpyDisguiseConcealsFromOwner(
            state,
            ai.owner,
            target
        );
        if (spyConcealed && actorIds != nullptr) {
            for (int actorId : *actorIds) {
                const WorldObjectInstance* actor = findObjectById(
                    state,
                    actorId
                );
                if (actor != nullptr
                    && !runtimeSpyDisguiseConcealsFromObject(
                        state,
                        *actor,
                        target
                    )) {
                    spyConcealed = false;
                    break;
                }
            }
        }
        if (!target.alive
            || !relationLegal
            || runtimeObjectHiddenFromOwnerByStealth(
                state,
                ai.owner,
                target
            )
            || spyConcealed) {
            continue;
        }
        long long score = target.kind == WorldObjectKind::Structure
            ? 5000LL
            : 1000LL;
        if (target.kind == WorldObjectKind::Structure) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, target.type);
            if (building != nullptr) {
                score += building->constructionYard ? 6000 : 0;
                score += !building->factory.empty() ? 2500 : 0;
                score += (!buildingSuperWeapons(*building).empty()) ? 4000 : 0;
                score += building->isBaseDefense ? 750 : 0;
            }
        }
        score -= static_cast<long long>(std::lround(
            runtimeCellDistance(
                ai.baseCenter,
                runtimeCellPointForObject(target)
            ) * 10.0
        ));
        if (best == nullptr
            || score > bestScore
            || (score == bestScore && target.instanceId < best->instanceId)) {
            best = &target;
            bestScore = score;
        }
    }
    return best;
}

bool runtimeComputerAiIssueAttack(
    RuntimeProductionHostState& state,
    WorldObjectInstance& attacker,
    const WorldObjectInstance& target,
    bool autoAcquired,
    int approachSlot) {
    if (!attacker.alive
        || !target.alive
        || !runtimeObjectVisibleToOwner(state, attacker.owner, target)
        || runtimeSpyDisguiseConcealsFromObject(
            state,
            attacker,
            target
        )) {
        return false;
    }
    const RuntimeWeaponProfile profile =
        runtimeWeaponProfileForObject(state, attacker, &target);
    if (!runtimeObjectHasAttackAmmo(state.rules, attacker)
        || profile.weapon == nullptr
        || (profile.weapon->damage <= 0 && !runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
        || !runtimeWeaponHasAttackRange(*profile.weapon)) {
        return false;
    }
    if (!runtimePsychicCanAttackTarget(
            state,
            attacker,
            target,
            true,
            runtimeOwnersAreHostileForAutoTarget(
                state,
                attacker.owner,
                target.owner
            )
        )) {
        return false;
    }
    const double distance = runtimeWeaponDistanceToTarget(
        state,
        attacker,
        target,
        *profile.weapon
    );
    const double attackRange = runtimeEffectiveWeaponRange(
        state,
        attacker,
        target,
        *profile.weapon
    );
    const bool hasLineOfSight = runtimeWeaponHasLineOfSight(
        state,
        attacker,
        target,
        *profile.weapon
    );
    const bool returnToDeployed = runtimeObjectHasDeployedState(state, attacker);
    departRuntimeAircraftDock(state, attacker);
    eraseRuntimeBehavior(state, attacker.instanceId);
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Attack;
    behavior.state = distance > attackRange || !hasLineOfSight
        ? RuntimeBehaviorState::Chasing
        : RuntimeBehaviorState::Queued;
    behavior.targetObjectId = target.instanceId;
    behavior.autoAcquired = autoAcquired;
    behavior.returnToDeployed = returnToDeployed;
    behavior.approachSlotIndex = approachSlot;
    attacker.mission = returnToDeployed
        ? "DeployedFire" : runtimeMissionName(RuntimeMissionIntent::Attacking);
    state.behaviors[attacker.instanceId] = std::move(behavior);
    return true;
}

bool runtimeComputerAiIssueMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& object,
    const RuntimeCellPoint& target,
    const std::unordered_set<int>& groupIds) {
    if (!object.alive || !runtimeObjectCanMove(state, object)) {
        return false;
    }
    eraseRuntimeBehavior(state, object.instanceId);
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = target;
    behavior.pendingIgnoredObjectIds = groupIds;
    object.mission = runtimeMissionName(RuntimeMissionIntent::Moving);
    state.behaviors[object.instanceId] = std::move(behavior);
    return true;
}

const WorldObjectInstance* runtimeComputerAiVisibleTargetNear(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& attacker,
    double maximumDistance) {
    if (!runtimeObjectHasAttackAmmo(state.rules, attacker)) {
        return nullptr;
    }
    const WorldObjectInstance* best = nullptr;
    double bestDistance = maximumDistance + 0.001;
    for (const WorldObjectInstance& target : state.world.getObjects()) {
        if (!target.alive
            || !runtimeObjectVisibleToOwner(state, attacker.owner, target)
            || runtimeSpyDisguiseConcealsFromObject(
                state,
                attacker,
                target
            )) {
            continue;
        }
        const RuntimeWeaponProfile profile =
            runtimeWeaponProfileForObject(state, attacker, &target);
        if (profile.weapon == nullptr
            || (profile.weapon->damage <= 0 && !runtimeWeaponHasAttachEffect(state.rules, profile.weapon))
            || !runtimeWeaponHasAttackRange(*profile.weapon)) {
            continue;
        }
        if (!runtimePsychicCanAttackTarget(
                state,
                attacker,
                target,
                true,
                runtimeOwnersAreHostileForAutoTarget(
                    state,
                    attacker.owner,
                    target.owner
                )
            )) {
            continue;
        }
        const double distance = runtimeCellDistanceToObjectFootprint(
            state,
            runtimeCellPointForObject(attacker),
            target
        );
        if (distance <= bestDistance
            && (best == nullptr
                || distance < bestDistance
                || target.instanceId < best->instanceId)) {
            best = &target;
            bestDistance = distance;
        }
    }
    return best;
}

bool advanceRuntimeComputerAiDefense(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile) {
    const WorldObjectInstance* intruder = nullptr;
    double bestBaseDistance = 18.001;
    for (const WorldObjectInstance& target : state.world.getObjects()) {
        bool actorSpecificTarget = false;
        if (target.alive) {
            for (const WorldObjectInstance& defender :
                    state.world.getObjects()) {
                if (!runtimeComputerAiOwnerMatches(defender, ai.owner)
                    || !runtimeComputerAiObjectCanFight(state, defender)
                    || runtimeComputerTacticReservesObject(state, defender.instanceId)
                    || runtimeCellDistance(
                        runtimeCellPointForObject(defender),
                        runtimeCellPointForObject(target)
                    ) > 28.0) {
                    continue;
                }
                if (runtimeComputerAiActorCanTarget(
                        state,
                        defender,
                        target
                    )) {
                    actorSpecificTarget = true;
                    break;
                }
            }
        }
        if (!target.alive
            || !actorSpecificTarget
            || !runtimeObjectVisibleToOwner(state, ai.owner, target)) {
            continue;
        }
        for (const WorldObjectInstance& structure : state.world.getObjects()) {
            if (!structure.alive
                || structure.kind != WorldObjectKind::Structure
                || !runtimeComputerAiOwnerMatches(structure, ai.owner)) {
                continue;
            }
            const double distance = runtimeCellDistanceToObjectFootprint(
                state,
                runtimeCellPointForObject(structure),
                target
            );
            if (distance < bestBaseDistance
                || (std::abs(distance - bestBaseDistance) < 0.001
                    && (intruder == nullptr
                        || target.instanceId < intruder->instanceId))) {
                intruder = &target;
                bestBaseDistance = distance;
            }
        }
    }
    if (intruder == nullptr) {
        return false;
    }
    std::vector<int> defenders;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (runtimeComputerAiOwnerMatches(object, ai.owner)
            && runtimeComputerAiObjectCanFight(state, object)
            && !runtimeComputerTacticReservesObject(state, object.instanceId)
            && runtimeComputerAiActorCanTarget(
                state,
                object,
                *intruder
            )
            && runtimeCellDistance(
                runtimeCellPointForObject(object),
                runtimeCellPointForObject(*intruder)
            ) <= 28.0) {
            const auto behavior = state.behaviors.find(object.instanceId);
            if (behavior == state.behaviors.end()
                || behavior->second.kind != RuntimeBehaviorKind::Attack) {
                defenders.push_back(object.instanceId);
            }
        }
    }
    std::sort(defenders.begin(), defenders.end(), [&](int leftId, int rightId) {
        const WorldObjectInstance* left = findObjectById(state, leftId);
        const WorldObjectInstance* right = findObjectById(state, rightId);
        const double leftDistance = left == nullptr
            ? std::numeric_limits<double>::max()
            : runtimeCellDistance(
                runtimeCellPointForObject(*left),
                runtimeCellPointForObject(*intruder)
            );
        const double rightDistance = right == nullptr
            ? std::numeric_limits<double>::max()
            : runtimeCellDistance(
                runtimeCellPointForObject(*right),
                runtimeCellPointForObject(*intruder)
            );
        return leftDistance != rightDistance
            ? leftDistance < rightDistance
            : leftId < rightId;
    });
    int ordered = 0;
    for (int objectId : defenders) {
        if (ordered >= profile.maximumDefenders) {
            break;
        }
        WorldObjectInstance* defender = findMutableObjectById(state, objectId);
        if (defender != nullptr
            && runtimeComputerAiIssueAttack(
                state, *defender, *intruder, true, ordered
            )) {
            ++ordered;
        }
    }
    ai.defenseOrders += ordered;
    if (ordered > 0) {
        ai.status = "defending against #"
            + std::to_string(intruder->instanceId);
    }
    return ordered > 0;
}

bool advanceRuntimeComputerAiTactics(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai) {
    std::vector<int> combatIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!runtimeComputerAiOwnerMatches(object, ai.owner)
            || !runtimeComputerAiObjectCanFight(state, object)
            || runtimeComputerTacticReservesObject(state, object.instanceId)) {
            continue;
        }
        const auto behavior = state.behaviors.find(object.instanceId);
        if (behavior != state.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Attack) {
            continue;
        }
        combatIds.push_back(object.instanceId);
    }
    std::sort(combatIds.begin(), combatIds.end());
    int ordered = 0;
    for (int objectId : combatIds) {
        if (ordered >= 8) {
            break;
        }
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr) {
            continue;
        }
        const WorldObjectInstance* target =
            runtimeComputerAiVisibleTargetNear(state, *object, 12.0);
        if (target != nullptr
            && runtimeComputerAiIssueAttack(
                state, *object, *target, true, ordered
            )) {
            ++ordered;
        }
    }
    ai.tacticalOrders += ordered;
    return ordered > 0;
}

bool advanceRuntimeComputerAiAttackWave(
    RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile) {
    if (state.tick < ai.nextAttackTick) {
        return false;
    }
    std::vector<int> attackers;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!runtimeComputerAiOwnerMatches(object, ai.owner)
            || !runtimeComputerAiObjectCanFight(state, object)
            || runtimeComputerTacticReservesObject(state, object.instanceId)
            || state.behaviors.find(object.instanceId) != state.behaviors.end()) {
            continue;
        }
        attackers.push_back(object.instanceId);
    }
    std::sort(attackers.begin(), attackers.end());
    if (static_cast<int>(attackers.size()) < profile.attackThreshold) {
        ai.nextAttackTick = state.tick + profile.attackRetryTicks;
        return false;
    }
    if (static_cast<int>(attackers.size()) > profile.attackWaveSize) {
        attackers.resize(static_cast<std::size_t>(profile.attackWaveSize));
    }
    const WorldObjectInstance* target =
        runtimeComputerAiStrategicTarget(state, ai, &attackers);
    if (target == nullptr) {
        ai.nextAttackTick = state.tick + profile.attackRetryTicks;
        return false;
    }
    std::unordered_set<int> groupIds(attackers.begin(), attackers.end());
    int ordered = 0;
    for (std::size_t index = 0; index < attackers.size(); ++index) {
        WorldObjectInstance* attacker =
            findMutableObjectById(state, attackers[index]);
        if (attacker == nullptr) {
            continue;
        }
        bool accepted = false;
        const bool spyConcealedFromActor =
            runtimeSpyDisguiseConcealsFromObject(
                state,
                *attacker,
                *target
            );
        const bool targetVisibleToActor =
            runtimeObjectVisibleToOwner(state, ai.owner, *target)
            && !spyConcealedFromActor;
        if (targetVisibleToActor) {
            accepted = runtimeComputerAiIssueAttack(
                state,
                *attacker,
                *target,
                true,
                static_cast<int>(index)
            );
        }
        if (!accepted && !spyConcealedFromActor) {
            const int ringX = static_cast<int>(index % 3U) - 1;
            const int ringY = static_cast<int>(index / 3U) - 1;
            const RuntimeCellPoint desired{
                target->x + ringX * 3,
                target->y + ringY * 3
            };
            const std::optional<RuntimeCellPoint> approach =
                findRuntimePassableCellNear(
                    state,
                    *attacker,
                    desired,
                    6
                );
            if (approach.has_value()) {
                accepted = runtimeComputerAiIssueMove(
                    state, *attacker, *approach, groupIds
                );
            }
        }
        if (accepted) {
            ++ordered;
        }
    }
    ai.nextAttackTick = state.tick + profile.attackIntervalTicks;
    if (ordered <= 0) {
        return false;
    }
    ++ai.attackWaves;
    ai.attacksOrdered += ordered;
    ai.lastTargetObjectId = target->instanceId;
    ai.status = "attack wave -> #" + std::to_string(target->instanceId);
    state.simulationStatus = ai.owner + " AI launched attack wave";
    return true;
}

bool advanceRuntimeComputerAiRepair(
    RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile) {
    if (state.tick < ai.nextRepairTick) {
        return false;
    }
    ai.nextRepairTick = state.tick + profile.repairIntervalTicks;
    WorldObjectInstance* repairTarget = nullptr;
    double lowestRatio = 1.0;
    int expectedMaximum = 0;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || !runtimeComputerAiOwnerMatches(object, ai.owner)) {
            continue;
        }
        const int maximum = runtimeComputerAiExpectedMaxHealth(state, ai, object);
        if (object.health >= maximum) {
            continue;
        }
        const double ratio = static_cast<double>(object.health)
            / static_cast<double>(std::max(1, maximum));
        if (repairTarget == nullptr
            || ratio < lowestRatio
            || (std::abs(ratio - lowestRatio) < 0.0001
                && object.instanceId < repairTarget->instanceId)) {
            repairTarget = &object;
            lowestRatio = ratio;
            expectedMaximum = maximum;
        }
    }
    if (repairTarget == nullptr || ai.credits <= 0) {
        return false;
    }
    const int desiredHeal = std::min(
        expectedMaximum - repairTarget->health,
        std::max(1, expectedMaximum / 20)
    );
    const int affordableHeal = std::min(desiredHeal, ai.credits * 2);
    if (affordableHeal <= 0) {
        return false;
    }
    const int cost = std::max(1, (affordableHeal + 1) / 2);
    spendRuntimeComputerStoredOre(state, ai.owner, cost);
    ai.credits -= cost;
    ai.creditsSpent += cost;
    repairTarget->health = std::min(
        expectedMaximum,
        repairTarget->health + affordableHeal
    );
    ++ai.repairsPerformed;
    ai.status = "repairing #" + std::to_string(repairTarget->instanceId);
    return true;
}

void updateRuntimeComputerAiBaseCenter(
    const RuntimeGameSessionState& state,
    RuntimeComputerAiState& ai) {
    const WorldObjectInstance* fallback = nullptr;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || !runtimeComputerAiOwnerMatches(object, ai.owner)) {
            continue;
        }
        if (fallback == nullptr
            || (object.kind == WorldObjectKind::Structure
                && fallback->kind != WorldObjectKind::Structure)
            || object.instanceId < fallback->instanceId) {
            fallback = &object;
        }
        if (object.kind == WorldObjectKind::Structure) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building != nullptr && building->constructionYard) {
                ai.baseCenter = runtimeCellPointForObject(object);
                return;
            }
        }
    }
    if (fallback != nullptr
        && (ai.baseCenter.x == 0 && ai.baseCenter.y == 0)) {
        ai.baseCenter = runtimeCellPointForObject(*fallback);
    }
}

bool deployRuntimeComputerAiMcvs(
    RuntimeProductionHostState& state,
    const RuntimeComputerAiState& ai) {
    std::vector<int> mcvIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Unit
            || !runtimeComputerAiOwnerMatches(object, ai.owner)) {
            continue;
        }
        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit != nullptr && runtimeUnitTypeIsMcv(state, *unit)) {
            mcvIds.push_back(object.instanceId);
        }
    }
    std::sort(mcvIds.begin(), mcvIds.end());
    bool changed = false;
    for (int objectId : mcvIds) {
        const auto* mcv = findObjectById(state,objectId);
        const auto* unit = mcv ? findRuntimeUnitType(state.rules,*mcv) : nullptr;
        const auto item = unit ? runtimeBuildItemForBuilding(state,unit->deploysInto)
            : std::optional<RuntimeBuildCatalogItem>{};
        const auto planned = item ? findRuntimeMcvDeployCell(state,*mcv,*item)
            : std::optional<RuntimeCellPoint>{};
        if(planned) changed = requestRuntimeMcvDeployment(state,objectId,false,planned) || changed;
    }
    return changed;
}

void ensureRuntimeComputerAiStates(RuntimeGameSessionState& state) {
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (slot.enabled && slot.human) {
            state.computerAiByOwner.erase(
                runtimeCanonicalHumanHouseOwner(
                    runtimeSkirmishOwnerForSlot(slotIndex)
                )
            );
        }
    }
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || slot.human) {
            continue;
        }
        const std::string owner = runtimeSkirmishOwnerForSlot(slotIndex);
        if (toLowerAsciiLocal(owner)
            == toLowerAsciiLocal(state.economy.owner)) {
            continue;
        }
        const std::string key = toLowerAsciiLocal(trimAsciiLocal(owner));
        RuntimeComputerAiState& ai = state.computerAiByOwner[key];
        if (ai.initialized) {
            continue;
        }
        ai.initialized = true;
        ai.owner = owner;
        ai.country = runtimeCountryForOwner(state, owner);
        ai.difficulty = runtimeSkirmishDifficultyForOwner(state, owner);
        ai.credits = std::clamp(
            state.config.startingCredits,
            0,
            runtimeMaximumCreditBalance
        );
        updateRuntimeComputerAiBaseCenter(state, ai);
        const RuntimeComputerAiProfile profile =
            runtimeComputerAiProfile(state, ai);
        const int slotStagger = static_cast<int>(slotIndex) * 2;
        ai.nextThinkTick = state.tick + 1 + slotStagger;
        ai.nextIncomeTick = 0;
        ai.nextBuildTick = state.tick + profile.buildRetryTicks + slotStagger;
        ai.nextAttackTick = state.tick + profile.initialAttackDelayTicks;
        ai.nextRepairTick = state.tick + profile.repairIntervalTicks;
        ai.status = "computer AI ready";
    }
}

bool runtimeComputerTacticCompare(int actual, int expected, int comparator) {
    switch (comparator) {
    case 0: return actual < expected;
    case 1: return actual <= expected;
    case 2: return actual == expected;
    case 3: return actual >= expected;
    case 4: return actual > expected;
    case 5: return actual != expected;
    default: return false;
    }
}

std::string runtimeComputerTacticEnemy(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai) {
    const WorldObjectInstance* best = nullptr;
    double distance = std::numeric_limits<double>::max();
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !runtimeOwnersAreHostileForAutoTarget(state, ai.owner, object.owner)) continue;
        const double candidate = runtimeCellDistance(ai.baseCenter, runtimeCellPointForObject(object))
            + (object.kind == WorldObjectKind::Structure ? 0.0 : 1000.0);
        if (candidate < distance || (candidate == distance && best && object.instanceId < best->instanceId)) {
            best = &object; distance = candidate;
        }
    }
    return best ? best->owner : std::string{};
}

bool runtimeComputerTacticCondition(RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeCampaignAiTriggerDefinition& trigger,
    const std::string& enemy) {
    if (trigger.conditionType == -1) return true;
    const auto compare = [&](int value) {
        return runtimeComputerTacticCompare(value, trigger.conditionNumber, trigger.conditionComparator);
    };
    if (trigger.conditionType == 0 || trigger.conditionType == 1 || trigger.conditionType == 7) {
        int count = 0;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || toUpperAsciiLocal(object.type) != toUpperAsciiLocal(trigger.conditionObject)) continue;
            if (trigger.conditionType == 7 ? runtimeOwnerIsNeutralForAutoTarget(object.owner)
                : runtimeComputerAiOwnerMatches(object, trigger.conditionType == 1 ? ai.owner : enemy)) ++count;
        }
        return compare(count);
    }
    if (trigger.conditionType == 2 || trigger.conditionType == 3) {
        int power = 0;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || !runtimeComputerAiOwnerMatches(object, enemy)) continue;
            if (const auto* building = findRuntimeBuildingTypeById(state.rules, object.type)) power += building->power;
        }
        return !enemy.empty() && (trigger.conditionType == 2 ? power < 0 : power >= 0);
    }
    if (trigger.conditionType == 4) {
        const auto other = state.computerAiByOwner.find(toLowerAsciiLocal(trimAsciiLocal(enemy)));
        if (other != state.computerAiByOwner.end()) return compare(other->second.credits);
        // Human houses are independently authoritative, including remote peers.
        for (const auto& authority : state.humanHouseAuthorities) {
            if (runtimeCanonicalHumanHouseOwner(enemy) != authority.canonicalOwner) continue;
            if (authority.localFacade) return compare(state.economy.credits);
            return authority.remoteEconomy && compare(authority.remoteEconomy->credits);
        }
        return enemy == state.economy.owner && compare(state.economy.credits);
    }
    if (trigger.conditionType == 5 || trigger.conditionType == 6) {
        if (!state.config.superweapons) return false;
        ensureRuntimeSuperWeaponsInitialized(state);
        const std::string identity = trigger.conditionType == 5 ? "ironcurtain" : "chrono";
        for (const auto& snapshot : state.superWeapons.sidebarSnapshots(ai.owner)) {
            const auto key = toLowerAsciiLocal(snapshot.superWeaponId + " " + snapshot.name);
            if (key.find(identity) != std::string::npos
                && snapshot.state != SuperWeaponRuntimeState::Unavailable
                && snapshot.progress >= state.rules.getComputerTacticsRules().minorSuperReadyPercent) return true;
        }
    }
    return false;
}

bool runtimeComputerTacticTemplateBuildable(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeTeamType& team) {
    const auto task = state.triggers.taskForces.find(team.taskForceId);
    const auto script = state.triggers.scripts.find(team.scriptId);
    if (team.max == 0 || task == state.triggers.taskForces.end() || task->second.members.empty()
        || script == state.triggers.scripts.end() || script->second.steps.empty()) return false;
    int size = 0;
    for (const auto& member : task->second.members) {
        const auto item = runtimeComputerAiBuildItem(state, member.unitId, ai.owner);
        if (member.count <= 0 || member.count > 100 || !item
            || item->kind == RuntimeBuildKind::Building || item->kind == RuntimeBuildKind::Defense
            || !runtimeComputerAiItemAllowed(state, ai, *item)) return false;
        size += member.count;
    }
    return size <= 256;
}

void reportRuntimeComputerTacticOutcome(RuntimeGameSessionState& state,
    RuntimeComputerTacticsState& tactics, RuntimeComputerTacticTeam& team) {
    if (team.secondary || team.outcomeReported) return;
    team.outcomeReported = true;
    const auto trigger = std::find_if(state.triggers.computerAiTriggers.begin(),
        state.triggers.computerAiTriggers.end(), [&](const auto& item) { return item.id == team.triggerId; });
    if (trigger == state.triggers.computerAiTriggers.end()) return;
    auto [entry, inserted] = tactics.history.try_emplace(team.triggerId);
    auto& history = entry->second;
    if (inserted) history.weight = trigger->currentWeight;
    const auto& rules = state.rules.getComputerTacticsRules();
    // Retail applies the prior win/loss record, then records this outcome.
    const double track = history.successes - 0.5 * (history.successes + history.failures);
    const double delta = team.achievedGreatSuccess
        ? rules.successWeightDelta + std::max(0.0, track)
        : rules.failureWeightDelta + std::min(0.0, track * rules.trackRecordCoefficient);
    history.weight = std::clamp(history.weight + delta, trigger->minimumWeight, trigger->maximumWeight);
    if (team.achievedGreatSuccess) ++history.successes;
    else ++history.failures;
}

bool selectRuntimeComputerTactic(RuntimeGameSessionState& state, RuntimeComputerAiState& ai,
    RuntimeComputerTacticsState& tactics) {
    if (state.tick < tactics.nextTriggerTick) return false;
    const auto& rules = state.rules.getComputerTacticsRules();
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    const int delay = runtimeLegacyScriptTicks(state, rules.teamDelays[difficulty]);
    tactics.nextTriggerTick = state.tick + delay;
    const std::string enemy = runtimeComputerTacticEnemy(state, ai);
    if (enemy.empty()) return false;
    std::map<std::string, int> templateCounts;
    int defenses = 0;
    for (const auto& [id, team] : tactics.teams) {
        ++templateCounts[team.teamTypeId];
        if (team.baseDefense) ++defenses;
    }
    const bool needDefense = rules.useMinimumDefenseRule && defenses < rules.minimumDefensiveTeams[difficulty];
    struct Choice { const RuntimeCampaignAiTriggerDefinition* trigger; double weight; bool defense; };
    std::vector<Choice> choices;
    bool defenseAvailable = false;
    for (const auto& trigger : state.triggers.computerAiTriggers) {
        if (!trigger.valid || !trigger.enabled || !trigger.availableInSkirmish
            || !trigger.difficultyEnabled[2 - difficulty] || trigger.techLevel > state.economy.techLevel) continue;
        const auto authoredOwner = toLowerAsciiLocal(trimAsciiLocal(trigger.owner));
        if (authoredOwner != "<all>" && authoredOwner != "all"
            && authoredOwner != toLowerAsciiLocal(ai.country)) continue;
        if (trigger.side > 0 && trigger.side != runtimeBasePlanningSideForCountry(ai.country) + 1) continue;
        const auto history = tactics.history.find(trigger.id);
        const double weight = history == tactics.history.end() ? trigger.currentWeight : history->second.weight;
        if (weight <= 0.0 || !runtimeComputerTacticCondition(state, ai, trigger, enemy)) continue;
        const auto* primary = findRuntimeTriggerTeamById(state, trigger.primaryTeamId);
        const auto secondaryId = toLowerAsciiLocal(trimAsciiLocal(trigger.secondaryTeamId));
        const bool hasSecondary = !secondaryId.empty() && secondaryId != "<none>";
        const auto* secondary = hasSecondary ? findRuntimeTriggerTeamById(state, trigger.secondaryTeamId) : nullptr;
        if (!primary || (hasSecondary && !secondary)) continue;
        std::map<std::string, int> addedCounts;
        int addedDefenses = 0, added = 0;
        bool buildable = true;
        for (const auto* team : {primary, secondary}) {
            if (!team) continue;
            // Retail tests both templates against the pre-selection count,
            // then CreateTeam checks Max again for each actual creation.
            if ((team->max >= 0 && templateCounts[team->id] >= team->max)
                || !runtimeComputerTacticTemplateBuildable(state, ai, *team)) buildable = false;
            if (team->max >= 0 && templateCounts[team->id] + addedCounts[team->id] >= team->max) continue;
            ++addedCounts[team->id];
            ++added;
            if (team->isBaseDefense || trigger.baseDefense) ++addedDefenses;
        }
        if (!buildable || static_cast<int>(tactics.teams.size()) + added > rules.totalTeamCap[difficulty]
            || defenses + addedDefenses > rules.maximumDefensiveTeams[difficulty]) continue;
        defenseAvailable |= addedDefenses > 0;
        choices.push_back({&trigger, weight, addedDefenses > 0});
    }
    double total = 0.0;
    for (const auto& choice : choices)
        if (!needDefense || !defenseAvailable || choice.defense) total += choice.weight;
    if (total <= 0.0) {
        // Retry prerequisites during an opening, while preserving the authored
        // interval between successful selections.
        tactics.nextTriggerTick = state.tick + std::min(delay, std::max(1, state.config.fixedTickHz * 2));
        return false;
    }
    const auto random = runtimeStableHashString(ai.owner + ":team:" + std::to_string(++tactics.selectionSerial)
        + ":" + std::to_string(state.tick));
    double roll = static_cast<double>(random % 1000000) / 1000000.0 * total;
    const RuntimeCampaignAiTriggerDefinition* selected = nullptr;
    for (const auto& choice : choices) {
        if (needDefense && defenseAvailable && !choice.defense) continue;
        selected = choice.trigger;
        roll -= choice.weight;
        if (roll < 0.0) break;
    }
    if (!selected) return false;
    auto [record, inserted] = tactics.history.try_emplace(selected->id);
    if (inserted) record->second.weight = selected->currentWeight;
    ++record->second.executions;
    for (bool secondary : {false, true}) {
        const auto* type = findRuntimeTriggerTeamById(state,
            secondary ? selected->secondaryTeamId : selected->primaryTeamId);
        if (!type) continue;
        if (type->max >= 0 && templateCounts[type->id] >= type->max) continue;
        ++templateCounts[type->id];
        RuntimeComputerTacticTeam team;
        team.instanceId = tactics.nextTeamInstanceId++;
        team.triggerId = selected->id; team.teamTypeId = type->id; team.targetOwner = enemy;
        team.secondary = secondary; team.baseDefense = type->isBaseDefense || selected->baseDefense;
        team.createdTick = team.lastProgressTick = state.tick;
        for (const auto& member : state.triggers.taskForces.at(type->taskForceId).members)
            for (int count = 0; count < member.count; ++count) team.missingMembers.push_back(member.unitId);
        tactics.teams.emplace(team.instanceId, std::move(team));
    }
    ai.status = "assembling " + selected->name;
    return true;
}

int runtimeComputerTacticGroup(const RuntimeGameSessionState& state, const RuntimeTeamType& type) {
    const auto task = state.triggers.taskForces.find(type.taskForceId);
    return type.group == -1 && task != state.triggers.taskForces.end() ? task->second.group : type.group;
}

bool runtimeComputerTacticMemberPresent(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, int id) {
    const auto* member = findRuntimeObjectByIdIncludingInactive(state, id);
    return member && runtimeComputerAiOwnerMatches(*member, ai.owner)
        && (member->alive || member->mission == "InsideTransport"
            || runtimeObjectIsPendingTeamRejoinAfterLimbo(state, id));
}

void refreshRuntimeComputerTacticMissing(RuntimeGameSessionState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    if (team.launched && (!type.reinforce || team.objectIds.empty())) return;
    const auto task = state.triggers.taskForces.find(type.taskForceId);
    if (task == state.triggers.taskForces.end()) return;
    std::map<std::string, int> counts;
    for (int id : team.objectIds)
        if (const auto* member = findRuntimeObjectByIdIncludingInactive(state, id))
            ++counts[toUpperAsciiLocal(member->type)];
    team.missingMembers.clear();
    for (const auto& member : task->second.members) {
        const auto key = toUpperAsciiLocal(member.unitId);
        for (int i = 0; i < member.count; ++i) {
            if (counts[key] > 0) --counts[key];
            else team.missingMembers.push_back(member.unitId);
        }
    }
}

bool runtimeComputerTacticMayRecruit(const RuntimeGameSessionState& state,
    const RuntimeComputerAiState& ai, const RuntimeComputerTacticTeam& team,
    const RuntimeTeamType& type, const WorldObjectInstance& object) {
    if (!object.alive || object.health <= 0 || !runtimeComputerAiOwnerMatches(object, ai.owner)
        || object.factoryExitHostId >= 0 || object.garrisonHostObjectId >= 0 || object.tankBunkerHostId >= 0
        || runtimeObjectDisabledByParasite(state, object.instanceId)) return false;
    // Retail's MissionControl forbids recruiting units during entry,
    // unloading, harvesting and other committed interactions.
    const auto mission = toLowerAsciiLocal(object.mission);
    if (mission != "guard" && mission != "areaguard" && mission != "area guard"
        && mission != "move" && mission != "attack" && mission != "secondaryfire" && mission != "dock"
        && mission != "stop" && mission != "patrol" && mission != "hunt") return false;
    if (state.pendingEngineerCaptures.contains(object.instanceId)
        || state.pendingTransportTargets.contains(object.instanceId)
        || state.pendingOccupyTargets.contains(object.instanceId)) return false;
    for (const auto& relation : state.grinding.relations())
        if (relation.sourceId == object.instanceId) return false;
    if (const auto* unit = findRuntimeUnitType(state.rules, object); unit) {
        if (unit->harvester || unit->resourceGatherer
            || (unit->airportBound && (object.airborne || !runtimeObjectHasAttackAmmo(state.rules, object)))) return false;
    }
    bool assigned = false;
    const int group = runtimeComputerTacticGroup(state, type);
    for (const auto& [house, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, existing] : tactics.teams) {
            if (std::find(existing.objectIds.begin(), existing.objectIds.end(), object.instanceId)
                == existing.objectIds.end()) continue;
            assigned = true;
            if (&existing == &team || existing.teamTypeId == team.teamTypeId) return false;
            const auto oldType = state.triggers.teams.find(existing.teamTypeId);
            if (oldType == state.triggers.teams.end() || !oldType->second.recruitable
                || oldType->second.priority >= type.priority) return false;
            // A Recruiter may draw from another authored group. Default
            // skirmish groups are -1; -2 explicitly accepts any group.
            if (!type.recruiter && group != -2 && runtimeComputerTacticGroup(state, oldType->second) != group) return false;
        }
    if (!assigned && !type.recruiter && group != -2 && group != -1) return false;
    return true;
}

bool recruitRuntimeComputerTacticMembers(RuntimeProductionHostState& state,
    RuntimeComputerAiState& ai, RuntimeComputerTacticsState& tactics,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    std::map<std::string, int> inProduction;
    for (const auto& order : tactics.production)
        if (order.teamInstanceId == team.instanceId && !order.typeId.empty())
            ++inProduction[toUpperAsciiLocal(order.typeId)];
    bool changed = false;
    for (auto missing = team.missingMembers.begin(); missing != team.missingMembers.end();) {
        const auto key = toUpperAsciiLocal(*missing);
        if (inProduction[key] > 0) { --inProduction[key]; ++missing; continue; }
        int recruitId = -1; double bestDistance = std::numeric_limits<double>::max();
        for (const auto& object : state.world.getObjects()) {
            if (toUpperAsciiLocal(object.type) != key
                || !runtimeComputerTacticMayRecruit(state, ai, team, type, object)) continue;
            const double distance = runtimeCellDistance(ai.baseCenter, runtimeCellPointForObject(object));
            if (distance < bestDistance || (distance == bestDistance && object.instanceId < recruitId)) {
                recruitId = object.instanceId; bestDistance = distance;
            }
        }
        if (recruitId < 0) { ++missing; continue; }
        detachRuntimeObjectFromTeamMembership(state, recruitId);
        eraseRuntimeBehavior(state, recruitId);
        team.objectIds.push_back(recruitId);
        if (team.launched) team.joiningObjectIds.push_back(recruitId);
        if (auto* recruit = findMutableObjectById(state, recruitId)) recruit->mission = "Guard";
        missing = team.missingMembers.erase(missing);
        team.lastProgressTick = state.tick; changed = true;
    }
    return changed;
}

bool joinRuntimeComputerTacticReinforcements(RuntimeProductionHostState& state,
    RuntimeComputerTacticTeam& team, const RuntimeTeamType& type) {
    bool changed = false;
    for (auto next = team.joiningObjectIds.begin(); next != team.joiningObjectIds.end();) {
        auto* member = findMutableObjectById(state, *next);
        if (!member) { next = team.joiningObjectIds.erase(next); continue; }
        if (member->factoryExitHostId >= 0 || member->mission == "FactoryExit") { ++next; continue; }
        member->veterancy = std::max(member->veterancy, type.veteranLevel - 1);
        const int memberId = member->instanceId;
        RuntimeTeamMissionState companions;
        for (int id : team.objectIds) if (id != memberId) companions.objectIds.push_back(id);
        RuntimeCellPoint destination = runtimeTeamMissionCenter(state, companions);
        bool ordered = false;
        if (auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
            mission != state.triggers.teamMissions.end()) {
            auto& current = mission->second;
            if (std::find(current.objectIds.begin(), current.objectIds.end(), memberId) == current.objectIds.end())
                current.objectIds.push_back(memberId);
            const auto script = state.triggers.scripts.find(current.scriptId);
            const int action = script != state.triggers.scripts.end() && current.stepIndex < script->second.steps.size()
                ? script->second.steps[current.stepIndex].action : -1;
            if (current.stepIssued && (action == 0 || action == 46))
                if (const auto* target = findObjectById(state, current.targetObjectId))
                    ordered = issueRuntimeTeamAttack(state, {memberId}, *target) > 0;
            if (current.targetCell.x >= 0) destination = current.targetCell;
        }
        if (!ordered && destination.x >= 0) (void)issueRuntimeTeamMove(state, {memberId}, destination);
        next = team.joiningObjectIds.erase(next); changed = true;
    }
    return changed;
}

bool advanceRuntimeComputerTactics(RuntimeProductionHostState& state, RuntimeComputerAiState& ai) {
    if (state.triggers.computerAiTriggers.empty()) return false;
    auto& tactics = state.triggers.computerTacticsByOwner[toLowerAsciiLocal(trimAsciiLocal(ai.owner))];
    bool changed = advanceRuntimeComputerTacticProduction(state, ai, tactics);
    // World scans and team selection need not run at the production tick rate.
    if (state.tick < ai.nextThinkTick) return changed;
    const auto& rules = state.rules.getComputerTacticsRules();
    std::vector<int> finished;
    for (auto& [id, team] : tactics.teams) {
        const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId);
        if (!type) { finished.push_back(id); continue; }
        for (auto member = team.objectIds.begin(); member != team.objectIds.end();) {
            if (runtimeComputerTacticMemberPresent(state, ai, *member)) { ++member; continue; }
            std::erase(team.joiningObjectIds, *member);
            member = team.objectIds.erase(member); changed = true;
        }
        if (team.launched) {
            const auto mission = state.triggers.teamMissions.find(team.missionInstanceId);
            if (mission != state.triggers.teamMissions.end())
                team.achievedGreatSuccess |= mission->second.achievedGreatSuccess;
            if (team.missionFinished && (!team.baseDefense || team.objectIds.empty())) {
                finished.push_back(id); continue;
            }
            changed = joinRuntimeComputerTacticReinforcements(state, team, *type) || changed;
            if (!type->reinforce || team.objectIds.empty()) continue;
        }
        const auto previousMissing = team.missingMembers;
        refreshRuntimeComputerTacticMissing(state, team, *type);
        changed = previousMissing != team.missingMembers || changed;
        changed = recruitRuntimeComputerTacticMembers(state, ai, tactics, team, *type) || changed;
        if (team.launched) continue;
        if (team.missingMembers.empty() && !team.objectIds.empty()) {
            bool exiting = false;
            for (int objectId : team.objectIds) {
                const auto* object = findObjectById(state, objectId);
                if (object && (object->factoryExitHostId >= 0 || object->mission == "FactoryExit")) exiting = true;
            }
            if (!exiting) {
                team.launched = true;
                team.missionInstanceId = state.triggers.nextTeamMissionInstanceId;
                auto launchType = *type; launchType.house = ai.owner;
                for (int objectId : team.objectIds)
                    if (auto* object = findMutableObjectById(state, objectId))
                        object->veterancy = std::max(object->veterancy, type->veteranLevel - 1);
                issueRuntimeTriggerTeamOrder(state, launchType, team.objectIds, team.triggerId, team.instanceId);
                ++ai.attackWaves; ai.attacksOrdered += static_cast<int>(team.objectIds.size());
                ai.status = "launched " + type->name; changed = true;
            }
        } else if (state.tick - team.lastProgressTick
            > runtimeLegacyScriptTicks(state, rules.dissolveUnfilledTeamDelay)) finished.push_back(id);
    }
    for (int id : finished) {
        auto found = tactics.teams.find(id);
        if (found == tactics.teams.end()) continue;
        reportRuntimeComputerTacticOutcome(state, tactics, found->second);
        // A paid unit already in progress completes as a free-to-recruit unit.
        for (auto& order : tactics.production) if (order.teamInstanceId == id) order.teamInstanceId = -1;
        state.triggers.teamMissions.erase(found->second.missionInstanceId);
        tactics.teams.erase(found); changed = true;
    }
    changed = selectRuntimeComputerTactic(state, ai, tactics) || changed;
    // New teams first recruit available members; do not start an unnecessary
    // paid unit merely because selection follows the maintenance pass.
    for (auto& [id, team] : tactics.teams) if (team.createdTick == state.tick && !team.launched) {
        if (const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId))
            changed = recruitRuntimeComputerTacticMembers(state, ai, tactics, team, *type) || changed;
    }

    // Economic recovery shares the same factory queue and takes the next free
    // slot before optional combat members. It never interrupts a paid order.
    const auto metrics = runtimeComputerAiMetrics(state, ai);
    auto economic = runtimeComputerAiSelectNeededHarvester(state, ai, metrics);
    if (!economic && runtimeIsMegawealthMode(state) && runtimeComputerAiNeedsOilEngineer(state, ai))
        economic = runtimeComputerAiSelectUnitForRole(state, ai, RuntimeComputerAiUnitRole::Engineer);
    if (economic) changed = queueRuntimeComputerTacticProduction(state, ai, *economic) || changed;
    std::vector<int> priority;
    for (const auto& [id, team] : tactics.teams) if (!team.missingMembers.empty()) priority.push_back(id);
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    const auto random = runtimeStableHashString(ai.owner + ":fill:" + std::to_string(state.tick));
    if (!priority.empty() && random % 100 >= static_cast<unsigned>(rules.fillEarliestTeamProbability[difficulty]))
        std::rotate(priority.begin(), priority.begin() + random % priority.size(), priority.end());
    for (int id : priority) {
        auto& team = tactics.teams.at(id);
        for (const auto& missing : team.missingMembers) {
            const auto item = runtimeComputerAiBuildItem(state, missing, ai.owner);
            if (item) changed = queueRuntimeComputerTacticProduction(state, ai, *item, id) || changed;
        }
    }
    return changed;
}

int runtimeComputerCaptureChoice(const ComputerTacticsRules& rules, int credits,
    bool lowPower, double healthRatio, unsigned roll, int teamOverride) {
    if (teamOverride >= 1 && teamOverride <= 5) return teamOverride;
    const auto& weights = credits < rules.captureLowMoneyMark ? rules.captureLowMoney
        : lowPower ? rules.captureLowPower
        : healthRatio < rules.captureWoundedMark ? rules.captureWounded : rules.captureNormal;
    int remaining = static_cast<int>(roll % 100) + 1;
    for (int index = 0; index < 4; ++index) {
        remaining -= weights[index];
        if (remaining <= 0) return index + 1;
    }
    return 5;
}

bool advanceRuntimeComputerCaptureFates(RuntimeProductionHostState& state, const std::string& ownerKey) {
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    const auto house = state.computerAiByOwner.find(ownerKey);
    if (house == state.computerAiByOwner.end() || !house->second.initialized
        || state.tick < house->second.nextThinkTick) return false;
    const std::string owner = house->second.owner;
    std::vector<MindControlLinkState> captures;
    for (const auto& [target, link] : state.specialMechanisms.mindControlLinks())
        if (toLowerAsciiLocal(trimAsciiLocal(link.controllerOwner)) == ownerKey) captures.push_back(link);
    bool changed = false;
    auto& decisions = state.triggers.computerTacticsByOwner[ownerKey].captureDecisions;
    changed |= std::erase_if(decisions, [&](const auto& entry) {
        const auto link = state.specialMechanisms.mindControlLinks().find(entry.first);
        return link == state.specialMechanisms.mindControlLinks().end()
            || link->second.controllerObjectId != entry.second.controllerId
            || link->second.startedTick != entry.second.startedTick
            || toLowerAsciiLocal(trimAsciiLocal(link->second.controllerOwner)) != ownerKey;
    }) > 0;
    // A grinder command commits the world and house state. Work from copied
    // links and reacquire every reference after each command.
    for (const auto& link : captures) {
        const auto* target = findObjectById(state, link.targetObjectId);
        const auto* controller = findObjectById(state, link.controllerObjectId);
        if (!target || !controller || !runtimeComputerAiOwnerMatches(*target, owner)
            || target->kind == WorldObjectKind::Structure) continue;
        auto& tactics = state.triggers.computerTacticsByOwner[ownerKey];
        if (tactics.captureDecisions.contains(link.targetObjectId)) continue;
        int teamId = -1, overrideDecision = 0;
        for (const auto& [id, team] : tactics.teams)
            if (std::find(team.objectIds.begin(), team.objectIds.end(), link.controllerObjectId) != team.objectIds.end()) {
                teamId = id;
                if (const auto* type = findRuntimeTriggerTeamById(state, team.teamTypeId))
                    overrideDecision = type->mindControlDecision;
                break;
            }
        const auto& ai = state.computerAiByOwner.at(ownerKey);
        const auto metrics = runtimeComputerAiMetrics(state, ai);
        const auto random = runtimeStableHashString(owner + ":capture:" + std::to_string(link.controllerObjectId)
            + ":" + std::to_string(link.targetObjectId) + ":" + std::to_string(link.startedTick));
        int decision = runtimeComputerCaptureChoice(state.rules.getComputerTacticsRules(), ai.credits,
            metrics.powerProduced < metrics.powerUsed,
            static_cast<double>(target->health) / std::max(1, runtimeMaxHealthForObject(state, *target)),
            static_cast<unsigned>(random % 100), overrideDecision);
        tactics.captureDecisions[link.targetObjectId] = {link.controllerObjectId, link.startedTick, decision};
        changed = true;
        if (decision == 5) continue;
        detachRuntimeObjectFromTeamMembership(state, link.targetObjectId);
        eraseRuntimeBehavior(state, link.targetObjectId);
        if (decision == 1 && teamId >= 0) {
            auto& team = tactics.teams.at(teamId);
            if (team.objectIds.size() < 256 && !team.missionFinished) {
                team.objectIds.push_back(link.targetObjectId);
                if (team.launched) team.joiningObjectIds.push_back(link.targetObjectId);
                if (auto* member = findMutableObjectById(state, link.targetObjectId)) member->mission = "Guard";
                continue;
            }
        }
        if (decision == 2 || decision == 3) {
            std::vector<std::pair<double, int>> destinations;
            for (const auto& building : state.world.getObjects()) {
                if (!building.alive || !runtimeComputerAiOwnerMatches(building, owner)
                    || building.rubble || building.buildAnimTicksRemaining > 0) continue;
                const auto* type = findRuntimeBuildingTypeById(state.rules, building.type);
                if (!type) continue;
                if (decision == 2 ? type->grinding && building.poweredOperational
                    : type->infantryAbsorb && target->kind == WorldObjectKind::Infantry
                        && static_cast<int>(building.transportPassengerIds.size())
                            + runtimeTransportReservedPassengerCount(state, building.instanceId) < type->passengers)
                    destinations.emplace_back(runtimeCellDistance(runtimeCellPointForObject(*target),
                        runtimeCellPointForObject(building)), building.instanceId);
            }
            std::sort(destinations.begin(), destinations.end());
            bool entered = false;
            for (const auto& [distance, hostId] : destinations) {
                if (decision == 2) {
                    entered = runtimeGrindingTeamEligibility(state, hostId, link.targetObjectId)
                            == RuntimeGrindingTeamEligibility::Accepted
                        && queueRuntimeGrindingEntry(state, hostId, link.targetObjectId, false);
                } else if (const auto* host = findObjectById(state, hostId)) {
                    const auto selection = state.selectedObjectIds;
                    const bool audio = state.runtimeSuppressPlayerAudioFeedback;
                    state.selectedObjectIds = {link.targetObjectId}; state.runtimeSuppressPlayerAudioFeedback = true;
                    (void)queueRuntimeTransportTarget(state, *host);
                    entered = state.pendingTransportTargets.contains(link.targetObjectId);
                    state.selectedObjectIds = selection; state.runtimeSuppressPlayerAudioFeedback = audio;
                }
                if (entered) break;
            }
            if (entered) continue;
        }
        // Retail falls back to Hunt if joining, recycling or absorption is
        // impossible; it does not remove a captured unit at a distance.
        RuntimeTeamMissionState hunting; hunting.objectIds = {link.targetObjectId};
        if (const auto* enemy = runtimeTeamEnemyTarget(state, hunting, 1))
            (void)issueRuntimeTeamAttack(state, hunting.objectIds, *enemy);
        else if (auto* member = findMutableObjectById(state, link.targetObjectId)) member->mission = "Hunt";
    }
    return changed;
}

bool advanceRuntimeComputerAi(RuntimeProductionHostState& state) {
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    RuntimePerfTraceScope trace("advanceRuntimeComputerAi", state.tick);
    if (state.campaignMode
        || !runtimeComputerAiEnabledForSession(state)
        || state.skirmishMatchFinished
        || !state.rulesLoaded
        || !state.economy.skirmishStartApplied
        || state.config.playerSlots.empty()) {
        return false;
    }
    ensureRuntimeComputerAiStates(state);
    std::vector<std::string> activeOwners;
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || slot.human) {
            continue;
        }
        const std::string owner = runtimeSkirmishOwnerForSlot(slotIndex);
        const std::string key = toLowerAsciiLocal(trimAsciiLocal(owner));
        if (state.computerAiByOwner.find(key) != state.computerAiByOwner.end()) {
            activeOwners.push_back(key);
        }
    }
    std::sort(activeOwners.begin(), activeOwners.end());
    activeOwners.erase(
        std::unique(activeOwners.begin(), activeOwners.end()),
        activeOwners.end()
    );
    bool changed = false;
    for (const std::string& key : activeOwners) {
        changed = advanceRuntimeComputerCaptureFates(state, key) || changed;
        RuntimeComputerAiState& ai = state.computerAiByOwner[key];
        if (ai.initialized) changed = advanceRuntimeComputerTactics(state, ai) || changed;
        if (!ai.initialized || state.tick < ai.nextThinkTick) {
            continue;
        }
        const RuntimeComputerAiProfile profile =
            runtimeComputerAiProfile(state, ai);
        ai.nextThinkTick = state.tick + profile.thinkIntervalTicks;
        changed = deployRuntimeComputerAiMcvs(state, ai) || changed;
        changed = advanceRuntimeComputerAiOilCapture(state, ai) || changed;
        updateRuntimeComputerAiBaseCenter(state, ai);
        const RuntimeComputerAiMetrics metrics =
            runtimeComputerAiMetrics(state, ai);
        if (metrics.structures <= 0 && metrics.mobileUnits <= 0) {
            ai.status = "defeated";
            continue;
        }
        changed = advanceRuntimeComputerAiRepair(
            state, ai, profile
        ) || changed;
        changed = advanceRuntimeComputerAiDefense(
            state, ai, profile
        ) || changed;
        changed = advanceRuntimeComputerAiTactics(state, ai) || changed;
        changed = advanceRuntimeComputerAiProduction(
            state, ai, profile, metrics
        ) || changed;
        changed = advanceRuntimeComputerAiAttackWave(
            state, ai, profile
        ) || changed;
    }
    return changed;
}

bool runtimeCampaignAiCompare(
    int actual,
    int expected,
    int comparator) {
    switch (comparator) {
    case 0:
        return actual < expected;
    case 1:
        return actual <= expected;
    case 2:
        return actual == expected;
    case 3:
        return actual >= expected;
    case 4:
        return actual > expected;
    case 5:
        return actual != expected;
    default:
        return false;
    }
}

int runtimeCampaignAiObjectCount(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const std::string& typeId,
    bool enemies) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(typeId));
    if (wanted.empty() || wanted == "<none>") {
        return 0;
    }
    int count = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || toLowerAsciiLocal(trimAsciiLocal(object.type)) != wanted) {
            continue;
        }
        const bool self = runtimeCampaignHouseMatches(
            state,
            house.owner,
            object.owner
        );
        const bool allied = runtimeOwnersAreAllied(
            state,
            house.owner,
            object.owner
        );
        if ((!enemies && self) || (enemies && !self && !allied)) {
            ++count;
        }
    }
    return count;
}

bool runtimeCampaignAiEnemyCreditsCondition(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    int expected,
    int comparator) {
    bool foundEnemy = false;
    for (const auto& [ownerKey, candidate] :
            state.triggers.campaignHouseProduction) {
        (void)ownerKey;
        if (runtimeCampaignHouseMatches(
                state,
                house.owner,
                candidate.owner
            )
            || runtimeOwnersAreAllied(
                state,
                house.owner,
                candidate.owner
            )) {
            continue;
        }
        foundEnemy = true;
        const int credits = candidate.playerControlled
            || runtimeCampaignHouseMatches(
                state,
                state.economy.owner,
                candidate.owner
            )
            ? state.economy.credits
            : candidate.credits;
        if (runtimeCampaignAiCompare(credits, expected, comparator)) {
            return true;
        }
    }
    return !foundEnemy && runtimeCampaignAiCompare(0, expected, comparator);
}

bool runtimeCampaignAiOwnerChronosphereReady(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house) {
    ensureRuntimeSuperWeaponsInitialized(state);
    if (!state.superWeaponsInitialized) {
        return false;
    }
    for (const SuperWeaponRule& rule : state.rules.getSuperWeaponTypes()) {
        const std::string identity = toLowerAsciiLocal(
            rule.id + " " + rule.type + " " + rule.action
        );
        if (identity.find("chrono") == std::string::npos) {
            continue;
        }
        const SuperWeaponRuntimeState runtimeState =
            state.superWeapons.stateOf(house.owner, rule.id);
        if (runtimeState == SuperWeaponRuntimeState::Ready
            || runtimeState == SuperWeaponRuntimeState::SelectingTarget
            || runtimeState
                == SuperWeaponRuntimeState::AwaitingSecondTarget) {
            return true;
        }
    }
    return false;
}

bool runtimeCampaignAiConditionMet(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger) {
    if (trigger.conditionType == 0 || trigger.conditionType == 1) {
        return runtimeCampaignAiCompare(
            runtimeCampaignAiObjectCount(
                state,
                house,
                trigger.conditionObject,
                trigger.conditionType == 0
            ),
            trigger.conditionNumber,
            trigger.conditionComparator
        );
    }
    if (trigger.conditionType == 4) {
        return runtimeCampaignAiEnemyCreditsCondition(
            state,
            house,
            trigger.conditionNumber,
            trigger.conditionComparator
        );
    }
    if (trigger.conditionType == 6) {
        return runtimeCampaignAiOwnerChronosphereReady(state, house);
    }
    // Power-state and neutral-owner conditions require additional authored
    // runtime state.  Unknown conditions must never become unconditional.
    return false;
}

int runtimeCampaignAiDifficultyIndex(const RuntimeGameSessionState& state) {
    const std::string difficulty = toUpperAsciiLocal(trimAsciiLocal(
        state.config.campaignDifficulty
    ));
    if (difficulty == "EASY") {
        return 0;
    }
    if (difficulty == "HARD" || difficulty == "BRUTAL") {
        return 2;
    }
    return 1;
}

int runtimeCampaignAiTeamInstanceCount(
    const RuntimeGameSessionState& state,
    const std::string& teamTypeId) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(teamTypeId));
    int count = 0;
    for (const auto& [missionId, mission] : state.triggers.teamMissions) {
        (void)missionId;
        if (!mission.completed
            && toLowerAsciiLocal(trimAsciiLocal(mission.teamTypeId))
                == wanted) {
            ++count;
        }
    }
    for (const auto& [buildId, build] : state.triggers.campaignTeamBuilds) {
        (void)buildId;
        if (!build.failed
            && toLowerAsciiLocal(trimAsciiLocal(build.teamTypeId)) == wanted) {
            ++count;
        }
    }
    return count;
}

bool runtimeCampaignAiTriggerEligible(
    RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger) {
    if (!trigger.valid
        || !trigger.enabled
        || trigger.currentWeight <= 0.0
        || state.tick < trigger.nextEligibleTick
        || !trigger.difficultyEnabled[static_cast<std::size_t>(
            runtimeCampaignAiDifficultyIndex(state)
        )]
        || trigger.techLevel > house.techLevel) {
        return false;
    }
    const std::string authoredOwner =
        toLowerAsciiLocal(trimAsciiLocal(trigger.owner));
    if (authoredOwner == "<all>" || authoredOwner == "all") {
        if (trigger.side > 0 && trigger.side != house.side) {
            return false;
        }
    } else if (!runtimeCampaignHouseMatches(
            state,
            trigger.owner,
            house.owner
        )) {
        return false;
    }
    const std::string secondary =
        toLowerAsciiLocal(trimAsciiLocal(trigger.secondaryTeamId));
    if (!secondary.empty() && secondary != "<none>") {
        return false;
    }
    const RuntimeTeamType* team = findRuntimeTriggerTeamById(
        state,
        trigger.primaryTeamId
    );
    if (team == nullptr) {
        return false;
    }
    const auto taskForce = state.triggers.taskForces.find(team->taskForceId);
    if (taskForce == state.triggers.taskForces.end()
        || taskForce->second.members.empty()) {
        return false;
    }
    if (team->max > 0
        && runtimeCampaignAiTeamInstanceCount(state, team->id) >= team->max) {
        return false;
    }
    return runtimeCampaignAiConditionMet(state, house, trigger);
}

std::uint64_t runtimeCampaignAiSelectionHash(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

int runtimeCampaignAiTeamAllocationRatio(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeCampaignAiTriggerDefinition& trigger) {
    int ratio = std::clamp(house.aiTriggerTeamRatio, 0, 100);
    const RuntimeTeamType* team = findRuntimeTriggerTeamById(
        state,
        trigger.primaryTeamId
    );
    const auto taskForce = team == nullptr
        ? state.triggers.taskForces.end()
        : state.triggers.taskForces.find(team->taskForceId);
    if (taskForce == state.triggers.taskForces.end()) {
        return ratio;
    }
    for (const RuntimeTaskForceMember& member : taskForce->second.members) {
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(state, member.unitId, house.owner);
        if (!item.has_value()) {
            continue;
        }
        if (item->kind == RuntimeBuildKind::Infantry) {
            ratio = std::min(
                ratio,
                std::clamp(house.teamInfantryRatio, 0, 100)
            );
        } else if (toLowerAsciiLocal(item->requiredFactory)
                == "aircrafttype") {
            ratio = std::min(
                ratio,
                std::clamp(house.teamAircraftRatio, 0, 100)
            );
        } else if (item->kind == RuntimeBuildKind::Unit) {
            ratio = std::min(
                ratio,
                std::clamp(house.teamUnitRatio, 0, 100)
            );
        }
    }
    return ratio;
}

RuntimeCampaignAiTriggerDefinition* selectRuntimeCampaignAiTrigger(
    RuntimeGameSessionState& state,
    RuntimeCampaignHouseProductionState& house) {
    std::vector<RuntimeCampaignAiTriggerDefinition*> eligible;
    std::uint64_t totalWeight = 0;
    for (RuntimeCampaignAiTriggerDefinition& trigger :
            state.triggers.campaignAiTriggers) {
        if (!runtimeCampaignAiTriggerEligible(state, house, trigger)) {
            continue;
        }
        const std::uint64_t weight = static_cast<std::uint64_t>(std::max(
            1.0,
            std::round(trigger.currentWeight * 1000.0)
        ));
        if (weight > std::numeric_limits<std::uint64_t>::max() - totalWeight) {
            continue;
        }
        totalWeight += weight;
        eligible.push_back(&trigger);
    }
    if (eligible.empty() || totalWeight == 0) {
        return nullptr;
    }
    const std::string seed = state.config.defaultMapPath + "|"
        + toLowerAsciiLocal(house.owner) + "|"
        + std::to_string(house.selectionSerial++);
    std::uint64_t roll = runtimeCampaignAiSelectionHash(seed) % totalWeight;
    RuntimeCampaignAiTriggerDefinition* selected = nullptr;
    for (RuntimeCampaignAiTriggerDefinition* trigger : eligible) {
        const std::uint64_t weight = static_cast<std::uint64_t>(std::max(
            1.0,
            std::round(trigger->currentWeight * 1000.0)
        ));
        if (roll < weight) {
            selected = trigger;
            break;
        }
        roll -= weight;
    }
    if (selected == nullptr) {
        selected = eligible.back();
    }

    // The current campaign AI produces task-force members only for teams.
    // Model the retail team-vs-random allocation bucket as a deterministic
    // idle outcome; a later scheduler check gets a fresh authored roll.
    const int allocationRatio = runtimeCampaignAiTeamAllocationRatio(
        state,
        house,
        *selected
    );
    const std::uint64_t allocationRoll = runtimeCampaignAiSelectionHash(
        seed + "|team-allocation|" + selected->id
    ) % 100ULL;
    return allocationRoll < static_cast<std::uint64_t>(allocationRatio)
        ? selected
        : nullptr;
}

bool runtimeCampaignAiItemAllowed(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item) {
    const bool secretLabUnlock = runtimeBuildItemHasSecretLabUnlock(
        state,
        house.owner,
        item
    );
    if ((!secretLabUnlock
            && (item.techLevel < 0 || item.techLevel > house.techLevel))
        || item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense
        || item.wall
        || (!secretLabUnlock
            && !evaluateTechTreeCountryFilter(
            state.rules,
            house.country,
            item.owner,
            item.requiredHouses,
            item.forbiddenHouses
        ).visible)
        || !runtimeComputerAiHasFactory(
            state,
            house.owner,
            item.requiredFactory
        )
        || (!secretLabUnlock
            && !runtimeComputerAiPrerequisitesMet(
                state,
                house.owner,
                item
            ))
        || !runtimeBuildLimitBlockReason(
            state,
            house.owner,
            item,
            true,
            false
        ).empty()) {
        return false;
    }
    return true;
}

const WorldObjectInstance* runtimeCampaignAiFactoryAnchor(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item) {
    RuntimeComputerAiState proxy;
    proxy.owner = house.owner;
    proxy.country = house.country;
    return runtimeComputerAiFactoryAnchor(state, proxy, item);
}

std::optional<int> completeRuntimeCampaignAiUnit(
    RuntimeProductionHostState& state,
    const RuntimeCampaignHouseProductionState& house,
    const RuntimeBuildCatalogItem& item,
    RuntimeCampaignTeamBuild& build) {
    const WorldObjectInstance* anchor = runtimeCampaignAiFactoryAnchor(
        state,
        house,
        item
    );
    if (anchor == nullptr) {
        return std::nullopt;
    }
    const int anchorId = anchor->instanceId;
    if (requestRuntimeProductionDoorClearance(state, item, *anchor, house.owner)) {
        return std::nullopt;
    }
    RuntimeComputerAiState proxy;
    proxy.owner = house.owner;
    proxy.country = house.country;
    const std::optional<RuntimeCellPoint> spawn =
        runtimeComputerAiSpawnCellNearAnchor(
            state,
            proxy,
            item,
            *anchor,
            build.instanceId + static_cast<int>(build.objectIds.size())
        );
    if (!spawn.has_value()) {
        return std::nullopt;
    }
    WorldObjectInstance* unit = addRuntimeObjectInstanceForOwner(
        state,
        house.owner,
        item.kind,
        item.typeId,
        *spawn
    );
    if (unit == nullptr) {
        return std::nullopt;
    }
    unit->mission = "FactoryExit";
    applyRuntimeProducedObjectVeterancy(state, *unit);
    recordRuntimeCompletedBuild(state, house.owner, item);
    const int unitId = unit->instanceId;
    if (WorldObjectInstance* liveUnit = findMutableObjectById(state, unitId)) {
        if (WorldObjectInstance* liveFactory =
                findMutableObjectById(state, anchorId)) {
            beginRuntimeFactoryExitAnimation(state, *liveFactory, *liveUnit);
            queueRuntimeProductionExitMove(state, *liveUnit, *liveFactory);
        }
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    if (item.kind == RuntimeBuildKind::Infantry) {
        (void)cloneRuntimeProducedInfantry(
            state,
            house.owner,
            item.typeId,
            anchorId
        );
    }
    ++state.triggers.unitsCreated;
    return unitId;
}

bool advanceRuntimeCampaignTeamBuild(
    RuntimeProductionHostState& state,
    RuntimeCampaignHouseProductionState& house,
    RuntimeCampaignTeamBuild& build) {
    const RuntimeTeamType* team = findRuntimeTriggerTeamById(
        state,
        build.teamTypeId
    );
    if (team == nullptr) {
        build.failed = true;
        build.status = "missing TeamType";
        return true;
    }
    const auto taskForce = state.triggers.taskForces.find(team->taskForceId);
    if (taskForce == state.triggers.taskForces.end()) {
        build.failed = true;
        build.status = "missing TaskForce";
        return true;
    }
    if (state.tick > build.deadlineTick) {
        if (build.paidCost > 0) {
            house.credits = std::min(
                std::numeric_limits<int>::max(),
                house.credits > std::numeric_limits<int>::max()
                        - build.paidCost
                    ? std::numeric_limits<int>::max()
                    : house.credits + build.paidCost
            );
            build.paidCost = 0;
        }
        build.failed = true;
        build.status = "unfilled team deadline";
        return true;
    }
    while (build.memberIndex < taskForce->second.members.size()) {
        const RuntimeTaskForceMember& member =
            taskForce->second.members[build.memberIndex];
        if (member.count <= 0 || build.memberProduced >= member.count) {
            ++build.memberIndex;
            build.memberProduced = 0;
            continue;
        }
        const std::optional<RuntimeBuildCatalogItem> item =
            runtimeComputerAiBuildItem(
                state,
                member.unitId,
                house.owner
            );
        if (!item.has_value()) {
            build.failed = true;
            build.status = "unknown member " + member.unitId;
            return true;
        }
        if (build.activeTypeId.empty()) {
            if (!runtimeCampaignAiItemAllowed(state, house, *item)) {
                build.status = "waiting factory/prerequisite for "
                    + item->typeId;
                return false;
            }
            if (house.credits < item->cost) {
                build.status = "waiting credits for " + item->typeId;
                return false;
            }
            spendRuntimeComputerStoredOre(state, house.owner, item->cost);
            house.credits -= item->cost;
            build.paidCost = item->cost;
            build.activeTypeId = item->typeId;
            build.remainingTicks = std::max(1, item->buildTicks);
            build.status = "building " + item->typeId;
            return true;
        }
        if (toLowerAsciiLocal(build.activeTypeId)
            != toLowerAsciiLocal(item->typeId)) {
            build.failed = true;
            build.status = "active member mismatch";
            return true;
        }
        if (runtimeCampaignAiFactoryAnchor(state, house, *item) == nullptr) {
            build.status = "factory lost while building " + item->typeId;
            return false;
        }
        if (build.remainingTicks > 0) {
            --build.remainingTicks;
            return true;
        }
        const std::optional<int> unitId = completeRuntimeCampaignAiUnit(
            state,
            house,
            *item,
            build
        );
        if (!unitId.has_value()) {
            build.status = "factory exit blocked for " + item->typeId;
            return false;
        }
        build.objectIds.push_back(*unitId);
        ++build.memberProduced;
        build.activeTypeId.clear();
        build.paidCost = 0;
        build.remainingTicks = 0;
        build.status = "produced " + item->typeId;
        return true;
    }
    build.completed = true;
    build.status = "team assembled, waiting factory exit";
    return true;
}

bool runtimeCampaignTeamBuildReadyForMission(
    const RuntimeGameSessionState& state,
    const RuntimeCampaignTeamBuild& build) {
    if (build.objectIds.empty()) {
        return false;
    }
    for (int objectId : build.objectIds) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr || !object->alive) {
            return false;
        }
        if (state.behaviors.find(objectId) != state.behaviors.end()) {
            return false;
        }
    }
    return true;
}

void dissolveRuntimeCampaignTeamBuild(
    RuntimeProductionHostState& state,
    const RuntimeCampaignTeamBuild& build) {
    bool removed = false;
    for (int objectId : build.objectIds) {
        WorldObjectInstance* object =
            findMutableRuntimeObjectByIdIncludingInactive(state, objectId);
        if (object == nullptr) {
            continue;
        }
        if (object->alive) {
            destroyRuntimeCampaignObject(
                state,
                *object,
                "",
                "campaign AI unfilled team dissolved",
                RuntimeObjectExitKind::ScriptRemoved,
                true
            );
            removed = true;
        } else {
            eraseRuntimeBehavior(state, objectId);
            removeRuntimeSelection(state, objectId);
        }
    }
    if (removed) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
    }
}

bool advanceRuntimeCampaignAiProduction(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeCampaignAiProduction", state.tick);
    if (!state.campaignMode
        || !state.triggers.loaded
        || state.triggers.campaignAiTriggers.empty()) {
        return false;
    }
    bool changed = false;
    std::vector<int> finishedBuildIds;
    for (auto& [houseKey, house] :
            state.triggers.campaignHouseProduction) {
        (void)houseKey;
        if (house.playerControlled) {
            continue;
        }
        if (house.activeBuildId >= 0) {
            const auto build = state.triggers.campaignTeamBuilds.find(
                house.activeBuildId
            );
            if (build == state.triggers.campaignTeamBuilds.end()) {
                house.activeBuildId = -1;
                changed = true;
                continue;
            }
            if (!house.productionStarted || !house.aiTriggersEnabled) {
                if (build->second.deadlineTick
                    < std::numeric_limits<int>::max()) {
                    ++build->second.deadlineTick;
                }
                continue;
            }
            changed = advanceRuntimeCampaignTeamBuild(
                state,
                house,
                build->second
            ) || changed;
            if (build->second.failed) {
                dissolveRuntimeCampaignTeamBuild(state, build->second);
                reportRuntimeCampaignAiTriggerOutcome(
                    state,
                    build->second.aiTriggerId,
                    false
                );
                finishedBuildIds.push_back(build->first);
                house.activeBuildId = -1;
                continue;
            }
            if (!build->second.completed) {
                continue;
            }
            bool anyDead = false;
            for (int objectId : build->second.objectIds) {
                const WorldObjectInstance* object = findObjectById(
                    state,
                    objectId
                );
                anyDead = anyDead || object == nullptr || !object->alive;
            }
            if (anyDead) {
                dissolveRuntimeCampaignTeamBuild(state, build->second);
                reportRuntimeCampaignAiTriggerOutcome(
                    state,
                    build->second.aiTriggerId,
                    false
                );
                finishedBuildIds.push_back(build->first);
                house.activeBuildId = -1;
                changed = true;
                continue;
            }
            if (!runtimeCampaignTeamBuildReadyForMission(
                    state,
                    build->second
                )) {
                continue;
            }
            const RuntimeTeamType* team = findRuntimeTriggerTeamById(
                state,
                build->second.teamTypeId
            );
            const auto script = team == nullptr
                ? state.triggers.scripts.end()
                : state.triggers.scripts.find(team->scriptId);
            if (team == nullptr
                || script == state.triggers.scripts.end()
                || script->second.steps.empty()) {
                dissolveRuntimeCampaignTeamBuild(state, build->second);
                reportRuntimeCampaignAiTriggerOutcome(
                    state,
                    build->second.aiTriggerId,
                    false
                );
            } else {
                std::unordered_set<int>& teamObjects =
                    state.triggers.campaignTeamObjects[
                        toLowerAsciiLocal(trimAsciiLocal(team->id))
                    ];
                teamObjects.insert(
                    build->second.objectIds.begin(),
                    build->second.objectIds.end()
                );
                ++state.triggers.teamsCreated;
                issueRuntimeTriggerTeamOrder(
                    state,
                    *team,
                    build->second.objectIds,
                    build->second.aiTriggerId,
                    build->second.instanceId
                );
            }
            finishedBuildIds.push_back(build->first);
            house.activeBuildId = -1;
            ++state.triggers.campaignAiTriggerSelections;
            changed = true;
            continue;
        }
        if (!house.productionStarted
            || !house.aiTriggersEnabled
            || state.tick < house.nextAiTriggerCheckTick) {
            continue;
        }
        house.nextAiTriggerCheckTick = state.tick
            + std::max(1, state.config.fixedTickHz * 2);
        RuntimeCampaignAiTriggerDefinition* selected =
            selectRuntimeCampaignAiTrigger(state, house);
        if (selected == nullptr) {
            continue;
        }
        const RuntimeTeamType* team = findRuntimeTriggerTeamById(
            state,
            selected->primaryTeamId
        );
        if (team == nullptr) {
            continue;
        }
        RuntimeCampaignTeamBuild build;
        build.instanceId = state.triggers.nextCampaignTeamBuildId++;
        build.aiTriggerId = selected->id;
        build.teamTypeId = team->id;
        build.owner = house.owner;
        build.deadlineTick = state.tick + std::max(
            5000,
            state.config.fixedTickHz * 120
        );
        build.status = "selected " + selected->name;
        house.activeBuildId = build.instanceId;
        state.triggers.campaignTeamBuilds.emplace(
            build.instanceId,
            std::move(build)
        );
        ++selected->timesExecuted;
        selected->nextEligibleTick = house.nextAiTriggerCheckTick;
        changed = true;
    }
    std::sort(finishedBuildIds.begin(), finishedBuildIds.end());
    finishedBuildIds.erase(
        std::unique(finishedBuildIds.begin(), finishedBuildIds.end()),
        finishedBuildIds.end()
    );
    for (int buildId : finishedBuildIds) {
        state.triggers.campaignTeamBuilds.erase(buildId);
    }
    return changed;
}

bool runtimeProjectileProximityFuseTriggered(
    const RuntimeGameSessionState& state,
    const RuntimeProjectileEffect& projectile,
    const ProjectileType& projectileType,
    int fixedTickHz) {
    if (!projectileType.proximity
        || projectile.targetObjectId < 0
        || projectile.elapsedTicks <= 0) {
        return false;
    }
    const double dx = projectile.targetX - projectile.currentX;
    const double dy = projectile.targetY - projectile.currentY;
    const double dz = projectile.targetZ - projectile.currentZ;
    const double distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    const double step = runtimeUsesMissileStartupAcceleration(state, projectile, projectileType)
        ? runtimeMissileAccelerationStepCells(projectile.speedCellsPerTick,
            projectile.acceleration, projectile.elapsedTicks, fixedTickHz)
        : projectile.speedCellsPerTick;
    const double fuseDistance = std::max(
        0.35,
        std::max(0.01, step * std::max(1, fixedTickHz) / 15.0) * 1.5
    );
    return distance <= fuseDistance;
}

std::string runtimeCampaignLocalizedText(
    const RuntimeProductionHostState& state,
    const std::string& key) {
    if (key.empty()) {
        return {};
    }
    const std::wstring localized = runtimeMenuLabel(state, key, L"");
    if (localized.empty()) {
        return key;
    }
    return runtimeEncodePresentationUtf8(localized,key);
}

}
