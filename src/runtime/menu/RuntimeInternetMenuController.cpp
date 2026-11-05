#include "runtime/menu/RuntimeInternetMenuController.h"
#include <algorithm>
#include <utility>

namespace concord::internet_menu {
using concord::lan_menu::RuntimeLanMenuFlow;
namespace { bool fail(std::string& error, std::string reason) { error=std::move(reason); return false; } }
void setMenuStatus(
    RuntimeInternetMenuController& state,
    std::string_view text
) {
    state.view.setText(RuntimeInternetMenuWidget::statusText, text);
}

void showInternetMenuPage(
    RuntimeInternetMenuController& state,
    InternetMenuPage page
) {
    state.page = page;
    const bool account = page == InternetMenuPage::Account;
    const bool community = page == InternetMenuPage::Community;
    const bool browser = page == InternetMenuPage::Browser;
    const bool ladder = page == InternetMenuPage::Ladder;
    const bool news = page == InternetMenuPage::News;
    const bool history = page == InternetMenuPage::MatchHistory;
    const bool lobby = page == InternetMenuPage::Lobby;
    const bool communityShell = community || browser || ladder || news || history;
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::navCommunityButton,
            RuntimeInternetMenuWidget::navGamesButton,
            RuntimeInternetMenuWidget::navLadderButton,
            RuntimeInternetMenuWidget::navNewsButton,
            RuntimeInternetMenuWidget::profileSummaryText
        }) {
        state.view.showWidget(control, communityShell);
    }
    if (history) state.view.showWidget(RuntimeInternetMenuWidget::profileSummaryText, false);
    state.view.showWidget(RuntimeInternetMenuWidget::connectionText, !history);
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::accountTitleText,
            RuntimeInternetMenuWidget::accountDescriptionText,
            RuntimeInternetMenuWidget::accountHandleLabel,
            RuntimeInternetMenuWidget::accountHandleEdit,
            RuntimeInternetMenuWidget::accountRegisterButton
        }) {
        state.view.showWidget(control, account);
    }
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::playerNameLabel,
            RuntimeInternetMenuWidget::playerNameEdit,
            RuntimeInternetMenuWidget::roomNameLabel,
            RuntimeInternetMenuWidget::roomNameEdit,
            RuntimeInternetMenuWidget::passwordLabel,
            RuntimeInternetMenuWidget::passwordEdit,
            RuntimeInternetMenuWidget::refreshButton,
            RuntimeInternetMenuWidget::hostButton,
            RuntimeInternetMenuWidget::resumeHostButton,
            RuntimeInternetMenuWidget::joinButton,
            RuntimeInternetMenuWidget::spectatorCheck,
            RuntimeInternetMenuWidget::quickMatchButton,
            RuntimeInternetMenuWidget::roomList
        }) {
        state.view.showWidget(control, browser);
    }
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::channelLabel,
            RuntimeInternetMenuWidget::channelList,
            RuntimeInternetMenuWidget::channelJoinButton,
            RuntimeInternetMenuWidget::chatLabel,
            RuntimeInternetMenuWidget::chatList,
            RuntimeInternetMenuWidget::chatEdit,
            RuntimeInternetMenuWidget::chatSendButton,
            RuntimeInternetMenuWidget::participantLabel,
            RuntimeInternetMenuWidget::participantList,
            RuntimeInternetMenuWidget::friendLabel,
            RuntimeInternetMenuWidget::friendList,
            RuntimeInternetMenuWidget::friendEdit,
            RuntimeInternetMenuWidget::friendAddButton,
            RuntimeInternetMenuWidget::friendRemoveButton
        }) {
        state.view.showWidget(control, community);
    }
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::ladderTitleText,
            RuntimeInternetMenuWidget::ladderList,
            RuntimeInternetMenuWidget::clanSummaryText,
            RuntimeInternetMenuWidget::clanMemberList,
            RuntimeInternetMenuWidget::clanInvitationList,
            RuntimeInternetMenuWidget::clanNameEdit,
            RuntimeInternetMenuWidget::clanTagEdit,
            RuntimeInternetMenuWidget::clanCreateButton,
            RuntimeInternetMenuWidget::clanTargetEdit,
            RuntimeInternetMenuWidget::clanInviteButton,
            RuntimeInternetMenuWidget::clanAcceptButton,
            RuntimeInternetMenuWidget::clanRemoveButton,
            RuntimeInternetMenuWidget::clanTransferButton,
            RuntimeInternetMenuWidget::clanLeaveButton,
            RuntimeInternetMenuWidget::clanLadderTitleText,
            RuntimeInternetMenuWidget::matchHistoryButton,
            RuntimeInternetMenuWidget::clanLadderList
        }) {
        state.view.showWidget(control, ladder);
    }
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::newsTitleText,
            RuntimeInternetMenuWidget::newsList,
            RuntimeInternetMenuWidget::newsDetail
        }) {
        state.view.showWidget(control, news);
    }
    state.view.showWidget(
        RuntimeInternetMenuWidget::communityRefreshButton,
        community || ladder || news || history
    );
    for (RuntimeInternetMenuWidget control : {RuntimeInternetMenuWidget::matchHistoryBackButton, RuntimeInternetMenuWidget::matchHistoryList, RuntimeInternetMenuWidget::matchHistoryDetail}) state.view.showWidget(control, history);
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::rosterList,
            RuntimeInternetMenuWidget::countryLabel,
            RuntimeInternetMenuWidget::countryEdit,
            RuntimeInternetMenuWidget::colorLabel,
            RuntimeInternetMenuWidget::colorEdit,
            RuntimeInternetMenuWidget::teamLabel,
            RuntimeInternetMenuWidget::teamEdit,
            RuntimeInternetMenuWidget::difficultyLabel,
            RuntimeInternetMenuWidget::difficultyEdit,
            RuntimeInternetMenuWidget::startPositionLabel,
            RuntimeInternetMenuWidget::startPositionEdit,
            RuntimeInternetMenuWidget::applySlotButton,
            RuntimeInternetMenuWidget::readyButton,
            RuntimeInternetMenuWidget::kickButton
        }) {
        state.view.showWidget(control, lobby);
    }
    state.view.showWidget(RuntimeInternetMenuWidget::startButton, lobby && state.hosting);
    state.view.showWidget(RuntimeInternetMenuWidget::resumeSwapButton, lobby && state.hosting && state.flow.present()
        && state.flow.lobbyFlow() && state.flow.lobbyFlow()->resuming());
    state.view.showWidget(RuntimeInternetMenuWidget::leaveButton, true);
    state.view.leaveLabel(page==InternetMenuPage::Kicked||page==InternetMenuPage::Account
        ? RuntimeInternetLeaveLabel::Close
        : lobby ? RuntimeInternetLeaveLabel::LeaveRoom : RuntimeInternetLeaveLabel::MainMenu);
    state.view.invalidateSkinned();
}

