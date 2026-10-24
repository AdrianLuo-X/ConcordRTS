#pragma once
#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeSkirmishSelection.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include "data/maps/MapDatabase.h"
#include "data/campaign/CooperativeScenarioDatabase.h"
#include <array>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace concord::skirmish {
namespace fs = std::filesystem;
inline constexpr int kBaseWidth = 800;
inline constexpr int kBaseHeight = 600;
inline constexpr int kShellSlotCount = 8;
inline constexpr int kShellAnimationFrames = 6;
inline constexpr std::uint64_t kShellAnimationFrameMs = 42;
inline constexpr int kPlayerSlots = 8;
inline constexpr int kVisibleMapRows = 9;

struct MapInfo {
    fs::path path;
    std::string id;
    std::wstring displayName;
    std::string theater;
    std::string size;
    int minPlayers = 2;
    int maxPlayers = 8;
    int previewWidth = 0;
    int previewHeight = 0;
    int headerX = 0;
    int headerY = 0;
    int headerWidth = 0;
    int headerHeight = 0;
    std::vector<RuntimeInputPixelPoint> startPoints;
    std::vector<std::string> filters;
    bool metadataLoaded = false;
    bool previewAttempted = false;
    MapPreviewImage preview;
    std::string previewError;
};

struct ModeInfo {
    int id = 0;
    std::string key;
    std::string filter = "standard";
    bool randomMapsAllowed = false;
    std::string rulesOverride;
    std::wstring displayName;
};

struct CountryInfo {
    std::string id;
    std::wstring displayName;
    std::string flagName;
};

enum class HitKind {
    None,
    Mode,
    Map,
    MapScrollUp,
    MapScrollDown,
    SlotEnabled,
    SlotCountry,
    DropdownOption,
    SlotColor,
    SlotStart,
    SlotTeam,
    SlotDifficulty,
    CreditsMinus,
    CreditsPlus,
    UnitsMinus,
    UnitsPlus,
    SpeedMinus,
    SpeedPlus,
    CreditsSlider,
    UnitsSlider,
    SpeedSlider,
    ToggleShortGame,
    ToggleMcvRepacks,
    ToggleCrates,
    ToggleSuperweapons,
    ToggleBuildOffAlly,
    ToggleComputerAi,
    ToggleNoCooldown,
    ChooseMap,
    CreateRandomMap, RandomOption, RandomGenerate, RandomSurprise, RandomUse, RandomSave,
    RandomLoad, RandomPreview, RandomSaved, RandomSavedScroll, RandomLoadSelected, RandomDelete, RandomCancel,
    UseMap,
    CancelMap,
    Start,
    Back
};

enum class DropdownKind {
    None,
    Player,
    Country,
    Color,
    Start,
    Team
};

struct HitTarget {
    HitKind kind = HitKind::None;
    int index = -1;

    bool operator==(const HitTarget& other) const {
        return kind == other.kind && index == other.index;
    }
    bool operator!=(const HitTarget& other) const {
        return !(*this == other);
    }
};

struct HitRegion {
    RuntimeInputPixelRect rect{};
    HitTarget target;
};

enum class SkirmishPage {
    Lobby,
    MapSelection,
    RandomMap,
    RandomMapLoad,
    RandomMapPreview
};

enum class ShellMenuPhase {
    SlideIn,
    Idle,
    SlideOut
};

struct SkirmishSlotSetup {
    std::vector<RuntimeSkirmishPlayerSlot> slots;
    std::array<int, kPlayerSlots> starts{};
};

class RuntimeSkirmishServices {
public:
    virtual ~RuntimeSkirmishServices() = default;
    virtual void requestRepaint() = 0;
    virtual void playSound(const std::string& event, bool beepOnFailure = false) = 0;
    virtual void stopMusic() = 0;
    virtual std::uint64_t nowMs() const = 0;
    virtual void destroyWindow() = 0;
    virtual void prewarmMainMenu() = 0;
    virtual bool saveCratePreference(const RuntimeConfig&, bool, std::string&) = 0;
    virtual bool saveNoCooldownPreference(const RuntimeConfig&, bool, std::string&) = 0;
    virtual bool ensurePreview(int mapIndex) = 0;
    virtual void loadDefaults(const RuntimeConfig&, RuntimeSkirmishSelection&) = 0;
    virtual std::wstring text(const std::string& key, const std::wstring& fallback) const = 0;
    virtual std::wstring label(std::initializer_list<std::string> keys, const wchar_t* fallback) const = 0;
    virtual std::wstring widenUtf8(const std::string&) const = 0;
    virtual std::string loadingBriefKey(const std::string&) const = 0;
    virtual std::string loadingScreenAsset(const std::string&) const = 0;
    // Borrow ends with the synchronous controller call; catalog is not reloaded then.
    virtual const CooperativeScenarioDefinition* cooperativeScenario(const std::string& mapId) const = 0;
    virtual bool generatedMapFile(const std::string& fileName) const = 0;
    virtual int sliderGripWidth() const = 0;
    virtual bool cursorAvailable() const = 0;
    virtual bool activateRandomTarget(HitTarget, int direction) = 0;
    virtual bool randomMapReady() const = 0;
    virtual void warnInvalidSelection() = 0;
    virtual void focus() = 0;
    virtual void capture() = 0;
    virtual void releaseCaptureIfOwned() = 0;
};

