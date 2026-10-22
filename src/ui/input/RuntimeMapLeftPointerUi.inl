// Private synchronous native effects and owned observations for the left policy.
// Construction borrows only the stable root and performs no query.
RuntimeMapLeftSuperWeaponState runtimeLeftPointerSuperWeaponState(SuperWeaponRuntimeState state) {
    switch (state) {
    case SuperWeaponRuntimeState::Ready: return RuntimeMapLeftSuperWeaponState::Ready;
    case SuperWeaponRuntimeState::Charging: return RuntimeMapLeftSuperWeaponState::Charging;
    case SuperWeaponRuntimeState::Suspended: return RuntimeMapLeftSuperWeaponState::Suspended;
    case SuperWeaponRuntimeState::AwaitingSecondTarget: return RuntimeMapLeftSuperWeaponState::AwaitingSecondTarget;
    default: return RuntimeMapLeftSuperWeaponState::Other;
    }
}

const char* runtimeLeftPointerSound(RuntimeMapLeftSound sound) {
    switch (sound) {
    case RuntimeMapLeftSound::Button: return runtimeSoundIntentSidebarButtonClick;
    case RuntimeMapLeftSound::Tab: return runtimeSoundIntentSidebarTabClick;
    case RuntimeMapLeftSound::Blocked: return "blocked";
    case RuntimeMapLeftSound::InsufficientFunds: return runtimeSoundIntentInsufficientFunds;
    case RuntimeMapLeftSound::Scold: return runtimeSoundIntentSidebarScold;
    case RuntimeMapLeftSound::CannotDeployHere: return runtimeSoundIntentCannotDeployHere;
    }
    return "blocked";
}

