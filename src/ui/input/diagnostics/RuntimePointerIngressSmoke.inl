// W010-03: bounded native oracle from the frozen original input policies.
// The old route never calls the new ingress/sidebar controllers for expected policy.
namespace RuntimePointerIngressSmokeOld {
int frozenBuildTabIndex(RuntimeBuildKind kind) {
    switch (kind) {
    case RuntimeBuildKind::Building:
        return 0;
    case RuntimeBuildKind::Defense:
        return 1;
    case RuntimeBuildKind::Infantry:
        return 2;
    case RuntimeBuildKind::Unit:
        return 3;
    }
    return 0;
}

int frozenActiveBuildPage(const RuntimeWindowState& state) {
    const int tab = frozenBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildPageByTab.size())) {
        return 0;
    }
    return std::max(0, state.buildPageByTab[tab]);
}

void frozenSetActiveBuildPage(RuntimeWindowState& state, int page) {
    const int tab = frozenBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildPageByTab.size())) {
        return;
    }
    state.buildPageByTab[tab] = std::max(0, page);
}

int frozenBuildPageCount(
    const RuntimeWindowState& state,
    const RECT& sidebar
) {
    const std::vector<int> filtered = runtimeFilteredBuildCatalogIndices(state);
    const int superWeaponCount = state.activeBuildTab == RuntimeBuildKind::Defense
        ? static_cast<int>(runtimeVisibleSuperWeapons(state).size())
        : 0;
    const int perPage = runtimeBuildButtonsPerPage(sidebar);
    return std::max(
        1,
        (static_cast<int>(filtered.size()) + superWeaponCount + perPage - 1)
            / perPage
    );
}

void frozenClampActiveBuildPage(
    RuntimeWindowState& state,
    const RECT& sidebar
) {
    frozenSetActiveBuildPage(
        state,
        std::min(
            frozenActiveBuildPage(state),
            frozenBuildPageCount(state, sidebar) - 1
        )
    );
}

bool frozenScrollSidebarWheel(RuntimeWindowState& state, const RECT& sidebar, int delta) {
    const int tab = frozenBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildWheelRemainderByTab.size())) return false;
    const int before = frozenActiveBuildPage(state);
    frozenClampActiveBuildPage(state, sidebar);
    int& remainder = state.buildWheelRemainderByTab[tab];
    const int pages = frozenBuildPageCount(state, sidebar);
    if (pages <= 1) {
        remainder = 0;
        return before != frozenActiveBuildPage(state);
    }
    const int accumulated = remainder + delta;
    const int steps = accumulated / WHEEL_DELTA;
    remainder = accumulated % WHEEL_DELTA;
    const int previous = frozenActiveBuildPage(state);
    const int next = std::clamp(previous - steps, 0, pages - 1);
    if (steps != 0 && next == previous) remainder = 0;
    frozenSetActiveBuildPage(state, next);
    if (next == before) return false;
    state.interactionStatus = "build page " + std::to_string(next + 1) + "/" + std::to_string(pages);
    return true;
}

constexpr std::array<int, 7> frozenDiplomacyChatControls{{1750,1751,1752,1753,1762,1763,1764}};
std::vector<RuntimeDiplomacyRow> frozenDiplomacyRows(const RuntimeWindowState& state) {
    std::vector<RuntimeDiplomacyRow> rows;
    std::set<std::string> added;
    const auto self = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto add = [&](const std::string& owner, const std::wstring& name,
                         std::optional<std::size_t> slot) {
        const auto key = runtimeCanonicalHumanHouseOwner(owner);
        if (key.empty() || runtimeOwnerIsNeutralForAutoTarget(key) || runtimeObserverOwner(state,key)
            || !added.insert(key).second) return;
        RuntimeDiplomacyRow row;
        row.owner=key; row.name=name.empty()?widenUtf8(owner):name;
        row.country=runtimeCountryForOwner(state,key); row.self=key==self;
        row.slot=slot;
        row.human=slot ? state.config.playerSlots[*slot].human
            : std::any_of(state.humanHouseAuthorities.begin(),state.humanHouseAuthorities.end(),
                [&](const auto& authority){return authority.canonicalOwner==key;});
        row.defeated=state.multiplayerDefeatedOwners.contains(key)
            || !std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),
                [&](const auto& object) {
                    return runtimeCanonicalHumanHouseOwner(object.owner)==key
                        && runtimeSkirmishObjectSurvives(state,object)
                        && (!state.config.shortGame || runtimeSkirmishObjectCountsForShortGame(state,object));
                });
        const auto score=state.matchScores.find(key);
        if (score!=state.matchScores.end()) row.kills=static_cast<long long>(score->second.unitsDestroyed)
            + score->second.structuresDestroyed;
        rows.push_back(std::move(row));
    };
    for (std::size_t i=0;i<state.config.playerSlots.size();++i) {
        const auto& slot=state.config.playerSlots[i];
        if (slot.enabled) add(runtimeSkirmishOwnerForSlot(i),widenUtf8(slot.name),i);
    }
    if (rows.empty()) {
        if (!state.multiplayerLocalSpectator) add(self,{},{});
        for (const auto& owner:runtimePlayerDiplomacyOwners(state)) add(owner,{},{});
    }
    const auto own=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.self;});
    if (own!=rows.end()) std::rotate(rows.begin(),own,own+1);
    return rows;
}

void frozenCloseDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=false; ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    state.rightPointer.active=state.rightPointer.moved=false;
    state.repaintRequested=true;
}
void frozenOpenDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=true; ui.diplomacyRow=1;
    ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    ui.chatOpen=ui.beaconEditing=false; ui.chatDraft.clear();
    state.leftPointer.active=state.leftPointer.moved=false;
    state.rightPointer.active=state.rightPointer.moved=false;
    state.repaintRequested=true;
}

bool frozenDiplomacyAllianceEnabled(const RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind=RuntimePlayerCommunicationKind::Alliance; command.text=row.owner;
    const auto source=runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto offer=state.playerCommands.alliances.find({source,row.owner});
    const bool offered=offer==state.playerCommands.alliances.end()
        ? runtimeOwnersAreAllied(state,source,row.owner):offer->second;
    command.value=offered?0:1;
    std::string error;
    return !row.self && !row.defeated && validateRuntimePlayerCommunication(state,source,command,error);
}
bool frozenDiplomacyChatEnabled(const RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    return !row.self && row.human && row.slot && *row.slot<8 && runtimePlayerChatRecipientsAvailable(state);
}
void frozenDiplomacyToggleChat(RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    if (!frozenDiplomacyChatEnabled(state,row)) return;
    auto& ui=state.playerCommunication;
    if (!ui.chatRecipientsExplicit) ui.chatRecipients=runtimePlayerChatSlotsMask(state);
    ui.chatRecipientsExplicit=true;
    ui.chatRecipients^=static_cast<std::uint8_t>(1U<<*row.slot);
    ui.chatRecipients|=static_cast<std::uint8_t>(1U<<*runtimePlayerChatSlot(state,state.economy.owner));
    state.repaintRequested=true;
}

using ::runtimePlayerToggleAlliance;
int frozenDiplomacyHitControl(const RuntimeWindowState& state, const RECT& client, POINT point) {
    if (pointInRectInclusive(runtimeDiplomacyControlRect(client,1059),point)) return 1059;
    const auto rows=frozenDiplomacyRows(state);
    for (std::size_t i=1;i<rows.size() && i<8;++i) {
        if (frozenDiplomacyAllianceEnabled(state,rows[i])
            && pointInRectInclusive(runtimeDiplomacyCheckboxRect(client,1742+static_cast<int>(i)),point))
            return 1742+static_cast<int>(i);
        if (frozenDiplomacyChatEnabled(state,rows[i])
            && pointInRectInclusive(runtimeDiplomacyCheckboxRect(client,frozenDiplomacyChatControls[i-1]),point))
            return frozenDiplomacyChatControls[i-1];
    }
    if (pointInRectInclusive(runtimeDiplomacyControlRect(client,1840),point)) return 1840;
    return 0;
}
void frozenHandleDiplomacyMouse(RuntimeWindowState& state,const RECT& client,UINT message,POINT point) {
    auto& ui=state.playerCommunication;
    const int hit=frozenDiplomacyHitControl(state,client,point);
    ui.diplomacyHoveredControl=hit;
    if (message==WM_LBUTTONDOWN) ui.diplomacyPressedControl=hit;
    else if (message==WM_LBUTTONUP) {
        const bool activate=hit!=0 && hit==ui.diplomacyPressedControl;
        ui.diplomacyPressedControl=0;
        if (activate) {
            if (hit==1059) frozenCloseDiplomacyPage(state);
            else if (hit==1840) ui.tauntsEnabled=!ui.tauntsEnabled;
            else {
                const auto rows=frozenDiplomacyRows(state);
                if (hit>=1743 && hit<=1749) {
                    const auto row=static_cast<std::size_t>(hit-1742);
                    if (row<rows.size()) {ui.diplomacyRow=static_cast<int>(row);runtimePlayerToggleAlliance(state,rows[row].owner);}
                } else {
                    const auto found=std::find(frozenDiplomacyChatControls.begin(),frozenDiplomacyChatControls.end(),hit);
                    const auto row=static_cast<std::size_t>(found-frozenDiplomacyChatControls.begin()+1);
                    if (row<rows.size()) {ui.diplomacyRow=static_cast<int>(row);frozenDiplomacyToggleChat(state,rows[row]);}
                }
            }
        }
    } else if (message==WM_RBUTTONUP) frozenCloseDiplomacyPage(state);
    state.repaintRequested=true;
}

