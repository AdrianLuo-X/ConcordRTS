// Private Win32 menu resource lifetime, included only after MenuState is complete.
inline MenuState::~MenuState() {
        if (hwnd != nullptr && IsWindow(hwnd)) DestroyWindow(hwnd);
        backBuffer.release(gdiReleaseTrace);
        if (titleFont != nullptr) {
            const BOOL disposed = DeleteObject(titleFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(3, disposed != FALSE);
        }
        if (captionFont != nullptr) {
            const BOOL disposed = DeleteObject(captionFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(4, disposed != FALSE);
        }
        if (normalFont != nullptr) {
            const BOOL disposed = DeleteObject(normalFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(5, disposed != FALSE);
        }
        if (smallFont != nullptr) {
            const BOOL disposed = DeleteObject(smallFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(6, disposed != FALSE);
        }
    }