class RuntimeMapLeftPointerUiHost final : public RuntimeMapLeftPointerHost {
public:
    RuntimeMapLeftPointerUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    RuntimeMapLeftPointerState& gestureState() override { return state_.leftPointer; }
    bool worldLoaded() override { return state_.world.getSummary().loaded; }
    RuntimeInputPixelRect clientRect() override {
        RECT rect; GetClientRect(window_, &rect); return runtimeRightPointerInputRect(rect);
    }
    RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) override {
        return runtimeRightPointerInputRect(runtimeSidebarRect(runtimeRightPointerNativeRect(client)));
    }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) override {
        return runtimeRightPointerInputRect(runtimeMapViewport(runtimeRightPointerNativeRect(client)));
    }
    void setMouseValid(bool value) override { state_.mouseClientPointValid = value; }
    void setMousePoint(RuntimeInputPixelPoint point) override {
        state_.mouseClientPoint = runtimeRightPointerNativePoint(point);
    }
    bool mouseValid() override { return state_.mouseClientPointValid; }
    RuntimeInputPixelPoint mousePoint() override {
        return {state_.mouseClientPoint.x, state_.mouseClientPoint.y};
    }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    bool rejectObserverSelection() override { return rejectRuntimeObserverSelection(state_); }
    void focusWindow() override { SetFocus(window_); }
    void captureWindow() override { SetCapture(window_); }
    bool ownsCapture() override { return GetCapture() == window_; }
    void releaseCapture() override { ReleaseCapture(); }
    void restoreForceAttackAfterCapture(std::uint64_t rawButtons) override {
        RuntimeForceAttackHeldUiHost host(state_, window_);
        handleRuntimeForceAttackHeld({RuntimeForceAttackEvent::PointerPressRestore, rawButtons}, host);
    }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override { RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); }
    void invalidateProjection() override { invalidateRuntimeObjectProjectionCache(state_); }
    void setStatus(std::string text) override { state_.interactionStatus = std::move(text); }
    std::string interactionStatus() override { return state_.interactionStatus; }
    std::string economyStatus() override { return state_.economy.status; }
    void setEconomyStatus(std::string text) override { state_.economy.status = std::move(text); }
    void emitSound(RuntimeMapLeftSound sound) override { emitRuntimeSound(state_, runtimeLeftPointerSound(sound)); }
    bool suppressPlayerAudio() override { return state_.runtimeSuppressPlayerAudioFeedback; }

    RuntimeMenuAction menuActionAtPoint(RuntimeInputPixelRect client, RuntimeInputPixelPoint point) override {
        return runtimeMenuActionAtPoint(state_, runtimeRightPointerNativeRect(client), runtimeRightPointerNativePoint(point));
    }
    std::vector<RuntimeMenuAction> menuFocusableActions(RuntimeInputPixelRect client) override {
        std::vector<RuntimeMenuAction> actions;
        for (const auto& button : runtimeMenuFocusableButtons(state_, runtimeRightPointerNativeRect(client)))
            actions.push_back(button.action);
        return actions;
    }
    void setMenuFocus(std::int32_t index) override { state_.runtimeMenuFocus = index; }
    RuntimeMenuAction menuPressed() override { return state_.runtimeMenuPressed; }
    void setMenuPressed(RuntimeMenuAction action) override { state_.runtimeMenuPressed = action; }
    RuntimeMenuAction menuSliderDrag() override { return state_.runtimeMenuSliderDrag; }
    void setMenuSliderDrag(RuntimeMenuAction action) override { state_.runtimeMenuSliderDrag = action; }
    RuntimeMenuAction menuHover() override { return state_.runtimeMenuHover; }
    void setMenuHover(RuntimeMenuAction action) override { state_.runtimeMenuHover = action; }
    std::optional<RuntimeMapLeftMenuControl> menuControl(RuntimeInputPixelRect client, RuntimeMenuAction action) override {
        const auto control = runtimeMenuControlForAction(state_, runtimeRightPointerNativeRect(client), action);
        if (!control) return std::nullopt;
        return RuntimeMapLeftMenuControl{control->kind == RuntimeMenuControlKind::Slider,
            runtimeRightPointerInputRect(runtimeMenuControlValueRect(*control))};
    }
    bool setMenuSliderFromX(RuntimeInputPixelRect client, RuntimeMenuAction action, std::int32_t x) override {
        return setRuntimeMenuSliderFromX(state_, runtimeRightPointerNativeRect(client), action, x);
    }
    void applyMenuSliderRuntimeEffects(RuntimeMenuAction action) override { applyRuntimeMenuSliderRuntimeEffects(state_, action); }
    void clearMenuStatus() override { state_.runtimeMenuStatus.clear(); }
    void persistMenuPreferences() override { (void)persistRuntimeMenuPreferences(state_); }
    void activateMenuAction(RuntimeMenuAction action) override { activateRuntimeMenuAction(window_, state_, action); }

    RuntimeInputPixelRect radarRect(RuntimeInputPixelRect sidebar) override {
        return runtimeRightPointerInputRect(runtimeRadarRect(runtimeRightPointerNativeRect(sidebar)));
    }
    std::optional<RuntimeMultiplayerGameplayCell> radarCellAtPoint(RuntimeInputPixelRect radar, RuntimeInputPixelPoint point) override {
        const auto cell = runtimeRadarCellAtPoint(state_, runtimeRightPointerNativeRect(radar), runtimeRightPointerNativePoint(point));
        if (!cell) return std::nullopt;
        return RuntimeMultiplayerGameplayCell{cell->x, cell->y};
    }
    void centerCamera(RuntimeInputPixelRect client, RuntimeMultiplayerGameplayCell cell) override {
        centerRuntimeCameraOnCell(state_, runtimeRightPointerNativeRect(client), {cell.x, cell.y});
    }
    std::optional<RuntimeSidebarTool> sidebarToolAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        return runtimeSidebarToolAtPoint(runtimeRightPointerNativeRect(sidebar), runtimeRightPointerNativePoint(point));
    }
    RuntimeSidebarTool activeTool() override { return state_.activeSidebarTool; }
    void setActiveTool(RuntimeSidebarTool tool) override { state_.activeSidebarTool = tool; }
    std::string sidebarToolLabel(RuntimeSidebarTool tool) override { return runtimeSidebarToolLabel(tool); }
    std::optional<RuntimeBuildKind> buildTabAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        return runtimeBuildTabAtPoint(state_, runtimeRightPointerNativeRect(sidebar), runtimeRightPointerNativePoint(point));
    }
    void setBuildTab(RuntimeBuildKind kind) override { state_.activeBuildTab = kind; }
    void clampBuildPage(RuntimeInputPixelRect sidebar) override { runtimeClampActiveBuildPage(state_, runtimeRightPointerNativeRect(sidebar)); }
    std::string buildTabLabel(RuntimeBuildKind kind) override { return runtimeBuildTabLabel(kind); }
    std::int32_t buildPageButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        return runtimeBuildPageButtonAtPoint(state_, runtimeRightPointerNativeRect(sidebar), runtimeRightPointerNativePoint(point));
    }
    std::int32_t buildPageCount(RuntimeInputPixelRect sidebar) override { return runtimeBuildPageCount(state_, runtimeRightPointerNativeRect(sidebar)); }
    std::int32_t activeBuildPage() override { return runtimeActiveBuildPage(state_); }
    void setActiveBuildPage(std::int32_t page) override { runtimeSetActiveBuildPage(state_, page); }
    std::optional<std::int32_t> buildButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        return runtimeBuildButtonAtPoint(state_, runtimeRightPointerNativeRect(sidebar), runtimeRightPointerNativePoint(point));
    }
    bool prepareReadyProduction(std::int32_t index) override {
        catalogItem_ = state_.economy.catalog[index];
        readyLane_ = RuntimeProductionLane::Structures;
        const auto* ready = runtimeReadyProductionForItem(state_, *catalogItem_, &readyLane_);
        readyOrder_ = ready ? std::optional<RuntimeProductionLaneOrder>(*ready) : std::nullopt;
        return readyOrder_.has_value();
    }
    bool preparedReadyCatalogAvailable() override {
        const auto catalog = runtimeProductionCatalogLookup(state_, readyOrder_->itemKey);
        placementItem_ = catalog && catalog->item ? std::optional<RuntimeBuildCatalogItem>(*catalog->item) : std::nullopt;
        return placementItem_.has_value();
    }
    void beginPreparedReadyPlacement() override {
        (void)beginRuntimePlacementFromUi(state_, RuntimeSelectedPlacement{readyLane_, *readyOrder_, &*placementItem_}, true);
    }
    std::string catalogTypeId(std::int32_t index) override {
        (void)index; return catalogItem_->typeId;
    }
    bool modifierDown(RuntimeMapLeftModifier modifier) override {
        const int key = modifier == RuntimeMapLeftModifier::Control ? VK_CONTROL
            : modifier == RuntimeMapLeftModifier::Alt ? VK_MENU : VK_SHIFT;
        return (GetKeyState(key) & 0x8000) != 0;
    }
    void queueProduction(std::int32_t index, bool afterActive) override {
        (void)index;
        (void)dispatchRuntimeQueueProductionFromUi(state_, *catalogItem_,
            afterActive ? RuntimeProductionInsertMode::AfterActive : RuntimeProductionInsertMode::Back);
    }

    void ensureSuperWeaponsInitialized() override { ensureRuntimeSuperWeaponsInitialized(state_); }
    std::optional<RuntimeMapLeftSuperWeaponChoice> superWeaponAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto* weapon = runtimeSuperWeaponAtPoint(state_, runtimeRightPointerNativeRect(sidebar), runtimeRightPointerNativePoint(point));
        if (!weapon) { chosenWeapon_.reset(); return std::nullopt; }
        chosenWeapon_ = RuntimeMapLeftSuperWeaponChoice{weapon->owner, weapon->superWeaponId, weapon->name};
        return chosenWeapon_;
    }
    RuntimeMapLeftSuperWeaponState chosenSuperWeaponState() override {
        for (const auto& snapshot : state_.superWeapons.sidebarSnapshots())
            if (snapshot.owner == chosenWeapon_->owner && snapshot.superWeaponId == chosenWeapon_->id)
                return runtimeLeftPointerSuperWeaponState(snapshot.state);
        return RuntimeMapLeftSuperWeaponState::Other;
    }
    void prepareSuperWeaponContext() override {
        superContext_ = runtimeSuperWeaponContext(state_);
        superContext_->emit = runtimeSuperWeaponEventSink(state_);
    }
    bool usesIntentRouter() override { return runtimeUsesGameplayIntentRouter(state_); }
    void refreshAuthority() override { superContext_.reset(); refreshRuntimeMultiplayerPlayerIntentAuthority(state_); }
    std::optional<std::string> superWeaponTargetMode(const std::string& id) override {
        const auto* rule = state_.rules.findSuperWeaponType(id);
        return rule ? std::optional<std::string>(rule->targetMode) : std::nullopt;
    }
    bool routerBeginSuperWeapon(const std::string& id, std::size_t count, std::string& error) override {
        return state_.multiplayerIntentRouter.beginSuperWeapon(id, count, error);
    }
    bool routerConfirmSuperWeapon(std::string& error) override { return state_.multiplayerIntentRouter.confirmSuperWeapon(error); }
    bool offlineBeginSuperWeapon(const std::string& owner, const std::string& id, std::string& error) override {
        auto context = std::move(*superContext_); superContext_.reset();
        return state_.superWeapons.beginTargetSelection(owner, id, context, &error);
    }
    bool multiplayerBridgePresent() override { return state_.multiplayerBridge != nullptr; }
    bool routerSuperWeaponPresent() override { return state_.multiplayerIntentRouter.superWeapon().has_value(); }
    std::string routerSuperWeaponId() override { return state_.multiplayerIntentRouter.superWeapon()->superWeaponId; }
    std::optional<RuntimeMapLeftSuperWeaponRemaining> routerSuperWeaponRemaining() override {
        const auto& remaining = state_.multiplayerIntentRouter.superWeapon();
        if (!remaining) return std::nullopt;
        RuntimeMapLeftSuperWeaponRemaining result{remaining->superWeaponId, remaining->expectedTargetCount, remaining->targets.size(), {}};
        if (!remaining->targets.empty()) result.firstTargetCell = remaining->targets.front().cell;
        return result;
    }
    bool superWeaponTargetsGroundArea(const std::string& id) override { return runtimeSuperWeaponTargetsGroundArea(state_, id); }
    bool routerTargetSuperWeapon(RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) override {
        return state_.multiplayerIntentRouter.targetSuperWeapon(std::move(target), error);
    }
    void beginChronosphereMarker(const std::string& id, RuntimeMultiplayerGameplayCell cell) override {
        beginRuntimeLocalChronosphereMarker(state_, id, {cell.x, cell.y});
    }
    void clearChronosphereMarker() override { clearRuntimeLocalChronosphereMarker(state_); }
    std::string economyOwner() override { return state_.economy.owner; }
    std::vector<RuntimeMapLeftSuperWeaponSnapshot> superWeaponSnapshots() override {
        std::vector<RuntimeMapLeftSuperWeaponSnapshot> result;
        for (const auto& snapshot : state_.superWeapons.sidebarSnapshots())
            result.push_back({snapshot.owner, snapshot.superWeaponId, snapshot.targetSelectionActive});
        return result;
    }
    void prepareParasiteHostPoses() override { parasiteHostPoses_ = runtimeParasiteLinkedHostPoses(state_); }
    bool offlineSubmitSuperWeapon(const std::string& owner, const std::string& id,
        RuntimeMultiplayerGameplaySuperWeaponTarget target, std::string& error) override {
        auto context = std::move(*superContext_); superContext_.reset();
        return state_.superWeapons.submitTarget(owner, id, {{target.cell.x, target.cell.y}, target.objectId.value_or(-1)}, context, &error);
    }
    void notifyParasiteHostsRelocated() override { (void)notifyRuntimeParasiteHostsRelocated(state_, parasiteHostPoses_); }
    void drainObjectExitQueue() override { (void)drainRuntimeObjectExitQueue(state_); }
    void flushSuperWeaponHostEvents() override { (void)flushRuntimeSuperWeaponHostEvents(state_); }
    RuntimeMapLeftSuperWeaponState superWeaponState(const std::string& owner, const std::string& id) override {
        return runtimeLeftPointerSuperWeaponState(state_.superWeapons.stateOf(owner, id));
    }

    RuntimeMultiplayerGameplayCell surfaceCell(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto cell = approximateCellFromScreenPoint(state_, runtimeRightPointerNativeRect(viewport), runtimeRightPointerNativePoint(point));
        return {cell.x, cell.y};
    }
    void prepareProjection(RuntimeInputPixelRect viewport) override {
        const auto& scene = runtimeProjectedObjectsForViewport(state_, runtimeRightPointerNativeRect(viewport));
        projection_ = scene;
        projectionIds_.clear(); projectionIds_.reserve(scene.size());
        for (std::size_t i = 0; i < scene.size(); ++i) {
            projectionIds_.push_back(scene[i].object ? std::optional<int>(scene[i].object->instanceId) : std::nullopt);
            projection_[i].object = nullptr;
        }
        pickedId_.reset();
    }
    void pickProjection(RuntimeInputPixelPoint point) override {
        // Only this immediate original picker call borrows current objects.
        auto view = projection_;
        for (std::size_t i = 0; i < view.size(); ++i)
            view[i].object = projectionIds_[i] ? findObjectById(state_, *projectionIds_[i]) : nullptr;
        const auto* picked = findProjectedObjectAtPoint(view, runtimeRightPointerNativePoint(point));
        pickedId_ = picked && picked->object ? std::optional<int>(picked->object->instanceId) : std::nullopt;
    }
    bool pickedObjectPresent() override { return pickedObject() != nullptr; }
    bool pickedObjectIsStructure() override {
        const auto* object = pickedObject(); return object && object->kind == WorldObjectKind::Structure;
    }
    std::int32_t pickedObjectId() override { return pickedObject()->instanceId; }
    std::vector<RuntimeMapLeftBoxItem> boxProjectionItems() override {
        std::vector<RuntimeMapLeftBoxItem> result; result.reserve(projection_.size());
        for (std::size_t i = 0; i < projection_.size(); ++i)
            result.push_back({projectionIds_[i], runtimeRightPointerInputRect(projection_[i].bounds)});
        return result;
    }
    bool objectCanBeBoxSelected(std::int32_t id) override {
        const auto* object = findObjectById(state_, id); return object && runtimeObjectCanBeBoxSelected(*object);
    }
    bool prepareTerrainWall(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        wall_ = visibleTerrainWallTargetAtPoint(state_, runtimeRightPointerNativeRect(viewport), runtimeRightPointerNativePoint(point));
        return wall_.has_value();
    }
    RuntimeMultiplayerGameplayCell preparedTerrainWallCell() override { return {wall_->x, wall_->y}; }
    bool dispatchTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind order,
        RuntimeMultiplayerGameplayCell cell, RuntimeMapLeftTargetSource target) override {
        std::optional<WorldObjectInstance> identity;
        if (target == RuntimeMapLeftTargetSource::TerrainWall) identity = wall_;
        else if (target == RuntimeMapLeftTargetSource::PickedObject) {
            if (const auto* object = pickedObject()) identity = *object;
        }
        return dispatchRuntimeTargetOrderFromUi(state_, order, {cell.x, cell.y}, identity ? &*identity : nullptr);
    }
    bool selectionEmpty() override { return state_.selectedObjectIds.empty(); }
    std::size_t selectionSize() override { return state_.selectedObjectIds.size(); }
    std::int32_t selectionFront() override { return state_.selectedObjectIds.front(); }
    bool selectionContains(std::int32_t id) override { return ::selectionContains(state_, id); }
    std::int32_t selectedMovableCount() override { return selectedMovableObjectCount(state_); }
    void applySelection(std::vector<std::int32_t> ids, bool append, bool toggle) override {
        applyRuntimePlayerSelection(state_, ids, append, toggle);
    }
    void clearSelectedBeacon() override { state_.playerCommunication.selectedBeacon.reset(); }
    void selectBeacon(RuntimeMultiplayerGameplayCell cell) override { state_.playerCommunication.selectedBeacon = cell; }
    bool beaconTargeting() override { return state_.playerCommunication.beaconTargeting; }
    void setBeaconTargeting(bool value) override { state_.playerCommunication.beaconTargeting = value; }
    bool dispatchBeacon(RuntimeMultiplayerGameplayCell cell) override {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
        command.kind = RuntimePlayerCommunicationKind::PlaceBeacon; command.cell = cell;
        return dispatchRuntimePlayerCommunicationFromUi(state_, command);
    }
    std::string communicationViewerOwner() override { return runtimePlayerCommunicationViewerOwner(state_); }
    std::vector<RuntimePlayerBeacon> beacons() override { return state_.playerCommunication.beacons; }
    bool beaconVisible(const RuntimePlayerBeacon& beacon) override { return runtimePlayerBeaconVisibleToLocal(state_, beacon); }
    bool followTargeting() override { return state_.playerLocalCommands.followTargeting; }
    void setFollowTargeting(bool value) override { state_.playerLocalCommands.followTargeting = value; }
    bool rallyTargeting() override { return state_.playerLocalCommands.rallyTargeting; }
    void setRallyTargeting(bool value) override { state_.playerLocalCommands.rallyTargeting = value; }
    bool planningWaypoints() override { return state_.playerLocalCommands.planning; }
    std::string canonicalHumanOwner() override { return runtimeCanonicalHumanHouseOwner(state_.economy.owner); }
    std::vector<RuntimeMapLeftWaypointRoute> waypointRoutes() override {
        std::vector<RuntimeMapLeftWaypointRoute> result;
        for (const auto& [id, route] : state_.playerCommands.waypoints.routes)
            result.push_back({id, route.owner, route.loopStart, route.points});
        return result;
    }
    std::optional<RuntimeInputPixelPoint> waypointScreenPoint(RuntimeInputPixelRect viewport, const RuntimePlayerWaypoint& waypoint) override {
        const auto point = runtimePlayerWaypointScreenPoint(state_, runtimeRightPointerNativeRect(viewport), waypoint);
        return point ? std::optional<RuntimeInputPixelPoint>({point->x, point->y}) : std::nullopt;
    }
    void setSelectedWaypoint(std::uint64_t id) override { state_.playerLocalCommands.selectedWaypoint = id; }
    bool closeWaypointLoop(std::uint64_t id) override {
        return dispatchRuntimePlayerWaypointFromUi(state_, RuntimePlayerWaypointOperation::CloseLoop, id);
    }
    bool mouseFactoryCanRally() override { return runtimeSelectedFactoryCanSetRallyPoint(state_); }
    bool mouseFactoryCanBePrimary(std::int32_t id) override {
        const auto* object = findObjectById(state_, id); return object && runtimeSelectedFactoryCanBePrimary(state_, *object);
    }
    bool dispatchFactory(std::int32_t id, std::optional<RuntimeMultiplayerGameplayCell> cell) override {
        return dispatchRuntimePlayerFactoryFromUi(state_, id, cell);
    }
    bool dispatchSecondary(std::int32_t id) override { return dispatchRuntimeSecondaryActionFromUi(state_, id); }
    bool dispatchStructureTool(RuntimeSidebarTool tool, std::int32_t id) override {
        return dispatchRuntimeStructureToolFromUi(state_, tool, id);
    }
    bool selectedPlacementPresent() override { return runtimeSelectedPlacement(state_).has_value(); }
    void updatePlacementPreview(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        updateRuntimePlacementPreview(state_, runtimeRightPointerNativeRect(viewport), runtimeRightPointerNativePoint(point));
    }
    bool placementValid() override { return runtimePlacementValidForUi(state_); }
    bool confirmPlacement(std::string& error) override { return state_.multiplayerIntentRouter.confirmPlacement(error); }
    void notePlacementResult(bool accepted, const std::string& error, RuntimeMapLeftSound sound) override {
        noteRuntimeMultiplayerIntentResult(state_, accepted, "building placement", error, runtimeLeftPointerSound(sound));
    }
    RuntimeMultiplayerGameplayCell placementCell() override {
        const auto cell = runtimePlacementCellForUi(state_); return {cell.x, cell.y};
    }
    void placeReadyBuilding(RuntimeMultiplayerGameplayCell cell) override { placeRuntimeReadyBuilding(state_, {cell.x, cell.y}); }
    bool handleExistingRightMove(const RuntimeMapLeftPointerInput& input) override {
        RuntimeMapRightPointerGestureUiHost host(state_, window_);
        return handleRuntimeMapRightPointerMove({input.point}, host);
    }
