#include "runtime/menu/RuntimeLanMenuController.h"
#include "network/lobby/RuntimeRoomPassword.h"
#include <algorithm>
#include <sstream>
#include <utility>

namespace concord::lan_menu {
namespace { bool fail(std::string& error, std::string reason) { error = std::move(reason); return false; } }
namespace fs = std::filesystem;
void setStatus(
    RuntimeLanMenuController& state,
    const std::string& text
) {
    state.view.setText(RuntimeLanMenuWidget::statusText, text);
}

void showPage(
    RuntimeLanMenuController& state,
    MultiplayerMenuPage page
) {
    state.page = page;
    const bool browser = page == MultiplayerMenuPage::Browser;
    const bool lan = state.network == RuntimeMultiplayerMenuNetwork::Lan;
    state.view.showWidget(RuntimeLanMenuWidget::playerNameLabel, browser);
    state.view.showWidget(RuntimeLanMenuWidget::playerNameEdit, browser);
    state.view.showWidget(RuntimeLanMenuWidget::hostPortLabel, browser && !lan);
    state.view.showWidget(RuntimeLanMenuWidget::hostPortEdit, browser && !lan);
    state.view.showWidget(RuntimeLanMenuWidget::directAddressLabel, browser && !lan);
    state.view.showWidget(RuntimeLanMenuWidget::directAddressEdit, browser && !lan);
    state.view.showWidget(RuntimeLanMenuWidget::passwordLabel, browser);
    state.view.showWidget(RuntimeLanMenuWidget::passwordEdit, browser);
    state.view.showWidget(RuntimeLanMenuWidget::hostButton, browser);
    state.view.showWidget(RuntimeLanMenuWidget::resumeHostButton, browser);
    state.view.showWidget(RuntimeLanMenuWidget::resumeSwapButton, !browser && state.hosting && state.flow.resuming());
    state.view.showWidget(RuntimeLanMenuWidget::refreshButton, browser && lan);
    state.view.showWidget(RuntimeLanMenuWidget::joinButton, browser);
    state.view.showWidget(RuntimeLanMenuWidget::spectatorCheck, browser);
    state.view.showWidget(RuntimeLanMenuWidget::backButton, browser);
    state.view.showWidget(RuntimeLanMenuWidget::roomList, browser && lan);
    state.view.showWidget(RuntimeLanMenuWidget::rosterList, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::countryLabel, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::countryEdit, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::colorLabel, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::colorEdit, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::teamLabel, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::teamEdit, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::difficultyLabel, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::difficultyEdit, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::startPositionLabel, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::startPositionEdit, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::applySlotButton, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::kickButton, !browser && state.hosting);
    state.view.showWidget(RuntimeLanMenuWidget::readyButton, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::startButton, !browser && state.hosting);
    state.view.showWidget(RuntimeLanMenuWidget::leaveButton, !browser);
    state.view.showWidget(RuntimeLanMenuWidget::networkHint, browser);
    if (!browser) {
        state.displayedSetupRevision = 0U;
        setStatus(state, "Lobby connected; verifying local map...");
    }
}

void updateRoomList(RuntimeLanMenuController& state) {
    state.rooms = state.flow.discoveredRooms();
    state.view.listClear(RuntimeLanMenuWidget::roomList);
    for (const RuntimeLanRoomAdvertisement& room : state.rooms) {
        std::ostringstream line;
        line << room.roomName << "  ["
             << static_cast<unsigned int>(room.playerCount) << '/'
             << static_cast<unsigned int>(room.maximumPlayers) << "]  "
             << room.hostName << "  " << room.hostAddress << ':'
             << room.sessionPort;
        if (room.passwordProtected) line << "  [Password]";
        state.view.listAdd(RuntimeLanMenuWidget::roomList, line.str());
    }
}

bool localMemberReady(const RuntimeLanMenuController& state) {
    if (!state.flow.bridgePresent()
        || !state.flow.sessionActive()) {
        return false;
    }
    const std::string& localPeerId =
        state.flow.localPeerId();
    for (const RuntimeMultiplayerRoomMember& member
         : state.flow.members()) {
        if (member.peerId == localPeerId) return member.ready;
    }
    return false;
}

const RuntimeMultiplayerMatchSlot* humanSlotForPeer(
    const RuntimeLanMenuController& state,
    std::string_view peerId
) {
    if (!state.flow.lobby().initialized()) return nullptr;
    const auto& slots = state.flow.lobby().setup().slots;
    const auto found = std::find_if(
        slots.begin(),
        slots.end(),
        [peerId](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && slot.peerId == peerId;
        }
    );
    return found == slots.end() ? nullptr : &*found;
}

void populateLocalSlotControls(RuntimeLanMenuController& state) {
    if (!state.flow.bridgePresent()
        || !state.flow.lobby().initialized()) {
        return;
    }
    const RuntimeMultiplayerMatchSlot* slot = humanSlotForPeer(
        state,
        state.flow.localPeerId()
    );
    const bool editable = slot != nullptr && !state.flow.resuming();
    for (RuntimeLanMenuWidget control : {
            RuntimeLanMenuWidget::countryEdit,
            RuntimeLanMenuWidget::colorEdit,
            RuntimeLanMenuWidget::teamEdit,
            RuntimeLanMenuWidget::difficultyEdit,
            RuntimeLanMenuWidget::startPositionEdit,
            RuntimeLanMenuWidget::applySlotButton
        }) {
        state.view.enableWidget(control, editable ? true : false);
    }
    if (!slot) return;
    if (state.flow.lobby().setup().rules.gameMode == "cooperative") {
        state.view.enableWidget(RuntimeLanMenuWidget::teamEdit, false);
        state.view.enableWidget(RuntimeLanMenuWidget::startPositionEdit, false);
    }
    state.view.setText(RuntimeLanMenuWidget::countryEdit, slot->countryId);
    state.view.setText(RuntimeLanMenuWidget::colorEdit, slot->colorId);
    state.view.setText(RuntimeLanMenuWidget::teamEdit, slot->teamId);
    state.view.setText(RuntimeLanMenuWidget::difficultyEdit, slot->difficultyId);
    state.view.setText(
        RuntimeLanMenuWidget::startPositionEdit,
        std::to_string(static_cast<unsigned int>(slot->startPosition))
    );
}

void updateRoster(RuntimeLanMenuController& state) {
    state.view.listClear(RuntimeLanMenuWidget::rosterList);
    state.rosterPeerIds.clear();
    if (!state.flow.bridgePresent()
        || !state.flow.sessionActive()) {
        return;
    }
    for (const RuntimeMultiplayerRoomMember& member
         : state.flow.members()) {
        std::string line = member.displayName;
        line += member.role == RuntimeMultiplayerRoomMemberRole::Host
            ? "  [Host]"
            : "  [Client]";
        line += member.participation
                == RuntimeMultiplayerParticipationRole::Spectator
            ? " [Spectator]"
            : " [Combatant]";
        line += member.ready ? " [Ready]" : " [Not Ready]";
        if (const RuntimeMultiplayerMatchSlot* slot = humanSlotForPeer(
                state,
                member.peerId
            )) {
            line += "  [" + slot->countryId + " / " + slot->colorId
                + " / Team " + slot->teamId + " / "
                + slot->difficultyId + " / Start "
                + std::to_string(
                    static_cast<unsigned int>(slot->startPosition)
                ) + "]";
        }
        state.view.listAdd(RuntimeLanMenuWidget::rosterList, line);
        state.rosterPeerIds.push_back(member.peerId);
    }
    const auto selected = std::find(
        state.rosterPeerIds.begin(),
        state.rosterPeerIds.end(),
        state.selectedRosterPeerId
    );
    if (selected != state.rosterPeerIds.end()) {
        state.view.listSelect(RuntimeLanMenuWidget::rosterList, static_cast<std::int64_t>(std::distance(
                state.rosterPeerIds.begin(),
                selected
            )));
    }
    if (state.flow.lobby().initialized()
        && state.displayedSetupRevision
            != state.flow.lobby().setup().revision) {
        state.displayedSetupRevision = state.flow.lobby().setup().revision;
        populateLocalSlotControls(state);
    }
    state.view.readyLabel(localMemberReady(state));
    std::string resumeError;
    const bool resumeReady = state.flow.resumeReady(resumeError);
    if (state.flow.resuming()) populateLocalSlotControls(state);
    state.view.enableWidget(
        RuntimeLanMenuWidget::readyButton,
        state.flow.localMapVerified() && resumeReady ? true : false
    );
    if (state.hosting) {
        std::string startError;
        state.view.enableWidget(
            RuntimeLanMenuWidget::startButton,
            state.flow.canStart(startError) ? true : false
        );
    }
    const std::string& localPeerId =
        state.flow.localPeerId();
    state.view.enableWidget(
        RuntimeLanMenuWidget::kickButton,
        state.hosting && !state.selectedRosterPeerId.empty()
                && state.selectedRosterPeerId != localPeerId
                && std::find(
                    state.rosterPeerIds.begin(),
                    state.rosterPeerIds.end(),
                    state.selectedRosterPeerId
                ) != state.rosterPeerIds.end()
            ? true
            : false
    );
}

bool ensureLocalMapVerified(
    RuntimeLanMenuController& state,
    std::string& errorMessage
) {
    if (state.flow.localMapVerified()) return true;
    if (!state.flow.canonicalSetupPublished()) {
        return fail(
            errorMessage,
            "waiting for the canonical room setup before map verification"
        );
    }
    if (!state.localMapPath.empty() && !state.flow.verifyLocalMap(state.localMapPath, errorMessage)) state.localMapPath.clear();
    if (state.localMapPath.empty()) {
        if (!state.view.resolveMap(
                state.baseConfig,
                state.flow.lobby().setup().map,
                state.localMapPath,
                errorMessage
            )) {
            if (!state.flow.hostAuthoritative())
                return state.flow.downloadMissingMap(state.baseConfig.localDataRootPath, state.localMapPath, errorMessage);
            return false;
        }
    }
    if (!state.flow.verifyLocalMap(state.localMapPath, errorMessage)) {
        state.localMapPath.clear();
        return false;
    }
    return true;
}

void closeMenu(RuntimeLanMenuController& state) {
    state.directDiscoveryPending = false;
    runtimeRoomPasswordSecureClear(state.pendingPassword);
    if (state.view.hasWidget(RuntimeLanMenuWidget::passwordEdit)) state.view.clearPassword();
    if (!state.view.accepted()) state.flow.close();
    state.done = true;
    state.view.destroyWindow();
}

bool beginLanBrowsing(
    RuntimeLanMenuController& state,
    std::string& errorMessage
) {
    if (!state.flow.bridgePresent()
        || state.flow.lanPhase()
            != RuntimeMultiplayerLanPhase::Browsing) {
        if (!state.flow.startBrowsing(
                runtimeLanDefaultDiscoveryPort,
                errorMessage
            )) {
            return false;
        }
    }
    std::string loopbackError;
    const bool broadcast = state.flow.sendDiscoveryProbe(
        "255.255.255.255",
        errorMessage
    );
    const bool loopback = state.flow.sendDiscoveryProbe(
        "127.0.0.1",
        loopbackError
    );
    if (!broadcast && !loopback) {
        if (errorMessage.empty()) errorMessage = std::move(loopbackError);
        return false;
    }
    errorMessage.clear();
    return true;
}

bool enterLobbyWithRoom(
    RuntimeLanMenuController& state,
    const RuntimeLanRoomAdvertisement& room,
    std::string playerName,
    bool spectator,
    std::string password,
    std::string& errorMessage
) {
    RuntimeMultiplayerLanJoinOptions join;
    if (!state.view.makeJoinOptions(
            room,
            std::move(playerName),
            state.peerId,
            spectator,
            std::move(password),
            state.compatibility,
            join,
            errorMessage
        )
        || !state.flow.join(std::move(join), errorMessage)) {
        return false;
    }
    state.hosting = false;
    state.view.clearPassword();
    state.localMapPath.clear();
    state.directDiscoveryPending = false;
    showPage(state, MultiplayerMenuPage::Lobby);
    setStatus(state, "Connecting and waiting for canonical room setup...");
    return true;
}

bool startHosting(
    RuntimeLanMenuController& state,
    std::string& errorMessage,
    bool fromSave
) {
    if (!state.compatibilityReady) {
        return fail(errorMessage, "local compatibility files are not verified");
    }
    std::string playerName = state.view.text(RuntimeLanMenuWidget::playerNameEdit);
    if (!state.view.normalizeName(playerName, 64U, errorMessage)) return false;

    std::uint16_t hostPort = runtimeLanDefaultSessionPort;
    if (state.network == RuntimeMultiplayerMenuNetwork::Internet) {
        RuntimeMultiplayerDirectEndpoint endpoint;
        if (!state.view.parseEndpoint(
                "host:" + state.view.text(RuntimeLanMenuWidget::hostPortEdit),
                endpoint,
                errorMessage
            )) {
            return false;
        }
        hostPort = endpoint.port;
    }

    RuntimeMultiplayerResumeSave resumeSave;
    RuntimeMultiplayerHostSelectionProjection projection;
    RuntimeSkirmishSelection selection;
    if (fromSave) {
        if (!state.view.selectResume(state.baseConfig, state.compatibility, playerName, state.peerId,
            resumeSave, projection, errorMessage)) return false;
    } else if (!state.view.runHostSetup(state.baseConfig, selection, errorMessage)) {
        return false;
    }

    std::uint64_t randomSeed = 0U;
    std::uint64_t roomGeneration = 0U;
    std::string roomId;
    if (!state.view.secureU64(randomSeed, errorMessage)
        || !state.view.secureU64(roomGeneration, errorMessage)
        || !state.view.secureId(
            "room-",
            roomId,
            errorMessage
        )) {
        return false;
    }

    if (!fromSave && !state.view.projectHost(
            state.baseConfig,
            selection,
            playerName,
            state.peerId,
            randomSeed,
            projection,
            errorMessage
        )) {
        return false;
    }

    RuntimeMultiplayerLanHostOptions network;
    network.session.roomId = std::move(roomId);
    network.session.hostPeerId = state.peerId;
    network.session.hostDisplayName = playerName;
    network.session.hostParticipation = projection.lobby.hostMember.participation;
    network.session.compatibility = state.compatibility;
    network.roomName = playerName + (fromSave ? " · 恢复存档" : " Room");
    network.discoveryPort =
        state.network == RuntimeMultiplayerMenuNetwork::Internet
        ? hostPort
        : runtimeLanDefaultDiscoveryPort;
    network.sessionPort = hostPort;
    network.roomGeneration = roomGeneration;
    network.password = state.view.text(RuntimeLanMenuWidget::passwordEdit);
    network.passwordProtected = !network.password.empty();
    if (!state.flow.startHost(
            std::move(network),
            projection.lobby,
            errorMessage,
            fromSave ? &resumeSave : nullptr
        )) {
        return false;
    }
    for (std::size_t index = 0U; !fromSave && index < projection.slots.size(); ++index) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit = state.flow.setSlot(
            index,
            projection.slots[index],
            publishError
        );
        if (!edit.accepted || !publishError.empty()) {
            state.flow.close();
            return fail(
                errorMessage,
                !publishError.empty() ? publishError : edit.reason
            );
        }
    }

