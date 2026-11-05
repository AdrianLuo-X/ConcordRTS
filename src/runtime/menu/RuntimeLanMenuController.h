#pragma once
#include "runtime/contracts/RuntimeMultiplayerMenuTypes.h"
#include "runtime/contracts/RuntimeSkirmishSelection.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace concord::lan_menu {
enum class MultiplayerMenuPage {
    Browser,
    Lobby
};
enum MultiplayerMenuControlId : int {
    PlayerNameEdit = 4101,
    HostPortEdit = 4102,
    DirectAddressEdit = 4103,
    HostButton = 4104,
    RefreshButton = 4105,
    JoinButton = 4106,
    SpectatorCheck = 4107,
    BackButton = 4108,
    RoomList = 4109,
    RosterList = 4110,
    ReadyButton = 4111,
    StartButton = 4112,
    LeaveButton = 4113,
    StatusText = 4114,
    NetworkHint = 4115,
    CountryEdit = 4116,
    ColorEdit = 4117,
    TeamEdit = 4118,
    DifficultyEdit = 4119,
    StartPositionEdit = 4120,
    ApplySlotButton = 4121,
    KickButton = 4122,
    PasswordEdit = 4123,
    ResumeHostButton = 4124,
    ResumeSwapButton = 4125
};
enum class RuntimeLanMenuWidget {
    hostWindow,
    window,
    playerNameLabel,
    playerNameEdit,
    hostPortLabel,
    hostPortEdit,
    directAddressLabel,
    directAddressEdit,
    passwordLabel,
    passwordEdit,
    hostButton,
    resumeHostButton,
    resumeSwapButton,
    refreshButton,
    joinButton,
    spectatorCheck,
    backButton,
    roomList,
    rosterList,
    countryLabel,
    countryEdit,
    colorLabel,
    colorEdit,
    teamLabel,
    teamEdit,
    difficultyLabel,
    difficultyEdit,
    startPositionLabel,
    startPositionEdit,
    applySlotButton,
    kickButton,
    readyButton,
    startButton,
    leaveButton,
    statusText,
    networkHint
};
class RuntimeLanMenuFlow {
public:
    virtual ~RuntimeLanMenuFlow() = default;
    virtual bool startHost(RuntimeMultiplayerLanHostOptions&& options, const RuntimeMultiplayerLobbyHostCreateOptions& lobby, std::string& error, const RuntimeMultiplayerResumeSave* resume = nullptr)  = 0;
    virtual bool startBrowsing(std::uint16_t port, std::string& error)  = 0;
    virtual bool join(RuntimeMultiplayerLanJoinOptions&& options, std::string& error)  = 0;
    virtual bool sendDiscoveryProbe(const std::string& address, std::string& error)  = 0;
    virtual std::vector<RuntimeLanRoomAdvertisement> discoveredRooms(std::uint64_t age = 5000) const = 0;
    virtual RuntimeMultiplayerLobbyFlowPollResult poll(int timeout = 0)  = 0;
    virtual RuntimeMultiplayerLobbyEditResult setSlot(std::size_t index, const RuntimeMultiplayerMatchSlot& slot, std::string& error)  = 0;
    virtual RuntimeMultiplayerLobbyEditResult setSlotController(std::size_t index, RuntimeMultiplayerMatchSlotController kind, std::string_view peer, std::string& error)  = 0;
    virtual RuntimeMultiplayerLobbyEditResult setSlotOptions(std::size_t index, std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position, std::string& error)  = 0;
    virtual RuntimeMultiplayerLanOperationResult requestLocalSlotOptions(std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position)  = 0;
    virtual RuntimeMultiplayerLanOperationResult kickPeer(const std::string& peer, std::string reason = "removed by the host")  = 0;
    virtual bool verifyLocalMap(const std::filesystem::path& path, std::string& error)  = 0;
    virtual bool localMapVerified() const = 0;
    virtual bool downloadMissingMap(const std::filesystem::path& root, std::filesystem::path& output, std::string& error)  = 0;
    virtual bool hostAuthoritative() const = 0;
    virtual bool canonicalSetupPublished() const = 0;
    virtual bool resuming() const = 0;
    virtual bool resumeReady(std::string& error) const = 0;
    virtual bool canStart(std::string& error) const = 0;
    virtual bool initialized() const = 0;
    virtual RuntimeMultiplayerLanOperationResult setReady(bool value)  = 0;
    virtual RuntimeMultiplayerLanOperationResult startMatch(int tick = 1)  = 0;
    virtual void close()  = 0;
    virtual const RuntimeMultiplayerLobbyModel& lobby() const = 0;
    virtual bool bridgePresent() const = 0;
    virtual bool sessionActive() const = 0;
    virtual const std::string& localPeerId() const = 0;
    virtual const std::vector<RuntimeMultiplayerRoomMember>& members() const = 0;
    virtual RuntimeMultiplayerLanRole lanRole() const = 0;
    virtual RuntimeMultiplayerLanPhase lanPhase() const = 0;
    virtual void leave() = 0;
};
class RuntimeLanMenuView {
public:
    virtual ~RuntimeLanMenuView() = default;
    virtual void showWidget(RuntimeLanMenuWidget, bool) = 0;
    virtual void enableWidget(RuntimeLanMenuWidget, bool) = 0;
    virtual std::string text(RuntimeLanMenuWidget) const = 0;
    virtual void setText(RuntimeLanMenuWidget, const std::string&) = 0;
    virtual void listClear(RuntimeLanMenuWidget) = 0;
    virtual void listAdd(RuntimeLanMenuWidget, const std::string&) = 0;
    virtual std::int64_t listSelected(RuntimeLanMenuWidget) const = 0;
    virtual void listSelect(RuntimeLanMenuWidget, std::int64_t) = 0;
    virtual bool checked(RuntimeLanMenuWidget) const = 0;
    virtual bool hasWidget(RuntimeLanMenuWidget) const = 0;
    virtual bool parseEndpoint(std::string_view, RuntimeMultiplayerDirectEndpoint&, std::string&) = 0;
    virtual bool makeJoinOptions(const RuntimeLanRoomAdvertisement&, std::string&&, const std::string&, bool, std::string&&, const RuntimeMultiplayerCompatibility&, RuntimeMultiplayerLanJoinOptions&, std::string&) = 0;
    virtual void clearPassword() = 0;
    virtual void readyLabel(bool) = 0;
    virtual bool runHostSetup(const RuntimeConfig&, RuntimeSkirmishSelection&, std::string&) = 0;
    virtual bool selectResume(const RuntimeConfig&, const RuntimeMultiplayerCompatibility&, const std::string&, const std::string&, RuntimeMultiplayerResumeSave&, RuntimeMultiplayerHostSelectionProjection&, std::string&) = 0;
    virtual bool normalizeName(std::string&, std::size_t, std::string&) = 0;
    virtual std::string trim(const std::string&) const = 0;
    virtual bool secureU64(std::uint64_t&, std::string&) = 0;
    virtual bool secureId(std::string_view, std::string&, std::string&) = 0;
    virtual bool projectHost(const RuntimeConfig&, const RuntimeSkirmishSelection&, const std::string&, const std::string&, std::uint64_t, RuntimeMultiplayerHostSelectionProjection&, std::string&) = 0;
    virtual bool resolveMap(const RuntimeConfig&, const RuntimeMultiplayerMatchMap&, std::filesystem::path&, std::string&) = 0;
    virtual bool makeCompatibility(const RuntimeConfig&, RuntimeMultiplayerCompatibility&, std::string&) = 0;
    virtual bool accepted() const = 0;
    virtual std::string& resultError() = 0;
    virtual void showKickedMessage(const std::string&) = 0;
    virtual void destroyWindow() = 0;
    // Existing S26 bridge/pre-attach owner stays intact, including failure cleanup order.
    virtual void finishStartedMatch() = 0;
    // Unreachable old direct Internet branch remains private and gets no LAN credit.
    virtual void beginDirectDiscovery() = 0;
    virtual void pollDirectDiscovery() = 0;
};
// Synchronous borrows of the existing S26 flow and private widget view.
struct RuntimeLanMenuController {
    RuntimeLanMenuController(RuntimeLanMenuFlow& network, RuntimeLanMenuView& nativeView) : flow(network), view(nativeView) {}
    RuntimeLanMenuController(const RuntimeLanMenuController&) = delete;
    RuntimeLanMenuController& operator=(const RuntimeLanMenuController&) = delete;
    RuntimeLanMenuFlow& flow;
    RuntimeLanMenuView& view;
    RuntimeConfig baseConfig;
    RuntimeMultiplayerMenuNetwork network =
        RuntimeMultiplayerMenuNetwork::Lan;
    RuntimeMultiplayerCompatibility compatibility;
    bool compatibilityReady = false;
    std::string peerId;
    MultiplayerMenuPage page = MultiplayerMenuPage::Browser;
    std::vector<RuntimeLanRoomAdvertisement> rooms;
    std::vector<std::string> rosterPeerIds;
    std::string selectedRosterPeerId;
    std::uint64_t displayedSetupRevision = 0U;
    std::filesystem::path localMapPath;
    bool hosting = false;
    bool directDiscoveryPending = false;
    std::string pendingPassword;
    bool done = false;
};
void setStatus(
    RuntimeLanMenuController& state,
    const std::string& text
);
void showPage(
    RuntimeLanMenuController& state,
    MultiplayerMenuPage page
);
void updateRoomList(RuntimeLanMenuController& state);
bool localMemberReady(const RuntimeLanMenuController& state);
const RuntimeMultiplayerMatchSlot* humanSlotForPeer(
    const RuntimeLanMenuController& state,
    std::string_view peerId
);
void populateLocalSlotControls(RuntimeLanMenuController& state);
void updateRoster(RuntimeLanMenuController& state);
bool ensureLocalMapVerified(
    RuntimeLanMenuController& state,
    std::string& errorMessage
);
void closeMenu(RuntimeLanMenuController& state);
bool beginLanBrowsing(
    RuntimeLanMenuController& state,
    std::string& errorMessage
);
bool enterLobbyWithRoom(
    RuntimeLanMenuController& state,
    const RuntimeLanRoomAdvertisement& room,
    std::string playerName,
    bool spectator,
    std::string password,
    std::string& errorMessage
);
bool startHosting(
    RuntimeLanMenuController& state,
    std::string& errorMessage,
    bool fromSave = false
);
void joinSelectedLanRoom(RuntimeLanMenuController& state);
void applyLocalSlotOptions(RuntimeLanMenuController& state);
void kickSelectedPeer(RuntimeLanMenuController& state);
void pollMenu(RuntimeLanMenuController& state);
void toggleReady(RuntimeLanMenuController& state);
void startMatch(RuntimeLanMenuController& state);
void leaveLobby(RuntimeLanMenuController& state);
void rosterSelectionChanged(RuntimeLanMenuController& state);
bool handleControlClick(RuntimeLanMenuController& state, int control);
bool initializeLanMenuData(RuntimeLanMenuController& state, std::string& initializationError);
void initializeLanBrowser(RuntimeLanMenuController& state, const std::string& initializationError);
} // namespace concord::lan_menu
