// Private Win32 menu resource lifetime, included only after MenuState is complete.
inline MenuState::~MenuState() {
        if (hwnd != nullptr && IsWindow(hwnd)) DestroyWindow(hwnd);
        backBuffer.release(gdiReleaseTrace);
        systemFonts.reset(gdiReleaseTrace);
    }
