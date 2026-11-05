#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/network/RuntimeInternetMenuSkin.h"

bool drawRuntimeInternetMenuSkinSurface(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction,
    std::uint32_t ladderRank
);

bool drawRuntimeInternetMenuSkinButton(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    std::string_view releasedAsset,
    std::string_view pressedAsset,
    std::wstring_view text,
    bool pressed,
    bool disabled
);

// UI-thread DC borrow only, no ownership/retention or second raster algorithm.
class RuntimeInternetSkinDrawTargetWin32 final : public RuntimeInternetSkinDrawTarget {
public:
    explicit RuntimeInternetSkinDrawTargetWin32(HDC dc) noexcept : dc_(dc) {}
    RuntimeInternetSkinDrawingCapability capability() const noexcept override {
        return RuntimeInternetSkinDrawingCapability::Available;
    }
    bool drawSurface(const RuntimeInputPixelRect& r,const RuntimeInternetMenuSkin& skin,
        RuntimeInternetMenuSkinPage page,RuntimeInternetMenuSkinFaction faction,
        std::uint32_t rank) override {
        const RECT native{r.left,r.top,r.right,r.bottom};
        return ::drawRuntimeInternetMenuSkinSurface(dc_,native,skin,page,faction,rank);
    }
    bool drawButton(const RuntimeInputPixelRect& r,const RuntimeInternetMenuSkin& skin,
        std::string_view released,std::string_view pressed,std::wstring_view text,
        bool down,bool disabled) override {
        const RECT native{r.left,r.top,r.right,r.bottom};
        return ::drawRuntimeInternetMenuSkinButton(dc_,native,skin,released,pressed,text,down,disabled);
    }
private:
    HDC dc_=nullptr;
};
inline bool drawRuntimeInternetMenuSkinSurfaceWin32(HDC dc,const RECT& r,
    const RuntimeInternetMenuSkin& skin,RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction,std::uint32_t rank) {
    RuntimeInternetSkinDrawTargetWin32 target(dc);
    return ::drawRuntimeInternetMenuSkinSurface(target,{r.left,r.top,r.right,r.bottom},skin,page,faction,rank);
}
inline bool drawRuntimeInternetMenuSkinButtonWin32(HDC dc,const RECT& r,
    const RuntimeInternetMenuSkin& skin,std::string_view released,std::string_view pressed,
    std::wstring_view text,bool down,bool disabled) {
    RuntimeInternetSkinDrawTargetWin32 target(dc);
    return ::drawRuntimeInternetMenuSkinButton(target,{r.left,r.top,r.right,r.bottom},skin,released,pressed,text,down,disabled);
}
int runRuntimeInternetSkinDrawingContract(const RuntimeConfig&,const std::string&);