void updateConnectionText(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present() || !state.flow.transportPresent()) return;
    state.view.setText(
        RuntimeInternetMenuWidget::connectionText,
        state.view.connectionStatus(
            state.flow.coordinatorAuthenticated(),
            state.flow.mode(),
            state.flow.primaryConnectionState()
        )
    );
    const bool browserActions =
        state.page == InternetMenuPage::Browser
        && state.flow.coordinatorAuthenticated()
        && state.flow.communitySnapshot().registeredProfile
        && state.flow.mode()
            == RuntimeInternetLobbyFlowMode::RoomListReady;
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::refreshButton,
            RuntimeInternetMenuWidget::hostButton,
            RuntimeInternetMenuWidget::resumeHostButton,
            RuntimeInternetMenuWidget::joinButton,
            RuntimeInternetMenuWidget::quickMatchButton
        }) {
        state.view.enableWidget(control, browserActions ? true : false);
    }
}

void updateRoomList(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    state.rooms = state.flow.rooms();
    const bool joiningAsSpectator = state.view.checked(RuntimeInternetMenuWidget::spectatorCheck);
    const auto rows = state.view.roomRows(
        state.rooms,
        joiningAsSpectator
    );
    state.view.listClear(RuntimeInternetMenuWidget::roomList);
    for (const RuntimeInternetMultiplayerRoomRow& row : rows) {
        state.view.listAdd(RuntimeInternetMenuWidget::roomList,row.text);
    }
}

std::string communityDisplayName(
    const RuntimeInternetMenuController& state
) {
    if (state.flow.present()) {
        const RuntimeInternetCommunitySnapshot& snapshot =
            state.flow.communitySnapshot();
        if (snapshot.registeredProfile
            && !snapshot.profile.displayName.empty()) {
            return snapshot.profile.displayName;
        }
    }
    return state.view.peerId();
}

void updateNewsDetail(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const auto& news = state.flow.communitySnapshot().news;
    if (news.empty()) {
        state.selectedNewsIndex = 0U;
        state.view.setText(RuntimeInternetMenuWidget::newsDetail, "No community news is available.");
        return;
    }
    state.selectedNewsIndex = std::min(
        state.selectedNewsIndex,
        news.size() - 1U
    );
    const RuntimeInternetCommunityNewsItem& selected =
        news[state.selectedNewsIndex];
    state.view.setText(
        RuntimeInternetMenuWidget::newsDetail,
        selected.title + "\r\n\r\n" + selected.body
    );
    state.view.listSelect(RuntimeInternetMenuWidget::newsList,static_cast<std::int64_t>(state.selectedNewsIndex));
}

void updateRankedHistory(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const auto& records = state.flow.communityMatchHistory();
    state.view.replaceRows(RuntimeInternetMenuWidget::matchHistoryList, state.view.historyRows(records, state.view.peerId()));
    if (records.empty()) {
        state.selectedRankedMatchId.clear();
        state.view.setText(RuntimeInternetMenuWidget::matchHistoryDetail, "暂无排位记录。服务端保留范围内最近的 32 场对局将在这里显示。");
        return;
    }
    auto selected = std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.matchId == state.selectedRankedMatchId; });
    if (selected == records.end()) selected = records.begin();
    state.selectedRankedMatchId = selected->matchId;
    state.view.listSelect(RuntimeInternetMenuWidget::matchHistoryList,static_cast<std::int64_t>(selected - records.begin()));
    state.view.setText(RuntimeInternetMenuWidget::matchHistoryDetail, state.view.historyDetail(*selected, state.view.peerId()));
}

