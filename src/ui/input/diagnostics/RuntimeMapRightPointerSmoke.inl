// Independent complete original W010 policy. Only explicit state-value/type/name adapters.
namespace RuntimeMapRightPointerSmokeFrozenOld {
void frozenCancelInteraction(RuntimeWindowState& state) {
    state.selectedObjectIds.clear();
    state.leftPointer.active = false;
    state.leftPointer.moved = false;
    state.rightPointer.active = false;
    state.rightPointer.moved = false;
    state.activeSidebarTool = RuntimeSidebarTool::None;
    state.lastCommand.valid = false;
    state.interactionStatus = "selection canceled";
    state.simulationStatus = "right cancel selection only";
    state.actionSummary.formationTargets = 0;
    refreshRuntimeActionSummary(state);
}


bool frozenPan(
    RuntimeWindowState& state,
    int dx,
    int dy
) {
    if ((dx * dx) + (dy * dy) <= 16) {
        return false;
    }

    const int panX = std::clamp(dx, -96, 96);
    const int panY = std::clamp(dy, -96, 96);
    if (panX == 0 && panY == 0) {
        return false;
    }

    if (!scrollRuntimeMapCameraClamped(state, panX, panY)) {
        state.interactionStatus = "map edge";
        return false;
    }
    invalidateRuntimeObjectProjectionCache(state);
    state.interactionStatus = "right drag pan";
    return true;
}

int frozenPanStep(int delta) {
    const int magnitude = std::abs(delta);
    if (magnitude <= 4) {
        return 0;
    }

    const int step = std::clamp(magnitude / 4, 1, 32);
    return delta < 0 ? -step : step;
}

bool frozenAdvance(RuntimeWindowState& state) {
    if (!state.runtimeScrollCoastingEnabled
        || !state.rightPointer.active
        || !state.rightPointer.moved) {
        return false;
    }

    const int dx = state.rightPointer.last.x - state.rightPointer.start.x;
    const int dy = state.rightPointer.last.y - state.rightPointer.start.y;
    const int rateScale = std::clamp(state.runtimeScrollRate, 0, 6) + 3;
    const int panX = frozenPanStep(dx) * rateScale / 6;
    const int panY = frozenPanStep(dy) * rateScale / 6;
    if (!frozenPan(state, panX, panY)) {
        return false;
    }
    return true;
}


bool frozenCaptureTarget(
    RuntimeWindowState& state,
    RuntimeMultiplayerGameplayTargetOrderKind order,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance* targetObject
) {
    refreshRuntimeMultiplayerPlayerIntentAuthority(state);
    RuntimeMultiplayerGameplayTarget target =
        targetObject != nullptr
            ? RuntimeMultiplayerGameplayTarget{
                RuntimeMultiplayerGameplayObjectTarget{
                    targetObject->instanceId
                }
              }
            : RuntimeMultiplayerGameplayTarget{
                RuntimeMultiplayerGameplayCellTarget{
                    runtimeGameplayCellForUi(targetCell)
                }
              };
    std::string errorMessage;
    const bool accepted = state.multiplayerIntentRouter.targetOrder(
        order,
        state.selectedObjectIds,
        std::move(target),
        errorMessage,
        state.playerLocalCommands.planning
    );
    noteRuntimeMultiplayerIntentResult(
        state,
        accepted,
        "command",
        errorMessage
    );
    return accepted;
}

bool frozenDispatchTarget(
    RuntimeWindowState& state,
    RuntimeMultiplayerGameplayTargetOrderKind order,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance* targetObject = nullptr
) {
    if (targetObject && runtimeIsOverlayWallTargetId(targetObject->instanceId)) {
        using Order = RuntimeMultiplayerGameplayTargetOrderKind;
        if (order != Order::Primary && order != Order::Attack && order != Order::ForceFireCell) return false;
        const auto current = runtimeOverlayWallTargetAtCell(state, {targetObject->x, targetObject->y});
        if (!current || current->instanceId != targetObject->instanceId
            || toUpperAsciiLocal(current->type) != toUpperAsciiLocal(targetObject->type)) return false;
        return frozenDispatchTarget(state,
            Order::ForceFireCell,
            {current->x, current->y}, nullptr);
    }
    if (runtimeUsesGameplayIntentRouter(state)) {
        return frozenCaptureTarget(
            state,
            order,
            targetCell,
            targetObject
        );
    }
    RuntimeMultiplayerGameplayTargetOrderCommand body;
    body.order = order;
    body.objectIds.assign(state.selectedObjectIds.begin(), state.selectedObjectIds.end());
    body.target = targetObject
        ? RuntimeMultiplayerGameplayTarget{RuntimeMultiplayerGameplayObjectTarget{targetObject->instanceId}}
        : RuntimeMultiplayerGameplayTarget{RuntimeMultiplayerGameplayCellTarget{{targetCell.x, targetCell.y}}};
    body.append = state.playerLocalCommands.planning;
    const bool accepted = applyRuntimePlayerTargetOrder(state, state.economy.owner, body, true);
    if (accepted && body.append) state.interactionStatus = "order appended";
    return accepted;
}


bool frozenCompletedClick(
    RuntimeWindowState& state,
    const RECT& clientRect,
    POINT point
) {
    if (state.playerCommunication.selectedBeacon) {
        state.playerCommunication.selectedBeacon.reset();state.repaintRequested=true;return true;
    }
    if (state.playerLocalCommands.followTargeting || state.playerLocalCommands.rallyTargeting
        || state.playerCommunication.beaconTargeting) {
        state.playerCommunication.beaconTargeting = false;
        state.playerLocalCommands.followTargeting = false;
        state.playerLocalCommands.rallyTargeting = false;
        return true;
    }
    if (cancelRuntimeActiveSuperWeaponTargetSelection(
            state,
            "right-click"
        )) {
        return true;
    }

    const RECT viewport = runtimeMapViewport(clientRect);
    if (!pointInRectInclusive(viewport, point)) {
        return false;
    }

    if (dismissRuntimePlacementFromUi(state)) return true;

    if (state.playerLocalCommands.planning) {
        state.playerLocalCommands.selectedWaypoint = 0;
        frozenCancelInteraction(state);
        return true;
    }

    if (state.activeSidebarTool != RuntimeSidebarTool::None) {
        state.activeSidebarTool = RuntimeSidebarTool::None;
        state.interactionStatus = "sidebar tool canceled";
        return true;
    }

    if (state.selectedObjectIds.empty()) {
        frozenCancelInteraction(state);
        return true;
    }

    const std::vector<RuntimeProjectedObject>& projectedObjects =
        runtimeProjectedObjectsForViewport(state, viewport);
    const RuntimeProjectedObject* projectedObject =
        findProjectedObjectAtPoint(projectedObjects, point);
    const auto terrainWallTarget = visibleTerrainWallTargetAtPoint(state, viewport, point);
    if (terrainWallTarget) {
        if (frozenDispatchTarget(state,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {terrainWallTarget->x, terrainWallTarget->y}, &*terrainWallTarget)) return true;
    }
    if (projectedObject != nullptr
        && projectedObject->object != nullptr
        && !selectionContains(state, projectedObject->object->instanceId)) {
        const POINT targetPoint =
            approximateCellFromScreenPoint(state, viewport, point);
        const RuntimeCellPoint targetCell{targetPoint.x, targetPoint.y};
        if (frozenDispatchTarget(
                state,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                targetCell,
                projectedObject->object
            )) {
            return true;
        }
    }

    frozenCancelInteraction(state);
    return true;
}


LRESULT CALLBACK frozenWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    RuntimeWindowState* campaignState =
        reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );

    // A synchronous window callback can enter during a local simulation
    // batch. Close its shared terrain signature before any handler mutates
    // state, including load/input paths that are ordinarily outside the batch.
    if (campaignState != nullptr)
        runtimeInvalidateVisionTerrainBatchForMessage(campaignState->world);

    if (campaignState != nullptr) {
        RuntimeForceAttackHeldUiHost forceHost(*campaignState, window);
        handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message, wParam), forceHost);
    }
    if (campaignState != nullptr && campaignState->rightPointer.active
        && (message == WM_KILLFOCUS || message == WM_CANCELMODE
            || (message == WM_ACTIVATEAPP && wParam == FALSE))) {
        campaignState->rightPointer.active = false;
        campaignState->rightPointer.moved = false;
        campaignState->repaintRequested = true;
        if (GetCapture() == window) ReleaseCapture();
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
                handleRuntimeDiplomacyMouse(*campaignState, client, message, pointFromMouseMessage(lParam));
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
    case WM_RBUTTONDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr || !state->world.getSummary().loaded) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (state->runtimeMenuOpen) {
            return 0;
        }

        RECT clientRect;
        GetClientRect(window, &clientRect);
        const POINT point = pointFromMouseMessage(lParam);
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        const RECT sidebar = runtimeSidebarRect(clientRect);
        const RECT viewport = runtimeMapViewport(clientRect);
        // A map press may become a drag. Only a completed click cancels
        // targeting; sidebar/outside-map presses retain immediate cancel.
        if (!pointInRectInclusive(viewport, point)
            && cancelRuntimeActiveSuperWeaponTargetSelection(
                *state,
                "right-click"
            )) {
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        if (pointInRectInclusive(sidebar, point)) {
            const std::optional<int> buttonIndex =
                runtimeBuildButtonAtPoint(*state, sidebar, point);
            if (buttonIndex.has_value()) {
                dispatchRuntimeBuildButtonSecondaryActionFromUi(
                    *state,
                    state->economy.catalog[*buttonIndex]
                );
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            if (state->activeSidebarTool != RuntimeSidebarTool::None) {
                state->activeSidebarTool = RuntimeSidebarTool::None;
                state->interactionStatus = "sidebar tool canceled";
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            return 0;
        }
        if (!pointInRectInclusive(viewport, point)) {
            return 0;
        }

        SetFocus(window);
        SetCapture(window);
        state->rightPointer.active = true;
        state->rightPointer.moved = false;
        state->rightPointer.start = {point.x, point.y};
        state->rightPointer.last = {point.x, point.y};
        state->rightPointer.cursorDelta = {0, 0};
        state->interactionStatus = "right drag pan";
        state->repaintRequested = true;
        return 0;
    }
    case WM_RBUTTONUP:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr || !state->world.getSummary().loaded) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (state->runtimeMenuOpen) {
            return 0;
        }

        // ReleaseCapture synchronously sends WM_CAPTURECHANGED. Commit
        // the completed gesture first so that callback cannot erase it.
        const bool wasRightDrag = state->rightPointer.active;
        const bool wasRightDragMove = state->rightPointer.moved;
        state->rightPointer.active = false;
        state->rightPointer.moved = false;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        const POINT point = pointFromMouseMessage(lParam);
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        // A release after losing capture, or a sidebar press already
        // handled on button-down, must not cancel a retained map ability.
        if (!wasRightDrag) {
            return 0;
        }
        if (wasRightDragMove) {
            state->interactionStatus = "camera panned";
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        RECT clientRect;
        GetClientRect(window, &clientRect);
        frozenCompletedClick(*state, clientRect, point);

        state->repaintRequested = true;
        RedrawWindow(
            window,
            nullptr,
            nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
        );
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        const POINT point = pointFromMouseMessage(lParam);
        const bool mousePointChanged =
            !state->mouseClientPointValid
            || state->mouseClientPoint.x != point.x
            || state->mouseClientPoint.y != point.y;
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        if (mousePointChanged) {
            state->repaintRequested = true;
        }
        if (state->runtimeMenuOpen) {
            RECT clientRect{};
            GetClientRect(window, &clientRect);
            if (state->runtimeMenuSliderDrag != RuntimeMenuAction::None) {
                if (GetCapture() == window && (wParam & MK_LBUTTON) != 0) {
                    if (updateRuntimeMenuSliderDrag(
                            *state,
                            clientRect,
                            point.x
                        )) {
                        state->repaintRequested = true;
                        RedrawWindow(
                            window,
                            nullptr,
                            nullptr,
                            RDW_INVALIDATE | RDW_NOERASE
                        );
                    }
                }
                return 0;
            }
            const RuntimeMenuAction hover = runtimeMenuActionAtPoint(
                *state,
                clientRect,
                point
            );
            if (hover != state->runtimeMenuHover) {
                state->runtimeMenuHover = hover;
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
            }
            return 0;
        }
        if (state->rightPointer.active) {
            const POINT previousPoint{state->rightPointer.last.x, state->rightPointer.last.y};
            const int totalDx = point.x - state->rightPointer.start.x;
            const int totalDy = point.y - state->rightPointer.start.y;
            if ((totalDx * totalDx) + (totalDy * totalDy) > 16) {
                state->rightPointer.moved = true;
            }
            if (point.x != previousPoint.x || point.y != previousPoint.y) {
                state->rightPointer.cursorDelta = {point.x - previousPoint.x, point.y - previousPoint.y};
            }
            state->rightPointer.last = {point.x, point.y};
            if (!state->runtimeScrollCoastingEnabled
                && state->rightPointer.moved) {
                frozenPan(
                    *state,
                    point.x - previousPoint.x,
                    point.y - previousPoint.y
                );
            }
            state->repaintRequested = true;
            return 0;
        }

        if (runtimeSelectedPlacement(*state).has_value()) {
            RECT clientRect;
            GetClientRect(window, &clientRect);
            const RECT viewport = runtimeMapViewport(clientRect);
            if (pointInRectInclusive(viewport, point)) {
                updateRuntimePlacementPreview(*state, viewport, point);
                state->repaintRequested = true;
            }
        }

        if (!state->leftPointer.active) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        state->leftPointer.current = {point.x, point.y};
        const int deltaX = point.x - state->leftPointer.start.x;
        const int deltaY = point.y - state->leftPointer.start.y;
        if ((deltaX * deltaX) + (deltaY * deltaY) > 16) {
            state->leftPointer.moved = true;
        }
        state->repaintRequested = true;
        return 0;
    }
    case WM_CAPTURECHANGED:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr && state->rightPointer.active) {
            state->rightPointer.active = false;
            state->rightPointer.moved = false;
            state->repaintRequested = true;
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
    default: return DefWindowProcW(window,message,wParam,lParam);
    }
}

} // namespace RuntimeMapRightPointerSmokeFrozenOld

