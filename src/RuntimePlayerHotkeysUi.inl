// UI-only helpers, included after camera/sidebar functions and before key routing.
RuntimePlayerBeacon* runtimePlayerSelectedBeacon(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    const auto owner=runtimePlayerCommunicationViewerOwner(state);
    if (ui.selectedBeacon) for (auto& beacon:ui.beacons)
        if (beacon.owner==owner && beacon.cell==*ui.selectedBeacon && runtimePlayerBeaconVisibleToLocal(state,beacon)) return &beacon;
    ui.selectedBeacon.reset();
    return nullptr;
}

bool dispatchRuntimePlayerCommunicationFromUi(RuntimeWindowState& state,
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command) {
    std::string error;
    RuntimeMultiplayerGameplayCommand envelope; envelope.body = command;
    bool accepted = validateRuntimeMultiplayerGameplayCommand(envelope, error)
        && validateRuntimePlayerCommunication(state, state.economy.owner, command, error);
    if (accepted && runtimeUsesGameplayIntentRouter(state)) {
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        accepted = state.multiplayerIntentRouter.communicate(std::move(command), error);
    } else if (accepted) accepted = applyRuntimePlayerCommunication(state, state.economy.owner, command);
    if (!accepted) runtimePlayerAddChatLine(state, "系统", "操作未执行：" + error);
    state.repaintRequested = true;
    return accepted;
}

std::string runtimePlayerChatUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 || size > static_cast<int>(runtimeMultiplayerGameplayMaximumIdentifierBytes)) return {};
    std::string encoded(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        encoded.data(), size, nullptr, nullptr);
    return encoded;
}

void runtimePlayerToggleAlliance(RuntimeWindowState& state, const std::string& target) {
    if (target.empty()) return;
    const auto owner = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto offer = state.playerCommands.alliances.find({owner, target});
    const bool offered = offer == state.playerCommands.alliances.end()
        ? runtimeOwnersAreAllied(state, owner, target) : offer->second;
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind = RuntimePlayerCommunicationKind::Alliance;
    command.text = target; command.value = offered ? 0 : 1;
    dispatchRuntimePlayerCommunicationFromUi(state, command);
}

void runtimePlayerCenterSelection(RuntimeWindowState& state, const RECT& client) {
    long long x = 0, y = 0, count = 0;
    for (int id : state.selectedObjectIds) {
        const auto* object = findObjectById(state, id);
        if (object && object->alive && !runtimeGateObjectIsLimbo(*object)
            && runtimeObjectVisibleToOwner(state, state.economy.owner, *object)) {
            x += object->x; y += object->y; ++count;
        }
    }
    if (count) {
        const bool following = state.playerLocalCommands.cameraFollow;
        centerRuntimeCameraOnCell(state, client,
            {static_cast<int>(x / count), static_cast<int>(y / count)});
        state.playerLocalCommands.cameraFollow = following;
    } else state.playerLocalCommands.cameraFollow = false;
}

void runtimePlayerCenterBase(RuntimeWindowState& state, const RECT& client) {
    const WorldObjectInstance* base = nullptr;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || runtimeCanonicalHumanHouseOwner(object.owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)) continue;
        const auto* type = findRuntimeBuildingTypeById(state.rules, object.type);
        if (!base || (type && type->constructionYard)) base = &object;
        if (type && type->constructionYard) break;
    }
    state.playerLocalCommands.cameraFollow = false;
    if (base) centerRuntimeCameraOnCell(state, client, {base->x, base->y});
    else if (state.economy.skirmishStartCell.x >= 0 && state.economy.skirmishStartCell.y >= 0)
        centerRuntimeCameraOnCell(state, client, state.economy.skirmishStartCell);
}