void updateCommunityViews(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const RuntimeInternetCommunitySnapshot& snapshot =
        state.flow.communitySnapshot();
    state.view.setText(
        RuntimeInternetMenuWidget::profileSummaryText,
        state.view.profileSummary(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::channelList,
        state.view.channelRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::chatList,
        state.view.chatRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::participantList,
        state.view.participantRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::friendList,
        state.view.friendRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::ladderList,
        state.view.ladderRows(snapshot)
    );
    state.view.setText(
        RuntimeInternetMenuWidget::clanSummaryText,
        state.view.clanSummary(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::clanMemberList,
        state.view.clanMemberRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::clanInvitationList,
        state.view.clanInvitationRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::clanLadderList,
        state.view.clanLadderRows(snapshot)
    );
    state.view.replaceRows(
        RuntimeInternetMenuWidget::newsList,
        state.view.newsRows(snapshot)
    );

    if (snapshot.registeredProfile) {
        state.view.setText(RuntimeInternetMenuWidget::playerNameEdit, snapshot.profile.displayName);
        state.view.setText(RuntimeInternetMenuWidget::accountHandleEdit, snapshot.profile.displayName);
        if (state.view.text(RuntimeInternetMenuWidget::roomNameEdit)
            == state.view.peerId() + " Room") {
            state.view.setText(
                RuntimeInternetMenuWidget::roomNameEdit,
                snapshot.profile.displayName + " Room"
            );
        }
        for (std::size_t index = 0U; index < snapshot.channels.size(); ++index) {
            if (snapshot.channels[index].channelId
                == snapshot.selectedChannelId) {
                state.view.listSelect(RuntimeInternetMenuWidget::channelList,static_cast<std::int64_t>(index));
                break;
            }
        }
        if (!snapshot.chatHistory.empty()) {
            state.view.listTop(RuntimeInternetMenuWidget::chatList,snapshot.chatHistory.size() - 1U);
        }
    } else if (state.view.text(RuntimeInternetMenuWidget::accountHandleEdit).empty()) {
        state.view.setText(RuntimeInternetMenuWidget::accountHandleEdit, state.view.peerId());
    }
    updateNewsDetail(state);
    updateRankedHistory(state);
    state.view.enableWidget(
        RuntimeInternetMenuWidget::accountRegisterButton,
        snapshot.registeredProfile ? false : true
    );
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::navCommunityButton,
            RuntimeInternetMenuWidget::navGamesButton,
            RuntimeInternetMenuWidget::navLadderButton,
            RuntimeInternetMenuWidget::navNewsButton,
            RuntimeInternetMenuWidget::communityRefreshButton,
            RuntimeInternetMenuWidget::channelJoinButton,
            RuntimeInternetMenuWidget::chatSendButton,
            RuntimeInternetMenuWidget::friendAddButton,
            RuntimeInternetMenuWidget::friendRemoveButton
        }) {
        state.view.enableWidget(control, snapshot.registeredProfile ? true : false);
    }
    const bool clanOwner = snapshot.registeredProfile
        && snapshot.clanMember
        && snapshot.clan.ownerPeerId == snapshot.profile.peerId;
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::clanNameEdit,
            RuntimeInternetMenuWidget::clanTagEdit,
            RuntimeInternetMenuWidget::clanCreateButton
        }) {
        state.view.enableWidget(
            control,
            snapshot.registeredProfile && !snapshot.clanMember ? true : false
        );
    }
    state.view.enableWidget(
        RuntimeInternetMenuWidget::clanAcceptButton,
        snapshot.registeredProfile
                && !snapshot.clanMember
                && !snapshot.clanInvitations.empty()
            ? true
            : false
    );
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::clanTargetEdit,
            RuntimeInternetMenuWidget::clanInviteButton,
            RuntimeInternetMenuWidget::clanRemoveButton,
            RuntimeInternetMenuWidget::clanTransferButton
        }) {
        state.view.enableWidget(control, clanOwner ? true : false);
    }
    state.view.enableWidget(
        RuntimeInternetMenuWidget::clanLeaveButton,
        snapshot.registeredProfile && snapshot.clanMember ? true : false
    );
    state.view.invalidateSkinned();
}

bool nextCommunityTransaction(
    RuntimeInternetMenuController& state,
    std::uint64_t& transactionId,
    std::string& errorMessage
) {
    RuntimeInternetMultiplayerMenuEntropy entropy;
    if (!state.view.entropy(
            entropy,
            errorMessage
        )) {
        return false;
    }
    transactionId = entropy.queryTransactionId;
    return transactionId != 0U;
}

const RuntimeMultiplayerMatchSlot* localHumanSlot(
    const RuntimeInternetMenuController& state
) {
    if (!state.flow.present() || !state.flow.lobbyFlow()
        || !state.flow.lobbyFlow()->lobby().initialized()) {
        return nullptr;
    }
    return state.view.slotForPeer(
        &state.flow.lobbyFlow()->lobby().setup(),
        state.view.peerId()
    );
}

bool localReady(const RuntimeInternetMenuController& state) {
    if (!state.flow.present() || !state.flow.lobbyFlow()
        || !state.flow.lobbyFlow()->bridgePresent()
        || !state.flow.lobbyFlow()->sessionActive()) {
        return false;
    }
    const auto& members =
        state.flow.lobbyFlow()->members();
    const auto found = std::find_if(
        members.begin(),
        members.end(),
        [&](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == state.view.peerId();
        }
    );
    return found != members.end() && found->ready;
}

void populateLocalSlotControls(
    RuntimeInternetMenuController& state
) {
    const RuntimeMultiplayerMatchSlot* slot = localHumanSlot(state);
    const bool enabled = slot != nullptr && state.flow.lobbyFlow() && !state.flow.lobbyFlow()->resuming();
    for (RuntimeInternetMenuWidget control : {
            RuntimeInternetMenuWidget::countryEdit,
            RuntimeInternetMenuWidget::colorEdit,
            RuntimeInternetMenuWidget::teamEdit,
            RuntimeInternetMenuWidget::difficultyEdit,
            RuntimeInternetMenuWidget::startPositionEdit,
            RuntimeInternetMenuWidget::applySlotButton
        }) {
        state.view.enableWidget(control, enabled ? true : false);
    }
    if (!slot) return;
    state.view.setText(RuntimeInternetMenuWidget::countryEdit, slot->countryId);
    state.view.setText(RuntimeInternetMenuWidget::colorEdit, slot->colorId);
    state.view.setText(RuntimeInternetMenuWidget::teamEdit, slot->teamId);
    state.view.setText(RuntimeInternetMenuWidget::difficultyEdit, slot->difficultyId);
    state.view.setText(
        RuntimeInternetMenuWidget::startPositionEdit,
        std::to_string(static_cast<unsigned int>(slot->startPosition))
    );
}