// Owned selection/page/interaction and CPU preview data. No native window,
// device, worker, font, resource watcher, or renderer cache is part of this state.
// Effects are borrowed from the concrete menu owner through each synchronous call.
struct RuntimeSkirmishController {
    explicit RuntimeSkirmishController(RuntimeSkirmishServices& effectPort) : services(effectPort) {}
    RuntimeSkirmishController(const RuntimeSkirmishController&) = delete;
    RuntimeSkirmishController& operator=(const RuntimeSkirmishController&) = delete;
    RuntimeSkirmishServices& services;
    RuntimeConfig config;
    RuntimeSkirmishSelection selection;
    std::vector<ModeInfo> modes;
    std::vector<MapInfo> maps;
    std::vector<int> filteredMaps;
    std::vector<CountryInfo> countries;
    std::unordered_map<std::string, CooperativeScenarioMapValidation>
        cooperativeValidationByMap;
    bool cooperativeDataComplete = false;
    std::vector<std::string> colorIds{
        "Gold", "DarkRed", "DarkBlue", "DarkGreen",
        "Orange", "DarkSky", "Purple", "Magenta"
    };
    std::vector<std::string> teams{"-", "1", "2", "3", "4"};
    std::vector<std::string> difficulties{"Easy", "Medium", "Hard"};
    std::array<int, kPlayerSlots> startingSpots{};
    std::optional<SkirmishSlotSetup> beforeCooperative;
    int selectedMode = 0;
    int selectedMap = -1;
    int mapScroll = 0;
    std::vector<HitRegion> hits;
    HitTarget hover;
    HitTarget pressed;
    bool mouseVisible = false;
    RuntimeInputPixelPoint mousePoint{};
    DropdownKind openDropdown = DropdownKind::None;
    int openDropdownSlot = -1;
    int dropdownHover = -1;
    HitKind draggingSlider = HitKind::None;
    int viewLeft = 0;
    int viewTop = 0;
    int viewWidth = kBaseWidth;
    int viewHeight = kBaseHeight;
    bool backDirty = true;
    std::wstring status;
    bool smoke = false;
    bool closing = false;
    SkirmishPage page = SkirmishPage::Lobby;
    ShellMenuPhase phase = ShellMenuPhase::Idle;
    std::uint64_t phaseStarted = 0;
    int phaseTick = 0;
    int mapPageOriginalMode = 0;
    int mapPageOriginalMap = -1;
    std::vector<RuntimeSkirmishPlayerSlot> mapPageOriginalSlots;
    std::array<int, kPlayerSlots> mapPageOriginalStarts{};
    std::optional<SkirmishSlotSetup> mapPageOriginalBeforeCooperative;
};