bool handleRuntimePlayerModalKey(RuntimeWindowState& state, int key, bool repeated) {
    auto& ui = state.playerCommunication;
    if (ui.chatOpen) {
        if (key == VK_ESCAPE) { ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear(); }
        else if (key == VK_BACK && !ui.chatDraft.empty()) {
            const wchar_t last = ui.chatDraft.back(); ui.chatDraft.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !ui.chatDraft.empty()
                && ui.chatDraft.back() >= 0xd800 && ui.chatDraft.back() <= 0xdbff)
                ui.chatDraft.pop_back();
        } else if (key == VK_RETURN && !repeated) {
            const auto text = trimAsciiLocal(runtimePlayerChatUtf8(ui.chatDraft));
            if (ui.beaconEditing && !runtimePlayerSelectedBeacon(state)) {
                ui.chatOpen=ui.beaconEditing=false;ui.chatDraft.clear();
            } else if (text.empty() && !ui.beaconEditing) { ui.chatOpen = false; ui.chatDraft.clear(); }
            else {
                RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
                command.kind = ui.beaconEditing ? RuntimePlayerCommunicationKind::BeaconText
                    : ui.chatAllies ? RuntimePlayerCommunicationKind::ChatAllies : RuntimePlayerCommunicationKind::ChatAll;
                if (ui.beaconEditing) command.cell=*ui.selectedBeacon;
                else command.value=runtimePlayerOutgoingChatMask(state);
                command.text = text;
                if (dispatchRuntimePlayerCommunicationFromUi(state, command)) {
                    ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear();
                }
            }
        }
        state.repaintRequested = true;
        return true;
    }
    if (!ui.diplomacyOpen) return false;
    const auto rows = runtimeDiplomacyRows(state);
    const int count=static_cast<int>(std::min<std::size_t>(rows.size(),8));
    if (key == VK_ESCAPE || key == VK_TAB || key == VK_RETURN
        || key==runtimeMenuKeyboardBindingCode(state,"Diplomacy",VK_TAB)) {
        if (!repeated) closeRuntimeDiplomacyPage(state);
    } else if (count>1) {
        ui.diplomacyRow = std::clamp(ui.diplomacyRow, 1, count-1);
        if (key == VK_UP) ui.diplomacyRow = ui.diplomacyRow==1?count-1:ui.diplomacyRow-1;
        else if (key == VK_DOWN) ui.diplomacyRow = ui.diplomacyRow==count-1?1:ui.diplomacyRow+1;
        else if ((key == 'A' || key == VK_SPACE) && !repeated
            && runtimeDiplomacyAllianceEnabled(state,rows[ui.diplomacyRow]))
            runtimePlayerToggleAlliance(state,rows[ui.diplomacyRow].owner);
    }
    state.repaintRequested = true;
    return true;
}