void updateRoster(RuntimeInternetMenuController& state) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (!lobby || !lobby->bridgePresent()
        || !lobby->sessionActive()) {
        return;
    }
    const RuntimeMultiplayerMatchSetup* setup = lobby->lobby().initialized()
        ? &lobby->lobby().setup()
        : nullptr;
    state.hosting = lobby->hostAuthoritative();
    state.rosterRows = state.view.rosterRows(
        lobby->members(),
        setup,
        state.view.peerId(),
        state.hosting
    );
    state.view.listClear(RuntimeInternetMenuWidget::rosterList);
    for (const RuntimeInternetMultiplayerRosterRow& row : state.rosterRows) {
        state.view.listAdd(RuntimeInternetMenuWidget::rosterList,row.text);
    }
    const auto selected = std::find_if(
        state.rosterRows.begin(),
        state.rosterRows.end(),
        [&](const RuntimeInternetMultiplayerRosterRow& row) {
            return row.peerId == state.selectedPeerId;
        }
    );
    if (selected != state.rosterRows.end()) {
        state.view.listSelect(RuntimeInternetMenuWidget::rosterList,static_cast<std::int64_t>(std::distance(
                state.rosterRows.begin(),
                selected
            )));
    }
    if (setup && state.displayedRevision != setup->revision) {
        state.displayedRevision = setup->revision;
        populateLocalSlotControls(state);
    }
    state.view.readyLabel(localReady(state));
    std::string resumeError;
    const bool resumeReady = lobby->resumeReady(resumeError);
    if (lobby->resuming()) populateLocalSlotControls(state);
    state.view.enableWidget(
        RuntimeInternetMenuWidget::readyButton,
        lobby->localMapVerified() && resumeReady ? true : false
    );
    std::string startError;
    state.view.enableWidget(
        RuntimeInternetMenuWidget::startButton,
        state.hosting && lobby->canStart(startError) ? true : false
    );
    const bool kickable = selected != state.rosterRows.end()
        && selected->kickable;
    state.view.enableWidget(RuntimeInternetMenuWidget::kickButton, kickable ? true : false);
    state.view.showWidget(RuntimeInternetMenuWidget::startButton, state.hosting);
}

bool verifyLocalMap(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (!lobby || !lobby->canonicalSetupPublished()) {
        return fail(
            errorMessage,
            "Waiting for the canonical lobby setup before map verification."
        );
    }
    if (!state.localMapPath.empty()) {
        std::string currentError;
        if (lobby->verifyLocalMap(state.localMapPath, currentError)) {
            errorMessage.clear();
            return true;
        }
        state.localMapPath.clear();
    }
    if (!state.view.resolveMap(
            state.baseConfig,
            lobby->lobby().setup().map,
            state.localMapPath,
            errorMessage
        )) {
        if (!lobby->hostAuthoritative())
            return lobby->downloadMissingMap(state.baseConfig.localDataRootPath, state.localMapPath, errorMessage);
        return false;
    }
    if (!lobby->verifyLocalMap(state.localMapPath, errorMessage)) {
        state.localMapPath.clear();
        return false;
    }
    return true;
}

bool applyPendingHostSlots(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
) {
    if (state.pendingHostSlots.empty()) return true;
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (!lobby || !lobby->hostAuthoritative()) return true;
    for (std::size_t index = 0U; index < state.pendingHostSlots.size();
         ++index) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit = lobby->setSlot(
            index,
            state.pendingHostSlots[index],
            publishError
        );
        if (!edit.accepted || !publishError.empty()) {
            return fail(
                errorMessage,
                !publishError.empty() ? publishError : edit.reason
            );
        }
    }
    state.pendingHostSlots.clear();
    return true;
}

void closeInternetMenu(RuntimeInternetMenuController& state) {
    if (!state.view.accepted() && state.flow.present()) state.flow.close();
    if (state.view.hasWidget(RuntimeInternetMenuWidget::passwordEdit)) state.view.clearText(RuntimeInternetMenuWidget::passwordEdit);
    state.view.clearCredentials();
    state.done = true;
    state.view.destroyWindow();
}

void requestCommunityLogin(RuntimeInternetMenuController& state) {
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!state.flow.present()
        || !nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityLogin(
            transactionId,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty()
                ? "Community sign-in could not start."
                : errorMessage
        );
        return;
    }
    setMenuStatus(state, "Signing in to the Westwood Online community...");
}

void registerCommunityProfile(
    RuntimeInternetMenuController& state
) {
    const std::string handle = state.view.trim(state.view.text(RuntimeInternetMenuWidget::accountHandleEdit));
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!state.flow.present()
        || !nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityRegistration(
            transactionId,
            handle,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty() ? "Profile registration failed." : errorMessage
        );
        return;
    }
    setMenuStatus(state, "Registering the authenticated community profile...");
}

void refreshCommunity(RuntimeInternetMenuController& state) {
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!state.flow.present()
        || !nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityRefresh(
            transactionId,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty() ? "Community refresh failed." : errorMessage
        );
        return;
    }
    setMenuStatus(
        state,
        "Refreshing profile, Buddy List, Clan, ladders, and news..."
    );
}

void joinSelectedCommunityChannel(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::channelList);
    const auto& channels = state.flow.communitySnapshot().channels;
    if (selected == -1
        || static_cast<std::size_t>(selected) >= channels.size()) {
        setMenuStatus(state, "Select a community channel first.");
        return;
    }
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityChannelJoin(
            transactionId,
            channels[static_cast<std::size_t>(selected)].channelId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Joining the selected community channel...");
}

void sendCommunityChat(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const std::string message = state.view.trim(state.view.text(RuntimeInternetMenuWidget::chatEdit));
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityChannelMessage(
            transactionId,
            message,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty() ? "Enter a chat message first." : errorMessage
        );
        return;
    }
    state.view.clearText(RuntimeInternetMenuWidget::chatEdit);
}

void addCommunityFriend(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const std::string target = state.view.trim(state.view.text(RuntimeInternetMenuWidget::friendEdit));
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityFriendAdd(
            transactionId,
            target,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty() ? "Enter a Buddy handle first." : errorMessage
        );
        return;
    }
    state.view.clearText(RuntimeInternetMenuWidget::friendEdit);
    setMenuStatus(state, "Updating the Buddy List...");
}

void removeSelectedCommunityFriend(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::friendList);
    const auto& friends = state.flow.communitySnapshot().friends;
    if (selected == -1
        || static_cast<std::size_t>(selected) >= friends.size()) {
        setMenuStatus(state, "Select a Buddy to remove.");
        return;
    }
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityFriendRemove(
            transactionId,
            friends[static_cast<std::size_t>(selected)].peerId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Updating the Buddy List...");
}

