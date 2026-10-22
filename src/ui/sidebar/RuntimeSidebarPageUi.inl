// Private synchronous native adapter. Include after runtimeBuildButtonsPerPage.
// Constructors only bind state/native values and perform no query.
class RuntimeSidebarPageQueryUiHost final : public RuntimeSidebarPageQueryHost {
public:
    explicit RuntimeSidebarPageQueryUiHost(const RuntimeWindowState& state) : state_(state) {}
    RuntimeBuildKind activeTab() override { return state_.activeBuildTab; }
    std::int32_t pageSlotCount() override {
        return static_cast<std::int32_t>(state_.buildPageByTab.size());
    }
    std::int32_t pageAt(std::int32_t tab) override {
        return state_.buildPageByTab[static_cast<std::size_t>(tab)];
    }
    std::int32_t filteredCatalogCount() override {
        return static_cast<std::int32_t>(runtimeFilteredBuildCatalogIndices(state_).size());
    }
    std::int32_t visibleSuperWeaponCount() override {
        return static_cast<std::int32_t>(runtimeVisibleSuperWeapons(state_).size());
    }
    std::int32_t buttonsPerPage(RuntimeInputPixelRect sidebar) override {
        return runtimeBuildButtonsPerPage(RECT{sidebar.left, sidebar.top, sidebar.right, sidebar.bottom});
    }
private:
    const RuntimeWindowState& state_;
};

class RuntimeSidebarPageUiHost final : public RuntimeSidebarWheelHost {
public:
    explicit RuntimeSidebarPageUiHost(
        RuntimeWindowState& state, HWND window = nullptr, WPARAM wheel = 0)
        : state_(state), window_(window), wheel_(wheel) {}
    RuntimeBuildKind activeTab() override { return state_.activeBuildTab; }
    std::int32_t pageSlotCount() override {
        return static_cast<std::int32_t>(state_.buildPageByTab.size());
    }
    std::int32_t pageAt(std::int32_t tab) override {
        return state_.buildPageByTab[static_cast<std::size_t>(tab)];
    }
    std::int32_t filteredCatalogCount() override {
        return static_cast<std::int32_t>(runtimeFilteredBuildCatalogIndices(state_).size());
    }
    std::int32_t visibleSuperWeaponCount() override {
        return static_cast<std::int32_t>(runtimeVisibleSuperWeapons(state_).size());
    }
    std::int32_t buttonsPerPage(RuntimeInputPixelRect sidebar) override {
        return runtimeBuildButtonsPerPage(RECT{sidebar.left, sidebar.top, sidebar.right, sidebar.bottom});
    }
    void setPageAt(std::int32_t tab, std::int32_t page) override {
        state_.buildPageByTab[static_cast<std::size_t>(tab)] = page;
    }
    std::int32_t wheelSlotCount() override {
        return static_cast<std::int32_t>(state_.buildWheelRemainderByTab.size());
    }
    std::int32_t wheelAt(std::int32_t tab) override {
        return state_.buildWheelRemainderByTab[static_cast<std::size_t>(tab)];
    }
    void setWheelAt(std::int32_t tab, std::int32_t remainder) override {
        state_.buildWheelRemainderByTab[static_cast<std::size_t>(tab)] = remainder;
    }
    void status(const std::string& text) override { state_.interactionStatus = text; }
    bool menuOpen() override { return state_.runtimeMenuOpen; }
    std::int32_t wheelDelta() override { return GET_WHEEL_DELTA_WPARAM(wheel_); }
    RuntimeInputPixelRect sidebarRect() override {
        RECT client{};
        GetClientRect(window_, &client);
        const RECT sidebar = runtimeSidebarRect(client);
        return {sidebar.left, sidebar.top, sidebar.right, sidebar.bottom};
    }
    void requestRepaint() override { state_.repaintRequested = true; }
    void redraw() override {
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
private:
    RuntimeWindowState& state_;
    HWND window_;
    WPARAM wheel_;
};