    state.hosting = true;
    state.view.clearPassword();
    state.localMapPath = projection.localMapPath;
    showPage(state, MultiplayerMenuPage::Lobby);
    std::string verificationError;
    if (ensureLocalMapVerified(state, verificationError)) {
        setStatus(
            state,
            state.network == RuntimeMultiplayerMenuNetwork::Internet
                ? "Direct room ready. Forward the displayed port for both UDP and TCP."
                : "LAN room ready. Waiting for players."
        );
    } else {
        setStatus(state, verificationError);
    }
    updateRoster(state);
    if (fromSave) setStatus(state, "恢复房间已建立。等待原有席位到齐并接收存档；选中玩家后可交换席位。");
    return true;
}

void joinSelectedLanRoom(RuntimeLanMenuController& state) {
    const std::int64_t selection = state.view.listSelected(RuntimeLanMenuWidget::roomList);
    if (selection == -1
        || static_cast<std::size_t>(selection) >= state.rooms.size()) {
        setStatus(state, "Select a discovered LAN room first.");
        return;
    }
    const bool spectator = state.view.checked(RuntimeLanMenuWidget::spectatorCheck);
    std::string errorMessage;
    if (!enterLobbyWithRoom(
            state,
            state.rooms[static_cast<std::size_t>(selection)],
            state.view.text(RuntimeLanMenuWidget::playerNameEdit),
            spectator,
            state.view.text(RuntimeLanMenuWidget::passwordEdit),
            errorMessage
        )) {
        setStatus(state, errorMessage);
    }
}

