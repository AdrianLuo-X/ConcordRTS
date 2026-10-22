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

std::u16string runtimeChatTextDraftFromWide(std::wstring_view text) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    std::u16string result;
    result.reserve(text.size());
    for (wchar_t unit : text) result.push_back(static_cast<char16_t>(unit));
    return result;
}

std::wstring runtimeChatTextDraftToWide(std::u16string_view text) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    std::wstring result;
    result.reserve(text.size());
    for (char16_t unit : text) result.push_back(static_cast<wchar_t>(unit));
    return result;
}

class RuntimeChatTextUiHost final : public RuntimeChatTextHost {
public:
    explicit RuntimeChatTextUiHost(RuntimeWindowState& state, HWND window = nullptr)
        : state_(state), window_(window) {}
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool selectedBeaconValid() override { return runtimePlayerSelectedBeacon(state_) != nullptr; }
    RuntimeMultiplayerGameplayCell selectedBeaconCell() override {
        return *state_.playerCommunication.selectedBeacon;
    }
    std::uint8_t outgoingChatMask() override { return runtimePlayerOutgoingChatMask(state_); }
    bool dispatchCommunication(RuntimeMultiplayerGameplayPlayerCommunicationCommand command) override {
        return dispatchRuntimePlayerCommunicationFromUi(state_, std::move(command));
    }
    void requestRepaint() override { state_.repaintRequested = true; }
    void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }
private:
    RuntimeWindowState& state_;
    HWND window_;
};

// Private synchronous root assembly. Standard controllers own the decisions;
// these adapters retain the existing queries, command consumers and UI owners.
template<class StandardHost>
class RuntimeCommunicationAllianceUiHost : public StandardHost {
public:
    explicit RuntimeCommunicationAllianceUiHost(RuntimeWindowState& state) : state_(state) {}
    std::string currentOwner() override { return state_.economy.owner; }
    std::string canonicalOwner(const std::string& owner) override {
        return runtimeCanonicalHumanHouseOwner(owner);
    }
    std::optional<bool> allianceOffer(const std::string& source, const std::string& target) override {
        const auto offer = state_.playerCommands.alliances.find({source, target});
        if (offer == state_.playerCommands.alliances.end()) return std::nullopt;
        return offer->second;
    }
    bool ownersAllied(const std::string& source, const std::string& target) override {
        return runtimeOwnersAreAllied(state_, source, target);
    }
    void dispatchCommunication(RuntimeMultiplayerGameplayPlayerCommunicationCommand command) override {
        (void)dispatchRuntimePlayerCommunicationFromUi(state_, std::move(command));
    }
protected:
    RuntimeWindowState& state_;
};

class RuntimeReferenceCommunicationUiHost final
    : public RuntimeCommunicationAllianceUiHost<RuntimeReferenceCommunicationHost> {
    using AllianceUiHost = RuntimeCommunicationAllianceUiHost<RuntimeReferenceCommunicationHost>;
    using AllianceUiHost::state_;
public:
    explicit RuntimeReferenceCommunicationUiHost(RuntimeWindowState& state) : AllianceUiHost(state) {}
    std::int32_t binding(const std::string& id, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, id, fallback);
    }
    std::optional<std::string> objectOwner(std::int32_t id) override {
        if (const auto* object = findObjectById(state_, id)) return object->owner;
        return std::nullopt;
    }
    void openDiplomacy() override { openRuntimeDiplomacyPage(state_); }
    std::optional<RuntimePlayerBeacon> selectedBeacon() override {
        if (const auto* beacon = runtimePlayerSelectedBeacon(state_)) return *beacon;
        return std::nullopt;
    }
    void setChatDraftUtf8(const std::string& text) override {
        state_.playerCommunication.chatDraft = runtimeChatTextDraftFromWide(widenUtf8(text));
    }
    std::string viewerOwner() override { return runtimePlayerCommunicationViewerOwner(state_); }
    bool beaconVisible(const RuntimePlayerBeacon& beacon) override {
        return runtimePlayerBeaconVisibleToLocal(state_, beacon);
    }
    void cancelSuperWeapon(const std::string& reason) override {
        (void)cancelRuntimeActiveSuperWeaponTargetSelection(state_, reason);
    }
    void dismissPlacement() override { dismissRuntimePlacementFromUi(state_); }
    void setSidebarTool(RuntimeSidebarTool tool) override { state_.activeSidebarTool = tool; }
    void setSelectionDragMoved(bool moved) override { state_.leftPointer.moved = moved; }
    void setSelectionDragActive(bool active) override { state_.leftPointer.active = active; }
    void beaconLimitStatus() override { state_.interactionStatus = "信标数量已达上限"; }
    void dispatchWaypoint(RuntimePlayerWaypointOperation operation, std::uint64_t id) override {
        dispatchRuntimePlayerWaypointFromUi(state_, operation, id);
    }
};

