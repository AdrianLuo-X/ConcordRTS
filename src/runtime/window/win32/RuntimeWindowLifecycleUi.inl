class RuntimeWindowLifecycleUiPort final : public RuntimeWindowLifecyclePort {
public:
    RuntimeWindowLifecycleUiPort(RuntimeWindowState* state, HWND window)
        : state_(state), window_(window) {}
    void resetFrameMeter() override { state_->fpsDisplayMeter.reset(); }
    void reportDeparture() override { reportRuntimeRankedDeparture(*state_); }
    void destroyWindow() override { DestroyWindow(window_); }
    void stopEva() override { stopRuntimeEvaPlayback(*state_); }
    void stopMusic() override { stopRuntimeMusic(*state_); }
    void releaseGraphics() override {
        // Retire the graphics owner while the native window still exists.
        state_->d3d11Presenter.reset();
        releaseRuntimeBackBuffer(state_->completedFrameBuffer);
        (void)state_->fpsDeviceOverlay.release();
        releaseRuntimeBackBuffer(state_->backBuffer);
        releaseRuntimeBackBuffer(state_->sidebarStaticBuffer);
        releaseRuntimeBackBuffer(state_->radarTerrainBuffer);
        state_->sidebarStaticKey.clear();
    }
    void postQuit() override { PostQuitMessage(0); }
private:
    RuntimeWindowState* state_;
    HWND window_;
};