void applyLocalSlotOptions(RuntimeLanMenuController& state) {
    if (!state.flow.bridgePresent()
        || !state.flow.lobby().initialized()) {
        setStatus(state, "Canonical lobby setup is not available yet.");
        return;
    }
    const RuntimeMultiplayerMatchSlot* localSlot = humanSlotForPeer(
        state,
        state.flow.localPeerId()
    );
    if (!localSlot) {
        setStatus(state, "Spectators do not own a player setup slot.");
        return;
    }
    std::string country = state.view.trim(state.view.text(RuntimeLanMenuWidget::countryEdit));
    std::string color = state.view.trim(state.view.text(RuntimeLanMenuWidget::colorEdit));
    std::string team = state.view.trim(state.view.text(RuntimeLanMenuWidget::teamEdit));
    std::string difficulty = state.view.trim(
        state.view.text(RuntimeLanMenuWidget::difficultyEdit)
    );
    const std::string startText = state.view.trim(
        state.view.text(RuntimeLanMenuWidget::startPositionEdit)
    );
    if (startText.size() != 1U || startText.front() < '0'
        || startText.front() > '8') {
        setStatus(state, "Start position must be one digit from 0 to 8.");
        return;
    }
    const auto startPosition = static_cast<std::uint8_t>(
        startText.front() - '0'
    );
    if (state.hosting) {
        std::string publishError;
        const RuntimeMultiplayerLobbyEditResult edit =
            state.flow.setSlotOptions(
                localSlot->slotIndex,
                std::move(country),
                std::move(color),
                std::move(team),
                std::move(difficulty),
                startPosition,
                publishError
            );
        if (!edit.accepted || !publishError.empty()) {
            setStatus(
                state,
                !publishError.empty() ? publishError : edit.reason
            );
            return;
        }
        setStatus(state, "Player setup updated for the whole lobby.");
    } else {
        const RuntimeMultiplayerLanOperationResult result =
            state.flow.requestLocalSlotOptions(
                std::move(country),
                std::move(color),
                std::move(team),
                std::move(difficulty),
                startPosition
            );
        if (!result.succeeded) {
            setStatus(state, result.reason);
            return;
        }
        setStatus(state, "Player setup request sent to the host...");
    }
    updateRoster(state);
}

