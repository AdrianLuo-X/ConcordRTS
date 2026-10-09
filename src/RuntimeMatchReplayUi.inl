bool handleRuntimeMatchReplayInput(RuntimeWindowState& state, HWND window,
    UINT message, WPARAM key, LPARAM data) {
    auto& replay=*state.matchReplay;
    const auto refresh=[&]() {
        invalidateRuntimeObjectProjectionCache(state);state.repaintRequested=true;
        SetWindowTextW(window,runtimeMatchReplayWindowTitle(state).c_str());
        InvalidateRect(window,nullptr,FALSE);
    };
    if(message==WM_KEYDOWN || message==WM_SYSKEYDOWN) {
        const bool repeated=(data&(static_cast<LPARAM>(1)<<30))!=0;
        if(key==VK_ESCAPE) {
            state.runtimeExitRequested=true;DestroyWindow(window);return true;
        }
        if(!repeated && key==VK_SPACE && !replay.failed && !replay.ended) replay.paused=!replay.paused;
        if(!repeated && (key==VK_ADD || key==VK_OEM_PLUS)) replay.speedIndex=std::min(5,replay.speedIndex+1);
        if(!repeated && (key==VK_SUBTRACT || key==VK_OEM_MINUS)) replay.speedIndex=std::max(0,replay.speedIndex-1);
        if(key==VK_LEFT) state.camera.worldOffsetX+=32;
        if(key==VK_RIGHT) state.camera.worldOffsetX-=32;
        if(key==VK_UP) state.camera.worldOffsetY+=24;
        if(key==VK_DOWN) state.camera.worldOffsetY-=24;
        // Keep the normal operating-system close shortcut available.
        if(message==WM_SYSKEYDOWN && key==VK_F4) return false;
        refresh();return true;
    }
    if(message==WM_RBUTTONDOWN) {
        replay.viewerDragging=true;
        replay.viewerDragPoint={static_cast<short>(LOWORD(data)),static_cast<short>(HIWORD(data))};
        SetCapture(window);return true;
    }
    if(message==WM_RBUTTONUP || message==WM_CAPTURECHANGED || message==WM_KILLFOCUS) {
        replay.viewerDragging=false;
        if(GetCapture()==window) ReleaseCapture();
        return message==WM_RBUTTONUP;
    }
    if(message==WM_MOUSEMOVE) {
        const POINT point{static_cast<short>(LOWORD(data)),static_cast<short>(HIWORD(data))};
        if(replay.viewerDragging) {
            state.camera.worldOffsetX+=point.x-replay.viewerDragPoint.x;
            state.camera.worldOffsetY+=point.y-replay.viewerDragPoint.y;
            replay.viewerDragPoint=point;refresh();
        }
        return true;
    }
    if(message==WM_LBUTTONDOWN) {
        RECT client{};GetClientRect(window,&client);
        const POINT point{static_cast<short>(LOWORD(data)),static_cast<short>(HIWORD(data))};
        const auto cell=runtimeRadarCellAtPoint(state,runtimeRadarRect(runtimeSidebarRect(client)),point);
        if(cell) {centerRuntimeCameraOnCell(state,client,*cell);refresh();}
        return true;
    }
    switch(message) {
    case WM_LBUTTONUP:case WM_LBUTTONDBLCLK:case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:case WM_CHAR:case WM_KEYUP:case WM_SYSKEYUP:
        return true;
    default:return false;
    }
}