void createCommunityClan(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    const std::string name = state.view.trim(state.view.text(RuntimeInternetMenuWidget::clanNameEdit));
    const std::string tag = state.view.trim(state.view.text(RuntimeInternetMenuWidget::clanTagEdit));
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanCreate(
            transactionId,
            name,
            tag,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty()
                ? "Enter a Clan name and a 2-8 character tag."
                : errorMessage
        );
        return;
    }
    setMenuStatus(state, "Creating the Clan...");
}

void inviteCommunityClanMember(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::string target = state.view.trim(state.view.text(RuntimeInternetMenuWidget::clanTargetEdit));
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanInvite(
            transactionId,
            target,
            errorMessage
        )) {
        setMenuStatus(
            state,
            errorMessage.empty() ? "Enter a member handle first." : errorMessage
        );
        return;
    }
    state.view.clearText(RuntimeInternetMenuWidget::clanTargetEdit);
    setMenuStatus(state, "Sending the Clan invitation...");
}

void acceptSelectedCommunityClanInvitation(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::clanInvitationList);
    const auto& invitations =
        state.flow.communitySnapshot().clanInvitations;
    if (selected == -1
        || static_cast<std::size_t>(selected) >= invitations.size()) {
        setMenuStatus(state, "Select a Clan invitation to accept.");
        return;
    }
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanInvitationAccept(
            transactionId,
            invitations[static_cast<std::size_t>(selected)].clanId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Accepting the Clan invitation...");
}

void removeSelectedCommunityClanMember(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::clanMemberList);
    const auto& members = state.flow.communitySnapshot().clanMembers;
    if (selected == -1
        || static_cast<std::size_t>(selected) >= members.size()) {
        setMenuStatus(state, "Select a Clan member to remove.");
        return;
    }
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanMemberRemove(
            transactionId,
            members[static_cast<std::size_t>(selected)].peerId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Removing the selected Clan member...");
}

void transferCommunityClanOwnership(
    RuntimeInternetMenuController& state
) {
    if (!state.flow.present()) return;
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::clanMemberList);
    const auto& members = state.flow.communitySnapshot().clanMembers;
    if (selected == -1
        || static_cast<std::size_t>(selected) >= members.size()) {
        setMenuStatus(state, "Select the new Clan owner.");
        return;
    }
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanOwnershipTransfer(
            transactionId,
            members[static_cast<std::size_t>(selected)].peerId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Transferring Clan ownership...");
}