void kickSelectedPeer(RuntimeLanMenuController& state) {
    if (!state.hosting || state.selectedRosterPeerId.empty()) {
        setStatus(state, "Select a remote player or spectator first.");
        return;
    }
    const RuntimeMultiplayerLanOperationResult result = state.flow.kickPeer(
        state.selectedRosterPeerId,
        "removed by the host"
    );
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    state.selectedRosterPeerId.clear();
    setStatus(state, "Selected lobby member was removed.");
    updateRoster(state);
}

void pollMenu(RuntimeLanMenuController& state) {
    if (!state.flow.initialized()) return;
    const RuntimeMultiplayerLobbyFlowPollResult polled = state.flow.poll();
    if (!polled.succeeded) {
        state.directDiscoveryPending = false;
        setStatus(state, polled.error);
        return;
    }
    for (const RuntimeMultiplayerLanEvent& event : polled.events) {
        if (event.kind == RuntimeMultiplayerLanEventKind::Kicked) {
            const std::string message = event.reason.empty()
                ? "You were removed from the lobby by the host." : event.reason;
            state.view.showKickedMessage(message);
            closeMenu(state);
            return;
        }
        if (event.kind
                == RuntimeMultiplayerLanEventKind::LobbySlotOptionsResult
            && event.lobbySlotOptionsResult.has_value()) {
            const auto& response = *event.lobbySlotOptionsResult;
            setStatus(
                state,
                response.accepted
                    ? "Host accepted the player setup."
                    : (response.reason.empty()
                        ? "Host rejected the player setup."
                        : response.reason)
            );
        }
    }
    if (state.page == MultiplayerMenuPage::Browser) {
        if (state.network == RuntimeMultiplayerMenuNetwork::Lan) {
            updateRoomList(state);
        } else {
            state.view.pollDirectDiscovery();
        }
        return;
    }

    std::string verificationError;
    if (!state.flow.localMapVerified()
        && state.flow.canonicalSetupPublished()) {
        if (ensureLocalMapVerified(state, verificationError)) {
            setStatus(state, "Local map verified. Ready is now available.");
        } else {
            setStatus(state, verificationError);
        }
    }
    updateRoster(state);
    if (polled.matchStarted) state.view.finishStartedMatch();
}

