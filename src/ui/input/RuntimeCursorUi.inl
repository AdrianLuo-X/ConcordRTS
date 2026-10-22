#include "ui/input/RuntimeCursorController.h"

// Private diagnostics only. A null sink adds no native calls or state queries.
// All values passed to a sink are owned before invoking external diagnostics.
class RuntimeCursorUiDiagnosticSink {
public:
    virtual ~RuntimeCursorUiDiagnosticSink() = default;
    // Fixture-only geometry preparation using the successful original sample.
    // The null-sink production path makes no additional native call.
    virtual void prepareOwnedFixtureCursorSample(HWND, const POINT&) {}
    virtual void observe(std::string name, std::vector<std::string> arguments,
        std::string result) = 0;
};
thread_local RuntimeCursorUiDiagnosticSink* runtimeCursorUiDiagnosticSink = nullptr;
static void runtimeCursorUiEmit(std::string name,
    std::vector<std::string> arguments, std::string result) {
    if (runtimeCursorUiDiagnosticSink)
        runtimeCursorUiDiagnosticSink->observe(std::move(name), std::move(arguments), std::move(result));
}
#define runtimeCursorUiObserve(...) do { if (runtimeCursorUiDiagnosticSink != nullptr) runtimeCursorUiEmit(__VA_ARGS__); } while (false)
static std::string runtimeCursorUiBool(bool value) { return value ? "1" : "0"; }
static std::string runtimeCursorUiPoint(RuntimeInputPixelPoint point) {
    return std::to_string(point.x) + "," + std::to_string(point.y);
}
static std::string runtimeCursorUiRect(RuntimeInputPixelRect rect) {
    return std::to_string(rect.left) + "," + std::to_string(rect.top) + ","
        + std::to_string(rect.right) + "," + std::to_string(rect.bottom);
}
static RuntimeInputPixelRect runtimeCursorInputRect(const RECT& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
static RECT runtimeCursorNativeRect(RuntimeInputPixelRect rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
static POINT runtimeCursorNativePoint(RuntimeInputPixelPoint point) { return {point.x, point.y}; }
static std::string runtimeCursorUiHandle(HCURSOR cursor) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(cursor));
}
static HCURSOR runtimeCursorLoadRequest(RuntimeCursorRequest request) {
    if (request == RuntimeCursorRequest::Hidden) return nullptr;
    const int resource = request == RuntimeCursorRequest::StockAction ? 32515
        : request == RuntimeCursorRequest::StockForbidden ? 32648
        : request == RuntimeCursorRequest::StockHand ? 32649 : 32512;
    const HCURSOR cursor = request == RuntimeCursorRequest::SystemArrow
        ? LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)) : loadRuntimeStockCursor(resource);
    runtimeCursorUiObserve(request == RuntimeCursorRequest::SystemArrow
        ? "LoadCursorW" : "loadRuntimeStockCursor", {std::to_string(resource)}, runtimeCursorUiHandle(cursor));
    return cursor;
}