bool handleRuntimePlayerReferenceKey(RuntimeWindowState& state, const RECT& client, int code, bool repeated) {
    auto& local = state.playerLocalCommands; auto& ui = state.playerCommunication;
    const auto matches = [&](const std::string& id, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, id, fallback);
    };
    const std::pair<const char*, RuntimeBuildKind> tabs[] = {
        {"StructureTab", RuntimeBuildKind::Building}, {"DefenseTab", RuntimeBuildKind::Defense},
        {"InfantryTab", RuntimeBuildKind::Infantry}, {"UnitTab", RuntimeBuildKind::Unit}};
    constexpr int tabKeys[] = {'Q','W','E','R'};
    for (int i = 0; i < 4; ++i) if (matches(tabs[i].first, tabKeys[i])) {
        state.activeBuildTab = tabs[i].second;
        runtimeClampActiveBuildPage(state, runtimeSidebarRect(client));
        if (!repeated && runtimeBuildKindUsesSingleStructureSlot(state.activeBuildTab)) {
            for (const auto& item : state.economy.catalog) {
                if (item.kind != state.activeBuildTab) continue;
                RuntimeProductionLane lane = RuntimeProductionLane::Structures;
                const auto* ready = runtimeReadyProductionForItem(state, item, &lane);
                if (ready == nullptr) continue;
                if (beginRuntimePlacementFromUi(state, {lane, *ready, &item}, true)) {
                    state.activeSidebarTool = RuntimeSidebarTool::None;
                    local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
                    state.selectionDragActive = state.selectionDragMoved = false;
                    state.interactionStatus = "placing " + item.typeId;
                    // A keyboard selection must follow a stationary mouse too.
                    const RECT viewport = runtimeMapViewport(client);
                    if (state.mouseClientPointValid
                        && pointInRectInclusive(viewport, state.mouseClientPoint)) {
                        updateRuntimePlacementPreview(state, viewport, state.mouseClientPoint);
                    }
                    emitRuntimeSound(state, runtimeSoundIntentSidebarButtonClick);
                    state.repaintRequested = true;
                }
                break;
            }
        }
        return true;
    }
    if (matches("ToggleRepair", 'K') || matches("ToggleSell", 'L')) {
        if (!repeated) {
            const auto tool = matches("ToggleRepair", 'K') ? RuntimeSidebarTool::Repair : RuntimeSidebarTool::Sell;
            state.activeSidebarTool = state.activeSidebarTool == tool ? RuntimeSidebarTool::None : tool;
            local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
        }
        return true;
    }
    if (matches("CenterBase", 'H')) { runtimePlayerCenterBase(state, client); return true; }
    if (matches("ShowCoordinates", 'O')) {
        if (!repeated) local.showCoordinates = !local.showCoordinates;
        return true;
    }
    if (matches("Follow", 'F')) {
        if (!repeated) local.cameraFollow = !local.cameraFollow && !state.selectedObjectIds.empty();
        if (local.cameraFollow) runtimePlayerCenterSelection(state, client);
        return true;
    }
    if (matches("Diplomacy", VK_TAB)) {
        if (!repeated) openRuntimeDiplomacyPage(state);
        return true;
    }
    if (matches("ToggleAlliance", 'A')) {
        if (repeated) return true;
        std::string target;
        if (!state.selectedObjectIds.empty())
            if (const auto* object = findObjectById(state, state.selectedObjectIds.front()))
                if (runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(state.economy.owner))
                    target = runtimeCanonicalHumanHouseOwner(object->owner);
        if (target.empty()) openRuntimeDiplomacyPage(state);
        else runtimePlayerToggleAlliance(state, target);
        return true;
    }
    if (matches("ChatToAll", VK_RETURN) || matches("ChatToAllies", VK_HOME)) {
        if (!repeated) {
            ui.chatOpen = true; ui.chatAllies = matches("ChatToAllies", VK_HOME); ui.chatDraft.clear();
            ui.beaconEditing=false;
            if (!ui.chatAllies) if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
                ui.beaconEditing=true;ui.chatDraft=widenUtf8(beacon->text);
            }
            ui.beaconTargeting = local.followTargeting = local.rallyTargeting = false;
            state.selectionDragActive = state.selectionDragMoved = false;
        }
        return true;
    }
    if (matches("PlaceBeacon", 'B')) {
        if (!repeated) {
            if (!ui.beaconTargeting) {
                const auto owner=runtimePlayerCommunicationViewerOwner(state);
                const auto count=std::count_if(ui.beacons.begin(),ui.beacons.end(),[&](const auto& beacon) {
                    return beacon.owner==owner && runtimePlayerBeaconVisibleToLocal(state,beacon);
                });
                if (count>=3) {
                    state.interactionStatus="信标数量已达上限";
                    return true;
                }
                cancelRuntimeActiveSuperWeaponTargetSelection(state,"beacon");
                dismissRuntimePlacementFromUi(state);
            }
            ui.beaconTargeting = !ui.beaconTargeting;
            ui.selectedBeacon.reset();
            local.followTargeting = local.rallyTargeting = false;
            state.activeSidebarTool = RuntimeSidebarTool::None;
        }
        return true;
    }
    if (matches("DeleteBeacon", VK_DELETE) || code == VK_DECIMAL) {
        if (repeated) return true;
        if (local.planning && local.selectedWaypoint) {
            dispatchRuntimePlayerWaypointFromUi(state, RuntimePlayerWaypointOperation::DeletePoint, local.selectedWaypoint);
            return true;
        }
        if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::DeleteBeacon; command.cell = beacon->cell;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    if (matches("AllToCheer", 'C')) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Cheer;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    for (int i = 1; i <= 8; ++i) if (matches("Taunt_" + std::to_string(i), VK_F5 + i - 1)) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Taunt; command.value = static_cast<std::uint8_t>(i);
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    return false;
}

