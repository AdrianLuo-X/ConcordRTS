// Private fixed-frame fixture only. Process sent native notifications at frame
// boundaries without dispatching queued input/timers or advancing simulation.
// See Microsoft PeekMessage PM_QS_SENDMESSAGE/nonqueued-message contract.
inline bool mapMainFrameClientVisible(HWND window, bool prepare=false) {
    if (prepare) {
        MSG pending{};
        PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE | PM_QS_SENDMESSAGE);
        if (!SetWindowPos(window,HWND_TOPMOST,0,0,0,0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return false;
    }
    if (!IsWindowVisible(window) || IsIconic(window)) return false;
    RECT client{};
    if (!GetClientRect(window,&client) || IsRectEmpty(&client)) return false;
    for (const int fraction : {25,50,75}) {
        POINT sample{client.left+(client.right-client.left)*fraction/100,
            client.top+(client.bottom-client.top)/2};
        if (!ClientToScreen(window,&sample)) return false;
        DWORD owner=0;
        GetWindowThreadProcessId(WindowFromPoint(sample),&owner);
        if (owner!=GetCurrentProcessId()) return false;
    }
    return true;
}
