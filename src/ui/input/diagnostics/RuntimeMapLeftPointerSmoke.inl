// Candidate only. Complete independently frozen old owners, with explicit type/member/name adapters.
namespace RuntimeMapLeftPointerSmokeFrozenOld {
bool beginRuntimeMenuSliderDrag(
    RuntimeWindowState& state,
    const RECT& clientRect,
    RuntimeMenuAction action,
    POINT point
) {
    const std::optional<RuntimeMenuControl> control =
        runtimeMenuControlForAction(state, clientRect, action);
    if (!control.has_value()
        || control->kind != RuntimeMenuControlKind::Slider) {
        return false;
    }

    const RECT valueRect = runtimeMenuControlValueRect(*control);
    if (!PtInRect(&valueRect, point)) {
        return false;
    }

    state.runtimeMenuPressed = action;
    state.runtimeMenuSliderDrag = action;
    (void)setRuntimeMenuSliderFromX(state, clientRect, action, point.x);
    return true;
}

bool updateRuntimeMenuSliderDrag(
    RuntimeWindowState& state,
    const RECT& clientRect,
    int clientX
) {
    if (state.runtimeMenuSliderDrag == RuntimeMenuAction::None) {
        return false;
    }
    return setRuntimeMenuSliderFromX(
        state,
        clientRect,
        state.runtimeMenuSliderDrag,
        clientX
    );
}

bool finishRuntimeMenuSliderDrag(
    RuntimeWindowState& state,
    const RECT& clientRect,
    int clientX
) {
    if (state.runtimeMenuSliderDrag == RuntimeMenuAction::None) {
        return false;
    }
    (void)RuntimeMapLeftPointerSmokeFrozenOld::updateRuntimeMenuSliderDrag(state, clientRect, clientX);
    const RuntimeMenuAction completedAction = state.runtimeMenuSliderDrag;
    state.runtimeMenuSliderDrag = RuntimeMenuAction::None;
    state.runtimeMenuPressed = RuntimeMenuAction::None;
    applyRuntimeMenuSliderRuntimeEffects(state, completedAction);
    state.runtimeMenuStatus.clear();
    (void)persistRuntimeMenuPreferences(state);
    return true;
}




bool handleRuntimePlayerWaypointClick(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    auto& local = state.playerLocalCommands;
    if (!local.planning || local.followTargeting || local.rallyTargeting
        || state.playerCommunication.beaconTargeting || state.activeSidebarTool != RuntimeSidebarTool::None) return false;
    const auto owner = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    std::uint64_t nearestId = 0;
    long long nearestDistance = 145;
    bool canClose = false;
    for (const auto& [id, route] : state.playerCommands.waypoints.routes) {
        if (route.owner != owner) continue;
        for (std::size_t i = 0; i < route.points.size(); ++i) {
            const auto position = runtimePlayerWaypointScreenPoint(state, viewport, route.points[i]);
            if (!position) continue;
            const long long dx = point.x - position->x, dy = point.y - position->y;
            const long long distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestId = route.points[i].id; nearestDistance = distance;
                canClose = !route.loopStart && i + 1 < route.points.size() && selectionContains(state, id);
            }
        }
    }
    local.selectedWaypoint = nearestId;
    if (!nearestId) return false;
    if (canClose) dispatchRuntimePlayerWaypointFromUi(state, RuntimePlayerWaypointOperation::CloseLoop, nearestId);
    state.leftPointer.active = state.leftPointer.moved = false;
    state.repaintRequested = true;
    return true;
}

bool handleRuntimePlayerTargetClick(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    auto& local = state.playerLocalCommands;
    if (!pointInRectInclusive(viewport, point)) return false;
    const POINT targetCell = approximateCellFromScreenPoint(state, viewport, point);
    if (state.playerCommunication.beaconTargeting) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
        command.kind = RuntimePlayerCommunicationKind::PlaceBeacon;
        command.cell = {targetCell.x, targetCell.y};
        if (dispatchRuntimePlayerCommunicationFromUi(state, command)) {
            state.playerCommunication.beaconTargeting = false;
            applyRuntimePlayerSelection(state,{},false,false);
            state.playerCommunication.selectedBeacon.reset();
        }
        state.leftPointer.active = state.leftPointer.moved = false;
        state.repaintRequested = true;
        return true;
    }
    if (RuntimeMapLeftPointerSmokeFrozenOld::handleRuntimePlayerWaypointClick(state, viewport, point)) return true;
    if (!local.followTargeting && !local.rallyTargeting && state.activeSidebarTool==RuntimeSidebarTool::None
        && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000) && !(GetKeyState(VK_SHIFT)&0x8000)) {
        const auto owner=runtimePlayerCommunicationViewerOwner(state);
        for (const auto& beacon:state.playerCommunication.beacons) {
            if (beacon.owner!=owner || !runtimePlayerBeaconVisibleToLocal(state,beacon)) continue;
            // The input path resolves ground cell centers. Native selection is
            // within half a cell of the beacon's stored ground point.
            const double dx=static_cast<double>(targetCell.x)-beacon.cell.x;
            const double dy=static_cast<double>(targetCell.y)-beacon.cell.y;
            if (dx*dx+dy*dy<0.25) {
                applyRuntimePlayerSelection(state,{},false,false);
                state.playerCommunication.selectedBeacon=beacon.cell;
                state.leftPointer.active=state.leftPointer.moved=false;
                state.repaintRequested=true;
                return true;
            }
        }
        state.playerCommunication.selectedBeacon.reset();
    }
    if (!local.followTargeting && !local.rallyTargeting) return false;
    if (local.rallyTargeting) {
        if (state.selectedObjectIds.size() == 1)
            dispatchRuntimePlayerFactoryFromUi(state, state.selectedObjectIds.front(),
                RuntimeMultiplayerGameplayCell{targetCell.x, targetCell.y});
        local.rallyTargeting = false;
    } else {
        const auto& projected = runtimeProjectedObjectsForViewport(state, viewport);
        const auto* picked = findProjectedObjectAtPoint(projected, point);
        if (picked && picked->object) dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow,
            {targetCell.x, targetCell.y}, picked->object);
        else state.interactionStatus = "follow requires an allied unit";
        local.followTargeting = false;
    }
    state.leftPointer.active = false;
    state.leftPointer.moved = false;
    state.repaintRequested = true;
    return true;
}

