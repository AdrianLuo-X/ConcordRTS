// Included only in the real-window diagnostic namespace. No World access.
inline std::optional<RECT> mapMainFrameClientRect(HWND window) {
    RECT client{};if(!GetClientRect(window,&client))return std::nullopt;return client;
}
