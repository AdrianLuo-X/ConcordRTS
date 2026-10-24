#include "runtime/menu/RuntimeSkirmishController.h"
#include "core/players/PlayerSlotRole.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <set>
#include <utility>

namespace concord::skirmish {
void invalidate(RuntimeSkirmishController& state) { state.backDirty = true; state.services.requestRepaint(); }
std::wstring label(const RuntimeSkirmishController& state, std::initializer_list<std::string> keys, const wchar_t* fallback) { return state.services.label(keys,fallback); }
bool ensurePreview(RuntimeSkirmishController& state, int index) { return state.services.ensurePreview(index); }
const HitRegion* hitTest(const RuntimeSkirmishController& state, RuntimeInputPixelPoint point) {
    for(auto it=state.hits.rbegin(); it!=state.hits.rend(); ++it)
        if(point.x>=it->rect.left && point.x<it->rect.right && point.y>=it->rect.top && point.y<it->rect.bottom) return &*it;
    return nullptr;
}
std::string trimAscii(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lowerAscii(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return text;
}

bool equalsAsciiNoCase(const std::string& left, const std::string& right) {
    return lowerAscii(left) == lowerAscii(right);
}

std::string pathString(const fs::path& path) {
    return path.string();
}

bool isUnavailableCooperativeMode(const ModeInfo& mode) {
    return equalsAsciiNoCase(mode.key, "Cooperative")
        || equalsAsciiNoCase(mode.filter, "cooperative")
        || equalsAsciiNoCase(
            fs::path(mode.rulesOverride).filename().string(),
            "MPCoopMD.ini"
        );
}

bool mapSupportsFilter(const MapInfo& map, const std::string& filter) {
    for (const std::string& candidate : map.filters) {
        if (equalsAsciiNoCase(candidate, filter)) {
            return true;
        }
    }
    return false;
}

bool allowsAllMapsFallback(const std::string& filter) {
    return !equalsAsciiNoCase(filter, "cooperative");
}

bool modeIsCooperative(const RuntimeSkirmishController& state) {
    return state.selectedMode >= 0
        && state.selectedMode < static_cast<int>(state.modes.size())
        && isUnavailableCooperativeMode(
            state.modes[static_cast<std::size_t>(state.selectedMode)]
        );
}

const CooperativeScenarioDefinition* selectedCooperativeScenario(
    const RuntimeSkirmishController& state
) {
    if (!modeIsCooperative(state)
        || state.selectedMap < 0
        || state.selectedMap >= static_cast<int>(state.maps.size())) {
        return nullptr;
    }
    const MapInfo& map = state.maps[static_cast<std::size_t>(state.selectedMap)];
    const auto validation = state.cooperativeValidationByMap.find(
        lowerAscii(map.id)
    );
    if (validation == state.cooperativeValidationByMap.end()
        || !validation->second.valid) {
        return nullptr;
    }
    return state.services.cooperativeScenario(map.id);
}

bool cooperativeCountryAllowed(
    const CooperativeScenarioDefinition& scenario,
    int slotIndex,
    const std::string& country
) {
    const std::vector<std::string>& allowed = slotIndex < 2
        ? scenario.playerCountries
        : scenario.enemyCountries;
    return std::any_of(
        allowed.begin(),
        allowed.end(),
        [&country](const std::string& candidate) {
            return equalsAsciiNoCase(candidate, country);
        }
    );
}

std::string canonicalCooperativeCountry(
    const CooperativeScenarioDefinition& scenario,
    int slotIndex,
    const std::string& requested
) {
    const std::vector<std::string>& allowed = slotIndex < 2
        ? scenario.playerCountries
        : scenario.enemyCountries;
    const auto found = std::find_if(
        allowed.begin(),
        allowed.end(),
        [&requested](const std::string& candidate) {
            return equalsAsciiNoCase(candidate, requested);
        }
    );
    return found == allowed.end()
        ? (allowed.empty() ? std::string{} : allowed.front())
        : *found;
}

void normalizeCooperativeSelection(RuntimeSkirmishController& state) {
    const CooperativeScenarioDefinition* scenario =
        selectedCooperativeScenario(state);
    if (scenario == nullptr || state.selection.slots.size() < 4U) {
        return;
    }
    constexpr std::array<const char*, 4> teams{{"1", "1", "2", "2"}};
    for (std::size_t index = 0; index < state.selection.slots.size(); ++index) {
        RuntimeSkirmishPlayerSlot& slot = state.selection.slots[index];
        if (index >= 4U) {
            slot.enabled = false;
            slot.human = false;
            state.startingSpots[index] = 0;
            continue;
        }
        slot.enabled = true;
        slot.human = index == 0U;
        if (slot.name.empty()) {
            slot.name = index == 0U
                ? "Commander"
                : "Computer " + std::to_string(index);
        }
        slot.country = canonicalCooperativeCountry(
            *scenario,
            static_cast<int>(index),
            slot.country
        );
        slot.team = teams[index];
        slot.startPosition = static_cast<int>(index) + 1;
        state.startingSpots[index] = static_cast<int>(index) + 1;
    }
}

std::vector<int> cooperativeCountryOptionIndices(
    const RuntimeSkirmishController& state,
    int slotIndex
) {
    std::vector<int> result;
    const CooperativeScenarioDefinition* scenario =
        selectedCooperativeScenario(state);
    if (scenario == nullptr || slotIndex < 0 || slotIndex >= 4) {
        return result;
    }
    const std::vector<std::string>& allowed = slotIndex < 2
        ? scenario->playerCountries
        : scenario->enemyCountries;
    for (const std::string& country : allowed) {
        for (std::size_t index = 0; index < state.countries.size(); ++index) {
            if (equalsAsciiNoCase(state.countries[index].id, country)) {
                result.push_back(static_cast<int>(index));
                break;
            }
        }
    }
    return result;
}

std::vector<int> countryOptionIndices(const RuntimeSkirmishController& state, int slotIndex) {
    if (modeIsCooperative(state)) {
        return cooperativeCountryOptionIndices(state, slotIndex);
    }
    std::vector<int> options;
    const bool canObserve = !state.config.multiplayerActive && slotIndex >= 0
        && slotIndex < static_cast<int>(state.selection.slots.size())
        && state.selection.slots[static_cast<std::size_t>(slotIndex)].human;
    for (std::size_t index = 0; index < state.countries.size(); ++index) {
        if (!runtimeObserverCountry(state.countries[index].id) || canObserve) {
            options.push_back(static_cast<int>(index));
        }
    }
    return options;
}

int countryIndexForDropdownOption(
    const RuntimeSkirmishController& state,
    int option
) {
    const std::vector<int> options = countryOptionIndices(
        state,
        state.openDropdownSlot
    );
    return option >= 0 && option < static_cast<int>(options.size())
        ? options[static_cast<std::size_t>(option)]
        : -1;
}

void rebuildFilteredMaps(RuntimeSkirmishController& state, bool preserveSelection) {
    const int previousMap = state.selectedMap;
    state.filteredMaps.clear();
    if (state.selectedMode < 0
        || state.selectedMode >= static_cast<int>(state.modes.size())) {
        state.selectedMode = 0;
    }
    const std::string filter = state.modes.empty()
        ? "standard"
        : state.modes[static_cast<std::size_t>(state.selectedMode)].filter;
    const bool allowRandom = !state.modes.empty() && state.modes[state.selectedMode].randomMapsAllowed;
    const auto allowed = [&](const MapInfo& map) {
        return allowRandom || !state.services.generatedMapFile(map.path.filename().string());
    };
    for (std::size_t i = 0; i < state.maps.size(); ++i) {
        if (allowed(state.maps[i]) && mapSupportsFilter(state.maps[i], filter)) {
            state.filteredMaps.push_back(static_cast<int>(i));
        }
    }
    // An unavailable cooperative selection must never degrade into an
    // ordinary all-maps list: that makes authored missions start as a normal
    // skirmish.  Other legacy filters retain their existing fallback.
    if (state.filteredMaps.empty() && allowsAllMapsFallback(filter)) {
        for (std::size_t i = 0; i < state.maps.size(); ++i) {
            if (allowed(state.maps[i]) && !mapSupportsFilter(
                    state.maps[i],
                    "cooperative-invalid"
                )) {
                state.filteredMaps.push_back(static_cast<int>(i));
            }
        }
    }
    const bool retained = preserveSelection
        && std::find(
            state.filteredMaps.begin(),
            state.filteredMaps.end(),
            previousMap
        ) != state.filteredMaps.end();
    state.selectedMap = state.filteredMaps.empty()
        ? -1
        : (retained ? previousMap : state.filteredMaps.front());
    state.mapScroll = 0;
    if (state.selectedMap >= 0) {
        const auto selected = std::find(
            state.filteredMaps.begin(),
            state.filteredMaps.end(),
            state.selectedMap
        );
        if (selected != state.filteredMaps.end()) {
            const int position = static_cast<int>(selected - state.filteredMaps.begin());
            state.mapScroll = std::max(0, position - kVisibleMapRows / 2);
        }
    }
    normalizeCooperativeSelection(state);
}

int selectedMapCombatantLimit(const RuntimeSkirmishController& state) {
    if (state.selectedMap < 0
        || state.selectedMap >= static_cast<int>(state.maps.size())) {
        return kPlayerSlots;
    }
    return std::clamp(
        state.maps[static_cast<std::size_t>(state.selectedMap)].maxPlayers,
        1,
        kPlayerSlots
    );
}

int selectedMapLimit(const RuntimeSkirmishController& state) {
    const int observers = !modeIsCooperative(state) && !state.config.multiplayerActive
        ? static_cast<int>(std::count_if(state.selection.slots.begin(),
            state.selection.slots.end(), [](const auto& slot) {
                return runtimeObserverSlot(slot);
            })) : 0;
    return std::min(kPlayerSlots, selectedMapCombatantLimit(state) + observers);
}

void applyMapSlotLimit(RuntimeSkirmishController& state) {
    const int limit = selectedMapLimit(state);
    for (int i = limit; i < static_cast<int>(state.selection.slots.size()); ++i) {
        state.selection.slots[static_cast<std::size_t>(i)].enabled = false;
    }
    if (limit >= 2 && state.selection.slots.size() >= 2) {
        const bool hasOpponent = std::any_of(
            state.selection.slots.begin() + 1,
            state.selection.slots.begin() + limit,
            [](const RuntimeSkirmishPlayerSlot& slot) { return slot.enabled; }
        );
        if (!hasOpponent) {
            state.selection.slots[1].enabled = true;
        }
    }
    normalizeCooperativeSelection(state);
}

void syncSelection(RuntimeSkirmishController& state) {
    normalizeCooperativeSelection(state);
    state.selection.mapStartingPointLimit = 0;
    if (state.selectedMap >= 0
        && state.selectedMap < static_cast<int>(state.maps.size())) {
        state.selection.mapPath = pathString(
            state.maps[static_cast<std::size_t>(state.selectedMap)].path
        );
        state.selection.mapStartingPointLimit =
            state.maps[static_cast<std::size_t>(state.selectedMap)].maxPlayers;
    }
    if (state.selectedMode >= 0
        && state.selectedMode < static_cast<int>(state.modes.size())) {
        const ModeInfo& mode =
            state.modes[static_cast<std::size_t>(state.selectedMode)];
        state.selection.gameMode = mode.key;
        state.selection.gameModeRulesOverride = mode.rulesOverride;
    }
    if (!state.selection.slots.empty()) {
        state.selection.playerCountry = state.selection.slots.front().country;
        state.selection.playerColor = state.selection.slots.front().color;
        const int countryIndex = findCountryIndex(
            state,
            state.selection.playerCountry
        );
        state.selection.playerCountryDisplayName =
            countryIndex >= 0
                && countryIndex < static_cast<int>(state.countries.size())
            ? state.countries[static_cast<std::size_t>(countryIndex)].displayName
            : state.services.widenUtf8(state.selection.playerCountry);
        const std::string briefKey = state.services.loadingBriefKey(
            state.selection.playerCountry
        );
        state.selection.playerCountryBrief = briefKey.empty()
            ? std::wstring{}
            : state.services.text(briefKey, L"");
        state.selection.loadingScreenAssetId =
            state.services.loadingScreenAsset(
                state.selection.playerCountry
            );
    }
    const std::size_t count = std::min(
        state.selection.slots.size(),
        state.startingSpots.size()
    );
    for (std::size_t slotIndex = 0; slotIndex < count; ++slotIndex) {
        state.selection.slots[slotIndex].startPosition =
            std::max(0, state.startingSpots[slotIndex]);
    }
}

int findCountryIndex(const RuntimeSkirmishController& state, const std::string& id) {
    for (std::size_t i = 0; i < state.countries.size(); ++i) {
        if (equalsAsciiNoCase(state.countries[i].id, id)) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void buildPlayerSlots(RuntimeSkirmishController& state) {
    state.selection.slots.clear();
    if (!state.config.playerSlots.empty()) {
        for (const RuntimeConfig::PlayerSlot& source : state.config.playerSlots) {
            const std::size_t slotIndex = state.selection.slots.size();
            RuntimeSkirmishPlayerSlot slot;
            slot.enabled = source.enabled;
            slot.human = source.human;
            slot.name = source.name;
            slot.country = source.country;
            slot.color = source.color;
            slot.team = source.team;
            slot.difficulty = source.difficulty;
            slot.startPosition = std::max(0, source.startPosition);
            state.startingSpots[slotIndex] = slot.startPosition;
            state.selection.slots.push_back(std::move(slot));
            if (state.selection.slots.size() == kPlayerSlots) {
                break;
            }
        }
    }
    while (state.selection.slots.size() < kPlayerSlots) {
        const int index = static_cast<int>(state.selection.slots.size());
        RuntimeSkirmishPlayerSlot slot;
        slot.enabled = index <= 1;
        slot.human = index == 0;
        slot.name = index == 0 ? "Commander" : "Computer " + std::to_string(index);
        slot.country = index == 0 ? state.selection.playerCountry : "Random";
        slot.color = index == 0
            ? state.selection.playerColor
            : state.colorIds[static_cast<std::size_t>(index % state.colorIds.size())];
        slot.team = "-";
        slot.difficulty = "Medium";
        state.selection.slots.push_back(std::move(slot));
    }
    RuntimeSkirmishPlayerSlot& human = state.selection.slots.front();
    human.enabled = true;
    human.human = true;
    if (human.name.empty()) human.name = "Commander";
    if (human.country.empty() || equalsAsciiNoCase(human.country, "Random")) {
        human.country = state.selection.playerCountry;
    }
    if (human.color.empty() || equalsAsciiNoCase(human.color, "Random")) {
        human.color = state.selection.playerColor;
    }
    for (std::size_t index = 0; index < state.selection.slots.size(); ++index) {
        auto& slot = state.selection.slots[index];
        if (!runtimeObserverCountry(slot.country)) continue;
        if (!slot.human || state.config.multiplayerActive) {
            slot.country = "Random";
            continue;
        }
        slot.country = "Observer";
        slot.team = "-";
        slot.startPosition = 0;
        state.startingSpots[index] = 0;
    }
}

void resetControllerForEntry(RuntimeSkirmishController& state, const RuntimeConfig& config) {
    state.config = config;
    state.selection = {};
    state.startingSpots.fill(0);
    state.beforeCooperative.reset();
    state.selectedMode = 0;
    state.selectedMap = -1;
    state.mapScroll = 0;
    state.hits.clear();
    state.hover = {};
    state.pressed = {};
    state.mouseVisible = false;
    state.mousePoint = {};
    state.openDropdown = DropdownKind::None;
    state.openDropdownSlot = -1;
    state.dropdownHover = -1;
    state.draggingSlider = HitKind::None;
    state.closing = false;
    state.page = SkirmishPage::Lobby;
    state.phase = ShellMenuPhase::Idle;
    state.phaseStarted = 0;
    state.phaseTick = 0;
    state.mapPageOriginalMode = 0;
    state.mapPageOriginalMap = -1;
    state.mapPageOriginalSlots.clear();
    state.mapPageOriginalStarts.fill(0);
    state.mapPageOriginalBeforeCooperative.reset();
    state.backDirty = true;
    state.selection.gameMode = config.gameMode.empty() ? "Battle" : config.gameMode;
    state.selection.gameModeRulesOverride = config.gameModeRulesOverride;
    state.selection.playerCountry = config.playerCountry.empty()
        ? "Americans" : config.playerCountry;
    state.selection.playerColor = config.playerColor.empty()
        ? "Gold" : config.playerColor;
    state.services.loadDefaults(config, state.selection);
    buildPlayerSlots(state);

    for (std::size_t i = 0; i < state.modes.size(); ++i) {
        if (equalsAsciiNoCase(state.modes[i].key, state.selection.gameMode)) {
            state.selectedMode = static_cast<int>(i);
            break;
        }
    }
    const std::string defaultFile = lowerAscii(
        fs::path(config.defaultMapPath).filename().string()
    );
    for (std::size_t i = 0; i < state.maps.size(); ++i) {
        if (lowerAscii(state.maps[i].path.filename().string()) == defaultFile
            || lowerAscii(state.maps[i].id) == lowerAscii(fs::path(defaultFile).stem().string())) {
            state.selectedMap = static_cast<int>(i);
            break;
        }
    }
    rebuildFilteredMaps(state, true);
    if (state.selectedMap < 0 && !state.filteredMaps.empty()) {
        state.selectedMap = state.filteredMaps.front();
    }
    applyMapSlotLimit(state);
    normalizeCooperativeSelection(state);
    syncSelection(state);
    ensurePreview(state, state.selectedMap);
    state.status = state.maps.empty()
        ? L"No multiplayer maps were found."
        : label(state, {"GUI:Ready", "TXT_READY"}, L"Ready");
}

void scrollMapList(RuntimeSkirmishController& state, int amount, bool playSound) {
    const int maxScroll = std::max(
        0,
        static_cast<int>(state.filteredMaps.size()) - kVisibleMapRows
    );
    const int oldScroll = state.mapScroll;
    state.mapScroll = std::clamp(state.mapScroll + amount, 0, maxScroll);
    if (state.mapScroll != oldScroll && playSound) {
        state.services.playSound(amount < 0 ? "MenuSlideIn" : "MenuSlideOut");
    }
    invalidate(state);
}

void selectFilteredMap(RuntimeSkirmishController& state, int filteredIndex, bool playSound) {
    if (filteredIndex < 0
        || filteredIndex >= static_cast<int>(state.filteredMaps.size())) {
        return;
    }
    const int mapIndex = state.filteredMaps[static_cast<std::size_t>(filteredIndex)];
    if (state.selectedMap != mapIndex) {
        state.selectedMap = mapIndex;
        ensurePreview(state, state.selectedMap);
        applyMapSlotLimit(state);
        syncSelection(state);
        if (playSound) state.services.playSound("MenuTab", true);
    }
    if (filteredIndex < state.mapScroll) {
        state.mapScroll = filteredIndex;
    } else if (filteredIndex >= state.mapScroll + kVisibleMapRows) {
        state.mapScroll = filteredIndex - kVisibleMapRows + 1;
    }
    state.status = label(state, {"GUI:Ready", "TXT_READY"}, L"Ready");
    invalidate(state);
}

void finishMenuTransition(RuntimeSkirmishController& state) {
    if (state.closing) {
        return;
    }
    state.closing = true;
    state.services.stopMusic();
    state.services.destroyWindow();
}

bool updateMenuTransition(RuntimeSkirmishController& state, std::uint64_t now) {
    if (state.phase == ShellMenuPhase::Idle || state.closing) {
        return false;
    }
    bool changed = false;
    const std::uint64_t elapsed = now - state.phaseStarted;
    const int nextTick = static_cast<int>(
        elapsed / kShellAnimationFrameMs
    );
    if (nextTick != state.phaseTick) {
        state.phaseTick = nextTick;
        changed = true;
    }
    const std::uint64_t duration = static_cast<std::uint64_t>(
        kShellSlotCount - 1 + kShellAnimationFrames
    ) * kShellAnimationFrameMs;
    if (elapsed < duration) {
        return changed;
    }
    if (state.phase == ShellMenuPhase::SlideIn) {
        state.phase = ShellMenuPhase::Idle;
        state.phaseTick = 0;
        return true;
    }
    finishMenuTransition(state);
    return true;
}

void completeMenu(RuntimeSkirmishController& state, bool accepted) {
    if (state.closing
        || state.phase == ShellMenuPhase::SlideOut) {
        return;
    }
    syncSelection(state);
    state.selection.accepted = accepted;
    if (accepted) {
        std::string saveError;
        if (!state.services.saveCratePreference(
                state.config, state.selection.crates, saveError)) {
            // A preference write failure must not discard a valid match setup.
            std::cerr << "Could not save skirmish crate preference: " << saveError << '\n';
        }
    }
    if (accepted && !state.config.multiplayerActive) {
        std::string saveError;
        if (!state.services.saveNoCooldownPreference(
                state.config, state.selection.noCooldown, saveError))
            std::cerr << "Could not save skirmish no-cooldown preference: " << saveError << '\n';
    }
    if (!accepted && !state.smoke) {
        state.services.prewarmMainMenu();
    }
    state.phase = ShellMenuPhase::SlideOut;
    state.phaseStarted = state.services.nowMs();
    state.phaseTick = 0;
    state.hover = {};
    state.pressed = {};
    state.openDropdown = DropdownKind::None;
    state.openDropdownSlot = -1;
    state.dropdownHover = -1;
    state.draggingSlider = HitKind::None;
    state.services.playSound("MenuSlideOut", true);
    invalidate(state);
}

DropdownKind dropdownKindForTarget(HitKind kind) {
    switch (kind) {
    case HitKind::SlotEnabled:
    case HitKind::SlotDifficulty:
        return DropdownKind::Player;
    case HitKind::SlotCountry:
        return DropdownKind::Country;
    case HitKind::SlotColor:
        return DropdownKind::Color;
    case HitKind::SlotStart:
        return DropdownKind::Start;
    case HitKind::SlotTeam:
        return DropdownKind::Team;
    default:
        return DropdownKind::None;
    }
}

HitKind hitKindForDropdown(DropdownKind kind) {
    switch (kind) {
    case DropdownKind::Player: return HitKind::SlotEnabled;
    case DropdownKind::Country: return HitKind::SlotCountry;
    case DropdownKind::Color: return HitKind::SlotColor;
    case DropdownKind::Start: return HitKind::SlotStart;
    case DropdownKind::Team: return HitKind::SlotTeam;
    default: return HitKind::None;
    }
}

void closeDropdown(RuntimeSkirmishController& state, bool playSound) {
    if (state.openDropdown == DropdownKind::None) return;
    state.openDropdown = DropdownKind::None;
    state.openDropdownSlot = -1;
    state.dropdownHover = -1;
    if (playSound) state.services.playSound("MenuACBClose", true);
    invalidate(state);
}

bool dropdownCanOpen(
    const RuntimeSkirmishController& state,
    DropdownKind kind,
    int slotIndex
) {
    if (state.page != SkirmishPage::Lobby
        || kind == DropdownKind::None
        || slotIndex < 0
        || slotIndex >= selectedMapLimit(state)
        || slotIndex >= static_cast<int>(state.selection.slots.size())) {
        return false;
    }
    const RuntimeSkirmishPlayerSlot& slot =
        state.selection.slots[static_cast<std::size_t>(slotIndex)];
    if (modeIsCooperative(state)
        && (kind == DropdownKind::Player
            || kind == DropdownKind::Start
            || kind == DropdownKind::Team)) {
        return false;
    }
    if (kind == DropdownKind::Player) {
        return slotIndex > 0 && !slot.human && !state.difficulties.empty();
    }
    if (!slot.enabled) return false;
    if (kind == DropdownKind::Country) return !state.countries.empty();
    if (runtimeObserverSlot(slot)
        && (kind == DropdownKind::Start || kind == DropdownKind::Team)) return false;
    if (kind == DropdownKind::Color) return !state.colorIds.empty();
    if (kind == DropdownKind::Team) return !state.teams.empty();
    return kind == DropdownKind::Start;
}

int dropdownOptionCount(const RuntimeSkirmishController& state, DropdownKind kind) {
    switch (kind) {
    case DropdownKind::Player:
        return static_cast<int>(state.difficulties.size()) + 1;
    case DropdownKind::Country:
        return static_cast<int>(countryOptionIndices(
            state, state.openDropdownSlot).size());
    case DropdownKind::Color:
        return static_cast<int>(state.colorIds.size());
    case DropdownKind::Start:
        return selectedMapCombatantLimit(state) + 1;
    case DropdownKind::Team:
        return static_cast<int>(state.teams.size());
    default:
        return 0;
    }
}

int currentDropdownOption(const RuntimeSkirmishController& state) {
    if (!dropdownCanOpen(state, state.openDropdown, state.openDropdownSlot)) {
        return 0;
    }
    const RuntimeSkirmishPlayerSlot& slot = state.selection.slots[
        static_cast<std::size_t>(state.openDropdownSlot)
    ];
    switch (state.openDropdown) {
    case DropdownKind::Player:
        return slot.enabled
            ? findStringIndex(state.difficulties, slot.difficulty) + 1
            : 0;
    case DropdownKind::Country:
        {
            const std::vector<int> options = countryOptionIndices(
                state,
                state.openDropdownSlot
            );
            const int globalIndex = findCountryIndex(state, slot.country);
            const auto found = std::find(
                options.begin(),
                options.end(),
                globalIndex
            );
            return found == options.end()
                ? 0
                : static_cast<int>(found - options.begin());
        }
    case DropdownKind::Color:
        return findStringIndex(state.colorIds, slot.color);
    case DropdownKind::Start:
        return std::clamp(
            state.startingSpots[static_cast<std::size_t>(state.openDropdownSlot)],
            0,
            selectedMapCombatantLimit(state)
        );
    case DropdownKind::Team:
        return findStringIndex(state.teams, slot.team);
    default:
        return 0;
    }
}

void toggleDropdown(
    RuntimeSkirmishController& state,
    DropdownKind kind,
    int slotIndex
) {
    if (!dropdownCanOpen(state, kind, slotIndex)) {
        closeDropdown(state, false);
        return;
    }
    if (state.openDropdown == kind && state.openDropdownSlot == slotIndex) {
        closeDropdown(state, true);
        return;
    }
    state.openDropdown = kind;
    state.openDropdownSlot = slotIndex;
    state.dropdownHover = currentDropdownOption(state);
    state.services.playSound("MenuACBOpen", true);
    invalidate(state);
}

void selectDropdownOption(RuntimeSkirmishController& state, int option) {
    const DropdownKind kind = state.openDropdown;
    const int slotIndex = state.openDropdownSlot;
    if (!dropdownCanOpen(state, kind, slotIndex)
        || option < 0
        || option >= dropdownOptionCount(state, kind)) {
        closeDropdown(state, true);
        return;
    }
    RuntimeSkirmishPlayerSlot& slot =
        state.selection.slots[static_cast<std::size_t>(slotIndex)];
    switch (kind) {
    case DropdownKind::Player:
        slot.enabled = option > 0;
        if (slot.enabled) {
            slot.difficulty = state.difficulties[
                static_cast<std::size_t>(option - 1)
            ];
        }
        break;
    case DropdownKind::Country:
        if (const int countryIndex = countryIndexForDropdownOption(
                state,
                option
            ); countryIndex >= 0
                && countryIndex < static_cast<int>(state.countries.size())) {
            slot.country = state.countries[
                static_cast<std::size_t>(countryIndex)
            ].id;
            if (runtimeObserverSlot(slot)) {
                slot.team = "-";
                slot.startPosition = 0;
                state.startingSpots[static_cast<std::size_t>(slotIndex)] = 0;
            }
            applyMapSlotLimit(state);
        }
        break;
    case DropdownKind::Color:
        slot.color = state.colorIds[static_cast<std::size_t>(option)];
        break;
    case DropdownKind::Start:
        state.startingSpots[static_cast<std::size_t>(slotIndex)] = option;
        break;
    case DropdownKind::Team:
        slot.team = state.teams[static_cast<std::size_t>(option)];
        break;
    default:
        break;
    }
    syncSelection(state);
    closeDropdown(state, true);
}

bool dropdownContainsTarget(const RuntimeSkirmishController& state, HitTarget target) {
    return target.kind == HitKind::DropdownOption
        || (dropdownKindForTarget(target.kind) == state.openDropdown
            && target.index == state.openDropdownSlot);
}

void cycleString(
    std::string& value,
    const std::vector<std::string>& choices,
    int direction
) {
    const int current = findStringIndex(choices, value);
    const int next = wrappedIndex(choices, current, direction);
    if (next >= 0) value = choices[static_cast<std::size_t>(next)];
}

void adjustCredits(RuntimeSkirmishSelection& selection, int direction) {
    static const std::vector<int> values{
        5000, 10000, 20000, 30000, 50000, 100000, 200000
    };
    auto found = std::lower_bound(values.begin(), values.end(), selection.startingCredits);
    int index = found == values.end()
        ? static_cast<int>(values.size()) - 1
        : static_cast<int>(found - values.begin());
    if (found != values.end() && *found == selection.startingCredits) {
        index += direction;
    } else if (direction < 0) {
        --index;
    }
    index = std::clamp(index, 0, static_cast<int>(values.size()) - 1);
    selection.startingCredits = values[static_cast<std::size_t>(index)];
}

const std::array<int, 7>& startingCreditValues() {
    static const std::array<int, 7> values{{
        5000, 10000, 20000, 30000, 50000, 100000, 200000
    }};
    return values;
}

bool isSliderTarget(HitKind kind) {
    return kind == HitKind::SpeedSlider
        || kind == HitKind::CreditsSlider
        || kind == HitKind::UnitsSlider;
}

int sliderMaximum(HitKind kind) {
    if (kind == HitKind::CreditsSlider) {
        return static_cast<int>(startingCreditValues().size()) - 1;
    }
    if (kind == HitKind::UnitsSlider) return 10;
    if (kind == HitKind::SpeedSlider) return 6;
    return 0;
}

void setSliderFromBaseX(RuntimeSkirmishController& state, HitKind kind, int baseX) {
    if (!isSliderTarget(kind)) return;
    constexpr int trackLeft = 373;
    constexpr int trackWidth = 79;
    const int gripWidth = state.services.sliderGripWidth();
    const int travel = std::max(1, trackWidth - gripWidth);
    const int gripCenterOffset = std::max(0, gripWidth / 2);
    const int positionPixels = std::clamp(
        baseX - trackLeft - gripCenterOffset,
        0,
        travel
    );
    const int maximum = sliderMaximum(kind);
    const int position = std::clamp(
        (positionPixels * maximum + travel / 2) / travel,
        0,
        maximum
    );
    if (kind == HitKind::SpeedSlider) {
        // Level 6 is fastest, so the right end of the slider is fastest.
        state.selection.gameSpeed = position;
    } else if (kind == HitKind::UnitsSlider) {
        state.selection.unitCount = position;
    } else {
        state.selection.startingCredits =
            startingCreditValues()[static_cast<std::size_t>(position)];
    }
    invalidate(state);
}

std::string skirmishLobbyAllianceTeam(const std::string& value) {
    const std::string team = lowerAscii(trimAscii(value));
    return team.empty() || team == "-" || team == "0"
        || team == "none" || team == "no team" ? std::string{} : team;
}

bool canStart(const RuntimeSkirmishController& state, std::wstring& reason) {
    if (state.selectedMap < 0
        || state.selectedMap >= static_cast<int>(state.maps.size())) {
        reason = L"Select a map before starting.";
        return false;
    }
    const MapInfo& map = state.maps[static_cast<std::size_t>(state.selectedMap)];
    if (modeIsCooperative(state)) {
        const CooperativeScenarioDefinition* scenario =
            selectedCooperativeScenario(state);
        if (!state.cooperativeDataComplete
            || scenario == nullptr
            || state.selection.slots.size() < 4U) {
            reason = L"Cooperative mission data is unavailable or invalid.";
            return false;
        }
        constexpr std::array<const char*, 4> expectedTeams{{
            "1", "1", "2", "2"
        }};
        for (std::size_t index = 0; index < 4U; ++index) {
            const RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[index];
            if (!slot.enabled
                || slot.human != (index == 0U)
                || slot.team != expectedTeams[index]
                || state.startingSpots[index]
                    != static_cast<int>(index) + 1
                || !cooperativeCountryAllowed(
                    *scenario,
                    static_cast<int>(index),
                    slot.country
                )) {
                reason = L"Cooperative A-D slot setup is invalid.";
                return false;
            }
        }
    }
    int activePlayers = 0;
    const int limit = selectedMapLimit(state);
    for (int i = 0; i < limit; ++i) {
        const auto& slot = state.selection.slots[static_cast<std::size_t>(i)];
        if (slot.enabled && !runtimeObserverSlot(slot)) {
            ++activePlayers;
        }
    }
    if (activePlayers < map.minPlayers) {
        reason = L"This map needs at least " + std::to_wstring(map.minPlayers)
            + L" active players (observers do not count).";
        return false;
    }
    const bool freeForAll = state.selectedMode >= 0
        && state.selectedMode < static_cast<int>(state.modes.size())
        && equalsAsciiNoCase(state.modes[state.selectedMode].key, "FreeForAll");
    std::optional<std::string> firstTeam;
    bool hasOpponent = false;
    for (int i = 0; i < limit; ++i) {
        const auto& slot = state.selection.slots[static_cast<std::size_t>(i)];
        if (!slot.enabled || runtimeObserverSlot(slot)) continue;
        const std::string team = skirmishLobbyAllianceTeam(slot.team);
        if (firstTeam.has_value()) {
            hasOpponent = hasOpponent || freeForAll || firstTeam->empty()
                || team.empty() || *firstTeam != team;
        } else {
            firstTeam = team;
        }
    }
    if (!hasOpponent) {
        reason = L"至少需要一位敵對玩家。請選擇不同小隊，或將小隊設為「無」。";
        return false;
    }
    if (state.selectedMode >= 0
        && state.selectedMode < static_cast<int>(state.modes.size())
        && equalsAsciiNoCase(
            state.modes[static_cast<std::size_t>(state.selectedMode)].key,
            "TeamGame"
        )) {
        std::set<std::string> teams;
        for (int i = 0; i < limit; ++i) {
            const RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[static_cast<std::size_t>(i)];
            if (!slot.enabled || runtimeObserverSlot(slot)) {
                continue;
            }
            const std::string team = skirmishLobbyAllianceTeam(slot.team);
            if (team.empty()) {
                reason = L"Team Game requires a non-zero team for every player.";
                return false;
            }
            teams.insert(team);
        }
        if (teams.size() < 2) {
            reason = L"Team Game requires at least two opposing teams.";
            return false;
        }
    }
    return true;
}

void selectSkirmishMode(RuntimeSkirmishController& state, int modeIndex) {
    const bool wasCooperative = modeIsCooperative(state);
    const bool becomesCooperative = isUnavailableCooperativeMode(state.modes[modeIndex]);
    if (!wasCooperative && becomesCooperative) {
        state.beforeCooperative = SkirmishSlotSetup{state.selection.slots, state.startingSpots};
    } else if (wasCooperative && !becomesCooperative) {
        if (state.beforeCooperative.has_value()) {
            state.selection.slots = std::move(state.beforeCooperative->slots);
            state.startingSpots = state.beforeCooperative->starts;
            state.beforeCooperative.reset();
        } else {
            // A menu opened directly in Cooperative has no earlier ordinary
            // setup. Do not carry the forced A/B alliance into its next match.
            for (auto& slot : state.selection.slots) {
                slot.team = "-";
                slot.startPosition = 0;
            }
            state.startingSpots.fill(0);
        }
    }
    state.selectedMode = modeIndex;
}

void activateTarget(RuntimeSkirmishController& state, HitTarget target, int direction) {
    if (target.kind == HitKind::None || state.closing) {
        return;
    }
    const int mapLimit = selectedMapLimit(state);
    switch (target.kind) {
    case HitKind::Mode:
        if (target.index >= 0
            && target.index < static_cast<int>(state.modes.size())) {
            selectSkirmishMode(state, target.index);
            rebuildFilteredMaps(state, true);
            applyMapSlotLimit(state);
            syncSelection(state);
            ensurePreview(state, state.selectedMap);
            state.services.playSound("MenuTab", true);
        }
        break;
    case HitKind::Map:
        selectFilteredMap(state, target.index, true);
        return;
    case HitKind::MapScrollUp:
        scrollMapList(state, -1, true);
        return;
    case HitKind::MapScrollDown:
        scrollMapList(state, 1, true);
        return;
    case HitKind::SlotEnabled:
        if (!modeIsCooperative(state)
            && target.index > 0 && target.index < mapLimit) {
            RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[static_cast<std::size_t>(target.index)];
            int occupation = slot.enabled
                ? findStringIndex(state.difficulties, slot.difficulty) + 1
                : 0;
            occupation = (occupation + (direction > 0 ? 1 : 3)) % 4;
            slot.enabled = occupation != 0;
            if (slot.enabled) {
                slot.difficulty = state.difficulties[
                    static_cast<std::size_t>(occupation - 1)
                ];
            }
            state.services.playSound("MenuClick", true);
        }
        break;
    case HitKind::SlotCountry:
        // The active shell handles countries as a real dropdown in
        // activateShellTarget.  Never fall back to click-to-cycle behavior.
        break;
    case HitKind::SlotColor:
        if (target.index >= 0 && target.index < mapLimit) {
            RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[static_cast<std::size_t>(target.index)];
            if (slot.enabled) {
                cycleString(slot.color, state.colorIds, direction);
                state.services.playSound(direction > 0 ? "MenuACBOpen" : "MenuACBClose", true);
            }
        }
        break;
    case HitKind::SlotStart:
        if (!modeIsCooperative(state)
            && target.index >= 0 && target.index < mapLimit) {
            int& start = state.startingSpots[static_cast<std::size_t>(target.index)];
            if (runtimeObserverSlot(state.selection.slots[static_cast<std::size_t>(target.index)])) break;
            const int choices = selectedMapCombatantLimit(state) + 1;
            start = (start + (direction > 0 ? 1 : choices - 1)) % choices;
            state.services.playSound("MenuACBOpen", true);
        }
        break;
    case HitKind::SlotTeam:
        if (!modeIsCooperative(state)
            && target.index >= 0 && target.index < mapLimit) {
            RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[static_cast<std::size_t>(target.index)];
            if (slot.enabled && !runtimeObserverSlot(slot)) {
                cycleString(slot.team, state.teams, direction);
                state.services.playSound(direction > 0 ? "MenuACBOpen" : "MenuACBClose", true);
            }
        }
        break;
    case HitKind::SlotDifficulty:
        if (target.index > 0 && target.index < mapLimit) {
            RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[static_cast<std::size_t>(target.index)];
            if (slot.enabled) {
                cycleString(slot.difficulty, state.difficulties, direction);
                state.services.playSound(direction > 0 ? "MenuACBOpen" : "MenuACBClose", true);
            }
        }
        break;
    case HitKind::CreditsMinus:
        adjustCredits(state.selection, -1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::CreditsPlus:
        adjustCredits(state.selection, 1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::UnitsMinus:
        state.selection.unitCount = std::max(0, state.selection.unitCount - 1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::UnitsPlus:
        state.selection.unitCount = std::min(10, state.selection.unitCount + 1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::SpeedMinus:
        state.selection.gameSpeed = std::max(0, state.selection.gameSpeed - 1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::SpeedPlus:
        state.selection.gameSpeed = std::min(6, state.selection.gameSpeed + 1);
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleShortGame:
        state.selection.shortGame = !state.selection.shortGame;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleMcvRepacks:
        state.selection.mcvRepacks = !state.selection.mcvRepacks;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleCrates:
        state.selection.crates = !state.selection.crates;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleSuperweapons:
        state.selection.superweapons = !state.selection.superweapons;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleBuildOffAlly:
        state.selection.buildOffAlly = !state.selection.buildOffAlly;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::ToggleNoCooldown:
        if (!state.config.multiplayerActive) {
            state.selection.noCooldown = !state.selection.noCooldown;
            state.services.playSound("MenuClick", true);
        }
        break;
    case HitKind::ToggleComputerAi:
        state.selection.computerAiEnabled =
            !state.selection.computerAiEnabled;
        state.services.playSound("MenuClick", true);
        break;
    case HitKind::Start: {
        std::wstring reason;
        if (canStart(state, reason)) {
            completeMenu(state, true);
            return;
        }
        state.status = std::move(reason);
        state.services.playSound("MenuClick", true);
        state.services.warnInvalidSelection();
        break;
    }
    case HitKind::Back:
        completeMenu(state, false);
        return;
    default:
        break;
    }
    syncSelection(state);
    invalidate(state);
}

void computeShellViewport(RuntimeSkirmishController& state, int width, int height) {
    const double fit = std::min(
        static_cast<double>(std::max(1, width)) / kBaseWidth,
        static_cast<double>(std::max(1, height)) / kBaseHeight
    );
    const int integral = static_cast<int>(std::floor(fit));
    const double scale = integral >= 1 ? static_cast<double>(integral) : fit;
    state.viewWidth = std::max(1, static_cast<int>(std::lround(kBaseWidth * scale)));
    state.viewHeight = std::max(1, static_cast<int>(std::lround(kBaseHeight * scale)));
    state.viewLeft = (width - state.viewWidth) / 2;
    state.viewTop = (height - state.viewHeight) / 2;
}

RuntimeInputPixelPoint shellClientToBase(const RuntimeSkirmishController& state, RuntimeInputPixelPoint point) {
    if (point.x < state.viewLeft || point.y < state.viewTop
        || point.x >= state.viewLeft + state.viewWidth
        || point.y >= state.viewTop + state.viewHeight) {
        return RuntimeInputPixelPoint{-1, -1};
    }
    return RuntimeInputPixelPoint{
        (point.x - state.viewLeft) * kBaseWidth / std::max(1, state.viewWidth),
        (point.y - state.viewTop) * kBaseHeight / std::max(1, state.viewHeight)
    };
}

bool updateShellMouse(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
    const RuntimeInputPixelPoint base = shellClientToBase(state, client);
    const bool visible = state.services.cursorAvailable() && base.x >= 0;
    const bool changed = state.mouseVisible != visible
        || (visible && (state.mousePoint.x != base.x || state.mousePoint.y != base.y));
    state.mousePoint = base;
    state.mouseVisible = visible;
    if (changed) invalidate(state);
    return changed;
}

bool isShellNavigationButton(HitKind kind) {
    return kind == HitKind::ChooseMap || kind == HitKind::UseMap
        || kind == HitKind::CancelMap || kind == HitKind::Start || kind == HitKind::Back;
}

void activateShellTarget(RuntimeSkirmishController& state, HitTarget target, int direction) {
    if (state.closing || state.phase != ShellMenuPhase::Idle) {
        return;
    }
    if (state.services.activateRandomTarget(target, direction)) return;

    const DropdownKind targetDropdown = dropdownKindForTarget(target.kind);
    if (targetDropdown != DropdownKind::None) {
        toggleDropdown(state, targetDropdown, target.index);
        return;
    }
    if (target.kind == HitKind::DropdownOption) {
        selectDropdownOption(state, target.index);
        return;
    }
    if (state.openDropdown != DropdownKind::None) {
        closeDropdown(state, true);
    }
    if (target.kind == HitKind::ChooseMap) {
        state.mapPageOriginalMode = state.selectedMode;
        state.mapPageOriginalMap = state.selectedMap;
        state.mapPageOriginalSlots = state.selection.slots;
        state.mapPageOriginalStarts = state.startingSpots;
        state.mapPageOriginalBeforeCooperative = state.beforeCooperative;
        state.page = SkirmishPage::MapSelection;
        state.status = label(state, {"GUI:ChooseMap"}, L"Choose a map");
        state.services.playSound("MenuSlideIn", true);
        invalidate(state);
        return;
    }
    if (target.kind == HitKind::UseMap) {
        state.page = SkirmishPage::Lobby;
        applyMapSlotLimit(state);
        syncSelection(state);
        state.status = label(state, {"GUI:Ready", "TXT_READY"}, L"Ready");
        state.services.playSound("MenuSlideOut", true);
        invalidate(state);
        return;
    }
    if (target.kind == HitKind::CancelMap) {
        state.selectedMode = state.mapPageOriginalMode;
        rebuildFilteredMaps(state, false);
        state.selectedMap = state.mapPageOriginalMap;
        state.selection.slots = state.mapPageOriginalSlots;
        state.startingSpots = state.mapPageOriginalStarts;
        state.beforeCooperative = state.mapPageOriginalBeforeCooperative;
        syncSelection(state);
        state.page = SkirmishPage::Lobby;
        state.status = label(state, {"GUI:Ready", "TXT_READY"}, L"Ready");
        state.services.playSound("MenuSlideOut", true);
        invalidate(state);
        return;
    }
    activateTarget(state, target, direction);
}

void handleSkirmishMouseMove(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
        updateShellMouse(state, client);
        const auto base = shellClientToBase(state, client);
        if (isSliderTarget(state.draggingSlider)) {
            setSliderFromBaseX(state, state.draggingSlider, base.x);
            return;
        }
        const HitRegion* hit = hitTest(state, base);
        const HitTarget next = hit == nullptr ? HitTarget{} : hit->target;
        bool dropdownHoverChanged = false;
        if (next.kind == HitKind::DropdownOption
            && next.index != state.dropdownHover) {
            state.dropdownHover = next.index;
            dropdownHoverChanged = true;
        }
        if (next != state.hover || dropdownHoverChanged) {
            state.hover = next;
            invalidate(state);
        }
        return;
    }

void handleSkirmishLeftDown(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
        state.services.focus();
        const auto base = shellClientToBase(state, client);
        const HitRegion* hit = hitTest(state, base);
        const HitTarget target = hit == nullptr ? HitTarget{} : hit->target;
        if (state.openDropdown != DropdownKind::None
            && !dropdownContainsTarget(state, target)) {
            closeDropdown(state, true);
        }
        if (target.kind == HitKind::DropdownOption) {
            state.dropdownHover = target.index;
        }
        if (isSliderTarget(target.kind)) {
            state.draggingSlider = target.kind;
            state.pressed = target;
            state.services.capture();
            setSliderFromBaseX(state, target.kind, base.x);
            return;
        }
        state.pressed = target;
        if (isShellNavigationButton(target.kind)) state.services.playSound("MenuClick", true);
        if (state.pressed.kind != HitKind::None) state.services.capture();
        invalidate(state);
        return;
    }

void handleSkirmishLeftUp(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
        const auto base = shellClientToBase(state, client);
        const HitRegion* hit = hitTest(state, base);
        const HitTarget released = hit == nullptr ? HitTarget{} : hit->target;
        const HitTarget pressed = state.pressed;
        if (isSliderTarget(state.draggingSlider)) {
            setSliderFromBaseX(state, state.draggingSlider, base.x);
            state.draggingSlider = HitKind::None;
            state.pressed = {};
            state.services.releaseCaptureIfOwned();
            state.services.playSound("MenuClick", true);
            syncSelection(state);
            invalidate(state);
            return;
        }
        state.pressed = {};
        state.services.releaseCaptureIfOwned();
        if (released == pressed && pressed.kind != HitKind::None) {
            activateShellTarget(state, pressed, 1);
        } else invalidate(state);
        return;
    }

void handleSkirmishDoubleClick(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
        if (state.page != SkirmishPage::MapSelection) return;
        const auto base = shellClientToBase(state, client);
        const HitRegion* hit = hitTest(state, base);
        if (hit != nullptr && hit->target.kind == HitKind::Map) {
            const HitTarget mapTarget = hit->target;
            activateShellTarget(state, mapTarget, 1);
            activateShellTarget(state, HitTarget{HitKind::UseMap, -1}, 1);
        }
        return;
    }

void handleSkirmishRightUp(RuntimeSkirmishController& state, RuntimeInputPixelPoint client) {
        const auto base = shellClientToBase(state, client);
        const HitRegion* hit = hitTest(state, base);
        if (hit != nullptr) activateShellTarget(state, hit->target, -1);
        return;
    }

void handleSkirmishMouseLeave(RuntimeSkirmishController& state) {
        updateShellMouse(state, RuntimeInputPixelPoint{-1, -1});
        if (state.hover.kind != HitKind::None) {
            state.hover = {};
            invalidate(state);
        }
        return;
}

void handleSkirmishCaptureLost(RuntimeSkirmishController& state) {
        state.draggingSlider = HitKind::None;
        state.pressed = {};
        invalidate(state);
        return;
}

bool handleSkirmishKey(RuntimeSkirmishController& state, RuntimeSkirmishKey key) {
        if (state.openDropdown != DropdownKind::None) {
            const int count = dropdownOptionCount(state, state.openDropdown);
            if (key == RuntimeSkirmishKey::Escape) {
                closeDropdown(state, true);
                return true;
            }
            if (count > 0 && (key == RuntimeSkirmishKey::Up || key == RuntimeSkirmishKey::Down
                || key == RuntimeSkirmishKey::Home || key == RuntimeSkirmishKey::End)) {
                int next = state.dropdownHover;
                if (next < 0 || next >= count) {
                    next = currentDropdownOption(state);
                }
                if (key == RuntimeSkirmishKey::Home) next = 0;
                else if (key == RuntimeSkirmishKey::End) next = count - 1;
                else next = (next + (key == RuntimeSkirmishKey::Up ? count - 1 : 1)) % count;
                state.dropdownHover = next;
                state.services.playSound("MenuTab");
                invalidate(state);
                return true;
            }
            if (count > 0 && (key == RuntimeSkirmishKey::Return || key == RuntimeSkirmishKey::Space)) {
                const int selected = std::clamp(
                    state.dropdownHover,
                    0,
                    count - 1
                );
                activateShellTarget(
                    state,
                    HitTarget{HitKind::DropdownOption, selected},
                    1
                );
                return true;
            }
        }
        if (state.page == SkirmishPage::RandomMap || state.page == SkirmishPage::RandomMapLoad || state.page == SkirmishPage::RandomMapPreview) {
            if (key == RuntimeSkirmishKey::Escape) activateShellTarget(state, {HitKind::RandomCancel, -1}, 1);
            else if (key == RuntimeSkirmishKey::Return) activateShellTarget(state,
                {state.page == SkirmishPage::RandomMapLoad ? HitKind::RandomLoadSelected
                    : state.services.randomMapReady() ? HitKind::RandomUse : HitKind::RandomGenerate, -1}, 1);
            return true;
        }
        if (key == RuntimeSkirmishKey::Escape) {
            state.services.playSound("MenuClick", true);
            activateShellTarget(state,
                HitTarget{state.page == SkirmishPage::Lobby
                    ? HitKind::Back : HitKind::CancelMap, -1}, 1);
            return true;
        }
        if (key == RuntimeSkirmishKey::Return) {
            state.services.playSound("MenuClick", true);
            activateShellTarget(state,
                HitTarget{state.page == SkirmishPage::Lobby
                    ? HitKind::Start : HitKind::UseMap, -1}, 1);
            return true;
        }
        if (state.page == SkirmishPage::MapSelection
            && (key == RuntimeSkirmishKey::Up || key == RuntimeSkirmishKey::Down)
            && !state.filteredMaps.empty()) {
            const auto current = std::find(
                state.filteredMaps.begin(), state.filteredMaps.end(), state.selectedMap
            );
            int position = current == state.filteredMaps.end()
                ? 0 : static_cast<int>(current - state.filteredMaps.begin());
            position = std::clamp(position + (key == RuntimeSkirmishKey::Up ? -1 : 1),
                0, static_cast<int>(state.filteredMaps.size()) - 1);
            selectFilteredMap(state, position, true);
            return true;
        }
        return false;
}

void handleSkirmishWheel(RuntimeSkirmishController& state, int notches) {
    if(state.page == SkirmishPage::MapSelection) scrollMapList(state, -notches * 3, false);
}
} // namespace concord::skirmish
