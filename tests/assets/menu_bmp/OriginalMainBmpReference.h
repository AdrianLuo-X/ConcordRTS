#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "assets/menu/MenuImage.h"
#include <filesystem>
#include <string>
namespace original_main_bmp {
namespace fs=std::filesystem;
using AssetImage=concord::assets::MenuImage;
std::string pathLabel(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}
bool loadBmp(const fs::path& path, AssetImage& output) {
    output = {};
    if (!regularFile(path)) return false;
    const std::wstring& wide = path.native();
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr, wide.c_str(), IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION
    ));
    if (bitmap == nullptr) return false;
    BITMAP facts{};
    GetObjectW(bitmap, sizeof(facts), &facts);
    if (facts.bmWidth <= 0 || facts.bmHeight <= 0) {
        DeleteObject(bitmap);
        return false;
    }
    output.width = facts.bmWidth;
    output.height = facts.bmHeight;
    output.bgra.resize(static_cast<std::size_t>(output.width * output.height * 4));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = output.width;
    info.bmiHeader.biHeight = -output.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(
        dc, bitmap, 0, static_cast<UINT>(output.height), output.bgra.data(),
        &info, DIB_RGB_COLORS
    );
    if (dc != nullptr) ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    if (copied != output.height) {
        output = {};
        return false;
    }
    for (std::size_t at = 3; at < output.bgra.size(); at += 4) output.bgra[at] = 255;
    output.source = pathLabel(path);
    return true;
}
}