namespace RuntimeMapRightPointerSmokeDetail {
LRESULT CALLBACK directWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    RuntimeWindowState* campaignState =
        reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );

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
        RuntimeMapRightPointerGestureUiHost gesture(*campaignState,window);
        const auto event = message == WM_KILLFOCUS ? RuntimeMapRightPointerCancelEvent::FocusLost
            : message == WM_CANCELMODE ? RuntimeMapRightPointerCancelEvent::CancelMode
            : message == WM_ACTIVATEAPP && wParam == FALSE ? RuntimeMapRightPointerCancelEvent::ApplicationDeactivated
            : RuntimeMapRightPointerCancelEvent::Other;
        cancelRuntimeMapRightPointer(event,gesture);
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
                handleRuntimeDiplomacyMouse(*campaignState, client, message, pointFromMouseMessage(lParam));
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
    case WM_RBUTTONDOWN: {
        auto* state=reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(!state)return DefWindowProcW(window,message,wParam,lParam);
        const POINT point=pointFromMouseMessage(lParam);RuntimeMapRightPointerGestureUiHost host(*state,window);
        return handleRuntimeMapRightPointerDown({{point.x,point.y}},host)?0:DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_RBUTTONUP: {
        auto* state=reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(!state)return DefWindowProcW(window,message,wParam,lParam);
        const POINT point=pointFromMouseMessage(lParam);RuntimeMapRightPointerGestureUiHost host(*state,window);
        RuntimeMapRightPointerClickUiHost click(*state);
        return handleRuntimeMapRightPointerUp({{point.x,point.y}},host,click)?0:DefWindowProcW(window,message,wParam,lParam);
    }
    case WM_MOUSEMOVE:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        const POINT point = pointFromMouseMessage(lParam);
        const bool mousePointChanged =
            !state->mouseClientPointValid
            || state->mouseClientPoint.x != point.x
            || state->mouseClientPoint.y != point.y;
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        if (mousePointChanged) {
            state->repaintRequested = true;
        }
        if (state->runtimeMenuOpen) {
            RECT clientRect{};
            GetClientRect(window, &clientRect);
            if (state->runtimeMenuSliderDrag != RuntimeMenuAction::None) {
                if (GetCapture() == window && (wParam & MK_LBUTTON) != 0) {
                    if (updateRuntimeMenuSliderDrag(
                            *state,
                            clientRect,
                            point.x
                        )) {
                        state->repaintRequested = true;
                        RedrawWindow(
                            window,
                            nullptr,
                            nullptr,
                            RDW_INVALIDATE | RDW_NOERASE
                        );
                    }
                }
                return 0;
            }
            const RuntimeMenuAction hover = runtimeMenuActionAtPoint(
                *state,
                clientRect,
                point
            );
            if (hover != state->runtimeMenuHover) {
                state->runtimeMenuHover = hover;
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
            }
            return 0;
        }
        { RuntimeMapRightPointerGestureUiHost host(*state,window);
          if(handleRuntimeMapRightPointerMove({{point.x,point.y}},host))return 0; }
        if (runtimeSelectedPlacement(*state).has_value()) {
            RECT clientRect;
            GetClientRect(window, &clientRect);
            const RECT viewport = runtimeMapViewport(clientRect);
            if (pointInRectInclusive(viewport, point)) {
                updateRuntimePlacementPreview(*state, viewport, point);
                state->repaintRequested = true;
            }
        }

        if (!state->leftPointer.active) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        state->leftPointer.current = {point.x, point.y};
        const int deltaX = point.x - state->leftPointer.start.x;
        const int deltaY = point.y - state->leftPointer.start.y;
        if ((deltaX * deltaX) + (deltaY * deltaY) > 16) {
            state->leftPointer.moved = true;
        }
        state->repaintRequested = true;
        return 0;
    }
    case WM_CAPTURECHANGED:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if(state){RuntimeMapRightPointerGestureUiHost host(*state,window);captureChangedRuntimeMapRightPointer(host);}
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
    default: return DefWindowProcW(window,message,wParam,lParam);
    }
}

inline constexpr const wchar_t* windowClassName=L"ConcordMapRightPointerSmoke";
struct Window;
thread_local std::map<HWND,Window*> windows;
LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
bool ownedMessage(UINT message,WPARAM raw){
    return message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_MOUSEMOVE
        ||message==WM_CAPTURECHANGED||message==WM_KILLFOCUS||message==WM_CANCELMODE
        ||(message==WM_ACTIVATEAPP&&raw==FALSE);
}
struct ClassRegistration {
    HINSTANCE instance=GetModuleHandleW(nullptr);bool registered=false;
    ClassRegistration(){WNDCLASSW type{};type.lpfnWndProc=windowProc;type.hInstance=instance;
        type.lpszClassName=windowClassName;registered=RegisterClassW(&type)!=0;}
    ~ClassRegistration(){if(registered)UnregisterClassW(windowClassName,instance);}
};
struct Event {
    std::size_t sequence=0;int depth=0;UINT message=0;bool entering=false;
    std::uintptr_t rawWindow=0,rawCapture=0,rawLParam=0;WPARAM rawWParam=0;
    int captureKind=0,lParamCaptureKind=0;bool active=false,moved=false;int tick=0;
    std::string semantic() const {std::ostringstream s;s<<depth<<','<<message<<','<<entering<<','
        <<captureKind<<','<<lParamCaptureKind<<','<<rawWParam<<','
        <<(message==WM_CAPTURECHANGED?0:rawLParam)<<','<<active<<','<<moved<<','<<tick;return s.str();}
};
struct Window {
    RuntimeWindowState& state;int path;HWND window=nullptr;std::vector<Event> events;
    int depth=0;std::size_t sequence=0;bool nestedCancel=false,nestedResize=false;bool nesting=false;
    UINT targetMessage=0;bool retainTrace=false;std::vector<std::string> semanticEvents;
    Window(RuntimeWindowState& value,int route):state(value),path(route){
        window=CreateWindowExW(0,windowClassName,L"Right pointer private contract",WS_POPUP,
            -32000,-32000,1024,768,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(window){windows.emplace(window,this);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));}
    }
    Window(const Window&)=delete;Window& operator=(const Window&)=delete;
    int captureKind(HWND value)const{return value==nullptr?0:value==window?1:2;}
    void record(UINT message,WPARAM raw,LPARAM flags,bool entering){
        const HWND capture=GetCapture();Event e;e.sequence=sequence++;e.depth=depth;e.message=message;e.entering=entering;
        e.rawWindow=reinterpret_cast<std::uintptr_t>(window);e.rawCapture=reinterpret_cast<std::uintptr_t>(capture);
        e.rawWParam=raw;e.rawLParam=static_cast<std::uintptr_t>(flags);e.captureKind=captureKind(capture);
        e.lParamCaptureKind=message==WM_CAPTURECHANGED?captureKind(reinterpret_cast<HWND>(flags)):0;
        e.active=state.rightPointer.active;e.moved=state.rightPointer.moved;e.tick=state.tick;events.push_back(e);
        if(retainTrace&&(message==targetMessage||message==WM_CAPTURECHANGED||message==WM_CANCELMODE
            ||message==WM_KILLFOCUS||message==WM_SETFOCUS))semanticEvents.push_back(e.semantic());
    }
    LRESULT dispatch(UINT message,WPARAM raw,LPARAM flags){
        ++depth;record(message,raw,flags,true);
        if(message==WM_CAPTURECHANGED&&!nesting&&(nestedCancel||nestedResize)){
            nesting=true;
            if(nestedResize){nestedResize=false;state.camera.tileWidth=90;state.camera.tileHeight=45;
                invalidateRuntimeObjectProjectionCache(state);
                SetWindowPos(window,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);}
            if(nestedCancel){nestedCancel=false;SendMessageW(window,WM_CANCELMODE,0,0);}
            nesting=false;
        }
        LRESULT result=0;
        if(ownedMessage(message,raw)){
            if(path==0)result=RuntimeMapRightPointerSmokeFrozenOld::frozenWindowProc(window,message,raw,flags);
            else if(path==1)result=directWindowProc(window,message,raw,flags);
            else result=runtimeWindowProc(window,message,raw,flags);
        }else result=runtimeWindowProc(window,message,raw,flags);
        record(message,raw,flags,false);--depth;return result;
    }
    void close(){if(window){const HWND previous=window;DestroyWindow(previous);windows.erase(previous);window=nullptr;}}
    ~Window(){close();}
};
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM raw,LPARAM flags){
    const auto found=windows.find(window);
    return found==windows.end()?DefWindowProcW(window,message,raw,flags):found->second->dispatch(message,raw,flags);
}
} // namespace RuntimeMapRightPointerSmokeDetail