class RuntimeDiplomacyNavigationUiHost final
    : public RuntimeCommunicationAllianceUiHost<RuntimeDiplomacyNavigationHost> {
    using AllianceUiHost = RuntimeCommunicationAllianceUiHost<RuntimeDiplomacyNavigationHost>;
    using AllianceUiHost::state_;
public:
    explicit RuntimeDiplomacyNavigationUiHost(RuntimeWindowState& state) : AllianceUiHost(state) {}
    std::vector<RuntimeCommunicationDiplomacyRow> rows() override {
        const auto privateRows = runtimeDiplomacyRows(state_);
        std::vector<RuntimeCommunicationDiplomacyRow> result;
        result.reserve(privateRows.size());
        for (const auto& row : privateRows) result.push_back({row.owner, row.self, row.defeated});
        return result;
    }
    std::int32_t binding(const std::string& id, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, id, fallback);
    }
    void closePage() override { closeRuntimeDiplomacyPage(state_); }
    bool allianceEnabled(const RuntimeCommunicationDiplomacyRow& row) override {
        RuntimeDiplomacyRow privateRow;
        privateRow.owner = row.owner; privateRow.self = row.self; privateRow.defeated = row.defeated;
        return runtimeDiplomacyAllianceEnabled(state_, privateRow);
    }
    void requestRepaint() override { state_.repaintRequested = true; }
};

void runtimePlayerToggleAlliance(RuntimeWindowState& state, const std::string& target) {
    RuntimeReferenceCommunicationUiHost host(state);
    toggleRuntimeCommunicationAlliance(target, host);
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
    RuntimeChatTextUiHost chatHost(state);
    if (handleRuntimeChatTextKey({key, repeated}, ui, chatHost)) return true;
    RuntimeDiplomacyNavigationUiHost host(state);
    return handleRuntimeDiplomacyNavigationKey({key, repeated}, ui, host);
}

