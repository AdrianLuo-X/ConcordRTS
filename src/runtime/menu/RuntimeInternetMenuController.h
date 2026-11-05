#pragma once
#include "runtime/contracts/RuntimeInternetMenuData.h"
#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "runtime/menu/RuntimeLanMenuController.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace concord::internet_menu {
enum class InternetMenuPage : std::uint8_t {
    Account,
    Community,
    Browser,
    Ladder,
    News,
    Lobby,
    Kicked,
    MatchHistory
};
enum InternetMenuControlId : int {
    InternetPlayerName = 5101,
    InternetRoomName = 5102,
    InternetRefresh = 5103,
    InternetHost = 5104,
    InternetResumeHost = 5160,
    InternetResumeSwap = 5161,
    InternetMatchHistory = 5162,
    InternetMatchHistoryBack = 5163,
    InternetMatchHistoryList = 5164,
    InternetJoin = 5105,
    InternetSpectator = 5106,
    InternetQuickMatch = 5107,
    InternetRoomList = 5108,
    InternetRoster = 5109,
    InternetCountry = 5110,
    InternetColor = 5111,
    InternetTeam = 5112,
    InternetDifficulty = 5113,
    InternetStartPosition = 5114,
    InternetApplySlot = 5115,
    InternetReady = 5116,
    InternetStart = 5117,
    InternetKick = 5118,
    InternetLeave = 5119,
    InternetStatus = 5120,
    InternetConnection = 5121,
    InternetPassword = 5122,
    InternetNavCommunity = 5123,
    InternetNavGames = 5124,
    InternetNavLadder = 5125,
    InternetNavNews = 5126,
    InternetProfileSummary = 5127,
    InternetAccountHandle = 5128,
    InternetAccountRegister = 5129,
    InternetChannelList = 5130,
    InternetChannelJoin = 5131,
    InternetChatList = 5132,
    InternetChatEdit = 5133,
    InternetChatSend = 5134,
    InternetParticipantList = 5135,
    InternetFriendList = 5136,
    InternetFriendEdit = 5137,
    InternetFriendAdd = 5138,
    InternetFriendRemove = 5139,
    InternetLadderList = 5140,
    InternetCommunityRefresh = 5141,
    InternetNewsList = 5142,
    InternetNewsDetail = 5143,
    InternetClanSummary = 5144,
    InternetClanMemberList = 5145,
    InternetClanInvitationList = 5146,
    InternetClanName = 5147,
    InternetClanTag = 5148,
    InternetClanCreate = 5149,
    InternetClanTarget = 5150,
    InternetClanInvite = 5151,
    InternetClanAccept = 5152,
    InternetClanRemove = 5153,
    InternetClanTransfer = 5154,
    InternetClanLeave = 5155,
    InternetClanLadderList = 5156
};
enum class RuntimeInternetMenuWidget {
    hostWindow,
    window,
    playerNameLabel,
    playerNameEdit,
    roomNameLabel,
    roomNameEdit,
    passwordLabel,
    passwordEdit,
    refreshButton,
    hostButton,
    resumeHostButton,
    resumeSwapButton,
    joinButton,
    spectatorCheck,
    quickMatchButton,
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
    readyButton,
    startButton,
    kickButton,
    leaveButton,
    statusText,
    connectionText,
    navCommunityButton,
    navGamesButton,
    navLadderButton,
    navNewsButton,
    profileSummaryText,
    accountTitleText,
    accountDescriptionText,
    accountHandleLabel,
    accountHandleEdit,
    accountRegisterButton,
    channelLabel,
    channelList,
    channelJoinButton,
    chatLabel,
    chatList,
    chatEdit,
    chatSendButton,
    participantLabel,
    participantList,
    friendLabel,
    friendList,
    friendEdit,
    friendAddButton,
    friendRemoveButton,
    ladderTitleText,
    ladderList,
    clanSummaryText,
    clanMemberList,
    clanInvitationList,
    clanNameEdit,
    clanTagEdit,
    clanCreateButton,
    clanTargetEdit,
    clanInviteButton,
    clanAcceptButton,
    clanRemoveButton,
    clanTransferButton,
    clanLeaveButton,
    clanLadderTitleText,
    clanLadderList,
    communityRefreshButton,
    newsTitleText,
    newsList,
    newsDetail,
    matchHistoryButton,
    matchHistoryBackButton,
    matchHistoryList,
    matchHistoryDetail
};
enum class RuntimeInternetLeaveLabel { Close, LeaveRoom, MainMenu };
class RuntimeInternetMenuFlow {
public:
    virtual ~RuntimeInternetMenuFlow() = default;
    virtual bool beginRoomQuery(RuntimeInternetLobbyQueryOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) = 0;
    virtual bool beginMatchmaking(RuntimeInternetLobbyMatchmakingOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) = 0;
    virtual bool startHost(RuntimeInternetLobbyHostOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) = 0;
    virtual bool join(RuntimeInternetLobbyJoinOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) = 0;
    virtual bool requestCommunityLogin(std::uint64_t transactionId, std::string& errorMessage) = 0;
    virtual bool requestCommunityRegistration(std::uint64_t transactionId, const std::string& displayName, std::string& errorMessage) = 0;
    virtual bool requestCommunityRefresh(std::uint64_t transactionId, std::string& errorMessage) = 0;
    virtual bool requestCommunityChannelJoin(std::uint64_t transactionId, const std::string& channelId, std::string& errorMessage) = 0;
    virtual bool requestCommunityChannelMessage(std::uint64_t transactionId, const std::string& text, std::string& errorMessage) = 0;
    virtual bool requestCommunityFriendAdd(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) = 0;
    virtual bool requestCommunityFriendRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanCreate(std::uint64_t transactionId, const std::string& clanName, const std::string& clanTag, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanInvite(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanInvitationAccept(std::uint64_t transactionId, const std::string& clanId, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanMemberRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanLeave(std::uint64_t transactionId, std::string& errorMessage) = 0;
    virtual bool requestCommunityClanOwnershipTransfer(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) = 0;
    virtual RuntimeInternetLobbyFlowPollResult poll(std::uint64_t nowMilliseconds, int transportTimeoutMilliseconds) = 0;
    virtual RuntimeInternetLobbyFlowMode mode() const = 0;
    virtual const std::vector<RuntimeInternetRoomRecord>& rooms() const = 0;
    virtual RuntimeInternetConnectionState primaryConnectionState() const = 0;
    virtual const RuntimeInternetCommunitySnapshot& communitySnapshot() const = 0;
    virtual const std::vector<RuntimeInternetRankedRecord>& communityMatchHistory() const = 0;
    virtual void close() noexcept = 0;
    virtual bool present() const = 0;
    virtual bool transportPresent() const = 0;
    virtual bool coordinatorAuthenticated() const = 0;
    virtual concord::lan_menu::RuntimeLanMenuFlow* lobbyFlow() const = 0;
};
class RuntimeInternetMenuView {
public:
    virtual ~RuntimeInternetMenuView() = default;
    virtual void showWidget(RuntimeInternetMenuWidget,bool) = 0;
    virtual void enableWidget(RuntimeInternetMenuWidget,bool) = 0;
    virtual std::string text(RuntimeInternetMenuWidget) const = 0;
    virtual void setText(RuntimeInternetMenuWidget,std::string_view) = 0;
    virtual bool hasWidget(RuntimeInternetMenuWidget) const = 0;
    virtual bool checked(RuntimeInternetMenuWidget) const = 0;
    virtual void clearText(RuntimeInternetMenuWidget) = 0;
    virtual void listClear(RuntimeInternetMenuWidget) = 0;
    virtual void listAdd(RuntimeInternetMenuWidget,const std::string&) = 0;
    virtual void replaceRows(RuntimeInternetMenuWidget,std::span<const std::string>) = 0;
    virtual std::int64_t listSelected(RuntimeInternetMenuWidget) const = 0;
    virtual void listSelect(RuntimeInternetMenuWidget,std::int64_t) = 0;
    virtual void listTop(RuntimeInternetMenuWidget,std::size_t) = 0;
    virtual void readyLabel(bool) = 0;
    virtual void leaveLabel(RuntimeInternetLeaveLabel) = 0;
    virtual void invalidateSkinned() = 0;
    virtual void invalidateSkin() = 0;
    virtual void destroyWindow() = 0;
    virtual bool accepted() const = 0;
    virtual const std::string& peerId() const = 0;
    virtual std::uint64_t nowMs() const = 0;
    virtual void clearCredentials() = 0;
    virtual bool entropy(RuntimeInternetMultiplayerMenuEntropy&,std::string&) = 0;
    virtual std::string trim(std::string&&) const = 0;
    virtual bool validateDisplayText(std::string&,std::size_t,std::string_view,std::string&) = 0;
    virtual const RuntimeMultiplayerMatchSlot* slotForPeer(const RuntimeMultiplayerMatchSetup*,std::string_view) const = 0;
    virtual bool queryOptions(const RuntimeMultiplayerCompatibility&,std::uint64_t,RuntimeInternetLobbyQueryOptions&,std::string&) = 0;
    virtual bool hostOptions(const RuntimeMultiplayerCompatibility&,const RuntimeMultiplayerHostSelectionProjection&,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeInternetLobbyHostOptions&,std::string&,const std::shared_ptr<RuntimeMultiplayerResumeSave>&) = 0;
    virtual bool joinOptions(const RuntimeMultiplayerCompatibility&,const RuntimeInternetRoomRecord&,bool,std::uint64_t,RuntimeInternetLobbyJoinOptions&,std::string&) = 0;
    virtual bool matchmakingOptions(const RuntimeMultiplayerCompatibility&,const RuntimeMultiplayerHostSelectionProjection&,const std::string&,std::int32_t,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeInternetLobbyMatchmakingOptions&,std::string&) = 0;
    virtual bool selectHostSetup(const std::string&,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeMultiplayerHostSelectionProjection&,std::string&) = 0;
    virtual bool selectResumeWithOriginalFailureStatus(const RuntimeMultiplayerCompatibility&,RuntimeMultiplayerResumeSave&,RuntimeMultiplayerHostSelectionProjection&,std::string&) = 0;
    virtual bool resolveMap(const RuntimeConfig&,const RuntimeMultiplayerMatchMap&,std::filesystem::path&,std::string&) = 0;
    virtual std::vector<RuntimeInternetMultiplayerRoomRow> roomRows(std::span<const RuntimeInternetRoomRecord>,bool) const = 0;
    virtual std::vector<RuntimeInternetMultiplayerRosterRow> rosterRows(std::span<const RuntimeMultiplayerRoomMember>,const RuntimeMultiplayerMatchSetup*,std::string_view,bool) const = 0;
    virtual std::string connectionStatus(bool,RuntimeInternetLobbyFlowMode,RuntimeInternetConnectionState) const = 0;
    virtual std::string profileSummary(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::string clanSummary(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> channelRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> chatRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> participantRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> friendRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> ladderRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> clanMemberRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> clanInvitationRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> clanLadderRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> newsRows(const RuntimeInternetCommunitySnapshot&) const = 0;
    virtual std::vector<std::string> historyRows(std::span<const RuntimeInternetRankedRecord>,std::string_view) const = 0;
    virtual std::string historyDetail(const RuntimeInternetRankedRecord&,std::string_view) const = 0;
    // Keep full original S26 launch/takeRunningBridge/preAttach transaction here.
    virtual void finishMatch() = 0;
};
// Synchronous borrowed ports; native/security/network ownership remains in its original host.
struct RuntimeInternetMenuController {
    RuntimeInternetMenuController(RuntimeInternetMenuFlow& network, RuntimeInternetMenuView& nativeView) : flow(network), view(nativeView) {}
    RuntimeInternetMenuController(const RuntimeInternetMenuController&) = delete;
    RuntimeInternetMenuController& operator=(const RuntimeInternetMenuController&) = delete;
    RuntimeInternetMenuFlow& flow;
    RuntimeInternetMenuView& view;
    RuntimeConfig baseConfig;
    RuntimeMultiplayerCompatibility compatibility;
    std::string selectedRankedMatchId;
    InternetMenuPage page = InternetMenuPage::Account;
    std::vector<RuntimeInternetRoomRecord> rooms;
    std::vector<RuntimeInternetMultiplayerRosterRow> rosterRows;
    std::string selectedPeerId;
    std::string selectedFriendPeerId;
    std::size_t selectedNewsIndex = 0U;
    std::filesystem::path localMapPath;
    std::vector<RuntimeMultiplayerMatchSlot> pendingHostSlots;
    std::uint64_t displayedRevision = 0U;
    bool hosting = false;
    bool done = false;
};
void setMenuStatus(
    RuntimeInternetMenuController& state,
    std::string_view text
);
void showInternetMenuPage(
    RuntimeInternetMenuController& state,
    InternetMenuPage page
);
void updateConnectionText(
    RuntimeInternetMenuController& state
);
void updateRoomList(RuntimeInternetMenuController& state);
std::string communityDisplayName(
    const RuntimeInternetMenuController& state
);
void updateNewsDetail(RuntimeInternetMenuController& state);
void updateRankedHistory(RuntimeInternetMenuController& state);
void updateCommunityViews(RuntimeInternetMenuController& state);
bool nextCommunityTransaction(
    RuntimeInternetMenuController& state,
    std::uint64_t& transactionId,
    std::string& errorMessage
);
const RuntimeMultiplayerMatchSlot* localHumanSlot(
    const RuntimeInternetMenuController& state
);
bool localReady(const RuntimeInternetMenuController& state);
void populateLocalSlotControls(
    RuntimeInternetMenuController& state
);
void updateRoster(RuntimeInternetMenuController& state);
bool verifyLocalMap(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
);
bool applyPendingHostSlots(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
);
void closeInternetMenu(RuntimeInternetMenuController& state);
void requestCommunityLogin(RuntimeInternetMenuController& state);
void registerCommunityProfile(
    RuntimeInternetMenuController& state
);
void refreshCommunity(RuntimeInternetMenuController& state);
void joinSelectedCommunityChannel(
    RuntimeInternetMenuController& state
);
void sendCommunityChat(RuntimeInternetMenuController& state);
void addCommunityFriend(RuntimeInternetMenuController& state);
void removeSelectedCommunityFriend(
    RuntimeInternetMenuController& state
);
void createCommunityClan(RuntimeInternetMenuController& state);
void inviteCommunityClanMember(
    RuntimeInternetMenuController& state
);
void acceptSelectedCommunityClanInvitation(
    RuntimeInternetMenuController& state
);
void removeSelectedCommunityClanMember(
    RuntimeInternetMenuController& state
);
void transferCommunityClanOwnership(
    RuntimeInternetMenuController& state
);
void leaveCommunityClan(RuntimeInternetMenuController& state);
bool beginRoomRefresh(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
);
void beginHost(RuntimeInternetMenuController& state, bool fromSave = false);
void joinSelectedRoom(RuntimeInternetMenuController& state);
void beginQuickMatch(RuntimeInternetMenuController& state);
void applyLocalOptions(RuntimeInternetMenuController& state);
void toggleReady(RuntimeInternetMenuController& state);
void kickSelected(RuntimeInternetMenuController& state);
void startInternetMatch(RuntimeInternetMenuController& state);
void showKickedPage(
    RuntimeInternetMenuController& state,
    std::string reason
);
void pollInternetMenu(RuntimeInternetMenuController& state);
bool handleInternetSelection(RuntimeInternetMenuController&,int control);
bool handleInternetDoubleClick(RuntimeInternetMenuController&,int control);
bool handleInternetClick(RuntimeInternetMenuController&,int control);
void initializeInternetMenu(RuntimeInternetMenuController&);
} // namespace concord::internet_menu