template<class Port>
class RuntimeCursorPointUiHost : public Port {
public:
    explicit RuntimeCursorPointUiHost(const RuntimeWindowState& state) : root_(&state) {}
    RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect client) override {
        const auto result = runtimeCursorInputRect(runtimeSidebarRect(runtimeCursorNativeRect(client)));
        runtimeCursorUiObserve("sidebarRect", {runtimeCursorUiRect(client)}, runtimeCursorUiRect(result)); return result;
    }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect client) override {
        const auto result = runtimeCursorInputRect(runtimeMapViewport(runtimeCursorNativeRect(client)));
        runtimeCursorUiObserve("mapViewport", {runtimeCursorUiRect(client)}, runtimeCursorUiRect(result)); return result;
    }
    bool superWeaponAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const bool result = runtimeSuperWeaponAtPoint(*root_, runtimeCursorNativeRect(sidebar), runtimeCursorNativePoint(point)) != nullptr;
        runtimeCursorUiObserve("superWeaponAtPoint", {runtimeCursorUiRect(sidebar), runtimeCursorUiPoint(point)}, runtimeCursorUiBool(result)); return result;
    }
    bool buildButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const bool result = runtimeBuildButtonAtPoint(*root_, runtimeCursorNativeRect(sidebar), runtimeCursorNativePoint(point)).has_value();
        runtimeCursorUiObserve("buildButtonAtPoint", {runtimeCursorUiRect(sidebar), runtimeCursorUiPoint(point)}, runtimeCursorUiBool(result)); return result;
    }
    std::int32_t buildPageButtonAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const auto result = runtimeBuildPageButtonAtPoint(*root_, runtimeCursorNativeRect(sidebar), runtimeCursorNativePoint(point));
        runtimeCursorUiObserve("buildPageButtonAtPoint", {runtimeCursorUiRect(sidebar), runtimeCursorUiPoint(point)}, std::to_string(result)); return result;
    }
    bool buildTabAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const bool result = runtimeBuildTabAtPoint(*root_, runtimeCursorNativeRect(sidebar), runtimeCursorNativePoint(point)).has_value();
        runtimeCursorUiObserve("buildTabAtPoint", {runtimeCursorUiRect(sidebar), runtimeCursorUiPoint(point)}, runtimeCursorUiBool(result)); return result;
    }
    bool sidebarToolAtPoint(RuntimeInputPixelRect sidebar, RuntimeInputPixelPoint point) override {
        const bool result = runtimeSidebarToolAtPoint(runtimeCursorNativeRect(sidebar), runtimeCursorNativePoint(point)).has_value();
        runtimeCursorUiObserve("sidebarToolAtPoint", {runtimeCursorUiRect(sidebar), runtimeCursorUiPoint(point)}, runtimeCursorUiBool(result)); return result;
    }
    RuntimeInputPixelRect radarRect(RuntimeInputPixelRect sidebar) override {
        const auto result = runtimeCursorInputRect(runtimeRadarRect(runtimeCursorNativeRect(sidebar)));
        runtimeCursorUiObserve("radarRect", {runtimeCursorUiRect(sidebar)}, runtimeCursorUiRect(result)); return result;
    }
    bool radarCellAtPoint(RuntimeInputPixelRect radar, RuntimeInputPixelPoint point) override {
        const bool result = runtimeRadarCellAtPoint(*root_, runtimeCursorNativeRect(radar), runtimeCursorNativePoint(point)).has_value();
        runtimeCursorUiObserve("radarCellAtPoint", {runtimeCursorUiRect(radar), runtimeCursorUiPoint(point)}, runtimeCursorUiBool(result)); return result;
    }
    bool selectedPlacementPresent() override {
        const bool result = runtimeSelectedPlacement(*root_).has_value();
        runtimeCursorUiObserve("selectedPlacementPresent", {}, runtimeCursorUiBool(result)); return result;
    }
    bool placementValid() override {
        const bool result = runtimePlacementValidForUi(*root_);
        runtimeCursorUiObserve("placementValid", {}, runtimeCursorUiBool(result)); return result;
    }
    bool activeSidebarToolPresent() override {
        const bool result = root_->activeSidebarTool != RuntimeSidebarTool::None;
        runtimeCursorUiObserve("activeSidebarToolPresent", {}, runtimeCursorUiBool(result)); return result;
    }
    std::int32_t selectedMovableCount() override {
        const auto result = selectedMovableObjectCount(*root_);
        runtimeCursorUiObserve("selectedMovableCount", {}, std::to_string(result)); return result;
    }
protected:
    RuntimeCursorPointUiHost() = default;
    const RuntimeWindowState* root_ = nullptr;
};