// Independent native differential probe: original literals / public same-CPP + real host / actual WndProc.
// No paint, external input injection, new world objects or altered production assertion.
bool runRuntimeMapRightPointerSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    using RuntimeMapRightPointerSmokeDetail::Window;
    std::size_t checks=0,failures=0,triples=0,steps=0,sourceReads=0,loadCases=0;
    std::size_t commandCases=0,acceptedCommands=0,rejectedCommands=0,replayedCommands=0;
    std::size_t captureCases=0,geometryCases=0,guardCases=0;
    const auto expect=[&](bool value,const std::string& label){++checks;if(!value){++failures;
        output<<"Map-right-pointer failure: "<<label<<'\n';}return value;};
    char* directoryValue=nullptr;std::size_t directoryLength=0;
    if(_dupenv_s(&directoryValue,&directoryLength,"CONCORD_MAP_RIGHT_POINTER_OUTPUT")!=0||directoryValue==nullptr)
        return expect(false,"explicit private output directory");
    const std::filesystem::path directory(directoryValue);std::free(directoryValue);
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    if(!expect(!ec&&std::filesystem::is_directory(directory),"private output root"))return false;
    const auto tracePath=directory/"right-pointer-trace.txt";
    if(!expect(!std::filesystem::exists(tracePath),"trace must be new"))return false;
    std::ofstream trace(tracePath,std::ios::binary|std::ios::out);
    if(!expect(static_cast<bool>(trace),"new trace"))return false;
    const RECT client{0,0,1024,768};const RECT viewport=runtimeMapViewport(client);
    const POINT center{(viewport.left+viewport.right)/2,(viewport.top+viewport.bottom)/2};
    RuntimeMapRightPointerSmokeDetail::ClassRegistration registration;
    if(!expect(registration.registered,"own unique native WindowProc class"))return false;
    struct Clock final : RuntimeUiClock {RuntimeUiTimeMs value=123456;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;}};
    struct ClockBinding {RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& c):state(s),previous(s.uiClock){s.uiClock=&c;}
        ~ClockBinding(){state.uiClock=previous;}};
    struct Capture {int tick=0;std::vector<std::uint8_t> payload;bool accepted=false,applied=false;std::string error;
        bool operator==(const Capture&) const=default;};
    struct Witness {RuntimeWindowState& state;int route;std::vector<Capture> commands;
        Witness(RuntimeWindowState& s,int mode):state(s),route(mode){attach();}
        void attach(){
            state.multiplayerBridge=nullptr;state.offlineGameplayIntentRouting=false;state.multiplayerIntentRouter.setCaptureCallback({});
            if(route==0)return;
            initializeRuntimeOfflineGameplayRouting(state);
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                Capture c;c.tick=state.tick;if(!encodeRuntimeMultiplayerGameplayCommand(command,c.payload,error))return false;
                if(route==3){error="right-pointer fixture capture refused";c.accepted=false;}
                else if(route==2){error.clear();c.accepted=true;}
                else c.accepted=c.applied=dispatchRuntimeOfflineGameplayCommand(state,command,error);
                c.error=error;commands.push_back(c);return c.accepted;});}
        bool replay(std::string& error){if(route!=2)return true;for(const auto& item:commands){
            RuntimeMultiplayerGameplayCommand command;
            if(!decodeRuntimeMultiplayerGameplayCommand(item.payload,command,error)||!dispatchRuntimeOfflineGameplayCommand(state,command,error))return false;}return true;}
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});}};
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char c:value)s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return s.str();};
    const auto wideText=[](const auto& units){std::ostringstream s;for(auto unit:units)s<<static_cast<std::uint32_t>(unit)<<',';return s.str();};
    const auto replaceExact=[](std::string value,const std::string& from,const std::string& to){if(from.empty())return value;std::size_t at=0;
        while((at=value.find(from,at))!=std::string::npos){value.replace(at,from.size(),to);at+=to.size();}return value;};
    std::map<const RuntimeWindowState*,std::pair<std::string,std::string>> privateIdentities;
    const auto canonical=[&](std::string value,const RuntimeWindowState& state){const auto found=privateIdentities.find(&state);
        if(found!=privateIdentities.end()){
            if(state.config.configPath==found->second.first)
                value=replaceExact(std::move(value),found->second.first,"<private-preferences>");
            if(state.config.localDataRootPath==found->second.second){
                const auto save=pathString(std::filesystem::path(found->second.second)/"runtime_save.ini");
                if(runtimeDefaultSavePath(state)==save)value=replaceExact(std::move(value),save,"<private-runtime-save>");
                value=replaceExact(std::move(value),found->second.second,"<private-save-root>");}}
        return value;};
    const auto authority=[&](const RuntimeWindowState& state){std::ostringstream s;
        s<<state.tick<<':'<<state.world.deterministicSimulationStateHash()<<':'<<hexText(state.playerCommands.serialize())<<':'<<state.playerCommands.stateHash()
            <<':'<<runtimeSerializeProductionLanes(state.economy.localProductionLanes)<<':'<<state.economy.credits
            <<':'<<state.lastCommand.valid<<':'<<state.lastCommand.verb<<':'<<state.lastCommand.tick
            <<':'<<state.lastCommand.selectedCount<<':'<<state.lastCommand.acceptedCount<<':'<<state.lastCommand.rejectedCount
            <<':'<<state.lastCommand.targetX<<':'<<state.lastCommand.targetY<<':'<<state.lastCommand.targetObjectId
            <<':'<<state.lastCommand.targetObjectType<<':'<<state.lastCommand.inputAckImmediate<<':'<<state.lastCommand.feedbackLatencyTicks;return s.str();};
    const auto baseState=[&](const RuntimeWindowState& state,bool normalized){
        std::ostringstream s;s<<"menu="<<state.runtimeMenuOpen<<','<<static_cast<int>(state.runtimeMenuPage)
            <<','<<static_cast<int>(state.runtimeMenuHover)<<','<<static_cast<int>(state.runtimeMenuPressed)<<','<<static_cast<int>(state.runtimeMenuSliderDrag)
            <<','<<state.runtimeMenuFocus<<','<<state.runtimeMenuKeyCaptureIndex<<','<<state.runtimeMenuKeyboardCategory<<','<<state.runtimeMenuKeyboardPage
            <<";status="<<hexText(normalized?canonical(state.interactionStatus,state):state.interactionStatus)
            <<";menuStatus="<<wideText(state.runtimeMenuStatus)<<";keySource="<<state.runtimeMenuKeyboardSource
            <<";keysLoaded="<<state.runtimeMenuKeyboardLoadAttempted<<";keys=";
        for(const auto& [id,code]:state.runtimeMenuKeyboardBindings)s<<id<<':'<<code<<',';
        s<<";configKeys=";for(const auto& [id,code]:state.config.keyBindings)s<<id<<':'<<code<<',';
        s<<";preferences="<<state.config.keyboardReferenceLayout<<','<<state.config.gameSpeed<<','<<state.config.scrollRate
            <<','<<state.config.visualDetail<<','<<state.config.tooltipsEnabled<<','<<state.config.targetLinesEnabled<<','<<state.config.scrollCoastingEnabled
            <<','<<state.config.seeHiddenObjects<<','<<state.config.windowWidth<<','<<state.config.windowHeight
            <<','<<state.config.musicEnabled<<','<<state.config.soundEffectsEnabled<<','<<state.config.musicVolume<<','<<state.config.soundVolume<<','<<state.config.voiceVolume
            <<";runtimePrefs="<<state.runtimeScrollRate<<','<<state.runtimeVisualDetail<<','<<state.runtimeTooltipsEnabled<<','<<state.runtimeTargetLinesEnabled
            <<','<<state.runtimeScrollCoastingEnabled<<','<<state.runtimeResolutionWidth<<','<<state.runtimeResolutionHeight
            <<','<<state.runtimeMusicEnabled<<','<<state.runtimeSoundEffectsEnabled<<','<<state.runtimeMusicVolume<<','<<state.runtimeSoundVolume<<','<<state.runtimeVoiceVolume
            <<";forceAttack="<<state.forceAttackHeld<<";repaint="<<state.repaintRequested
            <<";camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY<<','<<state.camera.initialized
            <<";clamp="<<state.cameraClampViewport.left<<','<<state.cameraClampViewport.top<<','<<state.cameraClampViewport.right<<','<<state.cameraClampViewport.bottom
            <<";projection="<<state.projectedObjectsCacheValid<<','<<state.projectedObjectsCacheSignature
            <<";local="<<state.playerLocalCommands.cameraFollow<<','<<state.playerLocalCommands.followTargeting<<','<<state.playerLocalCommands.rallyTargeting
            <<','<<state.playerLocalCommands.planning<<";selection=";for(int id:state.selectedObjectIds)s<<id<<',';
        s<<";drag="<<state.leftPointer.active<<','<<state.leftPointer.moved<<','<<state.rightPointer.active<<','<<state.rightPointer.moved
            <<";beacon="<<state.playerCommunication.beaconTargeting<<";match="<<state.skirmishMatchFinished<<','<<state.skirmishVictory<<','<<state.skirmishResult
            <<";result="<<state.resultPresentation.resigned<<','<<state.resultPresentation.started<<','<<state.resultPresentation.bannerVisible
            <<','<<state.resultPresentation.startedAtMs<<','<<state.resultPresentation.bannerDurationMs<<','<<state.resultPresentation.scorePage
            <<','<<state.resultPresentation.continueHovered<<','<<state.resultPresentation.continuePressed<<','<<state.resultPresentation.scoreMusicSelected
            <<";exit="<<state.runtimeExitRequested<<";behaviors="<<state.behaviors.size()<<";authority="<<authority(state)<<";economyStatus="<<state.economy.status
            <<";sound="<<state.runtimeSoundEvents<<','<<state.lastRuntimeSound;
        for(const auto& intent:state.runtimeSoundIntentHistory)s<<";soundIntent="<<intent;
        for(const auto& sound:state.runtimeSoundPlaybackHistory)s<<";soundRecord="<<sound.tick<<','<<sound.playbackOrder<<','<<sound.intent<<','<<sound.section
            <<','<<sound.sample<<','<<sound.path<<','<<sound.status<<','<<sound.semantic<<','<<sound.eva<<','<<sound.playable<<','<<sound.played
            <<','<<sound.suppressed<<','<<sound.queued<<','<<sound.queueRequested;
        s<<";prefsPath="<<state.config.configPath<<";localDataRoot="<<state.config.localDataRootPath;s<<";session="<<state.campaignMode<<','<<state.exitAfterPendingCampaignVideos<<','<<state.campaignScore.presentationStartedAtMs
            <<";videos=";for(const auto& movie:state.pendingCampaignVideoPaths)s<<movie<<',';
        s<<";sidebar="<<static_cast<int>(state.activeSidebarTool)<<";menuPersist="<<state.runtimePreferencePersistenceEnabled
            <<";chat="<<state.playerCommunication.chatOpen<<','<<state.playerCommunication.diplomacyOpen
            <<";particles="<<state.particlesInitialized<<','<<state.particles.currentTick()<<";superWeaponAuthority="<<state.superWeapons.stateHash();
        if(const auto& placement=state.multiplayerIntentRouter.placement())s<<";placement="<<static_cast<int>(placement->lane)<<','<<placement->productionSerial<<','<<placement->buildingTypeId<<','<<placement->cell.x<<','<<placement->cell.y<<','<<placement->previewValid;
        if(const auto& weapon=state.multiplayerIntentRouter.superWeapon())s<<";superWeapon="<<weapon->superWeaponId<<','<<weapon->expectedTargetCount<<','<<weapon->targets.size();
        return normalized?canonical(s.str(),state):s.str();};
    const auto projectionState=[](const RuntimeWindowState& state){
        std::ostringstream s;s<<state.projectedObjectsCacheValid<<':'<<state.projectedObjectsCacheSignature<<':'
            <<state.projectedObjectsCacheCameraX<<','<<state.projectedObjectsCacheCameraY<<','<<state.projectedObjectsCacheTileWidth<<','<<state.projectedObjectsCacheTileHeight
            <<':'<<state.projectedObjectsCacheViewport.left<<','<<state.projectedObjectsCacheViewport.top<<','<<state.projectedObjectsCacheViewport.right<<','<<state.projectedObjectsCacheViewport.bottom;
        for(const auto& p:state.projectedObjectsCache)s<<';'<<(p.object?p.object->instanceId:-1)<<':'<<p.bounds.left<<','<<p.bounds.top<<','<<p.bounds.right<<','<<p.bounds.bottom
            <<':'<<p.center.x<<','<<p.center.y<<','<<p.renderCenter.x<<','<<p.renderCenter.y<<','<<p.hasMobileSpriteBounds<<','<<p.hasBodyHitClip<<','<<p.observerType;
        return s.str();};
    const auto fullState=[&](const RuntimeWindowState& state,bool normalized){std::ostringstream s;s<<baseState(state,normalized);
        const auto& local=state.playerLocalCommands;for(std::size_t i=0;i<local.teams.size();++i){s<<";team"<<i<<'=';for(int id:local.teams[i])s<<id<<',';}
        for(std::size_t i=0;i<local.views.size();++i){s<<";view"<<i<<'=';if(local.views[i])s<<local.views[i]->x<<','<<local.views[i]->y;else s<<"none";}
        s<<";quick="<<local.lastSelectionKey<<','<<local.lastSelectionAtMs<<','<<local.veterancyRank<<','<<local.healthRank
            <<";waypoint="<<local.selectedWaypoint<<";coordinates="<<local.showCoordinates<<";activeTab="<<static_cast<int>(state.activeBuildTab)<<";pages=";
        for(int page:state.buildPageByTab)s<<page<<',';s<<";wheels=";for(int wheel:state.buildWheelRemainderByTab)s<<wheel<<',';
        s<<";projectionFull="<<projectionState(state)<<";mouse="<<state.mouseClientPointValid<<','<<state.mouseClientPoint.x<<','<<state.mouseClientPoint.y
            <<";selectionPoints="<<state.leftPointer.start.x<<','<<state.leftPointer.start.y<<','<<state.leftPointer.current.x<<','<<state.leftPointer.current.y
            <<";chatFull="<<state.playerCommunication.chatAllies<<','<<wideText(state.playerCommunication.chatDraft)<<','<<state.playerCommunication.beaconEditing
            <<','<<state.playerCommunication.diplomacyRow<<','<<state.playerCommunication.diplomacyPressedControl<<','<<state.playerCommunication.diplomacyHoveredControl
            <<','<<state.playerCommunication.chatRecipientsExplicit<<','<<static_cast<unsigned>(state.playerCommunication.chatRecipients);
        if(state.playerCommunication.selectedBeacon)s<<";selectedBeacon="<<state.playerCommunication.selectedBeacon->x<<','<<state.playerCommunication.selectedBeacon->y;
        for(const auto& b:state.playerCommunication.beacons)s<<";beaconFull="<<b.owner<<','<<b.cell.x<<','<<b.cell.y<<','<<b.untilTick<<','<<b.level<<','<<hexText(b.text);
        for(const auto& line:state.playerCommunication.lines)s<<";chatLine="<<line.owner<<','<<hexText(line.text)<<','<<line.allies<<','<<line.untilTick;
        std::map<int,int> cheer(state.playerCommunication.cheerUntilTick.begin(),state.playerCommunication.cheerUntilTick.end());
        for(const auto& [id,until]:cheer)s<<";cheer="<<id<<','<<until;
        for(const auto& [key,tick]:state.playerCommunication.nextFeedbackTick)s<<";feedback="<<key.first<<','<<static_cast<int>(key.second)<<','<<tick;
        if(state.playerLastRadarCell)s<<";radar="<<state.playerLastRadarCell->x<<','<<state.playerLastRadarCell->y;
        s<<";admission="<<state.campaignMode<<','<<state.triggers.campaignInputLocked<<','<<state.skirmishMatchFinished<<','<<state.runtimeMenuOpen
            <<";cameraDimensions="<<state.camera.tileWidth<<','<<state.camera.tileHeight;
        s<<";rightPointer="<<state.rightPointer.active<<','<<state.rightPointer.moved<<','
            <<state.rightPointer.start.x<<','<<state.rightPointer.start.y<<','<<state.rightPointer.last.x<<','<<state.rightPointer.last.y
            <<','<<state.rightPointer.cursorDelta.x<<','<<state.rightPointer.cursorDelta.y
            <<";simulationStatus="<<state.simulationStatus<<";sourceLoaded="<<state.world.getSummary().loaded;
        return s.str();};
    const auto makeBase=[&](int productionMode,bool threePlayers=false,bool movingUnits=false){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config;state.config.gameMode="Battle";state.config.gameModeRulesOverride.clear();
        state.config.computerAiEnabled=false;state.config.multiplayerActive=false;
        state.config.noCooldown=false;state.config.startingCredits=100000;state.config.startingUnits=movingUnits?1:0;
        state.config.playerCountry="Americans";state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.config.playerSlots.assign(threePlayers?3:2,{});
        for(std::size_t index=0;index<state.config.playerSlots.size();++index){
            auto& slot=state.config.playerSlots[index];slot.enabled=slot.human=true;slot.local=index==0;
            slot.country=index%2?"Russians":"Americans";slot.team="-";slot.startPosition=static_cast<int>(index)+1;
            slot.name="Presentation player "+std::to_string(index+1);
            const auto owner=runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[owner]=slot.country;
            state.skirmishTeamByOwner[owner]=runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report=stock.report;state.world=stock.world;state.world.accessObjects().clear();
        state.rules=stock.rules;state.art=stock.art;state.theater=stock.theater;state.mapFacts=stock.mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;state.mapFactsLoaded=stock.mapFactsLoaded;
        state.terrainInfoByCell=stock.terrainInfoByCell;state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.visionEnabled=false;initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
        if(!expect(initializeRuntimeSkirmishStart(state),"real skirmish production start"))return std::unique_ptr<RuntimeWindowState>{};
        if(!expect(deployRuntimeMcv(state,state.economy.skirmishStartObjectId,false),"real MCV base deployment"))
            return std::unique_ptr<RuntimeWindowState>{};
        state.triggers.bootstrapComplete=true;
        const auto* powerPointer=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAPOWR");
        const auto* wallPointer=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAWALL");
        if(!expect(powerPointer&&wallPointer,"actual GAPOWR/GAWALL catalog"))return std::unique_ptr<RuntimeWindowState>{};
        const RuntimeBuildCatalogItem power=*powerPointer,wall=*wallPointer;
        const auto finish=[&](const RuntimeBuildCatalogItem& item){
            if(!queueRuntimeBuildOrder(state,item))return false;
            ++state.tick;advanceRuntimeProductionEconomy(state);
            if(!runtimeReadyProductionForItem(state,item)){
                if(!completeRuntimeActiveProductionForProbe(state,item))return false;
                ++state.tick;advanceRuntimeProductionEconomy(state);
            }
            return runtimeReadyProductionForItem(state,item)!=nullptr;
        };
        if(productionMode==1){
            if(!expect(finish(power)&&stageRuntimeReadyPlacementForProbe(state,wall),
                    "real Structures production and stock Armory ready fixture"))
                return std::unique_ptr<RuntimeWindowState>{};
        }else if(productionMode==2){
            if(!expect(queueRuntimeBuildOrder(state,power),"real unfinished production queued"))
                return std::unique_ptr<RuntimeWindowState>{};
            ++state.tick;advanceRuntimeProductionEconomy(state);
            if(!expect(!runtimeReadyProductionForItem(state,power),"real unfinished production not ready"))
                return std::unique_ptr<RuntimeWindowState>{};
        }
        clearRuntimePlacementSelection(state);state.multiplayerIntentRouter.cancelPlacement();
        initializeRuntimeOfflineGameplayRouting(state);
        state.selectedObjectIds.clear();
        for(const auto& object:state.world.getObjects())
            if(object.alive&&runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)){
                state.selectedObjectIds={object.instanceId};break;
            }
        if(!expect(!state.selectedObjectIds.empty(),"actual current owned base selection"))
            return std::unique_ptr<RuntimeWindowState>{};
        state.activeBuildTab=RuntimeBuildKind::Unit;state.buildPageByTab.fill(999);
        state.buildWheelRemainderByTab={2,-3,4,-5};state.activeSidebarTool=RuntimeSidebarTool::Repair;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=true;
        state.playerLocalCommands.cameraFollow=false;state.playerLocalCommands.lastSelectionKey='?';
        state.playerLocalCommands.lastSelectionAtMs=777;state.playerLocalCommands.veterancyRank=1;
        state.playerLocalCommands.healthRank=2;state.playerCommunication.beaconTargeting=true;
        state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{4,6};
        state.leftPointer.active=state.leftPointer.moved=true;
        state.mouseClientPoint={viewport.left+100,viewport.top+100};state.mouseClientPointValid=true;
        runtimePlayerCenterBase(state,client);state.interactionStatus="presentation initial";
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();
        state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();state.repaintRequested=false;
        return storage;
    };
    const auto isolate=[&](RuntimeWindowState& state,const std::string& name){
        state.config.configPath=pathString(directory/(name+"-preferences.ini"));
        if(std::filesystem::exists(state.config.configPath)||std::filesystem::exists(directory/(name+"-save")))return false;
        std::ofstream preferences(state.config.configPath,std::ios::binary|std::ios::out);preferences<<"[Preserved]\nSentinel=keep\n";preferences.close();
        const auto saveRoot=directory/(name+"-save");std::filesystem::create_directories(saveRoot,ec);if(ec)return false;
        state.config.localDataRootPath=pathString(saveRoot);
        const bool valid=static_cast<bool>(preferences)&&runtimeDefaultSavePath(state)==pathString(saveRoot/"runtime_save.ini");
        if(valid)privateIdentities[&state]={pathString(directory/(name+"-preferences.ini")),pathString(saveRoot)};
        return valid;};

    const auto make=[&](const RuntimeWindowState& seed,const std::string& name){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;RuntimeSnapshotActiveStateScope active(state);
        state.config=seed.config;state.report=seed.report;state.world=seed.world;state.rules=seed.rules;state.art=seed.art;
        state.theater=seed.theater;state.mapFacts=seed.mapFacts;state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;
        state.mapFactsLoaded=seed.mapFactsLoaded;state.economy=seed.economy;
        state.skirmishCountryByOwner=seed.skirmishCountryByOwner;state.skirmishTeamByOwner=seed.skirmishTeamByOwner;state.camera=seed.camera;state.terrainInfoByCell=seed.terrainInfoByCell;
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);state.triggers.bootstrapComplete=true;state.tick=7;state.visionEnabled=false;
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=state.runtimeSoundEffectsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimeMusicVolume=state.runtimeSoundVolume=state.runtimeVoiceVolume=0;state.runtimePreferencePersistenceEnabled=false;
        state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        initializeRuntimeNavigation(state);initializeRuntimeOfflineGameplayRouting(state);ensureRuntimeMenuKeyboardLoaded(state);
        state.runtimeScrollRate=3;state.runtimeScrollCoastingEnabled=false;
        state.selectedObjectIds=seed.selectedObjectIds;
        state.playerCommunication.selectedBeacon.reset();state.rightPointer={};
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;state.activeSidebarTool=RuntimeSidebarTool::None;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;state.playerCommunication.beaconTargeting=false;
        state.leftPointer.active=state.leftPointer.moved=false;state.repaintRequested=false;state.interactionStatus="right-pointer initial";
        if(!isolate(state,name))return std::unique_ptr<RuntimeWindowState>{};return storage;};

    auto normal=makeBase(0,true,true),ready=makeBase(1,true,true),queued=makeBase(2,true,true);
    if(!expect(normal&&ready&&queued,"three real three-house stock-production prototypes"))return false;
    const auto chooseMovable=[&](RuntimeWindowState& state){
        for(const auto& object:state.world.getObjects())if(object.alive&&object.owner==state.economy.owner&&object.kind!=WorldObjectKind::Structure){
            state.selectedObjectIds={object.instanceId};if(selectedMovableObjectCount(state)>0)return true;}
        state.selectedObjectIds.clear();return false;};
    if(!expect(chooseMovable(*normal)&&chooseMovable(*ready)&&chooseMovable(*queued),"actual initialized selectable moving units"))return false;
    // Test fixture only: production resource lookup derives from localDataRootPath.
    // Save/load always runs outside this synchronous borrow and keeps the unique private root.
    struct TrustedResourceRootScope {
        RuntimeConfig& config;std::string privateRoot;
        TrustedResourceRootScope(RuntimeConfig& value,const std::string& trustedRoot)
            :config(value),privateRoot(value.localDataRootPath){config.localDataRootPath=trustedRoot;}
        ~TrustedResourceRootScope(){config.localDataRootPath=std::move(privateRoot);}
        TrustedResourceRootScope(const TrustedResourceRootScope&)=delete;
        TrustedResourceRootScope& operator=(const TrustedResourceRootScope&)=delete;
    };
    struct SavedStep {std::string label,state;std::vector<std::string> events;std::vector<Capture> commands;
        std::size_t clockReads=0;std::intptr_t result=0;bool operator==(const SavedStep&)const=default;};
    using Send=std::function<LRESULT(UINT,POINT,WPARAM)>;
    using Save=std::function<void(const std::string&,std::intptr_t)>;
    using Tick=std::function<bool()>;
    using Action=std::function<void(RuntimeWindowState&,Window&,Witness&,const Send&,const Save&,const Tick&)>;
    bool caseOk=true;
    const auto runCase=[&](const std::string& name,const RuntimeWindowState& seed,int routingMode,const Action& action){
        std::array<std::vector<SavedStep>,3> recorded;bool complete=true;
        for(int path=0;path<3;++path){
            const std::string label=name+"-path"+std::to_string(path);
            auto storage=make(seed,label);if(!expect(static_cast<bool>(storage),label+" unique private state")){complete=false;break;}
            auto& state=*storage;Clock clock;ClockBinding borrowed(state,clock);RuntimeSnapshotActiveStateScope active(state);
            Window window(state,path);if(!expect(window.window!=nullptr,label+" actual own HWND")){complete=false;break;}
            RECT initialClient{};GetClientRect(window.window,&initialClient);
            state.cameraClampViewport=runtimeMapViewport(initialClient);
            clampRuntimeMapCamera(state,state.cameraClampViewport);
            SetFocus(window.window);SetCapture(window.window);
            if(!expect(GetCapture()==window.window,label+" immediate own capture before input sequence")){complete=false;break;}
            Witness witness(state,routingMode);caseOk=true;
            const Save save=[&](const std::string& step,std::intptr_t result){
                SavedStep item{step,fullState(state,true),window.semanticEvents,witness.commands,clock.reads,result};
                // Own full bytes stay on disk; normalized state replaces only this state's exact registered private paths.
                std::ifstream prefs(state.config.configPath,std::ios::binary);
                std::ostringstream bytes;bytes<<prefs.rdbuf();item.state+=";preferencesFile="+hexText(bytes.str());
                recorded[static_cast<std::size_t>(path)].push_back(std::move(item));
                trace<<"case="<<name<<" path="<<path<<" route="<<routingMode<<" step="<<step
                    <<" result="<<result<<" clockReads="<<clock.reads<<" rawState="<<hexText(fullState(state,false))
                    <<" normalizedState="<<hexText(recorded[static_cast<std::size_t>(path)].back().state)<<'\n';++steps;
            };
            const Send send=[&](UINT message,POINT point,WPARAM flags){
                window.targetMessage=message;window.retainTrace=true;
                const LRESULT result=SendMessageW(window.window,message,flags,MAKELPARAM(point.x,point.y));
                window.retainTrace=false;
                save("message-"+std::to_string(message)+"-"+std::to_string(recorded[static_cast<std::size_t>(path)].size()),result);
                return result;
            };
            const Tick tick=[&](){
                const bool result=path==0?RuntimeMapRightPointerSmokeFrozenOld::frozenAdvance(state)
                    :path==1?[&](){RuntimeMapRightPointerGestureUiHost host(state);return advanceRuntimeMapRightPointerTick(host);}()
                    :advanceRuntimeRightMousePan(state);
                save("coasting-tick-"+std::to_string(recorded[static_cast<std::size_t>(path)].size()),result);return result;
            };
            if(name=="ordinary-active21-load-fresh-root")action(state,window,witness,send,save,tick);
            else {
                TrustedResourceRootScope resources(state.config,stock.config.localDataRootPath);
                action(state,window,witness,send,save,tick);
            }
            save("final-before-window-destruction",0);
            sourceReads+=clock.reads;
            for(const auto& command:witness.commands){
                trace<<"command case="<<name<<" path="<<path<<" tick="<<command.tick<<" accepted="<<command.accepted
                    <<" applied="<<command.applied<<" payload=";
                for(const auto byte:command.payload)trace<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
                trace<<std::dec<<" error="<<hexText(command.error)<<'\n';
            }
            // WM_DESTROY and cross-route focus/capture traffic remain raw observations, outside saved input results.
            window.close();
            for(const auto& event:window.events)trace<<"windowEvent case="<<name<<" path="<<path
                <<" seq="<<event.sequence<<" depth="<<event.depth<<" msg="<<event.message<<" entering="<<event.entering
                <<" hwnd="<<event.rawWindow<<" capture="<<event.rawCapture<<" wParam="<<event.rawWParam
                <<" lParam="<<event.rawLParam<<" captureKind="<<event.captureKind<<" lParamCaptureKind="<<event.lParamCaptureKind
                <<" active="<<event.active<<" moved="<<event.moved<<" tick="<<event.tick<<'\n';
            if(!caseOk)complete=false;
        }
        if(complete){
            expect(recorded[0]==recorded[1],name+" original complete literal / same production CPP all saved states, commands, clock reads and ordered nested events");
            expect(recorded[0]==recorded[2],name+" original complete literal / actual native WndProc all saved states, commands, clock reads and ordered nested events");
        }
        ++triples;return complete&&failures==0;
    };
    const auto need=[&](bool value,const std::string& why){const bool ok=expect(value,why);caseOk=caseOk&&ok;return ok;};
    const auto clicked=[&](const Send& send,POINT point){send(WM_RBUTTONDOWN,point,MK_RBUTTON);send(WM_RBUTTONUP,point,0);};
    const auto freshViewport=[](Window& window){RECT rect{};GetClientRect(window.window,&rect);return runtimeMapViewport(rect);};
    const auto pointForObject=[&](RuntimeWindowState& state,Window& window,int id)->std::optional<POINT>{
        const auto* object=findObjectById(state,id);if(!object)return std::nullopt;
        RECT rect{};GetClientRect(window.window,&rect);centerRuntimeCameraOnCell(state,rect,{object->x,object->y});
        const RECT view=runtimeMapViewport(rect);invalidateRuntimeObjectProjectionCache(state);
        const auto& projected=runtimeProjectedObjectsForViewport(state,view);
        for(const auto& item:projected)if(item.object&&item.object->instanceId==id){
            for(LONG y=std::max(view.top,item.bounds.top);y<std::min(view.bottom,item.bounds.bottom);++y)
                for(LONG x=std::max(view.left,item.bounds.left);x<std::min(view.right,item.bounds.right);++x){
                    const POINT point{x,y};const auto* found=findProjectedObjectAtPoint(projected,point);
                    if(found&&found->object&&found->object->instanceId==id)return point;}
        }return std::nullopt;};
    const auto enemyTarget=[&](RuntimeWindowState& state,Window& window)->std::optional<POINT>{
        for(const auto& object:state.world.getObjects())if(object.alive
            &&runtimeCanonicalHumanHouseOwner(object.owner)!=runtimeCanonicalHumanHouseOwner(state.economy.owner)
            &&state.skirmishCountryByOwner.contains(runtimeCanonicalHumanHouseOwner(object.owner))){
                if(const auto point=pointForObject(state,window,object.instanceId))return point;
            }return std::nullopt;};
    const auto blankPoint=[&](RuntimeWindowState& state,Window& window)->std::optional<POINT>{
        const RECT view=freshViewport(window);const auto& projected=runtimeProjectedObjectsForViewport(state,view);
        for(LONG y=view.top+2;y<view.bottom-2;y+=7)for(LONG x=view.left+2;x<view.right-2;x+=7){
            const POINT p{x,y};if(!findProjectedObjectAtPoint(projected,p)&&!visibleTerrainWallTargetAtPoint(state,view,p))return p;}
        return std::nullopt;};

    for(int guard=0;guard<6;++guard){
        runCase("prefix-guard-"+std::to_string(guard),*normal,0,[&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick&){
            if(guard==0)state.world=decltype(state.world){};
            if(guard==1)state.runtimeMenuOpen=true;
            if(guard==2){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
            if(guard==3){state.skirmishMatchFinished=true;state.resultPresentation.bannerVisible=true;}
            if(guard==4){state.playerCommunication.chatOpen=true;state.playerCommunication.chatDraft=u"kept until release";}
            if(guard==5){state.playerCommunication.diplomacyOpen=true;state.playerCommunication.diplomacyPressedControl=3;}
            save("guard-input",0);send(WM_RBUTTONDOWN,center,MK_RBUTTON);send(WM_MOUSEMOVE,{center.x+15,center.y+7},MK_RBUTTON);
            send(WM_RBUTTONUP,center,0);
            need(!state.rightPointer.active&&!state.rightPointer.moved,"guard never starts a right drag");
            if(guard==4)need(!state.playerCommunication.chatOpen&&state.playerCommunication.chatDraft.empty(),"modal right release closes original chat");
        });++guardCases;
    }
    for(int coast=0;coast<2;++coast)for(int variant=0;variant<5;++variant){
        runCase("pan-"+std::to_string(coast)+"-"+std::to_string(variant),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick& tick){
                // Choose a real playable isometric cell using the existing projection/bounds owners.
                const auto playable=buildRuntimeRadarProjection(state);
                const auto bounds=runtimeCameraWorldBounds(state);
                std::optional<RuntimeCellPoint> actualCenter;
                long long nearest=std::numeric_limits<long long>::max();
                for(const auto& terrain:state.world.getTerrainCells()){
                    if(!runtimeRadarTerrainCellIntersectsProjection(playable,terrain))continue;
                    const auto projected=runtimeProjectCellXY(terrain.x,terrain.y,terrain.level,state.camera);
                    const long long distance=std::abs(2LL*projected.x-bounds.left-bounds.right)
                        +std::abs(2LL*projected.y-bounds.top-bounds.bottom);
                    if(distance<nearest){nearest=distance;actualCenter=RuntimeCellPoint{terrain.x,terrain.y};}
                }
                if(!need(bounds.valid&&actualCenter.has_value(),"actual playable projected cell for movable pan fixture"))return;
                RECT actualClient{};GetClientRect(window.window,&actualClient);
                centerRuntimeCameraOnCell(state,actualClient,*actualCenter);
                state.runtimeScrollCoastingEnabled=coast!=0;
                state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=true;
                state.playerCommunication.beaconTargeting=true;
                const auto selection=state.selectedObjectIds;
                if(variant==3){state.camera.worldOffsetX=100000;state.camera.worldOffsetY=100000;clampRuntimeMapCamera(state,freshViewport(window));}
                save("pan-precondition",0);send(WM_RBUTTONDOWN,center,MK_RBUTTON);
                need(state.rightPointer.active&&!state.rightPointer.moved&&GetCapture()==window.window,"down owns capture without canceling retained ability");
                const auto cameraX=state.camera.worldOffsetX,cameraY=state.camera.worldOffsetY;
                send(WM_MOUSEMOVE,{center.x+4,center.y},MK_RBUTTON);
                need(!state.rightPointer.moved,"distance squared exactly16 remains click");
                send(WM_MOUSEMOVE,{center.x+5,center.y},MK_RBUTTON);
                need(state.rightPointer.moved,"distance squared25 starts drag");
                send(WM_MOUSEMOVE,{center.x+80,center.y+40},MK_RBUTTON);
                const auto delta=state.rightPointer.cursorDelta;
                send(WM_MOUSEMOVE,{center.x+80,center.y+40},MK_RBUTTON);
                need(state.rightPointer.cursorDelta.x==delta.x&&state.rightPointer.cursorDelta.y==delta.y,"same point keeps previous cursor delta");
                if(coast)tick();
                if(variant==1)send(WM_MOUSEMOVE,center,MK_RBUTTON);
                if(variant==2)send(WM_MOUSEMOVE,{-20,-20},MK_RBUTTON);
                if(variant==4){state.camera.tileWidth=90;state.camera.tileHeight=45;
                    SetWindowPos(window.window,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
                    invalidateRuntimeObjectProjectionCache(state);save("real-resize-and-zoom",0);++geometryCases;}
                const POINT end=variant==2?POINT{-20,-20}:variant==1?center:POINT{center.x+80,center.y+40};
                send(WM_RBUTTONUP,end,0);
                need(!state.rightPointer.active&&!state.rightPointer.moved&&GetCapture()!=window.window,"completed pan clears before synchronous release callback");
                need(state.playerLocalCommands.followTargeting&&state.playerLocalCommands.rallyTargeting&&state.playerCommunication.beaconTargeting
                    &&state.selectedObjectIds==selection,"all pan variants retain command ability and selection");
                if(variant==0)need(state.camera.worldOffsetX!=cameraX||state.camera.worldOffsetY!=cameraY,"real camera moved");
                need(state.interactionStatus=="camera panned","drag including return/or outside remains pan");
        });
    }
    for(int rate:{-1,0,3,6,8}){
        runCase("coast-rate-"+std::to_string(rate),*normal,0,[&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick& tick){
            state.runtimeScrollCoastingEnabled=true;state.runtimeScrollRate=rate;
            save("rate",0);send(WM_RBUTTONDOWN,center,MK_RBUTTON);send(WM_MOUSEMOVE,{center.x-130,center.y+57},MK_RBUTTON);
            tick();send(WM_RBUTTONUP,{center.x-130,center.y+57},0);
            need(!state.rightPointer.active,"coasting clears at release");});
    }
    for(int interruption=0;interruption<7;++interruption){
        runCase("capture-interruption-"+std::to_string(interruption),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                state.playerLocalCommands.followTargeting=true;const auto selection=state.selectedObjectIds;
                send(WM_RBUTTONDOWN,center,MK_RBUTTON);send(WM_MOUSEMOVE,{center.x+20,center.y+10},MK_RBUTTON);
                if(interruption==0){window.targetMessage=WM_CAPTURECHANGED;window.retainTrace=true;ReleaseCapture();window.retainTrace=false;save("external-release",0);}
                if(interruption==1)send(WM_KILLFOCUS,{},0);
                if(interruption==2)send(WM_CANCELMODE,{},0);
                if(interruption==3)send(WM_ACTIVATEAPP,{},FALSE);
                if(interruption==4){
                    const HWND other=CreateWindowExW(0,L"STATIC",L"Private capture recipient",WS_POPUP,-32000,-32000,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                    if(!need(other!=nullptr,"owned second hidden window"))return;
                    window.targetMessage=WM_CAPTURECHANGED;window.retainTrace=true;SetCapture(other);window.retainTrace=false;
                    need(GetCapture()==other,"actual other capture observed");save("actual-other-capture",0);
                    DestroyWindow(other);
                }
                if(interruption==5||interruption==6){
                    window.nestedCancel=interruption==5;window.nestedResize=interruption==6;
                    send(WM_CANCELMODE,{},0);
                }
                need(!state.rightPointer.active&&!state.rightPointer.moved&&GetCapture()!=window.window,"capture/cancel/focus interruption clears before nested observer");
                send(WM_RBUTTONUP,center,0);
                need(state.playerLocalCommands.followTargeting&&state.selectedObjectIds==selection,"stray up after lost capture cannot cancel retained ability");
                if(interruption==5)need(std::any_of(window.events.begin(),window.events.end(),[](const auto& e){return e.message==WM_CANCELMODE&&e.depth>=3;}),"synchronous nested CancelMode captured inside release");
                if(interruption==6)need(state.camera.tileWidth==90,"real reentrant resize/zoom executed");
        });++captureCases;
    }
    runCase("capture-up-commit-order",*normal,0,[&](auto& state,auto& window,auto&,const Send& send,const Save&,const Tick&){
        send(WM_RBUTTONDOWN,center,MK_RBUTTON);send(WM_MOUSEMOVE,{center.x+50,center.y+20},MK_RBUTTON);
        window.nestedCancel=true;send(WM_RBUTTONUP,{center.x+50,center.y+20},0);
        need(state.interactionStatus=="camera panned","up snapshots before reentrant CaptureChanged/CancelMode");
        for(const auto& e:window.events)if(e.message==WM_CAPTURECHANGED&&e.entering)
            need(!e.active&&!e.moved,"capture observer sees committed cleared gesture");
    });++captureCases;
    runCase("pan-step-signed-boundaries",*normal,0,[&](auto& state,auto& window,auto&,const Send&,const Save& save,const Tick&){
        const std::array<int,9> deltas{-129,-33,-5,-4,0,4,5,33,129};
        const std::array<int,9> expected{-32,-8,-1,0,0,0,1,8,32};
        for(std::size_t index=0;index<deltas.size();++index){
            const int result=window.path==0?RuntimeMapRightPointerSmokeFrozenOld::frozenPanStep(deltas[index]):runtimeMapRightPointerPanStep(deltas[index]);
            need(result==expected[index],"original signed integer pan-step boundary");save("pan-step-"+std::to_string(deltas[index]),result);
        }
        state.rightPointer.active=true;state.rightPointer.moved=true;
        for(const auto point:std::array<POINT,4>{POINT{0,0},POINT{4,0},POINT{5,0},POINT{300,-300}}){
            const bool result=window.path==0?RuntimeMapRightPointerSmokeFrozenOld::frozenPan(state,point.x,point.y)
                :[&](){RuntimeMapRightPointerGestureUiHost host(state);return panRuntimeMapRightPointerByDelta(point.x,point.y,host);}();
            save("pan-delta-"+std::to_string(point.x),result);
        }
    });

    runCase("selected-beacon-first",*normal,0,[&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick&){
        state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{4,6};
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=true;state.playerCommunication.beaconTargeting=true;
        const auto selection=state.selectedObjectIds;save("all-lower-priorities-present",0);clicked(send,center);
        need(!state.playerCommunication.selectedBeacon&&state.playerLocalCommands.followTargeting
            &&state.playerLocalCommands.rallyTargeting&&state.playerCommunication.beaconTargeting&&state.selectedObjectIds==selection,
            "selected beacon alone wins before all lower click priorities");
    });
    for(int mask=1;mask<8;++mask){
        runCase("target-flags-"+std::to_string(mask),*normal,0,[&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick&){
            state.playerLocalCommands.followTargeting=(mask&1)!=0;state.playerLocalCommands.rallyTargeting=(mask&2)!=0;
            state.playerCommunication.beaconTargeting=(mask&4)!=0;const auto selection=state.selectedObjectIds;
            save("target-flags",0);clicked(send,center);
            need(!state.playerLocalCommands.followTargeting&&!state.playerLocalCommands.rallyTargeting&&!state.playerCommunication.beaconTargeting
                &&state.selectedObjectIds==selection,"all target flags clear together without authority");
        });
    }
    for(int mode=0;mode<2;++mode)for(int outside=0;outside<2;++outside){
        runCase("real-superweapon-cancel-"+std::to_string(mode)+"-"+std::to_string(outside),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const SuperWeaponRule* rule=nullptr;
                for(const auto& item:state.rules.getSuperWeaponTypes())
                    if(containsAsciiCaseInsensitive(item.id,"spyplane")||containsAsciiCaseInsensitive(item.type,"spyplane")
                        ||containsAsciiCaseInsensitive(item.action,"spyplane")){rule=&item;break;}
                if(!need(rule!=nullptr,"actual SpyPlane rule"))return;const auto id=rule->id;
                state.superWeapons.reset(state.rules);state.superWeaponsInitialized=true;
                auto context=runtimeSuperWeaponContext(state);std::string error;
                if(!need(state.superWeapons.forceReady(state.economy.owner,id,&context),"actual ready superweapon: "+error))return;
                if(mode){context.tick=++state.tick;state.superWeapons.tick(context);refreshRuntimeMultiplayerPlayerIntentAuthority(state);
                    if(!need(state.multiplayerIntentRouter.beginSuperWeapon(id,1,error),"actual routed selected superweapon: "+error))return;}
                else if(!need(state.superWeapons.beginTargetSelection(state.economy.owner,id,context,&error),"actual selected superweapon: "+error))return;
                const auto selection=state.selectedObjectIds;const POINT point=outside?POINT{-20,-20}:center;
                save("actual-selected-superweapon",0);send(WM_RBUTTONDOWN,point,MK_RBUTTON);
                if(outside)need(!state.rightPointer.active,"outside press cancels before starting gesture");
                else need(state.rightPointer.active,"inside press retains target until completed click");
                send(WM_RBUTTONUP,point,0);
                need(!runtimeActiveLocalSuperWeaponTargeting(state)&&!state.multiplayerIntentRouter.superWeapon()
                    &&state.selectedObjectIds==selection&&witness.commands.empty(),"real superweapon canceled with no fire command or selection loss");
                need(!state.rightPointer.active&&GetCapture()!=window.window,"ability release clears capture");
        });
    }
    for(int kind=0;kind<4;++kind){
        runCase("map-click-priority-"+std::to_string(kind),kind==0?*ready:*normal,0,
            [&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick&){
                if(kind==0){const auto placement=runtimeOldestReadyPlacement(state);
                    if(!need(placement.has_value(),"actual ready lane before placement dismissal"))return;
                    selectRuntimeReadyPlacement(state,*placement,false);}
                if(kind==1){state.playerLocalCommands.planning=true;state.playerLocalCommands.selectedWaypoint=57;}
                if(kind==2)state.activeSidebarTool=RuntimeSidebarTool::Repair;
                if(kind==3)state.selectedObjectIds.clear();
                save("click-priority",0);clicked(send,center);
                if(kind==0)need(!runtimeSelectedPlacement(state),"original selected ready placement dismissed");
                if(kind==1)need(state.playerLocalCommands.selectedWaypoint==0&&state.selectedObjectIds.empty(),"planning click clears waypoint and uses whole cancel owner");
                if(kind==2)need(state.activeSidebarTool==RuntimeSidebarTool::None&&!state.selectedObjectIds.empty(),"sidebar tool alone canceled");
                if(kind==3)need(state.interactionStatus=="selection canceled"&&!state.lastCommand.valid,"empty selection invokes complete cancel owner");
        });
    }
    for(int kind=0;kind<4;++kind){
        runCase("sidebar-down-"+std::to_string(kind),kind==0?*ready:kind==1?*queued:*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                RECT rect{};GetClientRect(window.window,&rect);const RECT sidebar=runtimeSidebarRect(rect);
                std::optional<POINT> point;
                if(kind<2){
                    state.activeBuildTab=RuntimeBuildKind::Building;
                    for(int page=0;page<runtimeBuildPageCount(state,sidebar)&&!point;++page){
                        runtimeSetActiveBuildPage(state,page);
                        const auto visible=runtimeVisibleBuildCatalogIndices(state,sidebar);
                        for(std::size_t slot=0;slot<visible.size();++slot)
                            if(state.economy.catalog[static_cast<std::size_t>(visible[slot])].typeId=="GAPOWR"){
                                const RECT button=runtimeBuildButtonRect(sidebar,static_cast<int>(slot));
                                point=POINT{(button.left+button.right)/2,(button.top+button.bottom)/2};break;}
                    }
                    if(!need(point.has_value(),"actual ready/unfinished GAPOWR button"))return;
                    if(!need(runtimeBuildButtonAtPoint(state,sidebar,*point).has_value(),"button picker resolves actual catalog"))return;
                }else{
                    for(LONG y=sidebar.bottom-2;y>sidebar.top&&!point;--y)
                        for(LONG x=sidebar.left+1;x<sidebar.right-1;++x)
                            if(!runtimeBuildButtonAtPoint(state,sidebar,{x,y})){point=POINT{x,y};break;}
                    if(!need(point.has_value(),"actual blank sidebar point"))return;
                    if(kind==2)state.activeSidebarTool=RuntimeSidebarTool::Repair;
                }
                const auto production=runtimeSerializeProductionLanes(state.economy.localProductionLanes);
                save("sidebar-precondition",0);send(WM_RBUTTONDOWN,*point,MK_RBUTTON);
                need(!state.rightPointer.active,"sidebar never starts map drag");
                send(WM_RBUTTONUP,*point,0);
                if(kind<2)need(runtimeSerializeProductionLanes(state.economy.localProductionLanes)!=production,"secondary ready/queued production actually changes original lane");
                if(kind==2)need(state.activeSidebarTool==RuntimeSidebarTool::None,"blank sidebar cancels active tool");
        });
    }
    for(int position=0;position<4;++position){
        runCase("inclusive-viewport-"+std::to_string(position),*normal,0,[&](auto& state,auto& window,auto&,const Send& send,const Save&,const Tick&){
            const RECT view=freshViewport(window);
            const POINT point=position==0?POINT{view.left,view.top}:position==1?POINT{view.right,view.bottom}:
                position==2?POINT{view.left-1,view.top}:POINT{-32768,-32768};
            send(WM_RBUTTONDOWN,point,MK_RBUTTON);
            RECT actual{};GetClientRect(window.window,&actual);
            const bool expected=pointInRectInclusive(view,point)&&!pointInRectInclusive(runtimeSidebarRect(actual),point);
            need(state.rightPointer.active==expected,"original inclusive viewport, sidebar precedence and signed mouse coordinate boundary");
            send(WM_RBUTTONUP,point,0);
        });
    }
    runCase("plain-empty-map-cancel",*normal,0,[&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
        const auto point=blankPoint(state,window);if(!need(point.has_value(),"actual unoccupied projected map point"))return;
        save("empty-map-picked",0);clicked(send,*point);
        need(state.selectedObjectIds.empty()&&state.interactionStatus=="selection canceled","ordinary empty map right click cancels rather than issue move");
    });
    runCase("selected-object-falls-to-cancel",*normal,0,[&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
        const int id=state.selectedObjectIds.front();const auto point=pointForObject(state,window,id);
        if(!need(point.has_value(),"actual selected object opaque hit"))return;
        invalidateRuntimeObjectProjectionCache(state);save("cold-selected-object",0);clicked(send,*point);
        need(state.selectedObjectIds.empty()&&state.interactionStatus=="selection canceled","selected object cannot become a primary target");
    });
    for(int mode=0;mode<4;++mode){
        runCase("actual-primary-object-route-"+std::to_string(mode),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto point=enemyTarget(state,window);if(!need(point.has_value(),"actual original human enemy opaque hit"))return;
                const RECT view=freshViewport(window);const auto& projected=runtimeProjectedObjectsForViewport(state,view);
                const auto* hit=findProjectedObjectAtPoint(projected,*point);
                if(!need(hit&&hit->object,"actual enemy picker result"))return;const int targetId=hit->object->instanceId;
                const auto selected=state.selectedObjectIds;const auto queue=state.playerCommands.serialize();
                const auto world=state.world.deterministicSimulationStateHash();
                invalidateRuntimeObjectProjectionCache(state);save("cold-real-command-target",targetId);clicked(send,*point);
                if(mode==0)need(witness.commands.empty()&&state.lastCommand.valid&&state.lastCommand.acceptedCount>0,"old direct authority actually consumes primary command");
                else{
                    if(!need(witness.commands.size()==1,"one complete primary gameplay envelope"))return;
                    RuntimeMultiplayerGameplayCommand command;std::string error;
                    if(!need(decodeRuntimeMultiplayerGameplayCommand(witness.commands.front().payload,command,error),"independent existing wire decoder"))return;
                    const auto* body=std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&command.body);
                    const auto* target=body?std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&body->target):nullptr;
                    need(body&&body->order==RuntimeMultiplayerGameplayTargetOrderKind::Primary&&body->objectIds==selected
                        &&target&&target->objectId==targetId,"owned selected IDs and actual target ID in complete Primary envelope");
                    if(mode==1)need(witness.commands.front().accepted&&witness.commands.front().applied
                        &&state.lastCommand.acceptedCount>0,"offline router callback actually applied");
                    if(mode==2){
                        need(witness.commands.front().accepted&&!witness.commands.front().applied&&state.playerCommands.serialize()==queue
                            &&state.world.deterministicSimulationStateHash()==world,"capture-only leaves authority untouched before real replay");
                        need(witness.replay(error)&&state.lastCommand.acceptedCount>0,"captured Primary actually decoded and replayed: "+error);
                        save("actual-primary-replay",0);++replayedCommands;
                    }
                    if(mode==3){need(!witness.commands.front().accepted&&!witness.commands.front().applied&&state.selectedObjectIds.empty()
                        &&state.playerCommands.serialize()==queue,"capture rejection preserved and complete fallback cancel ran");++rejectedCommands;}
                }
                if(mode!=3)++acceptedCommands;++commandCases;
        });
    }
    runCase("invalid-selection-authority-rejection",*normal,1,[&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
        const auto point=enemyTarget(state,window);if(!need(point.has_value(),"real target for stale selection rejection"))return;
        state.selectedObjectIds={std::numeric_limits<int>::max()};const auto queue=state.playerCommands.serialize();
        save("stale-selection",0);clicked(send,*point);
        need(state.selectedObjectIds.empty()&&state.playerCommands.serialize()==queue
            &&std::none_of(witness.commands.begin(),witness.commands.end(),[](const auto& c){return c.applied;}),"stale owned ID rejected without authority, original fallback cancels");
        ++rejectedCommands;++commandCases;
    });

    const auto prepareWall=[&](RuntimeWindowState& state,Window& window)->std::optional<std::pair<POINT,RuntimeCellPoint>>{
        std::optional<RuntimeCellPoint> cell;bool authoredFixture=false;
        for(const auto& terrain:state.world.getTerrainCells())
            if(runtimeOverlayWallTargetAtCell(state,{terrain.x,terrain.y})){cell=RuntimeCellPoint{terrain.x,terrain.y};break;}
        if(!cell){
            int ordinal=-1;
            for(std::size_t index=0;index<state.rules.getOverlayTypes().size();++index){
                const auto& overlay=state.rules.getOverlayTypes()[index];const auto* building=state.rules.findBuildingType(overlay.id);
                if(overlay.wall&&building&&building->wall&&building->strength>0&&toUpperAsciiLocal(overlay.id)=="GAWALL"){ordinal=static_cast<int>(index);break;}
            }
            if(!need(ordinal>=0,"actual wall OverlayTypes ordinal and original BuildingType rule"))return std::nullopt;
            const auto* actor=findObjectById(state,state.selectedObjectIds.front());if(!need(actor!=nullptr,"actual selected actor for authored terrain fixture"))return std::nullopt;
            const int ax=actor->x,ay=actor->y;
            for(auto& terrain:state.world.accessTerrainCells()){
                const int distance=std::max(std::abs(terrain.x-ax),std::abs(terrain.y-ay));
                if(distance<7||distance>11||terrain.hasOverlay)continue;
                if(std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),[&](const auto& o){
                    return o.alive&&std::abs(o.x-terrain.x)<3&&std::abs(o.y-terrain.y)<3;}))continue;
                auto fact=std::find_if(state.mapFacts.cells.begin(),state.mapFacts.cells.end(),[&](const auto& f){return f.x==terrain.x&&f.y==terrain.y;});
                if(fact==state.mapFacts.cells.end())continue;
                const auto& overlay=state.rules.getOverlayTypes()[static_cast<std::size_t>(ordinal)];
                terrain.hasOverlay=true;terrain.overlayIndex=ordinal;terrain.overlayFrame=0;terrain.overlayType=terrain.overlayImage=overlay.id;
                terrain.overlayIsOre=terrain.overlayIsGem=false;
                initializeWorldOverlayWallState(terrain,state.rules);
                if(!fact->hasOverlay)++state.mapFacts.overlayCellCount;
                fact->hasOverlay=true;fact->overlay.cellIndex=terrain.cellIndex512;fact->overlay.x=terrain.x;fact->overlay.y=terrain.y;
                fact->overlay.overlayIndex=ordinal;fact->overlay.type=fact->overlay.image=overlay.id;
                fact->overlay.frameIndex=fact->overlay.frame=fact->overlay.overlayData=fact->overlay.fallbackFrame=0;
                state.mapFacts.contentHash^=0x10100001ULL;
                cell=RuntimeCellPoint{terrain.x,terrain.y};authoredFixture=true;break;
            }
            if(!need(cell.has_value(),"actual playable free terrain and original map fact for authored wall"))return std::nullopt;
            state.terrainByCell=buildRuntimeTerrainLookup(state.world);
            refreshRuntimeOverlayWallTopology(state);initializeRuntimeNavigation(state);
        }
        const auto target=runtimeOverlayWallTargetAtCell(state,*cell);
        if(!need(target.has_value(),"formal wall initializer/topology validates complete overlay identity"))return std::nullopt;
        bool selected=false;
        for(const auto& object:state.world.getObjects())if(object.alive&&object.owner==state.economy.owner&&object.kind!=WorldObjectKind::Structure){
            state.selectedObjectIds={object.instanceId};if(runtimeOverlayWallSelectionCanAttack(state,*target)){selected=true;break;}}
        if(!need(selected,"actual original owned armed unit eligible for overlay wall force-fire"))return std::nullopt;
        RECT rect{};GetClientRect(window.window,&rect);centerRuntimeCameraOnCell(state,rect,*cell);const RECT view=runtimeMapViewport(rect);
        invalidateRuntimeObjectProjectionCache(state);
        RuntimeMapRenderContext context;context.stableRenderConfiguration=true;
        context.theater=state.theaterLoaded?&state.theater:nullptr;context.art=state.artLoaded?&state.art:nullptr;
        context.rules=state.rulesLoaded?&state.rules:nullptr;context.mapFacts=state.mapFactsLoaded?&state.mapFacts:nullptr;
        context.ownerRemapColors=state.ownerRemapColors.empty()?nullptr:&state.ownerRemapColors;
        context.neutralRemapOwners=state.neutralRemapOwners.empty()?nullptr:&state.neutralRemapOwners;
        refreshRuntimeGateRenderSnapshots(state);refreshRuntimeSpecialMechanismRenderSnapshots(state);
        context.gateSnapshots=&state.gateRenderSnapshots;context.specialMechanismSnapshots=&state.specialMechanismRenderSnapshots;
        context.tileRootPath=runtimeTileRootPath(state.config,state.report.map);context.palettePath=runtimePalettePath(state.config,state.report.map);
        context.assetRootPath=runtimeAssetRootPath(state.config);context.structureRootPath=runtimeStructureRootPath(state.config,state.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(state.config,state.report.map);context.animationTick=state.tick;
        context.fixedTickHz=state.config.fixedTickHz;context.impactActors=runtimeImpactRenderActorsForRender(state);
        context.selectedObjectIds=&state.selectedObjectIds;context.playerCheerUntilTick=&state.playerCommunication.cheerUntilTick;
        std::unordered_map<int,std::vector<RuntimeSpriteHitSpan>> spans;
        {RuntimeGeometryQueryScope geometry(state.world,view,context,state.camera,state.renderCache,true);
            if(!need(resolveRuntimeBuildingVisibleHitSpans(state.world,view,context,state.camera,state.renderCache,spans),"actual no-paint wall hit span geometry"))return std::nullopt;}
        const auto found=spans.find(target->instanceId);
        if(!need(found!=spans.end(),"actual renderer emitted expected terrain-owned wall ID"))return std::nullopt;
        for(const auto& span:found->second)for(LONG x=span.left;x<span.right;++x){
            const POINT pixel{x,span.y};const auto& objects=runtimeProjectedObjectsForViewport(state,view);
            if(findProjectedObjectAtPoint(objects,pixel))continue;
            const auto visible=visibleTerrainWallTargetAtPoint(state,view,pixel);
            if(visible&&visible->instanceId==target->instanceId){
                trace<<"wallFixture path="<<window.path<<" kind="<<(authoredFixture?"authored-test-terrain":"natural-stock-observed")
                    <<" cell="<<cell->x<<','<<cell->y<<" originalIndex="<<target->instanceId<<" type="<<target->type<<" pixel="<<pixel.x<<','<<pixel.y<<'\n';
                ++geometryCases;return std::pair<POINT,RuntimeCellPoint>{pixel,*cell};
            }
        }
        need(false,"opaque wall pixel actually wins original wall target picker");return std::nullopt;
    };
    for(int mode:{0,1,2,3}){
        runCase("real-visible-wall-primary-"+std::to_string(mode),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto wall=prepareWall(state,window);if(!wall)return;
                const auto queue=state.playerCommands.serialize();save("actual-visible-wall",0);
                clicked(send,wall->first);
                if(mode==0)need(state.lastCommand.valid&&state.lastCommand.acceptedCount>0,"direct original wall revalidated and consumed ForceFireCell");
                else{
                    if(!need(witness.commands.size()==1,"one real visible-wall gameplay command"))return;
                    RuntimeMultiplayerGameplayCommand command;std::string error;
                    if(!need(decodeRuntimeMultiplayerGameplayCommand(witness.commands.front().payload,command,error),"wall envelope decoded"))return;
                    const auto* body=std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&command.body);
                    const auto* target=body?std::get_if<RuntimeMultiplayerGameplayCellTarget>(&body->target):nullptr;
                    need(body&&body->order==RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell&&target
                        &&target->cell.x==wall->second.x&&target->cell.y==wall->second.y,"Primary converted to actual current wall cell ForceFireCell");
                    if(mode==1)need(witness.commands.front().applied&&state.lastCommand.acceptedCount>0,"wall router actually applied");
                    if(mode==2){need(state.playerCommands.serialize()==queue&&!witness.commands.front().applied,"wall capture before authority");
                        need(witness.replay(error)&&state.lastCommand.acceptedCount>0,"wall actual replay consumed: "+error);save("wall-replayed",0);++replayedCommands;}
                    if(mode==3){need(!witness.commands.front().accepted&&state.playerCommands.serialize()==queue
                        &&state.selectedObjectIds.empty(),"wall rejection falls through complete cancel without authority");++rejectedCommands;}
                }
                if(mode!=3)++acceptedCommands;++commandCases;
        });
    }
    auto ordinaryLoad=makeBase(1,true,false);
    if(!expect(static_cast<bool>(ordinaryLoad),"original natural ordinary active three-house save fixture"))return false;
    runCase("ordinary-active21-load-fresh-root",*ordinaryLoad,1,
        [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
            state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
            if(!need(state.particlesInitialized&&state.particles.currentTick()==21,"real initialized tick21 particles"))return;
            state.rightPointer.active=true;state.rightPointer.moved=true;
            state.rightPointer.start={33,44};state.rightPointer.last={55,66};state.rightPointer.cursorDelta={7,-8};
            const RuntimeUiClock* borrowedClock=state.uiClock;
            const auto path=runtimeDefaultSavePath(state);std::string error;
            if(!need(!std::filesystem::exists(path),"new ordinary private save"))return;
            save("before-real-save",0);
            const bool saved=saveRuntimeState(state,path,error);const bool loaded=saved&&loadRuntimeState(state,path,error);
            if(!need(saved&&loaded&&state.tick==21&&state.uiClock==borrowedClock,"ordinary legal save/load retains root, tick and clock: "+error))return;
            need(!state.rightPointer.active&&state.rightPointer.moved&&state.rightPointer.start.x==33&&state.rightPointer.start.y==44
                &&state.rightPointer.last.x==55&&state.rightPointer.last.y==66&&state.rightPointer.cursorDelta.x==7&&state.rightPointer.cursorDelta.y==-8,
                "original load swaps only active, preserves other four nondefault gesture values");
            witness.attach();
            state.runtimeMenuOpen=false;state.playerCommunication.chatOpen=state.playerCommunication.diplomacyOpen=false;
            state.playerCommunication.selectedBeacon.reset();state.playerCommunication.beaconTargeting=false;
            state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
            state.leftPointer.active=state.leftPointer.moved=false;
            SetWindowPos(window.window,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
            state.camera.tileWidth=90;state.camera.tileHeight=45;invalidateRuntimeObjectProjectionCache(state);
            TrustedResourceRootScope freshInputResources(state.config,stock.config.localDataRootPath);
            const RECT view=freshViewport(window);state.cameraClampViewport=view;clampRuntimeMapCamera(state,view);
            const POINT point{(view.left+view.right)/2,(view.top+view.bottom)/2};
            SetFocus(window.window);SetCapture(window.window);need(GetCapture()==window.window,"post-load immediate own capture");
            save("after-real-load-fresh-state",0);send(WM_RBUTTONDOWN,point,MK_RBUTTON);
            send(WM_MOUSEMOVE,{point.x+36,point.y+18},MK_RBUTTON);send(WM_RBUTTONUP,{point.x+36,point.y+18},0);
            need(!state.rightPointer.active&&!state.rightPointer.moved&&state.interactionStatus=="camera panned"
                &&state.uiClock==borrowedClock&&state.tick==21&&witness.commands.empty(),"first right input after scratch commit uses current root/client and no gameplay authority");
            ++loadCases;
        });
    trace<<"summary checks="<<checks<<" triples="<<triples<<" steps="<<steps<<" sourceReads="<<sourceReads
        <<" captureCases="<<captureCases<<" geometryCases="<<geometryCases<<" guards="<<guardCases
        <<" commandCases="<<commandCases<<" accepted="<<acceptedCommands<<" rejected="<<rejectedCommands
        <<" replays="<<replayedCommands<<" loads="<<loadCases<<" failures="<<failures<<'\n';
    trace.flush();expect(static_cast<bool>(trace),"complete trace flush");
    output<<"Map-right-pointer integration "<<(failures==0?"passed":"failed")<<": checks="<<checks<<" oldCases="<<triples
        <<" directCases="<<triples<<" nativeCases="<<triples<<" steps="<<steps<<" sourceReads="<<sourceReads
        <<" captureCases="<<captureCases<<" geometryCases="<<geometryCases<<" guards="<<guardCases
        <<" commandCases="<<commandCases<<" accepted="<<acceptedCommands<<" rejected="<<rejectedCommands
        <<" replays="<<replayedCommands<<" loadCases="<<loadCases<<" failures="<<failures
        <<" frozenOldCompleteClosures=1 sameProductionCpp=1 actualHiddenWndProc=1 perRouteIndependentSequence=1"
        <<" fullStateCommandsTickAndOrderedCaptureTrace=1 sourceBeforeWindowDestruction=1"
        <<" ordinaryActiveThreeHouseLoad=1 loadSwapsOnlyActive=1 coldFreshGeometry=1 wallPixelPriority=1"
        <<" actualOfflineRouterConsume=1 captureThenActualReplay=1 physicalDesktop=outer-evidence"
        <<" fullRunningMultiplayer=not-run existingProjectionHistoryFailure=preserved existingTibspawnerFailure=preserved\n";
    return failures==0;
}