bool submitRuntimeSuperWeaponTargetAtPoint(
    RuntimeWindowState& state,
    const RECT& viewport,
    POINT point
) {
    ensureRuntimeSuperWeaponsInitialized(state);
    if (runtimeUsesGameplayIntentRouter(state)) {
        const auto& activeIntent =
            state.multiplayerIntentRouter.superWeapon();
        if (!activeIntent.has_value()) {
            return false;
        }
        const POINT targetPoint = approximateCellFromScreenPoint(
            state,
            viewport,
            point
        );
        RuntimeMultiplayerGameplaySuperWeaponTarget target;
        target.cell = {targetPoint.x, targetPoint.y};
        const std::vector<RuntimeProjectedObject>& projectedObjects =
            runtimeProjectedObjectsForViewport(state, viewport);
        const RuntimeProjectedObject* projectedObject =
            findProjectedObjectAtPoint(projectedObjects, point);
        if (projectedObject != nullptr
            && projectedObject->object != nullptr
            && !runtimeSuperWeaponTargetsGroundArea(state, activeIntent->superWeaponId)) {
            target.objectId = projectedObject->object->instanceId;
        }
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        std::string errorMessage;
        const bool accepted =
            state.multiplayerIntentRouter.targetSuperWeapon(
                std::move(target),
                errorMessage
            );
        if (accepted) {
            const auto& remaining =
                state.multiplayerIntentRouter.superWeapon();
            if (remaining && remaining->expectedTargetCount == 2
                && remaining->targets.size() == 1) {
                beginRuntimeLocalChronosphereMarker(state,
                    remaining->superWeaponId,
                    {remaining->targets.front().cell.x, remaining->targets.front().cell.y});
            } else if (!remaining) {
                clearRuntimeLocalChronosphereMarker(state);
            }
            state.interactionStatus = remaining.has_value()
                ? "superweapon choose destination"
                : (state.multiplayerBridge ? "superweapon launch queued for multiplayer" : "superweapon launched");
        } else {
            state.interactionStatus =
                "superweapon target rejected: " + errorMessage;
            emitRuntimeSound(state, "blocked");
        }
        state.leftPointer.active = false;
        state.leftPointer.moved = false;
        return true;
    }
    const std::string owner = toLowerAsciiLocal(
        trimAsciiLocal(state.economy.owner)
    );
    const SuperWeaponSidebarSnapshot* activeSuperWeapon = nullptr;
    for (const SuperWeaponSidebarSnapshot& snapshot :
            state.superWeapons.sidebarSnapshots()) {
        if (snapshot.targetSelectionActive
            && (owner.empty()
                || toLowerAsciiLocal(trimAsciiLocal(snapshot.owner)) == owner)) {
            activeSuperWeapon = &snapshot;
            break;
        }
    }
    if (activeSuperWeapon == nullptr) {
        return false;
    }
    const std::string activeSuperWeaponOwner = activeSuperWeapon->owner;
    const std::string activeSuperWeaponId = activeSuperWeapon->superWeaponId;

    const POINT targetPoint = approximateCellFromScreenPoint(
        state,
        viewport,
        point
    );
    int objectId = -1;
    const std::vector<RuntimeProjectedObject>& projectedObjects =
        runtimeProjectedObjectsForViewport(state, viewport);
    const RuntimeProjectedObject* projectedObject =
        findProjectedObjectAtPoint(projectedObjects, point);
    if (projectedObject != nullptr && projectedObject->object != nullptr
        && !runtimeSuperWeaponTargetsGroundArea(state, activeSuperWeaponId)) {
        objectId = projectedObject->object->instanceId;
    }

    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    const std::vector<std::array<int, 4>> parasiteHostPoses =
        runtimeParasiteLinkedHostPoses(state);
    std::string reason;
    const bool accepted = state.superWeapons.submitTarget(
        activeSuperWeaponOwner,
        activeSuperWeaponId,
        {{targetPoint.x, targetPoint.y}, objectId},
        context,
        &reason
    );
    (void)notifyRuntimeParasiteHostsRelocated(state, parasiteHostPoses);
    (void)drainRuntimeObjectExitQueue(state);
    if (accepted) {
        (void)flushRuntimeSuperWeaponHostEvents(state);
        const SuperWeaponRuntimeState nextState = state.superWeapons.stateOf(
            activeSuperWeaponOwner,
            activeSuperWeaponId
        );
        state.interactionStatus =
            nextState == SuperWeaponRuntimeState::AwaitingSecondTarget
                ? "superweapon choose destination"
                : "superweapon launch queued";
    } else {
        state.interactionStatus = "superweapon target rejected: " + reason;
        state.economy.status = state.interactionStatus;
        if (!state.runtimeSuppressPlayerAudioFeedback) {
            emitRuntimeSound(state, "blocked");
        }
    }
    state.leftPointer.active = false;
    state.leftPointer.moved = false;
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
    case WM_LBUTTONDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr || !state->world.getSummary().loaded) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        RECT clientRect;
        GetClientRect(window, &clientRect);
        const POINT point = pointFromMouseMessage(lParam);
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        if (!pointInRectInclusive(clientRect, point)) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (state->runtimeMenuOpen) {
            const RuntimeMenuAction action = runtimeMenuActionAtPoint(
                *state,
                clientRect,
                point
            );
            state->runtimeMenuPressed = action;
            const std::vector<RuntimeMenuButton> buttons =
                runtimeMenuFocusableButtons(*state, clientRect);
            for (std::size_t index = 0; index < buttons.size(); ++index) {
                if (buttons[index].action == action) {
                    state->runtimeMenuFocus = static_cast<int>(index);
                    break;
                }
            }
            if (RuntimeMapLeftPointerSmokeFrozenOld::beginRuntimeMenuSliderDrag(
                    *state,
                    clientRect,
                    action,
                    point
                )) {
                SetCapture(window);
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (action != RuntimeMenuAction::None) {
                SetCapture(window);
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
        const RECT sidebar = runtimeSidebarRect(clientRect);
        if (pointInRectInclusive(sidebar, point)) {
            const RECT radarRect = runtimeRadarRect(sidebar);
            const std::optional<RuntimeCellPoint> radarCell =
                runtimeRadarCellAtPoint(*state, radarRect, point);
            if (radarCell.has_value()) {
                centerRuntimeCameraOnCell(*state, clientRect, *radarCell);
                invalidateRuntimeObjectProjectionCache(*state);
                state->interactionStatus =
                    "radar jump "
                    + std::to_string(radarCell->x)
                    + ","
                    + std::to_string(radarCell->y);
                state->leftPointer.active = false;
                state->leftPointer.moved = false;
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            ensureRuntimeSuperWeaponsInitialized(*state);
            if (const SuperWeaponSidebarSnapshot* superWeapon =
                    runtimeSuperWeaponAtPoint(*state, sidebar, point)) {
                const std::string superWeaponOwner = superWeapon->owner;
                const std::string superWeaponId = superWeapon->superWeaponId;
                const std::string superWeaponName = superWeapon->name;
                SuperWeaponWorldContext superContext =
                    runtimeSuperWeaponContext(*state);
                superContext.emit = runtimeSuperWeaponEventSink(*state);
                std::string reason;
                bool beganTargeting = false;
                if (runtimeUsesGameplayIntentRouter(*state)) {
                    refreshRuntimeMultiplayerPlayerIntentAuthority(*state);
                    const SuperWeaponRule* rule =
                        state->rules.findSuperWeaponType(superWeaponId);
                    const std::string targetMode = rule == nullptr
                        ? std::string("cell")
                        : toLowerAsciiLocal(
                            trimAsciiLocal(rule->targetMode)
                        );
                    const std::size_t targetCount =
                        targetMode == "none"
                            || targetMode == "notarget"
                            || targetMode == "wholemap"
                            || targetMode == "map"
                        ? 0U
                        : (targetMode == "twostage"
                            || targetMode == "two-stage"
                            ? 2U
                            : 1U);
                    if (superWeapon->state
                            != SuperWeaponRuntimeState::Ready) {
                        reason = "not ready";
                    } else {
                        beganTargeting =
                            state->multiplayerIntentRouter.beginSuperWeapon(
                                superWeaponId,
                                targetCount,
                                reason
                            );
                        if (beganTargeting && targetCount == 0U) {
                            beganTargeting = state->multiplayerIntentRouter
                                .confirmSuperWeapon(reason);
                        }
                    }
                } else {
                    beganTargeting = state->superWeapons.beginTargetSelection(
                        superWeaponOwner,
                        superWeaponId,
                        superContext,
                        &reason
                    );
                }
                if (beganTargeting) {
                    clearRuntimeLocalChronosphereMarker(*state);
                    state->interactionStatus =
                        state->multiplayerBridge != nullptr
                            && !state->multiplayerIntentRouter
                                .superWeapon().has_value()
                        ? "superweapon launch queued for multiplayer"
                        : "superweapon target: " + superWeaponName;
                    emitRuntimeSound(
                        *state,
                        runtimeSoundIntentSidebarButtonClick
                    );
                } else {
                    state->interactionStatus =
                        "superweapon unavailable: " + reason;
                    const bool unfinished =
                        superWeapon->state
                            == SuperWeaponRuntimeState::Charging
                        || superWeapon->state
                            == SuperWeaponRuntimeState::Suspended;
                    if (!unfinished
                        && !state->runtimeSuppressPlayerAudioFeedback) {
                        emitRuntimeSound(*state, "blocked");
                    }
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

            const std::optional<RuntimeSidebarTool> sidebarTool =
                runtimeSidebarToolAtPoint(sidebar, point);
            if (sidebarTool.has_value()) {
                state->activeSidebarTool =
                    state->activeSidebarTool == *sidebarTool
                        ? RuntimeSidebarTool::None
                        : *sidebarTool;
                state->interactionStatus =
                    state->activeSidebarTool == RuntimeSidebarTool::None
                        ? "sidebar tool canceled"
                        : runtimeSidebarToolLabel(state->activeSidebarTool);
                emitRuntimeSound(
                    *state,
                    runtimeSoundIntentSidebarTabClick
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

            const std::optional<RuntimeBuildKind> tab =
                runtimeBuildTabAtPoint(*state, sidebar, point);
            if (tab.has_value()) {
                state->activeBuildTab = *tab;
                runtimeClampActiveBuildPage(*state, sidebar);
                state->interactionStatus =
                    "build tab " + runtimeBuildTabLabel(*tab);
                emitRuntimeSound(*state, runtimeSoundIntentSidebarTabClick);
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            const int pageDelta =
                runtimeBuildPageButtonAtPoint(*state, sidebar, point);
            if (pageDelta != 0) {
                const int pageCount = runtimeBuildPageCount(*state, sidebar);
                const int nextPage = std::clamp(
                    runtimeActiveBuildPage(*state) + pageDelta,
                    0,
                    pageCount - 1
                );
                runtimeSetActiveBuildPage(*state, nextPage);
                state->interactionStatus =
                    "build page " + std::to_string(nextPage + 1)
                    + "/" + std::to_string(pageCount);
                emitRuntimeSound(*state, runtimeSoundIntentSidebarButtonClick);
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }

            const std::optional<int> buttonIndex =
                runtimeBuildButtonAtPoint(*state, sidebar, point);
            if (buttonIndex.has_value()) {
                const RuntimeBuildCatalogItem& item =
                    state->economy.catalog[*buttonIndex];
                RuntimeProductionLane readyLane =
                    RuntimeProductionLane::Structures;
                if (const RuntimeProductionLaneOrder* ready =
                        runtimeReadyProductionForItem(
                            *state,
                            item,
                            &readyLane
                        )) {
                    const std::optional<RuntimeProductionCatalogAdapter>
                        catalog = runtimeProductionCatalogLookup(
                            *state,
                            ready->itemKey
                        );
                    if (catalog.has_value() && catalog->item != nullptr) {
                        beginRuntimePlacementFromUi(
                            *state,
                            RuntimeSelectedPlacement{
                                readyLane,
                                *ready,
                                catalog->item
                            },
                            true
                        );
                    }
                    state->interactionStatus =
                        "placing " + item.typeId;
                    emitRuntimeSound(*state, runtimeSoundIntentSidebarButtonClick);
                } else {
                    const bool addNext =
                        (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    dispatchRuntimeQueueProductionFromUi(
                        *state,
                        item,
                        addNext
                            ? RuntimeProductionInsertMode::AfterActive
                            : RuntimeProductionInsertMode::Back
                    );
                }
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

        if (rejectRuntimeObserverSelection(*state)) return 0;
        SetFocus(window);
        SetCapture(window);
        // Focus/capture can deliver nested messages. Restore this click's
        // modifiers and position before exposing the pressed cursor.
        {
            RuntimeForceAttackHeldUiHost forceHost(*state, window);
            handleRuntimeForceAttackHeld({RuntimeForceAttackEvent::PointerPressRestore,
                static_cast<std::uint64_t>(wParam)}, forceHost);
        }
        state->mouseClientPoint = point;
        state->repaintRequested = true;
        state->leftPointer.active = true;
        state->leftPointer.moved = false;
        state->leftPointer.start = {point.x, point.y};
        state->leftPointer.current = {point.x, point.y};
        state->interactionStatus = "selecting";
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
                    if (RuntimeMapLeftPointerSmokeFrozenOld::updateRuntimeMenuSliderDrag(
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
        {
            RuntimeMapRightPointerGestureUiHost rightHost(*state, window);
            if (handleRuntimeMapRightPointerMove({{point.x, point.y}}, rightHost)) return 0;
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
    case WM_LBUTTONUP:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        RECT clientRect;
        GetClientRect(window, &clientRect);
        const POINT point = pointFromMouseMessage(lParam);
        state->mouseClientPointValid = true;
        state->mouseClientPoint = point;
        if (state->runtimeMenuOpen) {
            if (RuntimeMapLeftPointerSmokeFrozenOld::finishRuntimeMenuSliderDrag(*state, clientRect, point.x)) {
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
                emitRuntimeSound(
                    *state,
                    runtimeSoundIntentSidebarButtonClick
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
            const RuntimeMenuAction released = runtimeMenuActionAtPoint(
                *state,
                clientRect,
                point
            );
            const RuntimeMenuAction pressed = state->runtimeMenuPressed;
            state->runtimeMenuPressed = RuntimeMenuAction::None;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            if (released == pressed && pressed != RuntimeMenuAction::None) {
                activateRuntimeMenuAction(window, *state, pressed);
            } else {
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
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        if (rejectRuntimeObserverSelection(*state)) return 0;
        const RECT viewport = runtimeMapViewport(clientRect);
        if (pointInRectInclusive(viewport, point)
            && RuntimeMapLeftPointerSmokeFrozenOld::submitRuntimeSuperWeaponTargetAtPoint(
                *state,
                viewport,
                point
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
        if (!state->leftPointer.active) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        state->leftPointer.current = {point.x, point.y};

        if (state->activeSidebarTool != RuntimeSidebarTool::None
            && pointInRectInclusive(viewport, point)) {
            const std::vector<RuntimeProjectedObject>& projectedObjects =
                runtimeProjectedObjectsForViewport(*state, viewport);
            const RuntimeProjectedObject* projectedObject =
                findProjectedObjectAtPoint(projectedObjects, point);
            bool toolHandled = false;
            if (projectedObject != nullptr
                && projectedObject->object != nullptr
                && projectedObject->object->kind == WorldObjectKind::Structure) {
                if (state->activeSidebarTool == RuntimeSidebarTool::Sell) {
                    toolHandled = dispatchRuntimeStructureToolFromUi(
                        *state,
                        RuntimeSidebarTool::Sell,
                        projectedObject->object->instanceId
                    );
                } else if (state->activeSidebarTool == RuntimeSidebarTool::Repair) {
                    toolHandled = dispatchRuntimeStructureToolFromUi(
                        *state,
                        RuntimeSidebarTool::Repair,
                        projectedObject->object->instanceId
                    );
                }
            } else {
                state->economy.status =
                    runtimeSidebarToolLabel(state->activeSidebarTool)
                    + " needs a structure";
            }
            if (toolHandled) {
                if (state->activeSidebarTool != RuntimeSidebarTool::Sell) {
                    state->activeSidebarTool = RuntimeSidebarTool::None;
                }
                state->interactionStatus = state->economy.status;
            } else {
                emitRuntimeSound(
                    *state,
                    containsAsciiCaseInsensitive(
                        state->economy.status,
                        "no credits"
                    )
                        ? runtimeSoundIntentInsufficientFunds
                        : runtimeSoundIntentSidebarScold
                );
            }
            state->leftPointer.active = false;
            state->leftPointer.moved = false;
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return toolHandled ? 0 : 0;
        }

        if (runtimeSelectedPlacement(*state).has_value()) {
            if (pointInRectInclusive(viewport, point)) {
                updateRuntimePlacementPreview(*state, viewport, point);
                if (runtimeUsesGameplayIntentRouter(*state)) {
                    const bool previewWasValid = runtimePlacementValidForUi(*state);
                    refreshRuntimeMultiplayerPlayerIntentAuthority(*state);
                    std::string errorMessage;
                    const bool accepted = state->multiplayerIntentRouter
                        .confirmPlacement(errorMessage);
                    noteRuntimeMultiplayerIntentResult(
                        *state,
                        accepted,
                        "building placement",
                        errorMessage,
                        previewWasValid ? runtimeSoundIntentSidebarScold
                            : runtimeSoundIntentCannotDeployHere
                    );
                } else {
                    placeRuntimeReadyBuilding(
                        *state,
                        runtimePlacementCellForUi(*state)
                    );
                }
            }
            state->leftPointer.active = false;
            state->leftPointer.moved = false;
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        const std::vector<RuntimeProjectedObject>& projectedObjects =
            runtimeProjectedObjectsForViewport(*state, viewport);
        std::vector<int> selectedIds;
        if (!state->leftPointer.moved && RuntimeMapLeftPointerSmokeFrozenOld::handleRuntimePlayerTargetClick(*state, viewport, point)) return 0;
        const bool append = (wParam & MK_SHIFT) != 0;
        const bool toggle = (wParam & MK_CONTROL) != 0;
        const bool forceFireClick = toggle && !state->selectedObjectIds.empty()
            && pointInRectInclusive(viewport, point);
        const bool altClick = (GetKeyState(VK_MENU) & 0x8000) != 0
            && !state->selectedObjectIds.empty() && pointInRectInclusive(viewport, point);
        const bool attackMoveClick = toggle && append && !altClick
            && !state->selectedObjectIds.empty() && pointInRectInclusive(viewport, point);
        if ((altClick || attackMoveClick) && !state->leftPointer.moved) {
            const auto* target = findProjectedObjectAtPoint(projectedObjects, point);
            const POINT cell = approximateCellFromScreenPoint(*state, viewport, point);
            const auto order = runtimePlayerModifiedTargetOrder(toggle, append, altClick,
                target != nullptr && target->object != nullptr);
            dispatchRuntimeTargetOrderFromUi(*state, order, {cell.x, cell.y},
                order == RuntimeMultiplayerGameplayTargetOrderKind::Follow ? target->object : nullptr);
        } else if (state->leftPointer.moved) {
            const RECT dragRect =
                normalizedRect(POINT{state->leftPointer.start.x, state->leftPointer.start.y}, POINT{state->leftPointer.current.x, state->leftPointer.current.y});
            for (const RuntimeProjectedObject& projectedObject : projectedObjects) {
                if (projectedObject.object != nullptr
                    && runtimeObjectCanBeBoxSelected(*projectedObject.object)
                    && rectsIntersect(dragRect, projectedObject.bounds)) {
                    selectedIds.push_back(projectedObject.object->instanceId);
                }
            }
            applyRuntimePlayerSelection(*state, selectedIds, append, toggle);
            state->playerCommunication.selectedBeacon.reset();
        } else {
            const RuntimeProjectedObject* projectedObject =
                findProjectedObjectAtPoint(projectedObjects, point);
            const bool hasMovableSelection =
                selectedMovableObjectCount(*state) > 0;
            const auto terrainWallTarget = (forceFireClick || (!append && !toggle))
                ? visibleTerrainWallTargetAtPoint(*state, viewport, point)
                : std::optional<WorldObjectInstance>{};
            if (forceFireClick) {
                const WorldObjectInstance* target = terrainWallTarget ? &*terrainWallTarget
                    : (projectedObject != nullptr ? projectedObject->object : nullptr);
                const POINT targetPoint =
                    approximateCellFromScreenPoint(*state, viewport, point);
                dispatchRuntimeTargetOrderFromUi(
                    *state,
                    target != nullptr
                        ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
                        : RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,
                    {targetPoint.x, targetPoint.y},
                    target
                );
                // Consume rejected force-fire too, preserving the selection.
            } else if (!append && !toggle && terrainWallTarget) {
                dispatchRuntimeTargetOrderFromUi(*state,
                    RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    {terrainWallTarget->x, terrainWallTarget->y}, &*terrainWallTarget);
            } else if (!append && !toggle
                && pointInRectInclusive(viewport, point)
                && (projectedObject == nullptr || projectedObject->object == nullptr)
                && runtimeSelectedFactoryCanSetRallyPoint(*state)) {
                const POINT cell = approximateCellFromScreenPoint(*state, viewport, point);
                dispatchRuntimePlayerFactoryFromUi(*state, state->selectedObjectIds.front(),
                    RuntimeMultiplayerGameplayCell{cell.x, cell.y});
            } else if (!append && !toggle && projectedObject != nullptr
                && projectedObject->object != nullptr
                && runtimeSelectedFactoryCanBePrimary(*state, *projectedObject->object)) {
                dispatchRuntimePlayerFactoryFromUi(*state, projectedObject->object->instanceId, std::nullopt);
            } else if (!append
                && !toggle
                && projectedObject != nullptr
                && projectedObject->object != nullptr
                && selectionContains(
                    *state,
                    projectedObject->object->instanceId
                )
                && dispatchRuntimeSecondaryActionFromUi(
                    *state,
                    projectedObject->object->instanceId
                )) {
                // handled secondary deploy/action
            } else if (!append
                && !toggle
                && hasMovableSelection
                && (projectedObject == nullptr
                    || projectedObject->object == nullptr
                    || !selectionContains(
                        *state,
                        projectedObject->object->instanceId
                    ))) {
                const POINT targetPoint =
                    approximateCellFromScreenPoint(*state, viewport, point);
                const RuntimeCellPoint targetCell{targetPoint.x, targetPoint.y};
                dispatchRuntimeTargetOrderFromUi(
                    *state,
                    RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    targetCell,
                    projectedObject != nullptr ? projectedObject->object : nullptr
                );
            } else if (projectedObject != nullptr
                && projectedObject->object != nullptr) {
                selectedIds.push_back(projectedObject->object->instanceId);
                applyRuntimePlayerSelection(*state, selectedIds, append, toggle);
            state->playerCommunication.selectedBeacon.reset();
            } else {
                applyRuntimePlayerSelection(*state, selectedIds, append, toggle);
            state->playerCommunication.selectedBeacon.reset();
            }
        }

        state->leftPointer.active = false;
        state->leftPointer.moved = false;
        state->repaintRequested = true;
        RedrawWindow(
            window,
            nullptr,
            nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
        );
        return 0;
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
    default: return DefWindowProcW(window,message,wParam,lParam);
    }
}
} // namespace RuntimeMapLeftPointerSmokeFrozenOld

namespace RuntimeMapLeftPointerSmokeDetail {
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
    switch(message) {
    case WM_LBUTTONDOWN: case WM_MOUSEMOVE: case WM_LBUTTONUP: {
        auto* state=reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(!state)return DefWindowProcW(window,message,wParam,lParam);
        const POINT point=pointFromMouseMessage(lParam);RuntimeMapLeftPointerUiHost host(*state,window);
        const RuntimeMapLeftPointerInput input{{point.x,point.y},static_cast<std::uint64_t>(wParam)};
        const bool handled=message==WM_LBUTTONDOWN?handleRuntimeMapLeftPointerDown(input,host)
            :message==WM_MOUSEMOVE?handleRuntimeMapLeftPointerMove(input,host):handleRuntimeMapLeftPointerUp(input,host);
        return handled?LRESULT{0}:DefWindowProcW(window,message,wParam,lParam);
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
    default:return DefWindowProcW(window,message,wParam,lParam);
    }
}

inline constexpr const wchar_t* windowClassName=L"ConcordMapLeftPointerSmoke";
struct Window;
thread_local std::map<HWND,Window*> windows;
LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
bool ownedMessage(UINT message,WPARAM raw){
    return message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE
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
        window=CreateWindowExW(0,windowClassName,L"Left pointer private contract",WS_POPUP,
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
        e.active=state.leftPointer.active;e.moved=state.leftPointer.moved;e.tick=state.tick;events.push_back(e);
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
            if(path==0)result=RuntimeMapLeftPointerSmokeFrozenOld::frozenWindowProc(window,message,raw,flags);
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
} // namespace RuntimeMapLeftPointerSmokeDetail

bool runRuntimeMapLeftPointerSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    using RuntimeMapLeftPointerSmokeDetail::Window;
    std::size_t checks=0,failures=0,triples=0,steps=0,sourceReads=0,loadCases=0;
    std::size_t commandCases=0,acceptedCommands=0,rejectedCommands=0,replayedCommands=0;
    std::size_t captureCases=0,geometryCases=0,guardCases=0;
    const auto expect=[&](bool value,const std::string& label){++checks;if(!value){++failures;
        output<<"Map-left-pointer failure: "<<label<<'\n';}return value;};
    char* directoryValue=nullptr;std::size_t directoryLength=0;
    if(_dupenv_s(&directoryValue,&directoryLength,"CONCORD_MAP_LEFT_POINTER_OUTPUT")!=0||directoryValue==nullptr)
        return expect(false,"explicit private output directory");
    const std::filesystem::path directory(directoryValue);std::free(directoryValue);
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    if(!expect(!ec&&std::filesystem::is_directory(directory),"private output root"))return false;
    const auto tracePath=directory/"left-pointer-trace.txt";
    if(!expect(!std::filesystem::exists(tracePath),"trace must be new"))return false;
    std::ofstream trace(tracePath,std::ios::binary|std::ios::out);
    if(!expect(static_cast<bool>(trace),"new trace"))return false;
    const RECT client{0,0,1024,768};const RECT viewport=runtimeMapViewport(client);
    const POINT center{(viewport.left+viewport.right)/2,(viewport.top+viewport.bottom)/2};
    RuntimeMapLeftPointerSmokeDetail::ClassRegistration registration;
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
                if(route==3){error="left-pointer fixture capture refused";c.accepted=false;}
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
        // Invalid cache rows retain owned geometry/pointer presence; -1 means no valid borrowed object identity.
        for(const auto& p:state.projectedObjectsCache)s<<';'<<(state.projectedObjectsCacheValid&&p.object?p.object->instanceId:-1)<<':'<<(p.object!=nullptr)<<':'<<p.bounds.left<<','<<p.bounds.top<<','<<p.bounds.right<<','<<p.bounds.bottom
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
    const auto makeBase=[&](int productionMode,bool threePlayers=false,bool movingUnits=false,const std::string& country="Americans"){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config;state.config.gameMode="Battle";state.config.gameModeRulesOverride.clear();
        state.config.computerAiEnabled=false;state.config.multiplayerActive=false;
        state.config.noCooldown=false;state.config.startingCredits=100000;state.config.startingUnits=movingUnits?2:0;
        state.config.playerCountry=country;state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.config.playerSlots.assign(threePlayers?3:2,{});
        for(std::size_t index=0;index<state.config.playerSlots.size();++index){
            auto& slot=state.config.playerSlots[index];slot.enabled=slot.human=true;slot.local=index==0;
            slot.country=index==0?country:(index%2?"Russians":"Americans");slot.team="-";slot.startPosition=static_cast<int>(index)+1;
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
        state.leftPointer.active=state.leftPointer.moved=false;state.repaintRequested=false;state.interactionStatus="left-pointer initial";
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
                const bool result=advanceRuntimeRightMousePan(state);
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
    const auto clicked=[&](const Send& send,POINT point){send(WM_LBUTTONDOWN,point,MK_LBUTTON);send(WM_LBUTTONUP,point,0);};
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
            const POINT p{x,y};if(!findProjectedObjectAtPoint(projected,p)&&!visibleTerrainWallTargetAtPoint(state,view,p)){
                const POINT cell=approximateCellFromScreenPoint(state,view,p);
                if(findRuntimeTerrainCellAt(state,{cell.x,cell.y})!=nullptr)return p;}}
        return std::nullopt;};


    // Actual native geometry and every capture observation are evaluated before HWND destruction.
    const auto menuPoint=[&](RuntimeWindowState& state,Window& window,RuntimeMenuAction action)->std::optional<POINT>{
        RECT rect{};GetClientRect(window.window,&rect);
        for(LONG y=0;y<rect.bottom;y+=3)for(LONG x=0;x<rect.right;x+=3)
            if(runtimeMenuActionAtPoint(state,rect,{x,y})==action)return POINT{x,y};
        return std::nullopt;
    };
    const auto ownClient=[](Window& window){RECT rect{};GetClientRect(window.window,&rect);return rect;};
    const auto playablePoint=[&](RuntimeWindowState& state,Window& window)->std::optional<POINT>{
        const RECT rect=ownClient(window),view=runtimeMapViewport(rect);
        const auto projection=buildRuntimeRadarProjection(state);const auto bounds=runtimeCameraWorldBounds(state);
        std::optional<RuntimeCellPoint> best;long long nearest=std::numeric_limits<long long>::max();
        for(const auto& cell:state.world.getTerrainCells()){
            if(!runtimeRadarTerrainCellIntersectsProjection(projection,cell))continue;
            const auto projected=runtimeProjectCellXY(cell.x,cell.y,cell.level,state.camera);
            const long long distance=std::abs(2LL*projected.x-bounds.left-bounds.right)
                +std::abs(2LL*projected.y-bounds.top-bounds.bottom);
            if(distance<nearest){nearest=distance;best=RuntimeCellPoint{cell.x,cell.y};}
        }
        if(!bounds.valid||!best)return std::nullopt;
        centerRuntimeCameraOnCell(state,rect,*best);state.cameraClampViewport=view;clampRuntimeMapCamera(state,view);
        const auto point=runtimeScreenPointForCell(state,view,*best,terrainLevelAtRuntimeCell(state,best->x,best->y));
        return pointInRectInclusive(view,point)?std::optional<POINT>(point):std::nullopt;
    };
    const auto actualBuild=[&](RuntimeWindowState& state,const std::string& type){
        const auto* pointer=findRuntimeGameplayCatalogItem(state,state.economy.owner,type);
        if(!pointer)return false;const RuntimeBuildCatalogItem item=*pointer;
        if(!queueRuntimeBuildOrder(state,item))return false;
        ++state.tick;advanceRuntimeProductionEconomy(state);
        if(!runtimeReadyProductionForItem(state,item)){
            if(!completeRuntimeActiveProductionForProbe(state,item))return false;
            ++state.tick;advanceRuntimeProductionEconomy(state);
        }
        const auto ready=runtimeOldestReadyPlacement(state);
        if(!ready||!ready->item||ready->item->typeId!=type)return false;
        if(!beginRuntimePlacementFromUi(state,*ready,true))return false;
        const auto cell=findRuntimePlacementCellNearBase(state,item);if(!cell)return false;
        const auto count=state.world.getObjects().size();if(!placeRuntimeReadyBuilding(state,*cell)
            ||state.world.getObjects().size()<=count)return false;
        for(int frame=0;frame<state.config.fixedTickHz*20;++frame){++state.tick;advanceRuntimeSimulation(state,false);}
        completeRuntimeWorldRefresh(state);return true;
    };
    trace<<"modifierObservation control="<<((GetKeyState(VK_CONTROL)&0x8000)!=0)<<" shift="<<((GetKeyState(VK_SHIFT)&0x8000)!=0)<<" alt="<<((GetKeyState(VK_MENU)&0x8000)!=0)<<" physicalInjection=none\n";
    for(int guard=0;guard<6;++guard){
        runCase("prefix-guard-"+std::to_string(guard),*normal,0,[&](auto& state,auto&,auto&,const Send& send,const Save& save,const Tick&){
            if(guard==0)state.world=decltype(state.world){};
            if(guard==1){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
            if(guard==2){state.skirmishMatchFinished=true;state.resultPresentation.bannerVisible=true;}
            if(guard==3){state.playerCommunication.chatOpen=true;state.playerCommunication.chatDraft=u"kept modal";}
            if(guard==4){state.playerCommunication.diplomacyOpen=true;state.playerCommunication.diplomacyPressedControl=3;}
            if(guard==5)state.multiplayerLocalSpectator=true;
            save("guard-before",0);send(WM_LBUTTONDOWN,center,MK_LBUTTON);
            send(WM_MOUSEMOVE,{center.x+8,center.y},MK_LBUTTON);send(WM_LBUTTONUP,center,0);
            need(!state.leftPointer.active&&!state.leftPointer.moved,"real outer guard suppresses completed map gesture");
        });++guardCases;
    }
    for(int delta:{0,4,5,-5})for(unsigned buttons:{0U,unsigned(MK_CONTROL),unsigned(MK_SHIFT),unsigned(MK_CONTROL|MK_SHIFT)}){
        runCase("threshold-"+std::to_string(delta)+"-"+std::to_string(buttons),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                const auto point=playablePoint(state,window);if(!need(point.has_value(),"actual playable centre"))return;
                state.selectedObjectIds.clear();save("empty-start",0);
                send(WM_LBUTTONDOWN,*point,MK_LBUTTON|buttons);
                need(state.leftPointer.active&&!state.leftPointer.moved,"down starts current gesture after real capture");
                need(state.forceAttackHeld==((buttons&MK_CONTROL)!=0),"pointer snapshot restored after SetFocus/SetCapture callbacks");
                send(WM_MOUSEMOVE,{point->x+delta,point->y},MK_LBUTTON|buttons);
                need(state.leftPointer.moved==(std::abs(delta)>4),"original strict squared-distance threshold 16");
                send(WM_MOUSEMOVE,*point,MK_LBUTTON|buttons);
                need(state.leftPointer.moved==(std::abs(delta)>4),"drag latch remains moved after return to origin");
                send(WM_LBUTTONUP,*point,buttons);
                need(!state.leftPointer.active&&!state.leftPointer.moved,"completed up clears only original left flags");
            });
    }
    for(int mode=0;mode<4;++mode){
        runCase("outside-no-down-"+std::to_string(mode),*normal,0,[&](auto& state,auto& window,auto&,const Send& send,const Save&,const Tick&){
            const RECT rect=ownClient(window);const POINT point=mode==0?POINT{-32768,-32768}:mode==1?POINT{rect.right+1,20}:center;
            if(mode<2){send(WM_LBUTTONDOWN,point,MK_LBUTTON);need(!state.leftPointer.active,"outside-client down does not begin selection");}
            if(mode==3){send(WM_LBUTTONDOWN,center,MK_LBUTTON);window.nestedCancel=true;window.nestedResize=true;++captureCases;}
            send(WM_LBUTTONUP,point,0);
            need(!state.leftPointer.active,"bare/reentrant up leaves no completed gesture");
        });
    }
    for(const auto action:{RuntimeMenuAction::Resume,RuntimeMenuAction::OpenSound,RuntimeMenuAction::OpenDisplay,RuntimeMenuAction::OpenKeyboard}){
        runCase("menu-button-"+std::to_string(static_cast<int>(action)),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                ReleaseCapture();
                if(!need(GetCapture()==nullptr,"happy menu setup starts without the original owned capture"))return;
                state.runtimeMenuOpen=true;state.runtimeMenuPage=action==RuntimeMenuAction::Resume?RuntimeMenuPage::Main:RuntimeMenuPage::Settings;
                const auto point=menuPoint(state,window,action);if(!need(point.has_value(),"real visible menu action"))return;
                save("menu-open",0);send(WM_LBUTTONDOWN,*point,MK_LBUTTON);
                need(state.runtimeMenuPressed==action&&GetCapture()==window.window,"actual pressed menu and own capture");
                send(WM_MOUSEMOVE,*point,MK_LBUTTON);send(WM_LBUTTONUP,*point,0);
                need(state.runtimeMenuPressed==RuntimeMenuAction::None,"pressed value cleared before synchronous release/activation");
                need(!state.leftPointer.active,"menu keeps map selection gate closed");
            });++captureCases;
    }
    for(const auto action:{RuntimeMenuAction::AdjustMusicVolume,RuntimeMenuAction::AdjustSoundVolume,RuntimeMenuAction::AdjustVoiceVolume}){
        runCase("menu-slider-"+std::to_string(static_cast<int>(action)),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                ReleaseCapture();
                if(!need(GetCapture()==nullptr,"happy slider setup starts without the original owned capture"))return;
                state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Sound;
                state.runtimePreferencePersistenceEnabled=true;const RECT rect=ownClient(window);
                const auto control=runtimeMenuControlForAction(state,rect,action);
                if(!need(control&&control->kind==RuntimeMenuControlKind::Slider,"actual sound slider definition"))return;
                const RECT value=runtimeMenuControlValueRect(*control);
                const POINT point{value.left+1,(value.top+value.bottom)/2};
                save("private-slider-before",0);send(WM_LBUTTONDOWN,point,MK_LBUTTON);
                need(state.runtimeMenuSliderDrag==action&&GetCapture()==window.window,"real slider owns capture");
                send(WM_MOUSEMOVE,{value.right-2,point.y},MK_LBUTTON);send(WM_LBUTTONUP,{value.right-2,point.y},0);
                need(state.runtimeMenuSliderDrag==RuntimeMenuAction::None&&state.runtimeMenuPressed==RuntimeMenuAction::None,
                    "slider finish/persist precedes original capture callback");
                need(std::filesystem::exists(state.config.configPath),"exact registered private preference file retained");
            });++captureCases;
    }
    runCase("menu-already-own-recapture-button",*normal,0,
        [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
            if(!need(GetCapture()==window.window,"same-window button reentry starts with actual owned capture"))return;
            state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Settings;
            const auto point=menuPoint(state,window,RuntimeMenuAction::OpenSound);
            if(!need(point.has_value(),"same-window reentry uses the original real menu button"))return;
            const auto before=authority(state);save("same-window-button-before-down",0);
            send(WM_LBUTTONDOWN,*point,MK_LBUTTON);
            need(GetCapture()==window.window&&state.runtimeMenuPressed==RuntimeMenuAction::None
                    &&state.runtimeMenuSliderDrag==RuntimeMenuAction::None,
                "same-window button recapture preserves ownership and original callback clears pressed state");
            need(std::any_of(window.events.begin(),window.events.end(),[](const auto& event){
                    return event.message==WM_CAPTURECHANGED&&event.entering&&event.depth>1&&event.lParamCaptureKind==1;}),
                "same-window button down records real synchronous own-HWND capture callback");
            need(authority(state)==before,"same-window button capture cleanup preserves original authoritative state");
            save("same-window-button-after-original-cleanup",0);
        });++captureCases;
    runCase("menu-already-own-recapture-slider",*normal,0,
        [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
            if(!need(GetCapture()==window.window,"same-window slider reentry starts with actual owned capture"))return;
            state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Sound;
            state.runtimePreferencePersistenceEnabled=true;
            const auto control=runtimeMenuControlForAction(state,ownClient(window),RuntimeMenuAction::AdjustMusicVolume);
            if(!need(control&&control->kind==RuntimeMenuControlKind::Slider,"same-window reentry uses the original sound slider"))return;
            const RECT value=runtimeMenuControlValueRect(*control);const POINT point{value.left+1,(value.top+value.bottom)/2};
            const auto before=authority(state);save("same-window-slider-before-down",0);
            send(WM_LBUTTONDOWN,point,MK_LBUTTON);
            need(GetCapture()==window.window&&state.runtimeMenuPressed==RuntimeMenuAction::None
                    &&state.runtimeMenuSliderDrag==RuntimeMenuAction::None,
                "same-window slider recapture preserves ownership and original callback clears drag state");
            need(std::any_of(window.events.begin(),window.events.end(),[](const auto& event){
                    return event.message==WM_CAPTURECHANGED&&event.entering&&event.depth>1&&event.lParamCaptureKind==1;}),
                "same-window slider down records real synchronous own-HWND capture callback");
            need(authority(state)==before,"same-window slider capture cleanup preserves original authoritative state");
            save("same-window-slider-after-original-cleanup",0);
        });++captureCases;
    for(const auto tool:{RuntimeSidebarTool::Repair,RuntimeSidebarTool::Sell}){
        runCase("sidebar-tool-"+std::to_string(static_cast<int>(tool)),*normal,0,
            [&](auto& state,auto& window,auto&,const Send& send,const Save&,const Tick&){
                const RECT sidebar=runtimeSidebarRect(ownClient(window));std::optional<POINT> found;
                for(LONG y=sidebar.top;y<sidebar.bottom&&!found;y+=2)for(LONG x=sidebar.left;x<sidebar.right;x+=2)
                    if(runtimeSidebarToolAtPoint(sidebar,{x,y})==tool){found=POINT{x,y};break;}
                if(!need(found.has_value(),"real sidebar tool rectangle"))return;
                clicked(send,*found);need(state.activeSidebarTool==tool,"sidebar tool exact original toggle");
                clicked(send,*found);need(state.activeSidebarTool==RuntimeSidebarTool::None,"second actual tool click toggles off");
            });
    }
    for(int mode=0;mode<2;++mode){
        runCase("sidebar-ready-or-queue-"+std::to_string(mode),mode?*normal:*ready,1,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                state.activeBuildTab=RuntimeBuildKind::Building;const RECT sidebar=runtimeSidebarRect(ownClient(window));
                std::optional<POINT> found;
                for(int page=0;page<runtimeBuildPageCount(state,sidebar)&&!found;++page){
                    runtimeSetActiveBuildPage(state,page);const auto visible=runtimeVisibleBuildCatalogIndices(state,sidebar);
                    for(std::size_t i=0;i<visible.size();++i)if(state.economy.catalog[visible[i]].typeId=="GAPOWR"){
                        const RECT rect=runtimeBuildButtonRect(sidebar,static_cast<int>(i));found=POINT{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};break;}}
                if(!need(found.has_value(),"real GAPOWR sidebar catalog point"))return;
                save("actual-catalog-state",0);clicked(send,*found);
                if(!mode)need(runtimeSelectedPlacement(state).has_value(),"ready stock item selects original real placement");
                else need(!witness.commands.empty()&&witness.commands.back().applied,"queue uses actual routed gameplay consumer");
            });
    }

    // The original failed positive fixture exposed the genuine busy single-slot rejection.
    runCase("sidebar-busy-queue-rejection",*queued,1,
        [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
            state.activeBuildTab=RuntimeBuildKind::Building;const RECT sidebar=runtimeSidebarRect(ownClient(window));
            std::optional<POINT> found;
            for(int page=0;page<runtimeBuildPageCount(state,sidebar)&&!found;++page){
                runtimeSetActiveBuildPage(state,page);const auto visible=runtimeVisibleBuildCatalogIndices(state,sidebar);
                for(std::size_t i=0;i<visible.size();++i)if(state.economy.catalog[visible[i]].typeId=="GAPOWR"){
                    const RECT rect=runtimeBuildButtonRect(sidebar,static_cast<int>(i));found=POINT{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};break;}}
            if(!need(found.has_value(),"busy GAPOWR uses the original real sidebar point"))return;
            const auto ledger=state.playerCommands.serialize();
            const auto lanes=runtimeSerializeProductionLanes(state.economy.localProductionLanes);
            const auto selection=state.selectedObjectIds;const auto tickBefore=state.tick;const auto credits=state.economy.credits;
            const auto worldBefore=state.world.deterministicSimulationStateHash();
            save("busy-queue-original-authority",0);clicked(send,*found);
            need(!witness.commands.empty()&&!witness.commands.back().accepted&&!witness.commands.back().applied,
                "busy single-slot production uses the actual rejected routed consumer");
            need(state.playerCommands.serialize()==ledger
                    &&runtimeSerializeProductionLanes(state.economy.localProductionLanes)==lanes
                    &&state.selectedObjectIds==selection&&state.tick==tickBefore&&state.economy.credits==credits
                    &&state.world.deterministicSimulationStateHash()==worldBefore,
                "busy rejection preserves complete authoritative ledgers selection credits tick and world");
            save("busy-queue-rejected",0);++commandCases;++rejectedCommands;
        });

    const auto requireCommand=[&](RuntimeWindowState& state,Witness& witness,int mode,
        const std::string& queueBefore,const std::vector<int>& selectionBefore,const Save& save){
        if(mode==0){need(witness.commands.empty(),"direct route never fabricates a captured envelope");return;}
        if(!need(!witness.commands.empty(),"real gameplay consumer produced complete wire envelope"))return;
        const auto& item=witness.commands.back();
        if(mode==1)need(item.accepted&&item.applied,"offline router actually consumed command");
        if(mode==2){std::string error;
            need(item.accepted&&!item.applied&&state.playerCommands.serialize()==queueBefore,"capture-only retains authority until replay");
            need(witness.replay(error),"complete wire decoded and actually replayed: "+error);save("real-replay",0);++replayedCommands;}
        if(mode==3){need(!item.accepted&&!item.applied&&state.playerCommands.serialize()==queueBefore
                &&state.selectedObjectIds==selectionBefore,"rejection consumes left input while retaining authority and selection");++rejectedCommands;}
        else ++acceptedCommands;++commandCases;
    };
    for(int mode=0;mode<4;++mode)for(unsigned flags:{0U,unsigned(MK_CONTROL),unsigned(MK_SHIFT|MK_CONTROL)}){
        runCase("actual-object-command-"+std::to_string(mode)+"-"+std::to_string(flags),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto point=enemyTarget(state,window);if(!need(point.has_value(),"actual opaque enemy target"))return;
                const auto selected=state.selectedObjectIds;const auto before=state.playerCommands.serialize();
                invalidateRuntimeObjectProjectionCache(state);save("cold-object-input",0);
                send(WM_LBUTTONDOWN,*point,MK_LBUTTON|flags);send(WM_LBUTTONUP,*point,flags);
                need(!state.leftPointer.active&&!state.leftPointer.moved,"target command consumed complete left gesture");
                requireCommand(state,witness,mode,before,selected,save);
                if(mode==0)need(state.lastCommand.valid&&state.lastCommand.acceptedCount>0,"direct normal/modified order actually applied");
            });++geometryCases;
    }
    for(int mode=0;mode<4;++mode){
        runCase("beacon-place-consumer-"+std::to_string(mode),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto point=playablePoint(state,window);if(!need(point.has_value(),"real beacon ground surface"))return;
                state.playerCommunication.beaconTargeting=true;state.playerLocalCommands.followTargeting=true;
                state.playerLocalCommands.rallyTargeting=true;const auto selected=state.selectedObjectIds;
                const auto queue=state.playerCommands.serialize();save("overlapping-target-modes",0);clicked(send,*point);
                need(!state.leftPointer.active&&!state.leftPointer.moved,"beacon precedes waypoint/follow/rally and consumes even rejected click");
                requireCommand(state,witness,mode,queue,selected,save);
                if(mode!=3)need(!state.playerCommunication.beaconTargeting,"actual accepted beacon exits targeting");
            });
    }
    for(int mode=0;mode<3;++mode){
        runCase("waypoint-nearest-close-"+std::to_string(mode),*normal,mode,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                const auto point=playablePoint(state,window);if(!need(point.has_value(),"real waypoint ground surface"))return;
                const RECT view=freshViewport(window);const POINT cell=approximateCellFromScreenPoint(state,view,*point);
                const int actor=state.selectedObjectIds.front();
                RuntimeMultiplayerGameplayTargetOrderCommand body;body.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;
                body.objectIds={actor};body.target=RuntimeMultiplayerGameplayCellTarget{{cell.x,cell.y}};
                const auto owner=runtimeCanonicalHumanHouseOwner(state.economy.owner);
                if(!need(applyRuntimePlayerWaypointCommand(state,owner,
                        RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::Begin,0}),
                        "actual Begin consumer establishes authoritative planning ownership"))return;
                if(!need(state.playerCommands.waypoints.append(owner,body),"formal first waypoint append"))return;
                const auto& initial=state.playerCommands.waypoints.routes.at(actor);const std::uint64_t firstId=initial.points.front().id;
                body.target=RuntimeMultiplayerGameplayCellTarget{{cell.x+1,cell.y}};
                if(!need(state.playerCommands.waypoints.append(owner,body),"formal second waypoint append"))return;
                state.playerLocalCommands.planning=true;
                const auto marker=runtimePlayerWaypointScreenPoint(state,view,state.playerCommands.waypoints.routes.at(actor).points.front());
                if(!need(marker.has_value(),"real native waypoint screen query"))return;
                const POINT input=mode==2?POINT{marker->x+12,marker->y+1}:mode==1?POINT{marker->x+12,marker->y}:*marker;
                save("legal-two-point-route",0);clicked(send,input);
                need(state.playerLocalCommands.selectedWaypoint==(mode==2?0:firstId),"original strict nearest-distance <145 and exact145 boundary");
                if(mode!=2)need(state.playerCommands.waypoints.routes.at(actor).loopStart==firstId,"eligible first point invokes actual CloseLoop consumer");
                need(!state.leftPointer.active,"waypoint input consumed before ordinary object selection");
            });
    }
    auto factorySeed=makeBase(0,true,true,"Germans");
    if(!expect(static_cast<bool>(factorySeed),"ordinary factory prototype"))return false;
    {
        RuntimeSnapshotActiveStateScope active(*factorySeed);
        for(const std::string type:{"GAPOWR","GAPILE","GAREFN","GAWEAP","GAAIRC"}){
            if(!expect(actualBuild(*factorySeed,type),"actual stock production and placement: "+type))return false;
        }
        for(int frame=0;frame<factorySeed->config.fixedTickHz*20;++frame){
            ++factorySeed->tick;advanceRuntimeSimulation(*factorySeed,false);
        }
        completeRuntimeWorldRefresh(*factorySeed);
    }
    for(int mode=0;mode<4;++mode){
        runCase("actual-factory-rally-"+std::to_string(mode),*factorySeed,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                int factory=-1;for(const auto& o:state.world.getObjects())if(o.alive&&o.type=="GAWEAP"
                    &&runtimeCanonicalHumanHouseOwner(o.owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)){factory=o.instanceId;break;}
                if(!need(factory>=0,"actual completed stock factory"))return;state.selectedObjectIds={factory};
                if(!need(runtimeSelectedFactoryCanSetRallyPoint(state),"unchanged strict mouse-factory eligibility"))return;
                const auto point=blankPoint(state,window);if(!need(point.has_value(),"real empty destination without object or wall"))return;
                const auto queue=state.playerCommands.serialize();const auto selected=state.selectedObjectIds;
                save("real-factory-selected",0);clicked(send,*point);
                requireCommand(state,witness,mode,queue,selected,save);
                if(mode!=3)need(state.playerCommands.rallyPoints.contains(factory),"real unique factory owner consumed rally destination");
            });
    }
    for(int mode=0;mode<2;++mode){
        runCase("actual-repair-sell-"+std::to_string(mode),*factorySeed,mode,
            [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
                int id=-1;for(const auto& o:state.world.getObjects())if(o.alive&&o.type=="GAPOWR"
                    &&o.owner==state.economy.owner){id=o.instanceId;break;}
                const auto point=pointForObject(state,window,id);if(!need(point.has_value(),"actual owned structure opaque hit"))return;
                state.activeSidebarTool=mode?RuntimeSidebarTool::Sell:RuntimeSidebarTool::Repair;
                save("real-structure-tool",0);clicked(send,*point);
                need(!state.leftPointer.active&&!state.leftPointer.moved,"structure tool consumes both actual success and legal rejection");
            });++geometryCases;
    }
    runCase("actual-radar-priority",*factorySeed,0,[&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
        const RECT rect=ownClient(window),sidebar=runtimeSidebarRect(rect),radar=runtimeRadarRect(sidebar);
        if(!need(runtimeHasOwnedRadar(state)&&runtimeRadarAvailable(state),"genuine completed stock GAAIRC and original power/radar gates"))return;
        std::optional<POINT> point;
        for(LONG y=radar.top;y<radar.bottom&&!point;y+=2)for(LONG x=radar.left;x<radar.right;x+=2)
            if(runtimeRadarCellAtPoint(state,radar,{x,y})){point=POINT{x,y};break;}
        if(!need(point.has_value(),"actual available radar pixel"))return;
        save("actual-radar-available",0);send(WM_LBUTTONDOWN,*point,MK_LBUTTON);
        need(state.interactionStatus.starts_with("radar jump "),"actual radar jump wins sidebar priority");
        send(WM_LBUTTONUP,*point,0);need(!state.leftPointer.active,"radar down never begins selection");
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

    for(int mode=0;mode<4;++mode)for(unsigned buttons:{0U,unsigned(MK_CONTROL)}){
        runCase("actual-wall-"+std::to_string(mode)+"-"+std::to_string(buttons),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto wall=prepareWall(state,window);if(!wall)return;
                const auto queue=state.playerCommands.serialize();const auto selected=state.selectedObjectIds;
                invalidateRuntimeObjectProjectionCache(state);save("cold-no-paint-wall",0);
                send(WM_LBUTTONDOWN,wall->first,MK_LBUTTON|buttons);send(WM_LBUTTONUP,wall->first,buttons);
                need(!state.leftPointer.active&&!state.leftPointer.moved,"wall consumes complete normal/force input");
                requireCommand(state,witness,mode,queue,selected,save);
                if(mode==0)need(state.lastCommand.valid&&state.lastCommand.acceptedCount>0,"wall formal validation reached direct real command apply");
            });
    }
    for(int mode=0;mode<4;++mode)for(int stages:{1,2}){
        runCase("actual-superweapon-"+std::to_string(mode)+"-"+std::to_string(stages),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const SuperWeaponRule* definition=nullptr;
                for(const auto& rule:state.rules.getSuperWeaponTypes())
                    if(stages==2?containsAsciiCaseInsensitive(rule.id,"chronosphere"):
                        (containsAsciiCaseInsensitive(rule.id,"spyplane")||containsAsciiCaseInsensitive(rule.type,"spyplane"))){definition=&rule;break;}
                if(!need(definition!=nullptr,"actual original one/two-stage rule"))return;
                const std::string id=definition->id;state.superWeapons.reset(state.rules);state.superWeaponsInitialized=true;
                auto context=runtimeSuperWeaponContext(state);std::string error;
                if(!need(state.superWeapons.forceReady(state.economy.owner,id,&context),"formal stock superweapon forceReady"))return;
                context.tick=++state.tick;state.superWeapons.tick(context);
                const RECT sidebar=runtimeSidebarRect(ownClient(window));std::optional<POINT> icon;
                state.activeBuildTab=RuntimeBuildKind::Defense;
                runtimeSetActiveBuildPage(state,0);runtimeClampActiveBuildPage(state,sidebar);
                for(int page=0;page<runtimeBuildPageCount(state,sidebar)&&!icon;++page){
                    runtimeSetActiveBuildPage(state,page);runtimeClampActiveBuildPage(state,sidebar);
                for(LONG y=sidebar.top;y<sidebar.bottom&&!icon;y+=2)for(LONG x=sidebar.left;x<sidebar.right;x+=2){
                    const auto* choice=runtimeSuperWeaponAtPoint(state,sidebar,{x,y});
                    if(choice&&choice->superWeaponId==id){icon=POINT{x,y};break;}}
                }
                if(!need(icon.has_value(),"actual sidebar superweapon rectangle"))return;
                save("ready-original-superweapon",0);clicked(send,*icon);
                const auto point=playablePoint(state,window);if(!need(point.has_value(),"actual superweapon ground picker"))return;
                // A superweapon click is accepted before the ordinary left-active gate, including a bare up.
                state.leftPointer.active=false;const auto queue=state.playerCommands.serialize();const auto selected=state.selectedObjectIds;
                send(WM_LBUTTONUP,*point,0);
                if(stages==2){
                    if(mode)need(state.multiplayerIntentRouter.superWeapon()
                        &&state.multiplayerIntentRouter.superWeapon()->targets.size()==1,"first source stays owned and awaits second target");
                    const POINT next{point->x+40,point->y};send(WM_LBUTTONUP,next,0);
                }
                need(!state.leftPointer.active&&!state.leftPointer.moved,"superweapon submission clears left flags even without a down");
                requireCommand(state,witness,mode,queue,selected,save);
            });
    }
    for(int mode=0;mode<4;++mode){
        runCase("actual-follow-target-"+std::to_string(mode),*normal,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const int follower=state.selectedObjectIds.front();int target=-1;
                for(const auto& object:state.world.getObjects())if(object.alive&&object.instanceId!=follower
                    &&object.kind!=WorldObjectKind::Structure&&object.owner==state.economy.owner){target=object.instanceId;break;}
                const auto point=pointForObject(state,window,target);if(!need(point.has_value(),"second genuine ordinary starting unit"))return;
                state.playerLocalCommands.followTargeting=true;
                const auto queue=state.playerCommands.serialize();const auto selection=state.selectedObjectIds;
                save("real-follow-over-ordinary-click",0);clicked(send,*point);
                need(!state.playerLocalCommands.followTargeting&&!state.leftPointer.active,"follow target takes complete click before primary");
                requireCommand(state,witness,mode,queue,selection,save);
            });
    }
    runCase("selected-object-secondary-fallback",*normal,0,
        [&](auto& state,auto& window,auto&,const Send& send,const Save& save,const Tick&){
            const int id=state.selectedObjectIds.front();const auto point=pointForObject(state,window,id);
            if(!need(point.has_value(),"actual selected unit opaque hit"))return;
            save("actual-secondary-eligibility",0);clicked(send,*point);
            need(!state.leftPointer.active&&selectionContains(state,id),"ordinary selected-object secondary/fallback retains selection");
        });

    for(int mode=0;mode<4;++mode){
        runCase("actual-mouse-primary-factory-"+std::to_string(mode),*factorySeed,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                int id=-1;for(const auto& object:state.world.getObjects())if(object.alive&&object.owner==state.economy.owner&&object.type=="GAWEAP"){id=object.instanceId;break;}
                if(!need(id>=0,"actual completed stock production factory"))return;state.selectedObjectIds={id};
                const auto point=pointForObject(state,window,id);if(!need(point.has_value(),"actual selected factory opaque hit"))return;
                const auto* current=findObjectById(state,id);if(!need(current&&runtimeSelectedFactoryCanBePrimary(state,*current),"original mouse-specific primary eligibility"))return;
                const auto queue=state.playerCommands.serialize();const auto selection=state.selectedObjectIds;
                save("same-selected-factory-primary",0);clicked(send,*point);requireCommand(state,witness,mode,queue,selection,save);
                need(!state.leftPointer.active,"primary-factory result consumes this click");
            });
    }
    for(int mode=0;mode<4;++mode)for(int valid=0;valid<2;++valid){
        runCase("actual-ready-placement-"+std::to_string(mode)+"-"+std::to_string(valid),*ready,mode,
            [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
                const auto choice=runtimeOldestReadyPlacement(state);
                if(!need(choice&&choice->item,"real paid stock Ready order"))return;
                const RuntimeBuildCatalogItem item=*choice->item;
                if(!need(beginRuntimePlacementFromUi(state,*choice,true),"original selected-placement primitive"))return;
                RuntimeCellPoint cell{};
                if(valid){const auto free=findRuntimePlacementCellNearBase(state,item);if(!need(free.has_value(),"original legal building location"))return;cell=*free;}
                else{const auto& object=state.world.getObjects().front();cell={object.x,object.y};}
                const RECT rect=ownClient(window);centerRuntimeCameraOnCell(state,rect,cell);const RECT view=runtimeMapViewport(rect);
                const POINT centerPixel=runtimeScreenPointForCell(state,view,cell,terrainLevelAtRuntimeCell(state,cell.x,cell.y));
                std::optional<POINT> point;
                for(int dy=-10;dy<=10&&!point;++dy)for(int dx=-10;dx<=10;++dx){
                    const POINT sample{centerPixel.x+dx,centerPixel.y+dy};if(!pointInRectInclusive(view,sample))continue;
                    const POINT actual=approximateCellFromScreenPoint(state,view,sample);
                    if(actual.x==cell.x&&actual.y==cell.y){point=sample;break;}}
                if(!need(point.has_value(),"real fresh exact surface placement pixel"))return;
                const auto before=state.world.getObjects().size();const auto queue=state.playerCommands.serialize();const auto selection=state.selectedObjectIds;
                save("before-real-placement",0);send(WM_MOUSEMOVE,*point,0);
                need(runtimePlacementValidForUi(state)==(valid!=0),"actual preview validity agrees with original fixture legality");
                send(WM_LBUTTONUP,*point,0);
                need(state.world.getObjects().size()==before,"bare up without active down does not place");
                clicked(send,*point);need(!state.leftPointer.active,"placement success/rejection consumes complete click");
                if(valid){requireCommand(state,witness,mode,queue,selection,save);
                    if(mode!=3)need(state.world.getObjects().size()>before,"actual placement produced original live structure");}
                else need(state.world.getObjects().size()==before&&runtimeOldestReadyPlacement(state).has_value(),"invalid preview retains paid Ready order without spawning");
            });
    }
    auto ordinaryLoad=makeBase(0,true,false);
    if(!expect(static_cast<bool>(ordinaryLoad),"natural active three-house save prototype without injected occupancy"))return false;
    runCase("ordinary-active21-load-fresh-root",*ordinaryLoad,1,
        [&](auto& state,auto& window,auto& witness,const Send& send,const Save& save,const Tick&){
            state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
            if(!need(state.particlesInitialized&&state.particles.currentTick()==21,"actual tick21 particle initialization"))return;
            state.leftPointer.active=true;state.leftPointer.moved=true;state.leftPointer.start={33,44};state.leftPointer.current={55,66};
            const RuntimeUiClock* borrowedClock=state.uiClock;const auto path=runtimeDefaultSavePath(state);std::string error;
            if(!need(!std::filesystem::exists(path),"new registered private legal save"))return;
            save("before-ordinary-save",0);const bool saved=saveRuntimeState(state,path,error);
            const bool loaded=saved&&loadRuntimeState(state,path,error);
            if(!need(saved&&loaded&&state.tick==21&&state.uiClock==borrowedClock,"ordinary save/load retains clock and tick: "+error))return;
            need(!state.leftPointer.active&&state.leftPointer.moved&&state.leftPointer.start.x==33&&state.leftPointer.start.y==44
                &&state.leftPointer.current.x==55&&state.leftPointer.current.y==66,"load swaps original active only, not moved/start/current");
            witness.attach();state.runtimeMenuOpen=false;state.playerCommunication.chatOpen=state.playerCommunication.diplomacyOpen=false;
            state.playerCommunication.beaconTargeting=false;state.playerCommunication.selectedBeacon.reset();
            state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
            SetWindowPos(window.window,nullptr,-32000,-32000,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
            state.camera.tileWidth=90;state.camera.tileHeight=45;invalidateRuntimeObjectProjectionCache(state);
            TrustedResourceRootScope freshResources(state.config,stock.config.localDataRootPath);
            const RECT view=freshViewport(window);state.cameraClampViewport=view;clampRuntimeMapCamera(state,view);
            const auto point=playablePoint(state,window);if(!need(point.has_value(),"fresh loaded root and actual new client"))return;
            SetFocus(window.window);SetCapture(window.window);need(GetCapture()==window.window,"fresh own capture after real load");
            state.selectedObjectIds.clear();save("fresh-after-load",0);
            send(WM_LBUTTONDOWN,*point,MK_LBUTTON);send(WM_MOUSEMOVE,{point->x+5,point->y},MK_LBUTTON);
            send(WM_LBUTTONUP,{point->x+5,point->y},0);
            need(!state.leftPointer.active&&!state.leftPointer.moved&&state.uiClock==borrowedClock&&state.tick==21,
                "first left input uses current loaded root and owned viewport without clock replacement");++loadCases;
        });
    trace<<"summary checks="<<checks<<" triples="<<triples<<" steps="<<steps<<" sourceReads="<<sourceReads
        <<" captureCases="<<captureCases<<" geometryCases="<<geometryCases<<" guards="<<guardCases
        <<" commandCases="<<commandCases<<" accepted="<<acceptedCommands<<" rejected="<<rejectedCommands
        <<" replays="<<replayedCommands<<" loads="<<loadCases<<" failures="<<failures<<'\n';
    trace.flush();expect(static_cast<bool>(trace),"complete trace flush");
    output<<"Map-left-pointer integration "<<(failures==0?"passed":"failed")<<": checks="<<checks<<" oldCases="<<triples
        <<" directCases="<<triples<<" nativeCases="<<triples<<" steps="<<steps<<" sourceReads="<<sourceReads
        <<" captureCases="<<captureCases<<" geometryCases="<<geometryCases<<" guards="<<guardCases
        <<" commandCases="<<commandCases<<" accepted="<<acceptedCommands<<" rejected="<<rejectedCommands
        <<" replays="<<replayedCommands<<" loadCases="<<loadCases<<" failures="<<failures
        <<" frozenOldCompleteClosures=1 sameProductionCpp=1 actualHiddenWndProc=1 perRouteIndependentSequence=1"
        <<" fullStateCommandsTickAndOrderedCaptureTrace=1 sourceBeforeWindowDestruction=1"
        <<" ordinaryActiveThreeHouseLoad=1 loadSwapsOnlyActive=1 coldFreshGeometry=1 wallPixelPriority=1"
        <<" actualOfflineRouterConsume=1 captureThenActualReplay=1 physicalDesktop=outer-evidence"
        <<" fullGlobalModifiers=not-run fullIME=not-run secondBackend=not-run fullRunningMultiplayer=not-run performance=not-run"
        <<" existingProjectionHistoryFailure=preserved existingTibspawnerFailure=preserved\n";
    return failures==0;
}