// Deliberately not final: Native may decorate the same private production host.
class RuntimeCursorUiHost : public RuntimeCursorPointUiHost<RuntimeCursorHost> {
public:
    explicit RuntimeCursorUiHost(HWND window) : window_(window) {}
    bool acquireWindowState() override {
        state_ = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window_, GWLP_USERDATA));
        root_ = state_;
        const bool result = state_ != nullptr;
        runtimeCursorUiObserve("acquireWindowState", {}, runtimeCursorUiBool(result)); return result;
    }
    bool worldLoaded() override {
        const bool result = state_->world.getSummary().loaded;
        runtimeCursorUiObserve("worldLoaded", {}, runtimeCursorUiBool(result)); return result;
    }
    bool menuOpen() override {
        const bool result = state_->runtimeMenuOpen;
        runtimeCursorUiObserve("menuOpen", {}, runtimeCursorUiBool(result)); return result;
    }
    bool finished() override {
        const bool result = state_->skirmishMatchFinished;
        runtimeCursorUiObserve("finished", {}, runtimeCursorUiBool(result)); return result;
    }
    bool diplomacyOpen() override {
        const bool result = state_->playerCommunication.diplomacyOpen;
        runtimeCursorUiObserve("diplomacyOpen", {}, runtimeCursorUiBool(result)); return result;
    }
    RuntimeInputPixelPoint cursorPoint() override {
        POINT point;
        const BOOL positioned = GetCursorPos(&point);
        if (positioned && runtimeCursorUiDiagnosticSink != nullptr)
            runtimeCursorUiDiagnosticSink->prepareOwnedFixtureCursorSample(window_, point);
        const BOOL converted = ScreenToClient(window_, &point);
        const RuntimeInputPixelPoint result{point.x, point.y};
        runtimeCursorUiObserve("cursorPoint", {std::to_string(positioned), std::to_string(converted)}, runtimeCursorUiPoint(result)); return result;
    }
    RuntimeInputPixelRect clientRect() override {
        RECT client;
        const BOOL queried = GetClientRect(window_, &client);
        const auto result = runtimeCursorInputRect(client);
        runtimeCursorUiObserve("clientRect", {std::to_string(queried)}, runtimeCursorUiRect(result)); return result;
    }
    void ensureSuperWeaponsInitialized() override {
        ensureRuntimeSuperWeaponsInitialized(*state_);
        runtimeCursorUiObserve("ensureSuperWeaponsInitialized", {}, "void");
    }
    bool activeLocalSuperWeaponTargeting() override {
        const bool result = runtimeActiveLocalSuperWeaponTargeting(*state_) != nullptr;
        runtimeCursorUiObserve("activeLocalSuperWeaponTargeting", {}, runtimeCursorUiBool(result)); return result;
    }
    void beginRouterSuperWeaponObservation() override {
        // The original accessor is a direct return of superWeapon_. Reading the
        // same stable root member later preserves reference freshness without
        // retaining an optional/string borrow across public query callbacks.
        const auto& observed = state_->multiplayerIntentRouter.superWeapon();
        (void)observed;
        runtimeCursorUiObserve("beginRouterSuperWeaponObservation", {}, "void");
    }
    bool routerSuperWeaponPresent() override {
        const bool result = state_->multiplayerIntentRouter.superWeapon().has_value();
        runtimeCursorUiObserve("routerSuperWeaponPresent", {}, runtimeCursorUiBool(result)); return result;
    }
    std::string routerSuperWeaponId() override {
        const std::string result = state_->multiplayerIntentRouter.superWeapon()->superWeaponId;
        runtimeCursorUiObserve("routerSuperWeaponId", {}, result); return result;
    }
    std::string economyOwner() override {
        const std::string result = state_->economy.owner;
        runtimeCursorUiObserve("economyOwner", {}, result); return result;
    }
    std::vector<RuntimeCursorSuperWeaponRow> superWeaponSnapshots() override {
        std::vector<RuntimeCursorSuperWeaponRow> result;
        for (const auto& row : state_->superWeapons.sidebarSnapshots())
            result.push_back({row.owner, row.superWeaponId, row.targetSelectionActive});
        if (runtimeCursorUiDiagnosticSink != nullptr) {
            std::vector<std::string> summary;
            for (const auto& row : result)
                summary.push_back(row.owner + "|" + row.id + "|" + runtimeCursorUiBool(row.targetSelectionActive));
            runtimeCursorUiObserve("superWeaponSnapshots", std::move(summary), std::to_string(result.size()));
        }
        return result;
    }
    bool usesIntentRouter() override {
        const bool result = runtimeUsesGameplayIntentRouter(*state_);
        runtimeCursorUiObserve("usesIntentRouter", {}, runtimeCursorUiBool(result)); return result;
    }
    RuntimeInputPixelPoint surfaceCell(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        const auto cell = approximateCellFromScreenPoint(*state_, runtimeCursorNativeRect(viewport), runtimeCursorNativePoint(point));
        const RuntimeInputPixelPoint result{cell.x, cell.y};
        runtimeCursorUiObserve("surfaceCell", {runtimeCursorUiRect(viewport), runtimeCursorUiPoint(point)}, runtimeCursorUiPoint(result)); return result;
    }
    void prepareAndPickProjection(RuntimeInputPixelRect viewport, RuntimeInputPixelPoint point) override {
        // These are the two original consecutive calls. No diagnostic callback,
        // ID read, geometry copy, or object-index lookup is inserted between them.
        const auto& scene = runtimeProjectedObjectsForViewport(*state_, runtimeCursorNativeRect(viewport));
        const auto* picked = findProjectedObjectAtPoint(scene, runtimeCursorNativePoint(point));
        pickedProjection_ = picked != nullptr;
        pickedObjectIndex_.reset();
        if (picked && picked->object) {
            // The actual producer assigns &object from getObjects() to each
            // projected entry; this owns an element position, never a pointer.
            const auto& objects = state_->world.getObjects();
            pickedObjectIndex_ = static_cast<std::size_t>(picked->object - objects.data());
        }
        runtimeCursorUiObserve("prepareAndPickProjection",
            {runtimeCursorUiRect(viewport), runtimeCursorUiPoint(point)},
            runtimeCursorUiBool(pickedProjection_));
    }
    bool pickedProjectionPresent() override {
        const bool result = pickedProjection_;
        runtimeCursorUiObserve("pickedProjectionPresent", {}, runtimeCursorUiBool(result)); return result;
    }
    bool pickedObjectPresent() override {
        const bool result = pickedObjectIndex_.has_value();
        runtimeCursorUiObserve("pickedObjectPresent", {}, runtimeCursorUiBool(result)); return result;
    }
    bool superWeaponTargetsGroundArea(const std::string& id) override {
        const bool result = runtimeSuperWeaponTargetsGroundArea(*state_, id);
        runtimeCursorUiObserve("superWeaponTargetsGroundArea", {id}, runtimeCursorUiBool(result)); return result;
    }
    std::int32_t pickedObjectId() override {
        // Ground-area's actual rule lookup is synchronous and cannot replace
        // the world. The ID is first read here, at the original short-circuit.
        const auto result = state_->world.getObjects()[*pickedObjectIndex_].instanceId;
        runtimeCursorUiObserve("pickedObjectId", {}, std::to_string(result)); return result;
    }
    bool previewSuperWeaponTarget(const std::string& owner, const std::string& id,
        RuntimeInputPixelPoint cell, std::int32_t objectId) override {
        SuperWeaponWorldContext context = runtimeSuperWeaponContext(*state_);
        std::string reason;
        const bool result = state_->superWeapons.previewTarget(owner, id,
            {{cell.x, cell.y}, objectId}, context, &reason);
        runtimeCursorUiObserve("previewSuperWeaponTarget",
            {owner, id, runtimeCursorUiPoint(cell), std::to_string(objectId), reason}, runtimeCursorUiBool(result)); return result;
    }
    void setCursorRequest(RuntimeCursorRequest request) override {
        const auto cursor = runtimeCursorLoadRequest(request);
        const auto previous = SetCursor(cursor);
        runtimeCursorUiObserve("SetCursor", {std::to_string(static_cast<unsigned>(request)),
            runtimeCursorUiHandle(cursor)}, runtimeCursorUiHandle(previous));
    }
private:
    HWND window_;
    RuntimeWindowState* state_ = nullptr;
    bool pickedProjection_ = false;
    std::optional<std::size_t> pickedObjectIndex_;
};

HCURSOR runtimeCursorForPoint(const RuntimeWindowState& state,
    const RECT& client, POINT point) {
    RuntimeCursorPointUiHost<RuntimeCursorPointHost> host(state);
    return runtimeCursorLoadRequest(runtimeCursorForInputPoint(host,
        runtimeCursorInputRect(client), {point.x, point.y}));
}

#undef runtimeCursorUiObserve
