#pragma once
#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeCampaignSelection.h"
#include "runtime/menu/RuntimeCampaignPresentationData.h"
#include "ui/time/RuntimeUiClock.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <string>
#include <vector>

namespace concord::campaign_menu {
// Original Campaign layout and first-enabled half-open hit policy.
constexpr int kCampaignShellWidth = 800;
constexpr int kCampaignShellHeight = 600;
constexpr RuntimeInputPixelRect kCampaignBackButton{653, 543, 779, 569};
constexpr RuntimeInputPixelRect kCampaignMapViewport{8, 40, 624, 502};
constexpr RuntimeInputPixelRect kCampaignSlider{206, 230, 422, 254};

enum class CampaignControlAction { Back, Confirm, Edition, Campaign, Previous, Next, Row, Difficulty };

struct CampaignControl {
    RuntimeInputPixelRect rect{};
    CampaignControlAction action = CampaignControlAction::Back;
    int index = 0;
    std::wstring label;
    bool enabled = true;
    bool selected = false;
};




class RuntimeCampaignMenuServices {
public:
    virtual ~RuntimeCampaignMenuServices() = default;
    virtual std::wstring text(const std::string&, const std::wstring& fallback = L"") const = 0;
    virtual std::wstring widen(const std::string&) const = 0;
    virtual std::wstring missionLabel(const CampaignMissionDefinition&) const = 0;
    virtual std::wstring editionSuffix(const CampaignDefinition*) const = 0;
    virtual std::string loadingScreenAsset(const std::string&) const = 0;
    virtual std::string savePath(const std::string&, const std::string&, const std::string&) const = 0;
    virtual const CampaignMapSelectionStage* findMapStage(const CampaignDefinition&, const std::string&) const = 0;
    virtual const CampaignMissionDefinition* findMission(const std::string&, const std::string&) const = 0;
    virtual bool saveProgress(const std::string&, const CampaignProgressData&, std::string&) = 0;
    virtual bool validateSavePaths(const RuntimeConfig&, const SkirmishSaveInfo&, std::string&) const = 0;
    virtual void refreshBackground() = 0;
    virtual void refreshPresentation() = 0;
    virtual void releaseMapCapture() = 0;
    virtual void stopVoice() = 0;
    virtual void stopMapEffect() = 0;
    virtual void playMapSound(const CampaignMapSelectionSound&) = 0;
    virtual void repaintValidWindow() = 0;
    virtual void destroyWindow() = 0;
    virtual int mapColorAt(int, int) const = 0;
    virtual std::wstring saveTitle(const LoadSaveEntry&) const = 0;
    virtual void updatePresentation() = 0;
    virtual RuntimeInputPixelRect eventClientRect() const = 0;
    virtual bool shiftPressed() const = 0;
    virtual void repaintEvent(bool requireValid) = 0;
    virtual void trackMouseLeave() = 0;
    virtual void setHandCursor(bool) = 0;
    virtual void focusEvent() = 0;
    virtual void captureEvent() = 0;
    virtual void releaseEventCapture() = 0;
    virtual CampaignDatabase loadDatabase(const CampaignDatabaseOptions&) = 0;
    virtual bool loadProgress(const std::string&, CampaignProgressData&, std::string&) = 0;
    virtual std::vector<CampaignSaveInfo> campaignSaves(const std::string&) = 0;
    virtual std::vector<SkirmishSaveInfo> skirmishSaves(const std::string&) = 0;
    virtual std::wstring incompatibleSaveWarning(const std::string&) const = 0;

};

// Navigation, progress/selection and owned CPU presentation data. The native
// view retains renderer caches, resource mounts, text catalog, audio and window.
// Services and clock are synchronous borrows and outlive this controller.
struct RuntimeCampaignMenuController {
    RuntimeCampaignMenuController(RuntimeCampaignMenuServices& effects, const RuntimeUiClock& clock)
        : services(effects), presentationClock(&clock) {}
    RuntimeCampaignMenuController(const RuntimeCampaignMenuController&) = delete;
    RuntimeCampaignMenuController& operator=(const RuntimeCampaignMenuController&) = delete;
    RuntimeCampaignMenuServices& services;
    const RuntimeUiClock* presentationClock;
    RuntimeConfig config;
    CampaignDatabase database;
    CampaignProgressData progress;
    std::vector<LoadSaveEntry> saves;
    std::wstring skirmishSaveWarning;
    int hoveredControl = -1;
    int pressedControl = -1;
    int focusedControl = -1;
    int briefingScroll = 0;
    CampaignMapPresentationState mapPresentation;
    CampaignPage page = CampaignPage::Campaigns;
    bool loadGame = false;
    bool closing = false;
    bool accepted = false;
    int campaignIndex = 0;
    int missionIndex = 0;
    int difficultyIndex = 1;
    int saveIndex = 0;
    std::wstring status;
    RuntimeCampaignSelection selection;

};
std::string lowerAscii(std::string value);
std::string trimAscii(const std::string& value);
const LoadSaveEntry* selectedSave(const RuntimeCampaignMenuController& state);
const std::vector<std::string>& difficultyNames();
const CampaignDefinition* selectedCampaign(const RuntimeCampaignMenuController& state);
bool missionUnlocked(
    const RuntimeCampaignMenuController& state,
    const CampaignDefinition& campaign,
    const CampaignMissionDefinition& mission
);
const CampaignMissionDefinition* selectedMission(const RuntimeCampaignMenuController& state);
const CampaignMapSelectionStage* selectedMapSelectionStage(
    const RuntimeCampaignMenuController& state
);
const CampaignMapSelectionStage* currentMapSelectionStage(
    const RuntimeCampaignMenuController& state
);
int missionIndexById(
    const CampaignDefinition& campaign,
    const std::string& missionId
);
const CampaignMapSelectionChoice* mapChoiceByColor(
    const CampaignMapSelectionStage* stage,
    int colorIndex
);
bool isSovietCampaign(const CampaignDefinition* campaign);
bool isTutorialCampaign(const CampaignDefinition* campaign);
bool campaignSourceIs(
    const CampaignDefinition* campaign,
    const std::string& sourceId
);
void chooseFirstUnlockedMission(RuntimeCampaignMenuController& state);
void appendMapPresentationDiagnostic(
    CampaignMapPresentationState& presentation,
    const std::string& message
);
int firstUsableMapChoiceColor(
    const RuntimeCampaignMenuController& state,
    const CampaignMapSelectionStage& stage
);
std::wstring resolvedCampaignName(
    const RuntimeCampaignMenuController& state,
    const CampaignDefinition* campaign
);
std::wstring campaignPageTitle(const RuntimeCampaignMenuController& state);
std::wstring difficultyDisplayName(
    const RuntimeCampaignMenuController& state,
    int index
);
void closeMenu(RuntimeCampaignMenuController& state, bool accepted);
void fillSelectionFromMission(
    RuntimeCampaignMenuController& state,
    const CampaignMissionDefinition& mission,
    bool loadedFromSave,
    const std::string& savePath,
    const std::string& difficulty
);
void fillSelectionFromSkirmishSave(
    RuntimeCampaignMenuController& state,
    const SkirmishSaveInfo& save
);
void acceptMission(RuntimeCampaignMenuController& state);
void acceptSave(RuntimeCampaignMenuController& state);
bool confirmCampaignMapChoice(RuntimeCampaignMenuController& state, int colorIndex);
void skipCampaignMapIntro(RuntimeCampaignMenuController& state);
void cycleCampaignMapChoice(RuntimeCampaignMenuController& state, int delta);
void updateCampaignMapHover(
    RuntimeCampaignMenuController& state,
    int baseX,
    int baseY
);
void moveSelection(RuntimeCampaignMenuController& state, int delta);
void activateSelection(RuntimeCampaignMenuController& state);
void goBack(RuntimeCampaignMenuController& state);
bool campaignShowsMap(const RuntimeCampaignMenuController& state);
enum class CampaignMenuKey { Other, Escape, Tab, Up, Down, Left, Right, Confirm };
bool campaignPointInRect(RuntimeInputPixelRect, RuntimeInputPixelPoint) noexcept;
RuntimeInputPixelRect campaignShellClientRect(const RuntimeInputPixelRect& client);
RuntimeInputPixelPoint campaignShellPoint(const RuntimeInputPixelRect& client, int x, int y);
RuntimeInputPixelPoint campaignMapPoint(RuntimeInputPixelPoint shell);
int campaignListStart(int selected, int count);
std::vector<CampaignControl> campaignControls(const RuntimeCampaignMenuController& state);
int campaignControlAt(const RuntimeCampaignMenuController& state, RuntimeInputPixelPoint point);

void resetCampaignMenuInput(RuntimeCampaignMenuController&);
void activateCampaignControl(RuntimeCampaignMenuController&, int);
bool campaignKeyDown(RuntimeCampaignMenuController& state, CampaignMenuKey key, bool repeated);
void campaignWheel(RuntimeCampaignMenuController& state, bool positive);
void campaignMouseMove(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint);
void campaignMouseLeave(RuntimeCampaignMenuController& state);
void campaignMouseDown(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint);
void campaignMouseUp(RuntimeCampaignMenuController& state, RuntimeInputPixelPoint clientPoint);
void campaignCaptureChanged(RuntimeCampaignMenuController& state);
void campaignSize(RuntimeCampaignMenuController& state);
void campaignTimer(RuntimeCampaignMenuController& state, bool timerOne);
void campaignDestroy(RuntimeCampaignMenuController& state);
void initializeCampaignMenu(RuntimeCampaignMenuController&, const RuntimeConfig&, bool);
} // namespace concord::campaign_menu