LRESULT frozenWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    RuntimeWindowState* campaignState = reinterpret_cast<RuntimeWindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    // A synchronous window callback can enter during a local simulation
    // batch. Close its shared terrain signature before any handler mutates
    // state, including load/input paths that are ordinarily outside the batch.
    if (campaignState != nullptr)
        runtimeInvalidateVisionTerrainBatchForMessage(campaignState->world);

    if (campaignState != nullptr) {
        RuntimeForceAttackHeldUiHost forceHost(*campaignState, window);
        handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message, wParam), forceHost);
    }
    if (campaignState != nullptr) {
        RuntimeMapRightPointerGestureUiHost rightHost(*campaignState, window);
        const auto event = message == WM_KILLFOCUS ? RuntimeMapRightPointerCancelEvent::FocusLost
            : message == WM_CANCELMODE ? RuntimeMapRightPointerCancelEvent::CancelMode
            : message == WM_ACTIVATEAPP && wParam == FALSE
                ? RuntimeMapRightPointerCancelEvent::ApplicationDeactivated
                : RuntimeMapRightPointerCancelEvent::Other;
        cancelRuntimeMapRightPointer(event, rightHost);
    }
    if (campaignState != nullptr) {
        RuntimeCampaignInputAdmissionUiHost admissionHost(*campaignState);
        if (rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message), admissionHost))
            return 0;
    }
    if (campaignState != nullptr && campaignState->skirmishMatchFinished) {
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        if (!campaignState->campaignMode && !campaignState->resultPresentation.bannerVisible
            && (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
            RECT client;
            GetClientRect(window, &client);
            const RECT button = runtimeMatchScoreContinueRect(client);
            const POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            const bool inside = PtInRect(&button, point) != FALSE;
            auto& presentation = campaignState->resultPresentation;
            if (message == WM_LBUTTONUP) {
                const bool activate = presentation.continuePressed && inside;
                presentation.continuePressed = false;
                campaignState->repaintRequested = true;
                if (activate) SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
            } else {
                if (message == WM_LBUTTONDOWN) presentation.continuePressed = inside;
                presentation.continueHovered = inside;
                campaignState->repaintRequested = true;
            }
            return 0;
        }
        switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 0;
        default:
            break;
        }
    }
    if (campaignState && !campaignState->runtimeMenuOpen && !campaignState->skirmishMatchFinished
        && (campaignState->playerCommunication.chatOpen || campaignState->playerCommunication.diplomacyOpen)) {
        if (campaignState->playerCommunication.diplomacyOpen) {
            if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || message == WM_KILLFOCUS
                || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
                campaignState->playerCommunication.diplomacyPressedControl = 0;
                if (message != WM_CAPTURECHANGED && GetCapture() == window) ReleaseCapture();
                return 0;
            }
            switch (message) {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
                RECT client{}; GetClientRect(window, &client);
                frozenHandleDiplomacyMouse(*campaignState, client, message, pointFromMouseMessage(lParam));
                if (message == WM_LBUTTONDOWN) SetCapture(window);
                if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
                    if (GetCapture() == window) ReleaseCapture();
                }
                campaignState->leftPointer.active = campaignState->leftPointer.moved = false;
                InvalidateRect(window,nullptr,FALSE); return 0;
            }
            default: break;
            }
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK
            || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MOUSEWHEEL) {
            auto& ui = campaignState->playerCommunication;
            if (message == WM_RBUTTONUP) {
                ui.chatOpen = ui.diplomacyOpen = ui.beaconEditing = false; ui.chatDraft.clear();
            }
            campaignState->leftPointer.active = campaignState->leftPointer.moved = false;
            campaignState->repaintRequested = true; InvalidateRect(window,nullptr,FALSE); return 0;
        }
    }
    switch (message) {
    case WM_LBUTTONDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerDown({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEMOVE:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerMove({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_LBUTTONUP:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerUp({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CAPTURECHANGED:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            RuntimeMapRightPointerGestureUiHost rightHost(*state, window);
            captureChangedRuntimeMapRightPointer(rightHost);
        }
        if (state != nullptr
            && (state->runtimeMenuSliderDrag != RuntimeMenuAction::None
                || state->runtimeMenuPressed != RuntimeMenuAction::None)) {
            const RuntimeMenuAction interruptedSlider =
                state->runtimeMenuSliderDrag;
            state->runtimeMenuSliderDrag = RuntimeMenuAction::None;
            state->runtimeMenuPressed = RuntimeMenuAction::None;
            if (interruptedSlider != RuntimeMenuAction::None) {
                applyRuntimeMenuSliderRuntimeEffects(
                    *state,
                    interruptedSlider
                );
                (void)persistRuntimeMenuPreferences(*state);
            }
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONDOWN:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerDown(input, host) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONUP:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        RuntimeMapRightPointerClickUiHost clickHost(*state);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerUp(input, host, clickHost) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CHAR:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state) {
            RuntimeChatTextUiHost host(*state, window);
            if (handleRuntimeChatTextCharacter({static_cast<std::uint64_t>(wParam)},
                state->playerCommunication, host)) return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        RuntimeKeyboardIngressUiHost host(*state, window, lParam);
        const RuntimeKeyboardIngressInput input =
            runtimeKeyboardIngressInputForMessage(message, wParam, lParam);
        if (::handleRuntimeKeyboardIngressKey(input, host)) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEWHEEL:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            if (state->runtimeMenuOpen) {
                return 0;
            }
            const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT clientRect{};
            GetClientRect(window, &clientRect);
            if (!frozenScrollSidebarWheel(*state, runtimeSidebarRect(clientRect), wheelDelta)) return 0;
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace RuntimePointerIngressSmokeOld

namespace RuntimePointerIngressSmokeDetail {
LRESULT directWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    RuntimeWindowState* campaignState = reinterpret_cast<RuntimeWindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    // A synchronous window callback can enter during a local simulation
    // batch. Close its shared terrain signature before any handler mutates
    // state, including load/input paths that are ordinarily outside the batch.
    if (campaignState != nullptr)
        runtimeInvalidateVisionTerrainBatchForMessage(campaignState->world);

    if (campaignState != nullptr) {
        RuntimeForceAttackHeldUiHost forceHost(*campaignState, window);
        handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message, wParam), forceHost);
    }
    if (campaignState != nullptr) {
        RuntimeMapRightPointerGestureUiHost rightHost(*campaignState, window);
        const auto event = message == WM_KILLFOCUS ? RuntimeMapRightPointerCancelEvent::FocusLost
            : message == WM_CANCELMODE ? RuntimeMapRightPointerCancelEvent::CancelMode
            : message == WM_ACTIVATEAPP && wParam == FALSE
                ? RuntimeMapRightPointerCancelEvent::ApplicationDeactivated
                : RuntimeMapRightPointerCancelEvent::Other;
        cancelRuntimeMapRightPointer(event, rightHost);
    }
    if (campaignState != nullptr) {
        RuntimeCampaignInputAdmissionUiHost admissionHost(*campaignState);
        if (rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message), admissionHost))
            return 0;
    }
    if (campaignState != nullptr) {
        RuntimePointerIngressUiHost host(*campaignState, window);
        if (handleRuntimePointerIngress(runtimePointerIngressInputForMessage(message, wParam, lParam), host))
            return 0;
    }
    switch (message) {
    case WM_LBUTTONDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerDown({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEMOVE:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerMove({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_LBUTTONUP:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        const POINT point = pointFromMouseMessage(lParam);
        RuntimeMapLeftPointerUiHost host(*state, window);
        return handleRuntimeMapLeftPointerUp({{point.x, point.y},
            static_cast<std::uint64_t>(wParam)}, host)
            ? 0 : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CAPTURECHANGED:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            RuntimeMapRightPointerGestureUiHost rightHost(*state, window);
            captureChangedRuntimeMapRightPointer(rightHost);
        }
        if (state != nullptr
            && (state->runtimeMenuSliderDrag != RuntimeMenuAction::None
                || state->runtimeMenuPressed != RuntimeMenuAction::None)) {
            const RuntimeMenuAction interruptedSlider =
                state->runtimeMenuSliderDrag;
            state->runtimeMenuSliderDrag = RuntimeMenuAction::None;
            state->runtimeMenuPressed = RuntimeMenuAction::None;
            if (interruptedSlider != RuntimeMenuAction::None) {
                applyRuntimeMenuSliderRuntimeEffects(
                    *state,
                    interruptedSlider
                );
                (void)persistRuntimeMenuPreferences(*state);
            }
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONDOWN:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerDown(input, host) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_RBUTTONUP:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
        RuntimeMapRightPointerGestureUiHost host(*state, window);
        RuntimeMapRightPointerClickUiHost clickHost(*state);
        const RuntimeMapRightPointerInput input{runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam))};
        return handleRuntimeMapRightPointerUp(input, host, clickHost) ? LRESULT{0}
            : DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_CHAR:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state) {
            RuntimeChatTextUiHost host(*state, window);
            if (handleRuntimeChatTextCharacter({static_cast<std::uint64_t>(wParam)},
                state->playerCommunication, host)) return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        RuntimeKeyboardIngressUiHost host(*state, window, lParam);
        const RuntimeKeyboardIngressInput input =
            runtimeKeyboardIngressInputForMessage(message, wParam, lParam);
        if (::handleRuntimeKeyboardIngressKey(input, host)) return 0;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEWHEEL:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            RuntimeSidebarPageUiHost host(*state, window, wParam);
            handleRuntimeSidebarWheel(host);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}


inline constexpr const wchar_t* className = L"ConcordPointerIngressSmoke";
struct Window;
thread_local std::map<HWND, Window*> windows;
LRESULT CALLBACK nativeProc(HWND, UINT, WPARAM, LPARAM);
struct RegisteredClass {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    bool ready = false;
    RegisteredClass() { WNDCLASSW c{}; c.lpfnWndProc = nativeProc; c.hInstance = instance;
        c.lpszClassName = className; ready = RegisterClassW(&c) != 0; }
    ~RegisteredClass() { if (ready) UnregisterClassW(className, instance); }
};
struct Event {
    std::size_t sequence = 0; int depth = 0; UINT message = 0; bool entering = false;
    std::uintptr_t hwnd = 0, capture = 0, rawLParam = 0; WPARAM rawWParam = 0;
    int captureKind = 0, destinationKind = 0; std::string state;
    std::string semantic() const { std::ostringstream s;
        s << depth << ',' << message << ',' << entering << ',' << captureKind << ',' << destinationKind
          << ',' << rawWParam << ',' << (message == WM_CAPTURECHANGED ? 0 : rawLParam) << ',' << state;
        return s.str(); }
};
bool routedMessage(UINT m) {
    return m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK
        || m == WM_RBUTTONDOWN || m == WM_RBUTTONUP || m == WM_RBUTTONDBLCLK
        || m == WM_MBUTTONDOWN || m == WM_MBUTTONUP || m == WM_MBUTTONDBLCLK
        || m == WM_XBUTTONDOWN || m == WM_XBUTTONUP || m == WM_XBUTTONDBLCLK
        || m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL || m == WM_CAPTURECHANGED
        || m == WM_CANCELMODE || m == WM_KILLFOCUS || m == WM_ACTIVATEAPP
        || m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_CHAR || m == WM_APP + 71;
}
std::string state(const RuntimeWindowState&, bool, const std::string&);
struct Window {
    RuntimeWindowState& state; int path; HWND handle = nullptr;
    bool recording = false, nesting = false; int mutation = 0, depth = 0;
    std::size_t sequence = 0; std::vector<Event> events;
    Window(RuntimeWindowState& s, int route) : state(s), path(route) {
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className,
            L"Pointer ingress private contract", WS_POPUP, -32000, -32000, 1024, 768,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (handle) { windows.emplace(handle, this);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state)); }
    }
    Window(const Window&) = delete; Window& operator=(const Window&) = delete;
    int kind(HWND h) const { return h == nullptr ? 0 : h == handle ? 1 : 2; }
    void log(UINT m, WPARAM w, LPARAM l, bool enter) {
        Event e; e.sequence = sequence++; e.depth = depth; e.message = m; e.entering = enter;
        e.hwnd = reinterpret_cast<std::uintptr_t>(handle);
        e.capture = reinterpret_cast<std::uintptr_t>(GetCapture());
        e.captureKind = kind(reinterpret_cast<HWND>(e.capture));
        e.destinationKind = m == WM_CAPTURECHANGED ? kind(reinterpret_cast<HWND>(l)) : 0;
        e.rawWParam = w; e.rawLParam = static_cast<std::uintptr_t>(l);
        e.state = RuntimePointerIngressSmokeDetail::state(state, true, state.config.configPath);
        events.push_back(std::move(e));
    }
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l) {
        if (!recording) return runtimeWindowProc(handle, m, w, l);
        ++depth; log(m, w, l, true);
        if (m == WM_CAPTURECHANGED && mutation && !nesting) {
            const int selected = mutation; mutation = 0; nesting = true;
            if (selected == 1) state.skirmishMatchFinished = false;
            if (selected == 2) state.campaignMode = true;
            if (selected == 3) state.resultPresentation.bannerVisible = true;
            if (selected == 4) state.resultPresentation.continuePressed = false;
            if (selected == 5) {
                state.playerCommunication.diplomacyPressedControl = 0;
                SendMessageW(handle, WM_CANCELMODE, 0, 0);
            }
            nesting = false;
        }
        LRESULT result;
        if (!routedMessage(m)) result = runtimeWindowProc(handle, m, w, l);
        else if (path == 0) result = RuntimePointerIngressSmokeOld::frozenWindowProc(handle, m, w, l);
        else if (path == 1) result = directWindowProc(handle, m, w, l);
        else result = runtimeWindowProc(handle, m, w, l);
        log(m, w, l, false); --depth; return result;
    }
    void close() {
        if (!handle) return;
        recording = false;
        if (GetCapture() == handle) ReleaseCapture();
        if (IsWindow(handle)) {
            SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
            DestroyWindow(handle);
        }
        windows.erase(handle); handle = nullptr;
    }
    ~Window() { close(); }
};
LRESULT CALLBACK nativeProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const auto found = windows.find(h);
    return found == windows.end() ? DefWindowProcW(h, m, w, l) : found->second->dispatch(m, w, l);
}
std::string hex(const std::string& text) {
    std::ostringstream s; for (const auto c : text)
        s << std::hex << std::setfill('0') << std::setw(2) << unsigned(static_cast<unsigned char>(c));
    return s.str();
}
struct Clock final : RuntimeUiClock {
    mutable std::size_t reads = 0; RuntimeUiTimeMs value = 123456;
    RuntimeUiTimeMs nowMs() const noexcept override { ++reads; return value; }
};
struct ClockBinding {
    RuntimeWindowState& state; const RuntimeUiClock* previous;
    ClockBinding(RuntimeWindowState& s, const RuntimeUiClock& c) : state(s), previous(s.uiClock) { state.uiClock = &c; }
    ~ClockBinding() { state.uiClock = previous; }
};
struct Command {
    int tick = 0; std::vector<std::uint8_t> payload; bool accepted = false, applied = false; std::string error;
    bool operator==(const Command&) const = default;
};
struct Witness {
    RuntimeWindowState& state; int mode; std::vector<Command> commands;
    Witness(RuntimeWindowState& s, int route) : state(s), mode(route) { attach(); }
    void attach() {
        state.multiplayerBridge = nullptr; state.offlineGameplayIntentRouting = false;
        state.multiplayerIntentRouter.setCaptureCallback({});
        if (mode == 0) return;
        initializeRuntimeOfflineGameplayRouting(state);
        state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& c, std::string& error) {
            Command row; row.tick = state.tick;
            if (!encodeRuntimeMultiplayerGameplayCommand(c, row.payload, error)) return false;
            if (mode == 3) { error = "pointer ingress capture rejected"; row.accepted = false; }
            else if (mode == 2) { error.clear(); row.accepted = true; }
            else row.accepted = row.applied = dispatchRuntimeOfflineGameplayCommand(state, c, error);
            row.error = error; commands.push_back(row); return row.accepted;
        });
    }
    bool replay(std::string& error) {
        if (mode != 2) return true;
        for (const auto& c : commands) {
            RuntimeMultiplayerGameplayCommand decoded;
            if (!decodeRuntimeMultiplayerGameplayCommand(c.payload, decoded, error)
                || !dispatchRuntimeOfflineGameplayCommand(state, decoded, error)) return false;
        }
        return true;
    }
    ~Witness() { state.multiplayerIntentRouter.setCaptureCallback({}); }
};
std::string authority(const RuntimeWindowState& s) {
    std::ostringstream o; o << s.tick << ':' << s.world.deterministicSimulationStateHash()
        << ':' << hex(s.playerCommands.serialize()) << ':' << s.playerCommands.stateHash()
        << ':' << hex(runtimeSerializeProductionLanes(s.economy.localProductionLanes)) << ':' << s.economy.credits
        << ':' << s.economy.owner << ':' << s.superWeapons.stateHash()
        << ':' << s.lastCommand.valid << ',' << s.lastCommand.verb << ',' << s.lastCommand.tick
        << ',' << s.lastCommand.selectedCount << ',' << s.lastCommand.acceptedCount << ',' << s.lastCommand.rejectedCount
        << ',' << s.lastCommand.targetX << ',' << s.lastCommand.targetY << ',' << s.lastCommand.targetObjectId
        << ',' << s.lastCommand.targetObjectType << ',' << s.lastCommand.inputAckImmediate << ',' << s.lastCommand.feedbackLatencyTicks;
    return o.str();
}
std::string state(const RuntimeWindowState& s, bool normalized, const std::string& privatePath) {
    std::ostringstream o; const auto& u = s.playerCommunication; const auto& r = s.resultPresentation;
    o << "communication=" << u.chatOpen << ',' << u.diplomacyOpen << ',' << u.beaconEditing << ',' << u.beaconTargeting
      << ',' << u.diplomacyRow << ',' << u.diplomacyPressedControl << ',' << u.diplomacyHoveredControl
      << ',' << u.tauntsEnabled << ',' << u.chatRecipientsExplicit << ',' << unsigned(u.chatRecipients)
      << ',' << u.chatAllies << ";diplomacyBackground=" << hex(u.diplomacyBackgroundSource);
    for (const auto& source : u.diplomacyControlSources) o << ";diplomacyControlSource=" << hex(source);
    for (const auto& line : u.lines) o << ";chatLine=" << hex(line.owner) << ',' << hex(line.text) << ',' << line.allies << ',' << line.untilTick;
    for (const auto& beacon : u.beacons) o << ";beaconLine=" << hex(beacon.owner) << ',' << beacon.cell.x << ',' << beacon.cell.y
        << ',' << beacon.untilTick << ',' << hex(beacon.text) << ',' << beacon.level;
    const std::map<int, int> cheer(u.cheerUntilTick.begin(), u.cheerUntilTick.end());
    for (const auto& entry : cheer) o << ";cheer=" << entry.first << ',' << entry.second;
    for (const auto& entry : u.nextFeedbackTick) o << ";feedback=" << hex(entry.first.first)
        << ',' << static_cast<int>(entry.first.second) << ',' << entry.second;
    o << ";draft=";
    for (const auto ch : u.chatDraft) o << static_cast<std::uint32_t>(ch) << ',';
    if (u.selectedBeacon) o << ";beacon=" << u.selectedBeacon->x << ',' << u.selectedBeacon->y;
    o << ";finished=" << s.skirmishMatchFinished << ',' << s.skirmishVictory << ',' << s.skirmishResult
      << ";result=" << r.resigned << ',' << r.started << ',' << r.bannerVisible << ',' << r.startedAtMs
      << ',' << r.bannerDurationMs << ',' << r.scorePage << ',' << r.continueHovered << ',' << r.continuePressed << ',' << r.scoreMusicSelected
      << ";menu=" << s.runtimeMenuOpen << ',' << static_cast<int>(s.runtimeMenuPage)
      << ',' << static_cast<int>(s.runtimeMenuHover) << ',' << static_cast<int>(s.runtimeMenuPressed)
      << ',' << static_cast<int>(s.runtimeMenuSliderDrag) << ',' << s.runtimeMenuFocus << ',' << s.runtimeMenuKeyCaptureIndex
      << ";drag=" << s.leftPointer.active << ',' << s.leftPointer.moved
      << ',' << s.leftPointer.start.x << ',' << s.leftPointer.start.y << ',' << s.leftPointer.current.x << ',' << s.leftPointer.current.y
      << ',' << s.rightPointer.active << ',' << s.rightPointer.moved
      << ',' << s.rightPointer.start.x << ',' << s.rightPointer.start.y << ',' << s.rightPointer.last.x << ',' << s.rightPointer.last.y
      << ',' << s.rightPointer.cursorDelta.x << ',' << s.rightPointer.cursorDelta.y
      << ";force=" << s.forceAttackHeld << ";local=" << s.playerLocalCommands.planning << ',' << s.playerLocalCommands.cameraFollow
      << ',' << s.playerLocalCommands.followTargeting << ',' << s.playerLocalCommands.rallyTargeting
      << ";pages=" << static_cast<int>(s.activeBuildTab);
    for (int p : s.buildPageByTab) o << ',' << p;
    o << ";remainders="; for (int p : s.buildWheelRemainderByTab) o << p << ',';
    o << ";repaint=" << s.repaintRequested << ";exit=" << s.runtimeExitRequested
      << ";status=" << hex(s.interactionStatus) << ";selection=";
    for (int id : s.selectedObjectIds) o << id << ',';
    o << ";camera=" << s.camera.worldOffsetX << ',' << s.camera.worldOffsetY << ',' << s.camera.initialized
      << ',' << s.camera.tileWidth << ',' << s.camera.tileHeight
      << ";viewport=" << s.cameraClampViewport.left << ',' << s.cameraClampViewport.top << ',' << s.cameraClampViewport.right << ',' << s.cameraClampViewport.bottom
      << ";projection=" << s.projectedObjectsCacheValid << ',' << s.projectedObjectsCacheSignature
      << ";authority=" << authority(s) << ";preferences=";
    o << (normalized && s.config.configPath == privatePath ? "<private-preferences>" : s.config.configPath);
    o << ";resourceRoot=" << s.config.localDataRootPath << ";sounds=" << s.runtimeSoundEvents << ',' << s.lastRuntimeSound
      << ";campaign=" << s.campaignMode << ',' << s.exitAfterPendingCampaignVideos << ',' << s.campaignScore.presentationStartedAtMs;
    for (const auto& movie : s.pendingCampaignVideoPaths) o << ";video=" << movie;
    for (const auto& row : s.config.playerSlots)
        o << ";slot=" << row.enabled << ',' << row.local << ',' << row.human << ',' << row.name << ',' << row.country << ',' << row.team;
    return o.str();
}
struct Saved {
    std::vector<std::string> states, events;
    std::vector<LRESULT> results; std::vector<std::size_t> clockReads;
    std::vector<Command> commands;
    bool operator==(const Saved&) const = default;
};
} // namespace RuntimePointerIngressSmokeDetail


bool runRuntimePointerIngressSmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
    using namespace RuntimePointerIngressSmokeDetail;
    std::size_t checks = 0, failures = 0, triples = 0, steps = 0, scoreCases = 0,
        diplomacyCases = 0, chatCases = 0, wheelCases = 0, captureCases = 0,
        acceptedCommands = 0, rejectedCommands = 0, replayedCommands = 0, destroyedByReturn = 0, loadCases = 0;
    const auto need = [&](bool value, const std::string& label) {
        ++checks; if (!value) { ++failures; output << "Pointer-ingress failure: " << label << '\n'; }
        return value;
    };
    char* requested = nullptr; std::size_t requestedLength = 0;
    const int environmentResult = _dupenv_s(&requested, &requestedLength, "CONCORD_POINTER_INGRESS_OUTPUT");
    const std::filesystem::path directory = requested ? requested : "";
    std::free(requested);
    if (!need(environmentResult == 0 && !directory.empty(), "explicit private output directory")) return false;
    std::error_code ec; std::filesystem::create_directories(directory, ec);
    if (!need(!ec, "private output root")) return false;
    const auto tracePath = directory / "pointer-ingress-trace.txt";
    if (!need(!std::filesystem::exists(tracePath), "trace must be new")) return false;
    std::ofstream trace(tracePath, std::ios::binary | std::ios::out);
    if (!need(static_cast<bool>(trace), "complete trace file")) return false;
    if (!need(stock.report.success && stock.world.getSummary().loaded
        && stock.rulesLoaded && stock.artLoaded && stock.theaterLoaded, "actual stock resources")) return false;
    RegisteredClass registration;
    if (!need(registration.ready, "own unique hidden native class")) return false;
    const auto prototype = [&]() {
        auto p = std::make_unique<RuntimeWindowState>(); auto& s = *p;
        RuntimeSnapshotActiveStateScope active(s);
        s.config = stock.config; s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = false;
        s.config.startingCredits = 100000; s.config.startingUnits = 0; s.config.playerCountry = "Americans";
        s.config.showMainMenu = s.config.showSkirmishMenu = false;
        s.config.configPath = pathString(directory / "prototype-private.ini");
        s.config.playerSlots.assign(3, {});
        for (std::size_t i = 0; i < s.config.playerSlots.size(); ++i) {
            auto& slot = s.config.playerSlots[i]; slot.enabled = slot.human = true; slot.local = i == 0;
            slot.country = i % 2 ? "Russians" : "Americans"; slot.team = "-";
            slot.startPosition = static_cast<int>(i) + 1; slot.name = "Ingress player " + std::to_string(i + 1);
            const auto owner = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(i));
            s.skirmishCountryByOwner[owner] = slot.country;
            s.skirmishTeamByOwner[owner] = runtimeNormalizeSkirmishTeam(slot.team);
        }
        s.report = stock.report; s.world = stock.world; s.world.accessObjects().clear();
        s.rules = stock.rules; s.art = stock.art; s.theater = stock.theater; s.mapFacts = stock.mapFacts;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.mapFactsLoaded = stock.mapFactsLoaded;
        s.terrainInfoByCell = stock.terrainInfoByCell; s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        s.runtimeSoundsEnabled = s.runtimeMusicEnabled = s.runtimeSoundEffectsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.visionEnabled = false; initializeRuntimeNavigation(s); initializeRuntimeEconomy(s);
        if (!need(initializeRuntimeSkirmishStart(s), "ordinary legal three-house skirmish start"))
            return std::unique_ptr<RuntimeWindowState>{};
        s.triggers.bootstrapComplete = true; s.skirmishModeAlliesAllowed = s.skirmishModeAllyChangeAllowed = true;
        s.skirmishModeMustAlly = false;
        return p;
    }();
    if (!prototype) return false;
    const auto make = [&](const std::string& label) {
        auto p = std::make_unique<RuntimeWindowState>(); auto& s = *p;
        RuntimeSnapshotActiveStateScope active(s);
        s.config = prototype->config; s.report = prototype->report; s.world = prototype->world;
        s.rules = prototype->rules; s.art = prototype->art; s.theater = prototype->theater;
        s.mapFacts = prototype->mapFacts; s.mapFactsLoaded = prototype->mapFactsLoaded;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.economy = prototype->economy; s.playerCommands = prototype->playerCommands;
        s.humanHouseAuthorities = prototype->humanHouseAuthorities;
        s.skirmishCountryByOwner = prototype->skirmishCountryByOwner;
        s.skirmishTeamByOwner = prototype->skirmishTeamByOwner;
        s.terrainInfoByCell = prototype->terrainInfoByCell; s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        s.skirmishModeAlliesAllowed = s.skirmishModeAllyChangeAllowed = true; s.skirmishModeMustAlly = false;
        s.triggers.bootstrapComplete = true; s.tick = 21; s.visionEnabled = false;
        s.runtimeSoundsEnabled = s.runtimeMusicEnabled = s.runtimeSoundEffectsEnabled = false;
        s.runtimeMusicVolume = s.runtimeSoundVolume = s.runtimeVoiceVolume = 0;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.config.configPath = pathString(directory / (label + "-preferences.ini"));
        if (!need(!std::filesystem::exists(s.config.configPath), "private preferences must be new " + label))
            return std::unique_ptr<RuntimeWindowState>{};
        std::ofstream preferences(s.config.configPath, std::ios::binary | std::ios::out);
        preferences << "[Preserved]\nSentinel=keep\n"; preferences.close();
        if (!need(static_cast<bool>(preferences), "private preferences " + label))
            return std::unique_ptr<RuntimeWindowState>{};
        s.config.keyboardReferenceLayout = true; s.config.keyBindings.clear();
        initializeRuntimeNavigation(s); initializeRuntimeOfflineGameplayRouting(s); ensureRuntimeMenuKeyboardLoaded(s);
        s.runtimeMenuOpen = false; s.runtimeMenuKeyCaptureIndex = -1;
        s.playerCommunication = {}; s.playerCommunication.chatDraft = u"kept";
        s.playerCommunication.beaconEditing = s.playerCommunication.beaconTargeting = true;
        s.playerCommunication.selectedBeacon = RuntimeMultiplayerGameplayCell{4, 6};
        s.leftPointer.active = s.leftPointer.moved = false; s.rightPointer.active = s.rightPointer.moved = false;
        s.repaintRequested = false; s.interactionStatus = "ingress initial";
        s.activeBuildTab = RuntimeBuildKind::Building;
        s.buildPageByTab = {0, 1, 2, 3}; s.buildWheelRemainderByTab = {17, -19, 23, -29};
        const RECT client{0, 0, 1024, 768};
        ensureRuntimeCameraForViewport(s, runtimeMapViewport(client));
        return p;
    };
    const auto center = [](RECT r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
    const auto packed = [](POINT p) { return MAKELPARAM(static_cast<short>(p.x), static_cast<short>(p.y)); };
    const auto open = [&](Window& w, RuntimeWindowState& s) {
        if (w.path == 0) RuntimePointerIngressSmokeOld::frozenOpenDiplomacyPage(s);
        else if (w.path == 1) { RuntimeDiplomacyPointerUiHost host(s); openRuntimeDiplomacyPointer(host); }
        else openRuntimeDiplomacyPage(s);
        ReleaseCapture();
        need(GetCapture() == nullptr, "happy diplomacy begins without capture after original route-local own-capture proof");
    };
    const auto ordinaryRoundTrip = [&](Window& window, RuntimeWindowState& s) {
        s.config.shortGame = true; s.tick = 21; (void)advanceRuntimeParticles(s);
        if (!need(s.world.getSummary().loaded && !s.campaignMode && !runtimeLocalObserver(s.config)
            && s.config.playerSlots.size() == 3 && s.particlesInitialized && s.particles.currentTick() == 21,
            "ordinary natural active three-house tick21 save preconditions")) return false;
        s.leftPointer.active = s.leftPointer.moved = true;
        s.leftPointer.start = {33, 44}; s.leftPointer.current = {55, 66};
        const RuntimeUiClock* borrowedClock = s.uiClock;
        struct ResourceRootRestore {
            RuntimeConfig& config; std::string original;
            ~ResourceRootRestore() { config.localDataRootPath = original; }
        } restore{s.config, s.config.localDataRootPath};
        const auto privateRoot = directory / ("ordinary-active-path-" + std::to_string(window.path));
        if (!need(!std::filesystem::exists(privateRoot), "new route-local ordinary save root")) return false;
        std::error_code saveDirectoryError; std::filesystem::create_directories(privateRoot, saveDirectoryError);
        if (!need(!saveDirectoryError, "private ordinary save root")) return false;
        s.config.localDataRootPath = pathString(privateRoot);
        const auto savePath = runtimeDefaultSavePath(s); std::string error;
        if (!need(!std::filesystem::exists(savePath), "new ordinary active save")) return false;
        trace << "loadFixture path=" << window.path << " phase=before-save file=" << hex(savePath)
            << " rawState=" << hex(RuntimePointerIngressSmokeDetail::state(s, false, s.config.configPath)) << '\n';
        const bool saved = saveRuntimeState(s, savePath, error);
        const bool loaded = saved && loadRuntimeState(s, savePath, error);
        if (!need(saved && loaded && s.tick == 21 && s.uiClock == borrowedClock,
            "real ordinary save/load preserves tick and borrowed clock: " + error)) return false;
        need(!s.leftPointer.active && s.leftPointer.moved && s.leftPointer.start.x == 33
            && s.leftPointer.start.y == 44 && s.leftPointer.current.x == 55 && s.leftPointer.current.y == 66,
            "ordinary load preserves original active-only left gesture swap");
        s.runtimeMenuOpen = false; s.playerCommunication.chatOpen = s.playerCommunication.diplomacyOpen = false;
        s.playerCommunication.beaconTargeting = false; s.playerCommunication.selectedBeacon.reset();
        s.playerLocalCommands.followTargeting = s.playerLocalCommands.rallyTargeting = false;
        SetWindowPos(window.handle, nullptr, -32000, -32000, 800, 600, SWP_NOACTIVATE | SWP_NOZORDER);
        invalidateRuntimeObjectProjectionCache(s);
        RECT client{}; GetClientRect(window.handle, &client);
        const RECT viewport = runtimeMapViewport(client);
        s.cameraClampViewport = viewport; clampRuntimeMapCamera(s, viewport);
        trace << "loadFixture path=" << window.path << " phase=after-load file=" << hex(savePath)
            << " rawState=" << hex(RuntimePointerIngressSmokeDetail::state(s, false, s.config.configPath)) << '\n';
        ++loadCases; return true;
    };
    const auto wheelFixture = [&](RuntimeWindowState& s, HWND h) {
        need(!s.economy.catalog.empty(), "real initializeRuntimeEconomy catalog exists");
        need(deployRuntimeMcv(s, s.economy.skirmishStartObjectId, false),
            "real ordinary starting MCV deployment establishes stock production prerequisites");
        const auto* powerPointer = findRuntimeGameplayCatalogItem(s, s.economy.owner, "GAPOWR");
        if (!need(powerPointer != nullptr, "stock Allied power catalog item exists after real MCV deployment")) return;
        const RuntimeBuildCatalogItem power = *powerPointer;
        if (!need(queueRuntimeBuildOrder(s, power), "real stock GAPOWR production queue accepted")) return;
        const int simulationLimit = std::max(1, s.config.fixedTickHz) * 60 * 10;
        for (int frame = 0; frame < simulationLimit && !runtimeReadyProductionForItem(s, power); ++frame) {
            ++s.tick;
            (void)advanceRuntimeSimulation(s, false);
        }
        if (!need(runtimeReadyProductionForItem(s, power) != nullptr,
            "real GAPOWR production reaches the original ready lane without forcing remaining ticks")) return;
        const auto ready = runtimeOldestReadyPlacement(s);
        if (!need(ready && ready->item && ready->item->typeId == power.typeId,
            "original oldest ready placement identifies the actual stock GAPOWR")) return;
        if (!need(beginRuntimePlacementFromUi(s, *ready, true),
            "original UI placement owner selects the real ready GAPOWR")) return;
        const auto cell = findRuntimePlacementCellNearBase(s, power);
        if (!need(cell.has_value(), "original placement query finds a legal GAPOWR cell")) return;
        std::vector<int> previousIds;
        for (const auto& object : s.world.getObjects()) previousIds.push_back(object.instanceId);
        const auto previousCount = s.world.getObjects().size();
        if (!need(placeRuntimeReadyBuilding(s, *cell) && s.world.getObjects().size() > previousCount,
            "original actual placement consumes the ready order and grows the real world")) return;
        int powerId = -1;
        for (const auto& object : s.world.getObjects()) {
            if (object.alive && object.owner == s.economy.owner && object.type == power.typeId
                && std::find(previousIds.begin(), previousIds.end(), object.instanceId) == previousIds.end()) {
                if (!need(powerId < 0, "stock power placement creates exactly one owned GAPOWR")) return;
                powerId = object.instanceId;
            }
        }
        if (!need(powerId >= 0, "real GAPOWR placement yields an owned stable object identity")) return;
        bool completed = false;
        for (int frame = 0; frame <= simulationLimit; ++frame) {
            const auto* placed = findObjectById(s, powerId);
            if (!need(placed && placed->alive && placed->owner == s.economy.owner && placed->type == power.typeId,
                "real placed GAPOWR identity remains valid during original construction simulation")) return;
            completed = placed->buildAnimTicksRemaining == 0;
            if (completed || frame == simulationLimit) break;
            ++s.tick;
            (void)advanceRuntimeSimulation(s, false);
        }
        (void)completeRuntimeWorldRefresh(s);
        if (!need(completed && s.tick > 21 && s.economy.powerProduced > 0
            && runtimePrerequisiteTokenMet(s, "POWER"),
            "real completed GAPOWR supplies POWER at the actual advanced simulation tick")) return;
        ensureRuntimeSuperWeaponsInitialized(s);
        SetWindowPos(h, nullptr, -32000, -32000, 512, 240, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT client{}; GetClientRect(h, &client);
        s.activeBuildTab = RuntimeBuildKind::Building;
        need(runtimeBuildPageCount(s, runtimeSidebarRect(client)) > 1,
            "stock Building availability and actual small client form multiple real pages");
    };
    const auto run = [&](const std::string& name, int commandMode, const auto& setup, const auto& action) {
        std::array<Saved, 3> saved;
        for (int path = 0; path < 3; ++path) {
            const std::string label = name + "-path-" + std::to_string(path);
            auto storage = make(label);
            if (!storage) continue;
            auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            Clock clock; ClockBinding binding(s, clock); Witness witness(s, commandMode);
            Window window(s, path);
            if (!need(window.handle != nullptr, label + " own HWND")) continue;
            SetFocus(window.handle); SetCapture(window.handle);
            if (!need(GetCapture() == window.handle, label + " immediate route-local own capture")) continue;
            setup(window, s);
            witness.attach();
            window.events.clear(); window.sequence = 0; window.recording = true;
            const std::string privatePath = s.config.configPath;
            std::size_t step = 0;
            const auto save = [&](const std::string& phase, LRESULT result) {
                const std::string normalized = RuntimePointerIngressSmokeDetail::state(s, true, privatePath)
                    + ";window=" + std::to_string(IsWindow(window.handle) != FALSE)
                    + ";capture=" + std::to_string(window.kind(GetCapture()));
                saved[path].states.push_back(normalized); saved[path].results.push_back(result);
                saved[path].clockReads.push_back(clock.reads);
                trace << "case=" << name << " path=" << path << " step=" << step++
                    << " phase=" << phase << " hwnd=" << reinterpret_cast<std::uintptr_t>(window.handle)
                    << " result=" << result << " clockReads=" << clock.reads
                    << " rawState=" << hex(RuntimePointerIngressSmokeDetail::state(s, false, privatePath))
                    << " normalizedState=" << hex(normalized) << '\n'; ++steps;
            };
            save("initial", 0);
            const auto send = [&](UINT message, WPARAM raw, LPARAM flags) {
                if (!need(IsWindow(window.handle) != FALSE, label + " target HWND alive")) return LRESULT{0};
                const LRESULT result = SendMessageW(window.handle, message, raw, flags);
                save("message-" + std::to_string(message), result); return result;
            };
            action(window, s, send);
            if ((name.starts_with("alliance-") || name == "post-load-alliance-recipient") && commandMode != 0) {
                need(witness.commands.size() == 1, label + " reaches exactly one original canonical alliance command");
                if (witness.commands.size() == 1) {
                    const auto& row = witness.commands.front();
                    RuntimeMultiplayerGameplayCommand decoded; std::string commandError; std::vector<std::uint8_t> canonical;
                    need(row.tick == 21 && !row.payload.empty()
                        && decodeRuntimeMultiplayerGameplayCommand(row.payload, decoded, commandError)
                        && encodeRuntimeMultiplayerGameplayCommand(decoded, canonical, commandError)
                        && canonical == row.payload, label + " complete original wire bytes roundtrip at actual tick");
                    need(row.accepted == (commandMode != 3) && row.applied == (commandMode == 1),
                        label + " original accepted/applied/refused consumer outcome");
                }
            }
            std::string replayError;
            const auto beforeReplay = witness.commands.size();
            need(witness.replay(replayError), label + " original offline replay " + replayError);
            if (commandMode == 2) replayedCommands += beforeReplay;
            save("final", 0);
            for (const auto& event : window.events) {
                saved[path].events.push_back(event.semantic());
                trace << "event case=" << name << " path=" << path << " seq=" << event.sequence
                    << " depth=" << event.depth << " msg=" << event.message << " entering=" << event.entering
                    << " hwnd=" << event.hwnd << " capture=" << event.capture
                    << " wParam=" << event.rawWParam << " lParam=" << event.rawLParam
                    << " captureKind=" << event.captureKind << " destinationKind=" << event.destinationKind
                    << " state=" << hex(event.state) << '\n';
            }
            saved[path].commands = witness.commands;
            for (std::size_t i = 0; i < witness.commands.size(); ++i) {
                const auto& command = witness.commands[i]; std::string bytes(command.payload.begin(), command.payload.end());
                trace << "command case=" << name << " path=" << path << " index=" << i << " tick=" << command.tick
                    << " accepted=" << command.accepted << " applied=" << command.applied
                    << " payload=" << hex(bytes) << " error=" << hex(command.error) << '\n';
                if (command.accepted) ++acceptedCommands; else ++rejectedCommands;
            }
            if (s.runtimeExitRequested && !IsWindow(window.handle)) ++destroyedByReturn;
            window.recording = false; window.close();
            MSG quit{}; while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
        }
        need(saved[0] == saved[1], name + " complete original / same-CPP state-command-clock-event trace");
        need(saved[0] == saved[2], name + " complete original / real WndProc state-command-clock-event trace");
        ++triples;
    };
    const auto noop = [](Window&, RuntimeWindowState&) {};
    for (int mode = 0; mode < 6; ++mode) {
        run("score-" + std::to_string(mode), 0,
            [&](Window&, RuntimeWindowState& s) { s.skirmishMatchFinished = true;
                s.resultPresentation.started = true; s.resultPresentation.bannerVisible = false;
                s.playerCommunication.beaconEditing = s.playerCommunication.beaconTargeting = false; },
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                RECT client{}; GetClientRect(w.handle, &client); const auto button = runtimeMatchScoreContinueRect(client);
                POINT p = center(button);
                if (mode == 1) p.x = button.right;
                if (mode == 2) p = {button.left, button.top};
                if (mode == 3) { SetWindowPos(w.handle, nullptr, -32000, -32000, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
                    GetClientRect(w.handle, &client); p = center(runtimeMatchScoreContinueRect(client)); }
                if (mode == 4) s.resultPresentation.continuePressed = true;
                send(WM_MOUSEMOVE, 0, packed(p));
                if (mode != 4) send(WM_LBUTTONDOWN, MK_LBUTTON, packed(p));
                if (mode == 5) p.x = button.right;
                send(WM_LBUTTONUP, 0, packed(p));
                if (mode == 0 || mode == 2 || mode == 3 || mode == 4)
                    need(s.runtimeExitRequested && !IsWindow(w.handle), "real score Return reaches original session destruction");
                else need(!s.runtimeExitRequested && IsWindow(w.handle), "score excluded/outside up cannot activate");
            }); ++scoreCases;
    }
    const std::array<UINT, 14> terminalMessages{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK,
        WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK, WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK,
        WM_XBUTTONDOWN, WM_XBUTTONUP, WM_XBUTTONDBLCLK, WM_MOUSEWHEEL, WM_MOUSEHWHEEL};
    for (UINT m : terminalMessages) {
        run("terminal-" + std::to_string(m), 0,
            [&](Window&, RuntimeWindowState& s) { s.skirmishMatchFinished = true; s.resultPresentation.bannerVisible = true; },
            [&](Window&, RuntimeWindowState&, const auto& send) { send(m, 0, 0); }); ++scoreCases;
    }
    for (int mutation = 1; mutation <= 4; ++mutation) {
        run("score-fresh-" + std::to_string(mutation), 0,
            [&](Window& w, RuntimeWindowState& s) { s.skirmishMatchFinished = true;
                s.resultPresentation.bannerVisible = false; s.resultPresentation.continuePressed = true; w.mutation = mutation; },
            [&](Window& w, RuntimeWindowState&, const auto& send) { RECT r{}; GetClientRect(w.handle, &r);
                send(mutation == 4 ? WM_LBUTTONUP : WM_LBUTTONDOWN, 0, packed(center(runtimeMatchScoreContinueRect(r)))); });
        ++captureCases;
    }
    run("terminal-other-capture", 0,
        [&](Window&, RuntimeWindowState& s) { s.skirmishMatchFinished = true; s.resultPresentation.bannerVisible = true; },
        [&](Window& w, RuntimeWindowState&, const auto& send) { send(WM_APP + 71, 0, 0);
            need(GetCapture() != w.handle, "Other still releases finished own capture"); }); ++captureCases;
    for (int commandMode = 0; commandMode < 4; ++commandMode) for (int id : {1743, 1744}) {
        run("alliance-" + std::to_string(commandMode) + "-" + std::to_string(id), commandMode, open,
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                const auto rows = RuntimePointerIngressSmokeOld::frozenDiplomacyRows(s);
                need(rows.size() == 3 && rows[0].self && !rows[1].defeated && !rows[2].defeated,
                    "ordinary actual three-house diplomacy rows");
                RECT client{}; GetClientRect(w.handle, &client); const auto p = center(runtimeDiplomacyCheckboxRect(client, id));
                const auto before = authority(s); send(WM_MOUSEMOVE, 0, packed(p)); send(WM_LBUTTONDOWN, MK_LBUTTON, packed(p));
                send(WM_LBUTTONUP, 0, packed(p));
                if (commandMode == 0 || commandMode == 1) need(authority(s) != before, "original real alliance consumer applied");
                if (commandMode == 3) need(authority(s) == before, "router refusal leaves full authority unchanged");
            }); ++diplomacyCases;
    }
    run("post-load-alliance-recipient", 1,
        [&](Window& w, RuntimeWindowState& s) {
            if (!ordinaryRoundTrip(w, s)) return;
            SetFocus(w.handle); SetCapture(w.handle);
            need(GetCapture() == w.handle, "fresh route-local own capture after actual ordinary load");
            open(w, s);
        },
        [&](Window& w, RuntimeWindowState& s, const auto& send) {
            const auto rows = RuntimePointerIngressSmokeOld::frozenDiplomacyRows(s);
            need(rows.size() == 3 && rows[0].self && !rows[1].defeated, "fresh loaded real diplomacy roster");
            RECT client{}; GetClientRect(w.handle, &client);
            const auto alliancePoint = center(runtimeDiplomacyCheckboxRect(client, 1743));
            const auto beforeAlliance = authority(s);
            send(WM_LBUTTONDOWN, MK_LBUTTON, packed(alliancePoint)); send(WM_LBUTTONUP, 0, packed(alliancePoint));
            need(authority(s) != beforeAlliance, "first loaded-root alliance reaches actual consumer");
            const auto beforeRecipient = authority(s);
            const auto recipientPoint = center(runtimeDiplomacyCheckboxRect(client, 1750));
            send(WM_LBUTTONDOWN, MK_LBUTTON, packed(recipientPoint)); send(WM_LBUTTONUP, 0, packed(recipientPoint));
            const auto own = runtimePlayerChatSlot(s, s.economy.owner);
            need(s.playerCommunication.chatRecipientsExplicit && own
                && (s.playerCommunication.chatRecipients & (1U << *own)),
                "fresh loaded-root recipient retains local slot");
            need(authority(s) == beforeRecipient, "post-load recipient remains local presentation only");
        }); ++diplomacyCases;
    for (int id : {1059, 1840, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1752, 1753, 1762, 1763, 1764}) {
        run("diplomacy-control-" + std::to_string(id), 1, open,
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                RECT client{}; GetClientRect(w.handle, &client);
                const POINT p = center(id == 1059 || id == 1840 ? runtimeDiplomacyControlRect(client, id)
                    : runtimeDiplomacyCheckboxRect(client, id));
                const auto before = authority(s); const bool beforeTaunts = s.playerCommunication.tauntsEnabled;
                send(WM_MOUSEMOVE, 0, packed(p)); send(WM_LBUTTONDOWN, MK_LBUTTON, packed(p)); send(WM_LBUTTONUP, 0, packed(p));
                if (id == 1059) need(!s.playerCommunication.diplomacyOpen, "real close control");
                if (id == 1840) need(s.playerCommunication.tauntsEnabled != beforeTaunts, "real taunts control toggles owned prior value");
                if (id == 1750 || id == 1751) {
                    need(s.playerCommunication.chatRecipientsExplicit, "actual recipient control owns explicit selection");
                    const auto ownSlot = runtimePlayerChatSlot(s, s.economy.owner);
                    need(ownSlot && (s.playerCommunication.chatRecipients & (1U << *ownSlot)),
                        "recipient policy retains local human slot");
                    need(authority(s) == before, "recipient local policy does not change world command authority");
                }
            }); ++diplomacyCases;
    }
    run("diplomacy-same-window-recapture", 1,
        [&](Window& w, RuntimeWindowState& s) { open(w, s); SetCapture(w.handle);
            need(GetCapture() == w.handle, "explicit same-window diplomacy recapture precondition"); },
        [&](Window& w, RuntimeWindowState& s, const auto& send) {
            RECT client{}; GetClientRect(w.handle, &client);
            const auto point = center(runtimeDiplomacyCheckboxRect(client, 1743));
            const auto before = authority(s);
            send(WM_LBUTTONDOWN, MK_LBUTTON, packed(point));
            need(GetCapture() == w.handle && s.playerCommunication.diplomacyPressedControl == 0,
                "original synchronous same-window capture clears pressed while still owning capture");
            send(WM_LBUTTONUP, 0, packed(point));
            need(authority(s) == before, "same-window recapture does not dispatch an unpressed alliance");
        }); ++captureCases;
    for (UINT m : {UINT(WM_CAPTURECHANGED), UINT(WM_CANCELMODE), UINT(WM_KILLFOCUS), UINT(WM_ACTIVATEAPP)}) {
        run("diplomacy-cancel-" + std::to_string(m), 0,
            [&](Window& w, RuntimeWindowState& s) { open(w, s); SetCapture(w.handle);
                need(GetCapture() == w.handle, "cancel scenario owns capture after opening");
                s.playerCommunication.diplomacyPressedControl = 1743; },
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                if (m == WM_CAPTURECHANGED) {
                    HWND other = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owned capture peer", WS_POPUP,
                        -32000, -32000, 8, 8, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                    need(other != nullptr, "own other capture HWND"); if (other) {
                        SetCapture(other); need(GetCapture() == other, "real other capture ownership");
                        send(WM_CANCELMODE, 0, 0); if (GetCapture() == other) ReleaseCapture(); DestroyWindow(other);
                    }
                } else send(m, 0, 0);
                need(s.playerCommunication.diplomacyOpen && s.playerCommunication.diplomacyPressedControl == 0,
                    "narrow cancel preserves page and clears pressed");
                if (m != WM_CAPTURECHANGED) need(GetCapture() != w.handle, "cancel releases only own capture");
            }); ++captureCases;
    }
    run("deactivate-full-width", 0,
        [&](Window& w, RuntimeWindowState& s) { open(w, s); SetCapture(w.handle);
                need(GetCapture() == w.handle, "cancel scenario owns capture after opening");
                s.playerCommunication.diplomacyPressedControl = 1743; },
        [&](Window&, RuntimeWindowState& s, const auto& send) {
            send(WM_ACTIVATEAPP, static_cast<WPARAM>(std::uint64_t{1} << 32), 0);
            need(s.playerCommunication.diplomacyPressedControl == 1743, "nonzero full raw wParam is not FALSE");
        }); ++captureCases;
    const std::array<UINT, 6> chatMessages{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK,
        WM_RBUTTONDOWN, WM_RBUTTONUP, WM_MOUSEWHEEL};
    for (UINT m : chatMessages) {
        run("chat-" + std::to_string(m), 0,
            [&](Window&, RuntimeWindowState& s) { s.playerCommunication.chatOpen = true;
                s.leftPointer.active = s.leftPointer.moved = true; },
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                send(m, 0, MAKELPARAM(100, 100));
                need(!s.leftPointer.active && !s.leftPointer.moved, "original chat clears only left flags");
                need(GetCapture() == w.handle, "chat path does not add ReleaseCapture");
                if (m == WM_RBUTTONUP) need(!s.playerCommunication.chatOpen && !s.playerCommunication.diplomacyOpen
                    && !s.playerCommunication.beaconEditing && s.playerCommunication.chatDraft.empty(),
                    "chat right-up preserves original full local clear");
            }); ++chatCases;
    }
    for (int tab = 0; tab < 5; ++tab) for (int variant = 0; variant < 3; ++variant) {
        run("wheel-" + std::to_string(tab) + "-" + std::to_string(variant), 0,
            [&](Window& w, RuntimeWindowState& s) {
                wheelFixture(s, w.handle);
                s.activeBuildTab = tab == 4 ? static_cast<RuntimeBuildKind>(99) : runtimeBuildKindForTabIndex(tab);
                s.buildPageByTab.fill(variant == 1 ? -3 : variant == 2 ? 999 : 0);
                s.buildWheelRemainderByTab = {119, -119, 17, -17};
            },
            [&](Window& w, RuntimeWindowState& s, const auto& send) {
                const auto before = authority(s);
                for (int delta : {0, -1, -119, -120, -360, 1, 119, 120, 720})
                    send(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(200, 200));
                need(authority(s) == before, "wheel real stock catalog never changes actual world/commands/tick");
                RECT client{}; GetClientRect(w.handle, &client);
                const int page = runtimeActiveBuildPage(s), count = runtimeBuildPageCount(s, runtimeSidebarRect(client));
                need(page >= 0 && page < count, "wheel page within fresh original count");
            }); ++wheelCases;
    }
    run("wheel-menu", 0,
        [&](Window& w, RuntimeWindowState& s) { wheelFixture(s, w.handle); s.runtimeMenuOpen = true; },
        [&](Window&, RuntimeWindowState& s, const auto& send) { const auto oldPages = s.buildPageByTab;
            const auto oldRemainders = s.buildWheelRemainderByTab;
            send(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), 0);
            need(s.buildPageByTab == oldPages && s.buildWheelRemainderByTab == oldRemainders, "menu wheel does not query/change page");
        }); ++wheelCases;
    for (bool empty : {false, true}) {
        run(empty ? "wheel-empty" : "wheel-single", 0,
            [&](Window& w, RuntimeWindowState& s) { wheelFixture(s, w.handle);
                s.activeBuildTab = empty ? static_cast<RuntimeBuildKind>(99) : RuntimeBuildKind::Building;
                SetWindowPos(w.handle, nullptr, -32000, -32000, 1024, 12000, SWP_NOZORDER | SWP_NOACTIVATE);
                RECT client{}; GetClientRect(w.handle, &client);
                const auto filtered = runtimeFilteredBuildCatalogIndices(s);
                need(!empty || filtered.empty(), "unknown stock tab naturally has empty filtered view");
                need(empty || !filtered.empty(), "stock deployed Building view is genuinely nonempty");
                need(runtimeBuildPageCount(s, runtimeSidebarRect(client)) == 1,
                    "actual empty or full-height stock view has exactly one page");
                s.buildPageByTab.fill(999); },
            [&](Window&, RuntimeWindowState& s, const auto& send) {
                send(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), 0);
                need(runtimeActiveBuildPage(s) == 0 && s.buildWheelRemainderByTab[0] == 0, "single page clamp and remainder reset");
            }); ++wheelCases;
    }
    run("null-state-wheel", 0, noop,
        [&](Window& w, RuntimeWindowState& s, const auto& send) { const auto before = authority(s);
            SetWindowLongPtrW(w.handle, GWLP_USERDATA, 0); send(WM_MOUSEWHEEL, 0, 0);
            SetWindowLongPtrW(w.handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
            need(authority(s) == before, "null state retains original native fallback"); }); ++wheelCases;
    need(windows.empty(), "all private HWND registry entries removed");
    trace << "summary triples=" << triples << " steps=" << steps << " failures=" << failures << '\n';
    trace.flush(); need(static_cast<bool>(trace), "complete trace flush");
    output << "Pointer-ingress native " << (failures ? "failed" : "passed") << ": checks=" << checks
        << " oldCases=" << triples << " directCases=" << triples << " nativeCases=" << triples
        << " steps=" << steps << " scoreCases=" << scoreCases << " diplomacyCases=" << diplomacyCases
        << " chatCases=" << chatCases << " wheelCases=" << wheelCases << " captureCases=" << captureCases
        << " accepted=" << acceptedCommands << " rejected=" << rejectedCommands << " replayed=" << replayedCommands
        << " actualResultDestroyed=" << destroyedByReturn << " ordinaryActiveLoadCases=" << loadCases << " failures=" << failures
        << " sameProductionCpp=1 completeOldOuter=1 rawPathsRetained=1 physicalInput=not-run IME=not-run"
        << " runningNetwork=not-run performance=not-run allWindowThreadsProven=not-claimed"
        << " W012FullMatrix=not-covered\n";
    return failures == 0;
}