// Private synchronous assembly; the standard controller owns the decisions.
class RuntimeReferencePresentationUiHost final : public RuntimeReferencePresentationHost {
public:
    RuntimeReferencePresentationUiHost(RuntimeWindowState& state, const RECT& client)
        : state_(state), client_(client) {}
    std::int32_t binding(const std::string& id, std::int32_t fallback) override {
        return runtimeMenuKeyboardBindingCode(state_, id, fallback);
    }
    void setActiveBuildTab(RuntimeBuildKind kind) override { state_.activeBuildTab = kind; }
    RuntimeBuildKind activeBuildTab() override { return state_.activeBuildTab; }
    void clampActiveBuildPage() override {
        runtimeClampActiveBuildPage(state_, runtimeSidebarRect(client_));
    }
    std::size_t catalogSize() override { return state_.economy.catalog.size(); }
    RuntimeBuildKind catalogKind(std::size_t ordinal) override {
        return state_.economy.catalog[ordinal].kind;
    }
    std::optional<RuntimeReferencePresentationReady> readyForItem(std::size_t ordinal) override {
        const auto& item = state_.economy.catalog[ordinal];
        RuntimeProductionLane lane = RuntimeProductionLane::Structures;
        if (const auto* ready = runtimeReadyProductionForItem(state_, item, &lane))
            return RuntimeReferencePresentationReady{ordinal, lane, *ready};
        return std::nullopt;
    }
    bool beginReadyPlacement(const RuntimeReferencePresentationReady& ready) override {
        const auto& item = state_.economy.catalog[ready.catalogOrdinal];
        return beginRuntimePlacementFromUi(state_, {ready.lane, ready.order, &item}, true);
    }
    RuntimeSidebarTool sidebarTool() override { return state_.activeSidebarTool; }
    void setSidebarTool(RuntimeSidebarTool tool) override { state_.activeSidebarTool = tool; }
    void setBeaconTargeting(bool targeting) override { state_.playerCommunication.beaconTargeting = targeting; }
    void setSelectionDragMoved(bool moved) override { state_.leftPointer.moved = moved; }
    void setSelectionDragActive(bool active) override { state_.leftPointer.active = active; }
    void placementStatus(std::size_t ordinal) override {
        state_.interactionStatus = "placing " + state_.economy.catalog[ordinal].typeId;
    }
    std::optional<RuntimeReferencePresentationPreview> stationaryPreviewInput() override {
        const RECT viewport = runtimeMapViewport(client_);
        if (!state_.mouseClientPointValid) return std::nullopt;
        return RuntimeReferencePresentationPreview{
            {viewport.left, viewport.top, viewport.right, viewport.bottom},
            {state_.mouseClientPoint.x, state_.mouseClientPoint.y}};
    }
    void previewPlacement(const RuntimeReferencePresentationPreview& preview) override {
        const RECT viewport{preview.viewport.left, preview.viewport.top,
            preview.viewport.right, preview.viewport.bottom};
        const POINT point{preview.point.x, preview.point.y};
        updateRuntimePlacementPreview(state_, viewport, point);
    }
    void sidebarClickSound() override { emitRuntimeSound(state_, runtimeSoundIntentSidebarButtonClick); }
    void requestRepaint() override { state_.repaintRequested = true; }
    bool selectionEmpty() override { return state_.selectedObjectIds.empty(); }
    void centerBase() override { runtimePlayerCenterBase(state_, client_); }
    void centerSelection() override { runtimePlayerCenterSelection(state_, client_); }
private:
    RuntimeWindowState& state_;
    RECT client_;
};

bool handleRuntimePlayerReferenceKey(RuntimeWindowState& state, const RECT& client, int code, bool repeated) {
    auto& local = state.playerLocalCommands;
    RuntimeReferencePresentationUiHost presentationHost(state, client);
    if (handleRuntimeReferencePresentationKey({code, repeated}, local, presentationHost)) return true;
    RuntimeReferenceCommunicationUiHost communicationHost(state);
    return handleRuntimeReferenceCommunicationKey({code, repeated}, local,
        state.playerCommunication, state.selectedObjectIds, communicationHost);
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
    const int noticeHeight = runtimePlayerDefeatNoticeStackHeight(state,viewport,runtimeUiClockNow(state));
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
        std::wstring text = (ui.beaconEditing ? L"[信标文字] " : ui.chatAllies ? L"[盟友] " : L"[所有人] ") + runtimeChatTextDraftToWide(ui.chatDraft) + L"▏";
        SetTextColor(dc,RGB(255,239,169)); DrawTextW(dc,text.c_str(),-1,&box,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    } else if (ui.beaconTargeting) line(viewport.left+16,viewport.bottom-40,L"点击地图放置信标 · 右键或 Esc 取消",RGB(255,220,64));
    else if (ui.selectedBeacon) line(viewport.left+16,viewport.bottom-40,
        runtimeKeyboardKeyName(runtimeMenuKeyboardBindingCode(state,"ChatToAll",VK_RETURN))+L" 编辑信标文字 · "
        +runtimeKeyboardKeyName(runtimeMenuKeyboardBindingCode(state,"DeleteBeacon",VK_DELETE))+L" 删除信标",RGB(255,220,64));
    RestoreDC(dc,saved); DeleteObject(font);
}