void toggleReady(RuntimeLanMenuController& state) {
    const RuntimeMultiplayerLanOperationResult result = state.flow.setReady(
        !localMemberReady(state)
    );
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    setStatus(
        state,
        result.queued ? "Ready change sent to host..." : "Ready state updated."
    );
    updateRoster(state);
}

void startMatch(RuntimeLanMenuController& state) {
    const RuntimeMultiplayerLanOperationResult result = state.flow.startMatch();
    if (!result.succeeded) {
        setStatus(state, result.reason);
        return;
    }
    setStatus(state, "Starting the verified room...");
}

void leaveLobby(RuntimeLanMenuController& state) {
    if (state.flow.bridgePresent()
        && state.flow.lanRole()
            == RuntimeMultiplayerLanRole::Client
        && state.flow.lanPhase()
            == RuntimeMultiplayerLanPhase::Lobby) {
        state.flow.leave();
    }
    closeMenu(state);
}
void rosterSelectionChanged(RuntimeLanMenuController& state) {
            const std::int64_t selected = state.view.listSelected(RuntimeLanMenuWidget::rosterList);
            state.selectedRosterPeerId =
                selected != -1
                    && static_cast<std::size_t>(selected)
                        < state.rosterPeerIds.size()
                ? state.rosterPeerIds[static_cast<std::size_t>(selected)]
                : std::string{};
            updateRoster(state);
}

