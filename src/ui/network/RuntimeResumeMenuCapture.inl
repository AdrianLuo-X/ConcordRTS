// Development-only capture of the actual Win32 controls, used by the resume
// menu smoke. No service connection or file dialog is opened.
bool captureRuntimeResumeMenu(HWND window, const std::filesystem::path& path) {
    RECT bounds{};GetClientRect(window, &bounds);
    const int width = bounds.right, height = bounds.bottom;
    if (width <= 0 || height <= 0) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth = width;info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;info.bmiHeader.biBitCount = 32;info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!memory || !bitmap || !pixels) { if (bitmap) DeleteObject(bitmap);if (memory) DeleteDC(memory);return false; }
    const auto old = SelectObject(memory, bitmap);
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();
    BITMAPFILEHEADER header{};header.bfType = 0x4d42;header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biSizeImage = width * height * 4;header.bfSize = header.bfOffBits + info.bmiHeader.biSizeImage;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(reinterpret_cast<const char*>(pixels), info.bmiHeader.biSizeImage);
    const bool succeeded = file.good();
    SelectObject(memory, old);DeleteObject(bitmap);DeleteDC(memory);return succeeded;
}