private:
    const WorldObjectInstance* pickedObject() const {
        return pickedId_ ? findObjectById(state_, *pickedId_) : nullptr;
    }
    RuntimeWindowState& state_;
    HWND window_;
    std::optional<RuntimeBuildCatalogItem> catalogItem_;
    RuntimeProductionLane readyLane_ = RuntimeProductionLane::Structures;
    std::optional<RuntimeProductionLaneOrder> readyOrder_;
    std::optional<RuntimeBuildCatalogItem> placementItem_;
    std::optional<RuntimeMapLeftSuperWeaponChoice> chosenWeapon_;
    std::optional<SuperWeaponWorldContext> superContext_;
    std::vector<std::array<int, 4>> parasiteHostPoses_;
    std::vector<RuntimeProjectedObject> projection_;
    std::vector<std::optional<int>> projectionIds_;
    std::optional<int> pickedId_;
    std::optional<WorldObjectInstance> wall_;
};

bool beginRuntimeMenuSliderDrag(RuntimeWindowState& state, const RECT& client, RuntimeMenuAction action, POINT point) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return beginRuntimeMapLeftMenuSlider(runtimeRightPointerInputRect(client), action, {point.x, point.y}, host);
}
bool updateRuntimeMenuSliderDrag(RuntimeWindowState& state, const RECT& client, int x) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return updateRuntimeMapLeftMenuSlider(runtimeRightPointerInputRect(client), x, host);
}
bool finishRuntimeMenuSliderDrag(RuntimeWindowState& state, const RECT& client, int x) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return finishRuntimeMapLeftMenuSlider(runtimeRightPointerInputRect(client), x, host);
}
bool handleRuntimePlayerWaypointClick(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return handleRuntimeMapLeftWaypointClick(runtimeRightPointerInputRect(viewport), {point.x, point.y}, host);
}
bool handleRuntimePlayerTargetClick(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return handleRuntimeMapLeftTargetClick(runtimeRightPointerInputRect(viewport), {point.x, point.y}, host);
}
bool submitRuntimeSuperWeaponTargetAtPoint(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    RuntimeMapLeftPointerUiHost host(state, nullptr);
    return submitRuntimeMapLeftSuperWeapon(runtimeRightPointerInputRect(viewport), {point.x, point.y}, host);
}