std::string trimAscii(const std::string& text);
std::string lowerAscii(std::string text);
bool equalsAsciiNoCase(const std::string& left, const std::string& right);
std::string pathString(const fs::path& path);
bool isUnavailableCooperativeMode(const ModeInfo& mode);
bool mapSupportsFilter(const MapInfo& map, const std::string& filter);
bool allowsAllMapsFallback(const std::string& filter);
bool modeIsCooperative(const RuntimeSkirmishController& state);
const CooperativeScenarioDefinition* selectedCooperativeScenario(
    const RuntimeSkirmishController& state
);
bool cooperativeCountryAllowed(
    const CooperativeScenarioDefinition& scenario,
    int slotIndex,
    const std::string& country
);
std::string canonicalCooperativeCountry(
    const CooperativeScenarioDefinition& scenario,
    int slotIndex,
    const std::string& requested
);
void normalizeCooperativeSelection(RuntimeSkirmishController& state);
std::vector<int> cooperativeCountryOptionIndices(
    const RuntimeSkirmishController& state,
    int slotIndex
);
std::vector<int> countryOptionIndices(const RuntimeSkirmishController& state, int slotIndex);
int countryIndexForDropdownOption(
    const RuntimeSkirmishController& state,
    int option
);
void rebuildFilteredMaps(RuntimeSkirmishController& state, bool preserveSelection);
int selectedMapCombatantLimit(const RuntimeSkirmishController& state);
int selectedMapLimit(const RuntimeSkirmishController& state);
void applyMapSlotLimit(RuntimeSkirmishController& state);
void syncSelection(RuntimeSkirmishController& state);
int findCountryIndex(const RuntimeSkirmishController& state, const std::string& id);
void buildPlayerSlots(RuntimeSkirmishController& state);
void resetControllerForEntry(RuntimeSkirmishController& state, const RuntimeConfig& config);
void scrollMapList(RuntimeSkirmishController& state, int amount, bool playSound);
void selectFilteredMap(RuntimeSkirmishController& state, int filteredIndex, bool playSound);
void finishMenuTransition(RuntimeSkirmishController& state);
bool updateMenuTransition(RuntimeSkirmishController& state, std::uint64_t now);
void completeMenu(RuntimeSkirmishController& state, bool accepted);
DropdownKind dropdownKindForTarget(HitKind kind);
HitKind hitKindForDropdown(DropdownKind kind);
void closeDropdown(RuntimeSkirmishController& state, bool playSound);
bool dropdownCanOpen(
    const RuntimeSkirmishController& state,
    DropdownKind kind,
    int slotIndex
);
int dropdownOptionCount(const RuntimeSkirmishController& state, DropdownKind kind);
int currentDropdownOption(const RuntimeSkirmishController& state);
void toggleDropdown(
    RuntimeSkirmishController& state,
    DropdownKind kind,
    int slotIndex
);
void selectDropdownOption(RuntimeSkirmishController& state, int option);
bool dropdownContainsTarget(const RuntimeSkirmishController& state, HitTarget target);
void cycleString(
    std::string& value,
    const std::vector<std::string>& choices,
    int direction
);
void adjustCredits(RuntimeSkirmishSelection& selection, int direction);
const std::array<int, 7>& startingCreditValues();
bool isSliderTarget(HitKind kind);
int sliderMaximum(HitKind kind);
void setSliderFromBaseX(RuntimeSkirmishController& state, HitKind kind, int baseX);
std::string skirmishLobbyAllianceTeam(const std::string& value);
bool canStart(const RuntimeSkirmishController& state, std::wstring& reason);
void selectSkirmishMode(RuntimeSkirmishController& state, int modeIndex);
void activateTarget(RuntimeSkirmishController& state, HitTarget target, int direction);
void computeShellViewport(RuntimeSkirmishController& state, int width, int height);
RuntimeInputPixelPoint shellClientToBase(const RuntimeSkirmishController& state, RuntimeInputPixelPoint point);
bool updateShellMouse(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
bool isShellNavigationButton(HitKind kind);
void activateShellTarget(RuntimeSkirmishController& state, HitTarget target, int direction);

enum class RuntimeSkirmishKey { Other, Escape, Return, Space, Up, Down, Home, End };
void handleSkirmishMouseMove(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
void handleSkirmishLeftDown(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
void handleSkirmishLeftUp(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
void handleSkirmishDoubleClick(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
void handleSkirmishRightUp(RuntimeSkirmishController& state, RuntimeInputPixelPoint client);
void handleSkirmishMouseLeave(RuntimeSkirmishController& state);
void handleSkirmishCaptureLost(RuntimeSkirmishController& state);
bool handleSkirmishKey(RuntimeSkirmishController& state, RuntimeSkirmishKey key);
void handleSkirmishWheel(RuntimeSkirmishController& state, int notches);
const HitRegion* hitTest(const RuntimeSkirmishController& state, RuntimeInputPixelPoint point);
void invalidate(RuntimeSkirmishController& state);
std::wstring label(const RuntimeSkirmishController&, std::initializer_list<std::string>, const wchar_t*);
bool ensurePreview(RuntimeSkirmishController&, int mapIndex);
template <typename Container>
int findStringIndex(const Container& values, const std::string& value) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (equalsAsciiNoCase(values[i], value)) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

template <typename Container>
int wrappedIndex(const Container& values, int current, int direction) {
    if (values.empty()) {
        return -1;
    }
    const int size = static_cast<int>(values.size());
    int next = (current + direction) % size;
    if (next < 0) next += size;
    return next;
}
} // namespace concord::skirmish