bool handleControlClick(RuntimeLanMenuController& state, int control) {
        switch (control) {
        case HostButton: {
            std::string errorMessage;
            if (!startHosting(state, errorMessage)) {
                setStatus(state, errorMessage);
            }
            return true;
        }
        case RefreshButton: {
            std::string errorMessage;
            if (!beginLanBrowsing(state, errorMessage)) {
                setStatus(state, errorMessage);
            } else {
                setStatus(state, "LAN discovery probe sent.");
            }
            return true;
        }
        case JoinButton:
            if (state.network == RuntimeMultiplayerMenuNetwork::Lan) {
                joinSelectedLanRoom(state);
            } else {
                state.view.beginDirectDiscovery();
            }
            return true;
        case ReadyButton:
            toggleReady(state);
            return true;
        case ResumeHostButton: {
            std::string error;
            if (!startHosting(state, error, true)) setStatus(state, error);
            return true;
        }
        case ResumeSwapButton: {
            const auto* slot = humanSlotForPeer(state, state.selectedRosterPeerId);
            std::string error;
            if (!slot) { setStatus(state, "请先选中一位占有席位的玩家。");return true; }
            const auto edit = state.flow.setSlotController(slot->slotIndex, RuntimeMultiplayerMatchSlotController::Human, state.peerId, error);
            setStatus(state, edit.accepted && error.empty() ? "席位已交换，请重新准备。" : error);
            updateRoster(state);return true;
        }
        case StartButton:
            startMatch(state);
            return true;
        case ApplySlotButton:
            applyLocalSlotOptions(state);
            return true;
        case KickButton:
            kickSelectedPeer(state);
            return true;
        case LeaveButton:
        case BackButton:
            leaveLobby(state);
            return true;
        default:
            break;
        }
    return false;
}

bool initializeLanMenuData(RuntimeLanMenuController& state, std::string& initializationError) {
    state.compatibilityReady = state.view.makeCompatibility(
        state.baseConfig,
        state.compatibility,
        initializationError
    );
    if (!state.view.secureId(
            "peer-",
            state.peerId,
            state.view.resultError()
        )) {
        return false;
    }
    return true;
}

void initializeLanBrowser(RuntimeLanMenuController& state, const std::string& initializationError) {
    if (state.compatibilityReady) {
        if (state.network == RuntimeMultiplayerMenuNetwork::Lan) {
            std::string browseError;
            if (!beginLanBrowsing(state, browseError)) {
                setStatus(state, browseError);
            } else {
                setStatus(state, "LAN browser ready. Refresh to discover rooms.");
            }
        } else {
            setStatus(
                state,
                "Enter a forwarded address:port for a direct UDP probe and TCP connection."
            );
        }
    } else {
        setStatus(state, initializationError);
    }
}

} // namespace concord::lan_menu