void leaveCommunityClan(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    std::uint64_t transactionId = 0U;
    std::string errorMessage;
    if (!nextCommunityTransaction(state, transactionId, errorMessage)
        || !state.flow.requestCommunityClanLeave(
            transactionId,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    setMenuStatus(state, "Leaving the Clan...");
}

bool beginRoomRefresh(
    RuntimeInternetMenuController& state,
    std::string& errorMessage
) {
    RuntimeInternetMultiplayerMenuEntropy entropy;
    RuntimeInternetLobbyQueryOptions query;
    if (!state.view.entropy(entropy, errorMessage)
        || !state.view.queryOptions(
            state.compatibility,
            entropy.queryTransactionId,
            query,
            errorMessage
        )
        || !state.flow.beginRoomQuery(
            std::move(query),
            state.view.nowMs(),
            errorMessage
        )) {
        return false;
    }
    state.rooms.clear();
    state.view.listClear(RuntimeInternetMenuWidget::roomList);
    setMenuStatus(state, "Refreshing formal Internet rooms...");
    updateConnectionText(state);
    return true;
}

void beginHost(RuntimeInternetMenuController& state, bool fromSave) {
    std::string errorMessage;
    RuntimeInternetMultiplayerMenuEntropy entropy;
    RuntimeMultiplayerHostSelectionProjection projection;
    std::shared_ptr<RuntimeMultiplayerResumeSave> resumeSave;
    const std::string displayName = communityDisplayName(state);
    if (fromSave) {
        resumeSave = std::make_shared<RuntimeMultiplayerResumeSave>();
        if (!state.view.selectResumeWithOriginalFailureStatus(state.compatibility,*resumeSave,projection,errorMessage)) return;
    }
    if (!state.view.entropy(entropy, errorMessage)
        || (!fromSave && !state.view.selectHostSetup(
            displayName,
            entropy,
            projection,
            errorMessage
        ))) {
        setMenuStatus(state, errorMessage);
        return;
    }
    RuntimeInternetLobbyHostOptions host;
    if (!state.view.hostOptions(
            state.compatibility,projection,entropy,host,errorMessage,resumeSave
        )
        || !state.flow.startHost(
            std::move(host),
            state.view.nowMs(),
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    state.pendingHostSlots = fromSave ? std::vector<RuntimeMultiplayerMatchSlot>{} : projection.slots;
    state.view.clearText(RuntimeInternetMenuWidget::passwordEdit);
    state.localMapPath = projection.localMapPath;
    state.hosting = true;
    if (!applyPendingHostSlots(state, errorMessage)) {
        state.flow.close();
        setMenuStatus(state, errorMessage);
        return;
    }
    showInternetMenuPage(state, InternetMenuPage::Lobby);
    if (verifyLocalMap(state, errorMessage)) {
        setMenuStatus(
            state,
            fromSave ? "恢复房间已建立。等待原有席位到齐并接收存档；房主可选中玩家交换席位。"
                : "Room is registered through the TLS service. Waiting for players."
        );
    } else {
        setMenuStatus(state, errorMessage);
    }
    updateRoster(state);
}

void joinSelectedRoom(RuntimeInternetMenuController& state) {
    const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::roomList);
    if (selected == -1
        || static_cast<std::size_t>(selected) >= state.rooms.size()) {
        setMenuStatus(state, "Select a formal Internet room first.");
        return;
    }
    std::string errorMessage;
    RuntimeInternetMultiplayerMenuEntropy entropy;
    RuntimeInternetLobbyJoinOptions joining;
    const bool spectator = state.view.checked(RuntimeInternetMenuWidget::spectatorCheck);
    if (!state.view.entropy(entropy, errorMessage)
        || !state.view.joinOptions(
            state.compatibility,state.rooms[static_cast<std::size_t>(selected)],spectator,entropy.joinTransactionId,joining,errorMessage
        )
        || !state.flow.join(
            std::move(joining),
            state.view.nowMs(),
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    state.hosting = false;
    state.view.clearText(RuntimeInternetMenuWidget::passwordEdit);
    state.localMapPath.clear();
    state.pendingHostSlots.clear();
    showInternetMenuPage(state, InternetMenuPage::Lobby);
    setMenuStatus(
        state,
        "Opening the authenticated TCP relay and joining the canonical lobby..."
    );
}

void beginQuickMatch(RuntimeInternetMenuController& state) {
    std::string errorMessage;
    std::string displayName = communityDisplayName(state);
    if (!state.view.validateDisplayText(
            displayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes,
            "Quick Match display name",
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    RuntimeInternetMultiplayerMenuEntropy entropy;
    RuntimeMultiplayerHostSelectionProjection projection;
    if (!state.view.entropy(entropy, errorMessage)
        || !state.view.selectHostSetup(
            displayName,
            entropy,
            projection,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }

    // A two-player queue always exposes one human host and one free human
    // position. Authored map/rules remain selected, while local AI choices do
    // not silently change the queue's promised player count.
    projection.lobby.computerSlotIndices.clear();
    std::vector<RuntimeMultiplayerMatchSlot> quickSlots = projection.slots;
    for (std::size_t index = 1U; index < quickSlots.size(); ++index) {
        quickSlots[index] = RuntimeMultiplayerMatchSlot{};
        quickSlots[index].slotIndex = static_cast<std::uint8_t>(index);
        quickSlots[index].controller =
            RuntimeMultiplayerMatchSlotController::Closed;
    }

    RuntimeInternetLobbyMatchmakingOptions matching;
    const std::int32_t skillRating = state.flow.present()
            && state.flow.communitySnapshot().registeredProfile
        ? state.flow.communitySnapshot().profile.points
        : 1000;
    if (!state.view.matchmakingOptions(
            state.compatibility,
            projection,
            displayName,
            skillRating,
            entropy,
            matching,
            errorMessage
        )
        || !state.flow.beginMatchmaking(
            std::move(matching),
            state.view.nowMs(),
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    state.pendingHostSlots = std::move(quickSlots);
    state.view.clearText(RuntimeInternetMenuWidget::passwordEdit);
    state.localMapPath = projection.localMapPath;
    state.hosting = false;
    setMenuStatus(
        state,
        "Quick Match queued for exactly two authenticated players..."
    );
}

void applyLocalOptions(RuntimeInternetMenuController& state) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    const RuntimeMultiplayerMatchSlot* slot = localHumanSlot(state);
    if (!lobby || !slot) {
        setMenuStatus(state, "Spectators do not own a player slot.");
        return;
    }
    const std::string startText = state.view.trim(
        state.view.text(RuntimeInternetMenuWidget::startPositionEdit)
    );
    if (startText.size() != 1U || startText.front() < '0'
        || startText.front() > '8') {
        setMenuStatus(state, "Start position must be one digit from 0 to 8.");
        return;
    }
    const std::uint8_t start = static_cast<std::uint8_t>(
        startText.front() - '0'
    );
    if (lobby->hostAuthoritative()) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit = lobby->setSlotOptions(
            slot->slotIndex,
            state.view.trim(state.view.text(RuntimeInternetMenuWidget::countryEdit)),
            state.view.trim(state.view.text(RuntimeInternetMenuWidget::colorEdit)),
            state.view.trim(state.view.text(RuntimeInternetMenuWidget::teamEdit)),
            state.view.trim(state.view.text(RuntimeInternetMenuWidget::difficultyEdit)),
            start,
            publishError
        );
        if (!edit.accepted || !publishError.empty()) {
            setMenuStatus(
                state,
                !publishError.empty() ? publishError : edit.reason
            );
            return;
        }
        setMenuStatus(state, "Canonical player setup updated.");
    } else {
        const RuntimeMultiplayerLanOperationResult result =
            lobby->requestLocalSlotOptions(
                state.view.trim(state.view.text(RuntimeInternetMenuWidget::countryEdit)),
                state.view.trim(state.view.text(RuntimeInternetMenuWidget::colorEdit)),
                state.view.trim(state.view.text(RuntimeInternetMenuWidget::teamEdit)),
                state.view.trim(state.view.text(RuntimeInternetMenuWidget::difficultyEdit)),
                start
            );
        if (!result.succeeded) {
            setMenuStatus(state, result.reason);
            return;
        }
        setMenuStatus(state, "Player setup request sent to the host...");
    }
    updateRoster(state);
}

void toggleReady(RuntimeInternetMenuController& state) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (!lobby) return;
    const RuntimeMultiplayerLanOperationResult result = lobby->setReady(
        !localReady(state)
    );
    if (!result.succeeded) {
        setMenuStatus(state, result.reason);
        return;
    }
    setMenuStatus(
        state,
        result.queued ? "Ready change sent to host..."
                      : "Ready state updated."
    );
    updateRoster(state);
}

void kickSelected(RuntimeInternetMenuController& state) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    const auto selected = std::find_if(
        state.rosterRows.begin(),
        state.rosterRows.end(),
        [&](const RuntimeInternetMultiplayerRosterRow& row) {
            return row.peerId == state.selectedPeerId && row.kickable;
        }
    );
    if (!lobby || selected == state.rosterRows.end()) {
        setMenuStatus(state, "Select a remote lobby member first.");
        return;
    }
    const RuntimeMultiplayerLanOperationResult result = lobby->kickPeer(
        selected->peerId,
        "removed by the Internet room host"
    );
    if (!result.succeeded) {
        setMenuStatus(state, result.reason);
        return;
    }
    state.selectedPeerId.clear();
    setMenuStatus(state, "Selected remote lobby member was removed.");
    updateRoster(state);
}

void startInternetMatch(RuntimeInternetMenuController& state) {
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (!lobby) return;
    const RuntimeMultiplayerLanOperationResult result = lobby->startMatch();
    if (!result.succeeded) {
        setMenuStatus(state, result.reason);
        return;
    }
    setMenuStatus(state, "Starting the verified Internet match...");
}

void showKickedPage(
    RuntimeInternetMenuController& state,
    std::string reason
) {
    state.hosting = false;
    state.selectedPeerId.clear();
    state.pendingHostSlots.clear();
    if (state.flow.present()) state.flow.close();
    showInternetMenuPage(state, InternetMenuPage::Kicked);
    setMenuStatus(
        state,
        reason.empty()
            ? "You were removed from the Internet lobby by its host."
            : "You were removed from the Internet lobby: " + reason
    );
}

void pollInternetMenu(RuntimeInternetMenuController& state) {
    if (!state.flow.present()) return;
    if (state.page == InternetMenuPage::Kicked) {
        updateConnectionText(state);
        return;
    }
    const RuntimeInternetLobbyFlowPollResult polled = state.flow.poll(
        state.view.nowMs(),
        0
    );
    updateConnectionText(state);
    for (const RuntimeInternetLobbyFlowEvent& event : polled.events) {
        switch (event.kind) {
        case RuntimeInternetLobbyFlowEventKind::MatchQueued:
            setMenuStatus(
                state,
                "Quick Match accepted. Waiting for a compatible player..."
            );
            break;
        case RuntimeInternetLobbyFlowEventKind::MatchAssigned:
            setMenuStatus(
                state,
                "Quick Match assigned. Preparing its authenticated room..."
            );
            break;
        case RuntimeInternetLobbyFlowEventKind::MatchActivated:
            setMenuStatus(
                state,
                "Quick Match room activated for both authenticated players."
            );
            break;
        case RuntimeInternetLobbyFlowEventKind::JoinAccepted:
            setMenuStatus(
                state,
                "Internet admission accepted. Synchronizing canonical lobby..."
            );
            break;
        case RuntimeInternetLobbyFlowEventKind::JoinRejected:
            setMenuStatus(
                state,
                event.reason.empty()
                    ? "Internet room admission was rejected."
                    : "Internet room admission was rejected: " + event.reason
            );
            break;
        case RuntimeInternetLobbyFlowEventKind::Failed:
            if (!event.reason.empty()) setMenuStatus(state, event.reason);
            break;
        default:
            break;
        }
    }
    for (const RuntimeInternetCommunityClientEvent& event
         : polled.communityEvents) {
        switch (event.kind) {
        case RuntimeInternetCommunityClientEventKind::RegistrationRequired:
            if (state.page != InternetMenuPage::Lobby
                && state.page != InternetMenuPage::Kicked) {
                showInternetMenuPage(state, InternetMenuPage::Account);
            }
            setMenuStatus(
                state,
                event.status.empty()
                    ? "Create a community profile to continue."
                    : event.status
            );
            break;
        case RuntimeInternetCommunityClientEventKind::SnapshotUpdated:
            if (state.page == InternetMenuPage::Account
                && state.flow.communitySnapshot().registeredProfile) {
                showInternetMenuPage(state, InternetMenuPage::Community);
            }
            if (!event.status.empty()) setMenuStatus(state, event.status);
            break;
        case RuntimeInternetCommunityClientEventKind::ChannelMessageReceived:
            if (state.page == InternetMenuPage::Community) {
                setMenuStatus(
                    state,
                    event.channelMessage.senderDisplayName
                        + " posted in the current channel."
                );
            }
            break;
        case RuntimeInternetCommunityClientEventKind::OperationRejected:
            setMenuStatus(
                state,
                event.status.empty()
                    ? "The community service rejected that operation."
                    : event.status
            );
            break;
        }
    }
    for (const RuntimeMultiplayerLanEvent& event : polled.lobbyEvents) {
        if (event.kind == RuntimeMultiplayerLanEventKind::Kicked) {
            showKickedPage(state, event.reason);
            return;
        }
        if (event.kind
                == RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult
            && event.lobbySlotOptionsResult.has_value()) {
            const auto& response = *event.lobbySlotOptionsResult;
            setMenuStatus(
                state,
                response.accepted
                    ? "Host accepted the player setup."
                    : (response.reason.empty()
                        ? "Host rejected the player setup."
                        : response.reason)
            );
        }
    }
    if (!polled.succeeded) {
        setMenuStatus(
            state,
            polled.error.empty()
                ? "Internet lobby connection failed."
                : polled.error
        );
        return;
    }
    if (polled.communityChanged) updateCommunityViews(state);
    if (polled.roomsChanged) updateRoomList(state);

    std::string slotError;
    if (!applyPendingHostSlots(state, slotError)) {
        setMenuStatus(state, slotError);
        return;
    }
    RuntimeLanMenuFlow* lobby = state.flow.lobbyFlow();
    if (lobby && lobby->initialized()
        && state.flow.mode() == RuntimeInternetLobbyFlowMode::Lobby) {
        state.hosting = lobby->hostAuthoritative();
        if (!state.hosting) state.pendingHostSlots.clear();
        if (state.page == InternetMenuPage::Browser) {
            showInternetMenuPage(state, InternetMenuPage::Lobby);
            setMenuStatus(
                state,
                "Authenticated relay connected. Verifying the canonical map..."
            );
        }
        if (!lobby->localMapVerified()
            && lobby->canonicalSetupPublished()) {
            std::string verificationError;
            if (verifyLocalMap(state, verificationError)) {
                setMenuStatus(
                    state,
                    "Local map verified. Player setup and Ready are available."
                );
            } else {
                setMenuStatus(state, verificationError);
            }
        }
        updateRoster(state);
    }
    if (polled.matchStarted
        || state.flow.mode() == RuntimeInternetLobbyFlowMode::Running) {
        state.view.finishMatch();
    }
}
bool handleInternetSelection(RuntimeInternetMenuController& state,int control) {
    if(control==InternetRoster) {
            const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::rosterList);
            state.selectedPeerId = selected != -1
                    && static_cast<std::size_t>(selected)
                        < state.rosterRows.size()
                ? state.rosterRows[static_cast<std::size_t>(selected)].peerId
                : std::string{};
            updateRoster(state);
            return true;
            }
    if(control==InternetNewsList) {
            const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::newsList);
            if (selected != -1) {
                state.selectedNewsIndex = static_cast<std::size_t>(selected);
                updateNewsDetail(state);
            }
            return true;
            }
    if(control==InternetMatchHistoryList && state.flow.present()) {
            const std::int64_t selected = state.view.listSelected(RuntimeInternetMenuWidget::matchHistoryList);
            const auto& records = state.flow.communityMatchHistory();
            if (selected >= 0 && static_cast<std::size_t>(selected) < records.size()) {
                state.selectedRankedMatchId = records[static_cast<std::size_t>(selected)].matchId;
                updateRankedHistory(state);
            }
            return true;
            }
    return false;
}

bool handleInternetDoubleClick(RuntimeInternetMenuController& state,int control) {
    if(control==InternetChannelList) { joinSelectedCommunityChannel(state);return true; }
    if(control==InternetClanInvitationList) { acceptSelectedCommunityClanInvitation(state);return true; }
    return false;
}

bool handleInternetClick(RuntimeInternetMenuController& state,int control) {
        switch (control) {
        case InternetMatchHistory:
            showInternetMenuPage(state, InternetMenuPage::MatchHistory);
            updateRankedHistory(state);
            refreshCommunity(state);
            return true;
        case InternetMatchHistoryBack:
            showInternetMenuPage(state, InternetMenuPage::Ladder);
            updateCommunityViews(state);
            return true;
        case InternetNavCommunity:
            showInternetMenuPage(state, InternetMenuPage::Community);
            updateCommunityViews(state);
            return true;
        case InternetNavGames:
            showInternetMenuPage(state, InternetMenuPage::Browser);
            updateRoomList(state);
            updateConnectionText(state);
            return true;
        case InternetNavLadder:
            showInternetMenuPage(state, InternetMenuPage::Ladder);
            updateCommunityViews(state);
            return true;
        case InternetNavNews:
            showInternetMenuPage(state, InternetMenuPage::News);
            updateCommunityViews(state);
            return true;
        case InternetAccountRegister:
            registerCommunityProfile(state);
            return true;
        case InternetCommunityRefresh:
            refreshCommunity(state);
            return true;
        case InternetChannelJoin:
            joinSelectedCommunityChannel(state);
            return true;
        case InternetChatSend:
            sendCommunityChat(state);
            return true;
        case InternetFriendAdd:
            addCommunityFriend(state);
            return true;
        case InternetFriendRemove:
            removeSelectedCommunityFriend(state);
            return true;
        case InternetClanCreate:
            createCommunityClan(state);
            return true;
        case InternetClanInvite:
            inviteCommunityClanMember(state);
            return true;
        case InternetClanAccept:
            acceptSelectedCommunityClanInvitation(state);
            return true;
        case InternetClanRemove:
            removeSelectedCommunityClanMember(state);
            return true;
        case InternetClanTransfer:
            transferCommunityClanOwnership(state);
            return true;
        case InternetClanLeave:
            leaveCommunityClan(state);
            return true;
        case InternetRefresh: {
            std::string errorMessage;
            if (!beginRoomRefresh(state, errorMessage)) {
                setMenuStatus(state, errorMessage);
            }
            return true;
        }
        case InternetHost:
            beginHost(state);
            return true;
        case InternetJoin:
            joinSelectedRoom(state);
            return true;
        case InternetResumeHost:
            beginHost(state, true);return true;
        case InternetResumeSwap: {
            auto* lobby = state.flow.lobbyFlow();
            if (!lobby || !lobby->hostAuthoritative() || !lobby->resuming()) return true;
            const auto& slots = lobby->lobby().setup().slots;
            const auto target = std::find_if(slots.begin(), slots.end(), [&](const auto& slot) {
                return slot.controller == RuntimeMultiplayerMatchSlotController::Human && slot.peerId == state.selectedPeerId;
            });
            if (target == slots.end()) { setMenuStatus(state, "请先选中一位占有席位的玩家。");return true; }
            std::string error;
            const auto edit = lobby->setSlotController(target->slotIndex, RuntimeMultiplayerMatchSlotController::Human, state.view.peerId(), error);
            setMenuStatus(state, edit.accepted && error.empty() ? "席位已交换，请重新准备。" : error);
            updateRoster(state);return true;
        }
        case InternetSpectator:
            updateRoomList(state);
            state.view.invalidateSkin();
            return true;
        case InternetQuickMatch:
            beginQuickMatch(state);
            return true;
        case InternetApplySlot:
            applyLocalOptions(state);
            return true;
        case InternetReady:
            toggleReady(state);
            return true;
        case InternetStart:
            startInternetMatch(state);
            return true;
        case InternetKick:
            kickSelected(state);
            return true;
        case InternetLeave:
            closeInternetMenu(state);
            return true;
        default:
            break;
        }
    return false;
}

void initializeInternetMenu(RuntimeInternetMenuController& state) {
    state.view.setText(RuntimeInternetMenuWidget::playerNameEdit, state.view.peerId());
    state.view.setText(RuntimeInternetMenuWidget::accountHandleEdit, state.view.peerId());
    state.view.setText(
        RuntimeInternetMenuWidget::roomNameEdit,
        state.view.peerId() + " Room"
    );
    std::string queryError;
    if (!beginRoomRefresh(state, queryError)) {
        setMenuStatus(state, queryError);
    } else {
        requestCommunityLogin(state);
    }
    updateConnectionText(state);
}

} // namespace concord::internet_menu
