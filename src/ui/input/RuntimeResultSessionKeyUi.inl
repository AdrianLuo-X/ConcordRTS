// Private Win32 effect owner; construction does not sample input or time.
class RuntimeResultSessionKeyUiHost final : public RuntimeResultSessionKeyHost {
public:
    RuntimeResultSessionKeyUiHost(RuntimeWindowState& state, HWND window)
        : state_(state), window_(window) {}
    bool finished() override { return state_.skirmishMatchFinished; }
    bool exitAfterPendingVideos() override { return state_.exitAfterPendingCampaignVideos; }
    bool bannerVisible() override { return state_.resultPresentation.bannerVisible; }
    bool campaignMode() override { return state_.campaignMode; }
    RuntimeUiTimeMs nowMs() override { return runtimeUiClockNow(state_); }
    void beginScorePage(RuntimeUiTimeMs now) override { beginRuntimeResultScorePage(state_, now); }
    bool queueCampaignPostScore() override { return queueRuntimeCampaignPostScore(state_); }
    void requestRepaint() override { state_.repaintRequested = true; }
    void setExitRequested(bool requested) override { state_.runtimeExitRequested = requested; }
    void destroyWindow() override { DestroyWindow(window_); }
    std::size_t scoreRowCount() override { return runtimeMatchScoreRows(state_).size(); }
    std::int32_t scorePage() override { return state_.resultPresentation.scorePage; }
    void setScorePage(std::int32_t page) override { state_.resultPresentation.scorePage = page; }
private:
    RuntimeWindowState& state_;
    HWND window_;
};
