// Bounded private native diagnostics, unused by ordinary menu owners.
inline void traceGpuMenuInputWin32(MenuState* state,HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(state->menuInputTrace&&state->menuInputTrace->size()<16384
        &&((message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||message==WM_CAPTURECHANGED
            ||message==WM_KEYDOWN||message==WM_SETFOCUS||message==WM_KILLFOCUS)) {
        POINT physical{};const bool got=GetCursorPos(&physical)!=FALSE;
        std::ostringstream row;row<<GetTickCount64()<<" msg="<<message<<" w="<<static_cast<std::uint64_t>(wParam)
            <<" x="<<static_cast<std::int16_t>(LOWORD(lParam))<<" y="<<static_cast<std::int16_t>(HIWORD(lParam))
            <<" cursor="<<got<<','<<physical.x<<','<<physical.y<<" ownForeground="<<(GetForegroundWindow()==window)
            <<" captured="<<(GetCapture()==window)<<" leftDown="<<((GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0)
            <<" speed="<<state->selection.gameSpeed<<" dragging="<<static_cast<int>(state->draggingSlider)
            <<" dropdown="<<static_cast<int>(state->openDropdown)<<" hover="<<state->dropdownHover
            <<" phase="<<static_cast<int>(state->phase);
        state->menuInputTrace->push_back(row.str());
    }
}