void drawRuntimePlayerHotkeyOverlay(HDC dc, const RECT& client, RuntimeWindowState& state) {
    if (state.playerCommunication.diplomacyOpen) {
        if (!state.runtimeMenuOpen && !state.skirmishMatchFinished) drawRuntimeDiplomacyPage(dc,client,state);
        return;
    }
    auto& ui = state.playerCommunication;
    const RECT viewport = runtimeMapViewport(client);
    drawRuntimePlayerWaypoints(dc, viewport, state);
    const int saved = SaveDC(dc);
    HFONT font = CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
    SelectObject(dc,font); SetBkMode(dc,TRANSPARENT);
    const auto line = [&](int x, int y, const std::wstring& text, COLORREF color = RGB(235,240,245)) {
        SetTextColor(dc,color); TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size()));
    };
    const auto panel = [&](RECT bounds) {
        HBRUSH background = CreateSolidBrush(RGB(19,29,39)); FillRect(dc,&bounds,background); DeleteObject(background);
    };
    std::erase_if(ui.beacons, [&](const auto& b) { return b.untilTick >= 0 && b.untilTick <= state.tick; });
    std::erase_if(ui.cheerUntilTick, [&](const auto& c) { return c.second <= state.tick; });
    (void)runtimePlayerSelectedBeacon(state);
    drawRuntimePlayerBeaconOverlay(dc,viewport,state);
    const int noticeHeight = runtimePlayerDefeatNoticeStackHeight(state,viewport,GetTickCount64());
    int y = viewport.top + 12 + noticeHeight;
    for (const auto& entry : ui.lines) if (!ui.diplomacyOpen && (entry.untilTick > state.tick || ui.chatOpen)) {
        if (y + 25 > viewport.bottom) break;
        const std::wstring text = (entry.allies ? L"[盟友] " : L"") + widenUtf8(entry.owner + ": " + entry.text);
        RECT bounds{viewport.left+10,y,std::min(viewport.right-10,viewport.left+850L),y+25}; panel(bounds);
        SetTextColor(dc,RGB(235,240,245)); DrawTextW(dc,text.c_str(),-1,&bounds,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        y += 27;
    }
    if (state.playerLocalCommands.showCoordinates && state.mouseClientPointValid) {
        const POINT cell = approximateCellFromScreenPoint(state,viewport,state.mouseClientPoint);
        if (pointInRectInclusive(viewport,state.mouseClientPoint)
            && viewport.top + 12 + noticeHeight + 25 <= viewport.bottom)
            line(std::max(viewport.left+10L,viewport.right-190),viewport.top+12+noticeHeight,
                L"坐标 " + std::to_wstring(cell.x) + L", " + std::to_wstring(cell.y));
    }
    if (ui.chatOpen) {
        RECT box{viewport.left+12,viewport.bottom-50,viewport.right-12,viewport.bottom-12}; panel(box);
        std::wstring text = (ui.beaconEditing ? L"[信标文字] " : ui.chatAllies ? L"[盟友] " : L"[所有人] ") + ui.chatDraft + L"▏";
        SetTextColor(dc,RGB(255,239,169)); DrawTextW(dc,text.c_str(),-1,&box,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    } else if (ui.beaconTargeting) line(viewport.left+16,viewport.bottom-40,L"点击地图放置信标 · 右键或 Esc 取消",RGB(255,220,64));
    else if (ui.selectedBeacon) line(viewport.left+16,viewport.bottom-40,
        runtimeKeyboardKeyName(runtimeMenuKeyboardBindingCode(state,"ChatToAll",VK_RETURN))+L" 编辑信标文字 · "
        +runtimeKeyboardKeyName(runtimeMenuKeyboardBindingCode(state,"DeleteBeacon",VK_DELETE))+L" 删除信标",RGB(255,220,64));
    RestoreDC(dc,saved); DeleteObject(font);
}
